// ipc_transport.hpp on Windows: Named Pipes with a DACL for the current user, remote clients
// rejected and the peer's user checked on both ends; and the one-host-per-user lock.
#include "agentdesk/platform/ipc_transport.hpp"
#include "agentdesk/platform/win/error.hpp"
// sddl.h needs windows.h first (included through win/error.hpp).
#include <sddl.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <mutex>
#include <stdexcept>

#include "agentdesk/platform/process.hpp"
#include "agentdesk/platform/win/unique_handle.hpp"
#include "agentdesk/platform/win/user.hpp"

namespace agentdesk::platform {

using win::UniqueHandle;

namespace win {

namespace {

struct LocalFreeDeleter {
    void operator()(void* p) const noexcept { ::LocalFree(p); }
};

// SID of the user owning `process`, as a string.
std::string process_user_sid(HANDLE process) {
    HANDLE token_raw = nullptr;
    if (!::OpenProcessToken(process, TOKEN_QUERY, &token_raw))
        throw_last_error("OpenProcessToken");
    const UniqueHandle token(token_raw);
    DWORD size = 0;
    ::GetTokenInformation(token.get(), TokenUser, nullptr, 0, &size);
    std::vector<std::byte> buf(size);
    if (!::GetTokenInformation(token.get(), TokenUser, buf.data(), size, &size))
        throw_last_error("GetTokenInformation(TokenUser)");
    // Win32 boundary: TOKEN_USER lives in the buffer we sized for it.
    const auto* user = reinterpret_cast<const TOKEN_USER*>(buf.data());
    LPWSTR text = nullptr;
    if (!::ConvertSidToStringSidW(user->User.Sid, &text))
        throw_last_error("ConvertSidToStringSidW");
    const std::unique_ptr<wchar_t, LocalFreeDeleter> guard(text);
    return to_utf8(std::wstring_view(text));
}

bool pid_belongs_to_current_user(DWORD pid) {
    const UniqueHandle process(::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
    if (!process)
        return false;
    try {
        return process_user_sid(process.get()) == user_sid();
    } catch (const std::system_error&) {
        return false;
    }
}

UniqueHandle make_event() {
    UniqueHandle h(::CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!h)
        throw_last_error("CreateEventW");
    return h;
}

constexpr DWORD kPipeBuffer = 64 * 1024;

// One end of a pipe, overlapped, so a read and a write can be in flight together and cancel() wakes
// both. A server end disconnects its client when cancelled: the Connection that owns it may live on.
class PipeStream final : public ByteStream {
public:
    PipeStream(UniqueHandle pipe, bool server)
        : pipe_(std::move(pipe)),
          server_(server),
          cancel_event_(make_event()),
          read_event_(make_event()),
          write_event_(make_event()) {}

    std::size_t read_some(std::span<char> buf) override {
        const auto len = static_cast<DWORD>(std::min<std::size_t>(buf.size(), 1u << 20));
        return transfer(read_event_.get(), [&](OVERLAPPED* ov) {
            return ::ReadFile(pipe_.get(), buf.data(), len, nullptr, ov);
        });
    }

    bool write_all(std::string_view bytes) override {
        while (!bytes.empty()) {
            const auto len = static_cast<DWORD>(std::min<std::size_t>(bytes.size(), 1u << 24));
            const std::size_t n = transfer(write_event_.get(), [&](OVERLAPPED* ov) {
                return ::WriteFile(pipe_.get(), bytes.data(), len, nullptr, ov);
            });
            if (n == 0)
                return false;
            bytes.remove_prefix(n);
        }
        return !cancelled_.load();
    }

    void cancel() override {
        if (cancelled_.exchange(true))
            return;
        ::SetEvent(cancel_event_.get());
        ::CancelIoEx(pipe_.get(), nullptr);
        // No FlushFileBuffers: it waits until the client has read everything, forever when the
        // client stopped reading, and that would block the server's stop and the handover.
        if (server_)
            ::DisconnectNamedPipe(pipe_.get());
    }

private:
    // Starts an operation and waits for it or a cancel: the bytes moved, 0 when it failed or was
    // cancelled. The kernel is done with the OVERLAPPED and the buffer afterwards.
    template <class Start>
    std::size_t transfer(HANDLE event, Start start) {
        if (cancelled_.load())
            return 0;
        OVERLAPPED ov{};
        ov.hEvent = event;
        if (!start(&ov) && ::GetLastError() != ERROR_IO_PENDING)
            return 0;
        const std::array<HANDLE, 2> waits = {event, cancel_event_.get()};
        if (::WaitForMultipleObjects(2, waits.data(), FALSE, INFINITE) != WAIT_OBJECT_0)
            ::CancelIoEx(pipe_.get(), &ov);
        DWORD n = 0;
        return ::GetOverlappedResult(pipe_.get(), &ov, &n, TRUE) ? n : 0;
    }

    UniqueHandle pipe_;
    bool server_;
    UniqueHandle cancel_event_;
    UniqueHandle read_event_;
    UniqueHandle write_event_;
    std::atomic<bool> cancelled_{false};
};

class ProcessStream final : public ByteStream {
public:
    explicit ProcessStream(std::shared_ptr<Process> child) : child_(std::move(child)) {}

    std::size_t read_some(std::span<char> buf) override {
        if (cancelled_.load())
            return 0;
        const auto chunk = child_->read_some(buf.size());
        if (!chunk)
            return 0;
        std::memcpy(buf.data(), chunk->data(), chunk->size());
        return chunk->size();
    }

    bool write_all(std::string_view bytes) override { return !cancelled_.load() && child_->write(bytes); }

    // Killing the child closes its end of stdout, which ends a blocked read.
    void cancel() override {
        if (!cancelled_.exchange(true))
            child_->kill();
    }

private:
    std::shared_ptr<Process> child_;
    std::atomic<bool> cancelled_{false};
};

}  // namespace

std::string user_sid() {
    static const std::string sid = process_user_sid(::GetCurrentProcess());
    return sid;
}

std::uint32_t logon_session() {
    DWORD session = 0;
    if (!::ProcessIdToSessionId(::GetCurrentProcessId(), &session))
        throw_last_error("ProcessIdToSessionId");
    return session;
}

}  // namespace win

std::string default_endpoint() {
    return "\\\\.\\pipe\\AgentDesk-" + win::user_sid() + "-s" + std::to_string(win::logon_session());
}

std::string endpoint_named(const std::string& name) {
    return name;
}

bool endpoint_in_use(const std::system_error& e) {
    return e.code().value() == ERROR_ACCESS_DENIED;
}

// ---------------------------------------------------------------- StreamListener

struct StreamListener::Impl {
    std::string name;
    UniqueHandle next;  // the pipe instance the next client connects to
    UniqueHandle stop_event;
    std::atomic<std::uint64_t> rejected{0};

