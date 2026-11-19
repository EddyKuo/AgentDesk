// The tab bar (with the New menu and window buttons) and the status bar.
#include "app_internal.hpp"

namespace agentdesk::desktop {
namespace detail {

namespace {
// ---------------------------------------------------------------- tab bar (tabs.js)
// herdr new-menu (defaults/mods/new-menu/main.js): Terminal, "Start an agent" (installed ones
// numbered 1-9, then up to two not installed), New space, New worktree…, Open worktree…. Rows
// 31 px, hover "selected"; Up/Down/Enter/Esc like the context menus (menu/main.js 131-137).
void draw_new_menu(App::State& s) {
    ImGui::SetNextWindowSize(ImVec2(px(250), 0));
    if (!ImGui::BeginPopup("##newmenu"))
        return;
    menu_keys();
    auto row = [&](const char* glyph, const std::string& label, const std::string& hint, const std::string& cmd,
                   bool enabled = true, const ui::Program* prog = nullptr) {
        const ImVec2 a = ImGui::GetCursorScreenPos();
        const float w = ImGui::GetContentRegionAvail().x;
        const float h = px(31) - ImGui::GetStyle().ItemSpacing.y;
        ImGui::BeginDisabled(!enabled);
        bool clicked = ImGui::InvisibleButton(("##" + label).c_str(), ImVec2(w, h));
        ImGui::EndDisabled();
        const bool hov = enabled && ImGui::IsItemHovered();
        bool run = false;
        const bool on = enabled && g_menu_nav.row(false, &run);
        clicked = enabled && (clicked || run);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (hov || on)
            dl->AddRectFilled(a, ImVec2(a.x + w, a.y + h), g_pal->selected, px(7));
        const float cy = a.y + h * 0.5f;
        const ImU32 col = enabled ? g_pal->text : g_pal->faint;
        if (prog != nullptr)
            program_icon(dl, *prog, ImVec2(a.x + px(9), cy - px(7)), px(14), col);  // new-menu/main.js 65
        else if (glyph != nullptr)
            draw_icon(s, dl, glyph, fpx(14), ImVec2(a.x + px(9) + px(7), cy), col);
        dl->AddText(s.fonts.ui, fpx(12.8f), ImVec2(a.x + px(9) + px(14) + px(10), cy - fpx(12.8f) * 0.6f), col,
                    label.c_str());
        if (!hint.empty()) {
            const float hw = s.fonts.ui->CalcTextSizeA(fpx(11), FLT_MAX, 0, hint.c_str()).x;
            dl->AddText(s.fonts.ui, fpx(11), ImVec2(a.x + w - hw - px(9), cy - fpx(11) * 0.6f), g_pal->faint,
                        hint.c_str());
        }
        if (clicked) {
            ImGui::CloseCurrentPopup();
            run_command(s, cmd);
        }
    };
    row(ui::icon::kTerminal, "Terminal", s.keymap.chord_for("tab.new"), "tab.new");
    // The agents of the active space's machine (herdr knownAgents({on: machine})).
    const std::string machine = s.ws.active() != nullptr ? s.ws.active()->machine : std::string();
    ui::HostClient* mh = s.host_for(machine);
    const auto profiles = mh->profiles();
    // The other shells this machine has (PowerShell 7, Windows PowerShell, cmd, WSL): a tab in
    // the focused folder, like Terminal (AgentDesk's own; herdr has the one shell).
    for (const auto& [id, name] : shell_profiles(s, machine))
        if (id != pick_shell(s.settings.shell_profile, profiles))
            row(ui::icon::kTerminal, name, "", "launch:tab:" + id);
    ImGui::Dummy(ImVec2(0, px(2)));
    ImGui::PushFont(s.fonts.ui_medium, 11);
    ImGui::TextDisabled("Start an agent");
    ImGui::PopFont();
    // Installed agents (profiles that are not shells), numbered 1-9; then up to two known
    // agents that are not installed, disabled. A digit with Ctrl, Alt or Win held is not ours
    // (new-menu/main.js 56).
    const ImGuiIO& io = ImGui::GetIO();
    const bool plain = !io.KeyCtrl && !io.KeyAlt && !io.KeySuper;
    int n = 0;
    std::string have;
    for (const auto& [id, name] : profiles) {
        if (is_shell_profile(id))
            continue;
        for (const char ch : id + " " + name + " ")
            have += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        ++n;
        const std::string key = n <= 9 ? std::to_string(n) : std::string();
        const ui::Program prog = program_of_profile(mh->profile_executable(id), id);
        row(nullptr, name, key, "launch:tab:" + id, true, &prog);
        if (n <= 9 && plain && ImGui::IsKeyPressed(static_cast<ImGuiKey>(ImGuiKey_1 + n - 1), false)) {
            ImGui::CloseCurrentPopup();
            run_command(s, "launch:tab:" + id);
        }
    }
    if (profiles.empty()) {
        ImGui::PushFont(s.fonts.ui, 12);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + px(9));
        ImGui::TextDisabled("%s", mh->connected() ? "Checking installed agents\xE2\x80\xA6"
                                                  : "Could not check installed agents");
        ImGui::PopFont();
    } else {
        static constexpr std::array<std::pair<const char*, const char*>, 4> kKnown = {
            {{"claude", "Claude Code"}, {"codex", "Codex"}, {"gemini", "Gemini CLI"}, {"opencode", "OpenCode"}}};
        int missing = 0;
        for (const auto& [key, label] : kKnown) {
            if (missing >= 2 || have.find(key) != std::string::npos)
                continue;
            ++missing;
            const ui::Program prog = program_of_profile({}, key);
            row(nullptr, label, "not installed", "", false, &prog);
        }
    }
    ImGui::Separator();
    // new-menu/main.js 69-71: worktrees need an open space (no hint on these rows). New space has
    // a "+"; both worktree rows have branch.svg.
    const bool space = s.ws.active() != nullptr;
    row(ui::icon::kPlus, "New space", s.keymap.chord_for("space.new"), "space.new");
    row(ui::icon::kBranch, "New worktree\xE2\x80\xA6", "", "worktree.new", space);
    row(ui::icon::kBranch, "Open worktree\xE2\x80\xA6", "", "worktree.open", space);
    ImGui::EndPopup();
}

void draw_window_buttons(App::State& s, float right, float top, float h) {
    if (s.config.hwnd == nullptr)
        return;
    const float b = px(26);
    float x = right - b * 3 - px(4);
    const float y = top + (h - b) * 0.5f;
    const bool zoomed = ::IsZoomed(s.config.hwnd) != FALSE;
    if (icon_button(s, "##win-min", ui::icon::kMinimize, ImVec2(x, y), b, fpx(16), nullptr, g_pal->dim))
        ::ShowWindow(s.config.hwnd, SW_MINIMIZE);
    x += b + px(2);
    if (icon_button(s, "##win-max", zoomed ? ui::icon::kRestore : ui::icon::kMaximize, ImVec2(x, y), b, fpx(16),
                    nullptr, g_pal->dim))
        ::ShowWindow(s.config.hwnd, zoomed ? SW_RESTORE : SW_MAXIMIZE);
    x += b + px(2);
    ImGui::SetCursorScreenPos(ImVec2(x, y));
    const bool close = ImGui::InvisibleButton("##win-close", ImVec2(b, b));
    const bool hov = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (hov)
        dl->AddRectFilled(ImVec2(x, y), ImVec2(x + b, y + b), g_pal->hover, px(6));
    draw_icon(s, dl, ui::icon::kWinClose, fpx(16), ImVec2(x + b * 0.5f, y + b * 0.5f), hov ? g_pal->text : g_pal->dim);
    if (close)
        ::PostMessageW(s.config.hwnd, WM_CLOSE, 0, 0);
}

}  // namespace

// Width of the tab-bar items right of the tabs (+, Files, Changes, window buttons).
void draw_tab_bar(App::State& s, ImVec2 pos, float width, float height) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height), g_pal->sidebar);
    dl->AddLine(ImVec2(pos.x, pos.y + height - 1), ImVec2(pos.x + width, pos.y + height - 1), g_pal->border);
    const float th = px(29);
    const float ty = pos.y + (height - th) * 0.5f;
    // Right part first, so the tabs know how much room is left.
    float right = pos.x + width - px(10);
    if (s.config.hwnd != nullptr) {
        draw_window_buttons(s, right + px(10) - px(6), pos.y, height);
        right -= px(26) * 3 + px(4) + px(12);
    }
    auto* sp = s.ws.active();
    auto panel_button = [&](const char* id, const char* glyph, const char* label, const char* which) {
        const float tw = s.fonts.ui->CalcTextSizeA(fpx(12), FLT_MAX, 0, label).x;
        const float bw = px(8) + px(14) + px(5) + tw + px(8);
        right -= bw;
        const ImVec2 a(right, pos.y + (height - px(26)) * 0.5f);
        ImGui::SetCursorScreenPos(a);
        const bool clicked = ImGui::InvisibleButton(id, ImVec2(bw, px(26)));
        const bool hov = ImGui::IsItemHovered();
        const bool on = sp != nullptr && sp->panel == which;
        if (on || hov)
            dl->AddRectFilled(a, ImVec2(a.x + bw, a.y + px(26)), on ? g_pal->selected : g_pal->hover, px(6));
        const ImU32 col = on || hov ? g_pal->text : g_pal->dim;
        draw_icon(s, dl, glyph, fpx(14), ImVec2(a.x + px(8) + px(7), a.y + px(13)), col);
        dl->AddText(s.fonts.ui, fpx(12), ImVec2(a.x + px(8) + px(14) + px(5), a.y + px(13) - fpx(12) * 0.6f), col, label);
        if (clicked && sp != nullptr) {
            sp->panel = on ? std::string() : std::string(which);
            s.doc_dirty = true;
            s.doc_dirty_at = Clock::now();
        }
        right -= px(3);
    };
    auto wants = [&](const char* p) {
        return std::find(s.settings.panels.begin(), s.settings.panels.end(), p) != s.settings.panels.end();
    };
    if (wants("changes"))
        panel_button("##p-changes", ui::icon::kChanges, "Changes", "changes");
    if (wants("files"))
        panel_button("##p-files", ui::icon::kFile, "Files", "files");

    // tabs/main.js 147-150: the tab strip is flex_1, so "+" sits at its right, next to Files.
    const float plus_x = right - px(26);
    float x = pos.x + px(10);
    const float tabs_right = plus_x - px(3);
    std::string close_tab;  // id of the tab to close after drawing
    if (sp != nullptr) {
        // Keep the active tab in view: start from an offset that shows it.
        struct T {
            int i;
            float w;
            std::string label;
        };
        std::vector<T> items;
        for (int i = 0; i < static_cast<int>(sp->tabs.size()); ++i) {
            // tabs/main.js 93: every tab of the space, settled ones too (herdr's bar does not
            // mark them; tab.settle only takes them out of tab.next/prev and the lists).
            auto& tab = sp->tabs[static_cast<std::size_t>(i)];
            const auto ids = s.ws.sessions_in(tab);
            // herdr: an unnamed tab follows its agent's live title; a shell shows its program.
            std::string label = tab.name;
            if (label.empty() && !ids.empty()) {
                const std::string shown_id = lead_pane(s, ids);
                if (const auto vi = s.views_by_id.find(shown_id); vi != s.views_by_id.end() && is_agent(vi->second))
                    label = agent_detail(s, shown_id, vi->second);
                if (label.empty())
                    label = s.pane_title(shown_id);
            }
            if (label.empty())
                label = std::to_string(i + 1);
            if (!tab.zoomed.empty())
                label += " Z";
            if (const auto deco = s.mods->decorate("tab", {{"title", label}, {"sessions", ids}}); !deco.empty())
                label += "  \xC2\xB7 " + deco;
            const float lw = std::min(s.fonts.ui->CalcTextSizeA(fpx(12.8f), FLT_MAX, 0, label.c_str()).x, px(170));
            float w = px(10) + px(14) + px(7) + lw + px(10);
            std::vector<Attention> atts;
            for (const auto& id : ids)
                atts.push_back(s.attention_of(id));
            const Attention att = ui::most_urgent(atts);
            if (att == Attention::kWorking || att == Attention::kBlocked || att == Attention::kDone)
                w += px(7) + px(12);
            if (i == sp->active_tab)
                w += px(7) + px(12);
            items.push_back({i, w, label});
        }
        float total = 0;
        for (const auto& it : items)
            total += it.w + px(3);
        float visible = tabs_right - x;
        float active_end = 0, active_start = 0, acc = 0;
        for (const auto& it : items) {
            if (it.i == sp->active_tab) {
                active_start = acc;
                active_end = acc + it.w;
            }
            acc += it.w + px(3);
        }
        s.tab_scroll = std::clamp(s.tab_scroll, 0.0f, std::max(0.0f, total - visible));
        if (active_end - s.tab_scroll > visible)
            s.tab_scroll = active_end - visible;
        if (active_start < s.tab_scroll)
            s.tab_scroll = active_start;
        // ImGui::PushClipRect clips hit-testing too: a tab running under "+" must not take its clicks.
        ImGui::PushClipRect(ImVec2(x, pos.y), ImVec2(tabs_right, pos.y + height), true);
        float tx = x - s.tab_scroll;
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        int drop = -1;
        for (const auto& it : items) {
            auto& tab = sp->tabs[static_cast<std::size_t>(it.i)];
            const bool active = it.i == sp->active_tab;
            const ImVec2 a(tx, ty);
            const ImVec2 b(tx + it.w, ty + th);
            tx += it.w + px(3);
            if (b.x < x || a.x > tabs_right)
                continue;
            ImGui::PushID(tab.id.c_str());
            ImGui::SetCursorScreenPos(a);
            // The close button is drawn on top of the tab (herdr tabs/main.js): let it take the
            // mouse, or the tab's own button keeps every press and ✕ never fires.
            ImGui::SetNextItemAllowOverlap();
            ImGui::InvisibleButton("##tab", ImVec2(it.w, th));
            const bool hov = ImGui::IsItemHovered();
            if (ImGui::IsItemActivated())
                s.drag_tab = it.i;
            if (s.drag_tab >= 0 && s.drag_tab != it.i && ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
                mouse.x >= a.x && mouse.x < b.x && mouse.y >= a.y && mouse.y < b.y)
                drop = it.i;
            if (hov && ImGui::IsMouseReleased(ImGuiMouseButton_Left) && s.drag_tab == it.i) {
                s.ws.select_tab(it.i);
                s.focus_view(s.ws.focused_session());
            }
            if (hov && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                run_command(s, "tab.rename:" + tab.id);  // tabs/main.js 113
            // menu/main.js 58-63: Rename, New tab, Close, all by the tab's id.
            if (begin_menu("##tabctx")) {
                if (menu_row(s, ui::icon::kEdit, "Rename", s.keymap.chord_for("tab.rename")))
                    run_command(s, "tab.rename:" + tab.id);
                if (menu_row(s, ui::icon::kPlus, "New tab", s.keymap.chord_for("tab.new")))
                    run_command(s, "tab.new");
                menu_separator();
                if (menu_row(s, ui::icon::kClose, "Close", s.keymap.chord_for("tab.close")))
                    close_tab = tab.id;
                ImGui::EndPopup();
            }
            if (active) {
                dl->AddRectFilled(a, b, raised_color(), px(7));
                dl->AddRect(a, b, g_pal->border, px(7));
            } else if (hov) {
                dl->AddRectFilled(a, b, g_pal->hover, px(7));
            }
            const float cy = a.y + th * 0.5f;
            float cx = a.x + px(10);
            const auto ids = s.ws.sessions_in(tab);
            const std::string shown = ids.empty() ? std::string() : lead_pane(s, ids);
            if (s.views_by_id.count(shown)) {
                const auto& v = s.views_by_id[shown];
                program_icon(dl, program_of(v), ImVec2(cx, cy - px(7)), px(14), g_pal->dim);  // tabs/main.js 120
            }
            cx += px(14) + px(7);
            const ImU32 col = active ? g_pal->text : g_pal->dim;
            const float lw = text_clipped(dl, s.fonts.ui, fpx(12.8f), ImVec2(cx, cy - fpx(12.8f) * 0.6f), col, it.label, px(170));
            cx += lw + px(7);
            std::vector<Attention> atts;
            for (const auto& id : ids)
                atts.push_back(s.attention_of(id));
            const Attention att = ui::most_urgent(atts);
            if (att == Attention::kWorking) {
                status_mark(s, dl, cx - px(1), cy, att, true);
                cx += px(12) + px(7);
            } else if (att == Attention::kBlocked || att == Attention::kDone) {
                dl->AddCircleFilled(ImVec2(cx + px(3), cy), px(3), att == Attention::kBlocked ? g_pal->needs_you : g_pal->done);
                cx += px(12) + px(7);
            }
            if (active) {
                ImGui::SetCursorScreenPos(ImVec2(cx - px(3), cy - px(9)));
                if (ImGui::InvisibleButton("##x", ImVec2(px(18), px(18))))
                    close_tab = tab.id;
                const bool xh = ImGui::IsItemHovered();
                if (xh)
                    dl->AddRectFilled(ImVec2(cx - px(3), cy - px(9)), ImVec2(cx + px(15), cy + px(9)), g_pal->hover, px(4));
                draw_icon(s, dl, ui::icon::kClose, fpx(12), ImVec2(cx + px(6), cy), xh ? g_pal->text : g_pal->dim);
            }
            ImGui::PopID();
        }
        ImGui::PopClipRect();
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            if (s.drag_tab >= 0 && drop >= 0)
                s.ws.move_tab(s.ws.active_space, s.drag_tab, drop);
            s.drag_tab = -1;
        }
        // The wheel over the bar switches tabs (herdr tabs.js wheel handling).
        const float wheel = ImGui::GetIO().MouseWheel;
        if (ImGui::IsMouseHoveringRect(ImVec2(x, pos.y), ImVec2(tabs_right, pos.y + height)) && wheel != 0 &&
            !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) {
            const auto now = Clock::now();
            auto& w = s.tab_wheel;
            int step = 0;
            if (std::abs(wheel) >= 1.0f) {  // a mouse notch: one tab
                step = wheel < 0 ? 1 : -1;
                w = {};
            } else {
                const float px_delta = -wheel * 120.0f;
                if (now - w.last > std::chrono::milliseconds(250) || (w.acc != 0 && (w.acc > 0) != (px_delta > 0)))
                    w = {};
                w.acc += px_delta;
                w.last = now;
                const float need = w.steps == 0 ? 20.0f : 60.0f;
                if (std::abs(w.acc) >= need && now - w.switched >= std::chrono::milliseconds(100)) {
                    step = w.acc > 0 ? 1 : -1;
                    w.acc -= static_cast<float>(step) * need;
                    ++w.steps;
                    w.switched = now;
                }
            }
            if (step != 0) {
                s.ws.cycle_tab(step);
                s.focus_view(s.ws.focused_session());
            }
        }
    }
    // "+" -> new-menu (tabs/main.js 150), right of the flex_1 tab strip; no tooltip there.
    if (icon_button(s, "##tab-new", ui::icon::kPlus, ImVec2(plus_x, pos.y + (height - px(26)) * 0.5f), px(26), fpx(14),
                    nullptr, g_pal->dim))
        ImGui::OpenPopup("##newmenu");
    draw_new_menu(s);
    if (!close_tab.empty())
        run_command(s, "tab.close:" + close_tab);  // by id: the shown tab stays (menu/main.js 62)
    window_drag_area(s, pos, ImVec2(pos.x + width, pos.y + height));
}

