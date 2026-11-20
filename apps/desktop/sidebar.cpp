// The sidebar: brand row, spaces list, agents list and footer.
// Portions adapted from herdr-app (Apache-2.0) and modified; see THIRD_PARTY_NOTICES.md.
#include "app_internal.hpp"

namespace agentdesk::desktop {
namespace detail {

namespace {
void toggle(std::set<std::string>& set, const std::string& key) {
    if (set.contains(key))
        set.erase(key);
    else
        set.insert(key);
}

// The most urgent state among a set of terminals.
Attention most_urgent_of(App::State& s, const std::vector<std::string>& ids) {
    std::vector<Attention> atts;
    for (const auto& id : ids)
        atts.push_back(s.attention_of(id));
    return ui::most_urgent(atts);
}

}  // namespace

// ---------------------------------------------------------------- sidebar: brand row
// The mark and the name, then search and the sidebar toggle; neither button has a tooltip. With
// the round window buttons the row starts after them, and the rail shows only them.
void draw_brand(App::State& s, ImVec2 pos, float w, bool rail) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float h = px(46);
    const float cy = pos.y + h * 0.5f;
    // The round window buttons take the row's left 76 px.
    const float x = pos.x + (rail ? px(6) : mac_window_buttons(s) ? px(76) : px(14));
    if (!rail) {
        const float m = px(20);
        dl->AddRectFilled(ImVec2(x, cy - m * 0.5f), ImVec2(x + m, cy + m * 0.5f), g_pal->focus, px(5));
        dl->AddRectFilled(ImVec2(x + px(5), cy - px(4)), ImVec2(x + px(14), cy - px(1.5f)), g_pal->card,
                          px(1));
        dl->AddRectFilled(ImVec2(x + px(5), cy + px(1.5f)), ImVec2(x + px(11), cy + px(4)), g_pal->card,
                          px(1));
        const ImVec2 t = s.fonts.ui_bold->CalcTextSizeA(fpx(13.5f), FLT_MAX, 0, "AgentDesk");
        dl->AddText(s.fonts.ui_bold, fpx(13.5f), ImVec2(x + m + px(8), cy - t.y * 0.5f), g_pal->text,
                    "AgentDesk");
    }
    const float b = px(26);
    float bx = rail ? pos.x + (w - b) * 0.5f : pos.x + w - px(10) - b;
    // On the rail the round window buttons fill the row; the rail's own "»" widens it again.
    if (!(rail && mac_window_buttons(s)) &&
        icon_button(s, "##sidebar-toggle", ui::icon::kSidebar, ImVec2(bx, cy - b * 0.5f), b, fpx(15), nullptr,
                    g_pal->dim))
        run_command(s, "sidebar.rail");
    if (!rail) {
        bx -= b + px(4);
        if (icon_button(s, "##search", ui::icon::kSearch, ImVec2(bx, cy - b * 0.5f), b, fpx(15), nullptr,
                        g_pal->dim))
            run_command(s, "app.picker");
    }
    window_drag_area(s, pos, ImVec2(pos.x + w, pos.y + h));
}

namespace {
// ---------------------------------------------------------------- sidebar: spaces
struct SpaceRow {
    int index = -1;       // space index, or -1 for a machine header
    std::string machine;  // header: machine name ("" = this computer)
    bool tree = false;    // a worktree drawn under its main checkout
};

// Git facts of a space's first terminal: where the space is now.
GitInfo space_git(App::State& s, const ui::Space& sp) {
    for (const auto& t : sp.tabs) {
        const auto ids = s.ws.sessions_in(t);
        if (ids.empty())
            continue;
        const auto it = s.views_by_id.find(ids.front());
        return it == s.views_by_id.end() ? GitInfo{} : s.git.get(it->second.cwd, sp.machine);
    }
    return {};
}

// The rows of the spaces list: machine headers (only when remote machines exist) with their
// spaces. Each linked worktree goes right under the first main checkout of its repo, and the
// spaces are reordered in the model to match, so rail numbers and the space.select keys follow
// what is drawn. No reordering while a space is being dragged.
std::vector<SpaceRow> space_rows(App::State& s) {
    const std::size_t n = s.ws.spaces.size();
    std::vector<ui::SpaceRepo> info(n);
    for (std::size_t i = 0; i < n; ++i) {
        const auto& sp = s.ws.spaces[i];
        const GitInfo g = space_git(s, sp);
        std::string key = g.common_top;
        for (auto& c : key)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        info[i] = {g.repo, g.repo && g.worktree, sp.machine + "|" + key};
    }
    std::vector<bool> tree;
    std::vector<int> order = ui::nest_worktrees(info, &tree);
    if (s.drag_space < 0 && s.ws.reorder_spaces(order)) {
        for (int i = 0; i < static_cast<int>(order.size()); ++i)
            order[static_cast<std::size_t>(i)] = i;
    }
    std::vector<std::string> machines = {""};
    for (const auto& r : s.remotes)
        machines.push_back(r.cfg.name);
    for (const auto& sp : s.ws.spaces)
        if (std::find(machines.begin(), machines.end(), sp.machine) == machines.end())
            machines.push_back(sp.machine);
    std::vector<SpaceRow> rows;
    for (const auto& m : machines) {
        if (!s.remotes.empty())
            rows.push_back({-1, m, false});
        if (s.folded_machines.contains(m))
            continue;
        for (std::size_t k = 0; k < order.size(); ++k)
            if (s.ws.spaces[static_cast<std::size_t>(order[k])].machine == m)
                rows.push_back({order[k], m, tree[k]});
    }
    return rows;
}

void space_menu(App::State& s, int i) {
    const auto& sp = s.ws.spaces[static_cast<std::size_t>(i)];
    GitInfo g;
    for (const auto& t : sp.tabs) {
        const auto ids = s.ws.sessions_in(t);
        if (!ids.empty() && s.views_by_id.count(ids.front())) {
            g = s.git.get(s.views_by_id[ids.front()].cwd, sp.machine);
            break;
        }
    }
    if (menu_row(s, ui::icon::kEdit, "Rename", s.keymap.chord_for("space.rename"))) {
        s.ws.select_space(i);
        open_rename(s, "space", sp.id, sp.name);
    }
    if (g.repo) {
        if (menu_row(s, ui::icon::kBranch, "New worktree", s.keymap.chord_for("worktree.new"))) {
            s.ws.select_space(i);
            run_command(s, "worktree.new");
        }
        if (menu_row(s, ui::icon::kBranch, "Open worktree")) {
            s.ws.select_space(i);
            run_command(s, "worktree.open");
        }
        if (g.worktree && menu_row(s, ui::icon::kBranch, "Remove worktree")) {
            s.ws.select_space(i);
            run_command(s, "worktree.remove");
        }
    }
    menu_separator();
    if (menu_row(s, ui::icon::kClose, "Close", s.keymap.chord_for("space.close")))
        confirm_close_space(s, i);
}

// A space remembers the folder it was made in; that names it when nothing else does.
void remember_space_folders(App::State& s) {
    std::map<std::string, std::string> starts;
    for (const auto& [id, v] : s.views_by_id)
        starts[id] = v.start_cwd.empty() ? v.cwd : v.start_cwd;
    s.ws.remember_folders(starts);
}

// Header (31 px): "Spaces" and a "+" that opens the shell picker. Picking the shell is AgentDesk's
// own: a Windows machine has several (PowerShell 7, Windows PowerShell, cmd, WSL).
void draw_spaces_header(App::State& s, ImDrawList* dl, ImVec2 pos, float w, bool rail) {
    if (!rail) {
        const ImVec2 t = s.fonts.ui_medium->CalcTextSizeA(fpx(11.5f), FLT_MAX, 0, "Spaces");
        dl->AddText(s.fonts.ui_medium, fpx(11.5f), ImVec2(pos.x + px(16), pos.y + (px(31) - t.y) * 0.5f),
                    g_pal->faint, "Spaces");
    }
    const float b = px(26);
    const ImVec2 bp = rail ? ImVec2(pos.x + (w - b) * 0.5f, pos.y + px(2))
                           : ImVec2(pos.x + w - px(10) - b, pos.y + px(2.5f));
    if (icon_button(s, "##space-new", ui::icon::kPlus, bp, b, fpx(14), nullptr, g_pal->dim))
        ImGui::OpenPopup("##space-shells");
    draw_shell_picker(s, "##space-shells", "space");
}

// What the spaces list shares between its rows while it draws.
struct SpaceList {
    App::State& s;
    ImDrawList* d;
    bool rail;
    float pad;  // left and right inset of the rows
    float rw;   // row width
    ImVec2 mouse;
    const std::map<std::string, std::string>& cwds;
    int drop_target = -1;  // space a dragged space would land on
    float drop_y = 0;
    int active_at = -1;  // list row of the active space
    float active_top = 0;
    float active_bottom = 0;
};

// Machine header (26 px): icon, name, state, "+", connection dot, fold chevron; a click folds.
// Connected, a small "+" left of the dot starts a space on that machine.
void draw_machine_header(SpaceList& L, const std::string& machine, ImVec2 a) {
    App::State& s = L.s;
    ImDrawList* d = L.d;
    const float rw = L.rw;
    ImGui::PushID(("m:" + machine).c_str());
    const float hh = px(26);
    const bool on = s.machine_connected(machine);
    const float plus_x = a.x + rw - px(8) - px(11) - px(7) - px(6) - px(7) - px(16);
    const bool over_plus =
        on && !L.rail && ImGui::IsMouseHoveringRect(ImVec2(plus_x, a.y), ImVec2(plus_x + px(16), a.y + hh));
    ImGui::SetCursorScreenPos(a);
    const bool header_clicked = ImGui::InvisibleButton("##mh", ImVec2(rw, hh));
    if (header_clicked && over_plus) {
        run_command(s, "space.new_on:" + machine);
    } else if (header_clicked) {
        toggle(s.folded_machines, machine);
        s.settings.folded_machines.assign(s.folded_machines.begin(), s.folded_machines.end());
        save_settings(s.settings);
    }
    const bool hov = ImGui::IsItemHovered();
    if (hov)
        d->AddRectFilled(a, ImVec2(a.x + rw, a.y + hh), g_pal->hover, px(6));
    const float cy = a.y + hh * 0.5f;
    const char* machine_icon = machine.empty() ? ui::icon::kMonitor : ui::icon::kServer;
    if (L.rail) {
        draw_icon(s, d, machine_icon, fpx(13), ImVec2(a.x + rw * 0.5f, cy), g_pal->dim);
        d->AddCircleFilled(ImVec2(a.x + rw * 0.5f + px(7), cy + px(5)), px(3),
                           on ? g_pal->done : g_pal->faint);
        // "<machine> · <state>", or "This computer".
        if (hov)
            ImGui::SetTooltip("%s", machine.empty()
                                        ? "This computer"
                                        : (machine + " \xC2\xB7 " + s.machine_state(machine)).c_str());
    } else {
        float x = a.x + px(8);
        draw_icon(s, d, machine_icon, fpx(13), ImVec2(x + px(6.5f), cy), g_pal->faint);
        x += px(13) + px(7);
        const std::string label = machine.empty() ? std::string("This computer") : machine;
        const float right = a.x + rw - px(8) - px(11) - px(7) - px(6) - px(7) - (on ? px(18) : 0.0f);
        const float nw = text_clipped(d, s.fonts.ui, fpx(11), ImVec2(x, cy - fpx(11) * 0.6f), g_pal->faint,
                                      label, right - x);
        if (!on && !machine.empty())
            text_clipped(d, s.fonts.ui, fpx(11), ImVec2(x + nw + px(7), cy - fpx(11) * 0.6f), g_pal->faint,
                         "unreachable", right - x - nw - px(7));
        if (on)
            draw_icon(s, d, ui::icon::kPlus, fpx(12), ImVec2(plus_x + px(8), cy),
                      over_plus ? g_pal->text : g_pal->faint);
        d->AddCircleFilled(ImVec2(a.x + rw - px(8) - px(11) - px(7) - px(3), cy), px(3),
                           on ? g_pal->done : g_pal->faint);
        draw_icon(s, d,
                  s.folded_machines.contains(machine) ? ui::icon::kChevronRight : ui::icon::kChevronDown,
                  fpx(11), ImVec2(a.x + rw - px(8) - px(5.5f), cy), g_pal->faint);
        if (hov && !on) {
            for (const auto& r : s.remotes)
                if (r.cfg.name == machine && !r.host->last_error().empty())
                    ImGui::SetTooltip("%s", r.host->last_error().c_str());
        }
    }
    ImGui::PopID();
}

// The row under a machine with no spaces (28 px). While the machine's layout loads it reads
// "Loading…" ("…" in the rail); otherwise "New space there" with a 12 px plus (in the rail an
// 18 px plus box in a 32 px cell), which starts a space on that machine. Returns the row's bottom.
float draw_machine_empty_row(SpaceList& L, const std::string& machine, ImVec2 a0, float top) {
    App::State& s = L.s;
    ImDrawList* d = L.d;
    const bool rail = L.rail;
    const bool loading = s.machine_loading(machine);
    const float row_h = px(28);
    const float ew = rail ? px(32) : L.rw;
    const ImVec2 e(rail ? a0.x + (ImGui::GetContentRegionAvail().x - ew) * 0.5f : a0.x + L.pad, top);
    ImGui::SetCursorScreenPos(e);
    ImGui::PushID(("e:" + machine).c_str());
    const bool clicked = ImGui::InvisibleButton("##empty", ImVec2(ew, row_h));
    const bool hov = !loading && ImGui::IsItemHovered();
    ImGui::PopID();
    const float ecy = e.y + row_h * 0.5f;
    if (loading) {
        // 8 px in (centred in the rail), 12 px faint.
        const char* text = rail ? "\xE2\x80\xA6" : "Loading\xE2\x80\xA6";
        const ImVec2 t = s.fonts.ui->CalcTextSizeA(fpx(12), FLT_MAX, 0, text);
        d->AddText(s.fonts.ui, fpx(12),
                   ImVec2(rail ? e.x + (ew - t.x) * 0.5f : e.x + px(8), ecy - t.y * 0.5f), g_pal->faint,
                   text);
        return e.y + row_h;
    }
    const ImU32 c = hov ? g_pal->text : g_pal->faint;
    if (hov)
        d->AddRectFilled(e, ImVec2(e.x + ew, e.y + row_h), g_pal->hover, rail ? px(6) : px(7));
    if (rail) {
        const float bs = px(18);
        const ImVec2 bp(e.x + (ew - bs) * 0.5f, ecy - bs * 0.5f);
        d->AddRect(bp, ImVec2(bp.x + bs, bp.y + bs), c, px(5));
        draw_icon(s, d, ui::icon::kPlus, fpx(10), ImVec2(bp.x + bs * 0.5f, ecy), c);
        if (hov)
            ImGui::SetTooltip("New space on %s", machine.empty() ? "this computer" : machine.c_str());
    } else {
        // 8 px in, then the 12 px plus, a 7 px gap and the words.
        draw_icon(s, d, ui::icon::kPlus, fpx(12), ImVec2(e.x + px(8) + px(6), ecy), c);
        const ImVec2 t = s.fonts.ui->CalcTextSizeA(fpx(12), FLT_MAX, 0, "New space there");
        d->AddText(s.fonts.ui, fpx(12), ImVec2(e.x + px(8) + px(12) + px(7), ecy - t.y * 0.5f), c,
                   "New space there");
    }
    if (clicked)
        run_command(s, "space.new_on:" + machine);
    return e.y + row_h;
}

// A space in the rail: a 32x26 cell (cells touch), its position number (13 px) and a status dot
// at the right. Returns the cell's bottom.
float draw_rail_space(SpaceList& L, int ri, int i, ImVec2 a0) {
    App::State& s = L.s;
    ImDrawList* d = L.d;
    const auto& sp = s.ws.spaces[static_cast<std::size_t>(i)];
    const bool active = i == s.ws.active_space;
    const float cw = px(32), ch = px(26);
    const ImVec2 c(a0.x + (ImGui::GetContentRegionAvail().x - cw) * 0.5f, a0.y);
    ImGui::SetCursorScreenPos(c);
    if (ImGui::InvisibleButton("##sr", ImVec2(cw, ch))) {
        s.ws.select_space(i);
        s.focus_view(s.ws.focused_session());
    }
    if (active)
        d->AddRectFilled(c, ImVec2(c.x + cw, c.y + ch), g_pal->selected, px(6));
    else if (ImGui::IsItemHovered())
        d->AddRectFilled(c, ImVec2(c.x + cw, c.y + ch), g_pal->hover, px(6));
    const std::string num = std::to_string(i + 1);
    const ImVec2 t = s.fonts.ui->CalcTextSizeA(fpx(13), FLT_MAX, 0, num.c_str());
    d->AddText(s.fonts.ui, fpx(13), ImVec2(c.x + (cw - t.x) * 0.5f, c.y + (ch - t.y) * 0.5f),
               active ? g_pal->text : g_pal->dim, num.c_str());
    std::vector<std::string> ids;
    for (const auto& tab : sp.tabs)
        for (const auto& id : s.ws.sessions_in(tab))
            ids.push_back(id);
    const Attention att = most_urgent_of(s, ids);
    const bool strong = att == Attention::kBlocked || att == Attention::kDone;
    d->AddCircleFilled(ImVec2(c.x + cw - px(5), c.y + ch * 0.5f), strong ? px(2.5f) : px(1.2f),
                       att == Attention::kNone ? g_pal->faint : attention_color(att));
    if (active) {
        L.active_at = ri;
        L.active_top = c.y;
        L.active_bottom = c.y + ch;
    }
    return c.y + ch;
}

// The avatar letter: the title's first character, upper-cased when ASCII.
std::string avatar_letter(const std::string& title) {
    std::string letter = title.empty() ? std::string("?") : title.substr(0, 1);
    if (static_cast<unsigned char>(letter[0]) >= 0x80) {
        std::size_t len = 1;
        while (len < title.size() && (static_cast<unsigned char>(title[len]) & 0xC0) == 0x80)
            ++len;
        letter = title.substr(0, len);
    } else {
        letter[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(letter[0])));
    }
    return letter;
}

// A dragged row draws at half opacity.
ImU32 faded(ImU32 c, ImU32 fade) {
    return alpha(c, static_cast<int>(((c >> 24) & 0xFF) * fade / 255));
}

// The space's name and, right-aligned, its git tail "repo · branch ↑N ↓N". When both don't fit
// they shrink in proportion, each to at least 48 px.
void draw_space_name_and_tail(SpaceList& L, const ui::Space& sp, const SpaceRow& row,
                              const std::string& title, float x, float cy, float right, ImU32 fade) {
    App::State& s = L.s;
    ImDrawList* d = L.d;
    const GitInfo g = space_git(s, sp);
    const GitTail tail = git_tail(g, title, row.tree);
    const std::string ahead = tail.ahead > 0 ? "\xE2\x86\x91" + std::to_string(tail.ahead) : std::string();
    const std::string behind = tail.behind > 0 ? "\xE2\x86\x93" + std::to_string(tail.behind) : std::string();
    const float ts = fpx(11.5f);
    auto tw = [&](const std::string& t) {
        return t.empty() ? 0.0f : s.fonts.ui->CalcTextSizeA(ts, FLT_MAX, 0, t.c_str()).x;
    };
    const float extra = (ahead.empty() ? 0 : tw(ahead) + px(5)) + (behind.empty() ? 0 : tw(behind) + px(5));
    const float avail = right - px(8) - x;
    const float name_w = s.fonts.ui->CalcTextSizeA(fpx(13), FLT_MAX, 0, title.c_str()).x;
    float tail_w = tw(tail.text);
    float name_room = name_w;
    if (name_w + px(9) + tail_w + extra > avail) {
        const float total = name_w + tail_w;
        const float room = std::max(0.0f, avail - px(9) - extra);
        name_room = std::max(px(48), room * name_w / std::max(1.0f, total));
        tail_w = std::max(tail.text.empty() ? 0.0f : px(48), room - name_room);
    }
    text_clipped(d, s.fonts.ui, fpx(13), ImVec2(x, cy - fpx(13) * 0.6f), faded(g_pal->text, fade), title,
                 name_room);
    float rx = right - px(8);
    if (!behind.empty()) {
        rx -= tw(behind);
        d->AddText(s.fonts.ui, ts, ImVec2(rx, cy - ts * 0.6f), faded(g_pal->needs_you, fade), behind.c_str());
        rx -= px(5);
    }
    if (!ahead.empty()) {
        rx -= tw(ahead);
        d->AddText(s.fonts.ui, ts, ImVec2(rx, cy - ts * 0.6f), faded(g_pal->done, fade), ahead.c_str());
        rx -= px(5);
    }
    if (!tail.text.empty()) {
        const float used = std::min(tail_w, tw(tail.text));
        text_clipped(d, s.fonts.ui, ts, ImVec2(rx - used, cy - ts * 0.6f), faded(g_pal->faint, fade),
                     tail.text, used);
    }
}

// A space in the full sidebar: 30 px (a worktree 28 px). Selects on mouse-up over the same row;
// a press dragged onto another row of the same machine moves the space there. Returns the
// row's bottom.
float draw_space_row(SpaceList& L, int ri, const SpaceRow& row, ImVec2 a) {
    App::State& s = L.s;
    ImDrawList* d = L.d;
    const int i = row.index;
    const auto& sp = s.ws.spaces[static_cast<std::size_t>(i)];
    const bool active = i == s.ws.active_space;
    const std::string title = s.ws.space_title(i, L.cwds);
    const float rh = row.tree ? px(28) : px(30);
    ImGui::SetCursorScreenPos(a);
    ImGui::InvisibleButton("##space", ImVec2(L.rw, rh),
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const bool hov = ImGui::IsItemHovered();
    const ImVec2 b(a.x + L.rw, a.y + rh);
    if (active) {
        L.active_at = ri;
        L.active_top = a.y;
        L.active_bottom = b.y;
    }
    if (ImGui::IsItemActivated() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        s.drag_space = i;
    if (s.drag_space >= 0 && s.drag_space < static_cast<int>(s.ws.spaces.size()) && s.drag_space != i &&
        ImGui::IsMouseDown(ImGuiMouseButton_Left) && L.mouse.y >= a.y && L.mouse.y < b.y &&
        s.ws.spaces[static_cast<std::size_t>(s.drag_space)].machine == sp.machine) {
        L.drop_target = i;
        L.drop_y = s.drag_space < i ? b.y : a.y;
    }
    if (hov && ImGui::IsMouseReleased(ImGuiMouseButton_Left) && s.drag_space == i) {
        s.ws.select_space(i);
        s.focus_view(s.ws.focused_session());
    }
    if (begin_menu("##spacectx")) {
        space_menu(s, i);
        ImGui::EndPopup();
    }
    const bool dragged = s.drag_space == i && ImGui::IsMouseDragging(ImGuiMouseButton_Left, px(4));
    if (active)
        d->AddRectFilled(a, b, g_pal->selected, px(8));
    else if (hov)
        d->AddRectFilled(a, b, g_pal->hover, px(8));
    const ImU32 fade = dragged ? 128u : 255u;
    const float cy = a.y + rh * 0.5f;
    float x = a.x + px(8);
    if (row.tree) {
        // A worktree: indented 22 px, a branch icon in place of the avatar.
        x += px(22);
        draw_icon(s, d, ui::icon::kBranch, fpx(13), ImVec2(x + px(6.5f), cy), faded(g_pal->faint, fade));
        x += px(13) + px(9);
    } else {
        const float side = px(18);
        const ImVec2 am(x, cy - side * 0.5f);
        d->AddRectFilled(am, ImVec2(am.x + side, am.y + side), faded(avatar_color(sp.color), fade), px(5));
        const std::string letter = avatar_letter(title);
        const ImVec2 t = s.fonts.ui_bold->CalcTextSizeA(fpx(10.5f), FLT_MAX, 0, letter.c_str());
        d->AddText(s.fonts.ui_bold, fpx(10.5f),
                   ImVec2(am.x + (side - t.x) * 0.5f, am.y + (side - t.y) * 0.5f),
                   faded(IM_COL32(255, 255, 255, 255), fade), letter.c_str());
        x += side + px(9);
    }
    draw_space_name_and_tail(L, sp, row, title, x, cy, b.x, fade);
    return a.y + rh;
}

// The drop line for a dragged space, and the drop itself on release.
void finish_space_drag(SpaceList& L, ImVec2 pos) {
    App::State& s = L.s;
    if (L.drop_target >= 0) {
        L.d->AddRectFilled(ImVec2(pos.x + L.pad, L.drop_y - px(1)),
                           ImVec2(pos.x + L.pad + L.rw, L.drop_y + px(1)), g_pal->focus);
    }
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        // The dragged space may have closed meanwhile (its terminals ended).
        if (s.drag_space >= 0 && s.drag_space < static_cast<int>(s.ws.spaces.size()) && L.drop_target >= 0)
            s.ws.move_space(s.drag_space, L.drop_target);
        s.drag_space = -1;
    }
}

// Brings the active space into view when it, or its place in the list, changes, and when the
// list is resized; a row already in view stays put. Runs inside the list's child window.
void reveal_active_space(const SpaceList& L) {
    App::State& s = L.s;
    const std::string active_id =
        s.ws.spaces.empty()
            ? std::string()
            : s.ws.spaces[static_cast<std::size_t>(
                              std::clamp(s.ws.active_space, 0, static_cast<int>(s.ws.spaces.size()) - 1))]
                  .id;
    const float list_h = ImGui::GetWindowHeight();
    const bool moved = active_id != s.spaces_revealed || L.active_at != s.spaces_revealed_at;
    const bool resized = std::abs(list_h - s.spaces_list_h) > 0.5f;
    if ((moved || resized) && L.active_at >= 0) {
        const float top = ImGui::GetWindowPos().y;
        const float scroll = ImGui::GetScrollY();
        if (L.active_top < top)
            ImGui::SetScrollY(scroll - (top - L.active_top));
        else if (L.active_bottom > top + list_h)
            ImGui::SetScrollY(scroll + (L.active_bottom - (top + list_h)));
    }
    s.spaces_revealed = active_id;
    s.spaces_revealed_at = L.active_at;
    s.spaces_list_h = list_h;
}

}  // namespace

float draw_spaces(App::State& s, ImVec2 pos, float w, float h, bool rail) {
    remember_space_folders(s);
    const auto rows = space_rows(s);
    const auto cwds = s.session_cwds();
    s.ws.assign_colors(&cwds);
    draw_spaces_header(s, ImGui::GetWindowDrawList(), pos, w, rail);
    const float head = px(31);
    ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + head));
    ImGui::BeginChild("##spaces", ImVec2(w, std::max(px(20), h - head)), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoBackground);
    const float pad = rail ? 0.0f : px(8);
    SpaceList list{s,
                   ImGui::GetWindowDrawList(),
                   rail,
                   pad,
                   ImGui::GetContentRegionAvail().x - pad * 2,
                   ImGui::GetIO().MousePos,
                   cwds};
    for (std::size_t ri = 0; ri < rows.size(); ++ri) {
        const SpaceRow& row = rows[ri];
        const ImVec2 a0 = ImGui::GetCursorScreenPos();
        const ImVec2 a(a0.x + pad, a0.y);
        if (row.index < 0) {
            draw_machine_header(list, row.machine, a);
            float bottom = a.y + px(26);
            ImGui::SetCursorScreenPos(ImVec2(a0.x, bottom));
            const bool empty = ri + 1 >= rows.size() || rows[ri + 1].index < 0;
            if (empty && !s.folded_machines.contains(row.machine)) {
                bottom = draw_machine_empty_row(list, row.machine, a0, bottom);
                ImGui::SetCursorScreenPos(ImVec2(a0.x, bottom));
            }
            continue;
        }
        ImGui::PushID(s.ws.spaces[static_cast<std::size_t>(row.index)].id.c_str());
        const float bottom = rail ? draw_rail_space(list, static_cast<int>(ri), row.index, a0)
                                  : draw_space_row(list, static_cast<int>(ri), row, a);
        ImGui::SetCursorScreenPos(ImVec2(a0.x, bottom));
        ImGui::PopID();
    }
    finish_space_drag(list, pos);
    reveal_active_space(list);
    // Mod panels for the sidebar, below the spaces.
    if (!rail)
        draw_mod_panels(s, "sidebar");
    ImGui::Dummy(ImVec2(0, px(14)));
    ImGui::EndChild();
    return h;
}

