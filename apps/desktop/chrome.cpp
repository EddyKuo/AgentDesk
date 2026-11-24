// Shared window-chrome drawing: mod panels, colour helpers, icons and icon buttons, context menus,
// the shell picker, agent status marks, window dragging and the confirm prompt helpers.
// Portions adapted from herdr-app (Apache-2.0) and modified; see THIRD_PARTY_NOTICES.md.
#include "app_internal.hpp"

namespace agentdesk::desktop {
namespace detail {

namespace {
// ------------------------------------------------------------------ mod panels

// A mod names its colours by role; map them onto the palette.
ImU32 mod_color(const std::string& c) {
    if (c == "dim")
        return g_pal->dim;
    if (c == "accent")
        return g_pal->focus;
    if (c == "good")
        return g_pal->done;
    if (c == "warn")
        return g_pal->working;
    if (c == "bad")
        return g_pal->needs_you;
    return g_pal->text;
}

void text_colored(const std::string& text, ImU32 col) {
    ImGui::PushStyleColor(ImGuiCol_Text, v4(col));
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopStyleColor();
}

void draw_mod_widget(App::State& s, const ui::ModPanel& panel, const ui::ModWidget& w, float inset) {
    switch (w.kind) {
        case ui::ModWidget::Kind::kText:
            ImGui::PushStyleColor(ImGuiCol_Text, v4(mod_color(w.color)));
            ImGui::TextWrapped("%s", w.text.c_str());
            ImGui::PopStyleColor();
            break;
        case ui::ModWidget::Kind::kButton:
            if (ImGui::SmallButton(w.text.c_str()))
                s.mods->click(panel.mod, panel.id, w.id);
            break;
        case ui::ModWidget::Kind::kSeparator:
            ImGui::Separator();
            break;
        case ui::ModWidget::Kind::kProgress:
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, v4(g_pal->focus));
            ImGui::ProgressBar(w.value, ImVec2(ImGui::GetContentRegionAvail().x - inset, px(6)), "");
            ImGui::PopStyleColor();
            if (!w.text.empty())
                ImGui::TextDisabled("%s", w.text.c_str());
            break;
        case ui::ModWidget::Kind::kRow: {
            // Label on the left, value right-aligned.
            text_colored(w.text, g_pal->dim);
            const float rw = ImGui::CalcTextSize(w.text2.c_str()).x;
            ImGui::SameLine(ImGui::GetContentRegionMax().x - rw - inset);
            text_colored(w.text2, mod_color(w.color));
            break;
        }
        case ui::ModWidget::Kind::kBadge: {
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const ImVec2 t = ImGui::CalcTextSize(w.text.c_str());
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(p, ImVec2(p.x + t.x + px(12), p.y + t.y + px(4)), alpha(mod_color(w.color), 60),
                              px(8));
            dl->AddText(ImVec2(p.x + px(6), p.y + px(2)), mod_color(w.color), w.text.c_str());
            ImGui::Dummy(ImVec2(t.x + px(12), t.y + px(4)));
            break;
        }
    }
}

}  // namespace

// Mod panels, inset and titled like the sidebar's own section headers.
// only_mod: just that mod's panels (a layout block naming it).
void draw_mod_panels(App::State& s, const std::string& where, const std::string& only_mod) {
    const float inset = px(18);
    for (const auto& panel : s.mods->panels(where)) {
        if (!only_mod.empty() && panel.mod != only_mod)
            continue;
        ImGui::PushID((panel.mod + "/" + panel.id).c_str());
        ImGui::Dummy(ImVec2(0, px(8)));
        ImGui::Indent(inset);
        if (!panel.title.empty()) {
            ImGui::PushFont(s.fonts.ui_medium, 11.5f);
            text_colored(panel.title, g_pal->dim);
            ImGui::PopFont();
            ImGui::Dummy(ImVec2(0, px(2)));
        }
        ImGui::PushFont(s.fonts.ui, 12.5f);
        ImGui::PushTextWrapPos(ImGui::GetContentRegionMax().x - inset);
        for (std::size_t i = 0; i < panel.widgets.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            draw_mod_widget(s, panel, panel.widgets[i], inset);
            ImGui::PopID();
        }
        ImGui::PopTextWrapPos();
        ImGui::PopFont();
        ImGui::Unindent(inset);
        ImGui::PopID();
        ImGui::Dummy(ImVec2(0, px(4)));
    }
}

// ------------------------------------------------------------------ window chrome
// Sizes in the chrome are design pixels at scale 1: px() scales positions and boxes, fpx() font
// sizes.

float fpx(float v) {
    return v * g_scale;
}

ImU32 mix(ImU32 a, ImU32 b, float t) {
    const ImVec4 x = ImGui::ColorConvertU32ToFloat4(a);
    const ImVec4 y = ImGui::ColorConvertU32ToFloat4(b);
    return ImGui::ColorConvertFloat4ToU32(
        ImVec4(x.x + (y.x - x.x) * t, x.y + (y.y - x.y) * t, x.z + (y.z - x.z) * t, x.w + (y.w - x.w) * t));
}

// A surface slightly raised from the card (active tab, diff hunk rows): darker in light themes,
// lighter in dark ones.
ImU32 raised_color() {
    return g_pal->light ? mix(g_pal->card, IM_COL32(0, 0, 0, 255), 0.05f)
                        : mix(g_pal->card, IM_COL32(255, 255, 255, 255), 0.09f);
}

// Text drawn on an accent fill.
ImU32 accent_foreground() {
    return IM_COL32(255, 255, 255, 255);
}

// Space avatar colours by name: a fixed table, the same in every theme.
ImU32 avatar_color(const std::string& name) {
    if (name == "ansi3")
        return IM_COL32(0xe3, 0x81, 0x4f, 255);
    if (name == "ansi4")
        return IM_COL32(0x5b, 0x8d, 0xef, 255);
    if (name == "ansi5")
        return IM_COL32(0x8b, 0x7c, 0xf6, 255);
    if (name == "ansi2")
        return IM_COL32(0x3f, 0xae, 0x6a, 255);
    if (name == "ansi1")
        return IM_COL32(0xd9, 0x64, 0x9a, 255);
    return IM_COL32(0x22, 0xa9, 0x9a, 255);  // ansi6
}

// Centres the glyph on `center`, snapped to whole pixels so icons stay crisp.
void draw_icon(App::State& s, ImDrawList* dl, const char* glyph, float size, ImVec2 center, ImU32 col) {
    const ImVec2 t = s.fonts.icons->CalcTextSizeA(size, FLT_MAX, 0, glyph);
    dl->AddText(s.fonts.icons, size,
                ImVec2(std::round(center.x - t.x * 0.5f), std::round(center.y - t.y * 0.5f)), col, glyph);
}

// Square icon button with radius 6 and a muted icon; hover (or `on`) adds a background and brings
// the icon to the foreground colour.
bool icon_button(App::State& s, const char* id, const char* glyph, ImVec2 pos, float side, float icon,
                 const char* tip, ImU32 col, bool on) {
    ImGui::SetCursorScreenPos(pos);
    const bool clicked = ImGui::InvisibleButton(id, ImVec2(side, side));
    const bool hov = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (hov || on)
        dl->AddRectFilled(pos, ImVec2(pos.x + side, pos.y + side), on ? g_pal->selected : g_pal->hover,
                          px(6));
    draw_icon(s, dl, glyph, icon, ImVec2(pos.x + side * 0.5f, pos.y + side * 0.5f),
              hov || on ? g_pal->text : col);
    if (hov && tip != nullptr && *tip != 0)
        ImGui::SetTooltip("%s", tip);
    return clicked;
}

// ------------------------------------------------------------------ context menus
// 5 px padding; 31 px rows with a 14 px muted icon, the label, and the bound key right-aligned in
// 11 px faint text. Rows go between begin_menu and ImGui::EndPopup. Only one menu is open at a
// time, so a single keyboard navigator serves all of them.
ui::MenuNav g_menu_nav;

// Call right after a menu popup begins: a fresh menu highlights its first row; Up/Down move,
// Enter runs the highlighted row (menu_row), Esc closes.
void menu_keys() {
    if (ImGui::IsWindowAppearing())
        g_menu_nav.open();
    g_menu_nav.frame(
        ImGui::IsKeyPressed(ImGuiKey_UpArrow), ImGui::IsKeyPressed(ImGuiKey_DownArrow),
        ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false));
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        ImGui::CloseCurrentPopup();
}

