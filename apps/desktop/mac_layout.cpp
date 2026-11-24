// The AgentDesk Mac layout: a menu bar, a wallpaper desktop with the active tab in a floating
// window, and a Dock of spaces and tabs. AgentDesk's own design: its bundled fonts, icon glyphs and
// program icons, procedural drawing, and colours taken from the active theme.
#include "overlay_kit.hpp"

namespace agentdesk::desktop {
namespace detail {

namespace {
namespace mac = ui::mac;

constexpr const char* kMenuPopup = "##mac-menu";

// The tab shown in the active space, if any (const: the menus and the tree read it).
const ui::Tab* shown_tab(const App::State& s) {
    if (s.ws.active_space < 0 || s.ws.active_space >= static_cast<int>(s.ws.spaces.size()))
        return nullptr;
    const ui::Space& sp = s.ws.spaces[static_cast<std::size_t>(s.ws.active_space)];
    if (sp.active_tab < 0 || sp.active_tab >= static_cast<int>(sp.tabs.size()))
        return nullptr;
    return &sp.tabs[static_cast<std::size_t>(sp.active_tab)];
}

// The first block naming `mod` in `tree`, and that block's parent.
const lay::Node* block_of(const lay::Node& tree, const char* mod) {
    for (const lay::Node* n : tree.walk())
        if (n->is_block() && n->mod() == mod)
            return n;
    return nullptr;
}

// ------------------------------------------------------------------ colours
// The menu bar: the theme's chrome a little toward the canvas.
ImU32 bar_color() {
    return mix(g_pal->sidebar, g_pal->bg, 0.35f);
}

// The window's title bar: between the card and the chrome.
ImU32 title_color() {
    return mix(g_pal->card, g_pal->sidebar, 0.55f);
}

// The Dock's glass: the chrome, letting a little wallpaper through.
ImU32 dock_color() {
    return alpha(g_pal->light ? mix(g_pal->sidebar, IM_COL32(255, 255, 255, 255), 0.35f) : g_pal->sidebar,
                 214);
}

// ------------------------------------------------------------------ desktop
// Layered translucent discs, the smaller ones on top, so the centre builds up to about `peak`
// (0..1) and the edge fades out.
void glow(ImDrawList* dl, ImVec2 c, float r, ImU32 col, float peak) {
    constexpr int kLayers = 40;
    const int a = std::max(1, static_cast<int>(std::round(peak * 255.0f / kLayers)));
    for (int i = 0; i < kLayers; ++i) {
        const float t = 1.0f - static_cast<float>(i) / kLayers;
        dl->AddCircleFilled(c, r * t, alpha(col, a), 96);
    }
}

// A diagonal gradient from the accent (top left) to the info colour (bottom right), both mixed
// well into the canvas, with two soft glows.
void wallpaper(ImDrawList* dl, ImVec2 a, ImVec2 b) {
    const bool light = g_pal->light;
    const ImU32 c0 = mix(g_pal->bg, g_pal->focus, light ? 0.22f : 0.30f);
    const ImU32 c1 = mix(g_pal->bg, g_pal->info, light ? 0.18f : 0.22f);
    const ImU32 mid = mix(g_pal->bg, mix(c0, c1, 0.5f), 0.75f);
    dl->AddRectFilledMultiColor(a, b, c0, mid, c1, mid);
    const float w = b.x - a.x;
    const float h = b.y - a.y;
    const float big = std::max(w, h);
    dl->PushClipRect(a, b, true);
    glow(dl, ImVec2(a.x + w * 0.24f, a.y + h * 0.28f), big * 0.42f, g_pal->focus, light ? 0.18f : 0.30f);
    glow(dl, ImVec2(a.x + w * 0.84f, a.y + h * 0.86f), big * 0.36f, g_pal->info, light ? 0.16f : 0.26f);
    dl->PopClipRect();
}

// A soft drop shadow: rounded rects growing 2 px at a time, offset down a little.
void window_shadow(ImDrawList* dl, ImVec2 a, ImVec2 b, float radius) {
    const int per = g_pal->light ? 6 : 14;
    for (int i = 1; i <= 9; ++i) {
        const float e = px(2.2f * static_cast<float>(i));
        dl->AddRectFilled(ImVec2(a.x - e, a.y - e + px(8)), ImVec2(b.x + e, b.y + e + px(8)),
                          IM_COL32(0, 0, 0, per), radius + e);
    }
}

// No tab to show: the mark and a line saying how to start, centred in the window's place.
void empty_desktop(App::State& s, ImDrawList* dl, ImVec2 a, ImVec2 b) {
    const char* text = "No tabs \xE2\x80\x94 press + in the Dock";
    const float fs = fpx(14);
    const ImVec2 t = s.fonts.ui_medium->CalcTextSizeA(fs, FLT_MAX, 0, text);
    const float cx = (a.x + b.x) * 0.5f;
    const float cy = (a.y + b.y) * 0.5f;
    const float m = px(44);
    draw_brand_mark(dl, ImVec2(cx - m * 0.5f, cy - m - px(10)), m);
    dl->AddText(s.fonts.ui_medium, fs, ImVec2(cx - t.x * 0.5f, cy + px(6)), g_pal->text, text);
}

// ------------------------------------------------------------------ menu bar
mac::MenuContext menu_context(App::State& s) {
    mac::MenuContext c;
    for (const auto& l : s.layouts)  // fun layouts only when listed (Settings > Look > Fun layouts)
        if (ui::styles::is_listed(l.name, s.settings.styles.fun_layouts, s.layout().name))
            c.layouts.emplace_back(l.name, layout_display_name(l.name));
    c.layout = s.layout().name;
    c.fill = s.mac.fill;
    if (ui::Space* sp = s.ws.active(); sp != nullptr) {
        c.space = s.ws.active_space;
        for (int i = 0; i < static_cast<int>(sp->tabs.size()); ++i) {
            const ui::Tab& t = sp->tabs[static_cast<std::size_t>(i)];
            c.tabs.push_back({tab_label(s, t, i, s.ws.sessions_in(t)), i == sp->active_tab, t.settled});
        }
    }
    return c;
}

void open_menu(App::State& s, int m) {
    s.mac.menu = m;
    s.mac.keyed = false;
    ImGui::OpenPopup(kMenuPopup);
}

// One dropdown row: 24 px, 10 px side padding, a check gutter, the label and the bound keys
// right-aligned. The row under the pointer (or chosen with the arrows) is filled with the accent.
void menu_row_mac(App::State& s, const mac::MenuItem& it, int n, float w) {
    const ImVec2 a = ImGui::GetCursorScreenPos();
    if (it.kind == mac::MenuItem::Kind::kSeparator) {
        ImGui::Dummy(ImVec2(w, px(9)));
        ImGui::GetWindowDrawList()->AddLine(ImVec2(a.x + px(8), a.y + px(4.5f)),
                                            ImVec2(a.x + w - px(8), a.y + px(4.5f)),
                                            mix(g_pal->border, g_pal->text, 0.3f));  // visible on dark menus too
        return;
    }
    if (it.kind == mac::MenuItem::Kind::kHeader) {
        ImGui::Dummy(ImVec2(w, px(22)));
        ImGui::GetWindowDrawList()->AddText(s.fonts.ui_medium, fpx(11), ImVec2(a.x + px(10), a.y + px(6)),
                                            g_pal->faint, it.label.c_str());
        return;
    }
    const float h = px(24);
    ImGui::PushID(n);
    ImGui::BeginDisabled(!it.enabled);
    bool clicked = ImGui::InvisibleButton("##row", ImVec2(w, h));
    ImGui::EndDisabled();
    ImGui::PopID();
    const ImGuiIO& io = ImGui::GetIO();
    const bool hov = it.enabled && ImGui::IsItemHovered();
    bool run = false;
    const bool moved = io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f;
    const bool on = it.enabled && g_menu_nav.row(hov && moved, &run);
    clicked = it.enabled && (clicked || run);
    const bool lit = hov || (on && s.mac.keyed);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (lit)
        dl->AddRectFilled(a, ImVec2(a.x + w, a.y + h), g_pal->focus, px(5));
    const float cy = a.y + h * 0.5f;
    const ImU32 ink = !it.enabled ? g_pal->faint : lit ? accent_foreground() : g_pal->text;
    if (it.checked)
        draw_icon(s, dl, ui::icon::kCheck, fpx(12), ImVec2(a.x + px(10) + px(6), cy), ink);
    dl->AddText(s.fonts.ui, fpx(13), ImVec2(a.x + px(10) + px(18), cy - fpx(13) * 0.6f), ink,
                it.label.c_str());
    if (const std::string chord = s.keymap.chord_for(it.command); !chord.empty()) {
        const float cw = s.fonts.ui->CalcTextSizeA(fpx(12), FLT_MAX, 0, chord.c_str()).x;
        dl->AddText(s.fonts.ui, fpx(12), ImVec2(a.x + w - px(10) - cw, cy - fpx(12) * 0.6f),
                    lit ? alpha(accent_foreground(), 200) : g_pal->faint, chord.c_str());
    }
    if (clicked) {
        ImGui::CloseCurrentPopup();
        s.mac.menu = -1;
        run_command(s, it.command);
    }
}

// The open menu's dropdown under its title: rounded 8, a 1 px border, raised, 5 px inside. Left
// and Right move to the neighbouring menu, as the arrows do in a menu bar.
void draw_menu_dropdown(App::State& s) {
    if (s.mac.menu < 0 || s.mac.menu >= static_cast<int>(s.mac.menu_at.size()))
        return;
    const auto items = mac::menu_items(static_cast<mac::Menu>(s.mac.menu), menu_context(s));
    float lw = 0;
    float cw = 0;
    for (const auto& it : items) {
        if (it.kind != mac::MenuItem::Kind::kItem)
            continue;
        lw = std::max(lw, s.fonts.ui->CalcTextSizeA(fpx(13), FLT_MAX, 0, it.label.c_str()).x);
        const std::string chord = s.keymap.chord_for(it.command);
        if (!chord.empty())
            cw = std::max(cw, s.fonts.ui->CalcTextSizeA(fpx(12), FLT_MAX, 0, chord.c_str()).x);
    }
    const float w = std::max(px(210), px(10) + px(18) + lw + (cw > 0 ? px(32) + cw : px(16)) + px(10));
    // Fades in and settles 4 px into place each time a menu opens; switching menus fades the new one.
    const ImGuiID fade_id = ImGui::GetID("##mac-menu-fade") + static_cast<ImGuiID>(s.mac.menu);
    const float k = tok::appear(fade_id, ui::tokens::kFastMs);
    ImVec2 at = s.mac.menu_at[static_cast<std::size_t>(s.mac.menu)];
    at.y -= (1.0f - k) * px(4);
    ImGui::SetNextWindowPos(at);
    ImGui::SetNextWindowSize(ImVec2(w + px(10), 0));
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, k);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(5), px(5)));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, px(8));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, v4(raised_color()));
    ImGui::PushStyleColor(ImGuiCol_Border, v4(mix(g_pal->border, g_pal->text, 0.12f)));
    int next = -1;
    if (ImGui::BeginPopup(kMenuPopup, ImGuiWindowFlags_NoMove)) {
        menu_keys();
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow) || ImGui::IsKeyPressed(ImGuiKey_DownArrow))
            s.mac.keyed = true;
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow))
            next = (s.mac.menu + mac::kMenuCount - 1) % mac::kMenuCount;
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow))
            next = (s.mac.menu + 1) % mac::kMenuCount;
        for (std::size_t i = 0; i < items.size(); ++i)
            menu_row_mac(s, items[i], static_cast<int>(i), w);
        const ImVec2 wp = ImGui::GetWindowPos();
        const ImVec2 ws = ImGui::GetWindowSize();
        tok::window_shadow(ImGui::GetWindowDrawList(), wp, ImVec2(wp.x + ws.x, wp.y + ws.y), px(8), 1);
        ImGui::EndPopup();
    } else {
        tok::forget(fade_id);
    }
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(5);
    if (next >= 0)
        open_menu(s, next);
}

