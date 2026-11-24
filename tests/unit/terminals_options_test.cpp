// Tests for the Terminals layout's options and keyboard tools (once the Tiling layout): square
// corners and gaps on the tree, pane.split:auto's direction, the resize mode's keys and the
// layout's default keys.
#include "agentdesk/ui/terminals_options.hpp"

#include <set>

#include <gtest/gtest.h>

#include "agentdesk/ui/command_names.hpp"
#include "agentdesk/ui/keymap.hpp"
#include "agentdesk/ui/layout.hpp"
#include "agentdesk/ui/layout_styles.hpp"

using namespace agentdesk::ui;
using namespace agentdesk::ui::terminals;

namespace {
const layout::PaneFrame& frame_of(const layout::Node& t) {
    return *t.find("main")->content.pane;
}
}  // namespace

TEST(TerminalsOptions, DefaultsLeaveTheShippedLayoutAsItIs) {
    const layout::Node file = layout::builtin("terminals");
    layout::Node t = file;
    const Options o;
    EXPECT_TRUE(is_default(o));
    ASSERT_TRUE(apply_options(t, o));
    EXPECT_EQ(frame_of(t).gap, frame_of(file).gap);
    EXPECT_EQ(frame_of(t).radius, frame_of(file).radius);
    EXPECT_EQ(t.padding, file.padding);  // 8 px gaps keep the file's 6 px padding
    EXPECT_EQ(t.gap, file.gap);
    const styles::PaneLook look = styles::pane_look("terminals", &frame_of(t));
    EXPECT_EQ(look.radius, 12.0f);
    EXPECT_EQ(look.gap, 8.0f);
}

TEST(TerminalsOptions, SquareCornersAndGaps) {
    layout::Node t = layout::builtin("terminals");
    ASSERT_TRUE(apply_options(t, Options{true, 12.0f, true}));
    EXPECT_EQ(frame_of(t).radius, 0.0f);
    EXPECT_EQ(frame_of(t).gap, 12.0f);
    EXPECT_EQ(t.padding, (layout::Padding{9.0f, 9.0f, true}));
    EXPECT_EQ(t.gap, 12.0f);
    EXPECT_FALSE(is_default(Options{true, 8.0f, true}));

    // Gaps off: panes touch each other and the window's edges.
    t = layout::builtin("terminals");
    ASSERT_TRUE(apply_options(t, Options{false, 12.0f, false}));
    EXPECT_EQ(frame_of(t).gap, 0.0f);
    EXPECT_EQ(t.padding, (layout::Padding{0.0f, 0.0f, true}));
    EXPECT_EQ(t.gap, 0.0f);
    EXPECT_EQ(frame_of(t).radius, 12.0f);  // corners stay as the file has them

    ASSERT_TRUE(apply_options(t, Options{false, 40.0f, true}));  // kept within 0..24
    EXPECT_EQ(frame_of(t).gap, 24.0f);
    EXPECT_EQ(clamp_gaps(-3.0f), 0.0f);
    layout::Node none =
        *layout::parse_text(R"({"type":"column","children":[{"type":"block","content":{"mod":"x"}}]})");
    EXPECT_FALSE(apply_options(none, Options{}));
}

TEST(TerminalsOptions, SplitDirectionFollowsLongerSide) {
    EXPECT_STREQ(split_direction(1200, 700), "right");
    EXPECT_STREQ(split_direction(700, 1200), "down");
    EXPECT_STREQ(split_direction(160, 100), "down");  // exactly 1.6: not wider than the threshold
    EXPECT_STREQ(split_direction(161, 100), "right");
    EXPECT_STREQ(split_direction(1000, 700), "down");  // wider but under 1.6 x
}

TEST(TerminalsOptions, ResizeModeKeys) {
    EXPECT_EQ(resize_key(Key::kH, false).command, "pane.resize:left");
    EXPECT_EQ(resize_key(Key::kJ, false).command, "pane.resize:down");
    EXPECT_EQ(resize_key(Key::kK, false).command, "pane.resize:up");
    EXPECT_EQ(resize_key(Key::kL, false).command, "pane.resize:right");
    EXPECT_EQ(resize_key(Key::kLeft, false).command, "pane.resize:left");
    EXPECT_EQ(resize_key(Key::kRight, true).command, "pane.resize:right:10");
    EXPECT_FALSE(resize_key(Key::kL, false).leave);
    EXPECT_TRUE(resize_key(Key::kEscape, false).leave);
    EXPECT_TRUE(resize_key(Key::kEnter, true).leave);
    for (const Key k : {Key::kH, Key::kJ, Key::kK, Key::kL})
        for (const bool shift : {false, true}) {
            const std::string c = resize_key(k, shift).command;
            EXPECT_TRUE(is_builtin_command(c.substr(0, c.find(':')))) << c;
            EXPECT_TRUE(parse_resize(c.substr(c.find(':') + 1)).has_value()) << c;
        }
}

TEST(TerminalsOptions, ResizeArgument) {
    EXPECT_EQ(parse_resize("left"), std::make_pair(std::string("left"), 40.0f));
    EXPECT_EQ(parse_resize("down:10"), std::make_pair(std::string("down"), 10.0f));
    EXPECT_FALSE(parse_resize("down:").has_value());
    EXPECT_FALSE(parse_resize("down:x").has_value());
    EXPECT_FALSE(parse_resize("down:0").has_value());
}

// The keys that came from Tiling now belong to the Terminals layout.
TEST(TerminalsOptions, LayoutKeysDoNotClashWithDefaults) {
    const auto file = Keymap::default_file();
    ASSERT_TRUE(file.contains("layout:terminals"));
    EXPECT_FALSE(file.contains("layout:tiling"));
    std::set<ImGuiKeyChord> taken;
    for (const char* sec : {"global", "terminal"})
        for (const auto& [chord, cmd] : file[sec].items())
            taken.insert(*Keymap::parse(chord));
    for (const auto& [chord, cmd] : file["layout:terminals"].items()) {
        const auto c = Keymap::parse(chord);
        ASSERT_TRUE(c.has_value()) << chord;
        EXPECT_FALSE(taken.contains(*c)) << chord;
        const std::string name = cmd.get<std::string>();
        EXPECT_TRUE(is_builtin_command(name.substr(0, name.find(':')))) << name;
    }
    Keymap k = Keymap::defaults();
    k.set_layout("terminals");
    EXPECT_EQ(*k.global(*Keymap::parse("alt-r")), "terminals.resize_mode");
    EXPECT_EQ(*k.global(*Keymap::parse("alt-shift-enter")), "pane.split:auto");
    EXPECT_EQ(*k.global(*Keymap::parse("alt-shift-g")), "terminals.gaps");
    EXPECT_EQ(*k.global(*Keymap::parse("alt-shift-q")), "pane.close");
    k.set_layout("workspace");
    EXPECT_FALSE(k.global(*Keymap::parse("alt-r")).has_value());  // only while Terminals shows
}

TEST(TerminalsOptions, CommandsAreKnown) {
    for (const char* c : {"terminals.resize_mode", "terminals.gaps", "terminals.corners",
                          "tiling.resize_mode", "tiling.gaps"})
        EXPECT_TRUE(is_builtin_command(c)) << c;
}