// ---------------------------------------------------------------- sidebar: agents
// A terminal whose program reports a status: the host has a structured state for it now or had
// one before.
bool is_agent(const ui::SessionView& v) {
    return !v.agent_source.empty() || v.agent_state != "Unknown" || v.last_agent_state != "Unknown";
}

std::string lead_pane(App::State& s, const std::vector<std::string>& ids) {
    for (const auto& id : ids)
        if (const auto it = s.views_by_id.find(id); it != s.views_by_id.end() && is_agent(it->second))
            return id;
    return ids.empty() ? std::string() : ids.front();
}

// The agent's live terminal title, cleaned up: no leading status glyph or spinner, no Codex
// "[ ! ] " mark; ignored when it is a path, has '@', or is just the program name. A tab name or a
// start name wins over the title.
std::string agent_detail(App::State& s, const std::string& id, const ui::SessionView& v) {
    const auto loc = s.ws.find(id);
    if (loc.space >= 0) {
        const auto& t =
            s.ws.spaces[static_cast<std::size_t>(loc.space)].tabs[static_cast<std::size_t>(loc.tab)];
        if (!t.name.empty())
            return t.name;
    }
    if (!v.name.empty())
        return v.name;  // the name it was started with (agentctl run --name, sub-agent name)
    std::string title;
    if (const auto it = s.by_id.find(id); it != s.by_id.end())
        title = it->second->snapshot()->title;
    // Strip leading non-ASCII glyphs (spinners, status dots) and spaces.
    std::size_t i = 0;
    while (i < title.size()) {
        const unsigned char c = static_cast<unsigned char>(title[i]);
        if (c == ' ') {
            ++i;
        } else if (c >= 0x80) {
            const std::size_t len = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : 2;
            // Keep letters of other scripts (CJK starts at U+2E80: 3-byte lead >= 0xE2 0xBA).
            if (c >= 0xE3)
                break;
            i += len;
        } else {
            break;
        }
    }
    title = title.substr(i);
    for (const char* mark : {"[ ! ] ", "[ . ] "})
        if (title.rfind(mark, 0) == 0)
            title = title.substr(6);
    const auto pi = program_of(v);
    if (title.empty() || title.find('@') != std::string::npos || title.find(":\\") != std::string::npos ||
        title.front() == '/' || title.front() == '~' || _stricmp(title.c_str(), pi.name.c_str()) == 0 ||
        _stricmp(title.c_str(), v.program.c_str()) == 0)
        return {};
    return title;
}

