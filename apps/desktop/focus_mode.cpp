// Focus mode: any layout can give way to one terminal and a 22 px strip with a dot per other
// agent. An agent that needs you makes the window's edge glow instead of a note.
#include "app_internal.hpp"

namespace agentdesk::desktop {
namespace detail {

LayoutSpec& focus_layout(App::State& s) {
    LayoutSpec& l = s.focus_mode.layout;
    if (l.name.empty()) {
        l.name = "focus";
        l.builtin = true;
        l.file = lay::builtin("focus");
        l.tree = l.file;
        derive_layout(l);
    }
    return l;
}

void set_focus(App::State& s, bool on) {
    if (s.settings.focus_on == on)
        return;
    s.settings.focus_on = on;
    save_settings(s.settings);
    s.focus_mode.header = {};
    s.focus_view(s.ws.focused_session());
}

void with_alpha(ImDrawList* dl, float a, const std::function<void()>& fn) {
    const int from = dl->VtxBuffer.Size;
    fn();
    if (a >= 1.0f)
        return;
    const float k = std::clamp(a, 0.0f, 1.0f);
    for (int i = from; i < dl->VtxBuffer.Size; ++i) {
        ImU32& c = dl->VtxBuffer[i].col;
        const auto av = static_cast<float>((c >> IM_COL32_A_SHIFT) & 0xFFu);
        c = (c & ~IM_COL32_A_MASK) | (static_cast<ImU32>(av * k + 0.5f) << IM_COL32_A_SHIFT);
    }
}

namespace {
std::string state_word(Attention a) {
    switch (a) {
        case Attention::kBlocked:
            return "Needs you";
        case Attention::kDone:
            return "Done";
        case Attention::kWorking:
            return "Working";
        case Attention::kIdle:
            return "Idle";
        case Attention::kNone:
            break;
    }
    return "Unknown";
}

// The agent and its sub-agents, most urgent (a sub-agent never counts as done).
Attention agent_attention(App::State& s, const AgentRoster& r, const std::string& id) {
    std::vector<Attention> all{s.attention_of(id)};
    for (const auto& row : ui::agent_tree(r.kids, id))
        all.push_back(ui::kid_status(s.attention_of(row.id)));
    return ui::most_urgent(all);
}

// One dot: 8 px (idle 6), filled for needs you, done and idle, a 1.5 px ring for working (it does
// not spin, so the strip needs no redraws). Hovered: 10 px.
void draw_dot(ImDrawList* dl, ImVec2 c, Attention a, bool hovered) {
    const float grow = hovered ? px(1) : 0.0f;
    switch (a) {
        case Attention::kBlocked:
            dl->AddCircleFilled(c, px(4) + grow, g_pal->needs_you, 16);
            break;
        case Attention::kDone:
            dl->AddCircleFilled(c, px(4) + grow, g_pal->done, 16);
            break;
        case Attention::kWorking:
            dl->AddCircle(c, px(3.25f) + grow, g_pal->working, 16, px(1.5f));
            break;
        case Attention::kIdle:
            dl->AddCircleFilled(c, px(3) + grow, g_pal->faint, 16);
            break;
        case Attention::kNone:
            break;
    }
}

// The left part: the focused terminal's program icon, "name · space" (12 px medium, dim, at most
// 40 % of the strip), or the connection's state. Returns where it ends.
float draw_strip_title(App::State& s, ImDrawList* dl, float x, float cy, float w) {
    if (!s.host->connected()) {
        const bool loading = s.machine_loading("");
        const std::string t = loading ? "Connecting\xE2\x80\xA6" : "agent-host not connected";
        return x + text_clipped(dl, s.fonts.ui_medium, fpx(12), ImVec2(x, cy - fpx(12) * 0.6f),
                                loading ? g_pal->dim : g_pal->needs_you, t, w * 0.4f);
    }
    const std::string id = s.ws.focused_session();
    const auto it = s.views_by_id.find(id);
    if (it == s.views_by_id.end())
        return x;
    program_icon(dl, program_of(it->second), ImVec2(x, cy - px(7)), px(14), g_pal->dim);
    x += px(14) + px(6);
    std::string t = s.pane_title(id);
    if (s.ws.active() != nullptr)
        t += " \xC2\xB7 " + s.ws.space_title(s.ws.active_space, s.session_cwds());
    return x + text_clipped(dl, s.fonts.ui_medium, fpx(12), ImVec2(x, cy - fpx(12) * 0.6f), g_pal->dim, t,
                            std::max(0.0f, w * 0.4f - px(20)));
}

// "Exit focus  Ctrl+Shift+." at the right (just "Exit" when narrow): 11.5 px faint, the text
// colour under the pointer. Returns its left edge.
float draw_strip_exit(App::State& s, ImDrawList* dl, float right, float top, float h, bool narrow) {
    const std::string chord = s.keymap.chord_for("focus.toggle");
    const std::string label =
        narrow || chord.empty() ? (narrow ? "Exit" : "Exit focus") : "Exit focus   " + chord;
    const float fs = fpx(11.5f);
    const float tw = s.fonts.ui->CalcTextSizeA(fs, FLT_MAX, 0, label.c_str()).x;
    const ImVec2 a(right - tw - px(8), top);
    ImGui::SetCursorScreenPos(a);
    if (ImGui::InvisibleButton("##focus-exit", ImVec2(tw + px(8), h)))
        run_command(s, "focus.toggle");
    const bool hov = ImGui::IsItemHovered();
    if (hov)
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    dl->AddText(s.fonts.ui, fs, ImVec2(a.x + px(4), top + (h - fs) * 0.5f - px(1)),
                hov ? g_pal->text : g_pal->faint, label.c_str());
    return a.x;
}

// Windows' minimize, maximize and close, small enough for the strip (Window buttons = Windows).
float draw_strip_window_buttons(App::State& s, float right, float top, float h) {
    if (s.config.hwnd == nullptr || mac_window_buttons(s))
        return right;
    const float b = px(20);
    float x = right - b * 3 - px(4);
    const float y = top + (h - b) * 0.5f;
    const float left = x;
    const bool zoomed = ::IsZoomed(s.config.hwnd) != FALSE;
    if (icon_button(s, "##fwin-min", ui::icon::kMinimize, ImVec2(x, y), b, fpx(13), nullptr, g_pal->dim))
        ::ShowWindow(s.config.hwnd, SW_MINIMIZE);
    x += b + px(2);
    if (icon_button(s, "##fwin-max", zoomed ? ui::icon::kRestore : ui::icon::kMaximize, ImVec2(x, y), b,
                    fpx(13), nullptr, g_pal->dim))
        ::ShowWindow(s.config.hwnd, zoomed ? SW_RESTORE : SW_MAXIMIZE);
    x += b + px(2);
    if (icon_button(s, "##fwin-close", ui::icon::kWinClose, ImVec2(x, y), b, fpx(13), nullptr, g_pal->dim))
        ::PostMessageW(s.config.hwnd, WM_CLOSE, 0, 0);
    return left - px(8);
}

// The dots between the title and the exit, centred in the strip when there is room. A click
// focuses that agent (still in Focus); "+N" opens the picker.
void draw_strip_dots(App::State& s, ImDrawList* dl, float left, float right, float cy, float strip_mid) {
    const AgentRoster roster = agent_roster(s);
    std::vector<ui::focus::DotAgent> agents;
    std::map<std::string, Attention> att;
    long long seq = 0;
    for (const auto& id : roster.ids) {
        const Attention a = agent_attention(s, roster, id);
        att[id] = a;
        const auto c = s.changed.find(id);
        agents.push_back({id, a, c == s.changed.end() ? 0LL : c->second.at, seq++});
    }
    const float dot = px(8);
    const float gap = px(6);
    const float more_w = s.fonts.ui->CalcTextSizeA(fpx(11), FLT_MAX, 0, "+99").x + px(4);
    const float room = std::max(0.0f, right - left);
    const auto dots = ui::focus::strip_dots(agents, s.ws.focused_session(), room, dot, gap, more_w);
    const auto n = static_cast<float>(dots.ids.size());
    float used = n > 0 ? n * dot + (n - 1) * gap : 0.0f;
    std::string more;
    if (dots.overflow > 0) {
        more = "+" + std::to_string(dots.overflow);
        used += (n > 0 ? gap : 0.0f) + s.fonts.ui->CalcTextSizeA(fpx(11), FLT_MAX, 0, more.c_str()).x;
    }
    float x = std::clamp(strip_mid - used * 0.5f, left, std::max(left, right - used));
    const long long now = now_ms();
    for (const auto& id : dots.ids) {
        const ImVec2 c(x + dot * 0.5f, cy);
        // A 14 px wide hit area over the strip's height, so a small dot is easy to hit.
        ImGui::SetCursorScreenPos(ImVec2(c.x - px(7), cy - px(11)));
        ImGui::PushID(id.c_str());
        const bool clicked = ImGui::InvisibleButton("##dot", ImVec2(px(14), px(22)));
        ImGui::PopID();
        const bool hov = ImGui::IsItemHovered();
        draw_dot(dl, c, att[id], hov);
        if (hov) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            const auto& v = s.views_by_id[id];
            std::string tip = program_of(v).name;
            if (const auto loc = s.ws.find(id); loc.space >= 0)
                tip += " \xC2\xB7 " + s.ws.space_title(loc.space, s.session_cwds());
            tip += " \xC2\xB7 " + state_word(att[id]);
            if (const auto ch = s.changed.find(id); ch != s.changed.end() && ch->second.at > 0)
                tip += " \xC2\xB7 " + ui::focus::short_ago(now - ch->second.at);
            ImGui::SetTooltip("%s", tip.c_str());
        }
        if (clicked)
            run_command(s, "agent.focus:" + id);
        x += dot + gap;
    }
    if (!more.empty()) {
        const ImVec2 t = s.fonts.ui->CalcTextSizeA(fpx(11), FLT_MAX, 0, more.c_str());
        ImGui::SetCursorScreenPos(ImVec2(x, cy - px(11)));
        if (ImGui::InvisibleButton("##dots-more", ImVec2(t.x, px(22))))
            run_command(s, "app.picker");
        const bool hov = ImGui::IsItemHovered();
        dl->AddText(s.fonts.ui, fpx(11), ImVec2(x, cy - t.y * 0.5f), hov ? g_pal->text : g_pal->faint,
                    more.c_str());
    }
}
}  // namespace

void draw_focus_strip(App::State& s, ImVec2 pos, float w, float h) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 b(pos.x + w, pos.y + h);
    dl->AddRectFilled(pos, b, g_pal->sidebar);
    dl->AddLine(pos, ImVec2(b.x, pos.y), g_pal->border);
    const float cy = pos.y + h * 0.5f;
    const bool narrow = w < px(480);
    float x = clear_of_traffic_lights(s, pos.x + px(10), pos.y, b.y, px(12));
    if (!narrow)
        x = draw_strip_title(s, dl, x, cy, w);
    float right = draw_strip_window_buttons(s, b.x - px(4), pos.y, h);
    right = draw_strip_exit(s, dl, right, pos.y, h, narrow);
    if (s.host->connected())
        draw_strip_dots(s, dl, x + px(16), right - px(16), cy, pos.x + w * 0.5f);
    window_drag_area(s, pos, b);
}

