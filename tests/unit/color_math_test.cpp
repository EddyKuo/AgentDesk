// Colour arithmetic: contrast, OKLab, CIEDE2000, colour-vision simulation, the palette check and
// the ambient tint (libs/ui color_math).
#include "agentdesk/ui/color_math.hpp"

#include <cmath>

#include <gtest/gtest.h>

namespace c = agentdesk::ui::color;

TEST(ColorMath, ContrastKnownValues) {
    EXPECT_NEAR(c::contrast(0xffffff, 0x000000), 21.0, 0.005);
    EXPECT_NEAR(c::contrast(0x777777, 0xffffff), 4.48, 0.005);
    EXPECT_NEAR(c::contrast(0x3a7bd5, 0x3a7bd5), 1.0, 1e-12);
    EXPECT_DOUBLE_EQ(c::contrast(0x123456, 0xfedcba), c::contrast(0xfedcba, 0x123456));
}

TEST(ColorMath, MixOverAndBestOn) {
    EXPECT_EQ(c::mix(0x000000, 0xffffff, 0.5), 0x808080u);
    EXPECT_EQ(c::over(0xffffff, 0, 0x102030), 0x102030u);
    EXPECT_EQ(c::over(0xffffff, 255, 0x102030), 0xffffffu);
    EXPECT_EQ(c::best_on(0x0d111b), 0xffffffu);
    EXPECT_EQ(c::best_on(0xfafaf7), 0x000000u);
}

TEST(ColorMath, OkLabRoundTripIsExact) {
    for (const c::Rgb x :
         {0x000000u, 0xffffffu, 0xff0000u, 0x00ff00u, 0x0000ffu, 0x8b7cf6u, 0x0d111bu, 0xe9b949u}) {
        const c::Linear l = c::to_linear(x);
        const c::Linear back = c::oklab_to_linear(c::linear_to_oklab(l));
        EXPECT_NEAR(back.r, l.r, 1e-6) << std::hex << x;
        EXPECT_NEAR(back.g, l.g, 1e-6) << std::hex << x;
        EXPECT_NEAR(back.b, l.b, 1e-6) << std::hex << x;
        EXPECT_EQ(c::from_oklab(c::to_oklab(x)), x);
    }
    // White is L = 1 with no chroma.
    const c::OkLab w = c::to_oklab(0xffffff);
    EXPECT_NEAR(w.L, 1.0, 1e-4);
    EXPECT_NEAR(w.a, 0.0, 1e-4);
    EXPECT_NEAR(w.b, 0.0, 1e-4);
}

TEST(ColorMath, ShiftTowardHueKeepsLightness) {
    const c::OkLab base = c::to_oklab(0x0d111b);
    const c::OkLab s = c::shift_toward_hue(base, c::hue_of(0xf0647a), 0.010);
    EXPECT_DOUBLE_EQ(s.L, base.L);
    EXPECT_NEAR(std::hypot(s.a - base.a, s.b - base.b), 0.010, 1e-12);
}

TEST(ColorMath, HueTurnsTheShortWayAndIsCapped) {
    EXPECT_DOUBLE_EQ(c::hue_delta(350, 10), 20);
    EXPECT_DOUBLE_EQ(c::hue_delta(10, 350), -20);
    EXPECT_DOUBLE_EQ(c::hue_delta(0, 180), 180);
    const c::OkLab from = c::from_oklch({0.6, 0.1, 350});
    const c::OkLch near = c::to_oklch(c::turn_hue_toward(from, 10, 12));
    EXPECT_NEAR(near.h, 2, 1e-9);  // +12, not -12
    EXPECT_NEAR(near.C, 0.1, 1e-12);
    EXPECT_NEAR(near.L, 0.6, 1e-12);
    const c::OkLch reached = c::to_oklch(c::turn_hue_toward(from, 355, 12));
    EXPECT_NEAR(reached.h, 355, 1e-9);
}