// The agents item: a dot and the count of agents that need you, a spinner and the count of
// working ones (just a quiet robot when neither). A click goes to the first agent that needs you,
// else opens Go to. Returns its left edge.
float draw_agents_item(App::State& s, ImDrawList* dl, float right, float top, float h) {
    std::vector<Attention> states;
    std::string first_blocked;
    for (const auto& id : s.agent_order)
        if (s.attention_of(id) == Attention::kBlocked && first_blocked.empty())
            first_blocked = id;
    for (const auto& id : s.ws.all_sessions()) {
        states.push_back(s.attention_of(id));
        if (first_blocked.empty() && states.back() == Attention::kBlocked)
            first_blocked = id;
    }
    const mac::AgentCounts c = mac::count_agents(states);
    const float fs = fpx(12.5f);
    const std::string needs = std::to_string(c.needs);
    const std::string working = std::to_string(c.working);
    const float nw = s.fonts.ui_medium->CalcTextSizeA(fs, FLT_MAX, 0, needs.c_str()).x;
    const float ww = s.fonts.ui_medium->CalcTextSizeA(fs, FLT_MAX, 0, working.c_str()).x;
    float inner = 0;
    if (c.needs > 0)
        inner += px(8) + px(5) + nw;
    if (c.working > 0)
        inner += (inner > 0 ? px(10) : 0.0f) + px(13) + px(4) + ww;
    if (inner == 0)
        inner = px(16);
    const float w = inner + px(16);
    const ImVec2 a(right - w, top + px(4));
    const ImVec2 b(right, top + h - px(5));
    ImGui::SetCursorScreenPos(a);
    const bool clicked = ImGui::InvisibleButton("##mac-agents", ImVec2(w, b.y - a.y));
    const bool hov = ImGui::IsItemHovered();
    if (hov)
        dl->AddRectFilled(a, b, g_pal->hover, px(5));
    const float cy = (a.y + b.y) * 0.5f;
    float x = a.x + px(8);
    if (c.needs > 0) {
        dl->AddCircleFilled(ImVec2(x + px(4), cy), px(4), g_pal->needs_you);
        x += px(8) + px(5);
        dl->AddText(s.fonts.ui_medium, fs, ImVec2(x, cy - fs * 0.6f), g_pal->text, needs.c_str());
        x += nw + px(10);
    }
    if (c.working > 0) {
        status_mark(s, dl, x - px(1), cy, Attention::kWorking, true);
        x += px(13) + px(4);
        dl->AddText(s.fonts.ui_medium, fs, ImVec2(x, cy - fs * 0.6f), g_pal->text, working.c_str());
    }
    if (c.needs == 0 && c.working == 0)
        draw_icon(s, dl, ui::icon::kBot, fpx(14), ImVec2(a.x + w * 0.5f, cy), hov ? g_pal->text : g_pal->dim);
    if (hov) {
        char tip[160];
        std::snprintf(
            tip, sizeof tip, "%d need%s you \xC2\xB7 %d working \xE2\x80\x94 %s", c.needs,
            c.needs == 1 ? "s" : "", c.working,
            first_blocked.empty() ? "click to find an agent" : "click to go to the first that needs you");
        ImGui::SetTooltip("%s", tip);
    }
    if (clicked)
        run_command(s, first_blocked.empty() ? std::string("app.picker") : "agent.focus:" + first_blocked);
    return a.x;
}

}  // namespace

