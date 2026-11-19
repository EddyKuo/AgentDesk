#pragma once

// SessionManager: the host side of IPC protocol v1. Owns the session table, answers requests,
// fans screen updates out to clients and persists session metadata.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
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
    std::string host_version;           // agent-host's version, said in hello (Settings > Updates)
    std::function<void()> on_shutdown;  // host.shutdown accepted
    std::chrono::milliseconds request_dedupe_window{std::chrono::minutes(10)};  // design doc 3.7
    // ---- self-update (design doc 7.4 OQ-09, `agentctl update`)
    // Instance ids whose sessions this host took over, newest last (said in hello as
    // "succeeds", so a client keeps those sessions instead of calling them Lost).
    std::vector<std::string> succeeds;
    // host.update went through: the new host holds every session. main stops serving, waits for
    // the reply to leave, then calls finish_handover() and exits without stopping anything.
    std::function<void()> on_handed_over;
};

// The new host's side of a handover (main, `agent-host --takeover`). Reads what the old host
// sent on `from_old`, adopts every session (nothing runs yet) and answers "ready" on `to_old`,
// or answers "error <why>" and throws.
std::shared_ptr<class SessionManager> receive_handover(HostConfig config, HANDLE from_old, HANDLE to_old);
// After "ready": true once the old host said "go" or has exited (it let go of everything);
// false when it went away without letting go (then call abandon_adopted()).
bool await_handover_go(HANDLE from_old, HANDLE old_process);

// Owns all sessions of one host and implements IPC protocol v1 (design doc 3.7).
//
// Locking rule: mutex_ guards the session table and subscribers. Session actor threads call
// back into the manager (snapshot/info listeners) and take mutex_, so while holding mutex_ we
// never call a blocking Session method (history(), wait_exit()) or destroy a Session.
// Code that uses a Session after releasing mutex_ copies Managed::session while holding it.
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
    // Releases Session objects of all but the newest exited sessions (not on actor threads).
    void prune_exited();

    // ---- self-update (design doc 7.4 OQ-09)
    // The new host: the takeover is final; sessions start reading, writing and watching.
    void start_adopted();
    // The new host: the takeover did not go through; let go of every adopted session without
    // ending it (the old host still owns them).
    void abandon_adopted();
    // The old host, after on_handed_over: let go of every session without ending it, then tell
    // the new host to go.
    void finish_handover();
    [[nodiscard]] bool handed_over() const noexcept { return handed_over_; }

private:
    friend std::shared_ptr<SessionManager> receive_handover(HostConfig, HANDLE, HANDLE);
    struct Adopting {};
    SessionManager(HostConfig config, Adopting);
    struct Subscriber {
        std::weak_ptr<ipc::Connection> conn;
        std::shared_ptr<const terminal::ScreenSnapshot> last;
        bool needs_snapshot = false;
    };
    struct Managed {
        SessionRecord record;
        // null for Lost records from a previous host. Shared: a request that uses it without
        // mutex_ (history, input, wait) copies it under mutex_, so prune_exited cannot destroy
        // it underneath; the last copy is always dropped outside mutex_.
        std::shared_ptr<Session> session;
        std::optional<std::uint64_t> controller;
        std::map<std::uint64_t, Subscriber> subscribers;
        std::string last_process_state;
        // First message for an agent (session.create "task"): sent once it reports Idle, or at
        // task_after when it reports nothing (see kTaskFallback in session_manager.cpp).
        std::string pending_task;
        std::chrono::steady_clock::time_point task_after{};
        // Reports from sub-agents, delivered when this agent is not busy (session.report).
        std::vector<std::string> pending_reports;
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
    protocol::Response do_ui_state_get(const ipc::ConnectionPtr& conn, const protocol::Request& req);
    protocol::Response do_ui_state_set(const ipc::ConnectionPtr& conn, const protocol::Request& req);
    protocol::Response do_worktree_add(const protocol::Request& req);
    protocol::Response do_fs_list(const protocol::Request& req);
    protocol::Response do_fs_read(const protocol::Request& req);
    protocol::Response do_git_run(const protocol::Request& req);
    protocol::Response do_wait(const protocol::Request& req);
    protocol::Response do_label(const protocol::Request& req);
    protocol::Response do_report(const protocol::Request& req);
    // host.update {exe}: hands every session to a new agent-host (handover.cpp).
    protocol::Response do_update(const protocol::Request& req);
    void start_maintenance();
    // Delivers due tasks and reports (maintenance thread, every 200 ms).
    void deliver_pending();

    // Resolves "session" (UUID or unique name). Sets `error` on failure. Takes mutex_.
    ManagedPtr resolve(const protocol::Request& req, std::optional<protocol::Response>& error);
    protocol::Json describe(const Managed& m) const;  // mutex_ held
    // The callbacks a session `id` reports through (fanout, on_session_info).
    SessionListener listener_for(const std::string& id);
    void fanout(const std::string& id, std::shared_ptr<const terminal::ScreenSnapshot> snap);
    void on_session_info(const std::string& id, const SessionInfo& info);
    // Sends a full snapshot as chunked "screen.snapshot" events. mutex_ held.
    bool send_snapshot(ipc::Connection& conn, const std::string& session_id,
                       const terminal::ScreenSnapshot& snap, bool droppable, std::string& snapshot_id);
    void persist_locked();
    void load_profiles();
    void save_profiles(const std::filesystem::path& file);

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
    // Shared UI document (spaces, tabs, splits): opaque to the host, newest write wins, every
    // watching client gets "ui.state" when it changes. It lives in the host so that every window
    // and every reconnect sees the same layout.
    nlohmann::json ui_state_ = nlohmann::json::object();
    std::uint64_t ui_state_rev_ = 0;
    bool ui_state_loaded_ = false;
    std::map<std::uint64_t, std::weak_ptr<ipc::Connection>> ui_watchers_;
    std::atomic<bool> stopping_{false};
    std::thread maintenance_;

    // ---- self-update (design doc 7.4 OQ-09)
    // An update is under way: requests that change anything wait it out (kOverloaded,
    // retryable), nothing is delivered, nothing pruned.
    std::atomic<bool> updating_{false};
    std::atomic<bool> handed_over_{false};
    std::mutex update_mutex_;  // one host.update at a time
    struct Successor {
        platform::win::UniqueHandle process;
        platform::win::UniqueHandle to_new;
        std::vector<ManagedPtr> sessions;  // frozen, handed over
    };
    std::optional<Successor> successor_;
};

}  // namespace agentdesk::host
