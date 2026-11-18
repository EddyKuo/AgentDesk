// Herdr 98, part 3: the window itself (herdr-app defaults/layouts/win98.json): the desktop, the
// panes' title bars, the taskbar and its Start menu, the games, the Office Assistant, and the
// layout's commands (keys.json section "layout:win98"). From defaults/mods/win98 taskbar.js,
// desktop.js, window.js, mines.js, sol.js, clippy.js (commit 321c9a8).

namespace w98 {

// ---------------------------------------------------------------- helpers (herdr commands by id)

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

std::string tab_status(App::State& s, const ui::Tab& t) {
    std::vector<Attention> atts;
    for (const auto& id : s.ws.sessions_in(t))
        atts.push_back(s.attention_of(id));
    return status_word(ui::most_urgent(atts));
}

std::string space_status(App::State& s, const ui::Space& sp) {
    std::vector<Attention> atts;
    for (const auto& t : sp.tabs)
        for (const auto& id : s.ws.sessions_in(t))
            atts.push_back(s.attention_of(id));
    return status_word(ui::most_urgent(atts));
}

std::string user_name_now() {
    return k98::user_name(home_dir());
}

void dirty(App::State& s) {
    s.doc_dirty = true;
    s.doc_dirty_at = Clock::now();
}

void select_space_id(App::State& s, const std::string& id) {
    for (int i = 0; i < static_cast<int>(s.ws.spaces.size()); ++i)
        if (s.ws.spaces[static_cast<std::size_t>(i)].id == id) {
            s.ws.select_space(i);
            s.focus_view(s.ws.focused_session());
            dirty(s);
        }
}

// herdr tab.select {id}: selects the tab and its space; a settled tab is shown again.
void select_tab_id(App::State& s, const std::string& id) {
    const auto loc = ui::find_tab(s.ws, id);
    if (loc.space < 0)
        return;
    s.ws.select_space(loc.space);
    s.ws.select_tab(loc.tab);
    s.focus_view(s.ws.focused_session());
    dirty(s);
}

// herdr tab.settle {id} (model.rs 470-484): the shown one hands over to the next unsettled tab;
// the last one stays shown, settled.
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

void rename_space_id(App::State& s, const std::string& id, const std::string& name) {
    for (auto& sp : s.ws.spaces)
        if (sp.id == id)
            sp.name = name;
    dirty(s);
}

// herdr tab.new {name, argv, space}: a terminal in a new tab (a shell, or the program asked for).
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

// herdr space.new {name, machine}.
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

// taskbar.js 519-541.
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

// dialog.js 247-251: every terminal that has not finished.
std::vector<Task> tasks(App::State& s) {
    std::vector<Task> out;
    for (const auto& sess : s.sessions) {
        const auto it = s.views_by_id.find(sess->id());
        if (it == s.views_by_id.end())
            continue;
        const auto& v = it->second;
        if (v.process_state == "Exited" || v.lost)
            continue;
        const std::string prog = program_of(v);
        Task t;
        t.address = v.id;
        t.program = prog;
        t.name =
            !prog.empty() ? program_info(v.program, v.profile_id).name : (!v.name.empty() ? v.name : v.id);
        t.status = is_agent(v) ? status_word(s.attention_of(v.id)) : std::string();
        out.push_back(t);
    }
    return out;
}

// dialog.js 919-925: the desktop's color or the clouds picture.
void set_desktop(App::State& s, const std::string& c) {
    set_shared(s, "win98.desktop", c);
}

// window.js 9-17: a pane's title.
std::string pane_title_98(App::State& s, const std::string& id) {
    const auto it = s.views_by_id.find(id);
    if (it == s.views_by_id.end())
        return {};
    const auto& v = it->second;
    const std::string prog = program_of(v);
    const std::string where = k98::dos_path(v.cwd, home_dir());
    if (prog.empty() || is_shell(prog) || is_shell_profile(prog))
        return "DOS Prompt - " + where;
    const std::string pname = program_info(v.program, v.profile_id).name;
    std::string t;
    if (const auto si = s.by_id.find(id); si != s.by_id.end())
        t = si->second->snapshot()->title;
    while (!t.empty() && std::isspace(static_cast<unsigned char>(t.back())))
        t.pop_back();
    while (!t.empty() && std::isspace(static_cast<unsigned char>(t.front())))
        t.erase(t.begin());
    return !t.empty() && t != pname ? pname + " - " + t : pname + " - " + where;
}

// ---------------------------------------------------------------- pane title bars (window.js)

void pane_header(App::State& s, const std::string& id, Box b, bool focused) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const auto loc = s.ws.find(id);
    const ui::Tab* tab =
        loc.space >= 0
            ? &s.ws.spaces[static_cast<std::size_t>(loc.space)].tabs[static_cast<std::size_t>(loc.tab)]
            : nullptr;
    const bool zoomed = tab != nullptr && tab->zoomed == id;
    const std::string tab_id = tab != nullptr ? tab->id : std::string();
    // div().bg(face).pb(1) around the bar.
    fill(dl, b, rgb(C::kFace));
    const Box bar{b.x0, b.y0, b.x1, b.y1 - one(s)};
    std::string act;
    auto window_menu = [&s, id, zoomed, tab_id] {
        MenuItems m;
        m.push_back({"&Restore", "", "", "", [&s, id] { s.ws.toggle_zoom(id); }, {}, !zoomed});
        m.push_back({"Mi&nimize", "", "", "", [&s, tab_id] { settle_tab_id(s, tab_id); }});
        m.push_back({"Ma&ximize", "", "", "", [&s, id] { s.ws.toggle_zoom(id); }, {}, zoomed});
        m.push_back({.sep = true});
        m.push_back({"Split &Right", "", "", "Ctrl+Shift+O", [&s, id] {
                         s.ws.focus(id);
                         run_command(s, "pane.split:right");
                     }});
        m.push_back({"Split &Down", "", "", "Ctrl+Shift+E", [&s, id] {
                         s.ws.focus(id);
                         run_command(s, "pane.split:down");
                     }});
        m.push_back({.sep = true});
        m.push_back({"&Close",
                     "",
                     "",
                     "Alt+F4",
                     [&s, id] {
                         s.ws.focus(id);
                         run_command(s, "pane.close");
                     },
                     {},
                     false,
                     false,
                     false,
                     true});
        open_menu(s, std::move(m), std::nullopt);
    };
    std::string program;
    if (const auto it = s.views_by_id.find(id); it != s.views_by_id.end())
        program = program_of(it->second);
    const auto ev = title_bar(
        s, dl, ("##pw-" + id).c_str(), bar, pane_title_98(s, id),
        [&s, dl, program](ImVec2 p) { program_icon(s, dl, program, 16, p); }, focused,
        {{"min", [&s, tab_id] { settle_tab_id(s, tab_id); }, "Minimize"},
         {zoomed ? "restore" : "max", [&s, id] { s.ws.toggle_zoom(id); }, zoomed ? "Restore" : "Maximize"},
         {"close",
          [&s, id] {
              s.ws.focus(id);
              run_command(s, "pane.close");
          },
          "Close"}},
        true);
    if (ev.click) {
        s.ws.focus(id);
        s.focus_view(id);
    }
    if (ev.double_click)
        s.ws.toggle_zoom(id);
    if (ev.right)
        window_menu();
}

// kit.js 464-473 (fitLayout): the pane header 18 and the border 3, in Win98 px.
float pane_border(App::State& s) {
    return snap(U(s, k98::kBorderPx));
}
float pane_header_h(App::State& s) {
    return snap(U(s, k98::kHeaderPx));
}
void pane_header_at(App::State& s, const std::string& id, ImVec2 a, ImVec2 b, bool focused) {
    pane_header(s, id, Box{snap(a.x), snap(a.y), snap(b.x), snap(b.y)}, focused);
}

// ---------------------------------------------------------------- the Start menu (taskbar.js 197-264)

// taskbar.js 15-27: the agents the Start menu offers when installed (here: a host profile).
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

MenuItem new_space_item(App::State& s, const std::string& label) {
    return {label, "folder", "", "", [&s] {
                open_dialog(s, prompt(
                                   "New Space", "Type a name for the new space:", "folder",
                                   [&s](const std::string& n, const std::string& m) { new_space(s, n, m); },
                                   std::nullopt, true));
            }};
}

