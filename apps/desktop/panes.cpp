// Panes: one terminal pane with its header, the split tree of panes, and the side panel header.
#include "win98_internal.hpp"

namespace agentdesk::desktop {
namespace detail {

namespace {
void draw_pane(App::State& s, const std::string& id, ui::Rect r, bool connected) {
    const auto vit = s.views_by_id.find(id);
    const auto sit = s.by_id.find(id);
    if (vit == s.views_by_id.end() || sit == s.by_id.end())
        return;
    const auto& v = vit->second;
    auto& vs = s.views[id];
    const bool focused = s.ws.focused_session() == id;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 pos(r.x, r.y);
    const ImVec2 max(r.x + r.w, r.y + r.h);
    const Attention att = s.attention_of(id);
    // herdr pane frame (main.json 107-131, pane.js 59-91): radius 10, 1 px border, 1 px accent
    // when focused; 30 px header: icon, name, folder (~), status; buttons only while hovered.
    // The Terminals layout (terminals.json 37-45): radius 12, a quiet border and focus edge, and
    // its own 38 px header (terminals-pane).
    // Herdr 98 (win98.json 26-58, kit.js 464-473): radius 0, a 3 px #c0c0c0 border (the same
    // focused), background #000000, its own 18 px title bar at the top (window.js).
    const bool tm = s.layout().style == "terminals";
    const bool w98p = s.layout().style == "win98";
    const float edge = w98p ? w98::pane_border(s) : 0.0f;
    const TerminalsLook tl = terminals_look();
    const float radius = w98p ? 0.0f : px(tm ? 12.0f : 10.0f);
    if (w98p) {
        dl->AddRectFilled(pos, max, IM_COL32(0, 0, 0, 255));
        const ImU32 face = IM_COL32(0xc0, 0xc0, 0xc0, 255);
        dl->AddRectFilled(pos, ImVec2(max.x, pos.y + edge), face);
        dl->AddRectFilled(ImVec2(pos.x, max.y - edge), max, face);
        dl->AddRectFilled(pos, ImVec2(pos.x + edge, max.y), face);
        dl->AddRectFilled(ImVec2(max.x - edge, pos.y), max, face);
    } else {
        dl->AddRectFilled(pos, max, g_pal->term_bg, radius);
        dl->AddRect(pos, max, tm ? (focused ? tl.pane_focus : tl.pane_border) : (focused ? g_pal->focus : g_pal->border),
                    radius, 0, 1.0f);
    }

    const float fs = ImGui::GetFontSize();
    const float header_h = w98p ? w98::pane_header_h(s) : px(tm ? 38.0f : 30.0f);
    ImGui::PushID(id.c_str());
    if (w98p) {
        w98::pane_header_at(s, id, ImVec2(pos.x + edge, pos.y + edge), ImVec2(max.x - edge, pos.y + edge + header_h), focused);
    } else if (tm) {
        draw_terminals_pane_header(s, id, v, pos, r.w, header_h, focused);
    } else {
        const float cy = pos.y + header_h * 0.5f;
        // herdr pane/main.js 86-90: the buttons show while the header is hovered. Pressing one
        // makes it the active item, which must not hide it before the release lands.
        const bool pane_hovered =
            ImGui::IsMouseHoveringRect(pos, ImVec2(max.x, pos.y + header_h), false) &&
            ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
        float hx = pos.x + px(10);
        const ui::Program prog = program_of(v);
        program_icon(dl, prog, ImVec2(hx, cy - px(7)), px(14), g_pal->dim);  // pane/main.js 76
        hx += px(14) + px(8);
        // pane/main.js 61: name || ((agent || shell) && program.name) || title || program.name || "shell".
        std::string title = s.pane_title(id);
        if (v.name.empty() && !prog.agent && !prog.shell)
            if (const auto si = s.by_id.find(id); si != s.by_id.end()) {
                std::string t = si->second->snapshot()->title;
                while (!t.empty() && std::isspace(static_cast<unsigned char>(t.back())))
                    t.pop_back();
                if (!t.empty() && (title.empty() || title == prog.name))
                    title = t;
            }
        if (title.empty())
            title = "shell";
        // pane/main.js 69, 78-79: the name 12 px medium, the folder 12 px dim.
        const float tw = text_clipped(dl, s.fonts.ui_medium, fpx(12), ImVec2(hx, cy - fpx(12) * 0.6f), g_pal->text, title,
                                      std::min(px(260), r.w * 0.4f));
        hx += tw + px(8);

        float rx = max.x - px(8);
        if (pane_hovered) {
            // pane/main.js 44-56: no tooltips on these buttons.
            auto hbtn = [&](const char* bid, const char* glyph, const char* cmd) {
                const float b = px(20);  // 14 px icon, 3 px padding
                rx -= b;
                if (icon_button(s, bid, glyph, ImVec2(rx, cy - b * 0.5f), b, fpx(14), nullptr, g_pal->dim)) {
                    s.ws.focus(id);
                    run_command(s, cmd);
                }
                rx -= px(2);
            };
            const auto* tab = s.ws.active_tab();
            const bool zoomed = tab != nullptr && tab->zoomed == id;
            hbtn("##close", ui::icon::kClose, "pane.close");
            hbtn("##zoom", zoomed ? ui::icon::kUnzoom : ui::icon::kZoom, "pane.zoom");
            hbtn("##sd", ui::icon::kSplitDown, "pane.split:down");
            hbtn("##sr", ui::icon::kSplitRight, "pane.split:right");
            rx -= px(4);
        }
        // Status: spinner / "Needs you" pill / done dot.
        if (v.process_state == "Running" && att != Attention::kNone && att != Attention::kIdle) {
            const float mw = status_mark(s, dl, 0, cy, att, false, true);
            if (rx - mw > hx) {
                status_mark(s, dl, rx - mw, cy, att, false);
                rx -= mw + px(8);
            }
        }
        // Folder (~ for home), dim, the rest of the width.
        text_clipped(dl, s.fonts.ui, fpx(12), ImVec2(hx, cy - fpx(12) * 0.6f), g_pal->faint, display_path(v.cwd), rx - hx);
        // Grip (herdr pane_drag.rs): 28x18 in the middle of the header, two rows of three dots;
        // only with two or more panes and not zoomed. Dragging it moves the pane.
        {
            const auto* tab = s.ws.active_tab();
            const bool can_drag = tab != nullptr && tab->zoomed.empty() && s.ws.sessions_in(*tab).size() >= 2;
            const bool header_hov = ImGui::IsMouseHoveringRect(pos, ImVec2(max.x, pos.y + header_h), false);
            if (can_drag && (header_hov || s.drag_pane == id)) {
                const ImVec2 g(pos.x + r.w * 0.5f - px(14), cy - px(9));
                ImGui::SetCursorScreenPos(g);
                ImGui::InvisibleButton("##grip", ImVec2(px(28), px(18)));
                const bool gh = ImGui::IsItemHovered() || s.drag_pane == id;
                if (ImGui::IsItemActivated()) {
                    s.drag_pane = id;
                    s.drag_pane_from = ImGui::GetIO().MousePos;
                }
                if (gh)
                    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                dl->AddRectFilled(g, ImVec2(g.x + px(28), g.y + px(18)), alpha(g_pal->term_bg, 217), px(4));
                for (int row = 0; row < 2; ++row)
                    for (int col = 0; col < 3; ++col)
                        dl->AddCircleFilled(ImVec2(g.x + px(9) + static_cast<float>(col) * px(5), g.y + px(6.5f) + static_cast<float>(row) * px(5)),
                                            px(1), gh ? g_pal->text : g_pal->faint);
            }
        }
        if (ImGui::IsMouseHoveringRect(pos, ImVec2(max.x, pos.y + header_h)) && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
            !ImGui::IsAnyItemHovered() && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)) {
            s.ws.focus(id);
            vs.focus_next = true;
        }
    }

