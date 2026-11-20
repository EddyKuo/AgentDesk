// Mission Control: a layout whose wall shows every agent as a tile (program, name, state, the
// last lines of its screen, its message), the ones that need you first. A click opens that agent
// as the usual pane in the same layout; Ctrl+Shift+M goes back to the wall.
#include "app_internal.hpp"

namespace agentdesk::desktop {
namespace detail {
namespace mui = ui::mission;

namespace {
// Design sizes (px() scales them).
constexpr float kMinTileW = 240.0f;
constexpr float kMinTileH = 150.0f;
constexpr float kGap = 12.0f;
constexpr float kHeaderH = 40.0f;
constexpr float kFooterH = 26.0f;
constexpr float kMonoSize = 11.0f;
constexpr float kLineH = 15.0f;
constexpr auto kTailEvery = std::chrono::milliseconds(250);  // a tile's lines at most 4 times a second

const lay::Node* wall_child(const App::State& s) {
    const lay::Node* st = s.layout().tree.find("wall-body");
    return st != nullptr ? st->active_child() : nullptr;
}

bool expanded(const App::State& s) {
    const lay::Node* c = wall_child(s);
    return c != nullptr && c->id == "main";
}

// Shows the stack's `child` without saving it: Mission Control always opens on the wall.
void show_child(App::State& s, const char* child) {
    LayoutSpec& l = s.layout();
    if (l.tree.select("wall-body", child))
        derive_layout(l);
}

struct Tile {
    std::string id;
    Attention att = Attention::kNone;
    bool sub = false;          // a sub-agent with a tile of its own
    bool unreachable = false;  // its machine does not answer
    std::vector<std::string> kids;
};

struct Wall {
    std::vector<Tile> tiles;  // in order, not filtered
    int blocked = 0, working = 0, done = 0;
    int shells = 0;  // terminals in panes without an agent status
};

// The agents with a tile: the sidebar's agents, their sub-agents when mission_sub_agents is on,
// terminals without a status when mission_shells is on (or "show them" was clicked).
Wall collect_wall(App::State& s) {
    Wall w;
    const AgentRoster r = agent_roster(s);
    const bool subs = s.settings.mission_sub_agents;
    const bool shells = s.settings.mission_shells || s.mission.shells_shown;
    std::vector<mui::WallAgent> list;
    std::set<std::string> in;
    long long seq = 0;
    auto add = [&](const std::string& id, int rank) {
        const auto& v = s.views_by_id[id];
        const auto mi = s.machine_of.find(id);
        const auto ch = s.changed.find(id);
        list.push_back({id, v.parent, rank, ch == s.changed.end() ? 0LL : ch->second.at, seq++,
                        !s.machine_connected(mi == s.machine_of.end() ? std::string() : mi->second)});
        in.insert(id);
    };
    for (const auto& id : r.ids)
        add(id, r.urgency.count(id) ? r.urgency.at(id) : ui::attention_rank(s.attention_of(id)));
    if (subs)
        for (const auto& [parent, kids] : r.kids)
            for (const auto& k : kids)
                if (!in.contains(k) && s.views_by_id.count(k))
                    add(k, ui::attention_rank(ui::kid_status(s.attention_of(k))));
    for (const auto& sess : s.sessions) {
        const std::string& id = sess->id();
        const auto& v = s.views_by_id[id];
        if (in.contains(id) || v.lost || v.process_state != "Running" || s.hidden.contains(id) ||
            s.ws.find(id).space < 0 || is_agent(v))
            continue;
        ++w.shells;
        if (shells)
            add(id, 0);
    }
    for (const auto& id : mui::wall_order(list, subs)) {
        Tile t;
        t.id = id;
        const auto& v = s.views_by_id[id];
        t.sub = !v.parent.empty() && s.views_by_id.count(v.parent) != 0 && in.contains(v.parent);
        t.att = t.sub ? ui::kid_status(s.attention_of(id)) : s.attention_of(id);
        for (const auto& wa : list)
            if (wa.id == id)
                t.unreachable = wa.unreachable;
        if (!subs)
            for (const auto& row : ui::agent_tree(r.kids, id))
                t.kids.push_back(row.id);
        w.blocked += t.att == Attention::kBlocked ? 1 : 0;
        w.working += t.att == Attention::kWorking ? 1 : 0;
        w.done += t.att == Attention::kDone ? 1 : 0;
        w.tiles.push_back(std::move(t));
    }
    return w;
}

// The tile's last lines, read again when the screen changed (at most every 250 ms) or the tile
// can hold a different number of lines.
const App::State::MissionState::Tile& tail_of(App::State& s, const std::string& id, int lines) {
    auto& c = s.mission.tiles[id];
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
        s.mission.animating = true;  // come back for it
    }
    return c;
}

ImU32 tail_color(const mui::TailSegment& seg) {
    if (seg.default_fg)
        return g_pal->term_fg;
    if (seg.ansi >= 0 && seg.ansi < 16 && g_pal->ansi != nullptr)
        return (*g_pal->ansi)[static_cast<std::size_t>(seg.ansi)];
    return IM_COL32((seg.rgb >> 16) & 0xFF, (seg.rgb >> 8) & 0xFF, seg.rgb & 0xFF, 255);
}

// The body: the screen's last lines in JetBrains Mono 11 on 15 px lines, cut at the right edge.
void draw_tile_body(App::State& s, ImDrawList* dl, const Tile& t, ImVec2 a, ImVec2 b) {
    dl->AddRectFilled(a, b, g_pal->term_bg, px(6));
    const ImVec2 in(a.x + px(8), a.y + px(6));
    const ImVec2 out(b.x - px(8), b.y - px(6));
    const int lines = mui::tail_line_budget(out.y - in.y, px(kLineH));
    if (lines <= 0)
        return;
    const auto& c = tail_of(s, t.id, lines);
    const auto it = s.by_id.find(t.id);
    const bool loading = c.tail.empty() && (it == s.by_id.end() || it->second->snapshot()->seq == 0);
    if (loading) {
        draw_skeleton_rows(dl, in, out.x - in.x, {0.7f, 0.5f, 0.6f}, px(kLineH), px(7));
        s.mission.animating = true;
        return;
    }
    const float fs = fpx(kMonoSize);
    const float cw = s.fonts.mono->CalcTextSizeA(fs, FLT_MAX, 0, "M").x;
    dl->PushClipRect(in, out, true);
    float y = in.y;
    for (const auto& line : c.tail) {
        for (const auto& seg : line.segments) {
            ImFont* f = seg.bold && s.fonts.mono_bold != nullptr ? s.fonts.mono_bold : s.fonts.mono;
            float x = in.x + static_cast<float>(seg.col) * cw;
            if (x > out.x)
                break;
            if (seg.wide)  // centred on its two columns
                x += (2.0f * cw - f->CalcTextSizeA(fs, FLT_MAX, 0, seg.text.c_str()).x) * 0.5f;
            dl->AddText(f, fs, ImVec2(x, y + (px(kLineH) - fs) * 0.5f), tail_color(seg), seg.text.c_str());
        }
        y += px(kLineH);
    }
    dl->PopClipRect();
    if (t.unreachable) {  // the last screen stays, dimmed, with why it does not change
        dl->AddRectFilled(a, b, alpha(g_pal->bg, 0xA0), px(6));
        const char* m = "Machine unreachable";
        const ImVec2 ts = s.fonts.ui->CalcTextSizeA(fpx(12), FLT_MAX, 0, m);
        dl->AddText(s.fonts.ui, fpx(12), ImVec2((a.x + b.x - ts.x) * 0.5f, (a.y + b.y - ts.y) * 0.5f),
                    g_pal->needs_you, m);
    }
}

// "Permission", "Question", "Auth": the kind of a blocked report, capitalised.
std::string kind_label(const std::string& kind) {
    std::string k = kind;
    if (!k.empty())
        k[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(k[0])));
    return k;
}

// The tile's right-click menu: Expand, Peek, Rename, Close pane.
void draw_tile_menu(App::State& s, const Tile& t) {
    if (!begin_menu("##tilectx"))
        return;
    menu_keys();
    const bool has_pane = s.ws.find(t.id).space >= 0;
    if (menu_row(s, ui::icon::kZoom, "Expand", "Enter"))
        run_command(s, "mission.expand:" + t.id);
    if (menu_row(s, ui::icon::kSearch, "Peek"))
        run_command(s, "agent.peek:" + t.id);
    if (menu_row(s, ui::icon::kEdit, "Rename", s.keymap.chord_for("pane.rename"), has_pane)) {
        s.ws.focus(t.id);
        run_command(s, "pane.rename");
    }
    menu_separator();
    if (menu_row(s, ui::icon::kClose, "Close pane", s.keymap.chord_for("pane.close"), has_pane)) {
        s.ws.focus(t.id);
        run_command(s, "pane.close");
    }
    ImGui::EndPopup();
}

// One tile: rounded 10 on the card colour. A 40 px header (program icon, name, "space · work",
// the state and how long ago it changed), the body, and a 26 px footer (the agent's message, else
// the kind of question it asks, and its sub-agents).
void draw_tile(App::State& s, const Tile& t, ImVec2 a, ImVec2 b, bool selected) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const auto& v = s.views_by_id[t.id];
    ImGui::SetCursorScreenPos(a);
    ImGui::PushID(t.id.c_str());
    const bool clicked =
        ImGui::InvisibleButton("##tile", ImVec2(b.x - a.x, b.y - a.y),
                               ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const bool hov = ImGui::IsItemHovered();
    const bool right = ImGui::IsItemClicked(ImGuiMouseButton_Right);
    draw_tile_menu(s, t);
    if (hov)
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    if (clicked && !right && !ImGui::IsPopupOpen("##tilectx"))
        run_command(s, "mission.expand:" + t.id);
    const float r = px(10);
    const bool blocked = t.att == Attention::kBlocked;
    if (blocked)  // a 4 px halo around the 2 px edge
        dl->AddRect(ImVec2(a.x - px(4), a.y - px(4)), ImVec2(b.x + px(4), b.y + px(4)),
                    alpha(g_pal->needs_you, 0x26), r + px(4), 0, px(4));
    dl->AddRectFilled(a, b, hov ? g_pal->hover : g_pal->card, r);
    if (blocked)
        dl->AddRect(a, b, g_pal->needs_you, r, 0, px(2));
    else if (t.att == Attention::kDone)
        dl->AddRect(a, b, g_pal->done, r, 0, 1.0f);
    else
        dl->AddRect(a, b, hov ? alpha(g_pal->focus, 120) : g_pal->border, r, 0, 1.0f);
    if (selected)  // the keyboard's tile: a 2 px accent ring just outside
        dl->AddRect(ImVec2(a.x - px(3), a.y - px(3)), ImVec2(b.x + px(3), b.y + px(3)), g_pal->focus,
                    r + px(3), 0, px(2));

    // Header.
    const float right_x = b.x - px(12);
    float mw = status_mark(s, dl, 0, 0, t.att, false, true);
    const float top_cy = a.y + px(14);
    if (mw > 0) {
        status_mark(s, dl, right_x - mw, top_cy, t.att, false);
    } else {  // a terminal without an agent status (mission_shells)
        const ImVec2 us = s.fonts.ui->CalcTextSizeA(fpx(11), FLT_MAX, 0, "Unknown");
        dl->AddText(s.fonts.ui, fpx(11), ImVec2(right_x - us.x, top_cy - us.y * 0.5f), g_pal->faint,
                    "Unknown");
        mw = us.x;
    }
    std::string ago;
    if (const auto ch = s.changed.find(t.id); ch != s.changed.end() && ch->second.at > 0)
        ago = ui::focus::short_ago(now_ms() - ch->second.at);
    if (t.unreachable)
        ago = "offline";
    const float ago_w = ago.empty() ? 0.0f : s.fonts.ui->CalcTextSizeA(fpx(11), FLT_MAX, 0, ago.c_str()).x;
    if (!ago.empty())
        dl->AddText(s.fonts.ui, fpx(11), ImVec2(right_x - ago_w, a.y + px(26)), g_pal->faint, ago.c_str());
    float x = a.x + px(12);
    program_icon(dl, program_of(v), ImVec2(x, top_cy - px(8)), px(16), g_pal->text);
    x += px(16) + px(8);
    const std::string name = (t.sub ? "\xE2\x86\xB3 " : "") + s.title_of(v);
    text_clipped(dl, s.fonts.ui_bold, fpx(13), ImVec2(x, top_cy - fpx(13) * 0.62f), g_pal->text, name,
                 std::max(0.0f, right_x - mw - px(10) - x));
    std::string where;
    if (const auto loc = s.ws.find(t.id); loc.space >= 0)
        where = s.ws.space_title(loc.space, s.session_cwds());
    if (const std::string d = agent_detail(s, t.id, v); !d.empty() && d != s.title_of(v))
        where += (where.empty() ? "" : " \xC2\xB7 ") + d;
    text_clipped(dl, s.fonts.ui, fpx(11.5f), ImVec2(x, a.y + px(25)), g_pal->dim, where,
                 std::max(0.0f, right_x - ago_w - px(10) - x));

    // Body and footer.
    const ImVec2 ba(a.x + px(8), a.y + px(kHeaderH));
    const ImVec2 bb(b.x - px(8), b.y - px(kFooterH));
    if (bb.y - ba.y > px(20))
        draw_tile_body(s, dl, t, ba, bb);
    const float fy = b.y - px(kFooterH) * 0.5f;
    float fr = right_x;
    if (!t.kids.empty()) {
        const std::string k =
            "+" + std::to_string(t.kids.size()) + (t.kids.size() == 1 ? " sub-agent" : " sub-agents");
        const ImVec2 ks = s.fonts.ui->CalcTextSizeA(fpx(11.5f), FLT_MAX, 0, k.c_str());
        ImGui::SetCursorScreenPos(ImVec2(fr - ks.x, fy - px(9)));
        const bool kc = ImGui::InvisibleButton("##kids", ImVec2(ks.x, px(18)));
        const bool kh = ImGui::IsItemHovered();
        dl->AddText(s.fonts.ui, fpx(11.5f), ImVec2(fr - ks.x, fy - ks.y * 0.5f),
                    kh ? g_pal->text : g_pal->dim, k.c_str());
        if (kc)
            run_command(s, "agent.peek:" + t.kids.front());
        fr -= ks.x + px(10);
    }
    std::string foot = mui::clean_message(v.agent_message);
    if (foot.empty() && blocked)
        foot = kind_label(v.agent_kind);
    if (!foot.empty())
        text_clipped(dl, s.fonts.ui, fpx(11.5f), ImVec2(a.x + px(12), fy - fpx(11.5f) * 0.6f),
                     blocked ? g_pal->needs_you : g_pal->dim, foot, std::max(0.0f, fr - a.x - px(12)));
    ImGui::PopID();
}

// The wall's keys while it is shown and nothing else wants them: arrows, Home and End move the
// selection, Enter opens it, 1-9 open that tile. Returns true when the selection moved.
bool wall_keys(App::State& s, const std::vector<const Tile*>& shown, int cols) {
    const ImGuiIO& io = ImGui::GetIO();
    if (shown.empty() || io.WantTextInput || io.KeyCtrl || io.KeyAlt || io.KeySuper ||
        ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId))
        return false;
    bool moved = false;
    int cur = -1;
    for (std::size_t i = 0; i < shown.size(); ++i)
        if (shown[i]->id == s.mission.selected)
            cur = static_cast<int>(i);
    const int n = static_cast<int>(shown.size());
    const std::pair<ImGuiKey, mui::Nav> moves[] = {
        {ImGuiKey_LeftArrow, mui::Nav::kLeft}, {ImGuiKey_RightArrow, mui::Nav::kRight},
        {ImGuiKey_UpArrow, mui::Nav::kUp},     {ImGuiKey_DownArrow, mui::Nav::kDown},
        {ImGuiKey_Home, mui::Nav::kHome},      {ImGuiKey_End, mui::Nav::kEnd}};
    for (const auto& [key, nav] : moves)
        if (ImGui::IsKeyPressed(key)) {
            cur = mui::move_selection(cur, n, cols, nav);
            s.mission.selected = shown[static_cast<std::size_t>(cur)]->id;
            moved = true;
        }
    if ((ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)) &&
        cur >= 0)
        run_command(s, "mission.expand:" + shown[static_cast<std::size_t>(cur)]->id);
    for (int k = 1; k <= 9 && k <= n; ++k)
        if (!io.KeyShift && ImGui::IsKeyPressed(static_cast<ImGuiKey>(ImGuiKey_0 + k), false))
            run_command(s, "mission.expand:" + shown[static_cast<std::size_t>(k - 1)]->id);
    return moved;
}