MenuItems start_items(App::State& s) {
    MenuItems agents;
    for (const auto* a : installed_agents(s)) {
        const std::string cmd = a->cmd, name = a->name;
        agents.push_back({name, "", a->icon, "", [&s, cmd, name] { new_tab(s, name, -1, cmd); }});
    }
    if (agents.empty())
        agents.push_back({"(Empty)", "", "", "", nullptr, {}, true});
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
        spaces.push_back({"(Empty)", "", "", "", nullptr, {}, true});
    MenuItems layouts;
    for (const auto& l : s.layouts) {
        const std::string name = l.name;
        MenuItem it{name == "win98" ? "Herdr 98" : name, "", "", "",
                    [&s, name] { run_command(s, "layout:" + name); }};
        it.radio = name == "win98";
        layouts.push_back(it);
    }
    MenuItems games = {{"Mines", "mines", "", "", [&s] { set_app(s, "mines", "open"); }},
                       {"Solitaire", "sol", "", "", [&s] { set_app(s, "sol", "open"); }}};
    MenuItems accessories = {
        {"Games", "programs", "", "", nullptr, games},
        {"Office Assistant", "clippy", "", "", [&s] { set_shared(s, "win98.clippy", "on"); }},
        {"Welcome to Herdr 98", "start", "", "", [&s] { open_dialog(s, kind_dialog("welcome")); }},
        {"Command Palette", "find", "", "", [&s] { run_command(s, "app.palette"); }},
    };
    MenuItems programs = {
        {"Accessories", "programs", "", "", nullptr, accessories},
        {"Agents", "programs", "", "", nullptr, agents},
        {"DOS Prompt", "dos", "", "", [&s] { new_tab(s, "DOS Prompt"); }},
        new_space_item(s, "New Space..."),
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
        {"Herdr Update", "update", "", "", [&s] { run_command(s, "app.settings:Updates"); }},
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

// taskbar.js 266-274.
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

// ---------------------------------------------------------------- commands (taskbar.js 126-176)

void open_run(App::State& s) {
    open_dialog(s, kind_dialog("run"));
}

void boot(App::State& s, bool force);

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
        open_dialog(s, prompt("New Tab", "Type a name for the new tab:", "dos",
                              [&s](const std::string& n, const std::string&) { new_tab(s, n); }));
    } else if (c == "new_space") {
        open_dialog(s, prompt(
                           "New Space", "Type a name for the new space:", "folder",
                           [&s](const std::string& n, const std::string& m) { new_space(s, n, m); },
                           std::nullopt, true));
    } else if (c == "rename_tab") {
        if (tab != nullptr) {
            const std::string id = tab->id;
            open_dialog(s,
                        prompt(
                            "Rename", "Type a new name for this tab:", "rename",
                            [&s, id](const std::string& n, const std::string&) { rename_tab_id(s, id, n); },
                            tab->name));
        }
    } else if (c == "rename_space") {
        if (sp != nullptr) {
            const std::string id = sp->id;
            open_dialog(s,
                        prompt(
                            "Rename", "Type a new name for this space:", "folder",
                            [&s, id](const std::string& n, const std::string&) { rename_space_id(s, id, n); },
                            sp->name));
        }
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
        open_run(s);
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

// taskbar.js 109-120: coming into Herdr 98: the boot screen and the welcome (at most every half
// hour unless forced), else just the chime (not on every reload).
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

// Seconds since agent-ui started (taskbar.js 48-59 asks ps for the app's age).
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

// ---------------------------------------------------------------- the taskbar (taskbar.js 276-516)

void tab_button(App::State& s, ImDrawList* dl, int i, const ui::Tab& t, Box b) {
    auto* sp = s.ws.active();
    const bool active = sp != nullptr && i == sp->active_tab;
    const std::string status = tab_status(s, t);
    const bool flash = status == "blocked";
    const bool pressed = active && !t.settled;
    const ImU32 fg = flash ? rgb(C::kSelectText) : rgb(C::kText);
    const std::string badge = status == "working" ? "hourglass"
                              : status == "done"  ? "check-badge"
                                                  : std::string();
    const auto ids = s.ws.sessions_in(t);
    std::string lead_id =
        std::find(ids.begin(), ids.end(), t.focused) != ids.end() ? t.focused : std::string();
    if (lead_id.empty())
        for (const auto& id : ids)
            if (const auto it = s.views_by_id.find(id); it != s.views_by_id.end() && is_agent(it->second)) {
                lead_id = id;
                break;
            }
    if (lead_id.empty() && !ids.empty())
        lead_id = ids.front();
    std::string program, pname;
    if (const auto it = s.views_by_id.find(lead_id); it != s.views_by_id.end()) {
        program = program_of(it->second);
        pname = program_info(it->second.program, it->second.profile_id).name;
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
        if (badge == "hourglass") {  // loop("opacity", {from: 0.35, to: 1, period: 1400})
            const double t2 = std::fmod(ImGui::GetTime() * 1000.0, 1400.0) / 1400.0;
            alpha = static_cast<int>(255.0 * (0.35 + 0.65 * (t2 < 0.5 ? t2 * 2 : 2 - t2 * 2)));
        }
        dl->PopClipRect();
        icon(s, dl, badge, 9,
             ImVec2(face.x0 + U(s, 16) + U(s, 3) - U(s, 9), iy + U(s, 16) + U(s, 3) - U(s, 9)), alpha);
        dl->PushClipRect(face.a(), face.b(), true);
    }
    const float tx = face.x0 + U(s, 16) + U(s, 3);
    label_clipped(s, dl, tx, face.y0 + (face.h() - U(s, 13)) * 0.5f, name, fg, face.x1 - tx, pressed);
    dl->PopClipRect();
    const std::string id = t.id;
    if (p.clicked) {
        if (pressed)
            settle_tab_id(s, id);
        else
            select_tab_id(s, id);
    }
    if (p.right) {
        MenuItems m;
        m.push_back({"&Restore", "", "", "", [&s, id] { select_tab_id(s, id); }, {}, pressed});
        m.push_back({"Mi&nimize", "", "", "", [&s, id] { settle_tab_id(s, id); }, {}, t.settled});
        m.push_back({"Ma&ximize", "", "", "", [&s] { s.ws.toggle_zoom(); }, {}, !active});
        m.push_back({.sep = true});
        const std::string tname = t.name;
        m.push_back({"Re&name...", "", "", "", [&s, id, tname] {
                         open_dialog(s, prompt(
                                            "Rename", "Type a new name for this tab:", "rename",
                                            [&s, id](const std::string& n, const std::string&) {
                                                rename_tab_id(s, id, n);
                                            },
                                            tname));
                     }});
        m.push_back({.sep = true});
        m.push_back({"&Close",
                     "",
                     "",
                     "Alt+F4",
                     [&s, id] { run_command(s, "tab.close:" + id); },
                     {},
                     false,
                     false,
                     false,
                     true});
        open_menu(s, std::move(m), std::nullopt);
    }
}

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
    if (p.right) {
        MenuItems m;
        m.push_back({"&Restore", "", "", "", [&s, key] { set_app(s, key, "open"); }, {}, open});
        m.push_back({"Mi&nimize", "", "", "", [&s, key] { set_app(s, key, "min"); }, {}, !open});
        m.push_back({.sep = true});
        m.push_back(
            {"&Close", "", "", "", [&s, key] { set_app(s, key, ""); }, {}, false, false, false, true});
        open_menu(s, std::move(m), std::nullopt);
    }
}

// taskbar.js 370-398: Quick Launch: flat until hovered, then a 1 px raised bump.
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
    std::string program;
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
        const auto loc = s.ws.find(id);
        std::string sname, tname;
        if (loc.space >= 0) {
            sname = s.ws.space_title(loc.space, cwds);
            tname = ui::Workspace::tab_title(
                s.ws.spaces[static_cast<std::size_t>(loc.space)].tabs[static_cast<std::size_t>(loc.tab)],
                loc.tab);
        }
        TrayAgent a;
        a.id = id;
        a.program = program_of(it->second);
        a.status = status_word(s.attention_of(id));
        a.tip = program_info(it->second.program, it->second.profile_id).name + ": " + a.status + " - " +
                sname + " \\ " + tname;
        out.push_back(a);
        if (out.size() == 10)
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
        const std::string badge = a.status == "working"   ? "hourglass"
                                  : a.status == "blocked" ? "warn-badge"
                                  : a.status == "done"    ? "check-badge"
                                                          : std::string();
        if (!badge.empty()) {
            int alpha = 255;
            if (a.status == "blocked") {  // loop("opacity", {from: 0, to: 1, period: 900})
                const double t = std::fmod(ImGui::GetTime() * 1000.0, 900.0) / 900.0;
                alpha = static_cast<int>(255.0 * (t < 0.5 ? t * 2 : 2 - t * 2));
            }
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
        label(s, dl, x + U(s, 4), in.y0 + (in.h() - U(s, 13)) * 0.5f, clock, rgb(C::kText), false, 11, 13,
              false);
        if (p.hovered)
            ImGui::SetTooltip("%s", k98::long_date(local_now()).c_str());
        if (p.double_click)
            open_dialog(s, kind_dialog("datetime"));
    }
}

void taskbar(App::State& s, ImDrawList* dl, Box b) {
    fill(dl, b, rgb(C::kFace));
    const float o = one(s);
    fill(dl, Box{b.x0, b.y0, b.x1, b.y0 + o}, rgb(C::kLight));
    fill(dl, Box{b.x0, b.y0 + o, b.x1, b.y0 + 2 * o}, rgb(C::kHi));
    const Box row{b.x0 + U(s, 2), b.y0 + 2 * o, b.x1 - U(s, 2), b.y1};
    const float cy = row.y0 + row.h() * 0.5f;
    float x = row.x0;
    // Start: pressed while its menu is open; acts on the press.
    {
        const float w = 2 * o + U(s, 3) + U(s, 16) + U(s, 3) + text_w(s, "Start", true) + U(s, 3) + 2 * o;
        const Box sb = box(ImVec2(x, cy - U(s, 11)), ImVec2(w, U(s, 22)));
        ButtonOpts bo;
        bo.toggled = !shared(s, "win98.start").is_null();
        bo.pad_x = 3;
        bo.on_down = true;
        const Pressed p = button(s, dl, "##start", sb, bo);
        icon(s, dl, "start", 16,
             ImVec2(p.content.x0, snap(p.content.y0 + (p.content.h() - U(s, 16)) * 0.5f)));
        label(s, dl, p.content.x0 + U(s, 16) + U(s, 3), p.content.y0 + (p.content.h() - U(s, 13)) * 0.5f,
              "Start", rgb(C::kText), true);
        st(s).start_bounds = ImVec4(sb.x0, sb.y0, sb.w(), sb.h());
        if (p.clicked)
            toggle_start(s, sb);
        x = sb.x1 + U(s, 2);
    }
    auto separator = [&] {
        etch(s, dl, snap(x), snap(cy - U(s, 10)), U(s, 20), true);
        x += 2 * o + U(s, 2);
        // kit.js 184-186: the grip, a 1 px bump, 20 tall.
        bevel(s, dl, box(ImVec2(x, cy - U(s, 10)), ImVec2(3 * o, U(s, 20))), Bevel::kBump);
        x += 3 * o + U(s, 2);
    };
    separator();
    // Quick Launch: DOS Prompt, the first two installed agents, Mines.
    {
        const float qy = cy - U(s, 11);
        quick(
            s, dl, "##ql-dos", box(ImVec2(x, qy), ImVec2(U(s, 23), U(s, 22))),
            [&](ImVec2 p) { icon(s, dl, "dos", 16, p); }, "DOS Prompt", [&s] { new_tab(s, "DOS Prompt"); });
        x += U(s, 23) + U(s, 1);
        int n = 0;
        for (const auto* a : installed_agents(s)) {
            if (n++ == 2)
                break;
            const std::string cmd = a->cmd, name = a->name;
            quick(
                s, dl, ("##ql-" + cmd).c_str(), box(ImVec2(x, qy), ImVec2(U(s, 23), U(s, 22))),
                [&](ImVec2 p) {
                    image(s, dl, root_dir() / win::to_wide(a->icon), box(p, ImVec2(U(s, 16), U(s, 16))));
                },
                name, [&s, cmd, name] { new_tab(s, name, -1, cmd); });
            x += U(s, 23) + U(s, 1);
        }
        quick(
            s, dl, "##ql-mines", box(ImVec2(x, qy), ImVec2(U(s, 23), U(s, 22))),
            [&](ImVec2 p) { icon(s, dl, "mines", 16, p); }, "Mines", [&s] { set_app(s, "mines", "open"); });
        x += U(s, 23) + U(s, 2);
    }
    separator();
    // The tray at the end; the windows between.
    const auto agents = tray_agents(s);
    const std::string clock = k98::clock_text(local_now());
    const float tw = tray_width(s, agents, clock);
    const Box tb = box(ImVec2(row.x1 - tw, cy - U(s, 11)), ImVec2(tw, U(s, 22)));
    tray(s, dl, tb, agents, clock);
    const float avail = tb.x0 - U(s, 2) - x;
    std::vector<std::pair<std::string, std::string>> games;
    for (const char* k : {"mines", "sol"}) {
        const std::string state = app_state(s, k);
        if (!state.empty())
            games.emplace_back(k, state);
    }
    auto* sp = s.ws.active();
    const int ntabs = sp != nullptr ? static_cast<int>(sp->tabs.size()) : 0;
    const int n = ntabs + static_cast<int>(games.size());
    if (n > 0 && avail > 0) {
        const float gap = U(s, 3);
        const float each = std::clamp((avail - gap * static_cast<float>(n - 1)) / static_cast<float>(n),
                                      U(s, 40), U(s, 160));
        dl->PushClipRect(ImVec2(x, b.y0), ImVec2(tb.x0 - U(s, 2), b.y1), true);
        for (int i = 0; i < n; ++i) {
            const Box bb = box(ImVec2(x, cy - U(s, 11)), ImVec2(each, U(s, 22)));
            if (i < ntabs)
                tab_button(s, dl, i, sp->tabs[static_cast<std::size_t>(i)], bb);
            else
                game_button(s, dl, games[static_cast<std::size_t>(i - ntabs)].first,
                            games[static_cast<std::size_t>(i - ntabs)].second, bb);
            x += each + gap;
        }
        dl->PopClipRect();
    }
    window_drag_area(s, b.a(), b.b());
}

