// The AgentDesk Cockpit layout: three gauges (working, needs you, done today), the agents roster,
// a log of state changes and the active tab's panes in the middle, on a faint grid. AgentDesk's own
// drawing: lines, arcs and rectangles from the active theme, the bundled fonts and the brand mark.
#include "app_internal.hpp"

namespace agentdesk::desktop {
namespace detail {

namespace {
namespace ck = ui::cockpit;

// The cockpit's colours, worked out from the active theme (any theme works).
struct CockpitLook {
    ImU32 panel;  // block fill
    ImU32 line;   // the bright line (the theme's accent)
    ImU32 line2;  // the second line colour (the theme's info colour)
    ImU32 edge;   // a block's 1 px frame
    ImU32 grid;
    ImU32 text;
    ImU32 label;
    bool glow;  // glow and grid allowed (setting, theme, high contrast)
    bool grid_on;
};

CockpitLook cockpit_look(const App::State& s) {
    CockpitLook l{};
    l.panel = alpha(mix(g_pal->bg, g_pal->card, 0.6f), 235);
    l.line = g_pal->focus;
    l.line2 = g_pal->info;
    l.edge = mix(g_pal->border, g_pal->focus, 0.45f);
    l.grid = alpha(g_pal->focus, g_pal->light ? 46 : 36);
    l.text = g_pal->text;
    l.label = g_pal->dim;
    const bool hc = system_high_contrast();
    l.grid_on = s.settings.styles.cockpit_effects != "off" && !hc;
    l.glow = ui::styles::effects_allowed(s.settings.styles.cockpit_effects, g_pal->light, hc);
    return l;
}

// Upper-case text drawn a letter at a time with `spacing` between letters (ImGui has no letter
// spacing). Returns the width; with `measure` it only measures.
float spaced(ImDrawList* dl, ImFont* font, float size, ImVec2 at, ImU32 col, const std::string& text,
             float spacing, bool measure = false) {
    float x = at.x;
    for (std::size_t i = 0; i < text.size();) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        const std::size_t len = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : 4;
        const char* a = text.c_str() + i;
        const char* b = a + std::min(len, text.size() - i);
        if (!measure)
            dl->AddText(font, size, ImVec2(x, at.y), col, a, b);
        x += font->CalcTextSizeA(size, FLT_MAX, 0, a, b).x + spacing;
        i += len;
    }
    return x - at.x - (text.empty() ? 0.0f : spacing);
}

// Four L-shaped corner marks `arm` long, `thick` wide, just inside a..b.
void brackets(ImDrawList* dl, ImVec2 a, ImVec2 b, float arm, float t, ImU32 col) {
    dl->AddRectFilled(a, ImVec2(a.x + arm, a.y + t), col);  // top left
    dl->AddRectFilled(a, ImVec2(a.x + t, a.y + arm), col);
    dl->AddRectFilled(ImVec2(b.x - arm, a.y), ImVec2(b.x, a.y + t), col);  // top right
    dl->AddRectFilled(ImVec2(b.x - t, a.y), ImVec2(b.x, a.y + arm), col);
    dl->AddRectFilled(ImVec2(b.x - arm, b.y - t), b, col);  // bottom right
    dl->AddRectFilled(ImVec2(b.x - t, b.y - arm), b, col);
    dl->AddRectFilled(ImVec2(a.x, b.y - t), ImVec2(a.x + arm, b.y), col);  // bottom left
    dl->AddRectFilled(ImVec2(a.x, b.y - arm), ImVec2(a.x + t, b.y), col);
}

// A block: its fill, a quiet 1 px frame with a soft glow, bright corner marks, and its title on
// the top edge (the frame breaks behind it).
void block_frame(App::State& s, ImDrawList* dl, ImVec2 a, ImVec2 b, const char* title) {
    const CockpitLook L = cockpit_look(s);
    const float r = px(4);
    dl->AddRectFilled(a, b, L.panel, r);
    if (L.glow) {
        for (const auto& [w, al] : {std::pair{6.0f, 20}, std::pair{3.0f, 51}}) {
            const float h = px(w) * 0.5f;
            dl->AddRect(ImVec2(a.x - h, a.y - h), ImVec2(b.x + h, b.y + h), alpha(L.line, al / 2), r + h, 0,
                        px(w));
        }
    }
    dl->AddRect(a, b, L.edge, r, 0, 1.0f);
    brackets(dl, a, b, px(10), line_px(2), L.line);
    if (title != nullptr) {
        const float fs = fpx(11);
        const float tw = spaced(dl, s.fonts.ui_bold, fs, {}, 0, title, px(1), true);
        const ImVec2 ta(a.x + px(18), a.y - fs * 0.62f);
        dl->AddRectFilled(ImVec2(ta.x - px(5), a.y - px(2)), ImVec2(ta.x + tw + px(5), a.y + px(2)),
                          g_pal->bg);
        spaced(dl, s.fonts.ui_bold, fs, ta, L.label, title, px(1));
    }
}

// "repo-a/claude": the agent's space and program (the machine first for another computer's).
std::string agent_name(App::State& s, const std::string& id, const std::map<std::string, std::string>& cwds) {
    std::string space;
    const auto loc = s.ws.find(id);
    if (loc.space >= 0)
        space = s.ws.space_title(loc.space, cwds);
    const auto mi = s.machine_of.find(id);
    if (mi != s.machine_of.end() && !mi->second.empty())
        space = mi->second + ":" + space;
    const auto vi = s.views_by_id.find(id);
    const std::string prog = vi != s.views_by_id.end() ? program_of(vi->second).name : std::string("agent");
    return space.empty() ? prog : space + "/" + prog;
}

std::string today_text() {
    std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &now);
    return ck::date_text(local.tm_year + 1900, local.tm_mon + 1, local.tm_mday);
}

std::string ago(long long ms) {
    const long long m = std::max(0LL, ms) / 60000;
    if (m < 1)
        return "<1m";
    if (m < 60)
        return std::to_string(m) + "m";
    const long long h = m / 60;
    return h < 24 ? std::to_string(h) + "h" : std::to_string(h / 24) + "d";
}

long long steady_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
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

const char* state_mark(Attention a) {
    switch (a) {
        case Attention::kBlocked:
            return "[!]";
        case Attention::kWorking:
            return "[~]";
        case Attention::kDone:
            return "[+]";
        case Attention::kIdle:
            return "[.]";
        case Attention::kNone:
            break;
    }
    return "[?]";
}

// The live agents (running, reporting a state, in a pane of a space), most urgent first, then
// the latest change.
std::vector<std::string> roster_ids(App::State& s) {
    std::vector<std::string> ids;
    for (const auto& [id, v] : s.views_by_id)
        if (live_agent(s, id, v))
            ids.push_back(id);
    const auto& at = s.styles.cockpit.changed_at;
    std::stable_sort(ids.begin(), ids.end(), [&](const std::string& a, const std::string& b) {
        const int ra = ck::roster_rank(s.attention_of(a)), rb = ck::roster_rank(s.attention_of(b));
        if (ra != rb)
            return ra < rb;
        const auto ia = at.find(a), ib = at.find(b);
        return (ia != at.end() ? ia->second : 0) > (ib != at.end() ? ib->second : 0);
    });
    return ids;
}

struct Counts {
    int working = 0;
    int needs = 0;
    int agents = 0;
    std::string first_blocked;
};

Counts gauge_counts(App::State& s) {
    Counts c;
    std::vector<Attention> states;
    for (const auto& id : roster_ids(s)) {
        states.push_back(s.attention_of(id));
        if (c.first_blocked.empty() && states.back() == Attention::kBlocked)
            c.first_blocked = id;
    }
    const auto n = ui::chrome_common::count_agents(states);
    c.working = n.working;
    c.needs = n.needs;
    c.agents = static_cast<int>(states.size());
    return c;
}

// One round gauge centred on `c`, radius `r`: the base arc, the value arc in the state's colour, 12
// ticks (every third long), a short pointer, the number and the label under it.
struct GaugeSpec {
    float value = 0;  // eased
    int shown = 0;    // the number written
    float max = 4;
    ImU32 color = 0;
    const char* label = "";
    bool known = true;
    bool alert = false;  // the number in the needs-you colour with a "!" chip
};

void draw_gauge(App::State& s, ImDrawList* dl, ImVec2 c, float r, const GaugeSpec& g, bool hovered,
                bool held) {
    const CockpitLook L = cockpit_look(s);
    const float a0 = ck::gauge_angle(0, 1);
    const float a1 = ck::gauge_angle(1, 1);
    const float w = px(6);
    if (!g.known) {  // connecting: a dashed base arc
        constexpr int kDashes = 24;
        for (int i = 0; i < kDashes; i += 2) {
            dl->PathArcTo(c, r, a0 + (a1 - a0) * static_cast<float>(i) / kDashes,
                          a0 + (a1 - a0) * static_cast<float>(i + 1) / kDashes, 6);
            dl->PathStroke(alpha(L.line, 90), 0, w);
        }
    } else {
        dl->PathArcTo(c, r, a0, a1, 48);
        dl->PathStroke(alpha(L.line, hovered ? 70 : 40), 0, w);
    }
    if (g.known && g.value > 0.001f) {
        const float av = ck::gauge_angle(g.value, g.max);
        if (L.glow) {
            dl->PathArcTo(c, r, a0, av, 48);
            dl->PathStroke(alpha(g.color, 40), 0, w * 2.2f);
        }
        dl->PathArcTo(c, r, a0, av, 48);
        dl->PathStroke(g.color, 0, held ? px(8) : w);
        const ImVec2 dir(std::cos(av), std::sin(av));
        dl->AddLine(ImVec2(c.x + dir.x * (r - px(18)), c.y + dir.y * (r - px(18))),
                    ImVec2(c.x + dir.x * (r - px(6)), c.y + dir.y * (r - px(6))), g.color, px(2));
    }
    for (int i = 0; i <= 12; ++i) {
        const float a = a0 + (a1 - a0) * static_cast<float>(i) / 12.0f;
        const bool long_tick = i % 3 == 0;
        const ImVec2 dir(std::cos(a), std::sin(a));
        const float r0 = r + px(7);
        const float r1 = r0 + (long_tick ? px(7) : px(3));
        dl->AddLine(ImVec2(c.x + dir.x * r0, c.y + dir.y * r0), ImVec2(c.x + dir.x * r1, c.y + dir.y * r1),
                    alpha(L.label, long_tick ? 200 : 110), long_tick ? px(1.5f) : 1.0f);
    }
    ImFont* bold = s.fonts.mono_bold != nullptr ? s.fonts.mono_bold : s.fonts.mono;
    const std::string num = ck::gauge_number(g.shown, g.known);
    const float fs = fpx(ck::number_size(g.shown)) * std::min(1.0f, r / px(70));
    const ImVec2 t = bold->CalcTextSizeA(fs, FLT_MAX, 0, num.c_str());
    const ImU32 ink = !g.known || g.shown == 0 ? L.label : g.alert ? g_pal->needs_you : L.text;
    dl->AddText(bold, fs, ImVec2(std::round(c.x - t.x * 0.5f), std::round(c.y - t.y * 0.55f)), ink,
                num.c_str());
    const float lfs = fpx(11);
    const float lw = spaced(dl, s.fonts.ui_bold, lfs, {}, 0, g.label, px(1), true);
    const float chip = g.alert ? px(14) + px(6) : 0.0f;
    const ImVec2 la(std::round(c.x - (lw + chip) * 0.5f), c.y + r * 0.42f);
    spaced(dl, s.fonts.ui_bold, lfs, la, L.label, g.label, px(1));
    if (g.alert) {
        const ImVec2 ca(la.x + lw + px(6), la.y - px(1));
        dl->AddRectFilled(ca, ImVec2(ca.x + px(14), ca.y + px(14)), g_pal->needs_you, px(3));
        const ImVec2 et = s.fonts.ui_bold->CalcTextSizeA(lfs, FLT_MAX, 0, "!");
        dl->AddText(s.fonts.ui_bold, lfs,
                    ImVec2(ca.x + (px(14) - et.x) * 0.5f, ca.y + (px(14) - et.y) * 0.5f), IM_COL32_WHITE,
                    "!");
    }
}

// The short form for low windows: the number on the left, the label and a bar on the right.
void draw_gauge_bar(App::State& s, ImDrawList* dl, ImVec2 a, ImVec2 b, const GaugeSpec& g, bool hovered) {
    const CockpitLook L = cockpit_look(s);
    ImFont* bold = s.fonts.mono_bold != nullptr ? s.fonts.mono_bold : s.fonts.mono;
    const std::string num = ck::gauge_number(g.shown, g.known);
    const float fs = fpx(24);
    const float cy = (a.y + b.y) * 0.5f;
    const ImU32 ink = !g.known || g.shown == 0 ? L.label : g.alert ? g_pal->needs_you : L.text;
    dl->AddText(bold, fs, ImVec2(a.x + px(10), cy - fs * 0.55f), ink, num.c_str());
    const float bx = a.x + px(64);
    spaced(dl, s.fonts.ui_bold, fpx(10.5f), ImVec2(bx, cy - px(14)), L.label, g.label, px(1));
    const ImVec2 ba(bx, cy + px(4));
    const ImVec2 bb(b.x - px(12), cy + px(10));
    dl->AddRectFilled(ba, bb, alpha(L.line, hovered ? 70 : 40), px(3));
    if (g.known && g.value > 0)
        dl->AddRectFilled(ba, ImVec2(ba.x + (bb.x - ba.x) * std::clamp(g.value / g.max, 0.0f, 1.0f), bb.y),
                          g.color, px(3));
}
}  // namespace

