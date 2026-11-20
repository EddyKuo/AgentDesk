// agent-host's self-update (design doc 7.4 OQ-09; terminals keep running): the pieces that move
// a live terminal to another owner.
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "agentdesk/host/session.hpp"
#include "agentdesk/ipc/pipe.hpp"
#include "agentdesk/platform/win/command_line.hpp"
#include "agentdesk/platform/win/conpty_api.hpp"
#include "agentdesk/platform/win/error.hpp"
#include "agentdesk/platform/win/job.hpp"
#include "agentdesk/platform/win/pty_process.hpp"
#include "agentdesk/ui/host_client.hpp"

using namespace std::chrono_literals;
namespace win = agentdesk::platform::win;
namespace ipc = agentdesk::ipc;
namespace ui = agentdesk::ui;

namespace {

win::ConPtyApi bundled() {
    return win::load_conpty(std::filesystem::path(AGENTDESK_BUNDLED_CONPTY_DIR));
}

template <typename Pred>
bool wait_until(Pred pred, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (pred())
            return true;
        std::this_thread::sleep_for(20ms);
    }
    return pred();
}

HANDLE dup(HANDLE h) {
    HANDLE out = nullptr;
    EXPECT_TRUE(::DuplicateHandle(::GetCurrentProcess(), h, ::GetCurrentProcess(), &out, 0, FALSE,
                                  DUPLICATE_SAME_ACCESS));
    return out;
}

struct Collected {
    std::mutex mutex;
    std::string text;
    void add(std::span<const char> b) {
        std::lock_guard lock(mutex);
        text.append(b.data(), b.size());
    }
    std::string get() {
        std::lock_guard lock(mutex);
        return text;
    }
};

}  // namespace

// The bundled ConPTY can be handed over: its HPCON is rebuilt from duplicated handles
// (ConptyPackPseudoConsole), the first owner lets go without ending anything, and the program,
// its output and the kill-on-close job carry on with the second owner.
TEST(Handover, PseudoConsoleMovesToAnotherOwner) {
    const auto api = bundled();
    ASSERT_TRUE(api.bundled) << "fetch the bundled ConPTY first (fetch_conpty.exe)";
    ASSERT_NE(api.pack, nullptr);
    Collected first;
    win::PtySpawnSpec spec;
    spec.executable = AGENTDESK_VT_PROBE;
    spec.args = {L"counter"};
    spec.cwd = std::filesystem::temp_directory_path();
    auto a = win::PtyProcess::spawn(api, spec, [&](std::span<const char> b) { first.add(b); });
    ASSERT_TRUE(wait_until([&] { return first.get().find("COUNT 3") != std::string::npos; }, 10s));

    ASSERT_TRUE(a->pause_reader(2s));
    std::string why;
    const auto h = a->handover_handles(&why);
    ASSERT_TRUE(h.has_value()) << why;
    win::PtyHandles copy = *h;
    copy.signal = dup(h->signal);
    copy.reference = dup(h->reference);
    copy.conhost = dup(h->conhost);
    copy.input_write = dup(h->input_write);
    copy.output_read = dup(h->output_read);
    copy.process = dup(h->process);
    copy.job = dup(h->job);
    const DWORD pid = a->pid();
    a->release_after_handover();
    a.reset();  // the first owner is gone: its job handle closed, its HPCON freed

    Collected second;
    auto b = win::PtyProcess::adopt(api, copy, [&](std::span<const char> bytes) { second.add(bytes); });
    EXPECT_EQ(b->pid(), pid);
    EXPECT_FALSE(b->wait_exit(0ms).has_value()) << "the program must still run";
    b->resume_reader();
    // Still counting, through the same pipe, nothing skipped at the seam.
    ASSERT_TRUE(wait_until([&] { return second.get().find("COUNT 12") != std::string::npos; }, 15s))
        << second.get();
    const std::string all = first.get() + second.get();
    for (int n = 1; n <= 12; ++n)
        EXPECT_NE(all.find("COUNT " + std::to_string(n) + "\r"), std::string::npos) << n;
    b->resize({100, 30});  // the signal pipe works from the new owner
    // Closing the new owner ends the program: the job still kills on close.
    win::UniqueHandle process(::OpenProcess(SYNCHRONIZE, FALSE, pid));
    b->close();
    ASSERT_TRUE(process);
    EXPECT_EQ(::WaitForSingleObject(process.get(), 10000), WAIT_OBJECT_0);
}

