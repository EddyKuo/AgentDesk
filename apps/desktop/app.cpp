// AgentDesk window contents: spaces, tabs, split panes, agent attention, notifications, the
// command palette and worktrees, drawn with AgentDesk's own look (ADR-12/ADR-13).
//
// This file: App construction and teardown and its small public members.
// Portions adapted from herdr-app (Apache-2.0) and modified; see THIRD_PARTY_NOTICES.md.
#include "app_internal.hpp"

namespace agentdesk::desktop {
namespace detail {

// ------------------------------------------------------------------ construction
// keys.json: installed from the defaults when missing; read again whenever it changes on disk;
// problems go to keys.error (removed once the file applies) and the defaults stay in force.
void load_keys(App::State& s) {
    const auto path = data_dir() / L"keys.json";
    const auto err_path = data_dir() / L"keys.error";
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        std::filesystem::create_directories(path.parent_path(), ec);
        std::ofstream(path, std::ios::binary) << ui::Keymap::default_file().dump(2) << "\n";
    }
    s.keys_mtime = std::filesystem::last_write_time(path, ec);
    s.keymap = ui::Keymap::defaults();
    s.keymap_errors.clear();
    const auto j = Json::parse(read_text_file(path), nullptr, false);
    if (j.is_discarded())
        s.keymap_errors.emplace_back("keys.json is not valid JSON");
    else
        s.keymap_errors = s.keymap.apply(j, known_command);
    if (s.keymap_errors.empty()) {
        std::filesystem::remove(err_path, ec);
    } else {
        std::ofstream out(err_path, std::ios::binary);
        for (const auto& e : s.keymap_errors)
            out << e << "\n";
    }
}

namespace {
// For telemetry's mod counts: of the enabled mods, the shipped ones as shipped (by name), how
// many are shipped but edited, and how many are the person's own. Shipped: the example mods
// next to agent-ui.exe (write_example_mods); as shipped: the same mod.dll and mod.json bytes.
std::tuple<std::vector<std::string>, std::size_t, std::size_t> mod_kinds(const std::set<std::string>& names) {
    const auto shipped = exe_dir() / L"mods";
    std::vector<std::string> builtin;
    std::size_t edited = 0;
    std::size_t custom = 0;
    for (const auto& name : names) {
        std::error_code ec;
        const auto from = shipped / win::to_wide(name);
        if (!std::filesystem::is_regular_file(from / L"mod.dll", ec)) {
            ++custom;
            continue;
        }
        const auto have = data_dir() / L"mods" / win::to_wide(name);
        bool same = true;
        for (const wchar_t* f : {L"mod.dll", L"mod.json"}) {
            bool a_ok = false;
            bool b_ok = false;
            const std::string a = read_text_file(from / f, &a_ok);
            const std::string b = read_text_file(have / f, &b_ok);
            same = same && a_ok == b_ok && a == b;
        }
        if (same)
            builtin.push_back(name);
        else
            ++edited;
    }
    return std::make_tuple(builtin, edited, custom);
}

// What telemetry.cpp asks the app.
ui::telemetry::Config telemetry_config(App::State& s) {
    ui::telemetry::Config c;
    c.data_dir = data_dir();
    c.app_version = AGENTDESK_VERSION_STRING;
    // The layouts and themes AgentDesk ships; any other name counts as the person's own.
    c.shipped_layouts = {"workspace", "terminals"};
    for (const auto& p : builtin_palettes())
        c.shipped_themes.emplace_back(p.id);
    for (const auto& [name, pal] : preset_palettes())
        c.shipped_themes.emplace_back(std::string(kPresetPrefix) + name);
    // The shipped theme files (third_party/herdr/themes and libs/ui/themes).
    for (const char* id :
         {"atom-one-dark", "mac-dusk", "mac-dawn", "phosphor-green", "phosphor-amber", "phosphor-paper"})
        c.shipped_themes.emplace_back(id);
    c.mod_kinds = mod_kinds;
    // keys.json differs from the shipped one.
    c.keys_custom = [] {
        bool ok = false;
        const std::string text = read_text_file(data_dir() / L"keys.json", &ok);
        if (!ok)
            return false;
        return Json::parse(text, nullptr, false) != ui::Keymap::default_file();
    };
    c.core_version = [] { return ui::updates::core().version; };
    c.refresh = s.config.wake;
    return c;
}

}  // namespace

// What telemetry counts: how much is open, never what is in it.
void observe(App::State& s) {
    if (!ui::telemetry::collecting())
        return;
    ui::telemetry::Observation o;
    o.foreground = s.window_focused;
    // Every terminal reporting an agent, named by what runs there (`claude`, `codex`): agent-host
    // does not pass the agent's own OSC 7501 name on.
    for (const auto& [id, v] : s.views_by_id) {
        if (v.agent_state == "Unknown" || v.lost)
            continue;
        o.agents.push_back(v.program.empty() ? std::nullopt : std::optional<std::string>(v.program));
        if (v.agent_state == "Working")
            ++o.agents_working;
    }
    o.spaces = s.ws.spaces.size();
    for (const auto& sp : s.ws.spaces) {
        o.tabs += sp.tabs.size();
        for (const auto& t : sp.tabs)
            o.terminals += s.ws.sessions_in(t).size();
    }
    o.layout = s.settings.layout;
    o.mods.assign(s.settings.mods_enabled.begin(), s.settings.mods_enabled.end());
    o.theme = g_pal->id;
    if (!s.settings.welcomed.empty())
        o.welcome = s.settings.welcomed;
    ui::telemetry::observe(std::move(o));
}

// A closed agent-host update error stays closed only while that same error is shown; a new
// error, or no update and no error, forgets it.
void reconcile_core_update_error(std::optional<std::string>& dismissed, const ui::updates::CoreInfo& core) {
    if ((core.error && dismissed != core.error) || (!core.updating && !core.error))
        dismissed.reset();
}

namespace {
// ui.json kept the workspace's sidebar, panel and list sizes before the layout engine; they are
// now its clicks in layouts\.state\workspace.json (seed_from_settings). Written once, then ui.json
// forgets them so a later reload does not bring them back.
void move_sizes_to_layout_state(App::State& s) {
    const Settings d;
    Settings& st = s.settings;
    if (st.sidebar_width == d.sidebar_width && st.panel_width == d.panel_width &&
        st.spaces_share == d.spaces_share && st.sidebar == d.sidebar)
        return;
    for (const auto& l : s.layouts)
        if (l.name == "workspace")
            save_layout_state(l);
    st.sidebar_width = d.sidebar_width;
    st.panel_width = d.panel_width;
    st.spaces_share = d.spaces_share;
    st.sidebar = d.sidebar;
    save_settings(st);
}

// Files / Changes and git for a pane on another machine: a blocking call to that machine's
// agent-host, made from a worker thread.
RemoteCall remote_call(App::State* st) {
    return [st](const std::string& machine, const std::string& method,
                const Json& params) -> std::optional<Json> {
        std::shared_ptr<ui::HostClient> h;
        {
            std::lock_guard lock(st->remotes_mutex);
            for (const auto& r : st->remotes)
                if (r.cfg.name == machine)
                    h = r.host;
        }
        if (!h || !h->connected())
            return std::nullopt;
        auto done = std::make_shared<std::promise<std::optional<Json>>>();
        auto result = done->get_future();
        // git worktree add / remove may take minutes: 10 minutes for those, else 1.
        const auto args = params.find("args");
        const bool slow = method == "git.run" && args != params.end() && args->is_array() && !args->empty() &&
                          (*args)[0] == "worktree";
        const auto wait = std::chrono::seconds(slow ? 600 : 60);
        h->call(
            method, params,
            [done](Json r, std::string error) {
                done->set_value(error.empty() ? std::optional<Json>(std::move(r)) : std::nullopt);
            },
            wait);
        if (result.wait_for(wait + std::chrono::seconds(5)) != std::future_status::ready)
            return std::nullopt;
        return result.get();
    };
}

// The welcome on the first run (until it is finished or skipped), else What's new once after an
// update. Neither in automated runs.
void show_first_dialogs(App::State& s) {
    if (s.config.automated)
        return;
    if (s.settings.welcomed.empty()) {
        s.show_welcome = true;
        s.settings.seen_version = AGENTDESK_VERSION_STRING;  // nothing is new on a first run
        save_settings(s.settings);
    } else if (s.settings.seen_version != AGENTDESK_VERSION_STRING) {
        s.show_notes = true;
        s.notes_kind = "whats_new";
        s.settings.seen_version = AGENTDESK_VERSION_STRING;
        save_settings(s.settings);
    }
}

}  // namespace
}  // namespace detail

