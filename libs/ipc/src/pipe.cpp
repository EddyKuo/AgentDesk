#include "agentdesk/ipc/pipe.hpp"

// sddl.h needs windows.h first (included through pipe.hpp).
#include <sddl.h>

#include <array>
#include <stdexcept>
#include <system_error>

#include "agentdesk/platform/win/error.hpp"

namespace agentdesk::ipc {

namespace win = platform::win;
using win::UniqueHandle;

namespace {

constexpr DWORD kIoBuffer = 64 * 1024;

struct LocalFreeDeleter {
    void operator()(void* p) const noexcept { ::LocalFree(p); }
};

// SID of the user owning `process` (nullptr = current process), as a string.
std::wstring process_user_sid(HANDLE process) {
    HANDLE token_raw = nullptr;
    if (!::OpenProcessToken(process != nullptr ? process : ::GetCurrentProcess(), TOKEN_QUERY, &token_raw)) {
        win::throw_last_error("OpenProcessToken");
    }
    UniqueHandle token(token_raw);
    DWORD size = 0;
    ::GetTokenInformation(token.get(), TokenUser, nullptr, 0, &size);
    std::vector<std::byte> buf(size);
    if (!::GetTokenInformation(token.get(), TokenUser, buf.data(), size, &size)) {
        win::throw_last_error("GetTokenInformation(TokenUser)");
    }
    // Win32 boundary: TOKEN_USER lives in the buffer we sized for it.
    const auto* user = reinterpret_cast<const TOKEN_USER*>(buf.data());
    LPWSTR text = nullptr;
    if (!::ConvertSidToStringSidW(user->User.Sid, &text)) {
        win::throw_last_error("ConvertSidToStringSidW");
    }
    std::unique_ptr<wchar_t, LocalFreeDeleter> guard(text);
    return std::wstring(text);
}

bool pid_belongs_to_current_user(DWORD pid) {
    UniqueHandle process(::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
    if (!process)
        return false;
    try {
        return process_user_sid(process.get()) == current_user_sid();
    } catch (const std::system_error&) {
        return false;
    }
}

UniqueHandle make_event(bool manual_reset) {
    UniqueHandle h(::CreateEventW(nullptr, manual_reset ? TRUE : FALSE, FALSE, nullptr));
    if (!h)
        win::throw_last_error("CreateEventW");
    return h;
}

}  // namespace

std::wstring current_user_sid() {
    static const std::wstring sid = process_user_sid(nullptr);
    return sid;
}

DWORD current_session_id() {
    DWORD id = 0;
    ::ProcessIdToSessionId(::GetCurrentProcessId(), &id);
    return id;
}

std::wstring default_pipe_name() {
    return L"\\\\.\\pipe\\AgentDesk-" + current_user_sid() + L"-s" + std::to_wstring(current_session_id());
}

std::wstring default_mutex_name() {
    return L"Local\\AgentDesk-host-" + current_user_sid() + L"-s" + std::to_wstring(current_session_id());
}

// ------------------------------------------------------------------ Connection

Connection::Connection(UniqueHandle pipe, std::uint64_t id, DWORD client_pid)
    : pipe_(std::move(pipe)),
      read_event_(make_event(true)),
      write_event_(make_event(true)),
      wake_event_(make_event(false)),
      id_(id),
      client_pid_(client_pid) {}

Connection::~Connection() = default;

bool Connection::send(const std::string& payload, bool droppable) {
    std::string frame;
    try {
        frame = protocol::encode_frame(payload);
    } catch (const std::length_error&) {
        return false;  // caller must split large messages (snapshot chunks)
    }
    {
        std::lock_guard lock(mutex_);
        if (closed_)
            return false;
        if (droppable && out_bytes_ + frame.size() > kClientSendBudget)
            return false;
        if (out_bytes_ + frame.size() > kClientHardLimit) {
            closed_ = true;  // client cannot keep up even with reliable messages: drop it
        } else {
            out_bytes_ += frame.size();
            out_.push_back(std::move(frame));
        }
    }
    ::SetEvent(wake_event_.get());
    return !closed_;
}

void Connection::close() {
    closed_ = true;
    ::SetEvent(wake_event_.get());
}

std::size_t Connection::queued_bytes() const {
    std::lock_guard lock(mutex_);
    return out_bytes_;
}

void Connection::run(const ServerHandlers& handlers) {
    auto self = shared_from_this();
    if (handlers.on_open)
        handlers.on_open(self);

    protocol::FrameDecoder decoder;
    std::array<char, kIoBuffer> buffer{};
    OVERLAPPED rov{};
    rov.hEvent = read_event_.get();
    OVERLAPPED wov{};
    wov.hEvent = write_event_.get();
    bool read_pending = false;
    bool write_pending = false;
    std::string writing;

    while (!closed_) {
        if (!read_pending) {
            if (!::ReadFile(pipe_.get(), buffer.data(), kIoBuffer, nullptr, &rov) &&
                ::GetLastError() != ERROR_IO_PENDING) {
                break;  // client went away
            }
            read_pending = true;
        }
        if (!write_pending) {
            {
                std::lock_guard lock(mutex_);
                if (!out_.empty()) {
                    writing = std::move(out_.front());
                    out_.pop_front();
                }
            }
            if (!writing.empty()) {
                if (!::WriteFile(pipe_.get(), writing.data(), static_cast<DWORD>(writing.size()), nullptr,
                                 &wov) &&
                    ::GetLastError() != ERROR_IO_PENDING) {
                    break;
                }
                write_pending = true;
            }
        }
        // Read completion, a wake-up (new data to send or close) and, while writing, write completion.
        const std::array<HANDLE, 3> waits = {read_event_.get(), wake_event_.get(), write_event_.get()};
        ::WaitForMultipleObjects(write_pending ? 3 : 2, waits.data(), FALSE, INFINITE);
        if (::WaitForSingleObject(read_event_.get(), 0) == WAIT_OBJECT_0) {
            DWORD n = 0;
            if (!::GetOverlappedResult(pipe_.get(), &rov, &n, FALSE)) {
                read_pending = false;
                break;
            }
            read_pending = false;
            ::ResetEvent(read_event_.get());
            std::vector<std::string> payloads;
            if (decoder.feed(std::string_view(buffer.data(), n), payloads) != protocol::FrameError::kNone) {
                break;  // protocol violation (QA-17): drop the connection, no resync
            }
            for (auto& p : payloads) {
                if (handlers.on_message)
                    handlers.on_message(self, std::move(p));
            }
        }
        if (write_pending && ::WaitForSingleObject(write_event_.get(), 0) == WAIT_OBJECT_0) {
            DWORD n = 0;
            if (!::GetOverlappedResult(pipe_.get(), &wov, &n, FALSE)) {
                write_pending = false;
                break;
            }
            write_pending = false;
            ::ResetEvent(write_event_.get());
            std::lock_guard lock(mutex_);
            out_bytes_ -= writing.size();
            writing.clear();
        }
    }

    closed_ = true;
    ::CancelIoEx(pipe_.get(), nullptr);
    DWORD ignored = 0;
    if (read_pending)
        ::GetOverlappedResult(pipe_.get(), &rov, &ignored, TRUE);
    if (write_pending)
        ::GetOverlappedResult(pipe_.get(), &wov, &ignored, TRUE);
    ::FlushFileBuffers(pipe_.get());
    ::DisconnectNamedPipe(pipe_.get());
    if (handlers.on_close)
        handlers.on_close(self);
}

// ------------------------------------------------------------------ PipeServer

UniqueHandle PipeServer::create_instance(bool first) {
    const std::wstring sddl = L"D:P(A;;GA;;;SY)(A;;GA;;;" + current_user_sid() + L")";
    PSECURITY_DESCRIPTOR sd_raw = nullptr;
    if (!::ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &sd_raw,
                                                                nullptr)) {
        win::throw_last_error("ConvertStringSecurityDescriptorToSecurityDescriptorW");
    }
    std::unique_ptr<void, LocalFreeDeleter> sd(sd_raw);
    SECURITY_ATTRIBUTES sa{sizeof(sa), sd.get(), FALSE};
    const DWORD open =
        PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | (first ? FILE_FLAG_FIRST_PIPE_INSTANCE : 0);
    const DWORD mode = PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS;
    HANDLE h =
        ::CreateNamedPipeW(name_.c_str(), open, mode, PIPE_UNLIMITED_INSTANCES, kIoBuffer, kIoBuffer, 0, &sa);
    if (h == INVALID_HANDLE_VALUE)
        win::throw_last_error("CreateNamedPipeW");
    return UniqueHandle(h);
}

PipeServer::PipeServer(std::wstring name, ServerHandlers handlers)
    : name_(std::move(name)), handlers_(std::move(handlers)), stop_event_(make_event(true)) {
    next_instance_ = create_instance(true);  // ERROR_ACCESS_DENIED: someone else owns the name
    acceptor_ = std::thread([this] { accept_loop(); });
}

PipeServer::~PipeServer() {
    stop();
}

void PipeServer::stop() {
    ::SetEvent(stop_event_.get());
    if (acceptor_.joinable())
        acceptor_.join();
    std::map<std::uint64_t, std::pair<ConnectionPtr, std::thread>> conns;
    {
        std::lock_guard lock(mutex_);
        conns.swap(connections_);
    }
    for (auto& [id, entry] : conns) {
        entry.first->close();  // wakes the connection thread, which cancels its own I/O
    }
    for (auto& [id, entry] : conns) {
        if (entry.second.joinable())
            entry.second.join();
    }
}

std::size_t PipeServer::connection_count() const {
    std::lock_guard lock(mutex_);
    std::size_t n = 0;
    for (const auto& [id, entry] : connections_) {
        if (!entry.first->closed())
            ++n;
    }
    return n;
}

void PipeServer::accept_loop() {
    UniqueHandle connect_event = make_event(true);
    for (;;) {
        // Reap finished connection threads.
        {
            std::lock_guard lock(mutex_);
            for (auto it = connections_.begin(); it != connections_.end();) {
                if (it->second.first->closed() && it->second.second.joinable()) {
                    // The thread finishes right after marking closed; joining is short.
                    it->second.second.join();
                    it = connections_.erase(it);
                } else {
                    ++it;
                }
            }
        }
        OVERLAPPED ov{};
        ov.hEvent = connect_event.get();
        ::ResetEvent(connect_event.get());
        bool connected = false;
        if (::ConnectNamedPipe(next_instance_.get(), &ov)) {
            connected = true;
        } else {
            const DWORD err = ::GetLastError();
            if (err == ERROR_PIPE_CONNECTED) {
                connected = true;
            } else if (err == ERROR_IO_PENDING) {
                const std::array<HANDLE, 2> waits = {connect_event.get(), stop_event_.get()};
                const DWORD r = ::WaitForMultipleObjects(2, waits.data(), FALSE, INFINITE);
                if (r != WAIT_OBJECT_0) {
                    ::CancelIoEx(next_instance_.get(), &ov);
                    DWORD ignored = 0;
                    ::GetOverlappedResult(next_instance_.get(), &ov, &ignored, TRUE);
                    return;
                }
                DWORD ignored = 0;
                connected = ::GetOverlappedResult(next_instance_.get(), &ov, &ignored, FALSE) != FALSE;
            }
        }
        if (::WaitForSingleObject(stop_event_.get(), 0) == WAIT_OBJECT_0)
            return;

        UniqueHandle accepted = std::move(next_instance_);
        try {
            next_instance_ = create_instance(false);
        } catch (const std::system_error&) {
            return;  // cannot accept more clients; existing ones keep running
        }
        if (!connected)
            continue;

        ULONG pid = 0;
        // Second line of defence after the DACL (skill rd/platform-windows-conpty): same user only.
        if (!::GetNamedPipeClientProcessId(accepted.get(), &pid) || !pid_belongs_to_current_user(pid)) {
            ++rejected_;
            ::DisconnectNamedPipe(accepted.get());
            continue;
        }
        auto conn = std::make_shared<Connection>(std::move(accepted), next_id_++, pid);
        std::lock_guard lock(mutex_);
        connections_.emplace(conn->id(),
                             std::make_pair(conn, std::thread([this, conn] { conn->run(handlers_); })));
    }
}

// ------------------------------------------------------------------ PipeClient

std::unique_ptr<PipeClient> PipeClient::adopt(UniqueHandle read, UniqueHandle write) {
    if (!read)
        return nullptr;
    auto c = std::unique_ptr<PipeClient>(new PipeClient(std::move(read)));
    std::lock_guard lock(c->write_mutex_);
    c->write_pipe_ = std::move(write);
    return c;
}

UniqueHandle open_raw_pipe(const std::wstring& name, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    for (;;) {
        HANDLE h =
            ::CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                          FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            UniqueHandle pipe(h);
            ULONG server_pid = 0;
            if (!::GetNamedPipeServerProcessId(pipe.get(), &server_pid) ||
                !pid_belongs_to_current_user(server_pid))
                return {};
            return pipe;
        }
        const DWORD err = ::GetLastError();
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline)
            return {};
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
        if (err == ERROR_PIPE_BUSY)
            ::WaitNamedPipeW(name.c_str(), static_cast<DWORD>(std::min<long long>(left, 1000)));
        else
            ::Sleep(static_cast<DWORD>(std::min<long long>(left, 50)));
    }
}

