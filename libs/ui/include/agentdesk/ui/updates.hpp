#pragma once

// The app's own updates, ported from herdr-app src/update.rs (Apache-2.0, commit 321c9a8).
// An installed copy (the folder unpacked from AgentDesk's portable zip, which holds
// THIRD_PARTY_NOTICES.md) checks <releases>/app/latest.json a little after it opens and every
// hour, downloads a newer zip for this machine, checks its sha256 and unpacks it next to itself
// (<install>.new). The frame then shows "Restart"; the swap happens on restart or the next quit,
// or before opening the next window after a crash (update.rs 1-7).
//
// herdr's release builds look at releases.herdr.dev; a build from source looks for no updates
// unless HERDR_RELEASES_URL points it at a server (update.rs 9-11, 122-129). AgentDesk has no
// server of its own: only AGENTDESK_RELEASES_URL turns the checker on.
//
// agent-host (herdr: herdr-core) is checked on the same clock (core/latest.json) when that
// variable is set (update.rs 13-15, 205-213).

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace agentdesk::ui::updates {

// update.rs 23-25
inline constexpr std::chrono::seconds kEvery{60 * 60};
inline constexpr std::chrono::seconds kRetry{30 * 60};

// update.rs 83-92: agent-host as it says it is, and a newer release waiting.
struct CoreInfo {
    std::optional<std::string> version;
    // A package manager that owns it: never set for agent-host (it ships in the zip).
    std::optional<std::string> managed;
    std::optional<std::string> ready;
    bool updating = false;
    std::optional<std::string> error;
};

// update.rs 131-141: what the settings screen shows.
struct Status {
    std::string app_version;
    std::optional<std::string> app_ready;
    bool app_updates = false;  // this copy updates itself (an install, pointed at a server)
    CoreInfo core;
    std::optional<std::uint64_t> checked;  // unix seconds of the last successful check
    bool checks = false;                   // the checker runs
    bool checking = false;                 // and is looking right now
};

struct Config {
    std::string version;             // this build (herdr: CARGO_PKG_VERSION)
    std::filesystem::path exe;       // agent-ui.exe, as started
    std::filesystem::path data_dir;  // the .updated marker (herdr: mods::app_dir())
    std::function<void()> wake;      // something changed: redraw
};

// Called once, first thing in wWinMain.
void configure(Config config);

// --- pure helpers (public for the tests)
// update.rs 402-417: `a` is a later version than `b` (semver; 0.2.0-rc.1 comes before 0.2.0).
bool newer(std::string_view a, std::string_view b);
// update.rs 201-203
std::optional<std::uint64_t> checked_at(bool succeeded, std::uint64_t now);
// update.rs 436-441
std::optional<std::string> startup_target(std::string_view running, const std::optional<std::string>& staged,
                                          const std::optional<std::string>& relaunched);
// update.rs 362-365: lowercase hex.
std::string sha256(std::string_view bytes);
// update.rs 50-59: <root><suffix> beside it.
std::filesystem::path beside(const std::filesystem::path& root, std::wstring_view suffix);
// update.rs 353-360: curl and tar from System32.
std::filesystem::path system(std::wstring_view name);
// update.rs 61-70: "windows-x86_64".
std::string target();
// update.rs 34-45: the install folder for this exe, if it is one.
std::optional<std::filesystem::path> find_root(const std::filesystem::path& exe);

// --- steps (public for the tests)
struct Found {
    bool ok = false;
    std::optional<std::string> version;  // a version is ready
    std::string error;
};
// update.rs 269-307: downloads the latest release if it's newer than `running`.
Found check_from(const std::filesystem::path& root, const std::string& releases, const std::string& running);
// update.rs 392-400: what `agent-ui --version` says in this folder.
std::optional<std::string> version_of(const std::filesystem::path& app);
// update.rs 419-434: a stage is trusted only with its receipt and a matching executable.
std::optional<std::string> staged_version(const std::filesystem::path& root);
// update.rs 494-513: moves the install aside and the stage in; puts it back on failure.
bool swap(const std::filesystem::path& root, std::string* error);

// --- the module
std::optional<std::filesystem::path> root();
std::optional<std::string> ready();
CoreInfo core();
// update.rs 107-120: what the connected agent-host says about itself.
void set_core(std::optional<std::string> version, std::optional<std::string> managed);
Status status();
void check_now();
// update.rs 157-199: starts checking in the background (only with AGENTDESK_RELEASES_URL).
void start();
// update.rs 443-457
void leave_install_folder();
// update.rs 459-479: true when this process should exit (a newer copy was put in place and
// started).
bool apply_on_startup();
// update.rs 481-492: puts the downloaded version in place. False with `error` empty: nothing is
// ready (herdr Ok(false)); false with `error` set: the swap failed (herdr Err).
bool apply(std::string* error);
// update.rs 515-518: opens the app again once this process has exited.
void relaunch();
// update.rs 581-621: the first start after an update.
void after_update();

}  // namespace agentdesk::ui::updates
