#pragma once

// Reading other processes on macOS: process group members, file name, folder and argv.

#include <cstdint>
#include <optional>
#include <vector>

#include <sys/types.h>

#include "agentdesk/platform/pty.hpp"

namespace agentdesk::platform::posix {

// Live members of a process group (zombies excluded).
std::vector<pid_t> process_group_members(pid_t group);
// Live processes whose controlling terminal is `tty` (a pty slave's device), zombies excluded:
// every job a shell in that terminal started, in whichever process group.
std::vector<pid_t> terminal_members(dev_t tty);
// Live processes of the session `sid` (zombies excluded). Unlike terminal_members, this still finds
// a job after the session leader exited: the kernel then detaches the terminal from the session,
// but the job keeps its session id.
std::vector<pid_t> session_members(pid_t sid);
// True while `pid` is a process that has not exited (a zombie counts as exited).
bool is_running(pid_t pid);
// When `pid` started, in microseconds since the epoch; nullopt when it is gone or not ours to read.
// A zombie still has it. Two readings that differ mean the pid now names another process.
std::optional<std::uint64_t> start_time(pid_t pid);

// Lower-case file name without extension, current folder and argv. Fields we may not read
// (another user's process) stay empty.
ForegroundInfo describe_process(pid_t pid);

}  // namespace agentdesk::platform::posix