std::unique_ptr<PipeClient> PipeClient::connect(const std::wstring& name, std::chrono::milliseconds timeout) {
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
                !pid_belongs_to_current_user(server_pid)) {
                return nullptr;  // a pipe with our name owned by someone else: refuse it
            }
            return std::unique_ptr<PipeClient>(new PipeClient(std::move(pipe)));
        }
        const DWORD err = ::GetLastError();
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline)
            return nullptr;
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
        if (err == ERROR_PIPE_BUSY) {
            ::WaitNamedPipeW(name.c_str(), static_cast<DWORD>(std::min<long long>(left, 1000)));
        } else {
            ::Sleep(static_cast<DWORD>(std::min<long long>(left, 50)));  // not created yet
        }
    }
}

PipeClient::PipeClient(UniqueHandle pipe) : pipe_(std::move(pipe)), stop_event_(make_event(true)) {
    LARGE_INTEGER t{};
    ::QueryPerformanceCounter(&t);
    id_prefix_ = "c-" + std::to_string(::GetCurrentProcessId()) + "-" + std::to_string(t.QuadPart) + "-";
    reader_ = std::thread([this] { reader_loop(); });
}

PipeClient::~PipeClient() {
    close();
}

void PipeClient::close() {
    ::SetEvent(stop_event_.get());
    if (reader_.joinable())
        reader_.join();
    std::lock_guard lock(write_mutex_);
    pipe_.reset();  // the server sees the disconnect and releases our control (design doc 3.7)
    write_pipe_.reset();
    connected_ = false;
}

