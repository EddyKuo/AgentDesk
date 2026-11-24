// Portions adapted from herdr-app (Apache-2.0) and modified; see THIRD_PARTY_NOTICES.md.
// Tests for the workspace model, attention tracking, keymap files and the agents-list helpers.
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
    auto share = [&] {
        const auto& ws = w.active_tab()->root.weights;
        return ws[0] / (ws[0] + ws[1]);
    };
    ASSERT_TRUE(w.resize(Dir::kRight, 100, kArea));
    EXPECT_NEAR(share(), 0.6f, 0.002f);
    for (int i = 0; i < 20; ++i)
        w.resize(Dir::kRight, 100, kArea);
    EXPECT_NEAR(share(), 0.95f, 0.002f);
    EXPECT_FALSE(w.resize(Dir::kDown, 50, kArea));  // no stacked split
}

// Focusing another pane of a zoomed tab moves the zoom to it.
TEST(Workspace, ZoomTogglesAndMovesWithFocus) {
    Workspace w;
    w.add_space("a");
    w.split("a", "b", Dir::kRight);
    w.toggle_zoom();
    EXPECT_EQ(w.active_tab()->zoomed, "b");
    w.focus("a");
    EXPECT_EQ(w.active_tab()->zoomed, "a");
    w.toggle_zoom();
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

// The ui.state document comes from the host (other windows, other versions): wrongly typed
// fields count as missing and never throw.
TEST(Workspace, MalformedDocumentLoadsWhatIsValid) {
    const auto doc = nlohmann::json::parse(R"({
        "version": 1, "active_space": "x", "pinned": {"a": 1}, "right_click_program": [1, "codex"],
        "spaces": [
            7, "junk",
            {"id": 5, "name": ["n"], "machine": null, "color": {}, "panel": 1, "folder": true,
             "active_tab": "0", "tabs": {"not": "an array"}},
            {"name": "ok", "active_tab": 9.5, "tabs": [
                3,
                {"id": [], "name": 1, "focused": 2, "zoomed": {}, "settled": "yes", "pane_names": {"a": 1, "b": "B"},
                 "root": {"split": 5, "children": [{"session": 1}, {"session": "a"}, {"session": "b"}],
                          "weights": ["x", 1, null]}},
                {"root": {"split": "down", "a": {"session": "c"}, "b": {"session": "d"}, "ratio": "half"}}
            ]}
        ]})");
    Workspace r;
    ASSERT_NO_THROW(r = Workspace::from_json(doc));
    ASSERT_EQ(r.spaces.size(), 1u);
    EXPECT_EQ(r.spaces[0].name, "ok");
    ASSERT_EQ(r.spaces[0].tabs.size(), 2u);
    const auto& t = r.spaces[0].tabs[0];
    EXPECT_EQ(t.name, "");
    EXPECT_FALSE(t.root.vertical);
    EXPECT_EQ(t.pane_names.size(), 1u);
    EXPECT_EQ(t.pane_names.at("b"), "B");
    EXPECT_EQ(r.spaces[0].active_tab, 1);
    EXPECT_EQ(r.spaces[0].tabs[1].root.children.size(), 2u);
    EXPECT_TRUE(r.pinned.empty());
    EXPECT_EQ(r.right_click_program.size(), 1u);
    EXPECT_EQ(r.active_space, 0);
    EXPECT_TRUE(
        Workspace::from_json(nlohmann::json::parse(R"({"version": "1", "spaces": []})")).spaces.empty());
    EXPECT_TRUE(Workspace::from_json(nlohmann::json::array()).spaces.empty());
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

#ifdef _WIN32  // the Windows key table; macOS: KeymapMac below
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
    // A file with a problem installs nothing: the defaults stay.
    auto bad = k.apply({{"global", {{"ctrl-shift-t", nullptr}, {"??x", "a"}}}});
    ASSERT_EQ(bad.size(), 1u);
    EXPECT_NE(bad[0].find("??x"), std::string::npos);
    EXPECT_EQ(*k.global(*Keymap::parse("ctrl-shift-t")), "tab.new");
    bad = k.apply({{"global", {{"ctrl-shift-t", nullptr}, {"ctrl-alt-n", "space.new"}}}});
    EXPECT_TRUE(bad.empty());
    EXPECT_FALSE(k.global(*Keymap::parse("ctrl-shift-t")).has_value());
    EXPECT_EQ(*k.global(*Keymap::parse("ctrl-alt-n")), "space.new");
}
#endif

