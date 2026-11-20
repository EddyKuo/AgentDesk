#pragma once
// The rules behind the Timeline overlay (apps/desktop/timeline.cpp): turning agent-host's state
// marks into bars, merging bars too thin to see, the time axis, and the order of the rows. Pure
// functions, unit-tested.
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace agentdesk::ui::timeline {

// What a stretch of time is drawn as. kUnknown draws only the baseline (no state is never guessed
// as Idle); kExited ends the row (an "x" where the program ended).
enum class Kind { kUnknown, kIdle, kWorking, kBlocked, kDone, kError, kExited };

// One state change as agent-host reported it (session.timeline marks), in this computer's clock.
struct Mark {
    long long at_ms = 0;
    std::string agent;    // "Working", "Blocked", ...
    std::string process;  // "Running", "Exited", ...
};

struct Segment {
    long long from_ms = 0;
    long long to_ms = 0;
    Kind kind = Kind::kUnknown;
    std::string last_known;  // for kUnknown: the state before it ("" when none)
    int merged = 1;          // how many short stretches pack() folded into this one
};

Kind kind_of(const std::string& agent, const std::string& process);
// How urgent a kind is when stretches merge: Blocked, Error, Done, Working, Idle, Exited, Unknown.
int attention_rank(Kind k);
const char* kind_label(Kind k);  // "Working", "Needs you", "Done", "Idle", "Error", "Exited", "No status"

// The marks as stretches of time, cut to [from_ms, to_ms]: each mark lasts until the next one (the
// last until to_ms). A mark earlier than the one before it (the clock was set back) is dropped
// and counted in `dropped`.
std::vector<Segment> segments(const std::vector<Mark>& marks, long long from_ms, long long to_ms,
                              int* dropped = nullptr);

// Stretches narrower than `min_px` at `px_per_ms` are folded together with their short neighbours
// into one stretch of the most urgent kind among them, so a moment of Blocked never disappears.
std::vector<Segment> pack(const std::vector<Segment>& in, double px_per_ms, float min_px);

// Axis ticks between from_ms and to_ms over `width` px, on whole local minutes or hours
// (`utc_offset_ms`: local time minus UTC). The step follows the range (15 min: 2 min, 1 h: 15 min,
// 4 h: 30 min, 24 h: 3 h) and doubles until labels are at least `min_gap` px apart.
struct Tick {
    long long at_ms = 0;
    float x = 0;
    std::string label;  // "13:15" (24-hour)
};
std::vector<Tick> ticks(long long from_ms, long long to_ms, float width, long long utc_offset_ms,
                        float min_gap = 56.0f);

// "15m", "1h", "4h", "24h" in milliseconds (1 h for anything else); the next shorter / longer one.
long long range_ms(const std::string& range);
std::string shorter_range(const std::string& range);
std::string longer_range(const std::string& range);

// "45s", "19m", "1h 05m", "26h".
std::string duration_text(long long ms);
// "13:05" in local time.
std::string clock_text(long long at_ms, long long utc_offset_ms);

// The rows, top to bottom: agents that need the person first, then by their last change (newest
// first); each one's sub-agents right under it (the same order), one level deeper. A sub-agent
// whose parent has no row of its own stands at the top level.
struct RowInput {
    std::string id;
    std::string parent;
    long long last_change_ms = 0;
    bool needs_you = false;
};
struct Row {
    std::string id;
    int depth = 0;
};
std::vector<Row> rows(const std::vector<RowInput>& in);

// One session of a session.timeline answer; nullopt when the entry is not an object with an id.
// Marks of the wrong shape are skipped; times are moved by `offset_ms` (this computer's clock
// minus the host's, from now_ms).
struct HostSession {
    std::string id;
    std::string name;
    std::string program;
    std::string profile;
    std::string parent;
    bool ended = false;
    bool truncated = false;
    bool adopted = false;
    std::vector<Mark> marks;
};
std::optional<HostSession> parse_session(const nlohmann::json& j, long long offset_ms);

}  // namespace agentdesk::ui::timeline
