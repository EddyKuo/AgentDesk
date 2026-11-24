#pragma once
// Focus mode's rules, without drawing: which agents get a dot on the strip and in what order, the
// edge glow's fade curve, and the pane header that fades in when the pointer rests at the top.

#include <string>
#include <vector>

#include "agentdesk/ui/workspace.hpp"

namespace agentdesk::ui::focus {

// One agent the strip may show.
struct DotAgent {
    std::string id;
    Attention attention = Attention::kNone;
    long long since = 0;  // ms of the last state change
    long long seq = 0;    // creation order
};

struct StripDots {
    std::vector<std::string> ids;  // left to right
    int overflow = 0;              // agents that did not fit ("+N")
};

// The dots for `width` px: every agent except `focused` and those with no known state, most urgent
// first (priority_before). Each dot takes `dot` px with `gap` px between; when they do not all fit,
// `more_w` px is kept at the end for the "+N" label.
StripDots strip_dots(std::vector<DotAgent> agents, const std::string& focused, float width, float dot,
                     float gap, float more_w = 0.0f);

// The edge glow: fades in for 150 ms, holds 400 ms, fades out over 900 ms (1450 ms in all).
inline constexpr double kGlowIn = 150.0;
inline constexpr double kGlowHold = 400.0;
inline constexpr double kGlowOut = 900.0;
inline constexpr double kGlowTotal = kGlowIn + kGlowHold + kGlowOut;
// The glow's alpha (0..peak) `elapsed_ms` after it started; 0 before and after.
int glow_alpha(double elapsed_ms, int peak);

// The pane header: shown after the pointer has rested in the top band for 300 ms, fading in over
// 150 ms; once the pointer leaves it fades out over 600 ms. Coming back while it is still partly
// shown brings it straight back.
class HeaderReveal {
public:
    static constexpr double kDwell = 300.0;
    static constexpr double kFadeIn = 150.0;
    static constexpr double kFadeOut = 600.0;

    // Call once a frame; `now_ms` is any steady clock in milliseconds. Returns the alpha (0..1).
    float update(bool hovering, double now_ms);
    [[nodiscard]] float alpha() const { return alpha_; }
    // Something will change without input: waiting out the dwell, or fading.
    [[nodiscard]] bool animating() const;

private:
    float alpha_ = 0.0f;
    bool hovering_ = false;
    double hover_since_ = 0.0;
    double last_ = -1.0;
};

// How long ago, short: "<1m", "5m", "2h", "3d" (negative counts as 0).
std::string short_ago(long long ms);

// The focus_alerts setting: "edge" (the glow only), "edge_sound" (the glow and the sound) or
// "normal" (sound and note as always). Anything else reads as the default, "edge_sound".
std::string alerts_mode(const std::string& value);

}  // namespace agentdesk::ui::focus
