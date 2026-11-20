#pragma once
// The strength of the ambient tint over time: eases in (1.2 s for the full range) when an agent
// needs you, holds at least 2 s, and eases out (2.5 s). A change of target midway carries on from
// the current strength, so the colour never jumps. Times are passed in (milliseconds); no clock
// is read.

namespace agentdesk::ui {

class AmbientFade {
public:
    static constexpr double kInMs = 1200;
    static constexpr double kOutMs = 2500;
    static constexpr double kHoldMs = 2000;

    // `target` (0..1) wanted at `now_ms`; returns the strength to show. With `instant` the
    // strength goes straight to the target (reduce motion, or the window coming back to the front);
    // the hold still applies.
    double update(double target, double now_ms, bool instant = false);
    // The strength at `now_ms` without changing the target.
    [[nodiscard]] double value(double now_ms) const;
    // True while the strength is still moving.
    [[nodiscard]] bool moving(double now_ms) const;

private:
    double from_ = 0, to_ = 0;
    double start_ = 0, duration_ = 0;
    double held_ = 0;         // the highest target asked for recently
    double hold_until_ = -1;  // until when it is kept
};

}  // namespace agentdesk::ui
