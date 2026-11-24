#pragma once

// UTF-8 to and from the system's wide text (UTF-16 on Windows, UTF-32 elsewhere).

#include <string>
#include <string_view>

namespace agentdesk::platform {

// Invalid input throws (std::system_error on Windows, std::range_error elsewhere); nothing is
// replaced silently.
std::string to_utf8(std::wstring_view text);
std::wstring to_wide(std::string_view utf8);

// For std::filesystem::path::native(), which is narrow (UTF-8) on POSIX systems.
inline std::string to_utf8(const std::string& text) {
    return text;
}
inline std::string to_utf8(const char* text) {
    return text;
}

}  // namespace agentdesk::platform
