// Tests for the layout styles' shared rules: the mod name -> style table and how a tree picks its
// style, the pane frame each style reads from the layout, toast placement and effects.
#include "agentdesk/ui/layout_styles.hpp"

#include <gtest/gtest.h>

using namespace agentdesk::ui;
using namespace agentdesk::ui::styles;

namespace {
layout::Node tree_of(const char* json) {
    auto n = layout::parse_text(json);
    EXPECT_TRUE(n.has_value());
    return n ? *n : layout::Node{};
}
}  // namespace

TEST(LayoutStyles, ModTableNamesEveryStyleMod) {
    EXPECT_EQ(style_of_mod("mac-menubar"), "mac");
    EXPECT_EQ(style_of_mod("mac-dock"), "mac");
    EXPECT_EQ(style_of_mod("mac-window-title"), "mac");
    EXPECT_EQ(style_of_mod("terminals-bar"), "terminals");
    EXPECT_EQ(style_of_mod("terminals-pane"), "terminals");
    for (const char* m :
         {"classic-menubar", "classic-tabs", "classic-status", "classic-sidebar", "classic-pane"})
        EXPECT_EQ(style_of_mod(m), "classic") << m;
    for (const char* m : {"tiling-bar", "tiling-pane"})
        EXPECT_EQ(style_of_mod(m), "tiling") << m;
    for (const char* m : {"cockpit-header", "cockpit-gauges", "cockpit-roster", "cockpit-log"})
        EXPECT_EQ(style_of_mod(m), "cockpit") << m;
    EXPECT_EQ(style_of_mod("focus-strip"), "focus");
    for (const char* m : {"mission-bar", "mission-wall"})
        EXPECT_EQ(style_of_mod(m), "mission") << m;
    for (const char* m : {"review-head", "review-changes", "review-commit"})
        EXPECT_EQ(style_of_mod(m), "review") << m;
    for (const char* m : {"", "tabs", "spaces", "agents", "workspace-chrome", "files", "my-own-mod"})
        EXPECT_EQ(style_of_mod(m), "") << m;
}

// The shipped layouts keep the styles they always had.
TEST(LayoutStyles, ShippedLayoutsKeepTheirStyles) {
    EXPECT_EQ(style_of_tree(layout::builtin("workspace")), "");
    EXPECT_EQ(style_of_tree(layout::builtin("terminals")), "terminals");
    EXPECT_EQ(style_of_tree(layout::builtin("mac")), "mac");
    EXPECT_EQ(style_of_tree(layout::builtin("focus")), "focus");
    EXPECT_EQ(style_of_tree(layout::builtin("mission")), "mission");
    EXPECT_EQ(style_of_tree(layout::builtin("review")), "review");
    EXPECT_EQ(style_of_tree(layout::builtin("tiling")), "tiling");
    EXPECT_EQ(style_of_tree(layout::builtin("cockpit")), "cockpit");
    EXPECT_EQ(style_of_tree(layout::builtin("classic")), "classic");
}

// A pane header mod picks a style too; Mac wins over the rest; otherwise the first one found.
TEST(LayoutStyles, TreeStyleRules) {
    EXPECT_EQ(style_of_tree(tree_of(R"({"type":"column","children":[
        {"type":"block","content":{"tab":"active","pane":{"header":{"content":{"mod":"terminals-pane"}}}}}]})")),
              "terminals");
    EXPECT_EQ(style_of_tree(tree_of(R"({"type":"column","children":[
        {"type":"block","content":{"mod":"classic-status"}},
        {"type":"block","content":{"mod":"mac-dock"}}]})")),
              "mac");
    EXPECT_EQ(style_of_tree(tree_of(R"({"type":"column","children":[
        {"type":"block","content":{"mod":"tiling-bar"}},
        {"type":"block","content":{"mod":"classic-status"}}]})")),
              "tiling");
    EXPECT_EQ(style_of_tree(tree_of(R"({"type":"column","children":[
        {"type":"block","content":{"mod":"spaces"}}]})")),
              "");
}

