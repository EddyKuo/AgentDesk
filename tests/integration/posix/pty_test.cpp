// Pty on macOS: real programs in real pseudo terminals.
#include "agentdesk/platform/pty.hpp"

#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>

#include <gtest/gtest.h>
#include <signal.h>
#include <unistd.h>

namespace ap = agentdesk::platform;
using namespace std::chrono_literals;

namespace {

// Collects everything a Pty prints and lets a test wait for text.
class Output {
public:
    ap::PtyOutputSink sink() {
        return [this](std::span<const char> bytes) {
            std::lock_guard lock(mutex_);
            text_.append(bytes.data(), bytes.size());
            cv_.notify_all();
        };
    }
    bool wait_for(const std::string& needle, std::chrono::milliseconds timeout) {
        std::unique_lock lock(mutex_);
        return cv_.wait_for(lock, timeout, [&] { return text_.find(needle) != std::string::npos; });
    }
    std::string text() {
        std::lock_guard lock(mutex_);
        return text_;
    }

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    std::string text_;
};

ap::PtySpawnSpec sh(const std::string& script) {
    ap::PtySpawnSpec spec;
    spec.executable = "/bin/sh";
    spec.args = {"-c", script};
    spec.cwd = std::filesystem::temp_directory_path();
    spec.size = {80, 24};
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
    return !process_exists(pid);
}

}  // namespace

TEST(PosixPty, ProgramOutputReachesTheSink) {
    Output out;
    auto pty = ap::Pty::spawn(sh("printf hello"), out.sink(), {});
    EXPECT_TRUE(out.wait_for("hello", 5s));
    EXPECT_EQ(pty->wait_exit(5s), 0);
    pty->close();
}

TEST(PosixPty, ExitCodeAndExitCallback) {
    Output out;
    std::atomic<int> exits{0};
    auto pty = ap::Pty::spawn(sh("exit 7"), out.sink(), [&] { ++exits; });
    EXPECT_EQ(pty->wait_exit(5s), 7);
    pty->close();
    EXPECT_EQ(exits.load(), 1);
    EXPECT_TRUE(pty->closed());
}

TEST(PosixPty, InputReachesTheProgram) {
    Output out;
    ap::PtySpawnSpec spec = sh("read line; echo got:$line");
    auto pty = ap::Pty::spawn(spec, out.sink(), {});
    pty->write("abc\r");
    EXPECT_TRUE(out.wait_for("got:abc", 5s)) << out.text();
    EXPECT_EQ(pty->wait_exit(5s), 0);
}

TEST(PosixPty, ResizeIsSeenByTheProgram) {
    Output out;
    auto spec = sh("stty size; read _; stty size");
    spec.size = {100, 30};
    auto pty = ap::Pty::spawn(spec, out.sink(), {});
    ASSERT_TRUE(out.wait_for("30 100", 5s)) << out.text();
    pty->resize({90, 25});
    pty->write("\r");
    EXPECT_TRUE(out.wait_for("25 90", 5s)) << out.text();
}

TEST(PosixPty, InterruptStopsTheProgramInFront) {
    Output out;
    auto pty = ap::Pty::spawn(sh("exec sleep 30"), out.sink(), {});
    std::this_thread::sleep_for(200ms);
    pty->interrupt();
    EXPECT_EQ(pty->wait_exit(5s), 128 + SIGINT);
}

TEST(PosixPty, CloseEndsBackgroundChildren) {
    Output out;
    auto pty = ap::Pty::spawn(sh("sleep 60 & echo BG=$!; sleep 60"), out.sink(), {});
    ASSERT_TRUE(out.wait_for("\n", 5s));
    const std::string text = out.text();
    const auto at = text.find("BG=");
    ASSERT_NE(at, std::string::npos) << text;
    const pid_t bg = std::stoi(text.substr(at + 3));
    EXPECT_GE(pty->live_processes(), 2u);
    pty->close();
    EXPECT_TRUE(wait_gone(bg, 3s));
    EXPECT_EQ(pty->live_processes(), 0u);
}

TEST(PosixPty, TerminateEndsTheWholeGroup) {
    Output out;
    auto pty = ap::Pty::spawn(sh("trap '' INT HUP TERM; sleep 60 & echo BG=$!; sleep 60"), out.sink(), {});
    ASSERT_TRUE(out.wait_for("\n", 5s));
    const std::string text = out.text();
    const pid_t bg = std::stoi(text.substr(text.find("BG=") + 3));
    pty->terminate();
    EXPECT_EQ(pty->wait_exit(5s), 128 + SIGKILL);
    EXPECT_TRUE(wait_gone(bg, 3s));
}

TEST(PosixPty, ForegroundIsTheRootProgramAndItsFolder) {
    Output out;
    ap::PtySpawnSpec spec;
    spec.executable = "/bin/sleep";
    spec.args = {"30"};
    spec.cwd = std::filesystem::temp_directory_path();
    auto pty = ap::Pty::spawn(spec, out.sink(), {});
    std::this_thread::sleep_for(200ms);
    const auto fg = pty->foreground();
    EXPECT_EQ(fg.program, "sleep");
    EXPECT_EQ(std::filesystem::path(fg.cwd), std::filesystem::canonical(spec.cwd));
    ASSERT_EQ(fg.argv.size(), 2u);
    EXPECT_EQ(fg.argv[1], "30");
}

