// The settings dialog: the navigation on the left and the Look, Machines, Alerts and Mods pages
// (Updates and Privacy are in settings_pages.cpp). Every change applies and saves at once.
// Portions adapted from herdr-app (Apache-2.0) and modified; see THIRD_PARTY_NOTICES.md.
#include "overlay_kit.hpp"

namespace agentdesk::desktop {
namespace detail {

namespace {
// Dialog 880 x 620 at scale 1; the navigation column is 190 px wide.
constexpr float kDialogW = 880;
constexpr float kDialogH = 620;
constexpr float kNavW = 190;

// Cards that wrap onto new rows, 8 px apart: where the next card goes.
struct CardFlow {
    float x0, right, x, y, row_h = 0;

    // Starts at the cursor and wraps at the content region's right edge.
    static CardFlow at_cursor() {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        return CardFlow{p.x, p.x + ImGui::GetContentRegionAvail().x, p.x, p.y};
    }
    ImVec2 next(float cw, float ch) {
        if (x > x0 && x + cw > right) {
            x = x0;
            y += row_h + px(8);
            row_h = 0;
        }
        const ImVec2 a(x, y);
        x += cw + px(8);
        row_h = std::max(row_h, ch);
        return a;
    }
    // Moves the cursor below the last row.
    void end() const {
        ImGui::SetCursorScreenPos(ImVec2(x0, y + row_h));
        ImGui::Dummy(ImVec2(0, 0));
    }
};

// Layout and theme cards: 124 px wide with 6 px padding, rounded 10.
constexpr float kCardW = 124;
constexpr float kCardPad = 6;

// A card's name under its preview: 12.5 px, the foreground with a 12 px accent check after it
// when chosen, else dim. `check_dy` places the check below the text's top.
void card_name(App::State& s, ImDrawList* d, ImVec2 lp, const std::string& name, bool on, float check_dy) {
    const float tw = text_clipped(d, s.fonts.ui, fpx(12.5f), lp, on ? g_pal->text : g_pal->dim, name,
                                  px(kCardW) - px(kCardPad) * 2 - (on ? px(17) : 0.0f));
    if (on)
        draw_icon(s, d, ui::icon::kCheck, fpx(12), ImVec2(lp.x + tw + px(5) + px(6), lp.y + check_dy),
                  g_pal->focus);
}

// ------------------------------------------------------------------ navigation
// The heading, a row per page and "Send feedback" at the bottom. Returns true when the dialog
// should close.
bool draw_settings_nav(App::State& s, float h) {
    struct Section {
        const char* name;
        const char* glyph;
    };
    static const std::vector<Section> kSections = [] {
        std::vector<Section> v{{"Look", ui::icon::kPalette}};
        if (os::has_keyboard_settings())
            v.push_back({"Keyboard", ui::icon::kTerminal});  // macOS only (FR-40)
        v.insert(v.end(), {{"Machines", ui::icon::kServer},
                           {"Alerts", ui::icon::kBell},
                           {"Mods", ui::icon::kExtensions},
                           {"Updates", ui::icon::kRefresh},
                           {"Privacy", ui::icon::kShield}});
        return v;
    }();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 wp = ImGui::GetWindowPos();
    const float nav_w = px(kNavW);
    dl->AddRectFilled(wp, ImVec2(wp.x + nav_w, wp.y + h), g_pal->sidebar, px(8),
                      ImDrawFlags_RoundCornersLeft);
    dl->AddLine(ImVec2(wp.x + nav_w, wp.y), ImVec2(wp.x + nav_w, wp.y + h), g_pal->border);
    // 10 px of padding; the heading 16 px semibold with 10 px more on the left, 6 above, 12 below.
    dl->AddText(s.fonts.ui_bold, fpx(16), ImVec2(wp.x + px(10) + px(10), wp.y + px(10) + px(6)), g_pal->text,
                "Settings");
    // An update waiting to be installed puts a dot on Updates.
    const auto upd = ui::updates::status();
    const bool waiting = upd.app_ready.has_value() || (upd.core.ready.has_value() && !upd.core.managed);
    if (s.settings_section.empty())
        s.settings_section = "Look";
    // Rows: 7 px above and below, 2 px apart, rounded 7; a 15 px icon (accent when chosen), then
    // 10 px on, 13.5 px text (foreground when chosen, else dim). Raised when chosen or hovered.
    const float item_w = nav_w - px(20);
    const float item_h = px(7) + line_h(13.5f) + px(7);
    float ny = wp.y + px(10) + px(6) + line_h(16) + px(12);
    for (const auto& sec : kSections) {
        const ImVec2 a(wp.x + px(10), ny);
        ImGui::SetCursorScreenPos(a);
        ImGui::PushID(sec.name);
        if (ImGui::InvisibleButton("##nav", ImVec2(item_w, item_h)))
            s.settings_section = sec.name;
        const bool hov = ImGui::IsItemHovered();
        ImGui::PopID();
        const bool on = s.settings_section == sec.name;
        if (on || hov)
            dl->AddRectFilled(a, ImVec2(a.x + item_w, a.y + item_h), raised_color(), px(7));
        const float cy = a.y + item_h * 0.5f;
        draw_icon(s, dl, sec.glyph, fpx(15), ImVec2(a.x + px(10) + px(7.5f), cy),
                  on ? g_pal->focus : g_pal->faint);
        const ImVec2 t = s.fonts.ui->CalcTextSizeA(fpx(13.5f), FLT_MAX, 0, sec.name);
        dl->AddText(s.fonts.ui, fpx(13.5f), ImVec2(a.x + px(10) + px(15) + px(10), cy - t.y * 0.5f),
                    on ? g_pal->text : g_pal->dim, sec.name);
        if (std::string_view(sec.name) == "Updates" && waiting)  // a 7 px accent dot at the end
            dl->AddCircleFilled(ImVec2(a.x + item_w - px(10) - px(3.5f), cy), px(3.5f), g_pal->focus);
        ny += item_h + px(2);
    }
    // Feedback opens its own dialog, so Settings closes first.
    const ImVec2 bs = settings_button_size(s, "Send feedback");
    ImGui::SetCursorScreenPos(ImVec2(wp.x + px(10), wp.y + h - px(10) - bs.y));
    if (settings_button(s, "##st-feedback", "Send feedback")) {
        run_command(s, "app.feedback");
        return true;
    }
    return false;
}

// ------------------------------------------------------------------ Look page
// One card per layout, shipped ones first: a 62 px preview (the layout's own picture when it has
// one), then its name. Raised when chosen or hovered, a 2 px accent border when chosen.
void look_layout_cards(App::State& s) {
    CardFlow lf = CardFlow::at_cursor();
    for (const auto& l : s.layouts) {
        if (!ui::styles::is_listed(l.name, s.settings.styles.fun_layouts, s.settings.layout))
            continue;
        const bool on = l.name == s.settings.layout;
        const float cw = px(kCardW), pad = px(kCardPad), ph = px(62), lh = line_h(12.5f);
        const float ch = pad + ph + px(6) + lh + pad;
        const ImVec2 a = lf.next(cw, ch);
        ImGui::SetCursorScreenPos(a);
        ImGui::PushID(l.name.c_str());
        if (ImGui::InvisibleButton("##lay", ImVec2(cw, ch)))
            run_command(s, "layout:" + l.name);
        const bool hov = ImGui::IsItemHovered();
        ImGui::PopID();
        ImDrawList* d = ImGui::GetWindowDrawList();
        if (on || hov)
            d->AddRectFilled(a, ImVec2(a.x + cw, a.y + ch), raised_color(), px(10));
        if (on)
            d->AddRect(a, ImVec2(a.x + cw, a.y + ch), g_pal->focus, px(10), 0, px(2));
        {  // drawn in the theme that layout shows with, not the current one
            const Palette* const shown = std::exchange(g_pal, layout_palette(s, l));
            layout_preview(d, l.name, ImVec2(a.x + pad, a.y + pad), ImVec2(cw - pad * 2, ph));
            g_pal = shown;
        }
        card_name(s, d, ImVec2(a.x + pad, a.y + pad + ph + px(6)), layout_display_name(l.name), on,
                  fpx(12.5f) * 0.55f);
    }
    lf.end();
}

// A theme card's right-click menu: edit a copy of any theme; edit or delete the person's own.
void theme_card_menu(App::State& s, const Palette& p) {
    if (!begin_menu("##theme-menu"))
        return;
    if (menu_row(s, ui::icon::kEdit, "Edit a copy..."))
        edit_theme(s, p.id, false, true);
    if (is_user_theme(p.id)) {
        if (menu_row(s, ui::icon::kEdit, "Edit..."))
            edit_theme(s, p.id, true, true);
        if (menu_row(s, ui::icon::kClose, "Delete...")) {
            s.confirm_title = std::string("Delete ") + p.label + "?";
            s.confirm_detail = std::string("themes\\") + p.id +
                               ".json is set aside (renamed .json.old-...), not erased. Layouts using it go back "
                               "to the default theme.";
            s.confirm_command = std::string("theme.delete:") + p.id;
            s.show_confirm = true;
        }
    }
    ImGui::EndPopup();
}

// The last card of a row: a dashed outline with a plus, "New theme". It starts a copy of the
// layout's current theme of that kind.
void new_theme_card(App::State& s, ImVec2 a, bool light_cards) {
    const float cw = px(kCardW), pad = px(kCardPad), ph = px(46);
    const float ch = pad + ph + px(6) + line_h(12.5f) + pad;
    ImGui::SetCursorScreenPos(a);
    ImGui::PushID(light_cards ? "new-light" : "new-dark");
    const bool clicked = ImGui::InvisibleButton("##new-theme", ImVec2(cw, ch));
    const bool hov = ImGui::IsItemHovered();
    ImGui::PopID();
    ImDrawList* d = ImGui::GetWindowDrawList();
    if (hov)
        d->AddRectFilled(a, ImVec2(a.x + cw, a.y + ch), raised_color(), px(10));
    const ImVec2 pa(a.x + pad, a.y + pad);
    const ImVec2 pb(a.x + cw - pad, pa.y + ph);
    // Dashed outline: short segments along each side.
    const ImU32 line = hov ? g_pal->dim : g_pal->faint;
    const float dash = px(5), gap = px(4);
    for (float x = pa.x + px(6); x < pb.x - px(6); x += dash + gap) {
        d->AddLine(ImVec2(x, pa.y), ImVec2(std::min(x + dash, pb.x - px(6)), pa.y), line, px(1));
        d->AddLine(ImVec2(x, pb.y), ImVec2(std::min(x + dash, pb.x - px(6)), pb.y), line, px(1));
    }
    for (float y = pa.y + px(6); y < pb.y - px(6); y += dash + gap) {
        d->AddLine(ImVec2(pa.x, y), ImVec2(pa.x, std::min(y + dash, pb.y - px(6))), line, px(1));
        d->AddLine(ImVec2(pb.x, y), ImVec2(pb.x, std::min(y + dash, pb.y - px(6))), line, px(1));
    }
    draw_icon(s, d, ui::icon::kPlus, fpx(16), ImVec2((pa.x + pb.x) * 0.5f, (pa.y + pb.y) * 0.5f),
              hov ? g_pal->text : g_pal->dim);
    card_name(s, d, ImVec2(a.x + pad, pb.y + px(6)), "New theme", false, 0);
    if (clicked) {
        const auto pair = layout_theme_pair(s.settings);
        edit_theme(s, light_cards ? pair.second : pair.first, false, true);
    }
}

// The theme cards of one kind (dark or light) for the current layout. Each has a 46 px preview
// in the theme's background (rounded 6, 1 px border) with a 44 x 4 bar in its terminal
// foreground and five 8 px dots (accent, then ANSI red, green, yellow, magenta), then its name.
// Raised on hover, a 2 px accent border when chosen. Returns true when a theme was picked.
bool look_theme_cards(App::State& s, bool light_cards) {
    bool changed = false;
    const auto suggested = usable_suggestion(s.layout());
    CardFlow tf = CardFlow::at_cursor();
    for (const Palette* pal : all_palettes()) {
        const Palette& p = *pal;
        if (p.light != light_cards)
            continue;
        const auto shown = layout_theme_pair(s.settings);
        const bool on = (light_cards ? shown.second : shown.first) == p.id;
        const float cw = px(kCardW), pad = px(kCardPad), ph = px(46);
        const float ch = pad + ph + px(6) + line_h(12.5f) + pad;
        const ImVec2 a = tf.next(cw, ch);
        ImGui::SetCursorScreenPos(a);
        ImGui::PushID(p.id);
        if (ImGui::InvisibleButton("##theme", ImVec2(cw, ch))) {
            run_command(s, std::string("theme:") + p.id);
            changed = true;
        }
        const bool hov = ImGui::IsItemHovered();
        theme_card_menu(s, p);
        ImGui::PopID();
        ImDrawList* d = ImGui::GetWindowDrawList();
        if (hov)
            d->AddRectFilled(a, ImVec2(a.x + cw, a.y + ch), raised_color(), px(10));
        if (on)
            d->AddRect(a, ImVec2(a.x + cw, a.y + ch), g_pal->focus, px(10), 0, px(2));
        const ImVec2 pa(a.x + pad, a.y + pad);
        const ImVec2 pb(a.x + cw - pad, pa.y + ph);
        d->AddRectFilled(pa, pb, p.bg, px(6));
        d->AddRect(pa, pb, g_pal->border, px(6));
        d->AddRectFilled(ImVec2(pa.x + px(7), pa.y + px(7)), ImVec2(pa.x + px(7) + px(44), pa.y + px(11)),
                         p.term_fg, px(2));
        // The theme's ANSI table, else libvterm's defaults (what its terminals then show).
        const auto ansi = [&](int i) {
            static constexpr ImU32 kVterm[] = {IM_COL32(0, 0, 0, 255),   IM_COL32(224, 0, 0, 255),
                                               IM_COL32(0, 224, 0, 255), IM_COL32(224, 224, 0, 255),
                                               IM_COL32(0, 0, 224, 255), IM_COL32(224, 0, 224, 255)};
            return p.ansi != nullptr ? (*p.ansi)[static_cast<std::size_t>(i)] : kVterm[i];
        };
        const ImU32 dots[] = {p.focus, ansi(1), ansi(2), ansi(3), ansi(5)};
        float dx = pa.x + px(7);
        const float dy = pa.y + px(7) + px(4) + px(6);
        for (const ImU32 c : dots) {
            d->AddCircleFilled(ImVec2(dx + px(4), dy + px(4)), px(4), c);
            dx += px(8) + px(4);
        }
        card_name(s, d, ImVec2(a.x + pad, pb.y + px(6)), p.label, on, line_h(12.5f) * 0.5f);
        // Made for this layout: a small accent star in the preview's top right corner.
        if (suggested && (light_cards ? suggested->second : suggested->first) == p.id) {
            const ImVec2 c(pb.x - px(9), pa.y + px(9));
            d->AddCircleFilled(c, px(7), alpha(p.bg, 230));
            draw_suggested_mark(d, c, px(5.5f), g_pal->focus);
        }
    }
    new_theme_card(s, tf.next(px(kCardW), px(kCardPad) * 2 + px(46) + px(6) + line_h(12.5f)), light_cards);
    tf.end();
    return changed;
}

// Appearance, then a note and the dark and light theme cards.
bool look_theme(App::State& s) {
    bool changed = false;
    settings_section(s, "Theme");
    settings_row(s, "Appearance", "Schedule switches by the clock.", px(320), px(32));
    if (segmented(s, "##appearance", s.settings.appearance,
                  {{"system", "System"}, {"light", "Light"}, {"dark", "Dark"}, {"schedule", "Schedule"}})) {
        s.schedule_override_until.reset();
        changed = true;
    }
    settings_row_done(s);
    if (s.settings.appearance == "schedule")
        changed |= look_schedule(s);
    if (window_draws_own_buttons()) {  // macOS keeps its own
        settings_row(s, "Window buttons", "Round ones at the top left, or Windows' at the top right.", px(240),
                     px(32));
        if (segmented(s, "##window-buttons", s.settings.window_buttons, {{"mac", "Round"}, {"windows", "Windows"}}))
            changed = true;
        settings_row_done(s);
    }
    changed |= look_ambient(s);
    settings_gap(px(12));
    settings_wrapped(s, "For the layout you're in: each layout keeps its own light and dark theme.",
                     g_pal->faint);
    settings_gap(px(8));
    changed |= look_theme_suggestion(s);
    for (const bool light_cards : {false, true}) {
        if (light_cards)
            settings_gap(px(10));
        settings_wrapped(s, light_cards ? "Light" : "Dark", g_pal->dim, 12.5f, 12.5f * 1.3f,
                         s.fonts.ui_medium);
        settings_gap(px(6));
        changed |= look_theme_cards(s, light_cards);
    }
    return changed;
}

// Zoom, interface text, the terminal and interface fonts and the terminal's metrics.
bool look_size_and_fonts(App::State& s) {
    bool changed = false;
    settings_section(s, "Size and fonts");
    settings_row(s, "Zoom", "Everything: terminals, panels, text.",
                 px(kStepperW) + px(8) + settings_button_size(s, "Reset").x, px(kStepperH));
    if (stepper(s, "zoom", s.settings.zoom, 0.1f, 0.5f, 3.0f, "%.0f%%", 100.0f)) {
        apply_zoom(s);
        changed = true;
    }
    ImGui::SameLine(0, px(8));
    if (settings_button(s, "##st-zoom-reset", "Reset"))
        run_command(s, "app.zoom_reset");
    settings_row_done(s);
    settings_row(s, "Interface text", "Sidebar, tabs, menus and panels, at 100% zoom.", px(kStepperW),
                 px(kStepperH));
    if (stepper(s, "uitext", s.settings.ui_text, 1.0f, 11.0f, 24.0f, "%.0f px")) {
        apply_zoom(s);
        changed = true;
    }
    settings_row_done(s);
    // The font combo (220 wide), 8 px, then the size as a bare number.
    settings_row(s, "Terminal font", nullptr, px(220) + px(8) + px(kStepperW), px(kStepperH));
    if (font_picker(s, "##tfont", s.settings.terminal_font, "Default (JetBrains Mono)", px(220))) {
        apply_terminal_font(s);
        changed = true;
    }
    ImGui::SameLine(0, px(8));
    if (stepper(s, "tsize", s.settings.font_size, 1.0f, 8.0f, 32.0f, "%.0f"))
        changed = true;
    settings_row_done(s);
    // 100 to 900 in steps of 100; the family's nearest face is used (FontFamily::file_for).
    settings_row(s, "Terminal font weight", "Bold text stays at 700.", px(kStepperW), px(kStepperH));
    float weight = static_cast<float>(s.settings.terminal_weight);
    if (stepper(s, "tweight", weight, 100.0f, 100.0f, 900.0f, "%.0f")) {
        s.settings.terminal_weight = static_cast<int>(weight);
        apply_terminal_font(s);
        changed = true;
    }
    settings_row_done(s);
    settings_row(s, "Terminal line height", nullptr, px(kStepperW), px(kStepperH));
    changed |= stepper(s, "lh", s.settings.line_height, 0.1f, 0.8f, 2.0f, "%.0f%%", 100.0f);
    settings_row_done(s);
    settings_row(s, "Terminal cell width", nullptr, px(kStepperW), px(kStepperH));
    changed |= stepper(s, "cw", s.settings.cell_width, 0.05f, 0.8f, 2.0f, "%.0f%%", 100.0f);
    settings_row_done(s);
    settings_row(s, "Terminal scroll speed", nullptr, px(kStepperW), px(kStepperH));
    changed |= stepper(s, "ss", s.settings.scroll_speed, 0.1f, 0.1f, 5.0f, "%.0f%%", 100.0f);
    settings_row_done(s);
    settings_row(s, "Interface font", nullptr, px(220), ImGui::GetFrameHeight());
    if (font_picker(s, "##uifont", s.settings.ui_font, "Default (Geist)", px(220))) {
        apply_ui_font(s);
        changed = true;
    }
    settings_row_done(s);
    return changed;
}

// The shell, copy on select, keys.json (and what it could not use) and the worktrees folder.
bool look_terminals(App::State& s) {
    bool changed = false;
    settings_section(s, "Terminals");
    {
        const auto profiles = s.host->profiles();
        const std::string cur = pick_shell(s.settings.shell_profile, profiles);
        std::string label = cur.empty() ? std::string("(none found)") : cur;
        for (const auto& [id, pname] : profiles)
            if (id == cur)
                label = pname;
        settings_row(s, "Shell", "New spaces, tabs and splits open it in the focused folder.", px(220),
                     ImGui::GetFrameHeight());
        if (ImGui::BeginCombo("##shell", label.c_str())) {
            for (const auto& [id, pname] : profiles)
                if (ImGui::Selectable(pname.c_str(), id == cur)) {
                    s.settings.shell_profile = id;
                    changed = true;
                }
            ImGui::EndCombo();
        }
        settings_row_done(s);
    }
    settings_row(s, "Copy on select", "Copy the selection when the mouse is released.", px(36), px(20));
    if (settings_toggle("##cos", s.settings.copy_on_select)) {
        s.settings.copy_on_select = !s.settings.copy_on_select;
        changed = true;
    }
    settings_row_done(s);
    settings_row(s, "Keys", "keys.json: change any shortcut.", settings_button_size(s, "Edit keys.json").x);
    if (settings_button(s, "##keys", "Edit keys.json"))
        run_command(s, "app.keys");
    settings_row_done(s);
    for (const auto& e : s.keymap_errors) {
        const std::string msg = "keys.json: cannot use \"" + e + "\"";
        settings_wrapped(s, msg.c_str(), g_pal->needs_you);
    }
    static char buf[512];
    if (ImGui::IsWindowAppearing() || buf[0] == '\0')
        std::snprintf(buf, sizeof buf, "%s", s.settings.worktree_dir.c_str());
    // The default as this system writes it (home folder, its own separator).
    static const std::string default_note = [] {
        const auto u8 = (os::home_dir() / ".agentdesk" / "worktrees").u8string();
        return "Empty: " + std::string(u8.begin(), u8.end());
    }();
    settings_row(s, "Worktrees folder", default_note.c_str(), px(260), ImGui::GetFrameHeight());
    if (ImGui::InputText("##wt", buf, sizeof buf)) {
        s.settings.worktree_dir = buf;
        changed = true;
    }
    settings_row_done(s);
    return changed;
}

// Fun layouts: Classic and Garden join the cards and the Ctrl+Shift+L cycle.
bool look_fun_layouts_row(App::State& s) {
    settings_row(s, "Fun layouts", "Show the playful layouts (AgentDesk Classic, Garden) with the others.", px(36),
                 px(20));
    const bool hit = settings_toggle("##fun-layouts", s.settings.styles.fun_layouts);
    if (hit)
        s.settings.styles.fun_layouts = !s.settings.styles.fun_layouts;
    settings_row_done(s);
    return hit;
}

bool settings_look(App::State& s) {
    settings_section(s, "Layout");
    look_layout_cards(s);
    bool changed = look_style_options(s);  // the shown style's own rows, if any
    changed |= look_tips_row(s);
    changed |= look_fun_layouts_row(s);
    settings_gap(px(18));
    changed |= look_theme(s);
    settings_gap(px(18));
    changed |= look_size_and_fonts(s);
    settings_gap(px(18));
    changed |= look_terminals(s);
    return changed;
}

// ------------------------------------------------------------------ Machines page
// The machine being renamed or removed (both ask inline first), and the last outcome. Kept
// while the dialog is open; cleared when it opens.
struct MachineEdit {
    std::string machine;  // the row being edited
    std::string action;   // "rename" or "rm"
    char rename_buf[128] = {};
    bool focus_rename = false;
    std::string notice;
};

// What a row asked for this frame.
struct MachineChange {
    int remove = -1;
    int rename = -1;
    std::string new_name;
};

// The inline rename field under a machine row: a label, then a 32 px input (rounded 7, accent
// border while focused) with Cancel and Save. `y` is the row's running bottom.
void machine_rename_field(App::State& s, const MachineConfig& m, int index, MachineEdit& edit,
                          MachineChange& change, ImDrawList* d, float x, float row_w, float button_h,
                          float& y) {
    y += px(6);  // 6 px under the label
    ImGui::SetCursorScreenPos(ImVec2(x, y));
    const float buttons = settings_button_size(s, "Cancel").x + px(6) + settings_button_size(s, "Save").x;
    ImGui::SetNextItemWidth(std::max(px(120), row_w - buttons - px(6)));
    if (std::exchange(edit.focus_rename, false))
        ImGui::SetKeyboardFocusHere();
    ImGui::PushFont(s.fonts.ui, 13);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(px(10), (px(32) - ImGui::GetFontSize()) * 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, px(7));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, v4(g_pal->bg));
    const bool enter =
        ImGui::InputText("##rename", edit.rename_buf, sizeof edit.rename_buf,
                         ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    ImGui::PopFont();
    d->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
               ImGui::IsItemActive() ? g_pal->focus : g_pal->border, px(7));
    const float iy = ImGui::GetItemRectMin().y;
    ImGui::SameLine(0, px(6));
    ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, iy + (px(32) - button_h) * 0.5f));
    if (settings_button(s, "##rn-cancel", "Cancel"))
        edit.machine.clear();
    ImGui::SameLine(0, px(6));
    if (settings_button(s, "##rn-save", "Save") || enter) {
        const std::string name = trimmed(edit.rename_buf);
        const bool taken = std::any_of(s.settings.machines.begin(), s.settings.machines.end(),
                                       [&](const MachineConfig& o) { return o.name == name; });
        if (!name.empty() && name != m.name && !taken) {
            change.rename = index;
            change.new_name = name;
        } else if (name == m.name || name.empty()) {
            edit.machine.clear();
        }
    }
    y = iy + px(32);
}

