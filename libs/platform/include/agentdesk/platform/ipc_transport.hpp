#pragma once

// Local byte streams between agent-host and its clients: the platform-neutral interface
// (docs/design/platform-abstraction.md 5.3). Framing, budgets and request matching live in
// libs/ipc on top of this.
//
// The endpoint admits only the current user. It is a same-user control channel, not an
// isolation boundary against malware running as that user (design doc 3.7).

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace agentdesk::platform {

class Process;

// A connected, full-duplex byte stream. One thread reads while another writes; cancel() from any
// thread wakes a blocked read and makes every later call fail.
class ByteStream {
public:
    virtual ~ByteStream() = default;
    // Blocks until at least one byte arrives; 0 when the stream ended or was cancelled.
    virtual std::size_t read_some(std::span<char> buf) = 0;
    virtual bool write_all(std::string_view bytes) = 0;
    virtual void cancel() = 0;
};

struct PeerIdentity {
    std::uint32_t pid = 0;
    bool same_user = false;
};

// The per-user endpoint of agent-host. macOS: <per-user temp dir>/AgentDesk/host.sock.
std::string default_endpoint();

class StreamListener {
public:
    // Throws std::system_error(EADDRINUSE) when a live server already owns the endpoint. A stale
    // endpoint left by a crashed server is replaced.
    static std::unique_ptr<StreamListener> listen(const std::string& endpoint);
    ~StreamListener();
    StreamListener(const StreamListener&) = delete;
    StreamListener& operator=(const StreamListener&) = delete;

    // Blocks until a client of the same user connects, or stop() (then nullptr). Clients of other
    // users are closed and counted, never returned.
    std::unique_ptr<ByteStream> accept(PeerIdentity* peer = nullptr);
    void stop();
    // After stop() and once no thread is in accept(): stops listening (new clients are refused)
    // but leaves the endpoint's file, which a successor may already listen on (host handover).
    void close_socket();
    [[nodiscard]] std::uint64_t rejected_clients() const noexcept;
    [[nodiscard]] const std::string& endpoint() const noexcept;

    struct Impl;

private:
    explicit StreamListener(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

// Connects, retrying while the server is not there yet; nullptr on timeout. The server must run
// as the current user.
std::unique_ptr<ByteStream> connect_stream(const std::string& endpoint, std::chrono::milliseconds timeout);

// A child's stdout (read) and stdin (write) as one stream, e.g. `ssh -T host agentctl bridge`.
// The spec must have asked for both pipes. cancel() kills the child.
std::unique_ptr<ByteStream> process_stream(std::unique_ptr<Process> child);

// Only one holder per name and user (agent-host). Released by the destructor or process exit.
class InstanceLock {
public:
    static std::unique_ptr<InstanceLock> try_acquire(const std::string& name);  // nullptr if held
    ~InstanceLock();
    InstanceLock(const InstanceLock&) = delete;
    InstanceLock& operator=(const InstanceLock&) = delete;

    struct Impl;

private:
    explicit InstanceLock(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

}  // namespace agentdesk::platform