// ------------------------------------------------------------------ tracking (every tick)
// Each agent's state is compared with the last one seen: a change goes into the log, and an agent
// that went from working to done or idle counts for today. The first sighting only remembers the
// state (an agent saved as working in ui.json and idle now finished while the window was closed:
// it counts then).
void cockpit_tick(App::State& s) {
    auto& ck_ui = s.styles.cockpit;
    auto& done = s.settings.styles.cockpit_done;
    const std::string today = today_text();
    bool save = done.roll(today);
    std::map<std::string, std::string> cwds;
    bool have_cwds = false;
    std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &now);
    for (const auto& [id, v] : s.views_by_id) {
        if (v.lost || v.process_state != "Running")
            continue;
        const Attention a = s.attention_of(id);
        const auto it = ck_ui.last.find(id);
        if (it == ck_ui.last.end()) {
            ck_ui.last[id] = a;
            ck_ui.changed_at[id] = steady_ms();
            if (s.settings.agents_changed.contains(id) && s.settings.agents_changed[id].is_object() &&
                safe::string_or(s.settings.agents_changed[id], "s") == "working" &&
                (a == Attention::kDone || a == Attention::kIdle)) {
                done.add(today);
                save = true;
            }
            continue;
        }
        if (it->second == a)
            continue;
        const Attention from = it->second;
        it->second = a;
        ck_ui.changed_at[id] = steady_ms();
        if (from == Attention::kNone || a == Attention::kNone)
            continue;  // starting up or going away: not a state change to log
        if (!have_cwds) {
            cwds = s.session_cwds();
            have_cwds = true;
        }
        ck_ui.log.push(
            {ck::time_text(local.tm_hour, local.tm_min, local.tm_sec), id, agent_name(s, id, cwds), from, a});
        if (ck::finished(from, a)) {
            done.add(today);
            save = true;
        }
    }
    if (save)
        save_settings(s.settings);
}

