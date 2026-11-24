// Tests for the AgentDesk Classic layout's rules: the shipped file, the monochrome mapping and its
// contrast, the cell grid, the tab line's truncation, the state marks, the scan lines at
// non-integer zooms, the narrow menu bar and the layout's default key.
#include "agentdesk/ui/classic_layout.hpp"

#include <cmath>
#include <set>

#include <gtest/gtest.h>

#include "agentdesk/ui/command_names.hpp"
#include "agentdesk/ui/keymap.hpp"
#include "agentdesk/ui/layout.hpp"
#include "agentdesk/ui/layout_styles.hpp"

using namespace agentdesk::ui;
using namespace agentdesk::ui::classic;

namespace {
constexpr ImU32 rgb(unsigned r, unsigned g, unsigned b) {
    return IM_COL32(r, g, b, 255);
}
// phosphor-green: terminal background and foreground.
constexpr ImU32 kGreenBg = rgb(0x07, 0x12, 0x0B);
constexpr ImU32 kGreenFg = rgb(0x7C, 0xF2, 0x9A);
// phosphor-paper.
constexpr ImU32 kPaperBg = rgb(0xF4, 0xF5, 0xEC);
constexpr ImU32 kPaperFg = rgb(0x21, 0x40, 0x2A);
}  // namespace

TEST(ClassicLayout, ShippedLayoutParsesAndIsClassicStyle) {
    const layout::Node n = layout::builtin("classic");
    EXPECT_EQ(styles::style_of_tree(n), "classic");
    for (const char* id : {"menubar", "tabs", "status"})
        ASSERT_NE(n.find(id), nullptr) << id;
    EXPECT_EQ(n.find("menubar")->mod(), "classic-menubar");
    EXPECT_EQ(n.find("tabs")->mod(), "classic-tabs");
    EXPECT_EQ(n.find("status")->mod(), "classic-status");
    ASSERT_NE(n.find("sidebar"), nullptr);  // Ctrl+Shift+B shows it
    EXPECT_TRUE(n.find("sidebar")->hidden);
    EXPECT_EQ(n.find("sidebar")->mod(), "classic-sidebar");
    const styles::PaneLook look = styles::pane_look("classic", &*n.find("main")->content.pane);
    EXPECT_EQ(look.radius, 0.0f);
    EXPECT_EQ(look.header, 20.0f);
    EXPECT_EQ(styles::display_name("classic"), "AgentDesk Classic");
    EXPECT_EQ(styles::toast_area("classic").offset, 22.0f);  // toasts keep the status line clear
}

// Any text on any background stays readable: luminance at least 0.35 apart after mapping, and
// reverse video maps first, then swaps.
TEST(ClassicLayout, MonoColorKeepsContrast) {
    for (const auto& [bgc, fgc] : {std::pair{kGreenBg, kGreenFg}, std::pair{kPaperBg, kPaperFg}}) {
        const MonoRamp r = mono_ramp(bgc, fgc);
        const ImU32 samples[] = {rgb(0, 0, 0),
                                 rgb(255, 255, 255),
                                 rgb(255, 0, 0),
                                 rgb(0, 255, 0),
                                 rgb(0, 0, 255),
                                 rgb(128, 128, 128),
                                 rgb(40, 40, 40),
                                 rgb(200, 30, 30),
                                 rgb(30, 200, 30),
                                 rgb(250, 250, 120),
                                 rgb(10, 60, 10),
                                 bgc,
                                 fgc};
        for (const ImU32 f : samples)
            for (const ImU32 b : samples)
                for (const bool fd : {false, true})
                    for (const bool bd : {false, true}) {
                        const ImU32 fg = fd ? fgc : f;
                        const ImU32 bg = bd ? bgc : b;
                        const CellPair p = mono_cell(fg, bg, fd, bd, false, r);
                        EXPECT_GE(std::abs(luminance(p.fg) - luminance(p.bg)), 0.35f - 1e-3f);
                        const CellPair q = mono_cell(fg, bg, fd, bd, true, r);
                        EXPECT_EQ(q.fg, p.bg);
                        EXPECT_EQ(q.bg, p.fg);
                    }
    }
}

