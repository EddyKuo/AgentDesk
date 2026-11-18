#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <variant>
#include <vector>

#include "agentdesk/host/input.hpp"
#include "agentdesk/host/states.hpp"
#include "agentdesk/platform/win/conpty_api.hpp"
#include "agentdesk/platform/win/process_tree.hpp"
#include "agentdesk/platform/win/pty_process.hpp"
#include "agentdesk/terminal/vt_engine.hpp"

namespace agentdesk::host {

struct SessionConfig {
    platform::win::ConPtyApi api;
    platform::win::PtySpawnSpec spec;
    terminal::VtEngineOptions engine;
    std::chrono::milliseconds snapshot_interval{8};       // UI updates are coalesced, VT bytes never
    std::chrono::milliseconds interrupt_grace{5000};      // design doc 3.2
    std::size_t output_queue_limit = 8u * 1024u * 1024u;  // design doc 3.3: 8 MiB, then backpressure
};

struct AgentStatus {
    AgentState state = AgentState::kUnknown;
    std::string source;  // "osc7501", "report:<source>" or empty
    std::chrono::system_clock::time_point updated_at{};
    terminal::StatusKind kind = terminal::StatusKind::kNone;
    std::string message;
    AgentState last_state = AgentState::kUnknown;  // value before the record was dropped
    std::string app;  // the 7501 report's `app` (herdr shows a terminal by it first), or empty
};

struct SessionInfo {
    ProcessState process_state = ProcessState::kStarting;
    std::optional<std::uint32_t> exit_code;
    std::uint32_t pid = 0;
    bool stop_requested = false;
    bool still_running_after_interrupt = false;  // grace period elapsed, offer force
    std::uint32_t remaining_processes = 0;       // job members after the root exited
    AgentStatus agent;
    std::uint64_t bytes_from_program = 0;
    bool history_truncated = false;
    std::uint64_t backpressure_waits = 0;  // times the reader paused because the queue was full
    std::uint64_t first_history_line_id = 0;
    std::string cwd_uri;  // last OSC 7 the shell sent ("" until one arrives)
};

// Callbacks run on the session actor thread; keep them short and never call back into the
// session synchronously from them.
struct SessionListener {
    std::function<void(std::shared_ptr<const terminal::ScreenSnapshot>)> on_snapshot;
    std::function<void(const SessionInfo&)> on_info;
};

// One managed terminal session: ConPTY child + VtEngine, owned by a single actor thread.
// The engine is touched only on that thread; the renderer reads immutable snapshots.
class Session {
public:
    static std::unique_ptr<Session> start(SessionConfig config, SessionListener listener = {});
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    Session(Session&&) = delete;
    Session& operator=(Session&&) = delete;

    void send(InputEvent input);
    void resize(int rows, int cols);
    void interrupt();   // Ctrl+C, then grace period
    void force_stop();  // TerminateJobObject (explicit user action only)
    // Structured status from an adapter (agentctl report-state). Stale seq values are ignored.
    void report_state(std::string source, std::uint64_t seq, AgentState state,
                      terminal::StatusKind kind = terminal::StatusKind::kNone, std::string message = {});

    [[nodiscard]] std::shared_ptr<const terminal::ScreenSnapshot> snapshot() const;
    [[nodiscard]] SessionInfo info() const;
    // Program running in the terminal now ("claude", "pwsh", ...); empty once exited.
    [[nodiscard]] platform::win::Foreground foreground() const;
    [[nodiscard]] std::string foreground_program() const;
    // Lines from history (oldest first) with line_id >= from, at most `max_lines`.
    [[nodiscard]] std::vector<terminal::HistoryLine> history(std::uint64_t from, std::size_t max_lines) const;
    // The newest `max_lines` history lines (oldest first).
    [[nodiscard]] std::vector<terminal::HistoryLine> history_tail(std::size_t max_lines) const;

    // Blocks until the root process exited (or timeout). Returns the exit code.
    std::optional<std::uint32_t> wait_exit(std::chrono::milliseconds timeout) const;

