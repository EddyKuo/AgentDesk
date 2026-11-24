#pragma once

// What the host does differently per system, behind one interface: host_os_win.cpp on Windows,
// host_os_posix.cpp on macOS. CMake builds one of them.

#include <cstdint>
#include <exception>
#include <filesystem>
#include <optional>
#include <string>

#include "agentdesk/platform/process.hpp"
#include "agentdesk/platform/pty.hpp"

namespace agentdesk::host::detail {

// `what` (an exception's text) made readable for a reply: Windows describes system errors in the
// ANSI code page, so they are described again in UTF-8; elsewhere `what` as it is.
std::string os_error_text(const std::exception& e, std::string what);

// The environment and arguments that make a shell report its prompt (OSC 133 A) and folder
// (OSC 7), plus what every terminal needs on this system.
void add_folder_hook(platform::PtySpawnSpec& spec);

// The program run_tool starts for `name`, found the way this system's shells do; nullopt when it is
// not installed.
std::optional<std::filesystem::path> find_tool(const std::string& name);
// What this system's helper programs need: environment, git's ownership check, process tree.
void prepare_tool(platform::ProcessSpec& spec);

std::uint32_t current_pid();

// The local folder an OSC 7 URI names, or "" when it is another machine's.
std::string local_path_from_cwd_uri(const std::string& uri);

// The person's home folder (worktrees go in ~/.agentdesk/worktrees).
std::filesystem::path home_dir();

}  // namespace agentdesk::host::detail