// The wall when there is nothing to put on it, or no agent-host to ask.
bool draw_wall_state(App::State& s, ImVec2 pos, ImVec2 size, const Wall& w, std::size_t shown) {
    if (!s.host->connected()) {
        if (s.machine_loading("")) {  // three tile outlines with grey bars, under "Connecting..."
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddText(s.fonts.ui, fpx(13), pos, g_pal->dim, "Connecting to agent-host\xE2\x80\xA6");
            const auto g = mui::wall_grid(3, size.x, size.y - px(28), px(kGap), px(kMinTileW), px(kMinTileH));
            for (int i = 0; i < 3; ++i) {
                const ImVec2 a(pos.x + static_cast<float>(i % g.cols) * (g.tile_w + px(kGap)),
                               pos.y + px(28) + static_cast<float>(i / g.cols) * (g.tile_h + px(kGap)));
                const ImVec2 b(a.x + g.tile_w, a.y + std::min(g.tile_h, px(220)));
                dl->AddRect(a, b, g_pal->border, px(10));
                draw_skeleton_rows(dl, ImVec2(a.x + px(14), a.y + px(16)), g.tile_w - px(28),
                                   {0.7f, 0.5f, 0.6f}, px(18), px(8));
            }
            s.mission.animating = true;
            return true;
        }
        StateCard card;
        card.title = "agent-host is not running";
        card.hint = "Mission Control shows the agents agent-host runs.";
        card.detail = s.host->last_error();
        card.detail_color = g_pal->needs_you;
        card.buttons = {{"Retry", "", [&s] { s.host->request_start_host(); }}};
        draw_state_card(s, pos, size, card);
        return true;
    }
    if (w.tiles.empty()) {
        StateCard card;
        card.title = "No agents running";
        card.hint = "Start Claude Code or Codex in a terminal and it shows up here.";
        card.buttons = {{with_chord(s, "Start a program...", "agent.new"), "agent.new"},
                        {with_chord(s, "New terminal", "tab.new"), "tab.new"}};
        if (w.shells > 0 && !s.settings.mission_shells && !s.mission.shells_shown) {
            card.link = std::to_string(w.shells) + (w.shells == 1 ? " terminal" : " terminals") +
                        " without agent status \xE2\x80\x94 show them";
            card.on_link = [&s] { s.mission.shells_shown = true; };
        }
        draw_state_card(s, pos, size, card);
        return true;
    }
    if (shown == 0) {
        const std::string f = s.settings.mission_filter;
        StateCard card;
        card.title = f == "blocked"   ? "Nothing needs you"
                     : f == "working" ? "Nothing is working"
                     : f == "done"    ? "Nothing has finished"
                                      : "Nothing is going on";
        card.hint = std::to_string(w.tiles.size()) + (w.tiles.size() == 1 ? " agent is" : " agents are") +
                    " hidden by the filter.";
        card.link = "Show all";
        card.on_link = [&s] { run_command(s, "mission.filter:all"); };
        draw_state_card(s, pos, size, card);
        return true;
    }
    return false;
}

