#pragma once
// Appearance by time of day: light between two fixed clock times, or between sunrise and sunset
// worked out from a latitude (and longitude) the person typed in. Pure arithmetic: the caller
// passes the local date, time and time-zone offsets, nothing here reads a clock or a location.
#include <optional>
#include <string>

namespace agentdesk::ui::schedule {

struct AppearanceSchedule {
    std::string mode = "fixed";  // "fixed" | "sun"
    int light_at = 7 * 60;       // minutes after local midnight
    int dark_at = 19 * 60;
    std::optional<double> latitude, longitude;  // degrees (north, east positive), rounded to 0.1
    int sun_offset = 0;                         // minutes added to sunrise and sunset (-120..120)
};

// The local moment to judge, with the offsets in force: utc_offset includes daylight saving,
// std_offset does not (its meridian stands in for a missing longitude).
struct LocalNow {
    int year = 2026, month = 1, day = 1;
    int minute = 0;      // minutes after local midnight, 0..1439
    int utc_offset = 0;  // local = UTC + utc_offset (minutes)
    int std_offset = 0;
};

// "HH:MM" (00:00..23:59) to minutes, else nullopt; and back.
std::optional<int> parse_hhmm(const std::string& text);
std::string format_hhmm(int minutes);
// Rounds to one decimal place (about 11 km), the precision the file keeps.
double round_tenth(double v);

// Sunrise and sunset in minutes after UTC midnight (NOAA's simplified solar position).
struct SunDay {
    enum Kind { kNormal, kPolarDay, kPolarNight } kind = kNormal;
    double sunrise = 0, sunset = 0;
};
SunDay sun_times_utc(int year, int month, int day, double latitude, double longitude);

struct Today {
    bool valid = true;   // false: the times are equal, or sun mode without a latitude
    bool light = false;  // what the schedule says now
    // Local minutes of today's switches; -1 when there is none (midnight sun, polar night).
    int light_from = -1, dark_from = -1;
    // Minutes from now to the next switch (at most a day; 1440 when there is none today or
    // tomorrow, as in the polar summer).
    int next_switch_in = 1440;
};
Today evaluate(const AppearanceSchedule& s, const LocalNow& now);

// Day of the year, 1..366.
int day_of_year(int year, int month, int day);

}  // namespace agentdesk::ui::schedule