// Adopting checks the handles themselves, never a PID: a process outside the job is refused.
TEST(Handover, AdoptRefusesHandlesThatDoNotBelongTogether) {
    const auto api = bundled();
    ASSERT_TRUE(api.bundled);
    Collected out;
    win::PtySpawnSpec spec;
    spec.executable = AGENTDESK_VT_PROBE;
    spec.args = {L"counter"};
    spec.cwd = std::filesystem::temp_directory_path();
    auto a = win::PtyProcess::spawn(api, spec, [&](std::span<const char> b) { out.add(b); });
    ASSERT_TRUE(a->pause_reader(2s));
    std::string why;
    const auto h = a->handover_handles(&why);
    ASSERT_TRUE(h.has_value()) << why;
    win::PtyHandles wrong = *h;
    wrong.signal = dup(h->signal);
    wrong.reference = dup(h->reference);
    wrong.conhost = dup(h->conhost);
    wrong.input_write = dup(h->input_write);
    wrong.output_read = dup(h->output_read);
    wrong.process = dup(::GetCurrentProcess());  // this test, not the program in the job
    wrong.pid = ::GetCurrentProcessId();
    wrong.job = dup(h->job);
    EXPECT_THROW(win::PtyProcess::adopt(api, wrong, [](std::span<const char>) {}), std::runtime_error);
    // The original owner is untouched and keeps going.
    a->resume_reader();
    const auto before = out.get().size();
    EXPECT_TRUE(wait_until([&] { return out.get().size() > before; }, 5s));
    a->close();
}

namespace {

agentdesk::host::SessionConfig counter_config() {
    agentdesk::host::SessionConfig c;
    c.api = bundled();
    c.spec.executable = AGENTDESK_VT_PROBE;
    c.spec.args = {L"counter"};
    c.spec.cwd = std::filesystem::temp_directory_path();
    c.spec.size = {60, 8};
    c.engine.rows = 8;
    c.engine.cols = 60;
    return c;
}

int last_count(const agentdesk::host::Session& s) {
    const auto snap = s.snapshot();
    int best = 0;
    for (int r = 0; r < snap->rows; ++r) {
        const auto t = snap->row_text(r);
        if (t.rfind("COUNT ", 0) == 0)
            best = std::max(best, std::atoi(t.c_str() + 6));
    }
    return best;
}

}  // namespace

// A whole session moves: screen, history (with its line ids), seq and the running program.
TEST(Handover, SessionMovesWithScreenAndHistory) {
    using agentdesk::host::Session;
    auto a = Session::start(counter_config());
    ASSERT_TRUE(wait_until([&] { return a->history_tail(100).size() > 3; }, 20s)) << "history fills";
    std::string why;
    ASSERT_TRUE(a->can_hand_over(&why)) << why;
    auto frozen = a->freeze(5s, &why);
    ASSERT_TRUE(frozen.has_value()) << why;
    ASSERT_TRUE(frozen->handles.has_value());
    const auto screen_before = a->snapshot();
    const auto history_before = a->history_tail(100);
    const int count_before = last_count(*a);
    win::PtyHandles copy = *frozen->handles;
    for (HANDLE* h : {&copy.signal, &copy.reference, &copy.conhost, &copy.input_write, &copy.output_read,
                      &copy.process, &copy.job})
        *h = dup(*h);
    a->release_after_handover();
    a.reset();

    auto b = Session::adopt(counter_config(), {}, frozen->state, copy);
    // Before starting: exactly the screen and history the old owner had.
    EXPECT_EQ(b->snapshot()->cells, screen_before->cells);
    EXPECT_EQ(b->snapshot()->seq, screen_before->seq);
    b->start_adopted();
    const auto history_after = b->history_tail(100);
    ASSERT_GE(history_after.size(), history_before.size());
    for (std::size_t i = 0; i < history_before.size(); ++i) {
        EXPECT_EQ(history_after[i].line_id, history_before[i].line_id);
        EXPECT_EQ(history_after[i].cells, history_before[i].cells);
    }
    EXPECT_EQ(b->info().process_state, agentdesk::host::ProcessState::kRunning);
    EXPECT_TRUE(wait_until([&] { return last_count(*b) >= count_before + 5; }, 10s)) << "keeps counting";
    // Counting is contiguous across the seam: every number appears, in history or on screen.
    std::string all;
    for (const auto& l : b->history(0, 10000)) {
        for (const auto& c : l.cells)
            if (c.chars[0] != 0)
                all.push_back(static_cast<char>(c.chars[0]));
        all.push_back('\n');
    }
    for (int r = 0; r < b->snapshot()->rows; ++r)
        all += b->snapshot()->row_text(r) + "\n";
    for (int n = count_before; n < count_before + 5; ++n)
        EXPECT_NE(all.find("COUNT " + std::to_string(n) + "\n"), std::string::npos) << n;
    b->force_stop();
    EXPECT_TRUE(b->wait_exit(10s).has_value());
}

