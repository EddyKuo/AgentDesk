// Pty on a POSIX pseudo terminal: the child leads its own session with the terminal as its
// controlling tty, so the shell's process group is what close() and terminate() end.
#include <algorithm>
#include <array>
#include <condition_variable>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#include "agentdesk/platform/pty.hpp"
#include "posix_util.hpp"
#include "proc_info.hpp"

namespace agentdesk::platform {

using posix::UniqueFd;

namespace {

// After the shell got SIGHUP, how long close() lets the reader drain what is still coming.
constexpr auto kDrainWait = std::chrono::seconds(2);
// After SIGKILL, how long close() waits for the process group to be gone.
constexpr auto kKillWait = std::chrono::seconds(2);

std::mutex g_ptsname_mutex;  // ptsname() returns a static buffer

winsize to_winsize(PtySize size) {
    winsize ws{};
    ws.ws_col = std::max<std::uint16_t>(size.cols, 1);
    ws.ws_row = std::max<std::uint16_t>(size.rows, 1);
    return ws;
}

}  // namespace

struct Pty::Impl {
    UniqueFd master;
    pid_t pid = -1;
    PtyOutputSink on_output;
    PtyExitSink on_exit;
    UniqueFd wake_read;  // wakes the reader's poll()
    UniqueFd wake_write;
    std::thread reader;
    std::thread waiter;

    mutable std::mutex mutex;
    mutable std::condition_variable cv;
    bool reader_done = false;
    bool pause_requested = false;
    bool paused = false;
    bool stop_requested = false;
    std::optional<int> exit_code;

    std::mutex write_mutex;
    mutable std::mutex close_mutex;  // lock order: close_mutex before write_mutex
    bool closed = false;

    void wake() const {
        const char b = 1;
        (void)::write(wake_write.get(), &b, 1);
    }

    void drain_wake() const {
        std::array<char, 64> buf{};
        while (::read(wake_read.get(), buf.data(), buf.size()) > 0) {
        }
    }

    void reader_loop() noexcept {
        std::array<char, 64 * 1024> buf{};
        for (;;) {
            {
                std::unique_lock lock(mutex);
                if (stop_requested)
                    break;
                if (pause_requested) {
                    paused = true;
                    cv.notify_all();
                    cv.wait(lock, [&] { return !pause_requested || stop_requested; });
                    paused = false;
                    continue;
                }
            }
            pollfd fds[2] = {{master.get(), POLLIN, 0}, {wake_read.get(), POLLIN, 0}};
            const int r = ::poll(fds, 2, -1);
            if (r < 0) {
                if (errno == EINTR)
                    continue;
                break;
            }
            if ((fds[1].revents & POLLIN) != 0) {
                drain_wake();
                continue;  // re-check pause / stop before reading more
            }
            if (fds[0].revents == 0)
                continue;
            const ssize_t n = ::read(master.get(), buf.data(), buf.size());
            if (n > 0) {
                try {
                    on_output(std::span<const char>(buf.data(), static_cast<std::size_t>(n)));
                } catch (...) {
                    // A sink that throws loses nothing it already took; keep reading.
                }
                continue;
            }
            if (n < 0 && (errno == EINTR || errno == EAGAIN))
                continue;
            break;  // EOF or EIO: no process holds the terminal any more
        }
        std::lock_guard lock(mutex);
        reader_done = true;
        cv.notify_all();
    }

    void waiter_loop() noexcept {
        int status = 0;
        pid_t r = -1;
        do {
            r = ::waitpid(pid, &status, 0);
        } while (r < 0 && errno == EINTR);
        {
            std::lock_guard lock(mutex);
            exit_code = r == pid ? posix::exit_code_from_status(status) : -1;
            cv.notify_all();
        }
        if (on_exit) {
            try {
                on_exit();
            } catch (...) {
            }
        }
    }