// Sharma, Wu and Dalal (2005), table 1: all 34 pairs.
TEST(ColorMath, DeltaE2000SharmaData) {
    struct Row {
        double l1, a1, b1, l2, a2, b2, de;
    };
    const Row rows[] = {
        {50.0000, 2.6772, -79.7751, 50.0000, 0.0000, -82.7485, 2.0425},
        {50.0000, 3.1571, -77.2803, 50.0000, 0.0000, -82.7485, 2.8615},
        {50.0000, 2.8361, -74.0200, 50.0000, 0.0000, -82.7485, 3.4412},
        {50.0000, -1.3802, -84.2814, 50.0000, 0.0000, -82.7485, 1.0000},
        {50.0000, -1.1848, -84.8006, 50.0000, 0.0000, -82.7485, 1.0000},
        {50.0000, -0.9009, -85.5211, 50.0000, 0.0000, -82.7485, 1.0000},
        {50.0000, 0.0000, 0.0000, 50.0000, -1.0000, 2.0000, 2.3669},
        {50.0000, -1.0000, 2.0000, 50.0000, 0.0000, 0.0000, 2.3669},
        {50.0000, 2.4900, -0.0010, 50.0000, -2.4900, 0.0009, 7.1792},
        {50.0000, 2.4900, -0.0010, 50.0000, -2.4900, 0.0010, 7.1792},
        {50.0000, 2.4900, -0.0010, 50.0000, -2.4900, 0.0011, 7.2195},
        {50.0000, 2.4900, -0.0010, 50.0000, -2.4900, 0.0012, 7.2195},
        {50.0000, -0.0010, 2.4900, 50.0000, 0.0009, -2.4900, 4.8045},
        {50.0000, -0.0010, 2.4900, 50.0000, 0.0010, -2.4900, 4.8045},
        {50.0000, -0.0010, 2.4900, 50.0000, 0.0011, -2.4900, 4.7461},
        {50.0000, 2.5000, 0.0000, 50.0000, 0.0000, -2.5000, 4.3065},
        {50.0000, 2.5000, 0.0000, 73.0000, 25.0000, -18.0000, 27.1492},
        {50.0000, 2.5000, 0.0000, 61.0000, -5.0000, 29.0000, 22.8977},
        {50.0000, 2.5000, 0.0000, 56.0000, -27.0000, -3.0000, 31.9030},
        {50.0000, 2.5000, 0.0000, 58.0000, 24.0000, 15.0000, 19.4535},
        {50.0000, 2.5000, 0.0000, 50.0000, 3.1736, 0.5854, 1.0000},
        {50.0000, 2.5000, 0.0000, 50.0000, 3.2972, 0.0000, 1.0000},
        {50.0000, 2.5000, 0.0000, 50.0000, 1.8634, 0.5757, 1.0000},
        {50.0000, 2.5000, 0.0000, 50.0000, 3.2592, 0.3350, 1.0000},
        {60.2574, -34.0099, 36.2677, 60.4626, -34.1751, 39.4387, 1.2644},
        {63.0109, -31.0961, -5.8663, 62.8187, -29.7946, -4.0864, 1.2630},
        {61.2901, 3.7196, -5.3901, 61.4292, 2.2480, -4.9620, 1.8731},
        {35.0831, -44.1164, 3.7933, 35.0232, -40.0716, 1.5901, 1.8645},
        {22.7233, 20.0904, -46.6940, 23.0331, 14.9730, -42.5619, 2.0373},
        {36.4612, 47.8580, 18.3852, 36.2715, 50.5065, 21.2231, 1.4146},
        {90.8027, -2.0831, 1.4410, 91.1528, -1.6435, 0.0447, 1.4441},
        {90.9257, -0.5406, -0.9208, 88.6381, -0.8985, -0.7239, 1.5381},
        {6.7747, -0.2908, -2.4247, 5.8714, -0.0985, -2.2286, 0.6377},
        {2.0776, 0.0795, -1.1350, 0.9033, -0.0636, -0.5514, 0.9082},
    };
    int i = 0;
    for (const Row& r : rows) {
        ++i;
        EXPECT_NEAR(c::delta_e2000(c::Lab{r.l1, r.a1, r.b1}, c::Lab{r.l2, r.a2, r.b2}), r.de, 0.0001)
            << "pair " << i;
        EXPECT_NEAR(c::delta_e2000(c::Lab{r.l2, r.a2, r.b2}, c::Lab{r.l1, r.a1, r.b1}), r.de, 0.0001)
            << "pair " << i;
    }
}

TEST(ColorMath, LabOfWhiteAndBlack) {
    const c::Lab w = c::to_lab(0xffffff);
    EXPECT_NEAR(w.L, 100, 0.01);
    EXPECT_NEAR(w.a, 0, 0.01);
    EXPECT_NEAR(w.b, 0, 0.01);
    EXPECT_NEAR(c::to_lab(0x000000).L, 0, 1e-9);
}

TEST(ColorMath, SimulationMatchesMachado) {
    const c::Rgb d = c::simulate(0xff7a6b, c::Cvd::kDeutan);
    EXPECT_NEAR(c::red(d), 0xbc, 1);
    EXPECT_NEAR(c::green(d), 0xad, 1);
    EXPECT_NEAR(c::blue(d), 0x68, 1);
    for (const c::Rgb g : {0x000000u, 0x777777u, 0xffffffu})
        for (const c::Cvd v : {c::Cvd::kProtan, c::Cvd::kDeutan, c::Cvd::kTritan})
            EXPECT_EQ(c::simulate(g, v), g);
    EXPECT_EQ(c::simulate(0x123456, c::Cvd::kNone), 0x123456u);
}

