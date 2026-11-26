// Pty on a POSIX pseudo terminal: the child leads its own session with the terminal as its
// controlling tty, so the shell's process group is what close() and terminate() end.
#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>

#include <fcntl.h>
#include <libproc.h>
#include <poll.h>
#include <signal.h>
#include <sys/event.h>
#include <sys/ioctl.h>
#include <sys/proc.h>
#include <sys/proc_info.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#include "agentdesk/platform/pty.hpp"
#include "agentdesk/platform/pty_handles.hpp"
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

constexpr std::uintptr_t kCancelWaiter = 1;  // EVFILT_USER ident that stops the waiter

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
    UniqueFd kq;           // the waiter's kqueue: the root's exit, or a cancel
    bool adopted = false;  // taken over from another host: the root is not our child
    // When the root started. Its pid is also the terminal's group and session id, but only while
    // no other process has taken the pid over; see root_pid_reused().
    std::optional<std::uint64_t> root_start;

    mutable std::mutex mutex;
    mutable std::condition_variable cv;
    bool reader_done = false;
    bool pause_requested = false;
    bool paused = false;
    bool stop_requested = false;
    std::optional<int> exit_code;

    dev_t tty = 0;  // the slave's device: every process of the terminal has it as its controlling tty

    std::mutex write_mutex;          // one writer at a time; never held while waiting on close_mutex
    mutable std::mutex close_mutex;  // lock order: close_mutex before write_mutex
    bool closed = false;
    std::atomic<bool> closing{false};  // a write waiting for room gives up once set

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

    // Watches the root's exit with kqueue, so release_after_handover() can stop watching a
    // process that keeps running. Our own child is reaped with waitpid() for its status; an
    // adopted root belongs to someone else, so its exit code stays unknown (-1).
    void waiter_loop() noexcept {
        struct kevent change{};
        EV_SET(&change, static_cast<uintptr_t>(pid), EVFILT_PROC, EV_ADD | EV_ONESHOT, NOTE_EXIT, 0, nullptr);
        ::kevent(kq.get(), &change, 1, nullptr, 0, nullptr);  // ESRCH: already gone, checked below
        int code = -1;
        for (;;) {
            if (!adopted) {
                int status = 0;
                const pid_t r = ::waitpid(pid, &status, WNOHANG);
                if (r == pid) {
                    code = posix::exit_code_from_status(status);
                    break;
                }
                if (r < 0 && errno != EINTR)
                    break;
            } else if (::kill(pid, 0) != 0 && errno == ESRCH) {
                break;
            }
            struct kevent event{};
            // A deadline, so the checks above also run when kevent itself fails.
            const timespec step{1, 0};
            const int n = ::kevent(kq.get(), nullptr, 0, &event, 1, &step);
            if (n < 0 && errno != EINTR)
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (n == 1 && event.filter == EVFILT_USER)
                return;  // cancelled: the program keeps running in another host
            if (n == 1 && event.filter == EVFILT_PROC && adopted)
                break;
        }
        {
            std::lock_guard lock(mutex);
            exit_code = code;
            cv.notify_all();
        }
        if (on_exit) {
            try {
                on_exit();
            } catch (...) {
            }
        }
    }

    void cancel_waiter() const {
        struct kevent ev{};
        EV_SET(&ev, kCancelWaiter, EVFILT_USER, 0, NOTE_TRIGGER, 0, nullptr);
        ::kevent(kq.get(), &ev, 1, nullptr, 0, nullptr);
    }

    // Starts the reader (paused when asked) and the waiter.
    void start_threads(bool paused_reader) {
        // Non-blocking: a program that stops reading its input must not hold a writer (and with it
        // close()) forever; write() waits for room with poll() instead.
        ::fcntl(master.get(), F_SETFL, ::fcntl(master.get(), F_GETFL) | O_NONBLOCK);
        {
            std::lock_guard lock(g_ptsname_mutex);
            struct stat st {};
            if (const char* name = ::ptsname(master.get()); name != nullptr && ::stat(name, &st) == 0)
                tty = st.st_rdev;
        }
        kq.reset(::kqueue());
        if (!kq.valid())
            posix::throw_errno("kqueue");
        posix::set_cloexec(kq.get());
        struct kevent ev{};
        EV_SET(&ev, kCancelWaiter, EVFILT_USER, EV_ADD | EV_CLEAR, 0, 0, nullptr);
        if (::kevent(kq.get(), &ev, 1, nullptr, 0, nullptr) != 0)
            posix::throw_errno("kevent(EVFILT_USER)");
        auto [rd, wr] = posix::make_pipe();
        ::fcntl(rd.get(), F_SETFL, O_NONBLOCK);
        ::fcntl(wr.get(), F_SETFL, O_NONBLOCK);
        wake_read = std::move(rd);
        wake_write = std::move(wr);
        pause_requested = paused_reader;
        waiter = std::thread([this] { waiter_loop(); });
        reader = std::thread([this] { reader_loop(); });
    }

    // True once the root's pid names another process: the root ended, was reaped, and the pid went
    // to a new program, which may lead a session or group of its own (another terminal's shell).
    // From then on the pid says nothing about this terminal and nothing is signalled through it.
    bool root_pid_reused() const {
        const auto now = posix::start_time(pid);
        return now && root_start && *now != *root_start;
    }

    // Every process of the terminal: the root's process group (its pid: it called setsid()), the
    // jobs a shell put in groups of their own, and, by session, those jobs after the root exited.
    // Listed before any signal goes out: once the root ends, the terminal no longer lists them.
    // Returns the processes it signalled.
    std::vector<pid_t> signal_all(int sig) const {
        const std::vector<pid_t> members = remaining();
        if (!root_pid_reused())
            ::killpg(pid, sig);  // ESRCH: the group is already empty
        for (const pid_t p : members)
            ::kill(p, sig);
        return members;
    }

    // The processes still running in the terminal, its root's group included. Scans every process
    // on the system (for the session), so callers that poll check the returned pids instead.
    std::vector<pid_t> remaining() const {
        const bool own_pid = !root_pid_reused();
        std::vector<pid_t> all;
        if (own_pid)
            all = posix::process_group_members(pid);
        const auto add = [&](const std::vector<pid_t>& more) {
            for (const pid_t p : more)
                if (std::find(all.begin(), all.end(), p) == all.end())
                    all.push_back(p);
        };
        add(posix::terminal_members(tty));
        if (own_pid)
            add(posix::session_members(pid));
        return all;
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
    impl->root_start = posix::start_time(pid);
    impl->on_output = std::move(on_output);
    impl->on_exit = std::move(on_exit);
    impl->start_threads(false);
    return std::unique_ptr<Pty>(new Pty(std::move(impl)));
}