// ------------------------------------------------------------------ blocks
// The top row: the round buttons, the mark and "AGENTDESK / COCKPIT", the spaces' avatars, the
// active space's tabs as text chips, the clock (and Windows' buttons when chosen).
void draw_cockpit_header(App::State& s, ImVec2 pos, float w, float h) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const CockpitLook L = cockpit_look(s);
    const ImVec2 b(pos.x + w, pos.y + h);
    const float cy = std::round(pos.y + h * 0.5f);
    float x = clear_of_traffic_lights(s, pos.x + px(2), pos.y, b.y, px(16));
    draw_brand_mark(dl, ImVec2(x, cy - px(9)), px(18));
    x += px(18) + px(10);
    x += spaced(dl, s.fonts.ui_bold, fpx(11.5f), ImVec2(x, cy - fpx(11.5f) * 0.62f), L.text,
                "AGENTDESK / COCKPIT", px(1.2f)) +
         px(22);
    const auto cwds = s.session_cwds();
    for (int i = 0; i < static_cast<int>(s.ws.spaces.size()); ++i) {
        const ui::Space& sp = s.ws.spaces[static_cast<std::size_t>(i)];
        const std::string title = s.ws.space_title(i, cwds);
        const float side = px(24);
        const ImVec2 a(x, cy - side * 0.5f);
        ImGui::SetCursorScreenPos(a);
        ImGui::PushID(i);
        if (ImGui::InvisibleButton("##ck-space", ImVec2(side, side)))
            run_command(s, "space.select:" + std::to_string(i + 1));
        const bool hov = ImGui::IsItemHovered();
        if (hov)
            ImGui::SetTooltip("%s", title.c_str());
        ImGui::PopID();
        draw_space_avatar(s, dl, a, side, avatar_color(sp.color), IM_COL32_WHITE, avatar_letter(title),
                          side * 0.45f, px(6));
        if (i == s.ws.active_space)
            dl->AddRect(ImVec2(a.x - px(3), a.y - px(3)), ImVec2(a.x + side + px(3), a.y + side + px(3)),
                        L.line, px(8), 0, line_px(2));
        else if (hov)
            dl->AddRect(ImVec2(a.x - px(2), a.y - px(2)), ImVec2(a.x + side + px(2), a.y + side + px(2)),
                        L.line, px(7));
        x += side + px(8);
    }
    x += px(14);

    // Right: Windows' buttons, the clock.
    float right = b.x - px(4);
    if (s.config.hwnd != nullptr && s.settings.window_buttons == "windows") {
        draw_window_buttons(s, b.x, pos.y, h);
        right = b.x - px(26) * 3 - px(4) - px(10);
    }
    std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &now);
    std::string clock = ui::chrome_common::clock_text(local.tm_wday, local.tm_hour, local.tm_min);
    for (char& c : clock)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    const float cfs = fpx(14);
    const float cw = s.fonts.mono->CalcTextSizeA(cfs, FLT_MAX, 0, clock.c_str()).x;
    dl->AddText(s.fonts.mono, cfs, ImVec2(right - cw, cy - cfs * 0.6f), L.text, clock.c_str());
    right -= cw + px(20);

    // The active space's tabs: "1 claude · 2 pwsh", the current one in the line colour.
    if (ui::Space* sp = s.ws.active(); sp != nullptr) {
        std::string close_tab;
        dl->PushClipRect(ImVec2(x, pos.y), ImVec2(std::max(x, right), b.y), true);
        const float fs = fpx(12);
        for (int i = 0; i < static_cast<int>(sp->tabs.size()) && x < right; ++i) {
            const ui::Tab& t = sp->tabs[static_cast<std::size_t>(i)];
            const std::string label = std::to_string(i + 1) + " " + tab_label(s, t, i, s.ws.sessions_in(t));
            const float tw = s.fonts.mono->CalcTextSizeA(fs, FLT_MAX, 0, label.c_str()).x + px(16);
            const ImVec2 a(x, cy - px(11));
            ImGui::SetCursorScreenPos(a);
            ImGui::PushID(t.id.c_str());
            if (ImGui::InvisibleButton("##ck-tab", ImVec2(tw, px(22))))
                run_command(s, "tab.goto:" + std::to_string(s.ws.active_space) + ":" + std::to_string(i));
            const bool hov = ImGui::IsItemHovered();
            tab_menu(s, t, close_tab);
            ImGui::PopID();
            const bool on = i == sp->active_tab;
            if (on || hov)
                dl->AddRectFilled(a, ImVec2(a.x + tw, a.y + px(22)), on ? alpha(L.line, 38) : g_pal->hover,
                                  px(3));
            if (on)
                dl->AddRect(a, ImVec2(a.x + tw, a.y + px(22)), alpha(L.line, 150), px(3));
            dl->AddText(s.fonts.mono, fs, ImVec2(a.x + px(8), cy - fs * 0.6f),
                        t.settled ? g_pal->faint
                        : on      ? L.text
                                  : L.label,
                        label.c_str());
            x += tw + px(6);
        }
        dl->PopClipRect();
        if (!close_tab.empty())
            run_command(s, "tab.close:" + close_tab);
    }
    window_drag_area(s, pos, b);
}

