#pragma once

// macOS stand-in for the Windows command-line helpers (see README.md in this folder). There is no
// command line to quote: programs get their argv directly.

#include <string>
#include <string_view>

namespace agentdesk::platform::win {

// Batch files do not exist here; nothing needs the cmd.exe adapter.
inline bool is_batch_file(std::wstring_view /*path*/) {
    return false;
}
bool is_launchable_file(const std::wstring& path);

}  // namespace agentdesk::platform::win
