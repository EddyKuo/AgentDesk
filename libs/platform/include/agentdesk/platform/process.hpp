#pragma once

// Child processes that are not terminals (git, version checks, ssh bridges, starting agent-host):
// the platform-neutral interface (docs/design/platform-abstraction.md 5.2).

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace agentdesk::platform {

struct ProcessSpec {
    std::filesystem::path executable;                  // started directly, never through a shell
    std::vector<std::string> args;                     // UTF-8, argv[1..]
    std::filesystem::path cwd;                         // empty: inherit
    std::map<std::string, std::string> env_overrides;  // empty value removes the variable
    bool pipe_stdin = false;
    bool pipe_stdout = true;
    bool pipe_stderr = false;   // stderr in a pipe of its own (ssh, git explain failures there)
    bool merge_stderr = false;  // stderr into the stdout pipe (neither: to the null device)
    // ~Process ends it and what it started, and so does our own end (Windows: a kill-on-close job;
    // macOS: its own process group, ended by ~Process only). false: it may outlive us.
    bool end_with_parent = true;
};

// A child with optional pipes. It inherits nothing but the pipes it asked for.
class Process {
public:
    // Throws std::invalid_argument for an executable that cannot be started, std::system_error
    // otherwise.
    static std::unique_ptr<Process> spawn(const ProcessSpec& spec);
    ~Process();
    Process(const Process&) = delete;
    Process& operator=(const Process&) = delete;
    Process(Process&&) = delete;
    Process& operator=(Process&&) = delete;

    bool write(std::string_view bytes);  // false once the child stopped reading
    void close_stdin();
    // Up to `max` bytes of stdout, blocking until some arrive; nullopt at end of stream.
    std::optional<std::string> read_some(std::size_t max);
    std::optional<std::string> read_some_stderr(std::size_t max);  // the same for pipe_stderr

    // Exit code (or 128 + signal) when the child exited within `timeout`.
    std::optional<int> wait(std::chrono::milliseconds timeout);
    void kill();
    [[nodiscard]] std::uint32_t pid() const noexcept;

    struct Impl;
    [[nodiscard]] Impl& impl() noexcept { return *impl_; }

private:
    explicit Process(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

struct RunResult {
    int exit_code = -1;
    std::string out;
    bool timed_out = false;
};
// Runs to completion and collects stdout; kills the child after `timeout`.
RunResult run_capture(const ProcessSpec& spec, std::chrono::milliseconds timeout);

struct DetachedResult {
    std::uint32_t pid = 0;
    bool dies_with_launcher = false;  // Windows only: breakaway from a kill-on-close job denied
};
// A background process that outlives its launcher (agent-host). Its stdio goes to the null device.
// `env_overrides` change its environment only (an empty value removes a variable), never ours.
DetachedResult spawn_detached(const std::filesystem::path& exe, const std::vector<std::string>& args,
                              const std::map<std::string, std::string>& env_overrides = {});

// PATH lookup without running anything (Windows: SearchPathW, ".exe" added to a bare name). A name
// with a slash is checked as a path.
std::optional<std::filesystem::path> find_executable(std::string_view name);

// True when `path` names a file this platform can start directly.
bool is_launchable_file(const std::filesystem::path& path);

// A Windows batch file (.cmd, .bat): only cmd.exe runs it, with its own parsing rules. Never
// elsewhere.
bool is_batch_file(const std::filesystem::path& path);

}  // namespace agentdesk::platform
