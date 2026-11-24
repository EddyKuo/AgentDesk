#pragma once

// macOS stand-in for the Windows process-tree helpers (see README.md in this folder).

#include <string>
#include <vector>

namespace agentdesk::platform::win {

struct Foreground {
    std::string program;
    std::string cwd;
    std::vector<std::string> argv;
};

bool is_shell_name(const std::wstring& name);

}  // namespace agentdesk::platform::win
