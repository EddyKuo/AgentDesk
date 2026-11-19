// Notices: the update pill, agent-host's update error, toasts and the sub-agent peek window.
#include "app_internal.hpp"

namespace agentdesk::desktop {
namespace detail {

namespace {
// Toasts (mods/toast): 340 wide, bottom-right 16 px in, 8 px apart; a dot coloured by level;
// title 13 semibold and body 12 muted, one line each; click runs its action and dismisses.
// ---- the update pill (herdr window.rs 4820-4890) and agent-host's update error (4800-4815)
// What's waiting, in words (window.rs 4824-4833).
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

// Height taken at the bottom right (34 + 8), 0 when hidden: toasts go above it (4727-4732).
float draw_update_pill(App::State& s) {
    // 4835-4841. herdr also shows "Updated to <version>" when no release notes opened by
    // themselves; AgentDesk's What's new always opens after an update (draw_notes), so it never
    // shows here.
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
    const float restart_w = app_ready ? s.fonts.ui_bold->CalcTextSizeA(fpx(13), FLT_MAX, 0, "Restart").x + px(20) : 0.0f;
    const float notes_w = s.fonts.ui->CalcTextSizeA(fpx(13), FLT_MAX, 0, notes).x + px(12);
    const float gap = px(10);
    const float w = px(14) + label_w + gap + (app_ready ? restart_w + gap : 0.0f) + notes_w + gap + px(24) + px(6);
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x - px(16) - w, vp->WorkPos.y + vp->WorkSize.y - px(16) - h));
    ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, h * 0.5f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::GetStyle().Colors[ImGuiCol_PopupBg]);
    ImGui::Begin("##update-pill", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                     ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 wp = ImGui::GetWindowPos();
    const float cy = wp.y + h * 0.5f;
    float x = wp.x + px(14);
    const float th = s.fonts.ui->CalcTextSizeA(fpx(13), FLT_MAX, 0, "Ag").y;
    dl->AddText(s.fonts.ui, fpx(13), ImVec2(x, cy - th * 0.5f), g_pal->text, label->c_str());
    x += label_w + gap;
    if (app_ready) {
        // 4859-4875: "Restart": 24 high, px 10, round, accent, semibold.
        ImGui::SetCursorScreenPos(ImVec2(x, cy - px(12)));
        if (ImGui::InvisibleButton("##update-restart", ImVec2(restart_w, px(24))))
            run_command(s, "app.update");
        const bool hov = ImGui::IsItemHovered();
        dl->AddRectFilled(ImVec2(x, cy - px(12)), ImVec2(x + restart_w, cy + px(12)),
                          hov ? alpha(g_pal->focus, 230) : g_pal->focus, px(12));
        dl->AddText(s.fonts.ui_bold, fpx(13), ImVec2(x + px(10), cy - th * 0.5f), accent_foreground(), "Restart");
        x += restart_w + gap;
    }
    // 4876-4881: "What changed" / "Open Updates" in the accent colour: the Updates page.
    ImGui::SetCursorScreenPos(ImVec2(x, cy - px(12)));
    if (ImGui::InvisibleButton("##update-notes", ImVec2(notes_w, px(24))))
        run_command(s, "app.settings:updates");
    dl->AddText(s.fonts.ui, fpx(13), ImVec2(x + px(6), cy - th * 0.5f), g_pal->focus, notes);
    x += notes_w + gap;
    // 4882-4897: x dismisses this one.
    ImGui::SetCursorScreenPos(ImVec2(x, cy - px(12)));
    if (ImGui::InvisibleButton("##update-dismiss", ImVec2(px(24), px(24))))
        s.update_dismissed = dismissal;
    const bool hov = ImGui::IsItemHovered();
    if (hov)
        dl->AddCircleFilled(ImVec2(x + px(12), cy), px(12), raised_color());
    const ImVec2 xt = s.fonts.ui->CalcTextSizeA(fpx(13), FLT_MAX, 0, "\xC3\x97");
    dl->AddText(s.fonts.ui, fpx(13), ImVec2(x + px(12) - xt.x * 0.5f, cy - xt.y * 0.5f), hov ? g_pal->text : g_pal->faint,
                "\xC3\x97");
    ImGui::End();
    return h + px(8);
}

// 4800-4815: agent-host's update failed (until dismissed): open Updates, or x. `bottom`: how
// far up from the bottom the toasts reach.
void draw_core_update_error(App::State& s, float bottom) {
    const auto core = ui::updates::core();
    if (!core.error || core.managed || s.core_update_error_dismissed == core.error)
        return;
    const std::string text = "agent-host update failed: " + *core.error + " \xC2\xB7 Open Updates";
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float max_w = px(540);
    const float tw = std::min(s.fonts.ui->CalcTextSizeA(fpx(13), FLT_MAX, 0, text.c_str()).x, max_w - px(24) - px(32));
    const float w = px(12) + tw + px(8) + px(24) + px(12);
    const float h = px(12) * 2 + px(24);
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x - px(16) - w, vp->WorkPos.y + vp->WorkSize.y - bottom - h));
    ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, px(8));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::GetStyle().Colors[ImGuiCol_PopupBg]);
    ImGui::Begin("##core-update-error", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                     ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 wp = ImGui::GetWindowPos();
    const float cy = wp.y + h * 0.5f;
    ImGui::SetCursorScreenPos(ImVec2(wp.x + px(12), wp.y + px(12)));
    if (ImGui::InvisibleButton("##core-update-error-open", ImVec2(tw, px(24))))
        run_command(s, "app.settings:updates");
    text_clipped(dl, s.fonts.ui, fpx(13), ImVec2(wp.x + px(12), cy - fpx(13) * 0.6f), g_pal->text, text, tw);
    const float xx = wp.x + px(12) + tw + px(8);
    ImGui::SetCursorScreenPos(ImVec2(xx, cy - px(12)));
    if (ImGui::InvisibleButton("##core-update-error-dismiss", ImVec2(px(24), px(24))))
        s.core_update_error_dismissed = core.error;  // window.rs 3973-3976
    const bool hov = ImGui::IsItemHovered();
    if (hov)
        dl->AddCircleFilled(ImVec2(xx + px(12), cy), px(12), raised_color());
    const ImVec2 xt = s.fonts.ui->CalcTextSizeA(fpx(13), FLT_MAX, 0, "\xC3\x97");
    dl->AddText(s.fonts.ui, fpx(13), ImVec2(xx + px(12) - xt.x * 0.5f, cy - xt.y * 0.5f), hov ? g_pal->text : g_pal->faint,
                "\xC3\x97");
    ImGui::End();
}

}  // namespace

