#pragma once
// Internal to agent-ui: App::State, the types the window's files share, and the helpers more
// than one of them calls (namespace detail). Not for use outside apps/desktop.
// Portions adapted from herdr-app (Apache-2.0) and modified; see THIRD_PARTY_NOTICES.md.
#include "app.hpp"
#include "safe_read.hpp"
#include "win98_state.hpp"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <chrono>
#include <climits>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <deque>
#include <filesystem>
#include <array>
#include <future>
#include <fstream>
#include <iomanip>
#include <map>
#include <mutex>
#include <set>
#include <span>
#include <sstream>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>
#include <dwmapi.h>
#include <imgui.h>
#include <mmsystem.h>
#include <shellapi.h>

#include "agentdesk/host/git_tools.hpp"
#include "agentdesk/ipc/host_launcher.hpp"
#include "agentdesk/platform/win/command_line.hpp"
#include "agentdesk/platform/win/error.hpp"
#include "agentdesk/ui/chrome_logic.hpp"
#include "agentdesk/ui/git_info.hpp"
#include "agentdesk/ui/image_cache.hpp"
#include "agentdesk/ui/imgui_adapter.hpp"
#include "agentdesk/ui/programs.hpp"
#include "agentdesk/ui/keymap.hpp"
#include "agentdesk/ui/layout.hpp"
#include "agentdesk/ui/mods.hpp"
#include "agentdesk/ui/telemetry.hpp"
#include "agentdesk/ui/terminal_view.hpp"
#include "agentdesk/ui/updates.hpp"
#include "agentdesk/ui/workspace.hpp"

