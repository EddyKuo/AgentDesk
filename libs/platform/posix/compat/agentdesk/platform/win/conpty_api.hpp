#pragma once

// macOS stand-in for the ConPTY loader (see README.md in this folder). A Unix pty needs no
// bundled component; `bundled` keeps its meaning "the host's OSC 7501 answers reach the program".

#include <filesystem>
#include <string>

namespace agentdesk::platform::win {

struct ConPtyApi {
    bool bundled = true;
    std::wstring source = L"posix pty";
};

inline ConPtyApi load_conpty(const std::filesystem::path& /*exe_dir*/) {
    return {};
}
inline ConPtyApi load_system_conpty() {
    return {};
}

}  // namespace agentdesk::platform::win
