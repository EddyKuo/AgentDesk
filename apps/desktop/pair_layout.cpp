// Pair: two agents side by side on the same task, a shared prompt bar under them, and a small
// race: when both finish (structured states only), a strip says who was first and by how much.
// The rules (the pair's life, the race, the names) are in libs/ui pair.cpp.
#include "app_internal.hpp"

namespace agentdesk::desktop {
namespace detail {
namespace pr = ui::pair;
using pr::Side;

namespace {
constexpr const char* kSwapIcon = "\xEE\xAF\x8B";   // codicon arrow-swap
constexpr const char* kEnterMark = "\xE2\x86\xB5";  // the return arrow
constexpr float kHeaderH = 40.0f;                   // a pane's header, design px
constexpr float kStripH = 92.0f;                    // the race result strip
constexpr std::array<Side, 2> kSides{Side::kLeft, Side::kRight};

long long epoch_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

const ui::SessionView* view_of(const App::State& s, const std::string& id) {
    if (id.empty())
        return nullptr;
    const auto it = s.views_by_id.find(id);
    return it == s.views_by_id.end() ? nullptr : &it->second;
}

// Still running, or just created and not listed by the host yet.
bool alive(const App::State& s, const std::string& id) {
    if (id.empty())
        return false;
    if (s.awaiting_list.contains(id))
        return true;
    const ui::SessionView* v = view_of(s, id);
    return v != nullptr && !v->lost && v->process_state != "Exited" && s.by_id.contains(id);
}

// The name the strip and the headers use for a side: the pane's title ("Claude Code (A)").
std::string side_title(const App::State& s, Side side) {
    const std::string& id = s.pair.model.id(side);
    if (id.empty())
        return side == Side::kLeft ? "Left" : "Right";
    return s.pane_title(id);
}

std::string machine_of_id(App::State& s, const std::string& id) {
    const auto it = s.machine_of.find(id);
    return it == s.machine_of.end() ? std::string() : it->second;
}

// The pair as ui.json keeps it.
void remember(App::State& s) {
    auto& ps = s.settings.pair;
    const bool on = s.pair.model.phase() == pr::Phase::kRunning || s.pair.model.phase() == pr::Phase::kHalf;
    const std::string l = on ? s.pair.model.id(Side::kLeft) : std::string();
    const std::string r = on ? s.pair.model.id(Side::kRight) : std::string();
    if (ps.left == l && ps.right == r)
        return;
    ps.left = l;
    ps.right = r;
    save_settings(s.settings);
}

// A new round: the strip, the choice and the finish line clear.
void new_round(App::State& s) {
    s.pair.dismissed = false;
    s.pair.kept.reset();
    s.pair.finish_side.reset();
}

std::vector<std::pair<std::string, std::string>> profiles_here(App::State& s, std::string* machine) {
    *machine = s.ws.active() != nullptr ? s.ws.active()->machine : std::string();
    return s.host_for(*machine)->profiles();
}

std::string profile_name(const std::vector<std::pair<std::string, std::string>>& profiles,
                         const std::string& id) {
    for (const auto& [pid, name] : profiles)
        if (pid == id)
            return name;
    return id;
}

// The setup card's two picks: the saved ones while the host still offers them, else the defaults.
std::pair<std::string, std::string> picks(const App::State& s,
                                          const std::vector<std::pair<std::string, std::string>>& profiles) {
    const auto def = pr::default_profiles(profiles);
    auto offered = [&](const std::string& id) {
        return std::any_of(profiles.begin(), profiles.end(), [&](const auto& p) { return p.first == id; });
    };
    const auto& ps = s.settings.pair;
    return {offered(ps.left_profile) ? ps.left_profile : def.first,
            offered(ps.right_profile) ? ps.right_profile : def.second};
}

// The folder a new pair works in: the focused pane's, else the launcher's.
std::string pair_folder(const App::State& s) {
    std::string cwd = s.focused_cwd();
    if (cwd.empty())
        cwd = s.launch_cwd;
    return cwd;
}

// Asks agent-host for one side: a worktree first when `branch` is set, then the session. The
// answers come on a HostClient thread; a placement or an error goes back under placements_mutex.
void start_side(App::State& s, ui::HostClient* hc, const std::string& machine, Side side,
                const std::string& profile, const std::string& cwd, const std::string& name,
                const std::string& repo_top, const std::string& branch, const std::string& path) {
    auto create = [st = &s, hc, machine, side, profile, name](const std::string& in) {
        hc->call(
            "session.create", Json{{"profile_id", profile}, {"cwd", in}, {"name", name}},
            [st, machine, side](Json r, std::string e) {
                std::lock_guard lock(st->placements_mutex);
                if (!e.empty()) {
                    st->pair.errors.push_back(e);
                    return;
                }
                Placement pl;
                pl.session = safe::string_or(r, "session_id");
                pl.machine = machine;
                pl.pair_side = pr::side_name(side);
                st->placements.push_back(pl);
            },
            std::chrono::seconds(15));
    };
    if (branch.empty()) {
        create(cwd);
        return;
    }
    Json params{{"repo", repo_top}, {"branch", branch}};
    if (!path.empty())
        params["path"] = path;
    hc->call(
        "worktree.add", params,
        [st = &s, create](Json result, std::string error) {
            if (!error.empty()) {
                std::lock_guard lock(st->placements_mutex);
                st->pair.errors.push_back("Could not create a worktree: " + error);
                return;
            }
            create(safe::string_or(result, "path"));
        },
        std::chrono::seconds(600));
}

// pair.start: both sides of a new pair (profiles by id; "" takes the setup card's picks).
std::string start_pair(App::State& s, std::string left, std::string right) {
    std::string machine;
    const auto profiles = profiles_here(s, &machine);
    ui::HostClient* hc = s.host_for(machine);
    if (!hc->connected())
        return "agent-host is not connected";
    const auto p = picks(s, profiles);
    if (left.empty())
        left = p.first;
    if (right.empty())
        right = p.second;
    for (const std::string* id : {&left, &right})
        if (std::none_of(profiles.begin(), profiles.end(), [&](const auto& q) { return q.first == *id; }))
            return "no profile named " + *id;
    const std::string cwd = pair_folder(s);
    const GitInfo g = s.git.get(cwd, machine);
    const bool worktrees = s.settings.pair.worktrees && g.repo;
    if (!s.pair.model.start())
        return "a pair is already starting";
    s.pair.race.reset();
    new_round(s);
    s.pair.starting_since = Clock::now();
    s.pair.starting_note = worktrees ? "Making a worktree for each side..." : "Starting both agents...";
    s.settings.pair.left_profile = left;
    s.settings.pair.right_profile = right;
    save_settings(s.settings);
    std::time_t t = std::time(nullptr);
    std::tm lt{};
    localtime_s(&lt, &t);
    const std::string stamp = pr::stamp_of(lt.tm_mon + 1, lt.tm_mday, lt.tm_hour, lt.tm_min);
    const std::string root = worktree_root(s, machine);
    const std::string repo = folder_name(g.common_top.empty() ? g.top : g.common_top);
    for (const Side side : kSides) {
        const std::string& prof = side == Side::kLeft ? left : right;
        std::string branch, path;
        if (worktrees) {
            branch = pr::branch_name(prof, side, stamp);
            if (!root.empty())
                path = root + "\\" + repo + "\\" + host::worktree_slug(branch);
        }
        start_side(s, hc, machine, side, prof, cwd, pr::session_name(profile_name(profiles, prof), side),
                   g.top, branch, path);
    }
    if (s.layout().style != "pair")
        toggle_layout(s, "pair");
    return {};
}

// pair.start:existing: the first two agents of the sidebar's list.
std::string pair_existing(App::State& s) {
    const AgentRoster r = agent_roster(s);
    std::vector<std::string> ids;
    for (const auto& id : r.ids)
        if (alive(s, id))
            ids.push_back(id);
    if (ids.size() < 2)
        return "Pair needs two agents running";
    s.pair.race.reset();
    new_round(s);
    s.pair.model.adopt(ids[0], ids[1]);
    remember(s);
    if (s.layout().style != "pair")
        toggle_layout(s, "pair");
    s.focus(ids[0]);
    return {};
}

// pair.send: the text (the bar's, or given) and Enter to one side or both. Sending to both starts
// a new round of the race.
std::string send_prompt(App::State& s, pr::Target target, std::string text) {
    const bool from_bar = text.empty();
    if (from_bar)
        text = trimmed(s.pair.prompt);
    if (text.empty())
        return "type something to send first";
    const long long now = epoch_ms();
    bool sent = false;
    if (target == pr::Target::kBoth) {
        s.pair.race.reset();
        new_round(s);
    }
    for (const Side side : kSides) {
        if ((target == pr::Target::kLeft && side != Side::kLeft) ||
            (target == pr::Target::kRight && side != Side::kRight))
            continue;
        const std::string& id = s.pair.model.id(side);
        const auto it = s.by_id.find(id);
        if (!alive(s, id) || it == s.by_id.end() || !it->second->can_input())
            continue;
        it->second->send(host::PasteInput{text});
        it->second->send(host::RawInput{"\r"});
        s.pair.race.sent(side, now);
        sent = true;
    }
    if (!sent)
        return "no side of the pair can take input";
    if (from_bar)
        s.pair.prompt[0] = 0;
    return {};
}

// ---------------------------------------------------------------- small drawing helpers
// A pill (20 high) in `col` on its 15 % tint; returns its width.
float pill(App::State& s, ImDrawList* dl, float x, float cy, const char* text, ImU32 col,
           bool measure = false) {
    const ImVec2 t = tok::measure(s, ui::tokens::kCaption, text);
    const float w = t.x + tok::gap_m() + tok::gap_xs();
    if (!measure) {
        dl->AddRectFilled(ImVec2(x, cy - px(10)), ImVec2(x + w, cy + px(10)), alpha(col, 0x26), px(10));
        tok::text(s, dl, ui::tokens::kCaption, ImVec2(x + (w - t.x) * 0.5f, cy - t.y * 0.5f), col, text);
    }
    return w;
}

// A quiet text button (Caption, dim; hover fill). Returns true when clicked.
bool text_button(App::State& s, ImDrawList* dl, const char* id, const char* label, ImVec2 at, float h,
                 bool enabled = true, ImU32 col = 0, const tok::TypeLevel& level = ui::tokens::kBody) {
    const ImVec2 t = tok::measure(s, level, label);
    const float w = t.x + tok::gap_m() * 2;
    ImGui::SetCursorScreenPos(at);
    ImGui::BeginDisabled(!enabled);
    const bool clicked = ImGui::InvisibleButton(id, ImVec2(w, h));
    ImGui::EndDisabled();
    const bool hov = enabled && ImGui::IsItemHovered();
    if (hov) {
        dl->AddRectFilled(at, ImVec2(at.x + w, at.y + h), alpha(g_pal->text, g_pal->light ? 0x0D : 0x10),
                          tok::radius_small());
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    dl->AddRect(at, ImVec2(at.x + w, at.y + h), hov ? mix(g_pal->border, g_pal->text, 0.2f) : g_pal->border,
                tok::radius_small());
    const ImU32 c = !enabled ? g_pal->faint : col != 0 ? col : g_pal->text;
    tok::text(s, dl, level, ImVec2(at.x + (w - t.x) * 0.5f, at.y + (h - t.y) * 0.5f), c, label);
    return clicked && enabled;
}

float text_button_width(const App::State& s, const char* label,
                        const tok::TypeLevel& level = ui::tokens::kBody) {
    return tok::measure(s, level, label).x + tok::gap_m() * 2;
}

// The accent button (the main action). Returns true when clicked.
bool accent_button(App::State& s, ImDrawList* dl, const char* id, const std::string& label, ImVec2 at,
                   float h, bool enabled = true) {
    const ImVec2 t = tok::measure(s, ui::tokens::kBody, label.c_str());
    const float w = t.x + tok::gap_l() * 2;
    ImGui::SetCursorScreenPos(at);
    ImGui::BeginDisabled(!enabled);
    const bool clicked = ImGui::InvisibleButton(id, ImVec2(w, h));
    ImGui::EndDisabled();
    const bool hov = enabled && ImGui::IsItemHovered();
    const ImU32 fill = !enabled ? alpha(g_pal->focus, 0x30) : hov ? alpha(g_pal->focus, 225) : g_pal->focus;
    dl->AddRectFilled(at, ImVec2(at.x + w, at.y + h), fill, tok::radius_small());
    if (hov)
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    ImFont* f = tok::font(s, ui::tokens::kBody);
    dl->AddText(s.fonts.ui_medium != nullptr ? s.fonts.ui_medium : f, tok::size(ui::tokens::kBody),
                ImVec2(at.x + (w - t.x) * 0.5f, at.y + (h - t.y) * 0.5f),
                enabled ? accent_foreground() : alpha(g_pal->text, 0x70), label.c_str());
    return clicked && enabled;
}

float accent_button_width(const App::State& s, const std::string& label) {
    return tok::measure(s, ui::tokens::kBody, label.c_str()).x + tok::gap_l() * 2;
}

// A small checkered flag of our own: a pole and a 3 x 2 board in `col`.
void draw_flag(ImDrawList* dl, ImVec2 at, float h, ImU32 col) {
    const float cell = h * 0.22f;
    dl->AddRectFilled(at, ImVec2(at.x + std::max(1.0f, h * 0.08f), at.y + h), col);
    const ImVec2 b(at.x + std::max(1.0f, h * 0.08f), at.y);
    for (int r = 0; r < 2; ++r)
        for (int c = 0; c < 3; ++c) {
            const ImVec2 p(b.x + static_cast<float>(c) * cell, b.y + static_cast<float>(r) * cell);
            dl->AddRectFilled(p, ImVec2(p.x + cell, p.y + cell), (r + c) % 2 == 0 ? col : alpha(col, 0x40));
        }
}

// The finish line: a checkered band across the top of a pane, eased in once (still under reduce
// motion: it just appears).
void draw_finish_band(ImDrawList* dl, ImVec2 a, float w, float k, ImU32 col, float radius) {
    if (k <= 0.0f)
        return;
    const float cell = px(4);
    const float shown = w * k;  // grows from the left like a ribbon being drawn
    dl->PushClipRect(ImVec2(a.x + radius * 0.5f, a.y),
                     ImVec2(a.x + std::min(w - radius * 0.5f, shown), a.y + cell * 2), true);
    const int n = static_cast<int>(w / cell) + 1;
    for (int r = 0; r < 2; ++r)
        for (int c = 0; c < n; ++c)
            if ((r + c) % 2 == 0)
                dl->AddRectFilled(
                    ImVec2(a.x + static_cast<float>(c) * cell, a.y + static_cast<float>(r) * cell),
                    ImVec2(a.x + static_cast<float>(c + 1) * cell, a.y + static_cast<float>(r + 1) * cell),
                    alpha(col, 0xB0));
    dl->PopClipRect();
}

ImU32 picker_fill() {
    return mix(g_pal->card, g_pal->bg, g_pal->light ? 0.35f : 0.25f);
}

// ---------------------------------------------------------------- pair-bar (42 px)
// "Pair", left vs right, and at the right Swap, Close pair and (Windows buttons) the window's.
void draw_bar(App::State& s, ImVec2 pos, float w, float h) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 end(pos.x + w, pos.y + h);
    dl->AddRectFilled(pos, end, g_pal->sidebar);
    tok::hline(dl, pos.x, end.x, end.y - 1.0f, g_pal->border);
    const float cy = pos.y + h * 0.5f;
    float x = clear_of_traffic_lights(s, pos.x + tok::gap_l(), pos.y, end.y, tok::gap_l());
    float right = end.x - tok::gap_s();
    if (s.config.hwnd != nullptr && !mac_window_buttons(s)) {
        draw_window_buttons(s, right + tok::gap_s(), pos.y, h);
        right -= px(26) * 3 + px(4) + px(12);
    }
    const pr::Phase ph = s.pair.model.phase();
    const bool paired = ph == pr::Phase::kRunning || ph == pr::Phase::kHalf;
    const float bh = px(26);
    if (paired) {
        const char* close = "Close pair";
        right -= text_button_width(s, close);
        if (text_button(s, dl, "##pair-close", close, ImVec2(right, cy - bh * 0.5f), bh))
            run_command(s, "pair.close");
        right -= tok::gap_s() + bh;
        const std::string tip = with_chord(s, "Swap sides", "pair.swap");
        if (icon_button(s, "##pair-swap", kSwapIcon, ImVec2(right, cy - bh * 0.5f), bh, fpx(14), tip.c_str(),
                        g_pal->dim))
            run_command(s, "pair.swap");
        right -= tok::gap_m();
    }
    x += tok::text(s, dl, ui::tokens::kTitle,
                   ImVec2(x, cy - tok::measure(s, ui::tokens::kTitle, "Pair").y * 0.5f), g_pal->text, "Pair");
    x += tok::gap_m();
    if (!paired)
        return;
    // "Claude Code (A)  vs  Codex (B)", cut to fit.
    const std::string l = side_title(s, Side::kLeft), r = side_title(s, Side::kRight);
    const float room = right - x;
    const float fs = tok::size(ui::tokens::kBody);
    const float lw = text_clipped(dl, s.fonts.ui, fs, ImVec2(x, cy - fs * 0.6f), g_pal->dim, l, room * 0.45f);
    x += lw + tok::gap_s();
    x += tok::text(s, dl, ui::tokens::kCaption,
                   ImVec2(x, cy - tok::measure(s, ui::tokens::kCaption, "vs").y * 0.5f), g_pal->faint, "vs") +
         tok::gap_s();
    text_clipped(dl, s.fonts.ui, fs, ImVec2(x, cy - fs * 0.6f), g_pal->dim, r, std::max(0.0f, right - x));
}

// ---------------------------------------------------------------- the setup card
// The program box for one side: icon, profile name, chevron; opens the list of profiles.
void draw_pick_box(App::State& s, ImDrawList* dl, Side side, ImVec2 a, ImVec2 b,
                   const std::vector<std::pair<std::string, std::string>>& profiles,
                   const std::string& picked, ui::HostClient* host, bool enabled) {
    const int si = static_cast<int>(pr::index(side));
    ImGui::SetCursorScreenPos(a);
    ImGui::PushID(si);
    ImGui::BeginDisabled(!enabled);
    const bool clicked = ImGui::InvisibleButton("##pick", ImVec2(b.x - a.x, b.y - a.y));
    ImGui::EndDisabled();
    const bool hov = enabled && ImGui::IsItemHovered();
    const bool open = s.pair.picker == si;
    dl->AddRectFilled(a, b, hov || open ? tok::hover(picker_fill()) : picker_fill(), tok::radius_small());
    dl->AddRect(a, b, open ? tok::focus_ring(picker_fill()) : g_pal->border, tok::radius_small());
    const float cy = (a.y + b.y) * 0.5f;
    float x = a.x + tok::gap_m();
    const float icon = px(18);
    if (!picked.empty())
        program_icon(dl, program_of_profile(host->profile_executable(picked), picked),
                     ImVec2(x, cy - icon * 0.5f), icon, g_pal->text);
    x += icon + tok::gap_s();
    const std::string name = picked.empty() ? std::string("No programs") : profile_name(profiles, picked);
    const float fs = tok::size(ui::tokens::kBody);
    text_clipped(dl, s.fonts.ui_medium, fs, ImVec2(x, cy - fs * 0.6f), g_pal->text, name, b.x - x - px(28));
    draw_icon(s, dl, ui::icon::kChevronDown, fpx(13), ImVec2(b.x - px(16), cy), g_pal->dim);
    if (clicked) {
        s.pair.picker = si;
        ImGui::OpenPopup("##pairpick");
    }
    ImGui::SetNextWindowPos(ImVec2(a.x, b.y + px(4)));
    ImGui::SetNextWindowSizeConstraints(ImVec2(b.x - a.x, 0), ImVec2(FLT_MAX, px(360)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(5), px(5)));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    const bool popup = ImGui::BeginPopup("##pairpick");
    ImGui::PopStyleVar(2);
    if (popup) {
        const ImVec2 wp = ImGui::GetWindowPos(), wsz = ImGui::GetWindowSize();
        tok::window_shadow(ImGui::GetWindowDrawList(), wp, ImVec2(wp.x + wsz.x, wp.y + wsz.y),
                           ImGui::GetStyle().PopupRounding, 1);
        ImDrawList* pd = ImGui::GetWindowDrawList();
        for (std::size_t i = 0; i < profiles.size(); ++i) {
            const auto& [pid, pname] = profiles[i];
            ImGui::PushID(static_cast<int>(i));
            const ImVec2 ra = ImGui::GetCursorScreenPos();
            const float rw = std::max(ImGui::GetContentRegionAvail().x, b.x - a.x - px(10));
            const float rh = px(32);
            if (ImGui::InvisibleButton("##p", ImVec2(rw, rh))) {
                (side == Side::kLeft ? s.settings.pair.left_profile : s.settings.pair.right_profile) = pid;
                save_settings(s.settings);
                ImGui::CloseCurrentPopup();
            }
            const bool rh_hov = ImGui::IsItemHovered();
            if (pid == picked || rh_hov)
                pd->AddRectFilled(ra, ImVec2(ra.x + rw, ra.y + rh),
                                  pid == picked ? tok::selected(g_pal->card) : tok::hover(g_pal->card),
                                  tok::radius_small());
            const float ry = ra.y + rh * 0.5f;
            program_icon(pd, program_of_profile(host->profile_executable(pid), pid),
                         ImVec2(ra.x + tok::gap_s(), ry - px(8)), px(16), g_pal->text);
            pd->AddText(s.fonts.ui, fs, ImVec2(ra.x + tok::gap_s() + px(16) + tok::gap_s(), ry - fs * 0.6f),
                        g_pal->text, pname.c_str());
            if (pid == picked)
                draw_icon(s, pd, ui::icon::kCheck, fpx(13), ImVec2(ra.x + rw - px(14), ry),
                          tok::selected_edge());
            ImGui::PopID();
        }
        ImGui::EndPopup();
    } else if (s.pair.picker == si && !ImGui::IsPopupOpen("##pairpick")) {
        s.pair.picker = -1;
    }
    ImGui::PopID();
}

// Two little terminals and a finish line between them (the card's drawing).
void draw_pair_art(ImDrawList* dl, ImVec2 c, float side) {
    const float w = side * 0.62f, h = side * 0.5f;
    const ImU32 line = mix(g_pal->card, g_pal->text, 0.22f);
    const ImU32 fill = mix(g_pal->card, g_pal->text, 0.05f);
    for (int i = 0; i < 2; ++i) {
        const float dx = (i == 0 ? -1.0f : 1.0f) * (w * 0.5f + side * 0.1f);
        const ImVec2 a(c.x + dx - w * 0.5f, c.y - h * 0.5f);
        const ImVec2 b(a.x + w, a.y + h);
        dl->AddRectFilled(a, b, fill, px(5));
        dl->AddRect(a, b, line, px(5), 0, px(1.2f));
        for (int k = 0; k < 3; ++k)
            dl->AddRectFilled(ImVec2(a.x + px(6), a.y + px(7) + static_cast<float>(k) * px(6)),
                              ImVec2(a.x + px(6) + w * (k == 1 ? 0.35f : 0.55f),
                                     a.y + px(9) + static_cast<float>(k) * px(6)),
                              k == 2 && i == 0 ? g_pal->done : line, px(1));
    }
    // The finish line: a short checkered post between them.
    const float cell = px(3);
    for (int r = 0; r < 6; ++r)
        for (int col = 0; col < 2; ++col)
            if ((r + col) % 2 == 0)
                dl->AddRectFilled(ImVec2(c.x - cell + static_cast<float>(col) * cell,
                                         c.y - h * 0.5f + static_cast<float>(r) * cell * 1.6f),
                                  ImVec2(c.x + static_cast<float>(col) * cell,
                                         c.y - h * 0.5f + static_cast<float>(r) * cell * 1.6f + cell),
                                  g_pal->done);
}

void draw_setup(App::State& s, ImVec2 pos, ImVec2 size) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (!s.host->connected()) {
        StateCard c;
        c.kind = CardKind::kLoading;
        c.title = "Connecting to agent-host...";
        draw_state_card(s, pos, size, c);
        return;
    }
    std::string machine;
    const auto profiles = profiles_here(s, &machine);
    ui::HostClient* host = s.host_for(machine);
    const auto [lp, rp] = picks(s, profiles);
    const std::string cwd = pair_folder(s);
    const GitInfo g = s.git.get(cwd, machine);
    const bool starting = s.pair.model.phase() == pr::Phase::kStarting;