namespace agentdesk::desktop {
// detail stands where app.cpp's anonymous namespace was: its names are visible unqualified
// in agentdesk::desktop, as they were before the split.
namespace detail {}
using namespace detail;

namespace detail {
namespace win = agentdesk::platform::win;
namespace ipc = agentdesk::ipc;
using Json = nlohmann::json;
using Clock = std::chrono::steady_clock;
using ui::Attention;
namespace safe = agentdesk::desktop::safe;
using ui::Dir;

// ------------------------------------------------------------ themes.cpp
// ------------------------------------------------------------------ palettes
// Every colour the chrome and the terminals use. AgentDesk's own palettes come first; the
// preset themes and the theme files (shipped themes\ and <data>\themes\) follow, with their
// chrome colours derived from each theme's background, foreground and ANSI table.
struct Palette {
    const char* id;
    const char* label;
    bool light;
    ImU32 bg, sidebar, card, border, focus, hover, selected, text, dim, faint;
    ImU32 needs_you, done, working, idle, info;
    ImU32 term_bg, term_fg, cursor, selection;
    bool layout_only = false;  // a layout's own theme: not offered in Settings or the palette
    const std::array<ImU32, 16>* ansi = nullptr;  // the theme's 16 ANSI colours (null: VtEngine's)
};
// AgentDesk's own palettes, in the order Settings lists them. The first is the fallback.
std::span<const Palette> builtin_palettes();

// A preset theme or a theme file before its chrome colours are derived (0xRRGGBB values).
struct PresetPalette {
    std::uint32_t background = 0, foreground = 0, selection = 0;
    std::optional<std::uint32_t> cursor;
    std::array<std::uint32_t, 16> ansi{};
    std::optional<std::array<std::uint32_t, 3>> status;  // working, blocked, done
};
// The four preset themes by name. Their ids get kPresetPrefix, since moss and paper are also
// names of our own palettes.
const std::array<std::pair<const char*, PresetPalette>, 4>& preset_palettes();

// The preset themes' ids. Settings written by 0.2.0 and earlier named them with another prefix:
// theme_id() reads "<anything>-<preset>" as the same preset.
inline constexpr std::string_view kPresetPrefix = "preset-";
std::string theme_id(std::string id);
std::vector<const Palette*> all_palettes();
extern const Palette* g_pal;
extern float g_scale;
float px(float v);
ImVec4 v4(ImU32 c);
ImU32 alpha(ImU32 c, int a);
const Palette* palette_by_id(const std::string& id);
bool system_light();
const Palette* effective_palette(const std::string& appearance, const std::string& dark, const std::string& light,
                                 const std::string& layout_theme);
void apply_style(float dpi, float zoom);

// ------------------------------------------------------------ settings_store.cpp
// ------------------------------------------------------------------ settings (ui.json)
struct MachineConfig {
    std::string name;    // shown in the sidebar
    std::string target;  // ssh target: user@host or an ssh config alias
    std::string ctl;     // agentctl on that machine (empty: on PATH)
};

struct Settings {
    std::vector<MachineConfig> machines;
    std::set<std::string> mods_enabled;  // the examples stay off until turned on (Settings > Mods)
    std::string layout = "workspace";
    bool close_notice_shown = false;
    std::string last_cwd;
    std::string theme = "midnight";
    float zoom = 1.0f;
    float font_size = 14.0f;
    bool sounds = true;
    // Read from ui.json and written back, but ignored: the corner note always shows, so Settings
    // no longer offers it.
    bool toasts = true;
    bool copy_on_select = true;
    float scroll_speed = 1.0f;
    bool sidebar = true;
    bool details = false;
    std::string worktree_dir;  // empty: %USERPROFILE%\.agentdesk\worktrees
    std::string shell_profile;  // new spaces/tabs/splits; empty: pwsh, powershell, cmd
    float ui_text = 15.0f;       // interface text size at zoom 1 (13 = the chrome's design size)
    float sidebar_width = 268;   // design px, 44..480; under 160 = rail
    float sidebar_restore = 268; // width the rail toggle goes back to
    float spaces_share = 0.5f;   // spaces / agents split of the sidebar lists
    float panel_width = 368;     // right panel (Files / Changes), 280..900
    std::string agents_sort = "priority";  // or "space"
    std::vector<std::string> palette_recent;  // last 20 palette commands, newest first
    std::string terminal_font;   // font family; "" = JetBrains Mono (bundled)
    int terminal_weight = 500;   // terminal font weight (500: JetBrains Mono Medium)
    std::string ui_font;         // interface font family; "" = Geist (bundled)
    std::string appearance = "system";  // "system", "light" or "dark"
    std::string theme_dark = "midnight";
    std::string theme_light = "paper";
    // A layout is a look: each keeps its own dark and light theme (layout -> {dark, light}).
    // Without an entry the two above are used.
    std::map<std::string, std::pair<std::string, std::string>> layout_themes;
    float line_height = 1.0f;    // terminal cell height multiplier
    float cell_width = 1.0f;     // terminal cell width multiplier
    std::string sound_done;      // MP3 replacing the bundled sounds
    std::string sound_blocked;
    std::string welcomed;        // "", "finished" or "skipped"
    std::string seen_version;    // What's new was shown for this version (once after an update)
    std::vector<std::string> folded_machines;  // machine headers folded in the sidebar
    std::vector<std::string> panels = {"files", "changes"};  // panel buttons in the tab bar
    Json agents_changed = Json::object();  // session id -> {s: state, at: ms since 1970}
    // On unless the file says "telemetry": false; turning it on again removes the key.
    bool telemetry = true;
};
// %AGENTDESK_DATA_DIR%, else %LOCALAPPDATA%\AgentDesk; created when missing.
std::filesystem::path data_dir();
// The folder agent-ui.exe is in (fonts\, sounds\, programs\, themes\, mods\ ship there).
std::filesystem::path exe_dir();
std::pair<std::string, std::string> layout_theme_pair(const Settings& st);
Settings load_settings();
float ui_text_factor(const Settings& s);
float ui_scale(const Settings& s);
extern std::string g_settings_problem;
std::string read_text_file(const std::filesystem::path& p, bool* ok = nullptr);
void save_settings(const Settings& s);

// ------------------------------------------------------------ layouts.cpp
// ------------------------------------------------------------------ layouts
// A layout is a tree of blocks (rows, columns, stacks; the sidebar, tab bar, panes, panel and
// status bar are blocks), parsed by libs/ui layout.hpp. Built-ins are workspace, terminals and
// win98; <data>\layouts\<name>.json adds one or replaces a built-in of that name. The person's
// clicks on a layout (sizes, shown/hidden, a stack's child, where a float sits) live apart from
// the file, in <data>\layouts\.state\<name>.json, so editing the file never loses them.
// AgentDesk's older files ({"sidebar", "tab_bar", "status_bar", "theme", "style", "font_size",
// "details"} and no tree) still load: read as the built-in of their style with those parts hidden
// (layout::from_legacy); the file itself is left as it is.
namespace lay = ui::layout;

struct LayoutSpec {
    std::string name;
    lay::Node file;  // as its file (or the built-in) has it
    lay::Node tree;  // with the person's clicks: what is drawn
    bool builtin = false;
    bool from_file = false;
    bool legacy = false;
    std::optional<bool> details;  // AgentDesk's own details pane (old files only)
    std::string error;            // the file is broken: the built-in shows meanwhile
    std::filesystem::file_time_type mtime{};
    // From the tree.
    std::string theme;         // a palette AgentDesk has, else "" (the person's theme)
    bool look_applies = true;  // false when the layout names a theme AgentDesk lacks
    std::string style;         // "", "terminals" or "win98": which renderers its blocks name
    std::optional<float> font_size;
};
std::filesystem::path layouts_dir();
std::filesystem::path layout_state_path(const std::string& name);
void derive_layout(LayoutSpec& l);
void save_layout_state(const LayoutSpec& l);
std::vector<LayoutSpec> load_layouts(const Settings& st);
// AgentDesk 98 is hidden for now (user's choice, 2026-11-20): its code stays and still builds, but
// the layout is not listed. AGENTDESK_WIN98=1 lists it again (the screenshot checks set it).
bool agentdesk98_shown();
void write_example_mods();

// ------------------------------------------------------------ programs.cpp
// The program table (what a terminal runs: name, icon, colour) and small helpers the drawing
// files share.
extern std::filesystem::file_time_type g_programs_mtime;
std::string load_programs();
ui::Program program_of(const ui::SessionView& v);
ui::Program program_of_profile(const std::string& executable, const std::string& id);
std::filesystem::path program_icon_file(const ui::Program& p);
ImU32 hex_color(const std::string& hex, ImU32 fallback);
extern ui::ImageCache* g_program_images;
void program_icon(ImDrawList* dl, const ui::Program& p, ImVec2 min, float size, ImU32 color);
bool is_shell(const std::string& program);
bool is_shell_profile(const std::string& id);
std::string folder_name(const std::string& cwd);
bool path_within(const std::string& p, const std::string& dir);
std::string display_path(const std::string& cwd);
std::string elapsed_since(const std::string& iso);
float text_clipped(ImDrawList* dl, ImFont* font, float size, ImVec2 pos, ImU32 col, const std::string& text,
                   float max_w);
ImU32 attention_color(Attention a);
Dir parse_dir(const std::string& s);

// ------------------------------------------------------------ commands.cpp
struct CommandInfo {
    const char* id;
    const char* label;
};

inline constexpr CommandInfo kCommands[] = {
    {"tab.new", "New terminal (tab)"},
    {"space.new", "New space (terminal)"},
    {"agent.new", "Start a program... (Claude Code, Codex, ...)"},
    {"pane.split:right", "Split right"},
    {"pane.split:down", "Split down"},
    {"pane.zoom", "Zoom / unzoom pane"},
    {"pane.close", "Close pane (ends its terminal)"},
    {"app.picker", "Find a space or agent"},
    {"sidebar.rail", "Collapse / expand the sidebar"},
    {"worktree.open", "Open worktree"},
    {"worktree.remove", "Remove worktree"},
    {"machine.add", "Add machine"},
    {"panel:files", "Show / hide the Files panel"},
    {"app.welcome", "Welcome"},
    {"app.whats_new", "What's new"},
    {"app.feedback", "Send feedback"},
    {"tab.settle", "Settle tab (put it away, it keeps running)"},
    {"tab.unsettle", "Bring back tabs put away"},
    {"terminal.right_click:program", "Send right-clicks to the pane"},
    {"terminal.right_click:menu", "Use the AgentDesk right-click menu"},
    {"layout.clone", "Duplicate layout"},
    {"layout.reset", "Reset layout"},
    {"mod.clone", "Duplicate mod"},
    {"app.reset", "Reset to the default screen"},
    {"terminal.kill", "End the focused terminal and its sub-agents"},
    {"panel:changes", "Show / hide the Changes panel"},
    {"pane.rename", "Rename pane"},
    {"tab.rename", "Rename tab"},
    {"space.rename", "Rename space"},
    {"tab.close", "Close tab (ends its terminals)"},
    {"space.close", "Close space (ends its terminals)"},
    {"tab.move:left", "Move tab left"},
    {"tab.move:right", "Move tab right"},
    {"tab.next", "Next tab"},
    {"tab.prev", "Previous tab"},
    {"space.next", "Next space"},
    {"space.prev", "Previous space"},
    {"agent.next", "Next agent"},
    {"agent.prev", "Previous agent"},
    {"pane.go:left", "Focus pane left"},
    {"pane.go:right", "Focus pane right"},
    {"pane.go:up", "Focus pane up"},
    {"pane.go:down", "Focus pane down"},
    {"pane.swap:left", "Swap pane left"},
    {"pane.swap:right", "Swap pane right"},
    {"pane.swap:up", "Swap pane up"},
    {"pane.swap:down", "Swap pane down"},
    {"terminal.search", "Find in terminal"},
    {"worktree.new", "New git worktree"},
    {"session.interrupt", "Send Ctrl+C to the focused session"},
    {"session.stop", "Stop the focused session"},
    {"session.force_stop", "Force stop the focused session"},
    {"session.restart", "Restart: new session in the same folder"},
    {"sidebar.toggle", "Show / hide sidebar"},
    {"details.toggle", "Show / hide details"},
    {"app.palette", "Command palette"},
    {"app.settings", "Settings"},
    {"app.help", "Keyboard shortcuts"},
    {"app.zoom_in", "Zoom in"},
    {"app.zoom_out", "Zoom out"},
    {"app.zoom_reset", "Reset zoom"},
    {"app.keys", "Edit keys.json"},
    {"layout.switch", "Next layout"},
    {"mods.reload", "Reload mods and layouts"},
    {"mods.folder", "Open the mods folder"},
    {"app.exit_all", "Exit and stop all sessions"},
};
bool known_command(std::string_view name);

// ------------------------------------------------------------ git_cache.cpp
// ------------------------------------------------------------------ git facts per folder
// The checkout's top, its common dir (worktrees), the branch and ahead/behind of upstream, for
// the sidebar's spaces. One background thread runs git for queued folders; a second watches each
// local repo's git folder, so a commit or checkout made outside shows at once. Facts are asked
// again every 60 s, but only while the window is in front.

// Calls another machine's agent-host (fs.list, fs.read, git.run). Runs on a worker thread;
// returns the result, or nullopt when the machine did not answer.
using RemoteCall =
    std::function<std::optional<Json>(const std::string& machine, const std::string& method, const Json& params)>;
// git `args` in `cwd` on `machine` through its agent-host's git.run (it adds its own safety
// switches and -C). Not started when the machine did not answer.
host::ToolResult remote_git(const RemoteCall& remote, const std::string& machine, const std::string& cwd,
                            const std::vector<std::wstring>& args);

struct GitInfo {
    bool repo = false;
    std::string top;         // checkout top (a worktree's own folder)
    std::string common_top;  // main checkout's top (differs for a linked worktree)
    std::string common_dir;  // the repo's own git folder (`git rev-parse --git-common-dir`)
    bool worktree = false;
    std::string branch;      // "HEAD" when detached
    int ahead = 0;
    int behind = 0;
};

class GitCache {
public:
    GitCache()
        : wake_(::CreateEventW(nullptr, FALSE, FALSE, nullptr)),
          worker_([this](std::stop_token st) { run(st); }),
          watcher_([this](std::stop_token st) { watch_loop(st); }) {}
    ~GitCache();
    GitCache(const GitCache&) = delete;
    GitCache& operator=(const GitCache&) = delete;

