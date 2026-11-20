// The Cockpit layout's rules; see cockpit.hpp.
#include "agentdesk/ui/cockpit.hpp"

#include <algorithm>
#include <cstdio>
#include <numbers>

namespace agentdesk::ui::cockpit {

float gauge_angle(float value, float max) {
    const float t = max > 0.0f ? std::clamp(value / max, 0.0f, 1.0f) : 0.0f;
    const float degrees = kStartDegrees - kSweepDegrees * t;  // counter-clockwise, y up
    // ImGui's y grows downward: the same direction on screen is the negated angle.
    return -degrees * std::numbers::pi_v<float> / 180.0f;
}

int agents_max(int agents) {
    return std::max(4, agents);
}

int done_max(int done) {
    const int up = done <= 0 ? 0 : ((done + 4) / 5) * 5;
    return std::max(10, up);
}

std::string gauge_number(int value, bool known) {
    return known ? std::to_string(value) : std::string("--");
}

float number_size(int value) {
    return value > 99 ? 24.0f : 30.0f;
}

bool DoneToday::roll(const std::string& today) {
    if (date == today)
        return false;
    date = today;
    count = 0;
    return true;
}

void DoneToday::add(const std::string& today) {
    roll(today);
    ++count;
}

std::string date_text(int year, int month, int day) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "%04d-%02d-%02d", year, month, day);
    return buf;
}

std::string time_text(int hour, int minute, int second) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "%02d:%02d:%02d", hour, minute, second);
    return buf;
}

bool finished(Attention from, Attention to) {
    return from == Attention::kWorking && (to == Attention::kDone || to == Attention::kIdle);
}

void EventLog::push(Event e) {
    items_.push_front(std::move(e));
    while (items_.size() > kLogSize)
        items_.pop_back();
}

const char* state_word(Attention a) {
    switch (a) {
        case Attention::kWorking:
            return "WORKING";
        case Attention::kBlocked:
            return "NEEDS YOU";
        case Attention::kDone:
            return "DONE";
        case Attention::kIdle:
            return "IDLE";
        case Attention::kNone:
            break;
    }
    return "UNKNOWN";
}

int roster_rank(Attention a) {
    switch (a) {
        case Attention::kBlocked:
            return 0;
        case Attention::kDone:
            return 1;
        case Attention::kWorking:
            return 2;
        case Attention::kIdle:
            return 3;
        case Attention::kNone:
            break;
    }
    return 4;
}

Fit fit(float window_w, float gauges_h) {
    Fit f;
    f.bars = gauges_h < 560.0f;
    f.roster = window_w >= 1000.0f;
    f.log = window_w >= 820.0f;
    return f;
}

}  // namespace agentdesk::ui::cockpit
