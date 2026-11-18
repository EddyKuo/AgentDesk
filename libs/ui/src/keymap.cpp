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
        // herdr terminal.paste_image: on Windows it pastes the clipboard text.
        {"ctrl-v", "terminal.paste"},
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

namespace {

// herdr keys.rs 132-149: names from older releases.
std::string migrate(std::string a) {
    if (a.rfind("project.", 0) == 0)
        a = "space." + a.substr(8);
    else if (a.rfind("context.", 0) == 0)
        a = "tab." + a.substr(8);
    if (a == "mod.open:start")
        a = "tab.new";
    return a;
}

}  // namespace

std::vector<std::string> Keymap::apply(const nlohmann::json& overrides,
                                       const std::function<bool(std::string_view)>& known) {
    std::vector<std::string> bad;
    if (!overrides.is_object()) {
        bad.emplace_back("keys.json must be an object of sections");
        return bad;
    }
    Keymap next = *this;
    for (const auto& [section, entries] : overrides.items()) {
        std::map<ImGuiKeyChord, std::string>* target = nullptr;
        if (section == "global")
            target = &next.global_;
        else if (section == "terminal")
            target = &next.terminal_;
        else if (section.rfind("layout:", 0) == 0 || section.rfind("mod:", 0) == 0)
            target = &next.sections_[section];
        if (target == nullptr || !entries.is_object()) {
            bad.push_back("unknown section \"" + section + "\"");
            continue;
        }
        for (const auto& [chord_text, value] : entries.items()) {
            const auto chord = parse(chord_text);
            if (!chord) {
                bad.push_back(section + ": cannot read the key \"" + chord_text + "\"");
                continue;
            }
            std::string action;
            if (value.is_null() || (value.is_string() && value.get<std::string>().empty())) {
                target->erase(*chord);
                continue;
            }
            if (value.is_string()) {
                action = value.get<std::string>();
            } else if (value.is_object() && value.contains("command") && value["command"].is_string()) {
                action = value["command"].get<std::string>();
                const auto args = value.value("args", nlohmann::json::object());
                if (!args.is_object() || args.size() > 1) {
                    bad.push_back(section + ": " + chord_text + ": only one argument is supported");
                    continue;
                }
                for (const auto& [k, v] : args.items())
                    action += ":" + (v.is_string() ? v.get<std::string>() : v.dump());
            } else {
                bad.push_back(section + ": " + chord_text + ": the action must be text or {\"command\", \"args\"}");
                continue;
            }
            action = migrate(action);
            if (action == "terminal") {
                if (section == "global")
                    continue;  // herdr: skipped in global
            } else if (action.rfind("send:", 0) != 0 && known) {
                const std::string name = action.substr(0, action.find(':'));
                if (!known(name)) {
                    bad.push_back(section + ": " + chord_text + ": unknown action \"" + name + "\"");
                    continue;
                }
            }
            (*target)[*chord] = action;
        }
    }
    if (bad.empty())
        *this = std::move(next);
    return bad;
}

std::optional<std::string> Keymap::global(ImGuiKeyChord chord) const {
    if (const auto s = sections_.find("layout:" + layout_); s != sections_.end()) {
        if (const auto it = s->second.find(chord); it != s->second.end())
            return it->second == "terminal" ? std::nullopt : std::optional(it->second);
    }
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