// One machine: 12 px above and below, a bottom border. The head line has a 7 px dot (green
// connected, the working colour while it connects, else blocked), the name (14 px) over
// "target · state · agent-host version" (12.5 px dim, the version only once known), then Rename
// and Remove. Under it, when asked: the remove question or the rename field; and the last
// connection error.
void machine_row(App::State& s, int index, MachineEdit& edit, MachineChange& change) {
    const auto& m = s.settings.machines[static_cast<std::size_t>(index)];
    const std::string state = s.machine_state(m.name);
    const ImU32 dot = state == "connected"    ? g_pal->done
                      : state == "connecting" ? g_pal->working
                                              : g_pal->needs_you;
    const std::string version = state == "connected" ? s.host_for(m.name)->host_version() : std::string();
    const std::string sub = m.target + " \xC2\xB7 " + state +
                            (version.empty() ? std::string() : " \xC2\xB7 agent-host " + version);
    const ImVec2 a = ImGui::GetCursorScreenPos();
    const float rw = ImGui::GetContentRegionAvail().x;
    ImDrawList* d = ImGui::GetWindowDrawList();
    const ImVec2 bsz_rename = settings_button_size(s, "Rename");
    const ImVec2 bsz_remove = settings_button_size(s, "Remove");
    const float text_h = line_h(14) + px(3) + line_h(12.5f);
    const float head_h = std::max(text_h, bsz_rename.y);
    const float cy = a.y + px(12) + head_h * 0.5f;
    d->AddCircleFilled(ImVec2(a.x + px(3.5f), cy), px(3.5f), dot);
    const float tx = a.x + px(7) + px(10);
    const float bx = a.x + rw - bsz_remove.x - px(6) - bsz_rename.x;
    const float ty = cy - text_h * 0.5f;
    text_clipped(d, s.fonts.ui, fpx(14), ImVec2(tx, ty), g_pal->text, m.name, bx - px(10) - tx);
    text_clipped(d, s.fonts.ui, fpx(12.5f), ImVec2(tx, ty + line_h(14) + px(3)), g_pal->faint, sub,
                 bx - px(10) - tx);
    ImGui::SetCursorScreenPos(ImVec2(bx, cy - bsz_rename.y * 0.5f));
    if (settings_button(s, "##rename-btn", "Rename")) {
        edit.machine = m.name;
        edit.action = "rename";
        std::snprintf(edit.rename_buf, sizeof edit.rename_buf, "%s", m.name.c_str());
        edit.notice.clear();
        edit.focus_rename = true;
    }
    ImGui::SameLine(0, px(6));
    if (settings_button(s, "##remove-btn", "Remove")) {
        edit.machine = m.name;
        edit.action = "rm";
        edit.notice.clear();
    }
    float y = a.y + px(12) + head_h;
    auto line = [&](const std::string& text, ImU32 color, float size) {
        y += px(8);
        d->AddText(s.fonts.ui, fpx(size), ImVec2(a.x, y), color, text.c_str());
        y += line_h(size);
    };
    if (edit.machine == m.name && edit.action == "rm") {
        line("Remove " + m.name + "? Its terminals keep running there.", g_pal->dim, 12.5f);
        y += px(8);
        ImGui::SetCursorScreenPos(ImVec2(a.x, y));
        if (settings_button(s, "##rm-cancel", "Cancel"))
            edit.machine.clear();
        ImGui::SameLine(0, px(6));
        if (settings_button(s, "##rm-confirm", "Remove machine"))
            change.remove = index;
        y += bsz_rename.y;
    } else if (edit.machine == m.name && edit.action == "rename") {
        line("New name (addresses using the old name stop working)", g_pal->faint, 12);
        machine_rename_field(s, m, index, edit, change, d, a.x, rw, bsz_rename.y, y);
    }
    if (state != "connected")
        for (const auto& r : s.remotes)
            if (r.cfg.name == m.name && !r.host->last_error().empty())
                line(r.host->last_error(), g_pal->needs_you, 12.5f);
    y += px(12);
    d->AddLine(ImVec2(a.x, y - 0.5f), ImVec2(a.x + rw, y - 0.5f), g_pal->border);
    ImGui::SetCursorScreenPos(ImVec2(a.x, y));
    ImGui::Dummy(ImVec2(0, 0));
}

