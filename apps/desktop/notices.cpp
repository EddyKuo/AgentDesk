// Notices in the bottom-right corner (the update pill, agent-host's update error, toasts) and
// the peek window over a sub-agent's live terminal.
// Portions adapted from herdr-app (Apache-2.0) and modified; see THIRD_PARTY_NOTICES.md.
#include "overlay_kit.hpp"

namespace agentdesk::desktop {
namespace detail {

namespace {
// What is waiting to be installed, in words; nothing when nothing is (or agent-host's update
// is managed elsewhere or already running).
std::optional<std::string> update_label() {
    const auto core = ui::updates::core();
    const auto c = core.ready && !core.updating && !core.managed ? core.ready : std::nullopt;
    const auto a = ui::updates::ready();
    if (a && c)
        return "AgentDesk " + *a + " and agent-host " + *c + " are ready";
    if (a)
        return "AgentDesk " + *a + " is ready";
    if (c)
        return "agent-host " + *c + " is ready";
    return std::nullopt;
}

// A corner notice's window: no decoration, focus or navigation, the popup background.
void begin_corner_window(const char* name) {
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::GetStyle().Colors[ImGuiCol_PopupBg]);
    ImGui::Begin(name, nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove);
    ImGui::PopStyleColor();
}

// A 24 px round "×" button with its left edge at `x`, centred on `cy`: raised under the mouse.
// Returns true when clicked.
bool dismiss_button(App::State& s, ImDrawList* dl, const char* id, float x, float cy) {
    ImGui::SetCursorScreenPos(ImVec2(x, cy - px(12)));
    const bool clicked = ImGui::InvisibleButton(id, ImVec2(px(24), px(24)));
    const bool hov = ImGui::IsItemHovered();
    if (hov)
        dl->AddCircleFilled(ImVec2(x + px(12), cy), px(12), raised_color());
    const ImVec2 xt = s.fonts.ui->CalcTextSizeA(fpx(13), FLT_MAX, 0, "\xC3\x97");
    dl->AddText(s.fonts.ui, fpx(13), ImVec2(x + px(12) - xt.x * 0.5f, cy - xt.y * 0.5f),
                hov ? g_pal->text : g_pal->faint, "\xC3\x97");
    return clicked;
}

// The update pill, a 34 px capsule 16 px in from the bottom right: what is ready, "Restart" when
// AgentDesk itself is, a link to the Updates page, and × to dismiss this one. Returns the height
// it takes (34 + 8 px), or 0 when hidden; the toasts go above it.
float draw_update_pill(App::State& s) {
    // No "Updated to <version>" note here: What's new always opens after an update (draw_notes).
    const auto app_version = ui::updates::ready();
    const bool app_ready = app_version.has_value();
    const auto label = update_label();
    if (!label)
        return 0.0f;
    const std::string dismissal = app_version ? "app:" + *app_version : *label;
    if (s.update_dismissed == dismissal)
        return 0.0f;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float h = px(34);
    const char* notes = app_ready ? "What changed" : "Open Updates";
    const float label_w = s.fonts.ui->CalcTextSizeA(fpx(13), FLT_MAX, 0, label->c_str()).x;
    const float restart_w =
        app_ready ? s.fonts.ui_bold->CalcTextSizeA(fpx(13), FLT_MAX, 0, "Restart").x + px(20) : 0.0f;
    const float notes_w = s.fonts.ui->CalcTextSizeA(fpx(13), FLT_MAX, 0, notes).x + px(12);
    const float gap = px(10);
    const float w =
        px(14) + label_w + gap + (app_ready ? restart_w + gap : 0.0f) + notes_w + gap + px(24) + px(6);
    ImGui::SetNextWindowPos(
        ImVec2(vp->WorkPos.x + vp->WorkSize.x - px(16) - w, vp->WorkPos.y + vp->WorkSize.y - px(16) - h));
    ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, h * 0.5f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    begin_corner_window("##update-pill");
    ImGui::PopStyleVar(3);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 wp = ImGui::GetWindowPos();
    const float cy = wp.y + h * 0.5f;
    float x = wp.x + px(14);
    const float th = s.fonts.ui->CalcTextSizeA(fpx(13), FLT_MAX, 0, "Ag").y;
    dl->AddText(s.fonts.ui, fpx(13), ImVec2(x, cy - th * 0.5f), g_pal->text, label->c_str());
    x += label_w + gap;
    if (app_ready) {  // "Restart": 24 px, 10 px in, round, accent, semibold
        ImGui::SetCursorScreenPos(ImVec2(x, cy - px(12)));
        if (ImGui::InvisibleButton("##update-restart", ImVec2(restart_w, px(24))))
            run_command(s, "app.update");
        const bool hov = ImGui::IsItemHovered();
        dl->AddRectFilled(ImVec2(x, cy - px(12)), ImVec2(x + restart_w, cy + px(12)),
                          hov ? alpha(g_pal->focus, 230) : g_pal->focus, px(12));
        dl->AddText(s.fonts.ui_bold, fpx(13), ImVec2(x + px(10), cy - th * 0.5f), accent_foreground(),
                    "Restart");
        x += restart_w + gap;
    }
    // The link, in the accent colour, opens the Updates page.
    ImGui::SetCursorScreenPos(ImVec2(x, cy - px(12)));
    if (ImGui::InvisibleButton("##update-notes", ImVec2(notes_w, px(24))))
        run_command(s, "app.settings:updates");
    dl->AddText(s.fonts.ui, fpx(13), ImVec2(x + px(6), cy - th * 0.5f), g_pal->focus, notes);
    x += notes_w + gap;
    if (dismiss_button(s, dl, "##update-dismiss", x, cy))
        s.update_dismissed = dismissal;
    ImGui::End();
    return h + px(8);
}

// agent-host's update failed: a notice that opens Updates when clicked, until dismissed (the
// same error stays dismissed). `bottom`: how far up from the window's bottom the toasts reach.
void draw_core_update_error(App::State& s, float bottom) {
    const auto core = ui::updates::core();
    if (!core.error || core.managed || s.core_update_error_dismissed == core.error)
        return;
    const std::string text = "agent-host update failed: " + *core.error + " \xC2\xB7 Open Updates";
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float max_w = px(540);
    const float tw =
        std::min(s.fonts.ui->CalcTextSizeA(fpx(13), FLT_MAX, 0, text.c_str()).x, max_w - px(24) - px(32));
    const float w = px(12) + tw + px(8) + px(24) + px(12);
    const float h = px(12) * 2 + px(24);
    ImGui::SetNextWindowPos(
        ImVec2(vp->WorkPos.x + vp->WorkSize.x - px(16) - w, vp->WorkPos.y + vp->WorkSize.y - bottom - h));
    ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, px(8));
    begin_corner_window("##core-update-error");
    ImGui::PopStyleVar(2);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 wp = ImGui::GetWindowPos();
    const float cy = wp.y + h * 0.5f;
    ImGui::SetCursorScreenPos(ImVec2(wp.x + px(12), wp.y + px(12)));
    if (ImGui::InvisibleButton("##core-update-error-open", ImVec2(tw, px(24))))
        run_command(s, "app.settings:updates");
    text_clipped(dl, s.fonts.ui, fpx(13), ImVec2(wp.x + px(12), cy - fpx(13) * 0.6f), g_pal->text, text, tw);
    if (dismiss_button(s, dl, "##core-update-error-dismiss", wp.x + px(12) + tw + px(8), cy))
        s.core_update_error_dismissed = core.error;
    ImGui::End();
}

}  // namespace

