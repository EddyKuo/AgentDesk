// Appearance by time of day (libs/ui schedule): fixed times, sunrise and sunset, polar days,
// time zones and daylight saving. Time and zone are parameters; no clock is read.
#include "agentdesk/ui/schedule.hpp"

#include <cstdlib>

#include "agentdesk/ui/command_names.hpp"

#include <gtest/gtest.h>

namespace sc = agentdesk::ui::schedule;

namespace {
sc::LocalNow at(int y, int mo, int d, int hh, int mm, int offset_min, int std_offset_min) {
    return sc::LocalNow{y, mo, d, hh * 60 + mm, offset_min, std_offset_min};
}
sc::AppearanceSchedule fixed(int light, int dark) {
    sc::AppearanceSchedule s;
    s.light_at = light;
    s.dark_at = dark;
    return s;
}
sc::AppearanceSchedule sun(double lat, std::optional<double> lon) {
    sc::AppearanceSchedule s;
    s.mode = "sun";
    s.latitude = lat;
    s.longitude = lon;
    return s;
}
// |a - b| in minutes on a clock.
int clock_gap(int a, int b) {
    const int d = std::abs(a - b) % 1440;
    return std::min(d, 1440 - d);
}
}  // namespace

TEST(Schedule, HhMmParsingAndFormatting) {
    EXPECT_EQ(sc::parse_hhmm("07:00"), 420);
    EXPECT_EQ(sc::parse_hhmm("7:05"), 425);
    EXPECT_EQ(sc::parse_hhmm("23:59"), 1439);
    EXPECT_EQ(sc::parse_hhmm("00:00"), 0);
    for (const char* bad : {"24:00", "12:60", "12", "12:5", "ab:cd", "", "12:00x", "-1:00", "1:2:3"})
        EXPECT_FALSE(sc::parse_hhmm(bad)) << bad;
    EXPECT_EQ(sc::format_hhmm(425), "07:05");
    EXPECT_EQ(sc::format_hhmm(-15), "23:45");
    EXPECT_EQ(sc::format_hhmm(1440 + 60), "01:00");
    EXPECT_DOUBLE_EQ(sc::round_tenth(25.04), 25.0);
    EXPECT_DOUBLE_EQ(sc::round_tenth(121.56), 121.6);
}

TEST(Schedule, FixedDaySpan) {
    const auto s = fixed(7 * 60, 19 * 60);
    EXPECT_TRUE(sc::evaluate(s, at(2026, 6, 21, 18, 59, 480, 480)).light);
    EXPECT_FALSE(sc::evaluate(s, at(2026, 6, 21, 19, 0, 480, 480)).light);
    EXPECT_FALSE(sc::evaluate(s, at(2026, 6, 21, 6, 59, 480, 480)).light);
    EXPECT_TRUE(sc::evaluate(s, at(2026, 6, 21, 7, 0, 480, 480)).light);
    const auto t = sc::evaluate(s, at(2026, 6, 21, 18, 0, 480, 480));
    EXPECT_EQ(t.next_switch_in, 60);
    EXPECT_EQ(t.light_from, 420);
    EXPECT_EQ(t.dark_from, 1140);
}

TEST(Schedule, FixedNightShiftWrapsPastMidnight) {
    const auto s = fixed(22 * 60, 6 * 60);
    EXPECT_TRUE(sc::evaluate(s, at(2026, 6, 21, 2, 0, 0, 0)).light);
    EXPECT_TRUE(sc::evaluate(s, at(2026, 6, 21, 23, 0, 0, 0)).light);
    EXPECT_FALSE(sc::evaluate(s, at(2026, 6, 21, 12, 0, 0, 0)).light);
    EXPECT_EQ(sc::evaluate(s, at(2026, 6, 21, 21, 0, 0, 0)).next_switch_in, 60);
}

TEST(Schedule, EqualTimesAreInvalid) {
    EXPECT_FALSE(sc::evaluate(fixed(600, 600), at(2026, 6, 21, 12, 0, 0, 0)).valid);
    sc::AppearanceSchedule no_latitude;
    no_latitude.mode = "sun";
    EXPECT_FALSE(sc::evaluate(no_latitude, at(2026, 6, 21, 12, 0, 0, 0)).valid);
}

TEST(Schedule, TaipeiSolsticesMatchNoaa) {
    // NOAA's solar calculator for 25.0 N, 121.5 E (UTC+8): 21 June sunrise 05:05, sunset 18:47;
    // 21 December sunrise 06:35, sunset 17:12.
    const auto june = sc::evaluate(sun(25.0, 121.5), at(2026, 6, 21, 12, 0, 480, 480));
    EXPECT_LE(clock_gap(june.light_from, 5 * 60 + 5), 3) << sc::format_hhmm(june.light_from);
    EXPECT_LE(clock_gap(june.dark_from, 18 * 60 + 47), 3) << sc::format_hhmm(june.dark_from);
    const auto dec = sc::evaluate(sun(25.0, 121.5), at(2026, 12, 21, 12, 0, 480, 480));
    EXPECT_LE(clock_gap(dec.light_from, 6 * 60 + 35), 3) << sc::format_hhmm(dec.light_from);
    EXPECT_LE(clock_gap(dec.dark_from, 17 * 60 + 12), 3) << sc::format_hhmm(dec.dark_from);
    EXPECT_TRUE(june.light);
    EXPECT_FALSE(sc::evaluate(sun(25.0, 121.5), at(2026, 6, 21, 20, 0, 480, 480)).light);
}

