// Tests for the AgentDesk Mac layout's rules: the built-in layout file, the menus (every row names
// a real command), the Dock's items, sizes and magnification, and the clock.
#include "agentdesk/ui/mac_layout.hpp"

#include <gtest/gtest.h>

#include "agentdesk/ui/command_names.hpp"
#include "agentdesk/ui/layout.hpp"

using namespace agentdesk::ui;
using namespace agentdesk::ui::mac;

namespace {
MenuContext sample_context() {
    MenuContext c;
    c.layouts = {{"workspace", "AgentDesk"}, {"terminals", "Terminals"}, {"mac", "AgentDesk Mac"}};
    c.layout = "mac";
    c.space = 1;
    c.tabs = {{"build", false, false}, {"claude", true, false}, {"logs", false, true}};
    return c;
}

int find_row(const std::vector<MenuItem>& rows, const std::string& command) {
    for (std::size_t i = 0; i < rows.size(); ++i)
        if (rows[i].command == command)
            return static_cast<int>(i);
    return -1;
}
}  // namespace

// The shipped file has the menu bar, a window (title block and the active tab's panes) and the
// Dock, puts the round buttons in the bar, and cannot be resized by dragging.
TEST(MacLayout, ShippedLayoutHasMenuBarWindowAndDock) {
    const layout::Node n = layout::builtin("mac");
    ASSERT_NE(n.find("menubar"), nullptr);
    EXPECT_EQ(n.find("menubar")->mod(), "mac-menubar");
    EXPECT_EQ(n.find("title")->mod(), "mac-window-title");
    EXPECT_EQ(n.find("dock")->mod(), "mac-dock");
    EXPECT_EQ(n.find("main")->content.type, layout::Content::Type::kTab);
    EXPECT_EQ(n.parent_of("title")->id, "window");
    EXPECT_EQ(n.parent_of("dock")->id, "desktop");
    EXPECT_FALSE(n.find("desktop")->resizable_or());
    ASSERT_TRUE(n.traffic_lights.is_array());
    EXPECT_EQ(n.traffic_lights[1].get<float>(), 8.0f);  // (28 - 12) / 2: centred in the bar
    EXPECT_EQ(n.find("sidebar"), nullptr);              // sidebar.toggle switches layouts here
}

// Every row of every menu runs a command AgentDesk has.
TEST(MacLayout, EveryMenuRowNamesAKnownCommand) {
    const MenuContext c = sample_context();
    for (int m = 0; m < kMenuCount; ++m) {
        const auto rows = menu_items(static_cast<Menu>(m), c);
        EXPECT_FALSE(rows.empty()) << menu_title(static_cast<Menu>(m));
        for (const auto& r : rows) {
            if (r.kind != MenuItem::Kind::kItem)
                continue;
            const std::string name = r.command.substr(0, r.command.find(':'));
            EXPECT_TRUE(is_builtin_command(name))
                << menu_title(static_cast<Menu>(m)) << ": " << r.label << " -> " << r.command;
        }
    }
    EXPECT_FALSE(is_builtin_command("win98.start"));
}

TEST(MacLayout, MenuTitlesInBarOrder) {
    EXPECT_STREQ(menu_title(Menu::kApp), "AgentDesk");
    EXPECT_STREQ(menu_title(Menu::kFile), "File");
    EXPECT_STREQ(menu_title(Menu::kEdit), "Edit");
    EXPECT_STREQ(menu_title(Menu::kView), "View");
    EXPECT_STREQ(menu_title(Menu::kWindow), "Window");
    EXPECT_STREQ(menu_title(Menu::kHelp), "Help");
}

// View checks the layout shown; Window lists the space's tabs (by index, settled ones marked)
// and checks the active one.
TEST(MacLayout, ViewAndWindowMenusFollowTheApp) {
    const MenuContext c = sample_context();
    const auto view = menu_items(Menu::kView, c);
    const int mac_row = find_row(view, "layout.switch:mac");
    ASSERT_GE(mac_row, 0);
    EXPECT_TRUE(view[static_cast<std::size_t>(mac_row)].checked);
    EXPECT_FALSE(view[static_cast<std::size_t>(find_row(view, "layout.switch:workspace"))].checked);

    const auto win = menu_items(Menu::kWindow, c);
    const int claude = find_row(win, "tab.goto:1:1");
    ASSERT_GE(claude, 0);
    EXPECT_TRUE(win[static_cast<std::size_t>(claude)].checked);
    const int logs = find_row(win, "tab.goto:1:2");
    ASSERT_GE(logs, 0);
    EXPECT_NE(win[static_cast<std::size_t>(logs)].label.find("Dock"), std::string::npos);
    EXPECT_TRUE(win[static_cast<std::size_t>(find_row(win, "tab.unsettle"))].enabled);
    EXPECT_FALSE(win[static_cast<std::size_t>(find_row(win, "window.fill"))].checked);
}

