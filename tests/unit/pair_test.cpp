// Pair (libs/ui pair): the pair's life, the race from structured states, worktree and session
// names, the default programs, and its commands, keys, layout and tips.
#include "agentdesk/ui/pair.hpp"

#include <algorithm>

#include <gtest/gtest.h>

#include "agentdesk/ui/command_names.hpp"
#include "agentdesk/ui/keymap.hpp"
#include "agentdesk/ui/layout.hpp"
#include "agentdesk/ui/layout_styles.hpp"
#include "agentdesk/ui/layout_tips.hpp"

namespace pair = agentdesk::ui::pair;
using pair::Phase;
using pair::Side;

// ---------------------------------------------------------------- the pair's life
TEST(Pair, StartsWaitsForBothSidesThenRuns) {
    pair::Model m;
    EXPECT_EQ(m.phase(), Phase::kSetup);
    ASSERT_TRUE(m.start());
    EXPECT_EQ(m.phase(), Phase::kStarting);
    EXPECT_FALSE(m.start());      // one start at a time
    m.placed(Side::kRight, "b");  // the right one may answer first
    EXPECT_EQ(m.phase(), Phase::kStarting);
    m.placed(Side::kLeft, "a");
    EXPECT_EQ(m.phase(), Phase::kRunning);
    EXPECT_EQ(m.id(Side::kLeft), "a");
    EXPECT_EQ(m.id(Side::kRight), "b");
    EXPECT_EQ(m.side_of("b"), Side::kRight);
    EXPECT_FALSE(m.side_of("c"));
    EXPECT_FALSE(m.has(""));
}

TEST(Pair, AFailedStartGoesBackToSetupWithWhy) {
    pair::Model m;
    m.start();
    m.placed(Side::kLeft, "a");
    m.failed("worktree exists");
    EXPECT_EQ(m.phase(), Phase::kSetup);
    EXPECT_EQ(m.error(), "worktree exists");
    EXPECT_TRUE(m.id(Side::kLeft).empty());
    m.placed(Side::kRight, "b");  // a late answer does not form a pair
    EXPECT_EQ(m.phase(), Phase::kSetup);
    m.start();
    EXPECT_TRUE(m.error().empty());  // a new start clears it
}

TEST(Pair, ASideThatEndsLeavesHalfAPairThenNone) {
    pair::Model m;
    ASSERT_TRUE(m.adopt("a", "b"));
    EXPECT_EQ(m.phase(), Phase::kRunning);
    m.prune({"a", "b", "c"});
    EXPECT_EQ(m.phase(), Phase::kRunning);
    m.prune({"b"});
    EXPECT_EQ(m.phase(), Phase::kHalf);
    EXPECT_TRUE(m.id(Side::kLeft).empty());
    m.prune({});
    EXPECT_EQ(m.phase(), Phase::kSetup);
    EXPECT_FALSE(m.adopt("a", "a"));
    EXPECT_FALSE(m.adopt("", "b"));
}

TEST(Pair, StartingIsNotPrunedWhileSessionsAreOnTheirWay) {
    pair::Model m;
    m.start();
    m.placed(Side::kLeft, "a");
    m.prune({});
    EXPECT_EQ(m.phase(), Phase::kStarting);
    EXPECT_EQ(m.id(Side::kLeft), "a");
}

TEST(Pair, RepairSwapAndClose) {
    pair::Model m;
    m.adopt("a", "b");
    EXPECT_TRUE(m.repair(Side::kRight, "c"));
    EXPECT_EQ(m.id(Side::kRight), "c");
    EXPECT_FALSE(m.repair(Side::kRight, "c"));  // already there
    EXPECT_TRUE(m.repair(Side::kLeft, "c"));    // the other side's: they swap
    EXPECT_EQ(m.id(Side::kLeft), "c");
    EXPECT_EQ(m.id(Side::kRight), "a");
    EXPECT_TRUE(m.swap());
    EXPECT_EQ(m.id(Side::kLeft), "a");
    EXPECT_EQ(m.id(Side::kRight), "c");
    m.prune({"a"});
    ASSERT_EQ(m.phase(), Phase::kHalf);
    EXPECT_TRUE(m.repair(Side::kRight, "d"));  // a new agent mends half a pair
    EXPECT_EQ(m.phase(), Phase::kRunning);
    m.close();
    EXPECT_EQ(m.phase(), Phase::kSetup);
    EXPECT_FALSE(m.swap());
    EXPECT_FALSE(m.repair(Side::kLeft, "a"));
}

