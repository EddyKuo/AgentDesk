#pragma once

// Win32 error helpers, with the UTF-8 / UTF-16 conversion (text.hpp) under the names Win32 code uses.

#include <windows.h>

#include <string>
#include <system_error>

#include "agentdesk/platform/text.hpp"

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
using platform::to_utf8;
using platform::to_wide;

}  // namespace agentdesk::platform::win
