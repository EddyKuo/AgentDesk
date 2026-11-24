// The handover channel on macOS (ADR-30, docs/design/platform-abstraction.md 5.5): one private
// socketpair, whose other end the new host gets as fd 3 (`agent-host --takeover <old pid>`). The
// state goes over it, then each live terminal's pty master as SCM_RIGHTS, in order. The new host
// checks each one (a pty master; the root is alive, ours, leads its own session, and that session
// owns the terminal) and never takes a process by a PID alone.
#include <array>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include "agentdesk/host/handover.hpp"
#include "agentdesk/platform/system.hpp"
#include "handover_os.hpp"

extern char** environ;  // NOLINT(readability-redundant-declaration): not declared by unistd.h on macOS

namespace agentdesk::host {

using platform::PtyHandles;

namespace {

constexpr int kChannelFd = 3;  // the new host's end of the channel

bool channel_write_all(int fd, std::string_view bytes) {
    while (!bytes.empty()) {
        const ssize_t n = ::send(fd, bytes.data(), bytes.size(), 0);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return false;
        }
        bytes.remove_prefix(static_cast<std::size_t>(n));
    }
    return true;
}

bool channel_read_exact(int fd, char* out, std::size_t len) {
    while (len > 0) {
        const ssize_t n = ::read(fd, out, len);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return false;
        out += n;
        len -= static_cast<std::size_t>(n);
    }
    return true;
}

// One line ("ready ...", "error ...", "go"); nullopt when the socket ended first.
std::optional<std::string> channel_read_line(int fd) {
    std::string line;
    for (;;) {
        char c = 0;
        if (!channel_read_exact(fd, &c, 1))
            return std::nullopt;
        if (c == '\n')
            return line;
        if (line.size() < 4096)
            line.push_back(c);
    }
}

// One descriptor with a one-byte payload.
bool send_fd(int channel, int fd) {
    char byte = 'F';
    iovec iov{&byte, 1};
    alignas(cmsghdr) std::array<char, CMSG_SPACE(sizeof(int))> control{};
    msghdr msg{};
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = control.data();
    msg.msg_controllen = control.size();
    cmsghdr* c = CMSG_FIRSTHDR(&msg);
    c->cmsg_level = SOL_SOCKET;
    c->cmsg_type = SCM_RIGHTS;
    c->cmsg_len = CMSG_LEN(sizeof(int));
    std::memcpy(CMSG_DATA(c), &fd, sizeof(int));
    for (;;) {
        const ssize_t n = ::sendmsg(channel, &msg, 0);
        if (n == 1)
            return true;
        if (n < 0 && errno == EINTR)
            continue;
        return false;
    }
}

// The descriptor sent with the next byte, or -1.
int receive_fd(int channel) {
    char byte = 0;
    iovec iov{&byte, 1};
    alignas(cmsghdr) std::array<char, CMSG_SPACE(sizeof(int))> control{};
    msghdr msg{};
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = control.data();
    msg.msg_controllen = control.size();
    ssize_t n = -1;
    do {
        n = ::recvmsg(channel, &msg, 0);
    } while (n < 0 && errno == EINTR);
    // Exactly one descriptor is expected; every one that arrived is ours to close otherwise.
    int fd = -1;
    bool extra = (msg.msg_flags & MSG_CTRUNC) != 0;
    for (cmsghdr* c = CMSG_FIRSTHDR(&msg); c != nullptr; c = CMSG_NXTHDR(&msg, c)) {
        if (c->cmsg_level != SOL_SOCKET || c->cmsg_type != SCM_RIGHTS)
            continue;
        const std::size_t count = (c->cmsg_len - CMSG_LEN(0)) / sizeof(int);
        for (std::size_t i = 0; i < count; ++i) {
            int got = -1;
            std::memcpy(&got, CMSG_DATA(c) + i * sizeof(int), sizeof(int));
            if (fd < 0) {
                fd = got;
            } else {
                ::close(got);
                extra = true;
            }
        }
    }
    if (n != 1 || extra) {
        if (fd >= 0)
            ::close(fd);
        return -1;
    }
    if (fd >= 0)
        ::fcntl(fd, F_SETFD, FD_CLOEXEC);
    return fd;
}

bool process_alive(pid_t pid) {
    return ::kill(pid, 0) == 0 || errno == EPERM;
}

// The new agent-host with its end of the channel as fd 3 and nothing else inherited, in its own
// session (it outlives us), started in the home folder so updates can replace the program.
pid_t spawn_successor(const std::filesystem::path& exe, const std::string& pipe_name,
                      const std::filesystem::path& data_dir, int child_end) {
    std::vector<std::string> argv_strings = {
        exe.string(), "--takeover",     std::to_string(::getpid()), "--pipe", pipe_name,
        "--data-dir", data_dir.string()};
    std::vector<char*> argv;
    for (auto& a : argv_strings)
        argv.push_back(a.data());
    argv.push_back(nullptr);
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&actions, 1, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&actions, 2, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_adddup2(&actions, child_end, 3);
    const std::string home = platform::home_dir().string();
    posix_spawn_file_actions_addchdir_np(&actions, home.c_str());
    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    sigset_t all;
    sigfillset(&all);
    posix_spawnattr_setsigdefault(&attr, &all);
    sigset_t none;
    sigemptyset(&none);
    posix_spawnattr_setsigmask(&attr, &none);
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_CLOEXEC_DEFAULT | POSIX_SPAWN_SETSID | POSIX_SPAWN_SETSIGDEF |
                                        POSIX_SPAWN_SETSIGMASK);
    pid_t pid = -1;
    const int err = ::posix_spawn(&pid, exe.c_str(), &actions, &attr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attr);
    if (err != 0)
        throw std::system_error(err, std::generic_category(), "posix_spawn(new agent-host)");
    return pid;
}

}  // namespace

