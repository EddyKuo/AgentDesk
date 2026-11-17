#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace agentdesk::platform::win {

// Quotes one argument so that CommandLineToArgvW / the MSVC CRT parse it back unchanged.
std::wstring quote_arg(std::wstring_view arg);

// executable is argv[0]; it is always quoted when it contains spaces. Never pass prompts or
// other user text through cmd.exe: .cmd/.bat launchers must go through a dedicated adapter.
std::wstring build_command_line(std::wstring_view executable, const std::vector<std::wstring>& args);

// True when the path names a batch file (.cmd/.bat), which build_command_line must not be
// used for because cmd.exe applies different parsing rules (CVE-2024-24576).
bool is_batch_file(std::wstring_view path);

// True when the path names a file CreateProcess can start: a regular file, or an app execution
// alias (the reparse points under %LOCALAPPDATA%\Microsoft\WindowsApps that Store apps such as
// PowerShell 7 install), which std::filesystem does not report as a regular file.
bool is_launchable_file(const std::wstring& path);

}  // namespace agentdesk::platform::win
