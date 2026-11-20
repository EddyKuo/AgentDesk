// The AgentDesk Tiling layout: a thin bar (space numbers, the space's tabs, the focused pane, the
// counts, the clock) over square panes with gaps, and a keyboard resize mode. AgentDesk's own
// drawing: rectangles and text from the active theme, the bundled fonts and the existing marks.
#include "agentdesk/ui/tiling_layout.hpp"

#include "app_internal.hpp"

namespace agentdesk::desktop {
namespace detail {

namespace {
namespace tl = ui::tiling;

constexpr const char* kResizePopup = "##tiling-resize";

float text_w(ImFont* f, float size, const std::string& t) {
    return f->CalcTextSizeA(size, FLT_MAX, 0, t.c_str()).x;
}

// The space's state for its chip: needs you wins, then done.
Attention space_attention(App::State& s, const ui::Space& sp) {
    std::vector<Attention> atts;
    for (const auto& t : sp.tabs)
        atts.push_back(tab_attention(s, s.ws.sessions_in(t)));
    return ui::most_urgent(atts);
}

// "claude · repo-a (main)": the focused pane's program, its folder and the branch.
std::string focused_title(App::State& s) {
    const std::string id = s.ws.focused_session();
    const auto it = s.views_by_id.find(id);
    if (it == s.views_by_id.end())
        return {};
    const ui::SessionView& v = it->second;
    std::string t = s.pane_title(id);
    const std::string folder = folder_name(v.cwd);
    if (!folder.empty())
        t += " \xC2\xB7 " + folder;
    const auto mi = s.machine_of.find(id);
    const GitInfo g = s.git.get(v.cwd, mi != s.machine_of.end() ? mi->second : std::string());
    if (g.repo && !g.branch.empty())
        t += " (" + (g.branch == "HEAD" ? std::string("detached") : g.branch) + ")";
    return t;
}

struct TabText {
    std::string full;   // "1:claude"
    std::string brief;  // "1"
    bool active = false;
    bool settled = false;
};

std::vector<TabText> tab_texts(App::State& s, const ui::Space* sp) {
    std::vector<TabText> out;
    if (sp == nullptr)
        return out;
    for (int i = 0; i < static_cast<int>(sp->tabs.size()); ++i) {
        const ui::Tab& t = sp->tabs[static_cast<std::size_t>(i)];
        TabText x;
        x.active = i == sp->active_tab;
        x.settled = t.settled;
        x.brief = std::to_string(i + 1);
        x.full = x.brief + ":" + tab_label(s, t, i, s.ws.sessions_in(t));
        if (x.active) {  // the current tab always keeps its name
            x.full += "*";
            x.brief = x.full;
        }
        if (x.settled) {
            x.full = "(" + x.full + ")";
            x.brief = "(" + x.brief + ")";
        }
        out.push_back(std::move(x));
    }
    return out;
}

void leave_resize(App::State& s) {
    s.styles.tiling.resizing = false;
    s.focus_view(s.ws.focused_session());
}
}  // namespace

// ------------------------------------------------------------------ the bar
// 26 px: the round buttons, the space chips, the tabs, the focused pane (centred), the counts and
// the clock (then Windows' buttons when chosen). Empty parts move the window.
void draw_tiling_bar(App::State& s, ImVec2 pos, float w, float h) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    auto& ts = s.styles.tiling;
    const ImVec2 b(pos.x + w, pos.y + h);
    dl->AddRectFilled(pos, b, g_pal->sidebar);
    dl->AddLine(ImVec2(pos.x, b.y - 1), ImVec2(b.x, b.y - 1), g_pal->border);
    const float cy = std::round(pos.y + (h - 1) * 0.5f);
    const bool active_window = s.window_focused;
    const ImU32 ink = active_window ? g_pal->text : g_pal->faint;
    const bool connected = s.host->connected();
    ImFont* mono = s.fonts.mono;
    ImFont* bold = s.fonts.mono_bold != nullptr ? s.fonts.mono_bold : s.fonts.mono;
    const float fs = fpx(12);
    const float chip_h = px(18);
    const float chip_pad = px(6);

