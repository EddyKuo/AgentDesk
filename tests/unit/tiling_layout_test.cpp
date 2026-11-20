// Tests for the AgentDesk Tiling layout's rules: the shipped file, pane.split:auto's direction,
// the resize mode's keys, the gaps, the title rows, the bar's order of giving way, the space chips
// and the layout's default keys.
#include "agentdesk/ui/tiling_layout.hpp"

#include <set>

#include <gtest/gtest.h>

#include "agentdesk/ui/command_names.hpp"
#include "agentdesk/ui/keymap.hpp"
#include "agentdesk/ui/layout.hpp"
#include "agentdesk/ui/layout_styles.hpp"

using namespace agentdesk::ui;
using namespace agentdesk::ui::tiling;

TEST(TilingLayout, ShippedLayoutParsesAndIsTilingStyle) {
    const layout::Node n = layout::builtin("tiling");
    EXPECT_EQ(styles::style_of_tree(n), "tiling");
    ASSERT_NE(n.find("bar"), nullptr);
    EXPECT_EQ(n.find("bar")->mod(), "tiling-bar");
    EXPECT_EQ(n.find("bar")->size, layout::Span::pixels(26));
    EXPECT_TRUE(n.find("bar")->window_drag);
    const layout::Node* main = n.find("main");
    ASSERT_NE(main, nullptr);
    ASSERT_TRUE(main->content.pane.has_value());
    const styles::PaneLook look = styles::pane_look("tiling", &*main->content.pane);
    EXPECT_EQ(look.radius, 0.0f);
    EXPECT_EQ(look.gap, 8.0f);
    EXPECT_EQ(look.header, 18.0f);
    EXPECT_EQ(look.border, (layout::Line{1.0f, "border"}));
    EXPECT_EQ(look.focus, (layout::Line{2.0f, "accent"}));
    EXPECT_EQ(n.find("sidebar"), nullptr);  // no sidebar, no tab cards
    EXPECT_EQ(styles::display_name("tiling"), "AgentDesk Tiling");
}

TEST(TilingLayout, SplitDirectionFollowsLongerSide) {
    EXPECT_STREQ(split_direction(1200, 700), "right");
    EXPECT_STREQ(split_direction(700, 1200), "down");
    EXPECT_STREQ(split_direction(160, 100), "down");  // exactly 1.6: not wider than the threshold
    EXPECT_STREQ(split_direction(161, 100), "right");
    EXPECT_STREQ(split_direction(1000, 700), "down");  // wider but under 1.6 x
}

TEST(TilingLayout, ResizeModeKeys) {
    EXPECT_EQ(resize_key(Key::kH, false).command, "pane.resize:left");
    EXPECT_EQ(resize_key(Key::kJ, false).command, "pane.resize:down");
    EXPECT_EQ(resize_key(Key::kK, false).command, "pane.resize:up");
    EXPECT_EQ(resize_key(Key::kL, false).command, "pane.resize:right");
    EXPECT_EQ(resize_key(Key::kLeft, false).command, "pane.resize:left");
    EXPECT_EQ(resize_key(Key::kDown, false).command, "pane.resize:down");
    EXPECT_EQ(resize_key(Key::kUp, false).command, "pane.resize:up");
    EXPECT_EQ(resize_key(Key::kRight, true).command, "pane.resize:right:10");
    EXPECT_EQ(resize_key(Key::kL, true).command, "pane.resize:right:10");
    EXPECT_FALSE(resize_key(Key::kL, false).leave);
    EXPECT_TRUE(resize_key(Key::kEscape, false).leave);
    EXPECT_TRUE(resize_key(Key::kEnter, true).leave);
    EXPECT_TRUE(resize_key(Key::kEscape, false).command.empty());
    // Every command it makes is one AgentDesk runs, and its argument reads back.
    for (const Key k : {Key::kH, Key::kJ, Key::kK, Key::kL})
        for (const bool shift : {false, true}) {
            const std::string c = resize_key(k, shift).command;
            EXPECT_TRUE(is_builtin_command(c.substr(0, c.find(':')))) << c;
            EXPECT_TRUE(parse_resize(c.substr(c.find(':') + 1)).has_value()) << c;
        }
}

TEST(TilingLayout, ResizeArgument) {
    EXPECT_EQ(parse_resize("left"), std::make_pair(std::string("left"), 40.0f));
    EXPECT_EQ(parse_resize("down:10"), std::make_pair(std::string("down"), 10.0f));
    EXPECT_FALSE(parse_resize("down:").has_value());
    EXPECT_FALSE(parse_resize("down:x").has_value());
    EXPECT_FALSE(parse_resize("down:0").has_value());
    EXPECT_FALSE(parse_resize("down:-5").has_value());
}

