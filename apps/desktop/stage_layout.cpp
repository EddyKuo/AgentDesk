// Stage: one agent's pane big on the stage, every other agent as a live thumbnail in a filmstrip
// along the bottom (its program, name, state and the last lines of its screen). An agent that
// needs you glows and moves to the front; a click swaps it onto the stage.
#include "app_internal.hpp"

namespace agentdesk::desktop {
namespace detail {
namespace st = ui::stage;
namespace mui = ui::mission;

namespace {
constexpr auto kTailEvery = std::chrono::milliseconds(250);  // a thumbnail's lines at most 4 times a second
constexpr float kBarH = 42.0f;

bool stage_shown(const App::State& s) {
    return !s.settings.focus_on && s.layout().style == "stage";
}

st::Size wanted_size(const App::State& s) {
    return st::parse_size(s.settings.stage_size);
}

// The size used now: the chosen one, smaller on a short window.
st::Size shown_size(const App::State& s) {
    const float h = ImGui::GetMainViewport()->WorkSize.y / std::max(0.01f, g_scale);
    return st::fit_size(wanted_size(s), h);
}

st::Filter strip_filter(const App::State& s) {
    return st::parse_filter(s.settings.stage_filter);
}

// A key or a click this frame (typing in the terminal included): the stage does not switch by
// itself for a while after one.
bool any_input() {
    if (ImGui::GetIO().InputQueueCharacters.Size > 0)
        return true;
    for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; ++k)
        if (ImGui::IsKeyPressed(static_cast<ImGuiKey>(k), false))
            return true;
    return false;
}

// ---------------------------------------------------------------- the model
// Works out this frame's agents, strip order and counts; remembers when each was first seen and
// when it started needing the person; forgets terminals that closed; notices a swap (for the
// fade and the history) and, when the person allowed it, puts a new needs-you agent on stage.
void stage_tick(App::State& s) {
    auto& g = s.stage;
    g.thumbs.clear();
    std::vector<st::StripAgent> list;
    const double now = tok::now_ms();
    if (s.host->connected()) {
        const AgentRoster r = agent_roster(s);
        auto add = [&](const std::string& id, bool shell) {
            if (g.thumbs.contains(id))
                return;
            const auto mi = s.machine_of.find(id);
            App::State::StageState::Thumb t;
            t.att = shell ? Attention::kNone : s.attention_of(id);
            t.unreachable = !s.machine_connected(mi == s.machine_of.end() ? std::string() : mi->second);
            t.shell = shell;
            if (const auto k = r.kids.find(id); k != r.kids.end())
                t.kids = static_cast<int>(k->second.size());
            if (!g.first_seen.contains(id))
                g.first_seen[id] = g.seen_next++;
            const auto ch = s.styles.changes.changed_at.find(id);
            list.push_back({id, t.att, ch == s.styles.changes.changed_at.end() ? 0LL : ch->second,
                            g.first_seen[id], t.unreachable});
            g.thumbs[id] = t;
        };
        for (const auto& id : r.ids)
            add(id, false);
        if (s.settings.stage_shells)
            for (const auto& sess : s.sessions) {
                const std::string& id = sess->id();
                const auto& v = s.views_by_id[id];
                if (v.lost || v.process_state != "Running" || s.hidden.contains(id) ||
                    s.ws.find(id).space < 0 || is_agent(v))
                    continue;
                add(id, true);
            }
    }
    // Forget what closed.
    for (auto it = g.first_seen.begin(); it != g.first_seen.end();) {
        if (s.views_by_id.contains(it->first)) {
            ++it;
            continue;
        }
        g.history.forget(it->first);
        g.last_att.erase(it->first);
        g.blocked_at.erase(it->first);
        g.slot.erase(it->first);
        s.screen_tails.erase(it->first);
        it = g.first_seen.erase(it);
    }
    // Who started needing the person since the last frame (the glow starts then).
    std::vector<std::string> newly;
    for (const auto& a : list) {
        const auto was = g.last_att.find(a.id);
        if (a.att == Attention::kBlocked && (was == g.last_att.end() || was->second != Attention::kBlocked)) {
            g.blocked_at[a.id] = now;
            if (was != g.last_att.end())  // not just seen for the first time
                newly.push_back(a.id);
        }
        g.last_att[a.id] = a.att;
    }
    const std::string on = s.ws.focused_session();
    g.ring = st::lineup(list);
    g.order = st::strip_order(list, on, strip_filter(s));
    g.blocked = g.working = 0;
    for (const auto& a : list)
        if (a.id != on) {
            g.blocked += a.att == Attention::kBlocked ? 1 : 0;
            g.working += a.att == Attention::kWorking ? 1 : 0;
        }
    if (any_input())
        g.input_at = now;
    if (!stage_shown(s))
        return;
    // Nothing on stage yet but agents to show: the first one takes it (nothing is taken away).
    if (on.empty() && !g.ring.empty()) {
        run_command(s, "stage.show:" + g.ring.front());
        return;
    }
    st::AutoSwitch as;
    as.enabled = s.settings.stage_auto_switch;
    as.stage_needs_you = g.thumbs.contains(on) && g.thumbs[on].att == Attention::kBlocked;
    as.ms_since_input = now - g.input_at;
    as.popup_open = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId) || ImGui::GetIO().WantTextInput;
    std::vector<std::string> newly_in_order;
    for (const auto& id : g.order)
        if (std::find(newly.begin(), newly.end(), id) != newly.end())
            newly_in_order.push_back(id);
    if (const std::string to = st::auto_switch_target(as, newly_in_order); !to.empty())
        run_command(s, "stage.show:" + to);
}

// The stage changed hands (by a click, a key, a toast or another layout's command): the new
// pane fades in, and the history remembers it.
void notice_swap(App::State& s) {
    auto& g = s.stage;
    const std::string on = s.ws.focused_session();
    if (on == g.shown)
        return;
    if (!g.shown.empty() && !on.empty() && !tok::reduce_motion())
        g.swap_at = tok::now_ms();
    g.shown = on;
    g.history.push(on);
}

ImU32 tail_color(const mui::TailSegment& seg) {
    if (seg.default_fg)
        return g_pal->term_fg;
    if (seg.ansi >= 0 && seg.ansi < 16 && g_pal->ansi != nullptr)
        return (*g_pal->ansi)[static_cast<std::size_t>(seg.ansi)];
    return IM_COL32((seg.rgb >> 16) & 0xFF, (seg.rgb >> 8) & 0xFF, seg.rgb & 0xFF, 255);
}

ImU32 state_color(Attention a) {
    switch (a) {
        case Attention::kBlocked:
            return g_pal->needs_you;
        case Attention::kWorking:
            return g_pal->working;
        case Attention::kDone:
            return g_pal->done;
        case Attention::kIdle:
            return g_pal->idle;
        case Attention::kNone:
            break;
    }
    return g_pal->faint;
}

// The program icon in a ring of the state's colour: full for needs you and done, a turning
// three-quarter arc while working, faint while idle, none for a plain terminal.
void icon_ring(App::State& s, ImDrawList* dl, const ui::SessionView& v, Attention a, ImVec2 c, float icon,
               ImGuiID fade_id) {
    const float r = icon * 0.5f + px(4);
    const ImU32 col = tok::cross_fade(fade_id, state_color(a));
    dl->AddCircleFilled(c, r, alpha(col, a == Attention::kNone ? 0 : 0x1C), 24);
    if (a == Attention::kWorking) {
        const float t = static_cast<float>(ImGui::GetTime());
        const float a0 = t * (2.0f * 3.14159265f / 1.4f);
        dl->PathArcTo(c, r, a0, a0 + 4.7f, 24);
        dl->PathStroke(col, 0, px(1.5f));
    } else if (a != Attention::kNone) {
        dl->AddCircle(c, r, a == Attention::kIdle ? alpha(col, 0x90) : col, 24, px(1.5f));
    }
    (void)s;
    program_icon(dl, program_of(v), ImVec2(c.x - icon * 0.5f, c.y - icon * 0.5f), icon, g_pal->text);
}

// "Permission", "Question", "Auth": the kind of a blocked report, capitalised.
std::string kind_label(const std::string& kind) {
    std::string k = kind;
    if (!k.empty())
        k[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(k[0])));
    return k;
}

