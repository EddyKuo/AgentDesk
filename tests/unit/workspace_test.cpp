#include "agentdesk/ui/workspace.hpp"

#include <gtest/gtest.h>

#include "agentdesk/ui/keymap.hpp"

using namespace agentdesk::ui;

namespace {
const Rect kArea{0, 0, 1000, 600};
}

TEST(Workspace, SpacesTabsAndFind) {
    Workspace w;
    w.add_space("a");
    w.add_tab("b");
    w.add_space("c");
    ASSERT_EQ(w.spaces.size(), 2u);
    EXPECT_EQ(w.find("b").space, 0);
    EXPECT_EQ(w.find("b").tab, 1);
    EXPECT_EQ(w.find("c").space, 1);
    EXPECT_EQ(w.active_space, 1);
    w.focus("b");
    EXPECT_EQ(w.active_space, 0);
    EXPECT_EQ(w.focused_session(), "b");
}

TEST(Workspace, SplitRightAndDownThenFocusByDirection) {
    Workspace w;
    w.add_space("a");
    ASSERT_TRUE(w.split("a", "b", Dir::kRight));  // a | b
    w.focus("a");
    ASSERT_TRUE(w.split("a", "c", Dir::kDown));  // a over c, left column
    const auto rects = layout_tree(w.active_tab()->root, kArea, 0);
    ASSERT_EQ(rects.size(), 3u);
    EXPECT_LT(rects.at("a").y, rects.at("c").y);
    EXPECT_LT(rects.at("a").x, rects.at("b").x);
    // focus is on c (new pane); right -> b, left -> c... up -> a
    EXPECT_EQ(w.focused_session(), "c");
    EXPECT_TRUE(w.focus_dir(Dir::kUp, kArea));
    EXPECT_EQ(w.focused_session(), "a");
    EXPECT_TRUE(w.focus_dir(Dir::kRight, kArea));
    EXPECT_EQ(w.focused_session(), "b");
    EXPECT_FALSE(w.focus_dir(Dir::kRight, kArea));
}

TEST(Workspace, RemoveCollapsesSplitAndClosesEmptyTabAndSpace) {
    Workspace w;
    w.add_space("a");
    w.split("a", "b", Dir::kRight);
    w.add_tab("c");
    w.remove("b");
    EXPECT_TRUE(w.spaces[0].tabs[0].root.leaf());
    EXPECT_EQ(w.spaces[0].tabs[0].root.session, "a");
    w.remove("c");
    EXPECT_EQ(w.spaces[0].tabs.size(), 1u);
    w.remove("a");
    EXPECT_TRUE(w.spaces.empty());
}

TEST(Workspace, PruneDropsVanishedSessions) {
    Workspace w;
    w.add_space("a");
    w.split("a", "b", Dir::kDown);
    w.add_space("c");
    w.prune({"a"});
    EXPECT_EQ(w.all_sessions(), std::vector<std::string>{"a"});
}

TEST(Workspace, SwapKeepsFocusOnMovedPane) {
    Workspace w;
    w.add_space("a");
    w.split("a", "b", Dir::kRight);
    w.focus("a");
    ASSERT_TRUE(w.swap_dir(Dir::kRight, kArea));
    const auto rects = layout_tree(w.active_tab()->root, kArea, 0);
    EXPECT_GT(rects.at("a").x, rects.at("b").x);
    EXPECT_EQ(w.focused_session(), "a");
}

TEST(Workspace, ResizeMovesBorderAndClamps) {
    Workspace w;
    w.add_space("a");
    w.split("a", "b", Dir::kRight);
    w.focus("a");
    ASSERT_TRUE(w.resize(Dir::kRight, 100, kArea));
    EXPECT_NEAR(w.active_tab()->root.ratio, 0.6f, 0.001f);
    for (int i = 0; i < 20; ++i)
        w.resize(Dir::kRight, 100, kArea);
    EXPECT_NEAR(w.active_tab()->root.ratio, 0.9f, 0.001f);
    EXPECT_FALSE(w.resize(Dir::kDown, 50, kArea));  // no stacked split
}