// The gauges column: WORKING, NEEDS YOU and DONE TODAY, each easing toward its value; bars when
// the column is short. A click on NEEDS YOU goes to the first agent that needs you (else Go to),
// on WORKING opens Go to; DONE TODAY explains itself.
void draw_cockpit_gauges(App::State& s, ImVec2 pos, float w, float h) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 b(pos.x + w, pos.y + h);
    block_frame(s, dl, pos, b, "GAUGES");
    auto& ck_ui = s.styles.cockpit;
    const bool connected = s.host->connected();
    const Counts c = gauge_counts(s);
    const int done = s.settings.styles.cockpit_done.count;
    const float target[3] = {static_cast<float>(c.working), static_cast<float>(c.needs),
                             static_cast<float>(done)};
    const float dt = ImGui::GetIO().DeltaTime;
    bool moving = false;
    for (int i = 0; i < 3; ++i) {
        if (!ck_ui.shown_set || motion_reduced())
            ck_ui.shown[i] = target[i];
        else
            ck_ui.shown[i] = ui::chrome_common::approach(ck_ui.shown[i], target[i], dt);
        moving = moving || ck_ui.shown[i] != target[i];
    }
    ck_ui.shown_set = true;
    if (moving && s.config.wake)
        s.config.wake();  // another frame while a gauge eases

    GaugeSpec g[3];
    g[0] = {ck_ui.shown[0], c.working, static_cast<float>(ck::agents_max(c.agents)),
            g_pal->working, "WORKING", connected,
            false};
    g[1] = {ck_ui.shown[1],   c.needs,     static_cast<float>(ck::agents_max(c.agents)),
            g_pal->needs_you, "NEEDS YOU", connected,
            c.needs > 0};
    g[2] = {ck_ui.shown[2], done, static_cast<float>(ck::done_max(done)), g_pal->done, "DONE TODAY",
            true,           false};
    const bool lost = !connected && !s.machine_loading("");
    const ck::Fit fit = ck::fit(ImGui::GetMainViewport()->WorkSize.x / g_scale, h / g_scale);
    const float top = pos.y + px(14);
    const float each = (b.y - px(8) - top) / 3.0f;
    static constexpr const char* kTips[3] = {"Click to find an agent",
                                             "Click to go to the first agent that needs you",
                                             "Counted since midnight, while AgentDesk was watching"};
    for (int i = 0; i < 3; ++i) {
        const ImVec2 a(pos.x + px(8), top + each * static_cast<float>(i));
        const ImVec2 z(b.x - px(8), a.y + each);
        ImGui::SetCursorScreenPos(a);
        ImGui::PushID(i);
        const bool clicked =
            ImGui::InvisibleButton("##ck-gauge", ImVec2(z.x - a.x, std::max(1.0f, z.y - a.y)));
        const bool hov = ImGui::IsItemHovered();
        const bool held = ImGui::IsItemActive();
        ImGui::PopID();
        if (hov) {
            ImGui::SetMouseCursor(i == 2 ? ImGuiMouseCursor_Arrow : ImGuiMouseCursor_Hand);
            ImGui::SetTooltip("%s", i == 1 && c.first_blocked.empty() ? kTips[0] : kTips[i]);
        }
        if (clicked && i == 0)
            run_command(s, "app.picker");
        if (clicked && i == 1)
            run_command(
                s, c.first_blocked.empty() ? std::string("app.picker") : "agent.focus:" + c.first_blocked);
        if (fit.bars) {
            draw_gauge_bar(s, dl, a, z, g[i], hov);
        } else {
            const float r = std::min({px(70), (z.x - a.x) * 0.5f - px(20), (z.y - a.y) * 0.5f - px(14)});
            draw_gauge(s, dl, ImVec2((a.x + z.x) * 0.5f, a.y + (z.y - a.y) * 0.5f + px(4)),
                       std::max(r, px(20)), g[i], hov, held);
        }
        if (lost && i < 2) {
            const char* t = "DISCONNECTED";
            const float tw = spaced(dl, s.fonts.ui_bold, fpx(10), {}, 0, t, px(1), true);
            spaced(dl, s.fonts.ui_bold, fpx(10), ImVec2(std::round((a.x + z.x - tw) * 0.5f), z.y - px(18)),
                   g_pal->needs_you, t, px(1));
        }
    }
}

