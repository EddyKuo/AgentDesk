// Option-as-Alt (keyboard.hpp). The layout check for "auto" is in macos/keyboard_macos.mm.
#include "agentdesk/ui/keyboard.hpp"

#include <atomic>
#include <mutex>

namespace agentdesk::ui::keyboard {

namespace {

std::mutex g_mutex;
std::string g_mode = "auto";
std::atomic<bool> g_left{false};
std::atomic<bool> g_right{false};

}  // namespace

#ifdef __APPLE__
bool layout_types_with_option_as_alt();  // keyboard_macos.mm
#endif

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

void note_option_keys(bool left, bool right) {
    g_left = left;
    g_right = right;
}

bool option_is_alt() {
#ifdef __APPLE__
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
#else
    return true;
#endif
}

}  // namespace agentdesk::ui::keyboard