    const float cw = std::min(px(600), size.x - tok::gap_xl() * 2);
    const float pad = tok::gap_xl();
    const float inner = cw - pad * 2;
    // Height: art, title, hint, labels + boxes, worktree row (2 lines), folder, error, buttons.
    const float title_fs = fpx(ui::tokens::kTitle.size + 3.0f);
    const float hint_fs = tok::size(ui::tokens::kBody);
    const float cap_fs = tok::size(ui::tokens::kCaption);
    const std::string error = s.pair.model.error();
    const float box_h = px(48);
    // The worktree note: what the switch means for this folder (a warning outside git or when off).
    const bool can_wt = g.repo;
    const bool wt_on = s.settings.pair.worktrees && can_wt;
    std::string note;
    ImU32 note_col = g_pal->dim;
    const char* glyph = nullptr;
    if (!can_wt) {
        note = (cwd.empty() ? std::string("This folder") : display_path(cwd)) +
               " is not a git repository, so both agents will work in the same folder and may change the "
               "same files.";
        note_col = g_pal->working;
        glyph = ui::icon::kWarning;
    } else if (wt_on) {
        note = "Each side gets its own checkout of " +
               folder_name(g.common_top.empty() ? g.top : g.common_top) +
               " on a new pair/ branch, so they never touch the same files. Uncommitted changes stay here.";
    } else {
        note = "Both agents work in " + display_path(g.top) +
               ". They may change the same files at the same time.";
        note_col = g_pal->working;
        glyph = ui::icon::kWarning;
    }
    const float note_indent = glyph != nullptr ? px(18) : 0.0f;
    const float note_h = s.fonts.ui->CalcTextSizeA(cap_fs, FLT_MAX, inner - note_indent, note.c_str()).y;
    // A short window drops the drawing first, then tightens the space between the sections.
    auto height = [&](float sec) {
        return pad + title_fs * 1.3f + tok::gap_xs() + hint_fs * 1.3f + sec + cap_fs * 1.3f + tok::gap_xs() +
               box_h + tok::gap_l() + px(22) + tok::gap_xs() + note_h + tok::gap_m() + cap_fs * 1.3f +
               (error.empty() ? 0.0f : tok::gap_s() + cap_fs * 1.3f) + sec + px(36) + pad;
    };
    const float art_h = px(56) + tok::gap_l();
    float sec = tok::gap_xl();
    float ch = height(sec);
    const bool show_art = ch + art_h <= size.y - tok::gap_l() * 2;
    if (!show_art && ch > size.y - tok::gap_s() * 2) {
        sec = tok::gap_m();
        ch = height(sec);
    }
    if (show_art)
        ch += art_h;
    const ImVec2 a(pos.x + (size.x - cw) * 0.5f, pos.y + std::max(0.0f, (size.y - ch) * 0.42f));
    const ImVec2 b(a.x + cw, a.y + ch);
    const float k = tok::appear(ImGui::GetID("##pair-setup"), ui::tokens::kNormalMs, 1000.0);
    const float lift = (1.0f - k) * px(ui::tokens::kSlideDistance);
    const ImVec2 la(a.x, a.y + lift), lb(b.x, b.y + lift);
    with_alpha(dl, k, [&] { tok::card(dl, la, lb, tok::radius_panel(), 1); });
    float y = la.y + pad;
    const float cx = (la.x + lb.x) * 0.5f;
    if (show_art) {
        with_alpha(dl, k, [&] { draw_pair_art(dl, ImVec2(cx, y + px(28)), px(64)); });
        y += art_h;
    }
    auto centred = [&](ImFont* f, float fs, const std::string& t, ImU32 col) {
        const ImVec2 ts = f->CalcTextSizeA(fs, FLT_MAX, 0, t.c_str());
        dl->AddText(f, fs, ImVec2(cx - ts.x * 0.5f, y), col, t.c_str());
        y += fs * 1.3f;
    };
    centred(s.fonts.ui_bold, title_fs, "Pair two agents", g_pal->text);
    y += tok::gap_xs();
    centred(s.fonts.ui, hint_fs, "Give both the same task, watch them side by side, keep the better result.",
            g_pal->dim);
    y += sec;

