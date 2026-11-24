#pragma once

// The key conventions that differ per system: keymap_win.cpp (Ctrl, "Ctrl+Shift+T") and
// macos/keymap_macos.cpp (Cmd, "⇧⌘T", herdr-app's keys.macos.json; FR-40). CMake builds one.

#include <optional>
#include <string>
#include <vector>

#include <imgui.h>

namespace agentdesk::ui::keymap_os {

// The modifier a keys.json name other than "shift" and "alt" stands for ("ctrl"; on macOS also
// "cmd"), or nullopt when it is not one.
std::optional<ImGuiKeyChord> modifier(const std::string& name);
// The chord's modifiers as shown in menus and hints, in this system's order and style.
std::string modifier_text(ImGuiKeyChord chord);

struct Binding {
    const char* section;  // "global", "terminal"
    std::string chord;    // keys.json text
    std::string command;
};
// Every default binding, in install order.
std::vector<Binding> default_bindings();

}  // namespace agentdesk::ui::keymap_os
