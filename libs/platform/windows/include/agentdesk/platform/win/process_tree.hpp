#pragma once

#include <windows.h>

#include <string>

namespace agentdesk::platform::win {

// Name of the program a terminal is running now: the newest, deepest descendant of `root_pid`
// inside `job` (console hosts excluded), lower-case without ".exe". A shell that started
// `claude` reports "claude"; back at the prompt it reports the shell again. Empty if the job
// has no live process. Cheap enough to call about once per second per session.
std::string foreground_program(HANDLE job, DWORD root_pid);

}  // namespace agentdesk::platform::win