// The tree to place: the window fills the desktop (no margins, square corners, the Dock in its own
// 10 px band), or there is no tab (the window card is not drawn).
std::optional<lay::Node> mac_layout_tree(const App::State& s, const lay::Node& tree) {
    const bool empty = shown_tab(s) == nullptr;
    if (!s.mac.fill && !empty)
        return std::nullopt;
    lay::Node t = tree;
    const lay::Node* dock_c = block_of(t, "mac-dock");
    const lay::Node* title_c = block_of(t, "mac-window-title");
    const lay::Node* desk_c = dock_c != nullptr ? t.parent_of(dock_c->id) : nullptr;
    const lay::Node* win_c = title_c != nullptr ? t.parent_of(title_c->id) : nullptr;
    lay::Node* dock = dock_c != nullptr ? t.find(dock_c->id) : nullptr;
    lay::Node* desk = desk_c != nullptr ? t.find(desk_c->id) : nullptr;
    lay::Node* win = win_c != nullptr ? t.find(win_c->id) : nullptr;
    if (s.mac.fill && desk != nullptr && dock != nullptr) {
        desk->padding = lay::Padding{0, 0, true};
        desk->gap = 0.0f;
        const float dock_h = dock->size && dock->size->px ? dock->size->value : 62.0f;
        dock->size = lay::Span::pixels(dock_h + 20.0f);
        dock->padding = lay::Padding{10, 0, true};
        if (win != nullptr) {
            win->radius = 0.0f;
            win->border = lay::Line{1.0f, "border"};
        }
    }
    if (empty && win != nullptr) {
        win->background = lay::Json();
        win->border.reset();
    }
    return t;
}