App::App(AppConfig config, std::unique_ptr<ui::HostClient> host) : s_(std::make_unique<State>()) {
    g_program_images = &s_->program_images;
    s_->config = std::move(config);
    s_->host = std::move(host);
    s_->settings = load_settings();
    g_pal = palette_by_id(s_->settings.theme);
    s_->folded_machines.insert(s_->settings.folded_machines.begin(), s_->settings.folded_machines.end());
    s_->keymap = ui::Keymap::defaults();
    load_keys(*s_);
    std::string cwd = s_->settings.last_cwd;
    if (cwd.empty()) {
        const auto u8 = std::filesystem::current_path().u8string();
        cwd.assign(u8.begin(), u8.end());
    }
    std::snprintf(s_->launch_cwd, sizeof s_->launch_cwd, "%s", cwd.c_str());
    start_remotes(*s_);
    write_example_mods();
    s_->layouts = load_layouts(s_->settings);
    move_sizes_to_layout_state(*s_);
    s_->panels->remote = remote_call(s_.get());
    s_->git.remote = s_->panels->remote;
    s_->mods = std::make_unique<ui::ModHost>(data_dir() / L"mods");
    s_->mods->load(s_->settings.mods_enabled);
    // A finished update first, then the update checker, then telemetry.
    ui::updates::after_update();
    ui::updates::start();
    ui::telemetry::start(telemetry_config(*s_), s_->settings.telemetry);
    show_first_dialogs(*s_);
}
namespace detail {
void start_remotes(App::State& s) {
    std::vector<Remote> keep;
    for (const auto& cfg : s.settings.machines) {
        auto it = std::find_if(s.remotes.begin(), s.remotes.end(), [&](const Remote& r) {
            return r.cfg.name == cfg.name && r.cfg.target == cfg.target && r.cfg.ctl == cfg.ctl;
        });
        if (it != s.remotes.end()) {
            keep.push_back(std::move(*it));
            continue;
        }
        ui::HostClientOptions o;
        o.wake = s.config.wake;
        o.connector = [cfg] {
            // "pipe:<name>": another agent-host on this PC through a local bridge (tests, a second
            // host under another data folder); anything else is an ssh target.
            if (cfg.target.rfind("pipe:", 0) == 0) {
                return ipc::connect_over_command(
                    {ipc::sibling_executable(L"agentctl.exe").native(), L"--pipe",
                     win::to_wide(cfg.target.substr(5)), L"--no-start", L"bridge"},
                    std::chrono::seconds(10));
            }
            return ipc::connect_over_command(ipc::ssh_bridge_argv(cfg.target, cfg.ctl),
                                             std::chrono::seconds(20));
        };
        keep.push_back({cfg, std::make_unique<ui::HostClient>(o)});
    }
    std::vector<Remote> old;
    {
        std::lock_guard lock(s.remotes_mutex);
        old = std::exchange(s.remotes, std::move(keep));
    }
    old.clear();  // removed machines disconnect here, outside the lock
}
}  // namespace detail

