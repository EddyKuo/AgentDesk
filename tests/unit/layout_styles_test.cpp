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
    // The removed Tiling and Cockpit layouts' mods select nothing any more.
    for (const char* m : {"tiling-bar", "tiling-pane", "cockpit-header", "cockpit-gauges"})
        EXPECT_EQ(style_of_mod(m), "") << m;
    EXPECT_EQ(style_of_mod("focus-strip"), "focus");
    for (const char* m : {"stage-bar", "stage-strip", "stage-empty"})
        EXPECT_EQ(style_of_mod(m), "stage") << m;
    // Mission Control's mods went with it (Stage replaced it).
    for (const char* m : {"mission-bar", "mission-wall"})
        EXPECT_EQ(style_of_mod(m), "") << m;
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
    EXPECT_EQ(style_of_tree(layout::builtin("stage")), "stage");
    EXPECT_EQ(style_of_tree(layout::builtin("review")), "review");
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
        {"type":"block","content":{"mod":"review-head"}},
        {"type":"block","content":{"mod":"classic-status"}}]})")),
              "review");
    EXPECT_EQ(style_of_tree(tree_of(R"({"type":"column","children":[
        {"type":"block","content":{"mod":"spaces"}}]})")),
              "");
}

TEST(LayoutStyles, DisplayNames) {
    EXPECT_EQ(display_name("workspace"), "AgentDesk");
    EXPECT_EQ(display_name("terminals"), "Terminals");
    EXPECT_EQ(display_name("mac"), "AgentDesk Mac");
    EXPECT_EQ(display_name("stage"), "Stage");
    EXPECT_EQ(display_name("review"), "Review");
    EXPECT_EQ(display_name("classic"), "AgentDesk Classic");
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
    EXPECT_EQ(pane_look("classic", nullptr).header, 20.0f);
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
    // Stage's filmstrip fills the bottom: notes come in under its bar.
    EXPECT_TRUE(toast_area("stage").top);
    EXPECT_EQ(toast_area("stage").offset, 50.0f);
    EXPECT_EQ(toast_area("", 40.0f).offset, 40.0f);
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

// Classic (and Garden, when it ships) are fun layouts: out of the cards and the Ctrl+Shift+L cycle
// until Settings > Look > Fun layouts is on; the layout showing always keeps its card.
TEST(LayoutStyles, FunLayoutsStayOutOfTheListUntilTurnedOn) {
    EXPECT_TRUE(is_fun_layout("classic"));
    EXPECT_TRUE(is_fun_layout("garden"));
    for (const char* l : {"workspace", "terminals", "mac", "stage", "review", "mine"})
        EXPECT_FALSE(is_fun_layout(l)) << l;
    EXPECT_FALSE(is_listed("classic", false));
    EXPECT_TRUE(is_listed("classic", true));
    EXPECT_TRUE(is_listed("classic", false, "classic"));
    EXPECT_TRUE(is_listed("terminals", false));

    const std::vector<std::string> names = {"workspace", "terminals", "mac", "stage",
                                            "review",    "classic",   "mine"};
    EXPECT_EQ(next_listed(names, "workspace", false), "terminals");
    EXPECT_EQ(next_listed(names, "review", false), "mine");  // Classic is skipped
    EXPECT_EQ(next_listed(names, "review", true), "classic");
    EXPECT_EQ(next_listed(names, "mine", false), "workspace");  // wraps
    EXPECT_EQ(next_listed(names, "classic", false), "mine");    // leaving the fun layout
    EXPECT_EQ(next_listed(names, "gone", false), "workspace");  // not in the list: the first
    EXPECT_EQ(next_listed({"classic"}, "classic", false), "classic");
    EXPECT_EQ(next_listed({}, "x", false), "");
}

// A saved layout that no longer ships opens its replacement.
TEST(LayoutStyles, RemovedLayoutsHaveReplacements) {
    const auto tiling = replacement_for("tiling");
    ASSERT_TRUE(tiling);
    EXPECT_EQ(tiling->layout, "terminals");
    EXPECT_TRUE(tiling->square_corners);
    const auto cockpit = replacement_for("cockpit");
    ASSERT_TRUE(cockpit);
    EXPECT_EQ(cockpit->layout, "workspace");
    EXPECT_FALSE(cockpit->square_corners);
    const auto mission = replacement_for("mission");  // Stage replaced Mission Control
    ASSERT_TRUE(mission);
    EXPECT_EQ(mission->layout, "stage");
    EXPECT_FALSE(mission->square_corners);
    for (const char* l : {"workspace", "terminals", "classic", "stage", "mine", ""})
        EXPECT_FALSE(replacement_for(l)) << l;
}
