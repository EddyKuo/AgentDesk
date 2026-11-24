// Tests for the rules Mission Control left behind (Stage and other views use them): a wall's grid,
// the tile order, the filter, the screen's last lines for a thumbnail, keyboard moves on a grid and
// message cleaning. Stage's own rules and Mission Control's retirement are in stage_test.cpp.
#include "agentdesk/ui/mission.hpp"

#include <gtest/gtest.h>

using namespace agentdesk;
using namespace agentdesk::ui;
using namespace agentdesk::ui::mission;

namespace {
// A rows x cols screen with `text` (ASCII, or char32_t for wide) written on rows.
terminal::ScreenSnapshot screen(int rows, int cols) {
    terminal::ScreenSnapshot s;
    s.rows = rows;
    s.cols = cols;
    s.cells.resize(static_cast<std::size_t>(rows * cols));
    return s;
}

void put(terminal::ScreenSnapshot& s, int row, int col, std::u32string_view text, int ansi = -1) {
    for (const char32_t c : text) {
        auto& cell = s.cells[static_cast<std::size_t>(row * s.cols + col)];
        cell.chars[0] = c;
        if (ansi >= 0) {
            cell.default_fg = false;
            cell.fg_ansi = static_cast<std::int8_t>(ansi);
            cell.fg = {205, 49, 49};
        }
        const bool wide = c >= 0x3000;
        cell.width = wide ? 2 : 1;
        if (wide)
            s.cells[static_cast<std::size_t>(row * s.cols + col + 1)].width = 0;
        col += wide ? 2 : 1;
    }
}

std::string text_of(const TailLine& l) {
    std::string t;
    for (const auto& seg : l.segments)
        t += seg.text;
    return t;
}
}  // namespace

// Columns by count (1, 2, 2x2, 3x2, 3x3, 4x3), the 240 x 150 minimum and scrolling.
TEST(Mission, WallGridColumnsRowsAndScrolling) {
    // The 1280 x 820 window's wall: 1256 x 726 inside 12 px padding.
    EXPECT_EQ(wall_grid(0, 1256, 726, 12, 240, 150).cols, 0);
    const auto one = wall_grid(1, 1256, 726, 12, 240, 150);
    EXPECT_EQ(one.cols, 1);
    EXPECT_EQ(one.rows, 1);
    EXPECT_EQ(wall_grid(2, 1256, 726, 12, 240, 150).cols, 2);
    const auto four = wall_grid(4, 1256, 726, 12, 240, 150);
    EXPECT_EQ(four.cols, 2);
    EXPECT_EQ(four.rows, 2);
    const auto six = wall_grid(6, 1256, 726, 12, 240, 150);
    EXPECT_EQ(six.cols, 3);
    EXPECT_EQ(six.rows, 2);
    EXPECT_NEAR(six.tile_w, (1256 - 24) / 3.0f, 0.01f);
    EXPECT_NEAR(six.tile_h, 357.0f, 0.01f);
    EXPECT_FALSE(six.scroll);
    EXPECT_EQ(wall_grid(9, 1256, 726, 12, 240, 150).cols, 3);
    const auto twelve = wall_grid(12, 1256, 726, 12, 240, 150);
    EXPECT_EQ(twelve.cols, 4);
    EXPECT_EQ(twelve.rows, 3);
    EXPECT_FALSE(twelve.scroll);
    // 30: four columns, rows of at least 150 px, scrolling; four rows share the height.
    const auto many = wall_grid(30, 1256, 726, 12, 240, 150);
    EXPECT_EQ(many.cols, 4);
    EXPECT_EQ(many.rows, 8);
    EXPECT_TRUE(many.scroll);
    EXPECT_GE(many.tile_h, 150.0f);
    EXPECT_NEAR(many.tile_h, (726 - 36) / 4.0f, 0.01f);
    EXPECT_NEAR(many.content_h, many.tile_h * 8 + 12 * 7, 0.01f);
    // A narrow window drops columns: 500 px fits two 244 px tiles, 400 px one.
    EXPECT_EQ(wall_grid(6, 500, 726, 12, 240, 150).cols, 2);
    EXPECT_EQ(wall_grid(6, 400, 726, 12, 240, 150).cols, 1);
    EXPECT_EQ(wall_grid(30, 3000, 2000, 12, 240, 150, 4).cols, 4);  // never more than four
}

