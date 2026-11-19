#pragma once

// Win32 error helpers and UTF-8 / UTF-16 conversion.

#include <windows.h>

#include <string>
#include <system_error>

namespace agentdesk::platform::win {

// Must be called on the line right after the failing BOOL API (nothing in between may
// touch the thread's last-error value).
[[noreturn]] inline void throw_last_error(const char* what) {
    const DWORD err = ::GetLastError();
    throw std::system_error(static_cast<int>(err), std::system_category(), what);
}

[[noreturn]] inline void throw_hresult(HRESULT hr, const char* what) {
    throw std::system_error(static_cast<int>(hr), std::system_category(), what);
}

// UTF-16 <-> UTF-8 conversion for Win32 boundaries. Invalid input throws.
std::string to_utf8(std::wstring_view text);
std::wstring to_wide(std::string_view utf8);

}  // namespace agentdesk::platform::win