void draw_toasts(App::State& s) {
    const auto now = Clock::now();
    std::erase_if(s.toasts, [&](const Toast& t) { return t.until < now; });
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    // window.rs 4727-4732: the update pill sits at the bottom; toasts go above it.
    const float pill = draw_update_pill(s);
    float y = vp->WorkPos.y + vp->WorkSize.y - px(16) - pill;
    const float w = px(340);
    int n = 0;
    // window.rs 4794-4796: toast_y grows with each toast in the order opened, so the oldest sits
    // at the bottom and newer ones stack above it.
    for (auto it = s.toasts.begin(); it != s.toasts.end(); ++it, ++n) {
        const auto& t = *it;
        const float h = px(72);
        y -= h;
        ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x - w - px(16), y));
        ImGui::SetNextWindowSize(ImVec2(w, h));
        char name[32];
        std::snprintf(name, sizeof name, "##toast%d", n);
        push_overlay_style();
        ImGui::Begin(name, nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 wp = ImGui::GetWindowPos();
        // toast/main.js 17-37: px 18, gap 12, items centred; the hover background; an 8 px dot,
        // then the title (13 semibold) over the body (12 muted), both truncated, no tooltip.
        if (ImGui::IsWindowHovered())
            dl->AddRectFilled(ImVec2(wp.x + 1, wp.y + 1), ImVec2(wp.x + w - 1, wp.y + h - 1), g_pal->hover, px(7));
        const float ty = wp.y + (t.body.empty() ? h * 0.5f - fpx(13) * 0.6f : h * 0.5f - px(17));
        dl->AddCircleFilled(ImVec2(wp.x + px(18) + px(4), wp.y + h * 0.5f), px(4), t.color);
        const float tx = wp.x + px(18) + px(8) + px(12);
        text_clipped(dl, s.fonts.ui_bold, fpx(13), ImVec2(tx, ty), g_pal->text, t.title, wp.x + w - px(18) - tx);
        if (!t.body.empty()) {
            std::string body = t.body;
            std::replace(body.begin(), body.end(), '\n', ' ');
            text_clipped(dl, s.fonts.ui, fpx(12), ImVec2(tx, ty + px(20)), g_pal->dim, body, wp.x + w - px(18) - tx);
        }
        if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            if (!t.session.empty())
                s.focus(t.session);
            it->until = now;  // dismiss
        }
        ImGui::End();
        pop_overlay_style();
        y -= px(8);
    }
    // window.rs 4800-4801: after the toasts, at the next toast_y.
    draw_core_update_error(s, vp->WorkPos.y + vp->WorkSize.y - y);
}

