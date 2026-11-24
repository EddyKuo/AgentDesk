// Tests for the cell sprites: the character tables, and the geometry TerminalView hands to ImGui
// (checked as shapes, since nothing is rendered here).
// Portions adapted from herdr-app (Apache-2.0) and modified; see THIRD_PARTY_NOTICES.md.

#include "agentdesk/ui/sprites.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace sprites = agentdesk::ui::sprites;

namespace {

using Lines = std::array<std::uint8_t, 4>;

std::optional<Lines> L(std::uint8_t u, std::uint8_t r, std::uint8_t d, std::uint8_t l) {
    return Lines{u, r, d, l};
}

}  // namespace

TEST(Sprites, BraillePatternsUseAllEightBitsAndStayInTheCell) {
    EXPECT_FALSE(sprites::is_sprite(U'⟿'));
    EXPECT_FALSE(sprites::is_sprite(U'⤀'));
    EXPECT_FALSE(sprites::is_sprite(std::u32string_view(U"⠁⠂")));
    EXPECT_FALSE(sprites::is_sprite(std::u32string_view(U"⠁️")));
    EXPECT_EQ(sprites::braille(U'A', 8, 22), std::nullopt);
    const std::array<std::array<int, 2>, 8> positions = {
        {{1, 2}, {1, 7}, {1, 13}, {5, 2}, {5, 7}, {5, 13}, {1, 18}, {5, 18}}};
    for (std::uint32_t pattern = 0; pattern < 256; ++pattern) {
        const auto c = static_cast<char32_t>(0x2800 + pattern);
        EXPECT_TRUE(sprites::is_sprite(std::u32string(1, c)));
        std::vector<std::array<int, 4>> expected;
        for (std::size_t bit = 0; bit < positions.size(); ++bit) {
            if ((pattern & (1u << bit)) != 0) {
                const auto [x, y] = positions[bit];
                expected.push_back({x, y, x + 2, y + 2});
            }
        }
        EXPECT_EQ(sprites::braille(c, 8, 22).value(), expected) << pattern;
    }
    for (int w = 0; w < 80; ++w) {
        for (int h = 0; h < 160; ++h) {
            const auto rects = sprites::braille(U'⣿', w, h).value();
            for (const auto& [x0, y0, x1, y1] : rects) {
                ASSERT_TRUE(0 <= x0 && x0 <= x1 && x1 <= w) << w << "x" << h;
                ASSERT_TRUE(0 <= y0 && y0 <= y1 && y1 <= h) << w << "x" << h;
                ASSERT_EQ(x1 - x0, y1 - y0) << w << "x" << h;
            }
            const auto dots = sprites::braille(U'⡇', w, h).value();
            const std::array<int, 4> gaps = {dots[1][1] - dots[0][1], dots[2][1] - dots[1][1],
                                             dots[3][1] - dots[2][1], h + dots[0][1] - dots[3][1]};
            const auto [lo, hi] = std::minmax_element(gaps.begin(), gaps.end());
            ASSERT_LE(*hi - *lo, 1) << w << "x" << h;
        }
    }
}

// The dots land on the device pixels braille() gives for the cell's snapped device bounds, at a 2x scale.
TEST(Sprites, BrailleMatchesDevicePixelLayout) {
    const float scale = 2.0f;
    const auto snap = [scale](float v) { return static_cast<int>(std::round(v * scale)); };
    const std::u32string chars = U"⠀⠁⠂⠄⠈⠐⠠⡀⢀⣿";
    for (const auto [w, h] :
         std::array<std::array<float, 2>, 4>{{{4.2f, 11.f}, {5.04f, 13.2f}, {8.4f, 22.f}, {10.08f, 26.4f}}}) {
        for (std::size_t col = 0; col < chars.size(); ++col) {
            const float cx = 7.3f + static_cast<float>(col) * w;
            const int x = snap(cx);
            const int y = snap(5.7f);
            const int dw = snap(7.3f + static_cast<float>(col + 1) * w) - x;
            const int dh = snap(5.7f + h) - y;
            const auto dots = sprites::braille(chars[col], dw, dh).value();
            const auto shapes = sprites::paint(chars[col], cx, 5.7f, w, h, scale);
            ASSERT_TRUE(shapes.paths.empty());
            std::vector<std::array<int, 4>> painted;
            for (const auto& r : shapes.rects)
                painted.push_back({snap(r.x0) - x, snap(r.y0) - y, snap(r.x1) - x, snap(r.y1) - y});
            std::vector<std::array<int, 4>> expected;
            for (const auto& d : dots)
                if (d[2] > d[0] && d[3] > d[1])
                    expected.push_back(d);
            EXPECT_EQ(painted, expected) << static_cast<std::uint32_t>(chars[col]) << " " << w << "x" << h;
        }
    }
}

