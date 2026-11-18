#include "agentdesk/ui/chrome_logic.hpp"

#include <gtest/gtest.h>

#include "agentdesk/ui/keymap.hpp"

using namespace agentdesk::ui;

// herdr menu/main.js 131-137.
TEST(MenuNav, FirstRowHighlightedArrowsWrapEnterRuns) {
    MenuNav nav;
    nav.open();
    auto draw = [&](int rows, bool up, bool down, bool enter, int pointed = -1) {
        nav.frame(up, down, enter);
        int ran = -1;
        for (int i = 0; i < rows; ++i) {
            bool run = false;
            nav.row(i == pointed, &run);
            if (run)
                ran = i;
        }
        return ran;
    };
    EXPECT_EQ(draw(3, false, false, false), -1);
    EXPECT_EQ(nav.at(), 0);  // the first row starts highlighted
    draw(3, true, false, false);
    EXPECT_EQ(nav.at(), 2);  // up from the first wraps to the last
    draw(3, false, true, false);
    EXPECT_EQ(nav.at(), 0);  // down from the last wraps to the first
    draw(3, false, true, false);
    EXPECT_EQ(draw(3, false, false, true), 1);  // Enter runs the highlighted row
    draw(3, false, false, false, 2);
    EXPECT_EQ(nav.at(), 2);  // the pointer takes the highlight
    nav.open();
    EXPECT_EQ(draw(3, false, true, true), -1);  // no rows counted yet: keys wait a frame
    EXPECT_EQ(nav.at(), 0);
}

// herdr picker/main.js 86-98.
TEST(Picker, BusyHasAnyStatusBlockedFirstEarlierAlwaysShown) {
    std::vector<PickerSpace> spaces = {
        {false, false, 1},  // quiet
        {true, false, 2},   // idle agent: busy
        {true, true, 1},    // needs you: busy, first
        {true, false, 0},   // nothing left after the filter
    };
    auto p = plan_picker(spaces, false, false);
    EXPECT_EQ(p.busy, (std::vector<int>{2, 1}));
    EXPECT_EQ(p.quiet, (std::vector<int>{0}));
    EXPECT_TRUE(p.earlier_row);
    EXPECT_FALSE(p.quiet_listed);  // collapsed by default
    EXPECT_TRUE(plan_picker(spaces, false, true).quiet_listed);
    EXPECT_TRUE(plan_picker(spaces, true, false).quiet_listed);  // expanded while filtering

    // Nothing busy: the row is still there, collapsed.
    std::vector<PickerSpace> calm = {{false, false, 2}, {false, false, 1}};
    p = plan_picker(calm, false, false);
    EXPECT_TRUE(p.busy.empty());
    EXPECT_TRUE(p.earlier_row);
    EXPECT_FALSE(p.quiet_listed);
    EXPECT_FALSE(plan_picker({{true, false, 1}}, false, false).earlier_row);
}

TEST(Picker, FindTabById) {
    Workspace w;
    w.add_space("a");
    w.add_tab("b");
    w.add_space("c");
    const std::string id = w.spaces[0].tabs[1].id;
    EXPECT_EQ(find_tab(w, id).space, 0);
    EXPECT_EQ(find_tab(w, id).tab, 1);
    EXPECT_EQ(find_tab(w, "nope").space, -1);
}

// herdr keys.rs listing() and help/main.js label().
TEST(Keymap, ListingKeepsTheFileAsWritten) {
    const auto file = nlohmann::json::parse(R"({
        "terminal": {"ctrl-shift-c": "terminal.copy", "ctrl-backspace": "send:\u0017"},
        "global": {"ctrl-shift-t": "tab.new", "f2": "context.rename",
                   "alt-x": {"command": "pane.split", "args": {"direction": "right"}}}
    })");
    const auto list = Keymap::listing(file);
    ASSERT_EQ(list.size(), 6u);
    EXPECT_EQ(list[0].section, "global");
    EXPECT_EQ(list[0].keys, "alt-x");
    EXPECT_EQ(list[0].action, R"({"args":{"direction":"right"},"command":"pane.split"})");
    EXPECT_EQ(list[1].keys, "ctrl-shift-t");
    EXPECT_EQ(list[2].action, "tab.rename");  // old names migrate
    EXPECT_EQ(list[3].section, "terminal");
    EXPECT_EQ(list[5].section, "global");  // the escape hatch, always listed
    EXPECT_EQ(list[5].keys, "ctrl-alt-shift-escape");
    EXPECT_EQ(list[5].action, "app.reset");

    // Unreadable: the defaults, which hold the escape hatch once.
    const auto defaults = Keymap::listing(nlohmann::json::array());
    int hatch = 0, terminal = 0;
    for (const auto& b : defaults) {
        hatch += b.action == "app.reset" ? 1 : 0;
        terminal += b.section == "terminal" ? 1 : 0;
    }
    EXPECT_EQ(hatch, 1);
    EXPECT_GT(terminal, 0);
}

TEST(Keymap, HelpLabelsAndGroups) {
    EXPECT_EQ(Keymap::help_label("pane.split:right"), "pane split right");
    EXPECT_EQ(Keymap::help_label("app.zoom_in"), "app zoom in");
    EXPECT_EQ(Keymap::help_label("send:\x1b" "b"), "type \"\\u001bb\"");
    EXPECT_STREQ(Keymap::help_group("space.new"), "spaces");
    EXPECT_STREQ(Keymap::help_group("send:x"), "terminal");
    EXPECT_STREQ(Keymap::help_group("terminal.copy"), "terminal");
    EXPECT_STREQ(Keymap::help_group("app.help"), "window");
}