// ---------------------------------------------------------------- the desktop (desktop.js)

struct DesktopItem {
    std::string key;
    std::string icon;
    std::string text;
    bool selected = false;
    std::string badge;
    std::function<void()> open;
    std::function<MenuItems()> menu;
};

void desktop(App::State& s, ImDrawList* dl, Box b) {
    W98& w = st(s);
    const auto cwds = s.session_cwds();
    std::vector<DesktopItem> items;
    items.push_back({"computer", "computer", "My Computer", false, "",
                     [&s] { open_dialog(s, kind_dialog("mycomputer")); }, nullptr});
    items.push_back(
        {"bin", "bin", "Recycle Bin", false, "", [&s] { open_dialog(s, kind_dialog("bin")); }, nullptr});
    for (int i = 0; i < static_cast<int>(s.ws.spaces.size()); ++i) {
        const auto& sp = s.ws.spaces[static_cast<std::size_t>(i)];
        const std::string st2 = space_status(s, sp);
        const std::string badge = st2 == "blocked"   ? "warn-badge"
                                  : st2 == "working" ? "hourglass"
                                  : st2 == "done"    ? "check-badge"
                                                     : "";
        const std::string id = sp.id;
        const std::string name = s.ws.space_title(i, cwds);
        const bool active = i == s.ws.active_space;
        const int index = i;
        DesktopItem it{id,     !sp.machine.empty() ? "netfolder" : active ? "folder-open" : "folder",
                       name,   active,
                       badge,  [&s, id] { select_space_id(s, id); },
                       nullptr};
        it.menu = [&s, id, name, index] {
            MenuItems m;
            m.push_back(
                {"&Open", "", "", "", [&s, id] { select_space_id(s, id); }, {}, false, false, false, true});
            m.push_back({"New &Tab...", "", "", "", [&s, id, name, index] {
                             open_dialog(
                                 s, prompt("New Tab", "Type a name for the new tab in '" + name + "':", "dos",
                                           [&s, index](const std::string& n, const std::string&) {
                                               new_tab(s, n, index);
                                           }));
                         }});
            m.push_back({.sep = true});
            m.push_back({"Re&name", "", "", "", [&s, id, index] {
                             const std::string cur = index < static_cast<int>(s.ws.spaces.size())
                                                         ? s.ws.spaces[static_cast<std::size_t>(index)].name
                                                         : std::string();
                             open_dialog(s, prompt(
                                                "Rename", "Type a new name for this space:", "folder",
                                                [&s, id](const std::string& n, const std::string&) {
                                                    rename_space_id(s, id, n);
                                                },
                                                cur));
                         }});
            m.push_back({"&Delete", "", "", "", [&s, index] {
                             if (index < static_cast<int>(s.ws.spaces.size()))
                                 open_dialog(s, close_space_ask(s, index));
                         }});
            m.push_back({.sep = true});
            m.push_back(
                {"P&roperties", "", "", "", [&s, index, name] {
                     if (index >= static_cast<int>(s.ws.spaces.size()))
                         return;
                     const auto& spc = s.ws.spaces[static_cast<std::size_t>(index)];
                     std::string branch = "(none)";
                     std::string folder = spc.folder;
                     if (!spc.tabs.empty()) {
                         const auto ids = s.ws.sessions_in(spc.tabs.front());
                         if (!ids.empty() && s.views_by_id.count(ids.front())) {
                             const auto g = s.git.get(s.views_by_id[ids.front()].cwd, spc.machine);
                             if (g.repo && !g.branch.empty())
                                 branch = g.branch;
                             if (folder.empty())
                                 folder = s.views_by_id[ids.front()].cwd;
                         }
                     }
                     const std::string status = space_status(s, spc);
                     open_dialog(
                         s,
                         message(
                             name + " Properties",
                             "Type:  Space\nLocation:  " +
                                 (spc.machine.empty() ? std::string("This computer") : spc.machine) +
                                 "\nFolder:  " + (folder.empty() ? std::string("~") : display_path(folder)) +
                                 "\nBranch:  " + branch + "\nContains:  " + std::to_string(spc.tabs.size()) +
                                 " tab(s)\nStatus:  " + (status.empty() ? std::string("idle") : status),
                             "folder"));
                 }});
            return m;
        };
        items.push_back(it);
    }
    items.push_back({"mines", "mines", "Mines", false, "", [&s] { set_app(s, "mines", "open"); }, nullptr});
    items.push_back({"sol", "sol", "Solitaire", false, "", [&s] { set_app(s, "sol", "open"); }, nullptr});
    items.push_back({"net", "network", "Network Neighborhood", false, "",
                     [&s] { open_dialog(s, kind_dialog("network")); }, nullptr});

    // Each icon: 76 wide, pt 4, pb 6, gap 3; the 32 px icon (navy veil when selected, the badge
    // at the lower left), the label (max 74, white; navy with a yellow dotted edge when selected).
    std::vector<float> heights;
    float total = U(s, 6);
    for (const auto& it : items) {
        const auto ls = wrap(s, it.text, U(s, 74) - U(s, 4));
        const float h =
            U(s, 4) + U(s, 32) + U(s, 3) + U(s, 13) * static_cast<float>(ls.size()) + 2 * one(s) + U(s, 6);
        heights.push_back(h);
        total += h;
    }
    const float max_scroll = std::max(0.0f, total - b.h());
    if (ImGui::IsMouseHoveringRect(b.a(), b.b()) && ImGui::IsWindowHovered())
        w.desktop_scroll -= ImGui::GetIO().MouseWheel * U(s, 40);
    w.desktop_scroll = std::clamp(w.desktop_scroll, 0.0f, max_scroll);
    dl->PushClipRect(b.a(), b.b(), true);
    float y = b.y0 + U(s, 6) - w.desktop_scroll;
    std::function<void()> open;
    std::function<MenuItems()> menu;
    bool menu_open = false;
    bool icon_hovered = false;
    for (std::size_t i = 0; i < items.size(); ++i) {
        const auto& it = items[i];
        const float x = snap(b.x0 + (b.w() - U(s, 76)) * 0.5f);
        const Box ib = box(ImVec2(x, y), ImVec2(U(s, 76), heights[i]));
        ImGui::PushID(it.key.c_str());
        const Pressed p = area("##dk", ib);
        ImGui::PopID();
        icon_hovered = icon_hovered || p.hovered;
        if (p.hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            w.desktop_sel = it.key;
        if (p.clicked)
            open = it.open;
        if (p.right) {
            w.desktop_sel = it.key;
            if (it.menu) {
                menu = it.menu;
                menu_open = true;
            }
        }
        const bool on = it.selected || w.desktop_sel == it.key;
        const ImVec2 ip(snap(x + (U(s, 76) - U(s, 32)) * 0.5f), snap(y + U(s, 4)));
        icon(s, dl, it.icon, 32, ip);
        if (on)
            fill(dl, box(ip, ImVec2(U(s, 32), U(s, 32))), IM_COL32(0x00, 0x00, 0x80, 0x60));
        if (!it.badge.empty()) {
            int alpha = 255;
            if (it.badge == "warn-badge") {
                const double t = std::fmod(ImGui::GetTime() * 1000.0, 900.0) / 900.0;
                alpha = static_cast<int>(255.0 * (t < 0.5 ? t * 2 : 2 - t * 2));
            }
            icon(s, dl, it.badge, 16, ImVec2(ip.x - U(s, 2), ip.y + U(s, 32) + U(s, 2) - U(s, 16)), alpha);
        }
        const auto ls = wrap(s, it.text, U(s, 74) - U(s, 4));
        float lw = 0;
        for (const auto& l : ls)
            lw = std::max(lw, text_w(s, l));
        const float boxw = std::min(U(s, 74), lw + U(s, 2) + 2 * one(s));
        const float ly = snap(y + U(s, 4) + U(s, 32) + U(s, 3));
        const Box lb = box(ImVec2(x + (U(s, 76) - boxw) * 0.5f, ly),
                           ImVec2(boxw, U(s, 13) * static_cast<float>(ls.size()) + 2 * one(s)));
        if (on) {
            fill(dl, lb, rgb(C::kNavy));
            dl->AddRect(lb.a(), lb.b(), IM_COL32(0xff, 0xff, 0x00, 0xff), 0, 0, one(s));
        }
        float ty = lb.y0 + one(s);
        for (const auto& l : ls) {
            label(s, dl, lb.x0 + (lb.w() - text_w(s, l)) * 0.5f, ty, l, rgb(C::kWhite), false, 11, 13, false);
            ty += U(s, 13);
        }
        y += heights[i];
    }
    dl->PopClipRect();
    // The desktop itself: a press clears the selection; right-click is its menu.
    // (A focused terminal keeps an item active, so ImGui::IsAnyItemHovered() is no help here.)
    if (ImGui::IsMouseHoveringRect(b.a(), b.b()) && ImGui::IsWindowHovered() && !icon_hovered) {
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            w.desktop_sel.clear();
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            sfx(s, "menu");
            MenuItems arrange = {{"by &Name", "", "", "", nullptr, {}, true},
                                 {"by &Type", "", "", "", nullptr, {}, true},
                                 {"&Auto Arrange", "", "", "", nullptr, {}, true, true}};
            MenuItems neu = {
                {"&Space...", "folder", "", "",
                 [&s] {
                     open_dialog(s,
                                 prompt(
                                     "New Space", "Type a name for the new space:", "folder",
                                     [&s](const std::string& n, const std::string& m) { new_space(s, n, m); },
                                     std::nullopt, true));
                 }},
                {"&Tab...", "dos", "", "", [&s] {
                     open_dialog(s,
                                 prompt("New Tab", "Type a name for the new tab:", "dos",
                                        [&s](const std::string& n, const std::string&) { new_tab(s, n); }));
                 }}};
            open_menu(s,
                      {{"Arrange &Icons", "", "", "", nullptr, arrange},
                       {"Line &up Icons", "", "", "", nullptr, {}, true},
                       {.sep = true},
                       {"&Refresh", "", "", "", [&s] { run_command(s, "layout:win98"); }},
                       {.sep = true},
                       {"&Paste", "", "", "", nullptr, {}, true},
                       {"Paste &Shortcut", "", "", "", nullptr, {}, true},
                       {.sep = true},
                       {"Ne&w", "", "", "", nullptr, neu},
                       {.sep = true},
                       {"P&roperties", "", "", "", [&s] { open_dialog(s, kind_dialog("display")); }}},
                      std::nullopt);
        }
    }
    window_drag_area(s, b.a(), b.b());
    if (open)
        open();
    if (menu_open && menu)
        open_menu(s, menu(), std::nullopt);
}