bool App::binds_chord(int chord) const {
    return s_->keymap.global(static_cast<ImGuiKeyChord>(chord)).has_value();
}

void App::run_chord(int chord) {
    if (const auto cmd = s_->keymap.global(static_cast<ImGuiKeyChord>(chord)))
        s_->chord_commands.push_back(*cmd);
}

App::~App() {
    // HostClient threads answer into State (session.create, worktree.add and shutdown_host lock
    // placements_mutex and write its fields). Members go in reverse order, so host and remotes
    // (declared first) would outlive that mutex: end them first, while everything is there.
    std::vector<Remote> remotes;
    {
        std::lock_guard lock(s_->remotes_mutex);
        remotes.swap(s_->remotes);
    }
    remotes.clear();
    s_->host.reset();
    g_program_images = nullptr;
}

std::filesystem::path data_folder() {
    return data_dir();
}

float saved_zoom() {
    return load_settings().zoom;
}

// The loop ended: telemetry's clean quit, then a downloaded update goes in place (AgentDesk is
// not restarted for it).
void App::on_quit() {
    ui::telemetry::quit();
    std::string error;
    ui::updates::apply(&error);
    ui::telemetry::stop();
}

ui::HostClient& App::host() {
    return *s_->host;
}

void App::load_fonts() {
    s_->fonts = ui::load_fonts(s_->settings.font_size, 13.0f);  // design size; ui_scale() scales it
    ImGui::GetIO().FontDefault = s_->fonts.ui;
    apply_terminal_font(*s_);
    apply_ui_font(*s_);
}

