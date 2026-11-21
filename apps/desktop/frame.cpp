// Drawing the layout tree, the --input automation script, and App's per-frame members (tick,
// frame, before_new_frame).
// Portions adapted from herdr-app (Apache-2.0) and modified; see THIRD_PARTY_NOTICES.md.
#include "app_internal.hpp"

namespace agentdesk::desktop {
namespace detail {

// ------------------------------------------------------------------ the layout, drawn
// Every shown node of the layout tree gets its rect (lay::place). A node's background and
// border are painted under its children, and each block is drawn by what its content names:
// AgentDesk's built-in blocks in C++ (below), or a C++ DLL mod's panels for a block naming that
// mod.

// A layout colour: one of the theme's names (canvas, chrome, surface, ...) or #rrggbb /
// #rrggbbaa.
std::optional<ImU32> layout_color(const std::string& c) {
    if (!c.empty() && c[0] == '#' && (c.size() == 7 || c.size() == 9)) {
        unsigned v[4] = {0, 0, 0, 255};
        for (std::size_t i = 0; i + 1 < c.size(); i += 2) {
            const unsigned long x = std::strtoul(c.substr(i + 1, 2).c_str(), nullptr, 16);
            v[i / 2] = static_cast<unsigned>(x);
        }
        return IM_COL32(v[0], v[1], v[2], v[3]);
    }
    static const std::map<std::string, ImU32 Palette::*> kNames = {{"canvas", &Palette::bg},
                                                                   {"chrome", &Palette::sidebar},
                                                                   {"surface", &Palette::card},
                                                                   {"raised", &Palette::hover},
                                                                   {"background", &Palette::term_bg},
                                                                   {"border", &Palette::border},
                                                                   {"accent", &Palette::focus},
                                                                   {"hover", &Palette::hover},
                                                                   {"selected", &Palette::selected},
                                                                   {"foreground", &Palette::text},
                                                                   {"muted", &Palette::dim},
                                                                   {"dim", &Palette::faint},
                                                                   {"terminal_foreground", &Palette::term_fg},
                                                                   {"selection", &Palette::selection}};
    if (const auto it = kNames.find(c); it != kNames.end())
        return g_pal->*(it->second);
    return std::nullopt;
}

namespace {
// A stack's `"header": {"content": "tabs"}`: one tab per child, by its title (else its number);
// a click selects it (layout.select).
void draw_stack_tabs(App::State& s, const lay::Node& stack, const lay::Rect& r) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(ImVec2(r.x, r.y), ImVec2(r.right(), r.bottom()), g_pal->card);
    const lay::Node* active = stack.active_child();
    float x = r.x;
    for (std::size_t i = 0; i < stack.children.size(); ++i) {
        const auto& ch = stack.children[i];
        const std::string label = ch.title.value_or(std::to_string(i + 1));
        const float w = ImGui::CalcTextSize(label.c_str()).x + px(24);
        const bool on = active == &ch;
        if (on)
            dl->AddRectFilled(ImVec2(x, r.y), ImVec2(x + w, r.bottom()), g_pal->hover);
        dl->AddText(ImVec2(x + px(12), r.y + (r.h - ImGui::GetFontSize()) * 0.5f),
                    on ? g_pal->text : g_pal->dim, label.c_str());
        ImGui::SetCursorScreenPos(ImVec2(x, r.y));
        ImGui::PushID(("##stacktab-" + stack.id + "/" + ch.id).c_str());
        if (ImGui::InvisibleButton("##t", ImVec2(w, std::max(1.0f, r.h))))
            run_command(s, "layout.select:" + Json{{"stack", stack.id}, {"child", ch.id}}.dump());
        ImGui::PopID();
        x += w;
    }
}

void draw_layout_block(App::State& s, const LayoutSpec& l, const lay::Node& n, const lay::Placed& p,
                       bool connected) {
    const lay::Rect& r = p.inner;
    if (r.w <= 0 || r.h <= 0)
        return;
    const ImVec2 pos(r.x, r.y);
    const ImVec2 size(r.w, r.h);
    const bool rail = r.w < px(160);  // workspace-chrome: the narrow sidebar
    switch (n.content.type) {
        case lay::Content::Type::kTab: {
            if (l.style == "mac" && s.ws.active_tab() == nullptr)
                return;  // the desktop says how to start instead (draw_mac_backdrop)
            // The details pane sits beside the panes when it is on (not in styled layouts).
            const bool details = l.style.empty() && l.details.value_or(s.settings.details);
            const float gap = px(8);
            const float details_w = details ? std::min(px(320), (r.w + 2 * gap) * 0.3f) : 0.0f;
            const ImVec2 panes(size.x - (details_w > 0 ? details_w + gap : 0.0f), size.y);
            draw_panes(s, pos, panes, connected, n.content.pane ? &*n.content.pane : nullptr);
            if (details_w > 0)
                draw_details(s, ImVec2(pos.x + panes.x + gap, pos.y), ImVec2(details_w, size.y), connected);
            return;
        }
        case lay::Content::Type::kMod:
            break;
        case lay::Content::Type::kTerminal:
        case lay::Content::Type::kAgent:
            return;  // terminals live in tabs here (not as layout blocks)
    }
    const std::string& m = n.content.value;
    const Json& props = n.content.props;
    if (m == "workspace-chrome") {
        if (props.is_object() && props.value("footer", false))
            draw_sidebar_footer(s, pos, size.x, rail);
        else
            draw_brand(s, pos, size.x, rail);
    } else if (m == "spaces") {
        refresh_git_facts(s);
        draw_spaces(s, pos, size.x, size.y, rail);
    } else if (m == "agents") {
        draw_agents(s, pos, size.x, size.y, rail);
    } else if (m == "tabs") {
        draw_tab_bar(s, pos, size.x, size.y);
    } else if (m == "terminals-bar") {
        draw_terminals_bar(s, pos, size.x, size.y);
    } else if (m == "workspace-status") {
        draw_status_bar(s, pos, size.x, size.y);
    } else if (m == "files" || m == "changes") {
        draw_side_panel(s, pos, size);
    } else if (draw_style_block(s, n, pos, size)) {
        // a Classic block
    } else if (m == "mac-menubar") {
        draw_mac_menubar(s, pos, size.x, size.y);
    } else if (m == "mac-window-title") {
        draw_mac_window_title(s, pos, size.x, size.y);
    } else if (m == "mac-dock") {
        draw_mac_dock(s, pos, size.x, size.y);
    } else if (m == "focus-strip") {
        draw_focus_strip(s, pos, size.x, size.y);
    } else if (m == "mission-bar") {
        draw_mission_bar(s, pos, size.x, size.y);
    } else if (m == "mission-wall") {
        draw_mission_wall(s, pos, size);
    } else if (m == "inbox-bar") {
        draw_inbox_bar(s, pos, size.x, size.y);
    } else if (m == "inbox-queue") {
        draw_inbox_queue(s, pos, size);
    } else if (draw_review_block(s, m, pos, size)) {
        // review-head, review-changes, review-commit (review_layout.cpp)
    } else if (draw_timeline_block(s, m, pos, size)) {
        // timeline (timeline.cpp)
    } else if (s.mods != nullptr) {
        // A C++ DLL mod: its panels, in the block.
        ImGui::SetCursorScreenPos(pos);
        ImGui::BeginChild(("##modblock-" + n.id).c_str(), size, ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoBackground);
        draw_mod_panels(s, "sidebar", m);
        draw_mod_panels(s, "details", m);
        ImGui::EndChild();
    }
}

// Backgrounds and borders, parents first: a colour, or a two-colour gradient drawn top to bottom
// (or left to right for angles near 90 / 270). `tree` is the one placed this frame.
void draw_layout_backgrounds(App::State& s, const LayoutSpec& l, const lay::Node& tree) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (const auto& id : s.placed.order) {
        const lay::Node* n = tree.find(id);
        const lay::Placed& p = s.placed.nodes.at(id);
        const ImVec2 a(p.outer.x, p.outer.y);
        const ImVec2 b(p.outer.right(), p.outer.bottom());
        const float radius = n->radius ? px(*n->radius) : 0.0f;
        if (l.look_applies && n->background.is_string()) {
            if (const auto c = layout_color(n->background.get<std::string>()))
                dl->AddRectFilled(a, b, *c, radius);
        } else if (const auto g = l.look_applies ? safe::gradient(n->background) : std::nullopt) {
            // A background from layouts\.state is checked here (the file loader checks its own).
            const auto c0 = layout_color(g->from);
            const auto c1 = layout_color(g->to);
            if (c0 && c1) {
                const float angle = g->angle;
                const bool across =
                    std::fmod(std::abs(angle), 180.0f) > 45.0f && std::fmod(std::abs(angle), 180.0f) < 135.0f;
                if (across)
                    dl->AddRectFilledMultiColor(a, b, *c0, *c1, *c1, *c0);
                else
                    dl->AddRectFilledMultiColor(a, b, *c0, *c0, *c1, *c1);
            }
        }
        if (const lay::Line* line = lay::node_border(*n, false))
            if (const auto c = layout_color(line->color))
                dl->AddRect(a, b, *c, radius, 0, px(line->width));
    }
}

// Separators: the divider line down the middle of each, and a drag handle where the container
// is resizable (the new sizes are saved when the drag ends).
void draw_layout_separators(App::State& s, LayoutSpec& l) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (const auto& sep : s.placed.separators) {
        const lay::Node* c = l.tree.find(sep.container);
        const bool row = sep.axis == lay::Axis::kRow;
        if (c != nullptr && c->divider) {
            if (const auto col = layout_color(c->divider->color)) {
                const float lw = px(c->divider->width);
                if (row) {
                    const float x = sep.box.x + (sep.box.w - lw) * 0.5f;
                    dl->AddLine(ImVec2(x, sep.box.y), ImVec2(x, sep.box.bottom()), *col, lw);
                } else {
                    const float y = sep.box.y + (sep.box.h - lw) * 0.5f;
                    dl->AddLine(ImVec2(sep.box.x, y), ImVec2(sep.box.right(), y), *col, lw);
                }
            }
        }
        if (!sep.resizable || sep.hit.w <= 0 || sep.hit.h <= 0)
            continue;
        ImGui::SetCursorScreenPos(ImVec2(sep.hit.x, sep.hit.y));
        ImGui::PushID(("##sep-" + sep.container + "/" + std::to_string(sep.index)).c_str());
        ImGui::InvisibleButton("##h", ImVec2(sep.hit.w, sep.hit.h));
        const bool active = ImGui::IsItemActive();
        if (ImGui::IsItemHovered() || active)
            ImGui::SetMouseCursor(row ? ImGuiMouseCursor_ResizeEW : ImGuiMouseCursor_ResizeNS);
        if (active) {
            const ImVec2 m = ImGui::GetIO().MousePos;
            lay::drag(l.tree, sep.container, sep.index, sep.axis, s.placed, row ? m.x : m.y, g_scale);
        }
        if (ImGui::IsItemDeactivated())
            save_layout_state(l);
        ImGui::PopID();
    }
}

