// keymap_os.hpp on macOS (ADR-30, FR-40): Cmd shortcuts, shown with the system's symbols.
// Portions adapted from herdr-app (Apache-2.0) and modified; see THIRD_PARTY_NOTICES.md.
#include "../keymap_os.hpp"

namespace agentdesk::ui::keymap_os {

namespace {

// macOS defaults: herdr-app's defaults/keys.macos.json, its command names mapped to ours
// (layout.toggle:sidebar -> sidebar.toggle, mod.open:picker -> app.picker). ctrl-1..9 select
// spaces and cmd-1..9 tabs, added by for_each_default.
constexpr std::pair<const char*, const char*> kGlobalMac[] = {
    {"cmd-q", "app.quit"},
    {"cmd-h", "app.hide"},
    {"cmd-alt-h", "app.hide_others"},
    {"cmd-m", "window.minimize"},
    {"cmd-t", "tab.new"},
    {"cmd-w", "tab.close"},
    {"ctrl-tab", "tab.next"},
    {"ctrl-shift-tab", "tab.prev"},
    {"cmd-shift-]", "tab.next"},
    {"cmd-shift-[", "tab.prev"},
    {"cmd-alt-]", "space.next"},
    {"cmd-alt-[", "space.prev"},
    {"cmd-]", "agent.next"},
    {"cmd-[", "agent.prev"},
    {"cmd-shift-l", "layout.switch"},
    {"cmd-n", "space.new"},
    {"cmd-p", "app.picker"},
    {"cmd-shift-p", "app.palette"},
    {"cmd-,", "app.settings"},
    {"cmd-d", "pane.split:right"},
    {"cmd-shift-d", "pane.split:down"},
    {"cmd-shift-w", "pane.close"},
    {"cmd-shift-enter", "pane.zoom"},
    {"cmd-alt-left", "pane.go:left"},
    {"cmd-alt-right", "pane.go:right"},
    {"cmd-alt-up", "pane.go:up"},
    {"cmd-alt-down", "pane.go:down"},
    {"cmd-alt-shift-left", "pane.swap:left"},
    {"cmd-alt-shift-right", "pane.swap:right"},
    {"cmd-alt-shift-up", "pane.swap:up"},
    {"cmd-alt-shift-down", "pane.swap:down"},
    {"f2", "tab.rename"},
    {"shift-f2", "space.rename"},
    {"cmd-/", "app.help"},
    {"cmd-shift-/", "app.help"},
    {"cmd-b", "sidebar.toggle"},
    {"cmd-=", "app.zoom_in"},
    {"cmd-shift-=", "app.zoom_in"},
    {"cmd--", "app.zoom_out"},
    {"cmd-0", "app.zoom_reset"},
    // AgentDesk's own layouts: the Windows letters with Cmd (Cmd+Shift+3 to 5 are screenshots).
    {"cmd-shift-.", "focus.toggle"},
    {"cmd-shift-m", "stage.toggle"},
    {"cmd-shift-i", "inbox.toggle"},
    {"cmd-shift-2", "pair.toggle"},
    {"cmd-shift-r", "review.toggle"},
    {"cmd-shift-h", "timeline.toggle"},
};

constexpr std::pair<const char*, const char*> kTerminalMac[] = {
    {"cmd-f", "terminal.search"},
    {"cmd-a", "terminal.select_all"},
    {"cmd-c", "terminal.copy"},
    {"cmd-v", "terminal.paste"},
    {"ctrl-v", "terminal.paste_image"},
    {"alt-left",
     "send:\x1b"
     "b"},
    {"alt-right",
     "send:\x1b"
     "f"},
    {"alt-backspace", "send:\x17"},
    {"cmd-left", "send:\x01"},
    {"cmd-right", "send:\x05"},
    {"cmd-backspace", "send:\x15"},
};

// Defaults that apply only while one layout shows ("layout:<name>" sections). Windows' Alt keys are
// Cmd+Option here: Option alone types characters. Closing a pane is already Cmd+Shift+W everywhere.
// clang-format off
constexpr struct {
    const char* section;
    const char* chord;
    const char* command;
} kLayoutMac[] = {
    {"layout:review", "f8", "review.next_file"},
    {"layout:review", "shift-f8", "review.prev_file"},
    {"layout:terminals", "cmd-alt-r", "terminals.resize_mode"},
    {"layout:terminals", "cmd-alt-enter", "pane.split:auto"},
    {"layout:terminals", "cmd-alt-g", "terminals.gaps"},
    {"layout:classic", "f10", "classic.menu"},
    {"layout:stage", "cmd-alt-left", "stage.prev"},
    {"layout:stage", "cmd-alt-right", "stage.next"},
    {"layout:pair", "cmd-alt-p", "pair.prompt"},
    {"layout:pair", "cmd-alt-s", "pair.swap"},
};
// clang-format on

}  // namespace

// ImGui swaps Cmd and Ctrl (ConfigMacOSXBehaviors), so Cmd is its "Ctrl" and the Control key its
// "Super".
std::optional<ImGuiKeyChord> modifier(const std::string& name) {
    if (name == "cmd" || name == "command" || name == "super")
        return ImGuiMod_Ctrl;
    if (name == "ctrl" || name == "control")
        return ImGuiMod_Super;
    return std::nullopt;
}

std::string modifier_text(ImGuiKeyChord chord) {
    std::string out;
    // The macOS symbols in the system's order: Control, Option, Shift, Command.
    if (chord & ImGuiMod_Super)
        out += "\xE2\x8C\x83";  // ⌃
    if (chord & ImGuiMod_Alt)
        out += "\xE2\x8C\xA5";  // ⌥
    if (chord & ImGuiMod_Shift)
        out += "\xE2\x87\xA7";  // ⇧
    if (chord & ImGuiMod_Ctrl)
        out += "\xE2\x8C\x98";  // ⌘
    return out;
}

// cmd-1..9 select tabs and ctrl-1..9 spaces.
std::vector<Binding> default_bindings() {
    std::vector<Binding> out;
    for (const auto& [chord, cmd] : kGlobalMac)
        out.push_back({"global", chord, cmd});
    for (int i = 1; i <= 9; ++i) {
        out.push_back({"global", "cmd-" + std::to_string(i), "tab.select:" + std::to_string(i)});
        out.push_back({"global", "ctrl-" + std::to_string(i), "space.select:" + std::to_string(i)});
    }
    for (const auto& [chord, cmd] : kTerminalMac)
        out.push_back({"terminal", chord, cmd});
    for (const auto& d : kLayoutMac)
        out.push_back({d.section, d.chord, d.command});
    return out;
}

}  // namespace agentdesk::ui::keymap_os
