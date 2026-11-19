// agent-host.exe + agentctl.exe end to end (Sprint 2, S2-5).
#include <windows.h>
// psapi.h needs windows.h first.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <set>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <psapi.h>
#include <tlhelp32.h>
// aclapi/sddl need windows.h first.
#include <aclapi.h>
#include <sddl.h>

#include "agentdesk/host/git_tools.hpp"
#include "agentdesk/ipc/host_launcher.hpp"
#include "agentdesk/ipc/pipe.hpp"
#include "agentdesk/platform/win/command_line.hpp"
#include "agentdesk/platform/win/error.hpp"
#include "agentdesk/platform/win/job.hpp"
#include "agentdesk/platform/win/unique_handle.hpp"
#include "agentdesk/protocol/messages.hpp"

using namespace std::chrono_literals;
namespace fs = std::filesystem;
namespace win = agentdesk::platform::win;
namespace ipc = agentdesk::ipc;
namespace protocol = agentdesk::protocol;
using Json = nlohmann::json;

namespace {

struct ProcResult {
    DWORD exit_code = 0;
    std::string out;
};

// Runs a console program with captured stdout (stderr discarded) and returns its exit code.
ProcResult run_capture(const fs::path& exe, const std::vector<std::wstring>& args, const fs::path& cwd = {},
                       const std::string* stdin_text = nullptr) {
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE rd = nullptr;
    HANDLE wr = nullptr;
    ::CreatePipe(&rd, &wr, &sa, 0);
    ::SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    win::UniqueHandle read_end(rd);
    win::UniqueHandle write_end(wr);
    HANDLE nul = ::CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
    win::UniqueHandle nul_h(nul);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = wr;
    si.hStdError = nul;
    si.hStdInput = nullptr;
    win::UniqueHandle in_read, in_write;
    if (stdin_text != nullptr) {
        HANDLE ir = nullptr;
        HANDLE iw = nullptr;
        ::CreatePipe(&ir, &iw, &sa, 0);
        ::SetHandleInformation(iw, HANDLE_FLAG_INHERIT, 0);
        in_read.reset(ir);
        in_write.reset(iw);
        si.hStdInput = ir;
    }
    std::wstring cmd = win::build_command_line(exe.native(), args);
    PROCESS_INFORMATION pi{};
    if (!::CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                          cwd.empty() ? nullptr : cwd.c_str(), &si, &pi)) {
        return {9999, {}};
    }
    write_end.reset();
    if (stdin_text != nullptr) {
        in_read.reset();
        DWORD written = 0;
        ::WriteFile(in_write.get(), stdin_text->data(), static_cast<DWORD>(stdin_text->size()), &written, nullptr);
        in_write.reset();  // EOF
    }
    win::UniqueHandle process(pi.hProcess);
    win::UniqueHandle thread(pi.hThread);
    ProcResult r;
    char buf[4096];
    DWORD n = 0;
    while (::ReadFile(read_end.get(), buf, sizeof(buf), &n, nullptr) && n > 0)
        r.out.append(buf, n);
    ::WaitForSingleObject(process.get(), 30000);
    ::GetExitCodeProcess(process.get(), &r.exit_code);
    return r;
}

std::wstring w(const std::string& s) {
    return win::to_wide(s);
}

class HostFixture : public ::testing::Test {
protected:
    void SetUp() override {
        static std::atomic<int> counter{0};
        const auto tag = std::to_wstring(::GetCurrentProcessId()) + L"-" + std::to_wstring(++counter);
        pipe_ = L"\\\\.\\pipe\\AgentDeskHostTest-" + tag;
        data_dir_ = fs::temp_directory_path() / (L"agentdesk-hosttest-" + tag);
        fs::remove_all(data_dir_);
        fs::create_directories(data_dir_);
        write_profiles();
        start_host();
    }
    void TearDown() override {
        if (auto c = connect())
            c->call("host.shutdown", {{"stop_sessions", true}}, 5s);
        if (host_)
            ::WaitForSingleObject(host_.get(), 10000);
        fs::remove_all(data_dir_);
    }
    void write_profiles() {
        wchar_t sys[MAX_PATH]{};
        const UINT n = ::GetSystemDirectoryW(sys, MAX_PATH);
        const auto cmd = fs::path(std::wstring(sys, n)) / L"cmd.exe";
        Json profiles = Json::array({
            {{"id", "echo"},
             {"name", "probe echo"},
             {"executable", win::to_utf8(AGENTDESK_VT_PROBE)},
             {"args", {"echo"}}},
            {{"id", "mouse"},
             {"name", "probe mouse"},
             {"executable", win::to_utf8(AGENTDESK_VT_PROBE)},
             {"args", {"mouse"}}},
            {{"id", "flood"},
             {"name", "probe flood"},
             {"executable", win::to_utf8(AGENTDESK_VT_PROBE)},
             {"args", {"flood", "3", "1048576"}}},
            {{"id", "report"},
             {"name", "probe report"},
             {"executable", win::to_utf8(AGENTDESK_VT_PROBE)},
             {"args", {"report"}}},
            {{"id", "cmd"},
             {"name", "cmd"},
             {"executable", win::to_utf8(cmd.native())},
             {"args", Json::array()}},
            {{"id", "powershell"},
             {"name", "Windows PowerShell"},
             {"executable", win::to_utf8((fs::path(std::wstring(sys, n)) / L"WindowsPowerShell" / L"v1.0" /
                                          L"powershell.exe")
                                             .native())},
             {"args", {"-NoLogo", "-NoProfile"}}},
            {{"id", "size"},
             {"name", "probe size"},
             {"executable", win::to_utf8(AGENTDESK_VT_PROBE)},
             {"args", {"size"}}},
            {{"id", "crash"},
             {"name", "probe crash"},
             {"executable", win::to_utf8(AGENTDESK_VT_PROBE)},
             {"args", {"crash"}}},
            {{"id", "broken"},
             {"name", "broken"},
             {"executable", "C:\\nowhere\\missing.exe"},
             {"args", Json::array()}},
        });
        std::ofstream(data_dir_ / L"profiles.json")
            << Json{{"schema_version", 1}, {"profiles", profiles}}.dump(2);
    }
    void start_host() {
        ::SetEnvironmentVariableW(L"AGENTDESK_DATA_DIR", data_dir_.c_str());
        std::wstring cmd = win::build_command_line(AGENTDESK_HOST_EXE, {L"--pipe", pipe_});
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        ASSERT_TRUE(::CreateProcessW(AGENTDESK_HOST_EXE, cmd.data(), nullptr, nullptr, FALSE,
                                     DETACHED_PROCESS, nullptr, nullptr, &si, &pi));
        host_.reset(pi.hProcess);
        ::CloseHandle(pi.hThread);
        // If this test process crashes, closing the job kills the host instead of leaking it.
        host_job_.assign(pi.hProcess);
        host_pid_ = pi.dwProcessId;
        auto c = connect();
        ASSERT_NE(c, nullptr) << "host did not come up";
    }
    std::unique_ptr<ipc::PipeClient> connect() { return ipc::PipeClient::connect(pipe_, 10s); }
    ProcResult ctl(std::vector<std::wstring> args) {
        args.insert(args.begin(), {L"--pipe", pipe_, L"--no-start"});
        return run_capture(AGENTDESK_CTL_EXE, args, fs::temp_directory_path());
    }
    std::string create(const std::string& profile, const std::string& name) {
        const auto r = ctl({L"run", L"--profile", w(profile), L"--name", w(name), L"--cwd",
                            fs::temp_directory_path().native()});
        EXPECT_EQ(r.exit_code, 0u) << r.out;
        std::string id = r.out;
        while (!id.empty() && (id.back() == '\n' || id.back() == '\r'))
            id.pop_back();
        return id;
    }
    std::string read(const std::string& session) { return ctl({L"read", w(session), L"--lines", L"50"}).out; }
    template <typename Pred>
    static bool wait_until(Pred p, std::chrono::milliseconds t) {
        const auto d = std::chrono::steady_clock::now() + t;
        while (std::chrono::steady_clock::now() < d) {
            if (p())
                return true;
            std::this_thread::sleep_for(50ms);
        }
        return p();
    }

