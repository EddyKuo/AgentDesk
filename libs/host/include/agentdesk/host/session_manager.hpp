#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "agentdesk/host/session.hpp"
#include "agentdesk/host/store.hpp"
#include "agentdesk/ipc/pipe.hpp"
#include "agentdesk/protocol/messages.hpp"

namespace agentdesk::host {

struct HostConfig {
    platform::win::ConPtyApi conpty;
    std::filesystem::path data_dir;     // profiles.json, sessions.json
    std::filesystem::path ctl_path;     // agentctl.exe advertised to children (AGENTDESK_CTL)
    std::wstring pipe_name;             // AGENTDESK_PIPE
    std::string host_instance_id;       // changes on every host start (design doc 4.3)
    std::function<void()> on_shutdown;  // host.shutdown accepted
    std::chrono::milliseconds request_dedupe_window{std::chrono::minutes(10)};  // design doc 3.7
};

// Owns all sessions of one host and implements IPC protocol v1 (design doc 3.7).
//
// Locking rule: mutex_ guards the session table and subscribers. Session actor threads call
// back into the manager (snapshot/info listeners) and take mutex_, so while holding mutex_ we
// never call a blocking Session method (history(), wait_exit()) or destroy a Session.
class SessionManager {
public:
    explicit SessionManager(HostConfig config);
    ~SessionManager();
    SessionManager(const SessionManager&) = delete;
    SessionManager& operator=(const SessionManager&) = delete;

    void on_open(const ipc::ConnectionPtr& conn);
    void on_message(const ipc::ConnectionPtr& conn, std::string payload);
    void on_close(const ipc::ConnectionPtr& conn);

    [[nodiscard]] std::size_t running_session_count() const;
    void stop_all();  // force stop every running session (host exit)

private:
    struct Subscriber {
        std::weak_ptr<ipc::Connection> conn;
        std::shared_ptr<const terminal::ScreenSnapshot> last;
        bool needs_snapshot = false;
    };
    struct Managed {
        SessionRecord record;
        std::unique_ptr<Session> session;  // null for Lost records from a previous host
        std::optional<std::uint64_t> controller;
        std::map<std::uint64_t, Subscriber> subscribers;
        std::string last_process_state;
    };
    using ManagedPtr = std::shared_ptr<Managed>;

    protocol::Response dispatch(const ipc::ConnectionPtr& conn, const protocol::Request& req);
    protocol::Response do_hello(const protocol::Request& req);
    protocol::Response do_profile_list(const protocol::Request& req);
    protocol::Response do_create(const protocol::Request& req);
    protocol::Response do_list(const protocol::Request& req);
    protocol::Response do_get(const protocol::Request& req);
    protocol::Response do_attach(const ipc::ConnectionPtr& conn, const protocol::Request& req);
    protocol::Response do_detach(const ipc::ConnectionPtr& conn, const protocol::Request& req);
    protocol::Response do_input(const ipc::ConnectionPtr& conn, const protocol::Request& req);
    protocol::Response do_resize(const ipc::ConnectionPtr& conn, const protocol::Request& req);
    protocol::Response do_stop(const protocol::Request& req);
    protocol::Response do_snapshot(const ipc::ConnectionPtr& conn, const protocol::Request& req);
    protocol::Response do_history(const protocol::Request& req);
    protocol::Response do_state_report(const protocol::Request& req);
    protocol::Response do_shutdown(const protocol::Request& req);

    // Resolves "session" (UUID or unique name). Sets `error` on failure. Takes mutex_.
    ManagedPtr resolve(const protocol::Request& req, std::optional<protocol::Response>& error);
    protocol::Json describe(const Managed& m) const;  // mutex_ held
    void fanout(const std::string& id, std::shared_ptr<const terminal::ScreenSnapshot> snap);
    void on_session_info(const std::string& id, const SessionInfo& info);
    // Sends a full snapshot as chunked "screen.snapshot" events. mutex_ held.
    bool send_snapshot(ipc::Connection& conn, const std::string& session_id,
                       const terminal::ScreenSnapshot& snap, bool droppable, std::string& snapshot_id);
    void persist_locked();
    void load_profiles();

    HostConfig config_;
    mutable std::mutex mutex_;
    std::map<std::string, ManagedPtr> sessions_;
    std::vector<Profile> profiles_;
    struct Dedupe {
        std::chrono::steady_clock::time_point at;
        protocol::Response response;
    };
    std::map<std::string, Dedupe> dedupe_;
    std::uint64_t next_snapshot_id_ = 1;
};

}  // namespace agentdesk::host
