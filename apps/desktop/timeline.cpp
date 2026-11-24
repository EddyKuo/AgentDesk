// The Timeline: a large overlay (and a "timeline" layout block) with one row per agent and its
// working / needs-you / done / idle stretches over the last 15 minutes to 24 hours, drawn from
// each agent-host's state history (session.timeline), with the time the window was away shaded.
#include "agentdesk/ui/timeline.hpp"

#include "overlay_kit.hpp"

namespace agentdesk::desktop {
namespace detail {

namespace {
namespace tl = ui::timeline;

constexpr auto kRefresh = std::chrono::seconds(15);

long long utc_offset_ms() {
    return os::local_time().utc_offset * 60LL * 1000LL;
}

std::vector<std::string> machine_names(App::State& s) {
    std::vector<std::string> names = {std::string()};
    for (const auto& r : s.remotes)
        names.push_back(r.cfg.name);
    return names;
}

// The shown range's right edge (now, or where the person panned to) and its length.
long long view_end(const App::State& s) {
    return s.timeline.end_ms > 0 ? s.timeline.end_ms : now_ms();
}

// ---------------------------------------------------------------- data
// Asks each machine's agent-host for its timeline when the last answer is 15 s old or covered a
// shorter range; answers land in the inbox (HostClient worker threads) for the UI thread.
void request(App::State& s) {
    // Ask a little wider than shown (clocks differ between machines); the host keeps a day.
    const long long since = view_end(s) - tl::range_ms(s.settings.timeline_range) - 10LL * 60 * 1000;
    for (const auto& m : machine_names(s)) {
        auto& tm = s.timeline.machines[m];
        if (tm.asked)
            continue;
        const bool stale = Clock::now() - tm.fetched >= kRefresh;
        const bool wider = since < tm.fetched_since;  // a longer range, or panned further back
        if (tm.answered && !stale && !wider)
            continue;
        ui::HostClient* h = s.host_for(m);
        if (h == nullptr)
            continue;
        tm.asked = true;
        tm.fetched = Clock::now();
        tm.fetched_since = since;
        auto inbox = s.timeline.inbox;
        h->call("session.timeline", {{"since_ms", since - tm.offset_ms}},
                [inbox, m](Json result, std::string error) {
                    std::lock_guard lock(inbox->mutex);
                    inbox->answers.emplace_back(m, std::move(result), std::move(error), now_ms());
                });
    }
}

// Takes the answers in; keeps what a failed machine had (it is drawn hatched from then on).
void take_answers(App::State& s) {
    std::vector<std::tuple<std::string, Json, std::string, long long>> answers;
    {
        std::lock_guard lock(s.timeline.inbox->mutex);
        answers.swap(s.timeline.inbox->answers);
    }
    for (auto& [machine, result, error, at] : answers) {
        auto& tm = s.timeline.machines[machine];
        tm.asked = false;
        if (!error.empty()) {
            tm.error = error;
            tm.unsupported = error.find("unknown method") != std::string::npos;
            tm.answered = true;
            continue;
        }
        tm.error.clear();
        tm.unsupported = false;
        tm.answered = true;
        tm.last_ok_ms = at;
        const long long host_now =
            result.is_object() && result.contains("now_ms") && result["now_ms"].is_number_integer()
                ? result["now_ms"].get<long long>()
                : at;
        const long long offset = at - host_now;  // this computer minus the host
        tm.offset_ms = offset;
        tm.sessions.clear();
        if (const auto it = result.find("sessions"); it != result.end() && it->is_array())
            for (const auto& j : *it)
                if (auto hs = tl::parse_session(j, offset))
                    tm.sessions.push_back(std::move(*hs));
    }
}

// Between answers, the states the sessions report now (session.state) are added as marks, so
// the bars move on without asking again.
void follow_live_states(App::State& s) {
    for (auto& [machine, tm] : s.timeline.machines) {
        for (auto& hs : tm.sessions) {
            const auto it = s.views_by_id.find(hs.id);
            if (it == s.views_by_id.end() || hs.marks.empty())
                continue;
            const auto mi = s.machine_of.find(hs.id);
            if (mi == s.machine_of.end() || mi->second != machine)
                continue;
            const auto& v = it->second;
            const std::string process = v.lost ? std::string("Lost") : v.process_state;
            const auto& last = hs.marks.back();
            if (last.agent == v.agent_state && last.process == process)
                continue;
            // When the host says it changed (session.state "at"), in this computer's clock; an
            // older host does not say: now.
            long long at = now_ms();
            if (v.state_at_ms > 0)
                at = std::min(at, v.state_at_ms + tm.offset_ms);
            at = std::max(at, last.at_ms);
            hs.marks.push_back({at, v.agent_state, process});
            if (process == "Exited" || process == "Failed" || process == "Lost")
                hs.ended = true;
        }
    }
}

// ---------------------------------------------------------------- rows
struct RowData {
    std::string machine;
    const tl::HostSession* session = nullptr;
    std::vector<tl::Segment> segs;
    int depth = 0;
    bool needs_you = false;
    bool live = false;  // a pane can be focused
};

bool ever_an_agent(const tl::HostSession& hs) {
    return std::any_of(hs.marks.begin(), hs.marks.end(),
                       [](const tl::Mark& m) { return m.agent != "Unknown"; });
}

std::vector<RowData> build_rows(App::State& s, const std::string& machine, long long from, long long to) {
    const auto& tm = s.timeline.machines[machine];
    std::vector<RowData> data;
    std::vector<tl::RowInput> order;
    for (const auto& hs : tm.sessions) {
        if (!ever_an_agent(hs))
            continue;  // a plain shell: no status to draw
        RowData r;
        r.machine = machine;
        r.session = &hs;
        r.segs = tl::segments(hs.marks, from, to);
        const bool visible = std::any_of(r.segs.begin(), r.segs.end(), [](const tl::Segment& g) {
            return g.kind != tl::Kind::kExited && g.kind != tl::Kind::kUnknown;
        });
        const bool exit_in_range = std::any_of(r.segs.begin(), r.segs.end(), [&](const tl::Segment& g) {
            return g.kind == tl::Kind::kExited && g.from_ms > from;
        });
        if (!visible && !exit_in_range)
            continue;  // nothing happened in the range
        const auto& last = hs.marks.back();
        r.needs_you = !hs.ended && last.agent == "Blocked";
        const auto v = s.views_by_id.find(hs.id);
        r.live = !hs.ended && v != s.views_by_id.end() && !v->second.lost;
        order.push_back({hs.id, hs.parent, last.at_ms, r.needs_you});
        data.push_back(std::move(r));
    }
    std::vector<RowData> out;
    for (const auto& row : tl::rows(order)) {
        for (auto& d : data)
            if (d.session->id == row.id) {
                d.depth = std::min(row.depth, 1);
                out.push_back(d);
            }
    }
    return out;
}

// ---------------------------------------------------------------- drawing helpers
ImU32 kind_color(tl::Kind k) {
    switch (k) {
        case tl::Kind::kWorking:
            return g_pal->working;
        case tl::Kind::kBlocked:
        case tl::Kind::kError:
            return g_pal->needs_you;
        case tl::Kind::kDone:
            return g_pal->done;
        case tl::Kind::kIdle:
            return g_pal->idle;
        default:
            return g_pal->faint;
    }
}

// Ink on a needs-you bar: white or the background, whichever stands out more.
ImU32 ink_on(ImU32 fill) {
    const auto lum = [](ImU32 c) {
        const float r = static_cast<float>((c >> IM_COL32_R_SHIFT) & 0xFF) / 255.0f;
        const float g = static_cast<float>((c >> IM_COL32_G_SHIFT) & 0xFF) / 255.0f;
        const float b = static_cast<float>((c >> IM_COL32_B_SHIFT) & 0xFF) / 255.0f;
        return 0.2126f * r + 0.7152f * g + 0.0722f * b;
    };
    return lum(fill) > 0.55f ? g_pal->bg : IM_COL32(255, 255, 255, 255);
}

// 45-degree stripes over [a, b].
void hatch(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 col) {
    if (b.x <= a.x || b.y <= a.y)
        return;
    dl->PushClipRect(a, b, true);
    const float h = b.y - a.y;
    for (float x = a.x - h; x < b.x; x += px(6))
        dl->AddLine(ImVec2(x, b.y), ImVec2(x + h, a.y), col, px(1));
    dl->PopClipRect();
}

// The sample a legend item and a bar of `k` share, drawn in [a, a + size].
void sample(App::State& s, ImDrawList* dl, tl::Kind k, ImVec2 a, ImVec2 size) {
    const float cy = a.y + size.y * 0.5f;
    const ImU32 c = kind_color(k);
    switch (k) {
        case tl::Kind::kWorking:
            dl->AddRectFilled(ImVec2(a.x, cy - px(4)), ImVec2(a.x + size.x, cy + px(4)), c, px(3));
            break;
        case tl::Kind::kBlocked: {
            dl->AddRectFilled(ImVec2(a.x, cy - px(5)), ImVec2(a.x + size.x, cy + px(5)), c, px(3));
            const ImVec2 t = s.fonts.ui_bold->CalcTextSizeA(fpx(9), FLT_MAX, 0, "!");
            dl->AddText(s.fonts.ui_bold, fpx(9), ImVec2(a.x + (size.x - t.x) * 0.5f, cy - t.y * 0.5f),
                        ink_on(c), "!");
            break;
        }
        case tl::Kind::kDone:
            draw_icon(s, dl, ui::icon::kCheck, fpx(12), ImVec2(a.x + size.x * 0.5f, cy), c);
            break;
        case tl::Kind::kIdle:
            dl->AddLine(ImVec2(a.x, cy), ImVec2(a.x + size.x, cy), c, px(2));
            break;
        case tl::Kind::kError:
            dl->AddRectFilled(ImVec2(a.x, cy - px(5)), ImVec2(a.x + size.x, cy + px(5)), c, px(3));
            hatch(dl, ImVec2(a.x, cy - px(5)), ImVec2(a.x + size.x, cy + px(5)), alpha(g_pal->bg, 0x90));
            break;
        case tl::Kind::kExited: {
            const ImVec2 t = s.fonts.ui_medium->CalcTextSizeA(fpx(12), FLT_MAX, 0, "\xC3\x97");
            dl->AddText(s.fonts.ui_medium, fpx(12), ImVec2(a.x + (size.x - t.x) * 0.5f, cy - t.y * 0.55f),
                        g_pal->dim, "\xC3\x97");
            break;
        }
        case tl::Kind::kUnknown:
            dl->AddLine(ImVec2(a.x, cy), ImVec2(a.x + size.x, cy), g_pal->faint, px(1));
            break;
    }
}

// A small segmented control (26 high): returns the chosen key when it changed.
std::optional<std::string> range_buttons(App::State& s, ImDrawList* dl, ImVec2 a, const std::string& value) {
    static constexpr const char* kKeys[] = {"15m", "1h", "4h", "24h"};
    float x = a.x + px(2);
    const float h = px(26);
    float total = px(4);
    for (const char* k : kKeys)
        total += s.fonts.ui_medium->CalcTextSizeA(fpx(12), FLT_MAX, 0, k).x + px(20);
    dl->AddRectFilled(a, ImVec2(a.x + total, a.y + h), raised_color(), px(7));
    std::optional<std::string> picked;
    for (const char* k : kKeys) {
        const float w = s.fonts.ui_medium->CalcTextSizeA(fpx(12), FLT_MAX, 0, k).x + px(20);
        ImGui::SetCursorScreenPos(ImVec2(x, a.y + px(2)));
        ImGui::PushID(k);
        const bool clicked = ImGui::InvisibleButton("##range", ImVec2(w, h - px(4)));
        const bool hov = ImGui::IsItemHovered();
        ImGui::PopID();
        const bool on = value == k;
        if (on || hov)
            dl->AddRectFilled(ImVec2(x, a.y + px(2)), ImVec2(x + w, a.y + h - px(2)),
                              on ? g_pal->selected : g_pal->hover, px(6));
        text_centered(dl, s.fonts.ui_medium, fpx(12), ImVec2(x, a.y + px(2)), ImVec2(w, h - px(4)),
                      on ? g_pal->text : g_pal->dim, k);
        if (clicked && !on)
            picked = k;
        x += w;
    }
    return picked;
}

// ---------------------------------------------------------------- the body
struct Geometry {
    ImVec2 a, size;
    float label_w = 0;
    float track_x0 = 0, track_x1 = 0;
    long long from = 0, to = 0;
    [[nodiscard]] float x_of(long long t) const {
        const double f = static_cast<double>(t - from) / static_cast<double>(std::max(1LL, to - from));
        return track_x0 + static_cast<float>(f) * (track_x1 - track_x0);
    }
};

struct BodyResult {
    std::string focus;  // agent.focus target clicked
};

std::string range_words(const std::string& r) {
    if (r == "15m")
        return "15 minutes";
    if (r == "4h")
        return "4 hours";
    if (r == "24h")
        return "24 hours";
    return "hour";
}

void draw_axis(App::State& s, ImDrawList* dl, const Geometry& g, float y, float h, long long offset) {
    const float w = g.track_x1 - g.track_x0;
    for (const auto& t : tl::ticks(g.from, g.to, w, offset)) {
        const float x = g.track_x0 + t.x;
        dl->AddLine(ImVec2(x, y + h - px(5)), ImVec2(x, y + h), g_pal->border);
        const ImVec2 sz = s.fonts.ui->CalcTextSizeA(fpx(11), FLT_MAX, 0, t.label.c_str());
        if (x + sz.x * 0.5f < g.track_x1 - px(30))
            dl->AddText(s.fonts.ui, fpx(11), ImVec2(x - sz.x * 0.5f, y + (h - sz.y) * 0.5f - px(2)),
                        g_pal->faint, t.label.c_str());
    }
    if (s.timeline.end_ms == 0) {
        const char* now = "now";
        const ImVec2 sz = s.fonts.ui_medium->CalcTextSizeA(fpx(11), FLT_MAX, 0, now);
        dl->AddText(s.fonts.ui_medium, fpx(11),
                    ImVec2(g.track_x1 - sz.x - px(4), y + (h - sz.y) * 0.5f - px(2)), g_pal->focus, now);
    }
    dl->AddLine(ImVec2(g.a.x, y + h), ImVec2(g.a.x + g.size.x, y + h), g_pal->border);
}

// One row's bars; returns the hovered segment's tooltip ("" when none) and sets `clicked`.
std::string draw_bars(App::State& s, ImDrawList* dl, const Geometry& g, const RowData& r, float y, float h,
                      long long offset, bool& clicked) {
    const float cy = y + h * 0.5f;
    const bool child = r.depth > 0;
    const float bh = px(child ? 8.0f : 12.0f);
    // The baseline from the first recorded moment (no state is a line, not a guess).
    if (!r.segs.empty())
        dl->AddLine(ImVec2(g.x_of(r.segs.front().from_ms), cy), ImVec2(g.x_of(r.segs.back().to_ms), cy),
                    alpha(g_pal->faint, 0x80), px(1));
    const double ppm =
        static_cast<double>(g.track_x1 - g.track_x0) / static_cast<double>(std::max(1LL, g.to - g.from));
    const auto packed = tl::pack(r.segs, ppm, px(2));
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    std::string tip;
    for (const auto& seg : packed) {
        float x0 = g.x_of(seg.from_ms);
        float x1 = std::max(x0 + px(2), g.x_of(seg.to_ms));
        x1 = std::min(x1, g.track_x1);
        const ImVec2 a(x0, cy - bh * 0.5f);
        const ImVec2 b(x1, cy + bh * 0.5f);
        const ImU32 c = kind_color(seg.kind);
        bool drawn = true;
        switch (seg.kind) {
            case tl::Kind::kWorking:
                dl->AddRectFilled(a, b, c, px(3));
                break;
            case tl::Kind::kBlocked:
                dl->AddRectFilled(a, b, c, px(3));
                if (x1 - x0 >= px(14)) {
                    const ImVec2 t = s.fonts.ui_bold->CalcTextSizeA(fpx(10), FLT_MAX, 0, "!");
                    dl->AddText(s.fonts.ui_bold, fpx(10), ImVec2((x0 + x1 - t.x) * 0.5f, cy - t.y * 0.5f),
                                ink_on(c), "!");
                }
                break;
            case tl::Kind::kError:
                dl->AddRectFilled(a, b, c, px(3));
                hatch(dl, a, b, alpha(g_pal->bg, 0x90));
                break;
            case tl::Kind::kIdle:
                dl->AddLine(ImVec2(x0, cy), ImVec2(x1, cy), c, px(2));
                break;
            case tl::Kind::kDone:
                dl->AddLine(ImVec2(x0, cy), ImVec2(x1, cy), alpha(c, 0x90), px(2));
                draw_icon(s, dl, ui::icon::kCheck, fpx(child ? 10.0f : 12.0f), ImVec2(x0 + px(5), cy), c);
                break;
            case tl::Kind::kExited: {
                const ImVec2 t = s.fonts.ui_medium->CalcTextSizeA(fpx(12), FLT_MAX, 0, "\xC3\x97");
                dl->AddText(s.fonts.ui_medium, fpx(12), ImVec2(x0 - t.x * 0.5f, cy - t.y * 0.55f), g_pal->dim,
                            "\xC3\x97");
                x1 = x0 + px(4);
                break;
            }
            case tl::Kind::kUnknown:
                drawn = false;
                break;
        }
        const bool hov = mouse.x >= x0 - px(1) && mouse.x < std::max(x1, x0 + px(4)) && mouse.y >= y &&
                         mouse.y < y + h && ImGui::IsWindowHovered();
        if (!hov)
            continue;
        if (drawn && seg.kind != tl::Kind::kExited)
            dl->AddRect(ImVec2(a.x - px(1), a.y - px(1)), ImVec2(b.x + px(1), b.y + px(1)), g_pal->text,
                        px(3), 0, px(1));
        std::string label = tl::kind_label(seg.kind);
        if (seg.kind == tl::Kind::kUnknown && !seg.last_known.empty())
            label += " (last: " + seg.last_known + ")";
        tip = label + " \xC2\xB7 " + tl::clock_text(seg.from_ms, offset) + "\xE2\x80\x93" +
              tl::clock_text(seg.to_ms, offset) + " (" + tl::duration_text(seg.to_ms - seg.from_ms) + ")";
        if (seg.merged > 1)
            tip += " \xC2\xB7 " + std::to_string(seg.merged) + " changes";
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            clicked = true;
    }
    return tip;
}

void draw_label(App::State& s, ImDrawList* dl, const RowData& r, ImVec2 a, float w, float h, bool narrow) {
    const auto& hs = *r.session;
    const auto v = s.views_by_id.find(hs.id);
    const bool child = r.depth > 0;
    float x = a.x + px(14) + (child ? px(16) : 0.0f);
    const float cy = a.y + h * 0.5f;
    if (child) {
        dl->AddText(s.fonts.ui, fpx(11), ImVec2(x - px(14), cy - fpx(11) * 0.6f), g_pal->faint,
                    "\xE2\x86\xB3");
    }
    const ui::Program prog =
        v != s.views_by_id.end() ? program_of(v->second) : program_of_profile({}, hs.profile);
    const float icon = px(child ? 14.0f : 16.0f);
    program_icon(dl, prog, ImVec2(x, cy - icon * 0.5f), icon, g_pal->text);
    x += icon + px(8);
    std::string name = v != s.views_by_id.end() ? s.title_of(v->second) : hs.name;
    if (name.empty())
        name = !hs.program.empty() ? hs.program : prog.name;
    std::string detail;
    if (v != s.views_by_id.end()) {
        if (const auto loc = s.ws.find(hs.id); loc.space >= 0)
            detail = s.ws.space_title(loc.space, s.session_cwds());
        if (const std::string task = agent_detail(s, hs.id, v->second); !task.empty() && task != name)
            detail += (detail.empty() ? "" : " \xC2\xB7 ") + task;
    }
    if (hs.ended)
        detail = detail.empty() ? std::string("ended") : detail + " \xC2\xB7 ended";
    const float room = a.x + w - x - px(8);
    const bool two = !child && !narrow && !detail.empty();
    const float ny = two ? cy - fpx(12.5f) * 1.05f : cy - fpx(12.5f) * 0.6f;
    text_clipped(dl, s.fonts.ui_medium, fpx(child ? 12.0f : 12.5f), ImVec2(x, ny),
                 r.live ? g_pal->text : g_pal->dim, name, room);
    if (two)
        text_clipped(dl, s.fonts.ui, fpx(11), ImVec2(x, cy + px(1)), g_pal->dim, detail, room);
}

// The rows and everything around them. In the overlay `header` is true (title row, range,
// "Since you left", counts and Esc); a layout block has none.
BodyResult draw_body(App::State& s, ImVec2 a, ImVec2 size, bool header, bool& close) {
    BodyResult out;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const long long offset = utc_offset_ms();
    const bool narrow = size.x < px(640);
    Geometry g;
    g.a = a;
    g.size = size;
    g.label_w = narrow ? px(140) : px(220);
    g.track_x0 = a.x + g.label_w + px(8);
    g.track_x1 = a.x + size.x - px(18);
    g.to = view_end(s);
    g.from = g.to - tl::range_ms(s.settings.timeline_range);
    // Rows of every machine (a header row per machine when there is more than one).
    const auto machines = machine_names(s);
    std::vector<std::pair<std::string, std::vector<RowData>>> groups;
    int agents = 0, need = 0;
    for (const auto& m : machines) {
        auto rows = build_rows(s, m, g.from, g.to);
        for (const auto& r : rows) {
            agents += r.depth == 0 ? 1 : 0;
            need += r.needs_you ? 1 : 0;
        }
        groups.emplace_back(m, std::move(rows));
    }
    float y = a.y;
    if (header) {
        const float hh = px(48);
        const float cy = y + hh * 0.5f;
        float x = a.x + px(18);
        const ImVec2 t = s.fonts.ui_bold->CalcTextSizeA(fpx(15), FLT_MAX, 0, "Timeline");
        dl->AddText(s.fonts.ui_bold, fpx(15), ImVec2(x, cy - t.y * 0.5f), g_pal->text, "Timeline");
        x += t.x + px(18);
        if (const auto r = range_buttons(s, dl, ImVec2(x, cy - px(13)), s.settings.timeline_range))
            run_command(s, "timeline.range:" + *r);
        x += px(4);
        for (const char* k : {"15m", "1h", "4h", "24h"})
            x += s.fonts.ui_medium->CalcTextSizeA(fpx(12), FLT_MAX, 0, k).x + px(20);
        x += px(16);
        // "Since you left": a check box.
        {
            const char* label = "Since you left";
            const ImVec2 lt = s.fonts.ui->CalcTextSizeA(fpx(12.5f), FLT_MAX, 0, label);
            const float box = px(14);
            ImGui::SetCursorScreenPos(ImVec2(x, cy - px(11)));
            if (ImGui::InvisibleButton("##sinceleft", ImVec2(box + px(8) + lt.x, px(22)))) {
                s.settings.timeline_since_left = !s.settings.timeline_since_left;
                save_settings(s.settings);
            }
            const bool hov = ImGui::IsItemHovered();
            const ImVec2 ba(x, cy - box * 0.5f);
            if (s.settings.timeline_since_left) {
                dl->AddRectFilled(ba, ImVec2(ba.x + box, ba.y + box), g_pal->focus, px(3));
                draw_icon(s, dl, ui::icon::kCheck, fpx(11), ImVec2(ba.x + box * 0.5f, cy),
                          accent_foreground());
            } else {
                dl->AddRect(ba, ImVec2(ba.x + box, ba.y + box), hov ? g_pal->text : g_pal->dim, px(3), 0,
                            px(1.2f));
            }
            dl->AddText(s.fonts.ui, fpx(12.5f), ImVec2(x + box + px(8), cy - lt.y * 0.5f), g_pal->text,
                        label);
            x += box + px(8) + lt.x;
        }
        // Right: Esc, then the counts.
        const ImVec2 e = s.fonts.ui->CalcTextSizeA(fpx(11), FLT_MAX, 0, "Esc");
        const ImVec2 cs(e.x + px(12), e.y + px(4));
        const ImVec2 ea(a.x + size.x - px(18) - cs.x, cy - cs.y * 0.5f);
        ImGui::SetCursorScreenPos(ea);
        if (ImGui::InvisibleButton("##tl-esc", cs))
            close = true;
        const bool eh = ImGui::IsItemHovered();
        if (eh)
            dl->AddRectFilled(ea, ImVec2(ea.x + cs.x, ea.y + cs.y), g_pal->hover, px(5));
        dl->AddText(s.fonts.ui, fpx(11), ImVec2(ea.x + px(6), ea.y + px(2)), eh ? g_pal->text : g_pal->faint,
                    "Esc");
        std::string counts = std::to_string(agents) + (agents == 1 ? " agent" : " agents");
        if (need > 0)
            counts += " \xC2\xB7 " + std::to_string(need) + " need" + (need == 1 ? "s" : "") + " you";
        const ImVec2 ct = s.fonts.ui->CalcTextSizeA(fpx(12), FLT_MAX, 0, counts.c_str());
        if (ea.x - px(14) - ct.x > x + px(12))
            dl->AddText(s.fonts.ui, fpx(12), ImVec2(ea.x - px(14) - ct.x, cy - ct.y * 0.5f),
                        need > 0 ? g_pal->needs_you : g_pal->dim, counts.c_str());
        y += hh;
        dl->AddLine(ImVec2(a.x, y), ImVec2(a.x + size.x, y), g_pal->border);
    }
    // Axis.
    const float axis_h = px(28);
    const float axis_y = y;
    draw_axis(s, dl, g, y, axis_h, offset);
    dl->AddLine(ImVec2(a.x + g.label_w, axis_y), ImVec2(a.x + g.label_w, axis_y + axis_h), g_pal->border);
    y += axis_h;
    const float legend_h = px(28);
    const ImVec2 body_a(a.x, y);
    const ImVec2 body_size(size.x, std::max(px(40), a.y + size.y - legend_h - y));
    // States of the whole body: not connected, loading, nothing yet.
    const auto& local = s.timeline.machines[""];
    const bool any_rows =
        std::any_of(groups.begin(), groups.end(), [](const auto& gr) { return !gr.second.empty(); });
    const bool loading = !local.answered;
    if (!s.host->connected() && !any_rows) {
        StateCard card;
        card.kind = CardKind::kError;
        card.title = "agent-host not connected";
        card.title_size = 15.0f;
        card.hint = "The timeline comes from agent-host, which keeps it while AgentDesk is closed.";
        draw_state_card(s, body_a, body_size, card);
    } else if (loading && !any_rows) {
        for (int i = 0; i < 4; ++i) {
            const float ry = body_a.y + static_cast<float>(i) * px(32);
            draw_skeleton_rows(dl, ImVec2(a.x + px(14), ry + px(12)), g.label_w - px(28), {0.70f}, px(32), px(8));
            draw_skeleton_rows(dl, ImVec2(g.track_x0, ry + px(12)),
                               (g.track_x1 - g.track_x0) * (0.5f + 0.1f * static_cast<float>(i)), {1.0f}, px(32),
                               px(8));
        }
    } else if (!any_rows && std::none_of(groups.begin(), groups.end(), [&](const auto& gr) {
                   const auto& tm = s.timeline.machines[gr.first];
                   return tm.unsupported || (!tm.error.empty() && !gr.first.empty());
               })) {
        StateCard card;
        card.title = "No agent activity in the last " + range_words(s.settings.timeline_range);
        card.title_size = ui::tokens::kTitle.size;
        card.art = tok::Art::kAgents;
        card.hint = "Agents report their state through OSC 7501 or agentctl report-state.";
        if (s.settings.timeline_range != "24h")
            card.buttons = {{"Show 24h", "timeline.range:24h"}};
        draw_state_card(s, body_a, body_size, card);
    } else {
        // The rows, scrolling under a fixed axis.
        ImGui::SetCursorScreenPos(body_a);
        // The label column's edge runs down beside the rows (not through the empty-state card).
        dl->AddLine(ImVec2(a.x + g.label_w, body_a.y), ImVec2(a.x + g.label_w, a.y + size.y - legend_h),
                    g_pal->border);
        ImGui::BeginChild("##tl-rows", body_size, ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
        ImDrawList* d = ImGui::GetWindowDrawList();
        const float top = ImGui::GetCursorScreenPos().y;
        float ry = top;
        int index = 0;
        std::string tip;
        std::vector<std::string> ids;  // in display order, for the keyboard
        for (const auto& [machine, rows] : groups) {
            const auto& tm = s.timeline.machines[machine];
            const bool show_header =
                machines.size() > 1 && (!rows.empty() || tm.unsupported || !tm.error.empty());
            const float group_top = ry;
            if (show_header) {
                const float gh = px(24);
                d->AddRectFilled(ImVec2(a.x, ry), ImVec2(a.x + size.x, ry + gh), alpha(g_pal->hover, 0x60));
                d->AddText(s.fonts.ui_bold, fpx(11.5f),
                           ImVec2(a.x + px(14), ry + (gh - fpx(11.5f)) * 0.5f - px(1)), g_pal->dim,
                           s.machine_label(machine).c_str());
                std::string note;
                if (tm.unsupported)
                    note = "Timeline needs a newer agent-host on this machine";
                else if (!tm.error.empty())
                    note = tm.last_ok_ms > 0
                               ? "Machine unreachable since " + tl::clock_text(tm.last_ok_ms, offset)
                               : std::string("Machine unreachable");
                if (!note.empty())
                    d->AddText(s.fonts.ui, fpx(11.5f),
                               ImVec2(g.track_x0, ry + (gh - fpx(11.5f)) * 0.5f - px(1)), g_pal->needs_you,
                               note.c_str());
                ImGui::Dummy(ImVec2(size.x, gh));
                ry += gh;
            }
            for (const auto& r : rows) {
                const float h = px(r.depth > 0 ? 24.0f : 32.0f);
                const ImVec2 ra(a.x, ry);
                ImGui::SetCursorScreenPos(ra);
                ImGui::PushID(r.session->id.c_str());
                const bool label_clicked = ImGui::InvisibleButton("##label", ImVec2(g.label_w, h));
                const bool label_hov = ImGui::IsItemHovered();
                ImGui::PopID();
                ImGui::SetCursorScreenPos(ImVec2(a.x, ry + h));
                if (index % 2 == 1)
                    d->AddRectFilled(ra, ImVec2(a.x + size.x, ry + h), alpha(g_pal->hover, 0x40));
                const bool sel = s.timeline.selected == r.session->id;
                if (sel) {
                    d->AddRectFilled(ra, ImVec2(a.x + g.label_w, ry + h), g_pal->selected);
                    d->AddRectFilled(ra, ImVec2(a.x + px(2), ry + h), g_pal->focus);
                } else if (label_hov) {
                    d->AddRectFilled(ra, ImVec2(a.x + g.label_w, ry + h), g_pal->hover);
                }
                draw_label(s, d, r, ra, g.label_w, h, narrow);
                if (label_hov) {
                    std::string t = r.live ? "Go to this agent" : "This agent has ended";
                    if (r.session->truncated)
                        t += "\nEarlier history dropped";
                    if (r.session->adopted)
                        t += "\nHistory starts when agent-host was last updated";
                    tip = t;
                }
                if (r.session->truncated)
                    d->AddText(s.fonts.ui, fpx(10), ImVec2(g.track_x0 - px(2), ry + px(1)), g_pal->faint,
                               "\xE2\x80\xB9 earlier dropped");
                bool clicked = false;
                const std::string bar_tip = draw_bars(s, d, g, r, ry, h, offset, clicked);
                if (!bar_tip.empty())
                    tip = bar_tip;
                if ((clicked || label_clicked) && r.live)
                    out.focus = r.session->id;
                ids.push_back(r.session->id);
                ry += h;
                ++index;
            }
            // A machine that stopped answering: hatched from its last answer to now.
            if (!tm.error.empty() && tm.last_ok_ms > 0 && ry > group_top) {
                const float hx = std::max(g.track_x0, g.x_of(tm.last_ok_ms));
                hatch(d, ImVec2(hx, group_top), ImVec2(g.track_x1, ry), alpha(g_pal->faint, 0x50));
            }
        }
        ImGui::Dummy(ImVec2(size.x, px(8)));
        const float content_bottom = std::max(ry, top + body_size.y);
        // Time away: shaded from when the window lost focus to now, labelled on the axis.
        if (s.settings.timeline_since_left && s.timeline.left_at_ms && *s.timeline.left_at_ms < g.to) {
            const float lx = std::max(g.track_x0, g.x_of(*s.timeline.left_at_ms));
            d->AddRectFilled(ImVec2(lx, top), ImVec2(g.track_x1, content_bottom), alpha(g_pal->focus, 0x14));
        }
        // "now": a 1 px accent line through every row.
        if (s.timeline.end_ms == 0)
            d->AddLine(ImVec2(g.track_x1, top), ImVec2(g.track_x1, content_bottom), g_pal->focus, px(1));
        ImGui::EndChild();
        if (!tip.empty() &&
            ImGui::IsMouseHoveringRect(body_a, ImVec2(body_a.x + body_size.x, body_a.y + body_size.y)))
            ImGui::SetTooltip("%s", tip.c_str());
        // Keyboard (overlay only): up / down choose a row, Enter goes to it.
        if (header && !ids.empty()) {
            auto it = std::find(ids.begin(), ids.end(), s.timeline.selected);
            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))
                s.timeline.selected = it == ids.end() || it + 1 == ids.end() ? ids.front() : *(it + 1);
            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))
                s.timeline.selected = it == ids.end() || it == ids.begin() ? ids.back() : *(it - 1);
            if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) && it != ids.end())
                out.focus = *it;
        }
    }
    // The axis label for the time away (over the tick labels, on its own pill).
    if (s.settings.timeline_since_left && s.timeline.left_at_ms && *s.timeline.left_at_ms > g.from &&
        *s.timeline.left_at_ms < g.to) {
        const float lx = g.x_of(*s.timeline.left_at_ms);
        const std::string t = "you left " + tl::clock_text(*s.timeline.left_at_ms, offset);
        const ImVec2 sz = s.fonts.ui->CalcTextSizeA(fpx(11), FLT_MAX, 0, t.c_str());
        const float tx = std::min(lx + px(4), g.track_x1 - sz.x - px(36));
        dl->AddRectFilled(ImVec2(tx - px(4), axis_y + px(5)),
                          ImVec2(tx + sz.x + px(4), axis_y + axis_h - px(7)),
                          header ? g_pal->card : g_pal->bg, px(4));
        dl->AddText(s.fonts.ui, fpx(11), ImVec2(tx, axis_y + (axis_h - sz.y) * 0.5f - px(1)), g_pal->dim,
                    t.c_str());
        dl->AddLine(ImVec2(lx, axis_y + axis_h - px(8)), ImVec2(lx, axis_y + axis_h), g_pal->focus, px(1));
    }
    // History of a machine whose agent-host was updated starts at the takeover.
    for (const auto& [machine, rows] : groups) {
        long long start = 0;
        for (const auto& r : rows)
            if (r.session->adopted && !r.session->marks.empty())
                start = std::max(start, r.session->marks.front().at_ms);
        if (start > g.from && start < g.to) {
            const float lx = g.x_of(start);
            const std::string t = "History starts " + tl::clock_text(start, offset) + " (agent-host updated)";
            dl->AddLine(ImVec2(lx, axis_y + px(4)), ImVec2(lx, axis_y + axis_h), g_pal->faint, px(1));
            dl->AddText(s.fonts.ui, fpx(10.5f), ImVec2(lx + px(4), axis_y + px(2)), g_pal->faint, t.c_str());
        }
    }
    // Legend.
    const float ly = a.y + size.y - legend_h;
    dl->AddLine(ImVec2(a.x, ly), ImVec2(a.x + size.x, ly), g_pal->border);
    float lx = a.x + px(18);
    const float lcy = ly + legend_h * 0.5f;
    for (const auto& [k, label] :
         {std::pair{tl::Kind::kWorking, "Working"}, std::pair{tl::Kind::kBlocked, "Needs you"},
          std::pair{tl::Kind::kDone, "Done"}, std::pair{tl::Kind::kIdle, "Idle"},
          std::pair{tl::Kind::kExited, "Exited"}}) {
        sample(s, dl, k, ImVec2(lx, lcy - px(7)), ImVec2(px(16), px(14)));
        lx += px(16) + px(6);
        dl->AddText(s.fonts.ui, fpx(11.5f), ImVec2(lx, lcy - fpx(11.5f) * 0.6f), g_pal->dim, label);
        lx += s.fonts.ui->CalcTextSizeA(fpx(11.5f), FLT_MAX, 0, label).x + px(16);
    }
    const char* hint =
        header ? "Hover a bar for times  \xC2\xB7  \xE2\x86\x90 \xE2\x86\x92 move  \xC2\xB7  1\xE2\x80\x93"
                 "4 range"
               : "Hover a bar for times";
    const ImVec2 hz = s.fonts.ui->CalcTextSizeA(fpx(11.5f), FLT_MAX, 0, hint);
    if (a.x + size.x - px(18) - hz.x > lx)
        dl->AddText(s.fonts.ui, fpx(11.5f), ImVec2(a.x + size.x - px(18) - hz.x, lcy - fpx(11.5f) * 0.6f),
                    g_pal->faint, hint);
    return out;
}

