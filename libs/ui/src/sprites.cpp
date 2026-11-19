// Box drawing, block, braille and powerline characters as exact-fit shapes for one cell (see
// sprites.hpp), following Ghostty's sprite geometry.
// Portions adapted from herdr-app (Apache-2.0) and modified; see THIRD_PARTY_NOTICES.md.

#include "agentdesk/ui/sprites.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace agentdesk::ui::sprites {

namespace {

// The n-th space-separated word of `table`.
std::optional<std::string_view> nth_word(std::string_view table, std::size_t n) {
    std::size_t i = 0;
    for (;;) {
        while (i < table.size() && table[i] == ' ')
            ++i;
        if (i >= table.size())
            return std::nullopt;
        std::size_t end = i;
        while (end < table.size() && table[end] != ' ')
            ++end;
        if (n == 0)
            return table.substr(i, end - i);
        --n;
        i = end;
    }
}

bool in_braille(char32_t c) {
    return c >= U'⠀' && c <= U'⣿';
}

bool in_powerline(char32_t c) {
    return c >= U'' && c <= U'';
}

// A path being built: filled, or stroked at a width.
struct Builder {
    Path path;
    void move_to(Point p) { path.steps.push_back({PathStep::Kind::kMove, p, {}, {}}); }
    void line_to(Point p) { path.steps.push_back({PathStep::Kind::kLine, p, {}, {}}); }
    // Control points a then b.
    void cubic_bezier_to(Point to, Point a, Point b) {
        path.steps.push_back({PathStep::Kind::kCubic, to, a, b});
    }
    void add_polygon(std::initializer_list<Point> points, bool close) {
        bool first = true;
        for (const Point p : points) {
            if (first)
                move_to(p);
            else
                line_to(p);
            first = false;
        }
        if (close)
            path.closed = true;
    }
    void close() { path.closed = true; }
};

Builder fill_builder() {
    Builder b;
    b.path.fill = true;
    return b;
}

Builder stroke_builder(float width) {
    Builder b;
    b.path.fill = false;
    b.path.thickness = width;
    return b;
}

// Nerd Font powerline separators (U+E0B0..E0BF): arrows, half circles and slants, drawn to fill
// the cell exactly as Ghostty does. Fonts that have them rarely match the cell, and fonts that
// don't fall back to whatever font has those codes. Even code points are solid, odd ones outlines.
void powerline(char32_t c, float x, float y, float w, float h, float line, float scale, Shapes& out) {
    const bool solid = (static_cast<std::uint32_t>(c) % 2) == 0;
    // A solid shape reaches a device pixel past its cell's sides, so its flat edge meets the
    // neighbor's color without an anti-aliased seam.
    const float ext = solid ? 1.0f / scale : 0.0f;
    const auto p = [&](float px_, float py_) {
        return Point{x + px_ * w + (px_ - 0.5f) * 2.0f * ext, y + py_ * h};
    };
    Builder path = solid ? fill_builder() : stroke_builder(line);
    // Half an ellipse filling the cell, from quarter arcs as cubics: 0.5522848 is the usual
    // control-point distance for a quarter circle.
    constexpr float K = 0.5522848f;
    switch (c) {
        case U'':
        case U'':
            path.add_polygon({p(0.0f, 0.0f), p(1.0f, 0.5f), p(0.0f, 1.0f)}, solid);
            break;
        case U'':
        case U'':
            path.add_polygon({p(1.0f, 0.0f), p(0.0f, 0.5f), p(1.0f, 1.0f)}, solid);
            break;
        case U'':
        case U'':
            path.move_to(p(0.0f, 0.0f));
            path.cubic_bezier_to(p(1.0f, 0.5f), p(K, 0.0f), p(1.0f, 0.5f - K / 2.0f));
            path.cubic_bezier_to(p(0.0f, 1.0f), p(1.0f, 0.5f + K / 2.0f), p(K, 1.0f));
            if (solid)
                path.close();
            break;
        case U'':
        case U'':
            path.move_to(p(1.0f, 0.0f));
            path.cubic_bezier_to(p(0.0f, 0.5f), p(1.0f - K, 0.0f), p(0.0f, 0.5f - K / 2.0f));
            path.cubic_bezier_to(p(1.0f, 1.0f), p(0.0f, 0.5f + K / 2.0f), p(1.0f - K, 1.0f));
            if (solid)
                path.close();
            break;
        case U'':
            path.add_polygon({p(0.0f, 0.0f), p(1.0f, 1.0f), p(0.0f, 1.0f)}, true);
            break;
        case U'':
            path.add_polygon({p(1.0f, 0.0f), p(1.0f, 1.0f), p(0.0f, 1.0f)}, true);
            break;
        case U'':
            path.add_polygon({p(0.0f, 0.0f), p(1.0f, 0.0f), p(0.0f, 1.0f)}, true);
            break;
        case U'':
            path.add_polygon({p(0.0f, 0.0f), p(1.0f, 0.0f), p(1.0f, 1.0f)}, true);
            break;
        case U'':
        case U'':
            path.add_polygon({p(0.0f, 0.0f), p(1.0f, 1.0f)}, false);
            break;
        default:  // E0BB, E0BD
            path.add_polygon({p(0.0f, 1.0f), p(1.0f, 0.0f)}, false);
            break;
    }
    out.paths.push_back(std::move(path.path));
}

}  // namespace