namespace {
// Midnight, AgentDesk's default dark palette.
c::PaletteColors midnight() {
    c::PaletteColors p;
    p.canvas = 0x0d111b;
    p.chrome = 0x101521;
    p.surface = 0x121724;
    p.selected = 0x232b42;
    p.foreground = 0xe2e6f2;
    p.muted = 0x8891aa;
    p.dim = 0x565f78;
    p.accent = 0x8b7cf6;
    p.blocked = 0xf0647a;
    p.done = 0x5ccb9a;
    p.working = 0xe9b949;
    p.idle = 0x8891aa;
    p.term_bg = 0x0f131e;
    p.term_fg = 0xdee2ee;
    return p;
}
}  // namespace

TEST(PaletteCheck, MidnightValuesAndProblems) {
    const auto all = c::check_palette(midnight());
    auto find = [&](const std::string& what) {
        for (const auto& x : all)
            if (x.what == what)
                return x;
        ADD_FAILURE() << "no check " << what;
        return c::PaletteCheck{};
    };
    EXPECT_NEAR(find("dim on canvas").value, 2.97, 0.01);
    EXPECT_FALSE(find("dim on canvas").pass());
    EXPECT_TRUE(find("foreground on canvas").pass());
    EXPECT_EQ(find("blocked on chrome").threshold, c::kGraphicAA);
    EXPECT_EQ(find("blocked/done (deuteranopia)").kind, c::CheckKind::kApart);
    // The same palette always yields the same list, in the same order.
    const auto again = c::check_palette(midnight());
    ASSERT_EQ(all.size(), again.size());
    for (std::size_t i = 0; i < all.size(); ++i)
        EXPECT_EQ(all[i].what, again[i].what);
    const auto problems = c::palette_problems(midnight());
    EXPECT_FALSE(problems.empty());
    for (const auto& x : problems)
        EXPECT_FALSE(x.pass()) << x.what;
}

TEST(AmbientTint, KeepsLightnessAndCapsTheAccentTurn) {
    c::TintInput in;
    in.base = midnight();
    in.hover = 0x1b2234;
    in.selected = 0x232b42;
    in.hover_opaque = in.selected_opaque = true;
    const c::TintOutput t = c::tint(in, in.base.blocked, 1.0);
    for (const auto& [before, after] :
         {std::pair{in.base.canvas, t.canvas}, {in.base.chrome, t.chrome}, {in.base.surface, t.surface}}) {
        // 8-bit rounding moves L by well under a hundredth.
        EXPECT_NEAR(c::to_oklab(after).L, c::to_oklab(before).L, 0.004);
        EXPECT_NE(after, before);
    }
    const double turned = std::abs(c::hue_delta(c::hue_of(in.base.accent), c::hue_of(t.accent)));
    EXPECT_LE(turned, 12.5);
    EXPECT_GT(turned, 5.0);
    const c::TintOutput none = c::tint(in, in.base.blocked, 0.0);
    EXPECT_EQ(none.canvas, in.base.canvas);
    EXPECT_EQ(none.accent, in.base.accent);
}

TEST(AmbientTint, ProtectionLowersKOnALowContrastPalette) {
    // A made-up palette whose muted text sits just at AA on the canvas: any shift that darkens or
    // lightens the canvas in luminance terms would push it under, so k must come down.
    c::TintInput in;
    in.base = midnight();
    in.base.canvas = 0x2a2a2a;
    in.base.muted = 0xc0c0c0;
    while (c::contrast(in.base.muted - 0x010101, in.base.canvas) >= 4.5)
        in.base.muted -= 0x010101;  // the darkest grey still at 4.5 or more
    ASSERT_GE(c::contrast(in.base.muted, in.base.canvas), 4.5);
    const c::TintOutput full = c::tint(in, 0x00ffff, 4.0);  // far stronger than any real use
    const c::TintOutput safe = c::tint_protected(in, 0x00ffff, 4.0);
    EXPECT_LT(c::contrast(in.base.muted, full.canvas), 4.5);
    EXPECT_LT(safe.k, 4.0);
    EXPECT_GE(c::contrast(in.base.muted, safe.canvas) + 0.005, 4.5);
    // A gentle tint on Midnight needs no lowering.
    c::TintInput m;
    m.base = midnight();
    EXPECT_DOUBLE_EQ(c::tint_protected(m, m.base.blocked, 1.0).k, 1.0);
}
