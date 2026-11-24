// Focus mode's rules; see focus_mode.hpp.
#include "agentdesk/ui/focus_mode.hpp"

#include <algorithm>
#include <cmath>

namespace agentdesk::ui::focus {

StripDots strip_dots(std::vector<DotAgent> agents, const std::string& focused, float width, float dot,
                     float gap, float more_w) {
    std::erase_if(agents,
                  [&](const DotAgent& a) { return a.id == focused || a.attention == Attention::kNone; });
    std::stable_sort(agents.begin(), agents.end(), [](const DotAgent& a, const DotAgent& b) {
        return priority_before({attention_rank(a.attention), a.since, a.seq},
                               {attention_rank(b.attention), b.since, b.seq});
    });
    StripDots out;
    const int n = static_cast<int>(agents.size());
    auto span = [&](int k) {
        return k <= 0 ? 0.0f : static_cast<float>(k) * dot + static_cast<float>(k - 1) * gap;
    };
    int fit = n;
    if (span(n) > width) {
        fit = 0;
        while (fit < n && span(fit + 1) + gap + more_w <= width)
            ++fit;
    }
    for (int i = 0; i < fit; ++i)
        out.ids.push_back(agents[static_cast<std::size_t>(i)].id);
    out.overflow = n - fit;
    return out;
}

int glow_alpha(double t, int peak) {
    if (t < 0.0 || t >= kGlowTotal)
        return 0;
    double f = 1.0;
    if (t < kGlowIn)
        f = t / kGlowIn;
    else if (t > kGlowIn + kGlowHold)
        f = 1.0 - (t - kGlowIn - kGlowHold) / kGlowOut;
    return static_cast<int>(std::lround(std::clamp(f, 0.0, 1.0) * peak));
}

float HeaderReveal::update(bool hovering, double now) {
    const double dt = last_ < 0.0 ? 0.0 : std::max(0.0, now - last_);
    last_ = now;
    if (hovering && !hovering_)
        hover_since_ = now;
    hovering_ = hovering;
    if (hovering) {
        // Already partly shown (pointer came back while fading): no second dwell.
        if (alpha_ > 0.0f || now - hover_since_ >= kDwell)
            alpha_ = std::min(1.0f, alpha_ + static_cast<float>(dt / kFadeIn));
        if (alpha_ == 0.0f && now - hover_since_ >= kDwell)
            alpha_ = 0.001f;  // the first frame past the dwell starts the fade
    } else {
        alpha_ = std::max(0.0f, alpha_ - static_cast<float>(dt / kFadeOut));
    }
    return alpha_;
}

bool HeaderReveal::animating() const {
    return hovering_ ? alpha_ < 1.0f : alpha_ > 0.0f;
}

std::string short_ago(long long ms) {
    const long long m = std::max(0LL, ms) / 60000;
    if (m < 1)
        return "<1m";
    if (m < 60)
        return std::to_string(m) + "m";
    const long long h = m / 60;
    return h < 24 ? std::to_string(h) + "h" : std::to_string(h / 24) + "d";
}

std::string alerts_mode(const std::string& v) {
    return v == "edge" || v == "normal" ? v : std::string("edge_sound");
}

}  // namespace agentdesk::ui::focus
