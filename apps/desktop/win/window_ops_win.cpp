// window_ops.hpp on Windows: Win32 calls on the HWND.
#include <windows.h>

#include "agentdesk/platform/win/error.hpp"
#include "window_ops.hpp"

namespace agentdesk::desktop {

namespace {

HWND hwnd(NativeWindow w) {
    return static_cast<HWND>(w);
}

}  // namespace

bool window_is_maximized(NativeWindow w) {
    return ::IsZoomed(hwnd(w)) != FALSE;
}

void window_minimize(NativeWindow w) {
    ::ShowWindow(hwnd(w), SW_MINIMIZE);
}

void window_toggle_maximize(NativeWindow w) {
    ::ShowWindow(hwnd(w), ::IsZoomed(hwnd(w)) ? SW_RESTORE : SW_MAXIMIZE);
}

void window_close(NativeWindow w) {
    ::PostMessageW(hwnd(w), WM_CLOSE, 0, 0);
}

// A press on the drag area becomes a caption click, so the system move loop runs (Aero Snap).
void window_begin_drag(NativeWindow w) {
    ::ReleaseCapture();
    ::SendMessageW(hwnd(w), WM_NCLBUTTONDOWN, HTCAPTION, 0);
}

void window_set_title(NativeWindow w, const std::string& utf8) {
    ::SetWindowTextW(hwnd(w), platform::win::to_wide(utf8).c_str());
}

bool window_draws_own_buttons() {
    return true;
}

float window_system_buttons_width() {
    return 0.0f;
}

float window_system_buttons_height() {
    return 0.0f;
}

void app_hide(bool /*others*/) {}

}  // namespace agentdesk::desktop