// The thumbnail's body: the screen's last lines in a small monospace, cut at the right edge.
void draw_thumb_body(App::State& s, ImDrawList* dl, const std::string& id, const st::SizePreset& p, ImVec2 a,
                     ImVec2 b, bool unreachable) {
    dl->AddRectFilled(a, b, g_pal->term_bg, tok::radius_small());
    const ImVec2 in(a.x + tok::gap_s(), a.y + tok::gap_xs() + px(2));
    const ImVec2 out(b.x - tok::gap_s(), b.y - tok::gap_xs());
    const int lines = mui::tail_line_budget(out.y - in.y, px(p.line_h), p.max_lines);
    if (lines > 0) {
        const auto& c = screen_tail_of(s, id, lines);
        const auto it = s.by_id.find(id);
        if (c.tail.empty() && (it == s.by_id.end() || it->second->snapshot()->seq == 0)) {
            draw_skeleton_rows(dl, in, out.x - in.x, {0.7f, 0.45f, 0.6f}, px(p.line_h), px(p.line_h * 0.5f));
            s.stage.animating = true;
        } else {
            const float fs = fpx(p.mono_size);
            const float cw = s.fonts.mono->CalcTextSizeA(fs, FLT_MAX, 0, "M").x;
            dl->PushClipRect(in, out, true);
            // From the top, like the terminal it comes from (a full screen shows its last lines).
            float y = in.y;
            for (const auto& line : c.tail) {
                for (const auto& seg : line.segments) {
                    ImFont* f = seg.bold && s.fonts.mono_bold != nullptr ? s.fonts.mono_bold : s.fonts.mono;
                    float x = in.x + static_cast<float>(seg.col) * cw;
                    if (x > out.x)
                        break;
                    if (seg.wide)
                        x += (2.0f * cw - f->CalcTextSizeA(fs, FLT_MAX, 0, seg.text.c_str()).x) * 0.5f;
                    dl->AddText(f, fs, ImVec2(x, y + (px(p.line_h) - fs) * 0.5f), tail_color(seg),
                                seg.text.c_str());
                }
                y += px(p.line_h);
            }
            dl->PopClipRect();
        }
    }
    if (unreachable) {  // the last screen stays, dimmed, with why it does not change
        dl->AddRectFilled(a, b, alpha(g_pal->bg, 0xA8), tok::radius_small());
        const char* m = "Machine offline";
        const ImVec2 ts = tok::measure(s, ui::tokens::kCaption, m);
        tok::text(s, dl, ui::tokens::kCaption, ImVec2((a.x + b.x - ts.x) * 0.5f, (a.y + b.y - ts.y) * 0.5f),
                  g_pal->dim, m);
    }
}