bool begin_menu(const char* id) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(5), px(5)));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::SetNextWindowSizeConstraints(ImVec2(px(200), 0), ImVec2(FLT_MAX, FLT_MAX));
    const bool open = ImGui::BeginPopupContextItem(id);
    ImGui::PopStyleVar(2);
    if (open) {
        menu_keys();
        // Raised like the other floating surfaces (the size is last frame's; it settles at once).
        const ImVec2 wp = ImGui::GetWindowPos();
        const ImVec2 ws = ImGui::GetWindowSize();
        tok::window_shadow(ImGui::GetWindowDrawList(), wp, ImVec2(wp.x + ws.x, wp.y + ws.y),
                           ImGui::GetStyle().PopupRounding, 1);
    }
    return open;
}

bool menu_row(App::State& s, const char* glyph, const char* label, const std::string& chord, bool enabled) {
    const ImVec2 a = ImGui::GetCursorScreenPos();
    const float w = std::max(ImGui::GetContentRegionAvail().x, px(190));
    ImGui::BeginDisabled(!enabled);
    bool clicked = ImGui::InvisibleButton(label, ImVec2(w, px(31)));
    ImGui::EndDisabled();
    const bool hov = enabled && ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    // The keyboard-highlighted row is drawn as selected, a hovered one as hover.
    bool run = false;
    const bool on = enabled && g_menu_nav.row(false, &run);
    clicked = clicked || run;
    if (hov || on)
        dl->AddRectFilled(a, ImVec2(a.x + w, a.y + px(31)), hov ? g_pal->hover : g_pal->selected, px(7));
    const float cy = a.y + px(15.5f);
    const ImU32 col = enabled ? g_pal->text : g_pal->faint;
    if (glyph != nullptr)
        draw_icon(s, dl, glyph, fpx(14), ImVec2(a.x + px(15), cy), enabled ? g_pal->dim : g_pal->faint);
    dl->AddText(s.fonts.ui, fpx(12.8f), ImVec2(a.x + px(31), cy - fpx(12.8f) * 0.6f), col, label);
    if (!chord.empty()) {
        const float cw = s.fonts.ui->CalcTextSizeA(fpx(11), FLT_MAX, 0, chord.c_str()).x;
        dl->AddText(s.fonts.ui, fpx(11), ImVec2(a.x + w - px(8) - cw, cy - fpx(11) * 0.6f), g_pal->faint,
                    chord.c_str());
    }
    if (clicked)
        ImGui::CloseCurrentPopup();
    return clicked;
}