    // Another machine's folders: git through its agent-host (git.run), set by the app.
    RemoteCall remote;

    // Cached facts for `dir` on `machine` ("" = here; empty until the first answer); schedules a
    // refresh when stale.
    GitInfo get(const std::string& dir, const std::string& machine = {});
    // Ask again soon (a command or an agent turn ended: a checkout or commit may have happened).
    void invalidate(const std::string& dir);
    // `dir` and every other folder in its repo (a commit there moves their ahead/behind too).
    void invalidate_repo(const std::string& dir, const std::string& machine = {});
    // The window is in front: the 60 s fallback runs only then.
    void set_active(bool active) { active_ = active; }

private:
    struct Entry {
        GitInfo info;
        Clock::time_point at{};
        bool queued = false;
        Clock::time_point used{};  // last asked for by the UI
    };

    // Entries are keyed by "<machine>|<dir>", or just <dir> on this computer.
    static std::string key_of(const std::string& dir, const std::string& machine);
    static std::string machine_of(const std::string& key);
    static std::string dir_of(const std::string& key);

    // Local repos' git folders, each with a change notification. A change looks again at every folder of that repo; folders no longer asked about for
    // 10 minutes stop being watched. At most 62 at once (WaitForMultipleObjects).
    void watch_loop(std::stop_token st);

    static std::string trim(std::string s);
    static std::string norm(std::string p);

    GitInfo query(const std::string& key);

    void run(std::stop_token st);

