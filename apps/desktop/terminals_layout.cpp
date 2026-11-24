// The Terminals layout: its top bar (space chip, tab tray, buttons) and pane headers.
// Portions adapted from herdr-app (Apache-2.0) and modified; see THIRD_PARTY_NOTICES.md.
#include "app_internal.hpp"

namespace agentdesk::desktop {
namespace detail {

// The hex values are what each step gives on the graphite theme (background #131619, text
// #e8e9eb), the colours this layout was designed in.
TerminalsLook terminals_look() {
    const ImU32 bg = g_pal->bg, fg = g_pal->text;
    TerminalsLook l{};
    l.tray = mix(bg, fg, 0.047f);      // #1d1f22
    l.tab = mix(bg, fg, 0.155f);       // #34373c
    l.tab_edge = mix(bg, fg, 0.258f);  // #4a4d53
    l.hover = alpha(fg, 7);            // #ffffff07
    l.edge = mix(bg, fg, 0.174f);      // #383b40
    l.chip = mix(bg, fg, 0.085f);      // #252628
    l.text = fg;                       // #e8e9eb
    l.dim = g_pal->dim;                // #8b8f95
    l.tab_text = mix(bg, fg, 0.83f);   // #c4c6ca
    l.pane_border = mix(bg, fg, 0.08f);   // #24272b
    l.pane_focus = mix(bg, fg, 0.15f);    // #33363b
    l.button = mix(bg, fg, 0.33f);        // #5d6168
    l.button_hover = mix(bg, fg, 0.92f);  // #d6d8db
    return l;
}

namespace {
// The pane a tab speaks for: the focused one, else its agent, else the first.
std::string terminals_lead(App::State& s, const ui::Tab& tab) {
    const auto ids = s.ws.sessions_in(tab);
    if (std::find(ids.begin(), ids.end(), tab.focused) != ids.end())
        return tab.focused;
    for (const auto& id : ids)
        if (const auto it = s.views_by_id.find(id); it != s.views_by_id.end() && is_agent(it->second))
            return id;
    return ids.empty() ? std::string() : ids.front();
}

// The program's tile: 26 px, radius 7, the program's tile colour (else the chip colour), a faint
// white edge (#ffffff14) and the program's glyph at 15 px in its own colour (else the text). A
// status other than idle puts an 11 px dot at the top right, ringed in the tray's colour.
void terminals_tile(App::State& s, ImDrawList* dl, ImVec2 a, const std::string& id, const TerminalsLook& l) {
    const float side = px(26);
    std::optional<ui::Program> g;
    if (const auto it = s.views_by_id.find(id); it != s.views_by_id.end())
        g = program_of(it->second);
    dl->AddRectFilled(a, ImVec2(a.x + side, a.y + side), g ? hex_color(g->tile, l.chip) : l.chip, px(7));
    dl->AddRect(a, ImVec2(a.x + side, a.y + side), IM_COL32(255, 255, 255, 0x14), px(7));
    if (g)
        program_icon(dl, *g, ImVec2(a.x + (side - px(15)) * 0.5f, a.y + (side - px(15)) * 0.5f), px(15),
                     hex_color(g->color, l.text));
    const Attention att = id.empty() ? Attention::kNone : s.attention_of(id);
    if (att != Attention::kNone && att != Attention::kIdle) {
        const ImVec2 c(a.x + side + px(4) - px(5.5f), a.y - px(4) + px(5.5f));
        dl->AddCircleFilled(c, px(5.5f), l.tray);  // the 2 px border in the tray's colour
        dl->AddCircleFilled(c, px(3.5f),
                            tok::cross_fade(ImGui::GetID(("##tile-dot" + id).c_str()), attention_color(att)));
    }
}

}  // namespace

// The draggable top bar: the space chip (opens the picker), the tabs in a tray, then the palette,
// "+" (a new tab at once) and the window buttons.
void draw_terminals_bar(App::State& s, ImVec2 pos, float width, float height) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const TerminalsLook l = terminals_look();
    auto* sp = s.ws.active();
    const float cy = pos.y + height * 0.5f;
    float right = pos.x + width;
    // Window buttons: 6 px left and 4 px right padding, 8 px apart; 24 px round chips with 14 px
    // icons; hover uses the edge colour and the text colour.
    if (windows_window_buttons(s)) {  // round ones sit at the top left instead
        const bool zoomed = window_is_maximized(s.config.window);
        struct B {
            const char* id;
            const char* glyph;
            const char* cmd;
        };
        const B buttons[] = {{"##tm-close", ui::icon::kWinClose, "window.close"},
                             {"##tm-max", zoomed ? ui::icon::kRestore : ui::icon::kMaximize, "window.maximize"},
                             {"##tm-min", ui::icon::kMinimize, "window.minimize"}};
        right -= px(4);
        for (const auto& b : buttons) {
            right -= px(24);
            const ImVec2 a(right, cy - px(12));
            ImGui::SetCursorScreenPos(a);
            const bool hit = ImGui::InvisibleButton(b.id, ImVec2(px(24), px(24)));
            const bool hov = ImGui::IsItemHovered();
            dl->AddCircleFilled(ImVec2(a.x + px(12), cy), px(12), hov ? l.edge : l.chip);
            draw_icon(s, dl, b.glyph, fpx(14), ImVec2(a.x + px(12), cy), hov ? l.text : l.dim);
            if (hit)
                run_command(s, b.cmd);
            right -= px(8);
        }
        right += px(8) - px(6) - px(8);  // the group's 6 px left padding, then the bar's 8 px gap
    }
    if (sp == nullptr) {
        window_drag_area(s, pos, ImVec2(pos.x + width, pos.y + height));
        return;
    }
    // Icon buttons: 7 px padding, radius 8, 17 px dim icons; hover adds the chip and the text colour
    // and a tooltip with the button's name and its shortcut, so the bar can be learned by pointing.
    auto icon_btn = [&](const char* id, const char* glyph, const char* cmd, const char* name) {
        const float side = px(17) + px(14);
        right -= side;
        const ImVec2 a(right, cy - side * 0.5f);
        ImGui::SetCursorScreenPos(a);
        const bool hit = ImGui::InvisibleButton(id, ImVec2(side, side));
        const bool hov = ImGui::IsItemHovered();
        if (hov)
            dl->AddRectFilled(a, ImVec2(a.x + side, a.y + side), l.chip, px(8));
        draw_icon(s, dl, glyph, fpx(17), ImVec2(a.x + side * 0.5f, cy), hov ? l.text : l.dim);
        if (hov) {
            const std::string chord = s.keymap.chord_for(cmd);
            ImGui::SetTooltip("%s", chord.empty() ? name : (std::string(name) + "  " + chord).c_str());
        }
        if (hit)
            run_command(s, cmd);
        right -= px(8);
    };
    icon_btn("##tm-new", ui::icon::kPlus, "tab.new", "New tab");                 // no menu: a new tab at once
    icon_btn("##tm-keys", "\xF3\xB0\x98\xB3", "app.palette", "Commands");  // nf-md-apple_keyboard_command
    // Not in the original bar: this layout has no sidebar, so without this button Settings (and the
    // layout picker on its Look page) could only be found through a shortcut or the palette.
    icon_btn("##tm-settings", ui::icon::kSettings, "app.settings", "Settings and layouts");
    // Space chip: 30 px high and round, 10 px left and 12 px right padding, 8 px gap; a 15 px icon
    // and the space name (at most 160 px).
    // With the round window buttons the bar starts 10 px after them.
    float x = clear_of_traffic_lights(s, pos.x, pos.y, pos.y + height, px(10));
    // Not in the original bar: this layout has no sidebar, so its first button brings one back
    // (sidebar.toggle switches to the AgentDesk layout), the same button and shortcut as there.
    {
        const float side = px(17) + px(14);
        const ImVec2 a(x, cy - side * 0.5f);
        ImGui::SetCursorScreenPos(a);
        const bool hit = ImGui::InvisibleButton("##tm-sidebar", ImVec2(side, side));
        const bool hov = ImGui::IsItemHovered();
        if (hov) {
            dl->AddRectFilled(a, ImVec2(a.x + side, a.y + side), l.chip, px(8));
            const std::string chord = s.keymap.chord_for("sidebar.toggle");
            ImGui::SetTooltip("%s", chord.empty() ? "Show sidebar" : ("Show sidebar  " + chord).c_str());
        }
        draw_icon(s, dl, ui::icon::kSidebar, fpx(17), ImVec2(a.x + side * 0.5f, cy), hov ? l.text : l.dim);
        if (hit)
            run_command(s, "sidebar.toggle");
        x += side + px(8);
    }
    const auto cwds = s.session_cwds();
    const std::string sname = s.ws.space_title(s.ws.active_space, cwds);
    const float nw = std::min(s.fonts.ui->CalcTextSizeA(fpx(13), FLT_MAX, 0, sname.c_str()).x, px(160));
    const float chip_w = px(10) + px(15) + px(8) + nw + px(12);
    {
        const ImVec2 a(x, cy - px(15));
        ImGui::SetCursorScreenPos(a);
        const bool hit = ImGui::InvisibleButton("##tm-space", ImVec2(chip_w, px(30)));
        const bool hov = ImGui::IsItemHovered();
        dl->AddRectFilled(a, ImVec2(a.x + chip_w, a.y + px(30)), l.chip, px(15));
        draw_icon(s, dl, ui::icon::kLayers, fpx(15), ImVec2(a.x + px(10) + px(7.5f), cy), hov ? l.text : l.dim);
        text_clipped(dl, s.fonts.ui, fpx(13), ImVec2(a.x + px(10) + px(15) + px(8), cy - fpx(13) * 0.6f), l.text, sname,
                     px(160));
        if (hit)
            run_command(s, "app.picker");  // the picker drops from the top (popup at top, offset 40)
        x += chip_w + px(8);
    }
    // The tray: 36 px high, 3 px padding and gap, radius 16; tabs 260 px wide that shrink to fit.
    const int n = static_cast<int>(sp->tabs.size());
    const float avail = std::max(0.0f, right - x);
    const float want = static_cast<float>(n) * px(260) + static_cast<float>(std::max(0, n - 1)) * px(3) + px(6);
    const float tray_w = std::min(want, avail);
    const float tab_w = n > 0 ? std::max(0.0f, (tray_w - px(6) - static_cast<float>(n - 1) * px(3)) / static_cast<float>(n)) : 0.0f;
    const ImVec2 ta(x, cy - px(18));
    if (n > 0 && tray_w > px(6))
        dl->AddRectFilled(ta, ImVec2(ta.x + tray_w, ta.y + px(36)), l.tray, px(16));
    dl->PushClipRect(ta, ImVec2(ta.x + tray_w, ta.y + px(36)), true);
    std::string close_tab;
    float tx = ta.x + px(3);
    for (int i = 0; i < n; ++i) {
        auto& tab = sp->tabs[static_cast<std::size_t>(i)];
        const bool active = i == sp->active_tab;
        const ImVec2 a(tx, ta.y + px(3));
        const ImVec2 b(tx + tab_w, ta.y + px(33));
        tx += tab_w + px(3);
        ImGui::PushID(tab.id.c_str());
        ImGui::SetCursorScreenPos(a);
        const bool hit = ImGui::InvisibleButton("##tm-tab", ImVec2(std::max(1.0f, tab_w), px(30)));
        const bool hov = ImGui::IsItemHovered();
        if (hit) {
            s.ws.select_tab(i);
            s.focus_view(s.ws.focused_session());
        }
        if (begin_menu("##tabctx")) {  // Rename, New tab, Close, acting on this tab by id
            if (menu_row(s, ui::icon::kEdit, "Rename", s.keymap.chord_for("tab.rename")))
                run_command(s, "tab.rename:" + tab.id);
            if (menu_row(s, ui::icon::kPlus, "New tab", s.keymap.chord_for("tab.new")))
                run_command(s, "tab.new");
            menu_separator();
            if (menu_row(s, ui::icon::kClose, "Close", s.keymap.chord_for("tab.close")))
                close_tab = tab.id;
            ImGui::EndPopup();
        }
        if (active) {
            dl->AddRectFilled(a, b, l.tab, px(13));
            dl->AddRect(a, b, l.tab_edge, px(13));
        } else if (hov) {
            dl->AddRectFilled(a, b, l.hover, px(13));
        }
        // 5 px left and 12 px right padding, 9 px gap: the tile, the full name, then the folder
        // (dim, truncated).
        dl->PushClipRect(a, ImVec2(b.x - px(12), b.y), true);
        const std::string lead = terminals_lead(s, tab);
        terminals_tile(s, dl, ImVec2(a.x + px(5), a.y + px(2)), lead, l);
        float nx = a.x + px(5) + px(26) + px(9);
        const std::string name = ui::Workspace::tab_title(tab, i);
        const float name_w = text_clipped(dl, s.fonts.ui, fpx(13), ImVec2(nx, a.y + px(15) - fpx(13) * 0.6f),
                                          active ? l.text : l.tab_text, name, std::max(0.0f, b.x - px(12) - nx));
        nx += name_w + px(6);
        if (const auto vi = s.views_by_id.find(lead); vi != s.views_by_id.end() && !vi->second.cwd.empty())
            text_clipped(dl, s.fonts.ui, fpx(13), ImVec2(nx, a.y + px(15) - fpx(13) * 0.6f), l.dim,
                         display_path(vi->second.cwd), b.x - px(12) - nx);
        dl->PopClipRect();
        ImGui::PopID();
    }
    dl->PopClipRect();
    if (!close_tab.empty())
        run_command(s, "tab.close:" + close_tab);
    window_drag_area(s, pos, ImVec2(pos.x + width, pos.y + height));
}

// The pane header of the Terminals layout: 38 px, 12 px left and 8 px right padding, 8 px gaps,
// 14 px text. The program icon, the terminal's title (semibold, at most 320 px), its folder when
// the title does not already say it, then split right, split down, zoom and close: always shown,
// no tooltips.
void draw_terminals_pane_header(App::State& s, const std::string& id, const ui::SessionView& v, ImVec2 pos,
                                float w, float h, bool focused) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const TerminalsLook l = terminals_look();
    const float cy = pos.y + h * 0.5f;
    float x = pos.x + px(12);
    program_icon(dl, program_of(v), ImVec2(x, cy - px(8)), px(16), IM_COL32(0xd6, 0xd8, 0xdb, 255));
    x += px(16) + px(8);
    // The folder is left out when the title already holds it or is itself a path.
    std::string title = s.pane_title(id);
    if (title.empty())
        title = "shell";
    const std::string where = display_path(v.cwd);
    const bool says = !where.empty() && (title.find(where) != std::string::npos || title[0] == '~' ||
                                         title[0] == '/' || (title.size() > 2 && title[1] == ':' && title[2] == '\\'));
    const std::string dir = says ? std::string() : where;
    // Buttons from the right: 4 px padding, radius 5, 15 px icons; hover adds a faint background
    // and brightens the icon.
    float right = pos.x + w - px(8);
    const auto* tab = s.ws.active_tab();
    const bool zoomed = tab != nullptr && tab->zoomed == id;
    struct B {
        const char* bid;
        const char* glyph;
        const char* cmd;
    };
    const B buttons[] = {{"##th-x", ui::icon::kClose, "pane.close"},
                         {"##th-z", zoomed ? ui::icon::kUnzoom : ui::icon::kZoom, "pane.zoom"},
                         {"##th-d", ui::icon::kSplitDown, "pane.split:down"},
                         {"##th-r", ui::icon::kSplitRight, "pane.split:right"}};
    std::string run;
    for (const auto& b : buttons) {
        const float side = px(15) + px(8);
        right -= side;
        const ImVec2 a(right, cy - side * 0.5f);
        ImGui::SetCursorScreenPos(a);
        const bool hit = ImGui::InvisibleButton(b.bid, ImVec2(side, side));
        const bool hov = ImGui::IsItemHovered();
        if (hov)
            dl->AddRectFilled(a, ImVec2(a.x + side, a.y + side), alpha(g_pal->text, 0x0d), px(5));
        draw_icon(s, dl, b.glyph, fpx(15), ImVec2(a.x + side * 0.5f, cy), hov ? l.button_hover : l.button);
        if (hit)
            run = b.cmd;
        right -= px(8);
    }
    const float tw = text_clipped(dl, s.fonts.ui_bold, fpx(14), ImVec2(x, cy - fpx(14) * 0.6f),
                                  focused ? l.text : mix(g_pal->dim, g_pal->text, 0.4f), title,
                                  std::min(px(320), std::max(0.0f, right - x)));
    x += tw + px(8);
    text_clipped(dl, s.fonts.ui, fpx(14), ImVec2(x, cy - fpx(14) * 0.6f), l.dim, dir, right - x);
    // A press on the header focuses the pane (pane.focus).
    if (ImGui::IsMouseHoveringRect(pos, ImVec2(pos.x + w, pos.y + h)) && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        !ImGui::IsAnyItemHovered() && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)) {
        s.ws.focus(id);
        s.focus_view(id);
    }
    if (!run.empty()) {
        s.ws.focus(id);
        run_command(s, run);
    }
}
}  // namespace detail

}  // namespace agentdesk::desktop