// Needs you, done, working, idle, others; unreachable machines last; sub-agents fold into their
// parent unless asked for, then follow it; an orphan sub-agent stands alone.
TEST(Mission, WallOrderGroupsAndSubAgents) {
    const std::vector<WallAgent> agents = {
        {"idle", "", 1, 900, 1},       {"work", "", 2, 100, 2},    {"blocked", "", 4, 10, 3},
        {"done", "", 3, 50, 4},        {"kid", "work", 4, 999, 5}, {"far", "", 4, 999, 6, true},
        {"orphan", "gone", 2, 300, 7}, {"none", "", 0, 0, 8},
    };
    EXPECT_EQ(wall_order(agents, false),
              (std::vector<std::string>{"blocked", "done", "orphan", "work", "idle", "none", "far"}));
    EXPECT_EQ(wall_order(agents, true),
              (std::vector<std::string>{"blocked", "done", "orphan", "work", "kid", "idle", "none", "far"}));
    EXPECT_TRUE(wall_order({}, true).empty());
}

TEST(Mission, FilterMatchesStates) {
    EXPECT_TRUE(filter_matches("all", Attention::kIdle));
    EXPECT_TRUE(filter_matches("active", Attention::kDone));
    EXPECT_TRUE(filter_matches("active", Attention::kWorking));
    EXPECT_TRUE(filter_matches("active", Attention::kBlocked));
    EXPECT_FALSE(filter_matches("active", Attention::kIdle));
    EXPECT_FALSE(filter_matches("active", Attention::kNone));
    EXPECT_TRUE(filter_matches("blocked", Attention::kBlocked));
    EXPECT_FALSE(filter_matches("blocked", Attention::kDone));
    EXPECT_TRUE(filter_matches("working", Attention::kWorking));
    EXPECT_TRUE(filter_matches("done", Attention::kDone));
    EXPECT_EQ(filter_name("active"), "active");
    EXPECT_EQ(filter_name("ACTIVE"), "all");
    EXPECT_EQ(filter_name(""), "all");
}

// The last lines that say something, top to bottom: blank rows and box-drawing-only rows skipped,
// trailing blanks cut, colours split into runs.
TEST(Mission, ScreenTailSkipsBlankAndFrameRows) {
    auto s = screen(8, 20);
    put(s, 0, 0, U"first");
    put(s, 2, 0, U"Edit token.ts");
    put(s, 3, 0, U"Allow? ");
    put(s, 3, 7, U"(y/n)", 1);
    put(s, 5, 0, U"╭───╮");  // the input box's frame
    put(s, 6, 0, U"│ > │");  // a framed line with text
    put(s, 7, 0, U"╰───╯");
    auto t = screen_tail(s, 3);
    ASSERT_EQ(t.size(), 3u);
    EXPECT_EQ(text_of(t[0]), "Edit token.ts");
    EXPECT_EQ(text_of(t[1]), "Allow? (y/n)");
    EXPECT_EQ(text_of(t[2]), "\xE2\x94\x82 > \xE2\x94\x82");
    EXPECT_EQ(t[2].row, 6);
    ASSERT_EQ(t[1].segments.size(), 2u);
    EXPECT_TRUE(t[1].segments[0].default_fg);
    EXPECT_EQ(t[1].segments[1].ansi, 1);
    EXPECT_EQ(t[1].segments[1].col, 7);
    EXPECT_EQ(t[1].segments[1].rgb, 0xCD3131u);
    // More lines asked than there are: all of them.
    EXPECT_EQ(screen_tail(s, 12).size(), 4u);
    EXPECT_TRUE(screen_tail(screen(4, 10), 5).empty());
    EXPECT_TRUE(screen_tail(s, 0).empty());
}

