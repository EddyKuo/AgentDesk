#pragma once
// Mods: small Lua programs that extend the window (ADR-14). Each mod lives in
// <data>\mods\<name>\main.lua and runs in its own sandboxed Lua state: no io, no os.execute, no
// require or loadfile; os.time/clock/date only. Mods see sessions, react to events, run any
// window command, send input, show notifications, add palette commands, draw simple panels and
// add text to tabs and agent rows. agentdesk.exec starts a program directly (no shell).
//
// The host is ImGui-free: panels come back as widget lists that the app draws, and button
// clicks are fed back for the next draw. UI thread only.
#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace agentdesk::ui {

struct ModSession {
    std::string id;
    std::string title;
    std::string program;
    std::string cwd;
    std::string agent_state;
    std::string process_state;
    std::string machine;
    std::string parent;
    std::string created_at;
    std::map<std::string, std::string> labels;
    bool focused = false;
};

struct ModWidget {
    enum class Kind { kText, kButton, kSeparator, kProgress, kRow, kBadge };
    Kind kind = Kind::kText;
    std::string text;
    std::string text2;  // row: right side
    std::string color;  // "", "dim", "accent", "good", "warn", "bad"
    float value = 0;    // progress 0..1
    std::string id;     // button id
};

struct ModPanel {
    std::string mod;
    std::string id;
    std::string title;
    std::string where;  // "sidebar", "details"
    std::vector<ModWidget> widgets;
};

struct ModCommand {
    std::string mod;
    std::string id;
    std::string label;
};

struct ModInfo {
    std::string name;
    std::string description;  // first comment line of main.lua
    bool enabled = false;
    bool loaded = false;
    std::string error;
};

// Effects requested by mods, applied by the app after the call.
struct ModOutput {
    std::vector<std::string> commands;  // window commands ("tab.new", "agent.focus:<id>", ...)
    struct Send {
        std::string session;
        std::string text;
        bool enter = false;
    };
    std::vector<Send> sends;
    struct Note {
        std::string title;
        std::string body;
    };
    std::vector<Note> notes;
    std::vector<std::vector<std::wstring>> execs;  // argv, started without a shell
};

class ModHost {
public:
    explicit ModHost(std::filesystem::path dir);
    ~ModHost();
    ModHost(const ModHost&) = delete;
    ModHost& operator=(const ModHost&) = delete;

    // (Re)loads every mod folder; only names in `enabled` run.
    void load(const std::set<std::string>& enabled);
    [[nodiscard]] std::vector<ModInfo> mods() const;
    [[nodiscard]] const std::filesystem::path& dir() const { return dir_; }

    void set_sessions(std::vector<ModSession> sessions);
    // Events: "agent_state" {session, state, previous, title}, "session_added" {session},
    // "session_exited" {session, exit_code}, "tick" {} (about once a second).
    void emit(const std::string& event, const nlohmann::json& payload);

    [[nodiscard]] std::vector<ModPanel> panels(const std::string& where);
    void click(const std::string& mod, const std::string& panel, const std::string& widget_id);
    // Text mods add after a tab title ("tab": {title, sessions:[ids]}) or an agent row ("agent":
    // a session), joined with spaces.
    [[nodiscard]] std::string decorate(const std::string& kind, const nlohmann::json& subject);
    [[nodiscard]] std::vector<ModCommand> commands() const;
    void run_command(const std::string& mod, const std::string& id);

    ModOutput take_output();

    struct Mod;  // one Lua state

private:
    friend struct ModHostAccess;  // the Lua C callbacks
    std::filesystem::path dir_;
    std::vector<std::unique_ptr<Mod>> mods_;
    std::vector<ModSession> sessions_;
    ModOutput output_;
    std::set<std::string> clicked_;  // mod/panel/widget, consumed by the next draw
};

}  // namespace agentdesk::ui
