// S1-5: inbox vs bundled ConPTY (design doc 5.2 item 7, ADR-08).
// The probe plays the agent: it sends "OSC 7501 ; ?" + DA1 and reports which replies arrive;
// the host side is a real VtEngine whose replies are written back through the ConPTY.
#include <chrono>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include "agentdesk/platform/win/conpty_api.hpp"
#include "agentdesk/platform/win/pty_process.hpp"
#include "agentdesk/terminal/vt_engine.hpp"

using namespace std::chrono_literals;
using agentdesk::platform::win::ConPtyApi;
using agentdesk::platform::win::PtyProcess;
using agentdesk::platform::win::PtySpawnSpec;
using agentdesk::terminal::StatusState;
using agentdesk::terminal::VtEngine;

namespace {

// Minimal host: engine fed on the reader thread under a lock, replies written by a separate
// writer thread (never read and write on the same thread).
class ProbeHost {
public:
    explicit ProbeHost(const ConPtyApi& api, std::wstring mode) {
        PtySpawnSpec spec;
        spec.executable = AGENTDESK_VT_PROBE;
        spec.args = {std::move(mode)};
        spec.cwd = std::filesystem::temp_directory_path();
        spec.size = {100, 30};
        writer_ = std::thread([this] { writer_loop(); });
        auto process = PtyProcess::spawn(api, spec, [this](std::span<const char> bytes) { on_output(bytes); });
        {
            std::lock_guard lock(mutex_);
            process_ = std::move(process);
        }
        cv_.notify_all();  // replies queued before spawn() returned can now be written
    }
    ~ProbeHost() {
        process_->close();
        {
            std::lock_guard lock(mutex_);
            stop_ = true;
        }
        cv_.notify_all();
        writer_.join();
    }
    ProbeHost(const ProbeHost&) = delete;
    ProbeHost& operator=(const ProbeHost&) = delete;

    bool wait_text(const std::string& needle, std::chrono::milliseconds timeout) {
        std::unique_lock lock(mutex_);
        return cv_.wait_for(lock, timeout, [&] { return raw_.find(needle) != std::string::npos; });
    }
    std::string raw() {
        std::lock_guard lock(mutex_);
        return raw_;
    }
    // Hex string the probe printed after "PROBE-REPLY:".
    std::string probe_reply_hex() {
        const std::string r = raw();
        const auto pos = r.find("PROBE-REPLY:");
        if (pos == std::string::npos) return {};
        auto end = r.find_first_not_of("0123456789abcdef", pos + 12);
        return r.substr(pos + 12, end - pos - 12);
    }
    template <typename Fn>
    auto with_engine(Fn fn) {
        std::lock_guard lock(mutex_);
        return fn(engine_);
    }
    PtyProcess& process() { return *process_; }

private:
    void on_output(std::span<const char> bytes) {
        {
            std::lock_guard lock(mutex_);
            raw_.append(bytes.data(), bytes.size());
            engine_.feed(std::string_view(bytes.data(), bytes.size()));
            std::string replies = engine_.take_pty_input();
            if (!replies.empty()) pending_.push_back(std::move(replies));
        }
        cv_.notify_all();
    }
    void writer_loop() {
        std::unique_lock lock(mutex_);
        for (;;) {
            cv_.wait(lock, [&] { return stop_ || (!pending_.empty() && process_); });
            if (stop_) return;
            std::string chunk = std::move(pending_.front());
            pending_.pop_front();
            lock.unlock();
            try {
                process_->write(chunk);
            } catch (const std::exception&) {
                // the probe may have exited; the test asserts on what arrived
            }
            lock.lock();
        }
    }

    std::mutex mutex_;
    std::condition_variable cv_;
    std::string raw_;
    VtEngine engine_;
    std::deque<std::string> pending_;
    bool stop_ = false;
    std::thread writer_;
    std::unique_ptr<PtyProcess> process_;
};

std::string hex(std::string_view s) {
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string out;
    for (const char c : s) {
        const auto u = static_cast<unsigned char>(c);
        out.push_back(kDigits[u >> 4]);
        out.push_back(kDigits[u & 0xF]);
    }
    return out;
}

ConPtyApi api_for(const std::string& which) {
    if (which == "inbox") return agentdesk::platform::win::load_system_conpty();
    const std::filesystem::path dir = AGENTDESK_BUNDLED_CONPTY_DIR;
    auto api = agentdesk::platform::win::load_conpty(dir);
    return api;
}

}  // namespace

