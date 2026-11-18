// agent-host's self-update (design doc 7.4 OQ-09; herdr src/update.rs 236: "terminals keep
// running"): the pieces that move a live terminal to another owner.
#include <windows.h>

#include <chrono>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include "agentdesk/host/session.hpp"
#include "agentdesk/platform/win/conpty_api.hpp"
#include "agentdesk/platform/win/pty_process.hpp"

using namespace std::chrono_literals;
namespace win = agentdesk::platform::win;

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
