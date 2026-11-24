// Keymap tests for the macOS key table (FR-40).
#include <gtest/gtest.h>

#include "agentdesk/ui/keymap.hpp"

using namespace agentdesk::ui;

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
