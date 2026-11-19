// The worktree dialogs: new worktree and open worktree.
#include "app_internal.hpp"

namespace agentdesk::desktop {
namespace detail {

namespace {
// ------------------------------------------------------------------ worktrees (herdr mods/worktree)
// herdr slug: runs of characters other than letters, digits and "._-" become "-", trimmed.
std::string branch_slug(const std::string& b) {
    return host::worktree_slug(b);  // herdr worktree/main.js slug(), shared with the host's default
}

}  // namespace

void draw_worktree(App::State& s) {
    if (s.show_worktree) {
        ImGui::OpenPopup("##worktree");
        s.show_worktree = false;
    }
    center_next(px(460), 0.2f);
    push_overlay_style();
    const bool open = ImGui::BeginPopupModal("##worktree", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize);
    pop_overlay_style();
    if (!open)
        return;
    const std::string wt_machine = s.ws.active() != nullptr ? s.ws.active()->machine : std::string();
    const GitInfo g = s.git.get(s.wt_repo, wt_machine);
    const std::string repo = folder_name(g.common_top.empty() ? g.top : g.common_top);
    // Another machine's home is its own: its agent-host picks %USERPROFILE%\.agentdesk\worktrees
    // there unless a folder is set.
    std::string root = s.settings.worktree_dir;
    if (root.empty() && wt_machine.empty()) {
        wchar_t home[MAX_PATH]{};
        ::GetEnvironmentVariableW(L"USERPROFILE", home, MAX_PATH);
        root = win::to_utf8(home) + "\\.agentdesk\\worktrees";
    }
    const std::string shown_root = root.empty() ? std::string("~\\.agentdesk\\worktrees") : root;
    bool cancel = false;
    worktree_title(s, g.repo ? "New worktree of " + repo : std::string("New worktree"));  // worktree/main.js 349
    ImGui::PushStyleColor(ImGuiCol_Text, v4(g_pal->dim));
    if (!g.repo)
        ImGui::TextWrapped("%s is not in a git repository.", display_path(s.wt_repo).c_str());
    else
        ImGui::TextWrapped("A new space on a checkout in %s\\%s\\. A branch that exists is checked out; a new one "
                           "starts from here.",
                           display_path(shown_root).c_str(), repo.c_str());
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0, px(6)));
    ui::HostClient* wthost = s.host_for(wt_machine);
    bool create = false;
    if (g.repo) {
        if (ImGui::IsWindowAppearing())
            ImGui::SetKeyboardFocusHere();
        create = worktree_input(s, "##branch", "branch name", s.wt_branch, sizeof s.wt_branch,
                                ImGuiInputTextFlags_EnterReturnsTrue);
    }
    bool busy = false;  // wt_busy and wt_error are written by the worktree.add answer (a HostClient thread)
    {
        std::lock_guard lock(s.placements_mutex);
        busy = s.wt_busy;
        if (!s.wt_error.empty() && s.wt_error != "done") {
            ImGui::PushStyleColor(ImGuiCol_Text, v4(g_pal->needs_you));
            ImGui::TextWrapped("%s", s.wt_error.c_str());
            ImGui::PopStyleColor();
        }
    }
    ImGui::Dummy(ImVec2(0, px(4)));
    ImGui::TextDisabled("%s", busy ? "working\xE2\x80\xA6" : "\xE2\x86\xB5 create \xC2\xB7 esc cancel");
    cancel = cancel || ImGui::IsKeyPressed(ImGuiKey_Escape) || dialog_outside_click();
    if (create && !busy) {
        const std::string branch = trimmed(s.wt_branch);  // herdr create(): this.text.trim()
        if (branch.empty()) {
            std::lock_guard lock(s.placements_mutex);
            s.wt_error = "Type a branch name.";
        } else if (wthost->connected()) {
            {
                std::lock_guard lock(s.placements_mutex);
                s.wt_busy = true;
                s.wt_error.clear();
            }
            Json params{{"repo", g.top}, {"branch", branch}};
            if (!root.empty())
                params["path"] = root + "\\" + repo + "\\" + branch_slug(branch);
            const std::string shell = pick_shell(s.settings.shell_profile, wthost->profiles());
            wthost->call(
                "worktree.add", params,
                [st = &s, shell, wthost, wt_machine](Json result, std::string error) {
                    if (!error.empty()) {
                        std::lock_guard lock(st->placements_mutex);
                        st->wt_error = error;
                        st->wt_busy = false;
                        return;
                    }
                    Json p{{"profile_id", shell}, {"cwd", safe::string_or(result, "path")}};
                    wthost->call(
                        "session.create", p,
                        [st, wt_machine](Json r, std::string e) {
                            std::lock_guard lock(st->placements_mutex);
                            st->wt_busy = false;
                            if (!e.empty()) {
                                st->wt_error = e;
                                return;
                            }
                            Placement pl;  // the space stays unnamed: it follows the checkout
                            pl.session = safe::string_or(r, "session_id");
                            pl.target = LaunchTarget::kSpace;
                            pl.machine = wt_machine;
                            st->placements.push_back(pl);
                            st->wt_error = "done";
                        },
                        std::chrono::seconds(15));
                },
                std::chrono::seconds(600));
        }
    }
    {
        std::lock_guard lock(s.placements_mutex);
        if (s.wt_error == "done") {
            s.wt_error.clear();
            cancel = true;
        }
    }
    if (cancel)
        ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

// "Open worktree of <repo>": main checkout first, then by last commit; filter; Up/Down wrap.
void draw_worktree_open(App::State& s) {
    if (s.show_worktree_open) {
        ImGui::OpenPopup("##wtopen");
        s.show_worktree_open = false;
        s.wt_filter[0] = 0;
        s.wt_sel = 0;
    }
    center_next(px(560), 0.12f);
    push_overlay_style();
    const bool open = ImGui::BeginPopupModal("##wtopen", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize);
    pop_overlay_style();
    if (!open)
        return;
    const GitInfo g = s.git.get(s.focused_cwd(), s.worktree_machine);
    const std::string repo = folder_name(g.common_top.empty() ? g.top : g.common_top);
    const bool no_repo = !s.worktree_not_repo.empty();
    std::string f = s.wt_filter;
    for (auto& c : f)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    std::vector<const WorktreeEntry*> shown;
    for (const auto& e : s.worktree_list) {
        std::string hay = e.branch + " " + e.path;
        for (auto& c : hay)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (f.empty() || hay.find(f) != std::string::npos)
            shown.push_back(&e);
    }
    char count[64];
    if (shown.size() == s.worktree_list.size())
        std::snprintf(count, sizeof count, "%zu checkouts", s.worktree_list.size());
    else
        std::snprintf(count, sizeof count, "%zu/%zu checkouts", shown.size(), s.worktree_list.size());
    // worktree/main.js 301: the title, and "N checkouts" at its right (11 px dim).
    bool close = false;
    worktree_title(s, no_repo ? std::string("Open worktree") : "Open worktree of " + repo,
                   no_repo ? std::string() : std::string(count));
    if (no_repo) {
        ImGui::PushStyleColor(ImGuiCol_Text, v4(g_pal->dim));
        ImGui::TextWrapped("%s is not in a git repository.", s.worktree_not_repo.c_str());
        ImGui::PopStyleColor();
        ImGui::TextDisabled("\xE2\x86\x91\xE2\x86\x93 choose \xC2\xB7 \xE2\x86\xB5 open \xC2\xB7 esc close");
        if (close || ImGui::IsKeyPressed(ImGuiKey_Escape))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    if (ImGui::IsWindowAppearing())
        ImGui::SetKeyboardFocusHere();
    if (worktree_input(s, "##wf", "filter worktrees", s.wt_filter, sizeof s.wt_filter))
        s.wt_sel = 0;
    const int n = static_cast<int>(shown.size());
    if (n > 0) {
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))
            s.wt_sel = (s.wt_sel + 1) % n;
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))
            s.wt_sel = (s.wt_sel + n - 1) % n;
        s.wt_sel = std::clamp(s.wt_sel, 0, n - 1);
    }
    std::string go;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::BeginChild("##wtl", ImVec2(0, std::min(px(330), static_cast<float>(std::max(n, 1)) * px(44) + px(4))),
                      ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
    ImDrawList* d = ImGui::GetWindowDrawList();
    if (n == 0) {  // herdr: p 10, dim
        ImGui::SetCursorPos(ImVec2(px(10), px(10)));
        ImGui::TextDisabled("No matching worktrees.");
    }
    const auto cwds = s.session_cwds();
    for (int i = 0; i < n; ++i) {
        const auto& e = *shown[static_cast<std::size_t>(i)];
        const ImVec2 a = ImGui::GetCursorScreenPos();
        const float w = ImGui::GetContentRegionAvail().x;
        ImGui::PushID(i);
        if (ImGui::InvisibleButton("##w", ImVec2(w, px(44))))
            go = e.path;
        if (ImGui::IsItemHovered() && (ImGui::GetIO().MouseDelta.x != 0 || ImGui::GetIO().MouseDelta.y != 0))
            s.wt_sel = i;
        ImGui::PopID();
        if (i == s.wt_sel)
            d->AddRectFilled(a, ImVec2(a.x + w, a.y + px(44)), g_pal->selected, px(7));
        float x = a.x + px(12);
        const std::string branch = e.branch.empty() ? std::string("detached") : e.branch;
        d->AddText(s.fonts.ui_medium, fpx(13), ImVec2(x, a.y + px(6)), g_pal->text, branch.c_str());
        x += s.fonts.ui_medium->CalcTextSizeA(fpx(13), FLT_MAX, 0, branch.c_str()).x + px(8);
        if (e.main) {
            d->AddText(s.fonts.ui, fpx(11), ImVec2(x, a.y + px(8)), g_pal->faint, "main checkout");
        }
        // "open" if a space shows it, else the age of its last commit.
        // herdr spaceOf(): a terminal in the checkout's folder, the deepest checkout winning.
        auto inside = [](const std::string& p, const std::string& dir) {
            return _strnicmp(p.c_str(), dir.c_str(), dir.size()) == 0 &&
                   (p.size() == dir.size() || p[dir.size()] == '\\' || p[dir.size()] == '/');
        };
        bool shown_in_space = false;
        for (const auto& [id, cwd] : cwds) {
            if (!inside(cwd, e.path))
                continue;
            const bool deeper = std::any_of(s.worktree_list.begin(), s.worktree_list.end(), [&](const WorktreeEntry& o) {
                return o.path.size() > e.path.size() && inside(cwd, o.path);
            });
            shown_in_space = shown_in_space || !deeper;
        }
        const std::string right = shown_in_space ? std::string("open") : e.age;
        const float rw = s.fonts.ui->CalcTextSizeA(fpx(11.5f), FLT_MAX, 0, right.c_str()).x;
        d->AddText(s.fonts.ui, fpx(11.5f), ImVec2(a.x + w - px(12) - rw, a.y + px(8)),
                   shown_in_space ? g_pal->focus : g_pal->faint, right.c_str());
        text_clipped(d, s.fonts.ui, fpx(11.5f), ImVec2(a.x + px(12), a.y + px(25)), g_pal->faint, display_path(e.path),
                     w - px(24));
        if (i == s.wt_sel && ImGui::IsKeyPressed(ImGuiKey_Enter, false))
            go = e.path;
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::TextDisabled("\xE2\x86\x91\xE2\x86\x93 choose \xC2\xB7 \xE2\x86\xB5 open \xC2\xB7 esc close");
    close = close || ImGui::IsKeyPressed(ImGuiKey_Escape) || dialog_outside_click();
    if (!go.empty()) {
        close = true;
        run_command(s, "worktree.open_at:" + s.worktree_machine + "|" + go);
    }
    if (close)
        ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}
}  // namespace detail

}  // namespace agentdesk::desktop
