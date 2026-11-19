// AgentDesk 98 chrome: command helpers, the panes' title bars, the Start menu, its commands and
// the taskbar.
// Portions adapted from herdr-app (Apache-2.0) and modified; see THIRD_PARTY_NOTICES.md.
#include "win98_internal.hpp"

namespace agentdesk::desktop {
namespace detail::w98 {

// ---------------------------------------------------------------- helpers: workspace commands by id

std::string status_word(Attention a) {
    switch (a) {
        case Attention::kBlocked:
            return "blocked";
        case Attention::kWorking:
            return "working";
        case Attention::kDone:
            return "done";
        case Attention::kIdle:
            return "idle";
        default:
            return {};
    }
}

std::string status_badge(const std::string& status) {
    return status == "blocked"   ? "warn-badge"
           : status == "working" ? "hourglass"
           : status == "done"    ? "check-badge"
                                 : "";
}

namespace {
void add_attention(App::State& s, const ui::Tab& t, std::vector<Attention>& out) {
    for (const auto& id : s.ws.sessions_in(t))
        out.push_back(s.attention_of(id));
}

// The most urgent status of the tab's sessions.
std::string tab_status(App::State& s, const ui::Tab& t) {
    std::vector<Attention> atts;
    add_attention(s, t, atts);
    return status_word(ui::most_urgent(atts));
}

}  // namespace

std::string space_status(App::State& s, const ui::Space& sp) {
    std::vector<Attention> atts;
    for (const auto& t : sp.tabs)
        add_attention(s, t, atts);
    return status_word(ui::most_urgent(atts));
}

std::string session_place(App::State& s, const std::string& id,
                          const std::map<std::string, std::string>& cwds) {
    const auto loc = s.ws.find(id);
    std::string sname, tname;
    if (loc.space >= 0) {
        sname = s.ws.space_title(loc.space, cwds);
        tname = ui::Workspace::tab_title(
            s.ws.spaces[static_cast<std::size_t>(loc.space)].tabs[static_cast<std::size_t>(loc.tab)],
            loc.tab);
    }
    return sname + " \\ " + tname;
}

std::string user_name_now() {
    return k98::user_name(home_dir());
}

namespace {
void dirty(App::State& s) {
    s.doc_dirty = true;
    s.doc_dirty_at = Clock::now();
}

}  // namespace

void select_space_id(App::State& s, const std::string& id) {
    for (int i = 0; i < static_cast<int>(s.ws.spaces.size()); ++i)
        if (s.ws.spaces[static_cast<std::size_t>(i)].id == id) {
            s.ws.select_space(i);
            s.focus_view(s.ws.focused_session());
            dirty(s);
        }
}

namespace {
// Selects the tab and its space; a settled tab is shown again.
void select_tab_id(App::State& s, const std::string& id) {
    const auto loc = ui::find_tab(s.ws, id);
    if (loc.space < 0)
        return;
    s.ws.select_space(loc.space);
    s.ws.select_tab(loc.tab);
    s.focus_view(s.ws.focused_session());
    dirty(s);
}

// Settles (minimizes) a tab: the shown one hands over to the next unsettled tab; the last one
// stays shown, settled.
void settle_tab_id(App::State& s, const std::string& id) {
    const auto loc = ui::find_tab(s.ws, id);
    if (loc.space < 0)
        return;
    s.ws.settle_tab(loc.space, loc.tab, true);
    s.focus_view(s.ws.focused_session());
    dirty(s);
}

void rename_tab_id(App::State& s, const std::string& id, const std::string& name) {
    const auto loc = ui::find_tab(s.ws, id);
    if (loc.space < 0)
        return;
    s.ws.spaces[static_cast<std::size_t>(loc.space)].tabs[static_cast<std::size_t>(loc.tab)].name = name;
    dirty(s);
}

}  // namespace

void rename_space_id(App::State& s, const std::string& id, const std::string& name) {
    for (auto& sp : s.ws.spaces)
        if (sp.id == id)
            sp.name = name;
    dirty(s);
}

// A terminal in a new tab (a shell, or the program asked for).
void new_tab(App::State& s, const std::string& name, int space, const std::string& profile,
             const std::vector<std::string>& args) {
    Placement p;
    p.target = LaunchTarget::kTab;
    p.tab_name = name;
    p.space = space >= 0 ? space : s.ws.active_space;
    p.machine = p.space >= 0 && p.space < static_cast<int>(s.ws.spaces.size())
                    ? s.ws.spaces[static_cast<std::size_t>(p.space)].machine
                    : std::string();
    ui::HostClient* h = s.host_for(p.machine);
    const std::string prof = profile.empty() ? pick_shell(s.settings.shell_profile, h->profiles()) : profile;
    if (!h->connected() || prof.empty()) {
        open_launcher(s, LaunchTarget::kTab);
        return;
    }
    std::string cwd = s.focused_cwd();
    if (cwd.empty())
        cwd = s.launch_cwd;
    create_session(s, prof, cwd, name, p, args);
}

// A new space with a shell, on this computer (machine "") or a remote one.
void new_space(App::State& s, const std::string& name, const std::string& machine) {
    Placement p;
    p.target = LaunchTarget::kSpace;
    p.machine = machine;
    p.space_name = name;
    ui::HostClient* h = s.host_for(machine);
    const std::string shell = pick_shell(s.settings.shell_profile, h->profiles());
    if (!h->connected() || shell.empty()) {
        open_launcher(s, LaunchTarget::kSpace);
        return;
    }
    create_session(s, shell, machine.empty() ? s.focused_cwd() : std::string(), name, p);
}

Dialog prompt(const std::string& title, const std::string& text, const std::string& icon,
              std::function<void(const std::string&, const std::string&)> cmd,
              std::optional<std::string> value, bool machines) {
    Dialog d;
    d.kind = "prompt";
    d.title = title;
    d.text = text;
    d.icon = icon;
    d.command = std::move(cmd);
    d.value = value;
    d.input = value.value_or("");
    d.machines = machines;
    return d;
}

Dialog message(const std::string& title, const std::string& text, const std::string& icon) {
    Dialog d;
    d.kind = "message";
    d.title = title;
    d.text = text;
    d.icon = icon;
    return d;
}

Dialog kind_dialog(const std::string& kind) {
    Dialog d;
    d.kind = kind;
    return d;
}

Dialog new_space_prompt(App::State& s) {
    return prompt(
        "New Space", "Type a name for the new space:", "folder",
        [&s](const std::string& n, const std::string& m) { new_space(s, n, m); }, std::nullopt, true);
}

Dialog new_tab_prompt(App::State& s) {
    return prompt("New Tab", "Type a name for the new tab:", "dos",
                  [&s](const std::string& n, const std::string&) { new_tab(s, n); });
}

Dialog rename_tab_prompt(App::State& s, const std::string& id, const std::string& current) {
    return prompt(
        "Rename", "Type a new name for this tab:", "rename",
        [&s, id](const std::string& n, const std::string&) { rename_tab_id(s, id, n); }, current);
}

Dialog rename_space_prompt(App::State& s, const std::string& id, const std::string& current) {
    return prompt(
        "Rename", "Type a new name for this space:", "folder",
        [&s, id](const std::string& n, const std::string&) { rename_space_id(s, id, n); }, current);
}

namespace {
// Asks before closing a tab.
Dialog close_tab_ask(App::State& s, const ui::Tab& tab, int index) {
    Dialog d;
    d.kind = "confirm";
    d.title = "Close Tab";
    d.icon = "warning";
    d.text = "Are you sure you want to close '" + ui::Workspace::tab_title(tab, index) +
             "'?\nIts programs will be ended.";
    const std::string id = tab.id;
    d.buttons = {{"&Yes", true, [&s, id] { run_command(s, "tab.close:" + id); }}, {"&No", false, nullptr}};
    return d;
}

}  // namespace

Dialog close_space_ask(App::State& s, int index) {
    const auto& sp = s.ws.spaces[static_cast<std::size_t>(index)];
    Dialog d;
    d.kind = "confirm";
    d.title = "Confirm Folder Delete";
    d.icon = "warning";
    d.text = "Are you sure you want to close the space '" + s.ws.space_title(index, s.session_cwds()) +
             "' and all its tabs?\nEvery program in it will be ended.";
    const std::string id = sp.id;
    d.buttons = {{"&Yes", true, [&s, id] { run_command(s, "space.close_now:" + id); }},
                 {"&No", false, nullptr}};
    return d;
}

// Every terminal that has not finished (Close Program's list).
std::vector<Task> tasks(App::State& s) {
    std::vector<Task> out;
    for (const auto& sess : s.sessions) {
        const auto it = s.views_by_id.find(sess->id());
        if (it == s.views_by_id.end())
            continue;
        const auto& v = it->second;
        if (v.process_state == "Exited" || v.lost)
            continue;
        const ui::Program prog = program_of(v);
        Task t;
        t.address = v.id;
        t.program = prog;
        t.name = prog.name;
        t.status = is_agent(v) ? status_word(s.attention_of(v.id)) : std::string();
        out.push_back(t);
    }
    return out;
}

// The desktop's color ("#rrggbb"), or "clouds" for the picture.
void set_desktop(App::State& s, const std::string& c) {
    set_shared(s, "win98.desktop", c);
}

namespace {
// A pane's title: "<program> - <terminal title>", or the DOS path when there is no title.
std::string pane_title_98(App::State& s, const std::string& id) {
    const auto it = s.views_by_id.find(id);
    if (it == s.views_by_id.end())
        return {};
    const auto& v = it->second;
    // No program, or a shell: "DOS Prompt - <dos path>".
    const ui::Program prog = program_of(v);
    const std::string where = k98::dos_path(v.cwd, home_dir());
    if (prog.shell)
        return "DOS Prompt - " + where;
    const std::string pname = prog.name;
    std::string t;
    if (const auto si = s.by_id.find(id); si != s.by_id.end())
        t = trim_space(si->second->snapshot()->title);
    return !t.empty() && t != pname ? pname + " - " + t : pname + " - " + where;
}

// ---------------------------------------------------------------- pane title bars

// Runs a pane command on pane `id` (it gets the focus first).
void pane_command(App::State& s, const std::string& id, const std::string& command) {
    s.ws.focus(id);
    run_command(s, command);
}

// A pane's window menu (right-click on its title bar).
MenuItems pane_menu(App::State& s, const std::string& id, bool zoomed, const std::string& tab_id) {
    return {
        {.label = "&Restore", .act = [&s, id] { s.ws.toggle_zoom(id); }, .disabled = !zoomed},
        {.label = "Mi&nimize", .act = [&s, tab_id] { settle_tab_id(s, tab_id); }},
        {.label = "Ma&ximize", .act = [&s, id] { s.ws.toggle_zoom(id); }, .disabled = zoomed},
        {.sep = true},
        {.label = "Split &Right",
         .shortcut = "Ctrl+Shift+O",
         .act = [&s, id] { pane_command(s, id, "pane.split:right"); }},
        {.label = "Split &Down",
         .shortcut = "Ctrl+Shift+E",
         .act = [&s, id] { pane_command(s, id, "pane.split:down"); }},
        {.sep = true},
        {.label = "&Close",
         .shortcut = "Alt+F4",
         .act = [&s, id] { pane_command(s, id, "pane.close"); },
         .bold = true},
    };
}

void pane_header(App::State& s, const std::string& id, Box b, bool focused) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const auto loc = s.ws.find(id);
    const ui::Tab* tab =
        loc.space >= 0
            ? &s.ws.spaces[static_cast<std::size_t>(loc.space)].tabs[static_cast<std::size_t>(loc.tab)]
            : nullptr;
    const bool zoomed = tab != nullptr && tab->zoomed == id;
    const std::string tab_id = tab != nullptr ? tab->id : std::string();
    // The face color around the bar, 1 px below it.
    fill(dl, b, rgb(C::kFace));
    const Box bar{b.x0, b.y0, b.x1, b.y1 - one(s)};
    ui::Program program;
    program.shell = true;  // no session: MS-DOS
    if (const auto it = s.views_by_id.find(id); it != s.views_by_id.end())
        program = program_of(it->second);
    const auto ev = title_bar(
        s, dl, ("##pw-" + id).c_str(), bar, pane_title_98(s, id),
        [&s, dl, program](ImVec2 p) { program_icon(s, dl, program, 16, p); }, focused,
        {{"min", [&s, tab_id] { settle_tab_id(s, tab_id); }, "Minimize"},
         {zoomed ? "restore" : "max", [&s, id] { s.ws.toggle_zoom(id); }, zoomed ? "Restore" : "Maximize"},
         {"close", [&s, id] { pane_command(s, id, "pane.close"); }, "Close"}},
        true);
    if (ev.click) {
        s.ws.focus(id);
        s.focus_view(id);
    }
    if (ev.double_click)
        s.ws.toggle_zoom(id);
    if (ev.right)
        open_menu(s, pane_menu(s, id, zoomed, tab_id), std::nullopt);
}

}  // namespace

// The pane header (18) and border (3), in Win98 px.
float pane_border(App::State& s) {
    return snap(U(s, k98::kBorderPx));
}
float pane_header_h(App::State& s) {
    return snap(U(s, k98::kHeaderPx));
}
void pane_header_at(App::State& s, const std::string& id, ImVec2 a, ImVec2 b, bool focused) {
    pane_header(s, id, Box{snap(a.x), snap(a.y), snap(b.x), snap(b.y)}, focused);
}

namespace {
// ---------------------------------------------------------------- the Start menu

// The agents the Start menu offers when installed (a host profile with that id).
struct Agent98 {
    const char* cmd;
    const char* name;
    const char* icon;
};
constexpr Agent98 kAgents[] = {{"claude", "Claude Code", "programs/claude.svg"},
                               {"codex", "Codex", "programs/codex.svg"},
                               {"pi", "Pi", "programs/pi.svg"},
                               {"opencode", "OpenCode", "programs/opencode.svg"},
                               {"amp", "Amp", "programs/amp.svg"},
                               {"gemini", "Gemini", "programs/gemini.svg"},
                               {"agent", "Cursor", "programs/cursor.svg"},
                               {"copilot", "Copilot", "programs/copilot.svg"},
                               {"grok", "Grok", "programs/grok.svg"},
                               {"kimi", "Kimi", "programs/kimi.svg"},
                               {"crush", "Crush", "programs/crush.svg"}};

std::vector<const Agent98*> installed_agents(App::State& s) {
    std::vector<const Agent98*> out;
    const auto profiles = s.host->profiles();
    for (const auto& a : kAgents)
        for (const auto& [id, name] : profiles)
            if (id == a.cmd) {
                out.push_back(&a);
                break;
            }
    return out;
}

MenuItems start_items(App::State& s) {
    MenuItems agents;
    for (const auto* a : installed_agents(s)) {
        const std::string cmd = a->cmd, name = a->name;
        agents.push_back({name, "", a->icon, "", [&s, cmd, name] { new_tab(s, name, -1, cmd); }});
    }
    if (agents.empty())
        agents.push_back({.label = "(Empty)", .disabled = true});
    MenuItems spaces;
    const auto cwds = s.session_cwds();
    for (int i = 0; i < static_cast<int>(s.ws.spaces.size()); ++i) {
        const auto& sp = s.ws.spaces[static_cast<std::size_t>(i)];
        const std::string id = sp.id;
        spaces.push_back(
            {(i < 9 ? "&" + std::to_string(i + 1) + " " : std::string()) + s.ws.space_title(i, cwds),
             i == s.ws.active_space ? "folder-open" : "folder", "", "",
             [&s, id] { select_space_id(s, id); }});
    }
    if (spaces.empty())
        spaces.push_back({.label = "(Empty)", .disabled = true});
    MenuItems layouts;
    for (const auto& l : s.layouts) {
        const std::string name = l.name;
        MenuItem it{name == "win98" ? "AgentDesk 98" : name, "", "", "",
                    [&s, name] { run_command(s, "layout:" + name); }};
        it.radio = name == "win98";
        layouts.push_back(it);
    }
    MenuItems games = {{"Mines", "mines", "", "", [&s] { set_app(s, "mines", "open"); }},
                       {"Solitaire", "sol", "", "", [&s] { set_app(s, "sol", "open"); }}};
    MenuItems accessories = {
        {"Games", "programs", "", "", nullptr, games},
        {"Office Assistant", "clippy", "", "", [&s] { set_shared(s, "win98.clippy", "on"); }},
        {"Welcome to AgentDesk 98", "start", "", "", [&s] { open_dialog(s, kind_dialog("welcome")); }},
        {"Command Palette", "find", "", "", [&s] { run_command(s, "app.palette"); }},
    };
    MenuItems programs = {
        {"Accessories", "programs", "", "", nullptr, accessories},
        {"Agents", "programs", "", "", nullptr, agents},
        {"DOS Prompt", "dos", "", "", [&s] { new_tab(s, "DOS Prompt"); }},
        {"New Space...", "folder", "", "", [&s] { open_dialog(s, new_space_prompt(s)); }},
    };
    MenuItems settings = {
        {"&Control Panel", "control", "", "", [&s] { run_command(s, "app.settings"); }},
        {"&Display...", "display", "", "", [&s] { open_dialog(s, kind_dialog("display")); }},
        {"&Layouts", "programs", "", "", nullptr, layouts},
        {"&Taskbar & Start Menu...", "taskbar", "", "", [&s] { open_dialog(s, kind_dialog("display")); }},
    };
    MenuItems find = {
        {"&Commands...", "find", "", "", [&s] { run_command(s, "app.palette"); }},
        {"&Agents...", "find", "", "", [&s] { open_dialog(s, kind_dialog("taskman")); }},
    };
    return {
        {"AgentDesk Update", "update", "", "", [&s] { run_command(s, "app.settings:Updates"); }},
        {.sep = true},
        {"&Programs", "programs", "", "", nullptr, programs},
        {"&Spaces", "documents", "", "", nullptr, spaces},
        {"S&ettings", "settings", "", "", nullptr, settings},
        {"&Find", "find", "", "", nullptr, find},
        {"&Help", "help", "", "", [&s] { run_command(s, "app.help"); }},
        {"&Run...", "run", "", "", [&s] { open_dialog(s, kind_dialog("run")); }},
        {.sep = true},
        {"&Log Off " + user_name_now() + "...", "logoff", "", "",
         [&s] { open_dialog(s, kind_dialog("logoff")); }},
        {"Sh&ut Down...", "shutdown", "", "", [&s] { open_dialog(s, kind_dialog("shutdown")); }},
    };
}

// Opens the Start menu (above its button when known), or closes it.
void toggle_start(App::State& s, std::optional<Box> bounds) {
    if (!shared(s, "win98.start").is_null()) {
        close_all_menus(s);
        set_shared(s, "win98.start", nullptr);
        return;
    }
    sfx(s, "start");
    const int id = open_menu(s, start_items(s), std::nullopt, true, {}, {}, bounds);
    set_shared(s, "win98.start", id);
}

}  // namespace

// ---------------------------------------------------------------- the layout's commands (win98.<name>)

void command(App::State& s, const std::string& c) {
    auto* sp = s.ws.active();
    const ui::Tab* tab = s.ws.active_tab();
    const int tab_index = sp != nullptr ? sp->active_tab : -1;
    if (c == "start") {
        const W98& w = st(s);
        if (w.start_bounds.z > 0)
            toggle_start(s, Box{w.start_bounds.x, w.start_bounds.y, w.start_bounds.x + w.start_bounds.z,
                                w.start_bounds.y + w.start_bounds.w});
        else
            toggle_start(s, std::nullopt);
    } else if (c == "new_tab") {
        open_dialog(s, new_tab_prompt(s));
    } else if (c == "new_space") {
        open_dialog(s, new_space_prompt(s));
    } else if (c == "rename_tab") {
        if (tab != nullptr)
            open_dialog(s, rename_tab_prompt(s, tab->id, tab->name));
    } else if (c == "rename_space") {
        if (sp != nullptr)
            open_dialog(s, rename_space_prompt(s, sp->id, sp->name));
    } else if (c == "close_tab") {
        if (tab != nullptr)
            open_dialog(s, close_tab_ask(s, *tab, tab_index));
    } else if (c == "close_space") {
        if (sp != nullptr)
            open_dialog(s, close_space_ask(s, s.ws.active_space));
    } else if (c == "taskman" || c == "shutdown" || c == "about" || c == "display" || c == "welcome" ||
               c == "mycomputer" || c == "network" || c == "bin" || c == "datetime" || c == "logoff") {
        open_dialog(s, kind_dialog(c));
    } else if (c == "run") {
        open_dialog(s, kind_dialog("run"));
    } else if (c == "boot") {
        boot(s, true);
    } else if (c == "small_fonts") {
        set_shared(s, "win98.scale", 1);
    } else if (c == "large_fonts") {
        set_shared(s, "win98.scale", 2);
    } else if (c == "mines" || c == "sol") {
        set_app(s, c, "open");
    }
}

// Coming into AgentDesk 98: the boot screen and the welcome (at most every half hour unless
// forced), else just the chime (not more than every 20 seconds).
void boot(App::State& s, bool force) {
    const long long now = now_ms();
    const Json& lb = shared(s, "win98.boot");
    const long long last = force ? 0 : (lb.is_number() ? lb.get<long long>() : 0);
    const Json& li = shared(s, "win98.init");
    const long long seen = li.is_number() ? li.get<long long>() : 0;
    set_shared(s, "win98.init", now);
    if (now - last > 30LL * 60 * 1000) {
        set_shared(s, "win98.boot", now);
        Dialog scr;
        scr.kind = "screen";
        scr.mode = "boot";
        open_dialog(s, std::move(scr));
    } else if (now - seen > 20 * 1000) {
        sfx(s, "startup");
    }
}

// Seconds since agent-ui started, or -1.
long long app_age_seconds() {
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!::GetProcessTimes(::GetCurrentProcess(), &created, &exited, &kernel, &user))
        return -1;
    FILETIME now{};
    ::GetSystemTimeAsFileTime(&now);
    const auto to64 = [](FILETIME f) {
        return (static_cast<long long>(f.dwHighDateTime) << 32) | f.dwLowDateTime;
    };
    return (to64(now) - to64(created)) / 10000000LL;
}

