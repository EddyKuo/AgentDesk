// Garden's rules (libs/ui garden): creature looks from the session id, poses from the state,
// where creatures stand for 1 to 20 agents, frame pacing, and the layout and its commands.
#include "agentdesk/ui/garden.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "agentdesk/ui/command_names.hpp"
#include "agentdesk/ui/layout.hpp"
#include "agentdesk/ui/layout_styles.hpp"
#include "agentdesk/ui/layout_tips.hpp"

namespace garden = agentdesk::ui::garden;
using agentdesk::ui::Attention;
using garden::Motion;
using garden::Pose;

// The same id always gives the same creature; different ids give a spread of looks.
TEST(Garden, VariantIsDeterministicFromTheId) {
    const auto a = garden::variant_for("s-1f3a");
    const auto b = garden::variant_for("s-1f3a");
    EXPECT_EQ(a.seed, b.seed);
    EXPECT_EQ(a.ears, b.ears);
    EXPECT_EQ(a.tail, b.tail);
    EXPECT_EQ(a.width, b.width);
    EXPECT_EQ(a.phase, b.phase);
    EXPECT_EQ(garden::hash_id(""), 2166136261u);   // FNV-1a offset basis
    EXPECT_EQ(garden::hash_id("a"), 0xe40c292cu);  // FNV-1a of "a"
    std::set<int> ears, tails;
    for (int i = 0; i < 64; ++i) {
        const auto v = garden::variant_for("session-" + std::to_string(i));
        ears.insert(static_cast<int>(v.ears));
        tails.insert(static_cast<int>(v.tail));
        EXPECT_GE(v.width, 0.92f);
        EXPECT_LE(v.width, 1.08f);
        EXPECT_GE(v.tone, -0.08f);
        EXPECT_LE(v.tone, 0.08f);
        EXPECT_GE(v.phase, 0.0f);
        EXPECT_LE(v.phase, 1.0f);
    }
    EXPECT_EQ(ears.size(), 4u);
    EXPECT_EQ(tails.size(), 3u);
}

// Each state has its pose; Done hops first, then sits; only the state decides (ADR-09).
TEST(Garden, StateToPose) {
    EXPECT_EQ(garden::pose_for(Attention::kWorking, 5000), Pose::kTrot);
    EXPECT_EQ(garden::pose_for(Attention::kBlocked, 5000), Pose::kWave);
    EXPECT_EQ(garden::pose_for(Attention::kDone, 300), Pose::kBounce);
    EXPECT_EQ(garden::pose_for(Attention::kDone, garden::kBounceMs + 1), Pose::kSit);
    EXPECT_EQ(garden::pose_for(Attention::kDone, 0), Pose::kSit);  // when it changed is unknown: no hop
    EXPECT_EQ(garden::pose_for(Attention::kIdle, 5000), Pose::kSleep);
    EXPECT_EQ(garden::pose_for(Attention::kNone, 5000), Pose::kRest);

    EXPECT_EQ(garden::motion_of(Pose::kTrot), Motion::kLively);
    EXPECT_EQ(garden::motion_of(Pose::kWave), Motion::kLively);
    EXPECT_EQ(garden::motion_of(Pose::kBounce), Motion::kLively);
    EXPECT_EQ(garden::motion_of(Pose::kSleep), Motion::kGentle);
    EXPECT_EQ(garden::motion_of(Pose::kSit), Motion::kStill);
    EXPECT_EQ(garden::motion_of(Pose::kRest), Motion::kStill);

    // What each pose shows.
    EXPECT_GT(garden::shape_of(Pose::kTrot).travel, 0.5f);
    EXPECT_GT(garden::shape_of(Pose::kWave).bubble, 0.5f);
    EXPECT_GT(garden::shape_of(Pose::kWave).front, 0.5f);
    EXPECT_GT(garden::shape_of(Pose::kBounce).sparkle, 0.5f);
    EXPECT_LT(garden::shape_of(Pose::kSleep).eyes, 0.5f);
    EXPECT_GT(garden::shape_of(Pose::kSleep).zzz, 0.5f);
    EXPECT_LT(garden::shape_of(Pose::kRest).alpha, 1.0f);  // Unknown is faded
    for (Pose p : {Pose::kTrot, Pose::kWave, Pose::kBounce, Pose::kSit, Pose::kSleep})
        EXPECT_EQ(garden::shape_of(p).alpha, 1.0f);
    // Only Wave has the bubble: the "!" means needs you and nothing else.
    for (Pose p : {Pose::kTrot, Pose::kBounce, Pose::kSit, Pose::kSleep, Pose::kRest})
        EXPECT_EQ(garden::shape_of(p).bubble, 0.0f);
}

