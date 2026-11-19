// AgentDesk 98 desktop: its icons, the Office Assistant, the overlay windows and the window as
// a whole.
// Portions adapted from herdr-app (Apache-2.0) and modified; see THIRD_PARTY_NOTICES.md.
#include "win98_internal.hpp"

namespace agentdesk::desktop {
namespace detail::w98 {

namespace {
// ---------------------------------------------------------------- the desktop

struct DesktopItem {
    std::string key;
    std::string icon;
    std::string text;
    bool selected = false;
    std::string badge;
    std::function<void()> open;
    std::function<MenuItems()> menu;
};

// A space's Properties: where it runs, its folder, its branch, its tabs and its status.
void space_properties(App::State& s, int index, const std::string& name) {
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
    open_dialog(s, message(name + " Properties",
                           "Type:  Space\nLocation:  " +
                               (spc.machine.empty() ? std::string("This computer") : spc.machine) +
                               "\nFolder:  " + (folder.empty() ? std::string("~") : display_path(folder)) +
                               "\nBranch:  " + branch + "\nContains:  " + std::to_string(spc.tabs.size()) +
                               " tab(s)\nStatus:  " + (status.empty() ? std::string("idle") : status),
                           "folder"));
}

// A space folder's right-click menu.
MenuItems space_menu(App::State& s, const std::string& id, const std::string& name, int index) {
    return {
        {.label = "&Open", .act = [&s, id] { select_space_id(s, id); }, .bold = true},
        {.label = "New &Tab...",
         .act =
             [&s, name, index] {
                 open_dialog(s, prompt("New Tab", "Type a name for the new tab in '" + name + "':", "dos",
                                       [&s, index](const std::string& n, const std::string&) {
                                           new_tab(s, n, index);
                                       }));
             }},
        {.sep = true},
        {.label = "Re&name",
         .act =
             [&s, id, index] {
                 const std::string cur = index < static_cast<int>(s.ws.spaces.size())
                                             ? s.ws.spaces[static_cast<std::size_t>(index)].name
                                             : std::string();
                 open_dialog(s, rename_space_prompt(s, id, cur));
             }},
        {.label = "&Delete",
         .act =
             [&s, index] {
                 if (index < static_cast<int>(s.ws.spaces.size()))
                     open_dialog(s, close_space_ask(s, index));
             }},
        {.sep = true},
        {.label = "P&roperties", .act = [&s, index, name] { space_properties(s, index, name); }},
    };
}

// The desktop's icons, top to bottom: My Computer, Recycle Bin, a folder per space, the games and
// Network Neighborhood.
std::vector<DesktopItem> desktop_items(App::State& s) {
    const auto cwds = s.session_cwds();
    std::vector<DesktopItem> items;
    items.push_back({"computer", "computer", "My Computer", false, "",
                     [&s] { open_dialog(s, kind_dialog("mycomputer")); }, nullptr});
    items.push_back(
        {"bin", "bin", "Recycle Bin", false, "", [&s] { open_dialog(s, kind_dialog("bin")); }, nullptr});
    for (int i = 0; i < static_cast<int>(s.ws.spaces.size()); ++i) {
        const auto& sp = s.ws.spaces[static_cast<std::size_t>(i)];
        const std::string id = sp.id;
        const std::string name = s.ws.space_title(i, cwds);
        const bool active = i == s.ws.active_space;
        const std::string folder = !sp.machine.empty() ? "netfolder" : active ? "folder-open" : "folder";
        items.push_back({id, folder, name, active, status_badge(space_status(s, sp)),
                         [&s, id] { select_space_id(s, id); },
                         [&s, id, name, i] { return space_menu(s, id, name, i); }});
    }
    items.push_back({"mines", "mines", "Mines", false, "", [&s] { set_app(s, "mines", "open"); }, nullptr});
    items.push_back({"sol", "sol", "Solitaire", false, "", [&s] { set_app(s, "sol", "open"); }, nullptr});
    items.push_back({"net", "network", "Network Neighborhood", false, "",
                     [&s] { open_dialog(s, kind_dialog("network")); }, nullptr});
    return items;
}

// The desktop's own right-click menu.
MenuItems desktop_menu(App::State& s) {
    MenuItems arrange = {{.label = "by &Name", .disabled = true},
                         {.label = "by &Type", .disabled = true},
                         {.label = "&Auto Arrange", .disabled = true, .checked = true}};
    MenuItems neu = {{"&Space...", "folder", "", "", [&s] { open_dialog(s, new_space_prompt(s)); }},
                     {"&Tab...", "dos", "", "", [&s] { open_dialog(s, new_tab_prompt(s)); }}};
    return {{.label = "Arrange &Icons", .sub = arrange},
            {.label = "Line &up Icons", .disabled = true},
            {.sep = true},
            {.label = "&Refresh", .act = [&s] { run_command(s, "layout:win98"); }},
            {.sep = true},
            {.label = "&Paste", .disabled = true},
            {.label = "Paste &Shortcut", .disabled = true},
            {.sep = true},
            {.label = "Ne&w", .sub = neu},
            {.sep = true},
            {.label = "P&roperties", .act = [&s] { open_dialog(s, kind_dialog("display")); }}};
}

// An icon's label lines: at most 74 px wide, less 4 px of padding.
std::vector<std::string> desktop_label_lines(App::State& s, const std::string& text) {
    return wrap(s, text, U(s, 74) - U(s, 4));
}

// Each icon: 76 wide, padding 4 above and 6 below, gap 3 between the icon and its label.
float desktop_icon_height(App::State& s, const DesktopItem& it) {
    const auto ls = desktop_label_lines(s, it.text);
    return U(s, 4) + U(s, 32) + U(s, 3) + U(s, 13) * static_cast<float>(ls.size()) + 2 * one(s) + U(s, 6);
}

// One icon at (x, y): the 32 px icon (a navy veil when selected, the status badge at its lower
// left) and the label (white; navy with a yellow edge when selected).
void draw_desktop_icon(App::State& s, ImDrawList* dl, const DesktopItem& it, float x, float y, bool on) {
    const ImVec2 ip(snap(x + (U(s, 76) - U(s, 32)) * 0.5f), snap(y + U(s, 4)));
    icon(s, dl, it.icon, 32, ip);
    if (on)
        fill(dl, box(ip, ImVec2(U(s, 32), U(s, 32))), IM_COL32(0x00, 0x00, 0x80, 0x60));
    if (!it.badge.empty()) {
        int alpha = 255;
        if (it.badge == "warn-badge")  // blinks over 900 ms
            alpha = static_cast<int>(255.0 * triangle_wave(900.0));
        icon(s, dl, it.badge, 16, ImVec2(ip.x - U(s, 2), ip.y + U(s, 32) + U(s, 2) - U(s, 16)), alpha);
    }
    const auto ls = desktop_label_lines(s, it.text);
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
        plain_label(s, dl, lb.x0 + (lb.w() - text_w(s, l)) * 0.5f, ty, l, rgb(C::kWhite));
        ty += U(s, 13);
    }
}

}  // namespace

// The desktop column: a scrolling list of icons. A click on an icon selects and opens it; a
// right-click opens its menu, or the desktop's own beside the icons.
void desktop(App::State& s, ImDrawList* dl, Box b) {
    W98& w = st(s);
    const auto items = desktop_items(s);
    std::vector<float> heights;
    float total = U(s, 6);
    for (const auto& it : items) {
        heights.push_back(desktop_icon_height(s, it));
        total += heights.back();
    }
    const float max_scroll = std::max(0.0f, total - b.h());
    if (ImGui::IsMouseHoveringRect(b.a(), b.b()) && ImGui::IsWindowHovered())
        w.desktop_scroll -= ImGui::GetIO().MouseWheel * U(s, 40);
    w.desktop_scroll = std::clamp(w.desktop_scroll, 0.0f, max_scroll);
    dl->PushClipRect(b.a(), b.b(), true);
    float y = b.y0 + U(s, 6) - w.desktop_scroll;
    std::function<void()> open;
    std::function<MenuItems()> menu;
    bool icon_hovered = false;
    for (std::size_t i = 0; i < items.size(); ++i) {
        const auto& it = items[i];
        const float x = snap(b.x0 + (b.w() - U(s, 76)) * 0.5f);
        ImGui::PushID(it.key.c_str());
        const Pressed p = area("##dk", box(ImVec2(x, y), ImVec2(U(s, 76), heights[i])));
        ImGui::PopID();
        icon_hovered = icon_hovered || p.hovered;
        if (p.hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            w.desktop_sel = it.key;
        if (p.clicked)
            open = it.open;
        if (p.right) {
            w.desktop_sel = it.key;
            if (it.menu)
                menu = it.menu;
        }
        draw_desktop_icon(s, dl, it, x, y, it.selected || w.desktop_sel == it.key);
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
            open_menu(s, desktop_menu(s), std::nullopt);
        }
    }
    window_drag_area(s, b.a(), b.b());
    if (open)
        open();
    if (menu)
        open_menu(s, menu(), std::nullopt);
}

namespace {
// ---------------------------------------------------------------- the Office Assistant

const char* const kTips[] = {
    "It looks like you're herding agents.\n\nDid you know? Ctrl+Esc opens the Start menu.",
    "Click a tab's taskbar button while it's\nshown to minimize it (AgentDesk settles it).\nClick again to "
    "bring "
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

// What the assistant reports: the agent that needs you (blocked), else the latest that finished,
// while you look elsewhere and did not dismiss it.
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
        const auto ch = s.changed.find(id);
        const long long seq = ch == s.changed.end() ? 0 : ch->second.at;
        list.push_back(
            {seq, {id, status, program_of(it->second).name, "'" + session_place(s, id, cwds) + "'"}});
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

// What the balloon says, and its links.
struct ClippyMessage {
    std::string text;
    std::vector<std::pair<std::string, std::function<void()>>> actions;
};

// News about an agent, else the tip asked for (a click on Clippy), else nothing.
ClippyMessage clippy_message(App::State& s, const std::optional<News>& news) {
    W98& w = st(s);
    ClippyMessage msg;
    if (news) {
        msg.text = news->status == "blocked" ? "It looks like " + news->name + " in\n" + news->where +
                                                   " is waiting for you.\n\nWould you like help?"
                                             : "It looks like " + news->name + " finished\nin " +
                                                   news->where + ".\n\nWant to see what it did?";
        const std::string key = news->id + ":" + news->status;
        const std::string id = news->id;
        msg.actions.push_back({"Take me there", [&s, &w, id, key] {
                                   run_command(s, "agent.focus:" + id);
                                   w.clippy.dismissed.push_back(key);
                               }});
        msg.actions.push_back({"Not now", [&w, key] { w.clippy.dismissed.push_back(key); }});
    } else if (w.clippy.tip) {
        const int n = static_cast<int>(std::size(kTips));
        msg.text = kTips[*w.clippy.tip % n];
        msg.actions.push_back({"Next tip", [&w, n] { w.clippy.tip = (*w.clippy.tip + 1) % n; }});
        msg.actions.push_back({"Close", [&w] { w.clippy.tip.reset(); }});
        msg.actions.push_back({"Hide me", [&s] { set_shared(s, "win98.clippy", "off"); }});
    }
    return msg;
}

// Where the float goes this frame (its top left): its home on the layout's floating block, under
// the pointer while dragged, or springing home after a drop. `size` is its size, m the margin.
ImVec2 clippy_position(App::State& s, Box owner, ui::layout::Float& f, ImVec2 size, float m) {
    W98& w = st(s);
    if (const Json& pj = shared(s, "win98.float.clippy"); pj.is_object()) {
        // Where it was dropped before the layout kept it: now the float's click.
        const auto old = k98::parse_place(safe::string_or(pj, "edge"), safe::string_or(pj, "align"));
        f.edge = static_cast<ui::layout::Edge>(old.edge);
        f.align = static_cast<ui::layout::Align>(old.align);
        save_layout_state(s.layout());
        set_shared(s, "win98.float.clippy", nullptr);
    }
    const k98::FloatPlace place{static_cast<k98::Edge>(f.edge), static_cast<k98::Align>(f.align)};
    const auto home = k98::float_home(owner.x0, owner.y0, owner.w(), owner.h(), size.x, size.y, place, m);
    ImVec2 at(snap(home[0]), snap(home[1]));
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    if (w.clippy.moving) {
        at = ImVec2(snap(mouse.x - size.x * w.clippy.grab.x), snap(mouse.y - size.y * w.clippy.grab.y));
    } else if (w.clippy.spring_from) {
        // Easing home: the offset scales by 1 - spring(elapsed / kSnapMs).
        const float ms =
            std::chrono::duration<float, std::milli>(Clock::now() - w.clippy.spring_start).count();
        const float left = 1.0f - k98::spring(ms / k98::kSnapMs);
        if (ms >= k98::kSnapMs) {
            w.clippy.spring_from.reset();
        } else {
            at = ImVec2(snap(at.x + w.clippy.spring_from->x * left),
                        snap(at.y + w.clippy.spring_from->y * left));
            if (s.config.wake)
                s.config.wake();  // keep drawing frames while it moves
        }
    }
    return at;
}

// The balloon: the text, then each action as a bullet and an underlined link. Returns the action
// clicked, if any.
std::function<void()> clippy_balloon(App::State& s, ImDrawList* dl, Box b, const std::vector<std::string>& ls,
                                     const ClippyMessage& msg) {
    std::function<void()> run;
    dl->AddRectFilled(b.a(), b.b(), rgb(C::kTip), U(s, 8));
    dl->AddRect(b.a(), b.b(), rgb(C::kText), U(s, 8), 0, one(s));
    float ty = b.y0 + one(s) + U(s, 8);
    ty += lines(s, dl, b.x0 + one(s) + U(s, 8), ty, ls, rgb(C::kText)) + U(s, 8);
    for (std::size_t i = 0; i < msg.actions.size(); ++i) {
        const auto& [text, act] = msg.actions[i];
        const float ax = b.x0 + one(s) + U(s, 8);
        const float tw = text_w(s, text);
        ImGui::PushID(static_cast<int>(i));
        const Pressed p = area("##clip", box(ImVec2(ax, ty), ImVec2(U(s, 12) + tw, U(s, 13))));
        ImGui::PopID();
        if (p.hovered)
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        dl->AddCircleFilled(ImVec2(ax + U(s, 3.5f), ty + U(s, 6.5f)), U(s, 3.5f), rgb(C::kNavy), 12);
        const float lx = ax + U(s, 7) + U(s, 5);
        plain_label(s, dl, lx, ty, text, rgb(C::kNavy));
        underline(s, dl, lx, ty, tw, rgb(C::kNavy));
        if (p.clicked)
            run = act;
        ty += U(s, 13) + U(s, 3);
    }
    return run;
}

// The drag after the press: it starts once either axis has moved kDragStart px; dropped, the
// float takes the place its center is nearest to (kept with the layout) and springs there from
// where it was let go.
void clippy_drag(App::State& s, Box owner, ui::layout::Float& f, ImVec2 size, float m) {
    W98& w = st(s);
    if (!w.clippy.pressing)
        return;
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const float cw = size.x, ch = size.y;
    if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        if (!w.clippy.moving && std::max(std::abs(mouse.x - w.clippy.down.x),
                                         std::abs(mouse.y - w.clippy.down.y)) >= k98::kDragStart * s.dpi)
            w.clippy.moving = true;
        return;
    }
    if (w.clippy.moving) {
        const float cx = mouse.x - cw * w.clippy.grab.x + cw * 0.5f - owner.x0;
        const float cy = mouse.y - ch * w.clippy.grab.y + ch * 0.5f - owner.y0;
        const auto to = k98::snap_float(cx, cy, owner.w(), owner.h());
        f.edge = static_cast<ui::layout::Edge>(to.edge);
        f.align = static_cast<ui::layout::Align>(to.align);
        save_layout_state(s.layout());
        const auto next = k98::float_home(owner.x0, owner.y0, owner.w(), owner.h(), cw, ch, to, m);
        w.clippy.spring_from =
            ImVec2(mouse.x - cw * w.clippy.grab.x - next[0], mouse.y - ch * w.clippy.grab.y - next[1]);
        w.clippy.spring_start = Clock::now();
    }
    w.clippy.pressing = w.clippy.moving = false;
}

// The Office Assistant: Clippy (64 x 80) under its balloon (200 wide), padding 4, gap 2.
// owner: the node the layout floats it over; f: its floating block there.
void clippy(App::State& s, Box owner, ui::layout::Float& f) {
    if (shared_str(s, "win98.clippy") == "off")
        return;
    W98& w = st(s);
    const auto news = clippy_news(s);
    const ClippyMessage msg = clippy_message(s, news);
    const bool has_text = !msg.text.empty();
    const float bw = U(s, 200);
    std::vector<std::string> ls;
    float bh = 0;
    if (has_text) {
        const auto n_actions = static_cast<float>(msg.actions.size());
        ls = wrap(s, msg.text, bw - U(s, 16) - 2 * one(s));
        bh = U(s, 8) * 2 + 2 * one(s) + U(s, 13) * static_cast<float>(ls.size()) + U(s, 8) +
             U(s, 13) * n_actions + U(s, 3) * static_cast<float>(msg.actions.size() - 1);
    }
    const float cw = std::max(U(s, 64), has_text ? bw : 0.0f) + U(s, 8);
    const float ch = U(s, 8) + (has_text ? bh + U(s, 2) : 0.0f) + U(s, 80);
    const float m = px(ui::layout::kFloatMargin);
    const ImVec2 at = clippy_position(s, owner, f, ImVec2(cw, ch), m);
    // Away from the left edge, the balloon and Clippy line up on the right.
    const bool right = static_cast<k98::Edge>(f.edge) != k98::Edge::kLeft;
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
    if (has_text) {
        const Box b = box(ImVec2(x + (right ? inner - bw : 0.0f), y), ImVec2(bw, bh));
        run = clippy_balloon(s, dl, b, ls, msg);
        y = b.y1 + U(s, 2);
    }
    // While there is news, Clippy bobs up to 5 px over 800 ms.
    const float bounce = news ? -U(s, 5) * static_cast<float>(triangle_wave(800.0)) : 0.0f;
    const Box cb = box(ImVec2(x + (right ? inner - U(s, 64) : 0.0f), y + bounce), ImVec2(U(s, 64), U(s, 80)));
    const Pressed p = area("##clippy", cb);
    if (p.hovered)
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    image(s, dl, root_dir() / L"img" / L"clippy.svg", cb);
    if (p.clicked) {  // a tip, or away with it
        if (w.clippy.tip)
            w.clippy.tip.reset();
        else
            w.clippy.tip = static_cast<int>(
                std::floor(static_cast<double>(std::rand()) / (RAND_MAX + 1.0) * std::size(kTips)));
        sfx(s, "clippy");
    }
    // A press on it that no control takes starts a drag.
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !g_area_hovered) {
        w.clippy.pressing = true;
        w.clippy.down = mouse;
        w.clippy.grab = ImVec2((mouse.x - at.x) / cw, (mouse.y - at.y) / ch);
    }
    ImGui::End();
    clippy_drag(s, owner, f, ImVec2(cw, ch), m);
    if (run)
        run();
}

// ---------------------------------------------------------------- the overlay windows

// One window per open (or minimized) game.
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

// Drops the windows marked closing, with their games. A closed dialog hands the keyboard back.
void remove_closed_windows(App::State& s) {
    W98& w = st(s);
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
}

// Whether the window draws this frame. A minimized game draws nothing; a window whose game is
// gone is marked closing.
bool window_shows(App::State& s, Window& win) {
    W98& w = st(s);
    if ((win.role == "mines" || win.role == "sol") && app_state(s, win.role) != "open") {
        if (win.role == "sol" && w.sol) {
            w.sol->animating = false;  // a hidden game stops animating
            w.sol_press.reset();
            w.sol_drag.reset();
        }
        return false;
    }
    if ((win.role == "mines" && !w.mines) || ((win.role == "sol" || win.role == "sol-dialog") && !w.sol)) {
        win.closing = true;
        return false;
    }
    return true;
}

// The veil under a modal window: it dims the whole window and swallows clicks beside the dialog.
void draw_dim(const std::string& name) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    begin_overlay(name, ImGuiWindowFlags_NoFocusOnAppearing);
    ImGui::GetWindowDrawList()->AddRectFilled(
        vp->WorkPos, ImVec2(vp->WorkPos.x + vp->WorkSize.x, vp->WorkPos.y + vp->WorkSize.y),
        IM_COL32(0, 0, 0, 89));
    ImGui::SetCursorScreenPos(vp->WorkPos);
    ImGui::InvisibleButton("##dim", vp->WorkSize);
    ImGui::End();
}

