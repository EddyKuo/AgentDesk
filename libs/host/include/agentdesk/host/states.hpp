#pragma once

// The two independent session states: the process lifecycle and the agent status.

#include <optional>
#include <string_view>

#include "agentdesk/terminal/program_status.hpp"

namespace agentdesk::host {

// Process and agent state are separate (design doc 3.6, ADR-06): a running process is not a
// working agent, and silence is never "done".
enum class ProcessState { kStarting, kRunning, kStopping, kExited, kFailed, kLost };
enum class AgentState { kUnknown, kIdle, kWorking, kBlocked, kDone, kError };

std::string_view to_string(ProcessState s);
std::string_view to_string(AgentState s);
std::optional<ProcessState> parse_process_state(std::string_view s);
std::optional<AgentState> parse_agent_state(std::string_view s);  // also accepts lowercase wire names

AgentState from_status(terminal::StatusState s);

}  // namespace agentdesk::host