#ifdef _WIN32  // the Windows key table; macOS: KeymapMac below
// keys.json forms: layout sections win over global, "terminal" gives the key to the
// program, {"command","args"} objects, old names migrated, unknown sections/actions reported.
TEST(Keymap, SectionsObjectsAndValidation) {
    auto k = Keymap::defaults();
    const auto known = [](std::string_view n) { return n != "no.such"; };
    const auto bad = k.apply(
        {{"global", {{"ctrl-shift-y", {{"command", "layout.switch"}, {"args", {{"name", "grid"}}}}},
                     {"ctrl-shift-u", "project.new"}}},
         {"layout:grid", {{"ctrl-shift-t", "terminal"}, {"ctrl-shift-j", "tab.next"}}},
         {"terminal", {{"ctrl-shift-f", "terminal"}}}},
        known);
    EXPECT_TRUE(bad.empty()) << bad.front();
    EXPECT_EQ(*k.global(*Keymap::parse("ctrl-shift-y")), "layout.switch:grid");
    EXPECT_EQ(*k.global(*Keymap::parse("ctrl-shift-u")), "space.new");
    EXPECT_EQ(*k.global(*Keymap::parse("ctrl-shift-t")), "tab.new");  // other layouts
    k.set_layout("grid");
    EXPECT_FALSE(k.global(*Keymap::parse("ctrl-shift-t")).has_value());  // to the program
    EXPECT_EQ(*k.global(*Keymap::parse("ctrl-shift-j")), "tab.next");
    EXPECT_EQ(*k.terminal(*Keymap::parse("ctrl-shift-f")), "terminal");
    EXPECT_EQ(*k.terminal(*Keymap::parse("ctrl-v")), "terminal.paste_image");

    const auto errs = k.apply({{"windows", {{"f5", "tab.new"}}}, {"global", {{"f6", "no.such"}}}}, known);
    EXPECT_EQ(errs.size(), 2u);
}
#endif

// Space colours go to the least-used name, spaces move only within their machine.
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

// Pane drag: drop on an edge moves the pane there; in the middle the two swap.
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