// A failed handover leaves the session as it was.
TEST(Handover, ResumeAfterFreezeCarriesOn) {
    using agentdesk::host::Session;
    auto a = Session::start(counter_config());
    ASSERT_TRUE(wait_until([&] { return last_count(*a) >= 2; }, 10s));
    std::string why;
    auto frozen = a->freeze(5s, &why);
    ASSERT_TRUE(frozen.has_value()) << why;
    const int at = last_count(*a);
    std::this_thread::sleep_for(600ms);
    EXPECT_EQ(last_count(*a), at) << "frozen: nothing read";
    a->resume();
    EXPECT_TRUE(wait_until([&] { return last_count(*a) >= at + 4; }, 10s));
    a->force_stop();
    EXPECT_TRUE(a->wait_exit(10s).has_value());
}

// ---------------------------------------------------------------- whole hosts

namespace {

using Json = nlohmann::json;

// An isolated agent-host (own pipe and data folder) with a counting vt_probe profile.
class HostUpdate : public ::testing::Test {
protected:
    void SetUp() override {
        static std::atomic<int> counter{0};
        const auto tag = std::to_wstring(::GetCurrentProcessId()) + L"-" + std::to_wstring(++counter);
        pipe_ = L"\\\\.\\pipe\\AgentDeskUpdateTest-" + tag;
        data_dir_ = std::filesystem::temp_directory_path() / (L"agentdesk-updatetest-" + tag);
        std::filesystem::remove_all(data_dir_);
        std::filesystem::create_directories(data_dir_);
        const Json profiles = Json::array({{{"id", "counter"},
                                            {"name", "probe counter"},
                                            {"executable", win::to_utf8(AGENTDESK_VT_PROBE)},
                                            {"args", {"counter"}}}});
        std::ofstream(data_dir_ / L"profiles.json") << Json{{"schema_version", 1}, {"profiles", profiles}}.dump(2);
        ::SetEnvironmentVariableW(L"AGENTDESK_DATA_DIR", data_dir_.c_str());
        std::wstring cmd = win::build_command_line(AGENTDESK_HOST_EXE, {L"--pipe", pipe_});
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        ASSERT_TRUE(::CreateProcessW(AGENTDESK_HOST_EXE, cmd.data(), nullptr, nullptr, FALSE, DETACHED_PROCESS,
                                     nullptr, nullptr, &si, &pi));
        first_host_.reset(pi.hProcess);
        ::CloseHandle(pi.hThread);
        // Every host of this test (the new one inherits the job: the job allows no breakaway)
        // and so every terminal ends if the test process dies.
        job_.assign(pi.hProcess);
        ASSERT_NE(connect(), nullptr) << "host did not come up";
    }
    void TearDown() override {
        if (auto c = connect())
            c->call("host.shutdown", {{"stop_sessions", true}}, 5s);
        ::Sleep(500);
        ::SetEnvironmentVariableW(L"AGENTDESK_DATA_DIR", nullptr);
        std::error_code ec;
        std::filesystem::remove_all(data_dir_, ec);
    }
    std::unique_ptr<ipc::PipeClient> connect(std::chrono::milliseconds t = 10s) {
        auto c = ipc::PipeClient::connect(pipe_, t);
        if (c && !c->call("hello", {{"version", 1}}, 5s))
            return nullptr;
        return c;
    }
    Json call(ipc::PipeClient& c, const std::string& method, Json params,
              std::chrono::milliseconds t = 10s) {
        const auto r = c.call(method, std::move(params), t);
        if (!r)
            return {{"error", "no response"}};
        if (r->error)
            return {{"error", r->error_message}};
        return r->result.value_or(Json::object());
    }
    std::vector<std::pair<std::uint64_t, std::string>> history(ipc::PipeClient& c, const std::string& id) {
        std::vector<std::pair<std::uint64_t, std::string>> out;
        const auto h = call(c, "history.read", {{"session", id}, {"from_line_id", 0}, {"max_lines", 200}});
        for (const auto& l : h.value("lines", Json::array()))
            out.emplace_back(l.value("line_id", std::uint64_t{0}), l.value("text", ""));
        return out;
    }