// The agents: two lines each (state mark, space/program; state and how long ago), a state bar
// on the left; the focused agent's bar is wider and its row raised. A click goes to the agent; a
// right-click offers Peek and Keep.
void draw_cockpit_roster(App::State& s, ImVec2 pos, float w, float h) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 b(pos.x + w, pos.y + h);
    block_frame(s, dl, pos, b, "AGENTS");
    const CockpitLook L = cockpit_look(s);
    const auto ids = roster_ids(s);
    const ImVec2 in_a(pos.x + px(6), pos.y + px(14));
    const ImVec2 in_size(w - px(12), h - px(20));
    if (in_size.x <= 0 || in_size.y <= 0)
        return;
    if (ids.empty()) {
        const char* t = s.host->connected() ? "No agents running" : "--";
        const ImVec2 ts = s.fonts.ui->CalcTextSizeA(fpx(12.5f), FLT_MAX, 0, t);
        dl->AddText(s.fonts.ui, fpx(12.5f), ImVec2(pos.x + (w - ts.x) * 0.5f, pos.y + h * 0.45f), L.label, t);
        return;
    }
    ImGui::SetCursorScreenPos(in_a);
    ImGui::BeginChild("##ck-roster", in_size, ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
    ImDrawList* cl = ImGui::GetWindowDrawList();
    const auto cwds = s.session_cwds();
    const long long now = steady_ms();
    const std::string focused = s.ws.focused_session();
    std::string open_menu_for;
    for (const auto& id : ids) {
        const auto& v = s.views_by_id[id];
        const Attention a = s.attention_of(id);
        const ImVec2 ra = ImGui::GetCursorScreenPos();
        const float rw = ImGui::GetContentRegionAvail().x;
        const float rh = px(40);
        ImGui::PushID(id.c_str());
        const bool clicked = ImGui::InvisibleButton("##row", ImVec2(rw, rh));
        const bool hov = ImGui::IsItemHovered();
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
            ImGui::OpenPopup("##ck-agent");
        if (ImGui::BeginPopup("##ck-agent")) {
            menu_keys();
            if (menu_row(s, ui::icon::kSearch, "Peek"))
                run_command(s, "agent.peek:" + id);
            if (menu_row(s, ui::icon::kTerminal, "Keep in a pane"))
                run_command(s, "agent.keep:" + id);
            ImGui::EndPopup();
        }
        ImGui::PopID();
        const bool on = id == focused;
        const ImVec2 rb(ra.x + rw, ra.y + rh - px(2));
        if (on)
            cl->AddRectFilled(ra, rb, g_pal->selected, px(3));
        else if (hov)
            cl->AddRectFilled(ra, rb, g_pal->hover, px(3));
        cl->AddRectFilled(ra, ImVec2(ra.x + (on ? px(5) : px(3)), rb.y), state_color(a), px(1));
        const bool remote_lost = !s.machine_connected(s.machine_of[id]);
        const ImU32 ink = v.lost || remote_lost ? g_pal->faint : L.text;
        const float x0 = ra.x + px(12);
        ImFont* mono = s.fonts.mono;
        const std::string line1 = std::string(state_mark(a)) + " " + agent_name(s, id, cwds);
        text_clipped(cl, mono, fpx(12), ImVec2(x0, ra.y + px(4)), ink, line1, rw - px(18));
        std::string word = a == Attention::kNone ? "unknown" : ck::state_word(a);
        for (char& c : word)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (v.lost)
            word = "lost";
        const auto ci = s.styles.cockpit.changed_at.find(id);
        const std::string when =
            ci != s.styles.cockpit.changed_at.end() ? ago(now - ci->second) : std::string();
        cl->AddText(mono, fpx(11), ImVec2(x0 + px(30), ra.y + px(21)), v.lost ? g_pal->faint : L.label,
                    word.c_str());
        const float ww = mono->CalcTextSizeA(fpx(11), FLT_MAX, 0, when.c_str()).x;
        cl->AddText(mono, fpx(11), ImVec2(ra.x + rw - px(8) - ww, ra.y + px(21)), L.label, when.c_str());
        if (clicked)
            run_command(s, "agent.focus:" + id);
    }
    ImGui::EndChild();
}

