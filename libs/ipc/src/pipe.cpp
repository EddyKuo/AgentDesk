// Local IPC: framing, send budgets and request matching over platform::ByteStream. Each connection
// has a reader (the connection thread) and a writer thread, so a slow client never blocks the
// messages coming from it.
#include "agentdesk/ipc/pipe.hpp"

#include <array>
#include <stdexcept>

#include "agentdesk/platform/system.hpp"

namespace agentdesk::ipc {

namespace {

constexpr std::size_t kIoBuffer = 64 * 1024;

}  // namespace

std::string default_pipe_name() {
    return platform::default_endpoint();
}

// ---------------------------------------------------------------- Connection

Connection::Connection(std::unique_ptr<platform::ByteStream> stream, std::uint64_t id,
                       std::uint32_t client_pid)
    : stream_(std::move(stream)), id_(id), client_pid_(client_pid) {}

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
            closed_ = true;  // the client cannot keep up even with reliable messages: drop it
        } else {
            out_bytes_ += frame.size();
            out_.push_back(std::move(frame));
        }
    }
    cv_.notify_all();
    if (closed_)
        stream_->cancel();
    return !closed_;
}

void Connection::close() {
    {
        std::lock_guard lock(mutex_);  // the writer checks closed_ under it: no lost wake-up
        closed_ = true;
    }
    cv_.notify_all();
    stream_->cancel();  // wakes the connection thread's read
}

std::size_t Connection::queued_bytes() const {
    std::lock_guard lock(mutex_);
    return out_bytes_;
}

void Connection::writer_loop() {
    std::unique_lock lock(mutex_);
    for (;;) {
        cv_.wait(lock, [&] { return closed_ || !out_.empty(); });
        if (closed_)
            return;
        std::string frame = std::move(out_.front());
        out_.pop_front();
        lock.unlock();
        const bool ok = stream_->write_all(frame);
        lock.lock();
        out_bytes_ -= frame.size();
        if (!ok) {
            closed_ = true;
            lock.unlock();
            stream_->cancel();
            return;
        }
    }
}

void Connection::run(const ServerHandlers& handlers) {
    auto self = shared_from_this();
    if (handlers.on_open)
        handlers.on_open(self);
    std::thread writer([this] { writer_loop(); });

    protocol::FrameDecoder decoder;
    std::array<char, kIoBuffer> buffer{};
    while (!closed_) {
        const std::size_t n = stream_->read_some(buffer);
        if (n == 0)
            break;  // client went away, or close()
        std::vector<std::string> payloads;
        if (decoder.feed(std::string_view(buffer.data(), n), payloads) != protocol::FrameError::kNone)
            break;  // protocol violation (QA-17): drop the connection, no resync
        for (auto& p : payloads) {
            if (handlers.on_message)
                handlers.on_message(self, std::move(p));
        }
    }

    {
        std::lock_guard lock(mutex_);
        closed_ = true;
    }
    cv_.notify_all();
    stream_->cancel();
    writer.join();
    if (handlers.on_close)
        handlers.on_close(self);
}

// ---------------------------------------------------------------- PipeServer

PipeServer::PipeServer(std::string name, ServerHandlers handlers)
    : name_(std::move(name)), handlers_(std::move(handlers)) {
    listener_ = platform::StreamListener::listen(platform::endpoint_named(name_));
    acceptor_ = std::thread([this] { accept_loop(); });
}

PipeServer::~PipeServer() {
    stop();
}