// Toasts: 340 x 72, 16 px in from the bottom right and 8 px apart, above the update pill. Each
// has an 8 px dot in its level's colour, the title (13 px semibold) over the body (12 px dim),
// both cut to one line; a click focuses its session, if any, and dismisses it.
void draw_toasts(App::State& s) {
    const auto now = Clock::now();
    std::erase_if(s.toasts, [&](const Toast& t) { return t.until < now; });
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float pill = draw_update_pill(s);
    float y = vp->WorkPos.y + vp->WorkSize.y - px(16) - pill;
    // The Mac layout keeps its Dock at the bottom: notes drop in under the menu bar, top right.
    const bool top = s.layout().style == "mac" && !s.settings.focus_on;
    if (top)
        y = vp->WorkPos.y + px(28) + px(10);
    const float w = px(340);
    int n = 0;
    // In the order opened: the oldest at the bottom (at the top under the menu bar), newer ones
    // stacked above it (below it).
    for (auto it = s.toasts.begin(); it != s.toasts.end(); ++it, ++n) {
        const auto& t = *it;
        const float h = px(72);
        if (!top)
            y -= h;
        ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x - w - px(16), y));
        ImGui::SetNextWindowSize(ImVec2(w, h));
        char name[32];
        std::snprintf(name, sizeof name, "##toast%d", n);
        push_overlay_style();
        ImGui::Begin(name, nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
                         ImGuiWindowFlags_NoMove);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 wp = ImGui::GetWindowPos();
        if (ImGui::IsWindowHovered())
            dl->AddRectFilled(ImVec2(wp.x + 1, wp.y + 1), ImVec2(wp.x + w - 1, wp.y + h - 1), g_pal->hover,
                              px(7));
        const float ty = wp.y + (t.body.empty() ? h * 0.5f - fpx(13) * 0.6f : h * 0.5f - px(17));
        dl->AddCircleFilled(ImVec2(wp.x + px(18) + px(4), wp.y + h * 0.5f), px(4), t.color);
        const float tx = wp.x + px(18) + px(8) + px(12);
        text_clipped(dl, s.fonts.ui_bold, fpx(13), ImVec2(tx, ty), g_pal->text, t.title,
                     wp.x + w - px(18) - tx);
        if (!t.body.empty()) {
            std::string body = t.body;
            std::replace(body.begin(), body.end(), '\n', ' ');
            text_clipped(dl, s.fonts.ui, fpx(12), ImVec2(tx, ty + px(20)), g_pal->dim, body,
                         wp.x + w - px(18) - tx);
        }
        if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            if (!t.session.empty())
                s.focus(t.session);
            it->until = now;  // dismiss
        }
        ImGui::End();
        pop_overlay_style();
        y += top ? h + px(8) : -px(8);
    }
    // Above the toasts (above the update pill when the toasts are at the top).
    draw_core_update_error(s, top ? px(16) + pill : vp->WorkPos.y + vp->WorkSize.y - y);
}