    std::wstring pipe_;
    fs::path data_dir_;
    win::UniqueHandle host_;
    win::KillOnCloseJob host_job_;
    DWORD host_pid_ = 0;
};

}  // namespace

// S2-M2 / QA-03 (host side): input to one session never reaches the other.
TEST_F(HostFixture, TwoSessionsAreIndependent) {
    const auto a = create("echo", "alpha");
    const auto b = create("echo", "beta");
    std::this_thread::sleep_for(300ms);
    EXPECT_EQ(ctl({L"send", L"alpha", L"--text", L"only-a", L"--enter"}).exit_code, 0u);
    ASSERT_TRUE(
        wait_until([&] { return read(a).find("PROBE-ECHO:6f6e6c792d610d") != std::string::npos; }, 10s));
    // b's probe also echoes the host's terminal query replies; only a's text must be absent.
    EXPECT_EQ(read(b).find("6f6e6c792d61"), std::string::npos);
}

// QA-13: --json output is pure JSON; send does not press Enter unless asked; exit codes 0,2..7.
TEST_F(HostFixture, CliContractAndExitCodes) {
    const auto id = create("echo", "contract");
    std::this_thread::sleep_for(300ms);
    const auto listed = ctl({L"list", L"--json"});
    ASSERT_EQ(listed.exit_code, 0u);
    const auto doc = Json::parse(listed.out, nullptr, false);
    ASSERT_FALSE(doc.is_discarded()) << listed.out;
    EXPECT_EQ(doc["sessions"].size(), 1u);

    EXPECT_EQ(ctl({L"send", w(id), L"--text", L"no-enter"}).exit_code, 0u);
    std::this_thread::sleep_for(800ms);
    EXPECT_EQ(read(id).find("6e6f2d656e746572"), std::string::npos) << "Enter was sent without --enter";
    EXPECT_EQ(ctl({L"send", w(id), L"--enter"}).exit_code, 0u);
    EXPECT_TRUE(
        wait_until([&] { return read(id).find("PROBE-ECHO:6e6f2d656e7465720d") != std::string::npos; }, 10s));

    EXPECT_EQ(ctl({L"stop", w(id)}).exit_code, 2u);  // stop without --interrupt/--force: bad args
    EXPECT_EQ(run_capture(AGENTDESK_CTL_EXE,
                          {L"--pipe", L"\\\\.\\pipe\\AgentDeskNobodyHome", L"--no-start", L"list"})
                  .exit_code,
              3u);
    EXPECT_EQ(ctl({L"read", L"nosuch"}).exit_code, 4u);
    create("echo", "twin");
    create("echo", "twin");
    EXPECT_EQ(ctl({L"read", L"twin"}).exit_code, 4u);  // ambiguous name
    EXPECT_EQ(ctl({L"shutdown"}).exit_code, 5u);       // active sessions
    EXPECT_EQ(ctl({L"run", L"--profile", L"broken", L"--cwd", fs::temp_directory_path().native()}).exit_code,
              6u);
    EXPECT_EQ(ctl({L"list", L"--timeout-ms", L"0"}).exit_code, 7u);
}

// QA-19: report-state through the CLI; stale seq ignored; invalid input rejected.
TEST_F(HostFixture, ReportStateViaCli) {
    const auto id = create("echo", "reporter");
    EXPECT_EQ(ctl({L"report-state", w(id), L"--source", L"codex-hook", L"--state", L"blocked", L"--kind",
                   L"permission", L"--message", L"approve rm?", L"--seq", L"200"})
                  .exit_code,
              0u);
    EXPECT_EQ(ctl({L"report-state", w(id), L"--source", L"codex-hook", L"--state", L"idle", L"--seq", L"150"})
                  .exit_code,
              0u);
    std::this_thread::sleep_for(300ms);
    const auto got = Json::parse(ctl({L"list", L"--json"}).out);
    EXPECT_EQ(got["sessions"][0]["agent_state"], "Blocked");
    EXPECT_EQ(got["sessions"][0]["agent_message"], "approve rm?");
    EXPECT_EQ(
        ctl({L"report-state", w(id), L"--source", L"x", L"--state", L"sleeping", L"--seq", L"1"}).exit_code,
        2u);
    EXPECT_EQ(
        ctl({L"report-state", w(id), L"--source", L"agentdesk:fake", L"--state", L"idle", L"--seq", L"1"})
            .exit_code,
        2u);
}

// Environment advertised to sessions (design doc 3.6).
TEST_F(HostFixture, SessionsGetAgentDeskEnvironment) {
    const auto id = create("cmd", "envcheck");
    std::this_thread::sleep_for(500ms);
    ctl({L"send", w(id), L"--text", L"echo [%AGENTDESK_SESSION_ID%]", L"--enter"});
    EXPECT_TRUE(wait_until([&] { return read(id).find("[" + id + "]") != std::string::npos; }, 10s))
        << read(id);
}

// QA-14 (host side): one controller; others read-only until an explicit take-over.
TEST_F(HostFixture, SingleInputController) {
    const auto id = create("echo", "shared");
    auto a = connect();
    auto b = connect();
    ASSERT_TRUE(a && b);
    const auto ra = a->call("session.attach", {{"session", id}, {"want_control", true}});
    ASSERT_TRUE(ra && ra->result);
    EXPECT_TRUE(ra->result->value("is_controller", false));
    const auto rb = b->call("session.attach", {{"session", id}, {"want_control", true}});
    ASSERT_TRUE(rb && rb->result);
    EXPECT_FALSE(rb->result->value("is_controller", true));
    const auto denied = b->call("session.input", {{"session", id}, {"kind", "text"}, {"text", "x"}});
    ASSERT_TRUE(denied);
    EXPECT_EQ(denied->error, protocol::ErrorCode::kNotController);
    EXPECT_EQ(b->call("session.resize", {{"session", id}, {"rows", 10}, {"cols", 40}})->error,
              protocol::ErrorCode::kNotController);
    b->call("session.attach", {{"session", id}, {"take_control", true}});
    EXPECT_FALSE(
        b->call("session.input", {{"session", id}, {"kind", "text"}, {"text", "x"}})->error.has_value());
    EXPECT_EQ(a->call("session.input", {{"session", id}, {"kind", "text"}, {"text", "x"}})->error,
              protocol::ErrorCode::kNotController);
    b->close();  // holder disconnects: control released
    EXPECT_TRUE(wait_until(
        [&] { return !a->call("session.input", {{"session", id}, {"kind", "text"}, {"text", "y"}})->error; },
        5s));
}

