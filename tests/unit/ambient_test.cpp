// The ambient tint's strength over time (libs/ui ambient): ease in, hold, ease out, reversal,
// and the instant mode, on a made-up clock.
#include "agentdesk/ui/ambient.hpp"

#include <gtest/gtest.h>

using agentdesk::ui::AmbientFade;

TEST(AmbientFade, EasesInToFullStrengthIn1200Ms) {
    AmbientFade f;
    EXPECT_DOUBLE_EQ(f.update(0, 0), 0);
    EXPECT_DOUBLE_EQ(f.update(1, 1000), 0);
    const double mid = f.update(1, 1600);
    EXPECT_NEAR(mid, 0.5, 1e-9);  // smoothstep is 0.5 half way
    EXPECT_TRUE(f.moving(1600));
    EXPECT_DOUBLE_EQ(f.update(1, 2200), 1);
    EXPECT_FALSE(f.moving(2200));
}

// Blocked for half a second: up to the peak, held to 2 s, then down, never rising again.
TEST(AmbientFade, ShortBlockStillRisesHoldsThenFalls) {
    AmbientFade f;
    double last = 0;
    bool falling = false;
    double peak_at = -1, fall_from = -1;
    for (int t = 0; t <= 5000; t += 10) {
        const double k = f.update(t < 500 ? 1.0 : 0.0, t);
        if (k < last - 1e-12) {
            falling = true;
            if (fall_from < 0)
                fall_from = t;
        }
        if (falling)
            EXPECT_LE(k, last + 1e-12) << "rose again at " << t;
        else
            EXPECT_GE(k, last - 1e-12) << "fell early at " << t;
        if (k == 1.0 && peak_at < 0)
            peak_at = t;
        last = k;
    }
    EXPECT_NEAR(peak_at, 1200, 10);
    EXPECT_NEAR(fall_from, 2010, 10);
    EXPECT_DOUBLE_EQ(last, 0);  // 2000 + 2500 ms later it is gone
}

TEST(AmbientFade, ReversalCarriesOnFromTheCurrentStrength) {
    AmbientFade f;
    f.update(1, 0);
    const double at = f.update(1, 600);  // half way in
    EXPECT_NEAR(at, 0.5, 1e-9);
    // The hold keeps it rising until 2 s.
    EXPECT_NEAR(f.update(0, 610), f.value(610), 1e-12);
    EXPECT_GT(f.update(0, 610), at);
    // After the hold, falling starts from wherever it is, without a jump.
    const double before = f.update(0, 1999);
    const double after = f.update(0, 2001);
    EXPECT_NEAR(before, 1.0, 1e-9);
    EXPECT_NEAR(after, 1.0, 1e-3);
    // Back up while falling: rises from the value it had, no jump.
    const double falling = f.update(0, 3000);
    EXPECT_LT(falling, 1.0);
    const double up = f.update(1, 3000);
    EXPECT_DOUBLE_EQ(up, falling);
    EXPECT_GT(f.update(1, 3100), falling);
}

TEST(AmbientFade, HalfStrengthForDoneAndInstantMode) {
    AmbientFade f;
    EXPECT_DOUBLE_EQ(f.update(0.5, 0, true), 0.5);  // reduce motion: no transition frames
    EXPECT_FALSE(f.moving(0));
    EXPECT_DOUBLE_EQ(f.update(1, 10, true), 1);
    EXPECT_DOUBLE_EQ(f.update(0, 20, true), 1);  // the hold still applies
    EXPECT_DOUBLE_EQ(f.update(0, 2100, true), 0);
}