TEST(Sprites, TablesCoverTheirRanges) {
    EXPECT_EQ(sprites::lines(U'─'), L(0, 1, 0, 1));
    EXPECT_EQ(sprites::lines(U'┼'), L(1, 1, 1, 1));
    EXPECT_EQ(sprites::lines(U'╋'), L(2, 2, 2, 2));
    EXPECT_EQ(sprites::lines(U'┌'), L(0, 1, 1, 0));
    EXPECT_EQ(sprites::lines(U'┘'), L(1, 0, 0, 1));
    EXPECT_EQ(sprites::lines(U'╏'), L(2, 0, 2, 0));
    EXPECT_EQ(sprites::lines(U'╔'), L(0, 3, 3, 0));
    EXPECT_EQ(sprites::lines(U'╬'), L(3, 3, 3, 3));
    EXPECT_EQ(sprites::lines(U'╭'), L(0, 1, 1, 0));
    EXPECT_EQ(sprites::lines(U'╰'), L(1, 1, 0, 0));
    EXPECT_EQ(sprites::lines(U'╱'), std::nullopt);
    EXPECT_EQ(sprites::lines(U'╿'), L(2, 0, 1, 0));
    for (char32_t c = U'─'; c <= U'╰'; ++c)
        EXPECT_TRUE(sprites::lines(c).has_value()) << static_cast<std::uint32_t>(c) << " has no entry";
    for (char32_t c = U'╴'; c <= U'╿'; ++c)
        EXPECT_TRUE(sprites::lines(c).has_value()) << static_cast<std::uint32_t>(c) << " has no entry";
    for (char32_t c = U'▀'; c <= U'▟'; ++c)
        EXPECT_TRUE(sprites::blocks(c).has_value()) << static_cast<std::uint32_t>(c) << " has no entry";
}

// Each arc endpoint meets the neighboring solid arm at the same center and thickness, at a 2x
// scale, half-sized cells included.
TEST(Sprites, RoundedCornersJoinStraightLines) {
    const float scale = 2.0f;
    const std::array<std::u32string, 3> rows = {U"╭─╮", U"│ │", U"╰─╯"};
    for (const auto [w, h] : std::array<std::array<float, 2>, 5>{
             {{4.2f, 11.f}, {5.46f, 14.3f}, {8.4f, 22.f}, {10.92f, 28.6f}, {16.f, 32.f}}}) {
        const auto at = [&](std::size_t row, std::size_t col) {
            return sprites::paint(rows[row][col], 7.3f + static_cast<float>(col) * w,
                                  5.7f + static_cast<float>(row) * h, w, h, scale);
        };
        // Horizontal joins: ─ in the middle column against the corners on its sides.
        for (const std::size_t row : {0u, 2u}) {
            const auto line = at(row, 1);
            ASSERT_EQ(line.rects.size(), 2u);
            const auto& arm = line.rects[0];
            const float center = (arm.y0 + arm.y1) / 2.0f;
            const float thickness = arm.y1 - arm.y0;
            for (const std::size_t col : {0u, 2u}) {
                const auto corner = at(row, col);
                ASSERT_EQ(corner.paths.size(), 1u);
                const auto& path = corner.paths[0];
                const auto& end = path.steps.back().to;  // line_to(ex, cy)
                EXPECT_FLOAT_EQ(end.y, center) << w << "x" << h << " row " << row << " col " << col;
                EXPECT_FLOAT_EQ(path.thickness, thickness) << w << "x" << h;
                const float edge = col == 0 ? std::round((7.3f + w) * scale) / scale
                                            : std::round((7.3f + 2 * w) * scale) / scale;
                EXPECT_FLOAT_EQ(end.x, edge) << w << "x" << h << " row " << row << " col " << col;
            }
        }
        // Vertical joins: │ in the middle row against the corners above and below.
        for (const std::size_t col : {0u, 2u}) {
            const auto line = at(1, col);
            ASSERT_EQ(line.rects.size(), 2u);
            const auto& arm = line.rects[0];
            const float center = (arm.x0 + arm.x1) / 2.0f;
            const float thickness = arm.x1 - arm.x0;
            for (const std::size_t row : {0u, 2u}) {
                const auto corner = at(row, col);
                const auto& path = corner.paths[0];
                const auto& start = path.steps.front().to;  // move_to(cx, ey)
                EXPECT_FLOAT_EQ(start.x, center) << w << "x" << h << " row " << row << " col " << col;
                EXPECT_FLOAT_EQ(path.thickness, thickness) << w << "x" << h;
                const float edge = row == 0 ? std::round((5.7f + h) * scale) / scale
                                            : std::round((5.7f + 2 * h) * scale) / scale;
                EXPECT_FLOAT_EQ(start.y, edge) << w << "x" << h << " row " << row << " col " << col;
            }
        }
    }
}

