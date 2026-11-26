// line_height_per_em: how a font size in em becomes the size ImGui wants (ascent minus descent).
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <vector>

#include <gtest/gtest.h>

#include "agentdesk/ui/fonts.hpp"

using agentdesk::ui::line_height_per_em;

namespace {

void put(std::vector<unsigned char>& b, std::size_t at, std::uint32_t v, int bytes) {
    if (b.size() < at + static_cast<std::size_t>(bytes))
        b.resize(at + static_cast<std::size_t>(bytes));
    for (int i = bytes - 1; i >= 0; --i, v >>= 8)
        b[at + static_cast<std::size_t>(i)] = static_cast<unsigned char>(v & 0xFFu);
}

struct Metrics {
    int upem = 1000;
    bool hhea = true;
    int hhea_ascent = 0, hhea_descent = 0;
    int typo_ascent = 0, typo_descent = 0;
    int win_ascent = 0, win_descent = 0;
};

// A face with only the tables that matter: head, hhea (optional) and OS/2, from `base` on.
void face(std::vector<unsigned char>& b, std::size_t base, const Metrics& m) {
    const int tables = m.hhea ? 3 : 2;
    put(b, base, 0x00010000u, 4);
    put(b, base + 4, static_cast<std::uint32_t>(tables), 2);
    std::size_t data = base + 12 + 16 * static_cast<std::size_t>(tables);
    int rec = 0;
    const auto table = [&](std::uint32_t tag, std::size_t len) {
        const std::size_t r = base + 12 + 16 * static_cast<std::size_t>(rec++);
        put(b, r, tag, 4);
        put(b, r + 8, static_cast<std::uint32_t>(data), 4);
        put(b, r + 12, static_cast<std::uint32_t>(len), 4);
        const std::size_t at = data;
        data += len;
        put(b, data - 1, 0, 1);
        return at;
    };
    const std::size_t head = table(0x68656164u, 54);
    put(b, head + 18, static_cast<std::uint32_t>(m.upem), 2);
    if (m.hhea) {
        const std::size_t hhea = table(0x68686561u, 36);
        put(b, hhea + 4, static_cast<std::uint16_t>(m.hhea_ascent), 2);
        put(b, hhea + 6, static_cast<std::uint16_t>(m.hhea_descent), 2);
    }
    const std::size_t os2 = table(0x4F532F32u, 96);
    put(b, os2 + 68, static_cast<std::uint16_t>(m.typo_ascent), 2);
    put(b, os2 + 70, static_cast<std::uint16_t>(m.typo_descent), 2);
    put(b, os2 + 74, static_cast<std::uint32_t>(m.win_ascent), 2);
    put(b, os2 + 76, static_cast<std::uint32_t>(m.win_descent), 2);
}

float ratio(const Metrics& m) {
    std::vector<unsigned char> b;
    face(b, 0, m);
    return line_height_per_em(b.data(), b.size());
}

}  // namespace

TEST(FontMetrics, HheaGivesTheHeightPerEm) {
    EXPECT_FLOAT_EQ(ratio({.upem = 1000, .hhea_ascent = 1020, .hhea_descent = -300}), 1.32f);
    EXPECT_FLOAT_EQ(ratio({.upem = 2048, .hhea_ascent = 1638, .hhea_descent = -410}), 1.0f);
}

TEST(FontMetrics, EmptyHheaFallsBackToTypoThenWinMetrics) {
    EXPECT_FLOAT_EQ(
        ratio(
            {.upem = 1000, .typo_ascent = 800, .typo_descent = -200, .win_ascent = 1100, .win_descent = 400}),
        1.0f);
    EXPECT_FLOAT_EQ(ratio({.upem = 1000, .win_ascent = 1100, .win_descent = 400}), 1.5f);
    EXPECT_FLOAT_EQ(ratio({.upem = 1000, .hhea = false, .typo_ascent = 900, .typo_descent = -300}), 1.2f);
}

TEST(FontMetrics, CollectionsPickTheFace) {
    std::vector<unsigned char> b;
    put(b, 0, 0x74746366u, 4);  // 'ttcf'
    put(b, 4, 0x00010000u, 4);
    put(b, 8, 2, 4);
    put(b, 12, 64, 4);
    put(b, 16, 512, 4);
    face(b, 64, {.upem = 1000, .hhea_ascent = 1000, .hhea_descent = -250});
    face(b, 512, {.upem = 1000, .hhea_ascent = 900, .hhea_descent = -500});
    EXPECT_FLOAT_EQ(line_height_per_em(b.data(), b.size(), 0), 1.25f);
    EXPECT_FLOAT_EQ(line_height_per_em(b.data(), b.size(), 1), 1.4f);
    EXPECT_EQ(line_height_per_em(b.data(), b.size(), 2), 0.0f);
}

TEST(FontMetrics, WhatIsNotAFontGivesZero) {
    const unsigned char junk[] = "not a font at all, just text";
    EXPECT_EQ(line_height_per_em(junk, sizeof junk), 0.0f);
    EXPECT_EQ(line_height_per_em(nullptr, 0), 0.0f);
    EXPECT_EQ(ratio({.upem = 0, .hhea_ascent = 1000, .hhea_descent = -300}), 0.0f);
    EXPECT_EQ(ratio({.upem = 1000}), 0.0f);
    EXPECT_EQ(line_height_per_em(static_cast<const ImFont*>(nullptr)), 1.0f);
}

// The bundled terminal font: 14 em becomes an ImGui size of 18.48 (the cell height herdr draws).
TEST(FontMetrics, BundledJetBrainsMono) {
    std::ifstream in(AGENTDESK_BUNDLED_FONTS_DIR "/JetBrainsMono-Medium.ttf", std::ios::binary);
    ASSERT_TRUE(in);
    const std::vector<unsigned char> b{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    EXPECT_FLOAT_EQ(line_height_per_em(b.data(), b.size()), 1.32f);
}