long long now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

namespace {
// "<1m", "5m", "2h", "3d" from milliseconds.
std::string ago_ms(long long ms) {
    const long long m = std::max(0LL, ms) / 60000;
    if (m < 1)
        return "<1m";
    if (m < 60)
        return std::to_string(m) + "m";
    const long long h = m / 60;
    return h < 24 ? std::to_string(h) + "h" : std::to_string(h / 24) + "d";
}

// Stamps the agents whose state changed, for the time column and the priority sort. Done and idle
// count as one state: looking at a finished agent turns done into idle, and looking is not the
// agent doing anything. Kept in ui.json across restarts; stamps of agents gone for 30 days are
// dropped.
void track_agents(App::State& s, const std::vector<std::string>& list) {
    if (!s.changed_loaded) {
        s.changed_loaded = true;
        for (const auto& [id, c] : s.settings.agents_changed.items())
            if (c.is_object())
                s.changed[id] = {safe::string_or(c, "s"), safe::int_or(c, "at", 0)};
    }
    const long long now = now_ms();
    bool dirty = false;
    for (const auto& id : list) {
        const Attention a = s.attention_of(id);
        if (a == Attention::kNone)
            continue;  // no state known yet (just started, or not running)
        const std::string st = a == Attention::kBlocked   ? "blocked"
                               : a == Attention::kWorking ? "working"
                                                          : "idle";
        auto& c = s.changed[id];
        if (c.s == st)
            continue;
        c = {st, now};
        dirty = true;
    }
    if (!dirty)
        return;
    const std::set<std::string> listed(list.begin(), list.end());
    Json out = Json::object();
    for (auto it = s.changed.begin(); it != s.changed.end();) {
        if (!listed.contains(it->first) && now - it->second.at > 30LL * 86400000LL) {
            it = s.changed.erase(it);
            continue;
        }
        out[it->first] = {{"s", it->second.s}, {"at", it->second.at}};
        ++it;
    }
    s.settings.agents_changed = std::move(out);
    save_settings(s.settings);
}

}  // namespace

