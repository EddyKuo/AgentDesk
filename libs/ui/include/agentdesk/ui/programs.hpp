#pragma once
// What runs in a terminal, as people see it: a name, an icon and a tile. One table: the shipped
// programs.json (next to agent-ui.exe in programs\), with the person's programs.json in the data
// folder on top, entry by entry.
// Portions adapted from herdr-app (Apache-2.0) and modified; see THIRD_PARTY_NOTICES.md.
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace agentdesk::ui {

// One entry of programs.json; unset fields fall back to the shipped entry or a default.
struct ProgramEntry {
    std::optional<std::string> name;
    std::optional<std::string> icon;  // a path relative to the table's folder ("icons/claude.svg")
    std::optional<bool> tint;         // a one-color glyph drawn in `color` (or the text color)
    std::optional<std::string> tile;  // "#rrggbb"
    std::optional<std::string> color;
    std::vector<std::string> also;  // "match": other process names or 7501 app names
    std::vector<std::string> run;   // how to start it (agents people can start from a menu)
};

// A resolved entry, as the UI shows it.
struct Program {
    std::string key;
    std::string name;
    std::string icon;  // relative path, empty: none
    bool tint = true;
    std::string tile;   // "#rrggbb" or empty
    std::string color;  // "#rrggbb" or empty
    bool shell = false;
    bool agent = false;
    std::vector<std::string> run;  // startable() only
};

class Programs {
public:
    // The built-in table from JSON text (the shipped programs.json).
    static Programs parse(std::string_view json);
    // The built-in table from `builtin`, the person's `mine` on top, entry by entry.
    // A problem with the person's file goes to `error` ("programs.json: ..."); the built-in
    // table is used then.
    static Programs load(const std::filesystem::path& builtin, const std::filesystem::path& mine,
                         std::string* error = nullptr);

    // What a terminal shows: from the agent's own report (`app`), else the program it runs.
    [[nodiscard]] Program of(std::string_view app, const std::vector<std::string>& argv) const;
    // How the app calls an agent, from its 7501 `app`.
    [[nodiscard]] std::string name(std::string_view app) const;
    // Every entry with `run`, the most picked first.
    [[nodiscard]] std::vector<Program> startable() const;

    // For tests and the person's overrides.
    std::map<std::string, ProgramEntry>& entries() { return programs_; }

private:
    [[nodiscard]] Program resolve(std::string_view app, const std::vector<std::string>& argv,
                                  std::size_t from) const;
    [[nodiscard]] const std::pair<const std::string, ProgramEntry>* find(std::string_view name) const;
    [[nodiscard]] bool is_shell(std::string_view s) const;
    [[nodiscard]] bool is_interpreter(std::string_view s) const;

    std::vector<std::string> shells_;
    std::vector<std::string> interpreters_;
    std::map<std::string, ProgramEntry> programs_;
};

// `& 'prog' 'arg 1'`, each word quoted for -Command.
std::string powershell_line(const std::vector<std::string>& words);
// The words back from powershell_line (none: not such a line).
std::optional<std::vector<std::string>> powershell_words(std::string_view line);

}  // namespace agentdesk::ui