    // LEFT  [box]  vs  [box]  RIGHT
    const float vs_w = px(36);
    const float bw = (inner - vs_w) * 0.5f;
    const float x0 = la.x + pad, x1 = x0 + bw + vs_w;
    tok::text(s, dl, ui::tokens::kCaption, ImVec2(x0, y), g_pal->faint, "LEFT");
    tok::text(s, dl, ui::tokens::kCaption, ImVec2(x1, y), g_pal->faint, "RIGHT");
    y += cap_fs * 1.3f + tok::gap_xs();
    draw_pick_box(s, dl, Side::kLeft, ImVec2(x0, y), ImVec2(x0 + bw, y + box_h), profiles, lp, host,
                  !starting);
    draw_pick_box(s, dl, Side::kRight, ImVec2(x1, y), ImVec2(x1 + bw, y + box_h), profiles, rp, host,
                  !starting);
    {
        const ImVec2 vc(x0 + bw + vs_w * 0.5f, y + box_h * 0.5f);
        dl->AddCircleFilled(vc, px(13), g_pal->card);
        dl->AddCircle(vc, px(13), g_pal->border, 0, 1.0f);
        const ImVec2 t = tok::measure(s, ui::tokens::kCaption, "vs");
        tok::text(s, dl, ui::tokens::kCaption, ImVec2(vc.x - t.x * 0.5f, vc.y - t.y * 0.5f), g_pal->dim,
                  "vs");
    }
    y += box_h + tok::gap_l();

