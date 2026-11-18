#pragma once
// Key chords -> command names, herdr's keys.json (spec-settings-fonts.md 3.1). Defaults below;
// %LOCALAPPDATA%\AgentDesk\keys.json overrides them per chord. Sections: "global" (window),
// "terminal" (a terminal has focus), "layout:<name>" (while that layout shows), "mod:<name>"
// (kept for herdr files; mods do not take focus here). More specific wins: terminal > layout >
// global. Actions: a command name, "name:value", {"command": ..., "args": {one value}},
// "send:<text>", or "terminal" (the key goes to the program). "" or null unbinds. Old herdr
// names are migrated (project.* -> space.*, context.* -> tab.*, mod.open:start -> tab.new).
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <imgui.h>
#include <nlohmann/json.hpp>

namespace agentdesk::ui {

class Keymap {
public:
    static Keymap defaults();
    // The defaults as a keys.json document (written when the file is missing, like herdr).
    static nlohmann::json default_file();

    // Applies a keys.json document. Returns its problems (bad chords, unknown sections or
    // actions); when there are any, nothing from the file is installed (herdr keys.rs: the
    // defaults stay). `known` tells whether a command name exists; null accepts all.
    std::vector<std::string> apply(const nlohmann::json& overrides,
                                   const std::function<bool(std::string_view)>& known = {});

    // The layout whose "layout:<name>" section applies.
    void set_layout(std::string name) { layout_ = std::move(name); }

    // Window chord: the layout section, then global. "terminal" there gives nullopt.
    [[nodiscard]] std::optional<std::string> global(ImGuiKeyChord chord) const;
    // Focused-terminal chord: the terminal section only; "terminal" means: send it to the program.
    [[nodiscard]] std::optional<std::string> terminal(ImGuiKeyChord chord) const;
    // First chord bound to `command` (for menus and the help overlay), formatted.
    [[nodiscard]] std::string chord_for(std::string_view command) const;
    [[nodiscard]] const std::map<ImGuiKeyChord, std::string>& global_map() const { return global_; }

    static std::optional<ImGuiKeyChord> parse(std::string_view text);  // "ctrl-shift-t"
    static std::string format(ImGuiKeyChord chord);                    // "Ctrl+Shift+T"

    // One keys.json entry as written, for the Keybinds list (herdr keys.rs listing()).
    struct Binding {
        std::string section;  // "global", "terminal", "layout:<name>", ...
        std::string keys;     // the key text as written ("ctrl-shift-t")
        std::string action;   // "tab.new", "pane.split:right", or an object action as JSON text
    };
    // Every binding of a keys.json document, by section then key (herdr's sorted maps); the
    // defaults when the document cannot be read; the escape hatch (ctrl-alt-shift-escape,
    // app.reset) is always listed.
    static std::vector<Binding> listing(const nlohmann::json& file);
    // herdr help/main.js 17-22: "send:<text>" -> type "<text>" (JSON-quoted); "a.b_c:x" -> "a b c x".
    static std::string help_label(std::string_view action);
    // herdr help/main.js 8-15: spaces, tabs, panes, agents, terminal (also send:), else window.
    static const char* help_group(std::string_view action);

private:
    std::map<ImGuiKeyChord, std::string> global_;
    std::map<ImGuiKeyChord, std::string> terminal_;
    std::map<std::string, std::map<ImGuiKeyChord, std::string>> sections_;  // "layout:x", "mod:x"
    std::string layout_;
};

}  // namespace agentdesk::ui