// When a command or an agent's turn ends it may have committed or checked out, so every space in
// that repo looks again. The periodic fallback refresh runs only while the window is in front.
void refresh_git_facts(App::State& s) {
    s.git.set_active(s.window_focused);
    for (const auto& [id, v] : s.views_by_id) {
        const bool busy = v.agent_state == "Working" || !(is_shell(v.program) || is_agent(v));
        const auto it = s.busy_terminal.find(id);
        if (it != s.busy_terminal.end() && it->second && !busy) {
            const auto mi = s.machine_of.find(id);
            s.git.invalidate_repo(v.cwd, mi == s.machine_of.end() ? std::string() : mi->second);
        }
        s.busy_terminal[id] = busy;
    }
}

// An agent in the agents list: a running terminal in a pane of a space whose program reports a
// status now. One that exited (its report dropped) or whose space or pane closed is gone.
bool live_agent(App::State& s, const std::string& id, const ui::SessionView& v) {
    return !v.lost && v.process_state == "Running" && v.agent_state != "Unknown" && !s.hidden.contains(id) &&
           s.ws.find(id).space >= 0;
}

namespace {
// The agents to list, in order, with what their rows show.
struct AgentList {
    std::map<std::string, std::vector<std::string>> kids;  // live sub-agents by parent
    std::vector<std::string> ids;                          // the rows, in display order
    std::set<std::string> listed;                          // every live agent, nested ones too
    std::map<std::string, int> urgency;  // rank of the agent and its sub-agents, most urgent
    std::map<std::string, std::string> label, space_name, detail;
};

long long changed_at(const App::State& s, const std::string& id) {
    const auto it = s.changed.find(id);
    return it == s.changed.end() ? 0LL : it->second.at;
}

// Creation order of the terminals, for the last priority tie-break.
std::map<std::string, std::size_t> creation_order(App::State& s) {
    std::vector<std::pair<std::string, std::size_t>> born;
    for (std::size_t i = 0; i < s.sessions.size(); ++i)
        born.emplace_back(s.views_by_id[s.sessions[i]->id()].created_at, i);
    std::stable_sort(born.begin(), born.end());
    std::map<std::string, std::size_t> seq;
    for (std::size_t k = 0; k < born.size(); ++k)
        seq[s.sessions[born[k].second]->id()] = k;
    return seq;
}

// Picks and orders the agents. A sub-agent without a pane is not listed; it shows in its
// parent's tree. A kept sub-agent (one with a pane) is listed too, but where a tree shows it, it
// gets no row of its own unless it is pinned; the rail shows no trees. Priority puts the most
// urgent first (the agent with its sub-agents), then the latest change, then the newest; "space"
// keeps model order. Pinned agents go first either way.
AgentList collect_agents(App::State& s, bool rail, bool by_priority) {
    AgentList out;
    auto seq = creation_order(s);
    for (const auto& sess : s.sessions) {
        const std::string& id = sess->id();
        const auto& v = s.views_by_id[id];
        if (v.lost)
            continue;
        const bool sub = !v.parent.empty() && s.views_by_id.count(v.parent) != 0;
        if (sub && v.process_state != "Exited")
            out.kids[v.parent].push_back(id);
        if (!live_agent(s, id, v))
            continue;
        out.ids.push_back(id);
    }
    auto& list = out.ids;
    out.listed = std::set<std::string>(list.begin(), list.end());
    track_agents(s, list);
    if (!rail) {
        std::set<std::string> nested;
        for (const auto& a : list)
            for (const auto& r : ui::agent_tree(out.kids, a))
                nested.insert(r.id);
        std::erase_if(list,
                      [&](const std::string& a) { return !s.ws.pinned.contains(a) && nested.contains(a); });
    }
    for (const auto& a : list) {
        int u = ui::attention_rank(s.attention_of(a));
        for (const auto& r : ui::agent_tree(out.kids, a))
            u = std::max(u, ui::attention_rank(ui::kid_status(s.attention_of(r.id))));
        out.urgency[a] = u;
    }
    if (by_priority) {
        auto& urgency = out.urgency;
        std::stable_sort(list.begin(), list.end(), [&](const std::string& a, const std::string& b) {
            return ui::priority_before({urgency[a], changed_at(s, a), static_cast<long long>(seq[a])},
                                       {urgency[b], changed_at(s, b), static_cast<long long>(seq[b])});
        });
    } else {
        // Model order: space, tab, pane.
        const auto all = s.ws.all_sessions();
        std::stable_sort(list.begin(), list.end(), [&](const std::string& a, const std::string& b) {
            return std::find(all.begin(), all.end(), a) < std::find(all.begin(), all.end(), b);
        });
    }
    std::stable_partition(list.begin(), list.end(),
                          [&](const std::string& id) { return s.ws.pinned.contains(id); });
    s.agent_order = list;
    return out;
}

// A row reads "space · detail": the detail is a tab someone named or the agent's own title, never
// an automatic tab number unless two rows would otherwise read the same.
void label_agents(App::State& s, AgentList& out) {
    const auto& list = out.ids;
    const auto cwds = s.session_cwds();
    std::map<std::string, int> tab_counts;
    auto tab_key = [&](const std::string& id) {
        const auto loc = s.ws.find(id);
        return loc.space < 0 ? std::string()
                             : s.ws.spaces[static_cast<std::size_t>(loc.space)]
                                   .tabs[static_cast<std::size_t>(loc.tab)]
                                   .id;
    };
    for (const auto& a : list)
        if (const auto k = tab_key(a); !k.empty())
            ++tab_counts[k];
    auto live_title = [&](const std::string& id) {
        const auto it = s.by_id.find(id);
        return it == s.by_id.end() ? std::string() : it->second->snapshot()->title;
    };
    std::vector<std::pair<std::string, std::string>> detail_in;
    for (const auto& a : list) {
        const auto& v = s.views_by_id[a];
        const auto loc = s.ws.find(a);
        if (loc.space >= 0) {
            const auto& tab =
                s.ws.spaces[static_cast<std::size_t>(loc.space)].tabs[static_cast<std::size_t>(loc.tab)];
            const bool many = tab_counts[tab.id] > 1;
            const std::string lead = many ? a : lead_pane(s, s.ws.sessions_in(tab));
            const auto& lv = s.views_by_id[lead];
            out.label[a] = ui::agent_tab_label(
                {ui::Workspace::tab_title(tab, loc.tab), !tab.name.empty() && !many, live_title(lead),
                 program_of(lv).name, lv.program.empty() ? lv.profile_id : lv.program, lead});
            out.space_name[a] = s.ws.space_title(loc.space, cwds);
        } else {
            const std::string t = ui::clean_agent_title(live_title(a));
            out.label[a] = !t.empty() ? t : program_of(v).name;
        }
        detail_in.emplace_back(out.space_name[a], out.label[a]);
    }
    const auto d = ui::agent_details(detail_in);
    for (std::size_t i = 0; i < list.size(); ++i)
        out.detail[list[i]] = d[i];
}

// The sort menu: a "Sort agents" caption, then 31 px rows with each sort's icon and a check on
// the current one.
void draw_agents_sort_menu(App::State& s, bool by_priority) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(5), px(5)));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(px(200), 0));
    const bool open = ImGui::BeginPopup("##sortagents");
    ImGui::PopStyleVar(2);
    if (!open)
        return;
    const ImVec2 m = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddText(s.fonts.ui, fpx(11), ImVec2(m.x + px(8), m.y + px(4)), g_pal->faint,
                                        "Sort agents");
    ImGui::Dummy(ImVec2(px(190), px(4) + fpx(11) * 1.15f + px(6)));
    struct Sort {
        const char* id;
        const char* label;
        const char* icon;
    };
    for (const Sort& o : {Sort{"priority", "Priority", ui::icon::kClock},
                          Sort{"space", "Grouped by space", ui::icon::kLayers}}) {
        const bool on = (o.id == std::string("space")) != by_priority;
        if (menu_row(s, o.icon, o.label)) {
            s.settings.agents_sort = o.id;
            save_settings(s.settings);
        }
        if (on) {
            const ImVec2 r1 = ImGui::GetItemRectMax();
            const ImVec2 r0 = ImGui::GetItemRectMin();
            draw_icon(s, ImGui::GetWindowDrawList(), ui::icon::kCheck, fpx(14),
                      ImVec2(r1.x - px(8) - px(7), (r0.y + r1.y) * 0.5f), g_pal->text);
        }
    }
    ImGui::EndPopup();
}