    // Separate worktrees: a switch and what it means here.
    {
        const bool can = can_wt;
        bool on = wt_on;
        const ImVec2 sw(x0, y + px(3));
        const ImVec2 sz(px(30), px(16));
        ImGui::SetCursorScreenPos(ImVec2(x0, y));
        ImGui::BeginDisabled(!can || starting);
        const char* label = "Separate worktrees";
        const ImVec2 lt = tok::measure(s, ui::tokens::kBody, label);
        if (ImGui::InvisibleButton("##pair-wt", ImVec2(sz.x + tok::gap_s() + lt.x, px(22)))) {
            s.settings.pair.worktrees = !s.settings.pair.worktrees;
            save_settings(s.settings);
            on = s.settings.pair.worktrees;
        }
        const bool hov = ImGui::IsItemHovered();
        ImGui::EndDisabled();
        const float kk = tok::fade(ImGui::GetID("##pair-wt-k"), on, ui::tokens::kFastMs);
        const ImU32 track =
            can ? mix(mix(g_pal->card, g_pal->text, 0.18f), g_pal->focus, kk) : alpha(g_pal->faint, 0x40);
        dl->AddRectFilled(sw, ImVec2(sw.x + sz.x, sw.y + sz.y), track, sz.y * 0.5f);
        const float knob = sz.y - px(4);
        const float kx = sw.x + px(2) + (sz.x - knob - px(4)) * kk;
        dl->AddCircleFilled(
            ImVec2(kx + knob * 0.5f, sw.y + sz.y * 0.5f), knob * 0.5f,
            can ? (hov ? IM_COL32(255, 255, 255, 255) : IM_COL32(250, 250, 250, 255)) : g_pal->card);
        tok::text(s, dl, ui::tokens::kBody, ImVec2(sw.x + sz.x + tok::gap_s(), y + (px(22) - lt.y) * 0.5f),
                  can ? g_pal->text : g_pal->dim, label);
        y += px(22) + tok::gap_xs();
        if (glyph != nullptr)
            draw_icon(s, dl, glyph, fpx(12), ImVec2(x0 + px(6), y + cap_fs * 0.65f), note_col);
        dl->AddText(s.fonts.ui, cap_fs, ImVec2(x0 + note_indent, y), note_col, note.c_str(), nullptr,
                    inner - note_indent);
        y += note_h + tok::gap_m();
    }
    // Where.
    {
        draw_icon(s, dl, ui::icon::kFolder, fpx(12), ImVec2(x0 + px(6), y + cap_fs * 0.65f), g_pal->faint);
        const std::string where =
            cwd.empty() ? std::string("No folder yet: focus a terminal first") : display_path(cwd);
        text_clipped(dl, s.fonts.ui, cap_fs, ImVec2(x0 + px(18), y), g_pal->faint, where, inner - px(18));
        y += cap_fs * 1.3f;
    }
    if (!error.empty()) {
        y += tok::gap_s();
        text_clipped(dl, s.fonts.ui, cap_fs, ImVec2(x0, y), g_pal->needs_you, error, inner);
        y += cap_fs * 1.3f;
    }
    y += sec;
    const float bh = px(36);
    if (starting) {
        const float t = static_cast<float>(ImGui::GetTime());
        const float a0 = t * (2.0f * 3.14159265f / 0.9f);
        dl->PathArcTo(ImVec2(x0 + px(8), y + bh * 0.5f), px(7), a0, a0 + 4.4f, 20);
        dl->PathStroke(g_pal->dim, 0, px(1.8f));
        tok::text(s, dl, ui::tokens::kBody, ImVec2(x0 + px(24), y + (bh - hint_fs) * 0.5f), g_pal->dim,
                  s.pair.starting_note.c_str());
        return;
    }
    const std::string start = "Start pair";
    const float sw = accent_button_width(s, start);
    if (accent_button(s, dl, "##pair-start", start, ImVec2(x0 + inner - sw, y), bh,
                      !lp.empty() && !rp.empty()))
        run_command(s, "pair.start");
    // Two agents already running: pair them as they are.
    int agents = 0;
    for (const auto& id : agent_roster(s).ids)
        agents += alive(s, id) ? 1 : 0;
    if (agents >= 2) {
        const char* existing = "Pair two open agents";
        const float ew = text_button_width(s, existing);
        if (text_button(s, dl, "##pair-existing", existing, ImVec2(x0 + inner - sw - tok::gap_s() - ew, y),
                        bh))
            run_command(s, "pair.start:existing");
    }
    const std::string chord = s.keymap.chord_for("pair.toggle");
    if (!chord.empty())
        tok::text(s, dl, ui::tokens::kCaption, ImVec2(x0, y + (bh - cap_fs) * 0.5f), g_pal->faint,
                  ("Back: " + chord).c_str());
}

// ---------------------------------------------------------------- the race strip
void draw_strip(App::State& s, ImVec2 a, ImVec2 b, const pr::Result& res) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float k = tok::appear(ImGui::GetID("##pair-strip"), ui::tokens::kSlowMs, 1000.0);
    const float lift = (1.0f - k) * px(ui::tokens::kSlideDistance);
    a.y -= lift;
    b.y -= lift;
    with_alpha(dl, k, [&] { tok::card(dl, a, b, tok::radius_card(), 1); });
    const float pad = tok::gap_l();
    const ImU32 win = g_pal->done;
    float x = a.x + pad;
    const float row1 = a.y + pad;
    draw_flag(dl, ImVec2(x, row1 + px(1)), px(18), res.winner ? win : g_pal->dim);
    x += px(18) + tok::gap_s();
    const std::string head = pr::headline(res, side_title(s, Side::kLeft), side_title(s, Side::kRight));
    const float tw = tok::text(s, dl, ui::tokens::kTitle, ImVec2(x, row1), g_pal->text, head.c_str());
    (void)tw;
    // Close (x) at the top right.
    const float cb = px(22);
    if (icon_button(s, "##pair-dismiss", ui::icon::kClose, ImVec2(b.x - pad - cb + px(4), row1 - px(2)), cb,
                    fpx(13), "Hide until the next round", g_pal->dim))
        run_command(s, "pair.dismiss");
    // Row 2: each side's time, changes, Review and Keep.
    const float row2 = row1 + px(30);
    const float half = (b.x - a.x - pad * 2 - tok::gap_l()) * 0.5f;
    const float bh = px(26);
    for (const Side side : kSides) {
        const float sx = a.x + pad + (side == Side::kLeft ? 0.0f : half + tok::gap_l());
        const float cy = row2 + bh * 0.5f;
        ImGui::PushID(static_cast<int>(pr::index(side)));
        float cx = sx;
        const bool won = res.winner && *res.winner == side;
        const char* letter = side == Side::kLeft ? "A" : "B";
        {
            const ImVec2 t = tok::measure(s, ui::tokens::kCaption, letter);
            const ImU32 bc = won ? win : g_pal->dim;
            dl->AddCircleFilled(ImVec2(cx + px(10), cy), px(10), alpha(bc, 0x30));
            tok::text(s, dl, ui::tokens::kCaption, ImVec2(cx + px(10) - t.x * 0.5f, cy - t.y * 0.5f), bc,
                      letter);
            cx += px(20) + tok::gap_s();
        }
        const std::string time = pr::format_duration(res.elapsed[pr::index(side)]);
        cx += tok::text(s, dl, ui::tokens::kBody, ImVec2(cx, cy - tok::size(ui::tokens::kBody) * 0.62f),
                        won ? g_pal->text : g_pal->dim, time.c_str()) +
              tok::gap_m();
        // Changes from the git cache.
        const ui::SessionView* v = view_of(s, s.pair.model.id(side));
        if (v != nullptr) {
            const GitInfo g = s.git.with_changes(v->cwd, machine_of_id(s, s.pair.model.id(side)));
            const std::string files =
                g.repo ? (g.changed_files < 0
                              ? std::string("...")
                              : std::to_string(g.changed_files) + (g.changed_files == 1 ? " file" : " files"))
                       : std::string("not git");
            cx += tok::text(s, dl, ui::tokens::kCaption,
                            ImVec2(cx, cy - tok::size(ui::tokens::kCaption) * 0.62f), g_pal->dim,
                            files.c_str()) +
                  tok::gap_s();
            if (g.repo && g.changed_files > 0) {
                const std::string add = "+" + std::to_string(g.lines_added);
                const std::string del = "-" + std::to_string(g.lines_removed);
                cx += tok::text(s, dl, ui::tokens::kCaption,
                                ImVec2(cx, cy - tok::size(ui::tokens::kCaption) * 0.62f), g_pal->done,
                                add.c_str()) +
                      tok::gap_xs();
                cx += tok::text(s, dl, ui::tokens::kCaption,
                                ImVec2(cx, cy - tok::size(ui::tokens::kCaption) * 0.62f), g_pal->needs_you,
                                del.c_str());
            }
        }
        // Buttons at the right end of the half.
        float rx = sx + half;
        const bool kept = s.pair.kept && *s.pair.kept == side;
        const char* keep = kept ? "Kept" : "Keep";
        rx -= text_button_width(s, keep);
        if (text_button(s, dl, "##keep", keep, ImVec2(rx, row2), bh, true, kept ? tok::selected_edge() : 0))
            run_command(s, std::string("pair.keep:") + pr::side_name(side));
        if (kept)
            draw_icon(s, dl, ui::icon::kCheck, fpx(12), ImVec2(rx - px(10), cy), tok::selected_edge());
        rx -= tok::gap_s() + (kept ? px(16) : 0.0f);
        const char* review = "Review";
        rx -= text_button_width(s, review);
        if (text_button(s, dl, "##review", review, ImVec2(rx, row2), bh))
            run_command(s, std::string("pair.review:") + pr::side_name(side));
        ImGui::PopID();
    }
}

// ---------------------------------------------------------------- a side with no terminal
void draw_gone_side(App::State& s, Side side, ImVec2 a, ImVec2 b) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(a, b, g_pal->term_bg, tok::radius_card());
    dl->AddRect(a, b, g_pal->border, tok::radius_card());
    ImGui::PushID(side == Side::kLeft ? "gone-l" : "gone-r");
    StateCard c;
    c.art = tok::Art::kTerminal;
    c.title = side == Side::kLeft ? "The left terminal ended" : "The right terminal ended";
    c.hint = "Pick another agent for this side, or close the pair.";
    c.title_size = ui::tokens::kTitle.size;
    c.buttons.push_back({"Choose an agent...", "", [&s, side] { s.pair.repair_menu = side; }});
    c.buttons.push_back({"Close pair", "pair.close", nullptr});
    draw_state_card(s, a, ImVec2(b.x - a.x, b.y - a.y), c);
    ImGui::PopID();
}

