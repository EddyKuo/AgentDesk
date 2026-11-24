// The shared design tokens: the spacing grid, type levels, easing, tweens and colour treatments.
#include "agentdesk/ui/design_tokens.hpp"

#include <gtest/gtest.h>

#include "agentdesk/ui/color_math.hpp"

namespace tk = agentdesk::ui::tokens;
namespace color = agentdesk::ui::color;

TEST(DesignTokens, SpacingSitsOnTheFourPixelGrid) {
    EXPECT_EQ(tk::space(1), 4.0f);
    EXPECT_EQ(tk::space(2), tk::kSpaceS);
    for (float v : {tk::kSpaceXs, tk::kSpaceS, tk::kSpaceM, tk::kSpaceL, tk::kSpaceXl, tk::kSpaceXxl})
        EXPECT_EQ(static_cast<int>(v) % 4, 0) << v;
    EXPECT_LT(tk::kRadiusSmall, tk::kRadiusCard);
    EXPECT_LT(tk::kRadiusCard, tk::kRadiusPanel);
}

TEST(DesignTokens, TypeLevelsFormAHierarchy) {
    EXPECT_GT(tk::kTitle.size, tk::kBody.size);
    EXPECT_GT(tk::kBody.size, tk::kCaption.size);
    EXPECT_EQ(tk::kTitle.weight, tk::Weight::kSemibold);
    EXPECT_EQ(tk::kBody.weight, tk::Weight::kRegular);
    for (const auto& l : {tk::kTitle, tk::kBody, tk::kCaption}) {
        EXPECT_GT(l.line, l.size);
        EXPECT_EQ(static_cast<int>(l.line) % 2, 0);
    }
}

TEST(DesignTokens, EasingStartsAndEndsInPlace) {
    for (auto f : {tk::ease_out, tk::ease_in_out}) {
        EXPECT_FLOAT_EQ(f(0.0f), 0.0f);
        EXPECT_FLOAT_EQ(f(1.0f), 1.0f);
        EXPECT_FLOAT_EQ(f(-1.0f), 0.0f);
        EXPECT_FLOAT_EQ(f(2.0f), 1.0f);
        float last = 0.0f;
        for (int i = 1; i <= 20; ++i) {
            const float v = f(static_cast<float>(i) / 20.0f);
            EXPECT_GE(v, last);
            last = v;
        }
    }
    EXPECT_GT(tk::ease_out(0.5f), 0.5f);  // most of the move happens early
    EXPECT_FLOAT_EQ(tk::ease_in_out(0.5f), 0.5f);
}

TEST(DesignTokens, TweenEasesToANewTargetAndStops) {
    tk::Tween tw;
    EXPECT_EQ(tw.update(0.0f, 1000.0), 0.0f);  // the first value shows at once
    EXPECT_FALSE(tw.moving(1000.0));
    const float a = tw.update(1.0f, 1000.0, 200.0);
    EXPECT_EQ(a, 0.0f);
    EXPECT_TRUE(tw.moving(1050.0));
    const float mid = tw.update(1.0f, 1100.0, 200.0);
    EXPECT_GT(mid, 0.5f);
    EXPECT_LT(mid, 1.0f);
    EXPECT_EQ(tw.update(1.0f, 1200.0, 200.0), 1.0f);
    EXPECT_FALSE(tw.moving(1200.0));
    // Turned back half way: it starts from where it was, not from the end.
    tw.update(0.0f, 2000.0, 200.0);
    const float back = tw.update(0.0f, 2100.0, 200.0);
    tw.update(1.0f, 2100.0, 200.0);
    EXPECT_NEAR(tw.value(2100.0), back, 1e-5);
}

TEST(DesignTokens, ReducedMotionJumps) {
    tk::Tween tw;
    tw.update(0.0f, 0.0);
    EXPECT_EQ(tw.update(1.0f, 10.0, 200.0, true), 1.0f);
    EXPECT_FALSE(tw.moving(11.0));
}

TEST(DesignTokens, SelectedIsStrongerThanHoverAndFocusReads) {
    const tk::Surface dark{0x17191c, 0xe8e9eb, 0x3b82f6, false};
    const tk::Surface light{0xfbfaf7, 0x1f2328, 0x2f6fde, true};
    for (const auto& s : {dark, light}) {
        const double hover = color::delta_e2000(tk::hover_fill(s), s.bg);
        const double sel = color::delta_e2000(tk::selected_fill(s), s.bg);
        EXPECT_GT(hover, 1.0);
        EXPECT_GT(sel, hover);
        EXPECT_GE(color::contrast(tk::focus_ring(s), s.bg), 3.0);
        EXPECT_GE(color::contrast(tk::selected_edge(s), tk::selected_fill(s)), 3.0);
        EXPECT_GT(color::delta_e2000(tk::divider(s), s.bg), 2.0);
    }
}

TEST(DesignTokens, ElevationIsSofterOnLightThemes) {
    const tk::Surface dark{0x17191c, 0xe8e9eb, 0x3b82f6, false};
    const tk::Surface light{0xfbfaf7, 0x1f2328, 0x2f6fde, true};
    EXPECT_GT(tk::elevation(dark, 1).shadow_alpha, tk::elevation(light, 1).shadow_alpha);
    EXPECT_GT(tk::elevation(dark, 2).shadow_spread, tk::elevation(dark, 1).shadow_spread);
    EXPECT_GT(tk::elevation(dark, 1).highlight_alpha, 0);
    EXPECT_EQ(tk::elevation(light, 1).highlight_alpha, 0);
}