// Header (41 px: 14 above, 5 below): "Agents" and the sort button, which shows the current sort's
// icon.
void draw_agents_header(App::State& s, ImDrawList* dl, ImVec2 pos, float w, bool by_priority) {
    const float hy = pos.y + px(14);
    const ImVec2 t = s.fonts.ui_medium->CalcTextSizeA(fpx(11.5f), FLT_MAX, 0, "Agents");
    dl->AddText(s.fonts.ui_medium, fpx(11.5f), ImVec2(pos.x + px(16), hy + (px(22) - t.y) * 0.5f),
                g_pal->faint, "Agents");
    const std::string tip = std::string("Sort agents: ") + (by_priority ? "Priority" : "Grouped by space");
    if (icon_button(s, "##agents-sort", by_priority ? ui::icon::kClock : ui::icon::kLayers,
                    ImVec2(pos.x + w - px(16) - px(22), hy), px(22), fpx(14), tip.c_str(), g_pal->faint))
        ImGui::OpenPopup("##sortagents");
    draw_agents_sort_menu(s, by_priority);
}

void draw_no_agents_note(App::State& s, float w) {
    ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x + px(12), ImGui::GetCursorScreenPos().y));
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + w - px(36));
    ImGui::PushStyleColor(ImGuiCol_Text, v4(g_pal->faint));
    ImGui::PushFont(s.fonts.ui, 12);
    ImGui::TextWrapped(
        "No agent status yet. Agents show up here when they report their status. Older versions and agents "
        "inside tmux may not.");
    ImGui::PopFont();
    ImGui::PopStyleColor();
    ImGui::PopTextWrapPos();
}

