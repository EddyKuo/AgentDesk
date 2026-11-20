// window_place: the window's default size per screen and scale, and restoring window.json.
#include "agentdesk/ui/window_place.hpp"

#include <gtest/gtest.h>

namespace place = agentdesk::ui::window_place;
using place::Monitor;
using place::Rect;

namespace {

Monitor screen(int w, int h, int taskbar, unsigned dpi, int x = 0, const char* name = "\\\\.\\DISPLAY1") {
    Monitor m;
    m.name = name;
    m.bounds = {x, 0, w, h};
    m.work = {x, 0, w, h - taskbar};
    m.dpi = dpi;
    return m;
}

}  // namespace

TEST(WindowPlace, FullHdAt100PercentGetsTheDesignSize) {
    const Rect r = place::default_rect(screen(1920, 1080, 48, 96), 1.0f);
    EXPECT_EQ(r, (Rect{320, 106, 1280, 820}));
}

TEST(WindowPlace, FourKAt150PercentScalesWithDpiAndZoom) {
    // 3840x2088 work area at 150 % is 2560x1392 DIPs; 1280x820 x zoom 1.2 = 1536x984 DIPs.
    const Rect r = place::default_rect(screen(3840, 2160, 72, 144), 1.2f);
    EXPECT_EQ(r, (Rect{768, 306, 2304, 1476}));
}

TEST(WindowPlace, FourKAt100PercentTakesAtLeastHalfTheScreen) {
    const Rect r = place::default_rect(screen(3840, 2160, 48, 96), 1.15f);
    EXPECT_EQ(r.w, 1920);
    EXPECT_EQ(r.h, 1056);
}

TEST(WindowPlace, SmallLaptopKeepsAMarginAroundTheWindow) {
    // 1920x1032 work area at 125 % is 1536x825.6 DIPs: the height stops at 90 % of it.
    const Rect r = place::default_rect(screen(1920, 1080, 48, 120), 1.0f);
    EXPECT_EQ(r.w, 1600);
    EXPECT_EQ(r.h, 929);
}

TEST(WindowPlace, NoSavedPlaceUsesTheMonitorUnderTheCursor) {
    const std::vector<Monitor> ms{screen(1920, 1080, 48, 96), screen(3840, 2160, 72, 144, 1920, "\\\\.\\DISPLAY2")};
    const auto p = place::restore(std::nullopt, ms, 1, 1.0f);
    EXPECT_FALSE(p.maximized);
    EXPECT_GE(p.rect.x, 1920);
    EXPECT_EQ(p.rect.w, 1920);  // 1280 DIPs at 150 %
}

TEST(WindowPlace, SavedPlaceComesBackOnItsMonitorInItsDips) {
    const std::vector<Monitor> ms{screen(1920, 1080, 48, 96), screen(3840, 2160, 72, 144, 1920, "\\\\.\\DISPLAY2")};
    const place::Saved saved{true, "\\\\.\\DISPLAY2", 100, 50, 1400, 900};
    const auto p = place::restore(saved, ms, 0, 1.0f);
    EXPECT_TRUE(p.maximized);
    EXPECT_EQ(p.rect, (Rect{1920 + 150, 75, 2100, 1350}));
}

TEST(WindowPlace, MissingMonitorCentresTheSavedSizeOnTheFallback) {
    const std::vector<Monitor> ms{screen(1920, 1080, 48, 96)};
    const place::Saved saved{false, "\\\\.\\DISPLAY9", 100, 50, 1000, 700};
    const auto p = place::restore(saved, ms, 0, 1.0f);
    EXPECT_EQ(p.rect, (Rect{460, 166, 1000, 700}));
}

TEST(WindowPlace, OversizedSavedWindowIsKeptInsideTheWorkArea) {
    const std::vector<Monitor> ms{screen(1920, 1080, 48, 96)};
    const place::Saved saved{false, "\\\\.\\DISPLAY1", 1800, 900, 3000, 2000};
    const auto p = place::restore(saved, ms, 0, 1.0f);
    EXPECT_EQ(p.rect, (Rect{0, 0, 1920, 1032}));
}

TEST(WindowPlace, TinySavedWindowGrowsToTheMinimum) {
    const std::vector<Monitor> ms{screen(1920, 1080, 48, 96)};
    const place::Saved saved{false, "\\\\.\\DISPLAY1", 10, 10, 50, 40};
    const auto p = place::restore(saved, ms, 0, 1.0f);
    EXPECT_EQ(p.rect.w, 480);
    EXPECT_EQ(p.rect.h, 320);
}

TEST(WindowPlace, CaptureAndFormatRoundTrip) {
    const Monitor m = screen(3840, 2160, 72, 144, 1920, "\\\\.\\DISPLAY2");
    const auto saved = place::capture(Rect{1920 + 300, 150, 2304, 1476}, false, m);
    EXPECT_DOUBLE_EQ(saved.x, 200);
    EXPECT_DOUBLE_EQ(saved.w, 1536);
    const auto back = place::parse(place::format(saved));
    ASSERT_TRUE(back.has_value());
    EXPECT_EQ(back->monitor, "\\\\.\\DISPLAY2");
    EXPECT_FALSE(back->maximized);
    EXPECT_DOUBLE_EQ(back->h, 984);
}

TEST(WindowPlace, BrokenFileIsIgnored) {
    EXPECT_FALSE(place::parse("{").has_value());
    EXPECT_FALSE(place::parse("[]").has_value());
    EXPECT_FALSE(place::parse(R"({"bounds":[1,2,3]})").has_value());
    EXPECT_FALSE(place::parse(R"({"bounds":[1,2,"a",4]})").has_value());
}