void App::apply_scale(float dpi) {
    s_->dpi = dpi;
    g_scale = dpi * ui_scale(s_->settings);
    apply_style(dpi, ui_scale(s_->settings));
}

void App::on_close_requested() {
    if (s_->settings.close_notice_shown || s_->config.automated)
        s_->exit_now = true;
    else
        s_->show_close_notice = true;
}

void App::on_focus_changed(bool focused) {
    if (!focused && s_->window_focused)  // the Timeline shades the time away from here
        s_->timeline.left_at_ms = now_ms();
    s_->window_focused = focused;
}

void App::on_files_dropped(std::vector<std::string> paths) {
    std::string target;
    for (const auto& [id, vs] : s_->views) {
        if (vs.view.hovered())
            target = id;
    }
    if (target.empty())
        target = s_->ws.focused_session();
    if (const auto it = s_->views.find(target); it != s_->views.end())
        it->second.view.drop_paths(std::move(paths));
}

bool App::exit_requested() const {
    return s_->exit_now;
}

long long App::next_wakeup_ms() const {
    long long ms = 1000;
    for (const auto& [id, vs] : s_->views) {
        if (const auto d = vs.view.next_deadline())
            ms = std::min<long long>(
                ms, std::max<long long>(
                        0, std::chrono::duration_cast<std::chrono::milliseconds>(*d - Clock::now()).count()) +
                        1);
    }
    if (s_->doc_dirty)
        ms = std::min<long long>(ms, 450);
    if (!s_->toasts.empty())
        ms = std::min<long long>(ms, 250);
    if (s_->updates_asked)
        ms = std::min<long long>(ms, 300);  // Settings polls the update check every 300 ms
    ms = std::min<long long>(ms, timeline_wakeup_ms(*s_));
    // The working mark spins; in Focus only the pane header (while shown) has one.
    const bool spinners = !s_->settings.focus_on || s_->focus_mode.header.alpha() > 0.0f;
    for (const auto& [id, v] : s_->views_by_id) {
        if (v.agent_state == "Working" && spinners)
            ms = std::min<long long>(ms, 50);
    }
    if (focus_animating(*s_))
        ms = std::min<long long>(ms, 16);  // the edge glow, the header fading
    if ((s_->stage.animating || s_->tails_pending) && !s_->settings.focus_on)
        ms = std::min<long long>(ms, 100);            // a thumbnail's lines held back, skeleton bars breathing
    if (ui::styles::has_clock(s_->layout().style)) {  // a bar's clock turns over on the minute
        const long long now = std::chrono::duration_cast<std::chrono::milliseconds>(
                                  std::chrono::system_clock::now().time_since_epoch())
                                  .count();
        ms = std::min<long long>(ms, ui::mac::ms_to_next_minute(now % 60000) + 5);
    }
    ms = std::min<long long>(ms, tok::wakeup_ms());  // an eased transition is under way
    if (ambient_moving())
        ms = std::min<long long>(ms, 33);  // the tint by agent state fades at about 30 frames a second
    return ms;
}

Json App::report() const {
    Json out = {{"connected", s_->host->connected()}, {"host_instance_id", s_->host->host_instance_id()}};
    out["ambient_k"] = ambient_strength();
    Json list = Json::array();
    for (const auto& s : s_->host->sessions()) {
        const auto v = s->view();
        const auto snap = s->snapshot();
        Json screen = Json::array();
        for (int r = 0; r < snap->rows; ++r)
            screen.push_back(snap->row_text(r));
        list.push_back({{"id", v.id},
                        {"name", v.name},
                        {"process_state", v.process_state},
                        {"pid", v.pid},
                        {"program", v.program},
                        {"agent_state", v.agent_state},
                        {"agent_source", v.agent_source},
                        {"is_controller", v.is_controller},
                        {"lost", v.lost},
                        {"cols", snap->cols},
                        {"rows", snap->rows},
                        {"screen", screen}});
    }
    out["sessions"] = list;
    std::size_t tabs = 0;
    for (const auto& sp : s_->ws.spaces)
        tabs += sp.tabs.size();
    out["spaces"] = s_->ws.spaces.size();
    out["tabs"] = tabs;
    out["selected"] = s_->ws.focused_session();
    out["layout"] = s_->settings.layout;
    out["focus_on"] = s_->settings.focus_on;
    out["workspace"] = s_->ws.to_json();

    return out;
}
}  // namespace agentdesk::desktop