// The window's contents by role. Returns its size.
ImVec2 render_window(App::State& s, Window& win, ImDrawList* dl, ImVec2 pos) {
    if (win.role == "dialog")
        return render_dialog(s, win, dl, pos);
    if (win.role == "mines")
        return render_mines(s, win, dl, pos);
    if (win.role == "sol")
        return render_sol(s, win, dl, pos);
    return render_sol_dialog(s, win, dl, pos);
}

// One overlay window in its own ImGui window. A new window is drawn off screen once to measure
// it, then centered. The window pressed last has the keyboard; a press on no control drags it.
// Returns the names of the ImGui windows it used, bottom first.
std::vector<std::string> draw_window(App::State& s, int id) {
    W98& w = st(s);
    std::vector<std::string> order;
    Window* win = find_window(s, id);
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const std::string name = "##w98win" + std::to_string(id);
    if (win->modal) {
        draw_dim(name + "dim");
        order.push_back(name + "dim");
    }
    const bool screen = win->dialog && win->dialog->kind == "screen";
    ImVec2 pos = screen ? vp->WorkPos : win->pos;
    if (!win->placed && !screen)
        pos = ImVec2(vp->WorkPos.x - 10000, vp->WorkPos.y - 10000);
    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(screen ? vp->WorkSize
                                    : ImVec2(std::max(1.0f, win->size.x), std::max(1.0f, win->size.y)));
    if (win->want_focus) {
        ImGui::SetNextWindowFocus();
        win->want_focus = false;
    }
    begin_overlay(name, ImGuiWindowFlags_NoScrollWithMouse);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImGui::PushID(id);
    g_area_hovered = false;
    const ImVec2 size = render_window(s, *win, dl, pos);
    ImGui::PopID();
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
    if (hovered &&
        (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right))) {
        w.focus = id;
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !g_area_hovered && !screen) {
            w.drag_id = id;
            w.drag_grab = ImVec2(mouse.x - win->pos.x, mouse.y - win->pos.y);
        }
    }
    if (w.focus == id && win->dialog && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
        w.menus.empty())
        dialog_keys(s, *win, *win->dialog);
    ImGui::End();
    win = find_window(s, id);
    if (win == nullptr)
        return order;
    win->size = size;
    if (!win->placed && !screen) {
        win->pos = ImVec2(snap(vp->WorkPos.x + (vp->WorkSize.x - size.x) * 0.5f),
                          snap(vp->WorkPos.y + (vp->WorkSize.y - size.y) * 0.5f));
        win->placed = true;
    }
    if (w.drag_id == id) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            win->pos = ImVec2(snap(mouse.x - w.drag_grab.x), snap(mouse.y - w.drag_grab.y));
        } else {
            w.drag_id = 0;
            if (!win->key.empty())
                w.dropped[win->key] = win->pos;
        }
    }
    order.push_back(name);
    return order;
}

