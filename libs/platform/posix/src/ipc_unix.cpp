// Local streams over Unix domain sockets. The socket lives in the per-user runtime folder (0700)
// and is itself 0600; both ends also check the peer's uid, so another account cannot connect or
// pose as the host.
#include <atomic>
#include <cstring>
#include <stdexcept>
#include <thread>

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include "agentdesk/platform/ipc_transport.hpp"
#include "agentdesk/platform/system.hpp"
#include "posix_util.hpp"
#include "process_impl.hpp"

namespace agentdesk::platform {

using posix::UniqueFd;

namespace {

sockaddr_un make_address(const std::string& path) {
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (path.empty() || path.size() >= sizeof(addr.sun_path))
        throw std::system_error(ENAMETOOLONG, std::generic_category(), "socket path: " + path);
    std::memcpy(addr.sun_path, path.c_str(), path.size() + 1);
    return addr;
}

UniqueFd new_socket() {
    UniqueFd fd(::socket(AF_UNIX, SOCK_STREAM, 0));
    if (!fd.valid())
        posix::throw_errno("socket");
    posix::set_cloexec(fd.get());
    const int one = 1;
    ::setsockopt(fd.get(), SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));  // EPIPE instead of SIGPIPE
    return fd;
}

bool peer_is_same_user(int fd) {
    uid_t uid = 0;
    gid_t gid = 0;
    return ::getpeereid(fd, &uid, &gid) == 0 && uid == ::geteuid();
}

std::uint32_t peer_pid(int fd) {
    pid_t pid = 0;
    socklen_t len = sizeof(pid);
    if (::getsockopt(fd, SOL_LOCAL, LOCAL_PEERPID, &pid, &len) != 0)
        return 0;
    return static_cast<std::uint32_t>(pid);
}

// Connects once; an invalid fd when nobody listens.
UniqueFd try_connect(const sockaddr_un& addr) {
    UniqueFd fd = new_socket();
    int r = 0;
    do {
        r = ::connect(fd.get(), reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
    } while (r != 0 && errno == EINTR);
    if (r != 0)
        return {};
    return fd;
}

class SocketStream final : public ByteStream {
public:
    explicit SocketStream(UniqueFd fd) : fd_(std::move(fd)) {}

    std::size_t read_some(std::span<char> buf) override {
        while (!cancelled_.load()) {
            const ssize_t n = ::read(fd_.get(), buf.data(), buf.size());
            if (n > 0)
                return static_cast<std::size_t>(n);
            if (n < 0 && errno == EINTR)
                continue;
            return 0;
        }
        return 0;
    }

    bool write_all(std::string_view bytes) override {
        if (cancelled_.load())
            return false;
        const char* p = bytes.data();
        std::size_t left = bytes.size();
        while (left > 0) {
            const ssize_t n = ::send(fd_.get(), p, left, 0);
            if (n < 0) {
                if (errno == EINTR)
                    continue;
                return false;
            }
            p += n;
            left -= static_cast<std::size_t>(n);
        }
        return true;
    }

    // shutdown() wakes a read blocked in another thread; the fd itself closes with the object.
    void cancel() override {
        if (!cancelled_.exchange(true))
            ::shutdown(fd_.get(), SHUT_RDWR);
    }

private:
    UniqueFd fd_;
    std::atomic<bool> cancelled_{false};
};

class ProcessStream final : public ByteStream {
public:
    explicit ProcessStream(std::unique_ptr<Process> child) : child_(std::move(child)) {}

    std::size_t read_some(std::span<char> buf) override {
        const int fd = child_->impl().out_read.get();
        while (fd >= 0 && !cancelled_.load()) {
            const ssize_t n = ::read(fd, buf.data(), buf.size());
            if (n > 0)
                return static_cast<std::size_t>(n);
            if (n < 0 && errno == EINTR)
                continue;
            return 0;
        }
        return 0;
    }

    bool write_all(std::string_view bytes) override { return !cancelled_.load() && child_->write(bytes); }

    // Killing the child closes its end of stdout, which ends a blocked read.
    void cancel() override {
        if (!cancelled_.exchange(true))
            child_->kill();
    }

private:
    std::unique_ptr<Process> child_;
    std::atomic<bool> cancelled_{false};
};

}  // namespace

std::string default_endpoint() {
    return (runtime_dir() / "host.sock").string();
}

struct StreamListener::Impl {
    std::string path;
    UniqueFd fd;
    UniqueFd wake_read;
    UniqueFd wake_write;
    std::atomic<bool> stopped{false};
    std::atomic<std::uint64_t> rejected{0};
};

StreamListener::StreamListener(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

StreamListener::~StreamListener() {
    stop();
    impl_->fd.reset();
    ::unlink(impl_->path.c_str());
}

std::unique_ptr<StreamListener> StreamListener::listen(const std::string& endpoint) {
    const sockaddr_un addr = make_address(endpoint);
    const auto parent = std::filesystem::path(endpoint).parent_path();
    if (!parent.empty() && !std::filesystem::exists(parent)) {
        std::filesystem::create_directories(parent);
        ::chmod(parent.c_str(), 0700);
    }
    UniqueFd fd = new_socket();
    // Sockets are created 0777 & ~umask: 0177 makes it 0600 whatever the process umask is.
    const mode_t old_mask = ::umask(0177);
    int r = ::bind(fd.get(), reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
    if (r != 0 && errno == EADDRINUSE) {
        // Someone listening means a live server; otherwise it is a crashed server's leftover.
        if (try_connect(addr).valid()) {
            ::umask(old_mask);
            throw std::system_error(EADDRINUSE, std::generic_category(), "endpoint in use: " + endpoint);
        }
        ::unlink(endpoint.c_str());
        r = ::bind(fd.get(), reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
    }
    const int bind_errno = errno;
    ::umask(old_mask);
    if (r != 0)
        throw std::system_error(bind_errno, std::generic_category(), "bind " + endpoint);
    if (::listen(fd.get(), 64) != 0)
        posix::throw_errno("listen");

    auto impl = std::make_unique<Impl>();
    impl->path = endpoint;
    impl->fd = std::move(fd);
    auto [wr, ww] = posix::make_pipe();
    ::fcntl(wr.get(), F_SETFL, O_NONBLOCK);
    ::fcntl(ww.get(), F_SETFL, O_NONBLOCK);
    impl->wake_read = std::move(wr);
    impl->wake_write = std::move(ww);
    return std::unique_ptr<StreamListener>(new StreamListener(std::move(impl)));
}

std::unique_ptr<ByteStream> StreamListener::accept(PeerIdentity* peer) {
    Impl& s = *impl_;
    while (!s.stopped.load()) {
        pollfd fds[2] = {{s.fd.get(), POLLIN, 0}, {s.wake_read.get(), POLLIN, 0}};
        const int r = ::poll(fds, 2, -1);
        if (r < 0) {
            if (errno == EINTR)
                continue;
            return nullptr;
        }
        if ((fds[1].revents & POLLIN) != 0 || s.stopped.load())
            return nullptr;
        if ((fds[0].revents & POLLIN) == 0)
            continue;
        UniqueFd client(::accept(s.fd.get(), nullptr, nullptr));
        if (!client.valid())
            continue;  // the client went away between poll and accept
        posix::set_cloexec(client.get());
        const int one = 1;
        ::setsockopt(client.get(), SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
        if (!peer_is_same_user(client.get())) {
            ++s.rejected;
            continue;
        }
        if (peer != nullptr) {
            peer->pid = peer_pid(client.get());
            peer->same_user = true;
        }
        return std::make_unique<SocketStream>(std::move(client));
    }
    return nullptr;
}

void StreamListener::stop() {
    if (!impl_->stopped.exchange(true)) {
        const char b = 1;
        (void)::write(impl_->wake_write.get(), &b, 1);
    }
}

std::uint64_t StreamListener::rejected_clients() const noexcept {
    return impl_->rejected.load();
}

const std::string& StreamListener::endpoint() const noexcept {
    return impl_->path;
}

std::unique_ptr<ByteStream> connect_stream(const std::string& endpoint, std::chrono::milliseconds timeout) {
    const sockaddr_un addr = make_address(endpoint);
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    for (;;) {
        UniqueFd fd = try_connect(addr);
        if (fd.valid()) {
            if (!peer_is_same_user(fd.get()))
                return nullptr;  // never talk to a server run by someone else
            return std::make_unique<SocketStream>(std::move(fd));
        }
        if (std::chrono::steady_clock::now() >= deadline)
            return nullptr;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}

std::unique_ptr<ByteStream> process_stream(std::unique_ptr<Process> child) {
    if (!child || !child->impl().out_read.valid() || !child->impl().in_write.valid())
        throw std::invalid_argument("process_stream needs a child with stdin and stdout pipes");
    return std::make_unique<ProcessStream>(std::move(child));
}

struct InstanceLock::Impl {
    UniqueFd fd;
};

InstanceLock::InstanceLock(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

InstanceLock::~InstanceLock() = default;  // closing the fd releases the lock

std::unique_ptr<InstanceLock> InstanceLock::try_acquire(const std::string& name) {
    const auto path = runtime_dir() / (name + ".lock");
    UniqueFd fd(::open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600));
    if (!fd.valid())
        posix::throw_errno("open(lock)");
    if (::flock(fd.get(), LOCK_EX | LOCK_NB) != 0) {
        if (errno == EWOULDBLOCK)
            return nullptr;
        posix::throw_errno("flock");
    }
    auto impl = std::make_unique<Impl>();
    impl->fd = std::move(fd);
    return std::unique_ptr<InstanceLock>(new InstanceLock(std::move(impl)));
}

}  // namespace agentdesk::platform