// The acceptance case: 24-bit red text on the default background in phosphor-green comes out
// green and at least 7:1 against it.
TEST(ClassicLayout, RedTextBecomesGreenWithStrongContrast) {
    const MonoRamp r = mono_ramp(kGreenBg, kGreenFg);
    const CellPair p = mono_cell(rgb(255, 0, 0), kGreenBg, false, true, false, r);
    EXPECT_EQ(p.bg, kGreenBg);  // the default background is left alone
    const unsigned red = (p.fg >> IM_COL32_R_SHIFT) & 0xFF, green = (p.fg >> IM_COL32_G_SHIFT) & 0xFF;
    EXPECT_GT(green, red);
    EXPECT_GE(contrast(p.fg, p.bg), 7.0f);
    // Default text keeps the theme's own colour; backgrounds stay darker than text.
    EXPECT_EQ(mono_cell(kGreenFg, kGreenBg, true, true, false, r).fg, kGreenFg);
    EXPECT_LT(luminance(mono_color(rgb(255, 255, 255), false, r)),
              luminance(mono_color(rgb(255, 255, 255), true, r)));
}

TEST(ClassicLayout, GridSnapsToWholePixels) {
    for (const float scale : {1.0f, 1.25f, 1.5f, 1.75f, 2.0f}) {
        for (const float advance : {7.2f, 8.4f, 9.6f, 11.05f}) {
            const Grid g = grid_for(advance * scale, scale);
            EXPECT_EQ(g.cell_w, std::round(g.cell_w));
            EXPECT_EQ(g.cell_h, std::round(g.cell_h));
            EXPECT_EQ(g.cell_h, std::round(20.0f * scale));
            const float width = 1003.0f;
            EXPECT_FLOAT_EQ(static_cast<float>(g.cols(width)) * g.cell_w + g.remainder(width), width);
            EXPECT_LT(g.remainder(width), g.cell_w);  // the rest stays on the right, under a cell
        }
    }
}

TEST(ClassicLayout, TextColumnsAndCuts) {
    EXPECT_EQ(text_cols("repo-a"), 6);
    EXPECT_EQ(text_cols("\xE4\xB8\xAD\xE6\x96\x87"), 4);  // 中文: two wide characters
    EXPECT_EQ(cut_cols("repo-abcdef", 6), "repo-\xE2\x80\xA6");
    EXPECT_EQ(cut_cols("repo", 6), "repo");
    // Never through a wide character: 中文字 in 4 cells is 中 and the ellipsis (3 cells).
    EXPECT_EQ(cut_cols("\xE4\xB8\xAD\xE6\x96\x87\xE5\xAD\x97", 4), "\xE4\xB8\xAD\xE2\x80\xA6");
    EXPECT_EQ(cut_cols("abc", 0), "");
}

TEST(ClassicLayout, TabLineTruncatesOldestFirst) {
    const std::vector<std::string> labels = {"1 claude [!]", "2 pwsh", "3 codex [~]", "4 logs"};
    auto out = fit_tabs(labels, 2, 200);
    EXPECT_EQ(out, labels);  // room for all
    out = fit_tabs(labels, 2, 36);
    EXPECT_EQ(out[0], "1\xE2\x80\xA6");  // the oldest shrinks first
    EXPECT_EQ(out[2], "3 codex [~]");    // the current one stays whole
    out = fit_tabs(labels, 2, 10);
    EXPECT_EQ(out[0], "1\xE2\x80\xA6");
    EXPECT_EQ(out[1], "2\xE2\x80\xA6");
    EXPECT_EQ(out[2], "3 codex [~]");
    EXPECT_EQ(out[3], "4\xE2\x80\xA6");
    // Wide names shrink to their number too, never half a character.
    const auto wide = fit_tabs({"1 \xE4\xB8\xAD\xE6\x96\x87", "2 x"}, 1, 5);
    EXPECT_EQ(wide[0], "1\xE2\x80\xA6");
}

