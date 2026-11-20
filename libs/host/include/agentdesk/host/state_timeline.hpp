#pragma once

// A session's state history for the Timeline: when its agent state or process state changed,
// nothing else (no messages, no screen content). Bounded by count and age.

#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

#include "agentdesk/host/states.hpp"

namespace agentdesk::host {

struct StateMark {
    std::int64_t at_ms = 0;  // wall clock, milliseconds since 1970 (UTC)
    AgentState agent = AgentState::kUnknown;
    ProcessState process = ProcessState::kStarting;
    friend bool operator==(const StateMark&, const StateMark&) = default;
};

class StateTimeline {
public:
    static constexpr std::size_t kMaxMarks = 1000;
    static constexpr std::int64_t kKeepMs = 24LL * 60 * 60 * 1000;

    // Records the states as of `at_ms` when either differs from the last mark; returns whether a
    // mark was added. A time earlier than the last mark's (the clock was set back) is recorded at
    // the last mark's time, so the order stays and no change is lost. Marks beyond kMaxMarks, and
    // marks older than kKeepMs except the last of those (the state the kept range starts in), are
    // dropped; truncated() says so from then on.
    bool add(std::int64_t at_ms, AgentState agent, ProcessState process);

    // The marks at or after `since_ms`, led by the last one before it (the state at since_ms).
    [[nodiscard]] std::vector<StateMark> since(std::int64_t since_ms) const;
    [[nodiscard]] const std::deque<StateMark>& marks() const noexcept { return marks_; }
    [[nodiscard]] bool truncated() const noexcept { return truncated_; }
    [[nodiscard]] bool empty() const noexcept { return marks_.empty(); }

private:
    std::deque<StateMark> marks_;
    bool truncated_ = false;
};

}  // namespace agentdesk::host