// A press outside a dialog closes it (ImGui modals dim the window but leave the press to us).
// Call inside the dialog's popup.
bool dialog_outside_click() {
    // Behind a modal nothing is hoverable, so "no window hovered" is outside the dialog and the
    // popups it opened (a combo list).
    // A press on one of the dialog's own widgets makes it the active item in that same frame;
    // without AllowWhenBlockedByActiveItem the dialog would then not count as hovered and close.
    return !ImGui::IsWindowAppearing() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
           !ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow |
                                   ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
}

namespace {
// The peek window's 34 px bar: the agent's status glyph, program icon, name and status word,
// who started it, then "go to" (or "keep") and ✕ at the right. Returns the command to run after
// the frame (empty: none); `close` is set when the window should close.
std::string peek_bar(App::State& s, const ui::SessionView& v, bool& close) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float bw = ImGui::GetContentRegionAvail().x;
    dl->AddRectFilled(p, ImVec2(p.x + bw, p.y + px(34)), g_pal->card, px(8), ImDrawFlags_RoundCornersTop);
    dl->AddLine(ImVec2(p.x, p.y + px(34) - 1), ImVec2(p.x + bw, p.y + px(34) - 1), g_pal->border);
    const float cy = p.y + px(17);
    const Attention att = s.attention_of(s.peek);
    float x = p.x + px(12);
    attention_glyph(s, dl, x, x + px(6), cy, att, g_pal->dim);
    x += px(12) + px(8);
    program_icon(dl, program_of(v), ImVec2(x, cy - px(7)), px(14), g_pal->text);
    x += px(14) + px(8);
    x += text_clipped(dl, s.fonts.ui_bold, fpx(12), ImVec2(x, cy - fpx(12) * 0.6f), g_pal->text,
                      s.pane_title(s.peek), px(260)) +
         px(8);
    const char* word = att == Attention::kWorking   ? "working"
                       : att == Attention::kBlocked ? "needs you"
                       : att == Attention::kDone    ? "finished"
                       : att == Attention::kIdle    ? "idle"
                                                    : nullptr;
    if (word != nullptr) {  // only when there is a status
        dl->AddText(s.fonts.ui, fpx(12), ImVec2(x, cy - fpx(12) * 0.6f), attention_color(att), word);
        x += s.fonts.ui->CalcTextSizeA(fpx(12), FLT_MAX, 0, word).x + px(8);
    }
    if (const auto pit = s.views_by_id.find(v.parent); pit != s.views_by_id.end())
        text_clipped(dl, s.fonts.ui, fpx(12), ImVec2(x, cy - fpx(12) * 0.6f), g_pal->dim,
                     "from " + s.pane_title(v.parent), px(200));
    // Buttons from the right: 20 px high, 8 px in, rounded 5, muted; hover background + foreground.
    const ImU32 muted = mix(g_pal->dim, g_pal->text, 0.4f);
    float bx = p.x + bw - px(12);
    auto button = [&](const char* id, const char* label) {
        const float tw = s.fonts.ui->CalcTextSizeA(fpx(12), FLT_MAX, 0, label).x + px(16);
        bx -= tw;
        const ImVec2 a(bx, cy - px(10));
        ImGui::SetCursorScreenPos(a);
        const bool hit = ImGui::InvisibleButton(id, ImVec2(tw, px(20)));
        const bool hov = ImGui::IsItemHovered();
        if (hov)
            dl->AddRectFilled(a, ImVec2(a.x + tw, a.y + px(20)), g_pal->hover, px(5));
        dl->AddText(s.fonts.ui, fpx(12), ImVec2(a.x + px(8), cy - fpx(12) * 0.6f), hov ? g_pal->text : muted,
                    label);
        bx -= px(8);
        return hit;
    };
    close = button("##peekx", "\xE2\x9C\x95") || dialog_outside_click();
    const bool has_pane = s.ws.find(s.peek).space >= 0;
    std::string cmd;
    if (button("##go", has_pane ? "\xE2\x86\x97 go to" : "\xE2\x86\x97 keep"))
        cmd = has_pane ? "agent.focus:" + s.peek : "agent.keep:" + s.peek;
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + px(34)));
    return cmd;
}

}  // namespace