// ---------------------------------------------------------------- Mines (mines.js)

k98::Mines::Options mines_options(App::State& s) {
    k98::Mines::Options o;
    const Json& j = shared(s, "win98.mines");
    if (j.is_object()) {
        o.level = j.value("level", o.level);
        o.marks = j.value("marks", o.marks);
        o.color = j.value("color", o.color);
        o.sound = j.value("sound", o.sound);
        if (const auto b = j.find("best"); b != j.end() && b->is_object())
            for (const auto& [k, v] : b->items())
                if (v.is_number())
                    o.best[k] = v.get<int>();
    }
    return o;
}

void save_mines(App::State& s) {
    const auto& o = st(s).mines->options();
    set_shared(s, "win98.mines",
               Json{{"level", o.level},
                    {"marks", o.marks},
                    {"color", o.color},
                    {"sound", o.sound},
                    {"best", o.best}});
}

void mines_request(App::State& s, const std::string& cmd) {
    W98& w = st(s);
    if (!w.mines)
        return;
    if (cmd == "exit") {
        set_app(s, "mines", "");
        w.mines->reset();
        return;
    }
    if (cmd == "best") {
        open_dialog(s, message("Fastest Mine Sweepers", w.mines->best_text()));
        return;
    }
    w.mines->request(cmd);
    if (std::exchange(w.mines->saved, false))
        save_mines(s);
}

// mines.js 125-136.
MenuItems mines_menu(App::State& s, bool help) {
    if (help)
        return {{"&Contents", "", "", "", [&s] { run_command(s, "app.help"); }},
                {.sep = true},
                {"&About Mines...", "", "", "", [&s] {
                     open_dialog(s, message("About Mines",
                                            "Mines\nHerdr 98\n\nFind all the mines without uncovering "
                                            "one.\nRight-click to mark a mine. "
                                            "Double-click a number to clear its neighbors."));
                 }}};
    const auto& o = st(s).mines->options();
    auto item = [&s](const std::string& name, const std::string& cmd) {
        return MenuItem{name, "", "", "", [&s, cmd] { mines_request(s, cmd); }};
    };
    MenuItems m;
    MenuItem n = item("&New", "new");
    n.shortcut = "F2";
    m.push_back(n);
    m.push_back({.sep = true});
    for (const char* level : {"beginner", "intermediate", "expert"}) {
        std::string label = std::string("&") + static_cast<char>(std::toupper(level[0])) + (level + 1);
        MenuItem it = item(label, level);
        it.radio = o.level == level;
        m.push_back(it);
    }
    m.push_back({.sep = true});
    MenuItem marks = item("&Marks (?)", "marks");
    marks.checked = o.marks;
    MenuItem col = item("&Color", "color");
    col.checked = o.color;
    MenuItem snd = item("&Sound", "sound");
    snd.checked = o.sound;
    m.push_back(marks);
    m.push_back(col);
    m.push_back(snd);
    m.push_back({.sep = true});
    m.push_back(item("&Best Times...", "best"));
    m.push_back({.sep = true});
    m.push_back(item("E&xit", "exit"));
    return m;
}

// kit.js 295-308 win(): the frame, the title bar, the menu bar, the body. Returns the inside box.
Box win_frame(App::State& s, ImDrawList* dl, ImVec2 at, float width, float height) {
    return bevel(s, dl, box(at, ImVec2(width, height)), Bevel::kWindow, rgb(C::kFace)).inset(one(s));
}

// mines.js 15-22.
void counter(App::State& s, ImDrawList* dl, ImVec2 at, int value) {
    const std::string chars = k98::counter_chars(value);
    const float w = U(s, 11) * 3 + U(s, 2) * 2 + U(s, 1) * 2 + 2 * one(s);
    const Box in = bevel(s, dl, box(at, ImVec2(w, U(s, 23) + 2 * one(s))), Bevel::kWell, rgb(0x000000))
                       .inset(0, U(s, 1), 0, U(s, 1));
    float x = in.x0;
    for (const char ch : chars) {
        const auto lit = k98::segments_lit(ch);
        for (int i = 0; i < 7; ++i) {
            const auto& r = k98::segment_rects()[static_cast<std::size_t>(i)];
            fill(dl,
                 box(ImVec2(x + U(s, static_cast<float>(r[0])), in.y0 + U(s, static_cast<float>(r[1]))),
                     ImVec2(U(s, static_cast<float>(r[2])), U(s, static_cast<float>(r[3])))),
                 lit[static_cast<std::size_t>(i)] ? rgb(0xff0000) : rgb(0x650000));
        }
        x += U(s, 11) + U(s, 2);
    }
}

void mines_art(App::State& s, ImDrawList* dl, const std::string& name, float size, ImVec2 at) {
    image(s, dl, root_dir() / L"icons" / win::to_wide("mines-" + name + ".svg"),
          box(at, ImVec2(U(s, size), U(s, size))));
}

ImVec2 render_mines(App::State& s, Window& win, ImDrawList* dl, ImVec2 at) {
    W98& w = st(s);
    auto& m = *w.mines;
    const long long now = now_ms();
    if (m.tick(now))
        (void)0;
    const float width = U(s, static_cast<float>(m.cols() * 16 + 22));
    const float board_h = U(s, static_cast<float>(m.rows() * 16)) + 6 * one(s);
    const float body_h = U(s, 5) + U(s, 33) + 2 * one(s) + U(s, 6) + board_h + U(s, 5);
    const float height = 3 * one(s) + U(s, 18) + U(s, 1) + U(s, 18) + U(s, 1) + body_h + 3 * one(s);
    const Box in = win_frame(s, dl, at, width, height).inset(one(s));
    title_bar(
        s, dl, "##mines-title", Box{in.x0, in.y0, in.x1, in.y0 + U(s, 18)}, "Mines",
        [&s, dl](ImVec2 p) { icon(s, dl, "mines", 16, p); }, true,
        {{"min", [&s] { set_app(s, "mines", "min"); }, "Minimize"},
         {"close",
          [&s] {
              set_app(s, "mines", "");
              st(s).mines->reset();
          },
          "Close"}},
        false);
    // The menu bar (18): Game, Help; a label stays pressed (a well) while its menu is open.
    float mx = in.x0;
    const float my = in.y0 + U(s, 18) + U(s, 1);
    for (int i = 0; i < 2; ++i) {
        const char* name = i == 0 ? "&Game" : "&Help";
        const std::string owner = "mines-" + std::to_string(i);
        const bool open = shared_str(s, "win98.menu.owner") == owner;
        const float bw = text_w(s, k98::plain(name)) + U(s, 12) + (open ? 2 * one(s) : 0.0f);
        const Box mb = box(ImVec2(mx, my), ImVec2(bw, U(s, 18)));
        ImGui::PushID(i);
        const Pressed p = area("##mm", mb, true);
        ImGui::PopID();
        const Box li = open ? bevel(s, dl, mb, Bevel::kWell) : mb;
        label(s, dl, li.x0 + U(s, 6), li.y0 + (li.h() - U(s, 13)) * 0.5f, name, rgb(C::kText));
        if (p.clicked)
            open_menu(s, mines_menu(s, i == 1), ImVec2(mb.x0, mb.y1), false, owner);
        mx += bw;
    }
    // The body: p 5, gap 6: the panel (a well, 33 tall) and the board.
    const float bx = in.x0 + U(s, 5);
    float by = my + U(s, 18) + U(s, 1) + U(s, 5);
    const float bwid = in.x1 - U(s, 5) - bx;
    {
        const Box pb = box(ImVec2(bx, by), ImVec2(bwid, U(s, 33) + 2 * one(s)));
        const Box pi = bevel(s, dl, pb, Bevel::kWell).inset(0, U(s, 5), 0, U(s, 5));
        const float cw = U(s, 11) * 3 + U(s, 2) * 2 + U(s, 1) * 2 + 2 * one(s);
        const float ch = U(s, 23) + 2 * one(s);
        counter(s, dl, ImVec2(pi.x0, snap(pi.y0 + (pi.h() - ch) * 0.5f)), m.mines_left());
        counter(s, dl, ImVec2(pi.x1 - cw, snap(pi.y0 + (pi.h() - ch) * 0.5f)), m.seconds());
        ButtonOpts o;
        o.pad_x = o.pad_y = 0;
        const Pressed f =
            button(s, dl, "##mines-face",
                   box(ImVec2(pi.x0 + (pi.w() - U(s, 26)) * 0.5f, pi.y0 + (pi.h() - U(s, 26)) * 0.5f),
                       ImVec2(U(s, 26), U(s, 26))),
                   o);
        mines_art(s, dl, "face-" + m.face(), 17,
                  ImVec2(snap(f.content.x0 + (f.content.w() - U(s, 17)) * 0.5f),
                         snap(f.content.y0 + (f.content.h() - U(s, 17)) * 0.5f)));
        if (f.clicked)
            m.reset();
        by = pb.y1 + U(s, 6);
    }
    const Box board =
        box(ImVec2(bx, by), ImVec2(U(s, static_cast<float>(m.cols() * 16)) + 6 * one(s), board_h));
    const Box cells = bevel(s, dl, board, Bevel::kPressed, rgb(C::kShadow)).inset(one(s));
    const bool lost = m.state() == k98::Mines::State::kLost;
    const bool playing = m.state() == k98::Mines::State::kReady || m.state() == k98::Mines::State::kPlaying;
    int hovered = -1;
    int active = -1;
    std::string sound;
    for (int i = 0; i < m.cols() * m.rows(); ++i) {
        const int cx = i % m.cols(), cy = i / m.cols();
        const Box cb = box(ImVec2(cells.x0 + U(s, static_cast<float>(cx * 16)),
                                  cells.y0 + U(s, static_cast<float>(cy * 16))),
                           ImVec2(U(s, 16), U(s, 16)));
        ImGui::PushID(i);
        const Pressed p = area("##cell", cb);
        ImGui::PopID();
        if (p.hovered)
            hovered = i;
        if (p.held)
            active = i;
        const auto& c = m.cells()[static_cast<std::size_t>(i)];
        if (p.clicked) {
            if (c.open && p.double_click)
                sound = m.chord(i, now);
            else
                sound = m.reveal(i, now);
            m.held = -1;
        }
        if (p.right)
            m.mark(i);
    }
    // mouse down held = the cell (left or middle), cleared when the pointer leaves it.
    if (active >= 0 && playing)
        m.held = active == hovered ? active : -1;
    else if (ImGui::IsMouseDown(ImGuiMouseButton_Middle) && hovered >= 0)
        m.held = hovered;
    else if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle))
        m.held = active >= 0 ? m.held : -1;
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Middle) && hovered >= 0) {
        sound = m.chord(hovered, now);
        m.held = -1;
    }
    for (int i = 0; i < m.cols() * m.rows(); ++i) {
        const auto& c = m.cells()[static_cast<std::size_t>(i)];
        const int cx = i % m.cols(), cy = i / m.cols();
        const Box cb = box(ImVec2(cells.x0 + U(s, static_cast<float>(cx * 16)),
                                  cells.y0 + U(s, static_cast<float>(cy * 16))),
                           ImVec2(U(s, 16), U(s, 16)));
        const bool exposed = c.open || (lost && c.mine && c.mark != 1);
        const bool down = m.held == i && c.mark != 1 && !exposed;
        fill(dl, cb, i == m.boom() ? rgb(0xff0000) : rgb(C::kFace));
        Box content;
        if (exposed || down) {
            fill(dl, Box{cb.x0, cb.y0, cb.x1, cb.y0 + one(s)}, rgb(C::kShadow));
            fill(dl, Box{cb.x0, cb.y0, cb.x0 + one(s), cb.y1}, rgb(C::kShadow));
            content = cb.inset(one(s), 0, 0, one(s));
        } else {
            const float t = 2 * one(s);
            fill(dl, Box{cb.x0, cb.y0, cb.x1, cb.y0 + t}, rgb(C::kHi));
            fill(dl, Box{cb.x0, cb.y0, cb.x0 + t, cb.y1}, rgb(C::kHi));
            fill(dl, Box{cb.x1 - t, cb.y0 + t, cb.x1, cb.y1}, rgb(C::kShadow));
            fill(dl, Box{cb.x0 + t, cb.y1 - t, cb.x1, cb.y1}, rgb(C::kShadow));
            content = cb.inset(t, 0, 0, t);
        }
        auto center_art = [&](const std::string& name) {
            mines_art(s, dl, name, 16,
                      ImVec2(snap(content.x0 + (content.w() - U(s, 16)) * 0.5f),
                             snap(content.y0 + (content.h() - U(s, 16)) * 0.5f)));
        };
        if (lost && c.mark == 1 && !c.mine) {
            center_art("wrong");
        } else if (c.mark == 1) {
            center_art("flag");
        } else if (exposed && c.mine) {
            center_art("mine");
        } else if (!exposed && c.mark == 2) {
            const float tw = text_w(s, "?", true);
            label(s, dl, content.x0 + (content.w() - tw) * 0.5f, content.y0 + (content.h() - U(s, 13)) * 0.5f,
                  "?", rgb(C::kText), true, 11, 13, false);
        } else if (c.open && c.n != 0) {
            const std::string t = std::to_string(c.n);
            const float tw = w.arial->CalcTextSizeA(U(s, 14), FLT_MAX, 0, t.c_str()).x;
            label(s, dl, content.x0 + (content.w() - tw) * 0.5f, content.y0 + (content.h() - U(s, 14)) * 0.5f,
                  t, m.options().color ? rgb(k98::mines_number_color(c.n)) : rgb(C::kText), true, 14, 14,
                  false, w.arial);
        }
    }
    area("##mines-own", Box{in.x0, my, in.x1, in.y1});  // own(): the menu bar and body are not for dragging
    if (!sound.empty())
        sfx(s, sound);
    if (std::exchange(m.saved, false))
        save_mines(s);
    (void)win;
    return ImVec2(width, snap(height));
}

