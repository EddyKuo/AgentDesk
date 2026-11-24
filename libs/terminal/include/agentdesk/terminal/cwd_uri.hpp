#pragma once

// OSC 7 ("ESC ] 7 ; file://HOST/PATH BEL"): the folder a shell reports at each prompt.
// Converts it to a path Windows programs (git, the Files panel) can open.

#include <string>
#include <string_view>

namespace agentdesk::terminal {

// UTF-8 Windows path for an OSC 7 URI, or "" when it cannot be mapped.
//   file://<local>/C:/x          -> C:\x            (Windows shells)
//   file://<any>/mnt/c/x         -> C:\x            (WSL's view of a Windows drive)
//   file://<local>/c/x           -> C:\x            (MSYS / Git Bash)
//   file://<distro>/home/u       -> \\wsl.localhost\<distro>\home\u   (WSL; AgentDesk's hook
//                                   sends $WSL_DISTRO_NAME as the host)
// <local> is empty, "localhost" or local_host (the computer name), compared case-insensitively.
// The path is percent-decoded; NUL, control characters and ".." segments make it unmappable.
std::string windows_path_from_cwd_uri(std::string_view uri, std::string_view local_host);

// macOS (ADR-30): the absolute POSIX path for an OSC 7 URI from this computer, or "" when the URI
// names another host or cannot be mapped. Same decoding and refusals as above.
//   file://<local>/Users/me/src%20x  -> /Users/me/src x
std::string posix_path_from_cwd_uri(std::string_view uri, std::string_view local_host);

}  // namespace agentdesk::terminal