// What the agent rows share while the list draws.
struct AgentRows {
    App::State& s;
    AgentList& list;
    ImDrawList* d;
    bool rail;
    bool by_priority;
    float pad;          // left inset of the rows
    float rw;           // row width
    std::string shown;  // the focused terminal
    ImVec2 mouse;

    [[nodiscard]] bool inside(ImVec2 a, ImVec2 b) const {
        return mouse.x >= a.x && mouse.x < b.x && mouse.y >= a.y && mouse.y < b.y;
    }
};

// Tree lines left of a sub-agent: each is a 9x17 box from 4 px above the row with a 1.5 px left
// border; ├ and └ add the bottom border with a 6 px rounded corner; 4 px after each. Advances x.
void draw_tree_lines(ImDrawList* d, const std::vector<ui::TreeLine>& lines, float row_top, float& x) {
    for (const auto line : lines) {
        const float top = row_top - px(4), bottom = row_top + px(13), lx = x + px(0.75f);
        if (line == ui::TreeLine::kBar) {
            d->AddLine(ImVec2(lx, top), ImVec2(lx, bottom), g_pal->border, px(1.5f));
        } else if (line != ui::TreeLine::kNone) {
            const float rr = px(6);
            const float by = bottom - px(0.75f);
            d->PathLineTo(ImVec2(lx, top));
            d->PathLineTo(ImVec2(lx, by - rr));
            d->PathArcTo(ImVec2(lx + rr, by - rr), rr, 3.14159265f, 3.14159265f * 0.5f, 8);
            d->PathLineTo(ImVec2(x + px(9), by));
            d->PathStroke(g_pal->border, 0, px(1.5f));
        }
        x += px(9) + px(4);
    }
}

// A sub-agent's row (26 px): its tree lines, program icon, name and status. A click focuses it;
// one without a pane is peeked instead.
void draw_sub_agent_row(AgentRows& R, const ui::TreeRow& r, bool compact) {
    App::State& s = R.s;
    ImDrawList* d = R.d;
    const std::string& k = r.id;
    const auto& v = s.views_by_id[k];
    ImGui::PushID(("sub:" + k).c_str());
    const ImVec2 a0 = ImGui::GetCursorScreenPos();
    const ImVec2 a(a0.x + R.pad, a0.y);
    const float rh = px(26);
    const ImVec2 b(a.x + R.rw, a.y + rh);
    ImGui::SetCursorScreenPos(a);
    const bool clicked = ImGui::InvisibleButton("##sub", ImVec2(R.rw, rh));
    const bool hov = ImGui::IsItemHovered();
    const bool on = k == R.shown;
    if (hov)
        d->AddRectFilled(a, b, g_pal->hover, px(8));
    else if (on)
        d->AddRectFilled(a, b, raised_color(), px(8));
    const float cy = a.y + rh * 0.5f;
    float x = a.x + px(39);
    draw_tree_lines(d, r.lines, a.y, x);
    program_icon(d, program_of(v), ImVec2(x, cy - px(7)), px(14), g_pal->text);
    x += px(14) + px(8);
    const Attention st = ui::kid_status(s.attention_of(k));
    const float mw = std::max(status_mark(s, d, 0, cy, st, compact, true), px(13));
    status_mark(s, d, b.x - px(8) - mw, cy, st, compact);
    const std::string name = !v.name.empty() ? v.name : program_of(v).name;
    text_clipped(d, s.fonts.ui, fpx(12.5f), ImVec2(x, cy - fpx(12.5f) * 0.6f), on ? g_pal->text : g_pal->dim,
                 name, b.x - px(8) - mw - x);
    if (clicked) {
        if (R.list.listed.contains(k) || s.ws.find(k).space >= 0)
            s.focus(k);
        else
            run_command(s, "agent.peek:" + k);
    }
    ImGui::SetCursorScreenPos(ImVec2(a0.x, a.y + rh));
    ImGui::PopID();
}

// An agent in the rail (26 px): its number right-aligned (13 px, foreground when focused) and the
// status in a 14 px cell.
void draw_rail_agent_row(AgentRows& R, const std::string& id, int index) {
    App::State& s = R.s;
    ImDrawList* d = R.d;
    const Attention att = s.attention_of(id);
    const bool focused = id == R.shown;
    const ImVec2 a0 = ImGui::GetCursorScreenPos();
    const ImVec2 a(a0.x + px(6), a0.y);
    const float rh = px(26);
    const float rw = R.rw;
    ImGui::SetCursorScreenPos(a);
    if (ImGui::InvisibleButton("##ar", ImVec2(rw, rh)))
        s.focus(id);
    const bool hov = ImGui::IsItemHovered();
    if (focused)
        d->AddRectFilled(a, ImVec2(a.x + rw, a.y + rh), g_pal->selected, px(6));
    else if (hov)
        d->AddRectFilled(a, ImVec2(a.x + rw, a.y + rh), g_pal->hover, px(6));
    const float cell = a.x + rw - px(2) - px(14);
    const float mw = status_mark(s, d, 0, a.y + rh * 0.5f, att, true, true);
    status_mark(s, d, cell + (px(14) - mw) * 0.5f, a.y + rh * 0.5f, att, true);
    const std::string num = std::to_string(index + 1);
    const ImVec2 t = s.fonts.ui->CalcTextSizeA(fpx(13), FLT_MAX, 0, num.c_str());
    d->AddText(s.fonts.ui, fpx(13), ImVec2(cell - px(4) - t.x, a.y + (rh - t.y) * 0.5f),
               focused ? g_pal->text : g_pal->dim, num.c_str());
    ImGui::SetCursorScreenPos(ImVec2(a0.x, a.y + rh));
}

// What the right side of an agent row left for the name, and which control the pointer is on.
struct AgentRowRight {
    float rx = 0;  // right edge left for the name
    bool on_pin = false;
    bool on_fold = false;
};