// ---------------------------------------------------------------- the race
TEST(PairRace, WorkingThenDoneOnBothSidesGivesTheResult) {
    pair::Race r;
    r.sent(Side::kLeft, 1000);
    r.sent(Side::kRight, 1000);
    r.observe(Side::kLeft, "Working", 1500, 1500);
    r.observe(Side::kRight, "Working", 1600, 1600);
    EXPECT_TRUE(r.runner(Side::kLeft).running());
    EXPECT_EQ(r.runner(Side::kLeft).started_ms, 1000);  // the prompt started the turn
    EXPECT_EQ(r.runner(Side::kLeft).elapsed(4000), 3000);
    r.observe(Side::kLeft, "Done", 31000, 31000);
    EXPECT_EQ(r.first_finished(), Side::kLeft);
    EXPECT_FALSE(r.result());
    r.observe(Side::kRight, "Blocked", 35000, 35000);  // asking the person: still running
    EXPECT_TRUE(r.runner(Side::kRight).running());
    r.observe(Side::kRight, "Working", 36000, 36000);
    r.observe(Side::kRight, "Done", 43000, 43000);
    const auto res = r.result();
    ASSERT_TRUE(res);
    EXPECT_EQ(res->winner, Side::kLeft);
    EXPECT_EQ(res->elapsed[0], 30000);
    EXPECT_EQ(res->elapsed[1], 42000);
    EXPECT_EQ(res->margin_ms, 12000);
    EXPECT_EQ(pair::headline(*res, "Claude Code (A)", "Codex (B)"),
              "Claude Code (A) finished first, 12s ahead");
}

TEST(PairRace, DoneWithoutWorkingIsNotAFinish) {
    pair::Race r;
    r.observe(Side::kLeft, "Done", 100, 100);  // done before the pair began: nothing to time
    EXPECT_FALSE(r.active());
    r.sent(Side::kLeft, 200);
    r.observe(Side::kLeft, "Idle", 300, 300);
    r.observe(Side::kLeft, "Done", 400, 400);  // never worked on this prompt
    EXPECT_FALSE(r.runner(Side::kLeft).finished());
}

TEST(PairRace, WorkingSeenFirstStartsTheTurnAtTheHostsTime) {
    pair::Race r;
    r.observe(Side::kRight, "Working", 0, 5000);  // no host time: now
    EXPECT_EQ(r.runner(Side::kRight).started_ms, 5000);
    r.observe(Side::kRight, "Working", 9000, 9000);  // a repeat changes nothing
    EXPECT_EQ(r.runner(Side::kRight).started_ms, 5000);
    r.observe(Side::kLeft, "Working", 4000, 6000);
    EXPECT_EQ(r.runner(Side::kLeft).started_ms, 4000);
}

TEST(PairRace, CarryingOnAfterDoneReopensTheTurn) {
    pair::Race r;
    r.observe(Side::kLeft, "Working", 1000, 1000);
    r.observe(Side::kLeft, "Done", 2000, 2000);
    EXPECT_TRUE(r.runner(Side::kLeft).finished());
    EXPECT_EQ(r.first_finished(), std::nullopt);  // the other side never started
    r.observe(Side::kLeft, "Working", 3000, 3000);
    EXPECT_TRUE(r.runner(Side::kLeft).running());
    EXPECT_EQ(r.runner(Side::kLeft).started_ms, 1000);
}