TEST(TilingLayout, GapsApplyToTree) {
    layout::Node t = layout::builtin("tiling");
    ASSERT_TRUE(apply_gaps(t, 12.0f, true));
    EXPECT_EQ(t.find("main")->padding, (layout::Padding{12.0f, 12.0f, true}));
    EXPECT_EQ(t.find("main")->content.pane->gap, 12.0f);
    ASSERT_TRUE(apply_gaps(t, 12.0f, false));
    EXPECT_EQ(t.find("main")->padding, (layout::Padding{0.0f, 0.0f, true}));
    EXPECT_EQ(t.find("main")->content.pane->gap, 0.0f);
    ASSERT_TRUE(apply_gaps(t, 40.0f, true));  // kept within 0..24
    EXPECT_EQ(t.find("main")->content.pane->gap, 24.0f);
    ASSERT_TRUE(apply_gaps(t, -3.0f, true));
    EXPECT_EQ(t.find("main")->content.pane->gap, 0.0f);
    EXPECT_EQ(clamp_gaps(8.0f), 8.0f);
    layout::Node none =
        *layout::parse_text(R"({"type":"column","children":[{"type":"block","content":{"mod":"x"}}]})");
    EXPECT_FALSE(apply_gaps(none, 8.0f, true));
}

TEST(TilingLayout, TitleRows) {
    EXPECT_FALSE(show_titles("auto", 1));
    EXPECT_TRUE(show_titles("auto", 2));
    EXPECT_TRUE(show_titles("always", 1));
    EXPECT_FALSE(show_titles("never", 3));
    EXPECT_FALSE(show_titles("something else", 1));
}

// A4 (the focused pane) gives way first, then the other tabs shrink to their numbers, then the
// clock goes; whatever is still too long is cut.
TEST(TilingLayout, BarSegmentsShrinkInOrder) {
    BarWidths w;
    w.lead = 100;
    w.tabs_full = 300;
    w.tabs_short = 120;
    w.title = 200;
    w.counts = 40;
    w.clock = 50;
    BarFit f = fit_bar(w, 1000);  // 690 needed: all of it
    EXPECT_TRUE(f.title);
    EXPECT_FALSE(f.short_tabs);
    EXPECT_TRUE(f.clock);
    EXPECT_FLOAT_EQ(f.tabs_room, 1000 - 100 - 200 - 40 - 50);
    f = fit_bar(w, 600);  // the title goes
    EXPECT_FALSE(f.title);
    EXPECT_FALSE(f.short_tabs);
    EXPECT_TRUE(f.clock);
    f = fit_bar(w, 400);  // then the tabs shrink
    EXPECT_FALSE(f.title);
    EXPECT_TRUE(f.short_tabs);
    EXPECT_TRUE(f.clock);
    f = fit_bar(w, 290);  // then the clock
    EXPECT_TRUE(f.short_tabs);
    EXPECT_FALSE(f.clock);
    EXPECT_FLOAT_EQ(f.tabs_room, 290 - 100 - 40);
    f = fit_bar(w, 150);  // then the tabs are cut
    EXPECT_FLOAT_EQ(f.tabs_room, 10);
    f = fit_bar(w, 50);
    EXPECT_FLOAT_EQ(f.tabs_room, 0);
}

TEST(TilingLayout, SpaceChipsPastNine) {
    SpaceChips c = space_chips(3, 1);
    EXPECT_EQ(c.shown, (std::vector<int>{0, 1, 2}));
    EXPECT_EQ(c.more, 0);
    c = space_chips(12, 2);
    EXPECT_EQ(c.shown.size(), 9u);
    EXPECT_EQ(c.more, 3);
    c = space_chips(12, 10);  // the active one is shown too
    EXPECT_EQ(c.shown.back(), 10);
    EXPECT_EQ(c.more, 2);
    EXPECT_TRUE(space_chips(0, 0).shown.empty());
}

// The layout's own keys are not bound in the global or terminal sections, and name real commands.
TEST(TilingLayout, LayoutKeysDoNotClashWithDefaults) {
    const auto file = Keymap::default_file();
    ASSERT_TRUE(file.contains("layout:tiling"));
    std::set<ImGuiKeyChord> taken;
    for (const char* sec : {"global", "terminal"})
        for (const auto& [chord, cmd] : file[sec].items())
            taken.insert(*Keymap::parse(chord));
    for (const auto& [chord, cmd] : file["layout:tiling"].items()) {
        const auto c = Keymap::parse(chord);
        ASSERT_TRUE(c.has_value()) << chord;
        EXPECT_FALSE(taken.contains(*c)) << chord;
        const std::string name = cmd.get<std::string>();
        EXPECT_TRUE(is_builtin_command(name.substr(0, name.find(':')))) << name;
    }
    Keymap k = Keymap::defaults();
    k.set_layout("tiling");
    EXPECT_EQ(*k.global(*Keymap::parse("alt-r")), "tiling.resize_mode");
    EXPECT_EQ(*k.global(*Keymap::parse("alt-shift-enter")), "pane.split:auto");
    EXPECT_EQ(*k.global(*Keymap::parse("alt-shift-g")), "tiling.gaps");
    EXPECT_EQ(*k.global(*Keymap::parse("alt-shift-q")), "pane.close");
    k.set_layout("workspace");
    EXPECT_FALSE(k.global(*Keymap::parse("alt-r")).has_value());  // only while Tiling shows
}
