// The Review layout: the agent's terminal on the left and, on the right, the uncommitted changes
// of the folder it works in (list, diff, commit), following the focused agent.
#include "agentdesk/ui/review.hpp"
#include "overlay_kit.hpp"

namespace agentdesk::desktop {
namespace detail {

namespace {
namespace rv = ui::review;

constexpr const char* kChevronLeft = "\xEE\xAA\xB5";  // U+EAB5 codicon chevron-left

bool review_shows(const App::State& s) {
    return s.layout().style == "review";
}

// The session Review follows: the focused pane when it is an agent; when the focus is on a plain
// shell, the last agent (review_follow "agent") or the shell itself ("focused"). With no agent seen
// yet, the shell is shown too, so a folder's changes are there without one. An agent's turn
// ending (Working to anything else) reads its changes again at once.
void update_target(App::State& s) {
    const std::string focused = s.ws.focused_session();
    const auto fv = s.views_by_id.find(focused);
    std::string next = s.review.target;
    if (s.settings.review_follow == "focused" || (fv != s.views_by_id.end() && is_agent(fv->second)))
        next = focused;
    else if (next.empty() || !s.views_by_id.contains(next))
        next = focused;
    const auto tv = s.views_by_id.find(next);
    const std::string state = tv != s.views_by_id.end() ? tv->second.agent_state : std::string();
    if (next != s.review.target) {
        s.review.target = next;
        s.review.committed.clear();
    } else if (s.review.target_state == "Working" && state != "Working" && !state.empty()) {
        s.panels->refresh_changes();
        if (tv != s.views_by_id.end())
            s.git.invalidate(tv->second.cwd);
    }
    s.review.target_state = state;
}

// The target's live folder and machine ("" = this computer); "" without a target.
std::string target_folder(App::State& s, std::string* machine) {
    const auto it = s.views_by_id.find(s.review.target);
    if (it == s.views_by_id.end())
        return {};
    if (machine != nullptr)
        *machine = s.machine_of.count(s.review.target) ? s.machine_of[s.review.target] : std::string();
    return it->second.cwd;
}

// Running agents (other than none) whose checkout is the target's: they all show in one list.
int agents_in_repo(App::State& s, const std::string& top, const std::string& machine) {
    if (top.empty())
        return 0;
    int n = 0;
    for (const auto& [id, v] : s.views_by_id) {
        if (!live_agent(s, id, v))
            continue;
        const std::string m = s.machine_of.count(id) ? s.machine_of[id] : std::string();
        if (m != machine)
            continue;
        const GitInfo g = s.git.get(v.cwd, m);
        if (g.repo && _stricmp(g.top.c_str(), top.c_str()) == 0)
            ++n;
    }
    return n;
}

std::string chord_in_layout(const App::State& s, std::string_view command) {
    for (const auto& [chord, cmd] : s.keymap.layout_map())
        if (cmd == command)
            return ui::Keymap::format(chord);
    return s.keymap.chord_for(command);
}

std::string with_chord(const std::string& label, const std::string& chord) {
    return chord.empty() ? label : label + "  " + chord;
}

// A small pill (22 high, rounded 11) in `col` on its 15 % tint; returns its width.
float pill(App::State& s, ImDrawList* dl, float x, float cy, const std::string& text, ImU32 col,
           bool measure) {
    const ImVec2 t = s.fonts.ui_medium->CalcTextSizeA(fpx(11.5f), FLT_MAX, 0, text.c_str());
    const float w = t.x + px(16);
    if (!measure) {
        dl->AddRectFilled(ImVec2(x, cy - px(11)), ImVec2(x + w, cy + px(11)), alpha(col, 0x26), px(11));
        dl->AddText(s.fonts.ui_medium, fpx(11.5f), ImVec2(x + px(8), cy - t.y * 0.5f), col, text.c_str());
    }
    return w;
}

// ---------------------------------------------------------------- review-head (42 px)
// < > (previous / next agent), the target's icon, name, "space · task" and state; at the right
// the checkout's branch, Refresh, a warning when other agents share the checkout, and the window
// buttons when they are the Windows kind (the tab bar on the left leaves them out here).
void draw_head_contents(App::State& s, ImVec2 pos, float w, float h) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(pos, ImVec2(pos.x + w, pos.y + h), g_pal->sidebar);
    dl->AddLine(ImVec2(pos.x, pos.y + h - 1), ImVec2(pos.x + w, pos.y + h - 1), g_pal->border);
    const float cy = pos.y + h * 0.5f;
    float right = pos.x + w - px(10);
    if (s.config.hwnd != nullptr && !mac_window_buttons(s)) {
        draw_window_buttons(s, right + px(10) - px(6), pos.y, h);
        right -= px(26) * 3 + px(4) + px(12);
    }
    float x = clear_of_traffic_lights(s, pos.x + px(10), pos.y, pos.y + h, px(12));
    const float bs = px(24);
    if (icon_button(s, "##rv-prev", kChevronLeft, ImVec2(x, cy - bs * 0.5f), bs, fpx(14),
                    with_chord("Previous agent", s.keymap.chord_for("agent.prev")).c_str(), g_pal->dim))
        run_command(s, "agent.prev");
    x += bs + px(2);
    if (icon_button(s, "##rv-next", ui::icon::kChevronRight, ImVec2(x, cy - bs * 0.5f), bs, fpx(14),
                    with_chord("Next agent", s.keymap.chord_for("agent.next")).c_str(), g_pal->dim))
        run_command(s, "agent.next");
    x += bs + px(10);