std::optional<std::array<std::uint8_t, 4>> lines(char32_t c) {
    // One 4-digit word (up, right, down, left) per code point, from the range's first one.
    static constexpr std::string_view kLightHeavy =
        "0101 0202 1010 2020 0101 0202 1010 2020 0101 0202 1010 2020 "
        "0110 0210 0120 0220 0011 0012 0021 0022 1100 1200 2100 2200 "
        "1001 1002 2001 2002 1110 1210 2110 1120 2120 2210 1220 2220 "
        "1011 1012 2011 1021 2021 2012 1022 2022 0111 0112 0211 0212 "
        "0121 0122 0221 0222 1101 1102 1201 1202 2101 2102 2201 2202 "
        "1111 1112 1211 1212 2111 1121 2121 2112 2211 1122 1221 2212 "
        "1222 2122 2221 2222 0101 0202 1010 2020";
    static constexpr std::string_view kDouble =
        "0303 3030 0310 0130 0330 0013 0031 0033 1300 3100 3300 1003 "
        "3001 3003 1310 3130 3330 1013 3031 3033 0313 0131 0333 1303 "
        "3101 3303 1313 3131 3333 0110 0011 1001 1100";
    static constexpr std::string_view kSingles =
        "0001 1000 0100 0010 0002 2000 0200 0020 0201 1020 0102 2010";
    const auto code = static_cast<std::uint32_t>(c);
    std::string_view table;
    std::uint32_t base = 0;
    if (code >= 0x2500 && code <= 0x254F) {
        table = kLightHeavy;
        base = 0x2500;
    } else if (code >= 0x2550 && code <= 0x2570) {
        table = kDouble;
        base = 0x2550;
    } else if (code >= 0x2574 && code <= 0x257F) {
        table = kSingles;
        base = 0x2574;
    } else {
        return std::nullopt;
    }
    const auto word = nth_word(table, code - base);
    if (!word)
        return std::nullopt;
    std::array<std::uint8_t, 4> out{};
    for (std::size_t i = 0; i < out.size() && i < word->size(); ++i)
        out[i] = static_cast<std::uint8_t>((*word)[i] - '0');
    return out;
}

std::optional<std::array<float, 2>> arc(char32_t c) {
    switch (c) {
        case U'╭':
            return std::array<float, 2>{1.0f, 1.0f};
        case U'╮':
            return std::array<float, 2>{-1.0f, 1.0f};
        case U'╯':
            return std::array<float, 2>{-1.0f, -1.0f};
        case U'╰':
            return std::array<float, 2>{1.0f, -1.0f};
        default:
            return std::nullopt;
    }
}

std::optional<Dashes> dashes(char32_t c) {
    switch (c) {
        case U'┄':
            return Dashes{3, false, true};
        case U'┅':
            return Dashes{3, true, true};
        case U'┆':
            return Dashes{3, false, false};
        case U'┇':
            return Dashes{3, true, false};
        case U'┈':
            return Dashes{4, false, true};
        case U'┉':
            return Dashes{4, true, true};
        case U'┊':
            return Dashes{4, false, false};
        case U'┋':
            return Dashes{4, true, false};
        case U'╌':
            return Dashes{2, false, true};
        case U'╍':
            return Dashes{2, true, true};
        case U'╎':
            return Dashes{2, false, false};
        case U'╏':
            return Dashes{2, true, false};
        default:
            return std::nullopt;
    }
}

