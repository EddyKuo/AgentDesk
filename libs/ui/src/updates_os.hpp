#pragma once

// The parts of the updater that differ per system: updates_win.cpp (a folder with agent-ui.exe,
// PowerShell relaunches it) and updates_posix.cpp (an AgentDesk.app bundle, ADR-30). CMake
// builds one of them; target and find_root (updates.hpp) are there too.

#include <filesystem>
#include <optional>
#include <string>

namespace agentdesk::ui::updates::os {

// The install inside a release zip, relative to the zip's top: tools/package names it.
std::wstring packed(const std::string& version);
// The programs inside an install.
std::filesystem::path ui_exe_in(const std::filesystem::path& install);
std::filesystem::path host_exe_in(const std::filesystem::path& install);
// agentctl beside the running agent-ui.
std::filesystem::path ctl_exe_beside(const std::filesystem::path& ui_dir);
// Starts the install again once this process has exited; with `expected` the new process sees
// AGENTDESK_UPDATE_EXPECTED and never retries an update.
bool relaunch(const std::filesystem::path& install, const std::optional<std::string>& expected);
// Two path components are the same name on this system's file system (both ignore case).
bool same_component(const std::filesystem::path& a, const std::filesystem::path& b);

}  // namespace agentdesk::ui::updates::os