    std::string machine;
    const std::string cwd = target_folder(s, &machine);
    const auto it = s.views_by_id.find(s.review.target);
    // Right side, right to left: branch, refresh, shared-checkout warning.
    const GitInfo g = cwd.empty() ? GitInfo{} : s.git.get(cwd, machine);
    if (g.repo && !g.branch.empty()) {
        std::string b = g.branch;
        if (g.ahead > 0)
            b += " \xE2\x86\x91" + std::to_string(g.ahead);
        if (g.behind > 0)
            b += " \xE2\x86\x93" + std::to_string(g.behind);
        const float bw = std::min(s.fonts.ui->CalcTextSizeA(fpx(12), FLT_MAX, 0, b.c_str()).x, px(180));
        const float bx = right - bw;
        text_clipped(dl, s.fonts.ui, fpx(12), ImVec2(bx, cy - fpx(12) * 0.6f), g_pal->faint, b, bw);
        draw_icon(s, dl, ui::icon::kBranch, fpx(13), ImVec2(bx - px(10), cy), g_pal->faint);
        right = bx - px(22);
    }
    bool loading = false;
    {
        std::lock_guard lock(s.panels->mutex);
        loading = s.panels->changes_loading || s.panels->committing;
    }
    if (!cwd.empty()) {
        const ImVec2 rp(right - bs, cy - bs * 0.5f);
        if (loading) {
            const float t = static_cast<float>(ImGui::GetTime());
            const float a0 = t * (2.0f * 3.14159265f / 0.9f);
            dl->PathArcTo(ImVec2(rp.x + bs * 0.5f, cy), px(5.5f), a0, a0 + 4.4f, 18);
            dl->PathStroke(g_pal->dim, 0, px(1.6f));
        } else if (icon_button(s, "##rv-refresh", ui::icon::kRefresh, rp, bs, fpx(14),
                               with_chord("Refresh changes", s.keymap.chord_for("review.refresh")).c_str(),
                               g_pal->dim)) {
            run_command(s, "review.refresh");
        }
        right = rp.x - px(8);
    }
    if (const int n = agents_in_repo(s, g.top, machine); n >= 2) {
        const std::string t = std::to_string(n) + " agents in this repo";
        const float pw = pill(s, dl, 0, cy, t, g_pal->working, true);
        if (right - pw > x + px(120)) {
            pill(s, dl, right - pw, cy, t, g_pal->working, false);
            if (ImGui::IsMouseHoveringRect(ImVec2(right - pw, cy - px(11)), ImVec2(right, cy + px(11))))
                ImGui::SetTooltip("%s",
                                  "Other agents work in this checkout too: the list holds everyone's\n"
                                  "uncommitted changes. A worktree per agent keeps them apart.");
            right -= pw + px(8);
        }
    }
    // Left: who.
    if (it == s.views_by_id.end()) {
        dl->AddText(s.fonts.ui, fpx(13), ImVec2(x, cy - fpx(13) * 0.6f), g_pal->dim, "No agent selected");
        return;
    }
    const auto& v = it->second;
    program_icon(dl, program_of(v), ImVec2(x, cy - px(7)), px(14), g_pal->text);
    x += px(14) + px(8);
    const Attention att = s.attention_of(s.review.target);
    const float mark_w = status_mark(s, dl, 0, cy, att, false, true);
    const float room = right - x - (mark_w > 0 ? mark_w + px(10) : 0.0f);
    const std::string name = s.title_of(v);
    const float nw =
        text_clipped(dl, s.fonts.ui_bold, fpx(13), ImVec2(x, cy - fpx(13) * 0.6f), g_pal->text, name, room);
    float tx = x + nw;
    std::string detail;
    if (const auto loc = s.ws.find(s.review.target); loc.space >= 0)
        detail = s.ws.space_title(loc.space, s.session_cwds());
    if (const std::string task = agent_detail(s, s.review.target, v); !task.empty() && task != name)
        detail += (detail.empty() ? "" : " \xC2\xB7 ") + task;
    if (!detail.empty() && room - nw > px(40)) {
        tx += px(8);
        tx += text_clipped(dl, s.fonts.ui, fpx(12), ImVec2(tx, cy - fpx(12) * 0.6f), g_pal->dim, detail,
                           room - nw - px(8));
    }
    if (mark_w > 0)
        status_mark(s, dl, tx + px(10), cy, att, false);
}

// Empty parts of the head move the window (after its buttons, so they keep their clicks).
void draw_head(App::State& s, ImVec2 pos, float w, float h) {
    update_target(s);
    draw_head_contents(s, pos, w, h);
    window_drag_area(s, pos, ImVec2(pos.x + w, pos.y + h));
}

// ---------------------------------------------------------------- the list of changed files
struct ListClicks {
    std::string select;
};

// Header (30): "CHANGES n" and the totals; rows of 26: letter, folder (dim) and name, counts.
void draw_change_list(App::State& s, const SidePanels& p, ImVec2 at, ImVec2 size, bool unreachable,
                      ListClicks& out) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float hh = px(30);
    const float cy = at.y + hh * 0.5f;
    std::string title = "CHANGES";
    if (!p.changes.empty())
        title += " " + std::to_string(p.changes.size()) + (p.changes_more ? "+" : "");
    // Letter-spaced 0.5 px, the small-caps look of the section titles.
    float tx = at.x + px(14);
    for (const char ch : title) {
        const char one[2] = {ch, 0};
        dl->AddText(s.fonts.ui_bold, fpx(11.5f), ImVec2(tx, cy - fpx(11.5f) * 0.6f), g_pal->dim, one);
        tx += s.fonts.ui_bold->CalcTextSizeA(fpx(11.5f), FLT_MAX, 0, one).x + px(0.5f);
    }
    if (unreachable) {
        const char* u = "Machine unreachable";
        const float uw = s.fonts.ui->CalcTextSizeA(fpx(11.5f), FLT_MAX, 0, u).x;
        dl->AddText(s.fonts.ui, fpx(11.5f), ImVec2(at.x + size.x - px(14) - uw, cy - fpx(11.5f) * 0.6f),
                    g_pal->needs_you, u);
    } else if (!p.changes.empty()) {
        int adds = 0, dels = 0;
        for (const auto& c : p.changes) {
            adds += c.add;
            dels += c.del;
        }
        float rx = at.x + size.x - px(14);
        const std::string del = "\xE2\x88\x92" + std::to_string(dels);
        const std::string add = "+" + std::to_string(adds);
        rx -= s.fonts.ui_medium->CalcTextSizeA(fpx(11.5f), FLT_MAX, 0, del.c_str()).x;
        dl->AddText(s.fonts.ui_medium, fpx(11.5f), ImVec2(rx, cy - fpx(11.5f) * 0.6f), g_pal->needs_you,
                    del.c_str());
        rx -= px(8) + s.fonts.ui_medium->CalcTextSizeA(fpx(11.5f), FLT_MAX, 0, add.c_str()).x;
        dl->AddText(s.fonts.ui_medium, fpx(11.5f), ImVec2(rx, cy - fpx(11.5f) * 0.6f), g_pal->done,
                    add.c_str());
    }
    const ImVec2 body(at.x, at.y + hh);
    const ImVec2 body_size(size.x, std::max(px(10), size.y - hh));
    if (p.changes.empty()) {
        const char* note = nullptr;
        if (!p.changes_error.empty())
            return;  // the caller shows the error card
        if (p.repo_top.empty() || p.changes_loading) {
            draw_skeleton_rows(dl, ImVec2(body.x + px(14), body.y + px(13)), body_size.x - px(28),
                               {0.70f, 0.50f, 0.60f, 0.45f}, px(26), px(8));
            return;
        }
        note = "No uncommitted changes";
        dl->AddText(s.fonts.ui, fpx(12.5f), ImVec2(body.x + px(14), body.y + px(6)), g_pal->faint, note);
        return;
    }
    ImGui::SetCursorScreenPos(body);
    ImGui::BeginChild("##rv-list", body_size, ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
    ImDrawList* d = ImGui::GetWindowDrawList();
    const float rh = px(26);
    const float rw = ImGui::GetContentRegionAvail().x;
    const bool scroll_to_open = p.open_diff != s.review.shown_diff;
    for (const auto& c : p.changes) {
        const ImVec2 a = ImGui::GetCursorScreenPos();
        ImGui::PushID(c.path.c_str());
        const bool clicked = ImGui::InvisibleButton("##row", ImVec2(rw, rh));
        const bool hov = ImGui::IsItemHovered();
        ImGui::PopID();
        const bool sel = c.path == p.open_diff;
        if (sel && scroll_to_open)
            ImGui::SetScrollHereY(0.5f);
        const ImVec2 b(a.x + rw, a.y + rh);
        if (sel) {
            d->AddRectFilled(a, b, g_pal->selected);
            d->AddRectFilled(a, ImVec2(a.x + px(2), b.y), g_pal->focus);
        } else if (hov) {
            d->AddRectFilled(a, b, g_pal->hover);
        }
        if (clicked)
            out.select = c.path;
        const float ry = a.y + rh * 0.5f;
        const char letter[2] = {c.untracked ? '?' : c.letter, 0};
        d->AddText(s.fonts.ui_bold, fpx(12), ImVec2(a.x + px(14), ry - fpx(12) * 0.6f),
                   c.untracked ? g_pal->dim : mark_color(c.letter), letter);
        // Right: counts, or "binary" / "untracked".
        float rx = a.x + rw - px(14);
        auto right_text = [&](const std::string& t, ImU32 col) {
            if (t.empty())
                return;
            rx -= s.fonts.ui->CalcTextSizeA(fpx(11.5f), FLT_MAX, 0, t.c_str()).x;
            d->AddText(s.fonts.ui, fpx(11.5f), ImVec2(rx, ry - fpx(11.5f) * 0.6f), col, t.c_str());
            rx -= px(8);
        };
        if (c.binary) {
            right_text("binary", g_pal->faint);
        } else if (c.untracked) {
            right_text("untracked", g_pal->faint);
        } else {
            right_text(c.del > 0 ? "\xE2\x88\x92" + std::to_string(c.del) : std::string(), g_pal->needs_you);
            right_text(c.add > 0 ? "+" + std::to_string(c.add) : std::string(), g_pal->done);
        }
        // Path: shortened in the middle (the root folder and the file name stay); the folder part dim.
        const float px0 = a.x + px(14) + px(18);
        const float room = std::max(0.0f, rx - px0 - px(4));
        ImFont* font = s.fonts.ui;
        const float fs = fpx(12.5f);
        const std::string shown = rv::middle_ellipsis(c.path, room, [&](std::string_view t) {
            return font->CalcTextSizeA(fs, FLT_MAX, 0, t.data(), t.data() + t.size()).x;
        });
        const auto slash = shown.find_last_of('/');
        const std::string dir = slash == std::string::npos ? std::string() : shown.substr(0, slash + 1);
        const std::string file = slash == std::string::npos ? shown : shown.substr(slash + 1);
        float x = px0;
        if (!dir.empty()) {
            d->AddText(s.fonts.ui, fs, ImVec2(x, ry - fs * 0.6f), g_pal->dim, dir.c_str());
            x += s.fonts.ui->CalcTextSizeA(fs, FLT_MAX, 0, dir.c_str()).x;
        }
        d->AddText(s.fonts.ui, fs, ImVec2(x, ry - fs * 0.6f), g_pal->text, file.c_str());
        if (hov)
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    if (p.changes_more) {
        const ImVec2 a = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(rw, rh));
        d->AddText(s.fonts.ui, fpx(12), ImVec2(a.x + px(14), a.y + rh * 0.5f - fpx(12) * 0.6f), g_pal->faint,
                   "More files not shown");
    }
    ImGui::EndChild();
}

// ---------------------------------------------------------------- the diff
// Title (30): the file and < > for the previous / next file. Rows: line number (faint), the +/-
// mark, the text in the terminal font at 12 (17 high); no wrapping, scrolls sideways.
void draw_diff_view(App::State& s, const SidePanels& p, ImVec2 at, ImVec2 size) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float hh = px(30);
    const float cy = at.y + hh * 0.5f;
    if (p.changes.empty() && !p.repo_top.empty() && !p.changes_loading && p.changes_error.empty()) {
        // Nothing changed: a faint check in the middle, no file title.
        const float mx = at.x + size.x * 0.5f;
        const float my = at.y + size.y * 0.42f;
        draw_icon(s, dl, ui::icon::kCheck, fpx(28), ImVec2(mx, my - px(14)), alpha(g_pal->done, 0x90));
        const char* t = "Nothing to review";
        const ImVec2 tz = s.fonts.ui_medium->CalcTextSizeA(fpx(13), FLT_MAX, 0, t);
        dl->AddText(s.fonts.ui_medium, fpx(13), ImVec2(mx - tz.x * 0.5f, my + px(10)), g_pal->dim, t);
        return;
    }
    dl->AddLine(ImVec2(at.x, at.y + hh), ImVec2(at.x + size.x, at.y + hh), g_pal->border);
    const float bs = px(22);
    const float bx = at.x + size.x - px(10) - bs * 2 - px(2);
    if (!p.changes.empty()) {
        if (icon_button(s, "##rv-fprev", kChevronLeft, ImVec2(bx, cy - bs * 0.5f), bs, fpx(13),
                        with_chord("Previous file", chord_in_layout(s, "review.prev_file")).c_str(),
                        g_pal->dim))
            run_command(s, "review.prev_file");
        if (icon_button(s, "##rv-fnext", ui::icon::kChevronRight, ImVec2(bx + bs + px(2), cy - bs * 0.5f), bs,
                        fpx(13), with_chord("Next file", chord_in_layout(s, "review.next_file")).c_str(),
                        g_pal->dim))
            run_command(s, "review.next_file");
    }
    if (!p.open_diff.empty())
        text_clipped(dl, s.fonts.ui_medium, fpx(12.5f), ImVec2(at.x + px(14), cy - fpx(12.5f) * 0.6f),
                     g_pal->text, p.open_diff, bx - at.x - px(24));
    const ImVec2 body(at.x, at.y + hh + 1);
    const ImVec2 body_size(size.x, std::max(px(10), size.y - hh - 1));
    // Nothing to show: say why.
    const SidePanels::Change* open = nullptr;
    for (const auto& c : p.changes)
        if (c.path == p.open_diff)
            open = &c;
    if (p.diff.empty()) {
        std::string msg = p.diff_message;
        if (open != nullptr && open->binary)
            msg = "Binary file \xE2\x80\x94 no preview";
        else if (msg.empty() && !p.changes.empty())
            msg = p.open_diff.empty() ? "Select a file to see its changes" : "";
        if (!msg.empty())
            dl->AddText(s.fonts.ui, fpx(12.5f), ImVec2(body.x + px(14), body.y + px(10)), g_pal->faint,
                        msg.c_str());
        return;
    }
    ImGui::SetCursorScreenPos(body);
    ImGui::BeginChild("##rv-diff", body_size, ImGuiChildFlags_None,
                      ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoBackground);
    ImDrawList* d = ImGui::GetWindowDrawList();
    const float fs = fpx(12);
    const float rh = px(17);
    const float cw = s.fonts.mono->CalcTextSizeA(fs, FLT_MAX, 0, "0").x;
    int max_no = 0;
    int longest = 0;
    for (const auto& r : p.diff) {
        max_no = std::max(max_no, r.number);
        longest = std::max(longest, rv::display_width(r.text));
    }
    const float num_w = std::max(px(44), static_cast<float>(std::to_string(max_no).size()) * cw + px(16));
    const float text_x = px(6) + num_w + px(6) + cw + px(6);
    const bool truncated = p.diff.size() >= 1500;
    const int rows = static_cast<int>(p.diff.size()) + (truncated ? 1 : 0);
    const float width = std::max(body_size.x, text_x + static_cast<float>(longest + 2) * cw);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGuiListClipper clip;
    clip.Begin(rows, rh);
    while (clip.Step()) {
        for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) {
            const ImVec2 a = ImGui::GetCursorScreenPos();
            ImGui::Dummy(ImVec2(width, rh));
            const ImVec2 b(a.x + width, a.y + rh);
            const float ty = a.y + (rh - fs) * 0.5f - px(1);
            if (i >= static_cast<int>(p.diff.size())) {
                d->AddText(s.fonts.ui, fpx(12), ImVec2(a.x + px(14), ty), g_pal->faint,
                           "Diff truncated at 1500 lines");
                continue;
            }
            const auto& r = p.diff[static_cast<std::size_t>(i)];
            if (r.kind == SidePanels::DiffRow::kHunk) {
                d->AddRectFilled(a, b, alpha(g_pal->info, 0x18));
                d->AddText(s.fonts.mono, fs, ImVec2(a.x + px(14), ty), g_pal->dim, r.text.c_str());
                continue;
            }
            const char* mark = nullptr;
            ImU32 mark_col = 0;
            if (r.kind == SidePanels::DiffRow::kAdd) {
                d->AddRectFilled(a, b, alpha(g_pal->done, 0x1F));
                mark = "+";
                mark_col = g_pal->done;
            } else if (r.kind == SidePanels::DiffRow::kDel) {
                d->AddRectFilled(a, b, alpha(g_pal->needs_you, 0x1F));
                mark = "\xE2\x88\x92";
                mark_col = g_pal->needs_you;
            }
            if (r.kind != SidePanels::DiffRow::kOther && r.number > 0) {
                const std::string n = std::to_string(r.number);
                const float nw = s.fonts.mono->CalcTextSizeA(fs, FLT_MAX, 0, n.c_str()).x;
                d->AddText(s.fonts.mono, fs, ImVec2(a.x + px(6) + num_w - px(6) - nw, ty), g_pal->faint,
                           n.c_str());
            }
            if (mark != nullptr)
                d->AddText(s.fonts.mono, fs, ImVec2(a.x + px(6) + num_w + px(6), ty), mark_col, mark);
            d->AddText(s.fonts.mono, fs, ImVec2(a.x + text_x, ty),
                       r.kind == SidePanels::DiffRow::kOther ? g_pal->dim : g_pal->text, r.text.c_str());
        }
    }
    ImGui::PopStyleVar();
    ImGui::EndChild();
}

