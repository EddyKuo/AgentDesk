// Shared window-chrome drawing: mod panels, icons, icon buttons, menus, shell picker, status
// marks, drag areas and the confirm prompt helpers.
#include "app_internal.hpp"

namespace agentdesk::desktop {
namespace detail {

namespace {
// ------------------------------------------------------------------ drawing

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

}  // namespace

// Mod panels, inset and titled like the sidebar's own sections (herdr spaces/agents headers).
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
            ImGui::PushStyleColor(ImGuiCol_Text, v4(g_pal->dim));
            ImGui::TextUnformatted(panel.title.c_str());
            ImGui::PopStyleColor();
            ImGui::PopFont();
            ImGui::Dummy(ImVec2(0, px(2)));
        }
        ImGui::PushFont(s.fonts.ui, 12.5f);
        ImGui::PushTextWrapPos(ImGui::GetContentRegionMax().x - inset);
        for (std::size_t i = 0; i < panel.widgets.size(); ++i) {
            const auto& w = panel.widgets[i];
            ImGui::PushID(static_cast<int>(i));
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
                    ImGui::PushStyleColor(ImGuiCol_Text, v4(g_pal->dim));
                    ImGui::TextUnformatted(w.text.c_str());
                    ImGui::PopStyleColor();
                    const float rw = ImGui::CalcTextSize(w.text2.c_str()).x;
                    ImGui::SameLine(ImGui::GetContentRegionMax().x - rw - inset);
                    ImGui::PushStyleColor(ImGuiCol_Text, v4(mod_color(w.color)));
                    ImGui::TextUnformatted(w.text2.c_str());
                    ImGui::PopStyleColor();
                    break;
                }
                case ui::ModWidget::Kind::kBadge: {
                    const ImVec2 p = ImGui::GetCursorScreenPos();
                    const ImVec2 t = ImGui::CalcTextSize(w.text.c_str());
                    ImGui::GetWindowDrawList()->AddRectFilled(p,
                                                              ImVec2(p.x + t.x + px(12), p.y + t.y + px(4)),
                                                              alpha(mod_color(w.color), 60), px(8));
                    ImGui::GetWindowDrawList()->AddText(ImVec2(p.x + px(6), p.y + px(2)), mod_color(w.color),
                                                        w.text.c_str());
                    ImGui::Dummy(ImVec2(t.x + px(12), t.y + px(4)));
                    break;
                }
            }
            ImGui::PopID();
        }
        ImGui::PopTextWrapPos();
        ImGui::PopFont();
        ImGui::Unindent(inset);
        ImGui::PopID();
        ImGui::Dummy(ImVec2(0, px(4)));
    }
}

// ------------------------------------------------------------------ herdr window chrome
// Layout, sizes and behaviour follow herdr-app's default layout and mods (layouts/main.json,
// mods/workspace-chrome, spaces, agents, tabs, pane, workspace-status; Apache-2.0, see
// docs/research/herdr-app/spec-sidebar-layout.md). Sizes are herdr design px through px().

float fpx(float v) {
    return v * g_scale;
}

ImU32 mix(ImU32 a, ImU32 b, float t) {
    const ImVec4 x = ImGui::ColorConvertU32ToFloat4(a);
    const ImVec4 y = ImGui::ColorConvertU32ToFloat4(b);
    return ImGui::ColorConvertFloat4ToU32(ImVec4(x.x + (y.x - x.x) * t, x.y + (y.y - x.y) * t,
                                                 x.z + (y.z - x.z) * t, x.w + (y.w - x.w) * t));
}

// herdr tokens derived from our palettes: raised (active tab), popover (menus).
ImU32 raised_color() {
    return g_pal->light ? mix(g_pal->card, IM_COL32(0, 0, 0, 255), 0.05f)
                        : mix(g_pal->card, IM_COL32(255, 255, 255, 255), 0.09f);
}

// herdr's accent_foreground: text on the accent fill.
ImU32 accent_foreground() {
    return IM_COL32(255, 255, 255, 255);
}

// Avatar colours: herdr's fixed table (spaces.js MARK), the same in every theme.
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

void draw_icon(App::State& s, ImDrawList* dl, const char* glyph, float size, ImVec2 center, ImU32 col) {
    const ImVec2 t = s.fonts.icons->CalcTextSizeA(size, FLT_MAX, 0, glyph);
    dl->AddText(s.fonts.icons, size, ImVec2(std::round(center.x - t.x * 0.5f), std::round(center.y - t.y * 0.5f)),
                col, glyph);
}

