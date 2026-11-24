// platform::Process on Windows: pipes, exit codes, timeouts and who ends whom.
#include "agentdesk/platform/process.hpp"

#include <chrono>
#include <thread>

#include <windows.h>

#include <gtest/gtest.h>

using namespace std::chrono_literals;
namespace ap = agentdesk::platform;

namespace {

ap::ProcessSpec cmd(std::string line) {
    ap::ProcessSpec s;
    s.executable = ap::find_executable("cmd").value();
    s.args = {"/d", "/c", std::move(line)};
    return s;
}

// Both streams are read while the child runs.
std::string read_all(ap::Process& p, bool err) {
    std::string out;
    while (auto chunk = err ? p.read_some_stderr(4096) : p.read_some(4096))
        out += *chunk;
    return out;
}

bool running(std::uint32_t pid) {
    HANDLE h = ::OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (h == nullptr)
        return false;
    const bool alive = ::WaitForSingleObject(h, 0) == WAIT_TIMEOUT;
    ::CloseHandle(h);
    return alive;
}

bool ends(std::uint32_t pid) {
    for (int i = 0; i < 250 && running(pid); ++i)
        std::this_thread::sleep_for(20ms);
    return !running(pid);
}

}  // namespace

TEST(Process, RunCaptureCollectsOutputAndExitCode) {
    const auto r = ap::run_capture(cmd("echo hello& exit 3"), 10s);
    EXPECT_FALSE(r.timed_out);
    EXPECT_EQ(r.exit_code, 3);
    EXPECT_EQ(r.out, "hello\r\n");
}

TEST(Process, RunCaptureEndsTheTreeAfterTheTimeout) {
    const auto start = std::chrono::steady_clock::now();
    const auto r = ap::run_capture(cmd("ping -n 60 127.0.0.1"), 500ms);
    EXPECT_TRUE(r.timed_out);
    EXPECT_LT(std::chrono::steady_clock::now() - start, 10s) << "ping still held the pipe";
}

TEST(Process, StandardErrorHasItsOwnPipe) {
    auto s = cmd("echo out& echo err 1>&2");
    s.pipe_stderr = true;
    auto p = ap::Process::spawn(s);
    std::string err;
    std::thread t([&] { err = read_all(*p, true); });
    const std::string out = read_all(*p, false);
    t.join();
    EXPECT_EQ(out, "out\r\n");
    EXPECT_NE(err.find("err"), std::string::npos);
    EXPECT_EQ(p->wait(5s), 0);
}

TEST(Process, MergedErrorsShareThePipe) {
    auto s = cmd("echo out& echo err 1>&2");
    s.merge_stderr = true;
    const auto r = ap::run_capture(s, 10s);
    EXPECT_NE(r.out.find("out"), std::string::npos);
    EXPECT_NE(r.out.find("err"), std::string::npos);
}

TEST(Process, InputReachesTheChildAndCloseEndsIt) {
    auto s = cmd("sort");
    s.pipe_stdin = true;
    auto p = ap::Process::spawn(s);
    EXPECT_TRUE(p->write("b\r\na\r\n"));
    p->close_stdin();
    EXPECT_EQ(read_all(*p, false), "a\r\nb\r\n");
    EXPECT_EQ(p->wait(5s), 0);
    EXPECT_FALSE(p->write("x"));
}

TEST(Process, EnvironmentAndFolderAreTheChildsOnly) {
    auto s = cmd("echo [%AGENTDESK_PROCESS_TEST%]& cd");
    s.env_overrides = {{"AGENTDESK_PROCESS_TEST", "v-1"}};
    s.cwd = std::filesystem::path(L"C:\\Windows");
    const auto r = ap::run_capture(s, 10s);
    EXPECT_NE(r.out.find("[v-1]"), std::string::npos);
    EXPECT_NE(r.out.find("C:\\Windows"), std::string::npos);
    wchar_t buf[8]{};
    EXPECT_EQ(::GetEnvironmentVariableW(L"AGENTDESK_PROCESS_TEST", buf, 8), 0u);
}

TEST(Process, EndsWithItsOwnerOnlyWhenAsked) {
    std::uint32_t owned = 0;
    std::uint32_t free_child = 0;
    {
        auto p = ap::Process::spawn(cmd("ping -n 30 127.0.0.1"));
        owned = p->pid();
        auto s = cmd("ping -n 30 127.0.0.1");
        s.end_with_parent = false;
        s.pipe_stdout = false;
        auto q = ap::Process::spawn(s);
        free_child = q->pid();
        EXPECT_TRUE(running(owned));
    }
    EXPECT_TRUE(ends(owned));
    EXPECT_TRUE(running(free_child));
    if (HANDLE h = ::OpenProcess(PROCESS_TERMINATE, FALSE, free_child); h != nullptr) {
        ::TerminateProcess(h, 1);
        ::CloseHandle(h);
    }
}

TEST(Process, BatchFilesAndMissingProgramsAreRefused) {
    ap::ProcessSpec s;
    s.executable = L"C:\\nowhere\\run.cmd";
    EXPECT_THROW(ap::Process::spawn(s), std::invalid_argument);
    s.executable = L"C:\\nowhere\\run.exe";
    EXPECT_THROW(ap::Process::spawn(s), std::system_error);
    EXPECT_FALSE(ap::find_executable("agentdesk-no-such-program").has_value());
}