// ---------------------------------------------------------------- the bar
struct PillOut {
    float w = 0;
    bool clicked = false;
};

// A count: 22 px high, rounded 11, the state's colour at low strength (stronger under the
// pointer, a 1 px edge while it is the filter), a glyph and "N <what>" in 12 px medium.
PillOut count_pill(App::State& s, ImDrawList* dl, const char* id, float x, float cy, Attention a,
                   const std::string& text, bool on) {
    const ImU32 col = a == Attention::kBlocked ? g_pal->needs_you
                      : a == Attention::kDone  ? g_pal->done
                                               : g_pal->working;
    const float fs = fpx(12);
    const ImVec2 t = s.fonts.ui_medium->CalcTextSizeA(fs, FLT_MAX, 0, text.c_str());
    const float h = px(22);
    const float w = px(8) + px(10) + px(5) + t.x + px(8);
    const ImVec2 p(x, cy - h * 0.5f);
    ImGui::SetCursorScreenPos(p);
    PillOut out{w, ImGui::InvisibleButton(id, ImVec2(w, h))};
    const bool hov = ImGui::IsItemHovered();
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), alpha(col, hov ? 0x40 : 0x26), px(11));
    if (on)
        dl->AddRect(p, ImVec2(p.x + w, p.y + h), col, px(11), 0, 1.0f);
    const ImVec2 g(p.x + px(8) + px(5), cy);
    if (a == Attention::kBlocked) {
        const ImVec2 bs = s.fonts.ui_bold->CalcTextSizeA(fs, FLT_MAX, 0, "!");
        dl->AddText(s.fonts.ui_bold, fs, ImVec2(g.x - bs.x * 0.5f, cy - bs.y * 0.5f), col, "!");
    } else if (a == Attention::kWorking) {
        dl->AddCircle(g, px(4), col, 16, px(1.5f));  // still: a spinning one would redraw the bar
    } else {
        draw_icon(s, dl, ui::icon::kCheck, fpx(12), g, col);
    }
    dl->AddText(s.fonts.ui_medium, fs, ImVec2(p.x + px(8) + px(10) + px(5), cy - t.y * 0.5f), col,
                text.c_str());
    if (hov)
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    return out;
}

