#pragma once
// The rules behind the AgentDesk Cockpit layout (gauges, the agents roster, the state log, the
// count of agents done today), kept apart from its drawing (apps/desktop/cockpit_layout.cpp) so
// they can be unit-tested.
#include <cstddef>
#include <deque>
#include <string>

#include "agentdesk/ui/workspace.hpp"

namespace agentdesk::ui::cockpit {

// A gauge's arc runs clockwise over 240 degrees, from the lower left (210 degrees, counted
// counter-clockwise from 3 o'clock) over the top to the lower right (-30 degrees).
inline constexpr float kSweepDegrees = 240.0f;
inline constexpr float kStartDegrees = 210.0f;
// The angle for `value` out of `max`, in radians as ImGui draws them (y grows downward, so angles
// run clockwise): from the start to the end of the sweep, clamped to it.
float gauge_angle(float value, float max);
// The gauge's top value. Working and needs-you: at least 4, else the number of agents. Done today:
// at least 10, else the count rounded up to the next multiple of 5.
int agents_max(int agents);
int done_max(int done);
// The gauge's number: the value, or "--" when agent-host is not there to say.
std::string gauge_number(int value, bool known);
// The number's size: 30 design px, 24 once it has three digits.
float number_size(int value);

// How many agents finished today (local time), kept across restarts. A new day (or a clock
// turned back past the saved date) starts again from zero.
struct DoneToday {
    std::string date;  // "YYYY-MM-DD"
    int count = 0;
    // Starts a new day when `today` differs from `date`. Returns true when it changed anything.
    bool roll(const std::string& today);
    // One more agent done today.
    void add(const std::string& today);
};
std::string date_text(int year, int month, int day);      // "2026-11-20"
std::string time_text(int hour, int minute, int second);  // "16:25:03"

// An agent finishing: working before, done or idle now (looking at a finished agent turns done
// into idle, so both count).
bool finished(Attention from, Attention to);

// One state change, for the log.
struct Event {
    std::string time;   // "16:25:03"
    std::string agent;  // the session id, so a click can go to it
    std::string who;    // "repo-a/claude"
    Attention from = Attention::kNone;
    Attention to = Attention::kNone;
};
// The log: newest first, at most kLogSize entries, in memory only.
inline constexpr std::size_t kLogSize = 200;
class EventLog {
public:
    void push(Event e);
    [[nodiscard]] const std::deque<Event>& items() const { return items_; }
    void clear() { items_.clear(); }

private:
    std::deque<Event> items_;
};

// A state as the cockpit writes it: "WORKING", "NEEDS YOU", "DONE", "IDLE", "UNKNOWN".
const char* state_word(Attention a);
// The roster's order: needs you, done, working, idle, then unknown.
int roster_rank(Attention a);

// What fits the window: below 560 design px of gauge column the gauges turn into bars; below 1000
// px of window width the roster goes, below 820 the log too.
struct Fit {
    bool bars = false;
    bool roster = true;
    bool log = true;
};
Fit fit(float window_w, float gauges_h);

}  // namespace agentdesk::ui::cockpit