TEST(PairRace, ANewRoundAfterBothFinished) {
    pair::Race r;
    for (const Side s : {Side::kLeft, Side::kRight})
        r.observe(s, "Working", 1000, 1000);
    r.observe(Side::kLeft, "Done", 2000, 2000);
    r.observe(Side::kRight, "Done", 2500, 2500);
    ASSERT_TRUE(r.result());
    EXPECT_FALSE(r.result()->winner);  // 500 ms apart: a tie
    EXPECT_EQ(pair::headline(*r.result(), "A", "B"), "A dead heat");
    r.observe(Side::kRight, "Working", 9000, 9000);  // a new task on one side
    EXPECT_FALSE(r.result());
    EXPECT_EQ(r.runner(Side::kRight).started_ms, 9000);
    EXPECT_EQ(r.runner(Side::kLeft).started_ms, 0);
    // A prompt sent after a finished round starts over too.
    r.observe(Side::kRight, "Done", 9500, 9500);
    r.sent(Side::kLeft, 10000);
    EXPECT_EQ(r.runner(Side::kLeft).started_ms, 10000);
}

TEST(PairRace, SwapMovesTheRunnersWithTheirSides) {
    pair::Race r;
    r.observe(Side::kLeft, "Working", 1000, 1000);
    r.swap();
    EXPECT_EQ(r.runner(Side::kRight).started_ms, 1000);
    EXPECT_EQ(r.runner(Side::kLeft).started_ms, 0);
    r.observe(Side::kRight, "Working", 5000, 5000);  // the same state on the same agent: no change
    EXPECT_EQ(r.runner(Side::kRight).started_ms, 1000);
}

TEST(PairRace, Durations) {
    EXPECT_EQ(pair::format_duration(0), "0s");
    EXPECT_EQ(pair::format_duration(8400), "8s");
    EXPECT_EQ(pair::format_duration(185000), "3m 05s");
    EXPECT_EQ(pair::format_duration(3720000), "1h 02m");
    EXPECT_EQ(pair::format_duration(-5), "0s");
    EXPECT_EQ(pair::changes_text(3, 42, 7), "3 files  +42 -7");
    EXPECT_EQ(pair::changes_text(1, 3, 0), "1 file  +3 -0");
    EXPECT_EQ(pair::changes_text(0, 0, 0), "No changes");
    EXPECT_EQ(pair::changes_text(-1, 0, 0), "Reading changes...");
}

// ---------------------------------------------------------------- setup
TEST(Pair, WorktreeBranchNames) {
    const std::string stamp = pair::stamp_of(11, 21, 9, 5);
    EXPECT_EQ(stamp, "1121-0905");
    EXPECT_EQ(pair::branch_name("claude", Side::kLeft, stamp), "pair/1121-0905-claude-a");
    EXPECT_EQ(pair::branch_name("Codex CLI", Side::kRight, stamp), "pair/1121-0905-codexcli-b");
    EXPECT_EQ(pair::branch_name("??", Side::kRight, stamp), "pair/1121-0905-agent-b");
    // The same program on both sides still gets two branches.
    EXPECT_NE(pair::branch_name("claude", Side::kLeft, stamp),
              pair::branch_name("claude", Side::kRight, stamp));
    EXPECT_EQ(pair::session_name("Codex", Side::kRight), "Codex (B)");
}

TEST(Pair, DefaultProgramsPreferClaudeCodeAndCodex) {
    using Profiles = std::vector<std::pair<std::string, std::string>>;
    EXPECT_EQ(pair::default_profiles(
                  Profiles{{"pwsh", "PowerShell"}, {"codex", "Codex"}, {"claude", "Claude Code"}}),
              (std::pair<std::string, std::string>{"claude", "codex"}));
    EXPECT_EQ(pair::default_profiles(Profiles{{"pwsh", "PowerShell"}, {"cc", "Claude Code"}}),
              (std::pair<std::string, std::string>{"cc", "cc"}));
    EXPECT_EQ(pair::default_profiles(Profiles{{"a", "A"}, {"b", "B"}}),
              (std::pair<std::string, std::string>{"a", "b"}));
    EXPECT_EQ(pair::default_profiles(Profiles{{"a", "A"}}), (std::pair<std::string, std::string>{"a", "a"}));
    EXPECT_EQ(pair::default_profiles({}), (std::pair<std::string, std::string>{}));
}

