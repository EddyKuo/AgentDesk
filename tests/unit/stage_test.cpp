// Tests for the Stage layout's rules: the strip's order, filters and thumbnail sizes, stepping and
// the swap history, when an agent may take the stage by itself, the strip's scrolling, the
// needs-you glow, and how Stage replaced Mission Control (layout, commands, keys, saved names).
#include "agentdesk/ui/stage.hpp"

#include <algorithm>

#include <gtest/gtest.h>

#include "agentdesk/ui/command_names.hpp"
#include "agentdesk/ui/keymap.hpp"
#include "agentdesk/ui/layout.hpp"
#include "agentdesk/ui/layout_styles.hpp"
#include "agentdesk/ui/layout_tips.hpp"

using namespace agentdesk::ui;
using namespace agentdesk::ui::stage;

namespace {
StripAgent agent(const char* id, Attention a, long long first_seen, long long blocked_since = 0,
                 bool unreachable = false) {
    return {id, a, blocked_since, first_seen, unreachable};
}
using Ids = std::vector<std::string>;
}  // namespace

// The strip keeps a stable order (first seen first); an agent that needs you jumps to the front,
// the one waiting longest first; the agent on stage is not in the strip; unreachable ones go last.
TEST(Stage, StripOrderPutsNeedsYouFirstAndKeepsTheRestStable) {
    const std::vector<StripAgent> agents = {
        agent("a", Attention::kWorking, 0),      agent("b", Attention::kIdle, 1),
        agent("c", Attention::kBlocked, 2, 500), agent("d", Attention::kDone, 3),
        agent("e", Attention::kBlocked, 4, 200), agent("f", Attention::kWorking, 5, 0, true),
    };
    EXPECT_EQ(strip_order(agents, "", Filter::kAll), (Ids{"e", "c", "a", "b", "d", "f"}));
    // The agent on stage stays out of its own strip.
    EXPECT_EQ(strip_order(agents, "a", Filter::kAll), (Ids{"e", "c", "b", "d", "f"}));
    EXPECT_EQ(strip_order(agents, "e", Filter::kAll), (Ids{"c", "a", "b", "d", "f"}));
    // A state change does not reshuffle the others: only needing you moves an agent.
    auto later = agents;
    later[1].att = Attention::kWorking;
    later[3].att = Attention::kIdle;
    EXPECT_EQ(strip_order(later, "", Filter::kAll), (Ids{"e", "c", "a", "b", "d", "f"}));
    // An unreachable agent that needs you does not jump ahead (nothing can be done about it).
    auto off = agents;
    off[5].att = Attention::kBlocked;
    off[5].blocked_since = 1;
    EXPECT_EQ(strip_order(off, "", Filter::kAll).back(), "f");
    EXPECT_TRUE(strip_order({}, "", Filter::kAll).empty());
}

TEST(Stage, FiltersAndTheirNames) {
    const std::vector<StripAgent> agents = {
        agent("a", Attention::kWorking, 0), agent("b", Attention::kBlocked, 1, 9),
        agent("c", Attention::kDone, 2), agent("d", Attention::kWorking, 3)};
    EXPECT_EQ(strip_order(agents, "", Filter::kNeedsYou), (Ids{"b"}));
    EXPECT_EQ(strip_order(agents, "", Filter::kWorking), (Ids{"a", "d"}));
    EXPECT_EQ(strip_order(agents, "a", Filter::kWorking), (Ids{"d"}));
    EXPECT_TRUE(filter_matches(Filter::kAll, Attention::kNone));
    EXPECT_FALSE(filter_matches(Filter::kNeedsYou, Attention::kWorking));
    for (const Filter f : {Filter::kAll, Filter::kNeedsYou, Filter::kWorking})
        EXPECT_EQ(parse_filter(filter_name(f)), f);
    EXPECT_EQ(filter_name(Filter::kNeedsYou), "needs_you");
    // Mission Control's saved filters read as the nearest one.
    EXPECT_EQ(parse_filter("blocked"), Filter::kNeedsYou);
    EXPECT_EQ(parse_filter("working"), Filter::kWorking);
    EXPECT_EQ(parse_filter("active"), Filter::kAll);
    EXPECT_EQ(parse_filter("done"), Filter::kAll);
    EXPECT_EQ(parse_filter(""), Filter::kAll);
}

