// Keymap tests for the Windows key table (Ctrl shortcuts).
#include <gtest/gtest.h>

#include "agentdesk/ui/keymap.hpp"

using namespace agentdesk::ui;

TEST(Keymap, ParsesChordsAndAppliesOverrides) {
    EXPECT_EQ(*Keymap::parse("ctrl-shift-t"), ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_T);
    EXPECT_EQ(*Keymap::parse("ctrl--"), ImGuiMod_Ctrl | ImGuiKey_Minus);
    EXPECT_EQ(*Keymap::parse("ctrl-?"), ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Slash);
    EXPECT_EQ(*Keymap::parse("alt-1"), ImGuiMod_Alt | ImGuiKey_1);
    EXPECT_EQ(*Keymap::parse("F2"), ImGuiKey_F2);
    EXPECT_FALSE(Keymap::parse("hyper-x").has_value());
    auto k = Keymap::defaults();
    EXPECT_EQ(*k.global(*Keymap::parse("ctrl-shift-t")), "tab.new");
    EXPECT_EQ(*k.terminal(*Keymap::parse("ctrl-shift-f")), "terminal.search");
    // A file with a problem installs nothing: the defaults stay.
    auto bad = k.apply({{"global", {{"ctrl-shift-t", nullptr}, {"??x", "a"}}}});
    ASSERT_EQ(bad.size(), 1u);
    EXPECT_NE(bad[0].find("??x"), std::string::npos);
    EXPECT_EQ(*k.global(*Keymap::parse("ctrl-shift-t")), "tab.new");
    bad = k.apply({{"global", {{"ctrl-shift-t", nullptr}, {"ctrl-alt-n", "space.new"}}}});
    EXPECT_TRUE(bad.empty());
    EXPECT_FALSE(k.global(*Keymap::parse("ctrl-shift-t")).has_value());
    EXPECT_EQ(*k.global(*Keymap::parse("ctrl-alt-n")), "space.new");
}

// keys.json forms: layout sections win over global, "terminal" gives the key to the
// program, {"command","args"} objects, old names migrated, unknown sections/actions reported.
TEST(Keymap, SectionsObjectsAndValidation) {
    auto k = Keymap::defaults();
    const auto known = [](std::string_view n) { return n != "no.such"; };
    const auto bad =
        k.apply({{"global",
                  {{"ctrl-shift-y", {{"command", "layout.switch"}, {"args", {{"name", "grid"}}}}},
                   {"ctrl-shift-u", "project.new"}}},
                 {"layout:grid", {{"ctrl-shift-t", "terminal"}, {"ctrl-shift-j", "tab.next"}}},
                 {"terminal", {{"ctrl-shift-f", "terminal"}}}},
                known);
    EXPECT_TRUE(bad.empty()) << bad.front();
    EXPECT_EQ(*k.global(*Keymap::parse("ctrl-shift-y")), "layout.switch:grid");
    EXPECT_EQ(*k.global(*Keymap::parse("ctrl-shift-u")), "space.new");
    EXPECT_EQ(*k.global(*Keymap::parse("ctrl-shift-t")), "tab.new");  // other layouts
    k.set_layout("grid");
    EXPECT_FALSE(k.global(*Keymap::parse("ctrl-shift-t")).has_value());  // to the program
    EXPECT_EQ(*k.global(*Keymap::parse("ctrl-shift-j")), "tab.next");
    EXPECT_EQ(*k.terminal(*Keymap::parse("ctrl-shift-f")), "terminal");
    EXPECT_EQ(*k.terminal(*Keymap::parse("ctrl-v")), "terminal.paste_image");

    const auto errs = k.apply({{"windows", {{"f5", "tab.new"}}}, {"global", {{"f6", "no.such"}}}}, known);
    EXPECT_EQ(errs.size(), 2u);
}

// The defaults are written as keys.json when the file is missing: that file must apply cleanly
// and change nothing.
TEST(Keymap, DefaultFileAppliesCleanly) {
    auto k = Keymap::defaults();
    const auto file = Keymap::default_file();
    EXPECT_TRUE(k.apply(file, [](std::string_view) { return true; }).empty());
    EXPECT_EQ(*k.global(*Keymap::parse("ctrl-shift-t")), "tab.new");
    EXPECT_EQ(*k.global(*Keymap::parse("ctrl-3")), "tab.select:3");
    EXPECT_EQ(*k.terminal(*Keymap::parse("ctrl-v")), "terminal.paste_image");
    EXPECT_EQ(Keymap::defaults().global_map(), k.global_map());
}