// The two-part filter (All | Active), 26 px high.
float filter_switch(App::State& s, ImDrawList* dl, float right, float cy) {
    const char* keys[] = {"all", "active"};
    const char* labels[] = {"All", "Active"};
    const float sw = px(56), h = px(26);
    const float w = sw * 2 + px(4);
    const ImVec2 a(right - w, cy - (h + px(4)) * 0.5f);
    dl->AddRectFilled(a, ImVec2(a.x + w, a.y + h + px(4)), raised_color(), px(7));
    for (int i = 0; i < 2; ++i) {
        const ImVec2 p(a.x + px(2) + sw * static_cast<float>(i), a.y + px(2));
        ImGui::SetCursorScreenPos(p);
        ImGui::PushID(i);
        if (ImGui::InvisibleButton("##mf", ImVec2(sw, h)))
            run_command(s, std::string("mission.filter:") + keys[i]);
        const bool hov = ImGui::IsItemHovered();
        ImGui::PopID();
        const bool on = s.settings.mission_filter == keys[i];
        if (on || hov)
            dl->AddRectFilled(p, ImVec2(p.x + sw, p.y + h), on ? g_pal->selected : g_pal->hover, px(6));
        const ImVec2 t = s.fonts.ui->CalcTextSizeA(fpx(12), FLT_MAX, 0, labels[i]);
        dl->AddText(s.fonts.ui, fpx(12), ImVec2(p.x + (sw - t.x) * 0.5f, p.y + (h - t.y) * 0.5f),
                    on ? g_pal->text : g_pal->dim, labels[i]);
    }
    return a.x;
}