    float top = pos.y + edge + header_h;
    std::string banner;
    ImU32 bc = g_pal->working;
    if (!connected) {
        banner = "Disconnected from agent-host";
    } else if (v.lost) {
        banner = "Lost: owned by a previous agent-host; nothing was re-run";
    } else if (v.process_state == "Exited") {
        char b[96];
        std::snprintf(b, sizeof b, "Exited%s%s", v.exit_code ? " with code " : "",
                      v.exit_code ? std::to_string(*v.exit_code).c_str() : "");
        banner = b;
        bc = v.exit_code && *v.exit_code != 0 ? g_pal->needs_you : g_pal->dim;
    } else if (!v.is_controller && v.process_state == "Running") {
        banner = "Read-only: another window controls this session";
        bc = g_pal->info;
    }
    if (!banner.empty()) {
        dl->AddRectFilled(ImVec2(pos.x + px(10), top), ImVec2(max.x - px(10), top + px(30)), alpha(bc, 34),
                          px(7));
        dl->AddText(s.fonts.ui, fs, ImVec2(pos.x + px(20), top + (px(30) - fs) * 0.5f), bc, banner.c_str());
        float bx = max.x - px(20);
        auto bbtn = [&](const char* label, auto fn) {
            const float bw = ImGui::CalcTextSize(label, nullptr, true).x + px(16);
            bx -= bw;
            ImGui::SetCursorScreenPos(ImVec2(bx, top + px(3)));
            if (ImGui::Button(label, ImVec2(bw, px(24))))
                fn();
            bx -= px(6);
        };
        if (connected && (v.lost || v.process_state == "Exited")) {
            bbtn("Restart here", [&] {
                s.ws.focus(id);
                run_command(s, "session.restart");
            });
        } else if (connected && !v.lost && !v.is_controller) {
            bbtn("Take control", [&] { sit->second->take_control(); });
        }
        top += px(36);
    }