    // The process group is the child's pid (it called setsid()).
    void signal_group(int sig) const {
        if (::killpg(pid, sig) != 0 && errno == ESRCH) {
            // Group already empty.
        }
    }
};

Pty::Pty(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

Pty::~Pty() {
    try {
        close();
    } catch (...) {
    }
}

std::unique_ptr<Pty> Pty::spawn(const PtySpawnSpec& spec, PtyOutputSink on_output, PtyExitSink on_exit) {
    posix::check_launchable(spec.executable);
    std::error_code ec;
    if (!std::filesystem::is_directory(spec.cwd, ec))
        throw std::invalid_argument("not a folder: " + spec.cwd.string());

    UniqueFd master(::posix_openpt(O_RDWR | O_NOCTTY));
    if (!master.valid())
        posix::throw_errno("posix_openpt");
    posix::set_cloexec(master.get());
    if (::grantpt(master.get()) != 0)
        posix::throw_errno("grantpt");
    if (::unlockpt(master.get()) != 0)
        posix::throw_errno("unlockpt");
    std::string slave_name;
    {
        std::lock_guard lock(g_ptsname_mutex);
        const char* name = ::ptsname(master.get());
        if (name == nullptr)
            posix::throw_errno("ptsname");
        slave_name = name;
    }
    // The parent keeps the slave open until the child holds it, so the master never sees a
    // hang-up before the child has started.
    UniqueFd slave(::open(slave_name.c_str(), O_RDWR | O_NOCTTY | O_CLOEXEC));
    if (!slave.valid())
        posix::throw_errno("open(pty slave)");
    const winsize ws = to_winsize(spec.size);
    if (::ioctl(slave.get(), TIOCSWINSZ, &ws) != 0)
        posix::throw_errno("ioctl(TIOCSWINSZ)");

    // Everything the child needs is prepared before fork(): after it, only async-signal-safe calls.
    std::vector<std::string> argv_strings;
    argv_strings.push_back(spec.executable.string());
    argv_strings.insert(argv_strings.end(), spec.args.begin(), spec.args.end());
    std::vector<std::string> env_strings = posix::make_environment(spec.env_overrides);
    std::vector<char*> argv = posix::c_string_array(argv_strings);
    std::vector<char*> envp = posix::c_string_array(env_strings);
    const std::string exe = spec.executable.string();
    const std::string cwd = spec.cwd.string();
    rlimit rl{};
    int max_fd = 4096;
    if (::getrlimit(RLIMIT_NOFILE, &rl) == 0 && rl.rlim_cur != RLIM_INFINITY)
        max_fd = static_cast<int>(std::min<rlim_t>(rl.rlim_cur, 65536));
    auto [err_read, err_write] = posix::make_pipe();
    const int slave_fd = slave.get();
    const int err_fd = err_write.get();

    const pid_t pid = ::fork();
    if (pid < 0)
        posix::throw_errno("fork");
    if (pid == 0) {
        sigset_t all;
        sigemptyset(&all);
        sigprocmask(SIG_SETMASK, &all, nullptr);
        struct sigaction dfl{};
        dfl.sa_handler = SIG_DFL;
        for (const int sig : {SIGPIPE, SIGINT, SIGQUIT, SIGTERM, SIGHUP, SIGCHLD, SIGTSTP, SIGTTIN, SIGTTOU})
            sigaction(sig, &dfl, nullptr);
        int err = 0;
        if (::setsid() < 0 || ::ioctl(slave_fd, TIOCSCTTY, 0) < 0)
            err = errno;
        if (err == 0 && (::dup2(slave_fd, 0) < 0 || ::dup2(slave_fd, 1) < 0 || ::dup2(slave_fd, 2) < 0))
            err = errno;
        if (err == 0) {
            for (int fd = 3; fd < max_fd; ++fd) {
                if (fd != err_fd)
                    ::close(fd);
            }
            if (::chdir(cwd.c_str()) != 0)
                err = errno;
        }
        if (err == 0) {
            ::execve(exe.c_str(), argv.data(), envp.data());
            err = errno;
        }
        (void)::write(err_fd, &err, sizeof(err));
        ::_exit(127);
    }

    slave.reset();
    err_write.reset();
    int child_err = 0;
    ssize_t got = -1;
    do {
        got = ::read(err_read.get(), &child_err, sizeof(child_err));
    } while (got < 0 && errno == EINTR);
    if (got == static_cast<ssize_t>(sizeof(child_err))) {
        int status = 0;
        while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {
        }
        throw std::system_error(child_err, std::generic_category(), "exec " + exe);
    }

    auto impl = std::make_unique<Impl>();
    impl->master = std::move(master);
    impl->pid = pid;
    impl->on_output = std::move(on_output);
    impl->on_exit = std::move(on_exit);
    auto [wake_read, wake_write] = posix::make_pipe();
    ::fcntl(wake_read.get(), F_SETFL, O_NONBLOCK);
    ::fcntl(wake_write.get(), F_SETFL, O_NONBLOCK);
    impl->wake_read = std::move(wake_read);
    impl->wake_write = std::move(wake_write);
    Impl* raw = impl.get();
    impl->waiter = std::thread([raw] { raw->waiter_loop(); });
    impl->reader = std::thread([raw] { raw->reader_loop(); });
    return std::unique_ptr<Pty>(new Pty(std::move(impl)));
}

void Pty::write(std::string_view bytes) {
    std::lock_guard close_lock(impl_->close_mutex);
    if (impl_->closed)
        throw std::system_error(EBADF, std::generic_category(), "pty closed");
    std::lock_guard lock(impl_->write_mutex);
    if (!posix::write_fully(impl_->master.get(), bytes.data(), bytes.size()))
        posix::throw_errno("write(pty)");
}

void Pty::resize(PtySize size) {
    std::lock_guard close_lock(impl_->close_mutex);
    if (impl_->closed)
        return;
    const winsize ws = to_winsize(size);
    if (::ioctl(impl_->master.get(), TIOCSWINSZ, &ws) != 0)
        posix::throw_errno("ioctl(TIOCSWINSZ)");
}

void Pty::interrupt() {
    write(std::string_view("\x03", 1));
}

void Pty::terminate() {
    impl_->signal_group(SIGKILL);
}

std::optional<int> Pty::exit_code() const {
    std::lock_guard lock(impl_->mutex);
    return impl_->exit_code;
}

std::optional<int> Pty::wait_exit(std::chrono::milliseconds timeout) const {
    std::unique_lock lock(impl_->mutex);
    impl_->cv.wait_for(lock, timeout, [&] { return impl_->exit_code.has_value(); });
    return impl_->exit_code;
}

std::uint32_t Pty::pid() const noexcept {
    return static_cast<std::uint32_t>(impl_->pid);
}

std::size_t Pty::live_processes() const {
    return posix::process_group_members(impl_->pid).size();
}

ForegroundInfo Pty::foreground() const {
    std::lock_guard close_lock(impl_->close_mutex);
    if (impl_->closed)
        return {};
    const pid_t group = ::tcgetpgrp(impl_->master.get());
    if (group <= 0)
        return {};
    const auto members = posix::process_group_members(group);
    if (members.empty())
        return {};
    // The group leader is the program the shell started; fall back to any member.
    const pid_t front =
        std::find(members.begin(), members.end(), group) != members.end() ? group : members.front();
    return posix::describe_process(front);
}

void Pty::close() {
    std::lock_guard close_lock(impl_->close_mutex);
    if (impl_->closed)
        return;
    Impl& s = *impl_;
    // Hang up the shell and let the reader take the last output while programs end.
    s.signal_group(SIGHUP);
    {
        std::unique_lock lock(s.mutex);
        s.cv.wait_for(lock, kDrainWait, [&] { return s.reader_done; });
        s.stop_requested = true;
        s.pause_requested = false;
        s.cv.notify_all();
    }
    s.wake();
    if (s.reader.joinable())
        s.reader.join();
    s.signal_group(SIGKILL);
    if (s.waiter.joinable())
        s.waiter.join();
    // SIGKILL is delivered asynchronously; return only when nothing of the terminal still runs.
    const auto deadline = std::chrono::steady_clock::now() + kKillWait;
    while (!posix::process_group_members(s.pid).empty() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    {
        std::lock_guard lock(s.write_mutex);
        s.master.reset();
    }
    s.closed = true;
}

bool Pty::closed() const {
    std::lock_guard lock(impl_->close_mutex);
    return impl_->closed;
}

bool Pty::pause_reader(std::chrono::milliseconds timeout) {
    Impl& s = *impl_;
    std::unique_lock lock(s.mutex);
    if (s.reader_done)
        return true;
    s.pause_requested = true;
    lock.unlock();
    s.wake();
    lock.lock();
    return s.cv.wait_for(lock, timeout, [&] { return s.paused || s.reader_done; });
}

void Pty::resume_reader() {
    std::lock_guard lock(impl_->mutex);
    impl_->pause_requested = false;
    impl_->cv.notify_all();
}

PtyCapabilities pty_capabilities() {
    PtyCapabilities caps;
    caps.answers_osc7501_query = true;  // a Unix pty passes every byte through
    caps.handover = false;              // design doc ADR-30: macOS handover comes in step 10
    return caps;
}

bool is_shell_name(std::string_view name) {
    static const std::set<std::string, std::less<>> kShells = {
        "pwsh", "powershell", "cmd",   "bash", "zsh", "fish", "sh", "dash",
        "nu",   "elvish",     "xonsh", "tcsh", "csh", "ksh",  "ion"};
    std::string n(name);
    std::transform(n.begin(), n.end(), n.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (n.size() > 4 && n.ends_with(".exe"))
        n.resize(n.size() - 4);
    if (!n.empty() && n.front() == '-')  // login shells: argv[0] "-zsh"
        n.erase(0, 1);
    return kShells.contains(n);
}

}  // namespace agentdesk::platform