// The fold chevron for an agent's sub-agents; folded, their count too, coloured by the most
// urgent sub-agent. Draws right-aligned at `rx`; returns its width.
float draw_agent_fold(AgentRows& R, const std::string& id, const std::vector<ui::TreeRow>& subs, float rx,
                      float cy, bool* on_fold) {
    App::State& s = R.s;
    ImDrawList* d = R.d;
    const bool open = !s.folded_agents.contains(id);
    Attention top = Attention::kNone;
    for (const auto& r : subs) {
        const Attention ks = ui::kid_status(s.attention_of(r.id));
        if (ui::attention_rank(ks) > ui::attention_rank(top))
            top = ks;
    }
    const std::string count = std::to_string(subs.size());
    const float cw = open ? 0.0f : s.fonts.ui->CalcTextSizeA(fpx(11), FLT_MAX, 0, count.c_str()).x + px(3);
    const float fw = px(3) + px(11) + cw + px(3);
    const ImVec2 f0(rx - fw, cy - px(9)), f1(rx, cy + px(9));
    *on_fold = R.inside(f0, f1);
    if (*on_fold)
        d->AddRectFilled(f0, f1, g_pal->hover, px(4));
    const ImU32 col = *on_fold                          ? g_pal->text
                      : open || top == Attention::kNone ? g_pal->faint
                                                        : attention_color(top);
    draw_icon(s, d, open ? ui::icon::kChevronDown : ui::icon::kChevronRight, fpx(11),
              ImVec2(f0.x + px(3) + px(5.5f), cy), col);
    if (!open)
        d->AddText(s.fonts.ui, fpx(11), ImVec2(f0.x + px(3) + px(11) + px(3), cy - fpx(11) * 0.6f), col,
                   count.c_str());
    return fw;
}

// The right side of an agent row, from the edge leftwards: status, then the pin (while hovered)
// or the time since the last change, the fold control, the machine.
AgentRowRight draw_agent_row_right(AgentRows& R, const std::string& id, const std::vector<ui::TreeRow>& subs,
                                   float right, float cy, bool hov, bool pinned) {
    App::State& s = R.s;
    ImDrawList* d = R.d;
    const Attention att = s.attention_of(id);
    AgentRowRight out;
    const bool compact = R.by_priority && !pinned;
    float rx = right - px(8);
    const float mw = std::max(status_mark(s, d, 0, cy, att, compact, true), px(13));
    status_mark(s, d, rx - mw, cy, att, compact);
    rx -= mw + px(9);
    if (hov) {
        const float pb = px(17);
        const ImVec2 p0(rx - pb, cy - pb * 0.5f), p1(rx, cy + pb * 0.5f);
        out.on_pin = R.inside(p0, p1);
        if (out.on_pin)
            d->AddRectFilled(p0, p1, g_pal->hover, px(4));
        draw_icon(s, d, pinned ? ui::icon::kPinned : ui::icon::kPin, fpx(13), ImVec2(rx - pb * 0.5f, cy),
                  out.on_pin ? g_pal->text : g_pal->faint);
        if (out.on_pin)
            ImGui::SetTooltip("%s", pinned ? "Unpin" : "Pin to the top");
        rx -= pb + px(9);
    } else if (const long long when = changed_at(s, id); when != 0) {
        const std::string t = ago_ms(now_ms() - when);
        const float tw = s.fonts.ui->CalcTextSizeA(fpx(11), FLT_MAX, 0, t.c_str()).x;
        d->AddText(s.fonts.ui, fpx(11), ImVec2(rx - tw, cy - fpx(11) * 0.6f), g_pal->faint, t.c_str());
        rx -= tw + px(9);
    }
    if (!subs.empty())
        rx -= draw_agent_fold(R, id, subs, rx, cy, &out.on_fold) + px(9);
    if (const auto mi = s.machine_of.find(id); mi != s.machine_of.end() && !mi->second.empty()) {
        const float mw2 = s.fonts.ui->CalcTextSizeA(fpx(10), FLT_MAX, 0, mi->second.c_str()).x;
        d->AddText(s.fonts.ui, fpx(10), ImVec2(rx - mw2, cy - fpx(10) * 0.6f), g_pal->faint,
                   mi->second.c_str());
        rx -= mw2 + px(9);
    }
    out.rx = rx;
    return out;
}

// The agent's name. Under a space header the space goes without saying; without one (priority,
// pinned) the row names its space, then "· detail" in faint text.
void draw_agent_name(AgentRows& R, const std::string& id, float x, float rx, float cy, bool pinned) {
    App::State& s = R.s;
    ImDrawList* d = R.d;
    const auto& v = s.views_by_id[id];
    const Attention att = s.attention_of(id);
    const bool focused = id == R.shown;
    const bool full = R.by_priority || pinned;
    std::string more = R.list.detail[id];
    if (const auto deco = s.mods->decorate("agent", {{"id", id},
                                                     {"title", s.pane_title(id)},
                                                     {"state", v.agent_state},
                                                     {"program", v.program},
                                                     {"cwd", v.cwd}});
        !deco.empty())
        more = deco + (more.empty() ? "" : "  " + more);
    const std::string& lab = R.list.label[id];
    const bool number =
        !lab.empty() && std::all_of(lab.begin(), lab.end(), [](char c) { return c >= '0' && c <= '9'; });
    std::string name;
    if (full)
        name = R.list.space_name[id].empty() ? lab : R.list.space_name[id];
    else if (!more.empty())
        name = more;
    else
        name = number ? program_of(v).name : lab;
    const ImU32 name_col = att == Attention::kIdle && !focused ? g_pal->dim : g_pal->text;
    const float fs = fpx(13);
    const float nw = text_clipped(d, s.fonts.ui, fs, ImVec2(x, cy - fs * 0.6f), name_col, name,
                                  std::min(full && !more.empty() ? px(120) : px(200), rx - x));
    if (full && !more.empty())
        text_clipped(d, s.fonts.ui, fs, ImVec2(x + nw + px(5), cy - fs * 0.6f), g_pal->faint,
                     "\xC2\xB7 " + more, rx - x - nw - px(5));
}

// An agent's row (30 px). The program icon (16 px) at the left folds the sub-agents when there
// are any; the pin toggles pinning; anywhere else focuses the agent. The row itself has no
// tooltip; only the pin and the sort button do.
void draw_agent_row(AgentRows& R, const std::string& id, const std::vector<ui::TreeRow>& subs) {
    App::State& s = R.s;
    ImDrawList* d = R.d;
    const auto& v = s.views_by_id[id];
    const bool focused = id == R.shown;
    const ImVec2 a0 = ImGui::GetCursorScreenPos();
    const float rh = px(30);
    const ImVec2 a(a0.x + R.pad, a0.y);
    const ImVec2 b(a.x + R.rw, a.y + rh);
    ImGui::SetCursorScreenPos(a);
    const bool clicked = ImGui::InvisibleButton("##agent", ImVec2(R.rw, rh));
    const bool hov = ImGui::IsItemHovered();
    if (focused)
        d->AddRectFilled(a, b, g_pal->selected, px(8));
    else if (hov)
        d->AddRectFilled(a, b, g_pal->hover, px(8));
    const float cy = a.y + rh * 0.5f;
    const bool pinned = s.ws.pinned.contains(id);
    const float x0 = a.x + px(8);
    program_icon(d, program_of(v), ImVec2(x0, cy - px(8)), px(16), g_pal->text);
    const bool on_icon = !subs.empty() && R.inside(ImVec2(x0, cy - px(8)), ImVec2(x0 + px(16), cy + px(8)));
    const float x = x0 + px(16) + px(9);
    const AgentRowRight right = draw_agent_row_right(R, id, subs, b.x, cy, hov, pinned);
    draw_agent_name(R, id, x, right.rx, cy, pinned);
    if (clicked) {
        if (on_icon || right.on_fold)
            toggle(s.folded_agents, id);
        else if (right.on_pin)
            toggle(s.ws.pinned, id);
        else
            s.focus(id);
    }
    ImGui::SetCursorScreenPos(ImVec2(a0.x, a.y + rh));
}