std::optional<Blocks> blocks(char32_t c) {
    constexpr float e = 1.0f / 8.0f;
    using R = std::array<float, 4>;
    const auto quads = [](bool ul, bool ur, bool ll, bool lr) {
        std::vector<R> v;
        if (ul)
            v.push_back({0.0f, 0.0f, 0.5f, 0.5f});
        if (ur)
            v.push_back({0.5f, 0.0f, 1.0f, 0.5f});
        if (ll)
            v.push_back({0.0f, 0.5f, 0.5f, 1.0f});
        if (lr)
            v.push_back({0.5f, 0.5f, 1.0f, 1.0f});
        return v;
    };
    const auto one = [](float x0, float y0, float x1, float y1, float alpha) {
        return Blocks{{R{x0, y0, x1, y1}}, alpha};
    };
    if (c == U'▀')
        return one(0.0f, 0.0f, 1.0f, 0.5f, 1.0f);
    if (c >= U'▁' && c <= U'█') {  // ▁..█: n eighths from the bottom
        const auto n = static_cast<float>(c - U'▁' + 1);
        return one(0.0f, 1.0f - n * e, 1.0f, 1.0f, 1.0f);
    }
    if (c >= U'▉' && c <= U'▏') {  // ▉..▏: n eighths from the left
        const auto n = static_cast<float>(8 - (c - U'▉' + 1));
        return one(0.0f, 0.0f, n * e, 1.0f, 1.0f);
    }
    switch (c) {
        case U'▐':
            return one(0.5f, 0.0f, 1.0f, 1.0f, 1.0f);
        case U'░':
            return one(0.0f, 0.0f, 1.0f, 1.0f, 0.25f);
        case U'▒':
            return one(0.0f, 0.0f, 1.0f, 1.0f, 0.5f);
        case U'▓':
            return one(0.0f, 0.0f, 1.0f, 1.0f, 0.75f);
        case U'▔':
            return one(0.0f, 0.0f, 1.0f, e, 1.0f);
        case U'▕':
            return one(1.0f - e, 0.0f, 1.0f, 1.0f, 1.0f);
        case U'▖':
            return Blocks{quads(false, false, true, false), 1.0f};
        case U'▗':
            return Blocks{quads(false, false, false, true), 1.0f};
        case U'▘':
            return Blocks{quads(true, false, false, false), 1.0f};
        case U'▙':
            return Blocks{quads(true, false, true, true), 1.0f};
        case U'▚':
            return Blocks{quads(true, false, false, true), 1.0f};
        case U'▛':
            return Blocks{quads(true, true, true, false), 1.0f};
        case U'▜':
            return Blocks{quads(true, true, false, true), 1.0f};
        case U'▝':
            return Blocks{quads(false, true, false, false), 1.0f};
        case U'▞':
            return Blocks{quads(false, true, true, false), 1.0f};
        case U'▟':
            return Blocks{quads(false, true, true, true), 1.0f};
        default:
            return std::nullopt;
    }
}

// Ghostty's dot sizing, with vertical spacing even across adjacent cells. Bit order is 1,2,3,7
// down the left, 4,5,6,8 down the right.
std::optional<std::vector<std::array<int, 4>>> braille(char32_t c, int width, int height) {
    if (!in_braille(c))
        return std::nullopt;
    int dot = std::min(width / 4, height / 8);
    int xs = width / 4;
    const int ys = height / 8;
    int xm = xs / 2;
    const int ym = ys / 2;
    int xr = width - 2 * xm - xs - 2 * dot;
    int yr = height - 2 * ym - 3 * ys - 4 * dot;
    if (xr >= 2 && yr >= 4 && dot == 0) {
        dot += 1;
        xr -= 2;
        yr -= 4;
    }
    if (xr >= 2 && xm == 0) {
        xm = 1;
        xr -= 2;
    }
    if (yr >= 2 && ym == 0)
        yr -= 2;
    if (xr >= 1) {
        xs += 1;
        xr -= 1;
    }
    if (yr >= 3)
        yr -= 3;
    if (xr >= 2) {
        xm += 1;
        xr -= 2;
    }
    if (yr >= 2)
        yr -= 2;
    if (xr >= 2 && yr >= 4)
        dot += 1;
    std::vector<std::array<int, 4>> rects;
    static constexpr std::array<std::array<int, 3>, 8> kDots = {{
        {0, 0, 0},
        {1, 0, 1},
        {2, 0, 2},
        {3, 1, 0},
        {4, 1, 1},
        {5, 1, 2},
        {6, 0, 3},
        {7, 1, 3},
    }};
    for (const auto& [bit, col, row] : kDots) {
        if ((static_cast<std::uint32_t>(c) & (1u << bit)) != 0) {
            const int x = xm + col * (dot + xs);
            const int y = ((2 * row + 1) * height - 4 * dot + 4) / 8;
            rects.push_back({x, y, x + dot, y + dot});
        }
    }
    return rects;
}

bool is_sprite(char32_t c) {
    return lines(c).has_value() || blocks(c).has_value() || in_braille(c) || in_powerline(c) || c == kRecord;
}