    std::wstring pipe_;
    std::filesystem::path data_dir_;
    win::UniqueHandle first_host_;
    win::KillOnCloseJob job_;
};

int count_in(const std::string& line) {
    return line.rfind("COUNT ", 0) == 0 ? std::atoi(line.c_str() + 6) : 0;
}

int last_count(const ui::RemoteSession& s) {
    const auto snap = s.snapshot();
    int best = 0;
    for (int r = 0; snap && r < snap->rows; ++r)
        best = std::max(best, count_in(snap->row_text(r)));
    return best;
}

}  // namespace

// Done-criterion of design doc 7.4 OQ-09: a terminal keeps running through agent-host's update,
// with its screen and history, and the UI's client reconnects to the same session.
TEST_F(HostUpdate, TerminalsKeepRunningThroughAnUpdate) {
    auto c = connect();
    ASSERT_TRUE(c);
    const Json hello = call(*c, "hello", {{"version", 1}});
    const std::string first_instance = hello.value("host_instance_id", "");
    const Json created = call(*c, "session.create",
                              {{"profile_id", "counter"},
                               {"cwd", win::to_utf8(std::filesystem::temp_directory_path().native())},
                               {"rows", 8},
                               {"cols", 60}});
    const std::string id = created.value("session_id", "");
    ASSERT_FALSE(id.empty()) << created.dump();

    // The UI's own client, as agent-ui uses it.
    ui::HostClientOptions o;
    o.host_exe = AGENTDESK_HOST_EXE;
    o.pipe_name = pipe_;
    ui::HostClient ui_client(o);
    std::shared_ptr<ui::RemoteSession> view;
    ASSERT_TRUE(wait_until(
        [&] {
            for (const auto& s : ui_client.sessions())
                if (s->id() == id && last_count(*s) >= 1)
                    view = s;
            return view != nullptr;
        },
        15s));
    ASSERT_TRUE(wait_until([&] { return history(*c, id).size() >= 5; }, 20s)) << "history fills";
    const auto pid = call(*c, "session.get", {{"session", id}}).value("pid", 0u);
    ASSERT_NE(pid, 0u);
    win::UniqueHandle program(::OpenProcess(SYNCHRONIZE, FALSE, pid));
    ASSERT_TRUE(program);

    const auto history_before = history(*c, id);
    // The state timeline is this host's own (decision: it does not move with an update).
    const Json timeline_before = call(*c, "session.timeline", {{"session", id}});
    ASSERT_EQ(timeline_before.value("sessions", Json::array()).size(), 1u) << timeline_before.dump();
    EXPECT_FALSE(timeline_before["sessions"][0].value("adopted", true));
    const std::int64_t update_at = timeline_before.value("now_ms", std::int64_t{0});
    const Json updated = call(*c, "host.update", {{"exe", win::to_utf8(AGENTDESK_HOST_EXE)}}, 60s);
    ASSERT_FALSE(updated.contains("error")) << updated.dump();
    EXPECT_EQ(updated.value("now", "").rfind("agent-host ", 0), 0u) << updated.dump();
    const std::string second_instance = updated.value("host_instance_id", "");
    EXPECT_NE(second_instance, first_instance);

    // The old host is gone, the program is not.
    EXPECT_EQ(::WaitForSingleObject(first_host_.get(), 15000), WAIT_OBJECT_0);
    EXPECT_EQ(::WaitForSingleObject(program.get(), 0), static_cast<DWORD>(WAIT_TIMEOUT));

    std::unique_ptr<ipc::PipeClient> c2;
    ASSERT_TRUE(wait_until([&] { return (c2 = connect(1s)) != nullptr; }, 15s));
    const Json hello2 = call(*c2, "hello", {{"version", 1}});
    EXPECT_EQ(hello2.value("host_instance_id", ""), second_instance);
    const auto succeeds = hello2.value("succeeds", Json::array());
    EXPECT_NE(std::find(succeeds.begin(), succeeds.end(), first_instance), succeeds.end());
    const Json got = call(*c2, "session.get", {{"session", id}});
    EXPECT_EQ(got.value("process_state", ""), "Running") << got.dump();
    EXPECT_EQ(got.value("pid", 0u), pid);
    // The new host's timeline starts at the takeover, with the state the session had then.
    const Json timeline_after = call(*c2, "session.timeline", {{"session", id}});
    ASSERT_EQ(timeline_after.value("sessions", Json::array()).size(), 1u) << timeline_after.dump();
    const Json& t = timeline_after["sessions"][0];
    EXPECT_TRUE(t.value("adopted", false));
    ASSERT_EQ(t["marks"].size(), 1u) << t.dump();
    EXPECT_GE(t["marks"][0][0].get<std::int64_t>(), update_at);
    EXPECT_EQ(t["marks"][0][2], "Running");

    // History came along, line ids and all, and keeps growing.
    const auto history_after = history(*c2, id);
    ASSERT_GE(history_after.size(), history_before.size());
    for (std::size_t i = 0; i < history_before.size(); ++i)
        EXPECT_EQ(history_after[i], history_before[i]) << i;

    // The UI's client reconnected to the same session (not Lost) and sees it counting on.
    const int before = last_count(*view);
    EXPECT_TRUE(wait_until(
        [&] { return ui_client.connected() && ui_client.host_instance_id() == second_instance; }, 15s));
    EXPECT_TRUE(wait_until([&] { return last_count(*view) >= before + 5; }, 15s))
        << "screen after: " << view->snapshot()->row_text(0);
    EXPECT_FALSE(view->view().lost);
    EXPECT_EQ(view->view().process_state, "Running");

    // Every number, in order, across the seam: history then screen.
    std::string all;
    for (const auto& [line_id, text] : history(*c2, id))
        all += text + "\n";
    for (int n = 1; n < before + 3 && n < 150; ++n)
        if (all.find("COUNT " + std::to_string(n) + "\n") == std::string::npos) {
            const auto snap = view->snapshot();
            std::string screen;
            for (int r = 0; r < snap->rows; ++r)
                screen += snap->row_text(r) + "\n";
            EXPECT_NE(screen.find("COUNT " + std::to_string(n) + "\n"), std::string::npos) << "missing " << n;
        }

    // sessions.json says Running (never Lost) for the session the new host owns.
    std::ifstream in(data_dir_ / L"sessions.json");
    const Json saved = Json::parse(in, nullptr, false);
    ASSERT_TRUE(saved.is_object());
    EXPECT_EQ(saved["sessions"][0].value("process_state", ""), "Running");

    // The job still kills on close: ending the new host ends the program.
    const auto new_pid = hello2.value("host_pid", 0u);
    win::UniqueHandle second_host(::OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, new_pid));
    ASSERT_TRUE(second_host);
    ::TerminateProcess(second_host.get(), 1);
    EXPECT_EQ(::WaitForSingleObject(program.get(), 10000), WAIT_OBJECT_0);
}