TEST(ClassicLayout, StatusGlyphs) {
    std::set<std::string> seen;
    for (const Attention a :
         {Attention::kBlocked, Attention::kWorking, Attention::kDone, Attention::kIdle, Attention::kNone})
        seen.insert(status_glyph(a));
    EXPECT_EQ(seen.size(), 5u);
    EXPECT_STREQ(status_glyph(Attention::kBlocked), "[!]");
    EXPECT_STREQ(status_glyph(Attention::kNone), "[?]");
    EXPECT_EQ(spinner_frame(0), '|');
    EXPECT_EQ(spinner_frame(125), '/');
    EXPECT_EQ(spinner_frame(250), '-');
    EXPECT_EQ(spinner_frame(375), '\\');
    EXPECT_EQ(spinner_frame(500), '|');
}

TEST(ClassicLayout, ScanlineRowsOnDevicePixels) {
    for (const float scale : {1.0f, 1.25f, 1.5f, 1.75f, 2.0f}) {
        const auto rows = scanlines(10.0f, 10.0f + 22.0f * scale, scale);
        ASSERT_FALSE(rows.empty());
        float last = -1;
        for (const auto& [y, h] : rows) {
            EXPECT_EQ(y, std::round(y)) << scale;
            EXPECT_EQ(h, std::round(h)) << scale;
            EXPECT_GE(h, 1.0f);
            EXPECT_GT(y, last);
            EXPECT_LE(y + h, 10.0f + 22.0f * scale);
            last = y;
        }
        // Every row the same height: no thick and thin lines at non-integer zooms.
        for (const auto& r : rows)
            EXPECT_EQ(r.second, rows.front().second);
    }
}

TEST(ClassicLayout, NarrowMenuBar) {
    BarFit f = fit_bar(120, 60, 30, 8, 11);
    EXPECT_TRUE(f.all_menus);
    EXPECT_TRUE(f.clock);
    EXPECT_TRUE(f.counts);
    f = fit_bar(79, 60, 30, 8, 11);  // under 80 cells: AGENTDESK and View only
    EXPECT_FALSE(f.all_menus);
    EXPECT_TRUE(f.clock);
    f = fit_bar(45, 60, 30, 8, 11);  // the clock goes first
    EXPECT_FALSE(f.clock);
    EXPECT_TRUE(f.counts);
    f = fit_bar(35, 60, 30, 8, 11);  // then the counts
    EXPECT_FALSE(f.counts);
}

// F10 opens the menu bar only while Classic shows, and clashes with no default key.
TEST(ClassicLayout, LayoutKeysDoNotClashWithDefaults) {
    const auto file = Keymap::default_file();
    ASSERT_TRUE(file.contains("layout:classic"));
    std::set<ImGuiKeyChord> taken;
    for (const char* sec : {"global", "terminal"})
        for (const auto& [chord, cmd] : file[sec].items())
            taken.insert(*Keymap::parse(chord));
    for (const auto& [chord, cmd] : file["layout:classic"].items()) {
        EXPECT_FALSE(taken.contains(*Keymap::parse(chord))) << chord;
        EXPECT_TRUE(is_builtin_command(cmd.get<std::string>())) << chord;
    }
    Keymap k = Keymap::defaults();
    k.set_layout("classic");
    EXPECT_EQ(*k.global(ImGuiKey_F10), "classic.menu");
    k.set_layout("workspace");
    EXPECT_FALSE(k.global(ImGuiKey_F10).has_value());  // elsewhere F10 goes to the program
    EXPECT_TRUE(is_builtin_command("classic.effects"));
}