void PipeServer::stop() {
    stopped_ = true;
    if (listener_)
        listener_->stop();
    if (acceptor_.joinable())
        acceptor_.join();
    if (listener_)
        listener_->close_socket();  // clients now get "refused" at once, not a queue nobody serves
    std::map<std::uint64_t, std::pair<ConnectionPtr, std::thread>> conns;
    {
        std::lock_guard lock(mutex_);
        conns.swap(connections_);
    }
    for (auto& [id, entry] : conns)
        entry.first->close();
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

std::uint64_t PipeServer::rejected_clients() const noexcept {
    return listener_ ? listener_->rejected_clients() : 0;
}

void PipeServer::accept_loop() {
    for (;;) {
        // Join the threads of connections that ended.
        std::vector<std::pair<ConnectionPtr, std::thread>> finished;
        {
            std::lock_guard lock(mutex_);
            for (auto it = connections_.begin(); it != connections_.end();) {
                if (it->second.first->closed()) {
                    finished.push_back(std::move(it->second));
                    it = connections_.erase(it);
                } else {
                    ++it;
                }
            }
        }
        for (auto& [conn, thread] : finished) {
            if (thread.joinable())
                thread.join();
        }
        platform::PeerIdentity peer;
        auto stream = listener_->accept(&peer);
        if (!stream || stopped_)
            return;
        std::lock_guard lock(mutex_);
        auto conn = std::make_shared<Connection>(std::move(stream), next_id_++, peer.pid);
        connections_.emplace(conn->id(),
                             std::make_pair(conn, std::thread([this, conn] { conn->run(handlers_); })));
    }
}

// ---------------------------------------------------------------- PipeClient

std::unique_ptr<platform::ByteStream> open_raw_pipe(const std::string& name,
                                                    std::chrono::milliseconds timeout) {
    return platform::connect_stream(platform::endpoint_named(name), timeout);
}

std::unique_ptr<PipeClient> PipeClient::connect(const std::string& name, std::chrono::milliseconds timeout) {
    auto stream = open_raw_pipe(name, timeout);
    if (!stream)
        return nullptr;
    return std::unique_ptr<PipeClient>(new PipeClient(std::move(stream)));
}

std::unique_ptr<PipeClient> PipeClient::adopt(std::unique_ptr<platform::ByteStream> stream) {
    if (!stream)
        return nullptr;
    return std::unique_ptr<PipeClient>(new PipeClient(std::move(stream)));
}

PipeClient::PipeClient(std::unique_ptr<platform::ByteStream> stream) : stream_(std::move(stream)) {
    const auto t = std::chrono::steady_clock::now().time_since_epoch().count();
    id_prefix_ = "c-" + std::to_string(platform::current_pid()) + "-" + std::to_string(t) + "-";
    reader_ = std::thread([this] { reader_loop(); });
}

PipeClient::~PipeClient() {
    close();
}

void PipeClient::close() {
    if (stream_)
        stream_->cancel();
    if (reader_.joinable())
        reader_.join();
    std::lock_guard lock(write_mutex_);
    stream_.reset();  // the server sees the disconnect and releases our control (design doc 3.7)
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
    return stream_ && stream_->write_all(bytes);
}

std::optional<protocol::Response> PipeClient::call(const std::string& method, protocol::Json params,
                                                   std::chrono::milliseconds timeout,
                                                   std::string request_id) {
    if (!connected_)
        return std::nullopt;
    protocol::Request req;
    // Unique across clients: the host deduplicates session.create by request id (design doc 3.7).
    req.id = request_id.empty() ? id_prefix_ + std::to_string(next_request_++) : std::move(request_id);
    req.method = method;
    req.params = std::move(params);
    std::string frame;
    try {
        frame = protocol::encode_frame(protocol::serialize(req));
    } catch (const std::length_error&) {
        return protocol::fail(req.id, protocol::ErrorCode::kBadRequest, "request too large for one message");
    }
    {
        std::lock_guard lock(mutex_);
        pending_.insert(req.id);
    }
    struct Forget {
        PipeClient& self;
        const std::string& id;
        ~Forget() {
            std::lock_guard lock(self.mutex_);
            self.pending_.erase(id);
            self.responses_.erase(id);
        }
    } forget{*this, req.id};
    if (!write_all(frame))
        return std::nullopt;
    std::unique_lock lock(mutex_);
    if (!cv_.wait_for(lock, timeout, [&] { return responses_.contains(req.id) || !connected_; }))
        return std::nullopt;
    const auto it = responses_.find(req.id);
    if (it == responses_.end())
        return std::nullopt;
    auto resp = std::move(it->second);
    responses_.erase(it);
    lock.unlock();
    return resp;
}

void PipeClient::reader_loop() {
    protocol::FrameDecoder decoder;
    std::array<char, kIoBuffer> buffer{};
    for (;;) {
        const std::size_t n = stream_->read_some(buffer);
        if (n == 0)
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
                    if (pending_.contains(resp->id))  // else its call already gave up
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
    {
        std::lock_guard lock(mutex_);  // call() checks connected_ under it: no lost wake-up
        connected_ = false;
    }
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