void draw_layout(App::State& s, ImVec2 origin, ImVec2 full, bool connected) {
    // Focus mode puts its own tree in place of the shown layout's (which keeps its theme).
    LayoutSpec& l = s.settings.focus_on ? focus_layout(s) : s.layout();
    sync_panel(s, l);
    // The Mac layout places an adjusted copy while its window fills the desktop or has no tab;
    // clicks still go to the layout's own tree (the ids are the same).
    // The other styles may adjust the tree the same way (Terminals' gaps and corners).
    const std::optional<lay::Node> mac =
        l.style == "mac" ? mac_layout_tree(s, l.tree) : style_layout_tree(s, l.tree);
    const lay::Node& tree = mac ? *mac : l.tree;
    s.placed = lay::place(tree, lay::Rect{origin.x, origin.y, full.x, full.y}, g_scale, {}, true);
    if (l.style == "mac")
        draw_mac_backdrop(s, tree);
    else
        draw_style_backdrop(s, tree);
    draw_layout_backgrounds(s, l, tree);
    for (const auto& id : s.placed.order) {  // the blocks, in tree order
        const lay::Node* n = tree.find(id);
        if (n->is_block())
            draw_layout_block(s, l, *n, s.placed.nodes.at(id), connected);
    }
    for (const auto& [id, r] : s.placed.headers) {
        const lay::Node* n = tree.find(id);
        if (n != nullptr && n->header && n->header->content.is_string())
            draw_stack_tabs(s, *n, r);
    }
    draw_layout_separators(s, l);
    draw_style_overlay(s);
}