// ---------------------------------------------------------------- Solitaire (sol.js)

using Sol = k98::Solitaire;

void sol_card(App::State& s, ImDrawList* dl, int c, bool up, bool selected, ImVec2 at, int deck) {
    const std::string file = up ? std::string(selected ? "select-" : "") + std::to_string(Sol::suit(c)) +
                                      "-" + std::to_string(Sol::rank(c))
                                : "back-" + std::to_string(deck);
    image(s, dl, root_dir() / L"img" / win::to_wide("sol-" + file + ".svg"),
          box(at, ImVec2(U(s, Sol::kCW), U(s, Sol::kCH))));
}

int sol_deck(App::State& s) {
    const Json& j = shared(s, "win98.sol.deck");
    return j.is_number() ? j.get<int>() : 0;
}

void sol_command(App::State& s, const std::string& cmd) {
    W98& w = st(s);
    if (!w.sol)
        return;
    if (cmd == "options" || cmd == "deck") {
        auto win = std::make_unique<Window>();
        win->role = "sol-dialog";
        win->sol_dialog = cmd;
        win->modal = true;
        win->draw = w.sol->draw;
        win->scoring = w.sol->scoring;
        win->deck = sol_deck(s);
        open_window(s, std::move(win));
        return;
    }
    if (cmd == "exit") {
        set_app(s, "sol", "");
        return;
    }
    if (cmd == "help") {
        open_dialog(s, message("Solitaire Help",
                               "Build each suit from Ace to King in the four foundations. Build the columns "
                               "down in alternating "
                               "colors. Only a King can fill an empty column. Click or drag cards to move "
                               "them. Double-click a card "
                               "to send it home."));
        return;
    }
    if (cmd == "about") {
        open_dialog(s, message("About Solitaire",
                               "Solitaire\nHerdr 98\n\nKlondike. A little patience goes a long way."));
        return;
    }
    w.sol->command(cmd);
    w.sol_press.reset();
    w.sol_drag.reset();
}

