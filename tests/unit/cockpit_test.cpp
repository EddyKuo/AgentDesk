// Tests for the AgentDesk Cockpit layout's rules: the shipped file, the gauges' angles and top
// values, today's count over midnight and a clock turned back, the log's size, the roster order
// and the counts that leave unknown states out.
#include "agentdesk/ui/cockpit.hpp"

#include <cmath>
#include <numbers>

#include <gtest/gtest.h>

#include "agentdesk/ui/command_names.hpp"
#include "agentdesk/ui/layout.hpp"
#include "agentdesk/ui/layout_styles.hpp"

using namespace agentdesk::ui;
using namespace agentdesk::ui::cockpit;

namespace {
float deg(float rad) {
    return rad * 180.0f / std::numbers::pi_v<float>;
}
}  // namespace

TEST(Cockpit, ShippedLayoutParsesAndIsCockpitStyle) {
    const layout::Node n = layout::builtin("cockpit");
    EXPECT_EQ(styles::style_of_tree(n), "cockpit");
    EXPECT_EQ(n.find("header")->mod(), "cockpit-header");
    EXPECT_EQ(n.find("gauges")->mod(), "cockpit-gauges");
    EXPECT_EQ(n.find("roster")->mod(), "cockpit-roster");
    EXPECT_EQ(n.find("log")->mod(), "cockpit-log");
    EXPECT_EQ(n.find("main")->content.type, layout::Content::Type::kTab);
    EXPECT_EQ(n.find("gauges")->size, layout::Span::pixels(240));
    EXPECT_EQ(n.find("roster")->size, layout::Span::pixels(280));
    EXPECT_EQ(n.find("log")->size, layout::Span::pixels(120));
    EXPECT_EQ(styles::pane_look("cockpit", &*n.find("main")->content.pane).radius, 4.0f);
    EXPECT_EQ(styles::display_name("cockpit"), "AgentDesk Cockpit");
    EXPECT_TRUE(is_builtin_command("cockpit.reset_today"));
}

// 0 sits at the lower left (210 degrees counter-clockwise, -210 on screen), the top value at the
// lower right (-30, +30 on screen); out-of-range values stay on the sweep.
TEST(Cockpit, GaugeAngleClampsToSweep) {
    EXPECT_NEAR(deg(gauge_angle(0, 4)), -210.0f, 0.01f);
    EXPECT_NEAR(deg(gauge_angle(4, 4)), 30.0f, 0.01f);
    EXPECT_NEAR(deg(gauge_angle(2, 4)), -90.0f, 0.01f);  // half way: straight up
    EXPECT_NEAR(deg(gauge_angle(9, 4)), 30.0f, 0.01f);
    EXPECT_NEAR(deg(gauge_angle(-3, 4)), -210.0f, 0.01f);
    EXPECT_NEAR(deg(gauge_angle(1, 0)), -210.0f, 0.01f);  // no top value: empty
    // Up on the screen is negative y in ImGui: the half-way point points up.
    EXPECT_LT(std::sin(gauge_angle(2, 4)), -0.99f);
}

TEST(Cockpit, GaugeMaxRoundsUp) {
    EXPECT_EQ(agents_max(0), 4);
    EXPECT_EQ(agents_max(3), 4);
    EXPECT_EQ(agents_max(9), 9);
    EXPECT_EQ(done_max(0), 10);
    EXPECT_EQ(done_max(7), 10);
    EXPECT_EQ(done_max(10), 10);
    EXPECT_EQ(done_max(11), 15);
    EXPECT_EQ(done_max(15), 15);
    EXPECT_EQ(done_max(101), 105);
    EXPECT_EQ(gauge_number(3, true), "3");
    EXPECT_EQ(gauge_number(3, false), "--");
    EXPECT_EQ(number_size(99), 30.0f);
    EXPECT_EQ(number_size(100), 24.0f);
}

TEST(Cockpit, DoneTodayRollsOverAtMidnight) {
    DoneToday d;
    d.add("2026-11-20");
    d.add("2026-11-20");
    EXPECT_EQ(d.count, 2);
    EXPECT_FALSE(d.roll("2026-11-20"));
    EXPECT_TRUE(d.roll("2026-11-21"));  // a new day starts at zero
    EXPECT_EQ(d.count, 0);
    EXPECT_EQ(d.date, "2026-11-21");
    d.add("2026-11-21");
    EXPECT_EQ(d.count, 1);
    d.add("2026-11-22");  // the first finish of a new day
    EXPECT_EQ(d.count, 1);
    EXPECT_EQ(d.date, "2026-11-22");
    EXPECT_EQ(date_text(2026, 1, 5), "2026-01-05");
    EXPECT_EQ(time_text(9, 5, 3), "09:05:03");
}

TEST(Cockpit, DoneTodayClockGoesBack) {
    DoneToday d{"2026-11-21", 4};
    EXPECT_TRUE(d.roll("2026-11-20"));  // the saved date is ahead of today: a new day, never negative
    EXPECT_EQ(d.count, 0);
    EXPECT_EQ(d.date, "2026-11-20");
}

TEST(Cockpit, FinishedMeansWorkingToDoneOrIdle) {
    EXPECT_TRUE(finished(Attention::kWorking, Attention::kDone));
    EXPECT_TRUE(finished(Attention::kWorking, Attention::kIdle));
    EXPECT_FALSE(finished(Attention::kWorking, Attention::kBlocked));
    EXPECT_FALSE(finished(Attention::kBlocked, Attention::kDone));
    EXPECT_FALSE(finished(Attention::kNone, Attention::kDone));
}

TEST(Cockpit, EventLogKeepsNewest200) {
    EventLog log;
    for (int i = 0; i < 250; ++i)
        log.push({time_text(0, 0, i % 60), "id", std::to_string(i), Attention::kWorking, Attention::kDone});
    EXPECT_EQ(log.items().size(), kLogSize);
    EXPECT_EQ(log.items().front().who, "249");  // newest first
    EXPECT_EQ(log.items().back().who, "50");
    log.clear();
    EXPECT_TRUE(log.items().empty());
}

// Unknown states are not counted, and the roster puts them last.
TEST(Cockpit, CountsIgnoreUnknown) {
    const auto c = chrome_common::count_agents(
        {Attention::kNone, Attention::kWorking, Attention::kBlocked, Attention::kNone, Attention::kWorking});
    EXPECT_EQ(c.working, 2);
    EXPECT_EQ(c.needs, 1);
    EXPECT_LT(roster_rank(Attention::kBlocked), roster_rank(Attention::kDone));
    EXPECT_LT(roster_rank(Attention::kDone), roster_rank(Attention::kWorking));
    EXPECT_LT(roster_rank(Attention::kWorking), roster_rank(Attention::kIdle));
    EXPECT_LT(roster_rank(Attention::kIdle), roster_rank(Attention::kNone));
    EXPECT_STREQ(state_word(Attention::kNone), "UNKNOWN");
    EXPECT_STREQ(state_word(Attention::kBlocked), "NEEDS YOU");
}

TEST(Cockpit, NarrowWindowsDropRosterThenLog) {
    Fit f = fit(1280, 700);
    EXPECT_TRUE(f.roster);
    EXPECT_TRUE(f.log);
    EXPECT_FALSE(f.bars);
    f = fit(900, 700);
    EXPECT_FALSE(f.roster);
    EXPECT_TRUE(f.log);
    f = fit(800, 500);
    EXPECT_FALSE(f.roster);
    EXPECT_FALSE(f.log);
    EXPECT_TRUE(f.bars);
}