// ------------------------------------------------------------------ --input automation
// The script is ";"-separated steps run one per turn after the --run commands, once sessions
// exist: wait:MS, move:X,Y, click / rclick / mclick / dblclick:X,Y, press:X,Y and release:X,Y
// (a drag, with move steps between), key:NAME, type:TEXT, wheel:N, and shot:NAME (asks the screenshot
// tool for a picture and waits until it has taken it, up to 10 s).

ImGuiKey input_key(const std::string& n) {
    static const std::pair<const char*, ImGuiKey> kKeys[] = {
        {"enter", ImGuiKey_Enter},      {"esc", ImGuiKey_Escape},     {"tab", ImGuiKey_Tab},
        {"up", ImGuiKey_UpArrow},       {"down", ImGuiKey_DownArrow}, {"left", ImGuiKey_LeftArrow},
        {"right", ImGuiKey_RightArrow}, {"home", ImGuiKey_Home},      {"end", ImGuiKey_End},
        {"pgup", ImGuiKey_PageUp},      {"pgdn", ImGuiKey_PageDown},  {"back", ImGuiKey_Backspace},
        {"del", ImGuiKey_Delete},       {"space", ImGuiKey_Space},    {"period", ImGuiKey_Period},
        {".", ImGuiKey_Period}};
    for (const auto& [name, key] : kKeys)
        if (n == name)
            return key;
    if (n.size() == 1 && n[0] >= 'a' && n[0] <= 'z')
        return static_cast<ImGuiKey>(ImGuiKey_A + (n[0] - 'a'));
    if (n.size() == 1 && n[0] >= '0' && n[0] <= '9')
        return static_cast<ImGuiKey>(ImGuiKey_0 + (n[0] - '0'));
    if (n.size() >= 2 && n.size() <= 3 && n[0] == 'f') {  // f1..f12
        const int f = std::atoi(n.c_str() + 1);
        if (f >= 1 && f <= 12)
            return static_cast<ImGuiKey>(ImGuiKey_F1 + (f - 1));
    }
    return ImGuiKey_None;
}