// Three sizes, each bigger than the last; names round-trip; the size button cycles; the strip
// gives way on a short window but never below S.
TEST(Stage, SizePresets) {
    const SizePreset& s = preset(Size::kSmall);
    const SizePreset& m = preset(Size::kMedium);
    const SizePreset& l = preset(Size::kLarge);
    EXPECT_LT(s.thumb_w, m.thumb_w);
    EXPECT_LT(m.thumb_w, l.thumb_w);
    EXPECT_LT(s.thumb_h, m.thumb_h);
    EXPECT_LT(m.thumb_h, l.thumb_h);
    EXPECT_LT(s.mono_size, m.mono_size);
    EXPECT_LT(m.mono_size, l.mono_size);
    EXPECT_EQ(s.footer_h, 0.0f);  // S has no footer line
    for (const SizePreset* p : {&s, &m, &l}) {
        // On the 4 px grid, and at least three screen lines fit under the header.
        EXPECT_EQ(static_cast<int>(p->thumb_w) % 4, 0);
        EXPECT_EQ(static_cast<int>(p->thumb_h) % 4, 0);
        EXPECT_GE((p->thumb_h - p->header_h - p->footer_h) / p->line_h, 3.0f);
        EXPECT_GE(p->line_h, p->mono_size);
    }
    for (const Size z : {Size::kSmall, Size::kMedium, Size::kLarge})
        EXPECT_EQ(parse_size(size_name(z)), z);
    EXPECT_EQ(parse_size("large"), Size::kLarge);
    EXPECT_EQ(parse_size("huge"), Size::kMedium);
    EXPECT_EQ(next_size(Size::kSmall), Size::kMedium);
    EXPECT_EQ(next_size(Size::kMedium), Size::kLarge);
    EXPECT_EQ(next_size(Size::kLarge), Size::kSmall);
    EXPECT_EQ(strip_height(Size::kMedium), kStripHeaderH + m.thumb_h + kStripBottomPad);
    EXPECT_EQ(fit_size(Size::kLarge, 1000.0f), Size::kLarge);
    EXPECT_EQ(fit_size(Size::kLarge, 500.0f), Size::kMedium);
    EXPECT_EQ(fit_size(Size::kMedium, 420.0f), Size::kSmall);
    EXPECT_EQ(fit_size(Size::kSmall, 100.0f), Size::kSmall);
    EXPECT_EQ(fit_size(Size::kSmall, 1000.0f), Size::kSmall);  // never bigger than asked for
}

// Next / previous go through every agent in the stable order, wrapping; the ring does not follow
// the strip's needs-you order.
TEST(Stage, StepThroughTheLineup) {
    const std::vector<StripAgent> agents = {
        agent("a", Attention::kIdle, 2), agent("b", Attention::kBlocked, 0, 5),
        agent("c", Attention::kWorking, 1, 0, true), agent("d", Attention::kDone, 3)};
    const Ids ring = lineup(agents);
    EXPECT_EQ(ring, (Ids{"b", "a", "d", "c"}));
    EXPECT_EQ(step(ring, "b", 1), "a");
    EXPECT_EQ(step(ring, "c", 1), "b");  // wraps
    EXPECT_EQ(step(ring, "b", -1), "c");
    EXPECT_EQ(step(ring, "zzz", 1), "b");   // not in it: the first
    EXPECT_EQ(step(ring, "zzz", -1), "c");  // or the last, going back
    EXPECT_EQ(step({}, "a", 1), "");
    EXPECT_EQ(step({"a"}, "a", 1), "a");
}

