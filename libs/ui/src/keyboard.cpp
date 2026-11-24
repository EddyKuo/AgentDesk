// The option_as_alt setting and the input method's composition (keyboard.hpp).
#include "agentdesk/ui/keyboard.hpp"

#include <mutex>

namespace agentdesk::ui::keyboard {

namespace {

std::mutex g_mutex;
std::string g_mode = "auto";

}  // namespace

void set_option_as_alt(std::string_view mode) {
    std::lock_guard lock(g_mutex);
    g_mode = mode == "on" || mode == "off" || mode == "left" || mode == "right" ? std::string(mode) : "auto";
}

std::string option_as_alt() {
    std::lock_guard lock(g_mutex);
    return g_mode;
}

namespace {
std::string g_preedit;  // guarded by g_mutex
}  // namespace

void set_preedit(std::string utf8) {
    std::lock_guard lock(g_mutex);
    g_preedit = std::move(utf8);
}

std::string preedit() {
    std::lock_guard lock(g_mutex);
    return g_preedit;
}

// note_option_keys and option_is_alt differ per system: keyboard_win.cpp, macos/keyboard_macos.mm.

}  // namespace agentdesk::ui::keyboard