void PipeClient::set_event_handler(std::function<void(protocol::Event)> handler) {
    std::lock_guard lock(mutex_);
    on_event_ = std::move(handler);
}

void PipeClient::set_disconnect_handler(std::function<void()> handler) {
    std::lock_guard lock(mutex_);
    on_disconnect_ = std::move(handler);
}

bool PipeClient::write_all(const std::string& bytes) {
    std::lock_guard lock(write_mutex_);
    if (!pipe_)
        return false;
    UniqueHandle event = make_event(true);
    OVERLAPPED ov{};
    ov.hEvent = event.get();
    HANDLE target = write_pipe_ ? write_pipe_.get() : pipe_.get();
    if (!::WriteFile(target, bytes.data(), static_cast<DWORD>(bytes.size()), nullptr, &ov) &&
        ::GetLastError() != ERROR_IO_PENDING) {
        return false;
    }
    DWORD n = 0;
    return ::GetOverlappedResult(target, &ov, &n, TRUE) && n == bytes.size();
}

std::optional<protocol::Response> PipeClient::call(const std::string& method, protocol::Json params,
                                                   std::chrono::milliseconds timeout,
                                                   std::string request_id) {
    if (!connected_)
        return std::nullopt;
    protocol::Request req;
    // Ids must be unique across clients: the host deduplicates session.create by request id for
    // 10 minutes (design doc 3.7), so per-process counters alone would collide.
    req.id = request_id.empty() ? id_prefix_ + std::to_string(next_request_++) : std::move(request_id);
    req.method = method;
    req.params = std::move(params);
    if (!write_all(protocol::encode_frame(protocol::serialize(req))))
        return std::nullopt;
    std::unique_lock lock(mutex_);
    if (!cv_.wait_for(lock, timeout, [&] { return responses_.contains(req.id) || !connected_; })) {
        return std::nullopt;
    }
    const auto it = responses_.find(req.id);
    if (it == responses_.end())
        return std::nullopt;
    auto resp = std::move(it->second);
    responses_.erase(it);
    return resp;
}

