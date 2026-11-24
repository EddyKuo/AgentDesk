// Process, run_capture, spawn_detached and find_executable on macOS.
#include "agentdesk/platform/process.hpp"

#include <filesystem>
#include <string>
#include <thread>

#include <fcntl.h>
#include <gtest/gtest.h>
#include <signal.h>
#include <unistd.h>

namespace ap = agentdesk::platform;
using namespace std::chrono_literals;

namespace {

ap::ProcessSpec sh(const std::string& script) {
    ap::ProcessSpec spec;
    spec.executable = "/bin/sh";
    spec.args = {"-c", script};
    return spec;
}

bool process_exists(pid_t pid) {
    return ::kill(pid, 0) == 0 || errno == EPERM;
}

bool wait_gone(pid_t pid, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (!process_exists(pid))
            return true;
        std::this_thread::sleep_for(10ms);
    }
    return false;
}

}  // namespace

TEST(PosixProcess, RunCaptureCollectsOutputAndExitCode) {
    auto r = ap::run_capture(sh("echo hi; exit 3"), 5s);
    EXPECT_FALSE(r.timed_out);
    EXPECT_EQ(r.out, "hi\n");
    EXPECT_EQ(r.exit_code, 3);
}

TEST(PosixProcess, RunCaptureKillsAfterTheTimeout) {
    const auto start = std::chrono::steady_clock::now();
    auto r = ap::run_capture(sh("sleep 10"), 200ms);
    EXPECT_TRUE(r.timed_out);
    EXPECT_LT(std::chrono::steady_clock::now() - start, 3s);
}

TEST(PosixProcess, StdinAndStdoutRoundTrip) {
    ap::ProcessSpec spec;
    spec.executable = "/bin/cat";
    spec.pipe_stdin = true;
    auto p = ap::Process::spawn(spec);
    ASSERT_TRUE(p->write("ping\n"));
    p->close_stdin();
    std::string out;
    while (auto chunk = p->read_some(1024))
        out += *chunk;
    EXPECT_EQ(out, "ping\n");
    EXPECT_EQ(p->wait(5s), 0);
}

TEST(PosixProcess, FolderAndEnvironment) {
    auto spec = sh("pwd; echo [$AGENTDESK_T2][$HOME]");
    spec.cwd = std::filesystem::temp_directory_path();
    spec.env_overrides = {{"AGENTDESK_T2", "two"}, {"HOME", ""}};
    auto r = ap::run_capture(spec, 5s);
    EXPECT_EQ(r.out, std::filesystem::canonical(spec.cwd).string() + "\n[two][]\n");
}

TEST(PosixProcess, StderrIsMergedOnlyWhenAsked) {
    auto spec = sh("echo out; echo err >&2");
    EXPECT_EQ(ap::run_capture(spec, 5s).out, "out\n");
    spec.merge_stderr = true;
    EXPECT_EQ(ap::run_capture(spec, 5s).out, "out\nerr\n");
}

TEST(PosixProcess, ChildInheritsNoStrayDescriptors) {
    // A descriptor without close-on-exec, as a library might leave one.
    const int fd = ::open("/dev/null", O_WRONLY);
    ASSERT_GE(fd, 0);
    ASSERT_EQ(::dup2(fd, 77), 77);
    ::close(fd);
    auto r = ap::run_capture(sh("if : >&77 2>/dev/null; then echo LEAK; else echo OK; fi"), 5s);
    ::close(77);
    EXPECT_EQ(r.out, "OK\n");
}

TEST(PosixProcess, DestructorEndsTheChildAndItsGroup) {
    pid_t bg = 0;
    {
        auto p = ap::Process::spawn(sh("sleep 60 & echo $!; wait"));
        std::string line;
        while (line.find('\n') == std::string::npos) {
            auto chunk = p->read_some(64);
            ASSERT_TRUE(chunk.has_value());
            line += *chunk;
        }
        bg = std::stoi(line);
        EXPECT_TRUE(process_exists(bg));
    }
    EXPECT_TRUE(wait_gone(bg, 3s));
}

TEST(PosixProcess, DetachedProcessLeadsItsOwnSession) {
    const auto r = ap::spawn_detached("/bin/sleep", {"5"});
    ASSERT_GT(r.pid, 0u);
    EXPECT_FALSE(r.dies_with_launcher);
    const auto pid = static_cast<pid_t>(r.pid);
    EXPECT_TRUE(process_exists(pid));
    EXPECT_EQ(::getsid(pid), pid);
    ::kill(pid, SIGKILL);
    EXPECT_TRUE(wait_gone(pid, 3s));
}

TEST(PosixProcess, FindExecutable) {
    const auto found = ap::find_executable("sh");
    ASSERT_TRUE(found.has_value());
    EXPECT_TRUE(ap::is_launchable_file(*found));
    EXPECT_EQ(ap::find_executable("/bin/sh"), std::filesystem::path("/bin/sh"));
    EXPECT_FALSE(ap::find_executable("agentdesk-no-such-program").has_value());
    EXPECT_FALSE(ap::find_executable("/etc/hosts").has_value());
}

TEST(PosixProcess, RejectsWhatCannotBeStarted) {
    ap::ProcessSpec spec;
    spec.executable = "/no/such/program";
    EXPECT_THROW(ap::Process::spawn(spec), std::invalid_argument);
    EXPECT_THROW(ap::spawn_detached("/etc/hosts", {}), std::invalid_argument);
}