// The shells this machine has (host profiles that run a shell), the default one first.
std::vector<std::pair<std::string, std::string>> shell_profiles(App::State& s, const std::string& machine) {
    ui::HostClient* h = s.host_for(machine);
    const auto profiles = h->profiles();
    const std::string def = pick_shell(s.settings.shell_profile, profiles);
    std::vector<std::pair<std::string, std::string>> out;
    for (const auto& [id, name] : profiles)
        if (is_shell_profile(id) || program_of_profile(h->profile_executable(id), id).shell)
            out.emplace_back(id, name);
    std::stable_partition(out.begin(), out.end(), [&](const auto& p) { return p.first == def; });
    return out;
}

// A menu of the shells: the chosen one starts a new space (or tab) in the focused folder.
void draw_shell_picker(App::State& s, const char* id, const char* target) {
    ImGui::SetNextWindowSizeConstraints(ImVec2(px(220), 0), ImVec2(FLT_MAX, FLT_MAX));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(5), px(5)));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    const bool open = ImGui::BeginPopup(id);
    ImGui::PopStyleVar(2);
    if (!open)
        return;
    menu_keys();
    const std::string machine = s.ws.active() != nullptr ? s.ws.active()->machine : std::string();
    const auto shells = shell_profiles(s, machine);
    if (shells.empty()) {
        ImGui::TextDisabled("No shell found");
    }
    for (std::size_t i = 0; i < shells.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        const std::string chord = i == 0 ? s.keymap.chord_for(std::string(target) + ".new") : std::string();
        if (menu_row(s, ui::icon::kTerminal, shells[i].second.c_str(), chord))
            run_command(s, std::string("launch:") + target + ":" + shells[i].first);
        ImGui::PopID();
    }
    ImGui::EndPopup();
}