// Under the layout's own backgrounds: the wallpaper over the Dock's container (the desktop), the
// floating window's shadow, and the empty-desktop hint.
void draw_mac_backdrop(App::State& s, const lay::Node& tree) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const lay::Node* dock = block_of(tree, "mac-dock");
    const lay::Node* title = block_of(tree, "mac-window-title");
    const lay::Node* desk = dock != nullptr ? tree.parent_of(dock->id) : nullptr;
    const lay::Node* win = title != nullptr ? tree.parent_of(title->id) : nullptr;
    if (const lay::Placed* p = desk != nullptr ? s.placed.at(desk->id) : nullptr)
        wallpaper(dl, ImVec2(p->outer.x, p->outer.y), ImVec2(p->outer.right(), p->outer.bottom()));
    const lay::Placed* wp = win != nullptr ? s.placed.at(win->id) : nullptr;
    if (wp == nullptr)
        return;
    const ImVec2 a(wp->outer.x, wp->outer.y);
    const ImVec2 b(wp->outer.right(), wp->outer.bottom());
    if (shown_tab(s) == nullptr)
        empty_desktop(s, dl, a, b);
    else if (!s.mac.fill)
        window_shadow(dl, a, b, px(win->radius.value_or(12.0f)));
}

// The menu bar: the round window buttons (when chosen), the mark and "AgentDesk" (the app menu),
// File, Edit, View, Window and Help; on the right Windows' buttons (when chosen), the clock and the
// agents item. A click opens a menu; while one is open, pointing at another title opens that one.
// The rest of the bar moves the window; a double-click maximizes.
void draw_mac_menubar(App::State& s, ImVec2 pos, float w, float h) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 b(pos.x + w, pos.y + h);
    dl->AddRectFilled(pos, b, bar_color());
    dl->AddLine(ImVec2(pos.x, b.y - 1), ImVec2(b.x, b.y - 1), g_pal->border);
    const float cy = pos.y + (h - 1) * 0.5f;
    // A click elsewhere or Esc closed the dropdown.
    const int was_open = s.mac.menu;
    if (s.mac.menu >= 0 && !ImGui::IsPopupOpen(kMenuPopup))
        s.mac.menu = -1;
    s.mac.menu_at.assign(mac::kMenuCount, ImVec2(pos.x, b.y));
    float x = clear_of_traffic_lights(s, pos.x + px(10), pos.y, b.y, px(14));
    const float fs = fpx(13);
    for (int i = 0; i < mac::kMenuCount; ++i) {
        const char* title = mac::menu_title(static_cast<mac::Menu>(i));
        ImFont* font = i == 0 ? s.fonts.ui_bold : s.fonts.ui;
        const float tw = font->CalcTextSizeA(fs, FLT_MAX, 0, title).x;
        const float mark = i == 0 ? px(15) + px(7) : 0.0f;
        const float iw = px(9) + mark + tw + px(9);
        const ImVec2 ia(x, pos.y + px(4));
        const ImVec2 ib(x + iw, b.y - px(5));
        ImGui::SetCursorScreenPos(ia);
        ImGui::PushID(i);
        ImGui::InvisibleButton("##mac-title", ImVec2(iw, ib.y - ia.y));
        ImGui::PopID();
        if (ImGui::IsItemActivated()) {
            if (was_open == i)
                s.mac.menu = -1;  // a click on the open menu's title closes it
            else
                open_menu(s, i);
        } else if (s.mac.menu >= 0 && s.mac.menu != i && ImGui::IsMouseHoveringRect(ia, ib))
            open_menu(s, i);
        const bool open = s.mac.menu == i;
        if (open)
            dl->AddRectFilled(ia, ib, g_pal->selected, px(5));
        else if (ImGui::IsItemHovered())
            dl->AddRectFilled(ia, ib, g_pal->hover, px(5));
        float tx = x + px(9);
        if (i == 0) {
            draw_brand_mark(dl, ImVec2(tx, cy - px(7.5f)), px(15));
            tx += mark;
        }
        dl->AddText(font, fs, ImVec2(tx, cy - fs * 0.6f), g_pal->text, title);
        s.mac.menu_at[static_cast<std::size_t>(i)] = ImVec2(x, b.y + px(2));
        x += iw + (i == 0 ? px(6) : px(1));
    }
    draw_menu_dropdown(s);

    // Right side, right to left.
    float right = b.x - px(10);
    if (windows_window_buttons(s)) {
        draw_window_buttons(s, b.x - px(4), pos.y, h - 1);
        right -= px(26) * 3 + px(4) + px(10);
    }
    std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &now);
    const std::string clock = mac::clock_text(local.tm_wday, local.tm_hour, local.tm_min);
    const float cfs = fpx(12.5f);
    const float cw = s.fonts.ui_medium->CalcTextSizeA(cfs, FLT_MAX, 0, clock.c_str()).x;
    dl->AddText(s.fonts.ui_medium, cfs, ImVec2(right - cw, cy - cfs * 0.6f), g_pal->text, clock.c_str());
    right -= cw + px(10);
    draw_agents_item(s, dl, right, pos.y, h);
    window_drag_area(s, pos, b);
}

