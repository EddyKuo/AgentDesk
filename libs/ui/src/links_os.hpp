#pragma once

// What links do differently per system: links_win.cpp (drive letters, UNC shares, ShellExecute)
// and links_posix.cpp (POSIX paths, /usr/bin/open; ADR-30). CMake builds one; open_link
// (links.hpp) is there too.

#include <optional>
#include <string>

namespace agentdesk::ui::links_os {

// The path a file: URL names on this computer, from its decoded path ("/C:/x", "/Users/x") and
// host ("" for this computer); nullopt when it is not one this computer can open.
std::optional<std::string> local_file_path(std::string path, const std::string& host);

}  // namespace agentdesk::ui::links_os