// The re-pair menu: every running terminal but the side's own (the other side's swaps).
void draw_repair_menu(App::State& s) {
    if (!s.pair.repair_menu)
        return;
    const Side side = *s.pair.repair_menu;
    if (!ImGui::IsPopupOpen("##pair-repair"))
        ImGui::OpenPopup("##pair-repair");
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(5), px(5)));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::SetNextWindowSizeConstraints(ImVec2(px(260), 0), ImVec2(px(420), px(420)));
    const bool open = ImGui::BeginPopup("##pair-repair");
    ImGui::PopStyleVar(2);
    if (!open) {
        s.pair.repair_menu.reset();
        return;
    }
    const ImVec2 wp = ImGui::GetWindowPos(), wsz = ImGui::GetWindowSize();
    tok::window_shadow(ImGui::GetWindowDrawList(), wp, ImVec2(wp.x + wsz.x, wp.y + wsz.y),
                       ImGui::GetStyle().PopupRounding, 1);
    menu_keys();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const std::string title = std::string("Use for the ") + pr::side_name(side) + " side";
    ImGui::Dummy(ImVec2(0, px(2)));
    const ImVec2 tp = ImGui::GetCursorScreenPos();
    tok::text(s, dl, ui::tokens::kCaption, ImVec2(tp.x + px(10), tp.y), g_pal->faint, title.c_str());
    ImGui::Dummy(ImVec2(0, tok::size(ui::tokens::kCaption) * 1.3f + px(4)));
    // Agents first (the sidebar's order), then the other terminals.
    std::vector<std::string> ids;
    for (const auto& id : agent_roster(s).ids)
        if (alive(s, id))
            ids.push_back(id);
    for (const auto& sess : s.sessions)
        if (alive(s, sess->id()) && std::find(ids.begin(), ids.end(), sess->id()) == ids.end() &&
            !s.hidden.contains(sess->id()))
            ids.push_back(sess->id());
    const std::string& mine = s.pair.model.id(side);
    int shown = 0;
    for (const auto& id : ids) {
        if (id == mine)
            continue;
        const ui::SessionView* v = view_of(s, id);
        if (v == nullptr)
            continue;
        ++shown;
        const ImVec2 ra = ImGui::GetCursorScreenPos();
        std::string label = s.pane_title(id);
        if (s.pair.model.id(pr::other(side)) == id)
            label += "  (swap sides)";
        ImGui::PushID(id.c_str());
        if (menu_row(s, nullptr, label.c_str(), display_path(v->cwd)))
            run_command(s, std::string("pair.repair:") + pr::side_name(side) + ":" + id);
        ImGui::PopID();
        program_icon(dl, program_of(*v), ImVec2(ra.x + px(8), ra.y + px(8.5f)), px(14), g_pal->dim);
    }
    if (shown == 0)
        menu_row(s, nullptr, "No other terminals are running", {}, false);
    ImGui::EndPopup();
}