    // Right side first: Windows' buttons, the clock, the counts.
    float right = b.x - px(8);
    if (s.config.hwnd != nullptr && s.settings.window_buttons == "windows") {
        draw_window_buttons(s, b.x, pos.y, h - 1);
        right = b.x - px(26) * 3 - px(4) - px(8);
    }
    std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &now);
    char clock[8];
    std::snprintf(clock, sizeof clock, "%02d:%02d", local.tm_hour, local.tm_min);
    std::vector<Attention> states;
    std::string first_blocked;
    for (const auto& id : s.ws.all_sessions()) {
        states.push_back(s.attention_of(id));
        if (first_blocked.empty() && states.back() == Attention::kBlocked)
            first_blocked = id;
    }
    const ui::chrome_common::AgentCounts counts = ui::chrome_common::count_agents(states);
    const std::string needs = counts.needs > 0 ? "!" + std::to_string(counts.needs) : std::string();
    const std::string working = counts.working > 0 ? "~" + std::to_string(counts.working) : std::string();
    const bool lost = !connected && !s.machine_loading("");
    float counts_w = 0;
    if (lost)
        counts_w = text_w(mono, fs, "disconnected") + px(12);
    else if (!needs.empty() || !working.empty())
        counts_w = text_w(mono, fs, needs) + text_w(mono, fs, working) +
                   (!needs.empty() && !working.empty() ? px(8) : 0.0f) + px(12);

    // Left: the round buttons' room, then the space chips.
    float x = clear_of_traffic_lights(s, pos.x + px(8), pos.y, b.y, px(12));
    const float left0 = x;
    const tl::SpaceChips chips = tl::space_chips(static_cast<int>(s.ws.spaces.size()), s.ws.active_space);
    std::vector<float> chip_w;
    float chips_total = 0;
    for (const int i : chips.shown) {
        const bool blocked =
            space_attention(s, s.ws.spaces[static_cast<std::size_t>(i)]) == Attention::kBlocked;
        const std::string label = std::to_string(i + 1) + (blocked ? "!" : "");
        chip_w.push_back(std::max(px(18), text_w(bold, fs, label) + chip_pad * 2));
        chips_total += chip_w.back() + px(4);
    }
    const std::string more = chips.more > 0 ? "+" + std::to_string(chips.more) : std::string();
    if (!more.empty())
        chips_total += text_w(bold, fs, more) + chip_pad * 2 + px(4);

    const ui::Space* sp = s.ws.active();
    const auto tabs = tab_texts(s, sp);
    float tabs_full = 0, tabs_short = 0;
    for (const auto& t : tabs) {
        tabs_full += text_w(mono, fs, t.full) + chip_pad * 2;
        tabs_short += text_w(mono, fs, t.brief) + chip_pad * 2;
    }
    const std::string title = ts.resizing ? std::string() : focused_title(s);
    const char* hint_text = "RESIZE  h/j/k/l or arrows \xC2\xB7 Esc to finish";
    const float title_w = ts.resizing ? text_w(bold, fs, hint_text) + px(20)
                                      : (title.empty() ? text_w(s.fonts.ui_medium, fpx(12.5f), "empty")
                                                       : text_w(s.fonts.ui_medium, fpx(12.5f), title)) +
                                            px(16);
    tl::BarWidths bw;
    bw.lead = (left0 - pos.x) + chips_total + px(12);
    bw.tabs_full = tabs_full;
    bw.tabs_short = tabs_short;
    bw.title = title_w;
    bw.counts = counts_w;
    bw.clock = text_w(mono, fs, clock) + px(12);
    const tl::BarFit fit = tl::fit_bar(bw, right - pos.x);
    const bool show_title = fit.title || ts.resizing;  // the resize hint never gives way