namespace {
// ---------------------------------------------------------------- the taskbar

// The session a taskbar button stands for: the tab's focused pane, else its first agent, else
// its first pane ("" for an empty tab).
std::string lead_session(App::State& s, const ui::Tab& t) {
    const auto ids = s.ws.sessions_in(t);
    if (std::find(ids.begin(), ids.end(), t.focused) != ids.end())
        return t.focused;
    for (const auto& id : ids)
        if (const auto it = s.views_by_id.find(id); it != s.views_by_id.end() && is_agent(it->second))
            return id;
    return ids.empty() ? std::string() : ids.front();
}

// A taskbar button's right-click menu for tab `t`.
MenuItems tab_menu(App::State& s, const ui::Tab& t, bool pressed, bool active) {
    const std::string id = t.id;
    const std::string tname = t.name;
    return {
        {.label = "&Restore", .act = [&s, id] { select_tab_id(s, id); }, .disabled = pressed},
        {.label = "Mi&nimize", .act = [&s, id] { settle_tab_id(s, id); }, .disabled = t.settled},
        {.label = "Ma&ximize", .act = [&s] { s.ws.toggle_zoom(); }, .disabled = !active},
        {.sep = true},
        {.label = "Re&name...", .act = [&s, id, tname] { open_dialog(s, rename_tab_prompt(s, id, tname)); }},
        {.sep = true},
        {.label = "&Close",
         .shortcut = "Alt+F4",
         .act = [&s, id] { run_command(s, "tab.close:" + id); },
         .bold = true},
    };
}

// A tab's taskbar button: pressed (dithered) while shown, highlighted while an agent in it is
// blocked, with a working or done badge on its icon.
void tab_button(App::State& s, ImDrawList* dl, int i, const ui::Tab& t, Box b) {
    auto* sp = s.ws.active();
    const bool active = sp != nullptr && i == sp->active_tab;
    const std::string status = tab_status(s, t);
    const bool flash = status == "blocked";
    const bool pressed = active && !t.settled;
    const ImU32 fg = flash ? rgb(C::kSelectText) : rgb(C::kText);
    const std::string badge = flash ? std::string() : status_badge(status);
    ui::Program program;
    program.shell = true;
    std::string pname;
    if (const auto it = s.views_by_id.find(lead_session(s, t)); it != s.views_by_id.end()) {
        program = program_of(it->second);
        pname = program.name;
    }
    const std::string name = ui::Workspace::tab_title(t, i);
    const std::string tip =
        name + (pname.empty() ? "" : " - " + pname) + (status.empty() ? "" : " (" + status + ")");
    ButtonOpts o;
    o.toggled = pressed;
    o.pad_x = 3;
    o.tooltip = tip.c_str();
    ImGui::PushID(t.id.c_str());
    const Pressed p = button(s, dl, "##tb", b, o);
    ImGui::PopID();
    const Box face = p.content;
    if (pressed && !flash)
        dither(s, dl, face);
    if (flash)
        fill(dl, face, rgb(C::kSelect));
    dl->PushClipRect(face.a(), face.b(), true);
    const float iy = snap(face.y0 + (face.h() - U(s, 16)) * 0.5f);
    program_icon(s, dl, program, 16, ImVec2(face.x0, iy));
    if (!badge.empty()) {
        int alpha = 255;
        if (badge == "hourglass")  // pulses between 35% and 100% over 1400 ms
            alpha = static_cast<int>(255.0 * (0.35 + 0.65 * triangle_wave(1400.0)));
        dl->PopClipRect();  // the badge may stick out of the face
        icon(s, dl, badge, 9,
             ImVec2(face.x0 + U(s, 16) + U(s, 3) - U(s, 9), iy + U(s, 16) + U(s, 3) - U(s, 9)), alpha);
        dl->PushClipRect(face.a(), face.b(), true);
    }
    const float tx = face.x0 + U(s, 16) + U(s, 3);
    label_clipped(s, dl, tx, face.y0 + (face.h() - U(s, 13)) * 0.5f, name, fg, face.x1 - tx, pressed);
    dl->PopClipRect();
    const std::string id = t.id;  // a click may change the workspace under `t`
    if (p.clicked) {
        if (pressed)
            settle_tab_id(s, id);
        else
            select_tab_id(s, id);
    }
    if (p.right)
        open_menu(s, tab_menu(s, t, pressed, active), std::nullopt);
}

// A game's taskbar button: pressed while its window is open.
void game_button(App::State& s, ImDrawList* dl, const std::string& key, const std::string& state, Box b) {
    const std::string name = key == "mines" ? "Mines" : "Solitaire";
    const bool open = state == "open";
    ButtonOpts o;
    o.toggled = open;
    o.pad_x = 3;
    ImGui::PushID(key.c_str());
    const Pressed p = button(s, dl, "##tg", b, o);
    ImGui::PopID();
    const Box face = p.content;
    if (open)
        dither(s, dl, face);
    icon(s, dl, key, 16, ImVec2(face.x0, snap(face.y0 + (face.h() - U(s, 16)) * 0.5f)));
    const float tx = face.x0 + U(s, 16) + U(s, 3);
    label_clipped(s, dl, tx, face.y0 + (face.h() - U(s, 13)) * 0.5f, name, rgb(C::kText), face.x1 - tx, open);
    if (p.clicked)
        set_app(s, key, open ? "min" : "open");
    if (p.right)
        open_menu(s,
                  {{.label = "&Restore", .act = [&s, key] { set_app(s, key, "open"); }, .disabled = open},
                   {.label = "Mi&nimize", .act = [&s, key] { set_app(s, key, "min"); }, .disabled = !open},
                   {.sep = true},
                   {.label = "&Close", .act = [&s, key] { set_app(s, key, ""); }, .bold = true}},
                  std::nullopt);
}

// A Quick Launch button: flat until hovered, then a 1 px raised bump.
void quick(App::State& s, ImDrawList* dl, const char* id, Box b,
           const std::function<void(ImVec2)>& draw_icon_at, const std::string& tip,
           const std::function<void()>& act) {
    const Pressed p = area(id, b);
    if (p.hovered) {
        const float o = one(s);
        dl->AddRectFilled(ImVec2(b.x1 - o, b.y0), ImVec2(b.x1, b.y1), rgb(C::kShadow));
        dl->AddRectFilled(ImVec2(b.x0, b.y1 - o), ImVec2(b.x1, b.y1), rgb(C::kShadow));
        dl->AddRectFilled(ImVec2(b.x0, b.y0), ImVec2(b.x1 - o, b.y0 + o), rgb(C::kHi));
        dl->AddRectFilled(ImVec2(b.x0, b.y0), ImVec2(b.x0 + o, b.y1 - o), rgb(C::kHi));
        ImGui::SetTooltip("%s", tip.c_str());
    }
    const float o = one(s);
    const Box in = b.inset(o, o, o, o);
    draw_icon_at(ImVec2(snap(in.x0 + (in.w() - U(s, 16)) * 0.5f), snap(in.y0 + (in.h() - U(s, 16)) * 0.5f)));
    if (p.clicked && act)
        act();
}

struct TrayAgent {
    std::string id;
    ui::Program program;
    std::string status;
    std::string tip;
};

std::vector<TrayAgent> tray_agents(App::State& s) {
    std::vector<TrayAgent> out;
    const auto cwds = s.session_cwds();
    for (const auto& id : s.agent_order) {
        const auto it = s.views_by_id.find(id);
        if (it == s.views_by_id.end() || !is_agent(it->second) || it->second.process_state != "Running" ||
            it->second.lost)
            continue;
        TrayAgent a;
        a.id = id;
        a.program = program_of(it->second);
        a.status = status_word(s.attention_of(id));
        a.tip = a.program.name + ": " + a.status + " - " + session_place(s, id, cwds);
        out.push_back(a);
        if (out.size() == 10)  // the tray shows at most ten agents
            break;
    }
    return out;
}

float tray_width(App::State& s, const std::vector<TrayAgent>& agents, const std::string& clock) {
    float w = U(s, 2) * 2 + 2 * one(s);
    for (std::size_t i = 0; i < agents.size(); ++i)
        w += U(s, 16) + U(s, 3);
    w += U(s, 16) + U(s, 3);                    // the speaker
    w += U(s, 4) + text_w(s, clock) + U(s, 2);  // the clock
    return snap(w);
}

void tray(App::State& s, ImDrawList* dl, Box b, const std::vector<TrayAgent>& agents,
          const std::string& clock) {
    const Box in = bevel(s, dl, b, Bevel::kWell);
    float x = in.x0 + U(s, 2);
    const float iy = snap(in.y0 + (in.h() - U(s, 16)) * 0.5f);
    for (const auto& a : agents) {
        ImGui::PushID(a.id.c_str());
        const Pressed p = area("##tray", box(ImVec2(x, iy), ImVec2(U(s, 16), U(s, 16))));
        ImGui::PopID();
        program_icon(s, dl, a.program, 16, ImVec2(x, iy));
        const std::string badge = status_badge(a.status);
        if (!badge.empty()) {
            int alpha = 255;
            if (a.status == "blocked")  // blinks over 900 ms
                alpha = static_cast<int>(255.0 * triangle_wave(900.0));
            icon(s, dl, badge, 9, ImVec2(x + U(s, 16) + U(s, 2) - U(s, 9), iy + U(s, 16) + U(s, 2) - U(s, 9)),
                 alpha);
        }
        if (p.hovered)
            ImGui::SetTooltip("%s", a.tip.c_str());
        if (p.clicked)
            run_command(s, "agent.focus:" + a.id);
        x += U(s, 16) + U(s, 3);
    }
    const bool mute = shared(s, "win98.mute") == Json(true);
    {
        const Pressed p = area("##tray-speaker", box(ImVec2(x, iy), ImVec2(U(s, 16), U(s, 16))));
        icon(s, dl, mute ? "speaker-mute" : "speaker", 16, ImVec2(x, iy));
        if (p.hovered)
            ImGui::SetTooltip("%s", mute ? "Sounds: off" : "Volume");
        if (p.clicked)
            set_shared(s, "win98.mute", !mute);
        x += U(s, 16) + U(s, 3);
    }
    {
        const float cw = U(s, 4) + text_w(s, clock) + U(s, 2);
        const Pressed p = area("##tray-clock", Box{x, in.y0, x + cw, in.y1});
        plain_label(s, dl, x + U(s, 4), in.y0 + (in.h() - U(s, 13)) * 0.5f, clock, rgb(C::kText));
        if (p.hovered)
            ImGui::SetTooltip("%s", k98::long_date(local_now()).c_str());
        if (p.double_click)
            open_dialog(s, kind_dialog("datetime"));
    }
}

// The Start button at x, centered on cy: pressed while its menu is open; acts on the press.
// Returns its right edge.
float start_button(App::State& s, ImDrawList* dl, float x, float cy) {
    const float o = one(s);
    const float w = 2 * o + U(s, 3) + U(s, 16) + U(s, 3) + text_w(s, "Start", true) + U(s, 3) + 2 * o;
    const Box sb = box(ImVec2(x, cy - U(s, 11)), ImVec2(w, U(s, 22)));
    ButtonOpts bo;
    bo.toggled = !shared(s, "win98.start").is_null();
    bo.pad_x = 3;
    bo.on_down = true;
    const Pressed p = button(s, dl, "##start", sb, bo);
    icon(s, dl, "start", 16, ImVec2(p.content.x0, snap(p.content.y0 + (p.content.h() - U(s, 16)) * 0.5f)));
    label(s, dl, p.content.x0 + U(s, 16) + U(s, 3), p.content.y0 + (p.content.h() - U(s, 13)) * 0.5f, "Start",
          rgb(C::kText), true);
    st(s).start_bounds = ImVec4(sb.x0, sb.y0, sb.w(), sb.h());
    if (p.clicked)
        toggle_start(s, sb);
    return sb.x1;
}

// An etched separator and a grip (a 1 px bump, 20 tall) at x. Returns where the next part starts.
float separator(App::State& s, ImDrawList* dl, float x, float cy) {
    const float o = one(s);
    etch(s, dl, snap(x), snap(cy - U(s, 10)), U(s, 20), true);
    x += 2 * o + U(s, 2);
    bevel(s, dl, box(ImVec2(x, cy - U(s, 10)), ImVec2(3 * o, U(s, 20))), Bevel::kBump);
    return x + 3 * o + U(s, 2);
}

// Quick Launch: DOS Prompt, the first two installed agents, Mines. Returns where the next part
// starts (2 px after it).
float quick_launch(App::State& s, ImDrawList* dl, float x, float cy) {
    const float qy = cy - U(s, 11);
    const ImVec2 size(U(s, 23), U(s, 22));
    quick(
        s, dl, "##ql-dos", box(ImVec2(x, qy), size), [&](ImVec2 p) { icon(s, dl, "dos", 16, p); },
        "DOS Prompt", [&s] { new_tab(s, "DOS Prompt"); });
    x += U(s, 23) + U(s, 1);
    int n = 0;
    for (const auto* a : installed_agents(s)) {
        if (n++ == 2)
            break;
        const std::string cmd = a->cmd, name = a->name;
        quick(
            s, dl, ("##ql-" + cmd).c_str(), box(ImVec2(x, qy), size),
            [&](ImVec2 p) {
                image(s, dl, root_dir() / win::to_wide(a->icon), box(p, ImVec2(U(s, 16), U(s, 16))));
            },
            name, [&s, cmd, name] { new_tab(s, name, -1, cmd); });
        x += U(s, 23) + U(s, 1);
    }
    quick(
        s, dl, "##ql-mines", box(ImVec2(x, qy), size), [&](ImVec2 p) { icon(s, dl, "mines", 16, p); },
        "Mines", [&s] { set_app(s, "mines", "open"); });
    return x + (U(s, 23) + U(s, 2));
}

// The buttons of the shown space's tabs, then of the open games, sharing [x0, x1) of the
// taskbar `b`: each 40 to 160 Win98 px wide, gap 3.
void task_buttons(App::State& s, ImDrawList* dl, Box b, float x0, float x1, float cy) {
    std::vector<std::pair<std::string, std::string>> games;
    for (const char* k : {"mines", "sol"}) {
        const std::string state = app_state(s, k);
        if (!state.empty())
            games.emplace_back(k, state);
    }
    auto* sp = s.ws.active();
    const int ntabs = sp != nullptr ? static_cast<int>(sp->tabs.size()) : 0;
    const int n = ntabs + static_cast<int>(games.size());
    const float avail = x1 - x0;
    if (n <= 0 || avail <= 0)
        return;
    const float gap = U(s, 3);
    const float each =
        std::clamp((avail - gap * static_cast<float>(n - 1)) / static_cast<float>(n), U(s, 40), U(s, 160));
    dl->PushClipRect(ImVec2(x0, b.y0), ImVec2(x1, b.y1), true);
    float x = x0;
    for (int i = 0; i < n; ++i) {
        const Box bb = box(ImVec2(x, cy - U(s, 11)), ImVec2(each, U(s, 22)));
        if (i < ntabs) {
            tab_button(s, dl, i, sp->tabs[static_cast<std::size_t>(i)], bb);
        } else {
            const auto& [key, state] = games[static_cast<std::size_t>(i - ntabs)];
            game_button(s, dl, key, state, bb);
        }
        x += each + gap;
    }
    dl->PopClipRect();
}

}  // namespace