    // herdr main.json terminal_padding [14, 12]; the Terminals layout keeps the default [10, 6]
    // (settings.rs 564); Herdr 98 [4, 2] inside its 3 px border (win98.json 93-96).
    const ImVec2 body_min = w98p ? ImVec2(pos.x + edge + px(4), top + px(2))
                                 : ImVec2(pos.x + px(tm ? 10.0f : 14.0f), top + px(tm ? 6.0f : 2.0f));
    const ImVec2 body_size = w98p ? ImVec2(r.w - edge * 2 - px(8), max.y - edge - top - px(4))
                                  : ImVec2(r.w - px(tm ? 20.0f : 28.0f), max.y - top - (tm ? px(12) : px(2) + px(12)));
    if (body_size.x > px(20) && body_size.y > px(10)) {
        ImGui::SetCursorScreenPos(body_min);
        ImGui::BeginChild(
            "##body", body_size, ImGuiChildFlags_None,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoNav);
        vs.view.keymap = &s.keymap;
        vs.view.copy_on_select = s.settings.copy_on_select;
        vs.view.scroll_speed = s.settings.scroll_speed;
        vs.view.line_height = s.settings.line_height;
        vs.view.right_click_to_program = s.ws.right_click_program.contains(id);
        // herdr pane menu (menu/main.js 24-81): Split right, Split down, Zoom, Rename, where
        // right-clicks go, Send back to <parent>, Close pane.
        vs.view.menu_rows = [&s, id, parent = v.parent] {
            menu_keys();  // Up/Down/Enter/Esc (menu/main.js 131-137)
            auto pick = [&](const char* glyph, const char* label, const char* cmd) {
                if (!menu_row(s, glyph, label, cmd != nullptr ? s.keymap.chord_for(cmd) : std::string()))
                    return false;
                s.ws.focus(id);
                return true;
            };
            if (pick(ui::icon::kSplitRight, "Split right", "pane.split:right")) {
                run_command(s, "pane.split:right");
                return true;
            }
            if (pick(ui::icon::kSplitDown, "Split down", "pane.split:down")) {
                run_command(s, "pane.split:down");
                return true;
            }
            if (pick(ui::icon::kSquare, "Zoom", "pane.zoom")) {  // menu/main.js 84: square
                run_command(s, "pane.zoom");
                return true;
            }
            if (pick(ui::icon::kEdit, "Rename", "pane.rename")) {  // menu/main.js 84: square-pen
                run_command(s, "pane.rename");
                return true;
            }
            const bool to_program = s.ws.right_click_program.contains(id);
            if (pick(ui::icon::kTerminal, to_program ? "Use AgentDesk right-click menu" : "Send right-clicks to pane",
                     nullptr)) {
                run_command(s, to_program ? "terminal.right_click:menu" : "terminal.right_click:program");
                return true;
            }
            if (!parent.empty() && s.views_by_id.count(parent)) {
                const std::string label = "Send back to " + s.pane_title(parent);
                if (pick(ui::icon::kTerminal, label.c_str(), nullptr)) {  // menu/main.js 84: terminal
                    run_command(s, "pane.park");
                    return true;
                }
            }
            menu_separator();
            if (pick(ui::icon::kClose, "Close pane", "pane.close")) {
                run_command(s, "pane.close");
                return true;
            }
            return false;
        };
        vs.view.parent_name = !v.parent.empty() && s.views_by_id.count(v.parent) ? s.pane_title(v.parent) : std::string();
        vs.view.cell_width = s.settings.cell_width;
        vs.view.theme.default_bg = g_pal->term_bg;
        vs.view.theme.default_fg = g_pal->term_fg;
        vs.view.theme.cursor = g_pal->cursor;
        vs.view.theme.selection = g_pal->selection;
        vs.view.theme.ansi = g_pal->ansi;
        vs.view.theme.scrollbar = alpha(g_pal->dim, 120);
        const bool active = vs.view.draw("grid", *sit->second, s.fonts, vs.focus_next);
        vs.focus_next = false;
        if (active && s.ws.focused_session() != id)
            s.ws.focus(id);
        for (const auto& cmd : vs.view.take_commands()) {
            s.ws.focus(id);
            run_command(s, cmd);
        }
        ImGui::EndChild();
    }
    ImGui::PopID();
}

}  // namespace

void draw_panes(App::State& s, ImVec2 pos, ImVec2 size, bool connected) {
    s.pane_area = {pos.x, pos.y, size.x, size.y};
    ui::Tab* tab = s.ws.active_tab();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (tab == nullptr) {
        dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), g_pal->card, px(12));
        dl->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), g_pal->border, px(12));
        const float fs = ImGui::GetFontSize();
        const char* title = connected ? "Open a terminal" : "Connecting to agent-host...";
        const ImVec2 tsz = s.fonts.ui_bold->CalcTextSizeA(fs * 1.4f, FLT_MAX, 0, title);
        const float cx = pos.x + size.x * 0.5f;
        const float cy = pos.y + size.y * 0.4f;
        dl->AddText(s.fonts.ui_bold, fs * 1.4f, ImVec2(cx - tsz.x * 0.5f, cy - tsz.y), g_pal->text, title);
        const char* hint = "cd to your project, then run claude or codex. It keeps running after you close this window.";
        const ImVec2 hsz = ImGui::CalcTextSize(hint);
        dl->AddText(s.fonts.ui, fs, ImVec2(cx - hsz.x * 0.5f, cy + px(10)), g_pal->dim, hint);
        if (connected) {
            const std::string label = "New terminal   " + s.keymap.chord_for("tab.new");
            const std::string other = "Start a program...   " + s.keymap.chord_for("agent.new");
            const float bw = ImGui::CalcTextSize(label.c_str()).x + px(36);
            const float ow = ImGui::CalcTextSize(other.c_str()).x + px(36);
            const float total = bw + px(10) + ow;
            ImGui::SetCursorScreenPos(ImVec2(cx - total * 0.5f, cy + px(44)));
            ImGui::PushStyleColor(ImGuiCol_Button, v4(g_pal->focus));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, v4(alpha(g_pal->focus, 220)));
            ImGui::PushStyleColor(ImGuiCol_Text, v4(IM_COL32(255, 255, 255, 255)));
            if (ImGui::Button(label.c_str(), ImVec2(bw, px(36))))
                run_command(s, "tab.new");
            ImGui::PopStyleColor(3);
            ImGui::SameLine(0, px(10));
            if (ImGui::Button(other.c_str(), ImVec2(ow, px(36))))
                run_command(s, "agent.new");
        }
        return;
    }
    const ui::Rect area{pos.x, pos.y, size.x, size.y};
    const float gap = px(8);
    if (!tab->zoomed.empty()) {
        draw_pane(s, tab->zoomed, area, connected);
        return;
    }
    s.pane_rects = ui::layout_tree(tab->root, area, gap);
    for (const auto& [id, r] : s.pane_rects)
        draw_pane(s, id, r, connected);
    // Pane drag: after 4 px, the pane under the pointer is the target; its outer 25 % picks the
    // nearest edge, the middle swaps. Accent preview; release drops, Esc cancels.
    if (!s.drag_pane.empty()) {
        const ImVec2 m = ImGui::GetIO().MousePos;
        const float dx = m.x - s.drag_pane_from.x, dy = m.y - s.drag_pane_from.y;
        const bool moved = dx * dx + dy * dy >= px(4) * px(4);
        std::string target;
        std::optional<Dir> edge;
        ui::Rect pr{};
        for (const auto& [pid, r] : s.pane_rects) {
            if (pid == s.drag_pane || m.x < r.x || m.x >= r.x + r.w || m.y < r.y || m.y >= r.y + r.h)
                continue;
            target = pid;
            const float fx = (m.x - r.x) / r.w, fy = (m.y - r.y) / r.h;
            const float d = std::min({fx, 1 - fx, fy, 1 - fy});
            pr = r;
            if (d < 0.25f) {
                if (d == fx) {
                    edge = Dir::kLeft;
                    pr.w *= 0.5f;
                } else if (d == 1 - fx) {
                    edge = Dir::kRight;
                    pr.x += r.w * 0.5f;
                    pr.w *= 0.5f;
                } else if (d == fy) {
                    edge = Dir::kUp;
                    pr.h *= 0.5f;
                } else {
                    edge = Dir::kDown;
                    pr.y += r.h * 0.5f;
                    pr.h *= 0.5f;
                }
            }
        }
        if (moved && !target.empty()) {
            ImDrawList* fg = ImGui::GetForegroundDrawList(ImGui::GetMainViewport());
            fg->AddRectFilled(ImVec2(pr.x, pr.y), ImVec2(pr.x + pr.w, pr.y + pr.h), alpha(g_pal->focus, 46), px(10));
            fg->AddRect(ImVec2(pr.x, pr.y), ImVec2(pr.x + pr.w, pr.y + pr.h), g_pal->focus, px(10), 0, 2.0f);
        }
        if (moved)
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            s.drag_pane.clear();
        } else if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            if (moved && !target.empty() && s.ws.move_pane(s.drag_pane, target, edge))
                s.focus_view(s.drag_pane);
            s.drag_pane.clear();
        }
    }
    // Draggable borders between panes.
    int n = 0;
    for (const auto& d : ui::dividers(tab->root, area, gap)) {
        ImGui::PushID(n++);
        ImGui::SetCursorScreenPos(ImVec2(d.bar.x, d.bar.y));
        ImGui::InvisibleButton("##div", ImVec2(std::max(d.bar.w, 2.0f), std::max(d.bar.h, 2.0f)));
        if (ImGui::IsItemHovered() || ImGui::IsItemActive())
            ImGui::SetMouseCursor(d.vertical ? ImGuiMouseCursor_ResizeNS : ImGuiMouseCursor_ResizeEW);
        if (ImGui::IsItemActive()) {
            const ImVec2 m = ImGui::GetIO().MousePos;
            ui::drag_divider(tab->root, d, d.vertical ? m.y - gap * 0.5f : m.x - gap * 0.5f, gap);
            dl->AddRectFilled(ImVec2(d.bar.x, d.bar.y), ImVec2(d.bar.x + d.bar.w, d.bar.y + d.bar.h),
                              alpha(g_pal->focus, 120), px(3));
        }
        ImGui::PopID();
    }
}

