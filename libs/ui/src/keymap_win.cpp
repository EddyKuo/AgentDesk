// keymap_os.hpp on Windows: Ctrl shortcuts, shown as "Ctrl+Shift+T".
// Portions adapted from herdr-app (Apache-2.0) and modified; see THIRD_PARTY_NOTICES.md.
#include "keymap_os.hpp"

namespace agentdesk::ui::keymap_os {

namespace {

// The default "global" section on Windows (spec-settings-fonts.md 3.2); ctrl-1..9 and alt-1..9
// are added by for_each_default.
constexpr std::pair<const char*, const char*> kGlobal[] = {
    {"ctrl-shift-t", "tab.new"},
    {"ctrl-shift-n", "space.new"},
    {"ctrl-shift-k", "app.picker"},
    {"ctrl-alt-shift-escape", "app.reset"},
    {"alt-[", "tab.prev"},
    {"alt-]", "tab.next"},
    {"ctrl-shift-w", "tab.close"},
    {"ctrl-shift-x", "pane.close"},
    {"ctrl-shift-o", "pane.split:right"},
    {"ctrl-shift-e", "pane.split:down"},
    {"ctrl-shift-z", "pane.zoom"},
    {"ctrl-shift-p", "app.palette"},
    {"ctrl-shift-/", "app.help"},  // = ctrl-? and ctrl-shift-? (same key)
    {"ctrl-shift-b", "sidebar.toggle"},
    {"ctrl-shift-l", "layout.switch"},
    {"ctrl-,", "app.settings"},
    {"ctrl-tab", "tab.next"},
    {"ctrl-shift-tab", "tab.prev"},
    {"ctrl-pagedown", "tab.next"},
    {"ctrl-pageup", "tab.prev"},
    {"ctrl-shift-pagedown", "space.next"},
    {"ctrl-shift-pageup", "space.prev"},
    {"alt-down", "agent.next"},
    {"alt-up", "agent.prev"},
    {"alt-h", "pane.go:left"},
    {"alt-j", "pane.go:down"},
    {"alt-k", "pane.go:up"},
    {"alt-l", "pane.go:right"},
    {"alt-left", "pane.go:left"},
    {"alt-right", "pane.go:right"},
    {"alt-shift-h", "pane.swap:left"},
    {"alt-shift-j", "pane.swap:down"},
    {"alt-shift-k", "pane.swap:up"},
    {"alt-shift-l", "pane.swap:right"},
    {"ctrl-=", "app.zoom_in"},
    {"ctrl-shift-=", "app.zoom_in"},
    {"ctrl--", "app.zoom_out"},
    {"ctrl-0", "app.zoom_reset"},
    {"f2", "tab.rename"},
    {"shift-f2", "space.rename"},
};

constexpr std::pair<const char*, const char*> kTerminal[] = {
    {"ctrl-shift-c", "terminal.copy"},
    {"ctrl-insert", "terminal.copy"},
    {"ctrl-shift-v", "terminal.paste"},
    {"shift-insert", "terminal.paste"},
    {"ctrl-shift-f", "terminal.search"},
    // terminal.paste_image: the clipboard text, else Ctrl+V so the program reads the clipboard.
    {"ctrl-v", "terminal.paste_image"},
    {"ctrl-backspace", "send:\x17"},
    {"ctrl-left",
     "send:\x1b"
     "b"},
    {"ctrl-right",
     "send:\x1b"
     "f"},
};

}  // namespace

std::optional<ImGuiKeyChord> modifier(const std::string& name) {
    if (name == "ctrl" || name == "control")
        return ImGuiMod_Ctrl;
    return std::nullopt;
}

std::string modifier_text(ImGuiKeyChord chord) {
    std::string out;
    if (chord & ImGuiMod_Ctrl)
        out += "Ctrl+";
    if (chord & ImGuiMod_Alt)
        out += "Alt+";
    if (chord & ImGuiMod_Shift)
        out += "Shift+";
    return out;
}

// ctrl-1..9 select tabs and alt-1..9 spaces.
std::vector<Binding> default_bindings() {
    std::vector<Binding> out;
    for (const auto& [chord, cmd] : kGlobal)
        out.push_back({"global", chord, cmd});
    for (int i = 1; i <= 9; ++i) {
        out.push_back({"global", "ctrl-" + std::to_string(i), "tab.select:" + std::to_string(i)});
        out.push_back({"global", "alt-" + std::to_string(i), "space.select:" + std::to_string(i)});
    }
    for (const auto& [chord, cmd] : kTerminal)
        out.push_back({"terminal", chord, cmd});
    return out;
}

}  // namespace agentdesk::ui::keymap_os