// The log: newest first, "HH:MM:SS  space/program  OLD → NEW"; a click goes to the agent.
void draw_cockpit_log(App::State& s, ImVec2 pos, float w, float h) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 b(pos.x + w, pos.y + h);
    block_frame(s, dl, pos, b, "LOG");
    const CockpitLook L = cockpit_look(s);
    const auto& items = s.styles.cockpit.log.items();
    const ImVec2 in_a(pos.x + px(8), pos.y + px(12));
    const ImVec2 in_size(w - px(16), h - px(18));
    if (in_size.x <= 0 || in_size.y <= 0)
        return;
    if (items.empty()) {
        dl->AddText(s.fonts.mono, fpx(12), ImVec2(in_a.x + px(4), in_a.y + px(2)), L.label, "Nothing yet");
        return;
    }
    ImGui::SetCursorScreenPos(in_a);
    ImGui::BeginChild("##ck-log", in_size, ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
    ImDrawList* cl = ImGui::GetWindowDrawList();
    ImFont* mono = s.fonts.mono;
    const float fs = fpx(12);
    int n = 0;
    for (const auto& e : items) {
        const ImVec2 ra = ImGui::GetCursorScreenPos();
        const float rw = ImGui::GetContentRegionAvail().x;
        ImGui::PushID(n++);
        const bool clicked = ImGui::InvisibleButton("##ev", ImVec2(rw, px(18)));
        if (ImGui::IsItemHovered())
            cl->AddRectFilled(ra, ImVec2(ra.x + rw, ra.y + px(18)), g_pal->hover, px(2));
        ImGui::PopID();
        float x = ra.x + px(4);
        const float ty = ra.y + (px(18) - fs) * 0.5f;
        cl->AddText(mono, fs, ImVec2(x, ty), L.label, e.time.c_str());
        x += mono->CalcTextSizeA(fs, FLT_MAX, 0, "00:00:00").x + px(14);
        const float who_w = std::min(px(170), rw * 0.4f);
        text_clipped(cl, mono, fs, ImVec2(x, ty), L.text, e.who, who_w - px(8));
        x += who_w;
        cl->AddText(mono, fs, ImVec2(x, ty), state_color(e.from), ck::state_word(e.from));
        x += mono->CalcTextSizeA(fs, FLT_MAX, 0, ck::state_word(e.from)).x + px(6);
        cl->AddText(mono, fs, ImVec2(x, ty), L.label, "\xE2\x86\x92");
        x += mono->CalcTextSizeA(fs, FLT_MAX, 0, "\xE2\x86\x92").x + px(6);
        cl->AddText(mono, fs, ImVec2(x, ty), state_color(e.to), ck::state_word(e.to));
        if (clicked && s.views_by_id.count(e.agent) != 0)
            run_command(s, "agent.focus:" + e.agent);
    }
    ImGui::EndChild();
}

