#pragma once

// Reading other processes on macOS: process group members, file name, folder and argv.

#include <vector>

#include <sys/types.h>

#include "agentdesk/platform/pty.hpp"

namespace agentdesk::platform::posix {

// Live members of a process group (zombies excluded).
std::vector<pid_t> process_group_members(pid_t group);

// Lower-case file name without extension, current folder and argv. Fields we may not read
// (another user's process) stay empty.
ForegroundInfo describe_process(pid_t pid);

}  // namespace agentdesk::platform::posix
