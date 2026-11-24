// Appearance by time of day; see schedule.hpp.
#include "agentdesk/ui/schedule.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numbers>

namespace agentdesk::ui::schedule {

namespace {
constexpr double kPi = std::numbers::pi;
constexpr int kDay = 24 * 60;

int wrap_day(int m) {
    m %= kDay;
    return m < 0 ? m + kDay : m;
}

bool leap(int y) {
    return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
}

// Light in [from, to): a plain span, or one that wraps past midnight (a night shift).
bool in_span(int m, int from, int to) {
    return from < to ? (m >= from && m < to) : (m >= from || m < to);
}

// Minutes until the clock next reads `at` (1..1440).
int until(int now, int at) {
    const int d = wrap_day(at - now);
    return d == 0 ? kDay : d;
}
}  // namespace

std::optional<int> parse_hhmm(const std::string& text) {
    // H:MM or HH:MM, digits only.
    const auto colon = text.find(':');
    if (colon == std::string::npos || colon == 0 || colon > 2 || text.size() - colon - 1 != 2)
        return std::nullopt;
    int h = 0, m = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (i == colon)
            continue;
        const char c = text[i];
        if (c < '0' || c > '9')
            return std::nullopt;
        int& part = i < colon ? h : m;
        part = part * 10 + (c - '0');
    }
    if (h < 0 || h > 23 || m < 0 || m > 59)
        return std::nullopt;
    return h * 60 + m;
}

std::string format_hhmm(int minutes) {
    const int m = wrap_day(minutes);
    char buf[8];
    std::snprintf(buf, sizeof buf, "%02d:%02d", m / 60, m % 60);
    return buf;
}

double round_tenth(double v) {
    return std::round(v * 10.0) / 10.0;
}

int day_of_year(int year, int month, int day) {
    static constexpr int kBefore[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    const int m = std::clamp(month, 1, 12);
    return kBefore[m - 1] + day + (m > 2 && leap(year) ? 1 : 0);
}

// NOAA Global Monitoring Laboratory, "General Solar Position Calculations": the fractional year
// at local noon, the equation of time and the declination, then the hour angle of the sun at
// 90.833 degrees from the zenith (refraction and the sun's radius included).
SunDay sun_times_utc(int year, int month, int day, double latitude, double longitude) {
    const double days = leap(year) ? 366.0 : 365.0;
    const double g = 2 * kPi / days * (day_of_year(year, month, day) - 1);
    const double eqtime = 229.18 * (0.000075 + 0.001868 * std::cos(g) - 0.032077 * std::sin(g) -
                                    0.014615 * std::cos(2 * g) - 0.040849 * std::sin(2 * g));
    const double decl = 0.006918 - 0.399912 * std::cos(g) + 0.070257 * std::sin(g) -
                        0.006758 * std::cos(2 * g) + 0.000907 * std::sin(2 * g) - 0.002697 * std::cos(3 * g) +
                        0.00148 * std::sin(3 * g);
    const double lat = latitude * kPi / 180.0;
    const double x =
        std::cos(90.833 * kPi / 180.0) / (std::cos(lat) * std::cos(decl)) - std::tan(lat) * std::tan(decl);
    SunDay out;
    if (x > 1) {
        out.kind = SunDay::kPolarNight;
        return out;
    }
    if (x < -1) {
        out.kind = SunDay::kPolarDay;
        return out;
    }
    const double ha = std::acos(x) * 180.0 / kPi;
    out.sunrise = 720 - 4 * (longitude + ha) - eqtime;
    out.sunset = 720 - 4 * (longitude - ha) - eqtime;
    return out;
}

Today evaluate(const AppearanceSchedule& s, const LocalNow& now) {
    Today t;
    const int m = wrap_day(now.minute);
    if (s.mode == "sun") {
        if (!s.latitude) {
            t.valid = false;
            return t;
        }
        // Without a longitude, the time zone's standard meridian (15 degrees an hour).
        const double lon = s.longitude.value_or(now.std_offset / 60.0 * 15.0);
        const SunDay d = sun_times_utc(now.year, now.month, now.day, *s.latitude, lon);
        if (d.kind != SunDay::kNormal) {
            t.light = d.kind == SunDay::kPolarDay;
            return t;
        }
        t.light_from = wrap_day(static_cast<int>(std::lround(d.sunrise)) + now.utc_offset + s.sun_offset);
        t.dark_from = wrap_day(static_cast<int>(std::lround(d.sunset)) + now.utc_offset + s.sun_offset);
    } else {
        if (s.light_at == s.dark_at) {
            t.valid = false;
            return t;
        }
        t.light_from = wrap_day(s.light_at);
        t.dark_from = wrap_day(s.dark_at);
    }
    if (t.light_from == t.dark_from) {  // an offset can fold them together
        t.valid = false;
        t.light_from = t.dark_from = -1;
        return t;
    }
    t.light = in_span(m, t.light_from, t.dark_from);
    t.next_switch_in = std::min(until(m, t.light_from), until(m, t.dark_from));
    return t;
}

}  // namespace agentdesk::ui::schedule