// herdr window.rs 4764-4777: a dialog dims the window behind it and a press outside it closes it
// (ImGui modals dim; the press is ours to catch). Call inside the dialog's popup.
bool dialog_outside_click() {
    // Behind a modal nothing is hoverable, so "no window hovered" is outside the dialog and the
    // popups it opened (a combo list).
    // A press on one of the dialog's own widgets makes it the active item in that same frame;
    // without AllowWhenBlockedByActiveItem the dialog would then not count as hovered and close.
    return !ImGui::IsWindowAppearing() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
           !ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
}

// Peek (window.rs 3949-3960, mods/peek): a dialog 80 % x 80 %, a 34 px bar over the live
// terminal. Esc stays with the terminal (window.rs 4757: an overlay with a layout takes no
// Overlay keys), so it closes by ✕, a press outside, or ↗.
void draw_peek(App::State& s) {
    const bool want = !s.peek.empty() && s.views_by_id.count(s.peek) != 0 && s.by_id.count(s.peek) != 0;
    if (!want)
        s.peek.clear();
    if (want && !ImGui::IsPopupOpen("##peek"))
        ImGui::OpenPopup("##peek");
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float w = vp->WorkSize.x * 0.8f;
    const float h = vp->WorkSize.y * 0.8f;
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + (vp->WorkSize.x - w) * 0.5f, vp->WorkPos.y + (vp->WorkSize.y - h) * 0.5f));
    ImGui::SetNextWindowSize(ImVec2(w, h));
    push_overlay_style();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    const bool open = ImGui::BeginPopupModal("##peek", nullptr,
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
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float bw = ImGui::GetContentRegionAvail().x;
    // peek/main.js 52-74: surface, bottom border; px 12, gap 8, 12 px text.
    dl->AddRectFilled(p, ImVec2(p.x + bw, p.y + px(34)), g_pal->card, px(8), ImDrawFlags_RoundCornersTop);
    dl->AddLine(ImVec2(p.x, p.y + px(34) - 1), ImVec2(p.x + bw, p.y + px(34) - 1), g_pal->border);
    const float cy = p.y + px(17);
    const Attention att = s.attention_of(s.peek);
    float x = p.x + px(12);
    // Glyph: spinner working, ○ idle, ● a status, · none (peek/main.js 51).
    if (att == Attention::kWorking)
        status_mark(s, dl, x, cy, att, true);
    else if (att == Attention::kIdle)
        dl->AddCircle(ImVec2(x + px(6), cy), px(3.5f), g_pal->idle, 0, px(1.3f));
    else if (att != Attention::kNone)
        dl->AddCircleFilled(ImVec2(x + px(6), cy), px(3.5f), attention_color(att));
    else
        dl->AddCircleFilled(ImVec2(x + px(6), cy), px(1.2f), g_pal->dim);
    x += px(12) + px(8);
    program_icon(dl, program_of(v), ImVec2(x, cy - px(7)), px(14), g_pal->text);  // peek/main.js 49
    x += px(14) + px(8);
    x += text_clipped(dl, s.fonts.ui_bold, fpx(12), ImVec2(x, cy - fpx(12) * 0.6f), g_pal->text, s.pane_title(s.peek),
                      px(260)) + px(8);
    // The status word only when there is a status (peek/main.js 63).
    const char* word = att == Attention::kWorking   ? "working"
                       : att == Attention::kBlocked ? "needs you"
                       : att == Attention::kDone    ? "finished"
                       : att == Attention::kIdle    ? "idle"
                                                    : nullptr;
    if (word != nullptr) {
        dl->AddText(s.fonts.ui, fpx(12), ImVec2(x, cy - fpx(12) * 0.6f), attention_color(att), word);
        x += s.fonts.ui->CalcTextSizeA(fpx(12), FLT_MAX, 0, word).x + px(8);
    }
    if (const auto pit = s.views_by_id.find(v.parent); pit != s.views_by_id.end())
        text_clipped(dl, s.fonts.ui, fpx(12), ImVec2(x, cy - fpx(12) * 0.6f), g_pal->dim, "from " + s.pane_title(v.parent),
                     px(200));
    // Buttons: px 8, py 2, radius 5, muted; hover background + foreground.
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
        dl->AddText(s.fonts.ui, fpx(12), ImVec2(a.x + px(8), cy - fpx(12) * 0.6f), hov ? g_pal->text : muted, label);
        bx -= px(8);
        return hit;
    };
    const bool close = button("##peekx", "\xE2\x9C\x95") || dialog_outside_click();
    const bool has_pane = s.ws.find(s.peek).space >= 0;
    std::string cmd;
    if (button("##go", has_pane ? "\xE2\x86\x97 go to" : "\xE2\x86\x97 keep"))
        cmd = has_pane ? "agent.focus:" + s.peek : "agent.keep:" + s.peek;
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + px(34)));
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