// Applies a remove or rename asked for this frame. A rename also moves the spaces and the folded
// state that name the machine. Returns true when ui.json changed.
bool apply_machine_change(App::State& s, const MachineChange& change, MachineEdit& edit) {
    bool changed = false;
    if (change.remove >= 0) {
        edit.notice = "Removed " + s.settings.machines[static_cast<std::size_t>(change.remove)].name +
                      "; its terminals keep running there.";
        s.settings.machines.erase(s.settings.machines.begin() + change.remove);
        edit.machine.clear();
        changed = true;
        start_remotes(s);
    }
    if (change.rename >= 0) {
        auto& m = s.settings.machines[static_cast<std::size_t>(change.rename)];
        const std::string old = m.name;
        m.name = change.new_name;
        for (auto& sp : s.ws.spaces)
            if (sp.machine == old)
                sp.machine = change.new_name;
        for (auto& f : s.settings.folded_machines)
            if (f == old)
                f = change.new_name;
        edit.notice = "Renamed " + old + " to " + change.new_name + ".";
        edit.machine.clear();
        s.doc_dirty = true;
        s.doc_dirty_at = Clock::now();
        changed = true;
        start_remotes(s);
    }
    return changed;
}

// A note, "This computer" with a green dot, a row per machine, then "Add a machine".
bool settings_machines(App::State& s) {
    settings_section(s, "Machines");
    settings_wrapped(s, "Your computers, reached over ssh. Their terminals keep running when you disconnect.",
                     g_pal->faint);
    settings_gap(px(8));
    {
        const std::string v = s.host->host_version();
        const std::string note = "Connected \xC2\xB7 " +
                                 (v.empty() ? std::string("agent-host version unknown") : "agent-host " + v);
        settings_row(s, "This computer", note.c_str(), px(7), px(7));
        const ImVec2 dp = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(dp.x + px(3.5f), dp.y + px(3.5f)), px(3.5f),
                                                    g_pal->done);
        settings_row_done(s);
    }
    static MachineEdit edit;
    if (ImGui::IsWindowAppearing()) {
        edit.machine.clear();
        edit.notice.clear();
    }
    MachineChange change;
    for (int i = 0; i < static_cast<int>(s.settings.machines.size()); ++i) {
        ImGui::PushID(i);
        machine_row(s, i, edit, change);
        ImGui::PopID();
    }
    if (s.settings.machines.empty()) {
        settings_gap(px(14));
        settings_wrapped(s, "No other machines yet.", g_pal->faint, 12.5f, 12.5f * 1.3f);
        settings_gap(px(14));
    }
    if (!edit.notice.empty()) {
        settings_gap(px(10));
        settings_wrapped(s, edit.notice.c_str(), g_pal->done, 12.5f, 12.5f * 1.3f);
        settings_gap(px(10));
    }
    const bool changed = apply_machine_change(s, change, edit);
    settings_gap(px(16));
    if (settings_button(s, "##st-m-add", "Add a machine"))
        s.show_machine_add = true;
    return changed;
}