// Holds only write_mutex: resize(), foreground() and close() never wait for a program that does
// not read its input. While the terminal's input queue is full it waits for room in 50 ms steps,
// and gives up once close() started.
void Pty::write(std::string_view bytes) {
    Impl& s = *impl_;
    std::lock_guard lock(s.write_mutex);
    while (!bytes.empty()) {
        if (s.closing.load() || !s.master.valid())
            throw std::system_error(EBADF, std::generic_category(), "pty closed");
        const ssize_t n = ::write(s.master.get(), bytes.data(), bytes.size());
        if (n > 0) {
            bytes.remove_prefix(static_cast<std::size_t>(n));
            continue;
        }
        if (n < 0 && errno == EINTR)
            continue;
        if (n < 0 && errno == EAGAIN) {
            pollfd p{s.master.get(), POLLOUT, 0};
            ::poll(&p, 1, 50);
            continue;
        }
        posix::throw_errno("write(pty)");
    }
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
    impl_->signal_all(SIGKILL);
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
    return impl_->remaining().size();
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
    s.closing = true;  // a write waiting for room stops
    // Hang up the terminal's programs and let the reader take the last output while they end.
    std::vector<pid_t> signalled = s.signal_all(SIGHUP);
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
    for (const pid_t p : s.signal_all(SIGKILL))
        if (std::find(signalled.begin(), signalled.end(), p) == signalled.end())
            signalled.push_back(p);
    if (s.waiter.joinable())
        s.waiter.join();
    // SIGKILL is delivered asynchronously; return only when nothing of the terminal still runs.
    // Only the pids signalled above are polled: a full scan every 5 ms would cost thousands of
    // system calls each time on a busy machine.
    const auto deadline = std::chrono::steady_clock::now() + kKillWait;
    const auto any_left = [&] { return std::ranges::any_of(signalled, posix::is_running); };
    while (any_left() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    {
        std::lock_guard lock(s.write_mutex);
        s.master.reset();
    }
    s.closed = true;
}

bool Pty::exit_code_known() const {
    const auto code = exit_code();
    return !code || *code != -1;  // real codes are 0..255
}

std::string Pty::foreground_program() const {
    return foreground().program;
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

std::string Pty::read_available(std::size_t max) {
    std::string out;
    std::array<char, 16 * 1024> buf{};
    while (out.size() < max) {
        pollfd p{impl_->master.get(), POLLIN, 0};
        if (::poll(&p, 1, 0) != 1 || (p.revents & POLLIN) == 0)
            break;
        const ssize_t n = ::read(impl_->master.get(), buf.data(), std::min(buf.size(), max - out.size()));
        if (n <= 0)
            break;
        out.append(buf.data(), static_cast<std::size_t>(n));
    }
    return out;
}

std::optional<PtyHandles> Pty::handover_handles(std::string* why) const {
    if (closed()) {
        if (why != nullptr)
            *why = "terminal already closed";
        return std::nullopt;
    }
    return PtyHandles{impl_->master.get(), pid()};
}

void Pty::release_after_handover() {
    std::lock_guard close_lock(impl_->close_mutex);
    if (impl_->closed)
        return;
    Impl& s = *impl_;
    s.cancel_waiter();
    if (s.waiter.joinable())
        s.waiter.join();
    {
        std::lock_guard lock(s.mutex);
        s.stop_requested = true;
        s.pause_requested = false;
        s.cv.notify_all();
    }
    s.wake();
    if (s.reader.joinable())
        s.reader.join();
    {
        std::lock_guard lock(s.write_mutex);
        s.master.reset();  // the new host holds its own copy; nothing is signalled
    }
    s.closed = true;
}

std::unique_ptr<Pty> Pty::adopt(const PtyHandles& handles, PtyOutputSink on_output, PtyExitSink on_exit) {
    UniqueFd master(handles.master_fd);
    const std::uint32_t pid = handles.pid;
    const auto fail = [](const std::string& why) { throw std::runtime_error("adopt terminal: " + why); };
    const auto root = static_cast<pid_t>(pid);
    {
        std::lock_guard lock(g_ptsname_mutex);
        if (!master.valid() || ::ptsname(master.get()) == nullptr)
            fail("not a pty master");
    }
    posix::set_cloexec(master.get());
    proc_bsdinfo info{};
    if (root <= 0 ||
        ::proc_pidinfo(root, PROC_PIDTBSDINFO, 0, &info, sizeof(info)) != static_cast<int>(sizeof(info)) ||
        info.pbi_status == SZOMB)
        fail("program " + std::to_string(pid) + " is not running");
    if (info.pbi_uid != ::geteuid())
        fail("program " + std::to_string(pid) + " belongs to another user");
    if (::getsid(root) != root)
        fail("program " + std::to_string(pid) + " does not lead its own session");
    const pid_t group = ::tcgetpgrp(master.get());
    if (group <= 0 || ::getsid(group) != root)
        fail("the terminal is not owned by program " + std::to_string(pid));

    auto impl = std::make_unique<Impl>();
    impl->master = std::move(master);
    impl->pid = root;
    impl->adopted = true;
    impl->root_start = info.pbi_start_tvsec * 1'000'000ULL + info.pbi_start_tvusec;
    impl->on_output = std::move(on_output);
    impl->on_exit = std::move(on_exit);
    impl->start_threads(true);
    return std::unique_ptr<Pty>(new Pty(std::move(impl)));
}

PtyCapabilities pty_capabilities() {
    PtyCapabilities caps;
    caps.answers_osc7501_query = true;  // a Unix pty passes every byte through
    caps.handover = true;
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