// Under everything: the 24 px grid over the whole window.
void draw_cockpit_backdrop(App::State& s) {
    const CockpitLook L = cockpit_look(s);
    if (!L.grid_on)
        return;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const ImVec2 a = vp->WorkPos;
    const ImVec2 b(a.x + vp->WorkSize.x, a.y + vp->WorkSize.y);
    const float step = px(24);
    for (float x = a.x + std::fmod(step * 0.5f, step); x < b.x; x += step)
        dl->AddLine(ImVec2(std::round(x), a.y), ImVec2(std::round(x), b.y), L.grid);
    for (float y = a.y + std::fmod(step * 0.5f, step); y < b.y; y += step)
        dl->AddLine(ImVec2(a.x, std::round(y)), ImVec2(b.x, std::round(y)), L.grid);
}

// Over the panes: corner marks on the focused pane.
void draw_cockpit_overlay(App::State& s) {
    const ui::Tab* tab = s.ws.active_tab();
    if (tab == nullptr)
        return;
    const std::string id = s.ws.focused_session();
    ui::Rect r = s.pane_area;
    if (tab->zoomed.empty()) {
        const auto it = s.pane_rects.find(id);
        if (it == s.pane_rects.end())
            return;
        r = it->second;
    }
    brackets(ImGui::GetWindowDrawList(), ImVec2(r.x, r.y), ImVec2(r.x + r.w, r.y + r.h), px(12), line_px(2),
             cockpit_look(s).line);
}