ImVec2 render_sol(App::State& s, Window& /*win*/, ImDrawList* dl, ImVec2 at) {
    W98& w = st(s);
    Sol& g = *w.sol;
    // sol.js 371-375: the options from shared; the clock runs while the window is open.
    const Json& jd = shared(s, "win98.sol.draw");
    const Json& js = shared(s, "win98.sol.scoring");
    if (jd.is_number())
        g.draw = jd.get<int>();
    if (js.is_boolean())
        g.scoring = js.get<bool>();
    const int deck = sol_deck(s);
    const auto now = Clock::now();
    if (g.running && !g.won) {
        while (now - w.sol_tick >= std::chrono::seconds(1)) {
            if (w.sol_tick == Clock::time_point{}) {
                w.sol_tick = now;
                break;
            }
            w.sol_tick += std::chrono::seconds(1);
            g.tick_second();
        }
    } else {
        w.sol_tick = now;
    }
    if (g.animating) {
        while (now - w.sol_anim >= std::chrono::milliseconds(33)) {
            if (w.sol_anim == Clock::time_point{} || now - w.sol_anim > std::chrono::seconds(1)) {
                w.sol_anim = now;
                g.animate();
                break;
            }
            w.sol_anim += std::chrono::milliseconds(33);
            if (!g.animate())
                break;
        }
    }
    if (!g.sound.empty())
        sfx(s, std::exchange(g.sound, {}));
    const float o = one(s);
    const float width = U(s, 602);
    const float height =
        3 * o + U(s, 18) + U(s, 1) + U(s, 19) + U(s, 1) + U(s, Sol::kH) + U(s, 1) + U(s, 19) + 3 * o;
    const Box in = win_frame(s, dl, at, width, height).inset(o);
    title_bar(
        s, dl, "##sol-title", Box{in.x0, in.y0, in.x1, in.y0 + U(s, 18)}, "Solitaire",
        [&s, dl](ImVec2 p) { icon(s, dl, "sol", 16, p); }, true,
        {{"min", [&s] { set_app(s, "sol", "min"); }, "Minimize"},
         {"close", [&s] { set_app(s, "sol", ""); }, "Close"}},
        false);
    // The menu (19 tall, gap 2): Game, Help, pressed while open.
    float mx = in.x0;
    const float my = in.y0 + U(s, 18) + U(s, 1);
    for (const char* name : {"Game", "Help"}) {
        const bool open = w.sol_menu == name && shared_str(s, "win98.menu.owner") == "sol";
        const std::string lab = std::string("&") + name;
        const float bw = text_w(s, name) + U(s, 12) + (open ? 4 * o : 0.0f);
        const Box mb = box(ImVec2(mx, my), ImVec2(bw, U(s, 19)));
        ImGui::PushID(name);
        const Pressed p = area("##sm", mb, true);
        ImGui::PopID();
        const Box li = open ? bevel(s, dl, mb, Bevel::kPressed) : mb;
        label(s, dl, li.x0 + U(s, 6), li.y0 + U(s, 1) + (li.h() - U(s, 2) - U(s, 13)) * 0.5f, lab,
              rgb(C::kText));
        if (p.clicked) {
            w.sol_menu = name;
            MenuItems items;
            if (std::string(name) == "Game") {
                const std::pair<const char*, const char*> entries[] = {
                    {"&Deal", "deal"},          {nullptr, nullptr}, {"&Undo", "undo"}, {"&Deck...", "deck"},
                    {"&Options...", "options"}, {nullptr, nullptr}, {"E&xit", "exit"}};
                for (const auto& [l, c] : entries) {
                    if (l == nullptr) {
                        items.push_back({.sep = true});
                        continue;
                    }
                    const std::string cmd = c;
                    MenuItem it{l, "", "", "", [&s, cmd] { sol_command(s, cmd); }};
                    it.disabled = cmd == "undo" && !g.undo;
                    items.push_back(it);
                }
            } else {
                items = {{"&Contents", "", "", "", [&s] { sol_command(s, "help"); }},
                         {"&About Solitaire...", "", "", "", [&s] { sol_command(s, "about"); }}};
            }
            open_menu(s, std::move(items), ImVec2(mb.x0, mb.y1), false, "sol");
        }
        mx += bw + U(s, 2);
    }
    // The table: 596 x 355, green; positions in Win98 px from its corner.
    const Box table = box(ImVec2(in.x0, my + U(s, 19) + U(s, 1)), ImVec2(U(s, Sol::kW), U(s, Sol::kH)));
    fill(dl, table, rgb(0x008000));
    dl->PushClipRect(table.a(), table.b(), true);
    auto P = [&](float x, float y) { return ImVec2(snap(table.x0 + U(s, x)), snap(table.y0 + U(s, y))); };
    auto spot = [&](float x, float y) {
        const Box b = box(P(x, y), ImVec2(U(s, Sol::kCW), U(s, Sol::kCH)));
        dl->AddRect(b.a(), b.b(), rgb(0x004000), U(s, 3), 0, o);
        return b;
    };
    // Hit targets, in drawing order (the last one under the pointer takes the press).
    struct Hit {
        Box b;
        int what;  // 0 stock, 1 card, 2 home spot, 3 tab spot
        Sol::Loc loc;
        bool up = true;
    };
    std::vector<Hit> hits;
    const bool dragging = w.sol_drag.has_value() && w.sol_press.has_value();
    // The stock: a card back, or the turn-over arrow.
    {
        const Box b = spot(12, 10);
        if (!g.g.stock.empty()) {
            sol_card(s, dl, 0, false, false, b.a(), deck);
        } else {
            ImFont* f = face(s, false);
            const char* arrow = "\xE2\x86\xBB";
            const float sz = U(s, 32);
            const ImVec2 t = f->CalcTextSizeA(sz, FLT_MAX, 0, arrow);
            dl->AddText(f, sz, ImVec2(snap(b.x0 + (b.w() - t.x) * 0.5f), snap(b.y0 + (b.h() - t.y) * 0.5f)),
                        rgb(0x004000), arrow);
        }
        hits.push_back({b, 0, {}});
    }
    auto add = [&](const Sol::Loc& loc, int c, float x, float y, bool up = true) {
        if (dragging && w.sol_press->loc.type == loc.type && w.sol_press->loc.i == loc.i &&
            loc.n >= w.sol_press->loc.n)
            return;
        const bool selected = g.pick && g.pick->type == loc.type && g.pick->i == loc.i && loc.n >= g.pick->n;
        sol_card(s, dl, c, up, selected, P(x, y), deck);
        hits.push_back({box(P(x, y), ImVec2(U(s, Sol::kCW), U(s, Sol::kCH))), 1, loc, up});
    };
    const int visible = std::min(g.draw, static_cast<int>(g.g.waste.size()));
    const int wn = static_cast<int>(g.g.waste.size());
    for (int j = wn - visible; j < wn; ++j)
        add({Sol::Pile::kWaste, 0, j}, g.g.waste[static_cast<std::size_t>(j)],
            static_cast<float>(94 + (j - wn + visible) * 15), 10);
    for (int i = 0; i < 4; ++i) {
        const auto& p = g.g.home[static_cast<std::size_t>(i)];
        const Sol::Loc loc{Sol::Pile::kHome, i, static_cast<int>(p.size()) - 1};
        hits.push_back({spot(static_cast<float>(258 + i * 82), 10), 2, loc});
        if (!p.empty())
            add(loc, p.back(), static_cast<float>(258 + i * 82), 10);
    }
    for (int i = 0; i < 7; ++i) {
        const auto& p = g.g.tab[static_cast<std::size_t>(i)];
        float y = 126;
        hits.push_back({spot(static_cast<float>(12 + i * 82), y), 3, {Sol::Pile::kTab, i, 0}});
        const float step = g.tab_step(i);
        for (int n = 0; n < static_cast<int>(p.size()); ++n) {
            add({Sol::Pile::kTab, i, n}, p[static_cast<std::size_t>(n)].c, static_cast<float>(12 + i * 82), y,
                p[static_cast<std::size_t>(n)].up);
            y += p[static_cast<std::size_t>(n)].up ? std::max(4.0f, step) : 7.0f;
        }
    }
    if (dragging) {
        const auto cards = g.cards(w.sol_press->loc);
        for (std::size_t i = 0; i < cards.size(); ++i)
            sol_card(s, dl, cards[i], true, false,
                     P(w.sol_drag->x, w.sol_drag->y + static_cast<float>(i) * 18), deck);
    }
    for (const auto& f : g.trails)
        sol_card(s, dl, f.c, true, false, P(f.x, f.y), deck);
    dl->PopClipRect();
    // The pointer on the table (sol.js 384-399, 401-407).
    const Pressed tp = area("##sol-table", table);
    const ImVec2 mp = ImGui::GetIO().MousePos;
    const ImVec2 pt((mp.x - table.x0) / K(s), (mp.y - table.y0) / K(s));
    if (tp.hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const Hit* hit = nullptr;
        for (const auto& h : hits)
            if (h.b.contains(mp))
                hit = &h;
        if (hit == nullptr) {
            g.animating = false;
        } else if (hit->what == 0) {
            g.stock();
        } else {
            const Sol::Down r = g.down(hit->loc);
            if (r == Sol::Down::kPick)
                w.sol_press =
                    W98::SolPress{hit->loc, mp, ImVec2((mp.x - hit->b.x0) / K(s), (mp.y - hit->b.y0) / K(s))};
            if (hit->what == 1 && hit->up && ImGui::GetIO().MouseClickedCount[0] >= 2) {
                // on_click with click_count 2: send it home.
                w.sol_press.reset();
                g.pick.reset();
                g.foundation(hit->loc);
            }
        }
    }
    if (w.sol_press && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        if (!tp.hovered && !w.sol_drag) {
            w.sol_press.reset();  // on_hover(false)
        } else if (w.sol_drag ||
                   std::abs(mp.x - w.sol_press->at.x) + std::abs(mp.y - w.sol_press->at.y) > 4 * s.dpi) {
            w.sol_drag = ImVec2(pt.x - w.sol_press->grab.x, pt.y - w.sol_press->grab.y);
            g.pick.reset();
        }
    }
    if (w.sol_press && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        const Sol::Loc from = w.sol_press->loc;
        if (w.sol_drag) {
            if (const auto to = Sol::target(pt.x, pt.y))
                g.move(from, *to);
            g.pick.reset();
        } else {
            g.pick = g.pick && *g.pick == from ? std::nullopt : std::optional<Sol::Loc>(from);
        }
        w.sol_press.reset();
        w.sol_drag.reset();
    }
    area("##sol-own",
         Box{in.x0, my, in.x1, in.y1});  // own(): the menu and the status bar are not for dragging
    // The status bar: the message, the score, the time.
    const float sy = table.y1 + U(s, 1);
    const float sh = U(s, 19);
    const Box tbx = box(ImVec2(in.x1 - U(s, 95), sy), ImVec2(U(s, 95), sh));
    const Box sbx = box(ImVec2(tbx.x0 - U(s, 2) - U(s, 95), sy), ImVec2(U(s, 95), sh));
    const Box mbx{in.x0, sy, sbx.x0 - U(s, 2), sy + sh};
    const Box mi = well(s, dl, mbx);
    label(s, dl, mi.x0, mi.y0 + (mi.h() - U(s, 13)) * 0.5f, g.won ? "You won!" : "", rgb(C::kText), false, 11,
          13, false);
    const Box si = well(s, dl, sbx);
    label(s, dl, si.x0, si.y0 + (si.h() - U(s, 13)) * 0.5f,
          "Score: " + std::to_string(g.scoring ? g.g.score : 0), rgb(C::kText), false, 11, 13, false);
    const Box ti = well(s, dl, tbx);
    label(s, dl, ti.x0, ti.y0 + (ti.h() - U(s, 13)) * 0.5f, "Time: " + std::to_string(g.seconds),
          rgb(C::kText), false, 11, 13, false);
    return ImVec2(width, snap(height));
}

// sol.js 345-365: Options and Select Card Back (a dialog over the window).
ImVec2 render_sol_dialog(App::State& s, Window& win, ImDrawList* dl, ImVec2 at) {
    const float o = one(s);
    const float width = U(s, 336);
    ImDrawListSplitter split;
    split.Split(dl, 2);
    split.SetCurrentChannel(dl, 1);
    const Box in{at.x + 3 * o, at.y + 3 * o, at.x + width - 3 * o, at.y + 100000};
    title_bar(s, dl, "##sold-title", Box{in.x0, in.y0, in.x1, in.y0 + U(s, 18)},
              win.sol_dialog == "options" ? "Options" : "Select Card Back", nullptr, true,
              {{"close", [&s, &win] { close_window(s, win.id); }, nullptr}}, false);
    const float x0 = in.x0 + U(s, 15), x1 = in.x1 - U(s, 15);
    float y = in.y0 + U(s, 18) + U(s, 1) + U(s, 15);
    auto choice = [&](const std::string& text, bool selected) {
        const std::string t = std::string(selected ? "\xE2\x97\x89" : "\xE2\x97\x8B") + "  " + text;
        const Pressed p = area(("##opt-" + text).c_str(), box(ImVec2(x0, y), ImVec2(x1 - x0, U(s, 23))));
        label(s, dl, x0, y + U(s, 5), t, rgb(C::kText), false, 11, 13, false);
        y += U(s, 23) + U(s, 4);
        return p.clicked;
    };
    if (win.sol_dialog == "options") {
        label(s, dl, x0, y, "Draw", rgb(C::kText), true, 11, 13, false);
        y += U(s, 13) + U(s, 4);
        for (const int n : {1, 3})
            if (choice(n == 1 ? "Draw One" : "Draw Three", win.draw == n))
                win.draw = n;
        y += U(s, 8);
        label(s, dl, x0, y, "Scoring", rgb(C::kText), true, 11, 13, false);
        y += U(s, 13) + U(s, 4);
        for (const bool v : {true, false})
            if (choice(v ? "Standard" : "None", win.scoring == v))
                win.scoring = v;
    } else {
        label(s, dl, x0, y, "Select a card back:", rgb(C::kText), false, 11, 13, false);
        y += U(s, 13) + U(s, 4);
        float x = x0;
        for (const int n : {0, 1}) {
            const Box b = box(ImVec2(x, y),
                              ImVec2(U(s, Sol::kCW) + U(s, 6) + U(s, 4), U(s, Sol::kCH) + U(s, 6) + U(s, 4)));
            ImGui::PushID(n);
            const Pressed p = area("##deck", b);
            ImGui::PopID();
            dl->AddRect(b.a(), b.b(), win.deck == n ? rgb(0x000080) : rgb(0xc0c0c0), 0, 0, U(s, 2));
            sol_card(s, dl, 0, false, false, ImVec2(b.x0 + U(s, 5), b.y0 + U(s, 5)), n);
            if (p.clicked)
                win.deck = n;
            x = b.x1 + U(s, 14);
            if (n == 1)
                y = b.y1 + U(s, 4);
        }
    }
    y += U(s, 12) - U(s, 4);
    const int hit = buttons(s, dl, x0, x1, y, {{"OK", true}, {"Cancel"}}, true);
    if (hit == 0) {
        set_shared(s, "win98.sol.draw", win.draw);
        set_shared(s, "win98.sol.scoring", win.scoring);
        set_shared(s, "win98.sol.deck", win.deck);
        close_window(s, win.id);
    } else if (hit == 1) {
        close_window(s, win.id);
    }
    const float height = snap(y + U(s, 23) + U(s, 15) + 3 * o - at.y);
    split.SetCurrentChannel(dl, 0);
    bevel(s, dl, box(at, ImVec2(width, height)), Bevel::kWindow, rgb(C::kFace));
    split.Merge(dl);
    return ImVec2(width, height);
}

