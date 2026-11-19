// Commands: the command table, run_command / exec_command (palette, keys.json, mods, --run)
// and the layout commands.
#include "win98_internal.hpp"

namespace agentdesk::desktop {
namespace detail {

namespace {
// The commands AgentDesk itself has (herdr commands::COMMANDS).
bool builtin_command(std::string_view name) {
    static const std::set<std::string, std::less<>> kNames = {
        "agent.focus", "agent.keep", "agent.new", "agent.next", "agent.peek", "agent.prev", "app.exit_all",
        "app.feedback", "app.help", "app.keys", "app.palette", "app.picker", "app.reset", "app.settings",
        "app.welcome", "app.whats_new", "app.zoom_in", "app.zoom_out", "app.zoom_reset", "details.toggle",
        "launch", "layout", "layout.clone", "layout.reset", "layout.switch", "layout.toggle", "machine.add",
        "mod", "mod.clone", "mod.open", "mods.folder", "mods.reload", "pane.close", "pane.go", "pane.park",
        "pane.rename", "pane.resize", "pane.split", "pane.swap", "pane.zoom", "panel", "session.focus",
        "session.force_stop", "session.interrupt", "session.restart", "session.stop", "sidebar.rail",
        "sidebar.toggle", "space.close", "space.move", "space.new", "space.next", "space.prev", "space.rename",
        "space.select", "tab.close", "tab.move", "tab.new", "tab.next", "tab.prev", "tab.rename", "tab.select",
        "tab.settle", "tab.unsettle", "terminal.copy", "terminal.kill", "terminal.open", "terminal.park",
        "terminal.paste", "terminal.paste_image", "terminal.peek", "terminal.pin", "terminal.right_click",
        "terminal.scroll", "terminal.search", "terminal.select_all", "theme", "window.close", "window.maximize",
        "window.minimize", "worktree.new", "worktree.open", "worktree.remove", "app.quit", "overlay.close",
        // herdr commands.rs 98, 99, 105
        "app.check_updates", "app.update_core", "app.update"};
    return kNames.contains(name);
}

}  // namespace

// Command names keys.json may bind (herdr validates actions and reports unknown ones). A
// "<mod>.<command>" action is accepted when that mod folder exists.
bool known_command(std::string_view name) {
    if (builtin_command(name))
        return true;
    // Herdr 98's commands (herdr-app defaults/mods/win98/README.md 12-17).
    static const std::set<std::string, std::less<>> kWin98 = {
        "win98.start",       "win98.taskman", "win98.new_tab", "win98.new_space", "win98.rename_tab", "win98.rename_space",
        "win98.run",         "win98.shutdown", "win98.about",  "win98.display",   "win98.welcome",    "win98.boot",
        "win98.mines",       "win98.sol",     "win98.mycomputer", "win98.network", "win98.bin",       "win98.datetime",
        "win98.logoff",      "win98.small_fonts", "win98.large_fonts", "win98.close_tab", "win98.close_space"};
    if (kWin98.contains(name))
        return true;
    const auto dot = name.find('.');
    std::error_code ec;
    return dot != std::string_view::npos &&
           std::filesystem::is_directory(data_dir() / L"mods" / win::to_wide(std::string(name.substr(0, dot))), ec);
}

namespace {
const char* command_label(const std::string& id) {
    for (const auto& c : kCommands)
        if (id == c.id)
            return c.label;
    return nullptr;
}

}  // namespace

// ------------------------------------------------------------------ commands

// Profile used for new spaces/tabs/splits: the configured one if it exists, else the first
// shell found (PowerShell 7, Windows PowerShell, Command Prompt), else empty.
std::string pick_shell(const std::string& wanted, const std::vector<std::pair<std::string, std::string>>& profiles) {
    const auto has = [&](const std::string& id) {
        return std::any_of(profiles.begin(), profiles.end(), [&](const auto& p) { return p.first == id; });
    };
    if (!wanted.empty() && has(wanted))
        return wanted;
    for (const char* id : {"pwsh", "powershell", "cmd"})
        if (has(id))
            return id;
    return {};
}

namespace {
// git in `cwd` on `machine` ("" = here); other machines run it through their agent-host's
// git.run (herdr worktree.js runs git on the space's machine).
host::ToolResult git_at(App::State& s, const std::string& machine, const std::string& cwd, std::vector<std::wstring> args,
                        std::chrono::seconds timeout = std::chrono::seconds(30)) {
    if (machine.empty()) {
        const std::filesystem::path p(win::to_wide(cwd));
        args.insert(args.begin(), {L"git", L"-C", p.native()});
        return host::run_tool(args, p, timeout);
    }
    host::ToolResult r;
    Json a = Json::array();
    for (const auto& x : args)
        a.push_back(win::to_utf8(x));
    if (const auto res = s.panels->remote ? s.panels->remote(machine, "git.run", Json{{"cwd", cwd}, {"args", a}})
                                          : std::nullopt) {
        r.started = true;
        r.exit_code = static_cast<unsigned long>(safe::int_or(*res, "exit_code", 1));
        r.output = safe::string_or(*res, "output");
        r.error = safe::string_or(*res, "error");
    }
    return r;
}

}  // namespace

// Text without surrounding whitespace (JavaScript's trim()).
std::string trimmed(const std::string& t) {
    const auto a = t.find_first_not_of(" \t\r\n");
    if (a == std::string::npos)
        return {};
    return t.substr(a, t.find_last_not_of(" \t\r\n") - a + 1);
}

namespace {
// What removing a linked worktree would lose (herdr worktree.js): uncommitted changes, and
// submodules with commits their upstream does not have. Plain git, no shell.
struct WorktreeRisk {
    bool dirty = false;
    bool submodules = false;
    std::vector<std::string> unpushed;  // submodule paths
    [[nodiscard]] std::string signature() const {
        std::string sig = std::string(dirty ? "d" : "-") + (submodules ? "s" : "-");
        for (const auto& u : unpushed)
            sig += "," + u;
        return sig;
    }
};

WorktreeRisk worktree_risk(App::State& s, const std::string& machine, const std::string& top) {
    WorktreeRisk r;
    const std::filesystem::path p(win::to_wide(top));
    const auto st = git_at(s, machine, top, {L"status", L"--porcelain"});
    r.dirty = st.started && st.exit_code == 0 && !st.output.empty();
    const auto subs = git_at(s, machine, top, {L"submodule", L"status", L"--recursive"});
    std::stringstream ss(subs.started && subs.exit_code == 0 ? subs.output : std::string());
    std::vector<std::string> paths;
    for (std::string line; std::getline(ss, line);) {
        // "<flag><sha> <path> (<describe>)"; '-': never checked out (herdr skips those too)
        if (line.empty() || line[0] == '-')
            continue;
        std::stringstream ls(line.substr(1));
        std::string sha, path;
        ls >> sha >> path;
        if (!path.empty())
            paths.push_back(path);
    }
    for (const auto& path : paths) {
        r.submodules = true;
        // herdr defaults/mods/worktree/main.js:100-110: commits that exist nowhere else, i.e. on
        // no remote and not the commit the parent pins (git fetches that by hash, so it is often
        // on no remote branch). The parent is the deepest other submodule containing it.
        std::string parent;
        for (const auto& other : paths) {
            if (other.size() > parent.size() && path.size() > other.size() && path.compare(0, other.size(), other) == 0 &&
                path[other.size()] == '/')
                parent = other;
        }
        const auto parent_dir = parent.empty() ? p : p / win::to_wide(parent);
        const std::string inner = parent.empty() ? path : path.substr(parent.size() + 1);
        const auto pinned = git_at(s, machine, win::to_utf8(parent_dir.native()),
                                   {L"rev-parse", L"HEAD:" + win::to_wide(inner)});
        std::vector<std::wstring> args = {L"rev-list", L"-n1", L"--all", L"--not", L"--remotes"};
        std::string pin = pinned.started && pinned.exit_code == 0 ? trimmed(pinned.output) : std::string();
        if (!pin.empty())
            args.push_back(win::to_wide(pin));
        const auto local = git_at(s, machine, win::to_utf8((p / win::to_wide(path)).native()), args);
        if (!local.started || local.exit_code != 0 || !trimmed(local.output).empty())
            r.unpushed.push_back(path);  // a check that failed counts as at risk
    }
    return r;
}

std::string worktree_remove_text(const std::string& top, const WorktreeRisk& r) {
    std::string t = "Closes the space and deletes " + display_path(top) + ". The branch stays.";
    if (r.dirty)
        t += "\n\nIt has uncommitted changes: they will be lost.";
    if (!r.unpushed.empty()) {  // herdr worktree/main.js render()
        t += "\n\nSubmodule ";
        for (std::size_t i = 0; i < r.unpushed.size(); ++i)
            t += (i ? ", " : "") + r.unpushed[i];
        t += " has commits that aren't pushed anywhere: they will be lost.";
    }
    return t;
}

}  // namespace

void open_launcher(App::State& s, LaunchTarget target) {
    s.show_launcher = true;
    s.launch_machine = s.ws.active() != nullptr ? s.ws.active()->machine : std::string();
    if (target == LaunchTarget::kSplitRight || target == LaunchTarget::kSplitDown) {
        const auto f = s.ws.focused_session();
        if (s.machine_of.count(f))
            s.launch_machine = s.machine_of[f];
    }
    s.launch_target = target;
    s.launch_split_of = s.ws.focused_session();
    s.launch_error.clear();
    s.launch_name[0] = '\0';
    s.launcher_sel = 0;
    const auto cwd = s.focused_cwd();
    if (!cwd.empty())
        std::snprintf(s.launch_cwd, sizeof s.launch_cwd, "%s", cwd.c_str());
}

void open_rename(App::State& s, const std::string& kind, const std::string& target,
                 const std::string& current) {
    // herdr window.rs 3047-3051: a second rename press closes the open prompt instead of
    // stacking another.
    if (s.rename_open) {
        s.rename_close = true;
        return;
    }
    s.show_rename = true;
    s.rename_kind = kind;
    s.rename_target = target;
    std::snprintf(s.rename_buf, sizeof s.rename_buf, "%s", current.c_str());
}

// ------------------------------------------------------------------ layout commands
// herdr tabs/main.js 62-80 syncPanel: the side panel is remembered per space (here the space's
// `panel`, kept in the UI document): the shown space's choice opens or closes the layout's
// "panel" node and picks the "panel-body" child.
void sync_panel(App::State& s, LayoutSpec& l) {
    lay::Node* panel = l.tree.find("panel");
    if (panel == nullptr)
        return;
    const auto* sp = s.ws.active();
    panel->hidden = sp == nullptr || sp->panel.empty();
    if (sp != nullptr && !sp->panel.empty())
        l.tree.select("panel-body", sp->panel);
}

namespace {
// The arguments herdr's layout.* commands take ({"id": ..., "px": ...}) as JSON, or the short
// spellings a key or the palette can carry: "id", "id=300" (px), "stack=child".
Json layout_args(const std::string& arg, const char* first, const char* second) {
    if (!arg.empty() && arg.front() == '{') {
        Json j = Json::parse(arg, nullptr, false);
        return j.is_object() ? j : Json::object();
    }
    Json j = Json::object();
    const auto eq = arg.find('=');
    j[first] = arg.substr(0, eq);
    if (eq != std::string::npos) {
        const std::string v = arg.substr(eq + 1);
        if (std::string(second) == "px") {
            char* end = nullptr;
            const double d = std::strtod(v.c_str(), &end);
            if (end != v.c_str())
                j["px"] = d;
        } else {
            j[second] = v;
        }
    }
    return j;
}

// herdr window.rs 3496-3538: layout.toggle / show / hide {id}, layout.resize {id, px | weight},
// layout.select {stack, child}; the change is a click, kept in layouts/.state.
std::optional<std::string> layout_command(App::State& s, const std::string& name, const std::string& arg) {
    LayoutSpec& l = s.layout();
    if (name == "layout.toggle" || name == "layout.show" || name == "layout.hide") {
        const std::string id = layout_args(arg, "id", "").value("id", std::string());
        lay::Node* n = l.tree.find(id);
        if (n == nullptr)
            return "no node \"" + id + "\" in the shown layout";
        const bool hide = name == "layout.toggle" ? !n->hidden : name == "layout.hide";
        if (id == "panel") {  // the space's choice (tabs/main.js 136-141)
            if (auto* sp = s.ws.active()) {
                const lay::Node* body = l.tree.find("panel-body");
                const lay::Node* shown = body != nullptr ? body->active_child() : nullptr;
                sp->panel = hide ? std::string() : shown != nullptr ? shown->id : std::string("files");
                s.doc_dirty = true;
                s.doc_dirty_at = Clock::now();
            }
        }
        n->hidden = hide;
    } else if (name == "layout.resize") {
        const Json a = layout_args(arg, "id", "px");
        const std::string id = a.value("id", std::string());
        lay::Node* n = l.tree.find(id);
        if (n == nullptr)
            return "no node \"" + id + "\"";
        if (a.contains("px") && a["px"].is_number())
            n->size = lay::Span::pixels(a["px"].get<float>());
        else if (a.contains("weight") && a["weight"].is_number())
            n->size = lay::Span::weight(a["weight"].get<float>());
    } else if (name == "layout.select") {
        const Json a = layout_args(arg, "stack", "child");
        const std::string st = a.value("stack", std::string());
        const std::string ch = a.value("child", std::string());
        if (!l.tree.select(st, ch))
            return "no stack \"" + st + "\" with a child \"" + ch + "\"";
        if (st == "panel-body")
            if (auto* sp = s.ws.active(); sp != nullptr && !sp->panel.empty()) {
                sp->panel = ch;
                s.doc_dirty = true;
                s.doc_dirty_at = Clock::now();
            }
    } else {
        return std::nullopt;
    }
    save_layout_state(l);
    derive_layout(l);
    return std::string();
}

// Runs a command and says why it failed ("" when it ran): herdr window.rs run_as() answers
// {"error": ...} for an unknown command, a missing target, or a git step that failed.
std::string exec_command(App::State& s, const std::string& cmd) {
    const auto colon = cmd.find(':');
    const std::string name = cmd.substr(0, colon);
    const std::string arg = colon == std::string::npos ? std::string() : cmd.substr(colon + 1);
    const std::string focused = s.ws.focused_session();
    auto session = [&]() -> std::shared_ptr<ui::RemoteSession> {
        const auto it = s.by_id.find(focused);
        return it == s.by_id.end() ? nullptr : it->second;
    };
    // herdr command names (keys.json written for herdr keeps working).
    static const std::map<std::string, std::string> kHerdrNames = {
        {"terminal.park", "pane.park"}, {"terminal.peek", "agent.peek"}, {"terminal.open", "tab.new"},
        {"app.quit", "window.close"}};  // herdr Quit: the window closes, agents keep running
    if (name == "layout.toggle" && arg.empty())  // the panel (AgentDesk's older spelling)
        return exec_command(s, "panel");
    if (auto r = layout_command(s, name, arg))
        return *r;
    if (const auto al = kHerdrNames.find(name); al != kHerdrNames.end())
        return exec_command(s, arg.empty() ? al->second : al->second + ":" + arg);
    if (name == "mod.open" && arg == "picker")
        return exec_command(s, "app.picker");
    if (name == "overlay.close") {  // herdr: closes the overlays (here: the floating ones)
        s.show_palette = s.show_picker = s.show_help = false;
        s.peek.clear();
        return {};
    }
    if (name == "terminal.pin") {  // herdr terminal.pin {pinned}: the focused agent
        if (!focused.empty()) {
            if (arg == "false")
                s.ws.pinned.erase(focused);
            else if (arg == "true" || !s.ws.pinned.erase(focused))
                s.ws.pinned.insert(focused);
            s.doc_dirty = true;
            s.doc_dirty_at = Clock::now();
        }
        return {};
    }
    if (name == "space.move") {  // herdr space.move {index} (1 = first): the active space
        const int to = std::atoi(arg.c_str()) - 1;
        if (to >= 0 && s.ws.move_space(s.ws.active_space, to)) {
            s.doc_dirty = true;
            s.doc_dirty_at = Clock::now();
        }
        return {};
    }
    if (name == "launch") {
        const auto c2 = arg.find(':');
        const std::string target = arg.substr(0, c2);
        const std::string profile = c2 == std::string::npos ? std::string() : arg.substr(c2 + 1);
        Placement p;
        p.target = target == "space"   ? LaunchTarget::kSpace
                   : target == "right" ? LaunchTarget::kSplitRight
                   : target == "down"  ? LaunchTarget::kSplitDown
                                       : LaunchTarget::kTab;
        p.split_of = focused;
        p.space = s.ws.active_space;
        p.space_id = s.ws.active() != nullptr ? s.ws.active()->id : std::string();
        p.machine = s.ws.active() != nullptr ? s.ws.active()->machine : std::string();
        const std::string cwd = s.focused_cwd().empty() ? std::string(s.launch_cwd) : s.focused_cwd();
        if (!profile.empty())
            create_session(s, profile, cwd, "", p);
    } else if (name == "tab.new" || name == "space.new" || name == "pane.split") {
        // New spaces, tabs and splits open a shell in the focused folder: cd there, then run
        // claude / codex yourself (the pane follows the program running in it).
        Placement p;
        p.target = name == "space.new"                 ? LaunchTarget::kSpace
                   : name == "tab.new" || focused.empty() ? LaunchTarget::kTab
                   : arg == "down"                      ? LaunchTarget::kSplitDown
                                                        : LaunchTarget::kSplitRight;
        p.split_of = focused;
        p.space = s.ws.active_space;
        p.space_id = s.ws.active() != nullptr ? s.ws.active()->id : std::string();
        p.machine = s.ws.active() != nullptr ? s.ws.active()->machine : std::string();
        if (s.machine_of.count(focused) && p.target != LaunchTarget::kSpace)
            p.machine = s.machine_of[focused];
        ui::HostClient* h = s.host_for(p.machine);
        const std::string shell = pick_shell(s.settings.shell_profile, h->profiles());
        if (!h->connected() || shell.empty()) {
            open_launcher(s, p.target);
        } else {
            std::string cwd = s.focused_cwd();
            if (cwd.empty())
                cwd = s.launch_cwd;
            create_session(s, shell, cwd, "", p);
        }
    } else if (name == "agent.new") {
        if (focused.empty())
            open_launcher(s, LaunchTarget::kTab);
        else
            open_launcher(s, arg == "space" ? LaunchTarget::kSpace : LaunchTarget::kTab);
    } else if (name == "pane.close") {
        // herdr: ends the terminal; the last pane closes the tab (and maybe asks about the space).
        if (focused.empty())
            return "no pane to close";
        {
            const auto* t = s.ws.active_tab();
            if (t != nullptr && s.ws.sessions_in(*t).size() == 1) {
                return exec_command(s, "tab.close");
            } else {
                if (auto it = s.by_id.find(focused); it != s.by_id.end())
                    it->second->stop(true);
                s.hidden.insert(focused);
                s.ws.remove(focused);
                s.focus_view(s.ws.focused_session());
            }
        }
    } else if (name == "terminal.kill") {
        // herdr: end a terminal and its sub-agents.
        const std::string target = arg.empty() ? focused : arg;
        for (const auto& [id, v] : s.views_by_id)
            if (id == target || v.parent == target)
                if (auto it = s.by_id.find(id); it != s.by_id.end())
                    it->second->stop(true);
    } else if (name == "app.reset") {
        // herdr escape hatch: keys.json set aside, default layout, sidebar back.
        const auto keys = data_dir() / L"keys.json";
        std::error_code ec;
        if (std::filesystem::exists(keys, ec))
            std::filesystem::rename(keys, std::filesystem::path(keys).concat(L".aside"), ec);
        s.keymap = ui::Keymap::defaults();
        s.keymap_errors.clear();
        s.settings.layout = "workspace";
        save_settings(s.settings);
        // The workspace's sidebar back as shipped (shown, 268 px).
        if (lay::Node* side = s.layout().tree.find("sidebar")) {
            const lay::Node* shipped = s.layout().file.find("sidebar");
            side->hidden = shipped != nullptr && shipped->hidden;
            side->size = shipped != nullptr ? shipped->size : side->size;
            save_layout_state(s.layout());
        }
        s.toast("Back to the default screen", "keys.json was set aside as keys.json.aside.", "", g_pal->info);
    } else if (name == "pane.park") {
        // herdr terminal.park: a sub-agent's pane goes away; it keeps running under its parent
        // (agents list, peek).
        if (!focused.empty()) {
            s.hidden.insert(focused);
            s.ws.remove(focused);
            s.focus_view(s.ws.focused_session());
        }
    } else if (name == "pane.zoom") {
        s.ws.toggle_zoom();
    } else if (name == "pane.go") {
        if (s.ws.focus_dir(parse_dir(arg), s.pane_area))
            s.focus_view(s.ws.focused_session());
    } else if (name == "pane.swap") {
        s.ws.swap_dir(parse_dir(arg), s.pane_area);
    } else if (name == "pane.resize") {
        s.ws.resize(parse_dir(arg), px(40), s.pane_area);
    } else if (name == "pane.rename") {
        if (focused.empty())
            return "no pane to rename";
        open_rename(s, "pane", focused, s.pane_title(focused));
    } else if (name == "tab.rename") {
        // herdr tab.rename {id}: that tab (default: the shown one), without selecting it.
        const auto loc = arg.empty() ? ui::PaneLoc{} : ui::find_tab(s.ws, arg);
        const ui::Tab* t = !arg.empty() ? (loc.space < 0 ? nullptr
                                                          : &s.ws.spaces[static_cast<std::size_t>(loc.space)]
                                                                 .tabs[static_cast<std::size_t>(loc.tab)])
                                        : s.ws.active_tab();
        if (t == nullptr)
            return arg.empty() ? std::string("no tab to rename") : "no tab \"" + arg + "\"";
        open_rename(s, "tab", t->id, t->name);
    } else if (name == "space.rename") {
        auto* sp = s.ws.active();
        if (sp == nullptr)
            return "no space to rename";
        open_rename(s, "space", sp->id, sp->name);
    } else if (name == "tab.close") {
        // herdr tab.close {id} (window.rs 3122-3132): by id, the shown tab and space stay; the
        // last tab takes its space with it, so that asks like closing the space.
        ui::PaneLoc loc;
        if (arg.empty()) {
            if (s.ws.active_tab() != nullptr)
                loc = {s.ws.active_space, s.ws.active()->active_tab};
        } else {
            loc = ui::find_tab(s.ws, arg);
        }
        if (loc.space < 0)
            return arg.empty() ? std::string("no tab to close") : "no tab \"" + arg + "\"";
        auto& sp = s.ws.spaces[static_cast<std::size_t>(loc.space)];
        if (sp.tabs.size() == 1) {
            confirm_close_space(s, loc.space);
        } else {
            for (const auto& id : s.ws.sessions_in(sp.tabs[static_cast<std::size_t>(loc.tab)])) {
                if (auto it = s.by_id.find(id); it != s.by_id.end())  // herdr: ends the tab's terminals
                    it->second->stop(true);
                s.hidden.insert(id);
            }
            s.ws.close_tab(loc.space, loc.tab);
            s.focus_view(s.ws.focused_session());
        }
    } else if (name == "space.close") {
        // herdr asks first ("Close space?"), then ends its terminals.
        if (s.ws.active() == nullptr)
            return "no space to close";
        confirm_close_space(s, s.ws.active_space);
    } else if (name == "space.close_now") {
        for (int i = 0; i < static_cast<int>(s.ws.spaces.size()); ++i) {
            const auto& sp = s.ws.spaces[static_cast<std::size_t>(i)];
            if (sp.id != arg)
                continue;
            for (const auto& t : sp.tabs)
                for (const auto& id : s.ws.sessions_in(t))
                    if (auto it = s.by_id.find(id); it != s.by_id.end())
                        it->second->stop(true);
            s.ws.close_space(i);
            s.focus_view(s.ws.focused_session());
            break;
        }
    } else if (name == "panel") {
        // herdr layout.toggle:panel / the Files and Changes buttons: a per-space choice.
        if (auto* sp = s.ws.active()) {
            const std::string which = arg.empty() ? std::string("files") : arg;
            sp->panel = sp->panel == which ? std::string() : which;
            s.doc_dirty = true;
            s.doc_dirty_at = Clock::now();
        }
    } else if (name == "sidebar.rail") {
        // herdr workspace-chrome/main.js 18-24 toggleSidebar(): remember the width and
        // layout.resize the sidebar to 44, or back to it (a hidden one is shown first).
        LayoutSpec& l = s.layout();
        lay::Node* side = l.tree.find("sidebar");
        if (side == nullptr)
            return "no node \"sidebar\" in the shown layout";
        const float width = side->size && side->size->px ? side->size->value : 268.0f;
        if (side->hidden) {
            side->hidden = false;
        } else if (width >= 160) {
            s.settings.sidebar_restore = width;
            save_settings(s.settings);
            side->size = lay::Span::pixels(44);
        } else {
            side->size = lay::Span::pixels(std::max(160.0f, s.settings.sidebar_restore));
        }
        save_layout_state(l);
    } else if (name == "app.picker") {
        s.show_picker = !s.show_picker;
        s.picker_buf[0] = '\0';
        s.picker_sel = -1;
        s.picker_reset = true;
    } else if (name == "tab.goto" || name == "tab.close_at") {
        const auto c = arg.find(':');
        const int sp = std::atoi(arg.substr(0, c).c_str());
        const int t = c == std::string::npos ? 0 : std::atoi(arg.substr(c + 1).c_str());
        if (sp < 0 || sp >= static_cast<int>(s.ws.spaces.size()) || t < 0 ||
            t >= static_cast<int>(s.ws.spaces[static_cast<std::size_t>(sp)].tabs.size()))
            return "no such tab";
        if (name == "tab.close_at")  // by id: the shown tab and space stay (herdr picker ctrl+d)
            return exec_command(s, "tab.close:" + s.ws.spaces[static_cast<std::size_t>(sp)].tabs[static_cast<std::size_t>(t)].id);
        s.ws.select_space(sp);
        s.ws.select_tab(t);
        s.focus_view(s.ws.focused_session());
    } else if (name == "space.new_on") {
        Placement p;
        p.target = LaunchTarget::kSpace;
        p.machine = arg;
        ui::HostClient* h = s.host_for(arg);
        const std::string shell = pick_shell(s.settings.shell_profile, h->profiles());
        if (h->connected() && !shell.empty())
            create_session(s, shell, "", "", p);
    } else if (name == "window.minimize") {
        ::ShowWindow(s.config.hwnd, SW_MINIMIZE);
    } else if (name == "window.maximize") {
        ::ShowWindow(s.config.hwnd, ::IsZoomed(s.config.hwnd) ? SW_RESTORE : SW_MAXIMIZE);
    } else if (name == "window.close") {
        ::PostMessageW(s.config.hwnd, WM_CLOSE, 0, 0);
    } else if (name == "machine.add") {
        s.show_machine_add = true;
    } else if (name == "app.welcome") {
        s.show_welcome = true;
    } else if (name == "worktree.open") {
        // Existing worktrees of the focused repo (git worktree list --porcelain, bare and prunable
        // skipped): main checkout first, then by the branch's last commit, newest first.
        s.worktree_list.clear();
        s.worktree_not_repo.clear();
        const std::string cwd = s.focused_cwd();
        const std::string machine = s.machine_of[s.ws.focused_session()];
        s.worktree_machine = machine;
        const auto g = s.git.get(cwd, machine);
        // herdr worktree/main.js checkouts(): each branch's last commit date, from its ref.
        std::map<std::string, long long> when;
        if (!cwd.empty()) {
            const auto refs = git_at(s, machine, cwd,
                                     {L"for-each-ref", L"--format=%(refname:short)%09%(committerdate:unix)", L"refs/heads"});
            std::stringstream rs(refs.started && refs.exit_code == 0 ? refs.output : std::string());
            for (std::string l; std::getline(rs, l);) {
                if (!l.empty() && l.back() == '\r')
                    l.pop_back();
                const auto tab = l.find('\t');
                if (tab != std::string::npos)
                    when[l.substr(0, tab)] = std::atoll(l.c_str() + tab + 1);
            }
        }
        if (!cwd.empty()) {
            const auto r = git_at(s, machine, cwd, {L"worktree", L"list", L"--porcelain"});
            std::stringstream ss(r.output + "\n");
            WorktreeEntry e;
            bool skip = false;
            for (std::string l; std::getline(ss, l);) {
                if (!l.empty() && l.back() == '\r')
                    l.pop_back();
                if (l.rfind("worktree ", 0) == 0) {
                    e = {};
                    skip = false;
                    e.path = l.substr(9);
                    for (auto& c : e.path)
                        if (c == '/')
                            c = '\\';
                } else if (l.rfind("branch ", 0) == 0) {
                    e.branch = l.substr(7);
                    if (e.branch.rfind("refs/heads/", 0) == 0)
                        e.branch = e.branch.substr(11);
                } else if (l == "bare" || l.rfind("prunable", 0) == 0) {
                    skip = true;
                } else if (l.empty() && !e.path.empty()) {
                    if (!skip) {
                        e.main = s.worktree_list.empty();  // git lists the main checkout first
                        // A detached checkout has no branch, so no date (herdr: at 0, no age).
                        const auto it = e.branch.empty() ? when.end() : when.find(e.branch);
                        e.time = it != when.end() ? it->second : 0;
                        // herdr ago(): rounded, minutes at least 1.
                        const double d = static_cast<double>(static_cast<long long>(std::time(nullptr)) - e.time);
                        auto round_to = [](double v) { return std::to_string(static_cast<long long>(std::floor(v + 0.5))); };
                        e.age = e.time <= 0           ? std::string()
                                : d < 3600.0          ? std::to_string(std::max(1LL, static_cast<long long>(std::floor(d / 60.0 + 0.5)))) + "m"
                                : d < 86400.0         ? round_to(d / 3600.0) + "h"
                                : d < 86400.0 * 60.0  ? round_to(d / 86400.0) + "d"
                                                      : round_to(d / 86400.0 / 30.0) + "mo";
                        s.worktree_list.push_back(e);
                    }
                    e = {};
                }
            }
            std::stable_sort(s.worktree_list.begin(), s.worktree_list.end(),
                             [](const WorktreeEntry& a2, const WorktreeEntry& b2) {
                                 if (a2.main != b2.main)
                                     return a2.main;
                                 return a2.time > b2.time;
                             });
        }
        // Outside git the dialog itself says so (herdr renderOpen).
        if (s.worktree_list.empty())
            s.worktree_not_repo = cwd.empty() ? std::string("This folder") : display_path(cwd);
        s.show_worktree_open = true;
    } else if (name == "worktree.open_at") {
        // arg: "<machine>|<path>": a space on that machine already in the checkout, else a new one.
        const auto bar = arg.find('|');
        const std::string machine = bar == std::string::npos ? std::string() : arg.substr(0, bar);
        const std::string path = bar == std::string::npos ? arg : arg.substr(bar + 1);
        int best = -1;
        for (int i = 0; i < static_cast<int>(s.ws.spaces.size()) && best < 0; ++i) {
            const auto& sp = s.ws.spaces[static_cast<std::size_t>(i)];
            if (sp.machine != machine || sp.tabs.empty())
                continue;
            const auto ids = s.ws.sessions_in(sp.tabs.front());
            if (!ids.empty() && s.views_by_id.count(ids.front())) {
                const auto top = s.git.get(s.views_by_id[ids.front()].cwd, machine).top;
                if (_stricmp(top.c_str(), path.c_str()) == 0)
                    best = i;
            }
        }
        if (best >= 0) {
            s.ws.select_space(best);
            s.focus_view(s.ws.focused_session());
            return {};
        }
        Placement p;
        p.target = LaunchTarget::kSpace;
        p.machine = machine;
        ui::HostClient* h = s.host_for(machine);
        const std::string shell = pick_shell(s.settings.shell_profile, h->profiles());
        if (!shell.empty())
            create_session(s, shell, path, "", p);
    } else if (name == "worktree.remove") {
        const std::string machine = s.machine_of[s.ws.focused_session()];
        const auto g = s.git.get(s.focused_cwd(), machine);
        if (!g.repo)
            return "not a git checkout";
        if (!g.worktree) {
            return "not a worktree: this is the repository's main checkout";
        } else {
            const auto risk = worktree_risk(s, machine, g.top);
            const auto loc = s.ws.find(s.ws.focused_session());
            open_confirm(s, "Remove worktree " + (loc.space >= 0 ? s.ws.space_title(loc.space, s.session_cwds()) : g.top) + "?",
                         worktree_remove_text(g.top, risk), "Remove",
                         "worktree.remove_checked:" + risk.signature() + "|" + machine + "|" + g.top);
        }
    } else if (name == "worktree.remove_checked") {
        // herdr worktree.js 228-245: Enter re-checks; a risk that appeared since the dialog
        // opened asks once more ("Enter again removes it anyway").
        const auto bar = arg.find('|');
        const auto bar2 = bar == std::string::npos ? std::string::npos : arg.find('|', bar + 1);
        const std::string seen = arg.substr(0, bar);
        const std::string machine = bar2 == std::string::npos ? std::string() : arg.substr(bar + 1, bar2 - bar - 1);
        const std::string top = bar2 == std::string::npos ? std::string() : arg.substr(bar2 + 1);
        const auto risk = worktree_risk(s, machine, top);
        const std::string force = risk.dirty || risk.submodules ? "worktree.remove_force:" : "worktree.remove_now:";
        if (risk.signature() != seen) {
            open_confirm(s, "Remove worktree?",
                         worktree_remove_text(top, risk) + "\n\nIt changed since this opened. Enter again removes it anyway.",
                         "Remove", force + machine + "|" + top);
        } else {
            return exec_command(s, force + machine + "|" + top);
        }
    } else if (name == "worktree.remove_now" || name == "worktree.remove_force") {
        // arg: "<machine>|<top>" (an older "<top>" means this computer)
        const auto bar = arg.find('|');
        const std::string machine = bar == std::string::npos ? std::string() : arg.substr(0, bar);
        const std::string top = bar == std::string::npos ? arg : arg.substr(bar + 1);
        const auto g = s.git.get(top, machine);
        const std::string main = g.common_top.empty() ? top : g.common_top;
        // End the terminals of spaces in that checkout first (they hold the folder open).
        for (int i = static_cast<int>(s.ws.spaces.size()) - 1; i >= 0; --i) {
            bool inside = false;
            for (const auto& t : s.ws.spaces[static_cast<std::size_t>(i)].tabs)
                for (const auto& id : s.ws.sessions_in(t))
                    if (s.views_by_id.count(id) && s.machine_of[id] == machine &&
                        path_within(s.views_by_id[id].cwd, top))
                        inside = true;
            if (inside)
                run_command(s, "space.close_now:" + s.ws.spaces[static_cast<std::size_t>(i)].id);
        }
        std::vector<std::wstring> argv = {L"worktree", L"remove"};
        if (name == "worktree.remove_force")
            argv.push_back(L"--force");
        argv.push_back(win::to_wide(top));
        const auto r = git_at(s, machine, main, argv, std::chrono::seconds(600));  // herdr worktree.js 243
        s.git.invalidate(top);
        if (!r.started || r.exit_code != 0)  // shown as "worktree.remove failed: ..."
            return r.started ? r.message() : std::string("git did not run");
    } else if (name == "tab.move") {
        if (auto* sp = s.ws.active())
            s.ws.move_tab(s.ws.active_space, sp->active_tab, sp->active_tab + (arg == "left" ? -1 : 1));
    } else if (name == "tab.settle" || name == "tab.unsettle") {
        // herdr: put a tab away (out of the bar, still running) until shown or it needs you.
        if (auto* sp = s.ws.active()) {
            if (name == "tab.unsettle") {
                for (int i = 0; i < static_cast<int>(sp->tabs.size()); ++i)
                    s.ws.unsettle_tab(s.ws.active_space, i);
            } else if (!s.ws.settle_tab(s.ws.active_space, sp->active_tab)) {
                s.toast("Nothing to put away", "The last tab shown in a space stays.", "", g_pal->info);
            }
            s.doc_dirty = true;
            s.doc_dirty_at = Clock::now();
            s.focus_view(s.ws.focused_session());
        }
    } else if (name == "terminal.right_click") {
        if (!focused.empty()) {
            if (arg == "program")
                s.ws.right_click_program.insert(focused);
            else
                s.ws.right_click_program.erase(focused);
            s.doc_dirty = true;
            s.doc_dirty_at = Clock::now();
        }
    } else if (name == "tab.next" || name == "tab.prev") {
        s.ws.cycle_tab(name == "tab.next" ? 1 : -1);
        s.focus_view(s.ws.focused_session());
    } else if (name == "tab.select") {
        s.ws.select_shown_tab(std::atoi(arg.c_str()));
        s.focus_view(s.ws.focused_session());
    } else if (name == "space.next" || name == "space.prev") {
        s.ws.cycle_space(name == "space.next" ? 1 : -1);
        s.focus_view(s.ws.focused_session());
    } else if (name == "space.select") {
        s.ws.select_space(std::atoi(arg.c_str()) - 1);
        s.focus_view(s.ws.focused_session());
    } else if (name == "agent.next" || name == "agent.prev") {
        if (!s.agent_order.empty()) {
            auto it = std::find(s.agent_order.begin(), s.agent_order.end(), focused);
            int i = it == s.agent_order.end() ? -1 : static_cast<int>(it - s.agent_order.begin());
            const int n = static_cast<int>(s.agent_order.size());
            i = name == "agent.next" ? (i + 1) % n : (i <= 0 ? n - 1 : i - 1);
            s.focus(s.agent_order[static_cast<std::size_t>(i)]);
        }
    } else if (name == "agent.focus" || name == "session.focus") {
        s.focus(arg);
    } else if (name == "agent.peek") {
        s.peek = arg;
        s.views[arg].focus_next = true;
    } else if (name == "agent.keep") {
        // Give a sub-agent a pane: beside its parent when the parent is shown, else a new tab.
        const auto it = s.views_by_id.find(arg);
        if (it != s.views_by_id.end() && s.ws.find(arg).space < 0) {
            s.hidden.erase(arg);
            if (!it->second.parent.empty() && s.ws.find(it->second.parent).space >= 0)
                s.ws.split(it->second.parent, arg, Dir::kRight);
            else
                s.ws.add_tab(arg, s.space_for_cwd(it->second.cwd));
        }
        s.peek.clear();
        s.focus(arg);
    } else if (name == "terminal.search") {
        if (!focused.empty())
            s.views[focused].view.open_search();
    } else if (name == "terminal.copy" || name == "terminal.paste" || name == "terminal.paste_image" ||
               name == "terminal.select_all" || name == "terminal.scroll") {
        // Run by the focused terminal on its next frame (palette, keys.json in global).
        if (!focused.empty())
            s.views[focused].view.request(cmd);
    } else if (name == "worktree.new") {
        s.show_worktree = true;
        s.wt_error.clear();
        std::snprintf(s.wt_repo, sizeof s.wt_repo, "%s", s.focused_cwd().c_str());
        s.wt_branch[0] = '\0';
    } else if (name == "session.interrupt") {
        if (auto r = session())
            r->send(host::RawInput{std::string("\x03", 1)});
    } else if (name == "session.stop") {
        if (auto r = session())
            r->stop(false);
    } else if (name == "session.force_stop") {
        if (auto r = session())
            r->stop(true);
    } else if (name == "session.restart") {
        const auto it = s.views_by_id.find(focused);
        if (it != s.views_by_id.end()) {
            Placement p;
            p.replace = focused;
            p.machine = s.machine_of.count(focused) ? s.machine_of[focused] : std::string();
            create_session(s, it->second.profile_id, it->second.cwd, it->second.name, p);
        }
    } else if (name == "sidebar.toggle") {
        // herdr ctrl-shift-b: layout.toggle {"id": "sidebar"} (nothing when the layout has none).
        if (s.layout().tree.find("sidebar") != nullptr)
            return *layout_command(s, "layout.toggle", "sidebar");
    } else if (name == "details.toggle") {
        s.settings.details = !s.settings.details;
        save_settings(s.settings);
    } else if (name == "app.palette") {
        s.show_palette = !s.show_palette;
        s.palette_buf[0] = '\0';
        s.palette_sel = 0;
    } else if (name == "app.help") {
        s.show_help = !s.show_help;
    } else if (name == "app.settings") {
        s.show_settings = true;
        // herdr app.settings {section}: open on that page ("mods", "alerts", ...).
        for (const char* sec : {"Look", "Machines", "Alerts", "Mods", "Updates", "Privacy"})
            if (_stricmp(arg.c_str(), sec) == 0)
                s.settings_tab = sec;
    } else if (name == "app.zoom_in" || name == "app.zoom_out" || name == "app.zoom_reset") {
        s.settings.zoom =
            name == "app.zoom_reset"
                ? 1.0f
                : std::clamp(std::round((s.settings.zoom + (name == "app.zoom_in" ? 0.1f : -0.1f)) * 10.0f) / 10.0f,
                             0.5f, 3.0f);
        save_settings(s.settings);
        g_scale = s.dpi * ui_scale(s.settings);
        apply_style(s.dpi, ui_scale(s.settings));
    } else if (name == "app.keys") {
        const auto path = data_dir() / L"keys.json";
        load_keys(s);  // writes the defaults when the file is missing
        ::ShellExecuteW(nullptr, L"open", L"notepad.exe", path.c_str(), nullptr, SW_SHOWNORMAL);
        s.toast("keys.json opened", "Saved changes apply right away.", "", g_pal->info);
    } else if (name == "app.exit_all") {
        s.show_exit = true;
        std::lock_guard lock(s.placements_mutex);
        s.exit_error.clear();
    } else if (name == "theme") {
        // herdr app.theme (window.rs 3620-3635): the theme goes on the shown layout, as its light
        // or its dark half; the other half stays. Appearance is not changed.
        const Palette* p = palette_by_id(arg);
        auto pair = layout_theme_pair(s.settings);
        (p->light ? pair.second : pair.first) = p->id;
        s.settings.layout_themes[s.settings.layout] = pair;
        s.settings.theme = p->id;
        save_settings(s.settings);
    } else if (name == "layout.switch" || name == "layout") {
        std::string next = arg;
        if (next.empty()) {  // cycle
            for (std::size_t i = 0; i < s.layouts.size(); ++i)
                if (s.layouts[i].name == s.settings.layout)
                    next = s.layouts[(i + 1) % s.layouts.size()].name;
        }
        if (next.empty() && !s.layouts.empty())
            next = s.layouts.front().name;
        s.settings.layout = next;
        save_settings(s.settings);
        s.toast("Layout: " + next, "", "", g_pal->info, 2);
        s.focus_view(s.ws.focused_session());
    } else if (name == "app.check_updates") {
        ui::updates::check_now();  // window.rs 3603
    } else if (name == "app.update_core") {
        // window.rs 3604-3609: nothing waiting (and nothing failed) is an error; else
        // `agentctl update` (herdr: `hc update`) in the background. agent-host hands its
        // terminals to the new one; they keep running (design doc 7.4 OQ-09).
        const auto c = ui::updates::core();
        if (!c.ready && !c.error)
            return "agent-host is up to date";
        ui::updates::update_core();
    } else if (name == "app.update") {
        // window.rs 3645-3657
        std::string error;
        if (!ui::updates::apply(&error)) {
            if (error.empty())
                return "no update is ready yet";
            return "Close AgentDesk's terminals (or restart Windows) and try again. Couldn't replace "
                   "AgentDesk's files: " +
                   error + ".";
        }
        ui::updates::relaunch();
        s.exit_now = true;
    } else if (name == "app.whats_new" || name == "app.feedback") {
        s.show_notes = true;
        s.notes_kind = name == "app.whats_new" ? "whats_new" : "feedback";
        s.feedback_note.clear();
        if (name == "app.feedback" && !arg.empty() && s.feedback_buf[0] == '\0')
            std::snprintf(s.feedback_buf, sizeof s.feedback_buf, "%s", arg.c_str());
    } else if (name == "layout.clone") {
        // herdr window.rs 3443-3454 and 266-290 clone_layout: the layout (default: the shown one)
        // as `as` (default <name>-copy), never over a file that is there, marked cloned_from, with
        // its clicks; then shown. Ours is written in herdr's format, also from an old file.
        const Json a = layout_args(arg, "name", "as");
        const std::string from = a.value("name", std::string()).empty() ? s.settings.layout : a.value("name", std::string());
        const std::string to = a.value("as", std::string()).empty() ? from + "-copy" : a.value("as", std::string());
        auto valid = [](const std::string& n) {  // mods.rs valid_name: letters, digits, - or _
            return !n.empty() && std::all_of(n.begin(), n.end(), [](char c) {
                return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '-' || c == '_';
            });
        };
        if (!valid(from) || !valid(to))
            return "cannot clone layout: names use letters, digits, - or _";
        const auto it =
            std::find_if(s.layouts.begin(), s.layouts.end(), [&](const LayoutSpec& l) { return l.name == from; });
        if (it == s.layouts.end())
            return "cannot clone layout: no layout \"" + from + "\"";
        save_layout_state(*it);
        std::error_code ec;
        std::filesystem::create_directories(layouts_dir(), ec);
        const auto dest = layouts_dir() / win::to_wide(to + ".json");
        if (std::filesystem::exists(dest, ec) ||
            std::any_of(s.layouts.begin(), s.layouts.end(), [&](const LayoutSpec& l) { return l.name == to; }))
            return "cannot clone layout: layouts\\" + to + ".json is already there";
        lay::Node node = it->file;
        node.cloned_from = Json{{"name", from}, {"version", AGENTDESK_VERSION_STRING}};
        {
            std::ofstream out(dest, std::ios::binary);
            out << lay::text_of(node);
        }
        if (std::filesystem::exists(layout_state_path(from), ec))
            std::filesystem::copy_file(layout_state_path(from), layout_state_path(to),
                                       std::filesystem::copy_options::overwrite_existing, ec);
        s.layouts = load_layouts(s.settings);
        run_command(s, "layout:" + to);
        s.toast("Layout duplicated", "layouts\\" + to + ".json", "", g_pal->info);
    } else if (name == "layout.reset") {
        // herdr window.rs 3514-3533: only a layout herdr ships; a file of that name is set aside
        // (<name>.json.old-<time>, never over an older one, window.rs 218-224) and its clicks go.
        const std::string which = arg.empty() ? s.settings.layout : arg;
        if (lay::builtin_text(which).empty())
            return "\"" + which + "\" isn't a layout AgentDesk ships";
        std::error_code ec;
        int aside = 0;
        std::vector<std::filesystem::path> files;
        for (const auto& e : std::filesystem::directory_iterator(layouts_dir(), ec))
            if (e.is_regular_file() && e.path().extension() == L".json")
                files.push_back(e.path());
        const long long stamp = static_cast<long long>(std::time(nullptr));
        for (const auto& f : files) {
            const auto j = Json::parse(read_text_file(f), nullptr, false);
            const auto stem = f.stem().u8string();
            const std::string file_name(stem.begin(), stem.end());
            // An old AgentDesk file says its name inside.
            const std::string n = lay::is_legacy(j) ? safe::string_or(j, "name", file_name) : file_name;
            if (n != which)
                continue;
            std::filesystem::path to;
            for (int i = 1;; ++i) {
                to = f;
                to += win::to_wide(".old-" + std::to_string(stamp) + (i == 1 ? "" : "-" + std::to_string(i)));
                if (!std::filesystem::exists(to, ec))
                    break;
            }
            std::filesystem::rename(f, to, ec);
            if (ec)
                return "cannot set layouts\\" + file_name + ".json aside: " + ec.message();
            ++aside;
        }
        std::filesystem::remove(layout_state_path(which), ec);
        s.layouts = load_layouts(s.settings);
        if (std::none_of(s.layouts.begin(), s.layouts.end(),
                         [&](const LayoutSpec& l) { return l.name == s.settings.layout; }))
            run_command(s, "layout:workspace");
        s.toast("Layout reset: " + which, aside > 0 ? "Your file was kept aside as <name>.json.old-<time>." : "", "",
                g_pal->info);
    } else if (name == "mod.clone") {
        if (arg.empty()) {
            open_rename(s, "mod.clone", "", "");
        } else {
            const auto dir = data_dir() / L"mods";
            const auto src = dir / win::to_wide(arg);
            std::error_code ec;
            if (arg.find_first_of("/\\:") != std::string::npos || arg == "." || arg == ".." ||
                !std::filesystem::is_directory(src, ec)) {
                s.toast("No mod named " + arg, "Mods are folders in the mods folder.", "", g_pal->info);
            } else {
                std::string to = arg + "-copy";
                for (int n = 2; std::filesystem::exists(dir / win::to_wide(to), ec); ++n)
                    to = arg + "-copy-" + std::to_string(n);
                std::filesystem::copy(src, dir / win::to_wide(to), std::filesystem::copy_options::recursive, ec);
                if (ec)
                    s.toast("Could not duplicate " + arg, ec.message(), "", g_pal->needs_you);
                else
                    s.toast("Mod duplicated", "mods\\" + to + " (off until you enable it)", "", g_pal->info);
            }
        }
    } else if (name == "mods.reload") {
        s.layouts = load_layouts(s.settings);
        s.mods->load(s.settings.mods_enabled);
        s.toast("Mods reloaded", "", "", g_pal->info, 2);
    } else if (name == "mods.folder") {
        const auto dir = data_dir() / L"mods";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        ::ShellExecuteW(nullptr, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    } else if (name == "mod") {
        const auto c2 = arg.find(':');
        if (c2 == std::string::npos)
            return "mod needs <mod>:<command>";
        s.mods->run_command(arg.substr(0, c2), arg.substr(c2 + 1));
    } else if (name.rfind("win98.", 0) == 0) {
        // Herdr 98's key commands (README.md 12-17), delivered to its taskbar (taskbar.js 126-176)
        // while the layout shows.
        if (s.layout().style == "win98")
            w98::command(s, name.substr(6));
    } else if (const auto dot = name.find('.');
               dot != std::string::npos && [&] {
                   std::error_code ec;  // "<mod>.<command>": keys.json accepts it when that mod folder exists
                   return std::filesystem::is_directory(data_dir() / L"mods" / win::to_wide(name.substr(0, dot)), ec);
               }()) {
        s.mods->run_command(name.substr(0, dot), name.substr(dot + 1));
    } else {
        // herdr window.rs 2975-2976.
        return "unknown command \"" + name + "\"";
    }
    return {};
}

}  // namespace

// herdr window.rs 2931-2943: a command run for the person (a click, a key, a menu, a mod) has
// nobody to read the reply, so a failure shows as an error toast "<command> failed".
void run_command(App::State& s, const std::string& cmd, bool person) {
    if (s.defer_commands) {
        s.deferred_commands.push_back({cmd, person, s.ws.focused_session()});
        return;
    }
    // herdr window.rs 2995-2997 / telemetry.rs 124: a command the person ran, by its name.
    if (const std::string name = cmd.substr(0, cmd.find(':')); person && builtin_command(name))
        ui::telemetry::command(name);
    const std::string error = exec_command(s, cmd);
    if (!error.empty())
        s.toast(cmd.substr(0, cmd.find(':')) + " failed", error, "", g_pal->needs_you, 8);
}

// The commands draw_layout raised, now that nothing it drew is still referenced; each with the
// focus it was raised with (a pane's buttons focus that pane first).
void run_deferred_commands(App::State& s) {
    s.defer_commands = false;
    for (const auto& d : std::exchange(s.deferred_commands, {})) {
        if (!d.focused.empty() && d.focused != s.ws.focused_session() && s.ws.find(d.focused).space >= 0)
            s.ws.focus(d.focused);
        run_command(s, d.cmd, d.person);
    }
}
}  // namespace detail

}  // namespace agentdesk::desktop
