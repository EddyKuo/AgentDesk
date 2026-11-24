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

// One child program in a pseudo terminal. Everything it starts ends with it: on macOS the child
// leads its own session and process group, which close() and terminate() end, and the shell gets
// SIGHUP when the terminal goes away with the host.
//
// Threads: write/resize/interrupt from one thread at a time; close() never on the reader thread
// (it joins it) and not from inside a sink.
class Pty {
public:
    // Throws std::invalid_argument for an executable that cannot be started (missing, not a
    // regular file, not executable) or a cwd that is not a folder, std::system_error otherwise.
    static std::unique_ptr<Pty> spawn(const PtySpawnSpec& spec, PtyOutputSink on_output, PtyExitSink on_exit);

    ~Pty();
    Pty(const Pty&) = delete;
    Pty& operator=(const Pty&) = delete;
    Pty(Pty&&) = delete;
    Pty& operator=(Pty&&) = delete;

    void write(std::string_view bytes);  // blocking; throws std::system_error once the terminal is gone
    void resize(PtySize size);
    void interrupt();  // writes 0x03; the terminal turns it into SIGINT for the program in front
    void terminate();  // ends the whole process group now (SIGKILL)

    // The root's exit code once it has exited: its status, or 128 + signal number.
    [[nodiscard]] std::optional<int> exit_code() const;
    // Waits up to `timeout` for the root to exit.
    std::optional<int> wait_exit(std::chrono::milliseconds timeout) const;
    [[nodiscard]] std::uint32_t pid() const noexcept;
    // The root and its descendants still running in the terminal's process group.
    [[nodiscard]] std::size_t live_processes() const;
    [[nodiscard]] ForegroundInfo foreground() const;

    // Lets the reader drain the last output (bounded wait), joins it, then ends whatever is left
    // in the process group. Idempotent.
    void close();
    [[nodiscard]] bool closed() const;

    // Stops the reader between two reads: every byte it read reached the sink, the rest stays in
    // the terminal. False when it did not stop within `timeout` (call resume_reader()).
    bool pause_reader(std::chrono::milliseconds timeout);
    void resume_reader();

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