// The thumbnail's right-click menu: Show on stage, Peek, Rename, Close pane.
void draw_thumb_menu(App::State& s, const std::string& id) {
    if (!begin_menu("##thumbctx"))
        return;
    menu_keys();
    const bool has_pane = s.ws.find(id).space >= 0;
    if (menu_row(s, ui::icon::kZoom, "Show on stage"))
        run_command(s, "stage.show:" + id);
    if (menu_row(s, ui::icon::kSearch, "Peek"))
        run_command(s, "agent.peek:" + id);
    if (menu_row(s, ui::icon::kEdit, "Rename", s.keymap.chord_for("pane.rename"), has_pane)) {
        s.ws.focus(id);
        run_command(s, "pane.rename");
    }
    menu_separator();
    if (menu_row(s, ui::icon::kClose, "Close pane", s.keymap.chord_for("pane.close"), has_pane)) {
        s.ws.focus(id);
        run_command(s, "pane.close");
    }
    ImGui::EndPopup();
}

// The needs-you glow: soft rings of the needs-you colour outside the card, `level` 0..1.
void draw_glow(ImDrawList* dl, ImVec2 a, ImVec2 b, float radius, float level) {
    if (level <= 0 || tok::high_contrast())
        return;
    constexpr int kRings = 5;
    for (int i = kRings; i >= 1; --i) {
        const float d = px(1.6f) * static_cast<float>(i);
        const float k = 1.0f - static_cast<float>(i - 1) / static_cast<float>(kRings);
        const int al = static_cast<int>(level * k * k * (g_pal->light ? 70.0f : 95.0f));
        dl->AddRect(ImVec2(a.x - d, a.y - d), ImVec2(b.x + d, b.y + d), alpha(g_pal->needs_you, al),
                    radius + d, 0, px(1.8f));
    }
}

// One thumbnail at [a, b]: a raised card with a header (state ring with the program icon, the
// name, how long ago its state changed or "Needs you"), the screen's last lines and, from M up, a
// footer (its message when it needs you, else where it runs, and its sub-agents).
void draw_thumb(App::State& s, const std::string& id, const App::State::StageState::Thumb& t, ImVec2 a,
                ImVec2 b, const st::SizePreset& p) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const auto& v = s.views_by_id[id];
    ImGui::SetCursorScreenPos(a);
    const bool clicked =
        ImGui::InvisibleButton("##thumb", ImVec2(b.x - a.x, b.y - a.y),
                               ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const bool hov = ImGui::IsItemHovered();
    draw_thumb_menu(s, id);  // opens on a right-click on the thumbnail
    if (hov)
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    if (clicked && ImGui::IsMouseReleased(ImGuiMouseButton_Left) && !ImGui::IsPopupOpen("##thumbctx"))
        run_command(s, "stage.show:" + id);
    const bool blocked = t.att == Attention::kBlocked && !t.unreachable;
    const float r = tok::radius_card();
    if (blocked) {
        const auto at = s.stage.blocked_at.find(id);
        const double ms = at == s.stage.blocked_at.end() ? 1e9 : tok::now_ms() - at->second;
        const bool reduced = tok::reduce_motion();
        draw_glow(dl, a, b, r, st::glow_level(ms, reduced));
        if (st::glow_moving(ms, reduced))
            tok::keep_animating();
    }
    const ImU32 fill = hov ? tok::hover(g_pal->card) : g_pal->card;
    const ImU32 edge = blocked ? g_pal->needs_you : hov ? tok::focus_ring(g_pal->card) : 0;
    tok::card(dl, a, b, r, 1, fill, edge);
    if (blocked)  // a second line inside makes the edge read as 2 px without a heavier card
        dl->AddRect(ImVec2(a.x + 1, a.y + 1), ImVec2(b.x - 1, b.y - 1), alpha(g_pal->needs_you, 0x70), r - 1);

    // Header.
    const float hh = px(p.header_h);
    const float cy = a.y + hh * 0.5f + px(1);
    const float icon = p.header_h >= 28.0f ? px(16) : px(14);
    float x = a.x + tok::gap_s() + px(2);
    icon_ring(s, dl, v, t.att, ImVec2(x + icon * 0.5f + px(4), cy), icon, ImGui::GetID("##ring"));
    x += icon + px(8) + tok::gap_s();
    const float right = b.x - tok::gap_s() - px(2);
    float rw = 0;
    if (blocked) {
        rw = status_mark(s, dl, 0, 0, t.att, p.header_h < 28.0f, true);
        status_mark(s, dl, right - rw, cy, t.att, p.header_h < 28.0f);
    } else {
        std::string ago;
        if (t.unreachable)
            ago = "offline";
        else if (const auto ch = s.changed.find(id); ch != s.changed.end() && ch->second.at > 0)
            ago = ui::focus::short_ago(now_ms() - ch->second.at);
        if (!ago.empty()) {
            const ImVec2 as = tok::measure(s, ui::tokens::kCaption, ago.c_str());
            rw = as.x;
            tok::text(s, dl, ui::tokens::kCaption, ImVec2(right - as.x, cy - as.y * 0.5f), g_pal->faint,
                      ago.c_str());
        }
    }
    const ui::tokens::TypeLevel& name_level = p.header_h >= 28.0f ? ui::tokens::kBody : ui::tokens::kCaption;
    const float name_fs = tok::size(name_level);
    text_clipped(dl, s.fonts.ui_bold, name_fs, ImVec2(x, cy - name_fs * 0.62f), g_pal->text, s.title_of(v),
                 std::max(0.0f, right - rw - tok::gap_s() - x));

    // Body and footer.
    const float fh = px(p.footer_h);
    const ImVec2 ba(a.x + tok::gap_xs() + px(2), a.y + hh);
    const ImVec2 bb(b.x - tok::gap_xs() - px(2), b.y - (fh > 0 ? fh : tok::gap_xs() + px(2)));
    if (bb.y - ba.y > px(16))
        draw_thumb_body(s, dl, id, p, ba, bb, t.unreachable);
    if (fh <= 0)
        return;
    const float fy = b.y - fh * 0.5f;
    float fr = right;
    if (t.kids > 0) {
        const std::string k = "+" + std::to_string(t.kids);
        const ImVec2 ks = tok::measure(s, ui::tokens::kCaption, k.c_str());
        fr -= ks.x;
        tok::text(s, dl, ui::tokens::kCaption, ImVec2(fr, fy - ks.y * 0.5f), g_pal->dim, k.c_str());
        fr -= tok::gap_s();
    }
    std::string foot = t.shell ? std::string() : mui::clean_message(v.agent_message);
    if (foot.empty() && blocked)
        foot = kind_label(v.agent_kind);
    ImU32 foot_col = blocked ? g_pal->needs_you : g_pal->dim;
    if (foot.empty()) {
        if (const auto loc = s.ws.find(id); loc.space >= 0)
            foot = s.ws.space_title(loc.space, s.session_cwds());
        if (const std::string d = agent_detail(s, id, v); !d.empty() && d != s.title_of(v))
            foot += (foot.empty() ? "" : " \xC2\xB7 ") + d;
        foot_col = g_pal->faint;
    }
    const float cs = tok::size(ui::tokens::kCaption);
    text_clipped(dl, tok::font(s, ui::tokens::kCaption), cs,
                 ImVec2(a.x + tok::gap_s() + px(2), fy - cs * 0.62f), foot_col, foot,
                 std::max(0.0f, fr - a.x - tok::gap_m()));
}