    std::mutex mutex_;
    std::condition_variable_any cv_;
    std::deque<std::string> queue_;
    std::map<std::string, Entry> entries_;
    std::atomic<bool> active_{true};
    HANDLE wake_ = nullptr;
    std::jthread worker_;
    std::jthread watcher_;
};

// ------------------------------------------------------------ side_panels.cpp
// ------------------------------------------------------------------ Files / Changes panels
// Both panels follow the focused pane's live folder, refresh every 10 s and when the folder
// changes on disk, and never write under .git while reading. Git and file reads run on one
// worker thread.
class SidePanels {
public:
    struct FileEntry {
        std::string name;
        bool dir = false;
    };
    struct Change {
        char letter = 'M';
        std::string path;  // relative to the repo top, '/' separators
        int add = 0;
        int del = 0;
        bool binary = false;
        bool untracked = false;
    };
    struct DiffRow {
        enum Kind { kContext, kAdd, kDel, kHunk, kOther } kind = kContext;
        int number = 0;
        std::string text;
    };

    SidePanels() : worker_([this](std::stop_token st) { run(st); }) {}
    ~SidePanels();
    SidePanels(const SidePanels&) = delete;
    SidePanels& operator=(const SidePanels&) = delete;

    // Folders on another machine go through its agent-host (fs.list, fs.read, git.run).
    RemoteCall remote;

    std::mutex mutex;  // guards everything below
    // Files
    std::string files_root;
    std::string files_machine;  // "" = this computer
    std::set<std::string> expanded;                  // relative folder paths ("" = root)
    std::map<std::string, std::vector<FileEntry>> listing;
    std::map<std::string, char> marks;               // relative file path (lower case) -> A/M/D/U
    std::string files_error;
    bool files_loading = false;
    bool files_limited = false;
    std::string preview_path;  // relative; empty = tree
    std::string preview_text;
    std::string preview_message;
    // Selection in the read-only preview: (line, byte) ends; line -1 = none.
    int sel_line_a = -1;
    std::size_t sel_byte_a = 0;
    int sel_line_b = -1;
    std::size_t sel_byte_b = 0;
    bool sel_dragging = false;
    // Changes
    std::string changes_cwd;
    std::string changes_machine;
    std::string repo_top;
    bool not_repo = false;
    std::vector<Change> changes;
    bool changes_more = false;
    std::string changes_error;
    bool changes_loading = false;
    std::string open_diff;  // path of the open file
    bool user_closed = false;
    std::vector<DiffRow> diff;
    std::string diff_message;
    std::string commit_note;
    bool committing = false;
    char commit_msg[512] = {};

    // Called every frame with the current folder; refreshes when it changed or every 10 s.
    void track_files(const std::string& root, const std::string& machine = {});
    void track_changes(const std::string& cwd, const std::string& machine = {});
    void toggle_folder(const std::string& rel);
    void open_file(const std::string& rel);
    void close_preview();
    void select_change(const std::string& path);
    void commit();

private:
    static std::string join(const std::string& root, const std::string& rel);
    static std::string lower(std::string s);
    static constexpr std::size_t kPerFolder = 500;
    static constexpr std::size_t kTotal = 5000;

    // Hidden and system files included, only .git skipped, folders first then by name.
    std::pair<std::vector<FileEntry>, bool> list_folder(const std::string& mc, const std::string& root,
                                                        const std::string& rel, bool* failed = nullptr);

    // git in `cwd`, here or on machine `mc` (its agent-host runs it with the same switches).
    host::ToolResult git_run(const std::string& mc, const std::string& cwd, std::vector<std::wstring> args);
    std::string git_out(const std::string& mc, const std::string& cwd, std::vector<std::wstring> args,
                        bool* ok = nullptr);
    std::string top_of(const std::string& mc, const std::string& cwd);
    // porcelain v1 -z: "XY path\0"
    std::vector<std::pair<std::string, std::string>> status(const std::string& mc, const std::string& top, bool* ok);
    // Changes list letter: conflicts (U) first, then D, A, M.
    static char letter_of(const std::string& xy);
    // Files tree mark: added first, then U, D, M.
    static char file_mark_of(const std::string& xy);

    void load_files(const std::string& mc, const std::string& root, std::set<std::string> folders,
                    const std::string& preview);

    bool read_text(const std::string& mc, const std::string& path, std::string& out, std::string& message);

    void load_preview(const std::string& mc, const std::string& root, const std::string& rel);

    void load_changes(const std::string& mc, const std::string& cwd, std::string open, bool closed);

    static std::string untab(const std::string& s);
    static std::string cut(std::string s);

    void load_diff(const std::string& mc, const std::string& top, const Change& c);

    // A recursive change notification on the watched folder (150 ms batching).
    void watch(const std::string& dir);

    enum class Panel { kFiles, kChanges };
    // Called with `mutex` held (from the public calls above).
    void post(Panel panel, std::function<void()> fn);
    // A task that threw (a name that does not convert, an answer of an unexpected shape): its
    // panel says so and stops loading, and the worker goes on.
    void task_failed(Panel panel, const char* what);

    void run(std::stop_token st);

    std::map<std::string, std::pair<std::set<std::string>, std::string>> files_contexts_;
    Clock::time_point files_at_{};
    Clock::time_point changes_at_{};
    std::atomic<bool> files_changed_{false};
    std::atomic<bool> changes_changed_{false};
    std::string watch_dir_;
    struct Task {
        Panel panel = Panel::kFiles;
        std::function<void()> fn;
    };
    std::deque<Task> tasks_;
    std::condition_variable_any cv_;
    std::jthread worker_;
};
}  // namespace detail

// ------------------------------------------------------------------ state
struct ViewState {
    ui::TerminalView view;
    bool focus_next = false;
};

struct Toast {
    std::string title;
    std::string body;
    std::string session;
    ImU32 color = 0;
    Clock::time_point until;
};

enum class LaunchTarget { kTab, kSpace, kSplitRight, kSplitDown };

struct Placement {
    std::string machine;  // "" = this PC
    std::string session;
    LaunchTarget target = LaunchTarget::kTab;
    int space = -1;
    std::string space_id;  // the space by id: indices move while the create is pending
    std::string split_of;
    std::string replace;  // restart in place
    std::string space_name;
    std::string tab_name;  // the new tab's name (AgentDesk 98 names its tabs)
};

struct WorktreeEntry {
    std::string path;
    std::string branch;  // empty: detached
    bool main = false;
    long long time = 0;  // last commit (unix seconds)
    std::string age;     // "5m", "3h", "2d", "4mo"
};

struct Remote {
    MachineConfig cfg;
    std::shared_ptr<ui::HostClient> host;  // shared: the panel worker may hold it during a request
};

struct App::State {
    AppConfig config;
    std::unique_ptr<ui::HostClient> host;           // this PC
    std::vector<Remote> remotes;                    // other machines over ssh
    std::mutex remotes_mutex;                       // remotes is replaced on the UI thread, read by the panel worker
    std::map<std::string, std::string> machine_of;  // session id -> machine name ("" here)
    std::string launch_machine;
    Settings settings;
    ui::Keymap keymap;
    std::vector<std::string> keymap_errors;
    std::filesystem::file_time_type keys_mtime{};
    Clock::time_point keys_checked{};
    ui::FontSet fonts;
    float dpi = 1.0f;
    bool window_focused = true;
    std::atomic<bool> exit_now{false};  // also set by shutdown_host's answer (a HostClient thread)
    // Updates and Privacy.
    std::optional<Clock::time_point> updates_asked;  // Settings asked for an update check (polls while set)
    bool show_data = false;                          // Privacy shows what telemetry would send
    std::optional<std::string> update_dismissed;     // the version whose update notice was closed
    std::optional<std::string> core_update_error_dismissed;  // the agent-host update error closed
    std::string noted_host;                          // host instance whose version updates.cpp has
    bool was_connected = false;
    bool window_opened = false;
    Clock::time_point observed_at{};
    ImFont* report_font = nullptr;  // JetBrains Mono for the telemetry report lines

