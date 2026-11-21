#pragma once
// Pair: two agents side by side on the same task. The pair's life (setup, starting, running, one
// side gone), the race between the two (who finished first and by how much, from structured agent
// states only), the worktree names each side gets and the text the result strip shows. No drawing
// here, so every rule is unit-tested.
#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace agentdesk::ui::pair {

enum class Side { kLeft = 0, kRight = 1 };
[[nodiscard]] constexpr std::size_t index(Side s) {
    return static_cast<std::size_t>(s);
}
[[nodiscard]] constexpr Side other(Side s) {
    return s == Side::kLeft ? Side::kRight : Side::kLeft;
}
const char* side_name(Side s);  // "left", "right"
std::optional<Side> parse_side(std::string_view s);

// ---------------------------------------------------------------- the pair's life
enum class Phase {
    kSetup,     // no pair: the setup card shows
    kStarting,  // Start was pressed; waiting for both sessions
    kRunning,   // both sides have a live session
    kHalf,      // one side's session ended (or was closed); the other still runs
};

class Model {
public:
    [[nodiscard]] Phase phase() const { return phase_; }
    [[nodiscard]] const std::string& id(Side s) const { return ids_[index(s)]; }
    [[nodiscard]] bool has(const std::string& session) const;
    [[nodiscard]] std::optional<Side> side_of(const std::string& session) const;
    [[nodiscard]] const std::string& error() const { return error_; }

    // Setup -> Starting (any phase but Starting: a new pair replaces the old one).
    bool start();
    // A side's session exists. Starting -> Running once both have one.
    void placed(Side s, std::string session);
    // Starting failed: back to Setup with why.
    void failed(std::string why);
    // Both ids at once (a pair from existing sessions, or one read back from ui.json): Running.
    bool adopt(std::string left, std::string right);
    // The sessions that still run (every tick): a side whose session is gone empties, Running ->
    // Half; both gone -> Setup. Starting is left alone (its sessions may not be listed yet).
    void prune(const std::vector<std::string>& alive);
    // Another session takes a side (Running or Half). A session already on the other side swaps.
    bool repair(Side s, std::string session);
    // Left and right trade places (Running or Half).
    bool swap();
    // The pair ends (the terminals keep running): Setup.
    void close();

private:
    Phase phase_ = Phase::kSetup;
    std::array<std::string, 2> ids_;
    std::string error_;
};

// ---------------------------------------------------------------- the race
// Times are ms since 1970 (the host's state "at" and this computer's clock are both that).
struct Runner {
    long long started_ms = 0;   // its turn began (prompt sent, or Working first seen): 0 none
    long long finished_ms = 0;  // went from Working to Done: 0 not yet
    bool working = false;       // Working was seen since it started
    [[nodiscard]] bool running() const { return started_ms != 0 && finished_ms == 0; }
    [[nodiscard]] bool finished() const { return started_ms != 0 && finished_ms != 0; }
    // The turn's length so far (running: up to now_ms), 0 when it has not started.
    [[nodiscard]] long long elapsed(long long now_ms) const;
};

struct Result {
    std::optional<Side> winner;  // none: a tie
    long long margin_ms = 0;     // how far ahead the winner was
    std::array<long long, 2> elapsed{};
};

// Within this, the two finished together.
inline constexpr long long kTieMs = 1000;

class Race {
public:
    // A new round: both runners clear.
    void reset();
    // A prompt went to this side at now_ms: its turn starts there (a round that had ended starts
    // over first).
    void sent(Side s, long long now_ms);
    // The side's structured agent state ("Working", "Done", "Blocked", "Idle", "Unknown") and when
    // it took that value (0: unknown, then now_ms is used). Feed every tick; repeats change nothing.
    // Working starts a turn (or keeps the one a prompt started); Done after Working finishes it;
    // Working again after Done reopens it (the agent carried on). Once both finished, Working on
    // either side starts a new round. Nothing is read from the screen (ADR-09).
    void observe(Side s, std::string_view agent_state, long long at_ms, long long now_ms);
    // Left and right trade places.
    void swap();

    [[nodiscard]] const Runner& runner(Side s) const { return runners_[index(s)]; }
    [[nodiscard]] bool active() const;  // either side has a turn under way or finished
    // The side that finished while the other still runs (the finish line), else none.
    [[nodiscard]] std::optional<Side> first_finished() const;
    // Both finished: who won and by how much.
    [[nodiscard]] std::optional<Result> result() const;

private:
    std::array<Runner, 2> runners_{};
    std::array<std::string, 2> last_state_{};
};

// "8s", "42s", "3m 05s", "1h 02m".
std::string format_duration(long long ms);
// The strip's headline: "<left> finished first, 12s ahead", "<right> ..." or "A dead heat".
std::string headline(const Result& r, std::string_view left_name, std::string_view right_name);
// "3 files  +42 -7", "1 file  +3 -0", "No changes"; files < 0: "Reading changes...".
std::string changes_text(int files, int added, int removed);

// ---------------------------------------------------------------- setup
// The branch a side's worktree gets: "pair/<stamp>-<program>-<a|b>". `stamp` is the start time as
// "MMDD-HHMM" (stamp_of); the program part is lower case letters and digits only (a fallback
// "agent").
std::string branch_name(std::string_view program, Side s, std::string_view stamp);
// "MMDD-HHMM" from a local date and time.
std::string stamp_of(int month, int day, int hour, int minute);
// The two profiles the setup card starts with (profiles: id, name, in the host's order): Claude
// Code on the left and Codex on the right when they exist, else the same agent twice, else the
// first two (or the first twice). Empty strings when there are no profiles.
std::pair<std::string, std::string> default_profiles(
    const std::vector<std::pair<std::string, std::string>>& profiles);
// A session's name in the pair: "<profile name> (A)" on the left, "(B)" on the right.
std::string session_name(std::string_view profile_name, Side s);

// ---------------------------------------------------------------- commands
// pair.send's argument: "both" (also empty), "left", "right".
enum class Target { kBoth, kLeft, kRight };
std::optional<Target> parse_target(std::string_view arg);
// Every pair.* command, for the palette and the known-command test.
const std::vector<std::string>& command_names();

}  // namespace agentdesk::ui::pair