    // The chips: a click selects the space; the wheel over them steps through spaces.
    const ImVec2 chips_a(x, pos.y);
    for (std::size_t k = 0; k < chips.shown.size(); ++k) {
        const int i = chips.shown[k];
        const ui::Space& space = s.ws.spaces[static_cast<std::size_t>(i)];
        const Attention att = space_attention(s, space);
        const bool on = i == s.ws.active_space;
        const std::string label = std::to_string(i + 1) + (att == Attention::kBlocked ? "!" : "");
        const ImVec2 a(x, cy - chip_h * 0.5f);
        const ImVec2 c(x + chip_w[k], a.y + chip_h);
        ImGui::SetCursorScreenPos(a);
        ImGui::PushID(i);
        if (ImGui::InvisibleButton("##tiling-space", ImVec2(chip_w[k], chip_h)))
            run_command(s, "space.select:" + std::to_string(i + 1));
        const bool hov = ImGui::IsItemHovered();
        if (hov)
            ImGui::SetTooltip("%s", s.ws.space_title(i, s.session_cwds()).c_str());
        ImGui::PopID();
        ImU32 fill = g_pal->hover;
        ImU32 text = g_pal->dim;
        if (att == Attention::kBlocked) {
            fill = g_pal->needs_you;
            text = IM_COL32_WHITE;
        } else if (on) {
            fill = g_pal->focus;
            text = accent_foreground();
        } else if (hov) {
            fill = g_pal->selected;
            text = g_pal->text;
        } else if (att == Attention::kDone) {
            text = g_pal->done;
        }
        dl->AddRectFilled(a, c, fill, px(3));
        const float lw = text_w(bold, fs, label);
        dl->AddText(bold, fs, ImVec2(std::round(a.x + (chip_w[k] - lw) * 0.5f), cy - fs * 0.6f), text,
                    label.c_str());
        x += chip_w[k] + px(4);
    }
    if (!more.empty()) {
        const float mw = text_w(bold, fs, more) + chip_pad * 2;
        const ImVec2 a(x, cy - chip_h * 0.5f);
        ImGui::SetCursorScreenPos(a);
        if (ImGui::InvisibleButton("##tiling-more", ImVec2(mw, chip_h)))
            run_command(s, "app.picker");
        dl->AddRectFilled(a, ImVec2(a.x + mw, a.y + chip_h),
                          ImGui::IsItemHovered() ? g_pal->selected : g_pal->hover, px(3));
        dl->AddText(bold, fs, ImVec2(a.x + chip_pad, cy - fs * 0.6f), g_pal->dim, more.c_str());
        x += mw + px(4);
    }
    const ImVec2 chips_b(x, b.y);
    const ImGuiIO& io = ImGui::GetIO();
    if (io.MouseWheel != 0.0f && ImGui::IsMouseHoveringRect(chips_a, chips_b) && ImGui::IsWindowHovered())
        run_command(s, io.MouseWheel > 0 ? "space.prev" : "space.next");

