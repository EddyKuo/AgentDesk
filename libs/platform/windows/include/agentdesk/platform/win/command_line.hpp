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

}  // namespace agentdesk::platform::win