// Every frame the timeline shows: ask, take answers, follow live states.
void keep_fresh(App::State& s) {
    take_answers(s);
    request(s);
    follow_live_states(s);
}

// Keys inside the overlay: Esc and the timeline.toggle keys close; left / right pan by half the range, End
// returns to now; 1-4 pick the range; Ctrl+wheel zooms around the pointer.
void overlay_keys(App::State& s, bool& close, ImVec2 a, ImVec2 size) {
    const ImGuiIO& io = ImGui::GetIO();
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        close = true;
    // The overlay is a popup, so the global shortcuts do not run under it.
    for (const auto& [chord, cmd] : s.keymap.global_map())
        if (cmd == "timeline.toggle" && ImGui::IsKeyChordPressed(chord))
            close = true;
    // (The overlay has no text field; the terminal under it may still ask for text input.)
    const long long range = tl::range_ms(s.settings.timeline_range);
    const long long now = now_ms();
    auto pan = [&](long long by) {
        long long end = view_end(s) + by;
        s.timeline.end_ms = end >= now ? 0 : end;
    };
    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow))
        pan(-range / 2);
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow))
        pan(range / 2);
    if (ImGui::IsKeyPressed(ImGuiKey_End, false))
        s.timeline.end_ms = 0;
    static constexpr const char* kKeys[] = {"15m", "1h", "4h", "24h"};
    for (int i = 0; i < 4; ++i)
        if (ImGui::IsKeyPressed(static_cast<ImGuiKey>(ImGuiKey_1 + i), false) && !io.KeyCtrl && !io.KeyAlt)
            run_command(s, std::string("timeline.range:") + kKeys[i]);
    if (io.KeyCtrl && io.MouseWheel != 0 &&
        ImGui::IsMouseHoveringRect(a, ImVec2(a.x + size.x, a.y + size.y))) {
        // Keep the time under the pointer where it is.
        const float label_w = size.x < px(640) ? px(140) : px(220);
        const float x0 = a.x + label_w + px(8);
        const float x1 = a.x + size.x - px(18);
        const float f = std::clamp((io.MousePos.x - x0) / std::max(1.0f, x1 - x0), 0.0f, 1.0f);
        const long long end = view_end(s);
        const long long at =
            end - range + static_cast<long long>(static_cast<double>(f) * static_cast<double>(range));
        const std::string next = io.MouseWheel > 0 ? tl::shorter_range(s.settings.timeline_range)
                                                   : tl::longer_range(s.settings.timeline_range);
        if (next != s.settings.timeline_range) {
            const long long nr = tl::range_ms(next);
            const long long new_end =
                at + static_cast<long long>((1.0 - static_cast<double>(f)) * static_cast<double>(nr));
            run_command(s, "timeline.range:" + next);
            s.timeline.end_ms = new_end >= now ? 0 : new_end;
        }
    }
}

}  // namespace