// ------------------------------------------------------------------ Alerts page
// Sounds and the corner notes, each with its own switch.
bool settings_alerts(App::State& s) {
    bool changed = false;
    settings_section(s, "Sounds and notes");
    settings_row(s, "Play sounds",
                 "When an agent needs you (always), and when one finishes while you're not looking.", px(36),
                 px(20));
    if (settings_toggle("##st-sounds", s.settings.sounds)) {
        s.settings.sounds = !s.settings.sounds;
        changed = true;
    }
    settings_row_done(s);
    settings_row(s, "Show notes",
                 "A note in the corner when an agent you're not looking at needs you or finishes; click it to "
                 "go there.",
                 px(36), px(20));
    if (settings_toggle("##st-toasts", s.settings.toasts)) {
        s.settings.toasts = !s.settings.toasts;
        changed = true;
    }
    settings_row_done(s);
    // Each sound by its file's name (not its path), with a Play button.
    auto sound_row = [&](const char* label, const std::string& file, bool blocked) {
        const auto slash = file.find_last_of("\\/");
        const std::string name = slash == std::string::npos ? file : file.substr(slash + 1);
        settings_row(s, label, file.empty() ? "AgentDesk's sound" : name.c_str(),
                     settings_button_size(s, "Play").x);
        if (settings_button(s, blocked ? "##st-play-blocked" : "##st-play-done", "Play"))
            play_alert(s, blocked);
        settings_row_done(s);
    };
    sound_row("An agent needs you", s.settings.sound_blocked, true);
    sound_row("An agent finished", s.settings.sound_done, false);
    // Focus mode, with the window in front: the window's edge glows instead of the note.
    settings_row(s, "In focus mode",
                 "With the window in front, the window's edge glows instead of a note.",
                 px(240), px(32));
    if (segmented(s, "##st-focus-alerts", s.settings.focus_alerts,
                  {{"edge", "Edge"}, {"edge_sound", "Edge + sound"}, {"normal", "Usual"}}))
        changed = true;
    settings_row_done(s);
    settings_footnote(
        s,
        "Want your own sounds? Ask your agent to set sound_done and sound_blocked in ui.json to "
        "an MP3.");
    return changed;
}