// Needs-you alerts even while looking; error counts as needs you; a first listing that
// is already done shows done without alerting.
TEST(Attention, AlertRules) {
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

// A settled tab leaves the bar and tab cycling, keeps its terminals, and comes
// back when shown (focus, select) — the last shown tab of a space cannot be put away.
TEST(Workspace, SettledTabsAreSkippedUntilShown) {
    Workspace w;
    w.add_space("a");
    w.add_tab("b");
    w.add_tab("c");
    w.select_tab(1);
    ASSERT_TRUE(w.settle_tab(0, 1));
    EXPECT_EQ(w.spaces[0].active_tab, 2);  // the next tab still shown
    EXPECT_EQ(w.find("b").space, 0);       // still there, still running
    w.cycle_tab(1);
    EXPECT_EQ(w.spaces[0].active_tab, 0);
    w.cycle_tab(1);
    EXPECT_EQ(w.spaces[0].active_tab, 2);  // skips the settled tab
    w.select_shown_tab(2);
    EXPECT_EQ(w.spaces[0].active_tab, 2);  // Ctrl+2 counts shown tabs only

    ASSERT_TRUE(w.settle_tab(0, 0));
    EXPECT_FALSE(w.settle_tab(0, 2));  // last one shown
    EXPECT_TRUE(w.unsettle_session("a"));
    EXPECT_FALSE(w.spaces[0].tabs[0].settled);
    w.focus("b");
    EXPECT_FALSE(w.spaces[0].tabs[1].settled);
    EXPECT_EQ(w.spaces[0].active_tab, 1);

    ASSERT_TRUE(w.settle_tab(0, 0));
    const auto back = Workspace::from_json(w.to_json());
    EXPECT_TRUE(back.spaces[0].tabs[0].settled);
    EXPECT_FALSE(back.spaces[0].tabs[1].settled);
}

#ifdef _WIN32  // the Windows key table; macOS: KeymapMac below
// The defaults are written as keys.json when the file is missing: that file must apply cleanly
// and change nothing.
TEST(Keymap, DefaultFileAppliesCleanly) {
    auto k = Keymap::defaults();
    const auto file = Keymap::default_file();
    EXPECT_TRUE(k.apply(file, [](std::string_view) { return true; }).empty());
    EXPECT_EQ(*k.global(*Keymap::parse("ctrl-shift-t")), "tab.new");
    EXPECT_EQ(*k.global(*Keymap::parse("ctrl-3")), "tab.select:3");
    EXPECT_EQ(*k.terminal(*Keymap::parse("ctrl-v")), "terminal.paste_image");
    EXPECT_EQ(Keymap::defaults().global_map(), k.global_map());
}
#endif

// Split, close and tab rules: splitting along the container's axis adds a sibling sharing
// the target's weight; closing gives the space back to all siblings; focus returns to the pane
// focused before; new tabs go right after the shown one; closing the shown tab shows the one
// that slides into its slot; moving a tab keeps the shown one.
TEST(Workspace, SplitCloseAndTabRules) {
    Workspace w;
    w.add_space("a");
    w.split("a", "b", Dir::kRight);
    w.split("b", "c", Dir::kRight);  // same axis: one row [a, b, c]
    const Node& root = w.active_tab()->root;
    ASSERT_EQ(root.children.size(), 3u);
    EXPECT_FLOAT_EQ(root.weights[0], 1.0f);
    EXPECT_FLOAT_EQ(root.weights[1], 0.5f);
    EXPECT_FLOAT_EQ(root.weights[2], 0.5f);
    w.focus("a");
    w.focus("c");
    w.remove("c");  // back to a, the pane focused before c
    EXPECT_EQ(w.focused_session(), "a");
    ASSERT_EQ(w.active_tab()->root.children.size(), 2u);

    // Round trip keeps the n-ary tree.
    w.split("a", "d", Dir::kDown);
    const auto back = Workspace::from_json(w.to_json());
    EXPECT_EQ(back.spaces[0].tabs[0].root.children.size(), 2u);
    EXPECT_TRUE(back.spaces[0].tabs[0].root.children[0].vertical);

    Workspace t;
    t.add_space("x");
    t.add_tab("y");
    t.select_tab(0);
    t.add_tab("z");  // after the shown tab x
    EXPECT_EQ(t.spaces[0].tabs[1].root.session, "z");
    EXPECT_EQ(t.spaces[0].active_tab, 1);
    t.close_tab(0, 1);  // y slides into the slot
    EXPECT_EQ(t.spaces[0].tabs[t.spaces[0].active_tab].root.session, "y");
    t.select_tab(0);
    ASSERT_TRUE(t.move_tab(0, 1, 0));  // move y; x stays shown
    EXPECT_EQ(t.spaces[0].tabs[t.spaces[0].active_tab].root.session, "x");
}

// Focusing another pane of a zoomed tab moves the zoom; pane.go works while zoomed.
TEST(Workspace, ZoomFollowsFocus) {
    Workspace w;
    w.add_space("a");
    w.split("a", "b", Dir::kRight);
    w.toggle_zoom("b");
    EXPECT_EQ(w.active_tab()->zoomed, "b");
    ASSERT_TRUE(w.focus_dir(Dir::kLeft, kArea));
    EXPECT_EQ(w.focused_session(), "a");
    EXPECT_EQ(w.active_tab()->zoomed, "a");
}

// Space title: the first terminal's checkout top, else the space's own folder; home is "~".
TEST(Workspace, SpaceTitleUsesCheckoutTopThenCreationFolder) {
    Workspace w;
    w.add_space("a");
    w.add_tab("b", 0);
    // Only the first terminal names it: a known folder of a later one does not.
    EXPECT_EQ(w.space_title(0, {{"b", "C:\\work\\other"}}), "~");
    w.remember_folders({{"a", "C:\\work\\made-here"}, {"b", "C:\\work\\other"}});
    EXPECT_EQ(w.spaces[0].folder, "C:\\work\\made-here");
    w.remember_folders({{"a", "C:\\elsewhere"}});  // kept once known
    EXPECT_EQ(w.spaces[0].folder, "C:\\work\\made-here");
    EXPECT_EQ(w.space_title(0, {}), "made-here");
    EXPECT_EQ(w.space_title(0, {{"a", "C:\\work\\repo-wt\\"}}), "repo-wt");
    const auto back = Workspace::from_json(w.to_json());
    EXPECT_EQ(back.spaces[0].folder, "C:\\work\\made-here");
}

TEST(Workspace, HomeSpaceGetsNoColourUntilNamed) {
    Workspace w;
    w.add_space("a");
    w.add_space("b");
    const std::map<std::string, std::string> tops{{"b", "C:\\work\\api"}};
    w.assign_colors(&tops);
    EXPECT_TRUE(w.spaces[0].color.empty());  // "~"
    EXPECT_EQ(w.spaces[1].color, "ansi6");
    w.spaces[0].name = "home";
    w.assign_colors(&tops);
    EXPECT_EQ(w.spaces[0].color, "ansi3");
}

// Worktrees go right after their repo's main checkout, and the model
// order follows so numbers and keys match what is drawn.
TEST(Workspace, WorktreesNestUnderMainCheckoutAndModelFollows) {
    const std::vector<SpaceRepo> info = {
            {true, true, "r1"},   // 0: worktree of r1, before its main checkout
            {true, false, "r2"},  // 1
            {false, false, ""},   // 2: not a repo
            {true, false, "r1"},  // 3: main checkout of r1
            {true, true, "r9"},   // 4: worktree whose repo has no main checkout here
            {true, true, "r1"},   // 5
            {true, false, "r1"},  // 6: a second main checkout of r1 gets no worktrees
    };
    std::vector<bool> tree;
    const auto order = nest_worktrees(info, &tree);
    EXPECT_EQ(order, (std::vector<int>{1, 2, 3, 0, 5, 4, 6}));
    EXPECT_EQ(tree, (std::vector<bool>{false, false, false, true, true, false, false}));

    Workspace w;
    for (const char* id : {"a", "b", "c"})
        w.add_space(id);
    w.select_space(0);
    EXPECT_TRUE(w.reorder_spaces({2, 0, 1}));
    EXPECT_EQ(w.spaces[0].tabs[0].focused, "c");
    EXPECT_EQ(w.active_space, 1);  // "a" stays active
    EXPECT_FALSE(w.reorder_spaces({0, 1, 2}));
    EXPECT_FALSE(w.reorder_spaces({0, 0, 1}));
}

TEST(Agents, CleanTitleAndTabLabel) {
    EXPECT_EQ(clean_agent_title("\xE2\x9C\xA6  Working"), "Working");     // "✦  Working"
    EXPECT_EQ(clean_agent_title("\xE2\xA0\x8B - Thinking"), "Thinking");  // "⠋ - Thinking"
    EXPECT_EQ(clean_agent_title("[ ! ] Fix login"), "Fix login");
    EXPECT_EQ(clean_agent_title("[web] login"), "[web] login");
    EXPECT_EQ(clean_agent_title("* \xE6\x96\xB0\xE5\x8A\x9F\xE8\x83\xBD "), "\xE6\x96\xB0\xE5\x8A\x9F\xE8\x83\xBD");
    TabLabel in{"1", false, "\xE2\x9C\xB3 Refactor parser", "Claude Code", "claude", "s1"};
    EXPECT_EQ(agent_tab_label(in), "Refactor parser");
    in.tab_name = "api work";
    in.tab_named = true;
    EXPECT_EQ(agent_tab_label(in), "api work");  // a named tab wins
    in.tab_name = "Claude Code";
    EXPECT_EQ(agent_tab_label(in), "Refactor parser");  // unless it is the program's name
    in = {"2", false, "~/src/api", "Claude Code", "claude", "s1"};
    EXPECT_EQ(agent_tab_label(in), "2");  // a path says nothing
    in.title = "claude";
    EXPECT_EQ(agent_tab_label(in), "2");
    in.title = "me@box";
    EXPECT_EQ(agent_tab_label(in), "2");
}

TEST(Agents, DetailsTreeAndPriority) {
    // Numbers say nothing unless two rows of a space would read the same.
    const auto d = agent_details({{"api", "1"}, {"api", "Fix"}, {"web", "1"}, {"web", "2"}});
    EXPECT_EQ(d, (std::vector<std::string>{"", "Fix", "1", "2"}));

    const std::map<std::string, std::vector<std::string>> kids{
            {"p", {"a", "b"}}, {"a", {"a1", "a2"}}, {"a2", {"p"}}};
    const auto t = agent_tree(kids, "p");
    ASSERT_EQ(t.size(), 4u);  // the loop back to p is cut
    EXPECT_EQ(t[0].id, "a");
    EXPECT_EQ(t[0].lines, (std::vector<TreeLine>{TreeLine::kTee}));
    EXPECT_EQ(t[1].id, "a1");
    EXPECT_EQ(t[1].lines, (std::vector<TreeLine>{TreeLine::kBar, TreeLine::kTee}));
    EXPECT_EQ(t[2].id, "a2");
    EXPECT_EQ(t[2].lines, (std::vector<TreeLine>{TreeLine::kBar, TreeLine::kElbow}));
    EXPECT_EQ(t[3].id, "b");
    EXPECT_EQ(t[3].lines, (std::vector<TreeLine>{TreeLine::kElbow}));

    EXPECT_EQ(kid_status(Attention::kDone), Attention::kIdle);
    EXPECT_EQ(kid_status(Attention::kBlocked), Attention::kBlocked);
    EXPECT_TRUE(priority_before({4, 0, 0}, {3, 100, 9}));
    EXPECT_TRUE(priority_before({2, 100, 0}, {2, 50, 9}));
    EXPECT_TRUE(priority_before({2, 50, 9}, {2, 50, 3}));
    EXPECT_FALSE(priority_before({2, 50, 3}, {2, 50, 9}));
}

#ifdef __APPLE__
// macOS (FR-40): "cmd" is ImGui's Ctrl (it swaps Cmd and Ctrl), "ctrl" the Control key (Super);
// the defaults are herdr-app's keys.macos.json; chords print with the macOS symbols.
TEST(KeymapMac, CmdAndControlAndTheMacDefaults) {
    EXPECT_EQ(*Keymap::parse("cmd-t"), ImGuiMod_Ctrl | ImGuiKey_T);
    EXPECT_EQ(*Keymap::parse("ctrl-1"), ImGuiMod_Super | ImGuiKey_1);
    EXPECT_EQ(*Keymap::parse("cmd-shift-["), ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_LeftBracket);
    auto k = Keymap::defaults();
    EXPECT_EQ(*k.global(*Keymap::parse("cmd-t")), "tab.new");
    EXPECT_EQ(*k.global(*Keymap::parse("cmd-d")), "pane.split:right");
    EXPECT_EQ(*k.global(*Keymap::parse("cmd-3")), "tab.select:3");
    EXPECT_EQ(*k.global(*Keymap::parse("ctrl-3")), "space.select:3");
    EXPECT_EQ(*k.global(*Keymap::parse("cmd-b")), "sidebar.toggle");
    EXPECT_EQ(*k.global(*Keymap::parse("cmd-p")), "app.picker");
    EXPECT_EQ(*k.terminal(*Keymap::parse("cmd-c")), "terminal.copy");
    EXPECT_EQ(*k.terminal(*Keymap::parse("cmd-left")), "send:\x01");
    EXPECT_FALSE(k.global(*Keymap::parse("ctrl-c")).has_value());  // Control+C is the program's
    EXPECT_EQ(Keymap::format(*Keymap::parse("cmd-shift-t")), "\xE2\x87\xA7\xE2\x8C\x98T");
    EXPECT_EQ(Keymap::format(*Keymap::parse("ctrl-alt-x")), "\xE2\x8C\x83\xE2\x8C\xA5X");
    // herdr-app's command names in a person's keys.json map to ours.
    const auto bad = k.apply({{"global", {{"cmd-k", "layout.toggle:sidebar"}, {"cmd-j", "mod.open:picker"}}}},
                             [](std::string_view) { return true; });
    EXPECT_TRUE(bad.empty());
    EXPECT_EQ(*k.global(*Keymap::parse("cmd-k")), "sidebar.toggle");
    EXPECT_EQ(*k.global(*Keymap::parse("cmd-j")), "app.picker");
    auto fresh = Keymap::defaults();
    EXPECT_TRUE(fresh.apply(Keymap::default_file(), [](std::string_view) { return true; }).empty());
    EXPECT_EQ(Keymap::defaults().global_map(), fresh.global_map());
}
#endif