void menu_separator() {
    const ImVec2 a = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, px(9)));
    ImGui::GetWindowDrawList()->AddLine(
        ImVec2(a.x, a.y + px(4)), ImVec2(a.x + ImGui::GetContentRegionAvail().x, a.y + px(4)), g_pal->border);
}

// ------------------------------------------------------------------ status marks
// Agent status marks: a spinner (working), a "Needs you" pill or a compact "!" (blocked), a done
// dot, an idle ring. Draws with its left edge at `x`, vertically centred on `cy`, and returns the
// width used; with `measure` it only returns the width.
float status_mark(App::State& s, ImDrawList* dl, float x, float cy, Attention a, bool compact, bool measure) {
    // A mark that has just changed fades in (tokens: normal duration), keyed by the row's ID scope
    // and the state, so a new state eases in instead of jumping.
    static bool fading = false;
    if (!measure && !fading && a != Attention::kNone) {
        static constexpr const char* kIds[] = {"##mk-none", "##mk-idle", "##mk-work", "##mk-done",
                                               "##mk-block"};
        const auto i = static_cast<std::size_t>(std::clamp(static_cast<int>(a), 0, 4));
        const float k = tok::appear(ImGui::GetID(kIds[i]), ui::tokens::kNormalMs);
        if (k < 1.0f) {
            float w = 0;
            fading = true;
            with_alpha(dl, k, [&] { w = status_mark(s, dl, x, cy, a, compact, false); });
            fading = false;
            return w;
        }
    }
    switch (a) {
        case Attention::kWorking: {
            const float r = px(5);
            if (!measure) {
                const float t = static_cast<float>(ImGui::GetTime());
                const float a0 = t * (2.0f * 3.14159265f / 0.9f);  // one turn per 900 ms
                dl->PathArcTo(ImVec2(x + px(1) + r + px(1), cy), r, a0, a0 + 4.4f, 18);
                dl->PathStroke(g_pal->working, 0, px(1.6f));
            }
            return px(14);
        }
        case Attention::kBlocked: {
            if (compact) {
                if (!measure) {
                    dl->AddRectFilled(ImVec2(x, cy - px(7)), ImVec2(x + px(14), cy + px(7)),
                                      alpha(g_pal->needs_you, 0x26), px(4));
                    const ImVec2 t = s.fonts.ui_bold->CalcTextSizeA(fpx(10.5f), FLT_MAX, 0, "!");
                    dl->AddText(s.fonts.ui_bold, fpx(10.5f),
                                ImVec2(x + (px(14) - t.x) * 0.5f, cy - t.y * 0.5f), g_pal->needs_you, "!");
                }
                return px(14);
            }
            const char* label = "Needs you";
            const ImVec2 t = s.fonts.ui_bold->CalcTextSizeA(fpx(10.5f), FLT_MAX, 0, label);
            const float w = t.x + px(12);
            if (!measure) {
                dl->AddRectFilled(ImVec2(x, cy - px(9)), ImVec2(x + w, cy + px(9)),
                                  alpha(g_pal->needs_you, 0x26), px(5));
                dl->AddText(s.fonts.ui_bold, fpx(10.5f), ImVec2(x + px(6), cy - t.y * 0.5f), g_pal->needs_you,
                            label);
            }
            return w;
        }
        case Attention::kDone:
            if (!measure)
                dl->AddCircleFilled(ImVec2(x + px(3) + px(3.5f), cy), px(3.5f), g_pal->done);
            return px(13);
        case Attention::kIdle:
            if (!measure)
                dl->AddCircle(ImVec2(x + px(3) + px(3.5f), cy), px(3.0f), g_pal->idle, 0, px(1.5f));
            return px(13);
        case Attention::kNone:
            break;
    }
    return 0;
}