TEST(Sprites, RoundedCornersAndDashesHaveTheirOwnShapes) {
    for (const char32_t c : std::u32string_view(U"╭╮╯╰"))
        EXPECT_TRUE(sprites::arc(c).has_value() && sprites::is_sprite(c)) << static_cast<std::uint32_t>(c);
    for (const char32_t c : std::u32string_view(U"┄┅┆┇┈┉┊┋╌╍╎╏"))
        EXPECT_TRUE(sprites::dashes(c).has_value() && sprites::is_sprite(c)) << static_cast<std::uint32_t>(c);
    EXPECT_EQ(sprites::arc(U'┌'), std::nullopt);
    EXPECT_EQ(sprites::dashes(U'─'), std::nullopt);
}

// The user's report: block rows (Claude Code's logo ▐▛███▜▌) and box lines fill their cell to
// the snapped edges, so rows and columns meet without seams.
TEST(Sprites, BlocksAndLinesReachTheCellEdges) {
    for (const auto [w, h] :
         std::array<std::array<float, 2>, 3>{{{9.f, 19.f}, {8.4f, 21.5f}, {12.f, 26.f}}}) {
        for (const float scale : {1.0f, 1.5f, 2.0f}) {
            const auto snap = [scale](float v) { return std::round(v * scale) / scale; };
            for (int row = 0; row < 3; ++row) {
                const float y = 3.3f + static_cast<float>(row) * h;
                const auto full = sprites::paint(U'█', 2.6f, y, w, h, scale);
                ASSERT_EQ(full.rects.size(), 1u);
                EXPECT_FLOAT_EQ(full.rects[0].y0, snap(y));
                EXPECT_FLOAT_EQ(full.rects[0].y1, snap(y + h));  // == next row's y0
                EXPECT_FLOAT_EQ(full.rects[0].x0, snap(2.6f));
                EXPECT_FLOAT_EQ(full.rects[0].x1, snap(2.6f + w));
                const auto vert = sprites::paint(U'│', 2.6f, y, w, h, scale);
                float top = 1e9f;
                float bottom = -1e9f;
                for (const auto& r : vert.rects) {
                    top = std::min(top, r.y0);
                    bottom = std::max(bottom, r.y1);
                }
                EXPECT_FLOAT_EQ(top, snap(y));
                EXPECT_FLOAT_EQ(bottom, snap(y + h));
            }
            const auto shade = sprites::paint(U'▒', 0, 0, w, h, scale);
            ASSERT_EQ(shade.rects.size(), 1u);
            EXPECT_FLOAT_EQ(shade.rects[0].alpha, 0.5f);
        }
    }
    EXPECT_TRUE(sprites::is_sprite(sprites::kRecord));
    EXPECT_TRUE(sprites::is_sprite(U''));
    EXPECT_FALSE(sprites::is_sprite(U'A'));
}
