// The shared design tokens' arithmetic; see design_tokens.hpp.
#include "agentdesk/ui/design_tokens.hpp"

#include <algorithm>

#include "agentdesk/ui/color_math.hpp"

namespace agentdesk::ui::tokens {

float ease_out(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    const float u = 1.0f - t;
    return 1.0f - u * u * u;
}

float ease_in_out(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    if (t < 0.5f)
        return 4.0f * t * t * t;
    const float u = -2.0f * t + 2.0f;
    return 1.0f - u * u * u * 0.5f;
}

float Tween::value(double now_ms) const {
    if (!set_ || duration_ <= 0.0 || now_ms >= start_ + duration_)
        return to_;
    const float t = static_cast<float>((now_ms - start_) / duration_);
    return from_ + (to_ - from_) * ease_out(t);
}

bool Tween::moving(double now_ms) const {
    return set_ && duration_ > 0.0 && now_ms < start_ + duration_ && from_ != to_;
}

float Tween::update(float target, double now_ms, double duration_ms, bool reduced) {
    if (!set_) {
        // The first value shows as it is: nothing eases in from zero on the first frame.
        set_ = true;
        from_ = to_ = target;
        start_ = now_ms;
        duration_ = 0.0;
        return target;
    }
    if (reduced) {
        from_ = to_ = target;
        duration_ = 0.0;
        return target;
    }
    if (target != to_) {
        from_ = value(now_ms);
        to_ = target;
        start_ = now_ms;
        duration_ = duration_ms;
    }
    return value(now_ms);
}

namespace {
using color::mix;
constexpr std::uint32_t kWhite = 0xffffff;
constexpr std::uint32_t kBlack = 0x000000;
}  // namespace

std::uint32_t hover_fill(const Surface& s) {
    return mix(s.bg, s.fg, s.light ? 0.05 : 0.06);
}

std::uint32_t selected_fill(const Surface& s) {
    return mix(s.bg, s.accent, s.light ? 0.12 : 0.16);
}

std::uint32_t selected_edge(const Surface& s) {
    const std::uint32_t wash = selected_fill(s);
    std::uint32_t c = s.accent;
    // Nudge toward the text colour until it stands off the wash (3:1, the graphics minimum).
    for (int i = 1; i <= 10 && color::contrast(c, wash) < 3.0; ++i)
        c = mix(s.accent, s.fg, 0.08 * i);
    return c;
}

std::uint32_t focus_ring(const Surface& s) {
    std::uint32_t c = s.accent;
    const std::uint32_t toward = s.light ? kBlack : kWhite;
    for (int i = 1; i <= 10 && color::contrast(c, s.bg) < 3.0; ++i)
        c = mix(s.accent, toward, 0.07 * i);
    return c;
}

std::uint32_t divider(const Surface& s) {
    return mix(s.bg, s.fg, s.light ? 0.11 : 0.10);
}

Elevation elevation(const Surface& s, int level) {
    Elevation e;
    level = std::clamp(level, 1, 2);
    e.shadow = kBlack;
    // Dark themes need a stronger shadow to read at all; light ones a gentler, wider one.
    e.shadow_alpha = s.light ? (level == 1 ? 22 : 34) : (level == 1 ? 60 : 90);
    e.shadow_spread = level == 1 ? 6.0f : 14.0f;
    e.shadow_offset = level == 1 ? 1.0f : 4.0f;
    e.highlight = kWhite;
    // The top-edge highlight only on dark surfaces (on light ones the card is already the lightest
    // thing there).
    e.highlight_alpha = s.light ? 0 : (level == 1 ? 14 : 18);
    return e;
}

}  // namespace agentdesk::ui::tokens
