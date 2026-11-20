// agent-host's per-session state history (libs/host state_timeline.hpp): what is kept, for how
// long, and what a range query returns. Times are given explicitly, so no clock is involved.
#include "agentdesk/host/state_timeline.hpp"

#include <gtest/gtest.h>

namespace agentdesk::host {
namespace {

constexpr std::int64_t kHour = 60LL * 60 * 1000;

TEST(StateTimeline, RecordsOnlyChanges) {
    StateTimeline t;
    EXPECT_TRUE(t.add(1000, AgentState::kUnknown, ProcessState::kRunning));
    EXPECT_FALSE(t.add(2000, AgentState::kUnknown, ProcessState::kRunning));  // same states
    EXPECT_TRUE(t.add(3000, AgentState::kWorking, ProcessState::kRunning));
    EXPECT_TRUE(t.add(4000, AgentState::kWorking, ProcessState::kStopping));  // the process alone
    ASSERT_EQ(t.marks().size(), 3u);
    EXPECT_EQ(t.marks()[1], (StateMark{3000, AgentState::kWorking, ProcessState::kRunning}));
    EXPECT_FALSE(t.truncated());
}

TEST(StateTimeline, KeepsTheNewestThousandAndSaysSo) {
    StateTimeline t;
    for (int i = 0; i < 2000; ++i)
        t.add(1000 + i, i % 2 == 0 ? AgentState::kWorking : AgentState::kBlocked, ProcessState::kRunning);
    EXPECT_EQ(t.marks().size(), StateTimeline::kMaxMarks);
    EXPECT_EQ(t.marks().front().at_ms, 2000);  // 1000..1999 dropped
    EXPECT_EQ(t.marks().back().at_ms, 2999);
    EXPECT_TRUE(t.truncated());
}

TEST(StateTimeline, DropsWhatIsOlderThanADayButKeepsTheStartingState) {
    StateTimeline t;
    t.add(0, AgentState::kWorking, ProcessState::kRunning);
    t.add(1 * kHour, AgentState::kBlocked, ProcessState::kRunning);
    t.add(2 * kHour, AgentState::kWorking, ProcessState::kRunning);
    EXPECT_FALSE(t.truncated());
    // At 26 h the cut is just after 2 h: the marks at 0 and 1 h go, the one at 2 h stays (the
    // state the kept day starts in).
    t.add(26 * kHour + 1, AgentState::kDone, ProcessState::kRunning);
    ASSERT_EQ(t.marks().size(), 2u);
    EXPECT_EQ(t.marks().front().at_ms, 2 * kHour);
    EXPECT_EQ(t.marks().front().agent, AgentState::kWorking);
    EXPECT_TRUE(t.truncated());
}

TEST(StateTimeline, AClockSetBackKeepsTheOrder) {
    StateTimeline t;
    t.add(5000, AgentState::kWorking, ProcessState::kRunning);
    EXPECT_TRUE(t.add(3000, AgentState::kBlocked, ProcessState::kRunning));  // the clock went back
    ASSERT_EQ(t.marks().size(), 2u);
    EXPECT_EQ(t.marks().back().at_ms, 5000);
    EXPECT_EQ(t.marks().back().agent, AgentState::kBlocked);
}

TEST(StateTimeline, SinceStartsWithTheStateTheRangeFallsIn) {
    StateTimeline t;
    t.add(1000, AgentState::kIdle, ProcessState::kRunning);
    t.add(2000, AgentState::kWorking, ProcessState::kRunning);
    t.add(3000, AgentState::kDone, ProcessState::kRunning);
    const auto r = t.since(2500);
    ASSERT_EQ(r.size(), 2u);
    EXPECT_EQ(r[0].at_ms, 2000);  // Working was the state at 2500
    EXPECT_EQ(r[1].at_ms, 3000);
    EXPECT_EQ(t.since(0).size(), 3u);
    EXPECT_EQ(t.since(2000).front().at_ms, 2000);  // exactly on a mark: that mark leads
    EXPECT_EQ(t.since(9999).size(), 1u);           // after the last: just the current state
    EXPECT_TRUE(StateTimeline().since(0).empty());
}

}  // namespace
}  // namespace agentdesk::host