class ConPtyCompareTest : public ::testing::TestWithParam<std::string> {
protected:
    void SetUp() override {
        if (GetParam() == "bundled" &&
            !std::filesystem::exists(std::filesystem::path(AGENTDESK_BUNDLED_CONPTY_DIR) / "conpty" / "conpty.dll")) {
            GTEST_SKIP() << "bundled ConPTY not fetched (run build/<preset>/tools/fetch_conpty.exe)";
        }
        api_ = api_for(GetParam());
        if (GetParam() == "bundled") {
            ASSERT_TRUE(api_.bundled);
        }
    }
    ConPtyApi api_;
};

TEST_P(ConPtyCompareTest, StatusQueryReplyReachesProgramBeforeDa1) {
    ProbeHost host(api_, L"query");
    ASSERT_TRUE(host.wait_text("PROBE-REPLY:", 15s)) << host.raw();
    const std::string got = host.probe_reply_hex();
    const auto osc = got.find(hex("\x1b]7501;?\x1b\\"));
    const auto da = got.find(hex("\x1b[?"));
    RecordProperty("probe_reply_hex", got);
    EXPECT_TRUE(host.with_engine([](VtEngine& e) { return e.status_query_answered(); }))
        << "the 7501 query did not reach the host engine";
    EXPECT_NE(da, std::string::npos) << "DA1 reply did not reach the program: " << got;
    if (GetParam() == "inbox") {
        // Characterization (measured 2026-11-17 on Windows 10.0.26300): the inbox ConPTY answers
        // DA1 itself ("ESC[?61;6;7;21;22;23;24;28;32;42c") and drops the host's OSC 7501 reply,
        // so programs conclude the terminal lacks status support. This is why ADR-08 bundles
        // ConPTY. If this starts failing, Windows changed: re-evaluate ADR-08.
        EXPECT_EQ(osc, std::string::npos) << "inbox ConPTY now forwards the 7501 reply: " << got;
        return;
    }
    ASSERT_NE(osc, std::string::npos) << "OSC 7501 reply did not reach the program: " << got;
    EXPECT_LT(osc, da) << got;
}

TEST_P(ConPtyCompareTest, StatusReportsReachHost) {
    ProbeHost host(api_, L"report");
    ASSERT_TRUE(host.wait_text("PROBE-DONE", 15s)) << host.raw();
    std::this_thread::sleep_for(100ms);  // the report precedes PROBE-DONE in the same stream
    const auto root = host.with_engine([](VtEngine& e) {
        const auto* r = e.status().root();
        return r ? std::optional<StatusState>(r->report.state) : std::nullopt;
    });
    ASSERT_TRUE(root.has_value()) << "no OSC 7501 report reached the host";
    EXPECT_EQ(*root, StatusState::kBlocked);
}

TEST_P(ConPtyCompareTest, KeyboardBytesArriveUnchanged) {
    ProbeHost host(api_, L"echo");
    std::this_thread::sleep_for(300ms);
    host.process().write("ab\xe6\xb8\xac\r");  // "ab測" + Enter
    ASSERT_TRUE(host.wait_text("PROBE-ECHO:", 15s)) << host.raw();
    EXPECT_NE(host.raw().find("PROBE-ECHO:6162e6b8ac0d"), std::string::npos) << host.raw();
    host.process().write("quit\r");
    EXPECT_TRUE(host.process().wait_exit(10s).has_value());
}

// ConPTY asks its viewer for win32-input-mode; the engine notices and
// sends Shift+Enter as win32 key events, which ConPTY turns back into Enter for the program.
TEST_P(ConPtyCompareTest, Win32InputModeShiftEnterReachesProgram) {
    ProbeHost host(api_, L"echo");
    std::this_thread::sleep_for(300ms);
    ASSERT_TRUE(host.with_engine([](VtEngine& e) { return e.win32_input(); })) << hex(host.raw().substr(0, 200));
    host.process().write("hi");
    const std::string keys = host.with_engine([](VtEngine& e) {
        e.key(agentdesk::terminal::Key::kEnter, agentdesk::terminal::kModShift);
        return e.take_pty_input();
    });
    EXPECT_EQ(keys.rfind("\x1b[13;28;13;1;16;1_", 0), 0u);
    host.process().write(keys);
    ASSERT_TRUE(host.wait_text("PROBE-ECHO:", 15s)) << host.raw();
    EXPECT_NE(host.raw().find("PROBE-ECHO:6869"), std::string::npos) << host.raw();
    host.process().write("quit\r");
    EXPECT_TRUE(host.process().wait_exit(10s).has_value());
}

INSTANTIATE_TEST_SUITE_P(Variants, ConPtyCompareTest, ::testing::Values("inbox", "bundled"));
