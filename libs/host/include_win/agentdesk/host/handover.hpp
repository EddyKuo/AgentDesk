#pragma once

// The new host's side of a handover on Windows (main, `agent-host --takeover`): anonymous pipes
// to and from the old host, which duplicated every terminal's handles into this process.
// libs/host/include_posix has the macOS version of this header.

#include <windows.h>

#include <memory>

#include "agentdesk/host/session_manager.hpp"

namespace agentdesk::host {

// Reads what the old host sent on `from_old`, adopts every session (nothing runs yet) and
// answers "ready" on `to_old`, or answers "error <why>" and throws.
std::shared_ptr<SessionManager> receive_handover(HostConfig config, HANDLE from_old, HANDLE to_old);
// After "ready": true once the old host said "go" or has exited (it let go of everything);
// false when it went away without letting go (then call abandon_adopted()).
bool await_handover_go(HANDLE from_old, HANDLE old_process);

}  // namespace agentdesk::host
