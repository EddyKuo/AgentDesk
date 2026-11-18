#pragma once

#include <windows.h>

#include <string>

namespace agentdesk::platform::win {

// What a terminal is running now. A pseudo console has no foreground process group, so this
// follows herdr's rule: when the root is a shell, the front is the shell's newest direct child
// (console hosts excluded), else the shell itself; a root that is not a shell is the front.
// Grandchildren never take the front, so an agent's helper processes do not replace it.
struct Foreground {
    std::string program;  // lower-case image name without ".exe"; empty if the job is empty
    std::string cwd;      // UTF-8 current folder of that process (empty if unreadable)
};
Foreground foreground(HANDLE job, DWORD root_pid);

// Name only (see Foreground::program).
std::string foreground_program(HANDLE job, DWORD root_pid);

// Current folder of a 64-bit process, read from its PEB (RTL_USER_PROCESS_PARAMETERS
// CurrentDirectory). A trailing backslash is removed except for a drive root. Empty for
// 32-bit (WOW64) processes, processes we cannot open, or on failure.
std::wstring process_cwd(DWORD pid);

// True for interactive shells whose children are "the program in front" (pwsh, powershell, cmd,
// bash, zsh, fish, nu, ...). `name` is lower-case, ".exe" optional.
bool is_shell_name(const std::wstring& name);

}  // namespace agentdesk::platform::win