// ---------------------------------------------------------------- the strip's header
// A row of segments ("All 5", "Needs you 2", ...) ending at `right`, 24 px high, on a raised
// group; the chosen one on the selected wash. Returns the clicked index (-1) and sets *left.
int segments(App::State& s, ImDrawList* dl, const char* id, float right, float cy,
             const std::vector<std::string>& labels, const std::vector<ImU32>& colors, int chosen,
             float* left) {
    const float h = px(24);
    const float pad = tok::gap_s() + px(2);
    std::vector<float> widths;
    float total = px(4);
    for (const auto& l : labels) {
        widths.push_back(tok::measure(s, ui::tokens::kCaption, l.c_str()).x + pad * 2);
        total += widths.back();
    }
    const ImU32 group_bg = mix(g_pal->sidebar, g_pal->card, 0.6f);
    const ImVec2 ga(right - total, cy - h * 0.5f - px(2));
    dl->AddRectFilled(ga, ImVec2(right, cy + h * 0.5f + px(2)), group_bg, tok::radius_small() + px(2));
    int hit = -1;
    float x = ga.x + px(2);
    ImGui::PushID(id);
    for (std::size_t i = 0; i < labels.size(); ++i) {
        const ImVec2 p(x, cy - h * 0.5f);
        ImGui::SetCursorScreenPos(p);
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::InvisibleButton("##seg", ImVec2(widths[i], h)))
            hit = static_cast<int>(i);
        const bool hov = ImGui::IsItemHovered();
        ImGui::PopID();
        const bool on = static_cast<int>(i) == chosen;
        const float k = tok::fade(ImGui::GetID((std::string(id) + std::to_string(i)).c_str()), on);
        if (k > 0.01f)
            dl->AddRectFilled(p, ImVec2(p.x + widths[i], p.y + h),
                              alpha(tok::selected(group_bg), static_cast<int>(255 * k)), tok::radius_small());
        else if (hov)
            dl->AddRectFilled(p, ImVec2(p.x + widths[i], p.y + h), tok::hover(group_bg), tok::radius_small());
        if (hov)
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        const ImU32 col = colors[i] != 0 ? colors[i] : on ? g_pal->text : g_pal->dim;
        const ImVec2 ts = tok::measure(s, ui::tokens::kCaption, labels[i].c_str());
        tok::text(s, dl, ui::tokens::kCaption, ImVec2(p.x + (widths[i] - ts.x) * 0.5f, cy - ts.y * 0.5f), col,
                  labels[i].c_str());
        x += widths[i];
    }
    ImGui::PopID();
    *left = ga.x;
    return hit;
}

void draw_strip_header(App::State& s, ImDrawList* dl, ImVec2 pos, float w) {
    const auto& g = s.stage;
    const float cy = pos.y + px(st::kStripHeaderH) * 0.5f + px(2);
    float x = pos.x + tok::gap_l();
    x += tok::text(s, dl, ui::tokens::kCaption, ImVec2(x, cy - tok::size(ui::tokens::kCaption) * 0.65f),
                   g_pal->dim, "Agents");
    // Every thumbnail the strip can show, the stage's own agent left out.
    int others = 0;
    for (const auto& [id, t] : g.thumbs)
        others += id != s.ws.focused_session() ? 1 : 0;
    if (g.thumbs.empty())
        return;  // nothing to filter or size yet: the empty row says what to do
    const std::string n = std::to_string(others);
    tok::text(s, dl, ui::tokens::kCaption,
              ImVec2(x + tok::gap_xs() + px(2), cy - tok::size(ui::tokens::kCaption) * 0.65f), g_pal->faint,
              n.c_str());

    float right = pos.x + w - tok::gap_l();
    // Size: S M L.
    // The size in use is the one marked (a short window may use a smaller one than chosen).
    const st::Size size = shown_size(s);
    float left = right;
    const int sz =
        segments(s, dl, "##stage-size", right, cy, {"S", "M", "L"}, {0, 0, 0}, static_cast<int>(size), &left);
    if (size != wanted_size(s) &&
        ImGui::IsMouseHoveringRect(ImVec2(left, cy - px(14)), ImVec2(right, cy + px(14))))
        ImGui::SetTooltip("The window is too short for %s thumbnails",
                          wanted_size(s) == st::Size::kLarge ? "large" : "medium");
    if (sz >= 0)
        run_command(s, std::string("stage.size:") + std::string(st::size_name(static_cast<st::Size>(sz))));
    right = left - tok::gap_m();
    // Filters.
    const st::Filter f = strip_filter(s);
    const std::string blocked =
        g.blocked > 0 ? "Needs you " + std::to_string(g.blocked) : std::string("Needs you");
    const std::string working =
        g.working > 0 ? "Working " + std::to_string(g.working) : std::string("Working");
    const int fi = segments(s, dl, "##stage-filter", right, cy, {"All", blocked, working},
                            {0, g.blocked > 0 ? g_pal->needs_you : 0, 0}, static_cast<int>(f), &left);
    if (fi >= 0) {
        const st::Filter to = static_cast<st::Filter>(fi);
        run_command(
            s, std::string("stage.filter:") + std::string(st::filter_name(to == f ? st::Filter::kAll : to)));
    }
}