// ------------------------------------------------------------------ Mods page
// A note on what mods are, a row with a switch per mod (its error under it), then Reload and the
// folder.
bool settings_mods(App::State& s) {
    bool changed = false;
    settings_section(s, "Installed");
    settings_wrapped(s,
                     "Mods are C++ DLLs in the mods folder (one folder per mod, mod.dll). They can react to "
                     "agents, add commands, panels and tab text, and start programs. A mod runs with your "
                     "rights like any program you install, so only turn on mods you trust; one that crashes "
                     "is turned off. The sdk folder of the zip has the headers and the examples' sources.",
                     g_pal->faint);
    settings_gap(px(8));
    for (const auto& m : s.mods->mods()) {
        ImGui::PushID(m.name.c_str());
        const bool on = s.settings.mods_enabled.contains(m.name);
        settings_row(s, m.name.c_str(), m.description.c_str(), px(36), px(20));
        if (settings_toggle("##on", on)) {
            if (!on) {
                s.mod_confirm = m.name;  // turning on asks first, below the row
            } else {
                s.settings.mods_enabled.erase(m.name);
                changed = true;
                s.mods->load(s.settings.mods_enabled);
            }
        }
        settings_row_done(s);
        if (!on && s.mod_confirm == m.name) {
            // A mod is code from someone else running as the person: say what that means before it
            // starts, every time one is turned on.
            settings_gap(px(6));
            settings_wrapped(s,
                             ("Turn on " + m.name +
                              "? It will run inside AgentDesk with your user account's rights: it can read and "
                              "change your files, start programs and type into your terminals. Turn it on only "
                              "if you trust where it came from. A mod that hangs freezes this window until you end "
                              "agent-ui; your terminals keep running.")
                                 .c_str(),
                             g_pal->needs_you);
            settings_gap(px(8));
            if (settings_button(s, "##mod-turn-on", "Turn on")) {
                s.settings.mods_enabled.insert(m.name);
                s.mod_confirm.clear();
                changed = true;
                s.mods->load(s.settings.mods_enabled);
            }
            ImGui::SameLine(0, px(8));
            if (settings_button(s, "##mod-cancel", "Cancel"))
                s.mod_confirm.clear();
        }
        if (!m.error.empty()) {
            settings_gap(px(6));
            settings_wrapped(s, m.error.c_str(), g_pal->needs_you);
        }
        ImGui::PopID();
    }
    if (s.mods->mods().empty()) {
        settings_gap(px(14));
        settings_wrapped(s, "No mods yet.", g_pal->faint, 12.5f, 12.5f * 1.3f);
    }
    settings_gap(px(16));
    if (settings_button(s, "##mods-reload", "Reload"))
        run_command(s, "mods.reload");
    ImGui::SameLine(0, px(8));
    if (settings_button(s, "##mods-folder", "Open mods folder"))
        run_command(s, "mods.folder");
    return changed;
}

}  // namespace