void draw_timeline_overlay(App::State& s) {
    // The layout (and any timeline block in it) has drawn by now: note for the wake-up timer.
    s.timeline.shown = s.timeline.showing || s.timeline.open;
    s.timeline.showing = false;
    if (s.timeline.open && !ImGui::IsPopupOpen("##timeline"))
        ImGui::OpenPopup("##timeline");
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    // 90 % x 80 % of the window, at least 640 x 400 (all of it when smaller).
    const ImVec2 want(std::max(vp->WorkSize.x * 0.9f, std::min(px(640), vp->WorkSize.x)),
                      std::max(vp->WorkSize.y * 0.8f, std::min(px(400), vp->WorkSize.y)));
    center_next_window(ImVec2(std::min(want.x, vp->WorkSize.x), std::min(want.y, vp->WorkSize.y)));
    push_overlay_style();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, px(12));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, v4(g_pal->card));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, v4(g_pal->card));
    const bool open = ImGui::BeginPopupModal(
        "##timeline", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);
    pop_overlay_style();
    if (!open)
        return;
    if (!s.timeline.open) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    keep_fresh(s);
    s.timeline.showing = true;
    const ImVec2 a = ImGui::GetWindowPos();
    const ImVec2 size = ImGui::GetWindowSize();
    bool close = false;
    const BodyResult r = draw_body(s, a, size, true, close);
    overlay_keys(s, close, a, size);
    if (!r.focus.empty()) {
        run_command(s, "agent.focus:" + r.focus);
        close = true;
    }
    if (close) {
        s.timeline.open = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

bool draw_timeline_block(App::State& s, const std::string& mod, ImVec2 pos, ImVec2 size) {
    if (mod != "timeline")
        return false;
    keep_fresh(s);
    s.timeline.showing = true;
    ImGui::SetCursorScreenPos(pos);
    ImGui::BeginChild(
        "##timeline-block", size, ImGuiChildFlags_None,
        ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    bool close = false;
    const BodyResult r = draw_body(s, pos, size, false, close);
    ImGui::EndChild();
    if (!r.focus.empty())
        run_command(s, "agent.focus:" + r.focus);
    return true;
}

std::optional<std::string> exec_timeline_command(App::State& s, const std::string& name,
                                                 const std::string& arg) {
    if (name == "timeline.toggle") {
        s.timeline.open = !s.timeline.open;
        if (s.timeline.open) {
            s.timeline.end_ms = 0;
            for (auto& [m, tm] : s.timeline.machines)
                tm.fetched = {};  // ask again now
        }
        return std::string();
    }
    if (name == "timeline.range") {
        if (arg != "15m" && arg != "1h" && arg != "4h" && arg != "24h")
            return "timeline.range takes 15m, 1h, 4h or 24h";
        s.settings.timeline_range = arg;
        save_settings(s.settings);
        return std::string();
    }
    return std::nullopt;
}

long long timeline_wakeup_ms(const App::State& s) {
    if (!s.timeline.shown)
        return 1000;
    for (const auto& [m, tm] : s.timeline.machines)
        if (tm.asked)
            return 100;  // an answer is on its way (the worker also wakes the window)
    return 5000;         // the "now" line moves on
}

}  // namespace detail
}  // namespace agentdesk::desktop