// A round button with a chevron at the strip's edge: scrolls a page that way.
bool chevron_button(App::State& s, const char* id, ImVec2 c, bool left) {
    const float r = px(13);
    ImGui::SetCursorScreenPos(ImVec2(c.x - r, c.y - r));
    const bool clicked = ImGui::InvisibleButton(id, ImVec2(r * 2, r * 2));
    const bool hov = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    tok::shadow(dl, ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), r, 1);
    dl->AddCircleFilled(c, r, hov ? tok::hover(g_pal->card) : g_pal->card, 24);
    dl->AddCircle(c, r, g_pal->border, 24, 1.0f);
    const float d = px(3.5f) * (left ? 1.0f : -1.0f);
    dl->AddPolyline(std::array<ImVec2, 3>{ImVec2(c.x + d * 0.5f, c.y - px(5)), ImVec2(c.x - d * 0.5f, c.y),
                                          ImVec2(c.x + d * 0.5f, c.y + px(5))}
                        .data(),
                    3, hov ? g_pal->text : g_pal->dim, 0, px(1.6f));
    if (hov)
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    (void)s;
    return clicked;
}

void draw_stage_strip(App::State& s, ImVec2 pos, ImVec2 size) {
    auto& g = s.stage;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 end(pos.x + size.x, pos.y + size.y);
    dl->AddRectFilled(pos, end, g_pal->sidebar);
    tok::hline(dl, pos.x, end.x, pos.y, tok::divider(g_pal->sidebar));
    draw_strip_header(s, dl, pos, size.x);

    const st::SizePreset& p = st::preset(shown_size(s));
    const float top = pos.y + px(st::kStripHeaderH) + px(2);
    const ImVec2 row_a(pos.x + tok::gap_l(), top);
    const ImVec2 row_b(end.x - tok::gap_l(),
                       std::min(end.y - px(st::kStripBottomPad) + px(4), top + px(p.thumb_h)));
    const float view_w = row_b.x - row_a.x;
    if (g.order.empty()) {
        EmptyRow e;
        const int all =
            static_cast<int>(g.thumbs.size()) - (g.thumbs.contains(s.ws.focused_session()) ? 1 : 0);
        const st::Filter f = strip_filter(s);
        if (g.thumbs.empty()) {
            e.art = tok::Art::kAgents;
            e.title = "No agents yet";
            e.hint = "Start Claude Code or Codex and each one gets a live thumbnail here.";
            e.button = with_chord(s, "Start a program\xE2\x80\xA6", "agent.new");
            e.command = "agent.new";
        } else if (all <= 0) {
            e.art = tok::Art::kAgents;
            e.title = "Everyone is on stage";
            e.hint = "Start another agent and it lines up here.";
            e.button = with_chord(s, "Start a program\xE2\x80\xA6", "agent.new");
            e.command = "agent.new";
        } else {
            e.art = f == st::Filter::kNeedsYou ? tok::Art::kAllDone : tok::Art::kAgents;
            e.title = f == st::Filter::kNeedsYou ? "Nothing needs you" : "Nothing is working";
            e.hint =
                std::to_string(all) + (all == 1 ? " agent is" : " agents are") + " hidden by the filter.";
            e.link = "Show all";
            e.on_link = [&s] { run_command(s, "stage.filter:all"); };
        }
        draw_empty_row(s, row_a, ImVec2(view_w, row_b.y - row_a.y), e);
        return;
    }
    const float gap = px(st::kThumbGap);
    const float tw = px(p.thumb_w);
    const float content = st::content_width(static_cast<int>(g.order.size()), tw, gap);
    const bool overflow = content > view_w + 0.5f;
    // The wheel (either direction) scrolls the strip while the pointer is over it.
    const double now = tok::now_ms();
    if (overflow && ImGui::IsMouseHoveringRect(pos, end) &&
        ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)) {
        const ImGuiIO& io = ImGui::GetIO();
        const float wheel = io.MouseWheel + io.MouseWheelH;
        if (wheel != 0.0f)
            g.scroll -= wheel * (tw + gap) * 0.75f;
    }
    g.scroll = st::clamp_scroll(g.scroll, content, view_w);
    const float shown_scroll =
        g.scroll_anim.update(g.scroll, now, ui::tokens::kNormalMs, tok::reduce_motion());
    if (g.scroll_anim.moving(now))
        tok::keep_animating();

    ImGui::PushClipRect(ImVec2(row_a.x - px(10), row_a.y - px(10)), ImVec2(row_b.x + px(10), end.y), true);
    for (std::size_t i = 0; i < g.order.size(); ++i) {
        const std::string& id = g.order[i];
        const float target = static_cast<float>(i) * (tw + gap);
        auto& slot = g.slot[id];
        const float at = slot.update(target, now, ui::tokens::kSlowMs, tok::reduce_motion());
        if (slot.moving(now))
            tok::keep_animating();
        const ImVec2 a(std::round(row_a.x + at - shown_scroll), row_a.y);
        const ImVec2 b(a.x + tw, a.y + px(p.thumb_h));
        if (b.x < row_a.x - px(12) || a.x > row_b.x + px(12))
            continue;  // scrolled out of view: not drawn
        ImGui::PushID(id.c_str());
        const float k = tok::appear(ImGui::GetID("##in"), ui::tokens::kNormalMs);
        const auto& t = g.thumbs[id];
        if (k < 1.0f)
            with_alpha(dl, k, [&] { draw_thumb(s, id, t, a, b, p); });
        else
            draw_thumb(s, id, t, a, b, p);
        ImGui::PopID();
    }
    ImGui::PopClipRect();
    if (!overflow)
        return;
    // Soft edges where thumbnails continue out of view, and a button to page that way.
    const float fade_w = px(28);
    const ImU32 solid = g_pal->sidebar;
    const ImU32 clear = alpha(g_pal->sidebar, 0);
    const float cy = (row_a.y + row_b.y) * 0.5f;
    if (shown_scroll > 1.0f) {
        dl->AddRectFilledMultiColor(ImVec2(pos.x, row_a.y - px(8)), ImVec2(row_a.x + fade_w, end.y), solid,
                                    clear, clear, solid);
        if (chevron_button(s, "##strip-left", ImVec2(row_a.x + px(6), cy), true))
            g.scroll -= view_w * 0.8f;
    }
    if (shown_scroll < content - view_w - 1.0f) {
        dl->AddRectFilledMultiColor(ImVec2(row_b.x - fade_w, row_a.y - px(8)), ImVec2(end.x, end.y), clear,
                                    solid, solid, clear);
        if (chevron_button(s, "##strip-right", ImVec2(row_b.x - px(6), cy), false))
            g.scroll += view_w * 0.8f;
    }
}