TEST(Workspace, ZoomTogglesAndUnzoomsOnFocusElsewhere) {
    Workspace w;
    w.add_space("a");
    w.split("a", "b", Dir::kRight);
    w.toggle_zoom();
    EXPECT_EQ(w.active_tab()->zoomed, "b");
    w.focus("a");
    EXPECT_TRUE(w.active_tab()->zoomed.empty());
}

TEST(Workspace, JsonRoundTrip) {
    Workspace w;
    w.add_space("a", "api");
    w.split("a", "b", Dir::kDown);
    w.active_tab()->name = "fix login";
    w.active_tab()->pane_names["b"] = "tests";
    w.add_space("c");
    const auto j = w.to_json();
    const auto r = Workspace::from_json(j);
    ASSERT_EQ(r.spaces.size(), 2u);
    EXPECT_EQ(r.spaces[0].name, "api");
    EXPECT_EQ(r.spaces[0].tabs[0].name, "fix login");
    EXPECT_TRUE(r.spaces[0].tabs[0].root.children[0].session == "a");
    EXPECT_TRUE(r.spaces[0].tabs[0].root.vertical);
    EXPECT_EQ(r.spaces[0].tabs[0].pane_names.at("b"), "tests");
    EXPECT_EQ(r.active_space, 1);
    EXPECT_TRUE(Workspace::from_json(nlohmann::json::object()).spaces.empty());
}

TEST(Workspace, SpaceTitleFollowsFirstTerminalFolderUntilNamed) {
    Workspace w;
    w.add_space("a");
    const std::map<std::string, std::string> cwd{{"a", "C:\\work\\api-server"}};
    EXPECT_EQ(w.space_title(0, cwd), "api-server");
    w.spaces[0].name = "backend";
    EXPECT_EQ(w.space_title(0, cwd), "backend");
}

TEST(Workspace, DividersMatchSplits) {
    Workspace w;
    w.add_space("a");
    w.split("a", "b", Dir::kRight);
    w.split("b", "c", Dir::kDown);
    const auto d = dividers(w.active_tab()->root, kArea, 8);
    ASSERT_EQ(d.size(), 2u);
    EXPECT_FALSE(d[0].vertical);
    EXPECT_TRUE(d[1].vertical);
    EXPECT_NE(node_at(w.active_tab()->root, d[1].path), nullptr);
}

TEST(Attention, DoneOnlyWhenFinishedUnseenAndClearsWhenSeen) {
    AttentionTracker t;
    EXPECT_FALSE(t.update("a", "Idle", false));  // first listing: never alerts
    EXPECT_FALSE(t.update("a", "Working", false));
    EXPECT_EQ(t.get("a"), Attention::kWorking);
    EXPECT_TRUE(t.update("a", "Idle", false));  // finished while not looking
    EXPECT_EQ(t.get("a"), Attention::kDone);
    t.seen("a");
    EXPECT_EQ(t.get("a"), Attention::kIdle);
    // Finished while looking: no attention.
    t.update("a", "Working", true);
    EXPECT_FALSE(t.update("a", "Idle", true));
    EXPECT_EQ(t.get("a"), Attention::kIdle);
}

TEST(Attention, BlockedNotifiesOnceAndRanksFirst) {
    AttentionTracker t;
    t.update("a", "Working", false);
    EXPECT_TRUE(t.update("a", "Blocked", false));
    EXPECT_FALSE(t.update("a", "Blocked", false));
    EXPECT_EQ(t.get("a"), Attention::kBlocked);
    EXPECT_EQ(most_urgent({Attention::kIdle, Attention::kDone, Attention::kBlocked, Attention::kWorking}),
              Attention::kBlocked);
    EXPECT_EQ(most_urgent({Attention::kIdle, Attention::kWorking, Attention::kDone}), Attention::kDone);
    EXPECT_EQ(t.get("unknown"), Attention::kNone);
}

