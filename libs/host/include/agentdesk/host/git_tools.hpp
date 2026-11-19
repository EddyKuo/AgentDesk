#pragma once

// Short-lived helper processes (git) run by the host, and the folder names of new worktrees.

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

namespace agentdesk::host {

struct ToolResult {
    bool started = false;
    unsigned long exit_code = 0;
    bool timed_out = false;
    std::string output;  // stdout, UTF-8 (git writes UTF-8 by default): what callers parse
    std::string error;   // stderr, kept apart so it can be shown when the command failed
    // The message to show when it failed: stderr, else stdout, trimmed.
    [[nodiscard]] std::string message() const;
};

// Most bytes kept of each stream: high enough to be no cap in practice, but memory stays bounded.
inline constexpr std::size_t kMaxToolOutput = 64u * 1024u * 1024u;

// Runs a short-lived helper (git) without a console, waits up to `timeout`, captures stdout and
// stderr separately. argv[0] is resolved on PATH. Never goes through a shell (design doc 3.2).
ToolResult run_tool(const std::vector<std::wstring>& argv, const std::filesystem::path& cwd,
                    std::chrono::milliseconds timeout);

// True for a folder inside a WSL distribution (\\wsl.localhost\... or \\wsl$\...).
bool is_wsl_path(const std::wstring& path);

// A branch name as a folder name: runs of anything but letters,
// digits, '.', '_' and '-' become '-', trimmed; a name with none left is "branch-<FNV-1a hash>".
std::string worktree_slug(const std::string& branch);

// Default checkout location for a new worktree: <base>/<repo folder>/<worktree_slug(branch)>.
std::filesystem::path default_worktree_path(const std::filesystem::path& base,
                                            const std::filesystem::path& repo, const std::string& branch);

}  // namespace agentdesk::host