void draw_windows(App::State& s) {
    W98& w = st(s);
    remove_closed_windows(s);
    std::vector<int> ids;
    for (const auto& win : w.windows)
        ids.push_back(win->id);
    std::vector<std::string> order;
    for (const int id : ids) {
        Window* win = find_window(s, id);
        if (win == nullptr || win->closing || !window_shows(s, *win))
            continue;
        for (auto& n : draw_window(s, id))
            order.push_back(std::move(n));
    }
    // The window pressed last goes on top.
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

// The layout's preview picture in Settings (win98.svg), fit whole inside the card's box, with
// rounded corners and a 1 px border.
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

// ---------------------------------------------------------------- the layout

// The layout's background: its color (Display Properties > Wallpaper), or the clouds picture
// covering it.
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

// The layout is a row of the desktop (84 px) and the panes (10 px padding; Clippy floats over
// it) above the taskbar (28 px). The layout engine places them (draw_layout); this runs each frame
// the layout shows, before its blocks draw.
void begin(App::State& s) {
    W98& w = st(s);
    ensure_fonts(s);
    if (!w.was_shown) {
        // Newly shown: the Start menu closed, then the boot (forced when the app just started).
        w.was_shown = true;
        if (!shared(s, "win98.start").is_null())
            set_shared(s, "win98.start", nullptr);
        const long long age = app_age_seconds();
        boot(s, age >= 0 && age < 45);
    }
    sync_games(s);
    w.wakeup_ms = 250;
}

// Sizes the desktop and taskbar bars in whole Win98 pixels: n * scale / zoom layout px, which the
// zoom turns back into n * scale screen px. These are runtime sizes, not edits to the saved layout.
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
            if (!f.hidden && f.content.type == ui::layout::Content::Type::kMod &&
                f.content.value == "win98" && f.content.props.is_object() &&
                f.content.props.value("role", "") == "clippy") {
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

// A layout switch closes windows and menus.
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