// The window's title bar: its three round buttons (red and amber put the tab away into the Dock,
// its programs running; green fills the desktop), and "<space> — <tab>" centred. A double-click
// on the bar fills the desktop too.
void draw_mac_window_title(App::State& s, ImVec2 pos, float w, float h) {
    const ui::Tab* tab = shown_tab(s);
    if (tab == nullptr)
        return;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 b(pos.x + w, pos.y + h);
    dl->AddRectFilled(pos, b, title_color(), s.mac.fill ? 0.0f : px(11), ImDrawFlags_RoundCornersTop);
    dl->AddLine(ImVec2(pos.x, b.y - 1), ImVec2(b.x, b.y - 1), g_pal->border);
    const float cy = pos.y + (h - 1) * 0.5f;
    const float r = px(5.5f);
    const float step = px(11) + px(8);
    const ImVec2 ga(pos.x + px(14) - px(4), cy - r - px(4));
    const ImVec2 gb(pos.x + px(14) + step * 3 - px(8) + px(4), cy + r + px(4));
    const bool group = ImGui::IsMouseHoveringRect(ga, gb) && ImGui::IsWindowHovered();
    static constexpr const char* kTips[3] = {"Hide this tab \xE2\x80\x94 programs keep running",
                                             "Minimize to the Dock", "Fill the desktop"};
    for (int i = 0; i < 3; ++i) {
        const ImVec2 c(pos.x + px(14) + r + step * static_cast<float>(i), cy);
        ImGui::SetCursorScreenPos(ImVec2(c.x - r - px(4), c.y - r - px(4)));
        ImGui::PushID(i);
        const bool hit = ImGui::InvisibleButton("##mac-win", ImVec2(r * 2 + px(8), r * 2 + px(8)));
        ImGui::PopID();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", i == 2 && s.mac.fill ? "Float the window again" : kTips[i]);
        paint_round_button(dl, c, r, i, s.window_focused || group, group, ImGui::IsItemActive(),
                           title_color());
        if (hit)
            run_command(s, i == 2 ? "window.fill" : "tab.settle");
    }
    const ui::Space* sp = s.ws.active();
    const std::string title = mac::window_title(
        sp != nullptr ? s.ws.space_title(s.ws.active_space, s.session_cwds()) : std::string(),
        tab_label(s, *tab, sp != nullptr ? sp->active_tab : 0, s.ws.sessions_in(*tab)));
    const float fs = fpx(12.5f);
    const float room = w - 2.0f * (px(14) + step * 3 + px(8));
    const float tw = std::min(room, s.fonts.ui_medium->CalcTextSizeA(fs, FLT_MAX, 0, title.c_str()).x);
    text_clipped(dl, s.fonts.ui_medium, fs, ImVec2(pos.x + (w - tw) * 0.5f, cy - fs * 0.6f),
                 s.window_focused ? g_pal->text : g_pal->dim, title, room);
    if (!ImGui::IsAnyItemHovered() && ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(pos, b) &&
        ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        run_command(s, "window.fill");
}

// The Dock: a rounded pill of 44 px tiles 10 px apart with 10 px inside each end, centred at the
// bottom: one tile per space (its avatar; a dot under the active one), a separator, the active
// space's tabs (the focused pane's program icon; a dot when something runs, a badge for its
// state, faded when put away), a separator, "+" and the gear. Pointing at a tile magnifies it and
// its neighbours and shows its name above it.
void draw_mac_dock(App::State& s, ImVec2 pos, float w, float h) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ui::Space* asp = s.ws.active();
    std::vector<std::string> space_ids;
    std::vector<std::string> tab_ids;
    for (const auto& sp : s.ws.spaces)
        space_ids.push_back(sp.id);
    if (asp != nullptr)
        for (const auto& t : asp->tabs)
            tab_ids.push_back(t.id);
    const auto items = mac::dock_items(space_ids, tab_ids);
    float tile = px(44);
    float gap = px(10);
    float pad = px(10);
    auto geom = mac::dock_geometry(items, tile, gap, pad);
    if (geom.width > w && geom.width > 0) {  // too many tiles: the whole Dock shrinks to fit
        const float k = w / geom.width;
        tile *= k;
        gap *= k;
        pad *= k;
        geom = mac::dock_geometry(items, tile, gap, pad);
    }
    const ImVec2 a(std::round(pos.x + (w - geom.width) * 0.5f), pos.y);
    const ImVec2 b(a.x + geom.width, pos.y + h);
    dl->AddRectFilled(a, b, dock_color(), px(18));
    dl->AddRect(a, b, g_pal->border, px(18));
    const float base_bottom = a.y + (h + tile) * 0.5f;
    const auto cwd = s.session_cwds();

    // Pass 1: hit areas (each tile's runs halfway into the gaps), clicks and the tab menus.
    int hovered = -1;
    std::string close_tab;
    for (std::size_t i = 0; i < items.size(); ++i) {
        const auto& it = items[i];
        if (!it.tile())
            continue;
        const float x = a.x + geom.x[i];
        ImGui::SetCursorScreenPos(ImVec2(x - gap * 0.5f, a.y));
        ImGui::PushID(it.key.c_str());
        const bool clicked = ImGui::InvisibleButton("##dock", ImVec2(tile + gap, h));
        if (ImGui::IsItemHovered())
            hovered = static_cast<int>(i);
        switch (it.kind) {
            case mac::DockItem::Kind::kSpace:
                if (clicked)
                    run_command(s, "space.select:" + std::to_string(it.index + 1));
                break;
            case mac::DockItem::Kind::kTab:
                if (clicked)
                    run_command(
                        s, "tab.goto:" + std::to_string(s.ws.active_space) + ":" + std::to_string(it.index));
                if (asp != nullptr)
                    tab_menu(s, asp->tabs[static_cast<std::size_t>(it.index)], close_tab);
                break;
            case mac::DockItem::Kind::kNew:
                if (clicked)
                    ImGui::OpenPopup("##newmenu");
                ImGui::SetNextWindowPos(ImVec2(x + tile * 0.5f, a.y - px(8)), ImGuiCond_Always,
                                        ImVec2(0.5f, 1.0f));
                draw_new_menu(s);
                break;
            case mac::DockItem::Kind::kSettings:
                if (clicked)
                    run_command(s, "app.settings");
                break;
            case mac::DockItem::Kind::kSeparator:
                break;
        }
        ImGui::PopID();
    }
    if (!close_tab.empty())
        run_command(s, "tab.close:" + close_tab);

    // Pass 2: the magnification, eased toward its target each frame.
    const float dt = ImGui::GetIO().DeltaTime;
    bool moving = false;
    std::map<std::string, float> scale;
    for (std::size_t i = 0; i < items.size(); ++i) {
        const float target = mac::dock_target_scale(items, static_cast<int>(i), hovered);
        const auto old = s.mac.scale.find(items[i].key);
        const float now = mac::approach(old != s.mac.scale.end() ? old->second : 1.0f, target, dt);
        moving = moving || now != target;
        scale[items[i].key] = now;
    }
    s.mac.scale = std::move(scale);
    if (moving && s.config.wake)
        s.config.wake();  // another frame while the tiles ease

    // Pass 3: the tiles, bottom-aligned so they grow upward.
    std::string bubble;
    ImVec2 bubble_at{};
    for (std::size_t i = 0; i < items.size(); ++i) {
        const auto& it = items[i];
        const float x = a.x + geom.x[i];
        if (!it.tile()) {
            dl->AddLine(ImVec2(x, a.y + px(13)), ImVec2(x, b.y - px(13)), g_pal->border, 1.0f);
            continue;
        }
        const float k = s.mac.scale[it.key];
        const float side = tile * k;
        const float cx = x + tile * 0.5f;
        const ImVec2 ta(cx - side * 0.5f, base_bottom - side);
        const ImVec2 tb(cx + side * 0.5f, base_bottom);
        const float rad = side * 0.24f;
        std::string label;
        Attention att = Attention::kNone;
        bool dot = false;
        bool faded = false;
        switch (it.kind) {
            case mac::DockItem::Kind::kSpace: {
                const ui::Space& sp = s.ws.spaces[static_cast<std::size_t>(it.index)];
                label = s.ws.space_title(it.index, cwd);
                draw_space_avatar(s, dl, ta, side, avatar_color(sp.color), IM_COL32(255, 255, 255, 255),
                                  avatar_letter(label), side * 0.42f, rad);
                dot = it.index == s.ws.active_space;
                std::vector<Attention> atts;
                for (const auto& t : sp.tabs)
                    atts.push_back(tab_attention(s, s.ws.sessions_in(t)));
                att = ui::most_urgent(atts);
                if (att == Attention::kWorking || att == Attention::kIdle)
                    att = Attention::kNone;  // a space only calls out what needs a look
                break;
            }
            case mac::DockItem::Kind::kTab: {
                const ui::Tab& t = asp->tabs[static_cast<std::size_t>(it.index)];
                const auto ids = s.ws.sessions_in(t);
                label = tab_label(s, t, it.index, ids);
                std::string shown =
                    std::find(ids.begin(), ids.end(), t.focused) != ids.end() ? t.focused : lead_pane(s, ids);
                ui::Program prog;
                prog.key = "program";
                if (const auto v = s.views_by_id.find(shown); v != s.views_by_id.end())
                    prog = program_of(v->second);
                dl->AddRectFilled(ta, tb, hex_color(prog.tile, IM_COL32(0x1c, 0x1d, 0x21, 255)), rad);
                dl->AddRect(ta, tb, alpha(IM_COL32(255, 255, 255, 255), g_pal->light ? 0 : 18), rad);
                const float icon = side * 0.56f;
                program_icon(dl, prog, ImVec2(cx - icon * 0.5f, ta.y + (side - icon) * 0.5f), icon,
                             hex_color(prog.color, IM_COL32(255, 255, 255, 255)));
                if (it.index == asp->active_tab)
                    dl->AddRect(ImVec2(ta.x - px(2), ta.y - px(2)), ImVec2(tb.x + px(2), tb.y + px(2)),
                                alpha(g_pal->focus, 200), rad + px(2), 0, px(1.5f));
                for (const auto& id : ids)
                    if (const auto v = s.views_by_id.find(id);
                        v != s.views_by_id.end() && !v->second.lost && v->second.process_state == "Running")
                        dot = true;
                att = tab_attention(s, ids);
                faded = t.settled;
                if (t.settled)
                    label += " (hidden)";
                break;
            }
            case mac::DockItem::Kind::kNew:
            case mac::DockItem::Kind::kSettings: {
                const bool plus = it.kind == mac::DockItem::Kind::kNew;
                dl->AddRectFilled(ta, tb, raised_color(), rad);
                dl->AddRect(ta, tb, g_pal->border, rad);
                draw_icon(s, dl, plus ? ui::icon::kPlus : ui::icon::kSettings, side * (plus ? 0.46f : 0.48f),
                          ImVec2(cx, (ta.y + tb.y) * 0.5f), g_pal->text);
                label = plus ? "New tab" : "Settings";
                break;
            }
            case mac::DockItem::Kind::kSeparator:
                break;
        }
        if (faded)  // put away: the glass shows through
            dl->AddRectFilled(ta, tb, alpha(dock_color(), 150), rad);
        if (dot)
            dl->AddCircleFilled(ImVec2(cx, b.y - px(4.5f)), px(2), g_pal->light ? g_pal->dim : g_pal->text);
        // The state badge on the top-right corner.
        const ImVec2 bc(tb.x - px(3), ta.y + px(3));
        const float br = px(7.5f);
        if (att == Attention::kBlocked) {
            dl->AddCircleFilled(bc, br, g_pal->needs_you, 24);
            const ImVec2 t = s.fonts.ui_bold->CalcTextSizeA(fpx(11), FLT_MAX, 0, "!");
            dl->AddText(s.fonts.ui_bold, fpx(11), ImVec2(bc.x - t.x * 0.5f, bc.y - t.y * 0.5f),
                        IM_COL32_WHITE, "!");
        } else if (att == Attention::kDone) {
            dl->AddCircleFilled(bc, br, g_pal->done, 24);
            draw_icon(s, dl, ui::icon::kCheck, fpx(10), bc, IM_COL32_WHITE);
        } else if (att == Attention::kWorking) {
            dl->AddCircleFilled(bc, br, g_pal->card, 24);
            dl->AddCircle(bc, br, g_pal->border, 24, 1.0f);
            const float t0 = static_cast<float>(ImGui::GetTime()) * (2.0f * 3.14159265f / 0.9f);
            dl->PathArcTo(bc, br - px(2.5f), t0, t0 + 4.4f, 18);
            dl->PathStroke(g_pal->working, 0, px(1.6f));
        }
        if (static_cast<int>(i) == hovered) {
            bubble = label;
            bubble_at = ImVec2(cx, ta.y - px(8));
        }
    }

    // The name above the tile under the pointer, over everything (the panes are child windows).
    if (!bubble.empty()) {
        ImDrawList* fg = ImGui::GetForegroundDrawList(ImGui::GetMainViewport());
        const float fs = fpx(12);
        const ImVec2 t = s.fonts.ui_medium->CalcTextSizeA(fs, FLT_MAX, 0, bubble.c_str());
        const float bw = std::min(t.x, px(320)) + px(20);
        const float bh = fs * 1.25f + px(10);
        const ImVec2 ba(
            std::clamp(bubble_at.x - bw * 0.5f, px(6), ImGui::GetMainViewport()->Size.x - bw - px(6)),
            bubble_at.y - bh);
        fg->AddRectFilled(ImVec2(ba.x, ba.y + px(2)), ImVec2(ba.x + bw, ba.y + bh + px(2)),
                          IM_COL32(0, 0, 0, 40), px(7));
        fg->AddRectFilled(ba, ImVec2(ba.x + bw, ba.y + bh), raised_color(), px(7));
        fg->AddRect(ba, ImVec2(ba.x + bw, ba.y + bh), g_pal->border, px(7));
        text_clipped(fg, s.fonts.ui_medium, fs, ImVec2(ba.x + px(10), ba.y + (bh - fs) * 0.5f - fs * 0.05f),
                     g_pal->text, bubble, px(320));
    }
}

}  // namespace detail
}  // namespace agentdesk::desktop
