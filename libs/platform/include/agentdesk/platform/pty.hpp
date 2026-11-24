#pragma once

// A child program in a pseudo terminal: the platform-neutral interface (design doc ADR-30,
// docs/design/platform-abstraction.md 5.1). One implementation per platform is compiled in.

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace agentdesk::platform {

struct PtySize {
    std::uint16_t cols = 120;
    std::uint16_t rows = 40;
};

struct PtySpawnSpec {
    std::filesystem::path executable;  // started directly, never through a shell command string
    std::vector<std::string> args;     // UTF-8, argv[1..]
    std::filesystem::path cwd;         // must exist; never silently replaced
    // Merged over the current environment; an empty value removes the variable.
    std::map<std::string, std::string> env_overrides;
    PtySize size;
};

// Reader thread, in order, every byte. May block (the caller's backpressure); while it blocks
// nothing more is read from the terminal.
using PtyOutputSink = std::function<void(std::span<const char>)>;
// Called once, from a platform thread, after the root process has exited. Keep it short: post to
// a queue and call close() from elsewhere.
using PtyExitSink = std::function<void()>;

// What runs in front of the terminal now.
struct ForegroundInfo {
    std::string program;            // lower-case file name without extension; empty if unknown
    std::string cwd;                // UTF-8; empty if unreadable
    std::vector<std::string> argv;  // empty if unreadable
};

// One terminal's handover data (agent-host's self-update): pty_handles.hpp of this system.
struct PtyHandles;

// One child program in a pseudo terminal. Everything it starts ends with it. Windows: ConPTY and a
// kill-on-close job. macOS: the child leads its own session and process group, which close() and
// terminate() end, and the shell gets SIGHUP when the terminal goes away with the host.
//
// Threads: write/resize/interrupt from one thread at a time; close() never on the reader thread
// (it joins it) and not from inside a sink.
class Pty {
public:
    // Throws std::invalid_argument or std::system_error: an executable that cannot be started
    // (missing, not a program, a Windows batch file), a cwd that is not a folder, a system error.
    static std::unique_ptr<Pty> spawn(const PtySpawnSpec& spec, PtyOutputSink on_output, PtyExitSink on_exit);

    ~Pty();
    Pty(const Pty&) = delete;
    Pty& operator=(const Pty&) = delete;
    Pty(Pty&&) = delete;
    Pty& operator=(Pty&&) = delete;

    void write(std::string_view bytes);  // blocking; throws std::system_error once the terminal is gone
    void resize(PtySize size);
    void interrupt();  // writes 0x03; the terminal turns it into SIGINT for the program in front
    void terminate();  // ends the whole process tree now

    // The root's exit code once it has exited (POSIX: its status, or 128 + signal number).
    [[nodiscard]] std::optional<int> exit_code() const;
    // False when the root has exited and the system cannot know its code (macOS: a root adopted in
    // a handover is not our child); exit_code() is -1 then.
    [[nodiscard]] bool exit_code_known() const;
    // Waits up to `timeout` for the root to exit.
    std::optional<int> wait_exit(std::chrono::milliseconds timeout) const;
    [[nodiscard]] std::uint32_t pid() const noexcept;
    // The root (while it runs) and its descendants still running.
    [[nodiscard]] std::size_t live_processes() const;
    [[nodiscard]] ForegroundInfo foreground() const;
    [[nodiscard]] std::string foreground_program() const;  // ForegroundInfo::program alone

    // Lets the reader drain the last output (bounded wait), joins it, then ends whatever is left
    // in the process group. Idempotent.
    void close();
    [[nodiscard]] bool closed() const;

    // Stops the reader between two reads: every byte it read reached the sink, the rest stays in
    // the terminal. False when it did not stop within `timeout` (call resume_reader()).
    bool pause_reader(std::chrono::milliseconds timeout);
    void resume_reader();

    // ---- agent-host's handover (design doc 7.4 OQ-09, ADR-30, platform-abstraction.md 5.5)
    // With the reader paused: up to `max` bytes the terminal holds now; never waits.
    std::string read_available(std::size_t max);
    // What the new host needs to take this terminal over, still owned by *this; nullopt with `why`
    // when it cannot be handed over.
    std::optional<PtyHandles> handover_handles(std::string* why) const;
    // The new host holds the terminal: close ours without ending anything. Closed afterwards.
    void release_after_handover();
    // In the new host: takes ownership of what the old host passed over after checking it (POSIX: a
    // pty master, and a root that is alive, ours and owns the terminal; Windows: process id, job
    // membership, kill-on-close, OpenConsole). The reader starts paused (resume_reader()). Throws
    // when a check fails; everything passed is closed then.
    static std::unique_ptr<Pty> adopt(const PtyHandles& handles, PtyOutputSink on_output,
                                      PtyExitSink on_exit);

    struct Impl;

private:
    explicit Pty(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

struct PtyCapabilities {
    bool answers_osc7501_query = false;  // the terminal passes the host's query answers through
    bool handover = false;               // agent-host can hand live terminals to a new host
};
PtyCapabilities pty_capabilities();

// True for interactive shells whose newest child counts as "the program in front"
// (zsh, bash, fish, sh, pwsh, ...). `name` is lower-case; ".exe" optional.
bool is_shell_name(std::string_view name);

}  // namespace agentdesk::platform