// ---------------------------------------------------------------- review-changes
void draw_changes(App::State& s, ImVec2 pos, ImVec2 size) {
    update_target(s);
    auto& p = *s.panels;
    std::string machine;
    const std::string cwd = target_folder(s, &machine);
    if (cwd.empty()) {
        StateCard card;
        card.title = "Select an agent to review its changes";
        card.title_size = 14.0f;
        card.hint = "Review follows the focused agent and shows the uncommitted changes in its folder.";
        card.buttons = {{"Go to\xE2\x80\xA6", "app.picker"}};
        draw_state_card(s, pos, size, card);
        return;
    }
    p.track_changes(cwd, machine);
    const bool unreachable = !machine.empty() && !s.machine_connected(machine);
    ImGui::SetCursorScreenPos(pos);
    ImGui::BeginChild(
        "##rv-changes", size, ImGuiChildFlags_None,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground);
    ListClicks clicks;
    {
        std::unique_lock lock(p.mutex);
        if (p.not_repo) {
            lock.unlock();
            StateCard card;
            card.title = "Not a git repository";
            card.title_size = 14.0f;
            card.hint = display_path(cwd);
            draw_state_card(s, pos, size, card);
            ImGui::EndChild();
            return;
        }
        if (!p.changes_error.empty() && p.changes.empty()) {
            const std::string err = p.changes_error;
            lock.unlock();
            StateCard card;
            card.kind = CardKind::kError;
            card.title = unreachable ? std::string("Machine unreachable") : err;
            card.title_size = 13.0f;
            card.buttons = {{"Retry", "review.refresh"}};
            draw_state_card(s, pos, size, card);
            ImGui::EndChild();
            return;
        }
        const rv::Split split = rv::review_split(size.x, size.y, g_scale, s.review.list_size);
        auto at = [&](const rv::Rect& r) { return ImVec2(pos.x + r.x, pos.y + r.y); };
        draw_change_list(s, p, at(split.list), ImVec2(split.list.w, split.list.h), unreachable, clicks);
        draw_diff_view(s, p, at(split.diff), ImVec2(split.diff.w, split.diff.h));
        s.review.shown_diff = p.open_diff;
        // The splitter: a 1 px line in a 6 px handle the person can drag.
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 ha = at(split.splitter);
        const ImVec2 hb(ha.x + split.splitter.w, ha.y + split.splitter.h);
        ImGui::SetCursorScreenPos(ha);
        ImGui::InvisibleButton("##rv-split",
                               ImVec2(std::max(1.0f, split.splitter.w), std::max(1.0f, split.splitter.h)));
        const bool hot = ImGui::IsItemHovered() || ImGui::IsItemActive();
        if (hot)
            ImGui::SetMouseCursor(split.side_by_side ? ImGuiMouseCursor_ResizeEW : ImGuiMouseCursor_ResizeNS);
        if (ImGui::IsItemActive()) {
            const ImVec2 m = ImGui::GetIO().MousePos;
            s.review.list_size = split.side_by_side ? m.x - pos.x - split.splitter.w * 0.5f
                                                    : m.y - pos.y - split.splitter.h * 0.5f;
        }
        const ImU32 lc = hot ? g_pal->focus : g_pal->border;
        if (split.side_by_side) {
            const float x = ha.x + split.splitter.w * 0.5f;
            dl->AddLine(ImVec2(x, ha.y), ImVec2(x, hb.y), lc);
        } else {
            const float y = ha.y + split.splitter.h * 0.5f;
            dl->AddLine(ImVec2(ha.x, y), ImVec2(hb.x, y), lc);
        }
    }
    ImGui::EndChild();
    if (!clicks.select.empty())
        p.select_change(clicks.select);
}