// "+" (the new menu), Settings and back to the layout before; returns their left edge.
float bar_buttons(App::State& s, float right, float cy) {
    const float b = px(28);
    auto tip = [&](const char* text, const char* cmd) {
        const std::string chord = s.keymap.chord_for(cmd);
        return chord.empty() ? std::string(text) : std::string(text) + "  " + chord;
    };
    right -= b;
    const std::string back =
        tip(expanded(s) ? "Back to the wall" : "Back to the layout before", "mission.toggle");
    if (icon_button(s, "##mission-back", ui::icon::kLayers, ImVec2(right, cy - b * 0.5f), b, fpx(15),
                    back.c_str(), g_pal->dim))
        run_command(s, "mission.toggle");
    right -= b + px(2);
    const std::string settings = tip("Settings", "app.settings");
    if (icon_button(s, "##mission-settings", ui::icon::kSettings, ImVec2(right, cy - b * 0.5f), b, fpx(15),
                    settings.c_str(), g_pal->dim))
        run_command(s, "app.settings");
    right -= b + px(2);
    if (icon_button(s, "##mission-new", ui::icon::kPlus, ImVec2(right, cy - b * 0.5f), b, fpx(15), "New",
                    g_pal->dim))
        ImGui::OpenPopup("##newmenu");
    draw_new_menu(s);
    return right;
}

