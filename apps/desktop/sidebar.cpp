// The sidebar: brand row, spaces list, agents list and footer.
#include "app_internal.hpp"

namespace agentdesk::desktop {
namespace detail {

// ---------------------------------------------------------------- sidebar: brand row
// workspace-chrome/main.js 48-53: the mark and name, then search and the sidebar toggle (no
// tooltips on either).
void draw_brand(App::State& s, ImVec2 pos, float w, bool rail) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float h = px(46);
    const float cy = pos.y + h * 0.5f;
    float x = pos.x + (rail ? px(6) : px(14));
    if (!rail) {
        const float m = px(20);
        dl->AddRectFilled(ImVec2(x, cy - m * 0.5f), ImVec2(x + m, cy + m * 0.5f), g_pal->focus, px(5));
        dl->AddRectFilled(ImVec2(x + px(5), cy - px(4)), ImVec2(x + px(14), cy - px(1.5f)), g_pal->card, px(1));
        dl->AddRectFilled(ImVec2(x + px(5), cy + px(1.5f)), ImVec2(x + px(11), cy + px(4)), g_pal->card, px(1));
        const ImVec2 t = s.fonts.ui_bold->CalcTextSizeA(fpx(13.5f), FLT_MAX, 0, "AgentDesk");
        dl->AddText(s.fonts.ui_bold, fpx(13.5f), ImVec2(x + m + px(8), cy - t.y * 0.5f), g_pal->text, "AgentDesk");
    }
    const float b = px(26);
    float bx = rail ? pos.x + (w - b) * 0.5f : pos.x + w - px(10) - b;
    if (icon_button(s, "##sidebar-toggle", ui::icon::kSidebar, ImVec2(bx, cy - b * 0.5f), b, fpx(15), nullptr,
                    g_pal->dim))
        run_command(s, "sidebar.rail");
    if (!rail) {
        bx -= b + px(4);
        if (icon_button(s, "##search", ui::icon::kSearch, ImVec2(bx, cy - b * 0.5f), b, fpx(15), nullptr, g_pal->dim))
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

// Git facts of a space's first terminal (spaces.js 22-27 place(): where the space is now).
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

std::vector<SpaceRow> space_rows(App::State& s) {
    // spaces.js 298-335: each linked worktree goes right under the first main checkout of its
    // repo, and the spaces are moved (space.move) so the model order is the drawn one: rail
    // numbers and the space.select keys then follow what you see.
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
    // herdr menu/main.js 84 menuIcon(): ".rename" -> square-pen, "worktree" -> git-branch (all
    // three), ".close" -> x.
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

}  // namespace

float draw_spaces(App::State& s, ImVec2 pos, float w, float h, bool rail) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    {
        // A space remembers where it was made (herdr Space.folder): its name when nothing else says.
        std::map<std::string, std::string> starts;
        for (const auto& [id, v] : s.views_by_id)
            starts[id] = v.start_cwd.empty() ? v.cwd : v.start_cwd;
        s.ws.remember_folders(starts);
    }
    const auto rows = space_rows(s);
    const auto cwds = s.session_cwds();
    s.ws.assign_colors(&cwds);
    // Header (31 px): "Spaces" + "+" (spaces.js 541-545).
    if (!rail) {
        const ImVec2 t = s.fonts.ui_medium->CalcTextSizeA(fpx(11.5f), FLT_MAX, 0, "Spaces");
        dl->AddText(s.fonts.ui_medium, fpx(11.5f), ImVec2(pos.x + px(16), pos.y + (px(31) - t.y) * 0.5f), g_pal->faint,
                    "Spaces");
    }
    {
        const float b = px(26);
        const ImVec2 bp = rail ? ImVec2(pos.x + (w - b) * 0.5f, pos.y + px(2)) : ImVec2(pos.x + w - px(10) - b, pos.y + px(2.5f));
        // AgentDesk's own (herdr opens its one shell): pick the shell the new space starts in,
        // from the shells found on this machine (PowerShell 7, Windows PowerShell, cmd, WSL).
        if (icon_button(s, "##space-new", ui::icon::kPlus, bp, b, fpx(14), nullptr, g_pal->dim))
            ImGui::OpenPopup("##space-shells");
        draw_shell_picker(s, "##space-shells", "space");
    }
    const float head = px(31);
    ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + head));
    ImGui::BeginChild("##spaces", ImVec2(w, std::max(px(20), h - head)), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoBackground);
    ImDrawList* d = ImGui::GetWindowDrawList();
    const float pad = rail ? 0.0f : px(8);
    const float rw = ImGui::GetContentRegionAvail().x - pad * 2;
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    int drop_target = -1;
    float drop_y = 0;
    // Where the active space's row is (spaces.js 342-345, 533-539).
    int active_at = -1;
    float active_top = 0, active_bottom = 0;
    for (std::size_t ri = 0; ri < rows.size(); ++ri) {
        const SpaceRow& row = rows[ri];
        const ImVec2 a0 = ImGui::GetCursorScreenPos();
        const ImVec2 a(a0.x + pad, a0.y);
        if (row.index < 0) {
            // Machine header (26 px): icon, name, state, "+", dot, chevron; click folds.
            ImGui::PushID(("m:" + row.machine).c_str());
            const float hh = px(26);
            const bool on = s.machine_connected(row.machine);
            // herdr: a small "+" link (space.new {machine}) when connected, left of the dot.
            const float plus_x = a.x + rw - px(8) - px(11) - px(7) - px(6) - px(7) - px(16);
            const bool over_plus = on && !rail && ImGui::IsMouseHoveringRect(ImVec2(plus_x, a.y), ImVec2(plus_x + px(16), a.y + hh));
            ImGui::SetCursorScreenPos(a);
            const bool header_clicked = ImGui::InvisibleButton("##mh", ImVec2(rw, hh));
            if (header_clicked && over_plus) {
                run_command(s, "space.new_on:" + row.machine);
            } else if (header_clicked) {
                if (s.folded_machines.contains(row.machine))
                    s.folded_machines.erase(row.machine);
                else
                    s.folded_machines.insert(row.machine);
                s.settings.folded_machines.assign(s.folded_machines.begin(), s.folded_machines.end());
                save_settings(s.settings);
            }
            const bool hov = ImGui::IsItemHovered();
            if (hov)
                d->AddRectFilled(a, ImVec2(a.x + rw, a.y + hh), g_pal->hover, px(6));
            const float cy = a.y + hh * 0.5f;
            if (rail) {
                draw_icon(s, d, row.machine.empty() ? ui::icon::kMonitor : ui::icon::kServer, fpx(13),
                          ImVec2(a.x + rw * 0.5f, cy), g_pal->dim);
                d->AddCircleFilled(ImVec2(a.x + rw * 0.5f + px(7), cy + px(5)), px(3), on ? g_pal->done : g_pal->faint);
                // spaces/main.js 370: "<m> · connected", "<m> · <state>", or "This computer".
                if (hov)
                    ImGui::SetTooltip("%s", row.machine.empty()
                                                ? "This computer"
                                                : (row.machine + " \xC2\xB7 " + s.machine_state(row.machine)).c_str());
            } else {
                float x = a.x + px(8);
                draw_icon(s, d, row.machine.empty() ? ui::icon::kMonitor : ui::icon::kServer, fpx(13),
                          ImVec2(x + px(6.5f), cy), g_pal->faint);
                x += px(13) + px(7);
                const std::string label = row.machine.empty() ? std::string("This computer") : row.machine;
                const float right = a.x + rw - px(8) - px(11) - px(7) - px(6) - px(7) - (on ? px(18) : 0.0f);
                float nw = text_clipped(d, s.fonts.ui, fpx(11), ImVec2(x, cy - fpx(11) * 0.6f), g_pal->faint, label,
                                        right - x);
                if (!on && !row.machine.empty())
                    text_clipped(d, s.fonts.ui, fpx(11), ImVec2(x + nw + px(7), cy - fpx(11) * 0.6f), g_pal->faint,
                                 "unreachable", right - x - nw - px(7));
                if (on)
                    draw_icon(s, d, ui::icon::kPlus, fpx(12), ImVec2(plus_x + px(8), cy), over_plus ? g_pal->text : g_pal->faint);
                d->AddCircleFilled(ImVec2(a.x + rw - px(8) - px(11) - px(7) - px(3), cy), px(3),
                                   on ? g_pal->done : g_pal->faint);
                draw_icon(s, d,
                          s.folded_machines.contains(row.machine) ? ui::icon::kChevronRight : ui::icon::kChevronDown,
                          fpx(11), ImVec2(a.x + rw - px(8) - px(5.5f), cy), g_pal->faint);
                if (hov && !on) {
                    for (const auto& r : s.remotes)
                        if (r.cfg.name == row.machine && !r.host->last_error().empty())
                            ImGui::SetTooltip("%s", r.host->last_error().c_str());
                }
            }
            ImGui::PopID();
            ImGui::SetCursorScreenPos(ImVec2(a0.x, a.y + hh));
            // herdr spaces/main.js 352, 396-402, 413: an empty, unfolded machine shows "Loading…"
            // only while its layout loads; otherwise "New space there" with a 12 px plus (in the
            // rail a cell with an 18 px plus box), which runs space.new on that machine.
            const bool empty = ri + 1 >= rows.size() || rows[ri + 1].index < 0;
            if (empty && !s.folded_machines.contains(row.machine)) {
                const bool loading = s.machine_loading(row.machine);
                const float eh_h = px(28);
                const float ew = rail ? px(32) : rw;  // RAIL_CELL
                const ImVec2 e(rail ? a0.x + (ImGui::GetContentRegionAvail().x - ew) * 0.5f : a0.x + pad, a.y + hh);
                ImGui::SetCursorScreenPos(e);
                ImGui::PushID(("e:" + row.machine).c_str());
                const bool clicked = ImGui::InvisibleButton("##empty", ImVec2(ew, eh_h));
                const bool eh = !loading && ImGui::IsItemHovered();
                ImGui::PopID();
                const float ecy = e.y + eh_h * 0.5f;
                if (loading) {
                    // 28 high, px 8 (0 in the rail, centred), 12 px dim: "Loading…" or "…".
                    const char* text = rail ? "\xE2\x80\xA6" : "Loading\xE2\x80\xA6";
                    const ImVec2 t = s.fonts.ui->CalcTextSizeA(fpx(12), FLT_MAX, 0, text);
                    d->AddText(s.fonts.ui, fpx(12),
                               ImVec2(rail ? e.x + (ew - t.x) * 0.5f : e.x + px(8), ecy - t.y * 0.5f), g_pal->faint, text);
                } else {
                    const ImU32 c = eh ? g_pal->text : g_pal->faint;
                    if (eh)
                        d->AddRectFilled(e, ImVec2(e.x + ew, e.y + eh_h), g_pal->hover, rail ? px(6) : px(7));
                    if (rail) {
                        const float bs = px(18);
                        const ImVec2 bp(e.x + (ew - bs) * 0.5f, ecy - bs * 0.5f);
                        d->AddRect(bp, ImVec2(bp.x + bs, bp.y + bs), c, px(5));
                        draw_icon(s, d, ui::icon::kPlus, fpx(10), ImVec2(bp.x + bs * 0.5f, ecy), c);
                        if (eh)
                            ImGui::SetTooltip("New space on %s",
                                              row.machine.empty() ? "this computer" : row.machine.c_str());
                    } else {
                        // px 8, gap 7: the 12 px plus, then the words.
                        draw_icon(s, d, ui::icon::kPlus, fpx(12), ImVec2(e.x + px(8) + px(6), ecy), c);
                        const ImVec2 t = s.fonts.ui->CalcTextSizeA(fpx(12), FLT_MAX, 0, "New space there");
                        d->AddText(s.fonts.ui, fpx(12), ImVec2(e.x + px(8) + px(12) + px(7), ecy - t.y * 0.5f), c,
                                   "New space there");
                    }
                    if (clicked)
                        run_command(s, "space.new_on:" + row.machine);
                }
                ImGui::SetCursorScreenPos(ImVec2(a0.x, e.y + eh_h));
            }
            continue;
        }
        const int i = row.index;
        const auto& sp = s.ws.spaces[static_cast<std::size_t>(i)];
        const bool active = i == s.ws.active_space;
        const std::string title = s.ws.space_title(i, cwds);
        ImGui::PushID(sp.id.c_str());
        if (rail) {
            // 32x26 cell, the cells touching: position number (13 px), status glyph at the right
            // (spaces.js 501-515).
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
            std::vector<Attention> atts;
            for (const auto& t2 : sp.tabs)
                for (const auto& id : s.ws.sessions_in(t2))
                    atts.push_back(s.attention_of(id));
            const Attention att = ui::most_urgent(atts);
            const bool strong = att == Attention::kBlocked || att == Attention::kDone;
            d->AddCircleFilled(ImVec2(c.x + cw - px(5), c.y + ch * 0.5f), strong ? px(2.5f) : px(1.2f),
                               att == Attention::kNone ? g_pal->faint : attention_color(att));
            if (active) {
                active_at = static_cast<int>(ri);
                active_top = c.y;
                active_bottom = c.y + ch;
            }
            ImGui::SetCursorScreenPos(ImVec2(a0.x, c.y + ch));
            ImGui::PopID();
            continue;
        }
        const float rh = row.tree ? px(28) : px(30);
        ImGui::SetCursorScreenPos(a);
        ImGui::InvisibleButton("##space", ImVec2(rw, rh), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
        const bool hov = ImGui::IsItemHovered();
        const ImVec2 b(a.x + rw, a.y + rh);
        if (active) {
            active_at = static_cast<int>(ri);
            active_top = a.y;
            active_bottom = b.y;
        }
        // Select on mouse-up on the same row; a press dragged onto another row of the same
        // machine moves the space there (spaces.js 466-489).
        if (ImGui::IsItemActivated() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            s.drag_space = i;
        if (s.drag_space >= 0 && s.drag_space < static_cast<int>(s.ws.spaces.size()) && s.drag_space != i &&
            ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
            mouse.y >= a.y && mouse.y < b.y &&
            s.ws.spaces[static_cast<std::size_t>(s.drag_space)].machine == sp.machine) {
            drop_target = i;
            drop_y = s.drag_space < i ? b.y : a.y;
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
        auto faded = [&](ImU32 c) { return alpha(c, static_cast<int>(((c >> 24) & 0xFF) * fade / 255)); };
        const float cy = a.y + rh * 0.5f;
        float x = a.x + px(8);
        if (row.tree) {
            // A worktree: indented 22 px, the branch icon in place of the avatar (spaces.js 494).
            x += px(22);
            draw_icon(s, d, ui::icon::kBranch, fpx(13), ImVec2(x + px(6.5f), cy), faded(g_pal->faint));
            x += px(13) + px(9);
        } else {
            const float side = px(18);
            const ImVec2 am(x, cy - side * 0.5f);
            d->AddRectFilled(am, ImVec2(am.x + side, am.y + side), faded(avatar_color(sp.color)), px(5));
            std::string letter = title.empty() ? std::string("?") : title.substr(0, 1);
            if (static_cast<unsigned char>(letter[0]) >= 0x80) {
                std::size_t len = 1;
                while (len < title.size() && (static_cast<unsigned char>(title[len]) & 0xC0) == 0x80)
                    ++len;
                letter = title.substr(0, len);
            } else {
                letter[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(letter[0])));
            }
            const ImVec2 t = s.fonts.ui_bold->CalcTextSizeA(fpx(10.5f), FLT_MAX, 0, letter.c_str());
            d->AddText(s.fonts.ui_bold, fpx(10.5f), ImVec2(am.x + (side - t.x) * 0.5f, am.y + (side - t.y) * 0.5f),
                       faded(IM_COL32(255, 255, 255, 255)), letter.c_str());
            x += side + px(9);
        }
        // Git tail, right-aligned: "repo · branch ↑N ↓N".
        const GitInfo g = space_git(s, sp);
        const GitTail tail = git_tail(g, title, row.tree);
        std::string ahead = tail.ahead > 0 ? "\xE2\x86\x91" + std::to_string(tail.ahead) : std::string();
        std::string behind = tail.behind > 0 ? "\xE2\x86\x93" + std::to_string(tail.behind) : std::string();
        const float ts = fpx(11.5f);
        auto tw = [&](const std::string& t) { return t.empty() ? 0.0f : s.fonts.ui->CalcTextSizeA(ts, FLT_MAX, 0, t.c_str()).x; };
        const float extra = (ahead.empty() ? 0 : tw(ahead) + px(5)) + (behind.empty() ? 0 : tw(behind) + px(5));
        const float avail = b.x - px(8) - x;
        const float name_w = s.fonts.ui->CalcTextSizeA(fpx(13), FLT_MAX, 0, title.c_str()).x;
        float tail_w = tw(tail.text);
        float name_room = name_w;
        if (name_w + px(9) + tail_w + extra > avail) {  // shrink both in proportion, 48 px minimum
            const float total = name_w + tail_w;
            const float room = std::max(0.0f, avail - px(9) - extra);
            name_room = std::max(px(48), room * name_w / std::max(1.0f, total));
            tail_w = std::max(tail.text.empty() ? 0.0f : px(48), room - name_room);
        }
        text_clipped(d, s.fonts.ui, fpx(13), ImVec2(x, cy - fpx(13) * 0.6f), faded(g_pal->text), title, name_room);
        float rx = b.x - px(8);
        if (!behind.empty()) {
            rx -= tw(behind);
            d->AddText(s.fonts.ui, ts, ImVec2(rx, cy - ts * 0.6f), faded(g_pal->needs_you), behind.c_str());
            rx -= px(5);
        }
        if (!ahead.empty()) {
            rx -= tw(ahead);
            d->AddText(s.fonts.ui, ts, ImVec2(rx, cy - ts * 0.6f), faded(g_pal->done), ahead.c_str());
            rx -= px(5);
        }
        if (!tail.text.empty()) {
            const float used = std::min(tail_w, tw(tail.text));
            text_clipped(d, s.fonts.ui, ts, ImVec2(rx - used, cy - ts * 0.6f), faded(g_pal->faint), tail.text, used);
        }
        ImGui::PopID();
        ImGui::SetCursorScreenPos(ImVec2(a0.x, a.y + rh));
    }
    if (drop_target >= 0) {
        d->AddRectFilled(ImVec2(pos.x + pad, drop_y - px(1)), ImVec2(pos.x + pad + rw, drop_y + px(1)), g_pal->focus);
    }
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        // The dragged space may have closed meanwhile (its terminals ended).
        if (s.drag_space >= 0 && s.drag_space < static_cast<int>(s.ws.spaces.size()) && drop_target >= 0)
            s.ws.move_space(s.drag_space, drop_target);
        s.drag_space = -1;
    }
    // Bring the active space into view when it, or its place in the list, changes, and when the
    // list is resized; a row already in view stays put (spaces.js 533-539, 547-548).
    {
        const std::string active_id =
            s.ws.spaces.empty() ? std::string()
                                : s.ws.spaces[static_cast<std::size_t>(std::clamp(s.ws.active_space, 0,
                                                                                  static_cast<int>(s.ws.spaces.size()) - 1))].id;
        const float list_h = ImGui::GetWindowHeight();
        const bool moved = active_id != s.spaces_revealed || active_at != s.spaces_revealed_at;
        const bool resized = std::abs(list_h - s.spaces_list_h) > 0.5f;
        if ((moved || resized) && active_at >= 0) {
            const float top = ImGui::GetWindowPos().y;
            const float scroll = ImGui::GetScrollY();
            if (active_top < top)
                ImGui::SetScrollY(scroll - (top - active_top));
            else if (active_bottom > top + list_h)
                ImGui::SetScrollY(scroll + (active_bottom - (top + list_h)));
        }
        s.spaces_revealed = active_id;
        s.spaces_revealed_at = active_at;
        s.spaces_list_h = list_h;
    }
    // Mod panels for the sidebar, below the spaces.
    if (!rail)
        draw_mod_panels(s, "sidebar");
    ImGui::Dummy(ImVec2(0, px(14)));
    ImGui::EndChild();
    return h;
}

// ---------------------------------------------------------------- sidebar: agents
// Terminals whose program reports a status (herdr agents()): the host has a structured state
// for them now or had one before.
bool is_agent(const ui::SessionView& v) {
    return !v.agent_source.empty() || v.agent_state != "Unknown" || v.last_agent_state != "Unknown";
}

std::string lead_pane(App::State& s, const std::vector<std::string>& ids) {
    for (const auto& id : ids)
        if (const auto it = s.views_by_id.find(id); it != s.views_by_id.end() && is_agent(it->second))
            return id;
    return ids.empty() ? std::string() : ids.front();
}

namespace {
int urgency(Attention a) {
    switch (a) {
        case Attention::kBlocked:
            return 4;
        case Attention::kDone:
            return 3;
        case Attention::kWorking:
            return 2;
        case Attention::kIdle:
            return 1;
        case Attention::kNone:
            break;
    }
    return 0;
}

}  // namespace

// The agent's live terminal title, cleaned like agents.js 28-51: no leading status glyph or
// spinner, no Codex "[ ! ] " mark; ignored when it is a path, has '@', or is the program name.
std::string agent_detail(App::State& s, const std::string& id, const ui::SessionView& v) {
    const auto loc = s.ws.find(id);
    if (loc.space >= 0) {
        const auto& t = s.ws.spaces[static_cast<std::size_t>(loc.space)].tabs[static_cast<std::size_t>(loc.tab)];
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
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
            .count();
}

namespace {
// "<1m", "5m", "2h", "3d" from ms (agents.js 67-73).
std::string ago_ms(long long ms) {
    const long long m = std::max(0LL, ms) / 60000;
    if (m < 1)
        return "<1m";
    if (m < 60)
        return std::to_string(m) + "m";
    const long long h = m / 60;
    return h < 24 ? std::to_string(h) + "h" : std::to_string(h / 24) + "d";
}

// agents.js 95-110: stamps agents whose state changed. Done and idle count as one: looking at a
// finished agent turns done into idle, and looking isn't the agent doing anything. Kept in
// ui.json across restarts; ones gone for 30 days are dropped.
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
        const std::string st = a == Attention::kBlocked ? "blocked" : a == Attention::kWorking ? "working" : "idle";
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

// herdr spaces.js 128-133 and 266-286: a command or an agent's turn that ends may have committed
// or checked out, so every space in that repo looks again; the 60 s fallback runs only while the
// window is in front.
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

// An agent as herdr's agents() lists it (window.rs 1855-1883, model.rs 597-605): a running
// terminal in a pane of a space whose program reports a status now.
bool live_agent(App::State& s, const std::string& id, const ui::SessionView& v) {
    return !v.lost && v.process_state == "Running" && v.agent_state != "Unknown" && !s.hidden.contains(id) &&
           s.ws.find(id).space >= 0;
}

void draw_agents(App::State& s, ImVec2 pos, float w, float h, bool rail) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddLine(ImVec2(pos.x, pos.y), ImVec2(pos.x + w, pos.y), g_pal->border);
    const bool by_priority = s.settings.agents_sort != "space";
    // Live terminals by the agent that started them (agents.js 171-172), and the agents: a
    // sub-agent without a pane isn't one of them; it shows in its parent's tree (53-54).
    std::map<std::string, std::vector<std::string>> kids;
    std::vector<std::string> list;
    std::map<std::string, std::size_t> seq;  // creation order (agents.js 240 seq)
    {
        std::vector<std::pair<std::string, std::size_t>> born;
        for (std::size_t i = 0; i < s.sessions.size(); ++i)
            born.emplace_back(s.views_by_id[s.sessions[i]->id()].created_at, i);
        std::stable_sort(born.begin(), born.end());
        for (std::size_t k = 0; k < born.size(); ++k)
            seq[s.sessions[born[k].second]->id()] = k;
    }
    for (const auto& sess : s.sessions) {
        const std::string& id = sess->id();
        const auto& v = s.views_by_id[id];
        if (v.lost)
            continue;
        const bool sub = !v.parent.empty() && s.views_by_id.count(v.parent) != 0;
        if (sub && v.process_state != "Exited")
            kids[v.parent].push_back(id);
        // herdr window.rs 1855-1883, model.rs 597-605: the list is built from the panes in the
        // spaces, and a terminal is in it only while it has a status now. An agent that exited
        // (its report dropped) or whose space or pane was closed is gone from the list.
        if (!live_agent(s, id, v))
            continue;
        list.push_back(id);
    }
    const std::set<std::string> listed(list.begin(), list.end());
    track_agents(s, list);
    auto since = [&](const std::string& id) {
        const auto it = s.changed.find(id);
        return it == s.changed.end() ? 0LL : it->second.at;
    };
    // A kept sub-agent (one with a pane) is listed too: where a tree shows it, it gets no row of
    // its own, unless it's pinned (agents.js 231-234). The rail shows no trees.
    if (!rail) {
        std::set<std::string> nested;
        for (const auto& a : list)
            for (const auto& r : ui::agent_tree(kids, a))
                nested.insert(r.id);
        std::erase_if(list, [&](const std::string& a) { return !s.ws.pinned.contains(a) && nested.contains(a); });
    }
    // Most urgent of the agent and its sub-agents (agents.js 238).
    std::map<std::string, int> urgency_of;
    for (const auto& a : list) {
        int u = ui::attention_rank(s.attention_of(a));
        for (const auto& r : ui::agent_tree(kids, a))
            u = std::max(u, ui::attention_rank(ui::kid_status(s.attention_of(r.id))));
        urgency_of[a] = u;
    }
    if (by_priority) {
        std::stable_sort(list.begin(), list.end(), [&](const std::string& a, const std::string& b) {
            return ui::priority_before({urgency_of[a], since(a), static_cast<long long>(seq[a])},
                                       {urgency_of[b], since(b), static_cast<long long>(seq[b])});
        });
    } else {
        // Model order: space, tab, pane.
        const auto all = s.ws.all_sessions();
        std::stable_sort(list.begin(), list.end(), [&](const std::string& a, const std::string& b) {
            return std::find(all.begin(), all.end(), a) < std::find(all.begin(), all.end(), b);
        });
    }
    std::stable_partition(list.begin(), list.end(), [&](const std::string& id) { return s.ws.pinned.contains(id); });
    s.agent_order = list;  // agents.js 244 setAgentOrder

    // A row reads "space · detail": the detail is a tab someone named or the agent's own title,
    // never an automatic tab number unless two rows would otherwise read the same (279-296).
    const auto cwds = s.session_cwds();
    std::map<std::string, int> tab_counts;
    auto tab_key = [&](const std::string& id) {
        const auto loc = s.ws.find(id);
        return loc.space < 0 ? std::string() : s.ws.spaces[static_cast<std::size_t>(loc.space)].tabs[static_cast<std::size_t>(loc.tab)].id;
    };
    for (const auto& a : list)
        if (const auto k = tab_key(a); !k.empty())
            ++tab_counts[k];
    auto live_title = [&](const std::string& id) {
        const auto it = s.by_id.find(id);
        return it == s.by_id.end() ? std::string() : it->second->snapshot()->title;
    };
    std::map<std::string, std::string> label, space_name;
    std::vector<std::pair<std::string, std::string>> detail_in;
    for (const auto& a : list) {
        const auto& v = s.views_by_id[a];
        const auto loc = s.ws.find(a);
        if (loc.space >= 0) {
            const auto& tab = s.ws.spaces[static_cast<std::size_t>(loc.space)].tabs[static_cast<std::size_t>(loc.tab)];
            const bool many = tab_counts[tab.id] > 1;
            const std::string lead = many ? a : lead_pane(s, s.ws.sessions_in(tab));
            const auto& lv = s.views_by_id[lead];
            label[a] = ui::agent_tab_label({ui::Workspace::tab_title(tab, loc.tab), !tab.name.empty() && !many,
                                            live_title(lead), program_of(lv).name,
                                            lv.program.empty() ? lv.profile_id : lv.program, lead});
            space_name[a] = s.ws.space_title(loc.space, cwds);
        } else {
            const std::string t = ui::clean_agent_title(live_title(a));
            label[a] = !t.empty() ? t : program_of(v).name;
        }
        detail_in.emplace_back(space_name[a], label[a]);
    }
    std::map<std::string, std::string> detail;
    {
        const auto d = ui::agent_details(detail_in);
        for (std::size_t i = 0; i < list.size(); ++i)
            detail[list[i]] = d[i];
    }

    float y = pos.y;
    if (!rail) {
        // Header (41 px: pt 14, pb 5): "Agents" + the sort button showing the current sort's icon
        // (agents.js 424-445).
        const float hy = pos.y + px(14);
        const ImVec2 t = s.fonts.ui_medium->CalcTextSizeA(fpx(11.5f), FLT_MAX, 0, "Agents");
        dl->AddText(s.fonts.ui_medium, fpx(11.5f), ImVec2(pos.x + px(16), hy + (px(22) - t.y) * 0.5f), g_pal->faint,
                    "Agents");
        const std::string tip = std::string("Sort agents: ") + (by_priority ? "Priority" : "Grouped by space");
        if (icon_button(s, "##agents-sort", by_priority ? ui::icon::kClock : ui::icon::kLayers,
                        ImVec2(pos.x + w - px(16) - px(22), hy), px(22), fpx(14), tip.c_str(), g_pal->faint))
            ImGui::OpenPopup("##sortagents");
        // The sort menu (agents.js 140-164): "Sort agents", then 31 px rows with each sort's icon
        // and a check on the current one.
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(5), px(5)));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(px(200), 0));
        const bool open = ImGui::BeginPopup("##sortagents");
        ImGui::PopStyleVar(2);
        if (open) {
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
        y += px(41);
    } else {
        y += px(6);
    }
    ImGui::SetCursorScreenPos(ImVec2(pos.x, y));
    const float rail_more = rail ? px(28) : 0.0f;
    ImGui::BeginChild("##agents", ImVec2(w, std::max(px(20), pos.y + h - y - rail_more)), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoBackground);
    ImDrawList* d = ImGui::GetWindowDrawList();
    const float pad = px(8);
    const float rw = ImGui::GetContentRegionAvail().x - (rail ? px(12) : pad * 2);
    const std::string shown = s.ws.focused_session();
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    auto inside = [&](ImVec2 a, ImVec2 b) { return mouse.x >= a.x && mouse.x < b.x && mouse.y >= a.y && mouse.y < b.y; };

