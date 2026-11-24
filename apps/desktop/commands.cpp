// Commands: the command names, run_command / exec_command (palette, keys.json, mods, --run,
// clicks) and the layout commands.
// Portions adapted from herdr-app (Apache-2.0) and modified; see THIRD_PARTY_NOTICES.md.
#include "overlay_kit.hpp"

#include "agentdesk/ui/command_names.hpp"

namespace agentdesk::desktop {
namespace detail {

namespace {
// Every command name AgentDesk runs itself; telemetry counts only these.
bool builtin_command(std::string_view name) {
    return ui::is_builtin_command(name);
}

// A "<mod>.<command>" name whose mod folder exists in <data>\mods.
bool mod_folder_exists(std::string_view mod) {
    std::error_code ec;
    return std::filesystem::is_directory(data_dir() / L"mods" / win::to_wide(std::string(mod)), ec);
}

}  // namespace

// The command names keys.json may bind; an unknown one is reported in keys.error. A
// "<mod>.<command>" action is accepted when that mod folder exists.
bool known_command(std::string_view name) {
    if (builtin_command(name))
        return true;
    const auto dot = name.find('.');
    return dot != std::string_view::npos && mod_folder_exists(name.substr(0, dot));
}

// Profile used for new spaces/tabs/splits: the configured one if it exists, else the first
// shell found (PowerShell 7, Windows PowerShell, Command Prompt; on macOS the login shell), else
// empty.
std::string pick_shell(const std::string& wanted,
                       const std::vector<std::pair<std::string, std::string>>& profiles) {
    const auto has = [&](const std::string& id) {
        return std::any_of(profiles.begin(), profiles.end(), [&](const auto& p) { return p.first == id; });
    };
    if (!wanted.empty() && has(wanted))
        return wanted;
    for (const char* id : {"pwsh", "powershell", "cmd"})
        if (has(id))
            return id;
    // Elsewhere (macOS) the host lists the login shell first among the shells it found.
    for (const auto& [id, name] : profiles)
        if (is_shell_profile(id))
            return id;
    return {};
}

// Text without surrounding whitespace (spaces, tabs, line ends).
std::string trimmed(const std::string& t) {
    const auto a = t.find_first_not_of(" \t\r\n");
    if (a == std::string::npos)
        return {};
    return t.substr(a, t.find_last_not_of(" \t\r\n") - a + 1);
}

namespace {
// git in `cwd` on `machine` ("" = here); another machine runs it through its agent-host's git.run.
host::ToolResult git_at(App::State& s, const std::string& machine, const std::string& cwd,
                        std::vector<std::wstring> args,
                        std::chrono::seconds timeout = std::chrono::seconds(30)) {
    if (machine.empty()) {
        const std::filesystem::path p(win::to_wide(cwd));
        args.insert(args.begin(), {L"git", L"-C", p.wstring()});
        return host::run_tool(args, p, timeout);
    }
    return remote_git(s.panels->remote, machine, cwd, args);
}

bool git_ok(const host::ToolResult& r) {
    return r.started && r.exit_code == 0;
}

// What removing a linked worktree would lose: uncommitted changes, and submodules with commits
// that exist nowhere else. Plain git, no shell.
struct WorktreeRisk {
    bool dirty = false;
    bool submodules = false;
    std::vector<std::string> unpushed;  // submodule paths
    // Compared when the removal is confirmed: a different signature means the risk changed.
    [[nodiscard]] std::string signature() const {
        std::string sig = std::string(dirty ? "d" : "-") + (submodules ? "s" : "-");
        for (const auto& u : unpushed)
            sig += "," + u;
        return sig;
    }
};

// Checked-out submodule paths ("<flag><sha> <path> (<describe>)"; '-' = never checked out).
std::vector<std::string> checked_out_submodules(App::State& s, const std::string& machine,
                                                const std::string& top) {
    const auto subs = git_at(s, machine, top, {L"submodule", L"status", L"--recursive"});
    std::stringstream ss(git_ok(subs) ? subs.output : std::string());
    std::vector<std::string> paths;
    for (std::string line; std::getline(ss, line);) {
        if (line.empty() || line[0] == '-')
            continue;
        std::stringstream ls(line.substr(1));
        std::string sha, path;
        ls >> sha >> path;
        if (!path.empty())
            paths.push_back(path);
    }
    return paths;
}

WorktreeRisk worktree_risk(App::State& s, const std::string& machine, const std::string& top) {
    WorktreeRisk r;
    const std::filesystem::path p(win::to_wide(top));
    const auto st = git_at(s, machine, top, {L"status", L"--porcelain"});
    r.dirty = git_ok(st) && !st.output.empty();
    const auto paths = checked_out_submodules(s, machine, top);
    for (const auto& path : paths) {
        r.submodules = true;
        // At risk: commits on no remote that are not the commit the parent pins either (git fetches
        // a pinned commit by hash, so it is often on no remote branch). The parent is the deepest
        // other submodule containing this one, else the checkout itself.
        std::string parent;
        for (const auto& other : paths) {
            if (other.size() > parent.size() && path.size() > other.size() &&
                path.compare(0, other.size(), other) == 0 && path[other.size()] == '/')
                parent = other;
        }
        const auto parent_dir = parent.empty() ? p : p / win::to_wide(parent);
        const std::string inner = parent.empty() ? path : path.substr(parent.size() + 1);
        const auto pinned = git_at(s, machine, win::to_utf8(parent_dir.native()),
                                   {L"rev-parse", L"HEAD:" + win::to_wide(inner)});
        std::vector<std::wstring> args = {L"rev-list", L"-n1", L"--all", L"--not", L"--remotes"};
        const std::string pin = git_ok(pinned) ? trimmed(pinned.output) : std::string();
        if (!pin.empty())
            args.push_back(win::to_wide(pin));
        const auto local = git_at(s, machine, win::to_utf8((p / win::to_wide(path)).native()), args);
        if (!git_ok(local) || !trimmed(local.output).empty())
            r.unpushed.push_back(path);  // a check that failed counts as at risk
    }
    return r;
}

std::string worktree_remove_text(const std::string& top, const WorktreeRisk& r) {
    std::string t = "Closes the space and deletes " + display_path(top) + ". The branch stays.";
    if (r.dirty)
        t += "\n\nIt has uncommitted changes: they will be lost.";
    if (!r.unpushed.empty()) {
        t += "\n\nSubmodule ";
        for (std::size_t i = 0; i < r.unpushed.size(); ++i)
            t += (i ? ", " : "") + r.unpushed[i];
        t += " has commits that aren't pushed anywhere: they will be lost.";
    }
    return t;
}

// Each local branch's last commit time (unix seconds).
std::map<std::string, long long> branch_dates(App::State& s, const std::string& machine,
                                              const std::string& cwd) {
    std::map<std::string, long long> when;
    const auto refs =
        git_at(s, machine, cwd,
               {L"for-each-ref", L"--format=%(refname:short)%09%(committerdate:unix)", L"refs/heads"});
    std::stringstream rs(git_ok(refs) ? refs.output : std::string());
    for (std::string l; std::getline(rs, l);) {
        if (!l.empty() && l.back() == '\r')
            l.pop_back();
        const auto tab = l.find('\t');
        if (tab != std::string::npos)
            when[l.substr(0, tab)] = std::atoll(l.c_str() + tab + 1);
    }
    return when;
}

// "5m", "3h", "2d", "4mo": rounded to the nearest unit, at least one minute; "" without a time.
std::string age_text(long long time) {
    if (time <= 0)
        return {};
    const double d = static_cast<double>(static_cast<long long>(std::time(nullptr)) - time);
    auto round_to = [](double v) { return std::to_string(static_cast<long long>(std::floor(v + 0.5))); };
    if (d < 3600.0)
        return std::to_string(std::max(1LL, static_cast<long long>(std::floor(d / 60.0 + 0.5)))) + "m";
    if (d < 86400.0)
        return round_to(d / 3600.0) + "h";
    if (d < 86400.0 * 60.0)
        return round_to(d / 86400.0) + "d";
    return round_to(d / 86400.0 / 30.0) + "mo";
}

// The repo's worktrees (git worktree list --porcelain; bare and prunable ones skipped): the main
// checkout first, then by their branch's last commit, newest first.
std::vector<WorktreeEntry> list_worktrees(App::State& s, const std::string& machine, const std::string& cwd) {
    std::vector<WorktreeEntry> list;
    const auto when = branch_dates(s, machine, cwd);
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
            std::replace(e.path.begin(), e.path.end(), '/', '\\');
        } else if (l.rfind("branch ", 0) == 0) {
            e.branch = l.substr(7);
            if (e.branch.rfind("refs/heads/", 0) == 0)
                e.branch = e.branch.substr(11);
        } else if (l == "bare" || l.rfind("prunable", 0) == 0) {
            skip = true;
        } else if (l.empty() && !e.path.empty()) {
            if (!skip) {
                e.main = list.empty();  // git lists the main checkout first
                // A detached checkout has no branch, so no date and no age.
                const auto it = e.branch.empty() ? when.end() : when.find(e.branch);
                e.time = it != when.end() ? it->second : 0;
                e.age = age_text(e.time);
                list.push_back(e);
            }
            e = {};
        }
    }
    std::stable_sort(list.begin(), list.end(), [](const WorktreeEntry& a, const WorktreeEntry& b) {
        if (a.main != b.main)
            return a.main;
        return a.time > b.time;
    });
    return list;
}

// "<machine>|<rest>"; without a bar the whole text is <rest> on this computer.
std::pair<std::string, std::string> split_machine(const std::string& arg) {
    const auto bar = arg.find('|');
    if (bar == std::string::npos)
        return {std::string(), arg};
    return {arg.substr(0, bar), arg.substr(bar + 1)};
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
    // A second rename press closes the open prompt instead of stacking another.
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
// The side panel is chosen per space (the space's `panel`, kept in the UI document): the shown
// space's choice opens or closes the layout's "panel" node and picks the "panel-body" child.
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
// The arguments of the layout.* commands: a JSON object ({"id": ..., "px": ...}), or the short
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

// layout.toggle / layout.show / layout.hide {id}, layout.resize {id, px | weight} and
// layout.select {stack, child}. The change counts as the person's click and is kept in
// layouts\.state. nullopt: not a layout command.
std::optional<std::string> layout_command(App::State& s, const std::string& name, const std::string& arg) {
    LayoutSpec& l = s.layout();
    if (name == "layout.toggle" || name == "layout.show" || name == "layout.hide") {
        const std::string id = layout_args(arg, "id", "").value("id", std::string());
        lay::Node* n = l.tree.find(id);
        if (n == nullptr)
            return "no node \"" + id + "\" in the shown layout";
        const bool hide = name == "layout.toggle" ? !n->hidden : name == "layout.hide";
        if (id == "panel") {  // the panel is the space's choice
            if (auto* sp = s.ws.active()) {
                const lay::Node* body = l.tree.find("panel-body");
                const lay::Node* shown = body != nullptr ? body->active_child() : nullptr;
                sp->panel = hide ? std::string() : shown != nullptr ? shown->id : std::string("files");
                s.doc_changed();
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
                s.doc_changed();
            }
    } else {
        return std::nullopt;
    }
    save_layout_state(l);
    derive_layout(l);
    return std::string();
}

// ------------------------------------------------------------------ exec_command
// One command, split into its name and its argument ("name:arg").
struct Command {
    const std::string& cmd;
    std::string name;
    std::string arg;
    std::string focused;  // the focused session when it started
};

// What a command group answers: nullopt when the name is not one of its commands, else "" when
// it ran or why it failed.
using Outcome = std::optional<std::string>;
const Outcome kDone = std::string();

std::string exec_command(App::State& s, const std::string& cmd);

std::shared_ptr<ui::RemoteSession> session_of(App::State& s, const std::string& id) {
    const auto it = s.by_id.find(id);
    return it == s.by_id.end() ? nullptr : it->second;
}

// Ends a terminal (forced: no wait for it to quit by itself).
void end_terminal(App::State& s, const std::string& id) {
    if (const auto r = session_of(s, id))
        r->stop(true);
}

// A new terminal next to what is focused: in the active space, on its machine.
Placement placement_here(App::State& s, const std::string& focused, LaunchTarget target) {
    Placement p;
    p.target = target;
    p.split_of = focused;
    p.space = s.ws.active_space;
    p.space_id = s.ws.active() != nullptr ? s.ws.active()->id : std::string();
    p.machine = s.ws.active() != nullptr ? s.ws.active()->machine : std::string();
    return p;
}

std::string machine_of_session(App::State& s, const std::string& id) {
    return s.machine_of.count(id) ? s.machine_of[id] : std::string();
}

// launch, tab.new / space.new / pane.split, agent.new, space.new_on, session.restart.
Outcome exec_launch_command(App::State& s, const Command& c) {
    if (c.name == "launch") {
        // "<target>:<profile>", target space, right, down or (anything else) tab.
        const auto c2 = c.arg.find(':');
        const std::string target = c.arg.substr(0, c2);
        const std::string profile = c2 == std::string::npos ? std::string() : c.arg.substr(c2 + 1);
        const Placement p = placement_here(s, c.focused,
                                           target == "space"   ? LaunchTarget::kSpace
                                           : target == "right" ? LaunchTarget::kSplitRight
                                           : target == "down"  ? LaunchTarget::kSplitDown
                                                               : LaunchTarget::kTab);
        const std::string cwd = s.focused_cwd().empty() ? std::string(s.launch_cwd) : s.focused_cwd();
        if (!profile.empty())
            create_session(s, profile, cwd, "", p);
    } else if (c.name == "tab.new" || c.name == "space.new" || c.name == "pane.split") {
        // New spaces, tabs and splits open a shell in the focused folder: cd there, then run
        // claude / codex yourself (the pane follows the program running in it).
        Placement p = placement_here(s, c.focused,
                                     c.name == "space.new"                      ? LaunchTarget::kSpace
                                     : c.name == "tab.new" || c.focused.empty() ? LaunchTarget::kTab
                                     : c.arg == "down"                          ? LaunchTarget::kSplitDown
                                                                                : LaunchTarget::kSplitRight);
        if (s.machine_of.count(c.focused) && p.target != LaunchTarget::kSpace)
            p.machine = s.machine_of[c.focused];
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
    } else if (c.name == "agent.new") {
        open_launcher(s, !c.focused.empty() && c.arg == "space" ? LaunchTarget::kSpace : LaunchTarget::kTab);
    } else if (c.name == "space.new_on") {
        // A new space on machine `arg`, in its default folder.
        Placement p;
        p.target = LaunchTarget::kSpace;
        p.machine = c.arg;
        ui::HostClient* h = s.host_for(c.arg);
        const std::string shell = pick_shell(s.settings.shell_profile, h->profiles());
        if (h->connected() && !shell.empty())
            create_session(s, shell, "", "", p);
    } else if (c.name == "session.restart") {
        // A new session of the same profile in the same folder, in the old one's place.
        const auto it = s.views_by_id.find(c.focused);
        if (it != s.views_by_id.end()) {
            Placement p;
            p.replace = c.focused;
            p.machine = machine_of_session(s, c.focused);
            create_session(s, it->second.profile_id, it->second.cwd, it->second.name, p);
        }
    } else {
        return std::nullopt;
    }
    return kDone;
}

// The focused pane and its terminal: close, park, zoom, move focus, swap, resize, rename, search,
// clipboard, right-click mode, pin, interrupt and stop.
Outcome exec_pane_command(App::State& s, const Command& c) {
    if (c.name == "pane.close") {
        // Ends the terminal; the tab's last pane closes the tab (which may ask about the space).
        if (c.focused.empty())
            return "no pane to close";
        const auto* t = s.ws.active_tab();
        if (t != nullptr && s.ws.sessions_in(*t).size() == 1)
            return exec_command(s, "tab.close");
        end_terminal(s, c.focused);
        s.hidden.insert(c.focused);
        s.ws.remove(c.focused);
        s.focus_view(s.ws.focused_session());
    } else if (c.name == "terminal.kill") {
        // A terminal (default: the focused one) and its sub-agents.
        const std::string target = c.arg.empty() ? c.focused : c.arg;
        for (const auto& [id, v] : s.views_by_id)
            if (id == target || v.parent == target)
                end_terminal(s, id);
    } else if (c.name == "pane.park") {
        // A sub-agent's pane goes away; it keeps running under its parent (agents list, peek).
        if (!c.focused.empty()) {
            s.hidden.insert(c.focused);
            s.ws.remove(c.focused);
            s.focus_view(s.ws.focused_session());
        }
    } else if (c.name == "pane.zoom") {
        s.ws.toggle_zoom();
    } else if (c.name == "pane.go") {
        if (s.ws.focus_dir(parse_dir(c.arg), s.pane_area))
            s.focus_view(s.ws.focused_session());
    } else if (c.name == "pane.swap") {
        s.ws.swap_dir(parse_dir(c.arg), s.pane_area);
    } else if (c.name == "pane.resize") {
        s.ws.resize(parse_dir(c.arg), px(40), s.pane_area);
    } else if (c.name == "pane.rename") {
        if (c.focused.empty())
            return "no pane to rename";
        open_rename(s, "pane", c.focused, s.pane_title(c.focused));
    } else if (c.name == "terminal.search") {
        if (!c.focused.empty())
            s.views[c.focused].view.open_search();
    } else if (c.name == "terminal.copy" || c.name == "terminal.paste" || c.name == "terminal.paste_image" ||
               c.name == "terminal.select_all" || c.name == "terminal.scroll") {
        // Run by the focused terminal on its next frame (palette, keys.json in global).
        if (!c.focused.empty())
            s.views[c.focused].view.request(c.cmd);
    } else if (c.name == "terminal.right_click") {
        // "program": right-clicks go to the program; anything else: AgentDesk's menu.
        if (!c.focused.empty()) {
            if (c.arg == "program")
                s.ws.right_click_program.insert(c.focused);
            else
                s.ws.right_click_program.erase(c.focused);
            s.doc_changed();
        }
    } else if (c.name == "terminal.pin") {
        // Pins the focused agent ("true" / "false"; no argument toggles).
        if (!c.focused.empty()) {
            if (c.arg == "false")
                s.ws.pinned.erase(c.focused);
            else if (c.arg == "true" || !s.ws.pinned.erase(c.focused))
                s.ws.pinned.insert(c.focused);
            s.doc_changed();
        }
    } else if (c.name == "session.interrupt") {
        if (const auto r = session_of(s, c.focused))
            r->send(host::RawInput{std::string("\x03", 1)});
    } else if (c.name == "session.stop" || c.name == "session.force_stop") {
        if (const auto r = session_of(s, c.focused))
            r->stop(c.name == "session.force_stop");
    } else {
        return std::nullopt;
    }
    return kDone;
}

// Closes the tab at `loc`; a space's last tab asks about closing the space instead.
void close_tab_at(App::State& s, ui::PaneLoc loc) {
    auto& sp = s.ws.spaces[static_cast<std::size_t>(loc.space)];
    if (sp.tabs.size() == 1) {
        confirm_close_space(s, loc.space);
        return;
    }
    for (const auto& id : s.ws.sessions_in(sp.tabs[static_cast<std::size_t>(loc.tab)])) {
        end_terminal(s, id);
        s.hidden.insert(id);
    }
    s.ws.close_tab(loc.space, loc.tab);
    s.focus_view(s.ws.focused_session());
}

// Tabs and spaces: rename, close, move, select, cycle, settle, and the per-space panel.
Outcome exec_tab_space_command(App::State& s, const Command& c) {
    const std::string& arg = c.arg;
    if (c.name == "tab.rename") {
        // The tab with id `arg` (default: the shown one), without selecting it.
        const auto loc = arg.empty() ? ui::PaneLoc{} : ui::find_tab(s.ws, arg);
        const ui::Tab* t = !arg.empty() ? (loc.space < 0 ? nullptr
                                                         : &s.ws.spaces[static_cast<std::size_t>(loc.space)]
                                                                .tabs[static_cast<std::size_t>(loc.tab)])
                                        : s.ws.active_tab();
        if (t == nullptr)
            return arg.empty() ? std::string("no tab to rename") : "no tab \"" + arg + "\"";
        open_rename(s, "tab", t->id, t->name);
    } else if (c.name == "space.rename") {
        auto* sp = s.ws.active();
        if (sp == nullptr)
            return "no space to rename";
        open_rename(s, "space", sp->id, sp->name);
    } else if (c.name == "tab.close") {
        // The tab with id `arg` (the shown tab and space stay shown), default the shown tab. Its
        // terminals end.
        ui::PaneLoc loc;
        if (arg.empty()) {
            if (s.ws.active_tab() != nullptr)
                loc = {s.ws.active_space, s.ws.active()->active_tab};
        } else {
            loc = ui::find_tab(s.ws, arg);
        }
        if (loc.space < 0)
            return arg.empty() ? std::string("no tab to close") : "no tab \"" + arg + "\"";
        close_tab_at(s, loc);
    } else if (c.name == "space.close") {
        // Asks first ("Close space?"); space.close_now does it.
        if (s.ws.active() == nullptr)
            return "no space to close";
        confirm_close_space(s, s.ws.active_space);
    } else if (c.name == "space.close_now") {
        // The space with id `arg`, ending its terminals.
        for (int i = 0; i < static_cast<int>(s.ws.spaces.size()); ++i) {
            const auto& sp = s.ws.spaces[static_cast<std::size_t>(i)];
            if (sp.id != arg)
                continue;
            for (const auto& t : sp.tabs)
                for (const auto& id : s.ws.sessions_in(t))
                    end_terminal(s, id);
            s.ws.close_space(i);
            s.focus_view(s.ws.focused_session());
            break;
        }
    } else if (c.name == "space.move") {
        // Moves the active space to position `arg` (1 = first).
        const int to = std::atoi(arg.c_str()) - 1;
        if (to >= 0 && s.ws.move_space(s.ws.active_space, to))
            s.doc_changed();
    } else if (c.name == "tab.goto" || c.name == "tab.close_at") {
        // "<space index>:<tab index>", both from 0.
        const auto colon = arg.find(':');
        const int sp = std::atoi(arg.substr(0, colon).c_str());
        const int t = colon == std::string::npos ? 0 : std::atoi(arg.substr(colon + 1).c_str());
        if (sp < 0 || sp >= static_cast<int>(s.ws.spaces.size()) || t < 0 ||
            t >= static_cast<int>(s.ws.spaces[static_cast<std::size_t>(sp)].tabs.size()))
            return "no such tab";
        if (c.name == "tab.close_at")  // by id, so the shown tab and space stay
            return exec_command(
                s, "tab.close:" +
                       s.ws.spaces[static_cast<std::size_t>(sp)].tabs[static_cast<std::size_t>(t)].id);
        s.ws.select_space(sp);
        s.ws.select_tab(t);
        s.focus_view(s.ws.focused_session());
    } else if (c.name == "tab.move") {
        if (auto* sp = s.ws.active())
            s.ws.move_tab(s.ws.active_space, sp->active_tab, sp->active_tab + (arg == "left" ? -1 : 1));
    } else if (c.name == "tab.settle" || c.name == "tab.unsettle") {
        // Settling puts a tab away (out of the bar, still running) until it is shown again or
        // needs the person.
        if (auto* sp = s.ws.active()) {
            if (c.name == "tab.unsettle") {
                for (int i = 0; i < static_cast<int>(sp->tabs.size()); ++i)
                    s.ws.unsettle_tab(s.ws.active_space, i);
            } else if (!s.ws.settle_tab(s.ws.active_space, sp->active_tab)) {
                s.toast("Nothing to put away", "The last tab shown in a space stays.", "", g_pal->info);
            }
            s.doc_changed();
            s.focus_view(s.ws.focused_session());
        }
    } else if (c.name == "tab.next" || c.name == "tab.prev") {
        s.ws.cycle_tab(c.name == "tab.next" ? 1 : -1);
        s.focus_view(s.ws.focused_session());
    } else if (c.name == "tab.select") {
        s.ws.select_shown_tab(std::atoi(arg.c_str()));
        s.focus_view(s.ws.focused_session());
    } else if (c.name == "space.next" || c.name == "space.prev") {
        s.ws.cycle_space(c.name == "space.next" ? 1 : -1);
        s.focus_view(s.ws.focused_session());
    } else if (c.name == "space.select") {
        s.ws.select_space(std::atoi(arg.c_str()) - 1);
        s.focus_view(s.ws.focused_session());
    } else if (c.name == "panel") {
        // The Files and Changes buttons: the panel `arg` (default files) for the active space,
        // or none when it is already showing.
        if (auto* sp = s.ws.active()) {
            const std::string which = arg.empty() ? std::string("files") : arg;
            sp->panel = sp->panel == which ? std::string() : which;
            s.doc_changed();
        }
    } else {
        return std::nullopt;
    }
    return kDone;
}

// Moving between agents, peeking at a sub-agent and giving it a pane.
Outcome exec_agent_command(App::State& s, const Command& c) {
    if (c.name == "agent.next" || c.name == "agent.prev") {
        if (!s.agent_order.empty()) {
            auto it = std::find(s.agent_order.begin(), s.agent_order.end(), c.focused);
            int i = it == s.agent_order.end() ? -1 : static_cast<int>(it - s.agent_order.begin());
            const int n = static_cast<int>(s.agent_order.size());
            i = c.name == "agent.next" ? (i + 1) % n : (i <= 0 ? n - 1 : i - 1);
            s.focus(s.agent_order[static_cast<std::size_t>(i)]);
        }
    } else if (c.name == "agent.focus" || c.name == "session.focus") {
        s.focus(c.arg);
    } else if (c.name == "agent.peek") {
        s.peek = c.arg;
        s.views[c.arg].focus_next = true;
    } else if (c.name == "agent.keep") {
        // Gives a sub-agent a pane: beside its parent when the parent is shown, else a new tab.
        const auto it = s.views_by_id.find(c.arg);
        if (it != s.views_by_id.end() && s.ws.find(c.arg).space < 0) {
            s.hidden.erase(c.arg);
            if (!it->second.parent.empty() && s.ws.find(it->second.parent).space >= 0)
                s.ws.split(it->second.parent, c.arg, Dir::kRight);
            else
                s.ws.add_tab(c.arg, s.space_for_cwd(it->second.cwd));
        }
        s.peek.clear();
        s.focus(c.arg);
    } else {
        return std::nullopt;
    }
    return kDone;
}

// worktree.remove_now / worktree.remove_force "<machine>|<top>" (an older "<top>" means this
// computer): ends the terminals of the spaces in that checkout (they hold the folder open), then
// git worktree remove from the main checkout.
std::string remove_worktree(App::State& s, const std::string& arg, bool force) {
    const auto [machine, top] = split_machine(arg);
    const auto g = s.git.get(top, machine);
    const std::string main = g.common_top.empty() ? top : g.common_top;
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
    if (force)
        argv.push_back(L"--force");
    argv.push_back(win::to_wide(top));
    // Removing a worktree with large submodules can take minutes.
    const auto r = git_at(s, machine, main, argv, std::chrono::seconds(600));
    s.git.invalidate(top);
    if (!git_ok(r))  // shown as "worktree.remove failed: ..."
        return r.started ? r.message() : std::string("git did not run");
    return {};
}

// Creating, opening and removing git worktrees.
Outcome exec_worktree_command(App::State& s, const Command& c) {
    if (c.name == "worktree.new") {
        s.show_worktree = true;
        s.wt_error.clear();
        std::snprintf(s.wt_repo, sizeof s.wt_repo, "%s", s.focused_cwd().c_str());
        s.wt_branch[0] = '\0';
    } else if (c.name == "worktree.open") {
        // The focused repo's worktrees, in a dialog; outside git the dialog says so.
        s.worktree_list.clear();
        s.worktree_not_repo.clear();
        const std::string cwd = s.focused_cwd();
        const std::string machine = s.machine_of[s.ws.focused_session()];
        s.worktree_machine = machine;
        s.git.get(cwd, machine);  // asks git about the folder now, if it has not yet
        if (!cwd.empty())
            s.worktree_list = list_worktrees(s, machine, cwd);
        if (s.worktree_list.empty())
            s.worktree_not_repo = cwd.empty() ? std::string("This folder") : display_path(cwd);
        s.show_worktree_open = true;
    } else if (c.name == "worktree.open_at") {
        // "<machine>|<path>": a space on that machine already in the checkout, else a new one.
        const auto [machine, path] = split_machine(c.arg);
        for (int i = 0; i < static_cast<int>(s.ws.spaces.size()); ++i) {
            const auto& sp = s.ws.spaces[static_cast<std::size_t>(i)];
            if (sp.machine != machine || sp.tabs.empty())
                continue;
            const auto ids = s.ws.sessions_in(sp.tabs.front());
            if (!ids.empty() && s.views_by_id.count(ids.front()) &&
                _stricmp(s.git.get(s.views_by_id[ids.front()].cwd, machine).top.c_str(), path.c_str()) == 0) {
                s.ws.select_space(i);
                s.focus_view(s.ws.focused_session());
                return kDone;
            }
        }
        Placement p;
        p.target = LaunchTarget::kSpace;
        p.machine = machine;
        const std::string shell = pick_shell(s.settings.shell_profile, s.host_for(machine)->profiles());
        if (!shell.empty())
            create_session(s, shell, path, "", p);
    } else if (c.name == "worktree.remove") {
        // Asks first, saying what would be lost.
        const std::string machine = s.machine_of[s.ws.focused_session()];
        const auto g = s.git.get(s.focused_cwd(), machine);
        if (!g.repo)
            return "not a git checkout";
        if (!g.worktree)
            return "not a worktree: this is the repository's main checkout";
        const auto risk = worktree_risk(s, machine, g.top);
        const auto loc = s.ws.find(s.ws.focused_session());
        open_confirm(s,
                     "Remove worktree " +
                         (loc.space >= 0 ? s.ws.space_title(loc.space, s.session_cwds()) : g.top) + "?",
                     worktree_remove_text(g.top, risk), "Remove",
                     "worktree.remove_checked:" + risk.signature() + "|" + machine + "|" + g.top);
    } else if (c.name == "worktree.remove_checked") {
        // "<signature>|<machine>|<top>": confirmed. Checks again; a risk that appeared since the
        // dialog opened asks once more ("Enter again removes it anyway").
        const auto bar = c.arg.find('|');
        const auto bar2 = bar == std::string::npos ? std::string::npos : c.arg.find('|', bar + 1);
        const std::string seen = c.arg.substr(0, bar);
        const std::string machine =
            bar2 == std::string::npos ? std::string() : c.arg.substr(bar + 1, bar2 - bar - 1);
        const std::string top = bar2 == std::string::npos ? std::string() : c.arg.substr(bar2 + 1);
        const auto risk = worktree_risk(s, machine, top);
        const std::string remove =
            risk.dirty || risk.submodules ? "worktree.remove_force:" : "worktree.remove_now:";
        if (risk.signature() == seen)
            return exec_command(s, remove + machine + "|" + top);
        open_confirm(s, "Remove worktree?",
                     worktree_remove_text(top, risk) +
                         "\n\nIt changed since this opened. Enter again removes it anyway.",
                     "Remove", remove + machine + "|" + top);
    } else if (c.name == "worktree.remove_now" || c.name == "worktree.remove_force") {
        return remove_worktree(s, c.arg, c.name == "worktree.remove_force");
    } else {
        return std::nullopt;
    }
    return kDone;
}

// The escape hatch: keys.json set aside, the workspace layout with its sidebar as shipped.
void reset_to_default_screen(App::State& s) {
    const auto keys = data_dir() / L"keys.json";
    std::error_code ec;
    if (std::filesystem::exists(keys, ec))
        std::filesystem::rename(keys, std::filesystem::path(keys).concat(L".aside"), ec);
    s.keymap = ui::Keymap::defaults();
    s.keymap_errors.clear();
    s.settings.layout = "workspace";
    save_settings(s.settings);
    if (lay::Node* side = s.layout().tree.find("sidebar")) {
        const lay::Node* shipped = s.layout().file.find("sidebar");
        side->hidden = shipped != nullptr && shipped->hidden;
        side->size = shipped != nullptr ? shipped->size : side->size;
        save_layout_state(s.layout());
    }
    s.toast("Back to the default screen", "keys.json was set aside as keys.json.aside.", "", g_pal->info);
}

// Collapses the sidebar to a rail (44 px, 76 px with the round window buttons), remembering its width, or widens it back (at least
// 160 px). A hidden sidebar is shown first.
std::string toggle_sidebar_rail(App::State& s) {
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
        side->size = lay::Span::pixels(mac_window_buttons(s) ? 76.0f : 44.0f);  // the rail holds the round buttons
    } else {
        side->size = lay::Span::pixels(std::max(160.0f, s.settings.sidebar_restore));
    }
    save_layout_state(l);
    return {};
}

// Window, overlays, zoom, theme, keys.json and updates.
Outcome exec_app_command(App::State& s, const Command& c) {
    const std::string& name = c.name;
    const std::string& arg = c.arg;
    if (name == "overlay.close") {  // the floating overlays
        s.show_palette = s.show_picker = s.show_help = false;
        s.peek.clear();
    } else if (name == "app.reset") {
        reset_to_default_screen(s);
    } else if (name == "app.picker") {
        s.show_picker = !s.show_picker;
        s.picker_buf[0] = '\0';
        s.picker_sel = -1;
        s.picker_reset = true;
    } else if (name == "app.hide" || name == "app.hide_others") {
        app_hide(name == "app.hide_others");
    } else if (name == "window.minimize") {
        window_minimize(s.config.hwnd);
    } else if (name == "window.maximize") {
        window_toggle_maximize(s.config.hwnd);
    } else if (name == "window.close") {
        window_close(s.config.hwnd);
    } else if (name == "window.fill") {
        // The Mac layout's window fills the desktop (no margins), or floats again.
        s.mac.fill = !s.mac.fill;
    } else if (name == "machine.add") {
        s.show_machine_add = true;
    } else if (name == "app.welcome") {
        s.show_welcome = true;
    } else if (name == "sidebar.rail") {
        return toggle_sidebar_rail(s);
    } else if (name == "focus.toggle" || name == "focus.on" || name == "focus.off") {
        set_focus(s, name == "focus.on" || (name == "focus.toggle" && !s.settings.focus_on));
    } else if (name == "sidebar.toggle") {
        // In Focus the sidebar is not drawn: asking for it leaves Focus, and shows it if hidden.
        if (s.settings.focus_on) {
            set_focus(s, false);
            const lay::Node* side = s.layout().tree.find("sidebar");
            if (side == nullptr || !side->hidden)
                return kDone;
        }
        // layout.toggle on the "sidebar" node. A layout without one (terminals) switches to the first
        // layout that has a sidebar, shown, so "show the sidebar" works from every layout.
        if (s.layout().tree.find("sidebar") != nullptr)
            return *layout_command(s, "layout.toggle", "sidebar");
        for (auto& l : s.layouts)
            if (lay::Node* side = l.tree.find("sidebar"); side != nullptr) {
                if (side->hidden) {
                    side->hidden = false;
                    save_layout_state(l);
                }
                return exec_command(s, "layout.switch:" + l.name);
            }
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
        // `arg` names the page to open on ("mods", "alerts", ...).
        s.show_settings = true;
        for (const char* sec : {"Look", "Keyboard", "Machines", "Alerts", "Mods", "Updates", "Privacy"})
            if (_stricmp(arg.c_str(), sec) == 0)
                s.settings_tab = sec;
    } else if (name == "app.zoom_in" || name == "app.zoom_out" || name == "app.zoom_reset") {
        // Steps of 0.1 between 0.5 and 3.
        s.settings.zoom =
            name == "app.zoom_reset"
                ? 1.0f
                : std::clamp(
                      std::round((s.settings.zoom + (name == "app.zoom_in" ? 0.1f : -0.1f)) * 10.0f) / 10.0f,
                      0.5f, 3.0f);
        save_settings(s.settings);
        apply_zoom(s);
    } else if (name == "app.keys") {
        const auto path = data_dir() / L"keys.json";
        load_keys(s);  // writes the defaults when the file is missing
        os::open_text_file(path);
        s.toast("keys.json opened", "Saved changes apply right away.", "", g_pal->info);
    } else if (name == "app.exit_all") {
        s.show_exit = true;
        std::lock_guard lock(s.placements_mutex);
        s.exit_error.clear();
    } else if (name == "theme") {
        // The theme goes on the shown layout, as its light or its dark half by the theme's kind;
        // the other half and the Appearance setting stay.
        const Palette* p = palette_by_id(arg);
        auto pair = layout_theme_pair(s.settings);
        (p->light ? pair.second : pair.first) = p->id;
        s.settings.layout_themes[s.settings.layout] = pair;
        s.settings.theme = p->id;
        save_settings(s.settings);
    } else if (name == "app.check_updates") {
        ui::updates::check_now();
    } else if (name == "app.update_core") {
        // `agentctl update` in the background when a new agent-host is waiting or the last try
        // failed. agent-host hands its terminals to the new one; they keep running (design doc
        // 7.4 OQ-09).
        const auto core = ui::updates::core();
        if (!core.ready && !core.error)
            return "agent-host is up to date";
        ui::updates::update_core();
    } else if (name == "app.update") {
        // Puts the downloaded AgentDesk in place and starts it.
        std::string error;
        if (!ui::updates::apply(&error)) {
            if (error.empty())
                return "no update is ready yet";
            return "Close AgentDesk's terminals (or restart the computer) and try again. Couldn't replace "
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
    } else {
        return std::nullopt;
    }
    return kDone;
}

// Letters, digits, - or _: a layout name that is also a safe file name.
bool valid_layout_name(const std::string& n) {
    return !n.empty() && std::all_of(n.begin(), n.end(), [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '-' || c == '_';
    });
}

// layout.clone {name, as}: the layout (default: the shown one) as a new file `as` (default
// <name>-copy), never over a file that is there, marked cloned_from, with its clicks; then shown.
// An old-format layout is written in the current format.
std::string clone_layout(App::State& s, const std::string& arg) {
    const Json a = layout_args(arg, "name", "as");
    const std::string from =
        a.value("name", std::string()).empty() ? s.settings.layout : a.value("name", std::string());
    const std::string to =
        a.value("as", std::string()).empty() ? from + "-copy" : a.value("as", std::string());
    if (!valid_layout_name(from) || !valid_layout_name(to))
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
    return {};
}

// layout.reset [name]: only a layout AgentDesk ships. The person's file of that name is set
// aside as <name>.json.old-<time> (never over an older one) and its clicks are dropped.
std::string reset_layout(App::State& s, const std::string& arg) {
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
    s.toast("Layout reset: " + which, aside > 0 ? "Your file was kept aside as <name>.json.old-<time>." : "",
            "", g_pal->info);
    return {};
}

// mod.clone <mod>: copies the mod folder to <mod>-copy (then -copy-2, ...); without a name, asks.
void clone_mod(App::State& s, const std::string& arg) {
    if (arg.empty()) {
        open_rename(s, "mod.clone", "", "");
        return;
    }
    const auto dir = data_dir() / L"mods";
    const auto src = dir / win::to_wide(arg);
    std::error_code ec;
    if (arg.find_first_of("/\\:") != std::string::npos || arg == "." || arg == ".." ||
        !std::filesystem::is_directory(src, ec)) {
        s.toast("No mod named " + arg, "Mods are folders in the mods folder.", "", g_pal->info);
        return;
    }
    std::string to = arg + "-copy";
    for (int n = 2; std::filesystem::exists(dir / win::to_wide(to), ec); ++n)
        to = arg + "-copy-" + std::to_string(n);
    std::filesystem::copy(src, dir / win::to_wide(to), std::filesystem::copy_options::recursive, ec);
    if (ec)
        s.toast("Could not duplicate " + arg, ec.message(), "", g_pal->needs_you);
    else
        s.toast("Mod duplicated", "mods\\" + to + " (off until you enable it)", "", g_pal->info);
}

// Switching, cloning and resetting layouts; mods.
Outcome exec_layout_mod_command(App::State& s, const Command& c) {
    if (c.name == "layout.switch" || c.name == "layout") {
        // `arg` names the layout; without one, the next listed one (fun layouts only when Settings >
        // Look > Fun layouts is on).
        std::string next = c.arg;
        if (next.empty()) {
            std::vector<std::string> names;
            for (const auto& l : s.layouts)
                names.push_back(l.name);
            next = ui::styles::next_listed(names, s.settings.layout, s.settings.styles.fun_layouts);
        }
        if (next.empty() && !s.layouts.empty())
            next = s.layouts.front().name;
        s.settings.layout = next;
        save_settings(s.settings);
        s.toast("Layout: " + layout_display_name(next), "", "", g_pal->info, 2);
        s.focus_view(s.ws.focused_session());
        if (s.layout().style == "inbox")
            inbox_entered(s);  // the queue, not the terminal, takes the keys
    } else if (c.name == "layout.clone") {
        return clone_layout(s, c.arg);
    } else if (c.name == "layout.reset") {
        return reset_layout(s, c.arg);
    } else if (c.name == "mod.clone") {
        clone_mod(s, c.arg);
    } else if (c.name == "mods.reload") {
        s.layouts = load_layouts(s.settings);
        s.mods->load(s.settings.mods_enabled);
        s.toast("Mods reloaded", "", "", g_pal->info, 2);
    } else if (c.name == "mods.folder") {
        const auto dir = data_dir() / L"mods";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        os::open_path(dir);
    } else if (c.name == "mod") {
        const auto c2 = c.arg.find(':');
        if (c2 == std::string::npos)
            return "mod needs <mod>:<command>";
        s.mods->run_command(c.arg.substr(0, c2), c.arg.substr(c2 + 1));
    } else {
        return std::nullopt;
    }
    return kDone;
}

// Runs a command and says why it failed ("" when it ran): an unknown command, a missing target,
// or a git step that failed.
std::string exec_command(App::State& s, const std::string& cmd) {
    const auto colon = cmd.find(':');
    const Command c{cmd, cmd.substr(0, colon),
                    colon == std::string::npos ? std::string() : cmd.substr(colon + 1),
                    s.ws.focused_session()};
    // Other spellings of commands AgentDesk has, so a keys.json written with them keeps working.
    // "app.quit" closes the window; the agents keep running.
    static const std::map<std::string, std::string> kCommandAliases = {{"terminal.park", "pane.park"},
                                                                       {"terminal.peek", "agent.peek"},
                                                                       {"terminal.open", "tab.new"},
                                                                       {"app.quit", "window.close"}};
    if (c.name == "layout.toggle" && c.arg.empty())  // the panel (AgentDesk's older spelling)
        return exec_command(s, "panel");
    if (auto r = layout_command(s, c.name, c.arg))
        return *r;
    if (auto r = stage_command(s, c.name, c.arg))  // stage_layout.cpp
        return *r;
    if (auto r = inbox_command(s, c.name, c.arg))  // inbox_layout.cpp
        return *r;
    if (auto r = exec_review_command(s, c.name, c.arg))  // review_layout.cpp
        return *r;
    if (auto r = exec_pair_command(s, c.name, c.arg))  // pair_layout.cpp
        return *r;
    if (auto r = exec_timeline_command(s, c.name, c.arg))  // timeline.cpp
        return *r;
    if (auto r = exec_tips_command(s, c.name, c.arg))  // layout.tips (layout_tips.cpp)
        return *r;
    if (const auto al = kCommandAliases.find(c.name); al != kCommandAliases.end())
        return exec_command(s, c.arg.empty() ? al->second : al->second + ":" + c.arg);
    if (c.name == "mod.open" && c.arg == "picker")
        return exec_command(s, "app.picker");
    if (auto r = exec_theme_command(s, c.name, c.arg))  // appearance:*, theme.* (theme_commands.cpp)
        return *r;
    if (auto r = exec_style_command(s, c.name, c.arg))  // Terminals' tools, Classic (style_layouts.cpp)
        return *r;
    for (const auto group :
         {exec_launch_command, exec_pane_command, exec_tab_space_command, exec_agent_command,
          exec_worktree_command, exec_app_command, exec_layout_mod_command})
        if (auto r = group(s, c))
            return *r;
    // "<mod>.<command>": keys.json accepts it when that mod folder exists.
    if (const auto dot = c.name.find('.');
        dot != std::string::npos && mod_folder_exists(c.name.substr(0, dot))) {
        s.mods->run_command(c.name.substr(0, dot), c.name.substr(dot + 1));
        return {};
    }
    return "unknown command \"" + c.name + "\"";
}

}  // namespace

// A command run for the person (a click, a key, a menu, a mod) has nobody to read its answer, so
// a failure shows as an error toast "<command> failed".
void run_command(App::State& s, const std::string& cmd, bool person) {
    if (s.defer_commands) {
        s.deferred_commands.push_back({cmd, person, s.ws.focused_session()});
        return;
    }
    // Telemetry counts the commands the person ran, by name only.
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