// Easing between poses goes through the values in between and lands on the target.
TEST(Garden, PosesMixSmoothly) {
    const auto a = garden::shape_of(Pose::kTrot);
    const auto b = garden::shape_of(Pose::kSleep);
    const auto mid = garden::mix(a, b, 0.5f);
    EXPECT_FLOAT_EQ(mid.sit, 0.5f);
    EXPECT_FLOAT_EQ(mid.travel, 0.5f);
    EXPECT_FLOAT_EQ(garden::mix(a, b, 0.0f).travel, a.travel);
    EXPECT_FLOAT_EQ(garden::mix(a, b, 1.0f).zzz, b.zzz);
    EXPECT_FLOAT_EQ(garden::mix(a, b, 7.0f).zzz, b.zzz);  // clamped
}

namespace {
float dist(const garden::Spot& a, const garden::Spot& b) {
    return std::hypot(a.x - b.x, a.y - b.y);
}
}  // namespace

// 1 to 20 agents fit the field without scrolling, inside it and apart; the same input gives the
// same places.
TEST(Garden, PlacementScalesFromOneToTwenty) {
    std::vector<garden::Spot> spots;
    for (int n = 1; n <= 20; ++n) {
        const std::vector<int> parents(static_cast<std::size_t>(n), -1);
        const auto f = garden::place(parents, 1200.0f, 560.0f, spots, 1e9f, 40.0f);
        ASSERT_EQ(spots.size(), parents.size());
        EXPECT_GE(f.cols * f.rows, n);
        EXPECT_GT(f.size, 30.0f) << n;
        for (int i = 0; i < n; ++i) {
            const auto& s = spots[static_cast<std::size_t>(i)];
            EXPECT_TRUE(s.shown);
            EXPECT_GE(s.x - s.size * 0.5f, 0.0f) << n << "/" << i;
            EXPECT_LE(s.x + s.size * 0.5f, 1200.0f) << n << "/" << i;
            EXPECT_GE(s.y - s.size, 0.0f) << n << "/" << i;
            EXPECT_LE(s.y, 560.0f - 40.0f) << n << "/" << i;  // room for the name under it
            for (int j = 0; j < i; ++j)
                EXPECT_GT(dist(s, spots[static_cast<std::size_t>(j)]), s.size) << n << ": " << i << " " << j;
        }
        std::vector<garden::Spot> again;
        garden::place(parents, 1200.0f, 560.0f, again, 1e9f, 40.0f);
        for (std::size_t i = 0; i < spots.size(); ++i) {
            EXPECT_EQ(spots[i].x, again[i].x);
            EXPECT_EQ(spots[i].y, again[i].y);
        }
    }
    // One agent stands in the middle; a narrow window stacks agents in fewer columns.
    garden::place(std::vector<int>{-1}, 800.0f, 400.0f, spots);
    EXPECT_FLOAT_EQ(spots[0].x, 400.0f);
    const auto tall = garden::place(std::vector<int>(6, -1), 300.0f, 900.0f, spots);
    EXPECT_LE(tall.cols, 2);
    // A cap on the size keeps a lone agent from filling a big window.
    EXPECT_FLOAT_EQ(garden::place(std::vector<int>{-1}, 2000.0f, 1000.0f, spots, 120.0f).size, 120.0f);
    EXPECT_LE(spots[0].size, 120.0f);
    // Nothing to place, or no room.
    EXPECT_EQ(garden::place(std::vector<int>{}, 800.0f, 400.0f, spots).cols, 0);
    EXPECT_TRUE(spots.empty());
    EXPECT_EQ(garden::place(std::vector<int>{-1}, 0.0f, 400.0f, spots).cols, 0);
}

