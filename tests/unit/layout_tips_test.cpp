// First-use layout tips (libs/ui layout_tips): a card for every built-in layout and Focus mode,
// tips that name real commands, keys looked up rather than written in, and seen-tracking.
#include "agentdesk/ui/layout_tips.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <string>

#include <gtest/gtest.h>

#include "agentdesk/ui/command_names.hpp"
#include "agentdesk/ui/keymap.hpp"
#include "agentdesk/ui/layout_styles.hpp"

namespace tips = agentdesk::ui::tips;

namespace {
std::string command_name(const std::string& command) {
    return command.substr(0, command.find(':'));
}
}  // namespace

// Every built-in layout and Focus mode has a card titled by its display name, with a purpose
// sentence and three to five tips.
TEST(LayoutTips, EveryLayoutAndFocusHasACard) {
    const auto& keys = tips::card_keys();
    for (const char* k : {"workspace", "terminals", "mac", "mission", "review", "classic", "focus"})
        EXPECT_NE(std::find(keys.begin(), keys.end(), k), keys.end()) << k;
    // Removed layouts have no card any more.
    for (const char* k : {"tiling", "cockpit"}) {
        EXPECT_EQ(std::find(keys.begin(), keys.end(), k), keys.end()) << k;
        EXPECT_FALSE(tips::card(k)) << k;
    }
    for (const auto& k : keys) {
        const auto c = tips::card(k);
        ASSERT_TRUE(c) << k;
        EXPECT_EQ(c->key, k);
        EXPECT_EQ(c->title,
                  k == "focus" ? std::string("Focus mode") : agentdesk::ui::styles::display_name(k));
        EXPECT_FALSE(c->purpose.empty()) << k;
        EXPECT_GE(c->tips.size(), 3u) << k;
        EXPECT_LE(c->tips.size(), 5u) << k;
    }
    EXPECT_FALSE(tips::card("my-layout"));
    EXPECT_FALSE(tips::card(""));
}

// A tip's command (and its second one) is a command AgentDesk runs; a tip without a command must
// be shown regardless (a mouse action or a note).
TEST(LayoutTips, EveryTipCommandIsAKnownCommand) {
    for (const auto& k : tips::card_keys()) {
        for (const auto& t : tips::card(k)->tips) {
            EXPECT_FALSE(t.text.empty()) << k;
            if (t.command.empty())
                EXPECT_TRUE(t.always) << k << ": " << t.text;
            else
                EXPECT_TRUE(agentdesk::ui::is_builtin_command(command_name(t.command)))
                    << k << ": " << t.command;
            if (!t.command2.empty())
                EXPECT_TRUE(agentdesk::ui::is_builtin_command(command_name(t.command2)))
                    << k << ": " << t.command2;
        }
    }
    EXPECT_TRUE(agentdesk::ui::is_builtin_command("layout.tips"));
}

// Keys come from the keymap: an unbound keyboard tip is left out, a mouse tip stays without a key,
// two commands are shown as "A / B".
TEST(LayoutTips, ResolveTakesKeysFromTheKeymap) {
    const auto review = *tips::card("review");
    const std::map<std::string, std::string, std::less<>> keys = {
        {"review.next_file", "F8"}, {"review.prev_file", "Shift+F8"}, {"review.toggle", "Ctrl+Shift+R"}};
    auto chord = [&](std::string_view c) {
        const auto it = keys.find(c);
        return it == keys.end() ? std::string() : it->second;
    };
    const auto shown = tips::resolve(review, chord);
    ASSERT_FALSE(shown.empty());
    EXPECT_EQ(shown.front().chord, "F8 / Shift+F8");
    bool has_toggle = false, has_mouse = false, has_agent_next = false;
    for (const auto& t : shown) {
        has_toggle = has_toggle || t.chord == "Ctrl+Shift+R";
        has_mouse = has_mouse || (t.chord.empty() && t.text.find("commit") != std::string::npos);
        has_agent_next = has_agent_next || t.text.find("follows") != std::string::npos;
    }
    EXPECT_TRUE(has_toggle);
    EXPECT_TRUE(has_mouse);
    EXPECT_FALSE(has_agent_next);  // agent.next unbound here: its keyboard tip is dropped

    // Nothing bound: only the mouse tips and notes remain, none with a key.
    for (const auto& k : tips::card_keys())
        for (const auto& t : tips::resolve(*tips::card(k), [](std::string_view) { return std::string(); }))
            EXPECT_TRUE(t.chord.empty()) << k;
}

// Which card goes with what is shown.
TEST(LayoutTips, CardKeyForTheShownLayout) {
    EXPECT_EQ(tips::card_key("workspace", "", false), "workspace");
    EXPECT_EQ(tips::card_key("mission", "mission", false), "mission");
    EXPECT_EQ(tips::card_key("mac", "mac", true), "focus");
    EXPECT_EQ(tips::card_key("mac-copy", "mac", false), "mac");  // a copy of a built-in style
    EXPECT_EQ(tips::card_key("wall", "", false), "");            // a layout of the person's own
    EXPECT_EQ(tips::card_key("focus", "", false), "");           // not a layout name
}

// Cards appear once per key while tips are on; an existing user still sees the cards of layouts
// they have not opened since tips arrived.
TEST(LayoutTips, SeenTracking) {
    std::vector<std::string> seen;
    EXPECT_TRUE(tips::should_show("mac", true, seen));
    EXPECT_FALSE(tips::should_show("mac", false, seen));
    EXPECT_FALSE(tips::should_show("", true, seen));
    EXPECT_FALSE(tips::should_show("nope", true, seen));
    EXPECT_TRUE(tips::mark_seen(seen, "mac"));
    EXPECT_FALSE(tips::mark_seen(seen, "mac"));
    EXPECT_FALSE(tips::mark_seen(seen, ""));
    EXPECT_EQ(seen.size(), 1u);
    EXPECT_FALSE(tips::should_show("mac", true, seen));
    EXPECT_TRUE(tips::should_show("focus", true, seen));
    EXPECT_TRUE(tips::should_show("workspace", true, seen));
}

// The Terminals card covers the options and keys that came from Tiling, and Classic's card says how
// to keep it listed (Fun layouts).
TEST(LayoutTips, MergedTerminalsAndFunLayoutsAreExplained) {
    const auto tm = tips::card("terminals");
    ASSERT_TRUE(tm);
    std::set<std::string> cmds;
    for (const auto& t : tm->tips)
        cmds.insert(t.command);
    EXPECT_TRUE(cmds.contains("terminals.resize_mode"));
    EXPECT_TRUE(cmds.contains("pane.split:auto"));
    EXPECT_TRUE(cmds.contains("terminals.gaps"));
    const auto classic = tips::card("classic");
    ASSERT_TRUE(classic);
    bool fun = false;
    for (const auto& t : classic->tips)
        fun = fun || t.text.find("Fun layouts") != std::string::npos;
    EXPECT_TRUE(fun);
    // Terminals' keys come from its own keys.json section.
    agentdesk::ui::Keymap k = agentdesk::ui::Keymap::defaults();
    k.set_layout("terminals");
    const auto shown = tips::resolve(*tm, [&](std::string_view c) {
        for (const auto& [chord, cmd] : k.layout_map())
            if (cmd == c)
                return agentdesk::ui::Keymap::format(chord);
        return k.chord_for(std::string(c));
    });
    bool resize_key = false;
    for (const auto& st : shown)
        resize_key = resize_key || (st.text.starts_with("Resize mode") && st.chord == "Alt+R");
    EXPECT_TRUE(resize_key);
}
