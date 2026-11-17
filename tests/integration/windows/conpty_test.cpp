// Windows integration tests for libs/platform/windows (label: windows_integration).
// They start real child processes inside a ConPTY and a kill-on-close job.
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <string>
#include <system_error>
#include <thread>

#include <gtest/gtest.h>

#include "agentdesk/platform/win/conpty_api.hpp"
#include "agentdesk/platform/win/error.hpp"
#include "agentdesk/platform/win/pty_process.hpp"

using namespace std::chrono_literals;
using agentdesk::platform::win::ConPtyApi;
using agentdesk::platform::win::load_system_conpty;
using agentdesk::platform::win::PtyProcess;
using agentdesk::platform::win::PtySpawnSpec;
using agentdesk::platform::win::to_utf8;

namespace {

class OutputCollector {
public:
    void append(std::span<const char> bytes) {
        std::lock_guard lock(mutex_);
        text_.append(bytes.data(), bytes.size());
        cv_.notify_all();
    }
    bool wait_for(const std::string& needle, std::chrono::milliseconds timeout, std::size_t occurrences = 1) {
        std::unique_lock lock(mutex_);
        return cv_.wait_for(lock, timeout, [&] { return count(needle) >= occurrences; });
    }
    std::string text() {
        std::lock_guard lock(mutex_);
        return text_;
    }

private:
    std::size_t count(const std::string& needle) const {
        std::size_t n = 0;
        for (auto pos = text_.find(needle); pos != std::string::npos; pos = text_.find(needle, pos + 1))
            ++n;
        return n;
    }
    std::mutex mutex_;
    std::condition_variable cv_;
    std::string text_;
};

std::filesystem::path system32() {
    wchar_t buf[MAX_PATH]{};
    const UINT n = ::GetSystemDirectoryW(buf, MAX_PATH);
    return std::filesystem::path(std::wstring(buf, n));
}

PtySpawnSpec cmd_spec(std::vector<std::wstring> args) {
    PtySpawnSpec spec;
    spec.executable = system32() / L"cmd.exe";
    spec.args = std::move(args);
    spec.cwd = std::filesystem::temp_directory_path();
    spec.size = {100, 30};
    return spec;
}

std::unique_ptr<PtyProcess> spawn(const PtySpawnSpec& spec, OutputCollector& out) {
    return PtyProcess::spawn(load_system_conpty(), spec, [&out](std::span<const char> b) { out.append(b); });
}

DWORD handle_count() {
    DWORD n = 0;
    ::GetProcessHandleCount(::GetCurrentProcess(), &n);
    return n;
}

}  // namespace

TEST(ConPty, SystemApiLoads) {
    const ConPtyApi api = load_system_conpty();
    EXPECT_NE(api.create, nullptr);
    EXPECT_NE(api.resize, nullptr);
    EXPECT_NE(api.close, nullptr);
    EXPECT_FALSE(api.bundled);
}

TEST(ConPty, EchoAppearsAndExitCodePropagates) {
    OutputCollector out;
    auto p = spawn(cmd_spec({L"/d", L"/c", L"echo", L"conpty-ok&", L"exit", L"7"}), out);
    const auto code = p->wait_exit(15s);
    ASSERT_TRUE(code.has_value()) << "child did not exit";
    EXPECT_EQ(*code, 7u);
    p->close();
    EXPECT_NE(out.text().find("conpty-ok"), std::string::npos) << out.text();
}

TEST(ConPty, CwdWithSpaceAndChineseIsUsed) {
    const auto dir = std::filesystem::temp_directory_path() / L"AgentDesk 測試 cwd";
    std::filesystem::create_directories(dir);
    OutputCollector out;
    auto spec = cmd_spec({L"/d", L"/c", L"cd"});
    spec.cwd = dir;
    auto p = spawn(spec, out);
    ASSERT_TRUE(p->wait_exit(15s).has_value());
    p->close();
    EXPECT_NE(out.text().find(to_utf8(dir.native())), std::string::npos) << out.text();
    std::filesystem::remove_all(dir);
}

TEST(ConPty, EnvironmentOverridesReachChild) {
    OutputCollector out;
    auto spec = cmd_spec({L"/d", L"/c", L"echo", L"[%AGENTDESK_TEST_VAR%]"});
    spec.env_overrides[L"AGENTDESK_TEST_VAR"] = L"value-42";
    auto p = spawn(spec, out);
    ASSERT_TRUE(p->wait_exit(15s).has_value());
    p->close();
    EXPECT_NE(out.text().find("[value-42]"), std::string::npos) << out.text();
}