TEST(PosixPty, ForegroundFollowsTheJobAnInteractiveShellRuns) {
    Output out;
    ap::PtySpawnSpec spec;
    spec.executable = "/bin/zsh";
    spec.args = {"-f", "-i"};  // no rc files: the person's setup must not change the test
    spec.cwd = std::filesystem::temp_directory_path();
    auto pty = ap::Pty::spawn(spec, out.sink(), {});
    std::this_thread::sleep_for(300ms);
    EXPECT_EQ(pty->foreground().program, "zsh");
    pty->write("sleep 30\r");
    std::string program;
    for (int i = 0; i < 100 && program != "sleep"; ++i) {
        std::this_thread::sleep_for(30ms);
        program = pty->foreground().program;
    }
    EXPECT_EQ(program, "sleep");
    pty->interrupt();
    for (int i = 0; i < 100 && program != "zsh"; ++i) {
        std::this_thread::sleep_for(30ms);
        program = pty->foreground().program;
    }
    EXPECT_EQ(program, "zsh");
}

TEST(PosixPty, EveryByteArrivesInOrderEvenWhenTheSinkIsSlow) {
    std::atomic<std::size_t> count{0};
    std::atomic<bool> wrong{false};
    std::atomic<int> calls{0};
    auto sink = [&](std::span<const char> bytes) {
        if (calls++ < 5)
            std::this_thread::sleep_for(100ms);  // backpressure: nothing is read meanwhile
        for (const char c : bytes) {
            if (c == 'a')
                ++count;
            else if (c != '\r' && c != '\n')
                wrong = true;
        }
    };
    auto pty = ap::Pty::spawn(sh("head -c 2000000 /dev/zero | tr '\\000' a"), sink, {});
    EXPECT_EQ(pty->wait_exit(30s), 0);
    pty->close();  // drains what is left
    EXPECT_EQ(count.load(), 2000000u);
    EXPECT_FALSE(wrong.load());
}

TEST(PosixPty, PausedReaderHoldsOutputUntilResumed) {
    Output out;
    auto pty = ap::Pty::spawn(sh("read _; printf later; sleep 5"), out.sink(), {});
    ASSERT_TRUE(pty->pause_reader(2s));
    pty->write("\r");
    std::this_thread::sleep_for(300ms);
    EXPECT_EQ(out.text().find("later"), std::string::npos);
    pty->resume_reader();
    EXPECT_TRUE(out.wait_for("later", 5s)) << out.text();
}

TEST(PosixPty, RejectsWhatCannotBeStarted) {
    Output out;
    ap::PtySpawnSpec spec = sh("true");
    spec.executable = "/no/such/program";
    EXPECT_THROW(ap::Pty::spawn(spec, out.sink(), {}), std::invalid_argument);
    spec = sh("true");
    spec.cwd = "/no/such/folder";
    EXPECT_THROW(ap::Pty::spawn(spec, out.sink(), {}), std::invalid_argument);
    spec = sh("true");
    spec.executable = "/etc/hosts";  // a file, but not executable
    EXPECT_THROW(ap::Pty::spawn(spec, out.sink(), {}), std::invalid_argument);
}

TEST(PosixPty, EnvironmentOverridesAndRemovals) {
    Output out;
    auto spec = sh("echo [$AGENTDESK_T1][$HOME]");
    spec.env_overrides = {{"AGENTDESK_T1", "one"}, {"HOME", ""}};
    auto pty = ap::Pty::spawn(spec, out.sink(), {});
    EXPECT_TRUE(out.wait_for("[one][]", 5s)) << out.text();
}

TEST(PosixPty, CapabilitiesOnMacOS) {
    EXPECT_TRUE(ap::pty_capabilities().answers_osc7501_query);
    EXPECT_TRUE(ap::is_shell_name("zsh"));
    EXPECT_TRUE(ap::is_shell_name("-zsh"));
    EXPECT_TRUE(ap::is_shell_name("pwsh.exe"));
    EXPECT_FALSE(ap::is_shell_name("node"));
}

// A program that never reads its input fills the terminal's input queue: the writer waits, but
// close() still ends everything promptly and the stuck write gives up.
TEST(PosixPty, CloseIsNotHeldUpByAWriteTheProgramNeverReads) {
    Output out;
    auto pty = ap::Pty::spawn(sh("stty raw -echo; echo ready; sleep 30"), out.sink(), {});
    ASSERT_TRUE(out.wait_for("ready", 5s));
    std::atomic<bool> write_ended{false};
    std::thread writer([&] {
        try {
            pty->write(std::string(1024 * 1024, 'x'));
        } catch (const std::exception&) {
        }
        write_ended = true;
    });
    std::this_thread::sleep_for(300ms);
    EXPECT_FALSE(write_ended.load());  // still waiting for room
    const auto t0 = std::chrono::steady_clock::now();
    pty->resize({100, 30});  // does not wait for the writer
    EXPECT_LT(std::chrono::steady_clock::now() - t0, 1s);
    pty->close();
    EXPECT_LT(std::chrono::steady_clock::now() - t0, 6s);
    writer.join();
    EXPECT_TRUE(write_ended.load());
}

// A shell puts each job in its own process group; closing the terminal ends those too.
TEST(PosixPty, CloseEndsJobsInOtherProcessGroups) {
    Output out;
    auto pty = ap::Pty::spawn(sh("set -m; sleep 300 & echo \"job=$!\"; wait"), out.sink(), {});
    ASSERT_TRUE(out.wait_for("\n", 5s));
    const std::string text = out.text();
    const auto at = text.find("job=");
    ASSERT_NE(at, std::string::npos) << text;
    const pid_t job = static_cast<pid_t>(std::stol(text.substr(at + 4)));
    ASSERT_NE(::getpgid(job), static_cast<pid_t>(pty->pid()));  // its own group
    EXPECT_GE(pty->live_processes(), 2u);
    pty->close();
    EXPECT_TRUE(wait_gone(job, 3s));
}