    ui::Workspace ws;
    std::set<std::string> hidden;  // sessions the user closed the pane of (still running)
    std::set<std::string> known;   // placed, hidden, or deliberately skipped
    bool doc_loaded = false;
    std::string last_doc;
    Clock::time_point doc_dirty_at{};
    bool doc_dirty = false;
    std::string last_sent_doc;

    std::map<std::string, ViewState> views;
    ui::AttentionTracker attention;
    std::map<std::string, std::string> prev_process_state;
    std::map<std::string, Clock::time_point> launched_here;
    // Created by this window but not yet in the host's session list (the create reply comes
    // before the next list refresh): kept in the layout for a while.
    std::map<std::string, Clock::time_point> awaiting_list;
    std::deque<Toast> toasts;
    std::vector<std::string> agent_order;
    std::string peek;  // sub-agent shown in the floating peek window
    std::unique_ptr<ui::ModHost> mods;
    std::vector<std::string> pending_mod_commands;
    std::vector<LayoutSpec> layouts;
    std::map<std::string, std::string> last_agent_state;  // for mod events
    Clock::time_point next_tick{};
    [[nodiscard]] const LayoutSpec& layout() const {
        for (const auto& l : layouts)
            if (l.name == settings.layout)
                return l;
        return layouts.front();
    }
    LayoutSpec& layout() {
        return const_cast<LayoutSpec&>(std::as_const(*this).layout());
    }
    ui::layout::Placement placed;  // the shown layout, as this frame drew it
    struct LayoutDrag {
        std::string container;
        int index = 0;
        ui::layout::Axis axis = ui::layout::Axis::kRow;
    };
    std::optional<LayoutDrag> layout_drag;  // a layout separator held down

    std::mutex placements_mutex;
    std::vector<Placement> placements;      // completed creates waiting for the UI thread
    std::vector<std::string> async_errors;  // from worker callbacks

    // per-frame
    std::vector<std::shared_ptr<ui::RemoteSession>> sessions;
    std::map<std::string, ui::SessionView> views_by_id;
    std::map<std::string, std::shared_ptr<ui::RemoteSession>> by_id;
    ui::Rect pane_area{};
    bool started_dev = false;
    std::size_t next_dev_command = 0;
    Clock::time_point next_dev_at{};
    std::size_t next_input = 0;  // --input step
    Clock::time_point input_at{};
    bool input_shot_wait = false;
    std::optional<ImVec2> input_mouse;  // last scripted mouse position, re-applied every frame
    int input_up = -1;                  // button to release on a later frame (a real click spans frames)
    int input_clicks_left = 0;          // dblclick: the second press
    Clock::time_point input_shot_since{};