std::vector<std::string> input_steps(const std::string& script) {
    std::vector<std::string> steps;
    for (std::size_t p = 0; p <= script.size();) {
        const auto q = script.find(';', p);
        steps.push_back(script.substr(p, q == std::string::npos ? std::string::npos : q - p));
        if (q == std::string::npos)
            break;
        p = q + 1;
    }
    return steps;
}

// Handles what must happen before the next step: waiting for a shot to be taken, the first
// frames settling (1.5 s), and releasing a click's button (pressing again for a double click).
// Returns true when the next step must wait.
bool input_busy(App::State& s, Clock::time_point now) {
    if (s.input_shot_wait) {
        std::error_code ec;
        if (std::filesystem::exists(s.config.shot_signal, ec) &&
            now - s.input_shot_since < std::chrono::seconds(10))
            return true;
        s.input_shot_wait = false;
        s.input_at = now + std::chrono::milliseconds(100);
    }
    if (s.input_at == Clock::time_point{})
        s.input_at = now + std::chrono::milliseconds(1500);
    if (s.input_up >= 0 && now >= s.input_at) {
        ImGui::GetIO().AddMouseButtonEvent(s.input_up, false);
        const int b = s.input_up;
        s.input_up = -1;
        s.input_at = now + std::chrono::milliseconds(80);
        if (s.input_clicks_left > 0) {
            --s.input_clicks_left;
            ImGui::GetIO().AddMouseButtonEvent(b, true);
            s.input_up = b;
        }
        return true;
    }
    return false;
}