// QA-06: attach while output is flowing; deltas continue exactly from the snapshot seq.
TEST_F(HostFixture, AttachSnapshotThenContiguousDeltas) {
    const auto id = create("flood", "flowing");
    std::this_thread::sleep_for(300ms);  // attach mid-stream
    auto c = connect();
    ASSERT_TRUE(c);
    std::mutex mu;
    std::vector<std::pair<std::uint64_t, std::uint64_t>> deltas;  // base_seq, seq
    std::uint64_t snapshot_seq = 0;
    c->set_event_handler([&](protocol::Event e) {
        std::lock_guard lock(mu);
        if (e.event == "screen.delta")
            deltas.emplace_back(e.body["base_seq"].get<std::uint64_t>(), e.body["seq"].get<std::uint64_t>());
        if (e.event == "screen.snapshot" && snapshot_seq == 0)
            snapshot_seq = e.body["meta"]["seq"].get<std::uint64_t>();
    });
    const auto r = c->call("session.attach", {{"session", id}});
    ASSERT_TRUE(r && r->result);
    const auto attached_seq = r->result->at("snapshot_meta").at("seq").get<std::uint64_t>();
    std::this_thread::sleep_for(1500ms);
    c->close();  // stop the reader thread before the captured locals go out of scope
    std::lock_guard lock(mu);
    ASSERT_FALSE(deltas.empty());
    EXPECT_EQ(snapshot_seq, attached_seq);
    std::uint64_t expect = attached_seq;
    for (const auto& [base, seq] : deltas) {
        ASSERT_EQ(base, expect) << "gap between deltas";
        expect = seq;
    }
}

// QA-10: host crash -> new host marks the old session Lost and does not adopt it.
TEST_F(HostFixture, HostCrashMarksSessionsLost) {
    const auto id = create("echo", "orphan");
    const auto before = Json::parse(ctl({L"list", L"--json"}).out);
    const DWORD child_pid = before["sessions"][0]["pid"].get<DWORD>();
    ::TerminateProcess(host_.get(), 99);  // simulated crash
    ::WaitForSingleObject(host_.get(), 10000);
    // KILL_ON_JOB_CLOSE: the child dies with the host's job handle.
    win::UniqueHandle child(::OpenProcess(SYNCHRONIZE, FALSE, child_pid));
    if (child)
        EXPECT_EQ(::WaitForSingleObject(child.get(), 10000), WAIT_OBJECT_0);
    start_host();
    const auto after = Json::parse(ctl({L"list", L"--json"}).out);
    ASSERT_EQ(after["sessions"].size(), 1u);
    EXPECT_EQ(after["sessions"][0]["id"], id);
    EXPECT_EQ(after["sessions"][0]["process_state"], "Lost");
    EXPECT_NE(after["host_instance_id"], before["host_instance_id"]);
    EXPECT_EQ(ctl({L"send", w(id), L"--text", L"x"}).exit_code, 4u);  // Lost sessions take no input
}

// QA-20: a client started inside a kill-on-close job (like a terminal window) auto-starts the
// host. With breakaway allowed the host survives the job; without it, the client warns first.
namespace {

struct JobRun {
    bool host_alive_after_job_closed = false;
    std::string stderr_text;
};

JobRun auto_start_host_inside_job(bool allow_breakaway, const std::wstring& pipe) {
    JobRun out;
    win::UniqueHandle job(::CreateJobObjectW(nullptr, nullptr));
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION li{};
    li.BasicLimitInformation.LimitFlags =
        JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | (allow_breakaway ? JOB_OBJECT_LIMIT_BREAKAWAY_OK : 0);
    ::SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation, &li, sizeof(li));

    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE rd = nullptr;
    HANDLE wr = nullptr;
    ::CreatePipe(&rd, &wr, &sa, 0);
    ::SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    win::UniqueHandle read_end(rd);
    win::UniqueHandle write_end(wr);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdError = wr;
    si.hStdOutput = wr;
    std::wstring cmd = win::build_command_line(AGENTDESK_UI_DIR_CTL_EXE, {L"--pipe", pipe, L"list"});
    PROCESS_INFORMATION pi{};
    if (!::CreateProcessW(AGENTDESK_UI_DIR_CTL_EXE, cmd.data(), nullptr, nullptr, TRUE,
                          CREATE_SUSPENDED | CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        return out;
    }
    ::AssignProcessToJobObject(job.get(), pi.hProcess);
    ::ResumeThread(pi.hThread);
    write_end.reset();
    char buf[4096];
    DWORD n = 0;
    while (::ReadFile(read_end.get(), buf, sizeof(buf), &n, nullptr) && n > 0)
        out.stderr_text.append(buf, n);
    ::WaitForSingleObject(pi.hProcess, 30000);
    ::CloseHandle(pi.hThread);
    ::CloseHandle(pi.hProcess);
    job.reset();  // "close the terminal": kills everything still in the job
    std::this_thread::sleep_for(500ms);
    auto probe = ipc::PipeClient::connect(pipe, 2s);
    out.host_alive_after_job_closed = probe != nullptr;
    if (probe)
        probe->call("host.shutdown", {{"stop_sessions", true}}, 5s);
    return out;
}

}  // namespace

TEST(HostLaunch, HostBreaksAwayFromLauncherJob) {
    const auto pipe = L"\\\\.\\pipe\\AgentDeskJobTest-ok-" + std::to_wstring(::GetCurrentProcessId());
    const auto r = auto_start_host_inside_job(true, pipe);
    EXPECT_TRUE(r.host_alive_after_job_closed) << r.stderr_text;
    EXPECT_EQ(r.stderr_text.find("warning"), std::string::npos) << r.stderr_text;
}

TEST(HostLaunch, WarnsWhenHostCannotLeaveKillOnCloseJob) {
    const auto pipe = L"\\\\.\\pipe\\AgentDeskJobTest-trap-" + std::to_wstring(::GetCurrentProcessId());
    const auto r = auto_start_host_inside_job(false, pipe);
    EXPECT_NE(r.stderr_text.find("will end agent-host when it closes"), std::string::npos) << r.stderr_text;
    EXPECT_FALSE(r.host_alive_after_job_closed);
}

