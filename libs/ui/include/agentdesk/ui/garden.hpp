#pragma once
// Garden's rules, without drawing: each agent's creature (a look picked from its session id), where
// the creatures stand for 1 to ~20 agents (sub-agents beside their parent), which pose an agent's
// state gives and how one pose eases into the next, and how often the scene needs a frame.
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "agentdesk/ui/workspace.hpp"

namespace agentdesk::ui::garden {

// ---------------------------------------------------------------- the creature's look
enum class Ears : std::uint8_t { kRound, kPointy, kAntenna, kSprout };
enum class Tail : std::uint8_t { kNone, kCurl, kPuff };
struct Variant {
    Ears ears = Ears::kRound;
    Tail tail = Tail::kNone;
    float width = 1.0f;      // body width factor, 0.92..1.08
    float tone = 0.0f;       // lighten (+) or darken (-) the space colour, -0.08..0.08
    float phase = 0.0f;      // 0..1: where in its cycles this creature starts (no two move in step)
    bool spots = false;      // two pale spots on the back
    std::uint32_t seed = 0;  // the id's hash, for anything else that should not change
};
// The same id always gives the same creature (a 32-bit FNV-1a hash of the id's bytes).
std::uint32_t hash_id(std::string_view id);
Variant variant_for(std::string_view id);

// ---------------------------------------------------------------- poses
// What a creature does. The state alone decides it (ADR-09: the structured report, never the
// screen's text).
enum class Pose : std::uint8_t {
    kTrot,    // Working: hops along a little path, a dust puff on each landing
    kWave,    // Blocked (needs you): stands facing you, waving, a "!" bubble over its head
    kBounce,  // Done, just now: a happy hop with a sparkle
    kSit,     // Done, after the hop
    kSleep,   // Idle: curled down, eyes closed, "z z"
    kRest,    // Unknown (no status): sits quietly, faded
};
// How long Done's hop lasts before it sits (ms).
inline constexpr long long kBounceMs = 1800;
// `since_ms`: how long ago the state last changed (0 or less: unknown, taken as long ago).
Pose pose_for(Attention a, long long since_ms);
// How much a pose moves by itself: Sit and Rest are still, Sleep only breathes, the rest are lively.
enum class Motion : std::uint8_t { kStill, kGentle, kLively };
Motion motion_of(Pose p);

// A pose as numbers, so one can ease into another.
struct PoseShape {
    float sit = 0.0f;      // 0 standing .. 1 sitting low
    float eyes = 1.0f;     // 1 open .. 0 closed
    float wave = 0.0f;     // arm raised and waving
    float hop = 0.0f;      // hopping
    float travel = 0.0f;   // moving along the path
    float front = 0.0f;    // 1: faces the viewer (no side glance)
    float alpha = 1.0f;    // opacity
    float bubble = 0.0f;   // the "!" bubble
    float sparkle = 0.0f;  // Done's sparkle
    float happy = 0.0f;    // eyes drawn as smiling arcs
    float zzz = 0.0f;      // Idle's "z z"
};
PoseShape shape_of(Pose p);
PoseShape mix(const PoseShape& a, const PoseShape& b, float t);
// Time to ease from one pose into the next (ms).
inline constexpr double kPoseEaseMs = 420.0;

// ---------------------------------------------------------------- placement
// Where creatures stand, in a `w` x `h` field (the ground; 0,0 at its top left). `parents[i]` is
// the index of creature i's parent in the same list, or -1 for an agent of its own. Agents are
// set in staggered rows, centred, the back rows a little smaller; up to `kMaxKidsShown` sub-agents
// stand on either side of their parent, smaller (a sub-agent of a sub-agent counts for the top one). Spots
// come back in the same order as `parents`.
struct Spot {
    float x = 0;  // feet, centre
    float y = 0;
    float size = 0;       // the creature's height in the field's units
    int row = 0;          // 0: the back row
    bool shown = true;    // false: a sub-agent past kMaxKidsShown (its parent shows "+n")
    int kids = 0;         // on a parent: its sub-agents
    int hidden_kids = 0;  // on a parent: sub-agents not drawn (past kMaxKidsShown)
};
inline constexpr int kMaxKidsShown = 2;
struct Field {
    int cols = 0;
    int rows = 0;
    float cell_w = 0;
    float cell_h = 0;
    float size = 0;  // a front-row agent's height
};
// Fills `out` (its capacity is reused, so a caller that keeps the vector does not allocate).
// `max_size` caps a creature's height (a few agents in a big window stay a friendly size);
// `below` is kept free under each agent's feet for its name.
Field place(std::span<const int> parents, float w, float h, std::vector<Spot>& out, float max_size = 1e9f,
            float below = 0.0f);

// ---------------------------------------------------------------- frames
// How often the scene needs a frame, in ms, for the liveliest pose shown: 33 (about 30 a second)
// while the window is in front and something hops or waves, 100 when only sleepers breathe; 250
// (4 a second) behind other windows; -1 (no frames of its own) when nothing moves or Windows'
// animation effects are off (every pose is drawn still then).
int frame_interval_ms(bool focused, bool reduce_motion, Motion motion);
// Milliseconds from `now_ms` to the start of the next frame slot of `interval_ms` (1..interval).
long long ms_to_next_frame(long long now_ms, int interval_ms);
// How fast the creatures' clock runs: slower behind other windows, so 4 frames a second still
// look smooth rather than jumpy.
float clock_speed(bool focused);

}  // namespace agentdesk::ui::garden
