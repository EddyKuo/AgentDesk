// Tests for Focus mode's rules: the strip's dots (order, the focused agent left out, overflow),
// the edge glow's curve, the header's reveal on hover, the alerts setting and the built-in tree.
#include "agentdesk/ui/focus_mode.hpp"

#include <algorithm>

#include <gtest/gtest.h>

#include "agentdesk/ui/command_names.hpp"
#include "agentdesk/ui/keymap.hpp"
#include "agentdesk/ui/layout.hpp"

using namespace agentdesk::ui;
using namespace agentdesk::ui::focus;

namespace {
std::vector<DotAgent> sample() {
    return {{"idle", Attention::kIdle, 500, 1},    {"work", Attention::kWorking, 100, 2},
            {"done", Attention::kDone, 50, 3},     {"need", Attention::kBlocked, 10, 4},
            {"none", Attention::kNone, 900, 5},    {"me", Attention::kBlocked, 999, 6},
            {"work2", Attention::kWorking, 300, 7}};
}
}  // namespace

// Needs you, done, working (latest change first), idle; the focused agent and unknown states get
// no dot.
TEST(FocusMode, StripDotsMostUrgentFirstWithoutTheFocusedOne) {
    const StripDots d = strip_dots(sample(), "me", 1000, 8, 6);
    EXPECT_EQ(d.ids, (std::vector<std::string>{"need", "done", "work2", "work", "idle"}));
    EXPECT_EQ(d.overflow, 0);
}

// When the dots do not fit, the ones that do (with room for "+N") are kept and the rest counted.
TEST(FocusMode, StripDotsCountWhatDoesNotFit) {
    // 5 dots need 5*8 + 4*6 = 64 px. In 50 px with 10 px kept for "+N": 3 dots would take
    // 36 + 6 + 10 = 52, so 2 dots (22 + 6 + 10 = 38) and 3 more.
    StripDots d = strip_dots(sample(), "me", 50, 8, 6, 10);
    EXPECT_EQ(d.ids, (std::vector<std::string>{"need", "done"}));
    EXPECT_EQ(d.overflow, 3);
    d = strip_dots(sample(), "me", 64, 8, 6, 10);
    EXPECT_EQ(d.ids.size(), 5u);  // all fit exactly: no "+N" needed
    EXPECT_EQ(d.overflow, 0);
    d = strip_dots(sample(), "me", 0, 8, 6, 10);
    EXPECT_TRUE(d.ids.empty());
    EXPECT_EQ(d.overflow, 5);
    EXPECT_TRUE(strip_dots({}, "", 100, 8, 6).ids.empty());
}

// 150 ms in, 400 ms held, 900 ms out.
TEST(FocusMode, GlowFadesInHoldsAndFadesOut) {
    EXPECT_EQ(glow_alpha(0, 0xB0), 0);
    EXPECT_EQ(glow_alpha(75, 0xB0), 0x58);
    EXPECT_EQ(glow_alpha(150, 0xB0), 0xB0);
    EXPECT_EQ(glow_alpha(550, 0xB0), 0xB0);
    EXPECT_EQ(glow_alpha(1000, 0xB0), 0x58);
    EXPECT_EQ(glow_alpha(1450, 0xB0), 0);
    EXPECT_EQ(glow_alpha(1600, 0xB0), 0);
    EXPECT_EQ(glow_alpha(-5, 0xB0), 0);
    EXPECT_EQ(glow_alpha(300, 0x60), 0x60);
}

// Nothing for the first 300 ms of hovering, then a 150 ms fade in; leaving fades out over 600 ms;
// coming back while it still shows brings it straight back without a second wait.
TEST(FocusMode, HeaderShowsAfterTheDwellAndFadesOut) {
    HeaderReveal r;
    EXPECT_FLOAT_EQ(r.update(false, 0), 0.0f);
    EXPECT_FALSE(r.animating());
    EXPECT_FLOAT_EQ(r.update(true, 100), 0.0f);
    EXPECT_TRUE(r.animating());  // waiting out the dwell
    EXPECT_FLOAT_EQ(r.update(true, 350), 0.0f);
    EXPECT_GT(r.update(true, 400), 0.0f);
    EXPECT_FLOAT_EQ(r.update(true, 560), 1.0f);
    EXPECT_FALSE(r.animating());
    EXPECT_NEAR(r.update(false, 860), 0.5f, 1e-4);
    EXPECT_TRUE(r.animating());
    EXPECT_NEAR(r.update(true, 875), 0.6f, 1e-3);  // back before it faded: no second dwell
    EXPECT_FLOAT_EQ(r.update(false, 2000), 0.0f);
    EXPECT_FALSE(r.animating());
}

TEST(FocusMode, AlertsSettingDefaultsToEdgeWithSound) {
    EXPECT_EQ(alerts_mode("edge"), "edge");
    EXPECT_EQ(alerts_mode("edge_sound"), "edge_sound");
    EXPECT_EQ(alerts_mode("normal"), "normal");
    EXPECT_EQ(alerts_mode(""), "edge_sound");
    EXPECT_EQ(alerts_mode("loud"), "edge_sound");
}

TEST(FocusMode, ShortAgo) {
    EXPECT_EQ(short_ago(-10), "<1m");
    EXPECT_EQ(short_ago(59'999), "<1m");
    EXPECT_EQ(short_ago(4 * 60'000), "4m");
    EXPECT_EQ(short_ago(125 * 60'000), "2h");
    EXPECT_EQ(short_ago(50LL * 3'600'000), "2d");
}

// The built-in Focus tree: one tab block and the 22 px strip; not a layout of the list.
TEST(FocusMode, BuiltInTreeIsNotAListedLayout) {
    std::string error;
    const auto n = layout::parse_text(layout::builtin_text("focus"), &error);
    ASSERT_TRUE(n.has_value()) << error;
    EXPECT_EQ(n->find("main")->content.type, layout::Content::Type::kTab);
    EXPECT_EQ(n->find("focus-strip")->mod(), "focus-strip");
    EXPECT_EQ(n->find("focus-strip")->size, layout::Span::pixels(22));
    const auto names = layout::builtin_names();
    EXPECT_EQ(std::find(names.begin(), names.end(), "focus"), names.end());
}

// focus.toggle is a known command bound by default (the keys: win/ and posix/ keymap tests);
// focus.on / focus.off are known too.
TEST(FocusMode, CommandsAreKnownAndBound) {
    for (const char* c : {"focus.toggle", "focus.on", "focus.off"})
        EXPECT_TRUE(is_builtin_command(c)) << c;
    EXPECT_NE(Keymap::defaults().chord_for("focus.toggle"), "");
}