// A new host that cannot take over (here: not agent-host at all) leaves everything with the old
// one, running as before.
TEST_F(HostUpdate, FailedTakeoverKeepsEverything) {
    auto c = connect();
    ASSERT_TRUE(c);
    const std::string first_instance = call(*c, "hello", {{"version", 1}}).value("host_instance_id", "");
    const Json created = call(*c, "session.create",
                              {{"profile_id", "counter"},
                               {"cwd", win::to_utf8(std::filesystem::temp_directory_path().native())},
                               {"rows", 8},
                               {"cols", 60}});
    const std::string id = created.value("session_id", "");
    ASSERT_FALSE(id.empty());
    ASSERT_TRUE(wait_until([&] { return history(*c, id).size() >= 2; }, 20s));
    const Json updated = call(*c, "host.update", {{"exe", win::to_utf8(AGENTDESK_VT_PROBE)}}, 60s);
    ASSERT_TRUE(updated.contains("error")) << updated.dump();
    EXPECT_EQ(call(*c, "hello", {{"version", 1}}).value("host_instance_id", ""), first_instance);
    const auto n = history(*c, id).size();
    EXPECT_TRUE(wait_until([&] { return history(*c, id).size() >= n + 3; }, 10s)) << "still counting";
    EXPECT_EQ(call(*c, "session.get", {{"session", id}}).value("process_state", ""), "Running");
    // And it takes input and requests again (not stuck in "updating").
    EXPECT_FALSE(call(*c, "session.resize", {{"session", id}, {"rows", 9}, {"cols", 61}}).contains("error"));
}

// The system's ConPTY has no ConptyPackPseudoConsole: it cannot hand over, and says so.
TEST(Handover, InboxConPtyRefuses) {
    const auto api = win::load_system_conpty();
    win::PtySpawnSpec spec;
    spec.executable = AGENTDESK_VT_PROBE;
    spec.args = {L"counter"};
    spec.cwd = std::filesystem::temp_directory_path();
    auto a = win::PtyProcess::spawn(api, spec, [](std::span<const char>) {});
    std::string why;
    EXPECT_FALSE(a->handover_handles(&why).has_value());
    EXPECT_FALSE(why.empty());
    a->close();
}
