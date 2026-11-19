// AgentDesk 98 chrome: command helpers, the panes' title bars, the Start menu, its commands and
// the taskbar.
#include "win98_internal.hpp"

namespace agentdesk::desktop {
namespace detail::w98 {

// Herdr 98, part 3: the window itself (herdr-app defaults/layouts/win98.json): the desktop, the
// panes' title bars, the taskbar and its Start menu, the games, the Office Assistant, and the
// layout's commands (keys.json section "layout:win98"). From defaults/mods/win98 taskbar.js,
// desktop.js, window.js, mines.js, sol.js, clippy.js (commit 321c9a8).

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

namespace {
std::string tab_status(App::State& s, const ui::Tab& t) {
    std::vector<Attention> atts;
    for (const auto& id : s.ws.sessions_in(t))
        atts.push_back(s.attention_of(id));
    return status_word(ui::most_urgent(atts));
}

}  // namespace

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

}  // namespace

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

namespace {
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

// dialog.js 919-925: the desktop's color or the clouds picture.
void set_desktop(App::State& s, const std::string& c) {
    set_shared(s, "win98.desktop", c);
}

namespace {
// window.js 9-17: a pane's title.
std::string pane_title_98(App::State& s, const std::string& id) {
    const auto it = s.views_by_id.find(id);
    if (it == s.views_by_id.end())
        return {};
    const auto& v = it->second;
    // window.js 12-13: no program, or a shell -> "DOS Prompt - <dos path>".
    const ui::Program prog = program_of(v);
    const std::string where = k98::dos_path(v.cwd, home_dir());
    if (prog.shell)
        return "DOS Prompt - " + where;
    const std::string pname = prog.name;
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
    ui::Program program;
    program.shell = true;  // no session: MS-DOS
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

}  // namespace

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

namespace {
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

}  // namespace

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

namespace {
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
    ui::Program program;
    program.shell = true;
    std::string pname;
    if (const auto it = s.views_by_id.find(lead_id); it != s.views_by_id.end()) {
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
        a.tip = a.program.name + ": " + a.status + " - " +
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

}  // namespace

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
}  // namespace detail::w98

}  // namespace agentdesk::desktop