// The taskbar: Start, Quick Launch, the task buttons, and the tray at the end.
void taskbar(App::State& s, ImDrawList* dl, Box b) {
    fill(dl, b, rgb(C::kFace));
    const float o = one(s);
    fill(dl, Box{b.x0, b.y0, b.x1, b.y0 + o}, rgb(C::kLight));
    fill(dl, Box{b.x0, b.y0 + o, b.x1, b.y0 + 2 * o}, rgb(C::kHi));
    const Box row{b.x0 + U(s, 2), b.y0 + 2 * o, b.x1 - U(s, 2), b.y1};
    const float cy = row.y0 + row.h() * 0.5f;
    float x = start_button(s, dl, row.x0, cy) + U(s, 2);
    x = separator(s, dl, x, cy);
    x = quick_launch(s, dl, x, cy);
    x = separator(s, dl, x, cy);
    const auto agents = tray_agents(s);
    const std::string clock = k98::clock_text(local_now());
    const float tw = tray_width(s, agents, clock);
    const Box tb = box(ImVec2(row.x1 - tw, cy - U(s, 11)), ImVec2(tw, U(s, 22)));
    tray(s, dl, tb, agents, clock);
    task_buttons(s, dl, b, x, tb.x0 - U(s, 2), cy);
    window_drag_area(s, b.a(), b.b());
}
}  // namespace detail::w98

}  // namespace agentdesk::desktop