// ---------------------------------------------------------------- review-commit (88 px)
// The message box and Commit (git add -A, git commit), and under them the outcome of the last
// commit or a warning while the agent is still working.
void draw_commit(App::State& s, ImVec2 pos, ImVec2 size) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddLine(pos, ImVec2(pos.x + size.x, pos.y), g_pal->border);
    auto& p = *s.panels;
    std::string machine;
    const std::string cwd = target_folder(s, &machine);
    const bool unreachable = !machine.empty() && !s.machine_connected(machine);
    const bool working = s.attention_of(s.review.target) == Attention::kWorking;
    std::unique_lock lock(p.mutex);
    const bool usable = !cwd.empty() && !p.not_repo;
    // The commit went through: say so until the folder changes or another starts.
    if (p.committing && !s.review.was_committing) {
        s.review.committing_msg = trimmed(p.commit_msg);
        s.review.committed.clear();
    }
    if (!p.committing && s.review.was_committing && p.commit_note.empty()) {
        std::string first = s.review.committing_msg.substr(0, s.review.committing_msg.find('\n'));
        s.review.committed = "Committed \xE2\x80\x9C" + first + "\xE2\x80\x9D";
    }
    s.review.was_committing = p.committing;
    const float pad = px(12);
    const float bh = px(32);
    const char* label = p.committing ? "Committing\xE2\x80\xA6" : "Commit";
    const float bw = s.fonts.ui_medium->CalcTextSizeA(fpx(13), FLT_MAX, 0, label).x + px(32);
    const ImVec2 in_a(pos.x + pad, pos.y + pad);
    const float in_w = size.x - pad * 2 - bw - px(8);
    ImGui::SetCursorScreenPos(in_a);
    ImGui::SetNextItemWidth(std::max(px(40), in_w));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(px(10), (bh - ImGui::GetFontSize()) * 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, px(6));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, v4(g_pal->card));
    ImGui::BeginDisabled(!usable || unreachable);
    bool commit = ImGui::InputTextWithHint("##rv-commitmsg", "Commit message", p.commit_msg,
                                           sizeof p.commit_msg, ImGuiInputTextFlags_EnterReturnsTrue);
    const bool active = ImGui::IsItemActive();
    ImGui::EndDisabled();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
    dl->AddRect(in_a, ImVec2(in_a.x + std::max(px(40), in_w), in_a.y + bh),
                active ? g_pal->focus : g_pal->border, px(6));
    const std::string msg = p.commit_msg;
    const bool can = usable && !unreachable && !msg.empty() &&
                     msg.find_first_not_of(" \n") != std::string::npos && !p.repo_top.empty() &&
                     !p.committing && !p.changes_loading && !p.changes.empty();
    ImGui::SetCursorScreenPos(ImVec2(in_a.x + std::max(px(40), in_w) + px(8), in_a.y));
    ImGui::BeginDisabled(!can);
    if (can) {
        primary_button_style(true);
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, v4(raised_color()));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, v4(raised_color()));
        ImGui::PushStyleColor(ImGuiCol_Text, v4(g_pal->faint));
    }
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, px(6));
    ImGui::PushFont(s.fonts.ui_medium, 13);
    commit |= ImGui::Button((std::string(label) + "##rv-commit").c_str(), ImVec2(bw, bh));
    ImGui::PopFont();
    ImGui::PopStyleVar();
    if (can)
        primary_button_style(false);
    else
        ImGui::PopStyleColor(3);
    ImGui::EndDisabled();
    // The note row, 6 px under the box.
    std::string note;
    ImU32 col = g_pal->dim;
    if (unreachable) {
        note = "Machine unreachable \xE2\x80\x94 commit is off until it answers";
        col = g_pal->needs_you;
    } else if (working && usable && !p.changes.empty()) {
        note = "\xE2\x9A\xA0 Agent is still working \xE2\x80\x94 files may change";
        col = g_pal->working;
    } else if (!p.commit_note.empty()) {
        note = p.commit_note;
        col = g_pal->needs_you;
    } else if (!s.review.committed.empty()) {
        note = s.review.committed;
        col = g_pal->done;
    }
    if (!note.empty()) {
        // One line (git's messages run to several); the whole text on hover.
        const ImVec2 na(in_a.x + px(2), in_a.y + bh + px(6));
        const auto nl = note.find_first_of("\r\n");
        const std::string line = nl == std::string::npos ? note : note.substr(0, nl) + " \xE2\x80\xA6";
        const float nw = text_clipped(dl, s.fonts.ui, fpx(12), na, col, line, size.x - pad * 2);
        if (ImGui::IsMouseHoveringRect(na, ImVec2(na.x + nw, na.y + fpx(12) * 1.3f)) && line != note)
            ImGui::SetTooltip("%s", note.c_str());
    }
    lock.unlock();
    if (commit && can)
        p.commit();
}

}  // namespace