TEST(ConPty, InputReachesInteractiveShell) {
    OutputCollector out;
    auto p = spawn(cmd_spec({L"/d", L"/q"}), out);
    p->write("echo typed-input-123\r");
    ASSERT_TRUE(out.wait_for("typed-input-123", 15s, 2)) << out.text();  // echo of command + output
    p->write("exit 3\r");
    const auto code = p->wait_exit(15s);
    ASSERT_TRUE(code.has_value());
    EXPECT_EQ(*code, 3u);
    p->close();
}

TEST(ConPty, ResizeSucceeds) {
    OutputCollector out;
    auto p = spawn(cmd_spec({L"/d", L"/q"}), out);
    EXPECT_NO_THROW(p->resize({80, 24}));
    EXPECT_NO_THROW(p->resize({0, 0}));  // clamped, never forwarded as zero
    p->close();
}

TEST(ConPty, TerminateKillsWholeTree) {
    OutputCollector out;
    auto p = spawn(cmd_spec({L"/d", L"/c", L"ping", L"-n", L"60", L"127.0.0.1"}), out);
    ASSERT_TRUE(out.wait_for("127.0.0.1", 15s)) << out.text();
    EXPECT_GE(p->job_active_processes(), 2u);  // cmd.exe + PING.EXE
    p->terminate(9);
    const auto code = p->wait_exit(5s);
    ASSERT_TRUE(code.has_value());
    EXPECT_EQ(*code, 9u);
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (p->job_active_processes() != 0 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(20ms);
    }
    EXPECT_EQ(p->job_active_processes(), 0u);
    p->close();
}

// Verifies the skill's "write 0x03 to interrupt" claim on this machine (design 5.2 item 7).
TEST(ConPty, CtrlCByteInterruptsPing) {
    OutputCollector out;
    PtySpawnSpec spec = cmd_spec({});
    spec.executable = system32() / L"PING.EXE";
    spec.args = {L"-n", L"60", L"127.0.0.1"};
    auto p = spawn(spec, out);
    ASSERT_TRUE(out.wait_for("127.0.0.1", 15s)) << out.text();
    p->interrupt();
    EXPECT_TRUE(p->wait_exit(5s).has_value()) << "PING did not react to 0x03";
    p->close();
}

TEST(ConPty, CloseWhileRunningDoesNotDeadlock) {
    OutputCollector out;
    auto p = spawn(cmd_spec({L"/d", L"/c", L"ping", L"-n", L"60", L"127.0.0.1"}), out);
    ASSERT_TRUE(out.wait_for("127.0.0.1", 15s));
    const auto start = std::chrono::steady_clock::now();
    p->close();
    EXPECT_LT(std::chrono::steady_clock::now() - start, 10s);
    EXPECT_TRUE(p->wait_exit(5s).has_value());
}

TEST(ConPty, RejectsBatchFilesAndMissingPaths) {
    OutputCollector out;
    auto bad_batch = cmd_spec({});
    bad_batch.executable = L"C:\\nowhere\\tool.cmd";
    EXPECT_THROW(spawn(bad_batch, out), std::invalid_argument);

    auto missing_exe = cmd_spec({});
    missing_exe.executable = L"C:\\nowhere\\missing.exe";
    EXPECT_THROW(spawn(missing_exe, out), std::system_error);

    auto missing_cwd = cmd_spec({});
    missing_cwd.cwd = L"C:\\nowhere\\at\\all";
    EXPECT_THROW(spawn(missing_cwd, out), std::system_error);
}

TEST(ConPty, RepeatedSpawnCloseDoesNotLeakHandles) {
    {
        OutputCollector warm;
        auto p = spawn(cmd_spec({L"/d", L"/c", L"exit", L"0"}), warm);
        p->wait_exit(15s);
    }
    const DWORD before = handle_count();
    for (int i = 0; i < 30; ++i) {
        OutputCollector out;
        auto p = spawn(cmd_spec({L"/d", L"/c", L"exit", L"0"}), out);
        ASSERT_TRUE(p->wait_exit(15s).has_value());
    }
    // Small slack for runtime-internal handles; a leak grows by several handles per spawn.
    EXPECT_LE(handle_count(), before + 8) << "before=" << before;
}
