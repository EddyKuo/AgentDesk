// Session actor: process/agent state, interrupt grace period, structured status sources.
#include "agentdesk/host/session.hpp"

#include <chrono>
#include <filesystem>
#include <thread>

#include <gtest/gtest.h>

#include "agentdesk/platform/win/conpty_api.hpp"

using namespace std::chrono_literals;
namespace win = agentdesk::platform::win;
using agentdesk::host::AgentState;
using agentdesk::host::ProcessState;
using agentdesk::host::RawInput;
using agentdesk::host::Session;
using agentdesk::host::SessionConfig;
using agentdesk::host::TextInput;

namespace {

win::ConPtyApi best_conpty() {
    return win::load_conpty(std::filesystem::path(AGENTDESK_BUNDLED_CONPTY_DIR));
}

SessionConfig probe_config(std::string mode) {
    SessionConfig c;
    c.api = best_conpty();
    c.spec.executable = AGENTDESK_VT_PROBE;
    c.spec.args = {std::move(mode)};
    c.spec.cwd = std::filesystem::temp_directory_path();
    c.spec.size = {80, 24};
    c.engine.rows = 24;
    c.engine.cols = 80;
    return c;
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

bool screen_contains(const Session& s, const std::string& needle) {
    const auto snap = s.snapshot();
    for (int r = 0; r < snap->rows; ++r) {
        if (snap->row_text(r).find(needle) != std::string::npos)
            return true;
    }
    return false;
}

}  // namespace

TEST(Session, StartsRunningAndExitsWithCode) {
    auto s = Session::start(probe_config("report"));
    EXPECT_NE(s->info().pid, 0u);
    const auto code = s->wait_exit(15s);
    ASSERT_TRUE(code.has_value());
    EXPECT_EQ(*code, 0u);
    EXPECT_EQ(s->info().process_state, ProcessState::kExited);
    EXPECT_TRUE(screen_contains(*s, "PROBE-DONE"));
}

// QA-08/QA-22: OSC 7501 drives agent state; process exit drops blocked -> Unknown, last kept.
TEST(Session, Osc7501StateThenUnknownAfterExitKeepsLastState) {
    auto s = Session::start(probe_config("report"));
    ASSERT_TRUE(s->wait_exit(15s).has_value());
    const auto info = s->info();
    EXPECT_EQ(info.agent.state, AgentState::kUnknown);
    EXPECT_EQ(info.agent.last_state, AgentState::kBlocked);
    EXPECT_EQ(info.agent.source, "osc7501");
}

// QA-09: a child that ignores Ctrl+C -> still running after the grace period -> force stop.
TEST(Session, InterruptGracePeriodThenForceStop) {
    auto config = probe_config("echo");  // raw input mode: 0x03 is just a byte to the probe
    config.interrupt_grace = 300ms;
    auto s = Session::start(config);
    std::this_thread::sleep_for(300ms);
    s->interrupt();
    ASSERT_TRUE(wait_until([&] { return s->info().still_running_after_interrupt; }, 5s));
    EXPECT_EQ(s->info().process_state, ProcessState::kStopping);
    s->force_stop();
    ASSERT_TRUE(s->wait_exit(5s).has_value());
    EXPECT_EQ(s->info().process_state, ProcessState::kExited);
    EXPECT_FALSE(s->info().still_running_after_interrupt);
}

TEST(Session, TextInputIsEncodedByEngine) {
    auto s = Session::start(probe_config("echo"));
    std::this_thread::sleep_for(300ms);
    s->send(TextInput{U"hi測\r"});
    ASSERT_TRUE(wait_until([&] { return screen_contains(*s, "PROBE-ECHO:6869e6b8ac0d"); }, 10s));
    s->send(RawInput{"quit\r"});
    EXPECT_TRUE(s->wait_exit(10s).has_value());
}

// QA-19 (host side): stale or replayed seq values from one source never override newer state.
TEST(Session, ReportStateIgnoresStaleSeq) {
    auto s = Session::start(probe_config("echo"));
    s->report_state("codex-hook", 100, AgentState::kWorking);
    s->report_state("codex-hook", 99, AgentState::kIdle);   // stale
    s->report_state("codex-hook", 100, AgentState::kDone);  // replay
    ASSERT_TRUE(wait_until([&] { return s->info().agent.state == AgentState::kWorking; }, 5s));
    std::this_thread::sleep_for(100ms);
    EXPECT_EQ(s->info().agent.state, AgentState::kWorking);
    EXPECT_EQ(s->info().agent.source, "report:codex-hook");
    s->report_state("codex-hook", 101, AgentState::kBlocked, agentdesk::terminal::StatusKind::kPermission,
                    "approve?");
    ASSERT_TRUE(wait_until([&] { return s->info().agent.state == AgentState::kBlocked; }, 5s));
    EXPECT_EQ(s->info().agent.message, "approve?");
    s->force_stop();
}

TEST(Session, ResizeChangesSnapshotSizeAndGeneration) {
    auto s = Session::start(probe_config("echo"));
    const auto gen = s->snapshot()->generation;
    s->resize(30, 100);
    ASSERT_TRUE(wait_until([&] { return s->snapshot()->cols == 100 && s->snapshot()->rows == 30; }, 5s));
    EXPECT_GT(s->snapshot()->generation, gen);
    s->force_stop();
}

// QA-07: with no structured source, silence never turns into Done (or anything else).
TEST(Session, SilenceWithoutSourceStaysUnknown) {
    auto s = Session::start(probe_config("echo"));
    std::this_thread::sleep_for(1500ms);
    const auto info = s->info();
    EXPECT_EQ(info.process_state, ProcessState::kRunning);
    EXPECT_EQ(info.agent.state, AgentState::kUnknown);
    EXPECT_TRUE(info.agent.source.empty());
    s->force_stop();
}

// OQ-06 backpressure: a tiny queue forces the reader to pause; every line still arrives in order.
TEST(Session, BackpressureNeverDropsOutput) {
    auto config = probe_config("flood");
    config.spec.args = {"flood", "2", "2097152"};  // 2 s at 2 MiB/s
    config.output_queue_limit = 4 * 1024;
    auto s = Session::start(config);
    ASSERT_TRUE(s->wait_exit(60s).has_value());
    const auto info = s->info();
    EXPECT_GT(info.backpressure_waits, 0u)
        << "queue limit never reached; test is not exercising backpressure";
    EXPECT_TRUE(screen_contains(*s, "FLOOD-DONE"));
    // Kept history lines must be consecutive: nothing dropped while the reader was paused.
    long long prev = -1;
    long long gaps = 0;
    for (const auto& line : s->history(0, 20000)) {
        std::string text;
        for (const auto& cell : line.cells) {
            if (cell.width != 0 && cell.chars[0] != 0)
                agentdesk::terminal::append_utf8(text, cell.chars[0]);
            else if (cell.width != 0)
                text.push_back(' ');
        }
        if (text.size() > 11 && text[0] == 'L') {
            const long long v = std::stoll(text.substr(1, 10));
            if (prev >= 0 && v != prev + 1)
                ++gaps;
            prev = v;
        }
    }
    EXPECT_GE(prev, 0);
    EXPECT_EQ(gaps, 0);
}

TEST(Session, StartFailureIsReported) {
    auto config = probe_config("echo");
    config.spec.executable = L"C:\\nowhere\\missing.exe";
    EXPECT_THROW(Session::start(config), std::system_error);
}