// ---------------------------------------------------------------- pair-stage
void draw_stage(App::State& s, ImVec2 pos, ImVec2 size) {
    auto& P = s.pair;
    const pr::Phase ph = P.model.phase();
    if (ph == pr::Phase::kSetup || ph == pr::Phase::kStarting) {
        draw_setup(s, pos, size);
        return;
    }
    tok::forget(ImGui::GetID("##pair-setup"));
    const bool connected = s.host->connected();
    // Coming back to Pair puts the keys in the left terminal (once).
    if (!P.shown) {
        for (const Side side : kSides)
            if (alive(s, P.model.id(side))) {
                s.focus(P.model.id(side));
                break;
            }
    }
    P.shown = true;
    float top = pos.y;
    const auto res = P.race.result();
    if (res && !P.dismissed) {
        draw_strip(s, ImVec2(pos.x, top), ImVec2(pos.x + size.x, top + px(kStripH)), *res);
        top += px(kStripH) + tok::gap_s();
    } else {
        tok::forget(ImGui::GetID("##pair-strip"));
    }
    const float gap = tok::gap_s();
    const float w = (size.x - gap) * 0.5f;
    const float h = pos.y + size.y - top;
    if (w < px(40) || h < px(40))
        return;
    s.pane_area = {pos.x, top, size.x, h};
    s.pane_rects.clear();
    const ui::styles::PaneLook look = ui::styles::pane_look("pair", nullptr);
    for (const Side side : kSides) {
        const ui::Rect r{pos.x + (side == Side::kLeft ? 0.0f : w + gap), top, w, h};
        const std::string& id = P.model.id(side);
        if (alive(s, id) && s.by_id.contains(id)) {
            s.pane_rects[id] = r;
            draw_one_pane(s, id, r, connected, look);
        } else {
            draw_gone_side(s, side, ImVec2(r.x, r.y), ImVec2(r.x + r.w, r.y + r.h));
        }
    }
    draw_repair_menu(s);
}

// ---------------------------------------------------------------- pair-prompt (60 px)
void draw_prompt(App::State& s, ImVec2 pos, float w, float h) {
    auto& P = s.pair;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 end(pos.x + w, pos.y + h);
    dl->AddRectFilled(pos, end, g_pal->sidebar);
    tok::hline(dl, pos.x, end.x, pos.y, g_pal->border);
    const pr::Phase ph = P.model.phase();
    const bool left_ok =
        (ph == pr::Phase::kRunning || ph == pr::Phase::kHalf) && alive(s, P.model.id(Side::kLeft));
    const bool right_ok =
        (ph == pr::Phase::kRunning || ph == pr::Phase::kHalf) && alive(s, P.model.id(Side::kRight));
    const bool any = left_ok || right_ok;
    const float bh = px(34);
    const float cy = pos.y + h * 0.5f;
    const float pad = tok::gap_m();
    float right = end.x - pad;
    const std::string both = std::string("Send to both  ") + kEnterMark;
    const float bw = accent_button_width(s, both);
    right -= bw;
    const bool has_text = P.prompt[0] != 0;
    bool send_both = accent_button(s, dl, "##pair-both", both, ImVec2(right, cy - bh * 0.5f), bh,
                                   left_ok && right_ok && has_text);
    right -= tok::gap_s();
    const char* ro = "Right only";
    right -= text_button_width(s, ro);
    const bool send_right =
        text_button(s, dl, "##pair-right", ro, ImVec2(right, cy - bh * 0.5f), bh, right_ok && has_text);
    right -= tok::gap_s();
    const char* lo = "Left only";
    right -= text_button_width(s, lo);
    const bool send_left =
        text_button(s, dl, "##pair-left", lo, ImVec2(right, cy - bh * 0.5f), bh, left_ok && has_text);
    right -= tok::gap_m();

    // The field: a rounded box; it only takes the keyboard when clicked or asked for (pair.prompt).
    const ImVec2 fa(pos.x + pad, cy - bh * 0.5f), fb(right, cy + bh * 0.5f);
    const ImU32 field = mix(g_pal->sidebar, g_pal->bg, 0.55f);
    dl->AddRectFilled(fa, fb, field, tok::radius_small());
    const float ring = tok::fade(ImGui::GetID("##pair-ring"), P.prompt_active, ui::tokens::kFastMs);
    dl->AddRect(fa, fb, mix(g_pal->border, tok::focus_ring(field), ring), tok::radius_small(), 0,
                ring > 0.5f ? 1.5f : 1.0f);
    const float fs = tok::size(ui::tokens::kBody);
    ImGui::SetCursorScreenPos(ImVec2(fa.x + tok::gap_m(), cy - (fs + px(8)) * 0.5f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, 0);
    ImGui::PushStyleColor(ImGuiCol_Text, v4(g_pal->text));
    ImGui::PushStyleColor(ImGuiCol_TextDisabled, v4(g_pal->faint));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, px(4)));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::SetNextItemWidth(std::max(px(40), fb.x - fa.x - tok::gap_m() * 2));
    if (P.prompt_focus && any) {
        ImGui::SetKeyboardFocusHere();
        P.prompt_focus = false;
    }
    ImGui::BeginDisabled(!any);
    const char* hint = any ? "Type a task for both agents..." : "Start a pair to send the same task to both";
    const bool enter = ImGui::InputTextWithHint("##pair-prompt", hint, P.prompt, sizeof P.prompt,
                                                ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::EndDisabled();
    P.prompt_active = ImGui::IsItemActive();
    if (ImGui::IsItemDeactivated() && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        // Esc hands the keys back to the left terminal.
        if (left_ok)
            s.focus_view(P.model.id(Side::kLeft));
    }
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(3);
    if (enter && P.prompt[0] != 0) {
        send_both = left_ok && right_ok;
        if (!send_both)  // one side only: Enter sends to the one there is
            run_command(s, left_ok ? "pair.send:left" : "pair.send:right");
        P.prompt_focus = true;  // keep typing the next one
    }
    if (send_both)
        run_command(s, "pair.send:both");
    else if (send_left)
        run_command(s, "pair.send:left");
    else if (send_right)
        run_command(s, "pair.send:right");
}

}  // namespace

