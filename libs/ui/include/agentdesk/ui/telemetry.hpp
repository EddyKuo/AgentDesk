#pragma once

// Anonymous usage data, ported from herdr-app src/telemetry.rs (Apache-2.0, commit 321c9a8):
// one small report per install per UTC day. Never what's in a terminal, never free text: every
// field is a number, a bucket, a yes/no or a name from a list the app ships (anything else is
// `other`).
//
// The window tells this module what it sees now and then (observe, command, count); a thread of
// its own keeps the day's counts in <data>\telemetry\today.json, freezes a finished day into
// telemetry\queue\ and sends it. Nothing here may affect the app: it runs off the UI thread,
// never waits on anything, and every error just loses data (telemetry.rs 1-10).
//
// On unless ui.json says "telemetry": false (that also deletes the id and anything not sent yet).
// DO_NOT_TRACK, AGENTDESK_TELEMETRY=0 and CI turn it off for one run (telemetry.rs 12-14).
// AgentDesk has no server of its own: like herdr's build from source it collects and shows but
// never sends, unless AGENTDESK_TELEMETRY_URL points it at one (telemetry.rs 14-16, 100-106).

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include <nlohmann/json.hpp>

namespace agentdesk::ui::telemetry {

// herdr's serde_json keeps keys in the order written (preserve_order, via indexmap in Cargo.lock):
// the report shows and goes out in that order.
using Json = nlohmann::ordered_json;

// telemetry.rs 27: the report format.
inline constexpr std::uint32_t kVersion = 1;
// telemetry.rs 30: frozen reports older than this are dropped unsent.
inline constexpr std::int64_t kKeepDays = 7;

// What the window sees, every few seconds while telemetry is on (telemetry.rs 33-49).
struct Observation {
    bool foreground = false;
    // Every terminal reporting an agent: its 7501 `app` (made safe in agent_name).
    std::vector<std::optional<std::string>> agents;
    std::size_t agents_working = 0;
    std::size_t spaces = 0;
    std::size_t tabs = 0;
    std::size_t terminals = 0;
    std::string layout;
    // Mods on screen.
    std::vector<std::string> mods;
    std::string theme;
    // The welcome's outcome ("finished", "skipped").
    std::optional<std::string> welcome;
};

// What the app knows that this module doesn't (herdr reads these from its own modules).
struct Config {
    std::filesystem::path data_dir;            // telemetry\ goes in it (telemetry.rs 77-79)
    std::string app_version;                   // CARGO_PKG_VERSION
    std::vector<std::string> shipped_layouts;  // window::shipped_layouts()
    std::vector<std::string> shipped_themes;   // settings::PRESETS + SHIPPED_THEMES
    // mods::kinds: (shipped as shipped by name, shipped but edited, the person's own).
    std::function<std::tuple<std::vector<std::string>, std::size_t, std::size_t>(
        const std::set<std::string>&)>
        mod_kinds;
    std::function<bool()> keys_custom;                         // keys.json differs from the shipped one
    std::function<std::optional<std::string>()> core_version;  // update::core().version
    std::function<void()> refresh;                             // mods::request(Refresh): redraw
};

// One UTC day's counts, kept locally until the day is over (telemetry.rs 187-210).
struct Day {
    std::string day;
    std::uint64_t open_secs = 0;
    std::uint64_t foreground_secs = 0;
    std::uint32_t startups = 0;
    std::vector<std::uint64_t> startup_ms;
    std::uint32_t unclean_exits = 0;
    std::set<std::string> agents;
    std::size_t agents_open = 0;
    std::size_t agents_working = 0;
    std::size_t spaces = 0;
    std::size_t tabs = 0;
    std::size_t terminals = 0;
    std::set<std::string> layouts;
    std::set<std::string> mods;
    std::set<std::string> commands;
    bool theme_custom = false;
    std::optional<std::string> core;
    std::optional<std::string> welcome;
    std::map<std::string, std::uint32_t> events;

    void observe(Observation o, const std::vector<std::string>& layouts,
                 const std::vector<std::string>& themes);
    // The report for this day: exactly what gets sent.
    [[nodiscard]] Json report(const std::string& install, const Config& config) const;
    [[nodiscard]] Json to_json() const;
    static Day from_json(const Json& j);
};

// Pure helpers (telemetry.rs 273-344, programs.rs 212-237), public for the tests.
std::string bucket(std::size_t n);
std::string time_bucket(std::uint64_t secs);
std::string startup_bucket(std::uint64_t ms);
// A release version (0.2.0, 0.2.0-rc.1), else "other": never a free string.
std::string version(std::string_view v);
// An agent's 7501 `app` as a name herdr's programs table ships, else "other" ("unknown" when it
// gave none).
std::string agent_name(const std::optional<std::string>& app);
// YYYY-MM-DD for days since 1970-01-01 (Howard Hinnant's civil_from_days).
std::string date(std::int64_t days);

// --- the module (telemetry.rs 108-183)
bool collecting();
void observe(Observation o);
// A command the person ran (names from the command list).
void command(std::string name);
// Something that happened, by a fixed name (core_lost, app_update_failed...).
void count(std::string name);
// The window is up: how long the start took.
void window_open();
// ui.json's `telemetry`.
void set_setting(bool on);
// What the settings screen shows (null before the thread published).
Json status();
// A clean quit, handed to the worker (never waited for).
void quit();
// When the app started, for how long the window took.
void clock();
// Starts the thread. `setting`: ui.json's `telemetry`.
void start(Config config, bool setting);
// Ends the thread (process exit; herdr's dies with the process).
void stop();

}  // namespace agentdesk::ui::telemetry