    // ---- agent-host's self-update (design doc 7.4 OQ-09; herdr src/update.rs 236, docs/
    // commands.md 547: "terminals keep running"). The old host freezes each session and hands
    // its state and handles to the new host, which adopts them.
    struct Frozen {
        std::string state;  // engine + session state (save format of this build)
        // The live terminal's handles, owned by *this; nullopt when the program already exited.
        std::optional<platform::win::PtyHandles> handles;
    };
    // Whether this session's terminal could be handed over (bundled ConPTY), without changing it.
    [[nodiscard]] bool can_hand_over(std::string* why) const;
    // Stops reading at a byte where no escape sequence is half read, writes every pending input
    // byte, stops reacting to the program's exit and returns the state. nullopt with `why` when
    // that did not work within `timeout`; then call resume().
    std::optional<Frozen> freeze(std::chrono::milliseconds timeout, std::string* why);
    // The handover failed: read, write and watch the program again.
    void resume();
    // The new host holds duplicates of every handle: close ours without ending anything. Destroy
    // the session afterwards.
    void release_after_handover();
    // In the new host: a session from frozen state and duplicated handles, not running yet (no
    // threads; nothing read or written). Throws when the state or handles do not check out.
    static std::unique_ptr<Session> adopt(SessionConfig config, SessionListener listener, std::string_view state,
                                          const std::optional<platform::win::PtyHandles>& handles);
    // The takeover is final: start reading, writing and watching.
    void start_adopted();

private:
    struct OutputChunk {
        std::string bytes;
    };
    struct ResizeCmd {
        int rows;
        int cols;
    };
    struct InterruptCmd {};
    struct ReportCmd {
        std::string source;
        std::uint64_t seq;
        AgentState state;
        terminal::StatusKind kind;
        std::string message;
    };
    struct HistoryCmd {
        std::uint64_t from;
        std::size_t max_lines;
        std::shared_ptr<std::promise<std::vector<terminal::HistoryLine>>> result;
        bool tail = false;
    };
    struct FreezeCmd {
        std::chrono::steady_clock::time_point deadline;
        std::shared_ptr<std::promise<std::string>> result;  // state, or empty with error set
        std::shared_ptr<std::string> error;
    };
    struct UnfreezeCmd {};
    using Command = std::variant<OutputChunk, InputEvent, ResizeCmd, InterruptCmd, ReportCmd, HistoryCmd,
                                 FreezeCmd, UnfreezeCmd>;

    explicit Session(SessionConfig config, SessionListener listener);
    void post(Command cmd);
    void post_output(std::string bytes);  // reader thread; may block (backpressure)
    void release_backpressure();
    void drain_queue();
    void actor_loop();
    void writer_loop();
    void handle(OutputChunk& c);
    void handle(InputEvent& c);
    void handle(ResizeCmd& c);
    void handle(InterruptCmd& c);
    void handle(ReportCmd& c);
    void handle(HistoryCmd& c);
    void handle(FreezeCmd& c);
    void handle(UnfreezeCmd& c);
    [[nodiscard]] std::string save_session_state() const;  // actor thread
    void on_root_exit();
    void on_engine_status(const terminal::StatusReport& report);
    void refresh_agent_from_engine();
    void queue_pty_bytes(std::string bytes);
    void publish(bool force);
    void publish_info();

    SessionConfig config_;
    SessionListener listener_;
    std::unique_ptr<terminal::VtEngine> engine_;  // actor thread only
    std::unique_ptr<platform::win::PtyProcess> process_;

    // command queue -> actor
    mutable std::mutex queue_mutex_;
    std::deque<Command> queue_;
    platform::win::UniqueHandle queue_event_;
    std::condition_variable queue_space_cv_;
    std::size_t queued_output_bytes_ = 0;   // guarded by queue_mutex_
    std::uint64_t backpressure_waits_ = 0;  // guarded by queue_mutex_
    bool overloaded_ = false;               // guarded by queue_mutex_
    bool no_backpressure_ = false;          // guarded by queue_mutex_ (set while closing)
    std::atomic<bool> stopping_{false};

    // bytes -> writer thread
    std::mutex write_mutex_;
    std::condition_variable write_cv_;
    std::deque<std::string> write_queue_;
    bool writer_stop_ = false;
    bool writer_busy_ = false;  // a chunk is being written (guarded by write_mutex_)
    std::condition_variable writer_idle_cv_;

    // handover: the actor neither watches nor reacts to the program's exit (actor thread only)
    bool frozen_ = false;

    // published state
    mutable std::mutex state_mutex_;
    std::shared_ptr<const terminal::ScreenSnapshot> snapshot_;
    SessionInfo info_;
    std::map<std::string, std::uint64_t> report_seq_;  // actor thread only
    std::uint64_t published_seq_ = 0;                  // actor thread only
    std::chrono::steady_clock::time_point last_publish_{};
    std::optional<std::chrono::steady_clock::time_point> interrupt_deadline_;
    mutable std::condition_variable exit_cv_;

    std::thread writer_;
    std::thread actor_;
};

}  // namespace agentdesk::host