// ---------------------------------------------------------------- the bar
// "+" (the new menu), Settings and back to the layout before; returns their left edge.
float bar_buttons(App::State& s, float right, float cy) {
    const float b = px(28);
    auto tip = [&](const char* text, const char* cmd) {
        const std::string chord = s.keymap.chord_for(cmd);
        return chord.empty() ? std::string(text) : std::string(text) + "  " + chord;
    };
    right -= b;
    const std::string back = tip("Back to the layout before", "stage.toggle");
    if (icon_button(s, "##stage-back", ui::icon::kLayers, ImVec2(right, cy - b * 0.5f), b, fpx(15),
                    back.c_str(), g_pal->dim))
        run_command(s, "stage.toggle");
    right -= b + px(2);
    const std::string settings = tip("Settings", "app.settings");
    if (icon_button(s, "##stage-settings", ui::icon::kSettings, ImVec2(right, cy - b * 0.5f), b, fpx(15),
                    settings.c_str(), g_pal->dim))
        run_command(s, "app.settings");
    right -= b + px(2);
    if (icon_button(s, "##stage-new", ui::icon::kPlus, ImVec2(right, cy - b * 0.5f), b, fpx(15), "New",
                    g_pal->dim))
        ImGui::OpenPopup("##newmenu");
    draw_new_menu(s);
    return right;
}

// "← <name>": swaps the agent that was on stage before back in (stage.back). Returns its left edge.
float back_pill(App::State& s, ImDrawList* dl, float right, float cy) {
    const std::string prev = s.stage.history.previous();
    const auto it = s.views_by_id.find(prev);
    if (prev.empty() || it == s.views_by_id.end())
        return right;
    const std::string name = s.title_of(it->second);
    const float max_name = px(140);
    const float nw = std::min(max_name, tok::measure(s, ui::tokens::kCaption, name.c_str()).x);
    const float h = px(26);
    const float w = px(14) + tok::gap_xs() + px(2) + nw + tok::gap_m() * 2;
    const ImVec2 a(right - w, cy - h * 0.5f);
    ImGui::SetCursorScreenPos(a);
    const bool clicked = ImGui::InvisibleButton("##stage-prev", ImVec2(w, h));
    const bool hov = ImGui::IsItemHovered();
    if (hov) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        const std::string chord = s.keymap.chord_for("stage.back");
        ImGui::SetTooltip("%s", chord.empty() ? "Swap back" : ("Swap back  " + chord).c_str());
    }
    dl->AddRectFilled(a, ImVec2(a.x + w, a.y + h), hov ? tok::hover(g_pal->sidebar) : 0, tok::radius_small());
    dl->AddRect(a, ImVec2(a.x + w, a.y + h), tok::divider(g_pal->sidebar), tok::radius_small());
    const ImU32 col = hov ? g_pal->text : g_pal->dim;
    draw_icon(s, dl, ui::icon::kArrowLeft, fpx(13), ImVec2(a.x + tok::gap_m() + px(7), cy), col);
    const float cs = tok::size(ui::tokens::kCaption);
    text_clipped(dl, tok::font(s, ui::tokens::kCaption), cs,
                 ImVec2(a.x + tok::gap_m() + px(14) + tok::gap_xs() + px(2), cy - cs * 0.62f), col, name,
                 max_name);
    if (clicked)
        run_command(s, "stage.back");
    return a.x;
}