// Runs one "op:arg" step; the next one follows 300 ms later unless the step says otherwise.
void run_input_step(App::State& s, const std::string& step, Clock::time_point now) {
    const auto colon = step.find(':');
    const std::string op = step.substr(0, colon);
    const std::string arg = colon == std::string::npos ? std::string() : step.substr(colon + 1);
    ImGuiIO& io = ImGui::GetIO();
    s.input_at = now + std::chrono::milliseconds(300);
    auto xy = [&] {
        const auto c = arg.find(',');
        return ImVec2(static_cast<float>(std::atof(arg.substr(0, c).c_str())),
                      static_cast<float>(std::atof(arg.substr(c + 1).c_str())));
    };
    if (op == "wait") {
        s.input_at = now + std::chrono::milliseconds(std::atoi(arg.c_str()));
    } else if (op == "move" || op == "click" || op == "rclick" || op == "mclick" || op == "dblclick") {
        const ImVec2 p = xy();
        s.input_mouse = p;
        io.AddMousePosEvent(p.x, p.y);
        const int button = op == "rclick" ? 1 : op == "mclick" ? 2 : 0;
        if (op != "move") {
            // Press now, release a few frames later (and press again for a double click).
            s.input_at = now + std::chrono::milliseconds(120);
            io.AddMouseButtonEvent(button, true);
            s.input_up = button;
            s.input_clicks_left = op == "dblclick" ? 1 : 0;
        }
    } else if (op == "press" || op == "release") {
        const ImVec2 p = xy();
        s.input_mouse = p;
        io.AddMousePosEvent(p.x, p.y);
        io.AddMouseButtonEvent(0, op == "press");
    } else if (op == "key") {
        // "ctrl+shift+m", "ctrl+shift+.", "shift+f8": modifiers held around the key.
        std::string name = arg;
        std::vector<ImGuiKey> mods;
        for (const auto& [prefix, mod] : {std::pair<const char*, ImGuiKey>{"ctrl+", ImGuiMod_Ctrl},
                                          {"shift+", ImGuiMod_Shift},
                                          {"alt+", ImGuiMod_Alt}}) {
            if (const auto at = name.find(prefix); at != std::string::npos) {
                name.erase(at, std::strlen(prefix));
                mods.push_back(mod);
            }
        }
        if (const ImGuiKey k = input_key(name); k != ImGuiKey_None) {
            for (const ImGuiKey m : mods)
                io.AddKeyEvent(m, true);
            io.AddKeyEvent(k, true);
            io.AddKeyEvent(k, false);
            for (const ImGuiKey m : mods)
                io.AddKeyEvent(m, false);
        }
    } else if (op == "type") {
        io.AddInputCharactersUTF8(arg.c_str());
    } else if (op == "wheel") {  // wheel:N, notches (negative scrolls down) where the mouse is
        io.AddMouseWheelEvent(0, static_cast<float>(std::atof(arg.c_str())));
    } else if (op == "shot" && !s.config.shot_signal.empty()) {
        std::ofstream(s.config.shot_signal, std::ios::binary) << arg;
        s.input_shot_wait = true;
        s.input_shot_since = now;
    }
}

void run_input_script(App::State& s) {
    if (s.config.input_script.empty() || s.next_dev_command < s.config.run_commands.size() ||
        s.sessions.empty() || !s.doc_loaded)
        return;
    const std::vector<std::string> steps = input_steps(s.config.input_script);
    const auto now = Clock::now();
    if (s.config.wake)
        s.config.wake();  // keep frames coming while the script runs
    if (input_busy(s, now))
        return;
    if (now < s.input_at || s.next_input >= steps.size())
        return;
    run_input_step(s, steps[s.next_input++], now);
}

// ------------------------------------------------------------------ per-frame upkeep
// keys.json and programs.json are read again when they change (checked every 400 ms), with a
// toast saying whether the change applied.
void reload_changed_files(App::State& s) {
    if (Clock::now() - s.keys_checked <= std::chrono::milliseconds(400))
        return;
    s.keys_checked = Clock::now();
    std::error_code ec;
    const auto t = std::filesystem::last_write_time(data_dir() / L"keys.json", ec);
    if (!ec && t != s.keys_mtime) {
        load_keys(s);
        if (s.keymap_errors.empty())
            s.toast("keys.json applied", "", "", g_pal->info, 2);
        else
            s.toast("keys.json not applied", s.keymap_errors.front(), "", g_pal->needs_you, 8);
    }
    const auto pt = std::filesystem::last_write_time(data_dir() / L"programs.json", ec);
    if (ec)
        ec.clear();
    if (pt != g_programs_mtime)
        if (const std::string problem = load_programs(); !problem.empty())
            s.toast("programs.json not applied", problem, "", g_pal->needs_you, 8);
}