// Folder the panels follow: the focused pane's live folder (herdr files.js 6-12), on the machine
// that pane runs on ("" = this computer).
std::string panel_folder(App::State& s, std::string* machine) {
    const auto id = s.ws.focused_session();
    if (const auto it = s.views_by_id.find(id); it != s.views_by_id.end()) {
        if (machine != nullptr)
            *machine = s.machine_of[id];
        return it->second.cwd;
    }
    return {};
}

ImU32 mark_color(char m) {
    return m == 'A' ? g_pal->done : (m == 'D' || m == 'U') ? g_pal->needs_you : g_pal->working;
}

void panel_header(App::State& s, ImDrawList* dl, ImVec2 a, float w, const char* title, const std::string& right,
                  bool back, bool* back_clicked) {
    const float h = px(42);
    const float cy = a.y + h * 0.5f;
    float x = a.x + px(14);
    if (back) {
        ImGui::SetCursorScreenPos(ImVec2(x - px(4), cy - px(11)));
        if (ImGui::InvisibleButton("##back", ImVec2(px(20), px(22))) && back_clicked != nullptr)
            *back_clicked = true;
        draw_icon(s, dl, ui::icon::kArrowLeft, fpx(14), ImVec2(x + px(6), cy), ImGui::IsItemHovered() ? g_pal->text : g_pal->dim);
        x += px(20) + px(4);
    }
    const ImVec2 t = s.fonts.ui_bold->CalcTextSizeA(fpx(13), FLT_MAX, 0, title);
    dl->AddText(s.fonts.ui_bold, fpx(13), ImVec2(x, cy - t.y * 0.5f), g_pal->text, title);
    x += t.x + px(8);
    if (!right.empty()) {
        const float rw = std::min(s.fonts.ui->CalcTextSizeA(fpx(12), FLT_MAX, 0, right.c_str()).x, a.x + w - px(14) - x);
        text_clipped(dl, s.fonts.ui, fpx(12), ImVec2(a.x + w - px(14) - rw, cy - fpx(12) * 0.6f), g_pal->faint, right, rw);
    }
    dl->AddLine(ImVec2(a.x, a.y + h), ImVec2(a.x + w, a.y + h), g_pal->border);
}
}  // namespace detail

}  // namespace agentdesk::desktop
