// AgentDesk 98 desktop: its icons, the Office Assistant, the overlay windows and the window as
// a whole.
#include "win98_internal.hpp"

namespace agentdesk::desktop {
namespace detail::w98 {

namespace {
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

}  // namespace

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

namespace {
// ---------------------------------------------------------------- the Office Assistant (clippy.js)

const char* const kTips[] = {
    "It looks like you're herding agents.\n\nDid you know? Ctrl+Esc opens the Start menu.",
    "Click a tab's taskbar button while it's\nshown to minimize it (AgentDesk settles it).\nClick again to bring "
    "it back.",
    "Ctrl+Shift+Esc opens Close Program:\nevery terminal AgentDesk runs, and End Task.",
    "Right-click a folder on the desktop to\nrename or close that space.",
    "Waiting on an agent? Mines is on\nthe desktop. I won't tell anyone.",
    "Double-click a window's title bar to\nmaximize it. Double-click again to restore.",
    "Start > Run... opens any command line\nin a new window, like it's 1998.",
    "Shut Down > Restart in DOS mode\ntakes you to the Terminals layout.",
    "Log Off takes you back to AgentDesk's\nnormal layout. I'll miss you.",
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
                        {id, status, program_of(it->second).name,
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

// owner: the node the layout floats it over; f: its floating block there (layout.rs 61-77).
void clippy(App::State& s, Box owner, ui::layout::Float& f) {
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
    // win98.json 70-80: where the layout's floating block says (the left edge of the screen row, at
    // its end, unless dragged: a click kept in layouts/.state); dragged elsewhere it snaps to the
    // nearest edge and third (layout.rs 732-751), 12 px in (floats.rs 14, 78-96).
    const float m = px(ui::layout::kFloatMargin);
    if (const Json& pj = shared(s, "win98.float.clippy"); pj.is_object()) {
        // Where it was dropped before the layout kept it: now the float's click.
        const auto old = k98::parse_place(safe::string_or(pj, "edge"), safe::string_or(pj, "align"));
        f.edge = static_cast<ui::layout::Edge>(old.edge);
        f.align = static_cast<ui::layout::Align>(old.align);
        save_layout_state(s.layout());
        set_shared(s, "win98.float.clippy", nullptr);
    }
    const k98::FloatPlace place{static_cast<k98::Edge>(f.edge), static_cast<k98::Align>(f.align)};
    const bool right = place.edge != k98::Edge::kLeft;  // clippy.js 469
    const auto home = k98::float_home(owner.x0, owner.y0, owner.w(), owner.h(), cw, ch, place, m);
    ImVec2 at(snap(home[0]), snap(home[1]));
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    if (w.clippy.moving) {
        at = ImVec2(snap(mouse.x - cw * w.clippy.grab.x), snap(mouse.y - ch * w.clippy.grab.y));
    } else if (w.clippy.spring_from) {
        // floats.rs 212-215: left = 1 - spring(elapsed / 360 ms), the body offset by from * left.
        const float ms = std::chrono::duration<float, std::milli>(Clock::now() - w.clippy.spring_start).count();
        const float left = 1.0f - k98::spring(ms / k98::kSnapMs);
        if (ms >= k98::kSnapMs) {
            w.clippy.spring_from.reset();
        } else {
            at = ImVec2(snap(at.x + w.clippy.spring_from->x * left), snap(at.y + w.clippy.spring_from->y * left));
            if (s.config.wake)
                s.config.wake();  // floats.rs 326: frames while anything moves
        }
    }
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
            // floats.rs 261-262: the larger of the two distances, from 4 px.
            if (!w.clippy.moving && std::max(std::abs(mouse.x - w.clippy.down.x),
                                             std::abs(mouse.y - w.clippy.down.y)) >= k98::kDragStart * s.dpi)
                w.clippy.moving = true;
        } else {
            if (w.clippy.moving) {
                const float cx = mouse.x - cw * w.clippy.grab.x + cw * 0.5f - owner.x0;
                const float cy = mouse.y - ch * w.clippy.grab.y + ch * 0.5f - owner.y0;
                const auto to = k98::snap_float(cx, cy, owner.w(), owner.h());
                f.edge = static_cast<ui::layout::Edge>(to.edge);
                f.align = static_cast<ui::layout::Align>(to.align);
                save_layout_state(s.layout());  // floats.rs 288-297 + layout_sync.rs FloatState
                // floats.rs 288-297: spring from where it was let go to its new home.
                const auto next = k98::float_home(owner.x0, owner.y0, owner.w(), owner.h(), cw, ch, to, m);
                w.clippy.spring_from = ImVec2(mouse.x - cw * w.clippy.grab.x - next[0],
                                              mouse.y - ch * w.clippy.grab.y - next[1]);
                w.clippy.spring_start = Clock::now();
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

}  // namespace

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
// Clippy floats over it) and the taskbar (28 px). The layout engine places them (draw_layout);
// this is what the taskbar does each time it shows, before its blocks draw.
void begin(App::State& s) {
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
    w.wakeup_ms = 250;
}

// kit.js 441-479 fitLayout: the bars in Win98 pixels (n * scale / zoom layout px, which the zoom
// makes n * scale screen px), whole on screen as before. Clicks, not edits (layout_sync.rs 324).
void fit(App::State& s, ui::layout::Node& root) {
    const float zoom = g_scale;
    auto want = [&](float n) { return snap(U(s, n)) / zoom; };
    if (auto* d = root.find("desktop"))
        d->size = ui::layout::Span::pixels(want(k98::kDesktopPx));
    if (auto* t = root.find("taskbar"))
        t->size = ui::layout::Span::pixels(want(k98::kTaskbarPx));
}

void overlays(App::State& s) {
    W98& w = st(s);
    // The floating blocks of the shown layout that are Clippy, over the node they float on.
    LayoutSpec& l = s.layout();
    for (const ui::layout::Node* n : l.tree.walk()) {
        const auto* placed = s.placed.at(n->id);
        if (placed == nullptr)
            continue;
        for (const auto& f : n->floating)
            if (!f.hidden && f.content.type == ui::layout::Content::Type::kMod && f.content.value == "win98" &&
                f.content.props.is_object() && f.content.props.value("role", "") == "clippy") {
                const auto& r = placed->padded;
                clippy(s, Box{r.x, r.y, r.right(), r.bottom()}, *l.tree.find_float(f.id));
            }
    }
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
}  // namespace detail::w98

}  // namespace agentdesk::desktop
