// StateTimeline: the bounded list of state changes behind session.timeline.
#include "agentdesk/host/state_timeline.hpp"

#include <algorithm>

namespace agentdesk::host {

bool StateTimeline::add(std::int64_t at_ms, AgentState agent, ProcessState process) {
    if (!marks_.empty()) {
        const StateMark& last = marks_.back();
        if (last.agent == agent && last.process == process)
            return false;
        at_ms = std::max(at_ms, last.at_ms);
    }
    marks_.push_back({at_ms, agent, process});
    while (marks_.size() > kMaxMarks) {
        marks_.pop_front();
        truncated_ = true;
    }
    // Older than a day: gone, but the newest of them stays as the state the day starts in.
    const std::int64_t cutoff = at_ms - kKeepMs;
    while (marks_.size() >= 2 && marks_[1].at_ms < cutoff) {
        marks_.pop_front();
        truncated_ = true;
    }
    return true;
}

std::vector<StateMark> StateTimeline::since(std::int64_t since_ms) const {
    auto first =
        std::find_if(marks_.begin(), marks_.end(), [&](const StateMark& m) { return m.at_ms >= since_ms; });
    if (first != marks_.begin() && (first == marks_.end() || first->at_ms > since_ms))
        --first;  // the state since_ms falls in
    return {first, marks_.end()};
}

}  // namespace agentdesk::host
