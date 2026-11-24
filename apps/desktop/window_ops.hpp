#pragma once

// What the window contents ask of the window they live in: title bar buttons, the window.*
// commands, dragging and the title. win/window_ops_win.cpp implements them with Win32 calls on the
// HWND; on macOS (ADR-30) the shell in macos/main_macos.mm does for its NSWindow, and the
// system's own window buttons (traffic lights) replace the ones the chrome draws on Windows.

#include <string>

namespace agentdesk::desktop {

using NativeWindow = void*;  // HWND on Windows, the shell's NSWindow on macOS

bool window_is_maximized(NativeWindow w);
void window_minimize(NativeWindow w);
void window_toggle_maximize(NativeWindow w);
// Like the window's X: the app decides what closing means (design doc 4.3).
void window_close(NativeWindow w);
// A press on the drag area starts the system's move loop (Aero Snap on Windows).
void window_begin_drag(NativeWindow w);
void window_set_title(NativeWindow w, const std::string& utf8);
// Whether the chrome draws minimize / maximize / close itself (Windows) or the system does.
bool window_draws_own_buttons();
// Room the system keeps at the left of the top bar for its own window buttons (logical px).
float window_system_buttons_width();
// How far down from the window's top those buttons reach (logical px; 0 where there are none).
float window_system_buttons_height();
// macOS's Cmd+H / Cmd+Alt+H (keys.macos.json): this app, or every other app; nothing on Windows.
void app_hide(bool others);

}  // namespace agentdesk::desktop