// The history: most recent first, each agent once; "back" is the one before; closed agents go.
TEST(Stage, SwapHistory) {
    History h;
    EXPECT_EQ(h.current(), "");
    EXPECT_EQ(h.previous(), "");
    h.push("a");
    EXPECT_EQ(h.previous(), "");
    h.push("b");
    EXPECT_EQ(h.current(), "b");
    EXPECT_EQ(h.previous(), "a");
    h.push("a");  // back and forth flips between the two
    EXPECT_EQ(h.current(), "a");
    EXPECT_EQ(h.previous(), "b");
    h.push("a");  // already in front: nothing changes
    h.push("");
    EXPECT_EQ(h.items(), (Ids{"a", "b"}));
    h.push("c");
    h.forget("a");
    EXPECT_EQ(h.items(), (Ids{"c", "b"}));
    for (int i = 0; i < 40; ++i)
        h.push("x" + std::to_string(i));
    EXPECT_EQ(h.items().size(), History::kMax);
    EXPECT_EQ(h.current(), "x39");
}

// Off by default; when on, never while typing, with a menu open, or away from an agent that needs
// you too; the first new one in strip order wins.
TEST(Stage, AutoSwitchNeverStealsTheStageWhileYouType) {
    const Ids fresh = {"b", "c"};
    AutoSwitch in;
    in.ms_since_input = 60000;
    EXPECT_EQ(auto_switch_target(in, fresh), "");  // off by default
    in.enabled = true;
    EXPECT_EQ(auto_switch_target(in, fresh), "b");
    EXPECT_EQ(auto_switch_target(in, {}), "");
    AutoSwitch typing = in;
    typing.ms_since_input = 500;
    EXPECT_EQ(auto_switch_target(typing, fresh), "");
    typing.ms_since_input = kTypingQuietMs - 1;
    EXPECT_EQ(auto_switch_target(typing, fresh), "");
    typing.ms_since_input = kTypingQuietMs;
    EXPECT_EQ(auto_switch_target(typing, fresh), "b");
    AutoSwitch busy = in;
    busy.stage_needs_you = true;
    EXPECT_EQ(auto_switch_target(busy, fresh), "");
    AutoSwitch menu = in;
    menu.popup_open = true;
    EXPECT_EQ(auto_switch_target(menu, fresh), "");
    EXPECT_FALSE(AutoSwitch{}.enabled);
}

TEST(Stage, StripScrolling) {
    EXPECT_EQ(content_width(0, 100, 10), 0.0f);
    EXPECT_EQ(content_width(1, 100, 10), 100.0f);
    EXPECT_EQ(content_width(4, 100, 10), 430.0f);
    EXPECT_EQ(clamp_scroll(-5, 430, 200), 0.0f);
    EXPECT_EQ(clamp_scroll(500, 430, 200), 230.0f);
    EXPECT_EQ(clamp_scroll(50, 150, 200), 0.0f);  // everything fits: no scroll
    // Bring a thumbnail into view, moving as little as possible.
    EXPECT_EQ(scroll_to_show(0, 0, 100, 10, 250), 0.0f);
    EXPECT_EQ(scroll_to_show(0, 2, 100, 10, 250), 70.0f);     // its right edge at the view's
    EXPECT_EQ(scroll_to_show(200, 1, 100, 10, 250), 110.0f);  // its left edge at the view's
    EXPECT_EQ(scroll_to_show(100, 1, 100, 10, 250), 100.0f);  // already in view
    EXPECT_EQ(scroll_to_show(42, -1, 100, 10, 250), 42.0f);
}

// The glow breathes a few times, then holds; with reduced motion it holds from the start.
TEST(Stage, NeedsYouGlowPulsesThenSettles) {
    EXPECT_FLOAT_EQ(glow_level(0, false), kGlowSteady);
    EXPECT_FLOAT_EQ(glow_level(kPulseMs * 0.5, false), 1.0f);  // the top of a breath
    for (double ms = 0; ms < kPulseMs * kPulseCount; ms += 37) {
        EXPECT_GE(glow_level(ms, false), kGlowSteady);
        EXPECT_LE(glow_level(ms, false), 1.0f);
        EXPECT_TRUE(glow_moving(ms, false));
    }
    EXPECT_FLOAT_EQ(glow_level(kPulseMs * kPulseCount + 1, false), kGlowSteady);
    EXPECT_FALSE(glow_moving(kPulseMs * kPulseCount, false));
    EXPECT_FLOAT_EQ(glow_level(kPulseMs * 0.5, true), kGlowSteady);
    EXPECT_FALSE(glow_moving(10, true));
}