// Dragging an empty part of the brand row or the tab bar moves the window; double-click toggles
// maximize. The press is handed to Windows as a caption click (WM_NCLBUTTONDOWN + HTCAPTION), so
// the system move loop runs and Aero Snap works.
void window_drag_area(App::State& s, ImVec2 a, ImVec2 b) {
    if (s.config.hwnd == nullptr || ImGui::IsAnyItemHovered() ||
        ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId))
        return;
    if (!ImGui::IsMouseHoveringRect(a, b, false) || !ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows))
        return;
    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        ::ShowWindow(s.config.hwnd, ::IsZoomed(s.config.hwnd) ? SW_RESTORE : SW_MAXIMIZE);
    } else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        ::ReleaseCapture();
        ::SendMessageW(s.config.hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
        ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, false);  // the move loop ate the release
    }
}

// ------------------------------------------------------------------ round window buttons
// Close, minimize and zoom as three buttons, left to right, at the layout's traffic_lights point:
// 12 px buttons 8 px apart in colours worked out from the theme (libs/ui window_buttons). The
// glyphs show only while the pointer is over the group, and an inactive window mutes them. Most
// layouts draw them round; Terminals with square corners as small flat squares.
namespace {
constexpr float kLightSize = 12.0f;
constexpr float kLightGap = 8.0f;
constexpr float kLightsWidth = kLightSize * 3 + kLightGap * 2;  // 52
constexpr const char* kLightCommands[3] = {"window.close", "window.minimize", "window.maximize"};

ImU32 col_of(std::uint32_t rgb, int a = 255) {
    return IM_COL32((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255, a);
}

// The layout's [x, y] for the first button's top-left corner, in design pixels; [12, 16] when the
// layout does not say.
ImVec2 traffic_lights_point(const App::State& s) {
    const Json& t = s.layout().tree.traffic_lights;
    if (t.is_array() && t.size() == 2 && t[0].is_number() && t[1].is_number())
        return {t[0].get<float>(), t[1].get<float>()};
    return {12.0f, 16.0f};
}

// The look of the shown layout's window buttons (Focus mode keeps them round).
WindowButtonLook light_look(const App::State& s) {
    if (s.settings.focus_on)
        return WindowButtonLook::kRound;
    if (s.layout().style == "terminals" && s.settings.styles.terminals_square)
        return WindowButtonLook::kSquare;
    return WindowButtonLook::kRound;
}
}  // namespace

ui::window_buttons::Colors window_button_colors(ImU32 bar) {
    ui::window_buttons::Input in;
    in.blocked = rgb_of(g_pal->needs_you);
    in.working = rgb_of(g_pal->working);
    in.done = rgb_of(g_pal->done);
    in.accent = rgb_of(g_pal->focus);
    in.bar = rgb_of(bar);
    in.border = rgb_of(g_pal->border);
    in.text = rgb_of(g_pal->text);
    // Worked out once per theme and bar (the ambient tint moves the palette, so key on the colours).
    static std::map<std::array<std::uint32_t, 7>, ui::window_buttons::Colors> cache;
    const std::array<std::uint32_t, 7> k = {in.blocked, in.working, in.done, in.accent,
                                            in.bar,     in.border,  in.text};
    if (const auto it = cache.find(k); it != cache.end())
        return it->second;
    if (cache.size() > 64)
        cache.clear();
    return cache.emplace(k, ui::window_buttons::derive(in)).first->second;
}

ImU32 window_button_color(int kind, ImU32 bar) {
    return col_of(window_button_colors(bar).fill[static_cast<std::size_t>(std::clamp(kind, 0, 2))]);
}

ImU32 window_button_glyph(int kind, ImU32 bar) {
    return col_of(window_button_colors(bar).glyph[static_cast<std::size_t>(std::clamp(kind, 0, 2))]);
}

bool mac_window_buttons(const App::State& s) {
    return s.config.hwnd != nullptr && s.settings.window_buttons != "windows" && !style_draws_window_buttons(s);
}

void traffic_lights_input(App::State& s, ImVec2 origin) {
    s.lights = {};
    if (!mac_window_buttons(s))
        return;
    const ImVec2 p = traffic_lights_point(s);
    s.lights.shown = true;
    s.lights.at = ImVec2(origin.x + px(p.x), origin.y + px(p.y));
    if (s.settings.focus_on)  // Focus has no top bar: they sit at the left of its 22 px strip
        s.lights.at = ImVec2(origin.x + px(10), origin.y + ImGui::GetMainViewport()->WorkSize.y - px(22) + px(5));
    // Each button's hit area runs halfway into the gaps, so the group has no dead spots.
    const float step = px(kLightSize + kLightGap);
    const float pad = px(kLightGap * 0.5f);
    for (int i = 0; i < 3; ++i) {
        ImGui::PushID(i);
        ImGui::SetCursorScreenPos(ImVec2(s.lights.at.x + step * static_cast<float>(i) - pad, s.lights.at.y - pad));
        const bool hit = ImGui::InvisibleButton("##light", ImVec2(px(kLightSize) + pad * 2, px(kLightSize) + pad * 2));
        if (ImGui::IsItemHovered())
            s.lights.group_hovered = true;
        if (ImGui::IsItemActive())
            s.lights.held = i;
        if (hit)
            run_command(s, kLightCommands[i]);
        ImGui::PopID();
    }
}

// One button: `kind` 0 close (a cross), 1 minimize (a bar), 2 zoom (a plus), in the theme's
// colours for buttons on `bar` (0: the chrome). Unlit ones take the muted colour; the glyph shows
// only with `glyph`; a held one darkens.
void paint_round_button(ImDrawList* dl, ImVec2 c, float r, int kind, bool lit, bool glyph, bool held,
                        ImU32 bar, WindowButtonLook look) {
    const int k = std::clamp(kind, 0, 2);
    const auto colors = window_button_colors(bar != 0 ? bar : g_pal->sidebar);
    const auto ki = static_cast<std::size_t>(k);
    ImU32 fill = lit ? col_of(colors.fill[ki]) : col_of(colors.muted);
    if (held)
        fill = mix(fill, IM_COL32(0, 0, 0, 255), 0.18f);
    const ImU32 ink = col_of(colors.glyph[ki], 225);
    // Glyph half-length: smaller inside a square, so it clears the edge.
    const float g = r * (look == WindowButtonLook::kRound ? 0.5f : 0.4f);
    const float thick = std::max(1.0f, r * 0.22f);
    switch (look) {
        case WindowButtonLook::kRound:
            dl->AddCircleFilled(c, r, fill, 24);
            dl->AddCircle(c, r - 0.5f, mix(fill, IM_COL32(0, 0, 0, 255), 0.22f), 24, 1.0f);
            break;
        case WindowButtonLook::kSquare: {
            const float h = std::round(r * 0.84f);
            dl->AddRectFilled(ImVec2(c.x - h, c.y - h), ImVec2(c.x + h, c.y + h), fill,
                              std::max(1.0f, r * 0.18f));
            break;
        }
    }
    if (!glyph)
        return;
    if (k == 0) {  // a cross
        dl->AddLine(ImVec2(c.x - g, c.y - g), ImVec2(c.x + g, c.y + g), ink, thick);
        dl->AddLine(ImVec2(c.x - g, c.y + g), ImVec2(c.x + g, c.y - g), ink, thick);
    } else {  // a bar, and a plus for zoom
        dl->AddLine(ImVec2(c.x - g - r * 0.08f, c.y), ImVec2(c.x + g + r * 0.08f, c.y), ink, thick);
        if (k == 2)
            dl->AddLine(ImVec2(c.x, c.y - g - r * 0.08f), ImVec2(c.x, c.y + g + r * 0.08f), ink, thick);
    }
}

void traffic_lights_paint(App::State& s) {
    if (!s.lights.shown)
        return;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float r = px(kLightSize * 0.5f);
    const WindowButtonLook look = light_look(s);
    for (int i = 0; i < 3; ++i) {
        const ImVec2 c(s.lights.at.x + px(kLightSize + kLightGap) * static_cast<float>(i) + r, s.lights.at.y + r);
        paint_round_button(dl, c, r, i, s.window_focused || s.lights.group_hovered, s.lights.group_hovered,
                           s.lights.held == i, g_pal->sidebar, look);
    }
}

float clear_of_traffic_lights(const App::State& s, float x, float top, float bottom, float margin) {
    if (!s.lights.shown)
        return x;
    const float lt = s.lights.at.y;
    const float lb = lt + px(kLightSize);
    if (bottom <= lt || top >= lb)
        return x;
    return std::max(x, s.lights.at.x + px(kLightsWidth) + margin);
}

GitTail git_tail(const GitInfo& g, const std::string& space_name, bool worktree_row) {
    GitTail t;
    if (!g.repo)
        return t;
    auto same = [](const std::string& a, const std::string& b) {
        return _stricmp(a.c_str(), b.c_str()) == 0;
    };
    const std::string repo = folder_name(g.common_top.empty() ? g.top : g.common_top);
    std::string branch = g.branch == "HEAD" ? std::string("(detached)") : g.branch;
    if (branch == "main" || branch == "master" || same(branch, space_name))
        branch.clear();
    t.text = worktree_row || same(repo, space_name) ? std::string() : repo;
    if (!branch.empty())
        t.text += (t.text.empty() ? "" : " \xC2\xB7 ") + branch;
    t.ahead = g.ahead;
    t.behind = g.behind;
    return t;
}

// ------------------------------------------------------------------ confirm prompt

void open_confirm(App::State& s, std::string title, std::string detail, std::string ok, std::string command) {
    s.confirm_title = std::move(title);
    s.confirm_detail = std::move(detail);
    s.confirm_ok = std::move(ok);
    s.confirm_command = std::move(command);
    s.show_confirm = true;
}

// "Close space?" with "<name> — N tabs, M terminals"; the counts stay plural even for one.
void confirm_close_space(App::State& s, int index) {
    if (index < 0 || index >= static_cast<int>(s.ws.spaces.size()))
        return;
    const auto& sp = s.ws.spaces[static_cast<std::size_t>(index)];
    std::size_t terms = 0;
    for (const auto& t : sp.tabs)
        terms += s.ws.sessions_in(t).size();
    char detail[256];
    std::snprintf(detail, sizeof detail, "%s \xE2\x80\x94 %zu tabs, %zu terminals",
                  s.ws.space_title(index, s.session_cwds()).c_str(), sp.tabs.size(), terms);
    open_confirm(s, "Close space?", detail, "Close", "space.close_now:" + sp.id);
}
}  // namespace detail

}  // namespace agentdesk::desktop
