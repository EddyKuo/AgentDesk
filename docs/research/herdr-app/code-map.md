# AgentDesk 程式碼與 herdr 原始碼對照表

這份表從程式碼註解整理而來，整理時 AgentDesk 的版本是 `de5bd05`。之後程式碼裡提到 herdr 的註解與對應行號全部移除（2026-11-19 使用者要求）；要查某段程式當初照 herdr 的哪裡寫，查這份表。

- herdr 原始碼版本：https://github.com/herdrdev/herdr-app 的 commit `321c9a8`。
- 「AgentDesk 位置」是整理當時的檔案與行號。重構後行號會變，請用「對應的程式」欄的程式碼開頭搜尋。
- `sol.js` 的行號原本每處多算 200 行，這份表已更正。
- 共 1163 筆。

| AgentDesk 位置 | 對應的程式 | 原註解 |
|---|---|---|
| `apps/cli/main.cpp:561` | `int update_failed(const Ctx& ctx, const std::string& message, const std::string& hint = {}` | herdr `hc update` (herdr-app src/core_bin.rs 242-265, docs/commands.md 547): agent-host to its |
| `apps/cli/main.cpp:619` | `const auto deadline = std::chrono::steady_clock::now() + 15s;` | The new agent-host serves the pipe once the old one let go (herdr: the app reconnects |
| `apps/desktop/CMakeLists.txt:3` | `target_compile_options(agent-ui PRIVATE /bigobj)` | app.cpp (with the Herdr 98 .inl files) passes the COFF section limit in Debug. |
| `apps/desktop/CMakeLists.txt:8` | `add_custom_command(TARGET agent-ui POST_BUILD` | Herdr 98's fonts, icons, pictures and sounds (third_party/herdr/win98/README.agentdesk.md): win98\. |
| `apps/desktop/CMakeLists.txt:17` | `COMMAND ${CMAKE_COMMAND} -E rm -f "$<TARGET_FILE_DIR:agent-ui>/win98/img/splash.png" "$<TA` | AgentDesk 98 draws its name itself (win98_kit.inl brand_*): not the pictures that say Herdr. |
| `apps/desktop/CMakeLists.txt:19` | `COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/third_party/herdr/progra` | herdr's program table and icons (defaults/programs.json, defaults/icons): programs\. |
| `apps/desktop/app.cpp:1` | `` | AgentDesk window contents. Feature set follows herdr's app (spaces, tabs, split panes, agent |
| `apps/desktop/app.cpp:75` | `bool layout_only = false;` | a layout's own theme (herdr: offered only there) |
| `apps/desktop/app.cpp:76` | `const std::array<ImU32, 16>* ansi = nullptr;` | the theme's "ansi" table (herdr themes/*.json) |
| `apps/desktop/app.cpp:79` | `constexpr std::array<ImU32, 16> kWin98Ansi = {` | herdr themes/win98.json "ansi" (0-7, then the bright 8-15). |
| `apps/desktop/app.cpp:177` | `{"win98",` | herdr themes/win98.json, Herdr 98's own (settings.rs 99): a theme named like a layout is that |
| `apps/desktop/app.cpp:210` | `struct HerdrPalette {` | ------------------------------------------------------------------ herdr's themes |
| `apps/desktop/app.cpp:211` | `struct HerdrPalette {` | The person chose to have herdr's themes beside ours (2026-11-19). herdr settings.rs: four |
| `apps/desktop/app.cpp:214` | `struct HerdrPalette {` | colors{}; 103-113). The chrome colors come from the palette as herdr's resolve() derives them |
| `apps/desktop/app.cpp:215` | `struct HerdrPalette {` | (306-375). Ids of herdr's presets get "herdr-" in front, since ours already use moss and paper. |
| `apps/desktop/app.cpp:223` | `constexpr std::array<std::uint32_t, 3> kHerdrDarkStatus = {0xe5c07b, 0xf0705f, 0x76a8ff};` | settings.rs 31 |
| `apps/desktop/app.cpp:255` | `bool herdr_is_light(std::uint32_t c) {` | settings.rs 268-270 is_light, 292-297 mix. |
| `apps/desktop/app.cpp:288` | `void add_herdr_palette(const std::string& id, const std::string& label, const HerdrPalette` | herdr resolve(): our Palette's fields from herdr's derived colors. bg = canvas (behind the |
| `apps/desktop/app.cpp:290` | `void add_herdr_palette(const std::string& id, const std::string& label, const HerdrPalette` | dim = muted, faint = dim, focus/info = accent; theme files' "colors" override by herdr's names. |
| `apps/desktop/app.cpp:341` | `std::optional<std::pair<HerdrPalette, std::map<std::string, std::string>>> read_theme_file` | herdr settings.rs 103-113: a theme file. A bad file is skipped (herdr: not listed). |
| `apps/desktop/app.cpp:403` | `void load_herdr_themes() {` | Presets, then the shipped files, then the person's (sorted by name), as herdr themes() lists |
| `apps/desktop/app.cpp:404` | `void load_herdr_themes() {` | them (settings.rs 272-286). win98 is Herdr 98's own and already one of ours. |
| `apps/desktop/app.cpp:425` | `std::replace(label.begin(), label.end(), '-', ' ');` | settings/main.js 367 |
| `apps/desktop/app.cpp:430` | `std::vector<const Palette*> all_palettes() {` | Ours, then herdr's. |
| `apps/desktop/app.cpp:480` | `const Palette* effective_palette(const std::string& appearance, const std::string& dark, c` | herdr Appearance: System follows Windows; Light and Dark use the chosen theme of that kind. A |
| `apps/desktop/app.cpp:497` | `s.WindowRounding = 8;` | herdr overlays: radius 8, popover background, 1 px border |
| `apps/desktop/app.cpp:540` | `c[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.35f);` | herdr window.rs 4767: hsla(0, 0, 0, 0.35) |
| `apps/desktop/app.cpp:567` | `bool toasts = true;` | Read from ui.json and written back, but ignored: herdr always shows the note (settings/main.js |
| `apps/desktop/app.cpp:576` | `float ui_text = 15.0f;` | interface text size at zoom 1 (herdr: 13); scales all chrome |
| `apps/desktop/app.cpp:577` | `float sidebar_width = 268;` | herdr design px, 44..480; under 160 = rail |
| `apps/desktop/app.cpp:584` | `int terminal_weight = 500;` | herdr terminal font weight (JetBrains Mono Medium) |
| `apps/desktop/app.cpp:586` | `std::string appearance = "system";` | "system", "light" or "dark" (herdr Appearance) |
| `apps/desktop/app.cpp:589` | `std::map<std::string, std::pair<std::string, std::string>> layout_themes;` | herdr window.rs 3620-3635: a layout is a look; each keeps its own dark and light theme |
| `apps/desktop/app.cpp:597` | `std::string seen_version;` | What's new was shown for this version (herdr: once after an update) |
| `apps/desktop/app.cpp:600` | `Json agents_changed = Json::object();` | session id -> {s: state, at: ms}: herdr agents.js 85-110 |
| `apps/desktop/app.cpp:601` | `bool telemetry = true;` | herdr settings.rs 718-726 / main.js 857-861: on unless the file says "telemetry": false; |
| `apps/desktop/app.cpp:622` | `float first_run_zoom() {` | herdr first run (settings.rs 778-802): zoom from Windows' text size (Accessibility |
| `apps/desktop/app.cpp:634` | `std::pair<std::string, std::string> layout_theme_pair(const Settings& st) {` | The shown layout's {dark, light} themes (herdr: on the layout's root), else the defaults. |
| `apps/desktop/app.cpp:665` | `if (j.is_object() && j.contains("telemetry") && j["telemetry"].is_boolean())` | herdr settings.rs 718-726: even when the rest can't be used, the file's own "telemetry". |
| `apps/desktop/app.cpp:738` | `float ui_text_factor(const Settings& s) {` | Interface text size relative to herdr's 13 px; the whole chrome scales with it and the zoom. |
| `apps/desktop/app.cpp:757` | `void save_settings(const Settings& s) {` | herdr settings.js 115-148: re-read the file, apply our keys onto it (keys we do not own stay), |
| `apps/desktop/app.cpp:838` | `if (s.telemetry)` | herdr settings main.js 858-861: off writes false, on deletes the key. |
| `apps/desktop/app.cpp:870` | `namespace lay = ui::layout;` | herdr's layout engine (libs/ui layout.hpp): a layout is a tree of blocks (rows, columns, |
| `apps/desktop/app.cpp:871` | `namespace lay = ui::layout;` | stacks; the sidebar, tab bar, panes, panel and status bar are blocks) in herdr's format |
| `apps/desktop/app.cpp:872` | `namespace lay = ui::layout;` | (defaults/AGENTS.md "Layouts"). Built-ins are herdr's main.json (named workspace here), |
| `apps/desktop/app.cpp:875` | `namespace lay = ui::layout;` | sits) live in <data>\layouts\.state\<name>.json (herdr window.rs 203-216, layout_sync.rs). |
| `apps/desktop/app.cpp:883` | `lay::Node file;` | as its file (or herdr) has it |
| `apps/desktop/app.cpp:889` | `std::string error;` | the file is broken: shown as shipped meanwhile (window.rs 1005-1018) |
| `apps/desktop/app.cpp:919` | `else if (th.is_object())` | layout.rs 197-210: the system's appearance picks |
| `apps/desktop/app.cpp:935` | `l.font_size.reset();` | layout.rs 150-151: the nearest node setting it wins; the panes' block decides. |
| `apps/desktop/app.cpp:951` | `lay::State seed_from_settings(const Settings& st, const lay::Node& file) {` | clicks (what herdr keeps in layouts/.state). |
| `apps/desktop/app.cpp:972` | `void apply_layout_state(LayoutSpec& l, const Settings& st) {` | herdr window.rs 997-1043 load_layout: the file (or its shipped default when it is broken), |
| `apps/desktop/app.cpp:990` | `void save_layout_state(const LayoutSpec& l) {` | herdr window.rs 1199-1220 write_state: the clicks, with the file they were made on; none, no |
| `apps/desktop/app.cpp:1047` | `l.file = lay::builtin(l.name);` | window.rs 180-182: as shipped, main when not one |
| `apps/desktop/app.cpp:1049` | `int k = 0;` | window.rs 1222-1243 keep_ids: nodes without an id get one (here only in memory). |
| `apps/desktop/app.cpp:1104` | `ui::Programs g_programs;` | herdr src/programs.rs: one table (defaults/programs.json, shipped in programs\ next to the exe) |
| `apps/desktop/app.cpp:1106` | `ui::Programs g_programs;` | problems go to programs.error (herdr window.rs 982-991, 909-912). |
| `apps/desktop/app.cpp:1139` | `ui::Program program_of(const ui::SessionView& v) {` | What runs in a terminal (herdr programs::of): the agent's 7501 `app` first, else the |
| `apps/desktop/app.cpp:1153` | `std::filesystem::path program_icon_file(const ui::Program& p) {` | A person's icon (in the data folder) before the shipped one (herdr: both live in its home). |
| `apps/desktop/app.cpp:1177` | `void program_icon(ImDrawList* dl, const ui::Program& p, ImVec2 min, float size, ImU32 colo` | herdr's icon(p, size, color) (pane/main.js 27-30, agents/main.js 78, tabs/main.js 120): a |
| `apps/desktop/app.cpp:1415` | `bool builtin_command(std::string_view name) {` | The commands AgentDesk itself has (herdr commands::COMMANDS). |
| `apps/desktop/app.cpp:1431` | `"app.check_updates", "app.update_core", "app.update"};` | herdr commands.rs 98, 99, 105 |
| `apps/desktop/app.cpp:1436` | `bool known_command(std::string_view name) {` | Command names keys.json may bind (herdr validates actions and reports unknown ones). A |
| `apps/desktop/app.cpp:1441` | `static const std::set<std::string, std::less<>> kWin98 = {` | Herdr 98's commands (herdr-app defaults/mods/win98/README.md 12-17). |
| `apps/desktop/app.cpp:1466` | `struct GitInfo {` | herdr's spaces mod asks git for the checkout's top, its common dir (worktrees), the branch and |
| `apps/desktop/app.cpp:1468` | `struct GitInfo {` | 60 s (spaces.js 105-147, 229-264). Here one background thread runs git for queued folders. |
| `apps/desktop/app.cpp:1469` | `struct GitInfo {` | Like herdr's watchFolder (spaces.js 266-286) a second thread watches each local repo's git |
| `apps/desktop/app.cpp:1471` | `struct GitInfo {` | while the window is in front (spaces.js 140). |
| `apps/desktop/app.cpp:1516` | `if (!e.queued && (e.at == Clock::time_point{} ¦¦ (active_ && now - e.at > std::chrono::sec` | The fallback look (60 s) only while the window is in front (spaces.js 140-145). |
| `apps/desktop/app.cpp:1530` | `void invalidate_repo(const std::string& dir, const std::string& machine = {}) {` | `dir` and every other folder in its repo (spaces.js 149-153: a commit there moves their |
| `apps/desktop/app.cpp:1575` | `void watch_loop(std::stop_token st) {` | Local repos' git folders, each with a change notification (spaces.js 266-286 watchFolder). |
| `apps/desktop/app.cpp:1750` | `class SidePanels {` | herdr-app mods/files and mods/changes (docs/research/herdr-app/spec-files-changes.md): both |
| `apps/desktop/app.cpp:1933` | `std::string msg = commit_msg;` | herdr changes/main.js commit(): this.message.trim() |
| `apps/desktop/app.cpp:1950` | `commit_note = "Commit failed: " + (r.started ? r.message() : std::string("git not found"))` | (stderr \|\| stdout).trim(), as herdr shows it |
| `apps/desktop/app.cpp:2079` | `static char letter_of(const std::string& xy) {` | Changes list (herdr changes/main.js): conflicts first. |
| `apps/desktop/app.cpp:2089` | `static char file_mark_of(const std::string& xy) {` | Files tree marks (herdr files/main.js:99): added first, then U, D, M. |
| `apps/desktop/app.cpp:2120` | `std::map<std::string, char> m;` | Git marks: keyed by path relative to `root` (normalised; herdr's string join misses on Windows). |
| `apps/desktop/app.cpp:2540` | `std::string tab_name;` | herdr tab.new {name} (Herdr 98 names its tabs) |
| `apps/desktop/app.cpp:2572` | `std::optional<Clock::time_point> updates_asked;` | Updates and Privacy (herdr update.rs, telemetry.rs, settings main.js 760-882). |
| `apps/desktop/app.cpp:2573` | `std::optional<Clock::time_point> updates_asked;` | main.js 65-71 `this.asked` |
| `apps/desktop/app.cpp:2574` | `bool show_data = false;` | main.js 870 `this.showData` |
| `apps/desktop/app.cpp:2575` | `std::optional<std::string> update_dismissed;` | window.rs 4840 |
| `apps/desktop/app.cpp:2576` | `std::optional<std::string> core_update_error_dismissed;` | window.rs 3974 |
| `apps/desktop/app.cpp:2581` | `ImFont* report_font = nullptr;` | JetBrains Mono for the report lines (main.js 840) |
| `apps/desktop/app.cpp:2676` | `GitCache git;` | herdr chrome |
| `apps/desktop/app.cpp:2691` | `struct Changed {` | ui.json (herdr agents.js 85-110). |
| `apps/desktop/app.cpp:2698` | `std::map<std::string, bool> busy_terminal;` | a command or an agent turn running (spaces.js 128-133) |
| `apps/desktop/app.cpp:2699` | `std::string spaces_revealed;` | active space last brought into view (spaces.js 533-539) |
| `apps/desktop/app.cpp:2704` | `std::string drag_pane;` | pane being dragged by its grip (herdr pane drag) |
| `apps/desktop/app.cpp:2726` | `ImVec2 welcome_pos{-1.0f, -1.0f};` | top-left once the person dragged it (herdr: Drag to move) |
| `apps/desktop/app.cpp:2729` | `std::map<std::string, std::string> machine_add_state;` | Add a machine (herdr machine-add): target -> "adding" or why it could not be added. |
| `apps/desktop/app.cpp:2753` | `ui::ImageCache program_images;` | herdr's program icons (programs\icons), as textures |
| `apps/desktop/app.cpp:2754` | `std::unique_ptr<w98state::W98> win98 = std::make_unique<w98state::W98>();` | Herdr 98 (win98_*.inl): its menus, dialogs, games and shared values. |
| `apps/desktop/app.cpp:2756` | `bool fonts_win98 = false;` | Herdr 98's fonts are the ones in use |
| `apps/desktop/app.cpp:2782` | `[[nodiscard]] std::map<std::string, std::string> session_cwds() {` | Folder that names an unnamed space (herdr model.rs 271-279): its first terminal's git top |
| `apps/desktop/app.cpp:2812` | `void toast(std::string title, std::string body, std::string session, ImU32 color, int seco` | herdr toasts: 5 s, 8 s for errors and warnings. |
| `apps/desktop/app.cpp:2816` | `toasts.push_back({std::move(title), std::move(body), std::move(session), color,` | No cap: herdr stacks every toast (window.rs 4794-4796). |
| `apps/desktop/app.cpp:2855` | `[[nodiscard]] bool machine_loading(const std::string& machine) const {` | herdr spaces/main.js 347-351 loading(): the machine's layout is still on its way. Here: |
| `apps/desktop/app.cpp:2869` | `[[nodiscard]] std::string machine_state(const std::string& machine) const {` | The state herdr writes after a machine's name (spaces/main.js 370, 385). |
| `apps/desktop/app.cpp:2911` | `void load_keys(App::State& s) {` | keys.json (herdr keys.rs): installed from the defaults when missing; read again whenever it |
| `apps/desktop/app.cpp:2939` | `ui::telemetry::Config telemetry_config(App::State& s) {` | What telemetry.cpp asks the app (herdr reads these from its own modules). |
| `apps/desktop/app.cpp:2944` | `c.shipped_layouts = {"workspace", "terminals"};` | window.rs 171-173 shipped_layouts(): herdr ships main and terminals; ours are named |
| `apps/desktop/app.cpp:2947` | `for (const auto& p : kPalettes)` | settings.rs 80, 101: the presets and shipped themes; ours are the palettes. |
| `apps/desktop/app.cpp:2950` | `for (const auto& [name, pal] : herdr_presets())` | herdr's presets and shipped file |
| `apps/desktop/app.cpp:2953` | `c.mod_kinds = [](const std::set<std::string>& names) {` | mods.rs 281-299 kinds(): shipped ones as shipped (by name), shipped but edited, the |
| `apps/desktop/app.cpp:2986` | `c.keys_custom = [] {` | telemetry.rs 317-324: keys.json differs from the shipped one. |
| `apps/desktop/app.cpp:2999` | `void observe(App::State& s) {` | window.rs 834-856: what telemetry.rs counts: how much is open, never what's in it. |
| `apps/desktop/app.cpp:3005` | `for (const auto& [id, v] : s.views_by_id) {` | Every terminal reporting an agent. herdr names it by its 7501 `app`; agent-host does not |
| `apps/desktop/app.cpp:3028` | `void reconcile_core_update_error(std::optional<std::string>& dismissed, const ui::updates:` | window.rs 5455-5459 |
| `apps/desktop/app.cpp:3086` | `const auto args = params.find("args");` | git worktree add/remove may take minutes (herdr worktree/main.js:205-207, 243). |
| `apps/desktop/app.cpp:3104` | `ui::updates::after_update();` | herdr main.rs 154-158: after an update, then the checker, then telemetry. |
| `apps/desktop/app.cpp:3108` | `if (s_->settings.welcomed.empty() && !s_->config.automated) {` | herdr: the welcome opens on the first run (until it is finished or skipped). |
| `apps/desktop/app.cpp:3114` | `s_->show_notes = true;` | herdr: What's new once after an update |
| `apps/desktop/app.cpp:3184` | `void App::on_quit() {` | herdr main.rs 177-192 on_app_quit: telemetry's clean quit, then the waiting update goes in |
| `apps/desktop/app.cpp:3198` | `s_->fonts = ui::load_fonts(s_->settings.font_size, 13.0f);` | herdr UI base 13, scaled by ui_scale() |
| `apps/desktop/app.cpp:3251` | `ms = std::min<long long>(ms, 300);` | settings main.js 66: every 300 ms |
| `apps/desktop/app.cpp:3256` | `if (s_->layout().style == "win98")` | Herdr 98's clock, carets, badges, games and screens |
| `apps/desktop/app.cpp:3318` | `void create_session(App::State& s, const std::string& profile, const std::string& cwd,` | `args` go after the profile's own (session.create "args"; Herdr 98's Run line). |
| `apps/desktop/app.cpp:3411` | `const auto& v = s.views_by_id[sess->id()];` | herdr window.rs 2262-2283, 2695-2719 (reconcile / checked_closed): a pane whose terminal |
| `apps/desktop/app.cpp:3413` | `const auto& v = s.views_by_id[sess->id()];` | they empty (model.rs 403-422). Lost: the agent-host that ran it is gone. |
| `apps/desktop/app.cpp:3441` | `continue;` | ended: never opened (herdr places live terminals only, window.rs 2277-2283) |
| `apps/desktop/app.cpp:3573` | `const bool blocked = v.agent_state == "Blocked";` | herdr: the sound plays either way; the note shows when you were not looking. |
| `apps/desktop/app.cpp:3578` | `if (s.ws.unsettle_session(id)) {` | herdr: an alert brings a tab put away back |
| `apps/desktop/app.cpp:3591` | `where, id, blocked ? g_pal->needs_you : g_pal->focus);` | herdr: "<space> / <tab>" |
| `apps/desktop/app.cpp:3594` | `const auto prev = s.prev_process_state[id];` | herdr window.rs 142-157, 2463-2481: a terminal that exited by itself with an error |
| `apps/desktop/app.cpp:3649` | `host::ToolResult git_at(App::State& s, const std::string& machine, const std::string& cwd,` | git.run (herdr worktree.js runs git on the space's machine). |
| `apps/desktop/app.cpp:3679` | `struct WorktreeRisk {` | What removing a linked worktree would lose (herdr worktree.js): uncommitted changes, and |
| `apps/desktop/app.cpp:3702` | `if (line.empty() ¦¦ line[0] == '-')` | "<flag><sha> <path> (<describe>)"; '-': never checked out (herdr skips those too) |
| `apps/desktop/app.cpp:3713` | `std::string parent;` | herdr defaults/mods/worktree/main.js:100-110: commits that exist nowhere else, i.e. on |
| `apps/desktop/app.cpp:3741` | `if (!r.unpushed.empty()) {` | herdr worktree/main.js render() |
| `apps/desktop/app.cpp:3771` | `if (s.rename_open) {` | herdr window.rs 3047-3051: a second rename press closes the open prompt instead of |
| `apps/desktop/app.cpp:3784` | `void sync_panel(App::State& s, LayoutSpec& l) {` | herdr tabs/main.js 62-80 syncPanel: the side panel is remembered per space (here the space's |
| `apps/desktop/app.cpp:3797` | `Json layout_args(const std::string& arg, const char* first, const char* second) {` | The arguments herdr's layout.* commands take ({"id": ..., "px": ...}) as JSON, or the short |
| `apps/desktop/app.cpp:3821` | `std::optional<std::string> layout_command(App::State& s, const std::string& name, const st` | herdr window.rs 3496-3538: layout.toggle / show / hide {id}, layout.resize {id, px \| weight}, |
| `apps/desktop/app.cpp:3831` | `if (id == "panel") {` | the space's choice (tabs/main.js 136-141) |
| `apps/desktop/app.cpp:3871` | `void run_command(App::State& s, const std::string& cmd, bool person = true);` | person: the person ran it (palette, keys, a click), not a mod or --run (window.rs 2995). |
| `apps/desktop/app.cpp:3877` | `std::string exec_command(App::State& s, const std::string& cmd) {` | Runs a command and says why it failed ("" when it ran): herdr window.rs run_as() answers |
| `apps/desktop/app.cpp:3888` | `static const std::map<std::string, std::string> kHerdrNames = {` | herdr command names (keys.json written for herdr keeps working). |
| `apps/desktop/app.cpp:3891` | `{"app.quit", "window.close"}};` | herdr Quit: the window closes, agents keep running |
| `apps/desktop/app.cpp:3900` | `if (name == "overlay.close") {` | herdr: closes the overlays (here: the floating ones) |
| `apps/desktop/app.cpp:3905` | `if (name == "terminal.pin") {` | herdr terminal.pin {pinned}: the focused agent |
| `apps/desktop/app.cpp:3916` | `if (name == "space.move") {` | herdr space.move {index} (1 = first): the active space |
| `apps/desktop/app.cpp:3970` | `if (focused.empty())` | herdr: ends the terminal; the last pane closes the tab (and maybe asks about the space). |
| `apps/desktop/app.cpp:3986` | `const std::string target = arg.empty() ? focused : arg;` | herdr: end a terminal and its sub-agents. |
| `apps/desktop/app.cpp:3993` | `const auto keys = data_dir() / L"keys.json";` | herdr escape hatch: keys.json set aside, default layout, sidebar back. |
| `apps/desktop/app.cpp:4011` | `if (!focused.empty()) {` | herdr terminal.park: a sub-agent's pane goes away; it keeps running under its parent |
| `apps/desktop/app.cpp:4032` | `const auto loc = arg.empty() ? ui::PaneLoc{} : ui::find_tab(s.ws, arg);` | herdr tab.rename {id}: that tab (default: the shown one), without selecting it. |
| `apps/desktop/app.cpp:4047` | `ui::PaneLoc loc;` | herdr tab.close {id} (window.rs 3122-3132): by id, the shown tab and space stay; the |
| `apps/desktop/app.cpp:4063` | `if (auto it = s.by_id.find(id); it != s.by_id.end())` | herdr: ends the tab's terminals |
| `apps/desktop/app.cpp:4071` | `if (s.ws.active() == nullptr)` | herdr asks first ("Close space?"), then ends its terminals. |
| `apps/desktop/app.cpp:4089` | `if (auto* sp = s.ws.active()) {` | herdr layout.toggle:panel / the Files and Changes buttons: a per-space choice. |
| `apps/desktop/app.cpp:4097` | `LayoutSpec& l = s.layout();` | herdr workspace-chrome/main.js 18-24 toggleSidebar(): remember the width and |
| `apps/desktop/app.cpp:4126` | `if (name == "tab.close_at")` | by id: the shown tab and space stay (herdr picker ctrl+d) |
| `apps/desktop/app.cpp:4158` | `std::map<std::string, long long> when;` | herdr worktree/main.js checkouts(): each branch's last commit date, from its ref. |
| `apps/desktop/app.cpp:4196` | `const auto it = e.branch.empty() ? when.end() : when.find(e.branch);` | A detached checkout has no branch, so no date (herdr: at 0, no age). |
| `apps/desktop/app.cpp:4199` | `const double d = static_cast<double>(static_cast<long long>(std::time(nullptr)) - e.time);` | herdr ago(): rounded, minutes at least 1. |
| `apps/desktop/app.cpp:4219` | `if (s.worktree_list.empty())` | Outside git the dialog itself says so (herdr renderOpen). |
| `apps/desktop/app.cpp:4267` | `const auto bar = arg.find('¦');` | herdr worktree.js 228-245: Enter re-checks; a risk that appeared since the dialog |
| `apps/desktop/app.cpp:4305` | `const auto r = git_at(s, machine, main, argv, std::chrono::seconds(600));` | herdr worktree.js 243 |
| `apps/desktop/app.cpp:4313` | `if (auto* sp = s.ws.active()) {` | herdr: put a tab away (out of the bar, still running) until shown or it needs you. |
| `apps/desktop/app.cpp:4402` | `if (s.layout().tree.find("sidebar") != nullptr)` | herdr ctrl-shift-b: layout.toggle {"id": "sidebar"} (nothing when the layout has none). |
| `apps/desktop/app.cpp:4416` | `for (const char* sec : {"Look", "Machines", "Alerts", "Mods", "Updates", "Privacy"})` | herdr app.settings {section}: open on that page ("mods", "alerts", ...). |
| `apps/desktop/app.cpp:4439` | `const Palette* p = palette_by_id(arg);` | herdr app.theme (window.rs 3620-3635): the theme goes on the shown layout, as its light |
| `apps/desktop/app.cpp:4461` | `ui::updates::check_now();` | window.rs 3603 |
| `apps/desktop/app.cpp:4463` | `const auto c = ui::updates::core();` | window.rs 3604-3609: nothing waiting (and nothing failed) is an error; else |
| `apps/desktop/app.cpp:4464` | `const auto c = ui::updates::core();` | `agentctl update` (herdr: `hc update`) in the background. agent-host hands its |
| `apps/desktop/app.cpp:4471` | `std::string error;` | window.rs 3645-3657 |
| `apps/desktop/app.cpp:4489` | `const Json a = layout_args(arg, "name", "as");` | herdr window.rs 3443-3454 and 266-290 clone_layout: the layout (default: the shown one) |
| `apps/desktop/app.cpp:4491` | `const Json a = layout_args(arg, "name", "as");` | its clicks; then shown. Ours is written in herdr's format, also from an old file. |
| `apps/desktop/app.cpp:4526` | `const std::string which = arg.empty() ? s.settings.layout : arg;` | herdr window.rs 3514-3533: only a layout herdr ships; a file of that name is set aside |
| `apps/desktop/app.cpp:4527` | `const std::string which = arg.empty() ? s.settings.layout : arg;` | (<name>.json.old-<time>, never over an older one, window.rs 218-224) and its clicks go. |
| `apps/desktop/app.cpp:4601` | `if (s.layout().style == "win98")` | Herdr 98's key commands (README.md 12-17), delivered to its taskbar (taskbar.js 126-176) |
| `apps/desktop/app.cpp:4612` | `return "unknown command \"" + name + "\"";` | herdr window.rs 2975-2976. |
| `apps/desktop/app.cpp:4618` | `void run_command(App::State& s, const std::string& cmd, bool person) {` | herdr window.rs 2931-2943: a command run for the person (a click, a key, a menu, a mod) has |
| `apps/desktop/app.cpp:4625` | `if (const std::string name = cmd.substr(0, cmd.find(':')); person && builtin_command(name)` | herdr window.rs 2995-2997 / telemetry.rs 124: a command the person ran, by its name. |
| `apps/desktop/app.cpp:4666` | `void draw_mod_panels(App::State& s, const std::string& where, const std::string& only_mod ` | Mod panels, inset and titled like the sidebar's own sections (herdr spaces/agents headers). |
| `apps/desktop/app.cpp:4742` | `std::string lead_pane(App::State& s, const std::vector<std::string>& ids);` | herdr tabs.js 25-29: a tab is shown by its lead pane: the first agent, else the first pane. |
| `apps/desktop/app.cpp:4745` | `` | ------------------------------------------------------------------ herdr window chrome |
| `apps/desktop/app.cpp:4746` | `` | Layout, sizes and behaviour follow herdr-app's default layout and mods (layouts/main.json, |
| `apps/desktop/app.cpp:4748` | `` | docs/research/herdr-app/spec-sidebar-layout.md). Sizes are herdr design px through px(). |
| `apps/desktop/app.cpp:4761` | `ImU32 raised_color() {` | herdr tokens derived from our palettes: raised (active tab), popover (menus). |
| `apps/desktop/app.cpp:4767` | `ImU32 accent_foreground() {` | herdr's accent_foreground: text on the accent fill. |
| `apps/desktop/app.cpp:4772` | `ImU32 avatar_color(const std::string& name) {` | Avatar colours: herdr's fixed table (spaces.js MARK), the same in every theme. |
| `apps/desktop/app.cpp:4793` | `bool icon_button(App::State& s, const char* id, const char* glyph, ImVec2 pos, float side,` | herdr icon button: square, radius 6, muted icon; hover background + foreground. |
| `apps/desktop/app.cpp:4808` | `ui::MenuNav g_menu_nav;` | herdr context menu (mods/menu): padding 5, rows 31 px with a 14 px muted icon, the label and |
| `apps/desktop/app.cpp:4810` | `ui::MenuNav g_menu_nav;` | Keyboard in the open menu (herdr menu/main.js 131-137): one menu is open at a time. |
| `apps/desktop/app.cpp:4842` | `bool run = false;` | herdr menu/main.js 152-153: the highlighted row is "selected", a hovered one "hover". |
| `apps/desktop/app.cpp:4906` | `float status_mark(App::State& s, ImDrawList* dl, float x, float cy, Attention a, bool comp` | Agent status marks (agents.js 208-217): spinner, "Needs you" pill or "!", done dot, idle ring. |
| `apps/desktop/app.cpp:4955` | `void window_drag_area(App::State& s, ImVec2 a, ImVec2 b) {` | Dragging an empty part of the brand row or the tab bar moves the window (herdr window_drag: |
| `apps/desktop/app.cpp:4971` | `struct GitTail {` | The space's git tail (spaces.js 425-442): "repo · branch" without the parts the name already |
| `apps/desktop/app.cpp:5005` | `void confirm_close_space(App::State& s, int index) {` | "Close space?" (window.rs 3067-3080): "<name> — N tabs, M terminals", always plural as |
| `apps/desktop/app.cpp:5006` | `void confirm_close_space(App::State& s, int index) {` | window.rs 3075 writes it. |
| `apps/desktop/app.cpp:5021` | `void draw_brand(App::State& s, ImVec2 pos, float w, bool rail) {` | workspace-chrome/main.js 48-53: the mark and name, then search and the sidebar toggle (no |
| `apps/desktop/app.cpp:5056` | `GitInfo space_git(App::State& s, const ui::Space& sp) {` | Git facts of a space's first terminal (spaces.js 22-27 place(): where the space is now). |
| `apps/desktop/app.cpp:5069` | `const std::size_t n = s.ws.spaces.size();` | spaces.js 298-335: each linked worktree goes right under the first main checkout of its |
| `apps/desktop/app.cpp:5117` | `if (menu_row(s, ui::icon::kEdit, "Rename", s.keymap.chord_for("space.rename"))) {` | herdr menu/main.js 84 menuIcon(): ".rename" -> square-pen, "worktree" -> git-branch (all |
| `apps/desktop/app.cpp:5145` | `std::map<std::string, std::string> starts;` | A space remembers where it was made (herdr Space.folder): its name when nothing else says. |
| `apps/desktop/app.cpp:5154` | `if (!rail) {` | Header (31 px): "Spaces" + "+" (spaces.js 541-545). |
| `apps/desktop/app.cpp:5163` | `if (icon_button(s, "##space-new", ui::icon::kPlus, bp, b, fpx(14), nullptr, g_pal->dim))` | AgentDesk's own (herdr opens its one shell): pick the shell the new space starts in, |
| `apps/desktop/app.cpp:5179` | `int active_at = -1;` | Where the active space's row is (spaces.js 342-345, 533-539). |
| `apps/desktop/app.cpp:5191` | `const float plus_x = a.x + rw - px(8) - px(11) - px(7) - px(6) - px(7) - px(16);` | herdr: a small "+" link (space.new {machine}) when connected, left of the dot. |
| `apps/desktop/app.cpp:5214` | `if (hov)` | spaces/main.js 370: "<m> · connected", "<m> · <state>", or "This computer". |
| `apps/desktop/app.cpp:5246` | `const bool empty = ri + 1 >= rows.size() ¦¦ rows[ri + 1].index < 0;` | herdr spaces/main.js 352, 396-402, 413: an empty, unfolded machine shows "Loading…" |
| `apps/desktop/app.cpp:5300` | `const float cw = px(32), ch = px(26);` | (spaces.js 501-515). |
| `apps/desktop/app.cpp:5344` | `if (ImGui::IsItemActivated() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))` | machine moves the space there (spaces.js 466-489). |
| `apps/desktop/app.cpp:5372` | `x += px(22);` | A worktree: indented 22 px, the branch icon in place of the avatar (spaces.js 494). |
| `apps/desktop/app.cpp:5441` | `{` | list is resized; a row already in view stays put (spaces.js 533-539, 547-548). |
| `apps/desktop/app.cpp:5471` | `bool is_agent(const ui::SessionView& v) {` | Terminals whose program reports a status (herdr agents()): the host has a structured state |
| `apps/desktop/app.cpp:5500` | `std::string agent_detail(App::State& s, const std::string& id, const ui::SessionView& v) {` | The agent's live terminal title, cleaned like agents.js 28-51: no leading status glyph or |
| `apps/desktop/app.cpp:5547` | `std::string ago_ms(long long ms) {` | "<1m", "5m", "2h", "3d" from ms (agents.js 67-73). |
| `apps/desktop/app.cpp:5558` | `void track_agents(App::State& s, const std::vector<std::string>& list) {` | agents.js 95-110: stamps agents whose state changed. Done and idle count as one: looking at a |
| `apps/desktop/app.cpp:5597` | `void refresh_git_facts(App::State& s) {` | herdr spaces.js 128-133 and 266-286: a command or an agent's turn that ends may have committed |
| `apps/desktop/app.cpp:5613` | `bool live_agent(App::State& s, const std::string& id, const ui::SessionView& v) {` | An agent as herdr's agents() lists it (window.rs 1855-1883, model.rs 597-605): a running |
| `apps/desktop/app.cpp:5624` | `std::map<std::string, std::vector<std::string>> kids;` | Live terminals by the agent that started them (agents.js 171-172), and the agents: a |
| `apps/desktop/app.cpp:5628` | `std::map<std::string, std::size_t> seq;` | creation order (agents.js 240 seq) |
| `apps/desktop/app.cpp:5645` | `if (!live_agent(s, id, v))` | herdr window.rs 1855-1883, model.rs 597-605: the list is built from the panes in the |
| `apps/desktop/app.cpp:5659` | `if (!rail) {` | its own, unless it's pinned (agents.js 231-234). The rail shows no trees. |
| `apps/desktop/app.cpp:5667` | `std::map<std::string, int> urgency_of;` | Most urgent of the agent and its sub-agents (agents.js 238). |
| `apps/desktop/app.cpp:5688` | `s.agent_order = list;` | agents.js 244 setAgentOrder |
| `apps/desktop/app.cpp:5735` | `const float hy = pos.y + px(14);` | (agents.js 424-445). |
| `apps/desktop/app.cpp:5744` | `ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(5), px(5)));` | The sort menu (agents.js 140-164): "Sort agents", then 31 px rows with each sort's icon |
| `apps/desktop/app.cpp:5805` | `auto sub_row = [&](const ui::TreeRow& r, bool compact) {` | A sub-agent's row (agents.js 182-206): its tree lines, icon, name and status. |
| `apps/desktop/app.cpp:5841` | `program_icon(d, program_of(v), ImVec2(x, cy - px(7)), px(14), g_pal->text);` | agents/main.js 185 |
| `apps/desktop/app.cpp:5853` | `run_command(s, "agent.peek:" + k);` | agents.js 199: one without a pane is peeked |
| `apps/desktop/app.cpp:5859` | `auto agent_row = [&](const std::string& id, int index, const std::vector<ui::TreeRow>& sub` | An agent's row (agents.js 298-374). |
| `apps/desktop/app.cpp:5868` | `const ImVec2 a(a0.x + px(6), a0.y);` | 14 px cell (agents.js 303-317). |
| `apps/desktop/app.cpp:5904` | `program_icon(d, program_of(v), ImVec2(x0, cy - px(8)), px(16), g_pal->text);` | agents/main.js 299 |
| `apps/desktop/app.cpp:6008` | `auto group_header = [&](const std::string& key, const std::string& text, ImU32 col,` | (agents.js 386-412). |
| `apps/desktop/app.cpp:6054` | `sub_row(r, by_priority);` | sub-rows are compact only in Priority (agents.js 250) |
| `apps/desktop/app.cpp:6062` | `struct Group {` | (agents.js 376-384). |
| `apps/desktop/app.cpp:6114` | `void draw_sidebar_footer(App::State& s, ImVec2 pos, float w, bool rail) {` | workspace-chrome/main.js 35-46: Settings in muted, Add machine all dim; hover brings both to |
| `apps/desktop/app.cpp:6148` | `void draw_new_menu(App::State& s) {` | herdr new-menu (defaults/mods/new-menu/main.js): Terminal, "Start an agent" (installed ones |
| `apps/desktop/app.cpp:6150` | `void draw_new_menu(App::State& s) {` | 31 px, hover "selected"; Up/Down/Enter/Esc like the context menus (menu/main.js 131-137). |
| `apps/desktop/app.cpp:6174` | `program_icon(dl, *prog, ImVec2(a.x + px(9), cy - px(7)), px(14), col);` | new-menu/main.js 65 |
| `apps/desktop/app.cpp:6190` | `const std::string machine = s.ws.active() != nullptr ? s.ws.active()->machine : std::strin` | The agents of the active space's machine (herdr knownAgents({on: machine})). |
| `apps/desktop/app.cpp:6195` | `for (const auto& [id, name] : shell_profiles(s, machine))` | the focused folder, like Terminal (AgentDesk's own; herdr has the one shell). |
| `apps/desktop/app.cpp:6205` | `const ImGuiIO& io = ImGui::GetIO();` | (new-menu/main.js 56). |
| `apps/desktop/app.cpp:6243` | `const bool space = s.ws.active() != nullptr;` | new-menu/main.js 69-71: worktrees need an open space (no hint on these rows). New space has |
| `apps/desktop/app.cpp:6320` | `const float plus_x = right - px(26);` | tabs/main.js 147-150: the tab strip is flex_1, so "+" sits at its right, next to Files. |
| `apps/desktop/app.cpp:6334` | `auto& tab = sp->tabs[static_cast<std::size_t>(i)];` | tabs/main.js 93: every tab of the space, settled ones too (herdr's bar does not |
| `apps/desktop/app.cpp:6338` | `std::string label = tab.name;` | herdr: an unnamed tab follows its agent's live title; a shell shows its program. |
| `apps/desktop/app.cpp:6397` | `ImGui::SetNextItemAllowOverlap();` | The close button is drawn on top of the tab (herdr tabs/main.js): let it take the |
| `apps/desktop/app.cpp:6412` | `run_command(s, "tab.rename:" + tab.id);` | tabs/main.js 113 |
| `apps/desktop/app.cpp:6413` | `if (begin_menu("##tabctx")) {` | menu/main.js 58-63: Rename, New tab, Close, all by the tab's id. |
| `apps/desktop/app.cpp:6436` | `program_icon(dl, program_of(v), ImVec2(cx, cy - px(7)), px(14), g_pal->dim);` | tabs/main.js 120 |
| `apps/desktop/app.cpp:6470` | `const float wheel = ImGui::GetIO().MouseWheel;` | The wheel over the bar switches tabs (herdr tabs.js wheel handling). |
| `apps/desktop/app.cpp:6500` | `if (icon_button(s, "##tab-new", ui::icon::kPlus, ImVec2(plus_x, pos.y + (height - px(26)) ` | "+" -> new-menu (tabs/main.js 150), right of the flex_1 tab strip; no tooltip there. |
| `apps/desktop/app.cpp:6506` | `run_command(s, "tab.close:" + close_tab);` | by id: the shown tab stays (menu/main.js 62) |
| `apps/desktop/app.cpp:6511` | `struct TerminalsLook {` | herdr layouts/terminals.json + mods/terminals-bar + mods/terminals-pane. herdr gives it its own |
| `apps/desktop/app.cpp:6515` | `ImU32 tray, tab, tab_edge, hover, edge, chip, text, dim, tab_text;` | terminals-bar/main.js 7-17 |
| `apps/desktop/app.cpp:6537` | `std::string terminals_lead(App::State& s, const ui::Tab& tab) {` | The pane a tab speaks for (terminals-bar/main.js 25-29): the focused one, else its agent, else |
| `apps/desktop/app.cpp:6549` | `void terminals_tile(App::State& s, ImDrawList* dl, ImVec2 a, const std::string& id, const ` | The program's tile (terminals-bar/main.js 31-53): 26 px, radius 7, a faint white edge, the |
| `apps/desktop/app.cpp:6553` | `std::optional<ui::Program> g;` | herdr: bg(g.tile ?? chip), border #ffffff14, the icon 15 px (tinted: g.color ?? text). |
| `apps/desktop/app.cpp:6570` | `void draw_terminals_bar(App::State& s, ImVec2 pos, float width, float height) {` | The draggable top bar (terminals-bar/main.js 94-187): the space chip (opens the picker), the |
| `apps/desktop/app.cpp:6624` | `icon_btn("##tm-keys", "\xF3\xB0\x98\xB3", "app.palette");` | nf-md-apple_keyboard_command (herdr command.svg) |
| `apps/desktop/app.cpp:6670` | `if (begin_menu("##tabctx")) {` | the tab menu (menu/main.js 58-63), by id |
| `apps/desktop/app.cpp:6707` | `void draw_terminals_pane_header(App::State& s, const std::string& id, const ui::SessionVie` | The pane header of the Terminals layout (terminals-pane/main.js 34-61): 38 px; pl 12, pr 8, |
| `apps/desktop/app.cpp:6716` | `program_icon(dl, program_of(v), ImVec2(x, cy - px(8)), px(16), IM_COL32(0xd6, 0xd8, 0xdb, ` | terminals-pane/main.js 53 |
| `apps/desktop/app.cpp:6735` | `const B buttons[] = {{"##th-x", ui::icon::kClose, "pane.close"},` | terminals-pane/main.js 19-32: no tooltips. |
| `apps/desktop/app.cpp:6827` | `const std::string chord = s.keymap.chord_for("app.palette");` | workspace-status/main.js 8-15: without a binding the hint is just "commands". |
| `apps/desktop/app.cpp:6860` | `const bool tm = s.layout().style == "terminals";` | herdr pane frame (main.json 107-131, pane.js 59-91): radius 10, 1 px border, 1 px accent |
| `apps/desktop/app.cpp:6862` | `const bool tm = s.layout().style == "terminals";` | The Terminals layout (terminals.json 37-45): radius 12, a quiet border and focus edge, and |
| `apps/desktop/app.cpp:6864` | `const bool tm = s.layout().style == "terminals";` | Herdr 98 (win98.json 26-58, kit.js 464-473): radius 0, a 3 px #c0c0c0 border (the same |
| `apps/desktop/app.cpp:6893` | `const bool pane_hovered =` | herdr pane/main.js 86-90: the buttons show while the header is hovered. Pressing one |
| `apps/desktop/app.cpp:6900` | `program_icon(dl, prog, ImVec2(hx, cy - px(7)), px(14), g_pal->dim);` | pane/main.js 76 |
| `apps/desktop/app.cpp:6902` | `std::string title = s.pane_title(id);` | pane/main.js 61: name \|\| ((agent \|\| shell) && program.name) \|\| title \|\| program.name \|\| "shell". |
| `apps/desktop/app.cpp:6914` | `const float tw = text_clipped(dl, s.fonts.ui_medium, fpx(12), ImVec2(hx, cy - fpx(12) * 0.` | pane/main.js 69, 78-79: the name 12 px medium, the folder 12 px dim. |
| `apps/desktop/app.cpp:6921` | `auto hbtn = [&](const char* bid, const char* glyph, const char* cmd) {` | pane/main.js 44-56: no tooltips on these buttons. |
| `apps/desktop/app.cpp:6949` | `{` | Grip (herdr pane_drag.rs): 28x18 in the middle of the header, two rows of three dots; |
| `apps/desktop/app.cpp:7021` | `const ImVec2 body_min = w98p ? ImVec2(pos.x + edge + px(4), top + px(2))` | herdr main.json terminal_padding [14, 12]; the Terminals layout keeps the default [10, 6] |
| `apps/desktop/app.cpp:7022` | `const ImVec2 body_min = w98p ? ImVec2(pos.x + edge + px(4), top + px(2))` | (settings.rs 564); Herdr 98 [4, 2] inside its 3 px border (win98.json 93-96). |
| `apps/desktop/app.cpp:7037` | `vs.view.menu_rows = [&s, id, parent = v.parent] {` | herdr pane menu (menu/main.js 24-81): Split right, Split down, Zoom, Rename, where |
| `apps/desktop/app.cpp:7040` | `menu_keys();` | Up/Down/Enter/Esc (menu/main.js 131-137) |
| `apps/desktop/app.cpp:7055` | `if (pick(ui::icon::kSquare, "Zoom", "pane.zoom")) {` | menu/main.js 84: square |
| `apps/desktop/app.cpp:7059` | `if (pick(ui::icon::kEdit, "Rename", "pane.rename")) {` | menu/main.js 84: square-pen |
| `apps/desktop/app.cpp:7071` | `if (pick(ui::icon::kTerminal, label.c_str(), nullptr)) {` | menu/main.js 84: terminal |
| `apps/desktop/app.cpp:7215` | `std::string panel_folder(App::State& s, std::string* machine = nullptr) {` | Folder the panels follow: the focused pane's live folder (herdr files.js 6-12), on the machine |
| `apps/desktop/app.cpp:7253` | `enum class CodeTok : std::uint8_t { kText, kKeyword, kString, kComment, kFunction, kType, ` | ---- read-only code preview (herdr files/main.js:141-144 shows files in a read-only editor with |
| `apps/desktop/app.cpp:7672` | `const ImVec2 area(size.x - 2, pos.y + size.y - top - px(4));` | Read-only editor in the terminal font at 12 (herdr files/main.js:141-144). |
| `apps/desktop/app.cpp:7797` | `const std::string badge = std::to_string(p.changes.size()) + " files";` | herdr changes/main.js render(): always "<n> files" and +add −del, even at 0. |
| `apps/desktop/app.cpp:7817` | `const float list_h = std::clamp(body_h * 0.5f, px(44),` | herdr: the list and the diff are both flex_1 (equal halves); the list is at least 44 px and |
| `apps/desktop/app.cpp:7888` | `if (p.changes_more) {` | its own row under the list (herdr: px 12, py 4, 11 px, dim) |
| `apps/desktop/app.cpp:7956` | `static std::string typed;` | Esc leaves the box and keeps the message (herdr changes/main.js on_key_down); ImGui would |
| `apps/desktop/app.cpp:8119` | `ImU32 popover_color() {` | ------------------------------------------------------------------ overlays (herdr mods) |
| `apps/desktop/app.cpp:8125` | `void push_overlay_style() {` | herdr overlay frame: popover background, 1 px border, radius 8. |
| `apps/desktop/app.cpp:8141` | `kButton,` | help/main.js 61, picker 250, machine-add 305: px 6, py 2, rounded 5, 11 px dim, |
| `apps/desktop/app.cpp:8143` | `kPlain,` | prompt/main.js 42, confirm/main.js 42: plain 11 px dim text |
| `apps/desktop/app.cpp:8171` | `void worktree_title(App::State& s, const std::string& title, const std::string& right = {}` | worktree/main.js 301, 349: the title at the dialog's 13 px, semibold, no Esc; `right` (Open's |
| `apps/desktop/app.cpp:8191` | `bool worktree_input(App::State& s, const char* id, const char* hint, char* buf, std::size_` | worktree/main.js 305, 353-361: the input, h 32, px 10, rounded 6, a 1 px border (accent while |
| `apps/desktop/app.cpp:8241` | `std::optional<float> palette_score(const std::string& title, const std::string& query, std` | herdr palette fuzzy score (palette/main.js 112-129): letters in order, +1 per hit, +6 at a word |
| `apps/desktop/app.cpp:8294` | `void draw_palette(App::State& s) {` | Command palette (herdr defaults/frame/palette/main.js): 18 px padding, 10 px gap; a 36 px |
| `apps/desktop/app.cpp:8310` | `struct Named {` | palette/main.js 22-63: the named entries (command, title, group, keywords), in order. |
| `apps/desktop/app.cpp:8369` | `if (std::string_view(n.cmd) == "app.quit")` | palette/main.js 62 |
| `apps/desktop/app.cpp:8374` | `for (const auto& l : s.layouts) {` | palette/main.js 86-89: "Layout: <Name>", capitalised; main is "Herdr" (ours: the |
| `apps/desktop/app.cpp:8375` | `for (const auto& l : s.layouts) {` | "workspace" layout, "AgentDesk"), terminals "Terminals", win98 "Herdr 98". |
| `apps/desktop/app.cpp:8385` | `for (const auto& c : kCommands)` | Then every other command, in its group (herdr adds the app's other commands the same way). |
| `apps/desktop/app.cpp:8401` | `e.score = *sc2 - 4;` | palette/main.js 179-180 |
| `apps/desktop/app.cpp:8417` | `std::vector<const Entry*> recent;` | Recent (5 of the last 20), then the groups in herdr's order without them (:170-173). |
| `apps/desktop/app.cpp:8467` | `push_overlay_style();` | padding 18 (palette/main.js 271) |
| `apps/desktop/app.cpp:8475` | `ImDrawList* dl = ImGui::GetWindowDrawList();` | palette/main.js 286-300: h 36, px 10, gap 10, radius 9, accent border, background fill, |
| `apps/desktop/app.cpp:8515` | `const float room = vp->WorkPos.y + vp->WorkSize.y * 0.85f - ImGui::GetCursorScreenPos().y ` | most 85 % of the window in herdr). |
| `apps/desktop/app.cpp:8550` | `float x = a.x + px(10);` | muted (palette/main.js 187-203). |
| `apps/desktop/app.cpp:8579` | `} else {` | palette/main.js 318: px 16, py 14, 13 dim |
| `apps/desktop/app.cpp:8637` | `std::vector<Attention> space_atts;` | picker/main.js 86: a space is busy when it has any status (idle too), from all its tabs. |
| `apps/desktop/app.cpp:8647` | `std::string programs;` | picker/main.js 79-80, 206, 224: the tab's agents only, each by its program's name |
| `apps/desktop/app.cpp:8680` | `const std::string tname = ui::Workspace::tab_title(tab, t);` | An unnamed tab is listed by its number (herdr model.rs 282-288 tab_name). |
| `apps/desktop/app.cpp:8693` | `const auto plan = ui::plan_picker(facts, !q.empty(), s.picker_earlier);` | picker/main.js 86-98: busy spaces (blocked first), then "earlier: N quiet spaces", always |
| `apps/desktop/app.cpp:8784` | `ImGui::Dummy(ImVec2(fw, px(31)));` | picker/main.js 157-168: px 10, pt 10, pb 3, gap 8; an 18 px avatar (rounded 5, |
| `apps/desktop/app.cpp:8827` | `const float x = gx + px(16);` | picker/main.js 216-224: the name takes the room (flex_1, truncated); then, gap 10, |
| `apps/desktop/app.cpp:8882` | `std::optional<std::string> update_label() {` | ---- the update pill (herdr window.rs 4820-4890) and agent-host's update error (4800-4815) |
| `apps/desktop/app.cpp:8883` | `std::optional<std::string> update_label() {` | What's waiting, in words (window.rs 4824-4833). |
| `apps/desktop/app.cpp:8899` | `const auto app_version = ui::updates::ready();` | 4835-4841. herdr also shows "Updated to <version>" when no release notes opened by |
| `apps/desktop/app.cpp:8999` | `s.core_update_error_dismissed = core.error;` | window.rs 3973-3976 |
| `apps/desktop/app.cpp:9013` | `const float pill = draw_update_pill(s);` | window.rs 4727-4732: the update pill sits at the bottom; toasts go above it. |
| `apps/desktop/app.cpp:9018` | `for (auto it = s.toasts.begin(); it != s.toasts.end(); ++it, ++n) {` | window.rs 4794-4796: toast_y grows with each toast in the order opened, so the oldest sits |
| `apps/desktop/app.cpp:9034` | `if (ImGui::IsWindowHovered())` | toast/main.js 17-37: px 18, gap 12, items centred; the hover background; an 8 px dot, |
| `apps/desktop/app.cpp:9056` | `draw_core_update_error(s, vp->WorkPos.y + vp->WorkSize.y - y);` | window.rs 4800-4801: after the toasts, at the next toast_y. |
| `apps/desktop/app.cpp:9060` | `bool dialog_outside_click() {` | herdr window.rs 4764-4777: a dialog dims the window behind it and a press outside it closes it |
| `apps/desktop/app.cpp:9071` | `void draw_peek(App::State& s) {` | Peek (window.rs 3949-3960, mods/peek): a dialog 80 % x 80 %, a 34 px bar over the live |
| `apps/desktop/app.cpp:9072` | `void draw_peek(App::State& s) {` | terminal. Esc stays with the terminal (window.rs 4757: an overlay with a layout takes no |
| `apps/desktop/app.cpp:9104` | `dl->AddRectFilled(p, ImVec2(p.x + bw, p.y + px(34)), g_pal->card, px(8), ImDrawFlags_Round` | peek/main.js 52-74: surface, bottom border; px 12, gap 8, 12 px text. |
| `apps/desktop/app.cpp:9110` | `if (att == Attention::kWorking)` | Glyph: spinner working, ○ idle, ● a status, · none (peek/main.js 51). |
| `apps/desktop/app.cpp:9120` | `program_icon(dl, program_of(v), ImVec2(x, cy - px(7)), px(14), g_pal->text);` | peek/main.js 49 |
| `apps/desktop/app.cpp:9124` | `const char* word = att == Attention::kWorking   ? "working"` | The status word only when there is a status (peek/main.js 63). |
| `apps/desktop/app.cpp:9199` | `if (push) {` | herdr "solid" buttons: foreground colour fill, background-coloured text. |
| `apps/desktop/app.cpp:9209` | `void draw_help(App::State& s) {` | Keybinds (defaults/mods/help/main.js, window.rs 3614 dialog 640 x 520): every keys.json entry |
| `apps/desktop/app.cpp:9237` | `ImGui::SetNextItemWidth(-1);` | help/main.js 63: h 36, px 10, radius 9, accent border, background fill, 12.8 text. |
| `apps/desktop/app.cpp:9262` | `ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, px(2)));` | rows 2 apart (help/main.js 74) |
| `apps/desktop/app.cpp:9360` | `px(14), g_pal->text);` | new-menu/main.js 65 |
| `apps/desktop/app.cpp:9423` | `if (!mod_clone && !ImGui::IsWindowAppearing())` | A second rename press closes the prompt (window.rs 3047-3051). Keys do not reach the |
| `apps/desktop/app.cpp:9433` | `bool cancel = overlay_title(s, title, EscChip::kPlain);` | prompt/main.js 42 |
| `apps/desktop/app.cpp:9466` | `std::string name = s.rename_buf;` | window.rs 3138: the name is trimmed. |
| `apps/desktop/app.cpp:9503` | `std::string branch_slug(const std::string& b) {` | ------------------------------------------------------------------ worktrees (herdr mods/worktree) |
| `apps/desktop/app.cpp:9504` | `std::string branch_slug(const std::string& b) {` | herdr slug: runs of characters other than letters, digits and "._-" become "-", trimmed. |
| `apps/desktop/app.cpp:9506` | `return host::worktree_slug(b);` | herdr worktree/main.js slug(), shared with the host's default |
| `apps/desktop/app.cpp:9533` | `worktree_title(s, g.repo ? "New worktree of " + repo : std::string("New worktree"));` | worktree/main.js 349 |
| `apps/desktop/app.cpp:9565` | `const std::string branch = trimmed(s.wt_branch);` | herdr create(): this.text.trim() |
| `apps/desktop/app.cpp:9655` | `bool close = false;` | worktree/main.js 301: the title, and "N checkouts" at its right (11 px dim). |
| `apps/desktop/app.cpp:9686` | `if (n == 0) {` | herdr: p 10, dim |
| `apps/desktop/app.cpp:9711` | `auto inside = [](const std::string& p, const std::string& dir) {` | herdr spaceOf(): a terminal in the checkout's folder, the deepest checkout winning. |
| `apps/desktop/app.cpp:9747` | `float line_h(float size) {` | ------------------------------------------------------------------ settings (herdr settings.js) |
| `apps/desktop/app.cpp:9748` | `float line_h(float size) {` | Dialog 880 wide (window.rs 3594), 620 high, left navigation 190 px (settings/main.js 897-918); |
| `apps/desktop/app.cpp:9750` | `float line_h(float size) {` | herdr's own. |
| `apps/desktop/app.cpp:9760` | `void settings_wrapped(App::State& s, const char* text, ImU32 color, float size = 12.5f, fl` | Dim text wrapped to the body's width (herdr: 12.5 px with 18 px lines, or `size` and `lh`). |
| `apps/desktop/app.cpp:9784` | `void settings_section(App::State& s, const char* title) {` | settings/main.js 157: pt 6, pb 8, 12 px semibold, dim, upper case. |
| `apps/desktop/app.cpp:9794` | `void settings_row(App::State& s, const char* label, const char* note, float control_w, flo` | settings/main.js 161-176 row(): gap 16, py 12, items centred, a 1 px bottom border; the |
| `apps/desktop/app.cpp:9840` | `constexpr float kStepperW = 28 + 54 + 28 + 2;` | settings/main.js 196-222 stepper(): one bordered group (rounded 7, raised): 28 x 28 cells |
| `apps/desktop/app.cpp:9882` | `const std::vector<ui::FontFamily>& font_families() {` | ---- fonts (herdr: every installed family, filterable; weight; interface font) |
| `apps/desktop/app.cpp:9903` | `void apply_terminal_font(App::State& s) {` | family's 700 face. A layout with fonts of its own (Herdr 98: win98.json "font") uses those. |
| `apps/desktop/app.cpp:9947` | `bool font_picker(App::State& /*s*/, const char* id, std::string& family, const char* defau` | herdr font list: a filter box and up to 200 matching families. |
| `apps/desktop/app.cpp:9991` | `bool segmented(App::State& s, const char* id, std::string& value,` | A row of choices, one selected (herdr's segment control). |
| `apps/desktop/app.cpp:10022` | `void layout_preview(ImDrawList* d, const std::string& name, ImVec2 a, ImVec2 size) {` | herdr frame/settings/main.js 242-270: a small drawing of each layout in the theme's colors. |
| `apps/desktop/app.cpp:10084` | `ImVec2 settings_button_size(App::State& s, const char* label, ImFont* font = nullptr) {` | ---- herdr settings main.js helpers used by Updates and Privacy |
| `apps/desktop/app.cpp:10085` | `ImVec2 settings_button_size(App::State& s, const char* label, ImFont* font = nullptr) {` | main.js 180-192: px 12, py 5, rounded 7, a 1 px border, raised, 13 px text, hover background. |
| `apps/desktop/app.cpp:10105` | `bool settings_primary(App::State& s, const char* id, const char* label) {` | main.js 787-788 primary(): px 12, py 5, rounded 7, accent fill, accent_foreground semibold 13, |
| `apps/desktop/app.cpp:10120` | `void settings_chip(App::State& s, const char* label, ImU32 color) {` | main.js 779, 781: a button's place taken by text (px 12, py 5, 13 px). |
| `apps/desktop/app.cpp:10130` | `bool settings_toggle(const char* id, bool on) {` | main.js 225-238: 36 x 20, rounded 10, padding 2; accent when on (knob at the end), hover |
| `apps/desktop/app.cpp:10144` | `void settings_footnote(App::State& s, const char* text) {` | main.js 757, 815: a dim note under the rows (pt 12, 12.5 px, 18 px lines). |
| `apps/desktop/app.cpp:10166` | `std::string clock_at(std::uint64_t secs) {` | main.js 764-767: local HH:MM. |
| `apps/desktop/app.cpp:10176` | `std::string local_date_time(std::uint64_t secs) {` | main.js 847: new Date(secs * 1000).toLocaleString(): the date and time as Windows writes them. |
| `apps/desktop/app.cpp:10192` | `bool settings_updates(App::State& s) {` | ---- Updates (herdr settings main.js 760-816). True: the settings dialog should close. |
| `apps/desktop/app.cpp:10205` | `const auto check_w = [&]() -> float {` | The check control's look (main.js 775-786). |
| `apps/desktop/app.cpp:10223` | `std::string app_text;` | main.js 789-793 |
| `apps/desktop/app.cpp:10235` | `const auto& c = u.core;` | main.js 794-805 |
| `apps/desktop/app.cpp:10292` | `settings_section(s, "AgentDesk");` | main.js 806-811: heading "Herdr" (the product), "Herdr <version>", What's new + the control. |
| `apps/desktop/app.cpp:10300` | `close = true;` | main.js 810: closeOverlay, then app.whats_new |
| `apps/desktop/app.cpp:10309` | `ImGui::Dummy(ImVec2(0, px(18)));` | main.js 812-814: 18 px, heading "herdr-core", "herdr-core <version>". |
| `apps/desktop/app.cpp:10318` | `settings_footnote(s, "AgentDesk looks for both every hour and tells you in the corner. age` | main.js 815 |
| `apps/desktop/app.cpp:10324` | `void settings_report(App::State& s, const std::string& title, const std::string& about,` | main.js 821-842: a report as it goes out, one line per line of JSON. |
| `apps/desktop/app.cpp:10366` | `bool settings_privacy(App::State& s) {` | ---- Privacy (herdr settings main.js 844-882). True: ui.json changed. |
| `apps/desktop/app.cpp:10383` | `s.settings.telemetry = !on;` | main.js 858-861: on -> "telemetry": false; off -> the key goes. |
| `apps/desktop/app.cpp:10386` | `ui::telemetry::set_setting(s.settings.telemetry);` | window.rs 955: read again on change |
| `apps/desktop/app.cpp:10409` | `if (const auto q = st.find("queued"); q != st.end() && q->is_array())` | main.js 879-881 |
| `apps/desktop/app.cpp:10434` | `const float w = std::min(px(880), vp->WorkSize.x - px(16));` | window.rs 3594: width 880 |
| `apps/desktop/app.cpp:10453` | `dl->AddText(s.fonts.ui_bold, fpx(16), ImVec2(wp.x + px(10) + px(10), wp.y + px(10) + px(6)` | settings/main.js 897-899: nav p 10; "Settings" px 10, pt 6, pb 12, 16 px semibold. |
| `apps/desktop/app.cpp:10459` | `static constexpr Section kSections[] = {{"Look", ui::icon::kPalette},  {"Machines", ui::ic` | main.js 9-16: palette, server, bell, refresh, shield (Mods is ours: C++ DLL mods). |
| `apps/desktop/app.cpp:10463` | `const auto upd = ui::updates::status();` | main.js 888-889: something waits to be installed. |
| `apps/desktop/app.cpp:10469` | `const float item_w = nav_w - px(20);` | main.js 900-918: items px 10, py 7, gap 10, rounded 7, gap 2 between; a 15 px icon (accent |
| `apps/desktop/app.cpp:10491` | `if (std::string_view(sec.name) == "Updates" && waiting)` | main.js 917: a 7 px accent dot at the row's end. |
| `apps/desktop/app.cpp:10496` | `{` | main.js 967: "Send feedback", a settings button at the bottom of the nav; it closes |
| `apps/desktop/app.cpp:10508` | `ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(28), px(22)));` | main.js 949-990: no title and no Esc in the body (Esc still closes); padding px 28, py 22 |
| `apps/desktop/app.cpp:10541` | `CardFlow lf = flow();` | herdr frame/settings/main.js 272-320: cards 124 wide, p 6, rounded 10, a 2 px accent |
| `apps/desktop/app.cpp:10561` | `if (l.style == "win98")` | herdr settings/main.js 86-99, 302-309: a layout with a "preview" picture shows it |
| `apps/desktop/app.cpp:10567` | `std::string label = l.name == "workspace" ? std::string("AgentDesk")` | herdr LOOKS: main -> "Herdr" (ours is AgentDesk), terminals -> "Terminals", |
| `apps/desktop/app.cpp:10568` | `std::string label = l.name == "workspace" ? std::string("AgentDesk")` | win98 -> "Herdr 98" (settings/main.js 18). |
| `apps/desktop/app.cpp:10581` | `settings_gap(px(18));` | main.js 520 |
| `apps/desktop/app.cpp:10587` | `settings_gap(px(12));` | main.js 523-527: the note (pt 12, pb 8, 12.5 dim), "Dark" (pb 6, 12.5 medium muted), |
| `apps/desktop/app.cpp:10597` | `CardFlow tf = flow();` | main.js 353-405 themePicker(): cards 124 wide, gap 6, p 6, rounded 10, a 2 px |
| `apps/desktop/app.cpp:10654` | `settings_gap(px(18));` | main.js 530 |
| `apps/desktop/app.cpp:10656` | `settings_row(s, "Zoom", "Everything: terminals, panels, text.",` | main.js 531-543: the Zoom stepper and a Reset button. |
| `apps/desktop/app.cpp:10676` | `settings_row(s, "Terminal font", nullptr, px(220) + px(8) + px(kStepperW), px(kStepperH));` | main.js 500-512 fontRow(): the font button (220 wide), gap 8, the size stepper |
| `apps/desktop/app.cpp:10687` | `settings_row(s, "Terminal font weight", "Bold text stays at 700.", px(kStepperW), px(kStep` | herdr settings main.js:546-548: a 100-900 stepper (step 100); the family's nearest |
| `apps/desktop/app.cpp:10734` | `settings_row(s, "Copy on select", "Copy the selection when the mouse is released.", px(36)` | On and off as herdr's settings switch it (main.js 225-238 toggle()). |
| `apps/desktop/app.cpp:10762` | `settings_section(s, "Machines");` | main.js 725-736 machines(): the heading, the note (pb 8), a "This computer" row with a |
| `apps/desktop/app.cpp:10769` | `const std::string v = s.host->host_version();` | main.js 730: "Connected · hc <version>" (ours: agent-host). |
| `apps/desktop/app.cpp:10778` | `static std::string edit_machine;` | herdr settings main.js machineRow(): Rename and Remove; both ask inline first. |
| `apps/desktop/app.cpp:10794` | `const std::string state = s.machine_state(m.name);` | main.js 637-662: py 12, gap 8, a bottom border; the head row (gap 10): a 7 px dot |
| `apps/desktop/app.cpp:10841` | `line("Remove " + m.name + "? Its terminals keep running there.", g_pal->dim, 12.5f);` | main.js 676-678: the question (12.5 muted), then Cancel and "Remove machine". |
| `apps/desktop/app.cpp:10852` | `line("New name (addresses using the old name stop working)", g_pal->faint, 12);` | main.js 679-683: "New name (...)" 12 px dim, then (gap 6) the input (h 32, px 10, |
| `apps/desktop/app.cpp:10891` | `if (state != "connected")` | main.js 685: what went wrong, 12.5 px in the blocked colour. |
| `apps/desktop/app.cpp:10902` | `if (s.settings.machines.empty()) {` | main.js 732-734: "No other machines yet." (py 14) and the notice (py 10), 12.5 px. |
| `apps/desktop/app.cpp:10938` | `settings_gap(px(16));` | main.js 736: pt 16, a settings button. |
| `apps/desktop/app.cpp:10943` | `settings_section(s, "Sounds");` | main.js 743-757 alerts(): Sounds; Play sounds (a toggle); the two sounds with Play; the |
| `apps/desktop/app.cpp:10956` | `const auto slash = file.find_last_of("\\/");` | herdr settings main.js:743: the file's name, not its path. |
| `apps/desktop/app.cpp:10966` | `settings_footnote(s, "Want your own sounds? Ask your agent to set sound_done and sound_blo` | main.js 757, with our file and keys (ui.json sound_done / sound_blocked). |
| `apps/desktop/app.cpp:10970` | `settings_section(s, "Installed");` | Ours (C++ DLL mods), laid out like herdr's pages: heading, note, rows with toggles. |
| `apps/desktop/app.cpp:11022` | `void play_alert(App::State& s, bool blocked) {` | ------------------------------------------------------------------ sounds (herdr sound.rs) |
| `apps/desktop/app.cpp:11052` | `void draw_welcome(App::State& s) {` | ------------------------------------------------------------------ welcome (herdr welcome.js) |
| `apps/desktop/app.cpp:11081` | `bool skip = false;` | Header (herdr welcome main.js:491-520): logo, "Welcome to AgentDesk", "Drag to move" and a |
| `apps/desktop/app.cpp:11118` | `ImGui::SetCursorScreenPos(ImVec2(wp.x, wp.y + hh + 1));` | The page scrolls between the header and the footer (herdr w-body, padding 22). |
| `apps/desktop/app.cpp:11125` | `auto title = [&](const char* t, const char* sub) {` | welcome/main.js 135-140 title(): 18 px semibold, then the paragraph (gap 6). |
| `apps/desktop/app.cpp:11136` | `auto feature = [&](const char* glyph, const char* head, const char* text) {` | welcome/main.js 217-223 feature(): a 32 px tile (rounded 9, raised, a 1 px border) with the |
| `apps/desktop/app.cpp:11157` | `auto para_at = [&](ImVec2 p, float width, const char* text, bool draw = true, ImU32 color ` | herdr welcome/main.js 139-141 para(): 13 px, 20 px lines, muted, the full width. Draws at |
| `apps/desktop/app.cpp:11188` | `title("AgentDesk runs your terminals in the background",` | welcome/main.js 284-293 pageOne (Herdr -> AgentDesk, herdr-core -> agent-host). Its |
| `apps/desktop/app.cpp:11189` | `title("AgentDesk runs your terminals in the background",` | "Coming from herdr?" box (261-280) is for herdr's earlier app and is left out. |
| `apps/desktop/app.cpp:11202` | `{` | welcome/main.js 290: a 1 px border line before Appearance and Size. |
| `apps/desktop/app.cpp:11207` | `ImGui::TextDisabled("Appearance");` | herdr welcome page 1: Appearance System / Light / Dark. |
| `apps/desktop/app.cpp:11224` | `{` | welcome/main.js 297-345: one rounded box (12, a 1 px border, the background): "This |
| `apps/desktop/app.cpp:11271` | `para("Not now? Add machine is at the bottom of the sidebar too.");` | welcome/main.js 345 |
| `apps/desktop/app.cpp:11273` | `title("Choose your panels",` | welcome/main.js 396-400. |
| `apps/desktop/app.cpp:11277` | `if (!s.welcome_panels_seen) {` | herdr welcome.js 396-413: the first visit to this page starts with both off; skipping |
| `apps/desktop/app.cpp:11284` | `auto toggle = [&](const char* id, const char* glyph, const char* label, const char* text) ` | welcome/main.js 348-394 panelCard(): p 12, gap 12, rounded 12, a 1 px border (accent |
| `apps/desktop/app.cpp:11333` | `if (auto* sp = s.ws.active()) {` | herdr togglePanel(): turning one on shows it at once; turning off the one shown |
| `apps/desktop/app.cpp:11345` | `toggle("files", ui::icon::kFile, "Files", "The folder you're in as a tree. Open a file to ` | welcome/main.js 18-21 PANELS. |
| `apps/desktop/app.cpp:11349` | `{` | welcome/main.js 403-411: p 12, rounded 10, raised. |
| `apps/desktop/app.cpp:11362` | `if (const std::string pal = s.keymap.chord_for("app.palette"); !pal.empty()) {` | welcome/main.js 416-436: first, while a key opens the palette, "One key opens |
| `apps/desktop/app.cpp:11399` | `ImGui::Dummy(ImVec2(0, px(8)));` | herdr page 4: a line to give your agent so it learns to drive the app. |
| `apps/desktop/app.cpp:11417` | `ImGui::Dummy(ImVec2(0, px(8)));` | herdr welcome/main.js 451-459: "More examples in Settings" (p 12, gap 10, rounded 10, |
| `apps/desktop/app.cpp:11418` | `ImGui::Dummy(ImVec2(0, px(8)));` | raised), naming Herdr 98. |
| `apps/desktop/app.cpp:11435` | `para("Something off or missing? Settings \xE2\x80\xBA Send feedback. We read every one.");` | welcome/main.js 461 |
| `apps/desktop/app.cpp:11462` | `if (!typing && (ImGui::IsKeyPressed(ImGuiKey_Enter, false) ¦¦ (ImGui::IsKeyPressed(ImGuiKe` | herdr: Enter or Right goes on; on the last page only Enter finishes. |
| `apps/desktop/app.cpp:11490` | `std::vector<std::pair<std::string, std::string>> ssh_config_hosts() {` | ------------------------------------------------------------------ add a machine (herdr machine-add) |
| `apps/desktop/app.cpp:11491` | `std::vector<std::pair<std::string, std::string>> ssh_config_hosts() {` | Port of herdr defaults/mods/machine-add/main.js parseConfig/includeFiles: named Host entries (no |
| `apps/desktop/app.cpp:11623` | `std::string shell_arg(const std::string& target) {` | herdr machine-add shellArg(): a target as a shell reads it. |
| `apps/desktop/app.cpp:11657` | `auto machine_of_target = [&](const std::string& target) -> const MachineConfig* {` | A machine counts as added once it answers (herdr: `hc machine add` succeeded). |
| `apps/desktop/app.cpp:11668` | `auto add = [&](std::string target) {` | herdr add(): connect at once; "Connecting" until it answers or says why not. |
| `apps/desktop/app.cpp:11731` | `auto row = [&](const std::string& target, const std::string& label, const std::string& sub` | One row (herdr row()): server icon, name, where it goes, then Added / Connecting / Add. |
| `apps/desktop/app.cpp:11749` | `const char* right = done ? "\xE2\x9C\x93 Added" : adding ? "Connecting" : "Add";` | machine-add/main.js 247-266: "Added" (ansi2), a 12 px working spinner and |
| `apps/desktop/app.cpp:11770` | `if (clicked && (idle ¦¦ (button && ImGui::IsMouseHoveringRect(ra, ImVec2(ra.x + rw_right, ` | The row adds while idle; after a failed add only its Add button does (main.js 268, 252). |
| `apps/desktop/app.cpp:11775` | `ImGui::SetCursorPosX(ImGui::GetCursorPosX() + px(35));` | main.js 233-243, 287: px 35, pb 6; the message (12 px blocked) and, when it needs |
| `apps/desktop/app.cpp:11795` | `auto section = [&](const char* text, float pt) {` | machine-add/main.js 320, 335: the section titles, 11.5 px medium dim, as written (pt 16 / |
| `apps/desktop/app.cpp:11805` | `ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(FLT_MAX, px(280)));` | herdr ma-hosts: at most 280 px, then it scrolls. |
| `apps/desktop/app.cpp:11818` | `ImGui::SetKeyboardFocusHere();` | herdr machine-add/main.js 73: the field has focus on open |
| `apps/desktop/app.cpp:11830` | `if (const auto st = s.machine_add_state.find(typed); !typed.empty() && st != s.machine_add` | The typed target's state under the box (herdr: Connecting, why not, or what to answer). |
| `apps/desktop/app.cpp:11834` | `const ImVec2 p = ImGui::GetCursorScreenPos();` | As the rows show it (machine-add/main.js 251): a spinner and "Connecting". |
| `apps/desktop/app.cpp:11861` | `constexpr const char* kWhatsNew[] = {` | herdr app.whats_new (release notes, shown once after an update) and app.feedback (a draft the |
| `apps/desktop/app.cpp:11882` | `const ImGuiViewport* vp = ImGui::GetMainViewport();` | window.rs 3584: 680 wide; feedback/main.js 86: 650 high. |
| `apps/desktop/app.cpp:11923` | `const ImVec2 wp = ImGui::GetWindowPos();` | feedback/main.js 86-112. AgentDesk has no server: Copy and Save to file stand in for |
| `apps/desktop/app.cpp:11924` | `const ImVec2 wp = ImGui::GetWindowPos();` | Send, and the line herdr shows when a build can't send says so. |
| `apps/desktop/app.cpp:12093` | `bool cancel = overlay_title(s, s.confirm_title.c_str(), EscChip::kPlain);` | confirm/main.js 42 |
| `apps/desktop/app.cpp:12107` | `const bool ok = ImGui::Button("Confirm", ImVec2(bw, px(30))) ¦¦ ImGui::IsKeyPressed(ImGuiK` | herdr confirm/main.js: Cancel and Confirm. |
| `apps/desktop/app.cpp:12208` | `` | ------------------------------------------------------------------ Herdr 98 (herdr-app win98) |
| `apps/desktop/app.cpp:12214` | `` | herdr window.rs 4439-4502 render_node: each shown node of the layout tree gets its rect |
| `apps/desktop/app.cpp:12216` | `` | apply_look), and each block is drawn by the renderer its content names. herdr runs mods for |
| `apps/desktop/app.cpp:12220` | `std::optional<ImU32> layout_color(const std::string& c) {` | A layout color: one of the theme's names (herdr themes: canvas, chrome, surface, ...) or |
| `apps/desktop/app.cpp:12242` | `void draw_stack_tabs(App::State& s, const lay::Node& stack, const lay::Rect& r) {` | window.rs 4611-4631 builtin_tabs: `"header": {"content": "tabs"}` on a stack: its children's |
| `apps/desktop/app.cpp:12279` | `const bool details = l.style.empty() && l.details.value_or(s.settings.details);` | AgentDesk's details pane (not herdr's): beside the panes when on. |
| `apps/desktop/app.cpp:12350` | `w98::background(s, dl, w98::Box{a.x, a.y, b.x, b.y});` | Herdr 98's wallpaper: the root's background (a click in herdr, layout_sync.rs 324). |
| `apps/desktop/app.cpp:12356` | `const auto c0 = layout_color(g->from);` | layout.rs 107-112: two colors at an angle; drawn top to bottom or left to right. A |
| `apps/desktop/app.cpp:12384` | `for (const auto& sep : s.placed.separators) {` | Separators (window.rs 4633-4669): the divider line, and a drag handle where the container |
| `apps/desktop/app.cpp:12392` | `if (row) {` | A line lw wide down the middle of the separator (window.rs 63-69 separator_line). |
| `apps/desktop/app.cpp:12537` | `if (connected && s.host->host_instance_id() != s.noted_host) {` | window.rs 879-887 note_core: on connect and every reconnect, what agent-host says it is. |
| `apps/desktop/app.cpp:12543` | `if (s.was_connected && !connected)` | window.rs 795-799: lost the core. |
| `apps/desktop/app.cpp:12547` | `reconcile_core_update_error(s.core_update_error_dismissed, ui::updates::core());` | window.rs 4142-4147: keep a dismissed core error dismissed until it changes. |
| `apps/desktop/app.cpp:12549` | `if (s.updates_asked) {` | settings main.js 65-71: after "Check now", follow the look, then "Up to date" for 3 s. |
| `apps/desktop/app.cpp:12587` | `if (Clock::now() - s.observed_at >= std::chrono::seconds(10)) {` | window.rs 4155-4158: every 25 polls of 400 ms, what is open. |
| `apps/desktop/app.cpp:12603` | `if (win98)` | kit.js 451-452: the MS-DOS font, 32 px with Large Fonts |
| `apps/desktop/app.cpp:12605` | `if (s.fonts_win98 != win98) {` | a layout with fonts of its own (Herdr 98), or back from it |
| `apps/desktop/app.cpp:12623` | `const auto pt = std::filesystem::last_write_time(data_dir() / L"programs.json", ec);` | herdr window.rs 982-991: programs.json is read again when it changes. |
| `apps/desktop/app.cpp:12661` | `std::set<ImGuiKeyChord> chords;` | The layout's section ("layout:<name>") first, then global (herdr keys.rs tab()). |
| `apps/desktop/app.cpp:12687` | `ImGui::GetWindowDrawList()->AddRectFilled(origin, ImVec2(origin.x + full.x, origin.y + ful` | The shown layout's tree (herdr main.json: the sidebar, a 1 px divider and the work column of |
| `apps/desktop/app.cpp:12695` | `if (win98)` | its floats, windows and menus (herdr overlays) |
| `apps/desktop/app.cpp:12727` | `if (!s.window_opened) {` | window.rs 832: the window is up. |
| `apps/desktop/app.hpp:51` | `[[nodiscard]] bool binds_chord(int chord) const;` | A window chord keys.json binds in the shown layout or globally (Herdr 98's Ctrl+Esc). |
| `apps/desktop/app.hpp:62` | `void on_quit();` | The loop ended (herdr main.rs 177-192 on_app_quit): telemetry's clean quit, then the |
| `apps/desktop/main.cpp:72` | `HWND g_main_hwnd = nullptr;` | herdr's Herdr 98 binds both (mods/win98/README.md 15-16: win98.start, win98.taskman); while |
| `apps/desktop/main.cpp:193` | `if (wparam == TRUE) {` | herdr's layout hides the system title bar and draws its own (window buttons in the |
| `apps/desktop/main.cpp:268` | `auto* drop = reinterpret_cast<HDROP>(wparam);` | (herdr src/terminal.rs:717-723). |
| `apps/desktop/main.cpp:393` | `{` | herdr update.rs 392-400 asks a downloaded copy `herdr-app --version` ("herdr-app 0.2.0"); |
| `apps/desktop/main.cpp:418` | `ui::updates::leave_install_folder();` | herdr main.rs 127-132: leave the install folder, put a waiting update in place (that |
| `apps/desktop/safe_read.hpp:69` | `struct Gradient {` | layout.rs 107-112 (libs/ui/src/layout.cpp read_background): two colors at an angle, 180 |
| `apps/desktop/win98_chrome.inl:1` | `` | Herdr 98, part 3: the window itself (herdr-app defaults/layouts/win98.json): the desktop, the |
| `apps/desktop/win98_chrome.inl:8` | `` | ---------------------------------------------------------------- helpers (herdr commands by id) |
| `apps/desktop/win98_chrome.inl:58` | `void select_tab_id(App::State& s, const std::string& id) {` | herdr tab.select {id}: selects the tab and its space; a settled tab is shown again. |
| `apps/desktop/win98_chrome.inl:69` | `void settle_tab_id(App::State& s, const std::string& id) {` | herdr tab.settle {id} (model.rs 470-484): the shown one hands over to the next unsettled tab; |
| `apps/desktop/win98_chrome.inl:95` | `void new_tab(App::State& s, const std::string& name, int space, const std::string& profile` | herdr tab.new {name, argv, space}: a terminal in a new tab (a shell, or the program asked for). |
| `apps/desktop/win98_chrome.inl:117` | `void new_space(App::State& s, const std::string& name, const std::string& machine) {` | herdr space.new {name, machine}. |
| `apps/desktop/win98_chrome.inl:162` | `Dialog close_tab_ask(App::State& s, const ui::Tab& tab, int index) {` | taskbar.js 519-541. |
| `apps/desktop/win98_chrome.inl:189` | `std::vector<Task> tasks(App::State& s) {` | dialog.js 247-251: every terminal that has not finished. |
| `apps/desktop/win98_chrome.inl:210` | `void set_desktop(App::State& s, const std::string& c) {` | dialog.js 919-925: the desktop's color or the clouds picture. |
| `apps/desktop/win98_chrome.inl:215` | `std::string pane_title_98(App::State& s, const std::string& id) {` | window.js 9-17: a pane's title. |
| `apps/desktop/win98_chrome.inl:221` | `const ui::Program prog = program_of(v);` | window.js 12-13: no program, or a shell -> "DOS Prompt - <dos path>". |
| `apps/desktop/win98_chrome.inl:308` | `float pane_border(App::State& s) {` | kit.js 464-473 (fitLayout): the pane header 18 and the border 3, in Win98 px. |
| `apps/desktop/win98_chrome.inl:319` | `` | ---------------------------------------------------------------- the Start menu (taskbar.js 197-264) |
| `apps/desktop/win98_chrome.inl:321` | `struct Agent98 {` | taskbar.js 15-27: the agents the Start menu offers when installed (here: a host profile). |
| `apps/desktop/win98_chrome.inl:428` | `void toggle_start(App::State& s, std::optional<Box> bounds) {` | taskbar.js 266-274. |
| `apps/desktop/win98_chrome.inl:440` | `` | ---------------------------------------------------------------- commands (taskbar.js 126-176) |
| `apps/desktop/win98_chrome.inl:507` | `void boot(App::State& s, bool force) {` | taskbar.js 109-120: coming into Herdr 98: the boot screen and the welcome (at most every half |
| `apps/desktop/win98_chrome.inl:527` | `long long app_age_seconds() {` | Seconds since agent-ui started (taskbar.js 48-59 asks ps for the app's age). |
| `apps/desktop/win98_chrome.inl:540` | `` | ---------------------------------------------------------------- the taskbar (taskbar.js 276-516) |
| `apps/desktop/win98_chrome.inl:667` | `void quick(App::State& s, ImDrawList* dl, const char* id, Box b,` | taskbar.js 370-398: Quick Launch: flat until hovered, then a 1 px raised bump. |
| `apps/desktop/win98_chrome.inl:812` | `bevel(s, dl, box(ImVec2(x, cy - U(s, 10)), ImVec2(3 * o, U(s, 20))), Bevel::kBump);` | kit.js 184-186: the grip, a 1 px bump, 20 tall. |
| `apps/desktop/win98_chrome.inl:1145` | `MenuItems mines_menu(App::State& s, bool help) {` | mines.js 125-136. |
| `apps/desktop/win98_chrome.inl:1188` | `Box win_frame(App::State& s, ImDrawList* dl, ImVec2 at, float width, float height) {` | kit.js 295-308 win(): the frame, the title bar, the menu bar, the body. Returns the inside box. |
| `apps/desktop/win98_chrome.inl:1193` | `void counter(App::State& s, ImDrawList* dl, ImVec2 at, int value) {` | mines.js 15-22. |
| `apps/desktop/win98_chrome.inl:1437` | `const Json& jd = shared(s, "win98.sol.draw");` | sol.js 171-175: the options from shared; the clock runs while the window is open. |
| `apps/desktop/win98_chrome.inl:1596` | `const Pressed tp = area("##sol-table", table);` | The pointer on the table (sol.js 184-199, 201-207). |
| `apps/desktop/win98_chrome.inl:1663` | `ImVec2 render_sol_dialog(App::State& s, Window& win, ImDrawList* dl, ImVec2 at) {` | sol.js 145-165: Options and Select Card Back (a dialog over the window). |
| `apps/desktop/win98_chrome.inl:1753` | `std::optional<News> clippy_news(App::State& s) {` | clippy.js 457-463: the agent that needs you (blocked), else the latest that finished, while you |
| `apps/desktop/win98_chrome.inl:1802` | `void clippy(App::State& s, Box owner, ui::layout::Float& f) {` | owner: the node the layout floats it over; f: its floating block there (layout.rs 61-77). |
| `apps/desktop/win98_chrome.inl:1840` | `const float m = px(ui::layout::kFloatMargin);` | win98.json 70-80: where the layout's floating block says (the left edge of the screen row, at |
| `apps/desktop/win98_chrome.inl:1842` | `const float m = px(ui::layout::kFloatMargin);` | nearest edge and third (layout.rs 732-751), 12 px in (floats.rs 14, 78-96). |
| `apps/desktop/win98_chrome.inl:1853` | `const bool right = place.edge != k98::Edge::kLeft;` | clippy.js 469 |
| `apps/desktop/win98_chrome.inl:1860` | `const float ms = std::chrono::duration<float, std::milli>(Clock::now() - w.clippy.spring_s` | floats.rs 212-215: left = 1 - spring(elapsed / 360 ms), the body offset by from * left. |
| `apps/desktop/win98_chrome.inl:1868` | `s.config.wake();` | floats.rs 326: frames while anything moves |
| `apps/desktop/win98_chrome.inl:1931` | `if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !g_area_ho` | floats.rs 253-300: a press on it that no control takes drags it (from 4 px); dropped, it |
| `apps/desktop/win98_chrome.inl:1941` | `if (!w.clippy.moving && std::max(std::abs(mouse.x - w.clippy.down.x),` | floats.rs 261-262: the larger of the two distances, from 4 px. |
| `apps/desktop/win98_chrome.inl:1952` | `save_layout_state(s.layout());` | floats.rs 288-297 + layout_sync.rs FloatState |
| `apps/desktop/win98_chrome.inl:1953` | `const auto next = k98::float_home(owner.x0, owner.y0, owner.w(), owner.h(), cw, ch, to, m)` | floats.rs 288-297: spring from where it was let go to its new home. |
| `apps/desktop/win98_chrome.inl:1968` | `void sync_games(App::State& s) {` | taskbar.js 181-193: one window per open (or minimized) game. |
| `apps/desktop/win98_chrome.inl:2020` | `w.sol->animating = false;` | sol.js 174: stop() |
| `apps/desktop/win98_chrome.inl:2033` | `const std::string dim = name + "dim";` | herdr dialog overlays dim the window and keep clicks beside them. |
| `apps/desktop/win98_chrome.inl:2135` | `void preview(App::State& s, ImDrawList* dl, ImVec2 a, ImVec2 size, float rounding, ImU32 b` | settings/main.js 302-309: the layout's preview picture (win98.json "preview": |
| `apps/desktop/win98_chrome.inl:2154` | `void background(App::State& s, ImDrawList* dl, Box b) {` | covering it (dialog.js 919-925). |
| `apps/desktop/win98_chrome.inl:2179` | `w.was_shown = true;` | taskbar.js 68-107: a new taskbar: the Start menu closed, the boot (always when the app |
| `apps/desktop/win98_chrome.inl:2191` | `void fit(App::State& s, ui::layout::Node& root) {` | kit.js 441-479 fitLayout: the bars in Win98 pixels (n * scale / zoom layout px, which the zoom |
| `apps/desktop/win98_chrome.inl:2192` | `void fit(App::State& s, ui::layout::Node& root) {` | makes n * scale screen px), whole on screen as before. Clicks, not edits (layout_sync.rs 324). |
| `apps/desktop/win98_chrome.inl:2232` | `void hidden(App::State& s) {` | A layout switch closes windows and menus (herdr.d.ts 405). |
| `apps/desktop/win98_dialogs.inl:1` | `` | Herdr 98, part 2: the dialogs (herdr-app defaults/mods/win98/dialog.js, commit 321c9a8). |
| `apps/desktop/win98_dialogs.inl:24` | `std::string kind_title(const std::string& kind) {` | dialog.js 26-38. |
| `apps/desktop/win98_dialogs.inl:41` | `struct Btn {` | dialog.js 263-273: 75 x 23 buttons in a row, gap 6, at the end or in the middle. Returns the |
| `apps/desktop/win98_dialogs.inl:72` | `void input(App::State& s, ImDrawList* dl, Box b, const Dialog& d) {` | dialog.js 275-285: the text box: the text (selected while fresh) and a blinking caret. |
| `apps/desktop/win98_dialogs.inl:92` | `bool radio(App::State& s, ImDrawList* dl, const char* id, float x, float y, const std::str` | dialog.js 287-299: a radio button and its label. |
| `apps/desktop/win98_dialogs.inl:103` | `bool check(App::State& s, ImDrawList* dl, const char* id, float x, float y, const std::str` | dialog.js 301-316: a check box (a 13 px field) and its label. |
| `apps/desktop/win98_dialogs.inl:121` | `void pick_machine(App::State& s, Dialog& d, int dir) {` | dialog.js 185-188. |
| `apps/desktop/win98_dialogs.inl:132` | `void run_it(App::State& s, Window& w, Dialog& d) {` | dialog.js 221-229: a command line in a new tab, then the shell stays (herdr: sh -c "<cmd>; |
| `apps/desktop/win98_dialogs.inl:159` | `void shutdown_choice(App::State& s, Window& w, Dialog& d) {` | dialog.js 231-245. |
| `apps/desktop/win98_dialogs.inl:181` | `void end_task(App::State& s, Dialog& d) {` | dialog.js 253-259. |
| `apps/desktop/win98_dialogs.inl:191` | `void ok(App::State& s, Window& w, Dialog& d) {` | dialog.js 190-219. |
| `apps/desktop/win98_dialogs.inl:228` | `void dialog_keys(App::State& s, Window& w, Dialog& d) {` | dialog.js 154-183: the keys of the dialog that has the keyboard. |
| `apps/desktop/win98_dialogs.inl:329` | `float render_prompt(App::State& s, Window& w, Dialog& d, ImDrawList* dl, Box b) {` | dialog.js 333-360. |
| `apps/desktop/win98_dialogs.inl:371` | `float render_confirm(App::State& s, Window& w, Dialog& d, ImDrawList* dl, Box b) {` | dialog.js 362-386 (confirm and message). |
| `apps/desktop/win98_dialogs.inl:397` | `float render_run(App::State& s, Window& w, Dialog& d, ImDrawList* dl, Box b) {` | dialog.js 388-423. |
| `apps/desktop/win98_dialogs.inl:444` | `float render_shutdown(App::State& s, Window& w, Dialog& d, ImDrawList* dl, Box b) {` | dialog.js 425-449. |
| `apps/desktop/win98_dialogs.inl:473` | `float render_logoff(App::State& s, Window& w, Dialog& d, ImDrawList* dl, Box b) {` | dialog.js 451-457. |
| `apps/desktop/win98_dialogs.inl:489` | `float render_about(App::State& s, Window& w, Dialog& /*d*/, ImDrawList* dl, Box b) {` | dialog.js 459-493. |
| `apps/desktop/win98_dialogs.inl:529` | `float render_taskman(App::State& s, Window& w, Dialog& d, ImDrawList* dl, Box b) {` | dialog.js 495-542. |
| `apps/desktop/win98_dialogs.inl:592` | `float render_display(App::State& s, Window& w, Dialog& d, ImDrawList* dl, Box b) {` | dialog.js 544-653. |
| `apps/desktop/win98_dialogs.inl:725` | `float render_datetime(App::State& s, Window& w, Dialog& /*d*/, ImDrawList* dl, Box b) {` | dialog.js 655-709 and 927-958. |
| `apps/desktop/win98_dialogs.inl:796` | `struct ExItem {` | dialog.js 711-755: an Explorer-ish window: icons in a 200 px field, a status bar. |
| `apps/desktop/win98_dialogs.inl:864` | `float render_mycomputer(App::State& s, Window& w, Dialog& d, ImDrawList* dl, Box b) {` | dialog.js 757-767. |
| `apps/desktop/win98_dialogs.inl:888` | `float render_network(App::State& s, Window& w, Dialog& d, ImDrawList* dl, Box b) {` | dialog.js 769-780. |
| `apps/desktop/win98_dialogs.inl:911` | `float render_bin(App::State& s, Window& w, Dialog& d, ImDrawList* dl, Box b) {` | dialog.js 871-876. |
| `apps/desktop/win98_dialogs.inl:920` | `float render_welcome(App::State& s, Window& w, Dialog& d, ImDrawList* dl, Box b) {` | dialog.js 782-869. |
| `apps/desktop/win98_dialogs.inl:1043` | `void screen_timers(App::State& s, Window& w, Dialog& d) {` | dialog.js 81-150: the full-window boot, shut down and restart screens. |
| `apps/desktop/win98_dialogs.inl:1098` | `ImVec2 render_dialog(App::State& s, Window& w, ImDrawList* dl, ImVec2 at) {` | dialog.js 878-916: a dialog's frame (kit.js win: title bar, help unless Explorer-ish, close) and |
| `apps/desktop/win98_kit.inl:1` | `` | Herdr 98, part 1: the drawing kit, the shared values, the popup menus and the overlay windows. |
| `apps/desktop/win98_kit.inl:2` | `` | Ported from herdr-app defaults/mods/win98 (commit 321c9a8, Apache-2.0): kit.js, menu.js; herdr's |
| `apps/desktop/win98_kit.inl:3` | `` | overlays (herdr.d.ts 395-425). Included by app.cpp inside its anonymous namespace, after |
| `apps/desktop/win98_kit.inl:6` | `` | Pixels: kit.js 4-6 draws in whole Win98 pixels, u(n) = n * scale (Small Fonts 1, Large 2) at |
| `apps/desktop/win98_kit.inl:7` | `` | the display's scale; herdr's zoom does not change them. Layout px (padding, gaps) are px(). |
| `apps/desktop/win98_kit.inl:70` | `` | ---------------------------------------------------------------- shared (herdr shared(), persisted) |
| `apps/desktop/win98_kit.inl:109` | `int scale(App::State& s) {` | kit.js 40-43. |
| `apps/desktop/win98_kit.inl:115` | `float K(App::State& s) {` | kit.js 45-51: one Win98 pixel on screen. |
| `apps/desktop/win98_kit.inl:133` | `std::string app_state(App::State& s, const std::string& name) {` | kit.js 417-427: which game windows are open: {mines: "open" \| "min", sol: ...}. |
| `apps/desktop/win98_kit.inl:150` | `` | ---------------------------------------------------------------- sounds (kit.js 404-413) |
| `apps/desktop/win98_kit.inl:164` | `void sfx(App::State& s, const std::string& name) {` | Plays a Herdr 98 sound unless sounds are off (settings, or the speaker in the tray). |
| `apps/desktop/win98_kit.inl:193` | `if (const auto* f = find_family("Arial"))` | mines.js 157: the numbers in Arial bold. |
| `apps/desktop/win98_kit.inl:207` | `float terminal_font_size(App::State& s) {` | kit.js 451-452, at Herdr's zoom (the terminal font size at zoom 1). |
| `apps/desktop/win98_kit.inl:217` | `` | ---------------------------------------------------------------- text (kit.js 55-80) |
| `apps/desktop/win98_kit.inl:234` | `float label(App::State& s, ImDrawList* dl, float x, float top, const std::string& text, Im` | noAccess (kit.js 56-71). Returns its width. |
| `apps/desktop/win98_kit.inl:280` | `std::vector<std::string> wrap(App::State& s, const std::string& text, float width, bool bo` | The text's lines (dialog.js 40-44: one per "\n", an empty one keeps its height), wrapped at |
| `apps/desktop/win98_kit.inl:345` | `` | ---------------------------------------------------------------- bevels (kit.js 82-137) |
| `apps/desktop/win98_kit.inl:384` | `void etch(App::State& s, ImDrawList* dl, float x, float y, float len, bool vertical) {` | kit.js 176-181: an etched line, 2 px. |
| `apps/desktop/win98_kit.inl:396` | `void gradient(ImDrawList* dl, Box b, ImU32 from, ImU32 to, bool down = false) {` | kit.js 192-195: a left-to-right gradient (angle 90), or top to bottom (180). |
| `apps/desktop/win98_kit.inl:404` | `void dither(App::State& s, ImDrawList* dl, Box b) {` | kit.js 197-200: a 1 px dither (Win98's checked buttons): white on every other Win98 pixel. |
| `apps/desktop/win98_kit.inl:416` | `` | ---------------------------------------------------------------- icons (kit.js 202-225) |
| `apps/desktop/win98_kit.inl:456` | `void brand_header(App::State& s, ImDrawList* dl, Box b) {` | The About box's picture (art/generate.ts 120: white to #a4d2ee, the name at 32). |
| `apps/desktop/win98_kit.inl:475` | `void program_icon(App::State& s, ImDrawList* dl, const ui::Program& p, float size, ImVec2 ` | kit.js 208-225 programIcon: from the app's program table (herdr programs.json, programs_of in |
| `apps/desktop/win98_kit.inl:493` | `` | ---------------------------------------------------------------- buttons (kit.js 139-174) |
| `apps/desktop/win98_kit.inl:554` | `if (p.held && !o.toggled && !o.disabled)` | kit.js 118: the face moves 1 px down and right |
| `apps/desktop/win98_kit.inl:562` | `Pressed text_button(App::State& s, ImDrawList* dl, const char* id, Box b, const std::strin` | A push button with a label, centered (kit.js 150-151). |
| `apps/desktop/win98_kit.inl:572` | `bool control(App::State& s, ImDrawList* dl, const char* id, const std::string& kind, ImVec` | kit.js 229-239: a title bar's little button, 16 x 14, its glyph 8 x 7. |
| `apps/desktop/win98_kit.inl:592` | `` | ---------------------------------------------------------------- title bars (kit.js 241-289) |
| `apps/desktop/win98_kit.inl:646` | `Box field(App::State& s, ImDrawList* dl, Box b, float pad_y = 2, float pad_x = 3, ImU32 bg` | kit.js 310-318: a sunken white field and a status bar panel. |
| `apps/desktop/win98_kit.inl:655` | `` | ---------------------------------------------------------------- menus (menu.js, kit.js 322-371) |
| `apps/desktop/win98_kit.inl:664` | `int open_menu(App::State& s, MenuItems items, std::optional<ImVec2> at, bool start = false` | kit.js 356-371: opens a popup menu at `at` (the pointer when not given), or above a box (the |
| `apps/desktop/win98_kit.inl:690` | `if (m.parents.empty())` | menu.js 237: a top menu starts the list over. |
| `apps/desktop/win98_kit.inl:704` | `void close_tree(App::State& s, int id) {` | menu.js 216-223: closes a menu and every menu opened from it. |
| `apps/desktop/win98_kit.inl:721` | `void close_all_menus(App::State& s) {` | menu.js 261-268: a choice closes the whole chain. |
| `apps/desktop/win98_kit.inl:731` | `void open_sub(App::State& s, int menu_id, int i, std::optional<Box> row) {` | menu.js 277-290: a submenu to the right of its row (at the pointer when opened by a key). |
| `apps/desktop/win98_kit.inl:760` | `void choose(App::State& s, const MenuItem& it) {` | menu.js 270-275. |
| `apps/desktop/win98_kit.inl:770` | `void escape_menu(App::State& s, int menu_id) {` | menu.js 245-251: Escape (and Left) closes this menu and what hangs off it. |
| `apps/desktop/win98_kit.inl:781` | `void move_menu(Menu& m, int d) {` | menu.js 292-301: the next row that is not a separator. |
| `apps/desktop/win98_kit.inl:795` | `void menu_keys(App::State& s, int menu_id) {` | menu.js 303-336: the keys of the menu opened last. |
| `apps/desktop/win98_kit.inl:841` | `void menu_row(App::State& s, ImDrawList* dl, int menu_id, int i, Box r, bool start) {` | menu.js 338-386: one row. Start menu rows are 34 px with 32 px icons. |
| `apps/desktop/win98_kit.inl:914` | `void raise(const std::string& name) {` | Puts a Herdr 98 window above the others, unless one of AgentDesk's own dialogs (an ImGui popup: |
| `apps/desktop/win98_kit.inl:922` | `void keyboard_back(App::State& s) {` | themselves, dialog.js 50-51), else back to the terminal. |
| `apps/desktop/win98_kit.inl:937` | `void draw_menus(App::State& s) {` | menu.js 388-426: every open menu, the last one opened on top with the keyboard. |
| `apps/desktop/win98_kit.inl:990` | `if (id == last && w.menu_focus != id) {` | menu.js 229-230: a menu takes the keyboard when it opens. |
| `apps/desktop/win98_kit.inl:1078` | `void open_dialog(App::State& s, Dialog d) {` | kit.js 375-386: a dialog, a window dragged by its title, its width by kind. |
| `apps/desktop/win98_kit.inl:1087` | `if (d.value)` | dialog.js 52-64: the text box starts with the value (Run: the last line run); the sounds. |
| `apps/desktop/win98_kit.inl:1099` | `if (d.kind == "screen" && d.mode == "boot") {` | dialog.js 86-88: straight to "Starting" |
| `apps/desktop/win98_state.hpp:2` | `` | Herdr 98's state in agent-ui (herdr-app defaults/mods/win98, commit 321c9a8): the open menus, |
| `apps/desktop/win98_state.hpp:3` | `` | dialogs and game windows that herdr opens as overlays, the shared values the mod keeps |
| `apps/desktop/win98_state.hpp:4` | `` | (herdr's shared(), persisted: <data>\win98.json), and the games. Drawn by win98.inl. |
| `apps/desktop/win98_state.hpp:23` | `struct MenuItem {` | kit.js 350-355: a menu item. `act` is what perform() does (kit.js 388-400). |
| `apps/desktop/win98_state.hpp:72` | `std::string input;` | state (dialog.js 47-75) |
| `apps/desktop/win98_state.hpp:84` | `struct Window {` | An overlay window: a dialog, a game or the Solitaire options (herdr openOverlay kind "window", |
| `apps/desktop/win98_state.hpp:106` | `bool pressing = false;` | A drag of the float (herdr floats.rs 253-300): where it was pressed, where in it, moving. |
| `apps/desktop/win98_state.hpp:111` | `std::optional<ImVec2> spring_from;` | floats.rs 64-69 FloatAnim: its offset from home when dropped, easing to zero. |
| `apps/desktop/win98_state.hpp:128` | `std::string sol_menu;` | sol.js 180: the Solitaire menu opened last |
| `apps/desktop/win98_state.hpp:154` | `ImFont* arial = nullptr;` | Mines numbers (mines.js 157: Arial bold) |
| `apps/host/main.cpp:84` | `if (version) {` | Like agent-ui --version (herdr update.rs 392-400): on the stdout it was given. |
| `libs/host/include/agentdesk/host/git_tools.hpp:15` | `std::string error;` | stderr, kept apart for messages (herdr worktree/main.js git()) |
| `libs/host/include/agentdesk/host/git_tools.hpp:20` | `inline constexpr std::size_t kMaxToolOutput = 64u * 1024u * 1024u;` | Most bytes kept of each stream: no cap in practice (herdr reads all), but memory stays bounded. |
| `libs/host/include/agentdesk/host/git_tools.hpp:31` | `std::string worktree_slug(const std::string& branch);` | A branch name as a folder name (herdr worktree/main.js slug): runs of anything but letters, |
| `libs/host/include/agentdesk/host/session.hpp:44` | `std::string app;` | the 7501 report's `app` (herdr shows a terminal by it first), or empty |
| `libs/host/include/agentdesk/host/session.hpp:101` | `struct Frozen {` | ---- agent-host's self-update (design doc 7.4 OQ-09; herdr src/update.rs 236, docs/ |
| `libs/host/include/agentdesk/host/session_manager.hpp:32` | `std::vector<std::string> succeeds;` | ---- self-update (design doc 7.4 OQ-09; herdr src/update.rs 236-262, `hc update`) |
| `libs/host/include/agentdesk/host/session_manager.hpp:162` | `nlohmann::json ui_state_ = nlohmann::json::object();` | watching client gets "ui.state" when it changes (herdr keeps its layout in the core too). |
| `libs/host/src/git_tools.cpp:109` | `std::string out;` | Port of herdr defaults/mods/worktree/main.js slug(): /[^\p{L}\p{M}\p{N}._-]+/ -> "-". |
| `libs/host/src/handover.cpp:3` | `` | herdr: "hc update" replaces herdr-core and "terminals keep running; the app reconnects when the |
| `libs/host/src/handover.cpp:4` | `` | core restarts" (herdr-app src/update.rs 235, docs/commands.md 547, src/hc.rs 26-28: "cores |
| `libs/host/src/handover.cpp:5` | `` | restart and their programs survive"). herdr-core's own mechanism is not in herdr-app's |
| `libs/host/src/handover.cpp:333` | `return protocol::ok(req.id, {{"now", "agent-host " + version}, {"host_instance_id", instan` | herdr core_bin.rs 256: `hc update --json` says {"now": ...}. |
| `libs/host/src/session_manager.cpp:120` | `void add_folder_hook(platform::win::PtySpawnSpec& spec) {` | what the host reads to follow the shell. Like herdr, wrap the prompt: after the person's own |
| `libs/host/src/session_manager.cpp:223` | `constexpr auto kWorktreeTimeout = std::chrono::seconds(600);` | herdr defaults/mods/worktree/main.js:205-207, 243: worktree add/remove may take minutes. |
| `libs/host/src/session_manager.cpp:529` | `namespace {` | ---- Files / Changes for a client on another machine (herdr mods/files, mods/changes). The |
| `libs/host/src/session_manager.cpp:624` | `const bool long_running = args.size() > 8 && args[8] == L"worktree";` | worktree remove may take minutes (herdr worktree/main.js:243). |
| `libs/host/src/session_manager.cpp:660` | `const auto known = run_tool({L"git", L"-C", repo_path.native(), L"show-ref", L"--verify", ` | herdr defaults/mods/worktree/main.js create(): a branch that exists is checked out, a new |
| `libs/host/src/session_manager.cpp:922` | `rec.name = p.value("name", std::string());` | herdr: a terminal has a name only when someone gives it one (hc run --name, pane.rename); |
| `libs/platform/windows/include/agentdesk/platform/win/process_tree.hpp:11` | `struct Foreground {` | follows herdr's rule: when the root is a shell, the front is the shell's newest direct child |
| `libs/platform/windows/include/agentdesk/platform/win/process_tree.hpp:17` | `std::vector<std::string> argv;` | That process's command line as words (CommandLineToArgvW); empty if unreadable. herdr |
| `libs/platform/windows/include/agentdesk/platform/win/process_tree.hpp:18` | `std::vector<std::string> argv;` | resolves a terminal's program from its argv (programs.rs 270-327). |
| `libs/platform/windows/src/detached.cpp:28` | `std::filesystem::path cwd = exe.parent_path();` | directory, so an update could never be put in place (herdr update.rs 443-457 leaves the |
| `libs/terminal/include/agentdesk/terminal/vt_engine.hpp:45` | `std::int8_t fg_ansi = -1;` | One of the 16 ANSI colours (0-15), else -1: a theme's own table may draw it (herdr themes |
| `libs/terminal/include/agentdesk/terminal/vt_engine.hpp:86` | `std::vector<std::uint8_t> wrapped;` | joins such rows without a line break (herdr vt.rs selection_text, unwrap). May be empty |
| `libs/terminal/include/agentdesk/terminal/vt_engine.hpp:190` | `[[nodiscard]] bool win32_input() const noexcept { return win32_on_; }` | Escape are then sent as win32 key events (herdr src/win32_keys.rs). |
| `libs/terminal/include/agentdesk/terminal/vt_engine.hpp:212` | `[[nodiscard]] bool at_handover_point() const;` | agent-host's self-update (design doc 7.4 OQ-09; herdr update.rs 236: "terminals keep |
| `libs/terminal/src/vt_engine.cpp:353` | `void VtEngine::watch_win32_mode(std::string_view bytes) {` | Port of herdr src/win32_keys.rs (Win32Input::watch, latest): libvterm ignores DEC mode 9001, |
| `libs/terminal/src/vt_engine.cpp:380` | `std::string win32_key(unsigned vk, unsigned scan, unsigned typed, std::uint8_t mods) {` | herdr src/win32_keys.rs:44-55: a key pressed and released as win32-input-mode events, |
| `libs/terminal/src/vt_engine.cpp:396` | `if (win32_on_) {` | herdr src/terminal.rs:1645-1652: with win32-input-mode on, modified Enter and Escape go as |
| `libs/terminal/src/vt_engine.cpp:450` | `std::string cr;` | herdr src/vt.rs:1631 (paste_bytes): without bracketed paste, line breaks are Enter (CR). |
| `libs/ui/CMakeLists.txt:33` | `set(_herdr_layouts "${PROJECT_SOURCE_DIR}/third_party/herdr/layouts")` | herdr's shipped layouts (third_party/herdr/layouts, verbatim), compiled in as text |
| `libs/ui/CMakeLists.txt:34` | `set(_herdr_layouts "${PROJECT_SOURCE_DIR}/third_party/herdr/layouts")` | (layout::builtin_text; herdr window.rs 160-166 include_str!). |
| `libs/ui/include/agentdesk/ui/chrome_logic.hpp:2` | `` | Rules behind herdr's chrome overlays (context menus, the "Go to" picker), kept apart from the |
| `libs/ui/include/agentdesk/ui/chrome_logic.hpp:3` | `` | drawing so they are unit-tested. Ported from herdr-app (Apache-2.0, commit 321c9a8). |
| `libs/ui/include/agentdesk/ui/chrome_logic.hpp:11` | `class MenuNav {` | Keyboard in a context menu (herdr defaults/mods/menu/main.js 131-137): the first row starts |
| `libs/ui/include/agentdesk/ui/chrome_logic.hpp:32` | `struct PickerSpace {` | One space as the picker sees it (herdr defaults/mods/picker/main.js 77-98). |
| `libs/ui/include/agentdesk/ui/chrome_logic.hpp:34` | `bool status = false;` | any terminal in it has a status, idle included (herdr p.w.status) |
| `libs/ui/include/agentdesk/ui/chrome_logic.hpp:46` | `PickerPlan plan_picker(const std::vector<PickerSpace>& spaces, bool filtering, bool show_e` | herdr picker/main.js 86-98: busy = has a status and tabs left; quiet = no status and tabs left; |
| `libs/ui/include/agentdesk/ui/chrome_logic.hpp:51` | `PaneLoc find_tab(const Workspace& ws, const std::string& tab_id);` | The space and index of the tab with id `tab_id` (herdr closes and renames tabs by id). |
| `libs/ui/include/agentdesk/ui/clipboard.hpp:3` | `` | What the Windows clipboard holds, for the terminal's paste (herdr src/terminal.rs:788-815). |
| `libs/ui/include/agentdesk/ui/fonts.hpp:15` | `ImFont* ui_medium = nullptr;` | labels, tab names (herdr "medium") |
| `libs/ui/include/agentdesk/ui/fonts.hpp:16` | `ImFont* ui_bold = nullptr;` | headings, names (herdr "semibold") |
| `libs/ui/include/agentdesk/ui/fonts.hpp:25` | `FontSet load_fonts(float mono_size_px, float ui_size_px);` | Loads the bundled fonts from `fonts\` next to the executable (herdr's families: Geist for the |
| `libs/ui/include/agentdesk/ui/fonts.hpp:37` | `struct FontFamily {` | Installed font families (herdr lists every family in its font pickers): the per-user and |
| `libs/ui/include/agentdesk/ui/fonts.hpp:84` | `inline constexpr const char* kBot = "\xEE\xAC\x88";` | U+EB08 codicon hubot (herdr lucide "bot") |
| `libs/ui/include/agentdesk/ui/fonts.hpp:91` | `inline constexpr const char* kEdit = "\xEE\xA9\xB3";` | U+EA73 codicon edit (herdr lucide square-pen) |
| `libs/ui/include/agentdesk/ui/fonts.hpp:93` | `inline constexpr const char* kSquare = "\xEE\xA9\xB2";` | U+EA72 codicon primitive-square (herdr square) |
| `libs/ui/include/agentdesk/ui/host_client.hpp:37` | `std::vector<std::string> program_argv;` | its command line as words (herdr resolves by it) |
| `libs/ui/include/agentdesk/ui/image_cache.hpp:3` | `` | decoded once (Windows Imaging Component). Herdr 98 draws its icons, cards and pictures this way |
| `libs/ui/include/agentdesk/ui/image_cache.hpp:4` | `` | (herdr renders the same files with its own SVG and image support). The textures go through |
| `libs/ui/include/agentdesk/ui/imgui_adapter.hpp:35` | `void bring_to_front(const char* window_name);` | Puts a window above the others in drawing order (Herdr 98: the window pressed last is on top). |
| `libs/ui/include/agentdesk/ui/input_mapping.hpp:69` | `std::string selection_text(const std::vector<LineCells>& lines, CellPos a, CellPos b);` | (CellPos::row indexes `lines`). As herdr copies (src/vt.rs:1158-1163, Ghostty's formatter with |
| `libs/ui/include/agentdesk/ui/input_mapping.hpp:77` | `int selection_scroll_lines(float y, float cell_h, int rows);` | Lines to scroll while a selection drag is held past the edge (herdr src/terminal.rs:1280-1288): |
| `libs/ui/include/agentdesk/ui/input_mapping.hpp:82` | `std::optional<std::string> quote_paths(const std::vector<std::string>& paths);` | trailing space, never Enter (herdr src/terminal/drag_paths.rs quote_paths, Windows branch). |
| `libs/ui/include/agentdesk/ui/keymap.hpp:2` | `` | Key chords -> command names, herdr's keys.json (spec-settings-fonts.md 3.1). Defaults below; |
| `libs/ui/include/agentdesk/ui/keymap.hpp:5` | `` | (kept for herdr files; mods do not take focus here). More specific wins: terminal > layout > |
| `libs/ui/include/agentdesk/ui/keymap.hpp:7` | `` | "send:<text>", or "terminal" (the key goes to the program). "" or null unbinds. Old herdr |
| `libs/ui/include/agentdesk/ui/keymap.hpp:24` | `static nlohmann::json default_file();` | The defaults as a keys.json document (written when the file is missing, like herdr). |
| `libs/ui/include/agentdesk/ui/keymap.hpp:28` | `std::vector<std::string> apply(const nlohmann::json& overrides,` | actions); when there are any, nothing from the file is installed (herdr keys.rs: the |
| `libs/ui/include/agentdesk/ui/keymap.hpp:49` | `struct Binding {` | One keys.json entry as written, for the Keybinds list (herdr keys.rs listing()). |
| `libs/ui/include/agentdesk/ui/keymap.hpp:55` | `static std::vector<Binding> listing(const nlohmann::json& file);` | Every binding of a keys.json document, by section then key (herdr's sorted maps); the |
| `libs/ui/include/agentdesk/ui/keymap.hpp:59` | `static std::string help_label(std::string_view action);` | herdr help/main.js 17-22: "send:<text>" -> type "<text>" (JSON-quoted); "a.b_c:x" -> "a b c x". |
| `libs/ui/include/agentdesk/ui/keymap.hpp:61` | `static const char* help_group(std::string_view action);` | herdr help/main.js 8-15: spaces, tabs, panes, agents, terminal (also send:), else window. |
| `libs/ui/include/agentdesk/ui/layout.hpp:2` | `` | herdr-app's layout engine (ADR-15), the parts with no drawing in them: the tree of blocks and |
| `libs/ui/include/agentdesk/ui/layout.hpp:3` | `` | containers and its JSON (herdr src/layout.rs), the sizes the window gives each node |
| `libs/ui/include/agentdesk/ui/layout.hpp:4` | `` | (src/window.rs 4439-4502 render_node and 5184-5209 sized; src/window/pane_size.rs 7-118), a |
| `libs/ui/include/agentdesk/ui/layout.hpp:5` | `` | separator drag (window.rs 4671-4723 drag_to), floating blocks (layout.rs 732-751, |
| `libs/ui/include/agentdesk/ui/layout.hpp:6` | `` | src/window/floats.rs 77-96), the person's clicks kept beside the file (src/layout_sync.rs) and |
| `libs/ui/include/agentdesk/ui/layout.hpp:7` | `` | the shipped layouts (defaults/layouts, window.rs 160-182). agent-ui draws the blocks |
| `libs/ui/include/agentdesk/ui/layout.hpp:25` | `enum class Edge { kTop, kRight, kBottom, kLeft };` | layout.rs 33-59: which side of its node a floating block sits on, and where along it. |
| `libs/ui/include/agentdesk/ui/layout.hpp:34` | `struct Span {` | layout.rs 79-84: {"px": n} or {"weight": n}. |
| `libs/ui/include/agentdesk/ui/layout.hpp:43` | `struct Line {` | layout.rs 86-99: width 1 and the theme's "border" unless said. |
| `libs/ui/include/agentdesk/ui/layout.hpp:50` | `struct Padding {` | layout.rs 132-148: one number, or [vertical, horizontal]. |
| `libs/ui/include/agentdesk/ui/layout.hpp:58` | `struct Header {` | layout.rs 234-256: a stack's or a pane's header. |
| `libs/ui/include/agentdesk/ui/layout.hpp:62` | `std::optional<float> height;` | px, default 30 (window.rs 32) |
| `libs/ui/include/agentdesk/ui/layout.hpp:71` | `struct PaneFrame {` | layout.rs 258-282: how a layout wraps each pane of the tab it shows. |
| `libs/ui/include/agentdesk/ui/layout.hpp:84` | `struct Content {` | layout.rs 9-31: what a block shows. |
| `libs/ui/include/agentdesk/ui/layout.hpp:94` | `struct Float {` | layout.rs 61-77: a block floating over a node, snapped to one of its edges. |
| `libs/ui/include/agentdesk/ui/layout.hpp:108` | `struct Node {` | layout.rs 327-400. Fields kept as JSON are only read by agent-ui or written back as they came |
| `libs/ui/include/agentdesk/ui/layout.hpp:109` | `struct Node {` | (each normalized as herdr's serde types would read and write them). |
| `libs/ui/include/agentdesk/ui/layout.hpp:129` | `Json look;` | layout.rs 150-186, flattened: theme, colors, font, font_size, ... (an object) |
| `libs/ui/include/agentdesk/ui/layout.hpp:151` | `[[nodiscard]] bool is_pane() const;` | layout.rs 489-492: a terminal or a mod block |
| `libs/ui/include/agentdesk/ui/layout.hpp:158` | `void fill_ids(const std::function<std::string()>& fresh);` | layout.rs 502-583: tree walks. |
| `libs/ui/include/agentdesk/ui/layout.hpp:172` | `std::optional<Node> remove(std::string_view id);` | layout.rs 585-670: tree edits. |
| `libs/ui/include/agentdesk/ui/layout.hpp:183` | `std::optional<Node> parse(const Json& j, std::string* error = nullptr);` | Reads a layout as herdr's serde types do: the same keys and defaults; unknown keys are left |
| `libs/ui/include/agentdesk/ui/layout.hpp:188` | `std::string text_of(const Node& n);` | layout_sync.rs 289-313: as the file holds it: pretty, whole numbers without ".0" (keys in |
| `libs/ui/include/agentdesk/ui/layout.hpp:194` | `std::vector<std::string> builtin_names();` | herdr window.rs 160-166: main, terminals and win98 (defaults/layouts, kept verbatim in |
| `libs/ui/include/agentdesk/ui/layout.hpp:195` | `std::vector<std::string> builtin_names();` | third_party/herdr/layouts). AgentDesk names main "workspace", as its settings always have. |
| `libs/ui/include/agentdesk/ui/layout.hpp:199` | `Node builtin(std::string_view name);` | window.rs 180-182: a layout as shipped (main for names herdr doesn't ship). |
| `libs/ui/include/agentdesk/ui/layout.hpp:219` | `const Line* node_border(const Node& n, bool focused);` | pane_size.rs 7-37. |
| `libs/ui/include/agentdesk/ui/layout.hpp:222` | `float header_height(const Header& h);` | window.rs 32: 30 |
| `libs/ui/include/agentdesk/ui/layout.hpp:225` | `std::vector<float> shares(const std::vector<const Node*>& children, float available, float` | pane_size.rs 49-85: the room each child gets along the axis: px sizes first (within min and |
| `libs/ui/include/agentdesk/ui/layout.hpp:227` | `std::vector<float> shares(const std::vector<const Node*>& children, float available, float` | again (flexbox's rules, which herdr's window lays out by). |
| `libs/ui/include/agentdesk/ui/layout.hpp:229` | `std::vector<float> shares(const std::vector<const Node*>& children, float available, float` | whole pixels, its px()); herdr leaves them fractional. |
| `libs/ui/include/agentdesk/ui/layout.hpp:238` | `Rect hit;` | the drag handle: HANDLE (6 px) or the box, centered (window.rs 4655-4659) |
| `libs/ui/include/agentdesk/ui/layout.hpp:261` | `bool drag(Node& root, std::string_view container, int index, Axis axis, const Placement& p` | window.rs 4671-4723 drag_to: the separator `index` of `container` dragged to `pointer` (screen |
| `libs/ui/include/agentdesk/ui/layout.hpp:267` | `std::pair<Edge, Align> snap(float x, float y, float w, float h);` | layout.rs 732-751: dropped with its center at (x, y) in its node (w x h): the nearest edge |
| `libs/ui/include/agentdesk/ui/layout.hpp:270` | `std::array<float, 2> float_home(Rect owner, float bw, float bh, Edge edge, Align align, fl` | floats.rs 77-96: its top left in `owner` for a block of bw x bh, `margin` in from the edges |
| `libs/ui/include/agentdesk/ui/layout.hpp:271` | `std::array<float, 2> float_home(Rect owner, float bw, float bh, Edge edge, Align align, fl` | (floats.rs 14: 12 px at zoom 1). |
| `libs/ui/include/agentdesk/ui/layout.hpp:325` | `Node without_clicks(const Node& file, const Node& now);` | layout_sync.rs 260-287: `now` with the click fields put back as `file` has them. |
| `libs/ui/include/agentdesk/ui/links.hpp:4` | `` | system. Ported from herdr src/links.rs (Apache-2.0), which uses Ghostty's link pattern; the |
| `libs/ui/include/agentdesk/ui/links.hpp:16` | `std::optional<std::pair<std::size_t, std::size_t>> find_link(const std::string& text, std:` | Byte range [first, second) of the link in `text` covering byte `at` (herdr links::find). |
| `libs/ui/include/agentdesk/ui/links.hpp:25` | `std::optional<LinkTarget> resolve_link(const std::string& link, const std::string& cwd);` | What a link opens (herdr links::resolve). File paths count only when they exist here; relative |
| `libs/ui/include/agentdesk/ui/links.hpp:30` | `void open_link(const LinkTarget& target);` | Hands the target to the system's default handler on a worker thread (herdr links::open, |
| `libs/ui/include/agentdesk/ui/programs.hpp:2` | `` | What runs in a terminal, as people see it: a name, an icon and a tile (herdr src/programs.rs, |
| `libs/ui/include/agentdesk/ui/programs.hpp:3` | `` | ported). One table: herdr's defaults/programs.json (shipped next to agent-ui.exe in programs\), |
| `libs/ui/include/agentdesk/ui/programs.hpp:14` | `struct ProgramEntry {` | herdr programs.rs Entry. |
| `libs/ui/include/agentdesk/ui/programs.hpp:25` | `struct Program {` | herdr programs.rs shown(): { key, name, icon, tint, tile, color, shell, agent }. |
| `libs/ui/include/agentdesk/ui/programs.hpp:40` | `static Programs parse(std::string_view json);` | The built-in table from JSON text (herdr defaults/programs.json). |
| `libs/ui/include/agentdesk/ui/programs.hpp:42` | `static Programs load(const std::filesystem::path& builtin, const std::filesystem::path& mi` | The built-in table from `builtin`, the person's `mine` on top (herdr programs.rs load()). |
| `libs/ui/include/agentdesk/ui/programs.hpp:48` | `[[nodiscard]] Program of(std::string_view app, const std::vector<std::string>& argv) const` | herdr programs.rs of(): from the agent's own report (`app`), else the program it runs. |
| `libs/ui/include/agentdesk/ui/programs.hpp:50` | `[[nodiscard]] std::string name(std::string_view app) const;` | herdr programs.rs name(): how the app calls an agent, from its 7501 `app`. |
| `libs/ui/include/agentdesk/ui/programs.hpp:52` | `[[nodiscard]] std::vector<Program> startable() const;` | herdr programs.rs startable(): every entry with `run`, the most picked first. |
| `libs/ui/include/agentdesk/ui/programs.hpp:70` | `std::string powershell_line(const std::vector<std::string>& words);` | herdr programs.rs powershell_line(): `& 'prog' 'arg 1'`, each word quoted for -Command. |
| `libs/ui/include/agentdesk/ui/sprites.hpp:7` | `` | Ported from herdr-app src/sprites.rs (Apache-2.0, third_party/herdr/LICENSE). The geometry is |
| `libs/ui/include/agentdesk/ui/sprites.hpp:52` | `std::optional<std::array<std::uint8_t, 4>> lines(char32_t c);` | Per direction (up, right, down, left): 0 none, 1 light, 2 heavy, 3 double. herdr sprites.rs:8 |
| `libs/ui/include/agentdesk/ui/sprites.hpp:56` | `std::optional<std::array<float, 2>> arc(char32_t c);` | up). herdr sprites.rs:39 |
| `libs/ui/include/agentdesk/ui/sprites.hpp:59` | `struct Dashes {` | Dashed lines: (dashes per cell, heavy, horizontal). herdr sprites.rs:50 |
| `libs/ui/include/agentdesk/ui/sprites.hpp:68` | `struct Blocks {` | Rectangles in cell fractions (x0, y0, x1, y1) and an alpha, for block elements. herdr |
| `libs/ui/include/agentdesk/ui/sprites.hpp:76` | `std::optional<std::vector<std::array<int, 4>>> braille(char32_t c, int width, int height);` | Braille dots in whole pixels (x0, y0, x1, y1) for a width × height cell. herdr sprites.rs:112 |
| `libs/ui/include/agentdesk/ui/sprites.hpp:79` | `inline constexpr char32_t kRecord = U'⏺';` | ⏺ (Claude Code marks every step with it). herdr sprites.rs:163 |
| `libs/ui/include/agentdesk/ui/sprites.hpp:82` | `bool is_sprite(char32_t c);` | True when the cell's text is exactly one sprite character. herdr sprites.rs:165 |
| `libs/ui/include/agentdesk/ui/sprites.hpp:87` | `Shapes paint(char32_t c, float x, float y, float w, float h, float scale);` | pixels at `scale` device pixels per unit. Empty when `c` is not a sprite. herdr sprites.rs:174 |
| `libs/ui/include/agentdesk/ui/telemetry.hpp:3` | `` | Anonymous usage data, ported from herdr-app src/telemetry.rs (Apache-2.0, commit 321c9a8): |
| `libs/ui/include/agentdesk/ui/telemetry.hpp:11` | `` | never waits on anything, and every error just loses data (telemetry.rs 1-10). |
| `libs/ui/include/agentdesk/ui/telemetry.hpp:14` | `` | DO_NOT_TRACK, AGENTDESK_TELEMETRY=0 and CI turn it off for one run (telemetry.rs 12-14). |
| `libs/ui/include/agentdesk/ui/telemetry.hpp:15` | `` | AgentDesk has no server of its own: like herdr's build from source it collects and shows but |
| `libs/ui/include/agentdesk/ui/telemetry.hpp:16` | `` | never sends, unless AGENTDESK_TELEMETRY_URL points it at one (telemetry.rs 14-16, 100-106). |
| `libs/ui/include/agentdesk/ui/telemetry.hpp:34` | `using Json = nlohmann::ordered_json;` | herdr's serde_json keeps keys in the order written (preserve_order, via indexmap in Cargo.lock): |
| `libs/ui/include/agentdesk/ui/telemetry.hpp:38` | `inline constexpr std::uint32_t kVersion = 1;` | telemetry.rs 27: the report format. |
| `libs/ui/include/agentdesk/ui/telemetry.hpp:40` | `inline constexpr std::int64_t kKeepDays = 7;` | telemetry.rs 30: frozen reports older than this are dropped unsent. |
| `libs/ui/include/agentdesk/ui/telemetry.hpp:43` | `struct Observation {` | What the window sees, every few seconds while telemetry is on (telemetry.rs 33-49). |
| `libs/ui/include/agentdesk/ui/telemetry.hpp:60` | `struct Config {` | What the app knows that this module doesn't (herdr reads these from its own modules). |
| `libs/ui/include/agentdesk/ui/telemetry.hpp:62` | `std::filesystem::path data_dir;` | telemetry\ goes in it (telemetry.rs 77-79) |
| `libs/ui/include/agentdesk/ui/telemetry.hpp:75` | `struct Day {` | One UTC day's counts, kept locally until the day is over (telemetry.rs 187-210). |
| `libs/ui/include/agentdesk/ui/telemetry.hpp:105` | `std::string bucket(std::size_t n);` | Pure helpers (telemetry.rs 273-344, programs.rs 212-237), public for the tests. |
| `libs/ui/include/agentdesk/ui/telemetry.hpp:111` | `std::string agent_name(const std::optional<std::string>& app);` | An agent's 7501 `app` as a name herdr's programs table ships, else "other" ("unknown" when it |
| `libs/ui/include/agentdesk/ui/telemetry.hpp:117` | `bool collecting();` | --- the module (telemetry.rs 108-183) |
| `libs/ui/include/agentdesk/ui/telemetry.hpp:136` | `void stop();` | Ends the thread (process exit; herdr's dies with the process). |
| `libs/ui/include/agentdesk/ui/terminal_view.hpp:28` | `const std::array<ImU32, 16>* ansi = nullptr;` | A theme's own 16 ANSI colours (herdr themes "ansi"); null: the standard ones. |
| `libs/ui/include/agentdesk/ui/terminal_view.hpp:47` | `void drop_paths(std::vector<std::string> paths) { dropped_.push_back(std::move(paths)); }` | draw (herdr src/terminal.rs:717-723). |
| `libs/ui/include/agentdesk/ui/terminal_view.hpp:66` | `float line_height = 1.0f;` | cell height multiplier (herdr terminal_line_height) |
| `libs/ui/include/agentdesk/ui/terminal_view.hpp:67` | `float cell_width = 1.0f;` | cell width multiplier (herdr terminal_cell_width) |
| `libs/ui/include/agentdesk/ui/terminal_view.hpp:68` | `bool right_click_to_program = false;` | herdr terminal.right_click: when the program reads the mouse, right-clicks go to it instead |
| `libs/ui/include/agentdesk/ui/terminal_view.hpp:71` | `std::function<bool()> menu_rows;` | Draws the right-click menu's rows inside the open popup (the app's herdr menu); empty uses |
| `libs/ui/include/agentdesk/ui/terminal_view.hpp:82` | `void paste_clipboard(TerminalSource& source, bool full);` | herdr src/terminal.rs:788-815. full (terminal.paste): copied files as quoted paths, text, |
| `libs/ui/include/agentdesk/ui/updates.hpp:3` | `` | The app's own updates, ported from herdr-app src/update.rs (Apache-2.0, commit 321c9a8). |
| `libs/ui/include/agentdesk/ui/updates.hpp:8` | `` | or before opening the next window after a crash (update.rs 1-7). |
| `libs/ui/include/agentdesk/ui/updates.hpp:10` | `` | herdr's release builds look at releases.herdr.dev; a build from source looks for no updates |
| `libs/ui/include/agentdesk/ui/updates.hpp:11` | `` | unless HERDR_RELEASES_URL points it at a server (update.rs 9-11, 122-129). AgentDesk has no |
| `libs/ui/include/agentdesk/ui/updates.hpp:14` | `` | agent-host (herdr: herdr-core) is checked on the same clock (core/latest.json) when that |
| `libs/ui/include/agentdesk/ui/updates.hpp:15` | `` | variable is set (update.rs 13-15, 205-213). |
| `libs/ui/include/agentdesk/ui/updates.hpp:28` | `inline constexpr std::chrono::seconds kEvery{60 * 60};` | update.rs 23-25 |
| `libs/ui/include/agentdesk/ui/updates.hpp:32` | `struct CoreInfo {` | update.rs 83-92: agent-host as it says it is, and a newer release waiting. |
| `libs/ui/include/agentdesk/ui/updates.hpp:42` | `struct Status {` | update.rs 131-141: what the settings screen shows. |
| `libs/ui/include/agentdesk/ui/updates.hpp:54` | `std::string version;` | this build (herdr: CARGO_PKG_VERSION) |
| `libs/ui/include/agentdesk/ui/updates.hpp:56` | `std::filesystem::path data_dir;` | the .updated marker (herdr: mods::app_dir()) |
| `libs/ui/include/agentdesk/ui/updates.hpp:64` | `bool newer(std::string_view a, std::string_view b);` | update.rs 402-417: `a` is a later version than `b` (semver; 0.2.0-rc.1 comes before 0.2.0). |
| `libs/ui/include/agentdesk/ui/updates.hpp:66` | `std::optional<std::uint64_t> checked_at(bool succeeded, std::uint64_t now);` | update.rs 201-203 |
| `libs/ui/include/agentdesk/ui/updates.hpp:68` | `std::optional<std::string> startup_target(std::string_view running, const std::optional<st` | update.rs 436-441 |
| `libs/ui/include/agentdesk/ui/updates.hpp:71` | `std::string sha256(std::string_view bytes);` | update.rs 362-365: lowercase hex. |
| `libs/ui/include/agentdesk/ui/updates.hpp:73` | `std::filesystem::path beside(const std::filesystem::path& root, std::wstring_view suffix);` | update.rs 50-59: <root><suffix> beside it. |
| `libs/ui/include/agentdesk/ui/updates.hpp:75` | `std::filesystem::path system(std::wstring_view name);` | update.rs 353-360: curl and tar from System32. |
| `libs/ui/include/agentdesk/ui/updates.hpp:77` | `std::string target();` | update.rs 61-70: "windows-x86_64". |
| `libs/ui/include/agentdesk/ui/updates.hpp:79` | `std::optional<std::filesystem::path> find_root(const std::filesystem::path& exe);` | update.rs 34-45: the install folder for this exe, if it is one. |
| `libs/ui/include/agentdesk/ui/updates.hpp:88` | `Found check_from(const std::filesystem::path& root, const std::string& releases, const std` | update.rs 269-307: downloads the latest release if it's newer than `running`. |
| `libs/ui/include/agentdesk/ui/updates.hpp:90` | `std::optional<std::string> version_of(const std::filesystem::path& app);` | update.rs 392-400: what `agent-ui --version` says in this folder. |
| `libs/ui/include/agentdesk/ui/updates.hpp:92` | `std::optional<std::string> staged_version(const std::filesystem::path& root);` | update.rs 419-434: a stage is trusted only with its receipt and a matching executable. |
| `libs/ui/include/agentdesk/ui/updates.hpp:94` | `bool swap(const std::filesystem::path& root, std::string* error);` | update.rs 494-513: moves the install aside and the stage in; puts it back on failure. |
| `libs/ui/include/agentdesk/ui/updates.hpp:101` | `void set_core(std::optional<std::string> version, std::optional<std::string> managed);` | update.rs 107-120: what the connected agent-host says about itself. |
| `libs/ui/include/agentdesk/ui/updates.hpp:105` | `void start();` | update.rs 157-199: starts checking in the background (only with AGENTDESK_RELEASES_URL). |
| `libs/ui/include/agentdesk/ui/updates.hpp:107` | `void leave_install_folder();` | update.rs 443-457 |
| `libs/ui/include/agentdesk/ui/updates.hpp:109` | `bool apply_on_startup();` | update.rs 459-479: true when this process should exit (a newer copy was put in place and |
| `libs/ui/include/agentdesk/ui/updates.hpp:112` | `bool apply(std::string* error);` | update.rs 481-492: puts the downloaded version in place. False with `error` empty: nothing is |
| `libs/ui/include/agentdesk/ui/updates.hpp:113` | `bool apply(std::string* error);` | ready (herdr Ok(false)); false with `error` set: the swap failed (herdr Err). |
| `libs/ui/include/agentdesk/ui/updates.hpp:115` | `void relaunch();` | update.rs 515-518: opens the app again once this process has exited. |
| `libs/ui/include/agentdesk/ui/updates.hpp:117` | `void after_update();` | update.rs 581-621: the first start after an update. |
| `libs/ui/include/agentdesk/ui/updates.hpp:122` | `void set_pipe(std::wstring pipe);` | agent-ui; herdr core_bin.rs 242-265 runs `hc update`) updates that agent-host. |
| `libs/ui/include/agentdesk/ui/updates.hpp:124` | `void update_core();` | update.rs 235-262: `agentctl update` in the background; the terminals keep running (agent-host |
| `libs/ui/include/agentdesk/ui/updates.hpp:127` | `bool run_core_update(const std::vector<std::wstring>& extra, std::string* now, std::string` | core_bin.rs 247-265: runs `agentctl update --json [extra]`. Ok: what agent-host is now |
| `libs/ui/include/agentdesk/ui/updates.hpp:131` | `struct CoreStageRequest {` | What `agentctl update` (herdr: `hc update`) does before it asks the running agent-host to |
| `libs/ui/include/agentdesk/ui/updates.hpp:136` | `std::optional<std::string> at_least;` | `--version X` (core_bin.rs 246) |
| `libs/ui/include/agentdesk/ui/win98.hpp:2` | `` | Herdr 98 (herdr-app defaults/mods/win98, commit 321c9a8), the parts with no drawing in them: |
| `libs/ui/include/agentdesk/ui/win98.hpp:3` | `` | the kit's pixel math and tables (kit.js), the menu metrics (kit.js 322-344), DOS paths and |
| `libs/ui/include/agentdesk/ui/win98.hpp:4` | `` | clock texts (kit.js 431-437, taskbar.js 36-46, dialog.js 655-691), and the rules of the two |
| `libs/ui/include/agentdesk/ui/win98.hpp:20` | `namespace color {` | ---------------------------------------------------------------- kit.js 12-32: the colors |
| `libs/ui/include/agentdesk/ui/win98.hpp:44` | `enum class Bevel { kRaised, kPressed, kWindow, kField, kWell, kBump, kDent, kThin, kFlat }` | ---------------------------------------------------------------- kit.js 84-95: the bevels |
| `libs/ui/include/agentdesk/ui/win98.hpp:53` | `struct Unit {` | kit.js 40-51: n Win98 pixels are n * k pixels on screen; k is the scale (1 Small Fonts, 2 Large |
| `libs/ui/include/agentdesk/ui/win98.hpp:54` | `struct Unit {` | Fonts). herdr divides by its zoom to get layout px, which its zoom multiplies back: the pixels |
| `libs/ui/include/agentdesk/ui/win98.hpp:63` | `inline constexpr float kTaskbarPx = 28.0f;` | kit.js 441-479 (fitLayout): the layout's bars in Win98 pixels. |
| `libs/ui/include/agentdesk/ui/win98.hpp:68` | `float terminal_font_px(int scale, float zoom);` | The MS-DOS font: Large Fonts, a crisp 32 px (16 Win98 px); Small, 16 px at Herdr's zoom. |
| `libs/ui/include/agentdesk/ui/win98.hpp:71` | `int access_index(std::string_view label);` | ---------------------------------------------------------------- text (kit.js 55-75) |
| `libs/ui/include/agentdesk/ui/win98.hpp:72` | `int access_index(std::string_view label);` | Index of the access key ("&File" -> 1, the F), or -1. kit.js 58: an "&" that is not last. |
| `libs/ui/include/agentdesk/ui/win98.hpp:74` | `std::string plain(std::string_view label);` | kit.js 73-75: the first "&x" becomes "x". |
| `libs/ui/include/agentdesk/ui/win98.hpp:76` | `bool access_matches(std::string_view label, char ch);` | menu.js 326-327: the item whose label holds "&" + ch (lowercase). |
| `libs/ui/include/agentdesk/ui/win98.hpp:79` | `struct MenuMetrics {` | ---------------------------------------------------------------- menus (kit.js 322-344) |
| `libs/ui/include/agentdesk/ui/win98.hpp:98` | `using Edge = layout::Edge;` | herdr src/layout.rs 33-59, 732-751 and src/window/floats.rs 78-96: a floating block (Clippy) |
| `libs/ui/include/agentdesk/ui/win98.hpp:109` | `float spring(float t);` | herdr floats.rs 71-75: ease out with a small overshoot, so the snap feels like a spring |
| `libs/ui/include/agentdesk/ui/win98.hpp:110` | `float spring(float t);` | (t in 0..1; 0 at the drop, 1 home). A dropped float springs home over kSnapMs (floats.rs 17). |
| `libs/ui/include/agentdesk/ui/win98.hpp:113` | `inline constexpr float kDragStart = 4.0f;` | floats.rs 16: a press moves it from this far |
| `libs/ui/include/agentdesk/ui/win98.hpp:121` | `float dialog_width(std::string_view kind);` | kit.js 373: each dialog's width in Win98 px (340 when not listed). |
| `libs/ui/include/agentdesk/ui/win98.hpp:125` | `std::string dos_path(std::string_view path, std::string_view home);` | kit.js 431-437, for Windows paths: under the home folder "C:\My Documents\...", else the path |
| `libs/ui/include/agentdesk/ui/win98.hpp:128` | `std::string user_name(std::string_view home);` | taskbar.js 61-65: the home folder's last part, capitalised ("User" without one). |
| `libs/ui/include/agentdesk/ui/win98.hpp:130` | `std::string run_name(std::string_view command);` | dialog.js 227: the program's name for a Run line: its first word, after the last "/". |
| `libs/ui/include/agentdesk/ui/win98.hpp:132` | `std::vector<std::string> run_history(std::vector<std::string> history, const std::string& ` | dialog.js 224: the Run history: this command first, no repeats, at most 12. |
| `libs/ui/include/agentdesk/ui/win98.hpp:135` | `struct DateTime {` | ---------------------------------------------------------------- time (taskbar.js 36-46) |
| `libs/ui/include/agentdesk/ui/win98.hpp:151` | `std::string time_zone_text(const DateTime& d);` | dialog.js 688-691: "UTC+08:00" |
| `libs/ui/include/agentdesk/ui/win98.hpp:152` | `std::vector<std::array<int, 7>> calendar(int year, int month);` | dialog.js 656-666: the month's weeks, Sunday first; 0 is an empty cell. |
| `libs/ui/include/agentdesk/ui/win98.hpp:154` | `struct ClockDot {` | dialog.js 927-958: the clock face. Each dot: left, top (Win98 px in the 96 px face), size, |
| `libs/ui/include/agentdesk/ui/win98.hpp:186` | `static std::optional<std::array<int, 3>> level(std::string_view name);` | mines.js 6: {cols, rows, mines}; nullopt for an unknown level. |
| `libs/ui/include/agentdesk/ui/win98.hpp:189` | `void reset();` | mines.js 34-42 |
| `libs/ui/include/agentdesk/ui/win98.hpp:191` | `std::string reveal(int i, long long now_ms);` | mines.js 71-104: the first reveal lays the mines away from i and its neighbours and starts |
| `libs/ui/include/agentdesk/ui/win98.hpp:194` | `std::string chord(int i, long long now_ms);` | mines.js 106-112: a number with as many flags around it reveals its other neighbours. |
| `libs/ui/include/agentdesk/ui/win98.hpp:196` | `void mark(int i);` | mines.js 173: right-click cycles none, flag, (?) on a closed cell while the game runs. |
| `libs/ui/include/agentdesk/ui/win98.hpp:198` | `bool tick(long long now_ms);` | mines.js 64-68: once a second; stops at 999. Returns true when the shown time changed. |
| `libs/ui/include/agentdesk/ui/win98.hpp:200` | `void request(const std::string& cmd);` | mines.js 114-123: a menu command. "best" is the caller's (a message box). |
| `libs/ui/include/agentdesk/ui/win98.hpp:202` | `[[nodiscard]] int mines_left() const;` | mines.js 186: the counter at the left. |
| `libs/ui/include/agentdesk/ui/win98.hpp:204` | `[[nodiscard]] std::string face() const;` | mines.js 185: the face: "dead", "cool", "ooh" (a cell held) or "smile". |
| `libs/ui/include/agentdesk/ui/win98.hpp:206` | `[[nodiscard]] std::string best_text() const;` | mines.js 122: the Best Times text. |
| `libs/ui/include/agentdesk/ui/win98.hpp:220` | `int held = -1;` | the cell pressed and not released (mines.js 40) |
| `libs/ui/include/agentdesk/ui/win98.hpp:236` | `std::string counter_chars(int value);` | mines.js 8-21: the seven-segment counter. The characters ("-" then two digits below zero, |
| `libs/ui/include/agentdesk/ui/win98.hpp:240` | `const std::array<std::array<int, 4>, 7>& segment_rects();` | mines.js 9: each segment's x, y, w, h in the 11 x 23 digit (a..g). |
| `libs/ui/include/agentdesk/ui/win98.hpp:242` | `std::uint32_t mines_number_color(int n);` | mines.js 7: the number colors 1..8 (index 0 is the face color). |
| `libs/ui/include/agentdesk/ui/win98.hpp:248` | `static constexpr int kW = 596, kH = 355, kCW = 71, kCH = 96;` | sol.js 6 |
| `libs/ui/include/agentdesk/ui/win98.hpp:279` | `static int rank(int c) { return c % 13 + 1; }` | sol.js 7-9 |
| `libs/ui/include/agentdesk/ui/win98.hpp:283` | `void deal();` | sol.js 223-234 |
| `libs/ui/include/agentdesk/ui/win98.hpp:284` | `[[nodiscard]] bool valid_pick(const Loc& loc) const;` | sol.js 51-59 |
| `libs/ui/include/agentdesk/ui/win98.hpp:285` | `bool move(const Loc& from, const Loc& to);` | sol.js 60-75. True when it moved; "tada" goes to `sound` on a win. |
| `libs/ui/include/agentdesk/ui/win98.hpp:287` | `void foundation(const Loc& loc);` | sol.js 76-79: a double-click sends it home |
| `libs/ui/include/agentdesk/ui/win98.hpp:288` | `void stock();` | sol.js 80-88 |
| `libs/ui/include/agentdesk/ui/win98.hpp:289` | `enum class Down { kNothing, kMoved, kTurned, kPick, kUnpick };` | sol.js 89-100, the part that is not about the pointer: a face-down card at the end of a |
| `libs/ui/include/agentdesk/ui/win98.hpp:293` | `[[nodiscard]] static std::optional<Loc> target(float x, float y);` | sol.js 105-109 |
| `libs/ui/include/agentdesk/ui/win98.hpp:294` | `void tick_second();` | sol.js 42-46 |
| `libs/ui/include/agentdesk/ui/win98.hpp:295` | `bool animate();` | sol.js 113-127: one 33 ms frame of the bouncing cards; false when it ended. |
| `libs/ui/include/agentdesk/ui/win98.hpp:297` | `void command(const std::string& cmd);` | sol.js 132-140 (deal, undo, autowin) |
| `libs/ui/include/agentdesk/ui/win98.hpp:298` | `[[nodiscard]] std::vector<int> cards(const Loc& loc) const;` | sol.js 50 |
| `libs/ui/include/agentdesk/ui/win98.hpp:300` | `[[nodiscard]] float tab_step(int i) const;` | sol.js 220: the step between face-up cards of a column. |
| `libs/ui/include/agentdesk/ui/win98.hpp:307` | `bool running = false;` | the clock runs (sol.js 41) |
| `libs/ui/include/agentdesk/ui/workspace.hpp:7` | `` | -> Node  (split tree like herdr's layout.rs: a row or column holds any number of |
| `libs/ui/include/agentdesk/ui/workspace.hpp:41` | `std::vector<std::string> focus_history;` | panes focused before, oldest first (herdr Tab::focus) |
| `libs/ui/include/agentdesk/ui/workspace.hpp:42` | `bool settled = false;` | herdr tab.settle: put away (not in the tab bar), still running |
| `libs/ui/include/agentdesk/ui/workspace.hpp:51` | `std::string color;` | avatar colour name (herdr: ansi6, ansi3, ansi4, ansi5, ansi2, ansi1) |
| `libs/ui/include/agentdesk/ui/workspace.hpp:53` | `std::string folder;` | where it was made: names it when its first terminal says nothing (herdr) |
| `libs/ui/include/agentdesk/ui/workspace.hpp:92` | `bool move_pane(const std::string& moving, const std::string& target, std::optional<Dir> ed` | Drag a pane onto another of the same tab (herdr pane drag): onto an edge it moves there |
| `libs/ui/include/agentdesk/ui/workspace.hpp:95` | `bool move_space(int from, int to);` | Moves a space within its machine's block (herdr space.move); keeps the active one active. |
| `libs/ui/include/agentdesk/ui/workspace.hpp:99` | `void assign_colors(const std::map<std::string, std::string>* session_top = nullptr);` | (herdr spaces.js 526). |
| `libs/ui/include/agentdesk/ui/workspace.hpp:102` | `bool reorder_spaces(const std::vector<int>& order);` | active (herdr spaces.js 316-334: space.move so the model follows the drawn tree). |
| `libs/ui/include/agentdesk/ui/workspace.hpp:113` | `bool settle_tab(int space, int tab, bool keep_shown = false);` | herdr tab.settle: hides a tab from the bar until it is shown or needs you. The last tab |
| `libs/ui/include/agentdesk/ui/workspace.hpp:115` | `bool settle_tab(int space, int tab, bool keep_shown = false);` | settled and stays shown (herdr model.rs 470-484; Herdr 98 minimizes the last window too). |
| `libs/ui/include/agentdesk/ui/workspace.hpp:128` | `void toggle_zoom(const std::string& pane = {});` | herdr pane.zoom {pane}: zooms and focuses it |
| `libs/ui/include/agentdesk/ui/workspace.hpp:131` | `[[nodiscard]] std::string space_title(int index,` | herdr model.rs 271-279: its name; else its first terminal's checkout top (a worktree's own |
| `libs/ui/include/agentdesk/ui/workspace.hpp:164` | `enum class Attention { kNone, kIdle, kWorking, kDone, kBlocked };` | ---- attention (herdr-style: "done" = finished while you were not looking) |
| `libs/ui/include/agentdesk/ui/workspace.hpp:186` | `int attention_rank(Attention a);` | Most urgent first: blocked > done > working > idle > none (herdr's ordering). |
| `libs/ui/include/agentdesk/ui/workspace.hpp:191` | `struct SpaceRepo {` | ---- spaces list (herdr mods/spaces, Apache-2.0) |
| `libs/ui/include/agentdesk/ui/workspace.hpp:199` | `std::vector<int> nest_worktrees(const std::vector<SpaceRepo>& info, std::vector<bool>* tre` | spaces.js 298-315: the order the list draws, each linked worktree right after the first main |
| `libs/ui/include/agentdesk/ui/workspace.hpp:204` | `std::string clean_agent_title(const std::string& title);` | ---- agents list (herdr mods/agents, Apache-2.0) |
| `libs/ui/include/agentdesk/ui/workspace.hpp:205` | `std::string clean_agent_title(const std::string& title);` | agents.js 28-31: drops what an agent puts before its title (a status glyph or spinner and the |
| `libs/ui/include/agentdesk/ui/workspace.hpp:209` | `struct TabLabel {` | agents.js 44-51: what a tab says about the agent in it. `tab_name` is the tab's name or number. |
| `libs/ui/include/agentdesk/ui/workspace.hpp:220` | `std::vector<std::string> agent_details(const std::vector<std::pair<std::string, std::strin` | agents.js 279-296: the detail after a row's space ("" = none): an automatic tab number says |
| `libs/ui/include/agentdesk/ui/workspace.hpp:224` | `Attention kid_status(Attention a);` | agents.js 53-58: a sub-agent's status. It never shows done (it reports to its parent): idle. |
| `libs/ui/include/agentdesk/ui/workspace.hpp:227` | `enum class TreeLine { kNone, kBar, kTee, kElbow };` | agents.js 170-180: the live sub-agents under `root`, depth first, each with its tree lines. |
| `libs/ui/include/agentdesk/ui/workspace.hpp:235` | `struct AgentSortKey {` | agents.js 240: priority order: most urgent first, then latest change, then newest. |
| `libs/ui/src/chrome_logic.cpp:14` | `void MenuNav::frame(bool up, bool down, bool enter) {` | herdr menu/main.js 132-134: down = (at + 1) % n, up = (at + n - 1) % n, enter = choose(at). |
| `libs/ui/src/host_client.cpp:500` | `const bool took_over = std::find(hc.host_succeeds.begin(), hc.host_succeeds.end(), instanc` | over in a self-update (design doc 7.4 OQ-09; herdr hc.rs 26-28: "cores restart and |
| `libs/ui/src/keymap.cpp:121` | `constexpr std::pair<const char*, const char*> kGlobal[] = {` | herdr's defaults/keys.json for Windows (spec-settings-fonts.md 3.2), plus ctrl-1..9 / alt-1..9. |
| `libs/ui/src/keymap.cpp:171` | `{"ctrl-v", "terminal.paste_image"},` | herdr terminal.paste_image: on Windows the clipboard text, else Ctrl+V for the program. |
| `libs/ui/src/keymap.cpp:182` | `constexpr std::pair<const char*, const char*> kWin98[] = {` | herdr-app defaults/mods/win98/README.md 12-13: keys.json section "layout:win98", |
| `libs/ui/src/keymap.cpp:231` | `std::string migrate(std::string a) {` | herdr keys.rs 132-149: names from older releases. |
| `libs/ui/src/keymap.cpp:294` | `continue;` | herdr: skipped in global |
| `libs/ui/src/keymap.cpp:310` | `std::vector<Keymap::Binding> Keymap::listing(const nlohmann::json& file) {` | herdr-app src/keys.rs 89-109 (listing): the file's sections as written (sorted maps), the |
| `libs/ui/src/keymap.cpp:341` | `std::string Keymap::help_label(std::string_view action) {` | herdr-app defaults/mods/help/main.js 17-22. |
| `libs/ui/src/keymap.cpp:359` | `const char* Keymap::help_group(std::string_view action) {` | herdr-app defaults/mods/help/main.js 8-15. |
| `libs/ui/src/layout.cpp:1` | `` | herdr-app's layout engine, ported (see layout.hpp). Every rule cites the herdr file it comes |
| `libs/ui/src/layout.cpp:2` | `` | from (herdr-app, Apache-2.0; third_party/herdr/LICENSE). |
| `libs/ui/src/layout.cpp:12` | `extern const char* const kHerdrMain;` | Generated from third_party/herdr/layouts/*.json (libs/ui/CMakeLists.txt). |
| `libs/ui/src/layout.cpp:81` | `std::optional<Line> read_line(const Json& v, const Reader& r, const std::string& at) {` | layout.rs 86-99. |
| `libs/ui/src/layout.cpp:107` | `bool read_background(const Json& j, const char* k, Json& out, const Reader& r, const std::` | layout.rs 101-130: a color, a gradient (angle 180 unless said) or an image (cover unless said). |
| `libs/ui/src/layout.cpp:140` | `std::optional<Header> read_header(const Json& v, const Reader& r, const std::string& at) {` | layout.rs 221-256. |
| `libs/ui/src/layout.cpp:164` | `std::optional<PaneFrame> read_pane(const Json& v, const Reader& r, const std::string& at) ` | layout.rs 258-282. |
| `libs/ui/src/layout.cpp:184` | `std::optional<Content> read_content(const Json& v, const Reader& r, const std::string& at)` | layout.rs 9-31: untagged, tried in order; a variant whose fields don't fit gives way to the next. |
| `libs/ui/src/layout.cpp:204` | `const char* tab = v.contains("tab") ? "tab" : v.contains("context") ? "context" : nullptr;` | `context`: 0.1.0's word for `tab` (layout.rs 23-24). |
| `libs/ui/src/layout.cpp:253` | `std::optional<Float> read_float_block(const Json& v, const Reader& r, const std::string& a` | layout.rs 61-77. |
| `libs/ui/src/layout.cpp:290` | `bool read_look(const Json& v, Json& look, const Reader& r, const std::string& at) {` | layout.rs 150-195: the look, flattened into the node. |
| `libs/ui/src/layout.cpp:557` | `if (active)` | layout.rs 672-675: the active one if shown, else the first shown. |
| `libs/ui/src/layout.cpp:589` | `if (is_line(node)) {` | layout.rs 681-696: its share goes back to the neighbor it was split from (the one |
| `libs/ui/src/layout.cpp:597` | `if (node.kind == Kind::kStack && node.active && *node.active == id) {` | layout.rs 697-702: the neighbor that slides into its place (or the last) becomes active. |
| `libs/ui/src/layout.cpp:614` | `void collapse(Node& node) {` | layout.rs 708-730. |
| `libs/ui/src/layout.cpp:834` | `bool Node::split(std::string_view target, Axis ax, Node fresh, std::string container_id) {` | layout.rs 598-629. |
| `libs/ui/src/layout.cpp:874` | `bool Node::swap(std::string_view a, std::string_view b) {` | layout.rs 651-670: sizes stay with the places. |
| `libs/ui/src/layout.cpp:919` | `Json to_json(const Node& n) {` | layout.rs 327-400, with serde's skip_serializing_if. |
| `libs/ui/src/layout.cpp:1015` | `void normalize(Json& v) {` | layout_sync.rs 291-306. |
| `libs/ui/src/layout.cpp:1092` | `static thread_local Line focus_line;` | pane_size.rs 7-9: the focus border while focused, else the border. |
| `libs/ui/src/layout.cpp:1108` | `return h.height.value_or(30.0f);` | window.rs 32 HEADER_HEIGHT |
| `libs/ui/src/layout.cpp:1121` | `std::vector<float> shares(const std::vector<const Node*>& children, float available, float` | pane_size.rs 49-85. |
| `libs/ui/src/layout.cpp:1184` | `constexpr float kHandle = 6.0f;` | window.rs 33 HANDLE |
| `libs/ui/src/layout.cpp:1186` | `void place_node(const Node& n, Rect b, float zoom, const std::set<std::string>& focused, b` | window.rs 4439-4502 render_node with sized() (5184-5209); pane_size.rs 87-118 block_bounds. |
| `libs/ui/src/layout.cpp:1253` | `bool drag(Node& root, std::string_view container, int index, Axis axis, const Placement& p` | window.rs 4671-4723. |
| `libs/ui/src/layout.cpp:1398` | `std::optional<PaneClick> PaneClick::of(const Node& n) {` | layout_sync.rs 63-78. |
| `libs/ui/src/layout.cpp:1441` | `State State::between(const Node& file, const Node& now) {` | layout_sync.rs 127-170. |
| `libs/ui/src/layout.cpp:1484` | `void State::apply(Node& root) const {` | layout_sync.rs 172-212. |
| `libs/ui/src/layout.cpp:1519` | `void State::forget_edited(const Node& old_file, const Node& new_file) {` | layout_sync.rs 214-257. |
| `libs/ui/src/layout.cpp:1623` | `Json State::to_json() const {` | layout_sync.rs 10-47 (PaneClick writes its three fields, null when unset). |
| `libs/ui/src/layout.cpp:1669` | `Node without_clicks(const Node& file, const Node& now) {` | layout_sync.rs 260-287. |
| `libs/ui/src/links.cpp:26` | `constexpr std::array<std::string_view, 15> kSchemes = {` | Ghostty's schemes (herdr src/links.rs:11), the only ones a link may open. |
| `libs/ui/src/links.cpp:79` | `std::size_t path_at(const std::string& t, std::size_t i) {` | rooted_path and bare_path (herdr src/links.rs:22-27), ending without a colon. |
| `libs/ui/src/links.cpp:176` | `std::optional<std::string> file_url_path(const std::string& link) {` | The path a file: URL names on this computer, percent-decoded (herdr links::file_url_path). |
| `libs/ui/src/programs.cpp:1` | `` | herdr-app src/programs.rs (Apache-2.0), ported to C++ for AgentDesk. |
| `libs/ui/src/programs.cpp:17` | `constexpr std::array<std::string_view, 9> kAgentsFirst = {"claude", "codex",   "pi",     "` | herdr programs.rs 64: the agents people pick most, first in menus; the rest by name. |
| `libs/ui/src/programs.cpp:21` | `constexpr std::string_view kFishExec =` | herdr remote_shell.rs 19 (a remote fish's wrapper). |
| `libs/ui/src/programs.cpp:60` | `void over(ProgramEntry& e, ProgramEntry mine) {` | herdr programs.rs Entry::over: the person's fields win where given. |
| `libs/ui/src/programs.cpp:83` | `Parsed parse_table(std::string_view text) {` | Throws nlohmann::json::exception on bad JSON (herdr: serde's error). |
| `libs/ui/src/programs.cpp:98` | `std::string_view base(std::string_view s) {` | herdr programs.rs base(): the file name, no leading dashes ("-zsh"), no ".exe". |
| `libs/ui/src/programs.cpp:109` | `constexpr std::array<std::string_view, 5> kPsQuotes = {"'", "\xE2\x80\x98", "\xE2\x80\x99"` | herdr programs.rs 133: PowerShell's single quotes, which include the curly ones. |
| `libs/ui/src/programs.cpp:124` | `const char* hash_color(std::string_view s) {` | herdr programs.rs hash_color: a color for a program nobody listed, the same every time. |
| `libs/ui/src/programs.cpp:250` | `const std::pair<const std::string, ProgramEntry>* Programs::find(std::string_view name) co` | herdr programs.rs find(): an explicit key wins (including the person's own entry), except the |
| `libs/ui/src/sprites.cpp:1` | `` | Ported from herdr-app src/sprites.rs (Apache-2.0, third_party/herdr/LICENSE), after Ghostty. |
| `libs/ui/src/sprites.cpp:2` | `` | Comments quote herdr; "herdr sprites.rs:N" gives the line each part comes from. |
| `libs/ui/src/sprites.cpp:14` | `std::optional<std::string_view> nth_word(std::string_view table, std::size_t n) {` | The n-th whitespace-separated word of `table`. herdr sprites.rs:29 |
| `libs/ui/src/sprites.cpp:80` | `void powerline(char32_t c, float x, float y, float w, float h, float line, float scale, Sh` | don't fall back to whatever font has those codes. herdr sprites.rs:296-338 |
| `libs/ui/src/sprites.cpp:84` | `const float ext = solid ? 1.0f / scale : 0.0f;` | neighbor's color without an anti-aliased seam. herdr sprites.rs:302-304 |
| `libs/ui/src/sprites.cpp:90` | `constexpr float K = 0.5522848f;` | Half an ellipse filling the cell: four quarter arcs as cubics. herdr sprites.rs:307-308 |
| `libs/ui/src/sprites.cpp:142` | `std::optional<std::array<std::uint8_t, 4>> lines(char32_t c) {` | herdr sprites.rs:8-35 |
| `libs/ui/src/sprites.cpp:182` | `std::optional<std::array<float, 2>> arc(char32_t c) {` | herdr sprites.rs:39-47 |
| `libs/ui/src/sprites.cpp:198` | `std::optional<Dashes> dashes(char32_t c) {` | herdr sprites.rs:50-66 |
| `libs/ui/src/sprites.cpp:230` | `std::optional<Blocks> blocks(char32_t c) {` | herdr sprites.rs:69-108 |
| `libs/ui/src/sprites.cpp:298` | `std::optional<std::vector<std::array<int, 4>>> braille(char32_t c, int width, int height) ` | down the left, 4,5,6,8 down the right. herdr sprites.rs:110-159 |
| `libs/ui/src/sprites.cpp:355` | `bool is_sprite(char32_t c) {` | herdr sprites.rs:165-171 |
| `libs/ui/src/sprites.cpp:364` | `Shapes paint(char32_t c, float x, float y, float w, float h, float scale) {` | Paints `c` (one sprite character) into the cell, snapped to device pixels. herdr |
| `libs/ui/src/sprites.cpp:378` | `const float dx = std::round(x * scale);` | herdr sprites.rs:186-194 |
| `libs/ui/src/sprites.cpp:389` | `if (c == kRecord) {` | herdr sprites.rs:195-200 |
| `libs/ui/src/sprites.cpp:397` | `if (in_powerline(c)) {` | herdr sprites.rs:201-205 |
| `libs/ui/src/sprites.cpp:403` | `if (const auto b = blocks(c)) {` | herdr sprites.rs:206-213 |
| `libs/ui/src/sprites.cpp:412` | `if (const auto a = arc(c)) {` | herdr sprites.rs:216-235 |
| `libs/ui/src/sprites.cpp:437` | `if (const auto d = dashes(c)) {` | herdr sprites.rs:236-253 |
| `libs/ui/src/sprites.cpp:457` | `const auto dirs_opt = lines(c);` | herdr sprites.rs:254-293 |
| `libs/ui/src/system_process.hpp:3` | `` | Runs Windows' own helpers (curl.exe, tar.exe from System32; herdr update.rs 353-360) without a |
| `libs/ui/src/telemetry.cpp:1` | `` | Port of herdr-app src/telemetry.rs (Apache-2.0, commit 321c9a8); line numbers below are that |
| `libs/ui/src/telemetry.cpp:78` | `std::uint64_t random_u64() {` | 656-662: random enough for an id (herdr: std's OS-seeded RandomState; here the OS's RNG). |
| `libs/ui/src/telemetry.cpp:123` | `if (const auto v = set(L"AGENTDESK_TELEMETRY")) {` | 93: HERDR_TELEMETRY, ours AGENTDESK_TELEMETRY. |
| `libs/ui/src/telemetry.cpp:142` | `std::optional<std::string> endpoint() {` | 100-106: where reports go, if this build sends at all. herdr's release builds send to its own |
| `libs/ui/src/telemetry.cpp:143` | `std::optional<std::string> endpoint() {` | server; AgentDesk has none, so only AGENTDESK_TELEMETRY_URL (herdr: HERDR_TELEMETRY_URL) sends. |
| `libs/ui/src/telemetry.cpp:510` | `{"endpoint", url ? Json(*url) : Json()},` | 624: herdr falls back to its own server's address; AgentDesk has none. |
| `libs/ui/src/telemetry.cpp:540` | `struct ProgramNames {` | herdr defaults/programs.json "programs": each key and its "match" names (programs.rs 212-237 |
| `libs/ui/src/telemetry.cpp:681` | `std::string agent_name(const std::optional<std::string>& app) {` | programs.rs 212-237 |
| `libs/ui/src/telemetry.cpp:1020` | `void stop() {` | herdr's thread dies with the process. Here: what was handed over (a quit) is handled, waiting |
| `libs/ui/src/telemetry.cpp:1021` | `void stop() {` | no longer than herdr's quit deadline (main.rs 177: 250 ms), then the process may go. |
| `libs/ui/src/terminal_view.cpp:22` | `constexpr auto kDragScrollEvery = std::chrono::milliseconds(50);` | herdr src/terminal.rs:1174 |
| `libs/ui/src/terminal_view.cpp:147` | `bool is_sprite_cell(const terminal::Cell& cell) {` | bounds, never as a font glyph (herdr src/terminal.rs:1983-1986, 2213; src/sprites.rs:174). |
| `libs/ui/src/terminal_view.cpp:164` | `for (const sprites::Rect& r : shapes.rects)` | herdr window.paint_quad(fill(...)) / corner_radii (src/sprites.rs:182, 198) |
| `libs/ui/src/terminal_view.cpp:167` | `for (const sprites::Path& p : shapes.paths) {` | herdr PathBuilder::fill / stroke + window.paint_path (src/sprites.rs:226-233, 306, 335-337) |
| `libs/ui/src/terminal_view.cpp:399` | `clear_selection();` | The screen and its history (herdr src/vt.rs:1144). Key 0 is before every history line. |
| `libs/ui/src/terminal_view.cpp:436` | `if (const auto t = keymap->terminal(chord)) {` | herdr: terminal section > layout section > global. |
| `libs/ui/src/terminal_view.cpp:471` | `clear_selection();` | typing clears a selection (herdr src/terminal.rs:1025-1028) |
| `libs/ui/src/terminal_view.cpp:510` | `if (menu_rows) {` | herdr menu: rows drawn by the app, 220 px wide |
| `libs/ui/src/terminal_view.cpp:548` | `if (ImGui::MenuItem(right_click_to_program ? "Use AgentDesk right-click menu" : "Send righ` | herdr pane menu: where right-clicks go (only matters while the program reads the mouse). |
| `libs/ui/src/terminal_view.cpp:704` | `const float cell_w =` | herdr terminal_cell_width / terminal_line_height multipliers (0.8..2); text sits centred |
| `libs/ui/src/terminal_view.cpp:773` | `const bool link_key = io.KeyCtrl && !io.KeyShift && !io.KeyAlt;` | ---- Ctrl+hover links (herdr src/terminal.rs:1141-1166, 1486-1489): OSC 8 links, then URLs |
| `libs/ui/src/terminal_view.cpp:942` | `const auto now = Clock::now();` | Held past the top or bottom edge: 1-5 lines every 50 ms (herdr src/terminal.rs:1172-1192). |
| `libs/ui/src/terminal_view.cpp:963` | `if (item.hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle) && !report_mouse && sou` | Middle-click pastes when the program doesn't take it (herdr src/terminal.rs:2567-2576). |
| `libs/ui/src/terminal_view.cpp:1007` | `if (is_sprite_cell(cell))` | (herdr src/terminal.rs:1983-1986, 2023, 2213). |
| `libs/ui/src/updates.cpp:1` | `` | Port of herdr-app src/update.rs (Apache-2.0, commit 321c9a8); line numbers below are that |
| `libs/ui/src/updates.cpp:31` | `constexpr std::uint64_t kGeneration = static_cast<std::uint64_t>(protocol::kProtocolVersio` | update.rs 13-15, hc.rs 207: a newer core is offered only within this app's generation. Our |
| `libs/ui/src/updates.cpp:79` | `std::optional<std::string> releases() {` | 72-74: herdr falls back to releases.herdr.dev. AgentDesk has no server of its own. |
| `libs/ui/src/updates.cpp:86` | `std::wstring packed(const std::string& version) {` | 47-48: the folder inside a release archive. herdr's tarball holds "herdr-app"; AgentDesk's |
| `libs/ui/src/updates.cpp:148` | `const fs::path tarball = work / L"app.zip";` | herdr: app.tar.gz; ours is the portable zip, which System32's tar (bsdtar) also unpacks. |
| `libs/ui/src/updates.cpp:166` | `if (!run(system(L"tar"), {L"-xf", tarball.native(), L"-C", work.native()}, error))` | herdr: tar -xzf; bsdtar picks the format itself (-z would refuse a zip). |
| `libs/ui/src/updates.cpp:178` | `std::string url;` | shape as app/latest.json's targets (herdr-core's own download is not in herdr-app). |
| `libs/ui/src/updates.cpp:266` | `const fs::path powershell = [] {` | herdr runs `powershell` from PATH; Windows' own copy here. |
| `libs/ui/src/updates.cpp:272` | `return detail::spawn_detached(powershell, {L"-NoProfile", L"-NonInteractive", L"-Command",` | 527: HERDR_APP_UPDATE_EXPECTED, ours AGENTDESK_UPDATE_EXPECTED. |
| `libs/ui/src/updates.cpp:545` | `const std::string prefix = "agent-ui ";` | herdr-app's answer is "herdr-app 0.2.0"; ours "agent-ui 0.2.0". |
| `libs/ui/src/updates.cpp:789` | `{` | 591-621: herdr brings herdr-core to the latest release of this generation |
| `libs/ui/src/updates.cpp:812` | `core.error = error;` | herdr: tracing::warn!("herdr-core update: {e}") |
| `libs/ui/src/updates.cpp:827` | `std::vector<std::wstring> args = {L"update", L"--json"};` | core_bin.rs 247-265: `hc update --json [--version X]`, stdin closed. |
| `libs/ui/src/updates.cpp:928` | `s.error = "no release server";` | update.rs 9-11, 122-129: a build from source looks for no updates without a server. |
| `libs/ui/src/win98.cpp:1` | `` | Herdr 98's rules and numbers, ported from herdr-app defaults/mods/win98 (commit 321c9a8, |
| `libs/ui/src/win98.cpp:46` | `std::vector<BevelLayer> bevel_layers(Bevel kind) {` | kit.js 84-95. |
| `libs/ui/src/win98.cpp:72` | `int scale_of(int shared_scale) {` | kit.js 40-43. |
| `libs/ui/src/win98.cpp:77` | `float terminal_font_px(int scale, float zoom) {` | kit.js 451-452: scale 2 gives want(16) (16 * 2 / zoom layout px, 32 px on screen); scale 1 |
| `libs/ui/src/win98.cpp:83` | `int access_index(std::string_view label) {` | kit.js 57-58. |
| `libs/ui/src/win98.cpp:91` | `std::string plain(std::string_view label) {` | kit.js 73-75: String.replace(/&(.)/, "$1"), the first one only. |
| `libs/ui/src/win98.cpp:100` | `bool access_matches(std::string_view label, char ch) {` | menu.js 326-327. |
| `libs/ui/src/win98.cpp:109` | `MenuSize menu_size(const std::vector<MenuRowText>& items, bool start) {` | kit.js 326-344. |
| `libs/ui/src/win98.cpp:131` | `float spring(float t) {` | herdr src/layout.rs 732-751. |
| `libs/ui/src/win98.cpp:159` | `std::array<float, 2> float_home(float ox, float oy, float w, float h, float bw, float bh, ` | herdr src/window/floats.rs 78-96. |
| `libs/ui/src/win98.cpp:182` | `FloatPlace parse_place(std::string_view edge, std::string_view align) {` | layout.rs 36-59: the defaults are right and center. |
| `libs/ui/src/win98.cpp:193` | `float dialog_width(std::string_view kind) {` | kit.js 373. |
| `libs/ui/src/win98.cpp:203` | `std::string dos_path(std::string_view path, std::string_view home) {` | kit.js 431-437. herdr's paths start with "/" (no drive); a Windows path that has a drive keeps |
| `libs/ui/src/win98.cpp:204` | `std::string dos_path(std::string_view path, std::string_view home) {` | it, one that does not gets herdr's "C:". |
| `libs/ui/src/win98.cpp:234` | `std::string user_name(std::string_view home) {` | taskbar.js 61-65. |
| `libs/ui/src/win98.cpp:255` | `std::string run_name(std::string_view command) {` | dialog.js 227: cmd.split(/\s+/)[0].split("/").pop(); "\" is a separator here too. |
| `libs/ui/src/win98.cpp:268` | `std::vector<std::string> run_history(std::vector<std::string> history, const std::string& ` | dialog.js 224. |
| `libs/ui/src/win98.cpp:281` | `std::string clock_text(const DateTime& d) {` | taskbar.js 36-42. |
| `libs/ui/src/win98.cpp:296` | `std::string long_date(const DateTime& d) {` | taskbar.js 44-46 (en-US long date). |
| `libs/ui/src/win98.cpp:304` | `std::string time_text(const DateTime& d) {` | dialog.js 687: toLocaleTimeString("en-US"). |
| `libs/ui/src/win98.cpp:311` | `std::string time_zone_text(const DateTime& d) {` | dialog.js 688-691. |
| `libs/ui/src/win98.cpp:318` | `std::vector<std::array<int, 7>> calendar(int year, int month) {` | dialog.js 656-666. |
| `libs/ui/src/win98.cpp:346` | `std::vector<ClockDot> clock_face(const DateTime& d) {` | dialog.js 927-958. |
| `libs/ui/src/win98.cpp:377` | `if (!level(options_.level))` | mines.js 27 |
| `libs/ui/src/win98.cpp:382` | `std::optional<std::array<int, 3>> Mines::level(std::string_view name) {` | mines.js 6. |
| `libs/ui/src/win98.cpp:393` | `void Mines::reset() {` | mines.js 34-42. |
| `libs/ui/src/win98.cpp:407` | `std::vector<int> Mines::neighbors(int i) const {` | mines.js 44-51. |
| `libs/ui/src/win98.cpp:418` | `void Mines::start(int i, long long now_ms) {` | mines.js 53-69. |
| `libs/ui/src/win98.cpp:444` | `std::string Mines::reveal(int i, long long now_ms) {` | mines.js 71-104. |
| `libs/ui/src/win98.cpp:493` | `std::string Mines::chord(int i, long long now_ms) {` | mines.js 106-112. |
| `libs/ui/src/win98.cpp:512` | `void Mines::mark(int i) {` | mines.js 173. |
| `libs/ui/src/win98.cpp:521` | `bool Mines::tick(long long now_ms) {` | mines.js 64-68. |
| `libs/ui/src/win98.cpp:532` | `void Mines::request(const std::string& cmd) {` | mines.js 114-123. |
| `libs/ui/src/win98.cpp:560` | `std::string Mines::face() const {` | mines.js 185. |
| `libs/ui/src/win98.cpp:569` | `std::string Mines::best_text() const {` | mines.js 122. |
| `libs/ui/src/win98.cpp:585` | `std::string counter_chars(int value) {` | mines.js 16. |
| `libs/ui/src/win98.cpp:592` | `std::array<bool, 7> segments_lit(char ch) {` | mines.js 8, 18. |
| `libs/ui/src/win98.cpp:630` | `void Solitaire::deal() {` | sol.js 223-234 (with stop(), 235-238). |
| `libs/ui/src/win98.cpp:667` | `std::vector<int> Solitaire::cards(const Loc& loc) const {` | sol.js 50. |
| `libs/ui/src/win98.cpp:683` | `bool Solitaire::valid_pick(const Loc& loc) const {` | sol.js 51-59. |
| `libs/ui/src/win98.cpp:702` | `void Solitaire::save() {` | sol.js 48. |
| `libs/ui/src/win98.cpp:708` | `void Solitaire::begin() {` | sol.js 39-47: the clock starts (the caller ticks it every second). |
| `libs/ui/src/win98.cpp:715` | `void Solitaire::tick_second() {` | sol.js 42-46. |
| `libs/ui/src/win98.cpp:722` | `bool Solitaire::move(const Loc& from, const Loc& to) {` | sol.js 60-75. |
| `libs/ui/src/win98.cpp:771` | `void Solitaire::foundation(const Loc& loc) {` | sol.js 76-79. |
| `libs/ui/src/win98.cpp:780` | `void Solitaire::stock() {` | sol.js 80-88. |
| `libs/ui/src/win98.cpp:803` | `Solitaire::Down Solitaire::down(const Loc& loc) {` | sol.js 89-100. |
| `libs/ui/src/win98.cpp:831` | `std::optional<Solitaire::Loc> Solitaire::target(float x, float y) {` | sol.js 105-109. |
| `libs/ui/src/win98.cpp:846` | `void Solitaire::victory() {` | sol.js 110-112. |
| `libs/ui/src/win98.cpp:859` | `bool Solitaire::animate() {` | sol.js 113-127. |
| `libs/ui/src/win98.cpp:891` | `void Solitaire::command(const std::string& cmd) {` | sol.js 132-140 (the commands that change the game). |
| `libs/ui/src/win98.cpp:918` | `float Solitaire::tab_step(int i) const {` | sol.js 219-220. |
| `libs/ui/src/workspace.cpp:66` | `bool remove_leaf(Node& n, const std::string& s) {` | Removes leaf `s`. herdr layout.rs: its share goes back to the siblings in proportion, and a |
| `libs/ui/src/workspace.cpp:297` | `std::string folder_of(const std::string& cwd) {` | herdr model.rs 635-647 (folder_name): the last folder; nothing or the home folder reads "~". |
| `libs/ui/src/workspace.cpp:421` | `int index = -1;` | herdr layout.rs 598-629: when the target's container already runs along that axis, the new |
| `libs/ui/src/workspace.cpp:466` | `const auto rest = sessions_in(tab);` | herdr model.rs 403-419: back to the pane focused before it, else the first. |
| `libs/ui/src/workspace.cpp:515` | `if (sp.active_tab > tab)` | herdr: closing the shown tab shows the one that slides into its slot, else the last. |
| `libs/ui/src/workspace.cpp:558` | `if (t.focused != session && !t.focused.empty()) {` | herdr Tab::focus history |
| `libs/ui/src/workspace.cpp:567` | `t.zoomed = session;` | herdr: the zoom moves to the focused pane |
| `libs/ui/src/workspace.cpp:600` | `int next = -1;` | The nearest tab still shown, preferring the right (herdr selects the one that slides in). |
| `libs/ui/src/workspace.cpp:636` | `const int n = static_cast<int>(sp->tabs.size());` | Wraps and skips tabs put away (herdr tab.next / tab.prev). |
| `libs/ui/src/workspace.cpp:662` | `focus(*next);` | a zoomed tab moves its zoom along (herdr model.rs 131-133) |
| `libs/ui/src/workspace.cpp:725` | `if (!pane.empty()) {` | herdr pane.zoom {pane}: that pane, focused |
| `libs/ui/src/workspace.cpp:748` | `for (const auto& t : sp.tabs) {` | herdr model.rs 271-279: the first terminal's checkout top (or its folder outside git), |
| `libs/ui/src/workspace.cpp:874` | `return false;` | herdr: a space moves only inside its machine's block |
| `libs/ui/src/workspace.cpp:891` | `if (session_top != nullptr && space_title(static_cast<int>(i), *session_top) == "~")` | herdr spaces.js 526: the home space ("~") waits for a real name. |
| `libs/ui/src/workspace.cpp:995` | `auto& e = entries_[session];` | herdr model.rs 529-572: to blocked/error from anything else always alerts (also while |
| `libs/ui/src/workspace.cpp:1092` | `std::vector<int> nest_worktrees(const std::vector<SpaceRepo>& info, std::vector<bool>* tre` | ------------------------------------------------------------------ spaces list (herdr spaces.js) |
| `libs/ui/src/workspace.cpp:1094` | `std::set<std::string> mains;` | spaces.js 298-315: mains are repos with a main checkout; a worktree of one goes under the |
| `libs/ui/src/workspace.cpp:1126` | `namespace {` | ------------------------------------------------------------------ agents list (herdr agents.js) |
| `libs/ui/src/workspace.cpp:1145` | `bool title_start(char32_t c) {` | \p{L}, \p{N}, \p{Ps} or \p{Pi}: what a title may start with (agents.js 31). An approximation by |
| `tools/package/package.cpp:93` | `dk::copy_tree(bin / L"win98", stage / L"win98");` | Herdr 98 (herdr-app defaults/mods/win98): fonts, icons, images, sounds, the layout file. |
| `tools/package/package.cpp:95` | `dk::copy_tree(bin / L"programs", stage / L"programs");` | herdr's program table and icons (defaults/programs.json, defaults/icons). |
| `tools/package/package.cpp:97` | `dk::copy_tree(bin / L"themes", stage / L"themes");` | herdr defaults/themes (atom-one-dark) |
| `tests/fixtures/fake_agent_ui.cpp:1` | `` | A stand-in agent-ui.exe for the update tests (herdr update.rs 729-753 writes a shell script |
| `tests/fixtures/fake_agent_ui.cpp:2` | `` | that echoes "herdr-app 0.1.8"). `--version` answers "agent-ui <fake-version.txt next to it>"; |
| `tests/integration/windows/conpty_compare_test.cpp:197` | `TEST_P(ConPtyCompareTest, Win32InputModeShiftEnterReachesProgram) {` | herdr src/win32_keys.rs: ConPTY asks its viewer for win32-input-mode; the engine notices and |
| `tests/integration/windows/handover_test.cpp:1` | `` | agent-host's self-update (design doc 7.4 OQ-09; herdr src/update.rs 236: "terminals keep |
| `tests/integration/windows/host_test.cpp:810` | `TEST_F(HostFixture, LiveCwdFollowsCd) {` | live_cwd follows cd in cmd (real current folder) and PowerShell (prompt hook, herdr-style). |
| `tests/integration/windows/host_test.cpp:857` | `git({L"branch", L"existing"});` | A branch that exists is checked out (show-ref, not git's wording; herdr worktree/main.js). |
| `tests/integration/windows/host_test.cpp:907` | `TEST_F(HostFixture, SubAgentTaskReportAndWait) {` | Agent collaboration (herdr-style helpers): a sub-agent records its parent, gets its first task |
| `tests/integration/windows/updates_test.cpp:1` | `` | Ports of herdr-app src/update.rs tests that run programs (curl.exe, tar.exe, a stand-in |
| `tests/integration/windows/updates_test.cpp:2` | `` | agent-ui.exe) against an isolated release server on 127.0.0.1 (update.rs 676-753). Nothing |
| `tests/integration/windows/updates_test.cpp:156` | `TEST(Updates, FailedCheckClearsSuccessAndRecoveryRecordsANewTime) {` | update.rs 676-714: an isolated release server drops the first connection, then recovers. |
| `tests/integration/windows/updates_test.cpp:175` | `TEST(Updates, StagesNeedAReceiptAndAMatchingExecutable) {` | update.rs 727-753 |
| `tests/integration/windows/updates_test.cpp:199` | `TEST(Updates, ANewerReleaseIsStagedThenSwapped) {` | update.rs 264-325: a newer release is downloaded, checked, unpacked and staged with a receipt; |
| `tests/integration/windows/updates_test.cpp:232` | `TEST(Updates, AChecksumMismatchStagesNothing) {` | update.rs 313-316: a damaged download is refused and nothing is staged. |
| `tests/integration/windows/updates_test.cpp:253` | `` | herdr: `hc update` / Settings > Updates "Update herdr-core" (update.rs 236-262, core_bin.rs |
| `tests/integration/windows/updates_test.cpp:254` | `` | 242-265, frame/settings/main.js 794-805); here `agentctl update` and updates::update_core |
| `tests/integration/windows/updates_test.cpp:399` | `TEST(CoreUpdate, AgentctlUpdateHandsTheTerminalsToTheRelease) {` | `agentctl update --json` (herdr `hc update --json`): finds the release, puts it in place, the |
| `tests/integration/windows/updates_test.cpp:437` | `TEST(CoreUpdate, UpdateCoreGoesFromUpdatingToDoneOrError) {` | "The update failed: <why>" (main.js 794-805) with everything left as it was. |
| `tests/unit/chrome_logic_test.cpp:9` | `TEST(MenuNav, FirstRowHighlightedArrowsWrapEnterRuns) {` | herdr menu/main.js 131-137. |
| `tests/unit/chrome_logic_test.cpp:39` | `TEST(Picker, BusyHasAnyStatusBlockedFirstEarlierAlwaysShown) {` | herdr picker/main.js 86-98. |
| `tests/unit/chrome_logic_test.cpp:75` | `TEST(Keymap, ListingKeepsTheFileAsWritten) {` | herdr keys.rs listing() and help/main.js label(). |
| `tests/unit/input_mapping_test.cpp:75` | `TEST(InputMapping, SelectionTextJoinsSoftWrapsAndTrims) {` | herdr src/vt.rs:1158-1163 (unwrap, trim): soft-wrapped rows join, trailing blank lines go. |
| `tests/unit/input_mapping_test.cpp:99` | `TEST(InputMapping, QuotePathsForPaste) {` | herdr src/terminal/drag_paths.rs quote_paths (Windows): plain paths as they are, the rest in |
| `tests/unit/input_mapping_test.cpp:110` | `TEST(Links, FindsUrlsAndPaths) {` | herdr src/links.rs (Ghostty's pattern): URLs without trailing punctuation, rooted and bare |
| `tests/unit/layout_test.cpp:1` | `` | herdr-app's layout engine as ported to agentdesk::ui::layout. The tests named after herdr's are |
| `tests/unit/layout_test.cpp:2` | `` | ports of src/layout.rs 754-915, src/layout_sync.rs 315-420 and src/window/drag_tests.rs. |
| `tests/unit/layout_test.cpp:50` | `TEST(Layout, FloatsSnapToTheNearestEdgeAndThird) {` | layout.rs 758-765 |
| `tests/unit/layout_test.cpp:59` | `TEST(Layout, FloatsRoundTrip) {` | layout.rs 767-776 |
| `tests/unit/layout_test.cpp:70` | `TEST(Layout, NodesCarryLooksBackgroundsPaddingAndShadows) {` | layout.rs 782-798 |
| `tests/unit/layout_test.cpp:89` | `TEST(Layout, TabBlocksSayHowTheirPanesAreFramed) {` | layout.rs 800-812 |
| `tests/unit/layout_test.cpp:105` | `TEST(Layout, DocumentsParseWithTypesSizesAndLooks) {` | layout.rs 814-828 |
| `tests/unit/layout_test.cpp:121` | `TEST(Layout, SplitsReuseASameAxisParentAndWrapOtherwise) {` | layout.rs 830-840 |
| `tests/unit/layout_test.cpp:134` | `TEST(Layout, ClosingASplitGivesItsShareBackToThePaneItCameFrom) {` | layout.rs 842-861 |
| `tests/unit/layout_test.cpp:151` | `TEST(Layout, RemovingCollapsesSingleChildrenButKeepsStacksAndTheRoot) {` | layout.rs 863-883 |
| `tests/unit/layout_test.cpp:172` | `TEST(Layout, RemovingACompositionSlotKeepsAuthoredContainers) {` | layout.rs 885-895 |
| `tests/unit/layout_test.cpp:184` | `TEST(Layout, StacksSelectAndHiddenChildrenFallBack) {` | layout.rs 897-904 |
| `tests/unit/layout_test.cpp:194` | `TEST(Layout, SwappingKeepsSizesWithPlaces) {` | layout.rs 906-914 |
| `tests/unit/layout_test.cpp:217` | `const Node n = node(R"({"type": "block", "content": {"context": "active"}, "colour": "red"` | `context` is 0.1.0's word for `tab` (layout.rs 23-24); unknown keys are left out. |
| `tests/unit/layout_test.cpp:223` | `TEST(Layout, ShippedLayoutsParseAndNameEveryNode) {` | window.rs 7568-7576 / layout_sync.rs 395-418: the shipped layouts parse, name every node, and |
| `tests/unit/layout_test.cpp:224` | `TEST(Layout, ShippedLayoutsParseAndNameEveryNode) {` | are the files in third_party/herdr/layouts. |
| `tests/unit/layout_test.cpp:266` | `TEST(LayoutSize, PxFirstThenWeightsShareWhatIsLeftWithinMinAndMax) {` | pane_size.rs 49-85 |
| `tests/unit/layout_test.cpp:278` | `b.max = 50.0f;` | free), as flexbox gives it (pane_size.rs 67-68), not all of what b left. |
| `tests/unit/layout_test.cpp:348` | `TEST(LayoutDrag, APxSideTakesTheDragAndWeightsSplitTheirSum) {` | window.rs 4671-4723 |
| `tests/unit/layout_test.cpp:434` | `TEST(LayoutFloat, HomeSitsMarginInFromItsEdge) {` | floats.rs 77-96 |
| `tests/unit/layout_test.cpp:442` | `TEST(LayoutSync, DesktopWallpaperAndPixelFitAreClicksNotStructureEdits) {` | layout_sync.rs 324-347 |
| `tests/unit/layout_test.cpp:466` | `TEST(LayoutSync, ClicksAreKeptApartAndAnEditWins) {` | layout_sync.rs 349-373 |
| `tests/unit/layout_test.cpp:493` | `TEST(LayoutSync, AnEditMadeWhileClosedBeatsAnOlderClick) {` | layout_sync.rs 375-393 |
| `tests/unit/programs_test.cpp:1` | `` | herdr-app src/programs.rs tests (Apache-2.0), ported. |
| `tests/unit/sprites_test.cpp:1` | `` | Ported from herdr-app src/sprites.rs tests (lines 340-516). herdr's two rendered tests run |
| `tests/unit/sprites_test.cpp:28` | `TEST(Sprites, BraillePatternsUseAllEightBitsAndStayInTheCell) {` | herdr sprites.rs:344-370 |
| `tests/unit/sprites_test.cpp:65` | `TEST(Sprites, BrailleMatchesDevicePixelLayout) {` | herdr sprites.rs:372-417 (rendered_braille_matches_device_pixel_layout): the dots land on the |
| `tests/unit/sprites_test.cpp:94` | `TEST(Sprites, TablesCoverTheirRanges) {` | herdr sprites.rs:419-439 |
| `tests/unit/sprites_test.cpp:116` | `TEST(Sprites, RoundedCornersJoinStraightLines) {` | herdr sprites.rs:441-503 (rendered_rounded_corners_join_straight_lines): each arc endpoint |
| `tests/unit/sprites_test.cpp:118` | `TEST(Sprites, RoundedCornersJoinStraightLines) {` | half-sized cells herdr includes. |
| `tests/unit/sprites_test.cpp:168` | `TEST(Sprites, RoundedCornersAndDashesHaveTheirOwnShapes) {` | herdr sprites.rs:505-515 |
| `tests/unit/telemetry_test.cpp:1` | `` | Ports of herdr-app src/telemetry.rs tests (telemetry.rs 699-742), plus its buckets. |
| `tests/unit/telemetry_test.cpp:8` | `TEST(Telemetry, DatesAreUtcCalendarDays) {` | telemetry.rs 703-709 |
| `tests/unit/telemetry_test.cpp:16` | `TEST(Telemetry, VersionsAreReleasesOrOther) {` | telemetry.rs 711-720 |
| `tests/unit/telemetry_test.cpp:27` | `TEST(Telemetry, AReportHoldsNoFreeText) {` | telemetry.rs 722-741 |
| `tests/unit/telemetry_test.cpp:55` | `TEST(Telemetry, ReportShape) {` | telemetry.rs 234-270: the keys in the order herdr writes them. |
| `tests/unit/telemetry_test.cpp:80` | `TEST(Telemetry, Buckets) {` | telemetry.rs 273-302 |
| `tests/unit/telemetry_test.cpp:96` | `TEST(Telemetry, AgentNamesComeFromTheTable) {` | programs.rs 212-237 |
| `tests/unit/telemetry_test.cpp:106` | `TEST(Telemetry, DayRoundTrips) {` | telemetry.rs 187-190: today.json reads back as it was written. |
| `tests/unit/updates_test.cpp:1` | `` | Ports of herdr-app src/update.rs tests (update.rs 637-903) that need no child process. |
| `tests/unit/updates_test.cpp:30` | `TEST(Updates, VersionsCompareLikeSemver) {` | update.rs 872-880 |
| `tests/unit/updates_test.cpp:58` | `TEST(Updates, Sha256IsHex) {` | update.rs 899-902 |
| `tests/unit/updates_test.cpp:63` | `TEST(Updates, FailedCheckClearsSuccessAndRecoveryRecordsANewTime) {` | update.rs 676-714 (the part about the recorded time; the request half is an integration test). |
| `tests/unit/updates_test.cpp:73` | `TEST(Updates, StartupAppliesOnlyANewerVersionAndNeverRelaunchesTwice) {` | update.rs 716-725 |
| `tests/unit/updates_test.cpp:84` | `TEST(Updates, FailedInstallRestoresThePreviousVersion) {` | update.rs 778-790 |
| `tests/unit/updates_test.cpp:100` | `TEST(Updates, StagesLiveBesideTheInstall) {` | update.rs 50-59: next to the install, by name. |
| `tests/unit/updates_test.cpp:108` | `TEST(Updates, OnlyTheZipFolderIsAnInstall) {` | update.rs 34-45: only a folder from the zip (it holds THIRD_PARTY_NOTICES.md) is an install. |
| `tests/unit/vt_engine_test.cpp:246` | `EXPECT_EQ(e.take_pty_input(), "a\rb\rc");` | herdr src/vt.rs:1631: line breaks are Enter |
| `tests/unit/vt_engine_test.cpp:270` | `TEST(VtEngine, Win32InputModeEncodesModifiedEnterAndEscape) {` | herdr src/win32_keys.rs: once ConPTY asks for win32-input-mode (even split across writes), |
| `tests/unit/vt_engine_test.cpp:400` | `TEST(VtEngine, CellsRememberWhichOfTheSixteenAnsiColoursTheyUse) {` | A theme's own 16 ANSI colours (herdr themes "ansi", Herdr 98) need the index, not just the RGB. |
| `tests/unit/win98_test.cpp:1` | `` | Herdr 98's rules (herdr-app defaults/mods/win98: kit.js, taskbar.js, dialog.js, mines.js, |
| `tests/unit/win98_test.cpp:36` | `TEST(Win98Kit, BevelLayersFromOutsideIn) {` | kit.js 84-95. |
| `tests/unit/win98_test.cpp:51` | `TEST(Win98Kit, PixelsAndScale) {` | kit.js 40-51, 451-452. |
| `tests/unit/win98_test.cpp:64` | `TEST(Win98Kit, AccessKeys) {` | kit.js 55-75, menu.js 326-327. |
| `tests/unit/win98_test.cpp:76` | `TEST(Win98Kit, MenuSize) {` | kit.js 326-344. |
| `tests/unit/win98_test.cpp:91` | `TEST(Win98Kit, PathsAndNames) {` | kit.js 431-437, taskbar.js 61-65, dialog.js 224-227. |
| `tests/unit/win98_test.cpp:111` | `TEST(Win98Kit, ClockTexts) {` | taskbar.js 36-46, dialog.js 655-691. |
| `tests/unit/win98_test.cpp:142` | `TEST(Win98Kit, ClockFaceTicks) {` | dialog.js 940-946: twelve 4 px ticks on a radius of 40 around (48, 48), navy every third. |
| `tests/unit/win98_test.cpp:155` | `TEST(Win98Mines, Counter) {` | mines.js 8-21. |
| `tests/unit/win98_test.cpp:172` | `TEST(Win98Mines, FirstRevealIsSafeAndStartsTheClock) {` | mines.js 6, 34-69: the first click is never a mine, nor next to one. |
| `tests/unit/win98_test.cpp:244` | `TEST(Win98Mines, ChordAndRequests) {` | mines.js 106-123. |
| `tests/unit/win98_test.cpp:279` | `TEST(Win98Solitaire, DealSevenColumns) {` | sol.js 223-234. |
| `tests/unit/win98_test.cpp:295` | `TEST(Win98Solitaire, RulesAndScoring) {` | sol.js 51-88. |
| `tests/unit/win98_test.cpp:342` | `TEST(Win98Solitaire, TargetsAndVictory) {` | sol.js 105-109, 113-127, 139. |
| `tests/unit/win98_test.cpp:367` | `TEST(Win98Float, SnapsToTheNearestEdgeAndThird) {` | herdr src/layout.rs 759-765 (its own test) and src/window/floats.rs 418-421. |
| `tests/unit/win98_test.cpp:385` | `TEST(Win98Float, TheSpringOvershootsThenSettles) {` | herdr floats.rs 410-415. |
| `tests/unit/workspace_test.cpp:98` | `TEST(Workspace, ZoomTogglesAndMovesWithFocus) {` | herdr model.rs 131-133: focusing another pane of a zoomed tab moves the zoom to it. |
| `tests/unit/workspace_test.cpp:225` | `auto bad = k.apply({{"global", {{"ctrl-shift-t", nullptr}, {"??x", "a"}}}});` | A file with a problem installs nothing (herdr keys.rs): the defaults stay. |
| `tests/unit/workspace_test.cpp:236` | `TEST(Keymap, HerdrSectionsObjectsAndValidation) {` | herdr keys.json forms: layout sections win over global, "terminal" gives the key to the |
| `tests/unit/workspace_test.cpp:261` | `TEST(Workspace, SpaceColorsAndMoves) {` | herdr spaces: colours go to the least-used name, spaces move only within their machine. |
| `tests/unit/workspace_test.cpp:285` | `TEST(Workspace, MovePaneToEdgeOrSwap) {` | herdr pane drag: drop on an edge moves the pane there; in the middle the two swap. |
| `tests/unit/workspace_test.cpp:306` | `TEST(Attention, HerdrAlertRules) {` | herdr: needs-you alerts even while looking; error counts as needs you; a first listing that |
| `tests/unit/workspace_test.cpp:320` | `TEST(Workspace, SettledTabsAreSkippedUntilShown) {` | herdr tab.settle: a settled tab leaves the bar and tab cycling, keeps its terminals, and comes |
| `tests/unit/workspace_test.cpp:352` | `TEST(Keymap, DefaultFileAppliesCleanly) {` | herdr writes its defaults as keys.json when the file is missing: that file must apply cleanly |
| `tests/unit/workspace_test.cpp:364` | `TEST(Workspace, HerdrSplitCloseAndTabRules) {` | herdr layout.rs / model.rs rules: splitting along the container's axis adds a sibling sharing |
| `tests/unit/workspace_test.cpp:404` | `TEST(Workspace, ZoomFollowsFocus) {` | herdr: focusing another pane of a zoomed tab moves the zoom; pane.go works while zoomed. |
| `tests/unit/workspace_test.cpp:416` | `TEST(Workspace, SpaceTitleUsesCheckoutTopThenCreationFolder) {` | herdr model.rs 271-279: the first terminal's checkout top, else the space's own folder; home is "~". |
| `tests/unit/workspace_test.cpp:446` | `TEST(Workspace, WorktreesNestUnderMainCheckoutAndModelFollows) {` | herdr spaces.js 298-334: worktrees go right after their repo's main checkout, and the model |