    // overlays
    bool show_palette = false;
    char palette_buf[256] = {};
    int palette_sel = 0;
    bool show_help = false;
    bool show_settings = false;
    bool show_exit = false;
    std::string exit_error;     // guarded by placements_mutex (shutdown_host answers on its thread)
    bool exit_pending = false;  // "Stop all and exit" sent, no answer yet; guarded by placements_mutex
    bool show_close_notice = false;
    bool show_launcher = false;
    LaunchTarget launch_target = LaunchTarget::kTab;
    std::string launch_split_of;
    char launch_cwd[512] = {};
    char launch_name[128] = {};
    int launcher_sel = 0;
    std::string launch_error;
    bool launch_busy = false;
    bool show_rename = false;
    std::string rename_kind;  // tab, space, pane
    std::string rename_target;
    char rename_buf[128] = {};
    bool rename_open = false;   // the prompt is showing
    bool rename_close = false;  // close it on the next frame (rename pressed again)
    bool show_worktree = false;
    char wt_repo[512] = {};
    char wt_branch[128] = {};
    int wt_profile = 0;
    std::string wt_error;
    bool wt_busy = false;
    bool open_popup_next = false;
    std::string popup_to_open;
    // chrome
    GitCache git;
    std::unique_ptr<SidePanels> panels = std::make_unique<SidePanels>();
    std::set<std::string> folded_machines;
    ImFont* base_ui = nullptr;  // the bundled interface fonts (Geist), restored when ui_font is cleared
    ImFont* base_ui_medium = nullptr;
    ImFont* base_ui_bold = nullptr;
    struct TabWheel {
        float acc = 0;
        int steps = 0;
        Clock::time_point last{}, switched{};
    } tab_wheel;
    std::set<std::string> folded_groups;
    std::set<std::string> folded_agents;
    // Each agent's last state and when it changed (ms since 1970), kept across restarts in
    // ui.json (agents_changed).
    struct Changed {
        std::string s;
        long long at = 0;
    };
    std::map<std::string, Changed> changed;
    bool changed_loaded = false;
    std::map<std::string, bool> busy_terminal;  // a command or an agent turn running
    std::string spaces_revealed;                // active space last brought into view
    int spaces_revealed_at = -1;
    float spaces_list_h = 0;
    int drag_space = -1;
    int drag_tab = -1;
    std::string drag_pane;          // pane being dragged by its grip
    ImVec2 drag_pane_from{};
    std::map<std::string, ui::Rect> pane_rects;  // panes of the shown tab, this frame
    float tab_scroll = 0;
    bool show_confirm = false;
    bool show_picker = false;
    char picker_buf[256] = {};
    int picker_sel = -1;
    bool picker_reset = true;
    bool picker_earlier = false;
    char help_buf[128] = {};
    std::string settings_section;
    float settings_row_end = 0;
    float settings_row_x = 0;
    bool show_welcome = false;
    bool welcome_panels_seen = false;
    bool welcome_copied = false;
    bool show_notes = false;  // What's new / Send feedback dialog
    std::string notes_kind;   // "whats_new" or "feedback"
    char feedback_buf[4096] = {};
    std::string feedback_note;
    int welcome_page = 0;
    ImVec2 welcome_pos{-1.0f, -1.0f};  // top-left once the person dragged it
    bool show_machine_add = false;
    std::vector<std::pair<std::string, std::string>> ssh_hosts;  // alias, what it reaches
    // Add a machine: target -> "adding" or why it could not be added.
    std::map<std::string, std::string> machine_add_state;
    std::set<std::string> machine_add_pending;  // needs the person to answer something first
    std::string machine_add_copied;
    char machine_buf[256] = {};
    std::string confirm_title, confirm_detail, confirm_ok, confirm_command;
    std::string settings_tab;  // tab to select when the settings window opens
    bool show_worktree_open = false;
    std::vector<WorktreeEntry> worktree_list;
    std::string worktree_not_repo;  // Open worktree outside git: the folder (shown in the dialog)
    std::string worktree_machine;  // machine the listed worktrees are on
    char wt_filter[128] = {};
    int wt_sel = 0;
    std::vector<std::string> chord_commands;  // window chords Windows reserves (main.cpp), run next frame
    // Commands raised while the layout draws (pane header buttons, pane and window menus, a
    // terminal's bound keys, the tab bar) wait until it is drawn: closing a pane can erase the
    // tab or space being drawn, and layout.reset / mods.reload replace the layout in use.
    struct DeferredCommand {
        std::string cmd;
        bool person = true;
        std::string focused;  // the focused session when it was raised (it ran with that focus)
    };
    bool defer_commands = false;
    std::vector<DeferredCommand> deferred_commands;
    ui::ImageCache program_images;  // the program icons (programs\icons), as textures
    // AgentDesk 98 (win98_*.cpp): its menus, dialogs, games and shared values.
    std::unique_ptr<w98state::W98> win98 = std::make_unique<w98state::W98>();
    bool fonts_win98 = false;  // AgentDesk 98's fonts are the ones in use