// A wide character is a run of its own, two columns wide, and the next column starts after it.
TEST(Mission, ScreenTailKeepsWideCharactersInTheirColumns) {
    auto s = screen(2, 12);
    put(s, 1, 0, U"ab中文cd");
    const auto t = screen_tail(s, 2);
    ASSERT_EQ(t.size(), 1u);
    const auto& segs = t[0].segments;
    ASSERT_EQ(segs.size(), 4u);
    EXPECT_EQ(segs[0].text, "ab");
    EXPECT_EQ(segs[1].text, "\xE4\xB8\xAD");
    EXPECT_TRUE(segs[1].wide);
    EXPECT_EQ(segs[1].col, 2);
    EXPECT_EQ(segs[1].cols, 2);
    EXPECT_EQ(segs[2].col, 4);
    EXPECT_EQ(segs[3].text, "cd");
    EXPECT_EQ(segs[3].col, 6);
}

// The alternate screen is read like the main one (only what is on screen).
TEST(Mission, ScreenTailReadsTheAlternateScreen) {
    auto s = screen(3, 10);
    s.alt_screen = true;
    put(s, 0, 0, U"top");
    put(s, 1, 0, U"menu");
    const auto t = screen_tail(s, 1);
    ASSERT_EQ(t.size(), 1u);
    EXPECT_EQ(text_of(t[0]), "menu");
}

TEST(Mission, TailLineBudget) {
    EXPECT_EQ(tail_line_budget(150, 15), 10);
    EXPECT_EQ(tail_line_budget(400, 15), 12);
    EXPECT_EQ(tail_line_budget(31, 15), 2);
    EXPECT_EQ(tail_line_budget(10, 15), 0);
    EXPECT_EQ(tail_line_budget(100, 0), 0);
}

// Arrows move on the grid; down from above a short last row lands on its last tile.
TEST(Mission, MoveSelectionOnTheGrid) {
    // 3 columns, 7 tiles: rows [0 1 2] [3 4 5] [6].
    EXPECT_EQ(move_selection(-1, 7, 3, Nav::kRight), 0);
    EXPECT_EQ(move_selection(-1, 7, 3, Nav::kEnd), 6);
    EXPECT_EQ(move_selection(0, 7, 3, Nav::kLeft), 0);
    EXPECT_EQ(move_selection(2, 7, 3, Nav::kRight), 3);
    EXPECT_EQ(move_selection(6, 7, 3, Nav::kRight), 6);
    EXPECT_EQ(move_selection(1, 7, 3, Nav::kDown), 4);
    EXPECT_EQ(move_selection(4, 7, 3, Nav::kDown), 6);
    EXPECT_EQ(move_selection(6, 7, 3, Nav::kDown), 6);
    EXPECT_EQ(move_selection(4, 7, 3, Nav::kUp), 1);
    EXPECT_EQ(move_selection(1, 7, 3, Nav::kUp), 1);
    EXPECT_EQ(move_selection(5, 7, 3, Nav::kHome), 0);
    EXPECT_EQ(move_selection(0, 0, 3, Nav::kDown), -1);
}

// One line; no control characters or invisible and direction marks.
TEST(Mission, CleanMessageMakesOneSafeLine) {
    EXPECT_EQ(clean_message("Allow edit\nof token.ts?"), "Allow edit of token.ts?");
    EXPECT_EQ(clean_message("  a\t\tb  "), "a b");
    EXPECT_EQ(clean_message("evil\xE2\x80\xAEtxt.exe"), "eviltxt.exe");  // U+202E
    EXPECT_EQ(clean_message("zero\xE2\x80\x8Bwidth\xEF\xBB\xBF"), "zerowidth");
    EXPECT_EQ(clean_message("bell\x07!"), "bell!");
    EXPECT_EQ(clean_message("\xE4\xB8\xAD\xE6\x96\x87"), "\xE4\xB8\xAD\xE6\x96\x87");
    EXPECT_EQ(clean_message("bad\xFF"
                            "byte"),
              "bad\xEF\xBF\xBD"
              "byte");
    EXPECT_EQ(clean_message(""), "");
}