    // A thin rule, then the tabs: a click selects, a double-click renames, a right-click opens
    // the tab menu; the wheel over them steps through tabs.
    x += px(2);
    dl->AddLine(ImVec2(x, cy - px(7)), ImVec2(x, cy + px(7)), g_pal->border);
    x += px(6);
    const ImVec2 tabs_a(x, pos.y);
    const float tabs_end = x + fit.tabs_room;
    std::string close_tab;
    if (sp != nullptr) {
        dl->PushClipRect(ImVec2(x, pos.y), ImVec2(tabs_end, b.y), true);
        for (int i = 0; i < static_cast<int>(tabs.size()); ++i) {
            const TabText& t = tabs[static_cast<std::size_t>(i)];
            const std::string& label = fit.short_tabs ? t.brief : t.full;
            const float tw = text_w(mono, fs, label) + chip_pad * 2;
            if (x >= tabs_end)
                break;
            const ImVec2 a(x, cy - chip_h * 0.5f);
            ImGui::SetCursorScreenPos(a);
            ImGui::PushID(sp->tabs[static_cast<std::size_t>(i)].id.c_str());
            const bool clicked =
                ImGui::InvisibleButton("##tiling-tab", ImVec2(std::min(tw, tabs_end - x), chip_h));
            const bool hov = ImGui::IsItemHovered();
            if (clicked)
                run_command(s, "tab.goto:" + std::to_string(s.ws.active_space) + ":" + std::to_string(i));
            if (hov && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                run_command(s, "tab.rename:" + sp->tabs[static_cast<std::size_t>(i)].id);
            tab_menu(s, sp->tabs[static_cast<std::size_t>(i)], close_tab);
            ImGui::PopID();
            if (hov)
                dl->AddRectFilled(a, ImVec2(a.x + tw, a.y + chip_h), g_pal->hover, px(3));
            const ImU32 col = t.settled ? g_pal->faint : t.active ? ink : g_pal->dim;
            dl->AddText(mono, fs, ImVec2(a.x + chip_pad, cy - fs * 0.6f), col, label.c_str());
            if (t.active)
                dl->AddRectFilled(ImVec2(a.x + chip_pad, a.y + chip_h - px(2)),
                                  ImVec2(a.x + tw - chip_pad, a.y + chip_h), g_pal->focus);
            x += tw;
        }
        dl->PopClipRect();
    }
    if (!close_tab.empty())
        run_command(s, "tab.close:" + close_tab);
    if (io.MouseWheel != 0.0f && ImGui::IsMouseHoveringRect(tabs_a, ImVec2(tabs_end, b.y)) &&
        ImGui::IsWindowHovered())
        run_command(s, io.MouseWheel > 0 ? "tab.prev" : "tab.next");
    const float left_end = std::min(x, tabs_end);

    // Right: the clock, then the counts (a click goes to the first agent that needs you).
    if (fit.clock) {
        const float cw = text_w(mono, fs, clock);
        dl->AddText(mono, fs, ImVec2(right - cw, cy - fs * 0.6f), active_window ? g_pal->dim : g_pal->faint,
                    clock);
        right -= cw + px(12);
    }
    if (counts_w > 0) {
        const ImVec2 a(right - counts_w, cy - chip_h * 0.5f);
        ImGui::SetCursorScreenPos(a);
        const bool clicked = ImGui::InvisibleButton("##tiling-counts", ImVec2(counts_w, chip_h));
        const bool hov = ImGui::IsItemHovered();
        if (hov && !lost)
            dl->AddRectFilled(a, ImVec2(a.x + counts_w, a.y + chip_h), g_pal->hover, px(3));
        float cx = a.x + px(6);
        if (lost) {
            dl->AddText(mono, fs, ImVec2(cx, cy - fs * 0.6f), g_pal->needs_you, "disconnected");
        } else {
            if (!needs.empty()) {
                dl->AddText(bold, fs, ImVec2(cx, cy - fs * 0.6f), g_pal->needs_you, needs.c_str());
                cx += text_w(mono, fs, needs) + px(8);
            }
            if (!working.empty())
                dl->AddText(mono, fs, ImVec2(cx, cy - fs * 0.6f), g_pal->working, working.c_str());
            if (hov)
                ImGui::SetTooltip(
                    "%s", first_blocked.empty() ? "Find an agent" : "Go to the first agent that needs you");
            if (clicked)
                run_command(
                    s, first_blocked.empty() ? std::string("app.picker") : "agent.focus:" + first_blocked);
        }
        right -= counts_w + px(4);
    }

    // The middle: the focused pane, centred on the bar when there is room, else between the
    // tabs and the counts; the resize hint takes its place while resizing.
    if (show_title) {
        const float room = right - left_end - px(16);
        const float want = std::min(title_w, room);
        float ax = std::round(pos.x + (w - want) * 0.5f);
        ax = std::clamp(ax, left_end + px(8), std::max(left_end + px(8), right - want - px(8)));
        if (ts.resizing) {
            const bool flash = Clock::now() < ts.flash_until;
            const ImVec2 a(ax, cy - chip_h * 0.5f);
            const ImVec2 c(ax + want, a.y + chip_h);
            dl->AddRectFilled(a, c, flash ? g_pal->needs_you : g_pal->working, px(3));
            text_clipped(dl, bold, fs, ImVec2(a.x + px(10), cy - fs * 0.6f), g_pal->bg, hint_text,
                         want - px(20));
            ts.hint_min = a;
            ts.hint_size = ImVec2(c.x - a.x, c.y - a.y);
        } else if (!connected && s.machine_loading("")) {
            const float sw = status_mark(s, dl, ax, cy, Attention::kWorking, true);
            text_clipped(dl, s.fonts.ui_medium, fpx(12.5f), ImVec2(ax + sw + px(4), cy - fpx(12.5f) * 0.6f),
                         g_pal->faint, "connecting\xE2\x80\xA6", want - sw - px(4));
        } else if (title.empty()) {
            dl->AddText(s.fonts.ui_medium, fpx(12.5f), ImVec2(ax + px(8), cy - fpx(12.5f) * 0.6f),
                        g_pal->faint, "empty");
        } else {
            text_clipped(dl, s.fonts.ui_medium, fpx(12.5f), ImVec2(ax + px(8), cy - fpx(12.5f) * 0.6f), ink,
                         title, want - px(16));
        }
    }
    window_drag_area(s, pos, b);
}

// ------------------------------------------------------------------ panes
// Square panes: a 1 px border, 2 px in the accent when focused, 2 px in the needs-you colour when
// an unfocused agent needs you, faint when agent-host is gone. An 18 px title row (program,
// folder, state) when the tab shows two panes or more (Pane titles: auto).
StyledPane draw_tiling_pane(App::State& s, const std::string& id, const ui::SessionView& v, ImVec2 pos,
                            ImVec2 max, const ui::styles::PaneLook& look, bool focused, bool connected) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const Attention att = s.attention_of(id);
    const ui::Tab* tab = s.ws.active_tab();
    const int panes =
        tab == nullptr ? 1 : (!tab->zoomed.empty() ? 1 : static_cast<int>(s.ws.sessions_in(*tab).size()));
    const bool titles = tl::show_titles(s.settings.styles.tiling_titles, panes);
    dl->AddRectFilled(pos, max, g_pal->term_bg);