    if (list.empty() && !rail) {
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

    // A sub-agent's row (agents.js 182-206): its tree lines, icon, name and status.
    auto sub_row = [&](const ui::TreeRow& r, bool compact) {
        const std::string& k = r.id;
        const auto& v = s.views_by_id[k];
        ImGui::PushID(("sub:" + k).c_str());
        const ImVec2 a0 = ImGui::GetCursorScreenPos();
        const ImVec2 a(a0.x + pad, a0.y);
        const float rh = px(26);
        const ImVec2 b(a.x + rw, a.y + rh);
        ImGui::SetCursorScreenPos(a);
        const bool clicked = ImGui::InvisibleButton("##sub", ImVec2(rw, rh));
        const bool hov = ImGui::IsItemHovered();
        const bool on = k == shown;
        if (hov)
            d->AddRectFilled(a, b, g_pal->hover, px(8));
        else if (on)
            d->AddRectFilled(a, b, raised_color(), px(8));
        const float cy = a.y + rh * 0.5f;
        float x = a.x + px(39);
        // Each line: a 9x17 box from 4 px above the row, 1.5 px left border; ├ and └ add the bottom
        // border with a 6 px rounded corner; 4 px after it.
        for (const auto line : r.lines) {
            const float top = a.y - px(4), bottom = a.y + px(13), lx = x + px(0.75f);
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
        program_icon(d, program_of(v), ImVec2(x, cy - px(7)), px(14), g_pal->text);  // agents/main.js 185
        x += px(14) + px(8);
        const Attention st = ui::kid_status(s.attention_of(k));
        const float mw = std::max(status_mark(s, d, 0, cy, st, compact, true), px(13));
        status_mark(s, d, b.x - px(8) - mw, cy, st, compact);
        const std::string name = !v.name.empty() ? v.name : program_of(v).name;
        text_clipped(d, s.fonts.ui, fpx(12.5f), ImVec2(x, cy - fpx(12.5f) * 0.6f), on ? g_pal->text : g_pal->dim, name,
                     b.x - px(8) - mw - x);
        if (clicked) {
            if (listed.contains(k) || s.ws.find(k).space >= 0)
                s.focus(k);
            else
                run_command(s, "agent.peek:" + k);  // agents.js 199: one without a pane is peeked
        }
        ImGui::SetCursorScreenPos(ImVec2(a0.x, a.y + rh));
        ImGui::PopID();
    };

    // An agent's row (agents.js 298-374).
    auto agent_row = [&](const std::string& id, int index, const std::vector<ui::TreeRow>& subs) {
        const auto& v = s.views_by_id[id];
        const Attention att = s.attention_of(id);
        const bool focused = id == shown;
        ImGui::PushID(id.c_str());
        const ImVec2 a0 = ImGui::GetCursorScreenPos();
        if (rail) {
            // 26 px, the number right-aligned (13 px, foreground when focused), the status in a
            // 14 px cell (agents.js 303-317).
            const ImVec2 a(a0.x + px(6), a0.y);
            const float rh = px(26);
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
            ImGui::PopID();
            return;
        }
        const float rh = px(30);
        const ImVec2 a(a0.x + pad, a0.y);
        const ImVec2 b(a.x + rw, a.y + rh);
        ImGui::SetCursorScreenPos(a);
        const bool clicked = ImGui::InvisibleButton("##agent", ImVec2(rw, rh));
        const bool hov = ImGui::IsItemHovered();
        if (focused)
            d->AddRectFilled(a, b, g_pal->selected, px(8));
        else if (hov)
            d->AddRectFilled(a, b, g_pal->hover, px(8));
        const float cy = a.y + rh * 0.5f;
        const bool pinned = s.ws.pinned.contains(id);
        // Left: the program icon (16 px); with sub-agents, clicking it folds them (360-365).
        const float x0 = a.x + px(8);
        program_icon(d, program_of(v), ImVec2(x0, cy - px(8)), px(16), g_pal->text);  // agents/main.js 299
        const bool on_icon = !subs.empty() && inside(ImVec2(x0, cy - px(8)), ImVec2(x0 + px(16), cy + px(8)));
        float x = x0 + px(16) + px(9);
        // Right, from the edge leftwards: status, pin (hovered) or time, fold, machine (369-373).
        const bool compact = by_priority && !pinned;
        float rx = b.x - px(8);
        const float mw = std::max(status_mark(s, d, 0, cy, att, compact, true), px(13));
        status_mark(s, d, rx - mw, cy, att, compact);
        rx -= mw + px(9);
        bool on_pin = false, on_fold = false;
        if (hov) {
            const float pb = px(17);
            const ImVec2 p0(rx - pb, cy - pb * 0.5f), p1(rx, cy + pb * 0.5f);
            on_pin = inside(p0, p1);
            if (on_pin)
                d->AddRectFilled(p0, p1, g_pal->hover, px(4));
            draw_icon(s, d, pinned ? ui::icon::kPinned : ui::icon::kPin, fpx(13), ImVec2(rx - pb * 0.5f, cy),
                      on_pin ? g_pal->text : g_pal->faint);
            if (on_pin)
                ImGui::SetTooltip("%s", pinned ? "Unpin" : "Pin to the top");
            rx -= pb + px(9);
        } else if (const long long when = since(id); when != 0) {
            const std::string t = ago_ms(now_ms() - when);
            const float tw = s.fonts.ui->CalcTextSizeA(fpx(11), FLT_MAX, 0, t.c_str()).x;
            d->AddText(s.fonts.ui, fpx(11), ImVec2(rx - tw, cy - fpx(11) * 0.6f), g_pal->faint, t.c_str());
            rx -= tw + px(9);
        }
        if (!subs.empty()) {
            // The fold chevron; folded, the count too, coloured by the most urgent sub-agent (253-277).
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
            on_fold = inside(f0, f1);
            if (on_fold)
                d->AddRectFilled(f0, f1, g_pal->hover, px(4));
            const ImU32 col = on_fold ? g_pal->text : open || top == Attention::kNone ? g_pal->faint : attention_color(top);
            draw_icon(s, d, open ? ui::icon::kChevronDown : ui::icon::kChevronRight, fpx(11),
                      ImVec2(f0.x + px(3) + px(5.5f), cy), col);
            if (!open)
                d->AddText(s.fonts.ui, fpx(11), ImVec2(f0.x + px(3) + px(11) + px(3), cy - fpx(11) * 0.6f), col,
                           count.c_str());
            rx -= fw + px(9);
        }
        if (const auto mi = s.machine_of.find(id); mi != s.machine_of.end() && !mi->second.empty()) {
            const float mw2 = s.fonts.ui->CalcTextSizeA(fpx(10), FLT_MAX, 0, mi->second.c_str()).x;
            d->AddText(s.fonts.ui, fpx(10), ImVec2(rx - mw2, cy - fpx(10) * 0.6f), g_pal->faint, mi->second.c_str());
            rx -= mw2 + px(9);
        }
        // Name: under a space header the space goes without saying; without one (priority,
        // pinned) the row names its space, then "· detail" (339-342, 366-368).
        const bool full = by_priority || pinned;
        std::string more = detail[id];
        if (const auto deco = s.mods->decorate("agent", {{"id", id},
                                                         {"title", s.pane_title(id)},
                                                         {"state", v.agent_state},
                                                         {"program", v.program},
                                                         {"cwd", v.cwd}});
            !deco.empty())
            more = deco + (more.empty() ? "" : "  " + more);
        const std::string& lab = label[id];
        const bool number = !lab.empty() && std::all_of(lab.begin(), lab.end(), [](char c) { return c >= '0' && c <= '9'; });
        std::string name;
        if (full)
            name = space_name[id].empty() ? lab : space_name[id];
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
        if (clicked) {
            if (on_icon || on_fold) {
                if (s.folded_agents.contains(id))
                    s.folded_agents.erase(id);
                else
                    s.folded_agents.insert(id);
            } else if (on_pin) {
                if (pinned)
                    s.ws.pinned.erase(id);
                else
                    s.ws.pinned.insert(id);
            } else {
                s.focus(id);
            }
        }
        // agents/main.js: the row has no tooltip (only the pin, 330, and the sort, 442, do).
        ImGui::SetCursorScreenPos(ImVec2(a0.x, a.y + rh));
        ImGui::PopID();
    };

    // A group's header; it folds the group. Folded, it says how many and the most urgent state
    // (agents.js 386-412).
    auto group_header = [&](const std::string& key, const std::string& text, ImU32 col,
                            const std::vector<std::string>& members) {
        const ImVec2 a0 = ImGui::GetCursorScreenPos();
        const ImVec2 a(a0.x + pad, a0.y + px(8));
        const float hh = px(22);
        ImGui::SetCursorScreenPos(a);
        ImGui::PushID(("g:" + key).c_str());
        if (ImGui::InvisibleButton("##gh", ImVec2(rw, hh))) {
            if (s.folded_groups.contains(key))
                s.folded_groups.erase(key);
            else
                s.folded_groups.insert(key);
        }
        ImGui::PopID();
        const bool folded = s.folded_groups.contains(key);
        const float cy = a.y + hh * 0.5f;
        float x = a.x + px(8);
        const float right = a.x + rw - px(8) - (folded ? px(11) + px(6) + px(20) : 0.0f);
        x += text_clipped(d, s.fonts.ui_medium, fpx(11), ImVec2(x, cy - fpx(11) * 0.6f), col, text, right - x);
        if (by_priority || folded) {
            const std::string n = std::to_string(members.size());
            d->AddText(s.fonts.ui_medium, fpx(11), ImVec2(x + px(6), cy - fpx(11) * 0.6f), g_pal->faint, n.c_str());
        }
        if (folded) {
            const float cx = a.x + rw - px(8) - px(11);
            draw_icon(s, d, ui::icon::kChevronRight, fpx(11), ImVec2(cx + px(5.5f), cy), g_pal->faint);
            Attention top = Attention::kNone;
            for (const auto& id : members)
                if (ui::attention_rank(s.attention_of(id)) > ui::attention_rank(top))
                    top = s.attention_of(id);
            if (!by_priority && top != Attention::kNone && top != Attention::kIdle) {
                const float mw = status_mark(s, d, 0, cy, top, true, true);
                status_mark(s, d, cx - px(6) - mw, cy, top, true);
            }
        }
        ImGui::SetCursorScreenPos(ImVec2(a0.x, a.y + hh));
        return !folded;
    };

    auto with_kids = [&](const std::string& id, int index) {
        const std::vector<ui::TreeRow> subs = rail ? std::vector<ui::TreeRow>{} : ui::agent_tree(kids, id);
        agent_row(id, index, subs);
        if (s.folded_agents.contains(id))
            return;
        for (const auto& r : subs)
            sub_row(r, by_priority);  // sub-rows are compact only in Priority (agents.js 250)
    };

    if (rail) {
        for (std::size_t i = 0; i < list.size(); ++i)
            with_kids(list[i], static_cast<int>(i));
    } else {
        // Groups in list order: Pinned, then Needs you / Done / Working / Idle, or one per space
        // (agents.js 376-384).
        struct Group {
            std::string key, label;
            ImU32 color;
            std::vector<std::string> ids;
        };
        std::vector<Group> groups;
        for (const auto& id : list) {
            std::string key, text;
            ImU32 col = g_pal->faint;
            if (s.ws.pinned.contains(id)) {
                key = "pinned";
                text = "Pinned";
            } else if (!by_priority) {
                const auto loc = s.ws.find(id);
                key = loc.space >= 0 ? "space:" + s.ws.spaces[static_cast<std::size_t>(loc.space)].id : "space:-";
                text = space_name[id];
            } else {
                const int u = std::max(urgency_of[id], 1);
                key = u == 4 ? "blocked" : u == 3 ? "done" : u == 2 ? "working" : "idle";
                text = u == 4 ? "Needs you" : u == 3 ? "Done" : u == 2 ? "Working" : "Idle";
                col = u == 4 ? g_pal->needs_you : u == 3 ? g_pal->done : u == 2 ? g_pal->working : g_pal->faint;
            }
            if (groups.empty() || groups.back().key != key)
                groups.push_back({key, text, col, {}});
            groups.back().ids.push_back(id);
        }
        int index = 0;
        for (const auto& g : groups) {
            if (!group_header(g.key, g.label, g.color, g.ids)) {
                index += static_cast<int>(g.ids.size());
                continue;
            }
            for (const auto& id : g.ids)
                with_kids(id, index++);
        }
    }
    ImGui::Dummy(ImVec2(0, px(8)));
    ImGui::EndChild();
    if (rail) {
        // "»" expands the sidebar again.
        const ImVec2 a(pos.x, pos.y + h - px(28));
        ImGui::SetCursorScreenPos(a);
        if (ImGui::InvisibleButton("##expand", ImVec2(w, px(28))))
            run_command(s, "sidebar.rail");
        const ImVec2 t = s.fonts.ui->CalcTextSizeA(fpx(13), FLT_MAX, 0, "\xC2\xBB");
        dl->AddText(s.fonts.ui, fpx(13), ImVec2(a.x + (w - t.x) * 0.5f, a.y + (px(28) - t.y) * 0.5f),
                    ImGui::IsItemHovered() ? g_pal->text : g_pal->faint, "\xC2\xBB");
    }
}

// ---------------------------------------------------------------- sidebar: footer
// workspace-chrome/main.js 35-46: Settings in muted, Add machine all dim; hover brings both to
// the foreground.
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
            dl->AddText(s.fonts.ui, fpx(13), ImVec2(x0 + px(8) + px(14) + px(9), cy - fpx(13) * 0.6f), c, label);
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