bool draw_review_block(App::State& s, const std::string& mod, ImVec2 pos, ImVec2 size) {
    if (mod == "review-head")
        draw_head(s, pos, size.x, size.y);
    else if (mod == "review-changes")
        draw_changes(s, pos, size);
    else if (mod == "review-commit")
        draw_commit(s, pos, size);
    else
        return false;
    return true;
}

std::optional<std::string> exec_review_command(App::State& s, const std::string& name,
                                               const std::string& arg) {
    if (name == "review.toggle")
        return toggle_layout(s, "review");
    if (name == "review.next_file" || name == "review.prev_file") {
        std::string to;
        {
            std::lock_guard lock(s.panels->mutex);
            std::vector<std::string> paths;
            for (const auto& c : s.panels->changes)
                paths.push_back(c.path);
            to = rv::next_change(paths, s.panels->open_diff, name == "review.next_file" ? 1 : -1);
            if (to == s.panels->open_diff)
                to.clear();  // one file, already open: selecting it again would close it
        }
        if (!to.empty())
            s.panels->select_change(to);
        return std::string();
    }
    if (name == "review.refresh") {
        s.panels->refresh_changes();
        std::string machine;
        if (const std::string cwd = target_folder(s, &machine); !cwd.empty())
            s.git.invalidate(cwd);
        return std::string();
    }
    if (name == "panel" && arg == "changes" && review_shows(s)) {
        s.toast("Changes are always shown in Review", "", "", g_pal->info, 3);
        return std::string();
    }
    return std::nullopt;
}

}  // namespace detail
}  // namespace agentdesk::desktop