TEST(Schedule, MissingLongitudeUsesTheZoneMeridian) {
    // Taiwan is 1.5 degrees east of the UTC+8 meridian: 6 minutes earlier than the meridian.
    const auto with = sc::evaluate(sun(25.0, 121.5), at(2026, 6, 21, 12, 0, 480, 480));
    const auto without = sc::evaluate(sun(25.0, std::nullopt), at(2026, 6, 21, 12, 0, 480, 480));
    EXPECT_LE(clock_gap(with.light_from, without.light_from), 7);
    EXPECT_LE(clock_gap(with.dark_from, without.dark_from), 7);
}

TEST(Schedule, EquatorSydneyAndOffset) {
    const auto eq = sc::evaluate(sun(0, 0), at(2026, 3, 20, 12, 0, 0, 0));
    EXPECT_LE(clock_gap(eq.light_from, 6 * 60 + 4), 6) << sc::format_hhmm(eq.light_from);
    EXPECT_LE(clock_gap(eq.dark_from, 18 * 60 + 10), 6) << sc::format_hhmm(eq.dark_from);
    // Sydney in its winter (UTC+10, no daylight saving in June): about 07:00 to 16:54.
    const auto syd = sc::evaluate(sun(-33.9, 151.2), at(2026, 6, 21, 12, 0, 600, 600));
    EXPECT_LE(clock_gap(syd.light_from, 7 * 60), 4) << sc::format_hhmm(syd.light_from);
    EXPECT_LE(clock_gap(syd.dark_from, 16 * 60 + 54), 4) << sc::format_hhmm(syd.dark_from);
    auto later = sun(25.0, 121.5);
    later.sun_offset = 30;
    const auto shifted = sc::evaluate(later, at(2026, 6, 21, 12, 0, 480, 480));
    const auto plain = sc::evaluate(sun(25.0, 121.5), at(2026, 6, 21, 12, 0, 480, 480));
    EXPECT_EQ(shifted.light_from, plain.light_from + 30);
    EXPECT_EQ(shifted.dark_from, plain.dark_from + 30);
}

TEST(Schedule, PolarDayAndNight) {
    const auto june = sc::evaluate(sun(69.6, 18.9), at(2026, 6, 21, 0, 30, 120, 60));
    EXPECT_TRUE(june.valid);
    EXPECT_TRUE(june.light);
    EXPECT_EQ(june.light_from, -1);
    const auto dec = sc::evaluate(sun(69.6, 18.9), at(2026, 12, 21, 12, 0, 60, 60));
    EXPECT_FALSE(dec.light);
    EXPECT_EQ(dec.dark_from, -1);
    // Southern summer at the same latitude.
    EXPECT_TRUE(sc::evaluate(sun(-69.6, 0), at(2026, 12, 21, 3, 0, 0, 0)).light);
}

TEST(Schedule, BerlinDaylightSavingChangeDays) {
    // 29 March 2026: clocks go to UTC+2 at 02:00. Sunrise about 06:50, sunset about 19:39 local.
    const auto march = sc::evaluate(sun(52.5, 13.4), at(2026, 3, 29, 12, 0, 120, 60));
    EXPECT_LE(clock_gap(march.light_from, 6 * 60 + 50), 6) << sc::format_hhmm(march.light_from);
    EXPECT_LE(clock_gap(march.dark_from, 19 * 60 + 39), 6) << sc::format_hhmm(march.dark_from);
    // 25 October 2026: back to UTC+1. Solar noon about 11:51 and ten hours of day: sunrise about
    // 06:48, sunset about 16:53.
    const auto oct = sc::evaluate(sun(52.5, 13.4), at(2026, 10, 25, 12, 0, 60, 60));
    EXPECT_LE(clock_gap(oct.light_from, 6 * 60 + 48), 6) << sc::format_hhmm(oct.light_from);
    EXPECT_LE(clock_gap(oct.dark_from, 16 * 60 + 53), 6) << sc::format_hhmm(oct.dark_from);
    // The hour that repeats on the October night is judged by the clock: 02:30 is dark either way.
    EXPECT_FALSE(sc::evaluate(sun(52.5, 13.4), at(2026, 10, 25, 2, 30, 120, 60)).light);
    EXPECT_FALSE(sc::evaluate(sun(52.5, 13.4), at(2026, 10, 25, 2, 30, 60, 60)).light);
}

TEST(Schedule, DayOfYear) {
    EXPECT_EQ(sc::day_of_year(2026, 1, 1), 1);
    EXPECT_EQ(sc::day_of_year(2026, 12, 31), 365);
    EXPECT_EQ(sc::day_of_year(2028, 3, 1), 61);
}

TEST(ThemeCommands, NamesAreBuiltIn) {
    for (const char* name : {"appearance", "appearance.toggle", "theme.suggested", "theme.ambient", "theme.new",
                             "theme.edit", "theme.folder"})
        EXPECT_TRUE(agentdesk::ui::is_builtin_command(name)) << name;
}