// Peek: a dialog at 80 % of the window with a bar over the sub-agent's live terminal. Esc goes
// to the terminal, so the window closes by ✕, a press outside it, or ↗.
void draw_peek(App::State& s) {
    const bool want = !s.peek.empty() && s.views_by_id.count(s.peek) != 0 && s.by_id.count(s.peek) != 0;
    if (!want)
        s.peek.clear();
    if (want && !ImGui::IsPopupOpen("##peek"))
        ImGui::OpenPopup("##peek");
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    center_next_window(ImVec2(vp->WorkSize.x * 0.8f, vp->WorkSize.y * 0.8f));
    push_overlay_style();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    const bool open =
        ImGui::BeginPopupModal("##peek", nullptr,
                               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar();
    pop_overlay_style();
    if (!open)
        return;
    if (!want) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    const auto& v = s.views_by_id.find(s.peek)->second;
    const auto session = s.by_id.find(s.peek)->second;
    bool close = false;
    const std::string cmd = peek_bar(s, v, close);
    ImGui::BeginChild("##peekbody", ImVec2(0, 0), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    auto& vs = s.views[s.peek];
    vs.view.keymap = &s.keymap;
    vs.view.theme.default_bg = g_pal->term_bg;
    vs.view.theme.default_fg = g_pal->term_fg;
    vs.view.theme.cursor = g_pal->cursor;
    vs.view.theme.selection = g_pal->selection;
    vs.view.theme.ansi = g_pal->ansi;
    vs.view.draw("peek", *session, s.fonts, vs.focus_next || ImGui::IsWindowAppearing());
    vs.focus_next = false;
    std::vector<std::string> commands = vs.view.take_commands();
    ImGui::EndChild();
    if (close || !cmd.empty()) {
        ImGui::CloseCurrentPopup();
        s.peek.clear();
    }
    ImGui::EndPopup();
    for (const auto& c : commands)
        run_command(s, c);
    if (!cmd.empty())
        run_command(s, cmd);
}
}  // namespace detail

}  // namespace agentdesk::desktop