// The expanded agent: "← Wall", its program icon, name, space and state.
void bar_expanded_title(App::State& s, ImDrawList* dl, float x, float cy, float max_x) {
    const char* back = "Wall";
    const ImVec2 bt = s.fonts.ui_medium->CalcTextSizeA(fpx(13), FLT_MAX, 0, back);
    const float bw = px(16) + px(6) + bt.x + px(12);
    ImGui::SetCursorScreenPos(ImVec2(x, cy - px(13)));
    if (ImGui::InvisibleButton("##mission-wall", ImVec2(bw, px(26))))
        run_command(s, "mission.wall");
    const bool hov = ImGui::IsItemHovered();
    if (hov)
        dl->AddRectFilled(ImVec2(x, cy - px(13)), ImVec2(x + bw, cy + px(13)), g_pal->hover, px(6));
    const ImU32 bc = hov ? g_pal->text : g_pal->dim;
    draw_icon(s, dl, ui::icon::kArrowLeft, fpx(14), ImVec2(x + px(6) + px(7), cy), bc);
    dl->AddText(s.fonts.ui_medium, fpx(13), ImVec2(x + px(6) + px(16) + px(4), cy - bt.y * 0.5f), bc, back);
    x += bw + px(14);
    const std::string id = s.ws.focused_session();
    const auto it = s.views_by_id.find(id);
    if (it == s.views_by_id.end())
        return;
    const Attention att = s.attention_of(id);
    const float mw = status_mark(s, dl, 0, 0, att, false, true);
    program_icon(dl, program_of(it->second), ImVec2(x, cy - px(8)), px(16), g_pal->text);
    x += px(16) + px(8);
    const float tw = text_clipped(dl, s.fonts.ui_bold, fpx(13), ImVec2(x, cy - fpx(13) * 0.62f), g_pal->text,
                                  s.title_of(it->second), std::max(0.0f, (max_x - x) * 0.5f));
    x += tw;
    std::string where;
    if (const auto loc = s.ws.find(id); loc.space >= 0)
        where = " \xC2\xB7 " + s.ws.space_title(loc.space, s.session_cwds());
    if (const std::string d = agent_detail(s, id, it->second); !d.empty() && d != s.title_of(it->second))
        where += " \xC2\xB7 " + d;
    x += text_clipped(dl, s.fonts.ui, fpx(13), ImVec2(x, cy - fpx(13) * 0.62f), g_pal->dim, where,
                      std::max(0.0f, max_x - x - mw - px(12)));
    if (mw > 0 && x + px(12) + mw < max_x)
        status_mark(s, dl, x + px(12), cy, att, false);
}
}  // namespace