    lay::Line line = focused ? look.focus : look.border;
    ImU32 col = layout_color(line.color).value_or(focused ? g_pal->focus : g_pal->border);
    if (!connected) {
        line.width = 1.0f;
        col = g_pal->faint;
    } else if (!focused && att == Attention::kBlocked) {
        line.width = std::max(2.0f, look.focus.width);
        col = g_pal->needs_you;
    }
    const float edge = line_px(std::max({look.border.width, look.focus.width, 2.0f}));

    float top = pos.y + edge;
    if (titles) {
        const float hh = px(look.header);
        const ImVec2 ha(pos.x, pos.y);
        const ImVec2 hb(max.x, pos.y + hh + edge);
        const bool hov =
            ImGui::IsMouseHoveringRect(ha, hb) && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
        dl->AddRectFilled(ha, hb, focused ? mix(g_pal->card, g_pal->focus, 0.12f) : g_pal->card);
        const float cy = ha.y + edge * 0.5f + hh * 0.5f;
        const float fs = fpx(11.5f);
        float rx = max.x - edge - px(6);
        if (v.process_state == "Running" && att != Attention::kNone && att != Attention::kIdle) {
            const float mw = status_mark(s, dl, 0, cy, att, true, true);
            status_mark(s, dl, rx - mw, cy, att, true);
            rx -= mw + px(6);
        }
        std::string label = s.pane_title(id);
        if (const std::string f = folder_name(v.cwd); !f.empty())
            label += " \xC2\xB7 " + f;
        text_clipped(dl, s.fonts.mono, fs, ImVec2(pos.x + edge + px(6), cy - fs * 0.6f),
                     focused || hov ? g_pal->text : g_pal->dim, label, rx - (pos.x + edge + px(6)));
        // A press focuses the pane and starts a pane move (it only moves once dragged 4 px).
        if (hov && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemHovered()) {
            s.ws.focus(id);
            s.views[id].focus_next = true;
            if (panes >= 2) {
                s.drag_pane = id;
                s.drag_pane_from = ImGui::GetIO().MousePos;
            }
        }
        top = hb.y;
    }
    // A shared 1 px line between gapless panes: unfocused borders reach one pixel past the right
    // and bottom edges, onto the next pane's line.
    const bool gapless = look.gap <= 0.0f;
    const ImVec2 fmax = gapless && line.width <= 1.0f ? ImVec2(max.x + 1, max.y + 1) : max;
    dl->AddRect(pos, fmax, col, 0.0f, 0, line_px(line.width));
    const float pad = px(4);
    return {ImVec2(pos.x + edge + pad, top + px(2)), ImVec2(max.x - edge - pad, max.y - edge - px(2))};
}

bool draw_tiling_no_tab(App::State& s, ImDrawList* dl, ImVec2 pos, ImVec2 size) {
    const std::string chord = s.keymap.chord_for("tab.new");
    const std::string text = chord.empty() ? std::string("No tabs \xE2\x80\x94 New terminal opens one")
                                           : "No tabs \xE2\x80\x94 " + chord + " opens one";
    const float fs = fpx(13);
    const ImVec2 t = s.fonts.mono->CalcTextSizeA(fs, FLT_MAX, 0, text.c_str());
    dl->AddText(s.fonts.mono, fs,
                ImVec2(std::round(pos.x + (size.x - t.x) * 0.5f), std::round(pos.y + (size.y - t.y) * 0.5f)),
                g_pal->faint, text.c_str());
    return true;
}

