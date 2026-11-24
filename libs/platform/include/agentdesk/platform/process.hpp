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
    bool merge_stderr = false;    // stderr into the stdout pipe (else to /dev/null)
    bool end_with_parent = true;  // ~Process kills it (own process group on macOS)
#ifndef _WIN32
    // When set (and stderr is not merged): the child's stderr goes to this descriptor, which the
    // caller keeps owning (ssh explains failures there).
    int stderr_fd = -1;
#endif
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
DetachedResult spawn_detached(const std::filesystem::path& exe, const std::vector<std::string>& args);

// PATH lookup without running anything. A name with a slash is checked as a path.
std::optional<std::filesystem::path> find_executable(std::string_view name);

// True when `path` names a file this platform can start directly.
bool is_launchable_file(const std::filesystem::path& path);

}  // namespace agentdesk::platform