// A group's header (22 px, 8 px above); a click folds the group. With Priority, or folded, it
// shows how many agents it holds; folded and grouped by space, also the most urgent state.
// Returns whether the group is open.
bool draw_group_header(AgentRows& R, const std::string& key, const std::string& text, ImU32 col,
                       const std::vector<std::string>& members) {
    App::State& s = R.s;
    ImDrawList* d = R.d;
    const float rw = R.rw;
    const ImVec2 a0 = ImGui::GetCursorScreenPos();
    const ImVec2 a(a0.x + R.pad, a0.y + px(8));
    const float hh = px(22);
    ImGui::SetCursorScreenPos(a);
    ImGui::PushID(("g:" + key).c_str());
    if (ImGui::InvisibleButton("##gh", ImVec2(rw, hh)))
        toggle(s.folded_groups, key);
    ImGui::PopID();
    const bool folded = s.folded_groups.contains(key);
    const float cy = a.y + hh * 0.5f;
    float x = a.x + px(8);
    const float right = a.x + rw - px(8) - (folded ? px(11) + px(6) + px(20) : 0.0f);
    x += text_clipped(d, s.fonts.ui_medium, fpx(11), ImVec2(x, cy - fpx(11) * 0.6f), col, text, right - x);
    if (R.by_priority || folded) {
        const std::string n = std::to_string(members.size());
        d->AddText(s.fonts.ui_medium, fpx(11), ImVec2(x + px(6), cy - fpx(11) * 0.6f), g_pal->faint,
                   n.c_str());
    }
    if (folded) {
        const float cx = a.x + rw - px(8) - px(11);
        draw_icon(s, d, ui::icon::kChevronRight, fpx(11), ImVec2(cx + px(5.5f), cy), g_pal->faint);
        Attention top = Attention::kNone;
        for (const auto& id : members)
            if (ui::attention_rank(s.attention_of(id)) > ui::attention_rank(top))
                top = s.attention_of(id);
        if (!R.by_priority && top != Attention::kNone && top != Attention::kIdle) {
            const float mw = status_mark(s, d, 0, cy, top, true, true);
            status_mark(s, d, cx - px(6) - mw, cy, top, true);
        }
    }
    ImGui::SetCursorScreenPos(ImVec2(a0.x, a.y + hh));
    return !folded;
}

// An agent's row, then (unless folded) its sub-agents' rows. Sub-rows are compact only with the
// Priority sort.
void draw_agent_with_subs(AgentRows& R, const std::string& id, int index) {
    App::State& s = R.s;
    ImGui::PushID(id.c_str());
    if (R.rail) {
        draw_rail_agent_row(R, id, index);
        ImGui::PopID();
        return;
    }
    const std::vector<ui::TreeRow> subs = ui::agent_tree(R.list.kids, id);
    draw_agent_row(R, id, subs);
    ImGui::PopID();
    if (s.folded_agents.contains(id))
        return;
    for (const auto& r : subs)
        draw_sub_agent_row(R, r, R.by_priority);
}

struct AgentGroup {
    std::string key, label;
    ImU32 color;
    std::vector<std::string> ids;
};

// Groups in list order: Pinned, then Needs you / Done / Working / Idle (Priority) or one per
// space.
std::vector<AgentGroup> agent_groups(App::State& s, AgentList& list, bool by_priority) {
    std::vector<AgentGroup> groups;
    for (const auto& id : list.ids) {
        std::string key, text;
        ImU32 col = g_pal->faint;
        if (s.ws.pinned.contains(id)) {
            key = "pinned";
            text = "Pinned";
        } else if (!by_priority) {
            const auto loc = s.ws.find(id);
            key = loc.space >= 0 ? "space:" + s.ws.spaces[static_cast<std::size_t>(loc.space)].id : "space:-";
            text = list.space_name[id];
        } else {
            const int u = std::max(list.urgency[id], 1);
            key = u == 4 ? "blocked" : u == 3 ? "done" : u == 2 ? "working" : "idle";
            text = u == 4 ? "Needs you" : u == 3 ? "Done" : u == 2 ? "Working" : "Idle";
            col = u == 4 ? g_pal->needs_you : u == 3 ? g_pal->done : u == 2 ? g_pal->working : g_pal->faint;
        }
        if (groups.empty() || groups.back().key != key)
            groups.push_back({key, text, col, {}});
        groups.back().ids.push_back(id);
    }
    return groups;
}

// "»" at the bottom of the rail expands the sidebar again.
void draw_rail_expand(App::State& s, ImDrawList* dl, ImVec2 pos, float w, float h) {
    const ImVec2 a(pos.x, pos.y + h - px(28));
    ImGui::SetCursorScreenPos(a);
    if (ImGui::InvisibleButton("##expand", ImVec2(w, px(28))))
        run_command(s, "sidebar.rail");
    const ImVec2 t = s.fonts.ui->CalcTextSizeA(fpx(13), FLT_MAX, 0, "\xC2\xBB");
    dl->AddText(s.fonts.ui, fpx(13), ImVec2(a.x + (w - t.x) * 0.5f, a.y + (px(28) - t.y) * 0.5f),
                ImGui::IsItemHovered() ? g_pal->text : g_pal->faint, "\xC2\xBB");
}

}  // namespace

void draw_agents(App::State& s, ImVec2 pos, float w, float h, bool rail) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddLine(ImVec2(pos.x, pos.y), ImVec2(pos.x + w, pos.y), g_pal->border);
    const bool by_priority = s.settings.agents_sort != "space";
    AgentList list = collect_agents(s, rail, by_priority);
    label_agents(s, list);

    float y = pos.y;
    if (!rail) {
        draw_agents_header(s, dl, pos, w, by_priority);
        y += px(41);
    } else {
        y += px(6);
    }
    ImGui::SetCursorScreenPos(ImVec2(pos.x, y));
    const float rail_more = rail ? px(28) : 0.0f;
    ImGui::BeginChild("##agents", ImVec2(w, std::max(px(20), pos.y + h - y - rail_more)),
                      ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
    const float pad = px(8);
    AgentRows rows{s,
                   list,
                   ImGui::GetWindowDrawList(),
                   rail,
                   by_priority,
                   pad,
                   ImGui::GetContentRegionAvail().x - (rail ? px(12) : pad * 2),
                   s.ws.focused_session(),
                   ImGui::GetIO().MousePos};
    if (list.ids.empty() && !rail)
        draw_no_agents_note(s, w);
    if (rail) {
        for (std::size_t i = 0; i < list.ids.size(); ++i)
            draw_agent_with_subs(rows, list.ids[i], static_cast<int>(i));
    } else {
        int index = 0;
        for (const auto& g : agent_groups(s, list, by_priority)) {
            if (!draw_group_header(rows, g.key, g.label, g.color, g.ids)) {
                index += static_cast<int>(g.ids.size());
                continue;
            }
            for (const auto& id : g.ids)
                draw_agent_with_subs(rows, id, index++);
        }
    }
    ImGui::Dummy(ImVec2(0, px(8)));
    ImGui::EndChild();
    if (rail)
        draw_rail_expand(s, dl, pos, w, h);
}

// ---------------------------------------------------------------- sidebar: footer
// Settings in dim, Add machine in faint; hover brings either to the foreground.
void draw_sidebar_footer(App::State& s, ImVec2 pos, float w, bool rail) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddLine(pos, ImVec2(pos.x + w, pos.y), g_pal->border);
    float y = pos.y + px(4);
    auto item = [&](const char* id, const char* glyph, const char* label, ImU32 col, const char* cmd) {
        const float rh = px(28);
        const float x0 = rail ? pos.x + (w - px(32)) * 0.5f : pos.x + px(8);
        const float iw = rail ? px(32) : w - px(16);
        ImGui::SetCursorScreenPos(ImVec2(x0, y));
        const bool clicked = ImGui::InvisibleButton(id, ImVec2(iw, rh));
        const bool hov = ImGui::IsItemHovered();
        if (hov)
            dl->AddRectFilled(ImVec2(x0, y), ImVec2(x0 + iw, y + rh), g_pal->hover, px(8));
        const float cy = y + rh * 0.5f;
        const ImU32 c = hov ? g_pal->text : col;
        if (rail) {
            draw_icon(s, dl, glyph, fpx(14), ImVec2(x0 + iw * 0.5f, cy), c);
            if (hov)
                ImGui::SetTooltip("%s", label);
        } else {
            draw_icon(s, dl, glyph, fpx(14), ImVec2(x0 + px(8) + px(7), cy), c);
            dl->AddText(s.fonts.ui, fpx(13), ImVec2(x0 + px(8) + px(14) + px(9), cy - fpx(13) * 0.6f), c,
                        label);
        }
        if (clicked)
            run_command(s, cmd);
        y += rh;
    };
    item("##f-settings", ui::icon::kSettings, "Settings", g_pal->dim, "app.settings");
    item("##f-machine", ui::icon::kPlus, "Add machine", g_pal->faint, "machine.add");
}
}  // namespace detail

}  // namespace agentdesk::desktop
