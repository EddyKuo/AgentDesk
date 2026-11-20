// The Timeline overlay's rules (libs/ui timeline.hpp): bars from marks, packing, axis ticks, row
// order, and reading agent-host's answer.
#include "agentdesk/ui/timeline.hpp"

#include <gtest/gtest.h>

#include "agentdesk/ui/command_names.hpp"
#include "agentdesk/ui/keymap.hpp"

namespace agentdesk::ui::timeline {
namespace {

constexpr long long kMin = 60LL * 1000;

// The acceptance example: work 13:00-13:20, blocked to 13:35, work to 13:50, done; seen at 14:00.
std::vector<Mark> example(long long t0) {
    return {{t0, "Working", "Running"},
            {t0 + 20 * kMin, "Blocked", "Running"},
            {t0 + 35 * kMin, "Working", "Running"},
            {t0 + 50 * kMin, "Done", "Running"}};
}

TEST(Timeline, SegmentsFollowTheMarksAndEndAtTheRange) {
    const long long t0 = 1'000'000'000'000LL;
    const auto s = segments(example(t0), t0 - 0, t0 + 60 * kMin);
    ASSERT_EQ(s.size(), 4u);
    EXPECT_EQ(s[0].kind, Kind::kWorking);
    EXPECT_EQ(s[1].kind, Kind::kBlocked);
    EXPECT_EQ(s[1].from_ms, t0 + 20 * kMin);
    EXPECT_EQ(s[1].to_ms, t0 + 35 * kMin);
    EXPECT_EQ(duration_text(s[1].to_ms - s[1].from_ms), "15m");
    EXPECT_EQ(s[3].kind, Kind::kDone);
    EXPECT_EQ(s[3].to_ms, t0 + 60 * kMin);
}

TEST(Timeline, SegmentsAreCutToTheRangeAndStartInTheEarlierState) {
    const long long t0 = 1'000'000'000'000LL;
    // A 15-minute window from 13:25: Blocked (since 13:20) is cut at 13:25.
    const auto s = segments(example(t0), t0 + 25 * kMin, t0 + 40 * kMin);
    ASSERT_EQ(s.size(), 2u);
    EXPECT_EQ(s[0].kind, Kind::kBlocked);
    EXPECT_EQ(s[0].from_ms, t0 + 25 * kMin);
    EXPECT_EQ(s[1].kind, Kind::kWorking);
    EXPECT_EQ(s[1].to_ms, t0 + 40 * kMin);
    EXPECT_TRUE(segments(example(t0), t0 - 60 * kMin, t0 - 30 * kMin).empty());
}

TEST(Timeline, MarksGoingBackInTimeAreDroppedAndCounted) {
    std::vector<Mark> m = {
        {1000, "Working", "Running"}, {500, "Blocked", "Running"}, {2000, "Idle", "Running"}};
    int dropped = 0;
    const auto s = segments(m, 0, 3000, &dropped);
    EXPECT_EQ(dropped, 1);
    ASSERT_EQ(s.size(), 2u);
    EXPECT_EQ(s[0].kind, Kind::kWorking);
    EXPECT_EQ(s[1].kind, Kind::kIdle);
}

TEST(Timeline, UnknownRemembersTheLastKnownStateAndExitEndsTheRow) {
    std::vector<Mark> m = {{0, "Done", "Running"}, {100, "Unknown", "Running"}, {200, "Unknown", "Exited"}};
    const auto s = segments(m, 0, 300);
    ASSERT_EQ(s.size(), 3u);
    EXPECT_EQ(s[1].kind, Kind::kUnknown);
    EXPECT_EQ(s[1].last_known, "Done");
    EXPECT_EQ(s[2].kind, Kind::kExited);
    EXPECT_STREQ(kind_label(Kind::kUnknown), "No status");
    EXPECT_STREQ(kind_label(Kind::kBlocked), "Needs you");
}

TEST(Timeline, PackFoldsThinStretchesIntoTheMostUrgent) {
    // At 1 px per ms: three 1 px stretches (Working, Blocked, Idle) between two wide ones.
    std::vector<Segment> in = {{0, 10, Kind::kWorking},
                               {10, 11, Kind::kWorking},
                               {11, 12, Kind::kBlocked},
                               {12, 13, Kind::kIdle},
                               {13, 40, Kind::kDone}};
    const auto out = pack(in, 1.0, 2.0f);
    ASSERT_EQ(out.size(), 4u);
    EXPECT_EQ(out[1].from_ms, 10);
    EXPECT_EQ(out[1].to_ms, 12);  // two thin ones make 2 px: wide enough, the next starts anew
    EXPECT_EQ(out[1].kind, Kind::kBlocked);
    EXPECT_EQ(out[1].merged, 2);
    EXPECT_EQ(out[2].kind, Kind::kIdle);
    EXPECT_EQ(out[3].kind, Kind::kDone);
    // Wide enough everywhere: unchanged.
    EXPECT_EQ(pack(in, 10.0, 2.0f).size(), in.size());
    EXPECT_GT(attention_rank(Kind::kBlocked), attention_rank(Kind::kDone));
    EXPECT_GT(attention_rank(Kind::kDone), attention_rank(Kind::kWorking));
    EXPECT_GT(attention_rank(Kind::kIdle), attention_rank(Kind::kUnknown));
}

TEST(Timeline, TicksFollowTheRangeAndNeverCrowd) {
    const long long from = 1'000'000'200'000LL;  // not on a whole minute
    for (const char* r : {"15m", "1h", "4h", "24h"}) {
        for (const float w : {320.0f, 900.0f, 2560.0f}) {
            const auto t = ticks(from, from + range_ms(r), w, 0);
            ASSERT_FALSE(t.empty()) << r << " " << w;
            for (std::size_t i = 1; i < t.size(); ++i)
                EXPECT_GE(t[i].x - t[i - 1].x, 56.0f - 0.01f) << r << " " << w;
            EXPECT_GE(t.front().x, 0.0f);
            EXPECT_LE(t.back().x, w + 0.01f);
        }
    }
    // A wide 1 h axis ticks every 15 minutes on the quarter hour.
    const long long hour = 3'600'000LL;
    const long long base = 400'000LL * hour;  // a whole hour
    const auto q = ticks(base + 60'000, base + hour + 60'000, 2000, 0);
    ASSERT_EQ(q.size(), 4u);
    EXPECT_EQ(q[0].at_ms, base + 15 * kMin);
    EXPECT_EQ(q[0].label, "16:15");  // 400'000 h is 16:00 UTC
    // The local offset moves the labels, not the spacing.
    EXPECT_EQ(ticks(base + 60'000, base + hour + 60'000, 2000, 8 * hour)[0].label, "00:15");
}

TEST(Timeline, RangesStepAndFormat) {
    EXPECT_EQ(range_ms("15m"), 15 * kMin);
    EXPECT_EQ(range_ms("24h"), 24 * 60 * kMin);
    EXPECT_EQ(range_ms("bogus"), 60 * kMin);
    EXPECT_EQ(shorter_range("1h"), "15m");
    EXPECT_EQ(shorter_range("15m"), "15m");
    EXPECT_EQ(longer_range("4h"), "24h");
    EXPECT_EQ(longer_range("24h"), "24h");
    EXPECT_EQ(duration_text(45'000), "45s");
    EXPECT_EQ(duration_text(65 * kMin), "1h 05m");
    EXPECT_EQ(duration_text(26 * 60 * kMin), "26h");
    EXPECT_EQ(clock_text(13 * 60 * kMin + 5 * kMin, 0), "13:05");
    EXPECT_EQ(clock_text(23 * 60 * kMin, 2 * 60 * kMin), "01:00");
}

TEST(Timeline, RowsPutNeedsYouFirstAndSubAgentsUnderTheirParent) {
    const std::vector<RowInput> in = {{"a", "", 100, false}, {"b", "", 300, false},
                                      {"c", "", 50, true},   {"a1", "a", 400, false},
                                      {"a2", "a", 10, true}, {"x1", "gone", 20, false}};
    const auto r = rows(in);
    ASSERT_EQ(r.size(), 6u);
    EXPECT_EQ(r[0].id, "c");  // needs you
    EXPECT_EQ(r[1].id, "b");  // newest change
    EXPECT_EQ(r[2].id, "a");
    EXPECT_EQ(r[3].id, "a2");  // a's children: needs you first
    EXPECT_EQ(r[3].depth, 1);
    EXPECT_EQ(r[4].id, "a1");
    EXPECT_EQ(r[5].id, "x1");  // its parent has no row: top level
    EXPECT_EQ(r[5].depth, 0);
    // A loop of parents still lists everyone once.
    EXPECT_EQ(rows({{"p", "q", 1, false}, {"q", "p", 2, false}}).size(), 2u);
}

TEST(Timeline, ParseSessionReadsTheAnswerAndSkipsBadMarks) {
    const auto j = nlohmann::json::parse(R"({"id": "s1", "name": "api", "parent": "", "ended": true,
        "truncated": true, "adopted": false, "extra": 1,
        "marks": [[1000, "Working", "Running"], ["x", "Idle", "Running"], [2000, "Done"], [3000, "Done", "Exited"]]})");
    const auto s = parse_session(j, 500);
    ASSERT_TRUE(s.has_value());
    EXPECT_EQ(s->name, "api");
    EXPECT_TRUE(s->ended);
    EXPECT_TRUE(s->truncated);
    ASSERT_EQ(s->marks.size(), 2u);
    EXPECT_EQ(s->marks[0].at_ms, 1500);  // moved to this computer's clock
    EXPECT_EQ(s->marks[1].process, "Exited");
    EXPECT_FALSE(parse_session(nlohmann::json::parse(R"({"name": "no id"})"), 0).has_value());
    EXPECT_FALSE(parse_session(nlohmann::json::array(), 0).has_value());
    // An answer without the newer fields (or with odd types) still reads.
    const auto bare = parse_session(nlohmann::json::parse(R"({"id": "s2", "ended": "yes"})"), 0);
    ASSERT_TRUE(bare.has_value());
    EXPECT_FALSE(bare->ended);
    EXPECT_TRUE(bare->marks.empty());
}

TEST(Timeline, CommandsAndShortcut) {
    for (const char* c : {"timeline.toggle", "timeline.range"})
        EXPECT_TRUE(is_builtin_command(c)) << c;
    const Keymap k = Keymap::defaults();
    EXPECT_EQ(k.global(*Keymap::parse("ctrl-shift-h")), "timeline.toggle");
}

}  // namespace
}  // namespace agentdesk::ui::timeline