// ---------------------------------------------------------------- commands, keys, layout, tips
TEST(Pair, CommandsAreKnownAndTargetsParse) {
    for (const auto& c : pair::command_names())
        EXPECT_TRUE(agentdesk::ui::is_builtin_command(c)) << c;
    EXPECT_EQ(pair::parse_target(""), pair::Target::kBoth);
    EXPECT_EQ(pair::parse_target("both"), pair::Target::kBoth);
    EXPECT_EQ(pair::parse_target("left"), pair::Target::kLeft);
    EXPECT_EQ(pair::parse_target("right"), pair::Target::kRight);
    EXPECT_FALSE(pair::parse_target("middle"));
    EXPECT_EQ(pair::parse_side("right"), Side::kRight);
    EXPECT_FALSE(pair::parse_side("up"));
}

TEST(Pair, KeysOnlyWhilePairShows) {
    using agentdesk::ui::Keymap;
    Keymap k = Keymap::defaults();
    const auto defaults = Keymap::default_file();
    const auto key_of = [&](const char* section, const char* command) {
        for (const auto& [chord, cmd] : defaults.at(section).items())
            if (cmd == command)
                return Keymap::parse(chord);
        return std::optional<ImGuiKeyChord>();
    };
    const auto prompt = key_of("layout:pair", "pair.prompt");
    const auto swap = key_of("layout:pair", "pair.swap");
    const auto toggle = key_of("global", "pair.toggle");
    ASSERT_TRUE(prompt && swap && toggle);
    k.set_layout("workspace");
    EXPECT_FALSE(k.global(*prompt).has_value());  // elsewhere the key reaches the terminal
    k.set_layout("pair");
    EXPECT_EQ(k.global(*prompt), "pair.prompt");
    EXPECT_EQ(k.global(*swap), "pair.swap");
    // The toggle key works from any layout, and nothing else has that chord by default.
    for (const auto& l : agentdesk::ui::layout::builtin_names()) {
        k.set_layout(l);
        EXPECT_EQ(k.global(*toggle), "pair.toggle") << l;
    }
    int uses = 0;
    for (const auto& [section, keys] : defaults.items())
        for (const auto& [chord, cmd] : keys.items())
            uses += Keymap::parse(chord) == toggle ? 1 : 0;
    EXPECT_EQ(uses, 1);
}

TEST(Pair, IsABuiltInLayoutWithItsOwnStyleAndTips) {
    namespace layout = agentdesk::ui::layout;
    namespace styles = agentdesk::ui::styles;
    const auto names = layout::builtin_names();
    EXPECT_NE(std::find(names.begin(), names.end(), "pair"), names.end());
    const auto tree = layout::builtin("pair");
    EXPECT_EQ(styles::style_of_tree(tree), "pair");
    for (const char* m : {"pair-bar", "pair-stage", "pair-prompt"}) {
        EXPECT_EQ(styles::style_of_mod(m), "pair") << m;
        bool found = false;
        for (const auto* n : tree.walk())
            found = found || n->mod() == m;
        EXPECT_TRUE(found) << m;
    }
    EXPECT_EQ(styles::display_name("pair"), "Pair");
    EXPECT_FALSE(styles::is_fun_layout("pair"));         // a main layout, listed by default
    EXPECT_GT(styles::toast_area("pair").offset, 0.0f);  // toasts stay above the prompt bar
    const auto card = agentdesk::ui::tips::card("pair");
    ASSERT_TRUE(card);
    EXPECT_EQ(card->title, "Pair");
    EXPECT_EQ(agentdesk::ui::tips::card_key("pair", "pair", false), "pair");
}
