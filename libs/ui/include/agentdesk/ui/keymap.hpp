#pragma once
// Key chords -> command names. Defaults below; %LOCALAPPDATA%\AgentDesk\keys.json overrides
// them per chord ("" or null unbinds). Global bindings work everywhere, including while a
// terminal has the keyboard (those chords are not sent to the program); terminal bindings
// only apply to a focused terminal. A command "send:<bytes>" writes bytes to the program.
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

    // Applies {"global": {"ctrl-shift-t": "tab.new", ...}, "terminal": {...}}. Returns the
    // entries that could not be parsed (for a notice), never throws.
    std::vector<std::string> apply(const nlohmann::json& overrides);

    [[nodiscard]] std::optional<std::string> global(ImGuiKeyChord chord) const;
    [[nodiscard]] std::optional<std::string> terminal(ImGuiKeyChord chord) const;
    // First chord bound to `command` (for menus and the help overlay), formatted.
    [[nodiscard]] std::string chord_for(std::string_view command) const;
    [[nodiscard]] const std::map<ImGuiKeyChord, std::string>& global_map() const { return global_; }

    static std::optional<ImGuiKeyChord> parse(std::string_view text);  // "ctrl-shift-t"
    static std::string format(ImGuiKeyChord chord);                    // "Ctrl+Shift+T"

private:
    std::map<ImGuiKeyChord, std::string> global_;
    std::map<ImGuiKeyChord, std::string> terminal_;
};

}  // namespace agentdesk::ui