void draw_settings(App::State& s) {
    if (s.show_settings) {
        ImGui::OpenPopup("##settings");
        s.show_settings = false;
        if (!s.settings_tab.empty())
            s.settings_section = s.settings_tab;
        s.settings_tab.clear();
    }
    const ImVec2 size = dialog_size(px(kDialogW), px(kDialogH));
    center_next_window(size);
    push_overlay_style();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    const bool open = ImGui::BeginPopupModal(
        "##settings", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
    ImGui::PopStyleVar();
    pop_overlay_style();
    if (!open)
        return;
    bool close = draw_settings_nav(s, size.y);
    // The page scrolls beside the navigation, 28 px in from the sides and 22 from the top; it
    // has no title of its own and starts with its first heading. Esc closes the dialog.
    const ImVec2 wp = ImGui::GetWindowPos();
    const float nav_w = px(kNavW);
    ImGui::SetCursorScreenPos(ImVec2(wp.x + nav_w + 1, wp.y));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(28), px(22)));
    ImGui::BeginChild("##settings-body", ImVec2(size.x - nav_w - 1, size.y),
                      ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoBackground);
    ImGui::PopStyleVar();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(px(8), 0));
    bool changed = false;
    const std::string& sec = s.settings_section;
    if (sec == "Look")
        changed = settings_look(s);
    else if (sec == "Keyboard")
        changed = settings_keyboard(s);
    else if (sec == "Machines")
        changed = settings_machines(s);
    else if (sec == "Alerts")
        changed = settings_alerts(s);
    else if (sec == "Mods")
        changed = settings_mods(s);
    else if (sec == "Updates")
        close = settings_updates(s) || close;
    else if (sec == "Privacy")
        changed = settings_privacy(s);
    ImGui::PopStyleVar();  // ItemSpacing
    ImGui::EndChild();
    if (changed)
        save_settings(s.settings);
    if (close || std::exchange(s.layout_tips.close_settings, false) || ImGui::IsKeyPressed(ImGuiKey_Escape))
        ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}
}  // namespace detail

}  // namespace agentdesk::desktop
