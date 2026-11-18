#pragma once

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

namespace agentdesk::host {

struct ToolResult {
    bool started = false;
    unsigned long exit_code = 0;
    bool timed_out = false;
    std::string output;  // stdout and stderr, UTF-8 (git writes UTF-8 by default)
};

// Runs a short-lived helper (git) without a console, waits up to `timeout`, captures output.
// argv[0] is resolved on PATH. Never goes through a shell (design doc 3.2).
ToolResult run_tool(const std::vector<std::wstring>& argv, const std::filesystem::path& cwd,
                    std::chrono::milliseconds timeout);

// True for a folder inside a WSL distribution (\\wsl.localhost\... or \\wsl$\...).
bool is_wsl_path(const std::wstring& path);

// Default checkout location for a new worktree: <base>/<repo folder>/<branch with / -> ->.
std::filesystem::path default_worktree_path(const std::filesystem::path& base,
                                            const std::filesystem::path& repo, const std::string& branch);

}  // namespace agentdesk::host