// Without a tab the rows that need a terminal are disabled, and Window lists no tabs.
TEST(MacLayout, RowsNeedingATerminalAreDisabledWithoutATab) {
    MenuContext c;
    c.layouts = {{"mac", "AgentDesk Mac"}};
    c.layout = "mac";
    for (const auto& r : menu_items(Menu::kEdit, c))
        if (r.kind == MenuItem::Kind::kItem)
            EXPECT_FALSE(r.enabled) << r.label;
    const auto file = menu_items(Menu::kFile, c);
    EXPECT_TRUE(file[static_cast<std::size_t>(find_row(file, "tab.new"))].enabled);
    EXPECT_FALSE(file[static_cast<std::size_t>(find_row(file, "pane.close"))].enabled);
    for (const auto& r : menu_items(Menu::kWindow, c))
        EXPECT_EQ(r.command.find("tab.goto"), std::string::npos);
}

TEST(MacDock, ItemsAreSpacesThenTabsThenNewAndSettings) {
    const auto items = dock_items({"a", "b"}, {"t1", "t2", "t3"});
    ASSERT_EQ(items.size(), 2u + 1u + 3u + 1u + 2u);
    EXPECT_EQ(items[0].kind, DockItem::Kind::kSpace);
    EXPECT_EQ(items[1].key, "s:b");
    EXPECT_EQ(items[2].kind, DockItem::Kind::kSeparator);
    EXPECT_EQ(items[3].kind, DockItem::Kind::kTab);
    EXPECT_EQ(items[5].index, 2);
    EXPECT_EQ(items[5].key, "t:t3");
    EXPECT_EQ(items[6].kind, DockItem::Kind::kSeparator);
    EXPECT_EQ(items[7].kind, DockItem::Kind::kNew);
    EXPECT_EQ(items[8].kind, DockItem::Kind::kSettings);
    // No spaces: only "+" and the gear, no separators.
    const auto empty = dock_items({}, {});
    ASSERT_EQ(empty.size(), 2u);
    EXPECT_EQ(empty[0].kind, DockItem::Kind::kNew);
}

TEST(MacDock, GeometryPadsTheEndsAndGapsTheItems) {
    const auto items = dock_items({"a"}, {"t"});  // space, sep, tab, sep, new, settings
    const DockGeometry g = dock_geometry(items, 44, 10, 10);
    ASSERT_EQ(g.x.size(), items.size());
    EXPECT_FLOAT_EQ(g.x[0], 10);
    EXPECT_FLOAT_EQ(g.x[1], 64);  // after the first tile and a gap
    EXPECT_FLOAT_EQ(g.w[1], 1);   // a separator is a 1 px line
    EXPECT_FLOAT_EQ(g.x[2], 75);
    EXPECT_FLOAT_EQ(g.width, 10 + 4 * 44 + 2 * 1 + 5 * 10 + 10);
}

// Hovered 1.25, the tiles beside it 1.1 (across a separator too), the rest 1.
TEST(MacDock, MagnificationFallsOffWithDistance) {
    const auto items = dock_items({"a", "b"}, {"t1", "t2"});  // a b | t1 t2 | new settings
    EXPECT_FLOAT_EQ(dock_target_scale(items, 0, -1), 1.0f);
    EXPECT_FLOAT_EQ(dock_target_scale(items, 1, 1), 1.25f);
    EXPECT_FLOAT_EQ(dock_target_scale(items, 0, 1), 1.1f);
    EXPECT_FLOAT_EQ(dock_target_scale(items, 3, 1), 1.1f);  // t1, across the separator
    EXPECT_FLOAT_EQ(dock_target_scale(items, 4, 1), 1.0f);
    EXPECT_FLOAT_EQ(dock_target_scale(items, 2, 1), 1.0f);  // a separator never grows
}

TEST(MacDock, ApproachMovesTowardTheTargetAndLands) {
    float v = 1.0f;
    v = approach(v, 1.25f, 1.0f / 60.0f);
    EXPECT_GT(v, 1.0f);
    EXPECT_LT(v, 1.25f);
    for (int i = 0; i < 60; ++i)
        v = approach(v, 1.25f, 1.0f / 60.0f);
    EXPECT_FLOAT_EQ(v, 1.25f);
    EXPECT_FLOAT_EQ(approach(1.1f, 1.0f, 0.0f), 1.1f);  // no time, no change
}

TEST(MacClock, WeekdayAndTwentyFourHourTime) {
    EXPECT_EQ(clock_text(4, 16, 25), "Thu 16:25");
    EXPECT_EQ(clock_text(0, 9, 5), "Sun 09:05");
    EXPECT_EQ(clock_text(6, 0, 0), "Sat 00:00");
    EXPECT_EQ(ms_to_next_minute(0), 60000);
    EXPECT_EQ(ms_to_next_minute(59500), 500);
}

TEST(MacLayout, AgentCountsAndWindowTitle) {
    const AgentCounts c = count_agents({Attention::kBlocked, Attention::kWorking, Attention::kWorking,
                                        Attention::kDone, Attention::kIdle, Attention::kBlocked});
    EXPECT_EQ(c.needs, 2);
    EXPECT_EQ(c.working, 2);
    EXPECT_EQ(window_title("api", "claude"), "api \xE2\x80\x94 claude");
    EXPECT_EQ(window_title("api", ""), "api");
    EXPECT_EQ(window_title("", "claude"), "claude");
}