// ---------------------------------------------------------------- status bar (workspace-status)
void draw_status_bar(App::State& s, ImVec2 pos, float width, float height) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height), g_pal->sidebar);
    dl->AddLine(pos, ImVec2(pos.x + width, pos.y), g_pal->border);
    const float fs = fpx(11.8f);
    const float cy = pos.y + height * 0.5f;
    float x = pos.x + px(14);
    auto text = [&](const std::string& t, ImU32 col) {
        dl->AddText(s.fonts.ui, fs, ImVec2(x, cy - fs * 0.6f), col, t.c_str());
        x += s.fonts.ui->CalcTextSizeA(fs, FLT_MAX, 0, t.c_str()).x;
    };
    const auto* sp = s.ws.active();
    const std::string machine = sp != nullptr ? sp->machine : std::string();
    const bool connected = s.machine_connected(machine);
    draw_icon(s, dl, machine.empty() ? ui::icon::kMonitor : ui::icon::kServer, fpx(13), ImVec2(x + px(6.5f), cy),
              connected ? g_pal->dim : g_pal->needs_you);
    x += px(13) + px(6);
    if (!s.host->connected()) {
        text("agent-host disconnected: " + s.host->last_error(), g_pal->needs_you);
        ImGui::SetCursorScreenPos(ImVec2(x + px(8), pos.y + px(3)));
        if (ImGui::SmallButton("Start host"))
            s.host->request_start_host();
        x += px(90);
    } else {
        text(machine.empty() ? std::string("This computer") : machine, g_pal->dim);
    }
    x += px(16);
    const std::string fid = s.ws.focused_session();
    if (s.views_by_id.count(fid)) {
        const auto g = s.git.get(s.views_by_id[fid].cwd, machine);
        if (g.repo && !g.branch.empty()) {
            draw_icon(s, dl, ui::icon::kBranch, fpx(13), ImVec2(x + px(6.5f), cy), g_pal->dim);
            x += px(13) + px(6);
            text(g.branch == "HEAD" ? "(detached)" : g.branch, g_pal->dim);
            x += px(16);
        }
    }
    int agents = 0, need = 0;
    for (const auto& [id, v] : s.views_by_id) {
        if (!live_agent(s, id, v))  // the same agents the sidebar lists
            continue;
        ++agents;
        need += s.attention_of(id) == Attention::kBlocked ? 1 : 0;
    }
    dl->AddCircleFilled(ImVec2(x + px(3), cy), px(3), g_pal->faint);
    x += px(12);
    text(std::to_string(agents) + (agents == 1 ? " agent" : " agents"), g_pal->dim);
    if (need > 0)
        text(" \xC2\xB7 " + std::to_string(need) + (need == 1 ? " needs you" : " need you"), g_pal->needs_you);
    if (s.host->host_dies_with_launcher()) {
        x += px(16);
        text("host ends when the launching terminal closes", g_pal->working);
    }
    // Right: the palette hint (click opens the palette).
    // workspace-status/main.js 8-15: without a binding the hint is just "commands".
    const std::string chord = s.keymap.chord_for("app.palette");
    const std::string hint = chord.empty() ? std::string("commands") : chord + "  commands";
    const float hw = s.fonts.ui->CalcTextSizeA(fs, FLT_MAX, 0, hint.c_str()).x;
    const ImVec2 ha(pos.x + width - px(14) - hw, pos.y);
    if (ha.x > x + px(16)) {
        ImGui::SetCursorScreenPos(ha);
        if (ImGui::InvisibleButton("##palette-hint", ImVec2(hw, height)))
            run_command(s, "app.palette");
        dl->AddText(s.fonts.ui, fs, ImVec2(ha.x, cy - fs * 0.6f), ImGui::IsItemHovered() ? g_pal->text : g_pal->faint,
                    hint.c_str());
    }
}
}  // namespace detail

}  // namespace agentdesk::desktop