// ---------------------------------------------------------------- the Office Assistant (clippy.js)

const char* const kTips[] = {
    "It looks like you're herding agents.\n\nDid you know? Ctrl+Esc opens the Start menu.",
    "Click a tab's taskbar button while it's\nshown to minimize it (Herdr settles it).\nClick again to bring "
    "it back.",
    "Ctrl+Shift+Esc opens Close Program:\nevery terminal Herdr runs, and End Task.",
    "Right-click a folder on the desktop to\nrename or close that space.",
    "Waiting on an agent? Mines is on\nthe desktop. I won't tell anyone.",
    "Double-click a window's title bar to\nmaximize it. Double-click again to restore.",
    "Start > Run... opens any command line\nin a new window, like it's 1998.",
    "Shut Down > Restart in DOS mode\ntakes you to the Terminals layout.",
    "Log Off takes you back to Herdr's\nnormal layout. I'll miss you.",
};

struct News {
    std::string id;
    std::string status;
    std::string name;
    std::string where;
};

// clippy.js 457-463: the agent that needs you (blocked), else the latest that finished, while you
// look elsewhere and did not dismiss it.
std::optional<News> clippy_news(App::State& s) {
    W98& w = st(s);
    const auto cwds = s.session_cwds();
    std::vector<std::pair<long long, News>> list;
    for (const auto& id : s.agent_order) {
        const auto it = s.views_by_id.find(id);
        if (it == s.views_by_id.end() || !is_agent(it->second))
            continue;
        const std::string status = status_word(s.attention_of(id));
        if (status != "blocked" && status != "done")
            continue;
        if (s.ws.focused_session() == id && s.window_focused)
            continue;
        if (std::find(w.clippy.dismissed.begin(), w.clippy.dismissed.end(), id + ":" + status) !=
            w.clippy.dismissed.end())
            continue;
        const auto loc = s.ws.find(id);
        std::string sname, tname;
        if (loc.space >= 0) {
            sname = s.ws.space_title(loc.space, cwds);
            tname = ui::Workspace::tab_title(
                s.ws.spaces[static_cast<std::size_t>(loc.space)].tabs[static_cast<std::size_t>(loc.tab)],
                loc.tab);
        }
        const auto ch = s.changed.find(id);
        const long long seq = ch == s.changed.end() ? 0 : ch->second.at;
        list.push_back({seq,
                        {id, status, program_info(it->second.program, it->second.profile_id).name,
                         "'" + sname + " \\ " + tname + "'"}});
    }
    std::stable_sort(list.begin(), list.end(), [](const auto& a, const auto& b) {
        const int ab = a.second.status == "blocked" ? 1 : 0, bb = b.second.status == "blocked" ? 1 : 0;
        if (ab != bb)
            return ab < bb;
        return a.first < b.first;
    });
    // Clean the dismissed list once their agents moved on.
    std::erase_if(w.clippy.dismissed, [&](const std::string& k) {
        const auto colon = k.rfind(':');
        const std::string id = k.substr(0, colon), status = k.substr(colon + 1);
        return status_word(s.attention_of(id)) != status;
    });
    if (list.empty())
        return std::nullopt;
    return list.back().second;
}

void clippy(App::State& s, Box owner) {
    if (shared_str(s, "win98.clippy") == "off")
        return;
    W98& w = st(s);
    const auto news = clippy_news(s);
    std::string text;
    std::vector<std::pair<std::string, std::function<void()>>> actions;
    if (news) {
        text = news->status == "blocked" ? "It looks like " + news->name + " in\n" + news->where +
                                               " is waiting for you.\n\nWould you like help?"
                                         : "It looks like " + news->name + " finished\nin " + news->where +
                                               ".\n\nWant to see what it did?";
        const std::string key = news->id + ":" + news->status;
        const std::string id = news->id;
        actions.push_back({"Take me there", [&s, &w, id, key] {
                               run_command(s, "agent.focus:" + id);
                               w.clippy.dismissed.push_back(key);
                           }});
        actions.push_back({"Not now", [&w, key] { w.clippy.dismissed.push_back(key); }});
    } else if (w.clippy.tip) {
        const int n = static_cast<int>(std::size(kTips));
        text = kTips[*w.clippy.tip % n];
        actions.push_back({"Next tip", [&w, n] { w.clippy.tip = (*w.clippy.tip + 1) % n; }});
        actions.push_back({"Close", [&w] { w.clippy.tip.reset(); }});
        actions.push_back({"Hide me", [&s] { set_shared(s, "win98.clippy", "off"); }});
    }
    // Its size: p 4, gap 2, the balloon (200 wide) over Clippy (64 x 80).
    const float bw = U(s, 200);
    std::vector<std::string> ls;
    float bh = 0;
    if (!text.empty()) {
        ls = wrap(s, text, bw - U(s, 16) - 2 * one(s));
        bh = U(s, 8) * 2 + 2 * one(s) + U(s, 13) * static_cast<float>(ls.size()) + U(s, 8) +
             U(s, 13) * static_cast<float>(actions.size()) + U(s, 3) * static_cast<float>(actions.size() - 1);
    }
    const float cw = std::max(U(s, 64), text.empty() ? 0.0f : bw) + U(s, 8);
    const float ch = U(s, 8) + (text.empty() ? 0.0f : bh + U(s, 2)) + U(s, 80);
    // win98.json 70-80: on the left edge of the screen row, at its end; dragged elsewhere it
    // snaps to the nearest edge and third (layout.rs 732-751), 12 px in (floats.rs 14, 78-96).
    const float m = px(12);
    const Json& pj = shared(s, "win98.float.clippy");
    const k98::FloatPlace place = pj.is_object()
                                      ? k98::parse_place(pj.value("edge", ""), pj.value("align", ""))
                                      : k98::FloatPlace{k98::Edge::kLeft, k98::Align::kEnd};
    const bool right = place.edge != k98::Edge::kLeft;  // clippy.js 469
    const auto home = k98::float_home(owner.x0, owner.y0, owner.w(), owner.h(), cw, ch, place, m);
    ImVec2 at(snap(home[0]), snap(home[1]));
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    if (w.clippy.moving)
        at = ImVec2(snap(mouse.x - cw * w.clippy.grab.x), snap(mouse.y - ch * w.clippy.grab.y));
    ImGui::SetNextWindowPos(at);
    ImGui::SetNextWindowSize(ImVec2(cw, ch));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, w.clippy.moving ? 0.92f : 1.0f);
    ImGui::Begin("##w98clippy", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoBackground |
                     ImGuiWindowFlags_NoFocusOnAppearing);
    ImGui::PopStyleVar(3);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    g_area_hovered = false;
    float y = at.y + U(s, 4);
    const float inner = cw - U(s, 8);
    const float x = at.x + U(s, 4);
    std::function<void()> run;
    if (!text.empty()) {
        const Box b = box(ImVec2(x + (right ? inner - bw : 0.0f), y), ImVec2(bw, bh));
        dl->AddRectFilled(b.a(), b.b(), rgb(C::kTip), U(s, 8));
        dl->AddRect(b.a(), b.b(), rgb(C::kText), U(s, 8), 0, one(s));
        float ty = b.y0 + one(s) + U(s, 8);
        ty += lines(s, dl, b.x0 + one(s) + U(s, 8), ty, ls, rgb(C::kText)) + U(s, 8);
        for (std::size_t i = 0; i < actions.size(); ++i) {
            const float ax = b.x0 + one(s) + U(s, 8);
            const float tw = text_w(s, actions[i].first);
            ImGui::PushID(static_cast<int>(i));
            const Pressed p = area("##clip", box(ImVec2(ax, ty), ImVec2(U(s, 12) + tw, U(s, 13))));
            ImGui::PopID();
            if (p.hovered)
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            dl->AddCircleFilled(ImVec2(ax + U(s, 3.5f), ty + U(s, 6.5f)), U(s, 3.5f), rgb(C::kNavy), 12);
            const float lx = ax + U(s, 7) + U(s, 5);
            label(s, dl, lx, ty, actions[i].first, rgb(C::kNavy), false, 11, 13, false);
            const float base = text_top(s, ty) + face(s, false)->GetFontBaked(U(s, 11))->Ascent;
            fill(dl, Box{snap(lx), snap(base + 1), snap(lx + tw), snap(base + 1) + one(s)}, rgb(C::kNavy));
            if (p.clicked)
                run = actions[i].second;
            ty += U(s, 13) + U(s, 3);
        }
        y = b.y1 + U(s, 2);
    }
    // loop("top", {from: 0, to: -5, period: 800}) while there is news.
    float bounce = 0;
    if (news) {
        const double t = std::fmod(ImGui::GetTime() * 1000.0, 800.0) / 800.0;
        bounce = -U(s, 5) * static_cast<float>(t < 0.5 ? t * 2 : 2 - t * 2);
    }
    const Box cb = box(ImVec2(x + (right ? inner - U(s, 64) : 0.0f), y + bounce), ImVec2(U(s, 64), U(s, 80)));
    const Pressed p = area("##clippy", cb);
    if (p.hovered)
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    image(s, dl, root_dir() / L"img" / L"clippy.svg", cb);
    if (p.clicked) {
        if (w.clippy.tip)
            w.clippy.tip.reset();
        else
            w.clippy.tip = static_cast<int>(
                std::floor(static_cast<double>(std::rand()) / (RAND_MAX + 1.0) * std::size(kTips)));
        sfx(s, "clippy");
    }
    // floats.rs 253-300: a press on it that no control takes drags it (from 4 px); dropped, it
    // takes the place its center is nearest to.
    if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !g_area_hovered) {
        w.clippy.pressing = true;
        w.clippy.down = mouse;
        w.clippy.grab = ImVec2((mouse.x - at.x) / cw, (mouse.y - at.y) / ch);
    }
    ImGui::End();
    if (w.clippy.pressing) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            if (!w.clippy.moving &&
                std::hypot(mouse.x - w.clippy.down.x, mouse.y - w.clippy.down.y) >= 4 * s.dpi)
                w.clippy.moving = true;
        } else {
            if (w.clippy.moving) {
                const float cx = mouse.x - cw * w.clippy.grab.x + cw * 0.5f - owner.x0;
                const float cy = mouse.y - ch * w.clippy.grab.y + ch * 0.5f - owner.y0;
                const auto to = k98::snap_float(cx, cy, owner.w(), owner.h());
                set_shared(s, "win98.float.clippy",
                           Json{{"edge", k98::edge_name(to.edge)}, {"align", k98::align_name(to.align)}});
            }
            w.clippy.pressing = w.clippy.moving = false;
        }
    }
    if (run)
        run();
}

// ---------------------------------------------------------------- the overlay windows

