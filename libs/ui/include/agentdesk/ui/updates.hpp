#pragma once

// The app's own updates. An installed copy (the folder unpacked from AgentDesk's portable zip,
// which holds THIRD_PARTY_NOTICES.md) checks <releases>/app/latest.json 10 s after it opens and
// then every hour, downloads a newer zip for this machine, checks its sha256 and unpacks it next
// to itself (<install>.new). The frame then shows "Restart"; the swap happens on restart or the
// next quit, or before opening the next window after a crash.
//
// AgentDesk has no release server of its own: only AGENTDESK_RELEASES_URL turns the checker on;
// without it a build looks for no updates. When it is set, agent-host is checked on the same
// clock (core/latest.json).
// Portions adapted from herdr-app (Apache-2.0) and modified; see THIRD_PARTY_NOTICES.md.

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace agentdesk::ui::updates {

// How often the checker looks, and how soon it tries again after a failed look.
inline constexpr std::chrono::seconds kEvery{60 * 60};
inline constexpr std::chrono::seconds kRetry{30 * 60};

// agent-host as it describes itself, and a newer release waiting.
struct CoreInfo {
    std::optional<std::string> version;
    // A package manager that owns it: never set for agent-host (it ships in the zip).
    std::optional<std::string> managed;
    std::optional<std::string> ready;
    bool updating = false;
    std::optional<std::string> error;
};

// What the settings screen shows.
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
    std::string version;             // this build's version
    std::filesystem::path exe;       // agent-ui.exe, as started
    std::filesystem::path data_dir;  // holds the .updated marker
    std::function<void()> wake;      // something changed: redraw
};

// Called once, first thing in wWinMain.
void configure(Config config);

// --- pure helpers (public for the tests)
// `a` is a later version than `b` (semver; 0.2.0-rc.1 comes before 0.2.0).
bool newer(std::string_view a, std::string_view b);
// The time to record for a check: `now` when it succeeded, else none.
std::optional<std::uint64_t> checked_at(bool succeeded, std::uint64_t now);
// The staged version to switch to at startup: only a newer one, and never right after a relaunch.
std::optional<std::string> startup_target(std::string_view running, const std::optional<std::string>& staged,
                                          const std::optional<std::string>& relaunched);
// Lowercase hex.
std::string sha256(std::string_view bytes);
// <root><suffix> beside it.
std::filesystem::path beside(const std::filesystem::path& root, std::wstring_view suffix);
// A tool from System32 (curl, tar).
std::filesystem::path system(std::wstring_view name);
// "windows-x86_64".
std::string target();
// The install folder for this exe, if it is one.
std::optional<std::filesystem::path> find_root(const std::filesystem::path& exe);

// --- steps (public for the tests)
struct Found {
    bool ok = false;
    std::optional<std::string> version;  // a version is ready
    std::string error;
};
// Downloads the latest release if it's newer than `running`.
Found check_from(const std::filesystem::path& root, const std::string& releases, const std::string& running);
// What `agent-ui --version` says in this folder.
std::optional<std::string> version_of(const std::filesystem::path& app);
// A stage is trusted only with its receipt and a matching executable.
std::optional<std::string> staged_version(const std::filesystem::path& root);
// Moves the install aside and the stage in; puts it back on failure.
bool swap(const std::filesystem::path& root, std::string* error);

// --- the module
std::optional<std::filesystem::path> root();
std::optional<std::string> ready();
CoreInfo core();
// What the connected agent-host says about itself.
void set_core(std::optional<std::string> version, std::optional<std::string> managed);
Status status();
void check_now();
// Starts checking in the background (only with AGENTDESK_RELEASES_URL).
void start();
// Moves the current directory out of the install folder (to the user's profile), so the folder
// can be swapped.
void leave_install_folder();
// True when this process should exit (a newer copy was put in place and started).
bool apply_on_startup();
// Puts the downloaded version in place. False with `error` empty: nothing is ready; false with
// `error` set: the swap failed.
bool apply(std::string* error);
// Opens the app again once this process has exited.
void relaunch();
// The first start after an update.
void after_update();

// ---- agent-host's own update (design doc 7.4 OQ-09)
// The pipe this window talks to ("" = the default one): `agentctl update` (agentctl.exe beside
// agent-ui) updates that agent-host.
void set_pipe(std::string pipe);
// `agentctl update` in the background; the terminals keep running (agent-host hands them to the
// new one) and the window reconnects.
void update_core();
// Runs `agentctl update --json [extra]`. Ok: what agent-host is now ("agent-host 0.2.0"); else
// the error ("message (hint)").
bool run_core_update(const std::vector<std::wstring>& extra, std::string* now, std::string* error);

// What `agentctl update` does before it asks the running agent-host to hand over: find the
// release and put it in place.
struct CoreStageRequest {
    std::string have;                              // the running agent-host's version
    std::filesystem::path host_exe;                // where it runs from (hello "host_exe")
    std::optional<std::string> at_least;           // `--version X`
    std::optional<std::filesystem::path> from;      // `--exe`: an agent-host already on disk
};
struct CoreStaged {
    bool ok = false;
    std::string error;
    std::string hint;
    std::optional<std::filesystem::path> exe;  // hand over to this; nullopt: already up to date
    std::string version;                       // what that is (or `have`)
    bool swapped = false;                      // agent-host.exe moved to .old: unstage_core undoes it
};
CoreStaged stage_core(const CoreStageRequest& request);
// Puts the agent-host.exe that was moved aside back (a handover that failed).
bool unstage_core(const std::filesystem::path& host_exe);
// What `<exe> --version` says ("agent-host 0.2.0" -> "0.2.0").
std::optional<std::string> host_version_of(const std::filesystem::path& exe);

}  // namespace agentdesk::ui::updates