// The shipped layout: a bar, the stack with the stage (one pane of the active tab) and the empty
// stage, and the filmstrip; Stage is a built-in where Mission Control was.
TEST(Stage, ShippedLayoutHasBarStageAndStrip) {
    const layout::Node n = layout::builtin("stage");
    EXPECT_EQ(n.find("bar")->mod(), "stage-bar");
    const layout::Node* stack = n.find("stage-body");
    ASSERT_NE(stack, nullptr);
    EXPECT_EQ(stack->kind, layout::Kind::kStack);
    EXPECT_EQ(stack->active_child()->id, "stage");
    const layout::Node* main = n.find("stage");
    EXPECT_EQ(main->content.type, layout::Content::Type::kTab);
    ASSERT_TRUE(main->content.pane.has_value());
    EXPECT_TRUE(main->content.pane->lone);  // one pane, the focused one
    EXPECT_EQ(n.find("empty")->mod(), "stage-empty");
    EXPECT_EQ(n.find("strip")->mod(), "stage-strip");
    EXPECT_EQ(n.find("strip")->size, layout::Span::pixels(strip_height(Size::kMedium)));
    EXPECT_EQ(styles::style_of_tree(n), "stage");
    EXPECT_EQ(styles::display_name("stage"), "Stage");
    const auto names = layout::builtin_names();
    EXPECT_NE(std::find(names.begin(), names.end(), "stage"), names.end());
    EXPECT_EQ(std::find(names.begin(), names.end(), "mission"), names.end());
    EXPECT_FALSE(styles::is_fun_layout("stage"));
}

// Every stage.* command is known (and Mission Control's names for the ones Stage took over);
// Ctrl+Shift+M toggles Stage; Alt+Left / Alt+Right step through agents only while Stage shows.
TEST(Stage, CommandsAndKeys) {
    for (const char* c : {"stage.toggle", "stage.show", "stage.next", "stage.prev", "stage.back",
                          "stage.filter", "stage.size", "stage.auto_switch", "stage.shells", "mission.toggle",
                          "mission.expand", "mission.filter"})
        EXPECT_TRUE(is_builtin_command(c)) << c;
    Keymap k = Keymap::defaults();
    EXPECT_EQ(k.chord_for("stage.toggle"), "Ctrl+Shift+M");
    EXPECT_EQ(k.chord_for("mission.toggle"), "");
    const ImGuiKeyChord alt_left = ImGuiMod_Alt | ImGuiKey_LeftArrow;
    const ImGuiKeyChord alt_right = ImGuiMod_Alt | ImGuiKey_RightArrow;
    EXPECT_EQ(k.global(alt_left), "pane.go:left");  // elsewhere unchanged
    k.set_layout("stage");
    EXPECT_EQ(k.global(alt_left), "stage.prev");
    EXPECT_EQ(k.global(alt_right), "stage.next");
    EXPECT_EQ(k.global(ImGuiMod_Ctrl | ImGuiKey_1), "tab.select:1");  // Ctrl+1..9 stay with the tabs
}

// A saved "layout": "mission" opens Stage; the first-use tips cover Stage and drop Mission Control.
TEST(Stage, ReplacesMissionControl) {
    const auto r = styles::replacement_for("mission");
    ASSERT_TRUE(r);
    EXPECT_EQ(r->layout, "stage");
    EXPECT_FALSE(styles::replacement_for("stage"));
    const auto& keys = tips::card_keys();
    EXPECT_NE(std::find(keys.begin(), keys.end(), "stage"), keys.end());
    EXPECT_EQ(std::find(keys.begin(), keys.end(), "mission"), keys.end());
    const auto card = tips::card("stage");
    ASSERT_TRUE(card);
    bool swap = false, keys_tip = false, auto_tip = false;
    for (const auto& t : card->tips) {
        swap = swap || t.text.find("swap") != std::string::npos;
        keys_tip = keys_tip || t.command == "stage.next";
        auto_tip = auto_tip || t.command == "stage.auto_switch";
    }
    EXPECT_TRUE(swap);
    EXPECT_TRUE(keys_tip);
    EXPECT_TRUE(auto_tip);
}