bool is_sprite(std::u32string_view text) {
    return text.size() == 1 && is_sprite(text[0]);
}

namespace {

// One cell being painted: its box in units and the device pixels per unit.
struct Cell {
    float x, y, w, h, scale;
    Shapes& out;

    // v rounded to a whole device pixel.
    [[nodiscard]] float snap(float v) const { return std::round(v * scale) / scale; }

    // A filled rectangle with snapped corners; nothing when it snaps to zero width or height.
    void rect(float x0, float y0, float x1, float y1, float alpha) const {
        x0 = snap(x0);
        y0 = snap(y0);
        x1 = snap(x1);
        y1 = snap(y1);
        if (x1 > x0 && y1 > y0)
            out.rects.push_back({x0, y0, x1, y1, alpha, 0.0f});
    }
};

// Braille dots are sized in whole device pixels for the cell as it lands on the screen.
bool paint_braille(char32_t c, const Cell& cell) {
    const float dx = std::round(cell.x * cell.scale);
    const float dy = std::round(cell.y * cell.scale);
    const float dw = std::round((cell.x + cell.w) * cell.scale) - dx;
    const float dh = std::round((cell.y + cell.h) * cell.scale) - dy;
    const auto dots = braille(c, static_cast<int>(dw), static_cast<int>(dh));
    if (!dots)
        return false;
    const float s = cell.scale;
    for (const auto& [x0, y0, x1, y1] : *dots)
        cell.rect((dx + static_cast<float>(x0)) / s, (dy + static_cast<float>(y0)) / s,
                  (dx + static_cast<float>(x1)) / s, (dy + static_cast<float>(y1)) / s, 1.0f);
    return true;
}

// ⏺: a centered disc, 62% of the width or half the height, whichever is smaller.
void paint_record(const Cell& cell) {
    const float d = cell.snap(std::min(cell.w * 0.62f, cell.h * 0.5f));
    const float x0 = cell.snap(cell.x + (cell.w - d) / 2.0f);
    const float y0 = cell.snap(cell.y + (cell.h - d) / 2.0f);
    cell.out.rects.push_back({x0, y0, x0 + d, y0 + d, 1.0f, d / 2.0f});
}

// A rounded corner: two arms from the center toward the cell's edges, joined by a short curve.
void paint_arc(std::array<float, 2> a, const Cell& cell, float light, float cx, float cy) {
    const float sx = a[0];
    const float sy = a[1];
    // Like Ghostty, center the stroke between its pixel-aligned edges. The solid arms snap
    // both edges, not the path center.
    const float x0 = cell.snap(cx - light / 2.0f);
    const float x1 = cell.snap(cx + light / 2.0f);
    const float y0 = cell.snap(cy - light / 2.0f);
    const float y1 = cell.snap(cy + light / 2.0f);
    const float acx = (x0 + x1) / 2.0f;
    const float acy = (y0 + y1) / 2.0f;
    const float ex = cell.snap(sx > 0.0f ? cell.x + cell.w : cell.x);
    const float ey = cell.snap(sy > 0.0f ? cell.y + cell.h : cell.y);
    // Keep the turn inside the snapped cell, without doubling back.
    const float r = std::min(std::abs(ex - acx), std::abs(ey - acy));
    const float k = 0.25f;
    Builder path = stroke_builder(x1 - x0);
    path.move_to({acx, ey});
    path.line_to({acx, acy + sy * r});
    path.cubic_bezier_to({acx + sx * r, acy}, {acx, acy + sy * k * r}, {acx + sx * k * r, acy});
    path.line_to({ex, acy});
    cell.out.paths.push_back(std::move(path.path));
}

void paint_dashes(const Dashes& d, const Cell& cell, float light, float cx, float cy) {
    // Gaps are at most half the cell. A horizontal line keeps half a gap at each end and a
    // vertical one its gap below, so neighbors tile evenly.
    const float n = static_cast<float>(d.count);
    const float t = d.heavy ? light * 2.0f : light;
    const float len = d.horizontal ? cell.w : cell.h;
    const float from = d.horizontal ? cell.x : cell.y;
    const float gap = std::min(len / (2.0f * n), std::max(light, 4.0f / cell.scale));
    const float dash = (len - gap * n) / n;
    float at = from + (d.horizontal ? gap / 2.0f : 0.0f);
    for (std::uint8_t i = 0; i < d.count; ++i) {
        if (d.horizontal)
            cell.rect(at, cy - t / 2.0f, at + dash, cy + t / 2.0f, 1.0f);
        else
            cell.rect(cx - t / 2.0f, at, cx + t / 2.0f, at + dash, 1.0f);
        at += dash + gap;
    }
}

// Straight box-drawing lines: each arm from the center to its edge, light, heavy (twice as thick)
// or double (two light lines), starting where the crossing arms make the join close.
void paint_lines(const std::array<std::uint8_t, 4>& dirs, const Cell& cell, float light, float cx, float cy) {
    const float x = cell.x;
    const float y = cell.y;
    const float w = cell.w;
    const float h = cell.h;
    const auto thick = [light](std::uint8_t k) { return k == 2 ? light * 2.0f : light; };
    // Where a line along an arm starts, measured from the center toward the arm: `before` and
    // `after` are the crossing arms on this line's side and the other side. Negative reaches past
    // the center so joins close.
    const auto cover = [&](std::uint8_t k) { return k == 3 ? light * 1.5f : thick(k) / 2.0f; };
    const auto start = [&](std::uint8_t before, std::uint8_t after, bool dbl) -> float {
        // A double line stops at a double crossing on its side (inner corner).
        if (before == 3 && dbl)
            return light / 2.0f;
        if (before == 0 && after == 0)
            return 0.0f;
        if (before == 0)
            return -cover(after);
        return -cover(before);
    };
    struct Arm {
        std::size_t i;
        float sign;
        bool horizontal;
    };
    // Arms: (direction, sign along the axis, horizontal?).
    static constexpr std::array<Arm, 4> kArms = {
        {{0, -1.0f, false}, {1, 1.0f, true}, {2, 1.0f, false}, {3, -1.0f, true}}};
    struct Line {
        float off, t;
        std::uint8_t before, after;
    };
    for (const Arm& arm : kArms) {
        const std::uint8_t k = dirs[arm.i];
        if (k == 0)
            continue;
        // The crossing arms on each side of this arm (up/down for a horizontal arm).
        const std::uint8_t side_a = arm.horizontal ? dirs[0] : dirs[3];
        const std::uint8_t side_b = arm.horizontal ? dirs[2] : dirs[1];
        std::vector<Line> arm_lines;
        if (k == 3) {
            arm_lines = {{-light, light, side_a, side_b}, {light, light, side_b, side_a}};
        } else {
            const auto a = side_a >= side_b ? side_a : side_b;
            const auto b = side_a >= side_b ? side_b : side_a;
            arm_lines = {{0.0f, thick(k), a, b}};
        }
        for (const Line& l : arm_lines) {
            const float near = start(l.before, l.after, k == 3);
            const float center = arm.horizontal ? cx : cy;
            const float edge = arm.horizontal ? (arm.sign > 0.0f ? x + w : x) : (arm.sign > 0.0f ? y + h : y);
            const float from = center + arm.sign * near;
            const float a0 = arm.sign > 0.0f ? from : edge;
            const float a1 = arm.sign > 0.0f ? edge : from;
            if (arm.horizontal)
                cell.rect(a0, cy + l.off - l.t / 2.0f, a1, cy + l.off + l.t / 2.0f, 1.0f);
            else
                cell.rect(cx + l.off - l.t / 2.0f, a0, cx + l.off + l.t / 2.0f, a1, 1.0f);
        }
    }
}

}  // namespace

