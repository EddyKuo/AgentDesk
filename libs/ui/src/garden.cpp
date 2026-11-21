// Garden's rules (creature looks, poses, placement, frame pacing); see garden.hpp.
#include "agentdesk/ui/garden.hpp"

#include <algorithm>
#include <cmath>

namespace agentdesk::ui::garden {

std::uint32_t hash_id(std::string_view id) {
    std::uint32_t h = 2166136261u;
    for (const char c : id) {
        h ^= static_cast<unsigned char>(c);
        h *= 16777619u;
    }
    return h;
}

namespace {
// A 0..1 value from `bits` bits of the hash starting at `shift`, mixed once more so neighbouring
// fields do not move together.
float unit(std::uint32_t h, int shift) {
    std::uint32_t x = (h >> shift) | (h << (32 - shift));
    x ^= x >> 15;
    x *= 0x2c1b3c6dU;
    x ^= x >> 12;
    return static_cast<float>(x & 0xFFFFu) / 65535.0f;
}
}  // namespace

Variant variant_for(std::string_view id) {
    const std::uint32_t h = hash_id(id);
    Variant v;
    v.seed = h;
    v.ears = static_cast<Ears>(h % 4u);
    v.tail = static_cast<Tail>((h / 4u) % 3u);
    v.spots = ((h >> 5) & 1u) != 0;
    v.width = 0.92f + 0.16f * unit(h, 7);
    v.tone = -0.08f + 0.16f * unit(h, 13);
    v.phase = unit(h, 19);
    return v;
}

Pose pose_for(Attention a, long long since_ms) {
    switch (a) {
        case Attention::kWorking:
            return Pose::kTrot;
        case Attention::kBlocked:
            return Pose::kWave;
        case Attention::kDone:
            return since_ms > 0 && since_ms < kBounceMs ? Pose::kBounce : Pose::kSit;
        case Attention::kIdle:
            return Pose::kSleep;
        case Attention::kNone:
            break;
    }
    return Pose::kRest;
}

Motion motion_of(Pose p) {
    switch (p) {
        case Pose::kTrot:
        case Pose::kWave:
        case Pose::kBounce:
            return Motion::kLively;
        case Pose::kSleep:
            return Motion::kGentle;
        case Pose::kSit:
        case Pose::kRest:
            break;
    }
    return Motion::kStill;
}

PoseShape shape_of(Pose p) {
    PoseShape s;
    switch (p) {
        case Pose::kTrot:
            s.hop = 1.0f;
            s.travel = 1.0f;
            break;
        case Pose::kWave:
            s.wave = 1.0f;
            s.front = 1.0f;
            s.bubble = 1.0f;
            break;
        case Pose::kBounce:
            s.hop = 1.0f;
            s.front = 1.0f;
            s.sparkle = 1.0f;
            s.happy = 1.0f;
            break;
        case Pose::kSit:
            s.sit = 1.0f;
            s.front = 1.0f;
            s.sparkle = 0.6f;
            s.happy = 1.0f;
            break;
        case Pose::kSleep:
            s.sit = 1.0f;
            s.eyes = 0.0f;
            s.front = 0.5f;
            s.zzz = 1.0f;
            break;
        case Pose::kRest:
            s.sit = 1.0f;
            s.eyes = 0.8f;
            s.front = 1.0f;
            s.alpha = 0.5f;
            break;
    }
    return s;
}

PoseShape mix(const PoseShape& a, const PoseShape& b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    auto m = [t](float x, float y) { return x + (y - x) * t; };
    PoseShape o;
    o.sit = m(a.sit, b.sit);
    o.eyes = m(a.eyes, b.eyes);
    o.wave = m(a.wave, b.wave);
    o.hop = m(a.hop, b.hop);
    o.travel = m(a.travel, b.travel);
    o.front = m(a.front, b.front);
    o.alpha = m(a.alpha, b.alpha);
    o.bubble = m(a.bubble, b.bubble);
    o.sparkle = m(a.sparkle, b.sparkle);
    o.happy = m(a.happy, b.happy);
    o.zzz = m(a.zzz, b.zzz);
    return o;
}

namespace {
// The agent a creature belongs to: itself, or the top of its chain of parents (bounded, so a
// broken list with a loop still ends).
int root_of(std::span<const int> parents, int i) {
    const int n = static_cast<int>(parents.size());
    int at = i;
    for (int step = 0; step < n; ++step) {
        const int p = parents[static_cast<std::size_t>(at)];
        if (p < 0 || p >= n || p == at)
            return at;
        at = p;
    }
    return i;  // a loop: treat it as an agent of its own
}
}  // namespace

Field place(std::span<const int> parents, float w, float h, std::vector<Spot>& out, float max_size,
            float below) {
    const int total = static_cast<int>(parents.size());
    out.assign(parents.size(), Spot{});
    Field f;
    if (total == 0 || w <= 0 || h <= 0)
        return f;
    // The agents, in list order.
    int n = 0;
    for (int i = 0; i < total; ++i)
        if (root_of(parents, i) == i)
            ++n;
    // Columns: the count that gives the largest creature, cells about 1.3 times wider than tall
    // (room for the name under it and the sub-agents beside it); fewer rows on a tie.
    float best = -1.0f;
    for (int c = 1; c <= std::min(n, 6); ++c) {
        const int r = (n + c - 1) / c;
        const float cw = w / static_cast<float>(c);
        const float ch = h / static_cast<float>(r);
        const float sz = std::min(cw * 0.40f, std::max(0.0f, ch - below) * 0.62f);
        if (sz > best + 0.01f) {
            best = sz;
            f.cols = c;
            f.rows = r;
        }
    }
    f.cell_w = w / static_cast<float>(f.cols);
    f.cell_h = h / static_cast<float>(f.rows);
    f.size = std::min({f.cell_w * 0.40f, std::max(1.0f, f.cell_h - below) * 0.62f, h * 0.42f, max_size});
    // Agents row by row, each row centred; odd rows lean right a little, so the rows read as a
    // meadow rather than a table. Rows never get much taller than their creatures: a few agents
    // gather in the middle of the field rather than at its bottom edge.
    const float row_h = std::min(f.cell_h, f.size * 1.8f + below);
    const float top = (h - row_h * static_cast<float>(f.rows)) * 0.5f;
    int k = 0;
    for (int i = 0; i < total; ++i) {
        if (root_of(parents, i) != i)
            continue;
        const int r = k / f.cols;
        const int in_row = std::min(f.cols, n - r * f.cols);
        const int c = k % f.cols;
        const float depth = f.rows > 1 ? static_cast<float>(r) / static_cast<float>(f.rows - 1) : 1.0f;
        Spot& s = out[static_cast<std::size_t>(i)];
        s.row = r;
        s.size = f.size * (0.86f + 0.14f * depth);
        const float start = (w - static_cast<float>(in_row) * f.cell_w) * 0.5f;
        const float lean = (r % 2 == 1 ? 0.12f : -0.04f) * f.cell_w * (f.cols > 1 ? 1.0f : 0.0f);
        s.x = std::clamp(start + (static_cast<float>(c) + 0.5f) * f.cell_w + lean, s.size * 0.6f,
                         w - s.size * 0.6f);
        s.y = top + (static_cast<float>(r) + 1.0f) * row_h - std::max(below, row_h * 0.2f);
        ++k;
    }
    // Sub-agents: left of the parent, then right; the rest are counted on the parent.
    for (int i = 0; i < total; ++i) {
        const int root = root_of(parents, i);
        if (root == i)
            continue;
        Spot& p = out[static_cast<std::size_t>(root)];
        Spot& s = out[static_cast<std::size_t>(i)];
        const int kth = p.kids;
        s.row = p.row;
        s.size = p.size * 0.58f;
        if (kth >= kMaxKidsShown) {
            s.shown = false;
            s.x = p.x;
            s.y = p.y;
            ++p.hidden_kids;
        } else {
            const float side = kth % 2 == 0 ? -1.0f : 1.0f;
            s.x = std::clamp(p.x + side * p.size * 0.80f, s.size * 0.6f, w - s.size * 0.6f);
            s.y = p.y - p.size * 0.06f;
        }
        ++p.kids;
    }
    return f;
}

int frame_interval_ms(bool focused, bool reduce_motion, Motion motion) {
    if (reduce_motion || motion == Motion::kStill)
        return -1;
    if (!focused)
        return 250;
    return motion == Motion::kLively ? 33 : 100;
}

long long ms_to_next_frame(long long now_ms, int interval_ms) {
    if (interval_ms <= 0)
        return 1;
    const long long into = ((now_ms % interval_ms) + interval_ms) % interval_ms;
    return interval_ms - into;
}

float clock_speed(bool focused) {
    return focused ? 1.0f : 0.4f;
}

}  // namespace agentdesk::ui::garden