void draw_stage_bar(App::State& s, ImVec2 pos, float w, float h) {
    stage_tick(s);
    notice_swap(s);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 end(pos.x + w, pos.y + h);
    dl->AddRectFilled(pos, end, g_pal->sidebar);
    tok::hline(dl, pos.x, end.x, end.y - 1.0f, tok::divider(g_pal->sidebar));
    const float cy = pos.y + h * 0.5f;
    float x = clear_of_traffic_lights(s, pos.x + tok::gap_l(), pos.y, end.y, tok::gap_m());
    float right = end.x - px(10);
    if (!mac_window_buttons(s)) {
        draw_window_buttons(s, right, pos.y, h);
        right -= px(26) * 3 + px(4) + px(8);
    }
    right = bar_buttons(s, right, cy) - tok::gap_s();
    right = back_pill(s, dl, right, cy) - tok::gap_m();

    // "Stage", then who is on it: the ring and icon, the name, where it runs, its state and, when
    // it needs you, what it asks.
    const float ts = tok::size(ui::tokens::kTitle);
    x += tok::text(s, dl, ui::tokens::kTitle, ImVec2(x, cy - ts * 0.62f), g_pal->text, "Stage");
    x += tok::gap_m();
    tok::vline(dl, x, cy - px(9), cy + px(9), tok::divider(g_pal->sidebar));
    x += tok::gap_m();
    const std::string id = s.ws.focused_session();
    const auto it = s.views_by_id.find(id);
    if (id.empty() || it == s.views_by_id.end()) {
        tok::text(s, dl, ui::tokens::kBody, ImVec2(x, cy - tok::size(ui::tokens::kBody) * 0.62f), g_pal->dim,
                  "Nothing on stage");
        window_drag_area(s, pos, end);
        return;
    }
    const auto& v = it->second;
    const auto th = s.stage.thumbs.find(id);
    const Attention att = th != s.stage.thumbs.end() ? th->second.att : s.attention_of(id);
    icon_ring(s, dl, v, att, ImVec2(x + px(8) + px(4), cy), px(16), ImGui::GetID("##bar-ring"));
    x += px(16) + px(8) + tok::gap_s() + px(2);
    const float bs = tok::size(ui::tokens::kBody);
    const float room = std::max(0.0f, right - x);
    x += text_clipped(dl, s.fonts.ui_bold, bs, ImVec2(x, cy - bs * 0.62f), g_pal->text, s.title_of(v),
                      room * 0.4f);
    std::string where;
    if (const auto loc = s.ws.find(id); loc.space >= 0)
        where = s.ws.space_title(loc.space, s.session_cwds());
    if (const std::string d = agent_detail(s, id, v); !d.empty() && d != s.title_of(v))
        where += (where.empty() ? "" : " \xC2\xB7 ") + d;
    if (!where.empty())
        x += text_clipped(dl, s.fonts.ui, bs, ImVec2(x, cy - bs * 0.62f), g_pal->dim, "  \xC2\xB7  " + where,
                          std::max(0.0f, (right - x) * 0.5f));
    const float mw = status_mark(s, dl, 0, 0, att, false, true);
    if (mw > 0 && x + tok::gap_m() + mw < right) {
        status_mark(s, dl, x + tok::gap_m(), cy, att, false);
        x += tok::gap_m() + mw;
    }
    if (att == Attention::kBlocked) {
        std::string msg = mui::clean_message(v.agent_message);
        if (msg.empty())
            msg = kind_label(v.agent_kind);
        if (!msg.empty() && right - x > px(60))
            text_clipped(dl, s.fonts.ui, bs, ImVec2(x + tok::gap_s(), cy - bs * 0.62f), g_pal->needs_you, msg,
                         right - x - tok::gap_s());
    }
    window_drag_area(s, pos, end);
}

// ---------------------------------------------------------------- the empty stage
void draw_stage_empty(App::State& s, ImVec2 pos, ImVec2 size) {
    if (!s.host->connected()) {
        StateCard card;
        if (s.machine_loading("")) {
            card.kind = CardKind::kLoading;
            card.title = "Connecting to agent-host\xE2\x80\xA6";
            s.stage.animating = true;
        } else {
            card.kind = CardKind::kError;
            card.title = "agent-host is not running";
            card.hint = "Stage shows the agents agent-host runs.";
            card.detail = s.host->last_error();
            card.detail_color = g_pal->needs_you;
            card.buttons = {{"Retry", "", [&s] { s.host->request_start_host(); }}};
        }
        card.title_size = ui::tokens::kTitle.size;
        draw_state_card(s, pos, size, card);
        return;
    }
    draw_empty_state(
        s, pos, size, tok::Art::kAgents, "Nothing on stage yet",
        "Start Claude Code or Codex and it takes the stage; every other agent lines up below as a "
        "live thumbnail.",
        with_chord(s, "Start a program\xE2\x80\xA6", "agent.new"), "agent.new");
}
}  // namespace

const App::State::ScreenTail& screen_tail_of(App::State& s, const std::string& id, int lines) {
    auto& c = s.screen_tails[id];
    const auto it = s.by_id.find(id);
    if (it == s.by_id.end())
        return c;
    const auto snap = it->second->snapshot();
    const auto now = Clock::now();
    const bool changed = snap->seq != c.seq || snap->generation != c.generation;
    if (!c.filled || c.lines != lines || (changed && now - c.at >= kTailEvery)) {
        c.tail = mui::screen_tail(*snap, lines);
        c.seq = snap->seq;
        c.generation = snap->generation;
        c.lines = lines;
        c.at = now;
        c.filled = true;
    } else if (changed) {
        s.tails_pending = true;  // come back for it
    }
    return c;
}

bool draw_stage_block(App::State& s, const std::string& mod, ImVec2 pos, ImVec2 size) {
    if (mod == "stage-bar") {
        s.stage.animating = false;
        draw_stage_bar(s, pos, size.x, size.y);
    } else if (mod == "stage-strip") {
        draw_stage_strip(s, pos, size);
    } else if (mod == "stage-empty") {
        draw_stage_empty(s, pos, size);
    } else {
        return false;
    }
    return true;
}