void mission_entered(App::State& s) {
    s.mission.entered = true;
    s.mission.scroll = 0;
    s.mission.shells_shown = false;
    show_child(s, "wall");
}

void draw_mission_bar(App::State& s, ImVec2 pos, float w, float h) {
    if (!s.mission.entered)
        mission_entered(s);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 end(pos.x + w, pos.y + h);
    dl->AddRectFilled(pos, end, g_pal->sidebar);
    dl->AddLine(ImVec2(pos.x, end.y - 1.0f), ImVec2(end.x, end.y - 1.0f), g_pal->border);
    const float cy = pos.y + h * 0.5f;
    float x = clear_of_traffic_lights(s, pos.x + px(14), pos.y, end.y, px(14));
    float right = end.x - px(10);
    if (!mac_window_buttons(s)) {
        draw_window_buttons(s, right, pos.y, h);
        right -= px(26) * 3 + px(4) + px(8);
    }
    right = bar_buttons(s, right, cy) - px(10);
    const bool open = expanded(s);
    if (!open)
        right = filter_switch(s, dl, right, cy) - px(12);
    else if (const std::string chord = s.keymap.chord_for("mission.toggle"); !chord.empty()) {
        const ImVec2 t = s.fonts.ui->CalcTextSizeA(fpx(11.5f), FLT_MAX, 0, chord.c_str());
        right -= t.x;
        dl->AddText(s.fonts.ui, fpx(11.5f), ImVec2(right, cy - t.y * 0.5f), g_pal->faint, chord.c_str());
        right -= px(14);
    }
    const Wall wall = s.host->connected() ? collect_wall(s) : Wall{};
    // The counts, right to left so they end where the filter starts; only "needs you" while open.
    struct Count {
        Attention a;
        int n;
        const char* what;
        const char* filter;
    };
    std::vector<Count> counts = {{Attention::kBlocked, wall.blocked, "Needs you", "blocked"}};
    if (!open) {
        counts.push_back({Attention::kWorking, wall.working, "Working", "working"});
        counts.push_back({Attention::kDone, wall.done, "Done", "done"});
    }
    float px_left = x;
    if (open) {
        // The expanded agent sits left; its pill (if any) right before the chord.
        float pr = right;
        for (auto it = counts.rbegin(); it != counts.rend(); ++it) {
            if (it->n <= 0)
                continue;
            const std::string text = std::to_string(it->n) + " " + it->what;
            const float tw = s.fonts.ui_medium->CalcTextSizeA(fpx(12), FLT_MAX, 0, text.c_str()).x + px(31);
            pr -= tw;
            const auto r = count_pill(s, dl, it->filter, pr, cy, it->a, text, false);
            if (r.clicked)
                run_command(s, "mission.wall");
            pr -= px(6);
        }
        bar_expanded_title(s, dl, x, cy, pr - px(12));
    } else {
        const char* title = "Mission Control";
        const ImVec2 t = s.fonts.ui_bold->CalcTextSizeA(fpx(13), FLT_MAX, 0, title);
        dl->AddText(s.fonts.ui_bold, fpx(13), ImVec2(x, cy - t.y * 0.5f), g_pal->text, title);
        px_left = x + t.x + px(16);
        for (const auto& c : counts) {
            if (c.n <= 0)
                continue;
            const std::string text = std::to_string(c.n) + " " + c.what;
            const bool on = s.settings.mission_filter == c.filter;
            const auto r = count_pill(s, dl, c.filter, px_left, cy, c.a, text, on);
            if (r.clicked)
                run_command(s, std::string("mission.filter:") + (on ? "all" : c.filter));
            px_left += r.w + px(6);
        }
    }
    window_drag_area(s, pos, end);
}