// A narrow window drops the roster, then the log (not saved: they come back when it widens).
std::optional<lay::Node> cockpit_layout_tree(const App::State& s, const lay::Node& tree) {
    (void)s;
    const ck::Fit fit = ck::fit(ImGui::GetMainViewport()->WorkSize.x / g_scale, 1000.0f);
    if (fit.roster && fit.log)
        return std::nullopt;
    lay::Node t = tree;
    for (const lay::Node* n : tree.walk()) {
        const std::string m = n->mod();
        if ((m == "cockpit-roster" && !fit.roster) || (m == "cockpit-log" && !fit.log))
            if (lay::Node* x = t.find(n->id))
                x->hidden = true;
    }
    return t;
}

// Toasts stay above the log's newest row.
float cockpit_toast_offset(const App::State& s) {
    for (const lay::Node* n : s.layout().tree.walk())
        if (n->mod() == "cockpit-log")
            if (const lay::Placed* p = s.placed.at(n->id)) {
                const ImGuiViewport* vp = ImGui::GetMainViewport();
                return std::max(0.0f, (vp->WorkPos.y + vp->WorkSize.y - p->outer.y) / g_scale - 16.0f + 6.0f);
            }
    return 0.0f;
}

// ------------------------------------------------------------------ Settings
// The card: a dark grid, three small arcs on the left, a framed box in the middle and a few lines
// on the right.
void cockpit_layout_preview(ImDrawList* d, ImVec2 a, ImVec2 size) {
    const ImVec2 b(a.x + size.x, a.y + size.y);
    const ImU32 bg = g_pal->light ? g_pal->bg : mix(g_pal->bg, IM_COL32(0, 0, 0, 255), 0.25f);
    d->AddRectFilled(a, b, bg, px(6));
    d->PushClipRect(a, b, true);
    for (float x = a.x + px(6); x < b.x; x += px(8))
        d->AddLine(ImVec2(x, a.y), ImVec2(x, b.y), alpha(g_pal->focus, 30));
    for (float y = a.y + px(6); y < b.y; y += px(8))
        d->AddLine(ImVec2(a.x, y), ImVec2(b.x, y), alpha(g_pal->focus, 30));
    d->PopClipRect();
    const float top = a.y + px(9);
    const ImU32 cols[3] = {g_pal->working, g_pal->needs_you, g_pal->done};
    const float each = (b.y - px(4) - top) / 3.0f;
    for (int i = 0; i < 3; ++i) {
        const ImVec2 c(a.x + px(12), top + each * (static_cast<float>(i) + 0.55f));
        const float r = std::min(px(6), each * 0.45f);
        d->PathArcTo(c, r, ck::gauge_angle(0, 1), ck::gauge_angle(1, 1), 16);
        d->PathStroke(alpha(g_pal->focus, 70), 0, px(1.5f));
        d->PathArcTo(c, r, ck::gauge_angle(0, 1), ck::gauge_angle(0.4f + 0.2f * static_cast<float>(i), 1),
                     12);
        d->PathStroke(cols[i], 0, px(1.5f));
    }
    const ImVec2 ma(a.x + px(24), top);
    const ImVec2 mb(b.x - px(30), b.y - px(14));
    d->AddRectFilled(ma, mb, g_pal->term_bg, px(1.5f));
    d->AddRect(ma, mb, g_pal->focus, px(1.5f));
    d->AddRectFilled(ImVec2(ma.x, mb.y + px(3)), ImVec2(mb.x, b.y - px(4)), alpha(g_pal->card, 230), px(1));
    d->AddRect(ImVec2(ma.x, mb.y + px(3)), ImVec2(mb.x, b.y - px(4)), alpha(g_pal->focus, 120), px(1));
    for (int i = 0; i < 5; ++i)
        d->AddRectFilled(
            ImVec2(b.x - px(26), top + px(2) + static_cast<float>(i) * px(7)),
            ImVec2(b.x - px(6) - (i % 2 ? px(6) : 0.0f), top + px(4) + static_cast<float>(i) * px(7)),
            i == 0 ? g_pal->needs_you : alpha(g_pal->text, 120), px(1));
    d->AddRect(a, b, g_pal->border, px(6));
}

bool look_cockpit_options(App::State& s) {
    auto& st = s.settings.styles;
    settings_row(s, "Glow and grid", "The background grid and the soft glow on lines. Never on light themes.",
                 px(36), px(20));
    const bool on = st.cockpit_effects != "off";
    if (settings_toggle("##cockpit-effects", on)) {
        st.cockpit_effects = on ? "off" : "full";
        settings_row_done(s);
        return true;
    }
    settings_row_done(s);
    return false;
}

std::optional<std::string> exec_cockpit_command(App::State& s, const std::string& name) {
    if (name != "cockpit.reset_today")
        return std::nullopt;
    s.settings.styles.cockpit_done = {today_text(), 0};
    save_settings(s.settings);
    s.toast("Done today: back to 0", "", "", g_pal->info, 2);
    return std::string();
}

}  // namespace detail
}  // namespace agentdesk::desktop