// taskbar.js 181-193: one window per open (or minimized) game.
void sync_games(App::State& s) {
    W98& w = st(s);
    for (const char* k : {"mines", "sol"}) {
        const bool want = !app_state(s, k).empty();
        Window* have = find_key(s, k);
        if (want && have == nullptr) {
            auto win = std::make_unique<Window>();
            win->role = k;
            win->key = k;
            if (std::string(k) == "mines")
                w.mines = std::make_unique<k98::Mines>(mines_options(s));
            else
                w.sol = std::make_unique<Sol>();
            open_window(s, std::move(win));
        } else if (!want && have != nullptr) {
            have->closing = true;
        }
    }
}

void draw_windows(App::State& s) {
    W98& w = st(s);
    // Closed windows go first (their games too).
    bool closed = false;
    for (auto& win : w.windows)
        if (win->closing) {
            closed = closed || win->role == "dialog" || win->role == "sol-dialog";
            if (win->role == "mines")
                w.mines.reset();
            if (win->role == "sol") {
                w.sol.reset();
                w.sol_press.reset();
                w.sol_drag.reset();
            }
            if (w.drag_id == win->id)
                w.drag_id = 0;
        }
    std::erase_if(w.windows, [](const auto& x) { return x->closing; });
    if (closed && w.menus.empty())
        keyboard_back(s);
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    std::vector<int> ids;
    for (const auto& win : w.windows)
        ids.push_back(win->id);
    std::vector<std::string> order;
    for (const int id : ids) {
        Window* win = find_window(s, id);
        if (win == nullptr || win->closing)
            continue;
        if ((win->role == "mines" || win->role == "sol") && app_state(s, win->role) != "open") {
            if (win->role == "sol" && w.sol) {
                w.sol->animating = false;  // sol.js 374: stop()
                w.sol_press.reset();
                w.sol_drag.reset();
            }
            continue;  // a minimized game draws nothing
        }
        if ((win->role == "mines" && !w.mines) ||
            ((win->role == "sol" || win->role == "sol-dialog") && !w.sol)) {
            win->closing = true;
            continue;
        }
        const std::string name = "##w98win" + std::to_string(id);
        if (win->modal) {
            // herdr dialog overlays dim the window and keep clicks beside them.
            const std::string dim = name + "dim";
            ImGui::SetNextWindowPos(vp->WorkPos);
            ImGui::SetNextWindowSize(vp->WorkSize);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
            ImGui::Begin(dim.c_str(), nullptr,
                         ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav |
                             ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoFocusOnAppearing);
            ImGui::PopStyleVar(3);
            ImGui::GetWindowDrawList()->AddRectFilled(
                vp->WorkPos, ImVec2(vp->WorkPos.x + vp->WorkSize.x, vp->WorkPos.y + vp->WorkSize.y),
                IM_COL32(0, 0, 0, 89));
            ImGui::SetCursorScreenPos(vp->WorkPos);
            ImGui::InvisibleButton("##dim", vp->WorkSize);
            ImGui::End();
            order.push_back(dim);
        }
        const bool screen = win->dialog && win->dialog->kind == "screen";
        ImVec2 pos = screen ? vp->WorkPos : win->pos;
        if (!win->placed && !screen)
            pos = ImVec2(vp->WorkPos.x - 10000, vp->WorkPos.y - 10000);  // measured once before it shows
        ImGui::SetNextWindowPos(pos);
        ImGui::SetNextWindowSize(screen ? vp->WorkSize
                                        : ImVec2(std::max(1.0f, win->size.x), std::max(1.0f, win->size.y)));
        if (win->want_focus) {
            ImGui::SetNextWindowFocus();
            win->want_focus = false;
        }
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
        ImGui::Begin(name.c_str(), nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav |
                         ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::PopStyleVar(3);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImGui::PushID(id);
        g_area_hovered = false;
        ImVec2 size;
        if (win->role == "dialog")
            size = render_dialog(s, *win, dl, pos);
        else if (win->role == "mines")
            size = render_mines(s, *win, dl, pos);
        else if (win->role == "sol")
            size = render_sol(s, *win, dl, pos);
        else
            size = render_sol_dialog(s, *win, dl, pos);
        ImGui::PopID();
        // The window pressed last is on top and has the keyboard; a press on no control drags it.
        const bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
        if (hovered &&
            (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right))) {
            w.focus = id;
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !g_area_hovered && !screen) {
                w.drag_id = id;
                w.drag_grab =
                    ImVec2(ImGui::GetIO().MousePos.x - win->pos.x, ImGui::GetIO().MousePos.y - win->pos.y);
            }
        }
        if (w.focus == id && win->dialog && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
            w.menus.empty())
            dialog_keys(s, *win, *win->dialog);
        ImGui::End();
        win = find_window(s, id);
        if (win == nullptr)
            continue;
        win->size = size;
        if (!win->placed && !screen) {
            win->pos = ImVec2(snap(vp->WorkPos.x + (vp->WorkSize.x - size.x) * 0.5f),
                              snap(vp->WorkPos.y + (vp->WorkSize.y - size.y) * 0.5f));
            win->placed = true;
        }
        if (w.drag_id == id) {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                win->pos = ImVec2(snap(ImGui::GetIO().MousePos.x - w.drag_grab.x),
                                  snap(ImGui::GetIO().MousePos.y - w.drag_grab.y));
            } else {
                w.drag_id = 0;
                if (!win->key.empty())
                    w.dropped[win->key] = win->pos;
            }
        }
        order.push_back(name);
    }
    // Pressed last, on top.
    if (w.focus != 0) {
        const auto it =
            std::find_if(w.windows.begin(), w.windows.end(), [&](const auto& x) { return x->id == w.focus; });
        if (it != w.windows.end() && it + 1 != w.windows.end()) {
            auto moved = std::move(*it);
            w.windows.erase(it);
            w.windows.push_back(std::move(moved));
        }
    }
    for (const auto& n : order)
        raise(n);
}

// settings/main.js 302-309: the layout's preview picture (win98.json "preview":
// layouts/win98.svg), the width of the card and 62 tall, kept whole (an image fits inside its
// box), rounded 6 with a 1 px border.
void preview(App::State& s, ImDrawList* dl, ImVec2 a, ImVec2 size, float rounding, ImU32 border) {
    constexpr float kW = 320, kH = 200;  // its viewBox
    const float k = std::min(size.x / kW, size.y / kH);
    const ImVec2 p(snap(a.x + (size.x - kW * k) * 0.5f), snap(a.y + (size.y - kH * k) * 0.5f));
    const auto img =
        st(s).images.svg(root_dir() / L"win98.svg", static_cast<int>(kW * k), static_cast<int>(kH * k));
    if (img.ok())
        dl->AddImageRounded(img.tex, p,
                            ImVec2(p.x + static_cast<float>(img.w), p.y + static_cast<float>(img.h)),
                            ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, rounding);
    dl->AddRect(a, ImVec2(a.x + size.x, a.y + size.y), border, rounding);
}

// ---------------------------------------------------------------- the window (win98.json)

// The layout's background: its color (Display Properties > Wallpaper), or the clouds picture
// covering it (dialog.js 919-925).
void background(App::State& s, ImDrawList* dl, Box b) {
    const std::string c = shared_str(s, "win98.desktop");
    if (c == "clouds") {
        const auto img = st(s).images.picture(root_dir() / L"img" / L"clouds.png");
        if (img.ok()) {
            const float k = std::max(b.w() / static_cast<float>(img.w), b.h() / static_cast<float>(img.h));
            const float w2 = static_cast<float>(img.w) * k, h2 = static_cast<float>(img.h) * k;
            const ImVec2 a(b.x0 + (b.w() - w2) * 0.5f, b.y0 + (b.h() - h2) * 0.5f);
            dl->PushClipRect(b.a(), b.b(), true);
            dl->AddImage(img.tex, a, ImVec2(a.x + w2, a.y + h2));
            dl->PopClipRect();
            return;
        }
    }
    fill(dl, b, c.empty() ? rgb(C::kDesktop) : hex(c));
}

// win98.json: a column of the screen (the desktop, 84 px, and the panes with 10 px padding;
// Clippy floats over it) and the taskbar (28 px).
void draw(App::State& s, ImVec2 origin, ImVec2 full, bool connected) {
    W98& w = st(s);
    ensure_fonts(s);
    if (!w.was_shown) {
        // taskbar.js 68-107: a new taskbar: the Start menu closed, the boot (always when the app
        // just started).
        w.was_shown = true;
        if (!shared(s, "win98.start").is_null())
            set_shared(s, "win98.start", nullptr);
        const long long age = app_age_seconds();
        boot(s, age >= 0 && age < 45);
    }
    sync_games(s);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const Box all = box(origin, full);
    background(s, dl, all);
    const float tb_h = snap(U(s, k98::kTaskbarPx));
    const Box screen{all.x0, all.y0, all.x1, all.y1 - tb_h};
    const Box desk{screen.x0, screen.y0, snap(screen.x0 + U(s, k98::kDesktopPx)), screen.y1};
    const Box main{desk.x1 + px(10), screen.y0 + px(10), screen.x1 - px(10), screen.y1 - px(10)};
    if (s.ws.active_tab() != nullptr && main.w() > 0 && main.h() > 0)
        draw_panes(s, main.a(), ImVec2(main.w(), main.h()), connected);
    desktop(s, dl, desk);
    taskbar(s, dl, Box{all.x0, screen.y1, all.x1, all.y1});
    st(s).start_bounds.w = st(s).start_bounds.w;  // set by the taskbar this frame
    w.wakeup_ms = 250;
    (void)screen;
}

void overlays(App::State& s) {
    W98& w = st(s);
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float tb_h = snap(U(s, k98::kTaskbarPx));
    clippy(s, Box{vp->WorkPos.x, vp->WorkPos.y, vp->WorkPos.x + vp->WorkSize.x,
                  vp->WorkPos.y + vp->WorkSize.y - tb_h});
    raise("##w98clippy");  // a float: over the layout, under the overlays
    draw_windows(s);
    draw_menus(s);
    // Timers: the clock, blinking carets and badges, the games, the screens.
    long long ms = 1000;
    if (!w.windows.empty() || !w.menus.empty())
        ms = 50;
    if (w.sol && w.sol->animating)
        ms = 16;
    for (const auto& [id, v] : s.views_by_id)
        if (is_agent(v) && s.attention_of(id) != Attention::kNone && s.attention_of(id) != Attention::kIdle)
            ms = std::min<long long>(ms, 50);
    w.wakeup_ms = ms;
}

// A layout switch closes windows and menus (herdr.d.ts 405).
void hidden(App::State& s) {
    W98& w = st(s);
    if (!w.was_shown)
        return;
    w.was_shown = false;
    w.menus.clear();
    w.windows.clear();
    w.mines.reset();
    w.sol.reset();
    w.drag_id = 0;
    w.wakeup_ms = 1000;
}

}  // namespace w98
