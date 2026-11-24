// The Stage layout's rules; see stage.hpp.
#include "agentdesk/ui/stage.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace agentdesk::ui::stage {

Filter parse_filter(std::string_view name) {
    if (name == "needs_you" || name == "blocked")
        return Filter::kNeedsYou;
    if (name == "working")
        return Filter::kWorking;
    return Filter::kAll;
}

std::string_view filter_name(Filter f) {
    switch (f) {
        case Filter::kNeedsYou:
            return "needs_you";
        case Filter::kWorking:
            return "working";
        case Filter::kAll:
            break;
    }
    return "all";
}

bool filter_matches(Filter f, Attention a) {
    switch (f) {
        case Filter::kNeedsYou:
            return a == Attention::kBlocked;
        case Filter::kWorking:
            return a == Attention::kWorking;
        case Filter::kAll:
            break;
    }
    return true;
}

namespace {
// clang-format off
constexpr std::array<SizePreset, 3> kPresets{{
    //  w       h       header  footer  mono   line   lines
    {160.0f,  96.0f, 24.0f,  0.0f,  7.0f,  9.5f,  6},
    {224.0f, 136.0f, 28.0f, 22.0f,  8.0f, 10.5f,  8},
    {304.0f, 184.0f, 30.0f, 24.0f,  9.5f, 12.5f, 10},
}};
// clang-format on
}  // namespace

const SizePreset& preset(Size s) {
    return kPresets[static_cast<std::size_t>(s)];
}

Size parse_size(std::string_view name) {
    if (name == "s" || name == "small")
        return Size::kSmall;
    if (name == "l" || name == "large")
        return Size::kLarge;
    return Size::kMedium;
}

std::string_view size_name(Size s) {
    switch (s) {
        case Size::kSmall:
            return "s";
        case Size::kLarge:
            return "l";
        case Size::kMedium:
            break;
    }
    return "m";
}

Size next_size(Size s) {
    switch (s) {
        case Size::kSmall:
            return Size::kMedium;
        case Size::kMedium:
            return Size::kLarge;
        case Size::kLarge:
            break;
    }
    return Size::kSmall;
}

float strip_height(Size s) {
    return kStripHeaderH + preset(s).thumb_h + kStripBottomPad;
}

Size fit_size(Size wanted, float window_h) {
    Size s = wanted;
    while (s != Size::kSmall && strip_height(s) > window_h * kMaxStripShare)
        s = s == Size::kLarge ? Size::kMedium : Size::kSmall;
    return s;
}

namespace {
// Stable order: reachable first, then first seen, then the id (two seen in the same tick).
bool stable_before(const StripAgent& a, const StripAgent& b) {
    if (a.unreachable != b.unreachable)
        return !a.unreachable;
    if (a.first_seen != b.first_seen)
        return a.first_seen < b.first_seen;
    return a.id < b.id;
}
}  // namespace

std::vector<std::string> lineup(const std::vector<StripAgent>& agents) {
    std::vector<const StripAgent*> list;
    for (const auto& a : agents)
        list.push_back(&a);
    std::stable_sort(list.begin(), list.end(),
                     [](const StripAgent* a, const StripAgent* b) { return stable_before(*a, *b); });
    std::vector<std::string> out;
    for (const auto* a : list)
        out.push_back(a->id);
    return out;
}

std::vector<std::string> strip_order(const std::vector<StripAgent>& agents, std::string_view stage_id,
                                     Filter filter) {
    std::vector<const StripAgent*> list;
    for (const auto& a : agents)
        if (a.id != stage_id && filter_matches(filter, a.att))
            list.push_back(&a);
    auto blocked = [](const StripAgent* a) { return a->att == Attention::kBlocked && !a->unreachable; };
    std::stable_sort(list.begin(), list.end(), [&](const StripAgent* a, const StripAgent* b) {
        if (blocked(a) != blocked(b))
            return blocked(a);
        if (blocked(a) && a->blocked_since != b->blocked_since)
            return a->blocked_since < b->blocked_since;  // waiting longest first
        return stable_before(*a, *b);
    });
    std::vector<std::string> out;
    for (const auto* a : list)
        out.push_back(a->id);
    return out;
}

std::string step(const std::vector<std::string>& ring, std::string_view current, int dir) {
    if (ring.empty())
        return {};
    const auto n = static_cast<long long>(ring.size());
    const auto it = std::find(ring.begin(), ring.end(), current);
    if (it == ring.end())
        return dir < 0 ? ring.back() : ring.front();
    const long long at = it - ring.begin();
    const long long next = ((at + (dir < 0 ? -1 : 1)) % n + n) % n;
    return ring[static_cast<std::size_t>(next)];
}

void History::push(std::string_view id) {
    if (id.empty() || (!items_.empty() && items_.front() == id))
        return;
    forget(id);
    items_.insert(items_.begin(), std::string(id));
    if (items_.size() > kMax)
        items_.resize(kMax);
}

void History::forget(std::string_view id) {
    items_.erase(std::remove(items_.begin(), items_.end(), id), items_.end());
}

std::string History::current() const {
    return items_.empty() ? std::string() : items_.front();
}

std::string History::previous() const {
    return items_.size() < 2 ? std::string() : items_[1];
}

std::string auto_switch_target(const AutoSwitch& in, const std::vector<std::string>& newly_blocked) {
    if (!in.enabled || in.stage_needs_you || in.popup_open || in.ms_since_input < kTypingQuietMs ||
        newly_blocked.empty())
        return {};
    return newly_blocked.front();
}

float content_width(int count, float thumb_w, float gap) {
    if (count <= 0)
        return 0.0f;
    return thumb_w * static_cast<float>(count) + gap * static_cast<float>(count - 1);
}

float clamp_scroll(float scroll, float content_w, float view_w) {
    return std::clamp(scroll, 0.0f, std::max(0.0f, content_w - view_w));
}

float scroll_to_show(float scroll, int index, float thumb_w, float gap, float view_w) {
    if (index < 0)
        return scroll;
    const float left = static_cast<float>(index) * (thumb_w + gap);
    const float right = left + thumb_w;
    if (left < scroll)
        return left;
    if (right > scroll + view_w)
        return std::max(0.0f, right - view_w);
    return scroll;
}

float glow_level(double ms, bool reduced) {
    if (reduced || ms < 0 || ms >= kPulseMs * kPulseCount)
        return kGlowSteady;
    // Each breath rises from the steady level to full and settles back (a raised cosine).
    const double phase = std::fmod(ms, kPulseMs) / kPulseMs;
    const double k = 0.5 - 0.5 * std::cos(phase * 2.0 * 3.14159265358979);
    return kGlowSteady + static_cast<float>(k) * (1.0f - kGlowSteady);
}

bool glow_moving(double ms, bool reduced) {
    return !reduced && ms >= 0 && ms < kPulseMs * kPulseCount;
}

}  // namespace agentdesk::ui::stage
