// The design tokens in screen terms; see ui_tokens.hpp.
#include "ui_tokens.hpp"

#include <unordered_map>

#include "app_internal.hpp"

namespace agentdesk::desktop::detail {

// The two accessibility reads every animated or decorated view shares (style layouts, the tint
// by agent state, the transitions below).
bool motion_reduced() {
    return tok::reduce_motion();
}

bool system_high_contrast() {
    return tok::high_contrast();
}

namespace tok {

float sp(int steps) {
    return px(t::space(steps));
}
float radius_small() {
    return px(t::kRadiusSmall);
}
float radius_card() {
    return px(t::kRadiusCard);
}
float radius_panel() {
    return px(t::kRadiusPanel);
}

ImFont* font(const App::State& s, const TypeLevel& level) {
    switch (level.weight) {
        case t::Weight::kSemibold:
            return s.fonts.ui_bold != nullptr ? s.fonts.ui_bold : s.fonts.ui;
        case t::Weight::kMedium:
            return s.fonts.ui_medium != nullptr ? s.fonts.ui_medium : s.fonts.ui;
        case t::Weight::kRegular:
            break;
    }
    return s.fonts.ui;
}

float size(const TypeLevel& level) {
    return fpx(level.size);
}

ImVec2 measure(const App::State& s, const TypeLevel& level, const char* text, const char* end) {
    return font(s, level)->CalcTextSizeA(size(level), FLT_MAX, 0, text, end);
}

float text(const App::State& s, ImDrawList* dl, const TypeLevel& level, ImVec2 pos, ImU32 col,
           const char* text, const char* end) {
    dl->AddText(font(s, level), size(level), pos, col, text, end);
    return measure(s, level, text, end).x;
}

namespace {
ImU32 with_rgb(std::uint32_t rgb, int a = 255) {
    return IM_COL32((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255, a);
}

t::Surface surface(ImU32 bg) {
    const ImU32 b = bg != 0 ? bg : g_pal->card;
    return {rgb_of(b), rgb_of(g_pal->text), rgb_of(g_pal->focus), g_pal->light};
}
}  // namespace

ImU32 hover(ImU32 bg) {
    return with_rgb(t::hover_fill(surface(bg)));
}
ImU32 selected(ImU32 bg) {
    return with_rgb(t::selected_fill(surface(bg)));
}
ImU32 selected_edge(ImU32 bg) {
    return with_rgb(t::selected_edge(surface(bg)));
}
ImU32 focus_ring(ImU32 bg) {
    return with_rgb(t::focus_ring(surface(bg)));
}
ImU32 divider(ImU32 bg) {
    return with_rgb(t::divider(surface(bg)));
}

void hline(ImDrawList* dl, float x0, float x1, float y, ImU32 col) {
    const float yy = std::floor(y);
    dl->AddRectFilled(ImVec2(std::round(x0), yy), ImVec2(std::round(x1), yy + 1.0f), col);
}

void vline(ImDrawList* dl, float x, float y0, float y1, ImU32 col) {
    const float xx = std::floor(x);
    dl->AddRectFilled(ImVec2(xx, std::round(y0)), ImVec2(xx + 1.0f, std::round(y1)), col);
}

void shadow(ImDrawList* dl, ImVec2 a, ImVec2 b, float radius, int level, ImU32 bg) {
    const t::Elevation e = t::elevation(surface(bg != 0 ? bg : g_pal->bg), level);
    if (e.shadow_alpha <= 0 || high_contrast())
        return;
    // Rings of falling alpha: the sum reads as one soft shadow, offset a little downward.
    constexpr int kSteps = 5;
    const float spread = px(e.shadow_spread);
    const float dy = px(e.shadow_offset);
    for (int i = kSteps; i >= 1; --i) {
        const float k = static_cast<float>(i) / kSteps;
        const float grow = spread * k;
        const int al =
            static_cast<int>(static_cast<float>(e.shadow_alpha) * (1.0f - k) * (1.0f - k) * 0.9f) + 2;
        dl->AddRectFilled(ImVec2(a.x - grow, a.y - grow + dy), ImVec2(b.x + grow, b.y + grow + dy),
                          with_rgb(e.shadow, std::min(255, al)), radius + grow);
    }
}

void card(ImDrawList* dl, ImVec2 a, ImVec2 b, float radius, int level, ImU32 fill, ImU32 border) {
    const ImU32 f = fill != 0 ? fill : g_pal->card;
    shadow(dl, a, b, radius, level);
    dl->AddRectFilled(a, b, f, radius);
    dl->AddRect(a, b, border != 0 ? border : g_pal->border, radius, 0, 1.0f);
    const t::Elevation e = t::elevation(surface(f), level);
    if (e.highlight_alpha > 0 && !high_contrast()) {
        // A one-pixel highlight just inside the top edge, between the rounded corners.
        const float inset = std::max(radius * 0.7f, 1.0f);
        hline(dl, a.x + inset, b.x - inset, a.y + 1.0f, with_rgb(e.highlight, e.highlight_alpha));
    }
}

void window_shadow(ImDrawList* dl, ImVec2 a, ImVec2 b, float radius, int level) {
    const t::Elevation e = t::elevation(surface(g_pal->bg), level);
    if (e.shadow_alpha <= 0 || high_contrast())
        return;
    dl->PushClipRectFullScreen();
    // Rings of about 2 px, each stroked inside its own outline (ImGui strokes rectangles inward),
    // so ring i covers [i, i + 1] steps outside the edge and none reaches into the window. No
    // downward offset here: an offset ring would cross the window's top edge.
    const float spread = px(e.shadow_spread);
    const int steps = std::clamp(static_cast<int>(spread / 2.0f), 4, 12);
    const float ring = spread / static_cast<float>(steps);
    for (int i = 0; i < steps; ++i) {
        const float k = (static_cast<float>(i) + 0.5f) / static_cast<float>(steps);
        const float grow = ring * static_cast<float>(i + 1);
        const int al = static_cast<int>(static_cast<float>(e.shadow_alpha) * 0.8f * (1.0f - k) * (1.0f - k));
        if (al <= 0)
            continue;
        dl->AddRect(ImVec2(a.x - grow, a.y - grow), ImVec2(b.x + grow, b.y + grow), with_rgb(e.shadow, al),
                    radius + grow, 0, ring);
    }
    dl->PopClipRect();
}

// ---------------------------------------------------------------- motion
namespace {
struct AccessReads {
    bool reduced = false;
    bool contrast = false;
    Clock::time_point read{};
};

const AccessReads& access() {
    static AccessReads r;
    const auto now = Clock::now();
    if (r.read == Clock::time_point{} || now - r.read >= std::chrono::seconds(2)) {
        r.read = now;
        r.reduced = os::reduced_motion();
        r.contrast = os::high_contrast();
    }
    return r;
}

struct Fade {
    t::Tween tween;
    int used = 0;           // frame last asked for
    double tween_last = 0;  // appear(): when it was last asked for (ms)
};
struct ColorFade {
    ImU32 from = 0, to = 0;
    t::Tween tween;  // 0 -> 1 from `from` to `to`
    int used = 0;
};
std::unordered_map<ImGuiID, Fade> g_fades;
std::unordered_map<ImGuiID, Fade> g_appears;
std::unordered_map<ImGuiID, ColorFade> g_colors;
double g_animate_until = 0;

int frame_no() {
    return ImGui::GetFrameCount();
}
}  // namespace

bool reduce_motion() {
    return access().reduced;
}

bool high_contrast() {
    return access().contrast;
}

double now_ms() {
    return std::chrono::duration<double, std::milli>(Clock::now().time_since_epoch()).count();
}

void keep_animating() {
    g_animate_until = std::max(g_animate_until, now_ms() + 20.0);
}

long long wakeup_ms() {
    const double left = g_animate_until - now_ms();
    return left > 0 ? 16 : 1000000;
}

float fade(ImGuiID id, bool on, double duration_ms) {
    Fade& f = g_fades[id];
    f.used = frame_no();
    const double now = now_ms();
    const float v = f.tween.update(on ? 1.0f : 0.0f, now, duration_ms, reduce_motion());
    if (f.tween.moving(now))
        keep_animating();
    return v;
}

float appear(ImGuiID id, double duration_ms, double forget_ms) {
    const double now = now_ms();
    auto [it, fresh] = g_appears.try_emplace(id);
    Fade& f = it->second;
    const bool restart = fresh || (now - f.tween_last > forget_ms);
    f.used = frame_no();
    f.tween_last = now;
    if (restart) {
        f.tween = t::Tween{};
        f.tween.update(0.0f, now);
    }
    const float v = f.tween.update(1.0f, now, duration_ms, reduce_motion());
    if (f.tween.moving(now))
        keep_animating();
    return v;
}

void forget(ImGuiID id) {
    g_appears.erase(id);
}

ImU32 cross_fade(ImGuiID id, ImU32 target, double duration_ms) {
    auto [it, fresh] = g_colors.try_emplace(id);
    ColorFade& c = it->second;
    c.used = frame_no();
    const double now = now_ms();
    if (fresh) {
        c.from = c.to = target;
        c.tween.update(1.0f, now, 0, true);
        return target;
    }
    if (target != c.to) {
        const float k = c.tween.value(now);
        c.from = mix(c.from, c.to, k);  // where it is now
        c.to = target;
        c.tween = t::Tween{};
        c.tween.update(0.0f, now, 0, true);
    }
    const float k = c.tween.update(1.0f, now, duration_ms, reduce_motion());
    if (c.tween.moving(now))
        keep_animating();
    return k >= 1.0f ? c.to : mix(c.from, c.to, k);
}

void end_frame() {
    // Every few seconds, forget transitions nothing asked for in the last ~10 s of frames.
    const int now = frame_no();
    if (now % 600 != 0)
        return;
    std::erase_if(g_fades, [&](const auto& kv) { return now - kv.second.used > 600; });
    std::erase_if(g_appears, [&](const auto& kv) { return now - kv.second.used > 600; });
    std::erase_if(g_colors, [&](const auto& kv) { return now - kv.second.used > 600; });
}

// ---------------------------------------------------------------- empty states
float art_height() {
    return px(64);
}

void draw_art(ImDrawList* dl, Art art, ImVec2 c, float side) {
    if (art == Art::kNone)
        return;
    // Lines in a quiet mix of the text into the background, one accent detail; 1.5 px strokes.
    const ImU32 line = mix(g_pal->bg, g_pal->text, g_pal->light ? 0.38f : 0.42f);
    const ImU32 fill = mix(g_pal->bg, g_pal->text, g_pal->light ? 0.05f : 0.07f);
    const ImU32 acc = g_pal->focus;
    const float u = side / 64.0f;  // the drawings are made on a 64 x 64 grid
    const float w = std::max(1.0f, 1.5f * u);
    auto P = [&](float x, float y) { return ImVec2(c.x + (x - 32.0f) * u, c.y + (y - 32.0f) * u); };
    switch (art) {
        case Art::kTerminal: {  // a window with a prompt and a cursor
            dl->AddRectFilled(P(8, 14), P(56, 50), fill, 6 * u);
            dl->AddRect(P(8, 14), P(56, 50), line, 6 * u, 0, w);
            dl->AddLine(P(8, 22), P(56, 22), line, w);
            for (int i = 0; i < 3; ++i)
                dl->AddCircleFilled(P(13.0f + 4.5f * static_cast<float>(i), 18), 1.3f * u, line);
            dl->AddPolyline(std::array{P(16, 30), P(21, 34), P(16, 38)}.data(), 3, acc, 0, w * 1.2f);
            dl->AddRectFilled(P(25, 36.5f), P(31, 38.5f), acc, 1 * u);
            break;
        }
        case Art::kChanges: {  // two sheets, the front one with added and removed lines
            dl->AddRectFilled(P(18, 10), P(46, 46), fill, 4 * u);
            dl->AddRect(P(18, 10), P(46, 46), line, 4 * u, 0, w);
            dl->AddRectFilled(P(12, 18), P(40, 54), g_pal->card, 4 * u);
            dl->AddRect(P(12, 18), P(40, 54), line, 4 * u, 0, w);
            dl->AddLine(P(17, 27), P(34, 27), line, w);
            dl->AddLine(P(17, 33), P(30, 33), g_pal->done, w);
            dl->AddLine(P(17, 39), P(32, 39), g_pal->needs_you, w);
            dl->AddLine(P(17, 45), P(27, 45), line, w);
            break;
        }
        case Art::kSearch: {  // a magnifier over a few lines
            for (int i = 0; i < 3; ++i)
                dl->AddLine(P(10, 18.0f + 8.0f * static_cast<float>(i)),
                            P(36, 18.0f + 8.0f * static_cast<float>(i)), fill, w * 2.0f);
            dl->AddCircleFilled(P(36, 30), 12 * u, fill);
            dl->AddCircle(P(36, 30), 12 * u, line, 0, w * 1.2f);
            dl->AddLine(P(45, 39), P(54, 48), acc, w * 2.0f);
            break;
        }
        case Art::kAgents: {  // three small round faces, one waving a dot of colour
            for (int i = 0; i < 3; ++i) {
                const float x = 13.0f + 19.0f * static_cast<float>(i);
                dl->AddCircleFilled(P(x, 36), 9 * u, fill);
                dl->AddCircle(P(x, 36), 9 * u, line, 0, w);
                dl->AddCircleFilled(P(x - 3, 35), 1.3f * u, line);  // eyes
                dl->AddCircleFilled(P(x + 3, 35), 1.3f * u, line);
            }
            dl->AddCircleFilled(P(51, 21), 2.5f * u, acc);  // one of them has something to say
            dl->AddCircleFilled(P(13, 21), 2.5f * u, g_pal->done);
            break;
        }
        case Art::kAllDone: {  // a circle with a check
            dl->AddCircleFilled(P(32, 32), 20 * u, fill);
            dl->AddCircle(P(32, 32), 20 * u, line, 0, w);
            dl->AddPolyline(std::array{P(23, 32), P(29.5f, 38.5f), P(41, 26)}.data(), 3, g_pal->done, 0,
                            w * 1.6f);
            break;
        }
        case Art::kFiles: {  // a folder
            dl->AddRectFilled(P(10, 20), P(54, 48), fill, 4 * u);
            dl->AddRect(P(10, 20), P(54, 48), line, 4 * u, 0, w);
            dl->AddRectFilled(P(10, 15), P(26, 21), fill, 2 * u);
            dl->AddRect(P(10, 15), P(26, 21), line, 2 * u, 0, w);
            dl->AddLine(P(18, 32), P(40, 32), line, w);
            dl->AddLine(P(18, 38), P(34, 38), acc, w);
            break;
        }
        case Art::kNone:
            break;
    }
}

}  // namespace tok
}  // namespace agentdesk::desktop::detail