std::optional<ui::layout::Node> stage_layout_tree(const App::State& s, const ui::layout::Node& tree) {
    ui::layout::Node t = tree;
    if (ui::layout::Node* strip = t.find("strip"))
        strip->size = ui::layout::Span::pixels(st::strip_height(shown_size(s)));
    // Nothing to put on stage (no agent, no terminal) or no agent-host: the empty stage, no strip.
    const bool empty = !s.host->connected() || (s.stage.ring.empty() && s.ws.focused_session().empty());
    if (empty) {
        t.select("stage-body", "empty");
        if (ui::layout::Node* strip = t.find("strip"))
            strip->hidden = true;
    }
    return t;
}

void draw_stage_overlay(App::State& s) {
    auto& g = s.stage;
    if (g.swap_at < 0 || !stage_shown(s))
        return;
    const double t = (tok::now_ms() - g.swap_at) / ui::tokens::kSlowMs;
    const lay::Placed* p = s.placed.at("stage");
    if (t >= 1.0 || tok::reduce_motion() || p == nullptr) {
        g.swap_at = -1.0;
        return;
    }
    // A layer above the pane (its terminal is a child window) that lets the pointer through: the
    // canvas colour clears from the stage as the new agent comes in.
    const float k = 1.0f - ui::tokens::ease_out(static_cast<float>(t));
    const ImVec2 a(p->inner.x, p->inner.y);
    const ImVec2 size(p->inner.w, p->inner.h);
    ImGui::SetCursorScreenPos(a);
    ImGui::BeginChild("##stage-swap", size, ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoBackground |
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings);
    ImGui::GetWindowDrawList()->AddRectFilled(a, ImVec2(a.x + size.x, a.y + size.y),
                                              alpha(g_pal->bg, static_cast<int>(k * 235.0f)), px(10));
    ImGui::EndChild();
    tok::keep_animating();
}

std::optional<std::string> stage_command(App::State& s, const std::string& name, const std::string& arg) {
    if (name == "stage.toggle" || name == "mission.toggle")
        return toggle_layout(s, "stage");
    if (name == "stage.show" || name == "mission.expand") {
        const auto it = s.views_by_id.find(arg);
        if (it == s.views_by_id.end())
            return "no terminal \"" + arg + "\"";
        if (s.ws.find(arg).space < 0 && !it->second.parent.empty()) {
            // A sub-agent without a pane of its own: a look in the peek window, the stage stays.
            s.peek = arg;
            s.views[arg].focus_next = true;
            return std::string();
        }
        if (s.ws.unsettle_session(arg))
            s.doc_changed();
        s.focus(arg);
        return std::string();
    }
    if (name == "stage.next" || name == "stage.prev") {
        const std::string to = st::step(s.stage.ring, s.ws.focused_session(), name == "stage.next" ? 1 : -1);
        if (to.empty())
            return std::string("no agents");
        if (to == s.ws.focused_session())
            return std::string();
        return stage_command(s, "stage.show", to);
    }
    if (name == "stage.back") {
        const std::string to = s.stage.history.previous();
        if (to.empty())
            return std::string("nothing to swap back to");
        return stage_command(s, "stage.show", to);
    }
    if (name == "stage.filter" || name == "mission.filter") {
        s.settings.stage_filter = std::string(st::filter_name(st::parse_filter(arg)));
        save_settings(s.settings);
        s.stage.scroll = 0;
        return std::string();
    }
    if (name == "stage.size") {
        const st::Size size = arg.empty() ? st::next_size(wanted_size(s)) : st::parse_size(arg);
        s.settings.stage_size = std::string(st::size_name(size));
        save_settings(s.settings);
        return std::string();
    }
    if (name == "stage.auto_switch") {
        s.settings.stage_auto_switch = !s.settings.stage_auto_switch;
        save_settings(s.settings);
        s.toast(s.settings.stage_auto_switch ? "Stage: an agent that needs you takes the stage"
                                             : "Stage: the stage changes only when you change it",
                "", "", g_pal->info, 3);
        return std::string();
    }
    if (name == "stage.shells") {
        s.settings.stage_shells = !s.settings.stage_shells;
        save_settings(s.settings);
        return std::string();
    }
    return std::nullopt;
}

bool look_stage_options(App::State& s) {
    bool changed = false;
    settings_row(s, "Thumbnail size",
                 "How big the live thumbnails in the strip are. A short window uses a smaller "
                 "size so the stage stays big.",
                 px(78) * 3 + px(4), px(32));
    if (segmented(s, "##stage-size-set", s.settings.stage_size,
                  {{"s", "Small"}, {"m", "Medium"}, {"l", "Large"}}))
        changed = true;
    settings_row_done(s);
    settings_row(s, "Plain terminals", "Thumbnails for terminals that run no agent, too.", px(36), px(20));
    if (settings_toggle("##stage-shells", s.settings.stage_shells)) {
        s.settings.stage_shells = !s.settings.stage_shells;
        changed = true;
    }
    settings_row_done(s);
    settings_row(
        s, "Switch to an agent that needs you",
        "Puts it on the stage by itself, but never while you are typing or when the agent on stage needs "
        "you too.",
        px(36), px(20));
    if (settings_toggle("##stage-auto", s.settings.stage_auto_switch)) {
        s.settings.stage_auto_switch = !s.settings.stage_auto_switch;
        changed = true;
    }
    settings_row_done(s);
    return changed;
}

}  // namespace detail
}  // namespace agentdesk::desktop