// herdr icon button: square, radius 6, muted icon; hover background + foreground.
bool icon_button(App::State& s, const char* id, const char* glyph, ImVec2 pos, float side, float icon,
                 const char* tip, ImU32 col, bool on) {
    ImGui::SetCursorScreenPos(pos);
    const bool clicked = ImGui::InvisibleButton(id, ImVec2(side, side));
    const bool hov = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (hov || on)
        dl->AddRectFilled(pos, ImVec2(pos.x + side, pos.y + side), on ? g_pal->selected : g_pal->hover, px(6));
    draw_icon(s, dl, glyph, icon, ImVec2(pos.x + side * 0.5f, pos.y + side * 0.5f), hov || on ? g_pal->text : col);
    if (hov && tip != nullptr && *tip != 0)
        ImGui::SetTooltip("%s", tip);
    return clicked;
}

// herdr context menu (mods/menu): padding 5, rows 31 px with a 14 px muted icon, the label and
// the bound key right-aligned in 11 px dim. Use between begin_menu / end_menu.
// Keyboard in the open menu (herdr menu/main.js 131-137): one menu is open at a time.
ui::MenuNav g_menu_nav;

// Call right after a menu popup begins: a fresh menu highlights its first row; Up/Down move,
// Enter runs the highlighted row (menu_row), Esc closes.
void menu_keys() {
    if (ImGui::IsWindowAppearing())
        g_menu_nav.open();
    g_menu_nav.frame(ImGui::IsKeyPressed(ImGuiKey_UpArrow), ImGui::IsKeyPressed(ImGuiKey_DownArrow),
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
    if (open)
        menu_keys();
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
    // herdr menu/main.js 152-153: the highlighted row is "selected", a hovered one "hover".
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
        dl->AddText(s.fonts.ui, fpx(11), ImVec2(a.x + w - px(8) - cw, cy - fpx(11) * 0.6f), g_pal->faint, chord.c_str());
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
    ImGui::GetWindowDrawList()->AddLine(ImVec2(a.x, a.y + px(4)), ImVec2(a.x + ImGui::GetContentRegionAvail().x, a.y + px(4)),
                                        g_pal->border);
}

// Agent status marks (agents.js 208-217): spinner, "Needs you" pill or "!", done dot, idle ring.
// Draws with its left edge at `x`, vertically centred on `cy`; returns the width used.
float status_mark(App::State& s, ImDrawList* dl, float x, float cy, Attention a, bool compact, bool measure) {
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
                    dl->AddText(s.fonts.ui_bold, fpx(10.5f), ImVec2(x + (px(14) - t.x) * 0.5f, cy - t.y * 0.5f),
                                g_pal->needs_you, "!");
                }
                return px(14);
            }
            const char* label = "Needs you";
            const ImVec2 t = s.fonts.ui_bold->CalcTextSizeA(fpx(10.5f), FLT_MAX, 0, label);
            const float w = t.x + px(12);
            if (!measure) {
                dl->AddRectFilled(ImVec2(x, cy - px(9)), ImVec2(x + w, cy + px(9)), alpha(g_pal->needs_you, 0x26),
                                  px(5));
                dl->AddText(s.fonts.ui_bold, fpx(10.5f), ImVec2(x + px(6), cy - t.y * 0.5f), g_pal->needs_you, label);
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

// Dragging an empty part of the brand row or the tab bar moves the window (herdr window_drag:
// posts SC_MOVE so snapping works); double-click toggles maximize.
void window_drag_area(App::State& s, ImVec2 a, ImVec2 b) {
    if (s.config.hwnd == nullptr || ImGui::IsAnyItemHovered() || ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId))
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

GitTail git_tail(const GitInfo& g, const std::string& space_name, bool worktree_row) {
    GitTail t;
    if (!g.repo)
        return t;
    auto same = [](const std::string& a, const std::string& b) { return _stricmp(a.c_str(), b.c_str()) == 0; };
    const std::string repo = folder_name(g.common_top.empty() ? g.top : g.common_top);
    std::string branch = g.branch == "HEAD" ? std::string("(detached)") : g.branch;
    if (branch == "main" || branch == "master" || same(branch, space_name))
        branch.clear();
    std::string r = worktree_row || same(repo, space_name) ? std::string() : repo;
    t.text = r;
    if (!branch.empty())
        t.text += (t.text.empty() ? "" : " \xC2\xB7 ") + branch;
    t.ahead = g.ahead;
    t.behind = g.behind;
    return t;
}

void open_confirm(App::State& s, std::string title, std::string detail, std::string ok, std::string command) {
    s.confirm_title = std::move(title);
    s.confirm_detail = std::move(detail);
    s.confirm_ok = std::move(ok);
    s.confirm_command = std::move(command);
    s.show_confirm = true;
}

// "Close space?" (window.rs 3067-3080): "<name> — N tabs, M terminals", always plural as
// window.rs 3075 writes it.
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