TEST(LayoutStyles, DisplayNames) {
    EXPECT_EQ(display_name("workspace"), "AgentDesk");
    EXPECT_EQ(display_name("terminals"), "Terminals");
    EXPECT_EQ(display_name("mac"), "AgentDesk Mac");
    EXPECT_EQ(display_name("mission"), "Mission Control");
    EXPECT_EQ(display_name("review"), "Review");
    EXPECT_EQ(display_name("classic"), "AgentDesk Classic");
    EXPECT_EQ(display_name("tiling"), "AgentDesk Tiling");
    EXPECT_EQ(display_name("cockpit"), "AgentDesk Cockpit");
    EXPECT_EQ(display_name("my-layout"), "my-layout");
}

// What the shipped layouts' panes read from their files is what they always drew.
TEST(LayoutStyles, PaneLookOfShippedLayoutsIsUnchanged) {
    const auto frame_of = [](const layout::Node& t) -> const layout::PaneFrame* {
        const layout::Node* m = t.find("main");
        return m != nullptr && m->content.pane ? &*m->content.pane : nullptr;
    };
    const layout::Node ws = layout::builtin("workspace");
    const PaneLook w = pane_look("", frame_of(ws));
    EXPECT_EQ(w.radius, 10.0f);
    EXPECT_EQ(w.header, 30.0f);
    EXPECT_EQ(w.gap, 8.0f);
    EXPECT_EQ(w.border, (layout::Line{1.0f, "border"}));
    EXPECT_EQ(w.focus, (layout::Line{1.0f, "accent"}));

    const layout::Node tm = layout::builtin("terminals");
    const PaneLook t = pane_look("terminals", frame_of(tm));
    EXPECT_EQ(t.radius, 12.0f);
    EXPECT_EQ(t.header, 38.0f);
    EXPECT_EQ(t.gap, 8.0f);

    const layout::Node mac = layout::builtin("mac");
    const PaneLook m = pane_look("mac", frame_of(mac));
    EXPECT_EQ(m.radius, 10.0f);
    EXPECT_EQ(m.header, 30.0f);
    EXPECT_EQ(m.border, (layout::Line{1.0f, "border"}));
    EXPECT_EQ(m.focus, (layout::Line{1.0f, "accent"}));

    // No frame: each style's defaults.
    EXPECT_EQ(pane_look("", nullptr).radius, 10.0f);
    EXPECT_EQ(pane_look("terminals", nullptr).header, 38.0f);
    EXPECT_EQ(pane_look("tiling", nullptr).radius, 0.0f);
    EXPECT_EQ(pane_look("tiling", nullptr).focus.width, 2.0f);
    EXPECT_EQ(pane_look("classic", nullptr).header, 20.0f);
    EXPECT_EQ(pane_look("cockpit", nullptr).radius, 4.0f);
}

TEST(LayoutStyles, PaneLookReadsTheFrame) {
    layout::PaneFrame f;
    f.radius = 3.0f;
    f.border = layout::Line{2.0f, "#112233"};
    f.focus = layout::Line{3.0f, "accent"};
    f.gap = 0.0f;
    layout::Header h;
    h.height = 22.0f;
    f.header = h;
    const PaneLook l = pane_look("", &f);
    EXPECT_EQ(l.radius, 3.0f);
    EXPECT_EQ(l.border.color, "#112233");
    EXPECT_EQ(l.focus.width, 3.0f);
    EXPECT_EQ(l.gap, 0.0f);
    EXPECT_EQ(l.header, 22.0f);
}

TEST(LayoutStyles, ToastAreas) {
    EXPECT_TRUE(toast_area("mac").top);
    EXPECT_EQ(toast_area("mac").offset, 38.0f);
    EXPECT_FALSE(toast_area("").top);
    EXPECT_EQ(toast_area("").offset, 0.0f);
    EXPECT_EQ(toast_area("classic").offset, 22.0f);
    EXPECT_EQ(toast_area("cockpit", 144.0f).offset, 144.0f);
    EXPECT_EQ(toast_area("tiling").offset, 0.0f);
}

TEST(LayoutStyles, EffectsAllowed) {
    EXPECT_TRUE(effects_allowed("light", false, false));
    EXPECT_TRUE(effects_allowed("full", false, false));
    EXPECT_FALSE(effects_allowed("off", false, false));
    EXPECT_FALSE(effects_allowed("full", true, false));  // light theme
    EXPECT_FALSE(effects_allowed("full", false, true));  // high contrast
}

TEST(LayoutStyles, Clocks) {
    EXPECT_TRUE(has_clock("mac"));
    EXPECT_FALSE(has_clock(""));
    EXPECT_FALSE(has_clock("terminals"));
}
