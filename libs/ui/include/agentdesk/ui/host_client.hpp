#pragma once

// UI-side client of agent-host (design doc 2.1, 4.3). The UI thread never blocks on IPC:
// requests run on a worker thread, events update per-session state, and a wake callback asks
// the UI loop to redraw.

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "agentdesk/ipc/host_launcher.hpp"
#include "agentdesk/ipc/pipe.hpp"
#include "agentdesk/ui/remote_screen.hpp"
#include "agentdesk/ui/terminal_source.hpp"

namespace agentdesk::ui {

class HostClient;

struct SessionView {
    std::string id;
    std::string name;
    std::string cwd;        // current folder: follows cd in the shell (live_cwd), else start_cwd
    std::string start_cwd;  // folder the session was started in
    std::string created_at;  // ISO 8601 UTC, from the host
    std::string program;     // what runs in the terminal now ("claude", "pwsh"), from the host
    std::vector<std::string> program_argv;  // its command line as words (herdr resolves by it)
    std::string agent_app;                  // the 7501 report's `app`, if any
    std::string parent;      // session that started this one (sub-agent), or empty
    std::map<std::string, std::string> labels;
    std::string profile_id;
    std::string process_state = "Unknown";
    std::optional<std::uint32_t> exit_code;
    std::uint32_t pid = 0;
    std::string agent_state = "Unknown";
    std::string agent_source;
    std::string agent_kind;
    std::string agent_message;
    std::string last_agent_state = "Unknown";
    bool still_running_after_interrupt = false;
    std::uint32_t remaining_processes = 0;
    bool history_truncated = false;
    std::uint64_t backpressure_waits = 0;
    bool lost = false;  // owned by a previous host instance
    bool is_controller = false;
    bool controlled_elsewhere = false;
};

class RemoteSession final : public TerminalSource {
public:
    RemoteSession(HostClient& owner, std::string id);

    [[nodiscard]] std::shared_ptr<const terminal::ScreenSnapshot> snapshot() const override;
    void send(host::InputEvent input) override;
    void resize(int rows, int cols) override;
    [[nodiscard]] bool controls_size() const override;
    [[nodiscard]] bool can_input() const override;
    void request_history(std::size_t lines) override;
    [[nodiscard]] std::shared_ptr<const std::vector<HistoryLineText>> history() const override;
    [[nodiscard]] std::string cwd() const override;

    [[nodiscard]] SessionView view() const;
    [[nodiscard]] const std::string& id() const noexcept { return id_; }
    void take_control();
    void stop(bool force);

private:
    friend class HostClient;
    HostClient& owner_;
    std::string id_;
    mutable std::mutex mutex_;
    RemoteScreen screen_;
    SessionView view_;
    bool attached_ = false;
    bool resync_requested_ = false;
    std::shared_ptr<const std::vector<HistoryLineText>> history_;
    std::size_t history_requested_ = 0;
    std::chrono::steady_clock::time_point history_fetched_at_{};
};

struct HostClientOptions {
    std::filesystem::path host_exe;
    std::wstring pipe_name;
    std::function<void()> wake;  // called from background threads when something changed
    // Another machine: connects through a stream command (ssh ... agentctl bridge) instead of
    // the local pipe; never starts a local host.
    std::function<ipc::HostConnection()> connector;
};

class HostClient {
public:
    explicit HostClient(HostClientOptions options);
    ~HostClient();
    HostClient(const HostClient&) = delete;
    HostClient& operator=(const HostClient&) = delete;

    [[nodiscard]] bool connected() const noexcept { return connected_; }
    [[nodiscard]] bool host_dies_with_launcher() const noexcept { return host_dies_with_launcher_; }
    [[nodiscard]] std::string host_instance_id() const;
    [[nodiscard]] std::string host_version() const;  // agent-host's own version, "" if it does not say
    [[nodiscard]] std::string last_error() const;
    [[nodiscard]] std::vector<std::shared_ptr<RemoteSession>> sessions() const;
    [[nodiscard]] std::vector<std::pair<std::string, std::string>> profiles() const;  // id, name
    // The program a profile starts (its executable), for its icon; empty if unknown.
    [[nodiscard]] std::string profile_executable(const std::string& id) const;

    void request_start_host();  // reconnect, starting agent-host.exe if needed
    void create_session(std::string profile_id, std::string cwd, std::string name,
                        std::function<void(std::string error)> done);
    void shutdown_host(bool stop_sessions, std::function<void(std::string error)> done);

    // Fire-and-forget request executed on the worker thread.
    void post(std::string method, protocol::Json params);
    // Request with a result; `done` runs on the worker thread (error empty on success).
    void call(std::string method, protocol::Json params,
              std::function<void(protocol::Json result, std::string error)> done,
              std::chrono::milliseconds timeout = std::chrono::seconds(5));

    // Shared UI document kept by the host (spaces, tabs, splits). take_ui_state() returns it
    // once after connecting and whenever another window changed it.
    std::optional<protocol::Json> take_ui_state();
    void set_ui_state(protocol::Json doc);

private:
    friend class RemoteSession;  // queues history/control requests on the worker
    struct Task {
        std::function<void(ipc::PipeClient&)> run;
        // Called instead of run when the connection is gone by the time the task is taken
        // (requests with a result answer with an error rather than never).
        std::function<void(const std::string& error)> fail;
    };
    void enqueue(std::function<void(ipc::PipeClient&)> run,
                 std::function<void(const std::string& error)> fail = nullptr);
    void worker_loop();
    bool try_connect(bool allow_start);
    void refresh_sessions(ipc::PipeClient& client);
    void on_event(protocol::Event e);
    void notify() const;

    HostClientOptions options_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<Task> tasks_;
    std::map<std::string, std::shared_ptr<RemoteSession>> sessions_;
    std::vector<std::pair<std::string, std::string>> profiles_;
    std::map<std::string, std::string> profile_exes_;
    std::optional<protocol::Json> pending_ui_state_;
    std::string instance_id_;
    std::string host_version_;
    std::string last_error_;
    std::unique_ptr<ipc::PipeClient> client_;  // worker thread owns connect/close
    std::shared_ptr<void> transport_;          // ssh process of a remote machine
    std::atomic<bool> connected_{false};
    std::atomic<bool> start_requested_{true};
    std::atomic<bool> host_dies_with_launcher_{false};
    std::atomic<bool> stop_{false};
    std::thread worker_;
};

}  // namespace agentdesk::ui