void draw_mission_wall(App::State& s, ImVec2 pos, ImVec2 size) {
    s.mission.animating = false;
    const Wall wall = s.host->connected() ? collect_wall(s) : Wall{};
    std::vector<const Tile*> shown;
    for (const auto& t : wall.tiles)
        if (mui::filter_matches(s.settings.mission_filter, t.att))
            shown.push_back(&t);
    // Forget the lines of tiles that are gone.
    for (auto it = s.mission.tiles.begin(); it != s.mission.tiles.end();)
        it = std::any_of(wall.tiles.begin(), wall.tiles.end(),
                         [&](const Tile& t) { return t.id == it->first; })
                 ? std::next(it)
                 : s.mission.tiles.erase(it);
    if (draw_wall_state(s, pos, size, wall, shown.size()))
        return;
    const float gap = px(kGap);
    const auto g =
        mui::wall_grid(static_cast<int>(shown.size()), size.x, size.y, gap, px(kMinTileW), px(kMinTileH));
    const bool moved = wall_keys(s, shown, g.cols);
    // Scrolling: the wheel moves a row at a time; the keyboard's tile is kept in view.
    const float row = g.tile_h + gap;
    const float max_scroll = std::max(0.0f, g.content_h - size.y);
    if (g.scroll && ImGui::IsMouseHoveringRect(pos, ImVec2(pos.x + size.x, pos.y + size.y)) &&
        ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows))
        s.mission.scroll -= ImGui::GetIO().MouseWheel * row;
    for (std::size_t i = 0; i < shown.size() && moved; ++i)
        if (shown[i]->id == s.mission.selected) {
            const float top = static_cast<float>(static_cast<int>(i) / g.cols) * row;
            if (top < s.mission.scroll)
                s.mission.scroll = top;
            else if (top + g.tile_h > s.mission.scroll + size.y)
                s.mission.scroll = top + g.tile_h - size.y;
        }
    s.mission.scroll = std::clamp(s.mission.scroll, 0.0f, max_scroll);
    ImGui::PushClipRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), true);
    for (std::size_t i = 0; i < shown.size(); ++i) {
        const int c = static_cast<int>(i) % g.cols;
        const int r = static_cast<int>(i) / g.cols;
        const ImVec2 a(pos.x + static_cast<float>(c) * (g.tile_w + gap),
                       pos.y + static_cast<float>(r) * row - s.mission.scroll);
        const ImVec2 b(a.x + g.tile_w, a.y + g.tile_h);
        if (b.y < pos.y || a.y > pos.y + size.y)
            continue;  // scrolled out of view: not drawn
        draw_tile(s, *shown[i], a, b, shown[i]->id == s.mission.selected);
    }
    ImGui::PopClipRect();
    if (g.scroll) {  // a 6 px bar at the right edge
        const float track = size.y;
        const float thumb = std::max(px(24), track * size.y / g.content_h);
        const float t = max_scroll > 0 ? s.mission.scroll / max_scroll : 0.0f;
        const float y = pos.y + (track - thumb) * t;
        ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(pos.x + size.x + px(3), y),
                                                  ImVec2(pos.x + size.x + px(9), y + thumb),
                                                  alpha(g_pal->dim, 120), px(3));
    }
}

std::optional<std::string> mission_command(App::State& s, const std::string& name, const std::string& arg) {
    if (name == "mission.toggle") {
        set_focus(s, false);
        if (s.layout().style != "mission") {
            // To the Mission Control layout (the first one, a person's own copy too), remembering
            // where from.
            const auto it = std::find_if(s.layouts.begin(), s.layouts.end(),
                                         [](const LayoutSpec& l) { return l.style == "mission"; });
            if (it == s.layouts.end())
                return std::string("no Mission Control layout");
            s.settings.layout_return = s.settings.layout;
            s.settings.layout = it->name;
            save_settings(s.settings);
            mission_entered(s);
        } else if (expanded(s)) {
            show_child(s, "wall");
        } else {
            std::string back = s.settings.layout_return;
            const auto it = std::find_if(s.layouts.begin(), s.layouts.end(), [&](const LayoutSpec& l) {
                return l.name == back && l.style != "mission";
            });
            s.settings.layout = it != s.layouts.end() ? back : std::string("workspace");
            s.mission.shells_shown = false;
            save_settings(s.settings);
            s.focus_view(s.ws.focused_session());
        }
        return std::string();
    }
    if (name == "mission.wall") {
        if (s.layout().tree.find("wall-body") == nullptr)
            return std::string("Mission Control isn't shown");
        show_child(s, "wall");
        return std::string();
    }
    if (name == "mission.expand") {
        const auto it = s.views_by_id.find(arg);
        if (it == s.views_by_id.end())
            return "no terminal \"" + arg + "\"";
        s.mission.selected = arg;
        if (s.ws.find(arg).space < 0 && !it->second.parent.empty()) {
            // A sub-agent without a pane: a look in the peek window, the wall stays.
            s.peek = arg;
            s.views[arg].focus_next = true;
            return std::string();
        }
        if (s.ws.unsettle_session(arg))
            s.doc_changed();
        s.focus(arg);
        if (s.layout().tree.find("wall-body") != nullptr)
            show_child(s, "main");
        return std::string();
    }
    if (name == "mission.filter") {
        s.settings.mission_filter = mui::filter_name(arg);
        save_settings(s.settings);
        s.mission.scroll = 0;
        return std::string();
    }
    return std::nullopt;
}

}  // namespace detail
}  // namespace agentdesk::desktop