// Sub-agents stand beside their parent, smaller, left then right; past two they are counted.
TEST(Garden, SubAgentsFollowTheirParent) {
    std::vector<garden::Spot> spots;
    // 0: agent, 1: agent, 2-5: sub-agents of 0, 6: a sub-agent of sub-agent 2 (counts for 0).
    const std::vector<int> parents = {-1, -1, 0, 0, 0, 0, 2};
    garden::place(parents, 1000.0f, 500.0f, spots);
    const auto& p = spots[0];
    EXPECT_EQ(p.kids, 5);
    EXPECT_EQ(p.hidden_kids, 3);
    EXPECT_TRUE(spots[2].shown);
    EXPECT_TRUE(spots[3].shown);
    EXPECT_FALSE(spots[4].shown);
    EXPECT_FALSE(spots[6].shown);
    EXPECT_LT(spots[2].x, p.x);  // left first
    EXPECT_GT(spots[3].x, p.x);
    for (int k : {2, 3}) {
        EXPECT_LT(spots[static_cast<std::size_t>(k)].size, p.size);
        EXPECT_EQ(spots[static_cast<std::size_t>(k)].row, p.row);
        EXPECT_LT(std::abs(spots[static_cast<std::size_t>(k)].x - p.x), p.size * 1.2f);
    }
    EXPECT_EQ(spots[1].kids, 0);
    // A parent index that loops or points outside the list: an agent of its own.
    garden::place(std::vector<int>{1, 0, 9}, 1000.0f, 500.0f, spots);
    for (const auto& s : spots)
        EXPECT_TRUE(s.shown);
}

// About 30 frames a second in front, 10 for sleepers, 4 behind other windows, none when nothing
// moves or animation effects are off.
TEST(Garden, FrameRatePolicy) {
    EXPECT_EQ(garden::frame_interval_ms(true, false, Motion::kLively), 33);
    EXPECT_EQ(garden::frame_interval_ms(true, false, Motion::kGentle), 100);
    EXPECT_EQ(garden::frame_interval_ms(false, false, Motion::kLively), 250);
    EXPECT_EQ(garden::frame_interval_ms(false, false, Motion::kGentle), 250);
    EXPECT_EQ(garden::frame_interval_ms(true, false, Motion::kStill), -1);
    EXPECT_EQ(garden::frame_interval_ms(true, true, Motion::kLively), -1);
    EXPECT_EQ(garden::frame_interval_ms(false, true, Motion::kLively), -1);
    EXPECT_EQ(garden::ms_to_next_frame(1000, 33), 33 - 1000 % 33);
    EXPECT_EQ(garden::ms_to_next_frame(990, 33), 33);  // on a slot: the next one
    EXPECT_EQ(garden::ms_to_next_frame(1, 250), 249);
    EXPECT_GE(garden::ms_to_next_frame(5, 0), 1);
    EXPECT_EQ(garden::clock_speed(true), 1.0f);
    EXPECT_LT(garden::clock_speed(false), 1.0f);
}

// The layout ships, is a fun layout named "Garden", has a tips card, and its commands are known.
TEST(Garden, LayoutCommandsAndTips) {
    namespace layout = agentdesk::ui::layout;
    namespace styles = agentdesk::ui::styles;
    const auto names = layout::builtin_names();
    EXPECT_NE(std::find(names.begin(), names.end(), "garden"), names.end());
    const layout::Node n = layout::builtin("garden");
    EXPECT_EQ(n.find("bar")->mod(), "garden-bar");
    EXPECT_EQ(n.find("garden")->mod(), "garden-scene");
    EXPECT_EQ(styles::style_of_tree(n), "garden");
    EXPECT_EQ(styles::display_name("garden"), "Garden");
    EXPECT_TRUE(styles::is_fun_layout("garden"));
    EXPECT_FALSE(styles::is_listed("garden", false));
    EXPECT_TRUE(styles::is_listed("garden", true));
    for (const char* c : {"garden.toggle", "garden.open"})
        EXPECT_TRUE(agentdesk::ui::is_builtin_command(c)) << c;
    EXPECT_EQ(agentdesk::ui::tips::card_key("garden", "garden", false), "garden");
    const auto card = agentdesk::ui::tips::card("garden");
    ASSERT_TRUE(card);
    EXPECT_EQ(card->title, "Garden");
}
