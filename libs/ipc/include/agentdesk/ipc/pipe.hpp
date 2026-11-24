#pragma once

// Local IPC (design doc 3.7) over platform::ByteStream: a Named Pipe on Windows, a Unix socket on
// macOS (ADR-30). One endpoint per user, the peer's user checked on both ends. A control channel, not
// an isolation boundary against malware running as the same user. Endpoint names are UTF-8.

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

#include "agentdesk/platform/ipc_transport.hpp"
#include "agentdesk/protocol/frame.hpp"
#include "agentdesk/protocol/messages.hpp"

namespace agentdesk::ipc {

// agent-host's endpoint for this user (platform::default_endpoint).
std::string default_pipe_name();

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
    Connection(std::unique_ptr<platform::ByteStream> stream, std::uint64_t id, std::uint32_t client_pid);
    ~Connection();
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    // Reliable messages (responses, state events) are always queued until the hard limit, then the
    // client is dropped; droppable ones (screen updates) are refused past the send budget.
    bool send(const std::string& payload, bool droppable = false);
    void close();

    [[nodiscard]] std::uint64_t id() const noexcept { return id_; }
    [[nodiscard]] std::uint32_t client_pid() const noexcept { return client_pid_; }
    [[nodiscard]] std::size_t queued_bytes() const;
    [[nodiscard]] bool closed() const noexcept { return closed_; }

    void run(const ServerHandlers& handlers);  // connection thread body

private:
    void writer_loop();

    std::unique_ptr<platform::ByteStream> stream_;
    std::uint64_t id_;
    std::uint32_t client_pid_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<std::string> out_;
    std::size_t out_bytes_ = 0;
    std::atomic<bool> closed_{false};
};

class PipeServer {
public:
    // Throws std::system_error when a live server owns the name (platform::endpoint_in_use).
    PipeServer(std::string name, ServerHandlers handlers);
    ~PipeServer();
    PipeServer(const PipeServer&) = delete;
    PipeServer& operator=(const PipeServer&) = delete;

    void stop();
    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] std::size_t connection_count() const;
    [[nodiscard]] std::uint64_t rejected_clients() const noexcept;

private:
    void accept_loop();

    std::string name_;
    ServerHandlers handlers_;
    std::unique_ptr<platform::StreamListener> listener_;
    std::thread acceptor_;
    mutable std::mutex mutex_;
    std::map<std::uint64_t, std::pair<ConnectionPtr, std::thread>> connections_;
    std::uint64_t next_id_ = 1;
    std::atomic<bool> stopped_{false};
};

// The host endpoint as a raw stream, for agentctl bridge which relays bytes without parsing them.
// nullptr on failure.
std::unique_ptr<platform::ByteStream> open_raw_pipe(const std::string& name,
                                                    std::chrono::milliseconds timeout);

class PipeClient {
public:
    static std::unique_ptr<PipeClient> connect(const std::string& name, std::chrono::milliseconds timeout);
    // Takes over a connected stream (a child's stdio for ssh bridges).
    static std::unique_ptr<PipeClient> adopt(std::unique_ptr<platform::ByteStream> stream);
    ~PipeClient();
    PipeClient(const PipeClient&) = delete;
    PipeClient& operator=(const PipeClient&) = delete;

    void set_event_handler(std::function<void(protocol::Event)> handler);
    void set_disconnect_handler(std::function<void()> handler);

    std::optional<protocol::Response> call(const std::string& method, protocol::Json params,
                                           std::chrono::milliseconds timeout = std::chrono::seconds(5),
                                           std::string request_id = {});
    [[nodiscard]] bool connected() const noexcept { return connected_; }
    void close();

private:
    explicit PipeClient(std::unique_ptr<platform::ByteStream> stream);
    void reader_loop();
    bool write_all(const std::string& bytes);

    std::unique_ptr<platform::ByteStream> stream_;
    std::thread reader_;
    std::mutex write_mutex_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::map<std::string, protocol::Response> responses_;
    std::set<std::string> pending_;
    std::function<void(protocol::Event)> on_event_;
    std::function<void()> on_disconnect_;
    std::atomic<bool> connected_{true};
    std::atomic<std::uint64_t> next_request_{1};
    std::string id_prefix_;
};

}  // namespace agentdesk::ipc