// --start and --run: the --run commands one at a time (800 ms before the first, 700 ms apart)
// once the document is loaded and a session exists.
void run_dev_commands(App::State& s) {
    if (!s.started_dev && !s.config.start_profile.empty())
        return;
    if (s.next_dev_command < s.config.run_commands.size() && s.doc_loaded && !s.launch_busy &&
        Clock::now() >= s.next_dev_at && !s.sessions.empty()) {
        if (s.next_dev_at == Clock::time_point{}) {
            s.next_dev_at = Clock::now() + std::chrono::milliseconds(800);
        } else {
            run_command(s, s.config.run_commands[s.next_dev_command++], false);
            s.next_dev_at = Clock::now() + std::chrono::milliseconds(700);
        }
    }
}

// Global shortcuts while no terminal holds the keyboard (a focused terminal reports them) and
// no popup is open. The keymap resolves a chord in the layout's section ("layout:<name>")
// first, then the global one.
void run_global_shortcuts(App::State& s) {
    if (ImGui::GetIO().WantTextInput || ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId))
        return;
    std::set<ImGuiKeyChord> chords;
    for (const auto& [chord, cmd] : s.keymap.global_map())
        chords.insert(chord);
    for (const auto& [chord, cmd] : s.keymap.layout_map())
        chords.insert(chord);
    // Run at the start of the next frame: a command that brings up a terminal (mission.toggle back
    // to a layout with a focused pane) must not let that terminal see the same key press again.
    for (const ImGuiKeyChord chord : chords) {
        if (ImGui::IsKeyChordPressed(chord))
            if (const auto cmd = s.keymap.global(chord))
                s.chord_commands.push_back(*cmd);
    }
}

// Dialogs and overlays, in the order they stack (later ones on top).
void draw_dialogs(App::State& s) {
    draw_peek(s);
    draw_timeline_overlay(s);
    draw_launcher(s);
    draw_rename(s);
    draw_worktree(s);
    draw_settings(s);
    draw_theme_editor(s);
    draw_exit(s);
    draw_welcome(s);
    draw_machine_add(s);
    draw_confirm(s);
    draw_notes(s);
    draw_worktree_open(s);
    draw_close_notice(s);
    draw_layout_tips(s);
    draw_palette(s);
    draw_picker(s);
    draw_help(s);
    draw_toasts(s);
}

// The window title follows the space shown.
void update_window_title(App::State& s) {
    static std::string last;
    std::string title = "AgentDesk";
    if (s.ws.active() != nullptr)
        title = s.ws.space_title(s.ws.active_space, s.session_cwds()) + " - AgentDesk";
    if (title != last) {
        last = title;
        ::SetWindowTextW(s.config.hwnd, win::to_wide(title).c_str());
    }
}

// The sessions of this computer and of every connected machine, by id.
void collect_sessions(App::State& s) {
    s.sessions = s.host->sessions();
    s.machine_of.clear();
    for (const auto& sess : s.sessions)
        s.machine_of[sess->id()] = "";
    for (auto& r : s.remotes) {
        for (const auto& sess : r.host->sessions()) {
            s.sessions.push_back(sess);
            s.machine_of[sess->id()] = r.cfg.name;
        }
    }
    s.views_by_id.clear();
    s.by_id.clear();
    for (const auto& sess : s.sessions) {
        s.views_by_id[sess->id()] = sess->view();
        s.by_id[sess->id()] = sess;
    }
}

