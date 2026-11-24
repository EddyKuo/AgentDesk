#pragma once

// The new host's side of a handover on macOS (ADR-30; main_posix, `agent-host --takeover`):
// `channel` is the socket shared with the old host, both directions, and carries the terminals'
// pty masters as SCM_RIGHTS. libs/host/include_win has the Windows version of this header.

#include <cstdint>
#include <memory>

#include "agentdesk/host/session_manager.hpp"

namespace agentdesk::host {

// Reads what the old host sent, adopts every session (nothing runs yet) and answers "ready", or
// answers "error <why>" and throws.
std::shared_ptr<SessionManager> receive_handover(HostConfig config, int channel);
// After "ready": true once the old host said "go" or has exited (it let go of everything);
// false when it went away without letting go (then call abandon_adopted()).
bool await_handover_go(int channel, std::uint32_t old_pid);

}  // namespace agentdesk::host