// ---------------------------------------------------------------- the pane frame and header
StyledPane draw_pair_pane(App::State& s, const std::string& id, const ui::SessionView& v, ImVec2 pos,
                          ImVec2 max, bool focused) {
    auto& P = s.pair;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float radius = tok::radius_card();
    const auto side = P.model.side_of(id);
    dl->AddRectFilled(pos, max, g_pal->term_bg, radius);
    const float hh = px(kHeaderH);
    const ImU32 head_bg = mix(g_pal->term_bg, g_pal->sidebar, 0.6f);
    dl->AddRectFilled(pos, ImVec2(max.x, pos.y + hh), head_bg, radius, ImDrawFlags_RoundCornersTop);
    tok::hline(dl, pos.x, max.x, pos.y + hh - 1.0f, tok::divider(head_bg));
    // While the shared prompt bar has the keys no terminal looks focused.
    const float ring =
        tok::fade(ImGui::GetID("##pair-focus"), focused && !P.prompt_active, ui::tokens::kFastMs);
    dl->AddRect(pos, max, mix(g_pal->border, g_pal->focus, ring), radius, 0, 1.0f);
    // The finish line, when this side crossed it first.
    const bool first = side && P.finish_side && *P.finish_side == *side;
    if (first) {
        const float k =
            tok::reduce_motion() ? 1.0f : tok::appear(ImGui::GetID("##pair-finish"), 600.0, 1000.0);
        draw_finish_band(dl, pos, max.x - pos.x, k, g_pal->done, radius);
    } else {
        tok::forget(ImGui::GetID("##pair-finish"));
    }
    const float cy = pos.y + hh * 0.5f + (first ? px(2) : 0.0f);
    float x = pos.x + tok::gap_m();
    // A, B: which side, in a small circle.
    if (side) {
        const char* letter = *side == Side::kLeft ? "A" : "B";
        const ImVec2 t = tok::measure(s, ui::tokens::kCaption, letter);
        dl->AddCircleFilled(ImVec2(x + px(9), cy), px(9), alpha(g_pal->text, 0x18));
        tok::text(s, dl, ui::tokens::kCaption, ImVec2(x + px(9) - t.x * 0.5f, cy - t.y * 0.5f), g_pal->dim,
                  letter);
        x += px(18) + tok::gap_s();
    }
    const ui::Program prog = program_of(v);
    program_icon(dl, prog, ImVec2(x, cy - px(8)), px(16), g_pal->text);
    x += px(16) + tok::gap_s();
    const float fs = tok::size(ui::tokens::kBody);
    const float cap = tok::size(ui::tokens::kCaption);
    const std::string title = s.pane_title(id);
    const Attention att = s.attention_of(id);
    const bool marked = v.process_state == "Running" && att != Attention::kNone && att != Attention::kIdle;
    const float mark_w = marked ? status_mark(s, dl, 0, cy, att, false, true) + tok::gap_s() : 0.0f;
    // The title keeps up to 150 px (and its state mark) before anything optional is shown.
    const float title_w = s.fonts.ui_medium->CalcTextSizeA(fs, FLT_MAX, 0, title.c_str()).x;
    const float need = std::min(title_w, px(150)) + tok::gap_s() + mark_w;

    // Right to left: Change (always), then what fits, most telling first: the finish pill, Kept,
    // the turn's time, the changes.
    float right = max.x - tok::gap_s();
    const float bh = px(22);
    {
        const char* change = "Change";
        right -= text_button_width(s, change, ui::tokens::kCaption);
        if (side && text_button(s, dl, "##pair-change", change, ImVec2(right, cy - bh * 0.5f), bh, true,
                                g_pal->dim, ui::tokens::kCaption))
            s.pair.repair_menu = *side;
        right -= tok::gap_m();
    }
    const GitInfo g = s.git.with_changes(v.cwd, machine_of_id(s, id));
    std::string files, add, del;
    float changes_w = 0;
    if (g.repo) {
        files = g.changed_files < 0 ? std::string("...")
                : g.changed_files == 0
                    ? std::string("no changes")
                    : std::to_string(g.changed_files) + (g.changed_files == 1 ? " file" : " files");
        changes_w = tok::measure(s, ui::tokens::kCaption, files.c_str()).x;
        if (g.changed_files > 0) {
            add = "+" + std::to_string(g.lines_added);
            del = "-" + std::to_string(g.lines_removed);
            changes_w += tok::gap_s() + tok::measure(s, ui::tokens::kCaption, del.c_str()).x + tok::gap_xs() +
                         tok::measure(s, ui::tokens::kCaption, add.c_str()).x;
        }
    }
    std::string time;
    bool time_done = false;
    float time_w = 0;
    const char* finish = nullptr;
    float finish_w = 0, kept_w = 0;
    const bool kept = side && P.kept && *P.kept == *side;
    if (side) {
        const pr::Runner& r = P.race.runner(*side);
        if (r.started_ms != 0) {
            time = pr::format_duration(r.elapsed(epoch_ms()));
            time_done = r.finished();
            time_w = tok::measure(s, ui::tokens::kCaption, time.c_str()).x + px(18);
        }
        if (first) {
            finish = P.race.result() ? "Won" : "Finished first";
            finish_w = pill(s, dl, 0, cy, finish, g_pal->done, true);
        }
        if (kept)
            kept_w = pill(s, dl, 0, cy, "Kept", tok::selected_edge(), true);
    }
    float room = right - x - need;
    auto take = [&](float w) {
        if (w <= 0 || w + tok::gap_m() > room)
            return false;
        room -= w + tok::gap_m();
        return true;
    };
    const bool show_finish = take(finish_w);
    const bool show_kept = take(kept_w);
    const bool show_time = take(time_w);
    const bool show_changes = take(changes_w);
    if (show_changes) {
        right -= tok::measure(s, ui::tokens::kCaption, files.c_str()).x;
        tok::text(s, dl, ui::tokens::kCaption, ImVec2(right, cy - cap * 0.62f), g_pal->faint, files.c_str());
        if (!add.empty()) {
            right -= tok::gap_s() + tok::measure(s, ui::tokens::kCaption, del.c_str()).x;
            tok::text(s, dl, ui::tokens::kCaption, ImVec2(right, cy - cap * 0.62f), g_pal->needs_you,
                      del.c_str());
            right -= tok::gap_xs() + tok::measure(s, ui::tokens::kCaption, add.c_str()).x;
            tok::text(s, dl, ui::tokens::kCaption, ImVec2(right, cy - cap * 0.62f), g_pal->done, add.c_str());
        }
        right -= tok::gap_m();
    }
    if (show_time) {
        right -= time_w - px(18);
        tok::text(s, dl, ui::tokens::kCaption, ImVec2(right, cy - cap * 0.62f),
                  time_done ? g_pal->text : g_pal->dim, time.c_str());
        draw_icon(s, dl, ui::icon::kClock, fpx(12), ImVec2(right - px(9), cy), g_pal->faint);
        right -= px(18) + tok::gap_m();
    }
    if (show_kept) {
        right -= kept_w;
        pill(s, dl, right, cy, "Kept", tok::selected_edge());
        right -= tok::gap_s();
    }
    if (show_finish) {
        right -= finish_w;
        pill(s, dl, right, cy, finish, g_pal->done);
        right -= tok::gap_s();
    }
    // Left: the pane's name, then the state.
    const float tw = text_clipped(dl, s.fonts.ui_medium, fs, ImVec2(x, cy - fs * 0.6f), g_pal->text, title,
                                  std::max(0.0f, right - x - mark_w));
    x += tw + tok::gap_s();
    if (marked && x + mark_w - tok::gap_s() <= right)
        status_mark(s, dl, x, cy, att, false);
    // A press on the header (not a button) puts the keys in this terminal.
    if (ImGui::IsMouseHoveringRect(pos, ImVec2(max.x, pos.y + hh)) &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemHovered() &&
        ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)) {
        s.ws.focus(id);
        s.views[id].focus_next = true;
    }
    return {ImVec2(pos.x + tok::gap_m(), pos.y + hh + tok::gap_xs()),
            ImVec2(max.x - tok::gap_m(), max.y - tok::gap_s())};
}

void pair_layout_preview(ImDrawList* d, ImVec2 a, ImVec2 size) {
    const ImVec2 b(a.x + size.x, a.y + size.y);
    d->AddRectFilled(a, b, g_pal->bg, px(6));
    d->AddRectFilled(a, ImVec2(b.x, a.y + px(8)), g_pal->sidebar, px(6), ImDrawFlags_RoundCornersTop);
    const float top = a.y + px(11), bottom = b.y - px(13);
    const float w = (size.x - px(11)) * 0.5f;
    for (int i = 0; i < 2; ++i) {
        const ImVec2 p(a.x + px(4) + static_cast<float>(i) * (w + px(3)), top);
        const ImVec2 q(p.x + w, bottom);
        d->AddRectFilled(p, q, g_pal->term_bg, px(3));
        d->AddRect(p, q, g_pal->border, px(3));
        for (int k = 0; k < 3; ++k)
            d->AddRectFilled(
                ImVec2(p.x + px(3), p.y + px(4) + static_cast<float>(k) * px(5)),
                ImVec2(p.x + px(3) + w * (k == 1 ? 0.4f : 0.6f), p.y + px(6) + static_cast<float>(k) * px(5)),
                k == 2 && i == 0 ? g_pal->done : alpha(g_pal->dim, 0xA0), px(1));
    }
    // A checkered band on the left pane: it finished first.
    for (int c = 0; c < static_cast<int>(w / px(2)); ++c)
        if (c % 2 == 0)
            d->AddRectFilled(ImVec2(a.x + px(4) + static_cast<float>(c) * px(2), top),
                             ImVec2(a.x + px(4) + static_cast<float>(c + 1) * px(2), top + px(2)),
                             g_pal->done);
    // The shared prompt bar.
    d->AddRectFilled(ImVec2(a.x + px(4), b.y - px(10)), ImVec2(b.x - px(16), b.y - px(4)), g_pal->card,
                     px(2));
    d->AddRect(ImVec2(a.x + px(4), b.y - px(10)), ImVec2(b.x - px(16), b.y - px(4)), g_pal->border, px(2));
    d->AddRectFilled(ImVec2(b.x - px(14), b.y - px(10)), ImVec2(b.x - px(4), b.y - px(4)), g_pal->focus,
                     px(2));
    d->AddRect(a, b, g_pal->border, px(6));
}

// ---------------------------------------------------------------- hooks
bool draw_pair_block(App::State& s, const std::string& mod, ImVec2 pos, ImVec2 size) {
    if (mod == "pair-bar")
        draw_bar(s, pos, size.x, size.y);
    else if (mod == "pair-stage")
        draw_stage(s, pos, size);
    else if (mod == "pair-prompt")
        draw_prompt(s, pos, size.x, size.y);
    else
        return false;
    s.pair.drawn_frame = ImGui::GetFrameCount();
    return true;
}

