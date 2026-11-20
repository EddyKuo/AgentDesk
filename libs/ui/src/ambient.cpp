// The ambient tint's strength over time; see ambient.hpp.
#include "agentdesk/ui/ambient.hpp"

#include <algorithm>
#include <cmath>

namespace agentdesk::ui {

namespace {
double smoothstep(double p) {
    return p * p * (3 - 2 * p);
}
}  // namespace

double AmbientFade::value(double now_ms) const {
    if (duration_ <= 0)
        return to_;
    const double p = std::clamp((now_ms - start_) / duration_, 0.0, 1.0);
    return from_ + (to_ - from_) * smoothstep(p);
}

bool AmbientFade::moving(double now_ms) const {
    return duration_ > 0 && now_ms - start_ < duration_;
}

double AmbientFade::update(double target, double now_ms, bool instant) {
    target = std::clamp(target, 0.0, 1.0);
    // A higher target starts (or renews) the hold; a lower one waits for the hold to run out.
    if (target > held_ || now_ms >= hold_until_) {
        if (target > held_)
            hold_until_ = now_ms + kHoldMs;
        held_ = target;
    }
    const double effective = now_ms < hold_until_ ? std::max(target, held_) : target;
    if (effective != to_) {
        const double now_value = value(now_ms);
        from_ = now_value;
        to_ = effective;
        start_ = now_ms;
        duration_ = instant ? 0.0 : std::abs(to_ - from_) * (to_ > from_ ? kInMs : kOutMs);
    } else if (instant) {
        duration_ = 0;
    }
    return value(now_ms);
}

}  // namespace agentdesk::ui