// Tried in order: braille, ⏺, powerline, blocks, arcs, dashes, then straight lines. A light line
// is an eighth of the cell width, at least one device pixel.
Shapes paint(char32_t c, float x, float y, float w, float h, float scale) {
    Shapes out;
    const Cell cell{x, y, w, h, scale, out};
    if (paint_braille(c, cell))
        return out;
    if (c == kRecord) {
        paint_record(cell);
        return out;
    }
    if (in_powerline(c)) {
        const float line = std::max(std::round(w / 8.0f), 1.0f / scale);
        powerline(c, x, y, w, h, line, scale, out);
        return out;
    }
    if (const auto b = blocks(c)) {
        for (const auto& [x0, y0, x1, y1] : b->rects)
            cell.rect(x + x0 * w, y + y0 * h, x + x1 * w, y + y1 * h, b->alpha);
        return out;
    }
    const float light = std::max(std::round(w / 8.0f), 1.0f / scale);
    const float cx = cell.snap(x + w / 2.0f);
    const float cy = cell.snap(y + h / 2.0f);
    if (const auto a = arc(c))
        paint_arc(*a, cell, light, cx, cy);
    else if (const auto d = dashes(c))
        paint_dashes(*d, cell, light, cx, cy);
    else if (const auto dirs = lines(c))
        paint_lines(*dirs, cell, light, cx, cy);
    return out;
}

}  // namespace agentdesk::ui::sprites