void place_pair_side(App::State& s, const Placement& p) {
    auto& P = s.pair;
    const auto side = pr::parse_side(p.pair_side).value_or(Side::kLeft);
    // Next to the other side when it is already placed, else in a new tab named Pair.
    const std::string other =
        P.model.phase() == pr::Phase::kStarting ? P.model.id(pr::other(side)) : std::string();
    bool placed = false;
    if (!other.empty() && s.ws.find(other).space >= 0)
        placed = s.ws.split(other, p.session, side == Side::kLeft ? Dir::kLeft : Dir::kRight);
    if (!placed) {
        int sp = -1;
        for (int i = 0; i < static_cast<int>(s.ws.spaces.size()); ++i)
            if (s.ws.spaces[static_cast<std::size_t>(i)].machine == p.machine &&
                (sp < 0 || i == s.ws.active_space))
                sp = i;
        if (sp >= 0) {
            const int t = s.ws.add_tab(p.session, sp);
            s.ws.spaces[static_cast<std::size_t>(sp)].tabs[static_cast<std::size_t>(t)].name = "Pair";
        } else {
            s.ws.add_space(p.session);
            s.ws.spaces.back().machine = p.machine;
            s.ws.spaces.back().tabs.front().name = "Pair";
        }
    }
    s.doc_changed();
    P.model.placed(side, p.session);
    if (P.model.phase() == pr::Phase::kRunning) {
        P.starting_note.clear();
        remember(s);
        s.focus(P.model.id(Side::kLeft));
    }
}

void pair_tick(App::State& s) {
    auto& P = s.pair;
    if (P.drawn_frame < ImGui::GetFrameCount() - 1)
        P.shown = false;  // not on screen: the next time it shows, the left side gets the keys
    // The pair from ui.json, once the host has listed its sessions.
    if (!P.restored && s.host->connected() && !s.sessions.empty()) {
        P.restored = true;
        const auto& ps = s.settings.pair;
        if (P.model.phase() == pr::Phase::kSetup && !ps.left.empty() && !ps.right.empty()) {
            if (alive(s, ps.left) && alive(s, ps.right))
                P.model.adopt(ps.left, ps.right);
            else
                remember(s);  // its terminals are gone
        }
    }
    {
        std::vector<std::string> errors;
        {
            std::lock_guard lock(s.placements_mutex);
            errors.swap(P.errors);
        }
        for (const auto& e : errors) {
            P.model.failed(e);
            s.toast("Could not start the pair", e, "", g_pal->needs_you);
        }
    }
    if (P.model.phase() == pr::Phase::kStarting && Clock::now() - P.starting_since > std::chrono::minutes(11))
        P.model.failed("agent-host did not answer");
    if (P.model.phase() != pr::Phase::kRunning && P.model.phase() != pr::Phase::kHalf)
        return;
    std::vector<std::string> live;
    for (const Side side : kSides)
        if (alive(s, P.model.id(side)))
            live.push_back(P.model.id(side));
    const pr::Phase before = P.model.phase();
    P.model.prune(live);
    if (P.model.phase() != before || P.model.phase() == pr::Phase::kSetup)
        remember(s);
    // The race, from each side's structured state.
    const long long now = epoch_ms();
    bool working = false;
    for (const Side side : kSides) {
        const ui::SessionView* v = view_of(s, P.model.id(side));
        if (v == nullptr)
            continue;
        const bool had_result = P.race.result().has_value();
        P.race.observe(side, v->agent_state, v->state_at_ms, now);
        if (had_result && !P.race.result())
            new_round(s);  // Working again after both finished: a new round
        auto& last = P.last_state[pr::index(side)];
        if (last != v->agent_state) {
            last = v->agent_state;
            s.git.invalidate(v->cwd);  // a turn began or ended: its changes moved
        }
        working = working || v->agent_state == "Working";
    }
    if (working && Clock::now() - P.counts_at > std::chrono::seconds(3)) {
        P.counts_at = Clock::now();
        for (const Side side : kSides)
            if (const ui::SessionView* v = view_of(s, P.model.id(side)))
                s.git.invalidate(v->cwd);
    }
    // The finish line goes to the first side to finish; it stays for the round.
    if (const auto f = P.race.first_finished(); f && !P.finish_side)
        P.finish_side = f;
    if (!P.race.active())
        P.finish_side.reset();
}

std::optional<std::string> exec_pair_command(App::State& s, const std::string& name, const std::string& arg) {
    auto& P = s.pair;
    if (name == "pair.toggle")
        return toggle_layout(s, "pair");
    if (name == "pair.start") {
        if (arg == "existing")
            return pair_existing(s);
        const auto comma = arg.find(',');
        if (!arg.empty() && comma == std::string::npos)
            return std::string("pair.start needs <left profile>,<right profile>");
        return start_pair(s, comma == std::string::npos ? std::string() : arg.substr(0, comma),
                          comma == std::string::npos ? std::string() : arg.substr(comma + 1));
    }
    const bool paired = P.model.phase() == pr::Phase::kRunning || P.model.phase() == pr::Phase::kHalf;
    if (name == "pair.send") {
        // "<both|left|right>" sends the bar's text; "<target>:<text>" that text.
        const auto colon = arg.find(':');
        const auto target = pr::parse_target(arg.substr(0, colon));
        if (!target)
            return std::string("pair.send needs both, left or right");
        if (!paired)
            return std::string("no pair yet");
        return send_prompt(s, *target, colon == std::string::npos ? std::string() : arg.substr(colon + 1));
    }
    if (name == "pair.prompt") {
        if (s.layout().style != "pair")
            toggle_layout(s, "pair");
        P.prompt_focus = true;
        return std::string();
    }
    if (name == "pair.swap") {
        if (!P.model.swap())
            return std::string("no pair yet");
        P.race.swap();
        if (P.kept)
            P.kept = pr::other(*P.kept);
        if (P.finish_side)
            P.finish_side = pr::other(*P.finish_side);
        std::swap(P.last_state[0], P.last_state[1]);
        // The tab shows them the other way round too.
        const std::string& l = P.model.id(Side::kLeft);
        const std::string& r = P.model.id(Side::kRight);
        const auto ll = s.ws.find(l), rl = s.ws.find(r);
        if (ll.space >= 0 && ll.space == rl.space && ll.tab == rl.tab) {
            s.ws.move_pane(l, r, std::nullopt);
            s.doc_changed();
        }
        remember(s);
        return std::string();
    }
    if (name == "pair.repair") {
        // "<side>" opens the menu; "<side>:<session>" puts that session there.
        const auto colon = arg.find(':');
        const auto side = pr::parse_side(arg.substr(0, colon));
        if (!side)
            return std::string("pair.repair needs left or right");
        if (!paired)
            return std::string("no pair yet");
        if (colon == std::string::npos) {
            if (s.layout().style != "pair")
                toggle_layout(s, "pair");
            P.repair_menu = *side;
            return std::string();
        }
        const std::string id = arg.substr(colon + 1);
        if (!alive(s, id))
            return "no running terminal " + id;
        const bool swapping = P.model.id(pr::other(*side)) == id;
        if (!P.model.repair(*side, id))
            return std::string();
        if (swapping) {
            P.race.swap();
        } else {
            P.race.reset();
            new_round(s);
        }
        remember(s);
        s.focus(id);
        return std::string();
    }
    if (name == "pair.close") {
        P.model.close();
        P.race.reset();
        new_round(s);
        remember(s);
        return std::string();
    }
    if (name == "pair.keep") {
        const auto side = pr::parse_side(arg);
        if (!side)
            return std::string("pair.keep needs left or right");
        P.kept = P.kept && *P.kept == *side ? std::nullopt : side;  // again: unmark
        return std::string();
    }
    if (name == "pair.review") {
        const auto side = pr::parse_side(arg);
        if (!side)
            return std::string("pair.review needs left or right");
        const std::string& id = P.model.id(*side);
        if (!alive(s, id))
            return std::string("that side has no terminal");
        s.focus(id);
        s.review.target = id;
        return toggle_layout(s, "review");
    }
    if (name == "pair.dismiss") {
        P.dismissed = true;
        return std::string();
    }
    return std::nullopt;
}

// ui.json "pair": {"left_profile", "right_profile", "worktrees", "left", "right"}; wrong types are
// ignored.
void read_pair_settings(const nlohmann::json& j, PairSettings& out) {
    const auto it = j.find("pair");
    if (it == j.end() || !it->is_object())
        return;
    auto str = [&](const char* k, std::string& to) {
        if (const auto f = it->find(k); f != it->end() && f->is_string())
            to = f->get<std::string>();
    };
    str("left_profile", out.left_profile);
    str("right_profile", out.right_profile);
    str("left", out.left);
    str("right", out.right);
    if (const auto f = it->find("worktrees"); f != it->end() && f->is_boolean())
        out.worktrees = f->get<bool>();
}

void write_pair_settings(nlohmann::json& j, const PairSettings& in) {
    j["pair"] = {{"left_profile", in.left_profile},
                 {"right_profile", in.right_profile},
                 {"worktrees", in.worktrees},
                 {"left", in.left},
                 {"right", in.right}};
}

}  // namespace detail
}  // namespace agentdesk::desktop
