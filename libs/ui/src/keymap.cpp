// Keymap: parsing and formatting key chords, the default bindings, and applying keys.json.
// Portions adapted from herdr-app (Apache-2.0) and modified; see THIRD_PARTY_NOTICES.md.
#include "agentdesk/ui/keymap.hpp"

#include <cctype>

#include "keymap_os.hpp"

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
        if (const auto m = keymap_os::modifier(part))
            mods |= *m;
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
    out += keymap_os::modifier_text(chord);
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

namespace {

// Calls f(section, chord text, command) for every default binding, in install order.
template <class F>
void for_each_default(F&& f) {
    for (auto& b : keymap_os::default_bindings())
        f(b.section, std::move(b.chord), std::move(b.command));
}

}  // namespace

const std::map<ImGuiKeyChord, std::string>& Keymap::layout_map() const {
    static const std::map<ImGuiKeyChord, std::string> kNone;
    const auto s = sections_.find("layout:" + layout_);
    return s == sections_.end() ? kNone : s->second;
}

Keymap Keymap::defaults() {
    Keymap k;
    for_each_default([&](const std::string& section, const std::string& chord, std::string cmd) {
        auto& map = section == "global"     ? k.global_
                    : section == "terminal" ? k.terminal_
                                            : k.sections_[section];
        map[*parse(chord)] = std::move(cmd);
    });
    return k;
}

nlohmann::json Keymap::default_file() {
    nlohmann::json doc = nlohmann::json::object();
    for_each_default([&](const std::string& section, const std::string& chord, std::string cmd) {
        doc[section][chord] = std::move(cmd);
    });
    return doc;
}

namespace {

// Command names from older keys.json files, renamed to the current ones.
std::string migrate(std::string a) {
    if (a.rfind("project.", 0) == 0)
        a = "space." + a.substr(8);
    else if (a.rfind("context.", 0) == 0)
        a = "tab." + a.substr(8);
    if (a == "mod.open:start")
        a = "tab.new";
    // herdr-app's names for two commands that are ours under other names.
    if (a == "layout.toggle:sidebar")
        a = "sidebar.toggle";
    if (a == "mod.open:picker")
        a = "app.picker";
    return a;
}

// One keys.json value: unbind ("" or null), an action, or a problem (the text after
// "<section>: <key>: ").
struct EntryValue {
    enum class Kind { kUnbind, kAction, kBad } kind;
    std::string text;
};

EntryValue read_entry(const nlohmann::json& value) {
    if (value.is_null() || (value.is_string() && value.get<std::string>().empty()))
        return {EntryValue::Kind::kUnbind, {}};
    if (value.is_string())
        return {EntryValue::Kind::kAction, value.get<std::string>()};
    if (value.is_object() && value.contains("command") && value["command"].is_string()) {
        std::string action = value["command"].get<std::string>();
        const auto args = value.value("args", nlohmann::json::object());
        if (!args.is_object() || args.size() > 1)
            return {EntryValue::Kind::kBad, "only one argument is supported"};
        for (const auto& [k, v] : args.items())
            action += ":" + (v.is_string() ? v.get<std::string>() : v.dump());
        return {EntryValue::Kind::kAction, std::move(action)};
    }
    return {EntryValue::Kind::kBad, "the action must be text or {\"command\", \"args\"}"};
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
            EntryValue entry = read_entry(value);
            if (entry.kind == EntryValue::Kind::kUnbind) {
                target->erase(*chord);
                continue;
            }
            if (entry.kind == EntryValue::Kind::kBad) {
                bad.push_back(section + ": " + chord_text + ": " + entry.text);
                continue;
            }
            const std::string action = migrate(std::move(entry.text));
            if (action == "terminal") {
                if (section == "global")
                    continue;  // "terminal" only means something where a terminal has focus
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

std::vector<Keymap::Binding> Keymap::listing(const nlohmann::json& file) {
    auto readable = [](const nlohmann::json& j) {
        if (!j.is_object())
            return false;
        for (const auto& [section, entries] : j.items()) {
            if (!entries.is_object())
                return false;
            for (const auto& [k, v] : entries.items())
                if (!v.is_string() && !v.is_object())
                    return false;
        }
        return true;
    };
    const nlohmann::json doc = readable(file) ? file : default_file();
    std::vector<Binding> out;
    bool hatch = false;
    for (const auto& [section, entries] : doc.items()) {
        for (const auto& [k, v] : entries.items()) {
            std::string action = v.is_string() ? migrate(v.get<std::string>()) : v.dump();
            if (section == "global" && lower(k) == "ctrl-alt-shift-escape")
                hatch = true;
            out.push_back({section, k, std::move(action)});
        }
    }
    if (!hatch)
        out.push_back({"global", "ctrl-alt-shift-escape", "app.reset"});
    return out;
}

std::string Keymap::help_label(std::string_view action) {
    if (action.rfind("send:", 0) == 0)
        return "type " + nlohmann::json(std::string(action.substr(5))).dump();
    // Only the first two ':'-separated parts: the name and its argument.
    const auto c = action.find(':');
    std::string name(action.substr(0, c));
    std::string arg;
    if (c != std::string_view::npos) {
        const auto rest = action.substr(c + 1);
        arg = std::string(rest.substr(0, rest.find(':')));
    }
    for (auto& ch : name)
        if (ch == '.' || ch == '_')
            ch = ' ';
    return arg.empty() ? name : name + " " + arg;
}

const char* Keymap::help_group(std::string_view action) {
    auto starts = [&](std::string_view p) { return action.rfind(p, 0) == 0; };
    if (starts("space."))
        return "spaces";
    if (starts("tab."))
        return "tabs";
    if (starts("pane."))
        return "panes";
    if (starts("agent."))
        return "agents";
    if (starts("terminal.") || starts("send:"))
        return "terminal";
    return "window";
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
