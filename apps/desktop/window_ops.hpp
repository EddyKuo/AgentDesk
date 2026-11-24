#pragma once

// What the window contents ask of the window they live in: title bar buttons, the window.*
// commands, dragging and the title. On Windows these are the Win32 calls on the HWND (unchanged);
// on macOS (ADR-30) the shell in macos/ provides them for its NSWindow, and the system's own
// window buttons (traffic lights) replace the ones the chrome draws on Windows.

#include <string>

#ifdef _WIN32
#include <windows.h>

#include "agentdesk/platform/win/error.hpp"
#endif

namespace agentdesk::desktop {

#ifdef _WIN32
using NativeWindow = HWND;

inline bool window_is_maximized(NativeWindow w) {
    return ::IsZoomed(w) != FALSE;
}
inline void window_minimize(NativeWindow w) {
    ::ShowWindow(w, SW_MINIMIZE);
}
inline void window_toggle_maximize(NativeWindow w) {
    ::ShowWindow(w, ::IsZoomed(w) ? SW_RESTORE : SW_MAXIMIZE);
}
// Like the window's X: the app decides what closing means (design doc 4.3).
inline void window_close(NativeWindow w) {
    ::PostMessageW(w, WM_CLOSE, 0, 0);
}
// A press on the drag area becomes a caption click, so the system move loop runs (Aero Snap).
inline void window_begin_drag(NativeWindow w) {
    ::ReleaseCapture();
    ::SendMessageW(w, WM_NCLBUTTONDOWN, HTCAPTION, 0);
}
inline void window_set_title(NativeWindow w, const std::string& utf8) {
    ::SetWindowTextW(w, platform::win::to_wide(utf8).c_str());
}
// The chrome draws minimize / maximize / close itself.
inline bool window_draws_own_buttons() {
    return true;
}
// Room the system keeps at the left of the top bar for its own window buttons (logical px).
inline float window_system_buttons_width() {
    return 0.0f;
}
// macOS's Cmd+H / Cmd+Alt+H (keys.macos.json); nothing on Windows.
inline void app_hide(bool /*others*/) {}
#else
using NativeWindow = void*;  // the shell's NSWindow

bool window_is_maximized(NativeWindow w);
void window_minimize(NativeWindow w);
void window_toggle_maximize(NativeWindow w);
void window_close(NativeWindow w);
void window_begin_drag(NativeWindow w);
void window_set_title(NativeWindow w, const std::string& utf8);
bool window_draws_own_buttons();
float window_system_buttons_width();
void app_hide(bool others);  // this app, or every other app
#endif

}  // namespace agentdesk::desktop