// ------------------------------------------------------------------ resize mode, focus follows mouse
// The resize mode holds the keyboard in a small invisible popup over the bar's hint, so no key
// reaches a terminal; a click anywhere, Esc, Enter, the mode's key again or the window losing
// focus ends it.
void draw_tiling_overlay(App::State& s) {
    auto& ts = s.styles.tiling;
    if (ts.resizing && (s.layout().style != "tiling" || !s.window_focused))
        leave_resize(s);
    if (ts.resizing && !ts.popup_open) {
        ImGui::OpenPopup(kResizePopup);
        ts.popup_open = true;
    }
    if (ts.popup_open) {
        ImGui::SetNextWindowPos(ts.hint_min);
        ImGui::SetNextWindowSize(ImVec2(std::max(ts.hint_size.x, 1.0f), std::max(ts.hint_size.y, 1.0f)));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 0.0f);
        const bool open =
            ImGui::BeginPopup(kResizePopup, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove |
                                                ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoNav);
        ImGui::PopStyleVar(2);
        if (open) {
            if (!ts.resizing) {
                ImGui::CloseCurrentPopup();
                ts.popup_open = false;
            } else {
                const ImGuiIO& io = ImGui::GetIO();
                static constexpr std::pair<ImGuiKey, tl::Key> kKeys[] = {
                    {ImGuiKey_H, tl::Key::kH},
                    {ImGuiKey_J, tl::Key::kJ},
                    {ImGuiKey_K, tl::Key::kK},
                    {ImGuiKey_L, tl::Key::kL},
                    {ImGuiKey_LeftArrow, tl::Key::kLeft},
                    {ImGuiKey_DownArrow, tl::Key::kDown},
                    {ImGuiKey_UpArrow, tl::Key::kUp},
                    {ImGuiKey_RightArrow, tl::Key::kRight},
                    {ImGuiKey_Escape, tl::Key::kEscape},
                    {ImGuiKey_Enter, tl::Key::kEnter},
                    {ImGuiKey_KeypadEnter, tl::Key::kEnter}};
                bool leave = false;
                // The mode's own key ends it too (whatever keys.json binds it to).
                for (const auto* map : {&s.keymap.layout_map(), &s.keymap.global_map()})
                    for (const auto& [chord, cmd] : *map)
                        if (cmd == "tiling.resize_mode" && ImGui::IsKeyChordPressed(chord))
                            leave = true;
                if (!leave && !io.KeyCtrl && !io.KeyAlt) {
                    for (const auto& [key, k] : kKeys) {
                        if (!ImGui::IsKeyPressed(key))
                            continue;
                        const tl::ResizeAction a = tl::resize_key(k, io.KeyShift);
                        if (a.leave)
                            leave = true;
                        else
                            run_command(s, a.command);
                    }
                }
                if (leave) {
                    ImGui::CloseCurrentPopup();
                    ts.popup_open = false;
                    leave_resize(s);
                }
            }
            ImGui::EndPopup();
        } else {  // closed from outside: a click elsewhere
            ts.popup_open = false;
            if (ts.resizing)
                leave_resize(s);
        }
    }
    if (ts.resizing || Clock::now() < ts.flash_until)
        if (s.config.wake)
            s.config.wake();

    // Focus follows the mouse: resting 150 ms on another pane focuses it.
    if (!s.settings.styles.tiling_focus_follows_mouse || ts.resizing ||
        ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId) || ImGui::IsAnyMouseDown() ||
        !s.drag_pane.empty()) {
        ts.hover_pane.clear();
        return;
    }
    const ImVec2 m = ImGui::GetIO().MousePos;
    std::string under;
    for (const auto& [id, r] : s.pane_rects)
        if (m.x >= r.x && m.x < r.x + r.w && m.y >= r.y && m.y < r.y + r.h)
            under = id;
    if (under.empty() || under == s.ws.focused_session()) {
        ts.hover_pane.clear();
        return;
    }
    if (under != ts.hover_pane) {
        ts.hover_pane = under;
        ts.hover_since = Clock::now();
    }
    if (Clock::now() - ts.hover_since >= std::chrono::milliseconds(150)) {
        s.ws.focus(under);
        s.focus_view(under);
        ts.hover_pane.clear();
    } else if (s.config.wake) {
        s.config.wake();
    }
}

