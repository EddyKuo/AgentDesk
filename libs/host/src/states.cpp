// Names of the process and agent states, as used in the protocol and in sessions.json.
#include "agentdesk/host/states.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <string>

namespace agentdesk::host {

namespace {

constexpr std::array<std::string_view, 6> kProcessNames = {"Starting", "Running", "Stopping",
                                                           "Exited",   "Failed",  "Lost"};
constexpr std::array<std::string_view, 6> kAgentNames = {"Unknown", "Idle", "Working",
                                                         "Blocked", "Done", "Error"};

bool iequals(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) ==
                      std::tolower(static_cast<unsigned char>(y));
           });
}

}  // namespace

std::string_view to_string(ProcessState s) {
    return kProcessNames[static_cast<std::size_t>(s)];
}

std::string_view to_string(AgentState s) {
    return kAgentNames[static_cast<std::size_t>(s)];
}

std::optional<ProcessState> parse_process_state(std::string_view s) {
    for (std::size_t i = 0; i < kProcessNames.size(); ++i) {
        if (iequals(s, kProcessNames[i]))
            return static_cast<ProcessState>(i);
    }
    return std::nullopt;
}

std::optional<AgentState> parse_agent_state(std::string_view s) {
    for (std::size_t i = 0; i < kAgentNames.size(); ++i) {
        if (iequals(s, kAgentNames[i]))
            return static_cast<AgentState>(i);
    }
    return std::nullopt;
}

AgentState from_status(terminal::StatusState s) {
    switch (s) {
        case terminal::StatusState::kIdle:
            return AgentState::kIdle;
        case terminal::StatusState::kWorking:
            return AgentState::kWorking;
        case terminal::StatusState::kDone:
            return AgentState::kDone;
        case terminal::StatusState::kBlocked:
            return AgentState::kBlocked;
        case terminal::StatusState::kError:
            return AgentState::kError;
        case terminal::StatusState::kClear:
            return AgentState::kUnknown;
    }
    return AgentState::kUnknown;
}

}  // namespace agentdesk::host
