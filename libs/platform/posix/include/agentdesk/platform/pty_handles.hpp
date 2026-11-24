#pragma once

// What agent-host's handover passes to the new host for one terminal on POSIX systems (design doc
// ADR-30); the Windows platform has its own header with this name.

#include <cstdint>

namespace agentdesk::platform {

// The pty master (passed with SCM_RIGHTS) and the root process. The fd stays owned by the Pty that
// handed it out.
struct PtyHandles {
    int master_fd = -1;
    std::uint32_t pid = 0;
};

}  // namespace agentdesk::platform