// The gaps setting (and its toggle) put on the tree's tab block.
std::optional<lay::Node> tiling_layout_tree(const App::State& s, const lay::Node& tree) {
    lay::Node t = tree;
    if (!tl::apply_gaps(t, s.settings.styles.tiling_gaps, s.settings.styles.tiling_gaps_on))
        return std::nullopt;
    return t;
}

// ------------------------------------------------------------------ Settings
// The card: a thin bar at the top, three square panes (one large, two small), the current one
// framed 2 px in the accent.
void tiling_layout_preview(ImDrawList* d, ImVec2 a, ImVec2 size) {
    const ImVec2 b(a.x + size.x, a.y + size.y);
    d->AddRectFilled(a, b, g_pal->bg, px(6));
    d->AddRectFilled(a, ImVec2(b.x, a.y + px(4)), g_pal->sidebar, px(6), ImDrawFlags_RoundCornersTop);
    for (int i = 0; i < 3; ++i)
        d->AddRectFilled(ImVec2(a.x + px(3) + static_cast<float>(i) * px(4), a.y + px(1)),
                         ImVec2(a.x + px(6) + static_cast<float>(i) * px(4), a.y + px(3)),
                         i == 0 ? g_pal->focus : g_pal->faint);
    const float g = px(3);
    const ImVec2 in(a.x + g, a.y + px(4) + g);
    const ImVec2 out(b.x - g, b.y - g);
    const float lw = (out.x - in.x - g) * 0.58f;
    const float mid = in.y + (out.y - in.y - g) * 0.5f;
    auto pane = [&](ImVec2 p, ImVec2 q, bool on) {
        d->AddRectFilled(p, q, g_pal->term_bg);
        d->AddRect(p, q, on ? g_pal->focus : g_pal->border, 0.0f, 0, on ? px(1.5f) : 1.0f);
        d->AddRectFilled(ImVec2(p.x + px(3), p.y + px(3)),
                         ImVec2(p.x + px(3) + (q.x - p.x) * 0.45f, p.y + px(5)), g_pal->faint);
    };
    pane(in, ImVec2(in.x + lw, out.y), true);
    pane(ImVec2(in.x + lw + g, in.y), ImVec2(out.x, mid), false);
    pane(ImVec2(in.x + lw + g, mid + g), out, false);
    d->AddRect(a, b, g_pal->border, px(6));
}

// Gaps (a stepper), Pane titles (auto, always, never) and Focus follows mouse.
bool look_tiling_options(App::State& s) {
    bool changed = false;
    auto& st = s.settings.styles;
    const std::string chord = style_chord_for(s, "tiling.gaps");
    const std::string note = "Space around and between panes." +
                             (chord.empty() ? std::string() : " " + chord + " turns them off and on.");
    settings_row(s, "Gaps", note.c_str(), px(kStepperW), px(kStepperH));
    if (stepper(s, "tiling-gaps", st.tiling_gaps, 2.0f, 0.0f, 24.0f, "%.0f px"))
        changed = true;
    settings_row_done(s);
    settings_row(s, "Pane titles", "A title row on each pane; Auto shows it with two panes or more.", px(240),
                 px(32));
    if (segmented(s, "##tiling-titles", st.tiling_titles,
                  {{"auto", "Auto"}, {"always", "Always"}, {"never", "Never"}}))
        changed = true;
    settings_row_done(s);
    settings_row(s, "Focus follows mouse", "Resting the pointer on a pane for a moment focuses it.", px(36),
                 px(20));
    if (settings_toggle("##tiling-ffm", st.tiling_focus_follows_mouse)) {
        st.tiling_focus_follows_mouse = !st.tiling_focus_follows_mouse;
        changed = true;
    }
    settings_row_done(s);
    return changed;
}

// tiling.resize_mode and tiling.gaps.
std::optional<std::string> exec_tiling_command(App::State& s, const std::string& name) {
    if (name == "tiling.resize_mode") {
        if (s.layout().style != "tiling")
            return std::string("not in this layout");
        if (s.styles.tiling.resizing)
            leave_resize(s);
        else
            s.styles.tiling.resizing = true;
        return std::string();
    }
    if (name == "tiling.gaps") {
        s.settings.styles.tiling_gaps_on = !s.settings.styles.tiling_gaps_on;
        save_settings(s.settings);
        return std::string();
    }
    return std::nullopt;
}

}  // namespace detail
}  // namespace agentdesk::desktop
