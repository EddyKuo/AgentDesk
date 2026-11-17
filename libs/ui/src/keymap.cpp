#include "agentdesk/ui/keymap.hpp"

#include <cctype>

namespace agentdesk::ui {

namespace {

struct NamedKey {
    const char* name;
    ImGuiKey key;
    bool shift = false;  // the name implies Shift ("?" is Shift+/)
};

constexpr NamedKey kKeys[] = {
    {"tab", ImGuiKey_Tab},        {"enter", ImGuiKey_Enter},      {"escape", ImGuiKey_Escape},
    {"esc", ImGuiKey_Escape},     {"space", ImGuiKey_Space},      {"backspace", ImGuiKey_Backspace},
    {"insert", ImGuiKey_Insert},  {"delete", ImGuiKey_Delete},    {"home", ImGuiKey_Home},
    {"end", ImGuiKey_End},        {"pageup", ImGuiKey_PageUp},    {"pagedown", ImGuiKey_PageDown},
    {"left", ImGuiKey_LeftArrow}, {"right", ImGuiKey_RightArrow}, {"up", ImGuiKey_UpArrow},
    {"down", ImGuiKey_DownArrow}, {"/", ImGuiKey_Slash},          {"?", ImGuiKey_Slash, true},
    {",", ImGuiKey_Comma},        {".", ImGuiKey_Period},         {"=", ImGuiKey_Equal},
    {"+", ImGuiKey_Equal, true},  {"-", ImGuiKey_Minus},          {"[", ImGuiKey_LeftBracket},
    {"]", ImGuiKey_RightBracket}, {";", ImGuiKey_Semicolon},      {"`", ImGuiKey_GraveAccent},
};

std::string lower(std::string_view s) {
    std::string out(s);
    for (char& c : out)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

std::optional<std::pair<ImGuiKey, bool>> key_by_name(const std::string& name) {
    if (name.size() == 1 && name[0] >= 'a' && name[0] <= 'z')
        return std::pair{static_cast<ImGuiKey>(ImGuiKey_A + (name[0] - 'a')), false};
    if (name.size() == 1 && name[0] >= '0' && name[0] <= '9')
        return std::pair{static_cast<ImGuiKey>(ImGuiKey_0 + (name[0] - '0')), false};
    if (name.size() >= 2 && name[0] == 'f') {
        try {
            const int n = std::stoi(name.substr(1));
            if (n >= 1 && n <= 12)
                return std::pair{static_cast<ImGuiKey>(ImGuiKey_F1 + (n - 1)), false};
        } catch (const std::exception&) {
        }
    }
    for (const auto& k : kKeys) {
        if (name == k.name)
            return std::pair{k.key, k.shift};
    }
    return std::nullopt;
}

}  // namespace

std::optional<ImGuiKeyChord> Keymap::parse(std::string_view text) {
    std::string s = lower(text);
    ImGuiKeyChord mods = 0;
    // Split on '-' but keep a trailing "-" (the minus key): "ctrl--".
    for (;;) {
        const auto dash = s.find('-');
        if (dash == std::string::npos || dash + 1 >= s.size())
            break;
        const std::string part = s.substr(0, dash);
        if (part == "ctrl" || part == "control")
            mods |= ImGuiMod_Ctrl;
        else if (part == "shift")
            mods |= ImGuiMod_Shift;
        else if (part == "alt")
            mods |= ImGuiMod_Alt;
        else
            break;
        s = s.substr(dash + 1);
    }
    const auto key = key_by_name(s);
    if (!key)
        return std::nullopt;
    if (key->second)
        mods |= ImGuiMod_Shift;
    return static_cast<ImGuiKeyChord>(mods | key->first);
}

std::string Keymap::format(ImGuiKeyChord chord) {
    std::string out;
    if (chord & ImGuiMod_Ctrl)
        out += "Ctrl+";
    if (chord & ImGuiMod_Alt)
        out += "Alt+";
    if (chord & ImGuiMod_Shift)
        out += "Shift+";
    const auto key = static_cast<ImGuiKey>(chord & ~ImGuiMod_Mask_);
    for (const auto& k : kKeys) {
        if (k.key == key && !k.shift && std::string_view(k.name).size() == 1) {
            out += k.name;
            return out;
        }
    }
    switch (key) {
        case ImGuiKey_PageUp:
            return out + "PgUp";
        case ImGuiKey_PageDown:
            return out + "PgDn";
        case ImGuiKey_LeftArrow:
            return out + "Left";
        case ImGuiKey_RightArrow:
            return out + "Right";
        case ImGuiKey_UpArrow:
            return out + "Up";
        case ImGuiKey_DownArrow:
            return out + "Down";
        default:
            break;
    }
    const char* name = ImGui::GetKeyName(key);
    out += name != nullptr ? name : "?";
    return out;
}

Keymap Keymap::defaults() {
    Keymap k;
    const std::pair<const char*, const char*> global[] = {
        {"ctrl-shift-t", "tab.new"},
        {"ctrl-shift-n", "space.new"},
        {"ctrl-shift-w", "tab.close"},
        {"ctrl-shift-x", "pane.close"},
        {"ctrl-shift-o", "pane.split:right"},
        {"ctrl-shift-e", "pane.split:down"},
        {"ctrl-shift-z", "pane.zoom"},
        {"ctrl-shift-p", "app.palette"},
        {"ctrl-shift-/", "app.help"},
        {"ctrl-shift-b", "sidebar.toggle"},
        {"ctrl-shift-d", "details.toggle"},
        {"ctrl-shift-g", "worktree.new"},
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
        {"ctrl-alt-left", "pane.resize:left"},
        {"ctrl-alt-right", "pane.resize:right"},
        {"ctrl-alt-up", "pane.resize:up"},
        {"ctrl-alt-down", "pane.resize:down"},
        {"ctrl-=", "app.zoom_in"},
        {"ctrl-shift-=", "app.zoom_in"},
        {"ctrl--", "app.zoom_out"},
        {"ctrl-0", "app.zoom_reset"},
        {"f2", "tab.rename"},
        {"shift-f2", "space.rename"},
    };
    for (const auto& [chord, cmd] : global)
        k.global_[*parse(chord)] = cmd;
    for (int i = 1; i <= 9; ++i) {
        k.global_[*parse("ctrl-" + std::to_string(i))] = "tab.select:" + std::to_string(i);
        k.global_[*parse("alt-" + std::to_string(i))] = "space.select:" + std::to_string(i);
    }
    const std::pair<const char*, const char*> terminal[] = {
        {"ctrl-shift-c", "terminal.copy"},
        {"ctrl-insert", "terminal.copy"},
        {"ctrl-shift-v", "terminal.paste"},
        {"shift-insert", "terminal.paste"},
        {"ctrl-shift-f", "terminal.search"},
        {"ctrl-shift-a", "terminal.select_all"},
        {"shift-pageup", "terminal.scroll:-page"},
        {"shift-pagedown", "terminal.scroll:page"},
        {"ctrl-backspace", "send:\x17"},
        {"ctrl-left",
         "send:\x1b"
         "b"},
        {"ctrl-right",
         "send:\x1b"
         "f"},
    };
    for (const auto& [chord, cmd] : terminal)
        k.terminal_[*parse(chord)] = cmd;
    return k;
}

std::vector<std::string> Keymap::apply(const nlohmann::json& overrides) {
    std::vector<std::string> bad;
    if (!overrides.is_object())
        return bad;
    for (const char* section : {"global", "terminal"}) {
        const auto it = overrides.find(section);
        if (it == overrides.end() || !it->is_object())
            continue;
        auto& target = std::string_view(section) == "global" ? global_ : terminal_;
        for (const auto& [chord_text, cmd] : it->items()) {
            const auto chord = parse(chord_text);
            if (!chord) {
                bad.push_back(chord_text);
                continue;
            }
            if (cmd.is_null() || (cmd.is_string() && cmd.get<std::string>().empty()))
                target.erase(*chord);
            else if (cmd.is_string())
                target[*chord] = cmd.get<std::string>();
            else
                bad.push_back(chord_text);
        }
    }
    return bad;
}

std::optional<std::string> Keymap::global(ImGuiKeyChord chord) const {
    const auto it = global_.find(chord);
    return it == global_.end() ? std::nullopt : std::optional(it->second);
}

std::optional<std::string> Keymap::terminal(ImGuiKeyChord chord) const {
    const auto it = terminal_.find(chord);
    return it == terminal_.end() ? std::nullopt : std::optional(it->second);
}

std::string Keymap::chord_for(std::string_view command) const {
    for (const auto* m : {&global_, &terminal_}) {
        for (const auto& [chord, cmd] : *m) {
            if (cmd == command)
                return format(chord);
        }
    }
    return {};
}

}  // namespace agentdesk::ui