// UI-less operation (M1 exit criterion): read, write and stop without any attached UI.
TEST_F(HostFixture, StopWorksWithoutUi) {
    const auto id = create("echo", "headless");
    std::this_thread::sleep_for(300ms);
    EXPECT_EQ(ctl({L"stop", w(id), L"--force"}).exit_code, 0u);
    EXPECT_TRUE(wait_until(
        [&] {
            const auto j = Json::parse(ctl({L"list", L"--json"}).out, nullptr, false);
            return !j.is_discarded() && j["sessions"][0]["process_state"] == "Exited";
        },
        10s));
}

// ======================================================================== Sprint 4 (M3)

// QA-04: 100 rapid resizes; host engine, published snapshot and the child's own view agree.
TEST_F(HostFixture, RapidResizesEndConsistent) {
    const auto id = create("size", "resizer");
    auto c = connect();
    ASSERT_TRUE(c);
    ASSERT_TRUE(c->call("session.attach", {{"session", id}, {"want_control", true}}));
    int rows = 0;
    int cols = 0;
    for (int i = 0; i < 100; ++i) {
        rows = 10 + (i * 7) % 40;
        cols = 40 + (i * 13) % 120;
        ASSERT_TRUE(c->call("session.resize", {{"session", id}, {"rows", rows}, {"cols", cols}}));
    }
    const std::string expected = "SIZE " + std::to_string(cols) + " " + std::to_string(rows);
    ASSERT_TRUE(wait_until([&] { return read(id).find(expected) != std::string::npos; }, 10s))
        << "child does not see " << expected << ":\n"
        << read(id);
    const auto got = c->call("session.get", {{"session", id}});
    ASSERT_TRUE(got && got->result);
    EXPECT_EQ(got->result->value("cols", 0), cols);
    EXPECT_EQ(got->result->value("rows", 0), rows);
}

// Fault case: a crashing child ends as Exited with its real exit code, not Done.
TEST_F(HostFixture, CrashingChildReportsExitCode) {
    const auto id = create("crash", "crasher");
    ASSERT_TRUE(wait_until(
        [&] {
            const auto j = Json::parse(ctl({L"list", L"--json"}).out, nullptr, false);
            return !j.is_discarded() && j["sessions"][0]["process_state"] == "Exited";
        },
        10s));
    const auto j = Json::parse(ctl({L"list", L"--json"}).out);
    EXPECT_EQ(j["sessions"][0]["exit_code"].get<std::uint32_t>(), 0xC0000005u);
    EXPECT_NE(j["sessions"][0]["agent_state"], "Done");
}

