// "auto" Option-as-Alt: on for the US and ABC keyboard layouts, where Option characters are rarely
// typed, off elsewhere (German, French, an IME...). The active input source is asked each time,
// so switching it takes effect at once.
//
// Also keyboard_os.hpp on macOS (FR-40): Cmd is the app's, Control and (as set) Option the program's.
#import <Carbon/Carbon.h>

#include <atomic>
#include <string>

#include "agentdesk/ui/keyboard.hpp"
#include "../keyboard_os.hpp"

namespace agentdesk::ui {

namespace keyboard {

namespace {

std::atomic<bool> g_left{false};
std::atomic<bool> g_right{false};

bool layout_types_with_option_as_alt() {
    TISInputSourceRef source = TISCopyCurrentKeyboardInputSource();
    if (source == nullptr)
        return false;
    bool on = false;
    if (auto id = static_cast<CFStringRef>(TISGetInputSourceProperty(source, kTISPropertyInputSourceID))) {
        char buf[256]{};
        if (CFStringGetCString(id, buf, sizeof buf, kCFStringEncodingUTF8)) {
            const std::string s = buf;
            on = s == "com.apple.keylayout.US" || s == "com.apple.keylayout.USInternational" ||
                 s == "com.apple.keylayout.ABC" || s == "com.apple.keylayout.ABC-India";
        }
    }
    CFRelease(source);
    return on;
}

}  // namespace

void note_option_keys(bool left, bool right) {
    g_left = left;
    g_right = right;
}

bool option_is_alt() {
    const std::string mode = option_as_alt();
    if (mode == "on")
        return true;
    if (mode == "off")
        return false;
    if (mode == "left")
        return g_left.load();
    if (mode == "right")
        return g_right.load();
    return layout_types_with_option_as_alt();
}

}  // namespace keyboard

namespace keyboard_os {

// ImGui swaps Cmd and Ctrl (ConfigMacOSXBehaviors): KeyCtrl is Cmd, KeySuper the Control key.
Modifiers program_modifiers(const ImGuiIO& io) {
    return Modifiers{io.KeySuper, io.KeyShift, io.KeyAlt && keyboard::option_is_alt()};
}

ImGuiKeyChord extra_held(const ImGuiIO& io) {
    return io.KeySuper ? ImGuiMod_Super : 0;
}

bool is_alt_gr(const Modifiers& /*program*/) {
    return false;  // Option text arrives as text (option_as_alt off)
}

bool app_only_modifier(const ImGuiIO& io) {
    return io.KeyCtrl;  // Cmd: an app chord or nothing, never input for the program
}

bool always_program(ImGuiKeyChord held) {
    return (held & ImGuiMod_Super) != 0;  // Control+C: always the program's interrupt
}

}  // namespace keyboard_os

}  // namespace agentdesk::ui
