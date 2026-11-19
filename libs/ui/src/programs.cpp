// The program table: which name, icon and tile a terminal shows for what it runs, read from the
// shipped programs.json with the person's file on top.
// Portions adapted from herdr-app (Apache-2.0) and modified; see THIRD_PARTY_NOTICES.md.
#include "agentdesk/ui/programs.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

namespace agentdesk::ui {
namespace {

using Json = nlohmann::json;
using Named = std::pair<const std::string, ProgramEntry>;  // a table entry with its key

// The agents people pick most, first in menus; the rest by name.
constexpr std::array<std::string_view, 9> kAgentsFirst = {"claude", "codex",   "pi",     "opencode", "cursor",
                                                          "copilot", "amp",    "gemini", "grok"};

// The wrapper a remote fish shell runs a command through (compared verbatim).
constexpr std::string_view kFishExec =
    R"(if not command -q -- $argv[2]; printf '\e]2;%s\acannot start "%s": No such file or directory\n' "$AGENTDESK_LAUNCH" $argv[2] >&2; exit 127; end; set -e AGENTDESK_LAUNCH; set -l p $argv[2]; string match -q -- '-*/*' $p; and set p ./$p; exec $p $argv[3..])";

std::string lower(std::string_view s) {
    std::string out(s);
    for (auto& c : out)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

std::optional<std::string> opt_string(const Json& j, const char* key) {
    if (const auto it = j.find(key); it != j.end() && it->is_string())
        return it->get<std::string>();
    return std::nullopt;
}

std::vector<std::string> strings(const Json& j, const char* key) {
    std::vector<std::string> out;
    if (const auto it = j.find(key); it != j.end() && it->is_array())
        for (const auto& v : *it)
            if (v.is_string())
                out.push_back(v.get<std::string>());
    return out;
}

ProgramEntry entry_of(const Json& j) {
    ProgramEntry e;
    e.name = opt_string(j, "name");
    e.icon = opt_string(j, "icon");
    if (const auto it = j.find("tint"); it != j.end() && it->is_boolean())
        e.tint = it->get<bool>();
    e.tile = opt_string(j, "tile");
    e.color = opt_string(j, "color");
    e.also = strings(j, "match");
    e.run = strings(j, "run");
    return e;
}

// The person's fields win where given.
void over(ProgramEntry& e, ProgramEntry mine) {
    if (mine.name)
        e.name = std::move(mine.name);
    if (mine.icon)
        e.icon = std::move(mine.icon);
    if (mine.tint)
        e.tint = mine.tint;
    if (mine.tile)
        e.tile = std::move(mine.tile);
    if (mine.color)
        e.color = std::move(mine.color);
    if (!mine.also.empty())
        e.also = std::move(mine.also);
    if (!mine.run.empty())
        e.run = std::move(mine.run);
}

struct Parsed {
    std::vector<std::string> shells, interpreters;
    std::map<std::string, ProgramEntry> programs;
};

// Throws nlohmann::json::exception on bad JSON, std::runtime_error when it is not an object.
Parsed parse_table(std::string_view text) {
    const Json j = Json::parse(text);
    if (!j.is_object())
        throw std::runtime_error("not an object");
    Parsed p;
    p.shells = strings(j, "shells");
    p.interpreters = strings(j, "interpreters");
    if (const auto it = j.find("programs"); it != j.end() && it->is_object())
        for (const auto& [k, v] : it->items())
            if (v.is_object())
                p.programs[k] = entry_of(v);
    return p;
}

// The file name, no leading dashes ("-zsh"), no ".exe".
std::string_view base(std::string_view s) {
    if (const auto slash = s.find_last_of("/\\"); slash != std::string_view::npos)
        s = s.substr(slash + 1);
    while (!s.empty() && s.front() == '-')
        s.remove_prefix(1);
    if (s.size() >= 4 && lower(s.substr(s.size() - 4)) == ".exe")
        s.remove_suffix(4);
    return s;
}

// PowerShell's single quotes, which include the curly ones.
constexpr std::array<std::string_view, 5> kPsQuotes = {"'", "\xE2\x80\x98", "\xE2\x80\x99", "\xE2\x80\x9A",
                                                       "\xE2\x80\x9B"};

// The UTF-8 character at `i` and its length.
std::string_view char_at(std::string_view s, std::size_t i) {
    const auto b = static_cast<unsigned char>(s[i]);
    const std::size_t n = b < 0x80 ? 1 : (b & 0xE0) == 0xC0 ? 2 : (b & 0xF0) == 0xE0 ? 3 : 4;
    return s.substr(i, std::min(n, s.size() - i));
}

bool is_ps_quote(std::string_view ch) {
    return std::find(kPsQuotes.begin(), kPsQuotes.end(), ch) != kPsQuotes.end();
}

// A color for a program nobody listed, the same every time (FNV-1a of the name).
const char* hash_color(std::string_view s) {
    static constexpr std::array<const char*, 8> kPalette = {"#6d5bd0", "#2f80ed", "#0f9d8a", "#d9822b",
                                                            "#c2417d", "#3a7d44", "#8a5a44", "#4e5d94"};
    std::uint32_t h = 2166136261u;
    for (const unsigned char b : s)
        h = (h ^ b) * 16777619u;
    return kPalette[h % kPalette.size()];
}

// A script's name without its extension (`vite.js` -> `vite`); a module path (`http.server`)
// keeps its dot.
std::string_view script_name(std::string_view name) {
    for (const std::string_view x : {".js", ".mjs", ".cjs", ".ts", ".py", ".rb"})
        if (name.size() > x.size() && name.ends_with(x)) {
            name.remove_suffix(x.size());
            break;
        }
    return name;
}

bool read_file(const std::filesystem::path& f, std::string& out) {
    std::ifstream in(f, std::ios::binary);
    if (!in)
        return false;
    std::stringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

Program shown(const std::string& key, const ProgramEntry& e, std::optional<std::string_view> name, bool shell,
              bool agent) {
    Program p;
    p.key = key;
    p.name = name ? std::string(*name) : e.name.value_or(key);
    p.icon = e.icon.value_or("");
    p.tint = e.tint.value_or(true);
    p.tile = e.tile.value_or("");
    p.color = e.color.value_or("");
    p.shell = shell;
    p.agent = agent;
    return p;
}

}  // namespace

std::string powershell_line(const std::vector<std::string>& words) {
    std::string out = "&";
    for (const auto& w : words) {
        out += " '";
        for (std::size_t i = 0; i < w.size();) {
            const auto ch = char_at(w, i);
            if (is_ps_quote(ch))
                out += ch;  // doubled
            out += ch;
            i += ch.size();
        }
        out += "'";
    }
    return out;
}

std::optional<std::vector<std::string>> powershell_words(std::string_view line) {
    if (!line.starts_with("& "))
        return std::nullopt;
    line.remove_prefix(2);
    std::vector<std::string> words;
    std::size_t i = 0;
    while (i < line.size()) {
        const auto c = char_at(line, i);
        i += c.size();
        if (c == " ")
            continue;
        if (!is_ps_quote(c))
            return std::nullopt;
        std::string w;
        while (i < line.size()) {
            const auto ch = char_at(line, i);
            i += ch.size();
            if (is_ps_quote(ch)) {
                if (i < line.size() && is_ps_quote(char_at(line, i))) {
                    const auto next = char_at(line, i);
                    w += next;
                    i += next.size();
                    continue;
                }
                break;
            }
            w += ch;
        }
        words.push_back(std::move(w));
    }
    return words;
}

Programs Programs::parse(std::string_view json) {
    Parsed p = parse_table(json);
    Programs t;
    t.shells_ = std::move(p.shells);
    t.interpreters_ = std::move(p.interpreters);
    t.programs_ = std::move(p.programs);
    return t;
}

Programs Programs::load(const std::filesystem::path& builtin, const std::filesystem::path& mine,
                        std::string* error) {
    // A broken built-in table leaves it empty; a broken personal one is reported and skipped.
    Programs t;
    std::string text;
    if (read_file(builtin, text)) {
        try {
            t = parse(text);
        } catch (const std::exception&) {
        }
    }
    if (!mine.empty() && read_file(mine, text)) {
        try {
            Parsed p = parse_table(text);
            t.shells_.insert(t.shells_.end(), p.shells.begin(), p.shells.end());
            t.interpreters_.insert(t.interpreters_.end(), p.interpreters.begin(), p.interpreters.end());
            for (auto& [k, e] : p.programs)
                over(t.programs_[k], std::move(e));
        } catch (const std::exception& e) {
            if (error != nullptr)
                *error = std::string("programs.json: ") + e.what();
        }
    }
    return t;
}

bool Programs::is_shell(std::string_view s) const {
    return std::find(shells_.begin(), shells_.end(), s) != shells_.end();
}

bool Programs::is_interpreter(std::string_view s) const {
    return std::find(interpreters_.begin(), interpreters_.end(), s) != interpreters_.end();
}

// An explicit key wins (including the person's own entry), except the
// generic `agent` fallback, which yields to a `match` (Cursor's CLI process is named `agent`).
const Named* Programs::find(std::string_view name) const {
    const std::string l = lower(name);
    const Named* by_key = nullptr;
    const Named* by_match = nullptr;
    for (const auto& kv : programs_)
        if (by_key == nullptr && (kv.first == name || lower(kv.first) == l))
            by_key = &kv;
    for (const auto& kv : programs_) {
        if (by_match != nullptr)
            break;
        for (const auto& a : kv.second.also)
            if (a == name || lower(a) == l) {
                by_match = &kv;
                break;
            }
    }
    if (by_key != nullptr && by_match != nullptr && by_key->first == "agent")
        return by_match;
    return by_key != nullptr ? by_key : by_match;
}

Program Programs::of(std::string_view app, const std::vector<std::string>& argv) const {
    return resolve(app, argv, 0);
}

Program Programs::resolve(std::string_view app, const std::vector<std::string>& all, std::size_t from) const {
    static const ProgramEntry kNone;
    auto entry = [&](const char* k) -> const ProgramEntry& {
        const auto it = programs_.find(k);
        return it != programs_.end() ? it->second : kNone;
    };
    if (!app.empty()) {
        if (const auto* f = find(app))
            return shown(f->first, f->second, std::nullopt, false, true);
        return shown("agent", entry("agent"), app, false, true);
    }
    const std::size_t n = all.size() > from ? all.size() - from : 0;
    auto arg = [&](std::size_t i) -> const std::string& { return all[from + i]; };
    // Commands started through the person's shell: `zsh -ic '"$@"; exec "$0" -i' zsh <cmd>...`,
    // or fish's `command $argv[2..]; exec $argv[1] -i`; login shells use `-lic` and `-li`.
    // The program is the command after the four wrapper words.
    if (n > 4 && is_shell(base(arg(0))) && arg(1).starts_with('-') && arg(1).ends_with('c') &&
        (arg(2).find("\"$@\"") != std::string::npos || arg(2) == kFishExec ||
         arg(2) == "command $argv[2..]; exec $argv[1] -i" || arg(2) == "command $argv[2..]; exec $argv[1] -li"))
        return resolve({}, all, from + 4);
    // On Windows: `pwsh -NoExit -Command "& 'codex' ..."`.
    if (n == 4 && is_shell(base(arg(0))) && arg(1) == "-NoExit" && arg(2) == "-Command")
        if (const auto words = powershell_words(arg(3)))
            return resolve({}, *words, 0);
    if (n == 0)
        return shown("shell", entry("shell"), std::nullopt, true, false);
    const std::string first(base(arg(0)));
    // `node .../vite.js dev`, `npx vite`, `uv run pytest`: the program they run, when it's known.
    if (is_interpreter(first)) {
        for (std::size_t i = 1; i < n; ++i) {
            if (arg(i).starts_with('-'))
                continue;
            if (const auto* f = find(script_name(base(arg(i)))); f != nullptr && !is_interpreter(f->first))
                return shown(f->first, f->second, std::nullopt, false, false);
        }
    }
    if (const auto* f = find(first))
        return shown(f->first, f->second, std::nullopt, false, false);
    if (is_shell(first))
        return shown("shell", entry("shell"), std::string_view(first), true, false);
    ProgramEntry e = entry("program");
    e.tile = hash_color(first);
    e.color = "#ffffff";
    return shown("program", e, std::string_view(first), false, false);
}

std::string Programs::name(std::string_view app) const {
    if (const auto* f = find(app))
        return f->second.name.value_or(f->first);
    return std::string(app);
}

std::vector<Program> Programs::startable() const {
    std::vector<const Named*> keys;
    for (const auto& kv : programs_)
        if (!kv.second.run.empty())
            keys.push_back(&kv);
    auto rank = [](const std::string& k) {
        const auto it = std::find(kAgentsFirst.begin(), kAgentsFirst.end(), k);
        return static_cast<std::size_t>(it - kAgentsFirst.begin());
    };
    std::stable_sort(keys.begin(), keys.end(), [&](auto* a, auto* b) {
        const auto ra = rank(a->first), rb = rank(b->first);
        return ra != rb ? ra < rb : a->first < b->first;
    });
    std::vector<Program> out;
    for (const auto* kv : keys) {
        Program p = shown(kv->first, kv->second, std::nullopt, false, true);
        p.run = kv->second.run;
        out.push_back(std::move(p));
    }
    return out;
}

}  // namespace agentdesk::ui