// QA-18 (IPC): a client that never reads cannot slow the host or starve other clients, and the
// host's memory stays bounded.
TEST_F(HostFixture, SlowClientDoesNotStallHost) {
    const auto id = create("flood", "flooder");  // 3 s at 1 MiB/s
    HANDLE raw =
        ::CreateFileW(pipe_.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    ASSERT_NE(raw, INVALID_HANDLE_VALUE);
    win::UniqueHandle stuck(raw);
    const std::string attach = protocol::encode_frame(
        protocol::serialize(protocol::Request{"stuck-1", "session.attach", {{"session", id}}}));
    DWORD written = 0;
    ASSERT_TRUE(
        ::WriteFile(stuck.get(), attach.data(), static_cast<DWORD>(attach.size()), &written, nullptr));
    auto healthy = connect();
    ASSERT_TRUE(healthy);
    std::atomic<int> deltas{0};
    healthy->set_event_handler([&](protocol::Event e) {
        if (e.event == "screen.delta")
            ++deltas;
    });
    ASSERT_TRUE(healthy->call("session.attach", {{"session", id}}));
    const auto start = std::chrono::steady_clock::now();
    ASSERT_TRUE(wait_until([&] { return read(id).find("FLOOD-DONE") != std::string::npos; }, 30s));
    const auto elapsed = std::chrono::steady_clock::now() - start;
    EXPECT_LT(elapsed, 10s) << "host consumption slowed down";
    EXPECT_GT(deltas.load(), 10) << "healthy client starved";
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    pmc.cb = sizeof(pmc);
    ASSERT_TRUE(
        ::GetProcessMemoryInfo(host_.get(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc)));
    EXPECT_LT(pmc.PrivateUsage, 500ull * 1024 * 1024) << "host private bytes " << pmc.PrivateUsage;
    healthy->close();
}

// Corrupt settings without backups: the host still starts, keeps the damaged files and uses
// defaults.
TEST_F(HostFixture, CorruptSettingsDoNotPreventStartup) {
    if (auto c = connect())
        c->call("host.shutdown", {{"stop_sessions", true}});
    ::WaitForSingleObject(host_.get(), 10000);
    for (const wchar_t* f : {L"profiles.json", L"sessions.json"}) {
        std::ofstream(data_dir_ / f, std::ios::trunc) << "{ this is not json";
        fs::remove(fs::path(data_dir_ / f).concat(L".bak"));
    }
    start_host();
    auto c = connect();
    ASSERT_TRUE(c);
    const auto profiles = c->call("profile.list", Json::object());
    ASSERT_TRUE(profiles && profiles->result);
    EXPECT_FALSE(profiles->result->at("profiles").empty()) << "defaults were not detected";
    int kept = 0;
    for (const auto& entry : fs::directory_iterator(data_dir_)) {
        if (entry.path().filename().wstring().find(L".corrupt-") != std::wstring::npos)
            ++kept;
    }
    EXPECT_EQ(kept, 2);
}

// One damaged entry in profiles.json or sessions.json is skipped; the host starts with the rest.
TEST_F(HostFixture, DamagedEntriesAreSkippedNotFatal) {
    if (auto c = connect())
        c->call("host.shutdown", {{"stop_sessions", true}});
    ::WaitForSingleObject(host_.get(), 10000);
    Json profiles = Json::array({
        {{"id", 5}, {"executable", "x"}},  // id is not a string
        {{"name", "no id"}},               // id and executable missing
        {{"id", "echo"},
         {"name", "probe echo"},
         {"executable", win::to_utf8(AGENTDESK_VT_PROBE)},
         {"args", {"echo"}}},
    });
    std::ofstream(data_dir_ / L"profiles.json", std::ios::trunc)
        << Json{{"schema_version", 1}, {"wsl_detected", true}, {"profiles", profiles}}.dump(2);
    Json sessions = Json::array({
        {{"id", 42}},                   // id is not a string
        {{"id", "ok-1"}, {"name", 7}},  // name is not a string
        {{"id", "ok-2"}, {"name", "kept"}, {"cwd", "C:\\"}, {"process_state", "Exited"}},
    });
    std::ofstream(data_dir_ / L"sessions.json", std::ios::trunc)
        << Json{{"schema_version", 1}, {"sessions", sessions}}.dump(2);
    fs::remove(fs::path(data_dir_ / L"profiles.json").concat(L".bak"));
    fs::remove(fs::path(data_dir_ / L"sessions.json").concat(L".bak"));
    start_host();
    auto c = connect();
    ASSERT_TRUE(c);
    const auto list = c->call("profile.list", Json::object());
    ASSERT_TRUE(list && list->result);
    std::set<std::string> ids;
    for (const auto& p : list->result->at("profiles"))
        ids.insert(p.value("id", ""));
    EXPECT_EQ(ids, std::set<std::string>{"echo"});
    const auto got = c->call("session.get", {{"session", "ok-2"}});
    ASSERT_TRUE(got && got->result) << "the good session record was not kept";
    EXPECT_EQ(got->result->value("name", ""), "kept");
    EXPECT_TRUE(c->call("session.get", {{"session", "ok-1"}})->error.has_value());
}

// Fault case: sessions.json cannot be written (e.g. disk or permission problem); sessions work.
TEST_F(HostFixture, PersistFailureKeepsHostRunning) {
    fs::create_directories(data_dir_ / L"sessions.json.tmp");  // makes the temp write fail
    const auto id = create("echo", "nodisk");
    EXPECT_FALSE(id.empty());
    std::this_thread::sleep_for(300ms);
    EXPECT_EQ(ctl({L"send", w(id), L"--text", L"still-alive", L"--enter"}).exit_code, 0u);
    EXPECT_TRUE(wait_until(
        [&] { return read(id).find("PROBE-ECHO:7374696c6c2d616c6976650d") != std::string::npos; }, 10s));
}

// Design doc 1.5 "UI 續接": 4 sessions of 120x40, attach until every snapshot is complete.
TEST_F(HostFixture, ReattachFourSessionsUnderTwoSeconds) {
    std::vector<std::string> ids;
    for (int i = 0; i < 4; ++i)
        ids.push_back(create("cmd", "re" + std::to_string(i)));
    std::this_thread::sleep_for(800ms);
    std::vector<double> ms;
    for (int round = 0; round < 20; ++round) {
        const auto t0 = std::chrono::steady_clock::now();
        auto c = connect();
        ASSERT_TRUE(c);
        const auto t_connected = std::chrono::steady_clock::now();
        c->call("hello", {{"version", 1}});
        const auto t_hello = std::chrono::steady_clock::now();
        std::mutex mu;
        std::condition_variable cv;
        std::map<std::string, int> complete;
        c->set_event_handler([&](protocol::Event e) {
            if (e.event != "screen.snapshot")
                return;
            std::lock_guard lock(mu);
            if (e.body["chunk_index"].get<int>() + 1 == e.body["chunk_count"].get<int>())
                ++complete[e.body["session_id"]];
            cv.notify_all();
        });
        for (const auto& id : ids)
            ASSERT_TRUE(c->call("session.attach", {{"session", id}, {"want_control", true}}));
        std::unique_lock lock(mu);
        ASSERT_TRUE(cv.wait_for(lock, 5s, [&] { return complete.size() == ids.size(); }));
        ms.push_back(
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
        if (round == 0) {
            std::cout << "[reattach] connect="
                      << std::chrono::duration<double, std::milli>(t_connected - t0).count() << "ms hello="
                      << std::chrono::duration<double, std::milli>(t_hello - t_connected).count() << "ms"
                      << std::endl;
        }
        lock.unlock();
        c->close();
    }
    std::sort(ms.begin(), ms.end());
    const double p95 = ms[static_cast<std::size_t>(0.95 * static_cast<double>(ms.size() - 1))];
    RecordProperty("reattach_p50_ms", std::to_string(ms[ms.size() / 2]));
    RecordProperty("reattach_p95_ms", std::to_string(p95));
    std::cout << "[reattach] p50=" << ms[ms.size() / 2] << "ms p95=" << p95 << "ms max=" << ms.back()
              << "ms\n";
    EXPECT_LT(p95, 2000.0);
}

// Stability (design doc 1.5): exited sessions beyond the retention limit release their threads,
// engines and history; their metadata stays listed.
TEST_F(HostFixture, ExitedSessionsAreReleasedBeyondRetention) {
    auto thread_count = [&] {
        DWORD n = 0;
        HANDLE snap = ::CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        THREADENTRY32 te{sizeof(te)};
        for (BOOL ok = ::Thread32First(snap, &te); ok; ok = ::Thread32Next(snap, &te)) {
            if (te.th32OwnerProcessID == ::GetProcessId(host_.get()))
                ++n;
        }
        ::CloseHandle(snap);
        return n;
    };
    const DWORD baseline = thread_count();
    std::vector<std::string> ids;
    for (int i = 0; i < 12; ++i)
        ids.push_back(create("echo", "churn" + std::to_string(i)));
    for (const auto& id : ids)
        ctl({L"stop", w(id), L"--force"});
    ASSERT_TRUE(wait_until(
        [&] {
            const auto j = Json::parse(ctl({L"list", L"--json"}).out, nullptr, false);
            if (j.is_discarded())
                return false;
            return std::all_of(j["sessions"].begin(), j["sessions"].end(),
                               [](const Json& s) { return s["process_state"] == "Exited"; });
        },
        20s));
    const auto j = Json::parse(ctl({L"list", L"--json"}).out);  // list prunes
    int released = 0;
    for (const auto& s : j["sessions"])
        released += s.value("history_only", false) ? 1 : 0;
    EXPECT_EQ(j["sessions"].size(), 12u);
    EXPECT_EQ(released, 4);
    EXPECT_LE(thread_count(), baseline + 8 * 2 + 4) << "baseline " << baseline;
}

// QA-17: the pipe's DACL grants access to SYSTEM and the current user only.
TEST_F(HostFixture, PipeDaclAllowsOnlySystemAndCurrentUser) {
    PACL dacl = nullptr;
    PSECURITY_DESCRIPTOR sd = nullptr;
    // Read the DACL from a client handle on the real pipe instance.
    HANDLE h =
        ::CreateFileW(pipe_.c_str(), GENERIC_READ | READ_CONTROL, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    ASSERT_NE(h, INVALID_HANDLE_VALUE);
    win::UniqueHandle pipe_handle(h);
    ASSERT_EQ(::GetSecurityInfo(pipe_handle.get(), SE_KERNEL_OBJECT, DACL_SECURITY_INFORMATION, nullptr,
                                nullptr, &dacl, nullptr, &sd),
              static_cast<DWORD>(ERROR_SUCCESS));
    ASSERT_NE(dacl, nullptr);
    std::vector<std::wstring> sids;
    for (DWORD i = 0; i < dacl->AceCount; ++i) {
        void* ace = nullptr;
        ASSERT_TRUE(::GetAce(dacl, i, &ace));
        auto* allowed = static_cast<ACCESS_ALLOWED_ACE*>(ace);
        ASSERT_EQ(allowed->Header.AceType, ACCESS_ALLOWED_ACE_TYPE);
        LPWSTR text = nullptr;
        ASSERT_TRUE(::ConvertSidToStringSidW(reinterpret_cast<PSID>(&allowed->SidStart), &text));
        sids.emplace_back(text);
        ::LocalFree(text);
    }
    ::LocalFree(sd);
    std::sort(sids.begin(), sids.end());
    std::vector<std::wstring> expected = {L"S-1-5-18", ipc::current_user_sid()};
    std::sort(expected.begin(), expected.end());
    EXPECT_EQ(sids, expected);  // no Everyone (S-1-1-0), no anonymous, no other users
}

// Design doc 3.9: diagnostics never contain what the user typed.
TEST_F(HostFixture, HostLogNeverContainsInput) {
    const auto id = create("echo", "secret");
    ctl({L"send", w(id), L"--text", L"TOP-SECRET-TOKEN-1234", L"--enter"});
    std::this_thread::sleep_for(500ms);
    std::ifstream in(data_dir_ / L"host.log", std::ios::binary);
    const std::string log((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    EXPECT_FALSE(log.empty());
    EXPECT_EQ(log.find("TOP-SECRET"), std::string::npos);
}

// Non-ASCII output survives a resize: ConPTY repaints the screen after a resize, and the repaint
// must still reach the engine as valid UTF-8 (cmd's banner is localized, e.g. Traditional Chinese).
TEST_F(HostFixture, LocalizedOutputSurvivesResize) {
    const auto id = create("cmd", "cjk");
    auto c = connect();
    ASSERT_TRUE(c);
    ASSERT_TRUE(c->call("session.attach", {{"session", id}, {"want_control", true}}));
    ASSERT_TRUE(wait_until([&] { return read(id).find("Microsoft") != std::string::npos; }, 10s));
    const std::string before = read(id);
    ASSERT_TRUE(c->call("session.resize", {{"session", id}, {"rows", 21}, {"cols", 67}}));
    std::this_thread::sleep_for(1500ms);
    const std::string after = read(id);
    std::cout << "[before]\n" << before << "\n[after]\n" << after << std::endl;
    EXPECT_EQ(before.find("\xEF\xBF\xBD"), std::string::npos) << "U+FFFD before resize";
    EXPECT_EQ(after.find("\xEF\xBF\xBD"), std::string::npos) << "U+FFFD after resize";
    c->close();
}

// The shared UI document: stored by the host, survives a host restart, and other watching
// windows get the change (spaces, tabs and splits shared between screens).
TEST_F(HostFixture, UiStateIsSharedAndPersisted) {
    auto a = connect();
    auto b = connect();
    ASSERT_TRUE(a && b);
    std::atomic<int> events{0};
    Json last;
    std::mutex m;
    b->set_event_handler([&](agentdesk::protocol::Event e) {
        if (e.event == "ui.state") {
            std::lock_guard lock(m);
            last = e.body;
            ++events;
        }
    });
    const auto g = b->call("ui.state.get", {{"watch", true}});
    ASSERT_TRUE(g && g->result);
    EXPECT_TRUE(g->result->at("doc").empty());
    const Json doc = {{"version", 1}, {"spaces", Json::array({{{"id", "s1"}, {"name", "api"}}})}};
    const auto set = a->call("ui.state.set", {{"doc", doc}});
    ASSERT_TRUE(set && set->result);
    ASSERT_TRUE(wait_until([&] { return events.load() == 1; }, 5s));
    {
        std::lock_guard lock(m);
        EXPECT_EQ(last["doc"], doc);
    }
    EXPECT_TRUE(a->call("ui.state.set", {{"doc", "not an object"}})->error.has_value());
    a->close();
    b->close();
    ::TerminateProcess(host_.get(), 99);
    ::WaitForSingleObject(host_.get(), 5000);
    start_host();
    auto c = connect();
    ASSERT_TRUE(c);
    const auto again = c->call("ui.state.get", Json::object());
    ASSERT_TRUE(again && again->result);
    EXPECT_EQ(again->result->at("doc"), doc);
    c->close();
}

// What runs in the terminal now: a shell that starts a program reports that program.
TEST_F(HostFixture, ForegroundProgramFollowsChild) {
    const auto id = create("cmd", "prog");
    auto program = [&] {
        const auto j = Json::parse(ctl({L"list", L"--json"}).out, nullptr, false);
        return j.is_discarded() ? std::string() : j["sessions"][0].value("program", "");
    };
    ASSERT_TRUE(wait_until([&] { return program() == "cmd"; }, 10s)) << program();
    ctl({L"send", w(id), L"--text", w("\"" + win::to_utf8(std::wstring(AGENTDESK_VT_PROBE)) + "\" echo"),
         L"--enter"});
    EXPECT_TRUE(wait_until([&] { return program() == "vt_probe"; }, 10s)) << program();
}

// live_cwd follows cd in cmd (real current folder) and PowerShell (the prompt hook).
TEST_F(HostFixture, LiveCwdFollowsCd) {
    const auto target = data_dir_ / L"sub dir";
    fs::create_directories(target);
    const std::string want = win::to_utf8(target.native());
    for (const char* profile : {"cmd", "powershell"}) {
        const auto id = create(profile, profile);
        auto live = [&] {
            const auto j = Json::parse(ctl({L"list", L"--json"}).out, nullptr, false);
            if (j.is_discarded())
                return std::string();
            for (const auto& s : j["sessions"])
                if (s.value("id", "") == id)
                    return s.value("live_cwd", "");
            return std::string();
        };
        ASSERT_TRUE(wait_until([&] { return !live().empty(); }, 15s)) << profile;
        ctl({L"send", w(id), L"--text", w("cd \"" + want + "\""), L"--enter"});
        EXPECT_TRUE(wait_until([&] { return _stricmp(live().c_str(), want.c_str()) == 0; }, 15s))
            << profile << ": " << live();
        ctl({L"stop", w(id), L"--force"});
    }
}

// worktree.add: a new branch checked out in its own folder; bad input is refused.
TEST_F(HostFixture, WorktreeAddCreatesCheckout) {
    const auto repo = data_dir_ / L"repo";
    std::filesystem::create_directories(repo);
    auto git = [&](std::vector<std::wstring> args) {
        args.insert(args.begin(), {L"git", L"-C", repo.native()});
        return agentdesk::host::run_tool(args, repo, 30s);
    };
    ASSERT_TRUE(git({L"init", L"-q"}).started) << "git not on PATH";
    git({L"-c", L"user.email=t@example.invalid", L"-c", L"user.name=t", L"commit", L"-q", L"--allow-empty",
         L"-m", L"init"});
    auto c = connect();
    ASSERT_TRUE(c);
    const auto target = data_dir_ / L"wt" / L"feature-x";
    const auto r = c->call("worktree.add",
                           {{"repo", win::to_utf8(repo.native())},
                            {"branch", "feature/x"},
                            {"path", win::to_utf8(target.native())}},
                           70s);
    ASSERT_TRUE(r && r->result) << (r && r->error ? r->error_message : "no response");
    EXPECT_TRUE(std::filesystem::exists(target / L".git"));
    EXPECT_TRUE(c->call("worktree.add", {{"repo", win::to_utf8(repo.native())}, {"branch", "-x"}})
                    ->error.has_value());
    // A branch that exists is checked out (asked with show-ref, never read from git's wording).
    git({L"branch", L"existing"});
    const auto again = data_dir_ / L"wt" / L"existing";
    const auto r2 = c->call("worktree.add",
                            {{"repo", win::to_utf8(repo.native())},
                             {"branch", "existing"},
                             {"path", win::to_utf8(again.native())}},
                            70s);
    ASSERT_TRUE(r2 && r2->result) << (r2 && r2->error ? r2->error_message : "no response");
    EXPECT_TRUE(std::filesystem::exists(again / L".git"));
    // A failure reports git's stderr, not its stdout.
    const auto bad = c->call("worktree.add", {{"repo", win::to_utf8(repo.native())},
                                              {"branch", "existing"},
                                              {"path", win::to_utf8((data_dir_ / L"wt" / L"dup").native())}},
                             70s);
    ASSERT_TRUE(bad && bad->error);
    EXPECT_NE(bad->error_message.find("existing"), std::string::npos) << bad->error_message;
    c->close();
}

// run_tool keeps stdout (parsed) apart from stderr (messages) and has no 64 KiB cap.
TEST(GitTools, RunToolSeparatesStreamsAndKeepsLargeOutput) {
    wchar_t sys[MAX_PATH]{};
    ::GetSystemDirectoryW(sys, MAX_PATH);
    const auto cmd = std::filesystem::path(sys) / L"cmd.exe";
    const auto r = agentdesk::host::run_tool({cmd.native(), L"/d", L"/c", L"echo out& echo err 1>&2"},
                                             std::filesystem::temp_directory_path(), 30s);
    ASSERT_TRUE(r.started);
    EXPECT_EQ(r.output, "out\r\n");
    EXPECT_EQ(r.error, "err \r\n");
    EXPECT_EQ(r.message(), "err");
    // About 200 KiB on stdout arrives whole.
    const auto big = agentdesk::host::run_tool(
        {cmd.native(), L"/d", L"/c", L"for /l %i in (1,1,4000) do @echo 0123456789012345678901234567890123456789012345"},
        std::filesystem::temp_directory_path(), 60s);
    ASSERT_TRUE(big.started);
    EXPECT_EQ(big.output.size(), 4000u * 48u);
}

TEST(GitTools, WorktreeSlugKeepsLettersDigitsAndDots) {
    using agentdesk::host::worktree_slug;
    EXPECT_EQ(worktree_slug("feature/x"), "feature-x");
    EXPECT_EQ(worktree_slug("--fix: a b--"), "fix-a-b");
    EXPECT_EQ(worktree_slug("中文/分支"), "中文-分支");
    EXPECT_EQ(worktree_slug("v1.2_rc-1"), "v1.2_rc-1");
    EXPECT_EQ(worktree_slug("///").rfind("branch-", 0), 0u);
    EXPECT_EQ(worktree_slug("///").size(), 15u);
    EXPECT_NE(worktree_slug("///"), worktree_slug("??"));
}

// Agent collaboration (helper agents): a sub-agent records its parent, gets its first task
// once it is ready, reports back into the parent's input, and the parent can wait for it.
TEST_F(HostFixture, SubAgentTaskReportAndWait) {
    const auto parent = create("cmd", "lead");
    auto r =
        ctl({L"run", L"--profile", L"cmd", L"--name", L"helper", L"--cwd", fs::temp_directory_path().native(),
             L"--parent", w(parent), L"--task", L"TASK-alpha", L"--label", L"role=review"});
    ASSERT_EQ(r.exit_code, 0u) << r.out;
    std::string child = r.out;
    while (!child.empty() && (child.back() == '\n' || child.back() == '\r'))
        child.pop_back();
    const auto list = Json::parse(ctl({L"list", L"--json"}).out);
    bool found = false;
    for (const auto& s : list["sessions"]) {
        if (s["id"] == child) {
            found = true;
            EXPECT_EQ(s["parent"], parent);
            EXPECT_EQ(s["labels"]["role"], "review");
        }
    }
    ASSERT_TRUE(found);
    // The helper says it is ready: the task arrives.
    ctl({L"report-state", w(child), L"--source", L"test", L"--state", L"idle", L"--seq", L"1"});
    EXPECT_TRUE(wait_until([&] { return read(child).find("TASK-alpha") != std::string::npos; }, 5s))
        << read(child);
    // Report back: tagged text in the parent's input.
    r = ctl({L"report", L"RESULT-42", L"--from", w(child)});
    ASSERT_EQ(r.exit_code, 0u) << r.out;
    EXPECT_TRUE(wait_until(
        [&] {
            return read(parent).find("[from helper") != std::string::npos &&
                   read(parent).find("RESULT-42") != std::string::npos;
        },
        5s))
        << read(parent);
    // Not delivered while the target works.
    ctl({L"report-state", w(parent), L"--source", L"test", L"--state", L"working", L"--seq", L"1"});
    ctl({L"report", L"LATER-7", L"--from", w(child)});
    std::this_thread::sleep_for(600ms);
    EXPECT_EQ(read(parent).find("LATER-7"), std::string::npos);
    // wait --until idle returns once the parent finishes.
    std::thread finisher([&] {
        std::this_thread::sleep_for(700ms);
        ctl({L"report-state", w(parent), L"--source", L"test", L"--state", L"idle", L"--seq", L"2"});
    });
    const auto waited = ctl({L"wait", w(parent), L"--until", L"idle", L"--timeout-ms", L"10000"});
    finisher.join();
    EXPECT_EQ(waited.exit_code, 0u) << waited.out;
    EXPECT_TRUE(wait_until([&] { return read(parent).find("LATER-7") != std::string::npos; }, 5s));
    // Timeout: exit code 7.
    EXPECT_EQ(ctl({L"wait", w(parent), L"--until", L"blocked", L"--timeout-ms", L"300"}).exit_code, 7u);
    // No parent: refused with a clear error.
    EXPECT_EQ(ctl({L"report", L"x", L"--from", w(parent)}).exit_code, 4u);
}

// Remote machines: the protocol over a command's stdin/stdout (ssh in real use). Locally the
// command is agentctl bridge itself, so the whole path is exercised without an ssh server.
TEST_F(HostFixture, ProtocolOverBridgeCommand) {
    const auto id = create("cmd", "remote-shell");
    auto conn = agentdesk::ipc::connect_over_command({AGENTDESK_CTL_EXE, L"--pipe", pipe_, L"bridge"}, 10s);
    ASSERT_TRUE(conn.client) << conn.error;
    EXPECT_FALSE(conn.host_instance_id.empty());
    const auto list = conn.client->call("session.list", Json::object());
    ASSERT_TRUE(list && list->result);
    ASSERT_EQ(list->result->at("sessions").size(), 1u);
    // Events flow too: attach gets a snapshot.
    std::atomic<int> snapshots{0};
    conn.client->set_event_handler([&](agentdesk::protocol::Event e) {
        if (e.event == "screen.snapshot")
            ++snapshots;
    });
    ASSERT_TRUE(conn.client->call("session.attach", {{"session", id}}));
    EXPECT_TRUE(wait_until([&] { return snapshots.load() > 0; }, 5s));
    conn.client->close();
}

TEST(RemoteTransport, FailingCommandExplainsWhy) {
    wchar_t sys[MAX_PATH]{};
    ::GetSystemDirectoryW(sys, MAX_PATH);
    const auto cmd = (fs::path(sys) / L"cmd.exe").native();
    auto conn =
        agentdesk::ipc::connect_over_command({cmd, L"/c", L"echo Permission denied (publickey) 1>&2"}, 3s);
    EXPECT_FALSE(conn.client);
    EXPECT_NE(conn.error.find("Permission denied"), std::string::npos) << conn.error;
    const auto argv = agentdesk::ipc::ssh_bridge_argv("dev@build-box", "");
    EXPECT_EQ(argv.front(), L"ssh");
    EXPECT_EQ(argv[argv.size() - 3], L"dev@build-box");
    EXPECT_EQ(argv.back(), L"bridge");
}

// Mouse reporting end to end: the program turns on click reporting (DECSET 1000 + SGR 1006), the
// client sends an abstract mouse event, the host encodes it and the program reads the bytes.
TEST_F(HostFixture, MouseEventsReachAProgramThatAskedForThem) {
    const auto id = create("mouse", "mouse");
    auto c = connect();
    ASSERT_TRUE(c);
    ASSERT_TRUE(wait_until(
        [&] {
            const auto r = c->call("session.list", Json::object());
            for (const auto& sjson : r->result->value("sessions", Json::array()))
                if (sjson.value("id", "") == id && sjson.value("process_state", "") == "Running")
                    return true;
            return false;
        },
        10s));
    std::this_thread::sleep_for(500ms);  // the DECSET has been parsed
    const auto r = c->call("session.input", {{"session", id}, {"kind", "mouse"}, {"row", 1}, {"col", 2},
                                             {"button", 1}, {"action", "press"}});
    ASSERT_TRUE(r && !r->error);
    // ESC [ < 0 ; 3 ; 2 M  (1-based col 3, row 2)
    EXPECT_TRUE(wait_until([&] { return read(id).find("1b5b3c303b333b324d") != std::string::npos; }, 10s))
        << read(id);
    const auto bad = c->call("session.input", {{"session", id}, {"kind", "mouse"}, {"button", 9}});
    EXPECT_TRUE(bad && bad->error);
}

// Files / Changes for a client on another machine: fs.list, fs.read and an allow-listed git.run.
TEST_F(HostFixture, FilesAndChangesRequestsForRemoteClients) {
    auto c = connect();
    ASSERT_TRUE(c);
    const fs::path repo = data_dir_ / L"repo";
    fs::create_directories(repo / L"src");
    std::ofstream(repo / L"a.txt", std::ios::binary) << "hello\n";
    std::ofstream(repo / L"bin.dat", std::ios::binary) << std::string("x\0y", 3);
    const std::string root = win::to_utf8(repo.native());

    const auto list = c->call("fs.list", {{"path", root}});
    ASSERT_TRUE(list && !list->error);
    std::set<std::string> names;
    for (const auto& e : list->result->at("entries"))
        names.insert(e.value("name", "") + (e.value("dir", false) ? "/" : ""));
    EXPECT_EQ(names, (std::set<std::string>{"a.txt", "bin.dat", "src/"}));

    const auto text = c->call("fs.read", {{"path", root + "\\a.txt"}});
    ASSERT_TRUE(text && !text->error);
    EXPECT_EQ(text->result->value("text", ""), "hello\n");
    EXPECT_TRUE(c->call("fs.read", {{"path", root + "\\bin.dat"}})->result->value("binary", false));
    EXPECT_TRUE(c->call("fs.read", {{"path", root + "\\nope"}})->result->value("missing", false));
    EXPECT_TRUE(c->call("fs.list", {{"path", "relative"}})->error.has_value());

    const auto init = agentdesk::host::run_tool({L"git", L"-C", repo.native(), L"init", L"-q"}, repo, 30s);
    if (!init.started || init.exit_code != 0)
        GTEST_SKIP() << "git not available";
    const auto st = c->call("git.run", {{"cwd", root}, {"args", {"status", "--porcelain=v1"}}});
    ASSERT_TRUE(st && !st->error);
    EXPECT_EQ(st->result->value("exit_code", 1), 0);
    EXPECT_NE(st->result->value("output", "").find("?? a.txt"), std::string::npos);
    // Only the panel's commands; no config overrides or external diff programs.
    EXPECT_FALSE(c->call("git.run", {{"cwd", root}, {"args", {"rev-list", "--all"}}})->error.has_value());  // branch tail
    EXPECT_TRUE(c->call("git.run", {{"cwd", root}, {"args", {"config", "--list"}}})->error.has_value());
    EXPECT_TRUE(c->call("git.run", {{"cwd", root}, {"args", {"diff", "--ext-diff"}}})->error.has_value());
    EXPECT_TRUE(c->call("git.run", {{"cwd", root}, {"args", {"status", "-c", "x=y"}}})->error.has_value());
}

// "agentctl hook" (the agent CLI status hook, C++ instead of a script): maps the event on stdin to
// a state report for the session in AGENTDESK_SESSION_ID, prints nothing, always exits 0.
TEST_F(HostFixture, HookCommandReportsStateSilently) {
    const auto id = create("echo", "hooked");
    ::SetEnvironmentVariableW(L"AGENTDESK_SESSION_ID", w(id).c_str());
    ::SetEnvironmentVariableW(L"AGENTDESK_PIPE", pipe_.c_str());
    const std::string permission = R"({"hook_event_name":"PermissionRequest","tool_name":"Bash"})";
    const auto r = run_capture(AGENTDESK_CTL_EXE, {L"hook"}, {}, &permission);
    EXPECT_EQ(r.exit_code, 0u);
    EXPECT_TRUE(r.out.empty()) << r.out;
    auto state = [&] {
        const auto doc = Json::parse(ctl({L"list", L"--json"}).out, nullptr, false);
        for (const auto& sj : doc.value("sessions", Json::array()))
            if (sj.value("id", "") == id)
                return sj.value("agent_state", std::string());
        return std::string();
    };
    EXPECT_TRUE(wait_until([&] { return state() == "Blocked"; }, 5s)) << state();

    const std::string garbage = "not json";
    const auto g = run_capture(AGENTDESK_CTL_EXE, {L"hook"}, {}, &garbage);
    EXPECT_EQ(g.exit_code, 0u);
    EXPECT_TRUE(g.out.empty());

    const std::string stop = R"({"hook_event_name":"Stop"})";
    EXPECT_EQ(run_capture(AGENTDESK_CTL_EXE, {L"hook"}, {}, &stop).exit_code, 0u);
    EXPECT_TRUE(wait_until([&] { return state() == "Idle"; }, 5s)) << state();

    ::SetEnvironmentVariableW(L"AGENTDESK_SESSION_ID", nullptr);  // outside a session: silent no-op
    EXPECT_EQ(run_capture(AGENTDESK_CTL_EXE, {L"hook"}, {}, &stop).exit_code, 0u);
    ::SetEnvironmentVariableW(L"AGENTDESK_PIPE", nullptr);
}
