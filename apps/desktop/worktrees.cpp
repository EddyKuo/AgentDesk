// The worktree dialogs: a new space on a new git worktree, and opening an existing worktree.
// Portions adapted from herdr-app (Apache-2.0) and modified; see THIRD_PARTY_NOTICES.md.
#include "overlay_kit.hpp"

namespace agentdesk::desktop {
namespace detail {

// Where new worktrees of `g` go: the Settings folder, else .agentdesk/worktrees in the home folder
// on this computer. Empty for another machine, or when the Settings folder is a path of another
// system than the repository's: the machine's agent-host picks that folder in its own home.
std::string worktree_root(const App::State& s, const std::string& machine, const GitInfo& g) {
    std::string root = s.settings.worktree_dir;
    if (!machine.empty() && ui::path_text::is_windows(root) != ui::path_text::is_windows(g.top))
        return {};
    if (root.empty() && machine.empty()) {
        const auto u8 = (os::home_dir() / ".agentdesk" / "worktrees").u8string();
        root.assign(u8.begin(), u8.end());
    }
    return root;
}

std::string worktree_path(const std::string& root, const std::string& repo, const std::string& branch) {
    if (root.empty())
        return {};
    return ui::path_text::join(ui::path_text::join(root, repo), host::worktree_slug(branch));
}

namespace {
// Asks agent-host to add the worktree, then to start the default shell in it; the new session
// becomes a space of its own. The answers come on a HostClient thread: they set wt_busy and
// wt_error ("done" on success) under placements_mutex.
void start_worktree(App::State& s, ui::HostClient* wthost, const std::string& machine, const GitInfo& g,
                    const std::string& root, const std::string& repo, const std::string& branch) {
    {
        std::lock_guard lock(s.placements_mutex);
        s.wt_busy = true;
        s.wt_error.clear();
    }
    Json params{{"repo", g.top}, {"branch", branch}};
    if (const std::string path = worktree_path(root, repo, branch); !path.empty())
        params["path"] = path;
    const std::string shell = pick_shell(s.settings.shell_profile, wthost->profiles());
    wthost->call(
        "worktree.add", params,
        [st = &s, shell, wthost, machine](Json result, std::string error) {
            if (!error.empty()) {
                std::lock_guard lock(st->placements_mutex);
                st->wt_error = error;
                st->wt_busy = false;
                return;
            }
            Json p{{"profile_id", shell}, {"cwd", safe::string_or(result, "path")}};
            wthost->call(
                "session.create", p,
                [st, machine](Json r, std::string e) {
                    std::lock_guard lock(st->placements_mutex);
                    st->wt_busy = false;
                    if (!e.empty()) {
                        st->wt_error = e;
                        return;
                    }
                    Placement pl;  // the space stays unnamed: it follows the checkout
                    pl.session = safe::string_or(r, "session_id");
                    pl.target = LaunchTarget::kSpace;
                    pl.machine = machine;
                    st->placements.push_back(pl);
                    st->wt_error = "done";
                },
                std::chrono::seconds(15));
        },
        std::chrono::seconds(600));
}

}  // namespace

// New worktree: a branch name field; Enter creates the checkout (an existing branch is checked
// out, a new one starts from the current commit) and opens a space on it.
void draw_worktree(App::State& s) {
    if (s.show_worktree) {
        ImGui::OpenPopup("##worktree");
        s.show_worktree = false;
    }
    center_next(px(460), 0.2f);
    push_overlay_style();
    const bool open = ImGui::BeginPopupModal("##worktree", nullptr,
                                             ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize);
    pop_overlay_style();
    if (!open)
        return;
    const std::string wt_machine = s.ws.active() != nullptr ? s.ws.active()->machine : std::string();
    const GitInfo g = s.git.get(s.wt_repo, wt_machine);
    const std::string repo = folder_name(g.common_top.empty() ? g.top : g.common_top);
    const std::string root = worktree_root(s, wt_machine, g);
    // Another machine's home folder is only known there: "~".
    const char sep = ui::path_text::separator(root.empty() ? g.top : root);
    const std::string shown_root =
        (root.empty() ? std::string("~") + sep + ".agentdesk" + sep + "worktrees" : display_path(root)) +
        sep + repo + sep;
    worktree_title(s, g.repo ? "New worktree of " + repo : std::string("New worktree"));
    ImGui::PushStyleColor(ImGuiCol_Text, v4(g_pal->dim));
    if (!g.repo)
        ImGui::TextWrapped("%s is not in a git repository.", display_path(s.wt_repo).c_str());
    else
        ImGui::TextWrapped(
            "A new space on a checkout in %s. A branch that exists is checked out; a new one "
            "starts from here.",
            shown_root.c_str());
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
    bool busy = false;
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
    bool cancel = ImGui::IsKeyPressed(ImGuiKey_Escape) || dialog_outside_click();
    if (create && !busy) {
        const std::string branch = trimmed(s.wt_branch);
        if (branch.empty()) {
            std::lock_guard lock(s.placements_mutex);
            s.wt_error = "Type a branch name.";
        } else if (wthost->connected()) {
            start_worktree(s, wthost, wt_machine, g, root, repo, branch);
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

namespace {
constexpr const char* kOpenHint =
    "\xE2\x86\x91\xE2\x86\x93 choose \xC2\xB7 \xE2\x86\xB5 open \xC2\xB7 esc close";

// True when `path` is `dir` or inside it (case aside, either slash).
bool path_inside(const std::string& path, const std::string& dir) {
    return _strnicmp(path.c_str(), dir.c_str(), dir.size()) == 0 &&
           (path.size() == dir.size() || path[dir.size()] == '\\' || path[dir.size()] == '/');
}

// A space shows the checkout when one of its terminals is in the checkout's folder and no
// deeper checkout holds that terminal.
bool shown_in_a_space(const App::State& s, const WorktreeEntry& e,
                      const std::map<std::string, std::string>& cwds) {
    bool shown = false;
    for (const auto& [id, cwd] : cwds) {
        if (!path_inside(cwd, e.path))
            continue;
        const bool deeper =
            std::any_of(s.worktree_list.begin(), s.worktree_list.end(), [&](const WorktreeEntry& o) {
                return o.path.size() > e.path.size() && path_inside(cwd, o.path);
            });
        shown = shown || !deeper;
    }
    return shown;
}

// One 44 px row: the branch (13 px medium; "main checkout" after the main one), at the right
// "open" (accent) when a space shows it, else its last commit's age; the path under it. Returns
// true when clicked.
bool worktree_row(App::State& s, ImDrawList* d, const WorktreeEntry& e, int i,
                  const std::map<std::string, std::string>& cwds) {
    const ImVec2 a = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    ImGui::PushID(i);
    const bool clicked = ImGui::InvisibleButton("##w", ImVec2(w, px(44)));
    if (ImGui::IsItemHovered() && (ImGui::GetIO().MouseDelta.x != 0 || ImGui::GetIO().MouseDelta.y != 0))
        s.wt_sel = i;  // the mouse selects only when it moves, so keys keep working
    ImGui::PopID();
    if (i == s.wt_sel)
        d->AddRectFilled(a, ImVec2(a.x + w, a.y + px(44)), g_pal->selected, px(7));
    float x = a.x + px(12);
    const std::string branch = e.branch.empty() ? std::string("detached") : e.branch;
    d->AddText(s.fonts.ui_medium, fpx(13), ImVec2(x, a.y + px(6)), g_pal->text, branch.c_str());
    x += s.fonts.ui_medium->CalcTextSizeA(fpx(13), FLT_MAX, 0, branch.c_str()).x + px(8);
    if (e.main)
        d->AddText(s.fonts.ui, fpx(11), ImVec2(x, a.y + px(8)), g_pal->faint, "main checkout");
    const bool open = shown_in_a_space(s, e, cwds);
    const std::string right = open ? std::string("open") : e.age;
    const float rw = s.fonts.ui->CalcTextSizeA(fpx(11.5f), FLT_MAX, 0, right.c_str()).x;
    d->AddText(s.fonts.ui, fpx(11.5f), ImVec2(a.x + w - px(12) - rw, a.y + px(8)),
               open ? g_pal->focus : g_pal->faint, right.c_str());
    text_clipped(d, s.fonts.ui, fpx(11.5f), ImVec2(a.x + px(12), a.y + px(25)), g_pal->faint,
                 display_path(e.path), w - px(24));
    return clicked;
}

}  // namespace

// "Open worktree of <repo>": the main checkout first, then by last commit; a filter on branch
// and path; Up/Down wrap; Enter opens a space on the checkout.
void draw_worktree_open(App::State& s) {
    if (s.show_worktree_open) {
        ImGui::OpenPopup("##wtopen");
        s.show_worktree_open = false;
        s.wt_filter[0] = 0;
        s.wt_sel = 0;
    }
    center_next(px(560), 0.12f);
    push_overlay_style();
    const bool open =
        ImGui::BeginPopupModal("##wtopen", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize);
    pop_overlay_style();
    if (!open)
        return;
    const GitInfo g = s.git.get(s.focused_cwd(), s.worktree_machine);
    const std::string repo = folder_name(g.common_top.empty() ? g.top : g.common_top);
    const bool no_repo = !s.worktree_not_repo.empty();
    const std::string f = ascii_lower(s.wt_filter);
    std::vector<const WorktreeEntry*> shown;
    for (const auto& e : s.worktree_list)
        if (f.empty() || ascii_lower(e.branch + " " + e.path).find(f) != std::string::npos)
            shown.push_back(&e);
    char count[64];
    if (shown.size() == s.worktree_list.size())
        std::snprintf(count, sizeof count, "%zu checkouts", s.worktree_list.size());
    else
        std::snprintf(count, sizeof count, "%zu/%zu checkouts", shown.size(), s.worktree_list.size());
    worktree_title(s, no_repo ? std::string("Open worktree") : "Open worktree of " + repo,
                   no_repo ? std::string() : std::string(count));
    if (no_repo) {
        ImGui::PushStyleColor(ImGuiCol_Text, v4(g_pal->dim));
        ImGui::TextWrapped("%s is not in a git repository.", s.worktree_not_repo.c_str());
        ImGui::PopStyleColor();
        ImGui::TextDisabled("%s", kOpenHint);
        if (ImGui::IsKeyPressed(ImGuiKey_Escape))
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
    ImGui::BeginChild("##wtl",
                      ImVec2(0, std::min(px(330), static_cast<float>(std::max(n, 1)) * px(44) + px(4))),
                      ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
    ImDrawList* d = ImGui::GetWindowDrawList();
    if (n == 0) {
        ImGui::SetCursorPos(ImVec2(px(10), px(10)));
        ImGui::TextDisabled("No matching worktrees.");
    }
    const auto cwds = s.session_cwds();
    for (int i = 0; i < n; ++i) {
        const auto& e = *shown[static_cast<std::size_t>(i)];
        if (worktree_row(s, d, e, i, cwds))
            go = e.path;
        if (i == s.wt_sel && ImGui::IsKeyPressed(ImGuiKey_Enter, false))
            go = e.path;
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::TextDisabled("%s", kOpenHint);
    bool close = ImGui::IsKeyPressed(ImGuiKey_Escape) || dialog_outside_click();
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