TEST(Keymap, ParsesChordsAndAppliesOverrides) {
    EXPECT_EQ(*Keymap::parse("ctrl-shift-t"), ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_T);
    EXPECT_EQ(*Keymap::parse("ctrl--"), ImGuiMod_Ctrl | ImGuiKey_Minus);
    EXPECT_EQ(*Keymap::parse("ctrl-?"), ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Slash);
    EXPECT_EQ(*Keymap::parse("alt-1"), ImGuiMod_Alt | ImGuiKey_1);
    EXPECT_EQ(*Keymap::parse("F2"), ImGuiKey_F2);
    EXPECT_FALSE(Keymap::parse("hyper-x").has_value());
    auto k = Keymap::defaults();
    EXPECT_EQ(*k.global(*Keymap::parse("ctrl-shift-t")), "tab.new");
    EXPECT_EQ(*k.terminal(*Keymap::parse("ctrl-shift-f")), "terminal.search");
    const auto bad =
        k.apply({{"global", {{"ctrl-shift-t", nullptr}, {"ctrl-alt-n", "space.new"}, {"??x", "a"}}}});
    EXPECT_EQ(bad, std::vector<std::string>{"??x"});
    EXPECT_FALSE(k.global(*Keymap::parse("ctrl-shift-t")).has_value());
    EXPECT_EQ(*k.global(*Keymap::parse("ctrl-alt-n")), "space.new");
}

// herdr spaces: colours go to the least-used name, spaces move only within their machine.
TEST(Workspace, SpaceColorsAndMoves) {
    Workspace w;
    w.add_space("a");
    w.add_space("b");
    w.add_space("c");
    w.spaces[2].machine = "box";
    w.assign_colors();
    EXPECT_EQ(w.spaces[0].color, "ansi6");
    EXPECT_EQ(w.spaces[1].color, "ansi3");
    EXPECT_EQ(w.spaces[2].color, "ansi4");
    w.select_space(0);
    EXPECT_TRUE(w.move_space(0, 1));
    EXPECT_EQ(w.spaces[1].tabs[0].focused, "a");
    EXPECT_EQ(w.active_space, 1);
    EXPECT_FALSE(w.move_space(1, 2));  // other machine
    w.pinned.insert("b");
    w.spaces[0].panel = "changes";
    const auto back = Workspace::from_json(w.to_json());
    EXPECT_EQ(back.spaces[0].panel, "changes");
    EXPECT_EQ(back.spaces[0].color, "ansi3");
    EXPECT_TRUE(back.pinned.contains("b"));
}

// herdr pane drag: drop on an edge moves the pane there; in the middle the two swap.
TEST(Workspace, MovePaneToEdgeOrSwap) {
    Workspace w;
    w.add_space("a");
    ASSERT_TRUE(w.split("a", "b", Dir::kRight));
    ASSERT_TRUE(w.split("b", "c", Dir::kDown));
    const Rect area{0, 0, 300, 200};
    // c (bottom right) onto the left edge of a: c | a | b-ish layout, c leftmost.
    ASSERT_TRUE(w.move_pane("c", "a", Dir::kLeft));
    auto r = layout_tree(w.active_tab()->root, area, 0);
    EXPECT_LT(r["c"].x, r["a"].x);
    EXPECT_EQ(w.focused_session(), "c");
    // swap a and b
    const float ax = r["a"].x, bx = r["b"].x;
    ASSERT_TRUE(w.move_pane("a", "b", std::nullopt));
    r = layout_tree(w.active_tab()->root, area, 0);
    EXPECT_EQ(r["a"].x, bx);
    EXPECT_EQ(r["b"].x, ax);
    EXPECT_FALSE(w.move_pane("a", "a", std::nullopt));
}

// herdr: needs-you alerts even while looking; error counts as needs you; a first listing that
// is already done shows done without alerting.
TEST(Attention, HerdrAlertRules) {
    AttentionTracker t;
    t.update("a", "Working", true);
    EXPECT_TRUE(t.update("a", "Blocked", true));
    EXPECT_FALSE(t.update("a", "Error", true));  // still needs you: no second alert
    EXPECT_EQ(t.get("a"), Attention::kBlocked);
    EXPECT_FALSE(t.update("b", "Done", false));
    EXPECT_EQ(t.get("b"), Attention::kDone);
    t.seen("b");
    EXPECT_EQ(t.get("b"), Attention::kIdle);
}