// Keeps the update and telemetry state in step with agent-host's connection.
void follow_core(App::State& s, bool connected) {
    // On connect and every reconnect: the version agent-host reports.
    if (connected && s.host->host_instance_id() != s.noted_host) {
        s.noted_host = s.host->host_instance_id();
        const std::string v = s.host->host_version();
        ui::updates::set_core(v.empty() ? std::nullopt : std::optional<std::string>(v), std::nullopt);
    }
    if (s.was_connected && !connected)
        ui::telemetry::count("core_lost");
    s.was_connected = connected;
    // A dismissed agent-host update error stays dismissed until the error changes.
    reconcile_core_update_error(s.core_update_error_dismissed, ui::updates::core());
    // After "Check now": keep the time fresh while it checks, then "Up to date" shows for 3 s.
    if (s.updates_asked) {
        if (ui::updates::status().checking)
            s.updates_asked = Clock::now();
        else if (Clock::now() - *s.updates_asked >= std::chrono::milliseconds(3000))
            s.updates_asked.reset();
    }
}

}  // namespace

}  // namespace detail

void App::before_new_frame() {
    // The Win32 backend reads the real cursor while the window has focus; a running --input
    // script wins over it.
    if (s_->input_mouse && !s_->config.input_script.empty())
        ImGui::GetIO().AddMousePosEvent(s_->input_mouse->x, s_->input_mouse->y);
}

void App::tick() {
    State& s = *s_;
    const bool connected = s.host->connected();
    follow_core(s, connected);
    if (auto doc = s.host->take_ui_state()) {
        if (!s.doc_loaded || doc->dump() != s.last_sent_doc)
            adopt_doc(s, *doc);
        s.doc_loaded = true;
    }
    collect_sessions(s);
    if (s.doc_loaded || !connected)
        sync_sessions(s);
    if (!s.started_dev && !s.config.start_profile.empty() && connected && s.doc_loaded) {
        s.started_dev = true;
        Placement p;
        p.target = LaunchTarget::kTab;
        create_session(s, s.config.start_profile, s.launch_cwd, "", p);
    }
    update_attention(s);
    style_tick(s);
    // What is open, for usage data: every 10 s.
    if (Clock::now() - s.observed_at >= std::chrono::seconds(10)) {
        s.observed_at = Clock::now();
        observe(s);
    }
    run_mods(s);
    persist(s);
}

void App::frame() {
    State& s = *s_;
    for (const auto& cmd : std::exchange(s.chord_commands, {}))
        run_command(s, cmd);
    g_scale = s.dpi * ui_scale(s.settings);
    s.fonts.mono_size = s.layout().font_size.value_or(s.settings.font_size) / ui_text_factor(s.settings);
    reload_changed_files(s);
    if (!g_settings_problem.empty()) {
        s.toast(g_settings_problem, "", "", g_pal->needs_you, 8);
        g_settings_problem.clear();
    }
    s.keymap.set_layout(s.settings.layout);  // keys.json "layout:<name>" section
    update_palette(s);  // theme, appearance schedule, editor preview, tint (theme_commands.cpp)
    const bool connected = s.host->connected();
    for (const auto& c : std::exchange(s.pending_mod_commands, {}))
        run_command(s, c, false);
    run_input_script(s);
    run_dev_commands(s);
    run_global_shortcuts(s);

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
    ImGui::Begin("##root", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNav |
                     ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar(3);
    const ImVec2 origin = vp->WorkPos;
    const ImVec2 full = vp->WorkSize;
    ImGui::GetWindowDrawList()->AddRectFilled(origin, ImVec2(origin.x + full.x, origin.y + full.y),
                                              g_pal->bg);
    // Commands raised while the layout draws wait for the end of it (State::deferred_commands).
    traffic_lights_input(s, origin);
    s.defer_commands = true;
    s.drawn_sessions.clear();
    draw_layout(s, origin, full, connected);
    s.visible_sessions = s.drawn_sessions;  // what update_attention counts as seen
    run_deferred_commands(s);
    traffic_lights_paint(s);
    draw_focus_glow(s);
    ImGui::End();
    draw_dialogs(s);
    // The dialog on top is raised like the other floating surfaces (drawn outside its edges only).
    if (ui::adapter::WindowBox box; ui::adapter::topmost_modal(box))
        tok::window_shadow(box.draw_list, box.min, box.max, box.rounding, 2);
    update_window_title(s);
    tok::end_frame();
    persist(s);
    if (!s.window_opened) {  // the window is up: counted once for usage data
        s.window_opened = true;
        ui::telemetry::window_open();
    }
}
}  // namespace agentdesk::desktop
