#pragma once

// macOS stand-in for the Windows error/text helpers (see README.md in this folder).

#include <string>
#include <string_view>
#include <system_error>

namespace agentdesk::platform::win {

// UTF-8 <-> wide (UTF-32 on macOS). Invalid input throws std::range_error.
std::string to_utf8(std::wstring_view text);
std::wstring to_wide(std::string_view utf8);

// std::filesystem::path::native() is already UTF-8 here.
inline std::string to_utf8(const std::string& text) {
    return text;
}
inline std::string to_utf8(const char* text) {
    return text;
}

}  // namespace agentdesk::platform::win