    // ---------------------------------------------------------------- helpers
    // The UI document (spaces, tabs, panes) changed: persist() sends it to the host soon.
    void doc_changed() {
        doc_dirty = true;
        doc_dirty_at = Clock::now();
    }
    [[nodiscard]] std::string title_of(const ui::SessionView& v) const {
        if (!v.name.empty())
            return v.name;
        return program_of(v).name;
    }
    [[nodiscard]] std::string pane_title(const std::string& id) const {
        const auto loc = ws.find(id);
        if (loc.space >= 0) {
            const auto& t =
                ws.spaces[static_cast<std::size_t>(loc.space)].tabs[static_cast<std::size_t>(loc.tab)];
            if (const auto it = t.pane_names.find(id); it != t.pane_names.end() && !it->second.empty())
                return it->second;
        }
        const auto it = views_by_id.find(id);
        return it == views_by_id.end() ? std::string("terminal") : title_of(it->second);
    }
    [[nodiscard]] Attention attention_of(const std::string& id) const {
        const auto it = views_by_id.find(id);
        if (it != views_by_id.end() &&
            (it->second.lost || it->second.process_state != "Running"))
            return Attention::kNone;
        return attention.get(id);
    }
    // Folder that names an unnamed space: its first terminal's git top (a worktree's own
    // folder), on whichever machine it runs, else that terminal's folder.
    [[nodiscard]] std::map<std::string, std::string> session_cwds() {
        std::map<std::string, std::string> m;
        for (const auto& sp : ws.spaces) {
            if (sp.tabs.empty())
                continue;
            const auto ids = ws.sessions_in(sp.tabs.front());
            if (ids.empty())
                continue;
            const auto it = views_by_id.find(ids.front());
            if (it == views_by_id.end() || it->second.cwd.empty())
                continue;
            const auto mi = machine_of.find(ids.front());
            const std::string machine = mi != machine_of.end() ? mi->second : sp.machine;
            const GitInfo g = git.get(it->second.cwd, machine);
            std::string top = g.repo ? g.top : std::string();
            if (top.empty() && machine.empty())
                top = ui::git_top(it->second.cwd);  // until git answers
            m[ids.front()] = top.empty() ? it->second.cwd : top;
        }
        return m;
    }
    [[nodiscard]] std::string focused_cwd() const {
        const auto id = ws.focused_session();
        const auto it = views_by_id.find(id);
        if (it != views_by_id.end())
            return it->second.cwd;
        return settings.last_cwd;
    }
    // A toast shows 5 s, errors and warnings (needs_you, working colours) 8 s.
    void toast(std::string title, std::string body, std::string session, ImU32 color, int seconds = 0) {
        if (seconds <= 0)
            seconds = color == g_pal->needs_you || color == g_pal->working ? 8 : 5;
        // No cap: every toast stacks until it times out.
        toasts.push_back({std::move(title), std::move(body), std::move(session), color,
                          Clock::now() + std::chrono::seconds(seconds)});
    }
    void focus(const std::string& id) {
        if (id.empty())
            return;
        if (ws.find(id).space < 0) {
            hidden.erase(id);
            const std::string m = machine_of.count(id) ? machine_of[id] : std::string();
            const int sp = space_for_cwd(views_by_id.count(id) ? views_by_id[id].cwd : std::string(), m);
            if (sp >= 0) {
                ws.add_tab(id, sp);
            } else {
                ws.add_space(id);
                ws.spaces.back().machine = m;
            }
        }
        ws.focus(id);
        views[id].focus_next = true;
        attention.seen(id);
    }
    ui::HostClient* host_for(const std::string& machine) {
        for (auto& r : remotes)
            if (r.cfg.name == machine)
                return r.host.get();
        return host.get();
    }
    [[nodiscard]] bool machine_connected(const std::string& machine) const {
        if (machine.empty())
            return host->connected();
        for (const auto& r : remotes)
            if (r.cfg.name == machine)
                return r.host->connected();
        return false;
    }
    [[nodiscard]] std::string machine_label(const std::string& machine) const {
        return machine.empty() ? std::string("This PC") : machine;
    }
    // The machine's spaces are still on their way: it is connecting (not connected, no error
    // yet), or this computer's document has not arrived. Disconnected or failed is not loading.
    [[nodiscard]] bool machine_loading(const std::string& machine) const {
        const ui::HostClient* h = host.get();
        for (const auto& r : remotes)
            if (r.cfg.name == machine && !machine.empty())
                h = r.host.get();
        if (!machine.empty() && h == host.get())
            return false;  // a machine no longer configured
        if (h->connected())
            return machine.empty() && !doc_loaded;
        return h->last_error().empty();
    }
    // The state shown after a machine's name in the sidebar.
    [[nodiscard]] std::string machine_state(const std::string& machine) const {
        if (machine_connected(machine))
            return "connected";
        return machine_loading(machine) ? "connecting" : "unreachable";
    }
    int space_for_cwd(const std::string& cwd, const std::string& machine = {}) {
        if (cwd.empty()) {
            if (ws.spaces.empty())
                return -1;
            const auto& a = ws.spaces[static_cast<std::size_t>(
                std::clamp(ws.active_space, 0, static_cast<int>(ws.spaces.size()) - 1))];
            return a.machine == machine ? ws.active_space : -1;
        }
        for (int s = 0; s < static_cast<int>(ws.spaces.size()); ++s) {
            if (ws.spaces[static_cast<std::size_t>(s)].machine != machine)
                continue;
            for (const auto& t : ws.spaces[static_cast<std::size_t>(s)].tabs) {
                for (const auto& id : ws.sessions_in(t)) {
                    const auto it = views_by_id.find(id);
                    if (it != views_by_id.end() && _stricmp(it->second.cwd.c_str(), cwd.c_str()) == 0)
                        return s;
                }
            }
        }
        return -1;
    }
    void focus_view(const std::string& id) {
        if (!id.empty())
            views[id].focus_next = true;
    }
};
namespace detail {
// ------------------------------------------------------------ app.cpp
void load_keys(App::State& s);
void observe(App::State& s);
void reconcile_core_update_error(std::optional<std::string>& dismissed, const ui::updates::CoreInfo& core);
void start_remotes(App::State& s);

// ------------------------------------------------------------ sessions.cpp
void adopt_doc(App::State& s, const Json& doc);
void create_session(App::State& s, const std::string& profile, const std::string& cwd,
                    const std::string& name, Placement placement, const std::vector<std::string>& args = {});
void sync_sessions(App::State& s);
void persist(App::State& s);
void run_mods(App::State& s);
void update_attention(App::State& s);

// ------------------------------------------------------------ commands.cpp
std::string pick_shell(const std::string& wanted, const std::vector<std::pair<std::string, std::string>>& profiles);
std::string trimmed(const std::string& t);
void open_launcher(App::State& s, LaunchTarget target);
void open_rename(App::State& s, const std::string& kind, const std::string& target,
                 const std::string& current);
void sync_panel(App::State& s, LayoutSpec& l);
// person: the person ran it (palette, keys, a click), not a mod or --run; only those count in
// telemetry.
void run_command(App::State& s, const std::string& cmd, bool person = true);
void run_deferred_commands(App::State& s);

// ------------------------------------------------------------ chrome.cpp
void draw_mod_panels(App::State& s, const std::string& where, const std::string& only_mod = {});
float fpx(float v);
ImU32 mix(ImU32 a, ImU32 b, float t);
ImU32 raised_color();
ImU32 accent_foreground();
ImU32 avatar_color(const std::string& name);
void draw_icon(App::State& s, ImDrawList* dl, const char* glyph, float size, ImVec2 center, ImU32 col);
bool icon_button(App::State& s, const char* id, const char* glyph, ImVec2 pos, float side, float icon,
                 const char* tip, ImU32 col, bool on = false);
extern ui::MenuNav g_menu_nav;
void menu_keys();
bool begin_menu(const char* id);
bool menu_row(App::State& s, const char* glyph, const char* label, const std::string& chord = {}, bool enabled = true);
std::vector<std::pair<std::string, std::string>> shell_profiles(App::State& s, const std::string& machine);
void draw_shell_picker(App::State& s, const char* id, const char* target);
void menu_separator();
float status_mark(App::State& s, ImDrawList* dl, float x, float cy, Attention a, bool compact, bool measure = false);
void window_drag_area(App::State& s, ImVec2 a, ImVec2 b);

// The space's git tail: "repo · branch" without the parts the name already says, main/master
// dropped, then ahead/behind.
struct GitTail {
    std::string text;
    int ahead = 0;
    int behind = 0;
};
GitTail git_tail(const GitInfo& g, const std::string& space_name, bool worktree_row);
void open_confirm(App::State& s, std::string title, std::string detail, std::string ok, std::string command);
void confirm_close_space(App::State& s, int index);

// ------------------------------------------------------------ sidebar.cpp
void draw_brand(App::State& s, ImVec2 pos, float w, bool rail);
float draw_spaces(App::State& s, ImVec2 pos, float w, float h, bool rail);
bool is_agent(const ui::SessionView& v);
// A tab is shown by its lead pane: the first agent, else the first pane.
std::string lead_pane(App::State& s, const std::vector<std::string>& ids);
std::string agent_detail(App::State& s, const std::string& id, const ui::SessionView& v);
long long now_ms();
void refresh_git_facts(App::State& s);
bool live_agent(App::State& s, const std::string& id, const ui::SessionView& v);
void draw_agents(App::State& s, ImVec2 pos, float w, float h, bool rail);
void draw_sidebar_footer(App::State& s, ImVec2 pos, float w, bool rail);

// ------------------------------------------------------------ tab_bar.cpp
void draw_tab_bar(App::State& s, ImVec2 pos, float width, float height);

// ------------------------------------------------------------ terminals_layout.cpp
// ---------------------------------------------------------------- Terminals layout
// A layout with its own top bar and pane headers. It was designed in graphite colours
// (background #131619, text #e8e9eb); here each colour is taken from the active theme by the same
// step from the background toward the text.
struct TerminalsLook {
    ImU32 tray, tab, tab_edge, hover, edge, chip, text, dim, tab_text;  // top bar
    ImU32 pane_border, pane_focus, button, button_hover;                 // panes and pane headers
};
TerminalsLook terminals_look();
void draw_terminals_bar(App::State& s, ImVec2 pos, float width, float height);
void draw_terminals_pane_header(App::State& s, const std::string& id, const ui::SessionView& v, ImVec2 pos,
                                float w, float h, bool focused);

// ------------------------------------------------------------ tab_bar.cpp
void draw_status_bar(App::State& s, ImVec2 pos, float width, float height);

// ------------------------------------------------------------ panes.cpp
void draw_panes(App::State& s, ImVec2 pos, ImVec2 size, bool connected);
std::string panel_folder(App::State& s, std::string* machine = nullptr);
ImU32 mark_color(char m);
void panel_header(App::State& s, ImDrawList* dl, ImVec2 a, float w, const char* title, const std::string& right,
                  bool back, bool* back_clicked);

// ------------------------------------------------------------ code_preview.cpp
void draw_code_preview(App::State& s, SidePanels& p, ImVec2 size);

// ------------------------------------------------------------ files_changes.cpp
void draw_side_panel(App::State& s, ImVec2 pos, ImVec2 size);
void draw_details(App::State& s, ImVec2 pos, ImVec2 size, bool connected);

// ------------------------------------------------------------ overlays.cpp
void push_overlay_style();
void pop_overlay_style();

// How a dialog's title row shows Esc.
enum class EscChip {
    kButton,  // a chip, 6 x 2 px padding, rounded 5, 11 px dim, no border; the hover colour and
              // foreground under the mouse; a click closes
    kPlain,   // plain 11 px dim text (prompts)
};
bool overlay_title(App::State& s, const char* title, EscChip esc = EscChip::kButton, float size = 15.0f);
void worktree_title(App::State& s, const std::string& title, const std::string& right = {});
bool worktree_input(App::State& s, const char* id, const char* hint, char* buf, std::size_t size,
                    ImGuiInputTextFlags flags = 0);
void draw_palette(App::State& s);
void draw_picker(App::State& s);

// ------------------------------------------------------------ notices.cpp
void draw_toasts(App::State& s);
bool dialog_outside_click();
void draw_peek(App::State& s);

// ------------------------------------------------------------ overlays.cpp
void center_next(float w, float y_frac = 0.18f);
void heading(App::State& s, const char* text);
void primary_button_style(bool push);
void draw_help(App::State& s);
void draw_launcher(App::State& s);
void draw_rename(App::State& s);

// ------------------------------------------------------------ worktrees.cpp
void draw_worktree(App::State& s);
void draw_worktree_open(App::State& s);

// ------------------------------------------------------------ settings_pages.cpp
float line_h(float size);
void settings_gap(float h);
void settings_wrapped(App::State& s, const char* text, ImU32 color, float size = 12.5f, float lh = 18.0f,
                      ImFont* font = nullptr);
void settings_section(App::State& s, const char* title);
void settings_row(App::State& s, const char* label, const char* note, float control_w, float control_h = -1.0f);
void settings_row_done(App::State& s);

// A stepper: one bordered group (rounded 7, raised) of 28 x 28 cells with "−" and "+" (15 px
// dim; the hover colour and foreground under the mouse) around a 54 px value (13 px); 112 x 30
// with the border. Returns true when the value changed.
inline constexpr float kStepperW = 28 + 54 + 28 + 2;
inline constexpr float kStepperH = 28 + 2;
bool stepper(App::State& s, const char* id, float& v, float step, float lo, float hi, const char* fmt, float scale = 1.0f);
const ui::FontFamily* find_family(const std::string& name);
void apply_terminal_font(App::State& s);
void apply_ui_font(App::State& s);
bool font_picker(App::State& /*s*/, const char* id, std::string& family, const char* default_label, float width);
bool segmented(App::State& s, const char* id, std::string& value,
               std::initializer_list<std::pair<const char*, const char*>> options);
void layout_preview(ImDrawList* d, const std::string& name, ImVec2 a, ImVec2 size);
ImVec2 settings_button_size(App::State& s, const char* label, ImFont* font = nullptr);
bool settings_button(App::State& s, const char* id, const char* label);
bool settings_toggle(const char* id, bool on);
void settings_footnote(App::State& s, const char* text);
bool settings_updates(App::State& s);
bool settings_privacy(App::State& s);

// ------------------------------------------------------------ settings_dialog.cpp
void draw_settings(App::State& s);

// ------------------------------------------------------------ sessions.cpp
void play_alert(App::State& s, bool blocked);

// ------------------------------------------------------------ welcome.cpp
void draw_welcome(App::State& s);

// ------------------------------------------------------------ machine_add.cpp
void draw_machine_add(App::State& s);

// ------------------------------------------------------------ notes.cpp
void draw_notes(App::State& s);
void draw_confirm(App::State& s);
void draw_exit(App::State& s);
void draw_close_notice(App::State& s);
}  // namespace detail

}  // namespace agentdesk::desktop