void PipeClient::reader_loop() {
    UniqueHandle read_event = make_event(true);
    protocol::FrameDecoder decoder;
    std::array<char, kIoBuffer> buffer{};
    for (;;) {
        OVERLAPPED ov{};
        ov.hEvent = read_event.get();
        if (!::ReadFile(pipe_.get(), buffer.data(), kIoBuffer, nullptr, &ov) &&
            ::GetLastError() != ERROR_IO_PENDING) {
            break;
        }
        const std::array<HANDLE, 2> waits = {read_event.get(), stop_event_.get()};
        if (::WaitForMultipleObjects(2, waits.data(), FALSE, INFINITE) != WAIT_OBJECT_0) {
            ::CancelIoEx(pipe_.get(), &ov);
            DWORD ignored = 0;
            ::GetOverlappedResult(pipe_.get(), &ov, &ignored, TRUE);
            break;
        }
        DWORD n = 0;
        if (!::GetOverlappedResult(pipe_.get(), &ov, &n, FALSE))
            break;
        std::vector<std::string> payloads;
        if (decoder.feed(std::string_view(buffer.data(), n), payloads) != protocol::FrameError::kNone)
            break;
        for (auto& p : payloads) {
            auto parsed = protocol::parse_message(p);
            if (!parsed.message)
                continue;
            if (auto* resp = std::get_if<protocol::Response>(&*parsed.message)) {
                {
                    std::lock_guard lock(mutex_);
                    responses_[resp->id] = std::move(*resp);
                }
                cv_.notify_all();
            } else if (auto* ev = std::get_if<protocol::Event>(&*parsed.message)) {
                std::function<void(protocol::Event)> handler;
                {
                    std::lock_guard lock(mutex_);
                    handler = on_event_;
                }
                if (handler)
                    handler(std::move(*ev));
            }
        }
    }
    connected_ = false;
    cv_.notify_all();
    std::function<void()> handler;
    {
        std::lock_guard lock(mutex_);
        handler = on_disconnect_;
    }
    if (handler)
        handler();
}

}  // namespace agentdesk::ipc
