#pragma once

// Local IPC over a Named Pipe (design doc 3.7): one pipe per user + logon session, DACL limited
// to the current user, remote clients rejected, client identity verified on connect.
// This is a same-user control channel, not an isolation boundary against same-account malware.
// libs/ipc/include_posix has the macOS version of this header (Unix sockets).

#include <windows.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "agentdesk/platform/win/unique_handle.hpp"
#include "agentdesk/protocol/frame.hpp"
#include "agentdesk/protocol/messages.hpp"

namespace agentdesk::ipc {

std::wstring current_user_sid();
DWORD current_session_id();
// \\.\pipe\AgentDesk-<SID>-s<session>  and  Local\AgentDesk-host-<SID>-s<session>
std::wstring default_pipe_name();
std::wstring default_mutex_name();

inline constexpr std::size_t kClientSendBudget = 4u * 1024u * 1024u;  // design doc 3.3
inline constexpr std::size_t kClientHardLimit = 16u * 1024u * 1024u;  // beyond: disconnect

class Connection;
using ConnectionPtr = std::shared_ptr<Connection>;

struct ServerHandlers {
    std::function<void(const ConnectionPtr&)> on_open;
    std::function<void(const ConnectionPtr&, std::string payload)> on_message;  // connection thread
    std::function<void(const ConnectionPtr&)> on_close;
};

// One accepted client. Thread-safe send; messages are delivered on the connection's thread.
class Connection : public std::enable_shared_from_this<Connection> {
public:
    Connection(platform::win::UniqueHandle pipe, std::uint64_t id, DWORD client_pid);
    ~Connection();
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    // Reliable messages (responses, state events) are always queued until the hard limit, then
    // the client is disconnected. Droppable messages (screen deltas) are refused once the
    // queue exceeds the 4 MiB budget; the caller then marks the client as needing a snapshot.
    bool send(const std::string& payload, bool droppable = false);
    void close();

    [[nodiscard]] std::uint64_t id() const noexcept { return id_; }
    [[nodiscard]] DWORD client_pid() const noexcept { return client_pid_; }
    [[nodiscard]] std::size_t queued_bytes() const;
    [[nodiscard]] bool closed() const noexcept { return closed_; }

    void run(const ServerHandlers& handlers);  // connection thread body

private:
    platform::win::UniqueHandle pipe_;
    platform::win::UniqueHandle read_event_;
    platform::win::UniqueHandle write_event_;
    platform::win::UniqueHandle wake_event_;
    std::uint64_t id_;
    DWORD client_pid_;
    mutable std::mutex mutex_;
    std::deque<std::string> out_;
    std::size_t out_bytes_ = 0;
    std::atomic<bool> closed_{false};
};

class PipeServer {
public:
    // Throws std::system_error(ERROR_ACCESS_DENIED) when another process owns the name.
    PipeServer(std::wstring name, ServerHandlers handlers);
    ~PipeServer();
    PipeServer(const PipeServer&) = delete;
    PipeServer& operator=(const PipeServer&) = delete;

    void stop();
    [[nodiscard]] const std::wstring& name() const noexcept { return name_; }
    [[nodiscard]] std::size_t connection_count() const;
    [[nodiscard]] std::uint64_t rejected_clients() const noexcept { return rejected_; }

private:
    void accept_loop();
    platform::win::UniqueHandle create_instance(bool first);

    std::wstring name_;
    ServerHandlers handlers_;
    platform::win::UniqueHandle stop_event_;
    platform::win::UniqueHandle next_instance_;
    std::thread acceptor_;
    mutable std::mutex mutex_;
    std::map<std::uint64_t, std::pair<ConnectionPtr, std::thread>> connections_;
    std::uint64_t next_id_ = 1;
    std::atomic<std::uint64_t> rejected_{0};
};

// Client side. A reader thread matches responses to requests by id; events go to on_event.
// Opens the host pipe (same owner checks as PipeClient::connect) as a raw overlapped handle,
// for agentctl bridge which relays bytes without parsing them. Invalid handle on failure.
platform::win::UniqueHandle open_raw_pipe(const std::wstring& name, std::chrono::milliseconds timeout);

class PipeClient {
public:
    static std::unique_ptr<PipeClient> connect(const std::wstring& name, std::chrono::milliseconds timeout);
    // Takes over a connected stream opened with FILE_FLAG_OVERLAPPED: reads from `read`, writes
    // to `write` (a stream to another machine's host through ssh; the far end checks its own
    // pipe). `write` may be empty when one duplex handle does both.
    static std::unique_ptr<PipeClient> adopt(platform::win::UniqueHandle read,
                                             platform::win::UniqueHandle write = {});
    ~PipeClient();
    PipeClient(const PipeClient&) = delete;
    PipeClient& operator=(const PipeClient&) = delete;

    void set_event_handler(std::function<void(protocol::Event)> handler);
    void set_disconnect_handler(std::function<void()> handler);

    // Sends a request and waits for its response. Returns nullopt on timeout or disconnect
    // (a timeout does not mean the request was not executed; design doc 3.3), and a
    // BAD_REQUEST response without sending anything when the request does not fit one frame.
    std::optional<protocol::Response> call(const std::string& method, protocol::Json params,
                                           std::chrono::milliseconds timeout = std::chrono::seconds(5),
                                           std::string request_id = {});
    [[nodiscard]] bool connected() const noexcept { return connected_; }
    void close();

private:
    explicit PipeClient(platform::win::UniqueHandle pipe);
    void reader_loop();
    bool write_all(const std::string& bytes);

    platform::win::UniqueHandle pipe_;
    platform::win::UniqueHandle write_pipe_;  // separate write stream (adopt), else pipe_
    platform::win::UniqueHandle stop_event_;
    std::thread reader_;
    std::mutex write_mutex_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::map<std::string, protocol::Response> responses_;
    std::set<std::string> pending_;  // ids of calls still waiting; guarded by mutex_
    std::function<void(protocol::Event)> on_event_;
    std::function<void()> on_disconnect_;
    std::atomic<bool> connected_{true};
    std::atomic<std::uint64_t> next_request_{1};
    std::string id_prefix_;
};

}  // namespace agentdesk::ipc

