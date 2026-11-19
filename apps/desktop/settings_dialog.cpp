// The settings dialog.
#include "win98_internal.hpp"

namespace agentdesk::desktop {
namespace detail {

void draw_settings(App::State& s) {
    if (s.show_settings) {
        ImGui::OpenPopup("##settings");
        s.show_settings = false;
        if (!s.settings_tab.empty())
            s.settings_section = s.settings_tab;
        s.settings_tab.clear();
    }
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float w = std::min(px(880), vp->WorkSize.x - px(16));  // window.rs 3594: width 880
    const float h = std::min(px(620), vp->WorkSize.y - px(16));
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + (vp->WorkSize.x - w) * 0.5f, vp->WorkPos.y + (vp->WorkSize.y - h) * 0.5f));
    ImGui::SetNextWindowSize(ImVec2(w, h));
    push_overlay_style();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    const bool open = ImGui::BeginPopupModal("##settings", nullptr,
                                             ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
    ImGui::PopStyleVar();
    pop_overlay_style();
    if (!open)
        return;
    bool changed = false;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 wp = ImGui::GetWindowPos();
    // Left navigation (190 px): heading, sections.
    const float nav_w = px(190);
    dl->AddRectFilled(wp, ImVec2(wp.x + nav_w, wp.y + h), g_pal->sidebar, px(8), ImDrawFlags_RoundCornersLeft);
    dl->AddLine(ImVec2(wp.x + nav_w, wp.y), ImVec2(wp.x + nav_w, wp.y + h), g_pal->border);
    // settings/main.js 897-899: nav p 10; "Settings" px 10, pt 6, pb 12, 16 px semibold.
    dl->AddText(s.fonts.ui_bold, fpx(16), ImVec2(wp.x + px(10) + px(10), wp.y + px(10) + px(6)), g_pal->text, "Settings");
    struct Section {
        const char* name;
        const char* glyph;
    };
    // main.js 9-16: palette, server, bell, refresh, shield (Mods is ours: C++ DLL mods).
    static constexpr Section kSections[] = {{"Look", ui::icon::kPalette},  {"Machines", ui::icon::kServer},
                                            {"Alerts", ui::icon::kBell},    {"Mods", ui::icon::kExtensions},
                                            {"Updates", ui::icon::kRefresh}, {"Privacy", ui::icon::kShield}};
    // main.js 888-889: something waits to be installed.
    const auto upd = ui::updates::status();
    const bool waiting = upd.app_ready.has_value() || (upd.core.ready.has_value() && !upd.core.managed);
    if (s.settings_section.empty())
        s.settings_section = "Look";
    bool close = false;
    // main.js 900-918: items px 10, py 7, gap 10, rounded 7, gap 2 between; a 15 px icon (accent
    // when chosen, else dim), 13.5 px text (foreground when chosen, else muted); raised when
    // chosen or hovered.
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
        draw_icon(s, dl, sec.glyph, fpx(15), ImVec2(a.x + px(10) + px(7.5f), cy), on ? g_pal->focus : g_pal->faint);
        const ImVec2 t = s.fonts.ui->CalcTextSizeA(fpx(13.5f), FLT_MAX, 0, sec.name);
        dl->AddText(s.fonts.ui, fpx(13.5f), ImVec2(a.x + px(10) + px(15) + px(10), cy - t.y * 0.5f),
                    on ? g_pal->text : g_pal->dim, sec.name);
        // main.js 917: a 7 px accent dot at the row's end.
        if (std::string_view(sec.name) == "Updates" && waiting)
            dl->AddCircleFilled(ImVec2(a.x + item_w - px(10) - px(3.5f), cy), px(3.5f), g_pal->focus);
        ny += item_h + px(2);
    }
    // main.js 967: "Send feedback", a settings button at the bottom of the nav; it closes
    // Settings first, then opens the feedback window.
    {
        const ImVec2 bs = settings_button_size(s, "Send feedback");
        ImGui::SetCursorScreenPos(ImVec2(wp.x + px(10), wp.y + h - px(10) - bs.y));
        if (settings_button(s, "##st-feedback", "Send feedback")) {
            close = true;
            run_command(s, "app.feedback");
        }
    }
    // Content.
    ImGui::SetCursorScreenPos(ImVec2(wp.x + nav_w + 1, wp.y));
    // main.js 949-990: no title and no Esc in the body (Esc still closes); padding px 28, py 22
    // inside the scroll area; the page starts with its first heading.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(28), px(22)));
    ImGui::BeginChild("##settings-body", ImVec2(w - nav_w - 1, h), ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoBackground);
    ImGui::PopStyleVar();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(px(8), 0));
    const std::string& sec = s.settings_section;
    // Cards that wrap (flex_wrap, gap 8): the next card's place.
    struct CardFlow {
        float x0, right, x, y, row_h = 0;
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
        void end() const {
            ImGui::SetCursorScreenPos(ImVec2(x0, y + row_h));
            ImGui::Dummy(ImVec2(0, 0));
        }
    };
    auto flow = [] {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        return CardFlow{p.x, p.x + ImGui::GetContentRegionAvail().x, p.x, p.y};
    };
    if (sec == "Look") {
        settings_section(s, "Layout");
        // herdr frame/settings/main.js 272-320: cards 124 wide, p 6, rounded 10, a 2 px accent
        // border and raised fill when chosen (raised on hover), a 62 px preview, then the name
        // (12.5, muted; foreground and a check when chosen). Shipped ones first.
        CardFlow lf = flow();
        for (const auto& l : s.layouts) {
            const bool on = l.name == s.settings.layout;
            const float cw = px(124), pad = px(6), ph = px(62), lh = line_h(12.5f);
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
            // herdr settings/main.js 86-99, 302-309: a layout with a "preview" picture shows it
            // (win98.json "preview": layouts/win98.svg), rounded 6 with a 1 px border.
            if (l.style == "win98")
                w98::preview(s, d, ImVec2(a.x + pad, a.y + pad), ImVec2(cw - pad * 2, ph), px(6), g_pal->border);
            else
                layout_preview(d, l.name, ImVec2(a.x + pad, a.y + pad), ImVec2(cw - pad * 2, ph));
            // herdr LOOKS: main -> "Herdr" (ours is AgentDesk), terminals -> "Terminals",
            // win98 -> "Herdr 98" (settings/main.js 18).
            std::string label = l.name == "workspace" ? std::string("AgentDesk")
                                : l.name == "terminals" ? std::string("Terminals")
                                : l.name == "win98"     ? std::string("AgentDesk 98")
                                                        : l.name;
            const ImVec2 lp(a.x + pad, a.y + pad + ph + px(6));
            const float tw = text_clipped(d, s.fonts.ui, fpx(12.5f), lp, on ? g_pal->text : g_pal->dim, label,
                                          cw - pad * 2 - (on ? px(17) : 0.0f));
            if (on)
                draw_icon(s, d, ui::icon::kCheck, fpx(12), ImVec2(lp.x + tw + px(5) + px(6), lp.y + fpx(12.5f) * 0.55f),
                          g_pal->focus);
        }
        lf.end();
        settings_gap(px(18));  // main.js 520
        settings_section(s, "Theme");
        settings_row(s, "Appearance", "Light or dark, or as your system is now.", px(240), px(32));
        if (segmented(s, "##appearance", s.settings.appearance, {{"system", "System"}, {"light", "Light"}, {"dark", "Dark"}}))
            changed = true;
        settings_row_done(s);
        // main.js 523-527: the note (pt 12, pb 8, 12.5 dim), "Dark" (pb 6, 12.5 medium muted),
        // the dark cards, "Light" (pt 10, pb 6), the light cards.
        settings_gap(px(12));
        settings_wrapped(s, "For the layout you're in: each layout keeps its own light and dark theme.", g_pal->faint);
        settings_gap(px(8));
        for (const bool light_cards : {false, true}) {
            if (light_cards)
                settings_gap(px(10));
            settings_wrapped(s, light_cards ? "Light" : "Dark", g_pal->dim, 12.5f, 12.5f * 1.3f, s.fonts.ui_medium);
            settings_gap(px(6));
            // main.js 353-405 themePicker(): cards 124 wide, gap 6, p 6, rounded 10, a 2 px
            // border (accent when chosen), raised on hover; a 46 px preview (p 7, gap 6, rounded
            // 6, 1 px border, the theme's background) with a 44 x 4 bar in its terminal
            // foreground and 8 px dots (gap 4) in accent, ansi1, ansi2, ansi3, ansi5; under it
            // the name (12.5 px, foreground when chosen, else muted) and a 12 px accent check.
            CardFlow tf = flow();
            for (const Palette* pal : all_palettes()) {
                const Palette& p = *pal;
                if (p.light != light_cards || p.layout_only)
                    continue;
                const auto shown = layout_theme_pair(s.settings);
                const bool on = (light_cards ? shown.second : shown.first) == p.id;
                const float cw = px(124), pad = px(6), ph = px(46);
                const float ch = pad + ph + px(6) + line_h(12.5f) + pad;
                const ImVec2 a = tf.next(cw, ch);
                ImGui::SetCursorScreenPos(a);
                ImGui::PushID(p.id);
                if (ImGui::InvisibleButton("##theme", ImVec2(cw, ch))) {
                    run_command(s, std::string("theme:") + p.id);
                    changed = true;
                }
                const bool hov = ImGui::IsItemHovered();
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
                d->AddRectFilled(ImVec2(pa.x + px(7), pa.y + px(7)), ImVec2(pa.x + px(7) + px(44), pa.y + px(11)), p.term_fg,
                                 px(2));
                // The theme's ansi table, else libvterm's defaults (what its terminals show).
                const auto ansi = [&](int i) {
                    static constexpr ImU32 kVterm[] = {IM_COL32(0, 0, 0, 255),     IM_COL32(224, 0, 0, 255),
                                                       IM_COL32(0, 224, 0, 255),   IM_COL32(224, 224, 0, 255),
                                                       IM_COL32(0, 0, 224, 255),   IM_COL32(224, 0, 224, 255)};
                    return p.ansi != nullptr ? (*p.ansi)[static_cast<std::size_t>(i)] : kVterm[i];
                };
                const ImU32 dots[] = {p.focus, ansi(1), ansi(2), ansi(3), ansi(5)};
                float dx = pa.x + px(7);
                const float dy = pa.y + px(7) + px(4) + px(6);
                for (const ImU32 c : dots) {
                    d->AddCircleFilled(ImVec2(dx + px(4), dy + px(4)), px(4), c);
                    dx += px(8) + px(4);
                }
                const ImVec2 lp(a.x + pad, pb.y + px(6));
                const float tw = text_clipped(d, s.fonts.ui, fpx(12.5f), lp, on ? g_pal->text : g_pal->dim, p.label,
                                              cw - pad * 2 - (on ? px(17) : 0.0f));
                if (on)
                    draw_icon(s, d, ui::icon::kCheck, fpx(12), ImVec2(lp.x + tw + px(5) + px(6), lp.y + line_h(12.5f) * 0.5f),
                              g_pal->focus);
            }
            tf.end();
        }
        settings_gap(px(18));  // main.js 530
        settings_section(s, "Size and fonts");
        // main.js 531-543: the Zoom stepper and a Reset button.
        settings_row(s, "Zoom", "Everything: terminals, panels, text.",
                     px(kStepperW) + px(8) + settings_button_size(s, "Reset").x, px(kStepperH));
        if (stepper(s, "zoom", s.settings.zoom, 0.1f, 0.5f, 3.0f, "%.0f%%", 100.0f)) {
            g_scale = s.dpi * ui_scale(s.settings);
            apply_style(s.dpi, ui_scale(s.settings));
            changed = true;
        }
        ImGui::SameLine(0, px(8));
        if (settings_button(s, "##st-zoom-reset", "Reset"))
            run_command(s, "app.zoom_reset");
        settings_row_done(s);
        settings_row(s, "Interface text", "Sidebar, tabs, menus and panels, at 100% zoom.", px(kStepperW), px(kStepperH));
        if (stepper(s, "uitext", s.settings.ui_text, 1.0f, 11.0f, 24.0f, "%.0f px")) {
            g_scale = s.dpi * ui_scale(s.settings);
            apply_style(s.dpi, ui_scale(s.settings));
            changed = true;
        }
        settings_row_done(s);
        {
            // main.js 500-512 fontRow(): the font button (220 wide), gap 8, the size stepper
            // with the bare number (`${Math.round(sizeNow)}`, 437 / 511).
            settings_row(s, "Terminal font", nullptr, px(220) + px(8) + px(kStepperW), px(kStepperH));
            if (font_picker(s, "##tfont", s.settings.terminal_font, "Default (JetBrains Mono)", px(220))) {
                apply_terminal_font(s);
                changed = true;
            }
            ImGui::SameLine(0, px(8));
            if (stepper(s, "tsize", s.settings.font_size, 1.0f, 8.0f, 32.0f, "%.0f"))
                changed = true;
            settings_row_done(s);
            // herdr settings main.js:546-548: a 100-900 stepper (step 100); the family's nearest
            // face is used (FontFamily::file_for).
            settings_row(s, "Terminal font weight", "Bold text stays at 700.", px(kStepperW), px(kStepperH));
            float weight = static_cast<float>(s.settings.terminal_weight);
            if (stepper(s, "tweight", weight, 100.0f, 100.0f, 900.0f, "%.0f")) {
                s.settings.terminal_weight = static_cast<int>(weight);
                apply_terminal_font(s);
                changed = true;
            }
            settings_row_done(s);
        }
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
        settings_gap(px(18));
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
        // On and off as herdr's settings switch it (main.js 225-238 toggle()).
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
        {
            static char buf[512];
            if (ImGui::IsWindowAppearing() || buf[0] == '\0')
                std::snprintf(buf, sizeof buf, "%s", s.settings.worktree_dir.c_str());
            settings_row(s, "Worktrees folder", "Empty: %USERPROFILE%\\.agentdesk\\worktrees", px(260),
                         ImGui::GetFrameHeight());
            if (ImGui::InputText("##wt", buf, sizeof buf)) {
                s.settings.worktree_dir = buf;
                changed = true;
            }
            settings_row_done(s);
        }
    } else if (sec == "Machines") {
        // main.js 725-736 machines(): the heading, the note (pb 8), a "This computer" row with a
        // green 7 px dot, a row per machine, then (pt 16) an "Add a machine" button.
        settings_section(s, "Machines");
        settings_wrapped(s, "Your computers, reached over ssh. Their terminals keep running when you disconnect.",
                         g_pal->faint);
        settings_gap(px(8));
        {
            // main.js 730: "Connected · hc <version>" (ours: agent-host).
            const std::string v = s.host->host_version();
            const std::string note = "Connected \xC2\xB7 " + (v.empty() ? std::string("agent-host version unknown")
                                                                         : "agent-host " + v);
            settings_row(s, "This computer", note.c_str(), px(7), px(7));
            const ImVec2 dp = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(dp.x + px(3.5f), dp.y + px(3.5f)), px(3.5f), g_pal->done);
            settings_row_done(s);
        }
        // herdr settings main.js machineRow(): Rename and Remove; both ask inline first.
        static std::string edit_machine;   // the row being edited
        static std::string edit_action;    // "rename" or "rm"
        static char rename_buf[128] = {};
        static bool focus_rename = false;
        static std::string machine_notice;
        if (ImGui::IsWindowAppearing()) {
            edit_machine.clear();
            machine_notice.clear();
        }
        int remove = -1;
        int rename = -1;
        std::string new_name;
        for (int i = 0; i < static_cast<int>(s.settings.machines.size()); ++i) {
            const auto& m = s.settings.machines[static_cast<std::size_t>(i)];
            ImGui::PushID(i);
            // main.js 637-662: py 12, gap 8, a bottom border; the head row (gap 10): a 7 px dot
            // (green connected, working while it connects, else blocked), the name 14 px over
            // "target · state · version" 12.5 px dim, then Rename and Remove (gap 6). The version
            // is left out while it is not known.
            const std::string state = s.machine_state(m.name);
            const ImU32 dot = state == "connected"    ? g_pal->done
                              : state == "connecting" ? g_pal->working
                                                      : g_pal->needs_you;
            const std::string version = state == "connected" ? s.host_for(m.name)->host_version() : std::string();
            const std::string sub =
                m.target + " \xC2\xB7 " + state + (version.empty() ? std::string() : " \xC2\xB7 agent-host " + version);
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
                edit_machine = m.name;
                edit_action = "rename";
                std::snprintf(rename_buf, sizeof rename_buf, "%s", m.name.c_str());
                machine_notice.clear();
                focus_rename = true;
            }
            ImGui::SameLine(0, px(6));
            if (settings_button(s, "##remove-btn", "Remove")) {
                edit_machine = m.name;
                edit_action = "rm";
                machine_notice.clear();
            }
            float y = a.y + px(12) + head_h;
            auto line = [&](const std::string& text, ImU32 color, float size) {
                y += px(8);
                d->AddText(s.fonts.ui, fpx(size), ImVec2(a.x, y), color, text.c_str());
                y += line_h(size);
            };
            if (edit_machine == m.name && edit_action == "rm") {
                // main.js 676-678: the question (12.5 muted), then Cancel and "Remove machine".
                line("Remove " + m.name + "? Its terminals keep running there.", g_pal->dim, 12.5f);
                y += px(8);
                ImGui::SetCursorScreenPos(ImVec2(a.x, y));
                if (settings_button(s, "##rm-cancel", "Cancel"))
                    edit_machine.clear();
                ImGui::SameLine(0, px(6));
                if (settings_button(s, "##rm-confirm", "Remove machine"))
                    remove = i;
                y += bsz_rename.y;
            } else if (edit_machine == m.name && edit_action == "rename") {
                // main.js 679-683: "New name (...)" 12 px dim, then (gap 6) the input (h 32, px 10,
                // rounded 7, accent border while focused, background, 13 px), Cancel and Save.
                line("New name (addresses using the old name stop working)", g_pal->faint, 12);
                y += px(6);  // gap 6 under the label
                ImGui::SetCursorScreenPos(ImVec2(a.x, y));
                const float buttons = settings_button_size(s, "Cancel").x + px(6) + settings_button_size(s, "Save").x;
                ImGui::SetNextItemWidth(std::max(px(120), rw - buttons - px(6)));
                if (std::exchange(focus_rename, false))
                    ImGui::SetKeyboardFocusHere();
                ImGui::PushFont(s.fonts.ui, 13);
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(px(10), (px(32) - ImGui::GetFontSize()) * 0.5f));
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, px(7));
                ImGui::PushStyleColor(ImGuiCol_FrameBg, v4(g_pal->bg));
                const bool enter = ImGui::InputText("##rename", rename_buf, sizeof rename_buf,
                                                    ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
                ImGui::PopStyleColor();
                ImGui::PopStyleVar(2);
                ImGui::PopFont();
                d->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                           ImGui::IsItemActive() ? g_pal->focus : g_pal->border, px(7));
                const float iy = ImGui::GetItemRectMin().y;
                ImGui::SameLine(0, px(6));
                ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, iy + (px(32) - bsz_rename.y) * 0.5f));
                if (settings_button(s, "##rn-cancel", "Cancel"))
                    edit_machine.clear();
                ImGui::SameLine(0, px(6));
                if (settings_button(s, "##rn-save", "Save") || enter) {
                    const std::string name = trimmed(rename_buf);
                    const bool taken = std::any_of(s.settings.machines.begin(), s.settings.machines.end(),
                                                   [&](const MachineConfig& o) { return o.name == name; });
                    if (!name.empty() && name != m.name && !taken) {
                        rename = i;
                        new_name = name;
                    } else if (name == m.name || name.empty()) {
                        edit_machine.clear();
                    }
                }
                y = iy + px(32);
            }
            // main.js 685: what went wrong, 12.5 px in the blocked colour.
            if (state != "connected")
                for (const auto& r : s.remotes)
                    if (r.cfg.name == m.name && !r.host->last_error().empty())
                        line(r.host->last_error(), g_pal->needs_you, 12.5f);
            y += px(12);
            d->AddLine(ImVec2(a.x, y - 0.5f), ImVec2(a.x + rw, y - 0.5f), g_pal->border);
            ImGui::SetCursorScreenPos(ImVec2(a.x, y));
            ImGui::Dummy(ImVec2(0, 0));
            ImGui::PopID();
        }
        // main.js 732-734: "No other machines yet." (py 14) and the notice (py 10), 12.5 px.
        if (s.settings.machines.empty()) {
            settings_gap(px(14));
            settings_wrapped(s, "No other machines yet.", g_pal->faint, 12.5f, 12.5f * 1.3f);
            settings_gap(px(14));
        }
        if (!machine_notice.empty()) {
            settings_gap(px(10));
            settings_wrapped(s, machine_notice.c_str(), g_pal->done, 12.5f, 12.5f * 1.3f);
            settings_gap(px(10));
        }
        if (remove >= 0) {
            machine_notice = "Removed " + s.settings.machines[static_cast<std::size_t>(remove)].name +
                             "; its terminals keep running there.";
            s.settings.machines.erase(s.settings.machines.begin() + remove);
            edit_machine.clear();
            changed = true;
            start_remotes(s);
        }
        if (rename >= 0) {
            auto& m = s.settings.machines[static_cast<std::size_t>(rename)];
            const std::string old = m.name;
            m.name = new_name;
            for (auto& sp : s.ws.spaces)
                if (sp.machine == old)
                    sp.machine = new_name;
            for (auto& f : s.settings.folded_machines)
                if (f == old)
                    f = new_name;
            machine_notice = "Renamed " + old + " to " + new_name + ".";
            edit_machine.clear();
            s.doc_dirty = true;
            s.doc_dirty_at = Clock::now();
            changed = true;
            start_remotes(s);
        }
        // main.js 736: pt 16, a settings button.
        settings_gap(px(16));
        if (settings_button(s, "##st-m-add", "Add a machine"))
            s.show_machine_add = true;
    } else if (sec == "Alerts") {
        // main.js 743-757 alerts(): Sounds; Play sounds (a toggle); the two sounds with Play; the
        // footnote. No other section: a note shows in the corner either way.
        settings_section(s, "Sounds");
        settings_row(s, "Play sounds",
                     "When an agent needs you, and when one finishes while you're not looking. A note shows in the "
                     "corner either way.",
                     px(36), px(20));
        if (settings_toggle("##st-sounds", s.settings.sounds)) {
            s.settings.sounds = !s.settings.sounds;
            changed = true;
        }
        settings_row_done(s);
        auto sound_row = [&](const char* label, const std::string& file, bool blocked) {
            // herdr settings main.js:743: the file's name, not its path.
            const auto slash = file.find_last_of("\\/");
            const std::string name = slash == std::string::npos ? file : file.substr(slash + 1);
            settings_row(s, label, file.empty() ? "AgentDesk's sound" : name.c_str(), settings_button_size(s, "Play").x);
            if (settings_button(s, blocked ? "##st-play-blocked" : "##st-play-done", "Play"))
                play_alert(s, blocked);
            settings_row_done(s);
        };
        sound_row("An agent needs you", s.settings.sound_blocked, true);
        sound_row("An agent finished", s.settings.sound_done, false);
        // main.js 757, with our file and keys (ui.json sound_done / sound_blocked).
        settings_footnote(s, "Want your own sounds? Ask your agent to set sound_done and sound_blocked in ui.json to "
                             "an MP3.");
    } else if (sec == "Mods") {
        // Ours (C++ DLL mods), laid out like herdr's pages: heading, note, rows with toggles.
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
                if (!on)
                    s.settings.mods_enabled.insert(m.name);
                else
                    s.settings.mods_enabled.erase(m.name);
                changed = true;
                s.mods->load(s.settings.mods_enabled);
            }
            settings_row_done(s);
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
    } else if (sec == "Updates") {
        close = settings_updates(s) || close;
    } else if (sec == "Privacy") {
        changed |= settings_privacy(s);
    }
    ImGui::PopStyleVar();  // ItemSpacing
    ImGui::EndChild();
    if (changed)
        save_settings(s.settings);
    if (close || ImGui::IsKeyPressed(ImGuiKey_Escape))
        ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}
}  // namespace detail

}  // namespace agentdesk::desktop