// ---------------------------------------------------------------- the old host's side

struct NewHost::Impl {
    pid_t pid = -1;
    int channel = -1;
    std::vector<int> terminals;  // pty masters to send, still owned by their sessions
    ~Impl() {
        if (channel >= 0)
            ::close(channel);
    }
};

NewHost::NewHost(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

NewHost::~NewHost() = default;

std::unique_ptr<NewHost> NewHost::start(const std::filesystem::path& exe, const std::string& pipe_name,
                                        const std::filesystem::path& data_dir) {
    // Our end, and the new host's end that becomes its fd 3.
    std::array<int, 2> pair{-1, -1};
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, pair.data()) != 0)
        throw std::system_error(errno, std::generic_category(), "cannot create the handover channel");
    ::fcntl(pair[0], F_SETFD, FD_CLOEXEC);
    ::fcntl(pair[1], F_SETFD, FD_CLOEXEC);
    const int one = 1;
    ::setsockopt(pair[0], SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
    auto impl = std::make_unique<Impl>();
    impl->channel = pair[0];
    try {
        impl->pid = spawn_successor(exe, pipe_name, data_dir, pair[1]);
    } catch (...) {
        ::close(pair[1]);
        throw;
    }
    ::close(pair[1]);  // the new host has its copy
    return std::unique_ptr<NewHost>(new NewHost(std::move(impl)));
}

bool NewHost::write_all(std::string_view bytes) {
    return channel_write_all(impl_->channel, bytes);
}

std::optional<std::string> NewHost::read_line() {
    return channel_read_line(impl_->channel);
}

void NewHost::add_terminal(PtyHandles& h) {
    impl_->terminals.push_back(h.master_fd);
}

void NewHost::write_handles(terminal::ByteWriter& w, const PtyHandles& h) {
    w.u32(h.pid);
}

bool NewHost::send_terminals() {
    for (const int fd : impl_->terminals)
        if (!send_fd(impl_->channel, fd))
            return false;
    return true;
}

void NewHost::kill() {
    ::kill(impl_->pid, SIGKILL);
}

void NewHost::end() {
    kill();
    int status = 0;
    while (::waitpid(impl_->pid, &status, 0) < 0 && errno == EINTR) {
    }
}

void NewHost::close_channel() {
    if (impl_->channel >= 0)
        ::close(std::exchange(impl_->channel, -1));
}

// ---------------------------------------------------------------- the new host's side

struct OldHost::Impl {
    pid_t pid = 0;
    int channel = kChannelFd;
    ~Impl() { ::close(channel); }
};

OldHost::OldHost(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

OldHost::~OldHost() = default;

std::unique_ptr<OldHost> OldHost::from_takeover(const std::string& argument) {
    ::fcntl(kChannelFd, F_SETFD, FD_CLOEXEC);
    auto impl = std::make_unique<Impl>();
    impl->pid = static_cast<pid_t>(std::strtoul(argument.c_str(), nullptr, 10));
    return std::unique_ptr<OldHost>(new OldHost(std::move(impl)));
}

bool OldHost::read_exact(char* out, std::size_t len) {
    return channel_read_exact(impl_->channel, out, len);
}

bool OldHost::write_all(std::string_view bytes) {
    return channel_write_all(impl_->channel, bytes);
}

std::optional<std::string> OldHost::read_line() {
    return channel_read_line(impl_->channel);
}

// The terminals' pty masters follow the state, in the order it lists them.
std::optional<PtyHandles> OldHost::read_handles(terminal::ByteReader& r) {
    PtyHandles h;
    h.pid = r.u32();
    h.master_fd = receive_fd(impl_->channel);
    if (h.master_fd < 0)
        return std::nullopt;
    return h;
}

bool OldHost::wait_exit(std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (process_alive(impl_->pid)) {
        if (std::chrono::steady_clock::now() >= deadline)
            return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return true;
}

}  // namespace agentdesk::host
