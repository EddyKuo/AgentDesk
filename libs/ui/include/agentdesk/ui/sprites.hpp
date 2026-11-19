#pragma once

// Box drawing, block and braille characters drawn as shapes that fit their cell exactly, as
// Ghostty does. Font glyphs for these rarely span the whole cell, so tables get gaps between
// rows and block art gets seams.
//
// The geometry is pure (no ImGui): `paint` returns rectangles and paths in pixel space for one
// cell, and TerminalView hands them to an ImDrawList.
// Portions adapted from herdr-app (Apache-2.0) and modified; see THIRD_PARTY_NOTICES.md.

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace agentdesk::ui::sprites {

// One rectangle to fill: corners, the alpha to multiply the cell colour by, and a corner radius
// (only the ⏺ disc rounds its corners).
struct Rect {
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    float alpha = 1.0f;
    float rounding = 0.0f;
};

struct Point {
    float x = 0, y = 0;
};

// One path step: move to, line to, or a cubic Bézier to `to` with controls c1 then c2.
struct PathStep {
    enum class Kind { kMove, kLine, kCubic } kind = Kind::kLine;
    Point to;
    Point c1;  // kCubic only
    Point c2;  // kCubic only
};

// A path filled (convex) or stroked with `thickness`; `closed` joins the last point to the first.
struct Path {
    std::vector<PathStep> steps;
    bool fill = false;
    bool closed = false;
    float thickness = 1.0f;
};

struct Shapes {
    std::vector<Rect> rects;
    std::vector<Path> paths;
};

// Per direction (up, right, down, left): 0 none, 1 light, 2 heavy, 3 double.
std::optional<std::array<std::uint8_t, 4>> lines(char32_t c);

// Rounded corners (╭╮╯╰): which way their two arms leave the center, (right or left, down or
// up).
std::optional<std::array<float, 2>> arc(char32_t c);

// Dashed lines: (dashes per cell, heavy, horizontal).
struct Dashes {
    std::uint8_t count = 0;
    bool heavy = false;
    bool horizontal = false;
    friend bool operator==(const Dashes&, const Dashes&) = default;
};
std::optional<Dashes> dashes(char32_t c);

// Rectangles in cell fractions (x0, y0, x1, y1) and an alpha, for block elements.
struct Blocks {
    std::vector<std::array<float, 4>> rects;
    float alpha = 1.0f;
};
std::optional<Blocks> blocks(char32_t c);

// Braille dots in whole pixels (x0, y0, x1, y1) for a width × height cell.
std::optional<std::vector<std::array<int, 4>>> braille(char32_t c, int width, int height);

// ⏺ (Claude Code marks every step with it).
inline constexpr char32_t kRecord = U'⏺';

// True when the cell's text is exactly one sprite character.
bool is_sprite(char32_t c);
bool is_sprite(std::u32string_view text);

// Shapes for `c` (one sprite character) in the cell at (x, y) sized w × h, snapped to device
// pixels at `scale` device pixels per unit. Empty when `c` is not a sprite.
Shapes paint(char32_t c, float x, float y, float w, float h, float scale);

}  // namespace agentdesk::ui::sprites