void draw_focus_empty(App::State& s, ImDrawList* dl, ImVec2 pos, ImVec2 size, bool /*connected*/) {
    dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), g_pal->term_bg);
    StateCard card;
    card.title = "No terminal to focus";
    card.hint = "Open one here, or pick another agent from the dots below.";
    card.buttons = {{with_chord(s, "New terminal", "tab.new"), "tab.new"},
                    {with_chord(s, "Start a program...", "agent.new"), "agent.new"}};
    card.framed = false;
    draw_state_card(s, pos, size, card);
}

bool focus_alert(App::State& s, bool blocked) {
    if (!s.settings.focus_on || !s.window_focused)
        return false;
    const std::string mode = ui::focus::alerts_mode(s.settings.focus_alerts);
    if (mode == "normal")
        return false;
    const auto now = Clock::now();
    const double since = std::chrono::duration<double, std::milli>(now - s.focus_mode.glow_at).count();
    // A second event while one glows starts it over; needing you wins the colour over done.
    const bool glowing = s.focus_mode.glow_peak > 0 && since < ui::focus::kGlowTotal;
    if (blocked || !glowing || s.focus_mode.glow_color != g_pal->needs_you) {
        s.focus_mode.glow_color = blocked ? g_pal->needs_you : g_pal->done;
        s.focus_mode.glow_peak = blocked ? 0xB0 : 0x60;
    }
    s.focus_mode.glow_at = now;
    if (mode == "edge_sound" && s.settings.sounds && !s.config.automated)
        play_alert(s, blocked);
    return true;
}

void draw_focus_glow(App::State& s) {
    if (!s.settings.focus_on || s.focus_mode.glow_peak <= 0)
        return;
    const double t = std::chrono::duration<double, std::milli>(Clock::now() - s.focus_mode.glow_at).count();
    const int a = ui::focus::glow_alpha(t, s.focus_mode.glow_peak);
    if (a <= 0)
        return;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    // AddRect strokes inside the rectangle: the band lies along the window's inner edge.
    const ImVec2 p1(vp->WorkPos.x + vp->WorkSize.x, vp->WorkPos.y + vp->WorkSize.y);
    ImGui::GetWindowDrawList()->AddRect(vp->WorkPos, p1, alpha(s.focus_mode.glow_color, a), 0.0f, 0, px(3));
}

bool focus_animating(const App::State& s) {
    if (!s.settings.focus_on)
        return false;
    const double t = std::chrono::duration<double, std::milli>(Clock::now() - s.focus_mode.glow_at).count();
    return (s.focus_mode.glow_peak > 0 && t < ui::focus::kGlowTotal) || s.focus_mode.header.animating();
}

}  // namespace detail
}  // namespace agentdesk::desktop