    void create_instance(bool first) {
        const std::wstring sddl = L"D:P(A;;GA;;;SY)(A;;GA;;;" + to_wide(win::user_sid()) + L")";
        PSECURITY_DESCRIPTOR sd_raw = nullptr;
        if (!::ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &sd_raw,
                                                                    nullptr))
            win::throw_last_error("ConvertStringSecurityDescriptorToSecurityDescriptorW");
        const std::unique_ptr<void, win::LocalFreeDeleter> sd(sd_raw);
        SECURITY_ATTRIBUTES sa{sizeof(sa), sd.get(), FALSE};
        const DWORD open =
            PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | (first ? FILE_FLAG_FIRST_PIPE_INSTANCE : 0);
        const DWORD mode = PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS;
        HANDLE h = ::CreateNamedPipeW(to_wide(name).c_str(), open, mode, PIPE_UNLIMITED_INSTANCES,
                                      win::kPipeBuffer, win::kPipeBuffer, 0, &sa);
        if (h == INVALID_HANDLE_VALUE)
            win::throw_last_error("CreateNamedPipeW");
        next.reset(h);
    }

    [[nodiscard]] bool stopped() const { return ::WaitForSingleObject(stop_event.get(), 0) == WAIT_OBJECT_0; }

    // True when a client connected to `next`; false when stopping or the connect failed.
    bool wait_for_client(HANDLE connect_event) const {
        OVERLAPPED ov{};
        ov.hEvent = connect_event;
        ::ResetEvent(connect_event);
        if (::ConnectNamedPipe(next.get(), &ov))
            return true;
        const DWORD err = ::GetLastError();
        if (err == ERROR_PIPE_CONNECTED)
            return true;
        if (err != ERROR_IO_PENDING)
            return false;
        const std::array<HANDLE, 2> waits = {connect_event, stop_event.get()};
        DWORD ignored = 0;
        if (::WaitForMultipleObjects(2, waits.data(), FALSE, INFINITE) != WAIT_OBJECT_0) {
            ::CancelIoEx(next.get(), &ov);
            ::GetOverlappedResult(next.get(), &ov, &ignored, TRUE);
            return false;
        }
        return ::GetOverlappedResult(next.get(), &ov, &ignored, FALSE) != FALSE;
    }
};

StreamListener::StreamListener(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

StreamListener::~StreamListener() {
    stop();
}

std::unique_ptr<StreamListener> StreamListener::listen(const std::string& endpoint) {
    auto impl = std::make_unique<Impl>();
    impl->name = endpoint;
    impl->stop_event = win::make_event();
    impl->create_instance(true);  // ERROR_ACCESS_DENIED: someone else owns the name
    return std::unique_ptr<StreamListener>(new StreamListener(std::move(impl)));
}

std::unique_ptr<ByteStream> StreamListener::accept(PeerIdentity* peer) {
    Impl& s = *impl_;
    const UniqueHandle connect_event = win::make_event();
    while (s.next) {
        const bool connected = s.wait_for_client(connect_event.get());
        if (s.stopped())
            return nullptr;
        UniqueHandle accepted = std::move(s.next);
        try {
            s.create_instance(false);
        } catch (const std::system_error&) {
            return nullptr;  // cannot accept more clients; existing ones keep running
        }
        if (!connected)
            continue;
        ULONG pid = 0;
        // Second line of defence after the DACL: same user only.
        if (!::GetNamedPipeClientProcessId(accepted.get(), &pid) || !win::pid_belongs_to_current_user(pid)) {
            ++s.rejected;
            ::DisconnectNamedPipe(accepted.get());
            continue;
        }
        if (peer != nullptr) {
            peer->pid = pid;
            peer->same_user = true;
        }
        return std::make_unique<win::PipeStream>(std::move(accepted), true);
    }
    return nullptr;
}

void StreamListener::stop() {
    ::SetEvent(impl_->stop_event.get());
}

void StreamListener::close_socket() {
    impl_->next.reset();
}

std::uint64_t StreamListener::rejected_clients() const noexcept {
    return impl_->rejected.load();
}

const std::string& StreamListener::endpoint() const noexcept {
    return impl_->name;
}

std::unique_ptr<ByteStream> connect_stream(const std::string& endpoint, std::chrono::milliseconds timeout) {
    const std::wstring name = to_wide(endpoint);
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    for (;;) {
        // SECURITY_IDENTIFICATION: the server may identify us but not act as us.
        HANDLE h =
            ::CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                          FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            UniqueHandle pipe(h);
            ULONG server_pid = 0;
            if (!::GetNamedPipeServerProcessId(pipe.get(), &server_pid) ||
                !win::pid_belongs_to_current_user(server_pid))
                return nullptr;  // a pipe with our name owned by someone else: refuse it
            return std::make_unique<win::PipeStream>(std::move(pipe), false);
        }
        const DWORD err = ::GetLastError();
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline)
            return nullptr;
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
        if (err == ERROR_PIPE_BUSY)
            ::WaitNamedPipeW(name.c_str(), static_cast<DWORD>(std::min<long long>(left, 1000)));
        else
            ::Sleep(static_cast<DWORD>(std::min<long long>(left, 50)));  // not created yet
    }
}

std::unique_ptr<ByteStream> process_stream(std::shared_ptr<Process> child) {
    if (!child)
        throw std::invalid_argument("process_stream needs a child");
    return std::make_unique<win::ProcessStream>(std::move(child));
}

// ---------------------------------------------------------------- InstanceLock

struct InstanceLock::Impl {
    UniqueHandle mutex;
};

InstanceLock::InstanceLock(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

InstanceLock::~InstanceLock() = default;  // the name goes with its last handle

std::unique_ptr<InstanceLock> InstanceLock::try_acquire(const std::string& name, bool taking_over) {
    const std::wstring full = to_wide("Local\\AgentDesk-" + name + "-" + win::user_sid() + "-s" +
                                      std::to_string(win::logon_session()));
    auto impl = std::make_unique<Impl>();
    impl->mutex.reset(::CreateMutexW(nullptr, FALSE, full.c_str()));
    if (!impl->mutex || (::GetLastError() == ERROR_ALREADY_EXISTS && !taking_over))
        return nullptr;
    return std::unique_ptr<InstanceLock>(new InstanceLock(std::move(impl)));
}

}  // namespace agentdesk::platform
