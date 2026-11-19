#pragma once

// A Job Object that ends every process in it when its last handle closes.

#include <windows.h>

#include "agentdesk/platform/win/unique_handle.hpp"

namespace agentdesk::platform::win {

// Job object with JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE. Breakaway is deliberately NOT allowed
// (design doc 3.2). A job manages lifetime only; it is not a security sandbox.
class KillOnCloseJob {
public:
    KillOnCloseJob();
    // agent-host's handover (design doc 7.4 OQ-09): a job another host created and duplicated
    // into this process. Throws unless it still kills its processes when closed.
    explicit KillOnCloseJob(UniqueHandle existing);

    void assign(HANDLE process) const;     // throws on failure
    void terminate(UINT exit_code) const;  // kills every process in the job
    [[nodiscard]] DWORD active_processes() const;
    [[nodiscard]] HANDLE get() const noexcept { return job_.get(); }

private:
    UniqueHandle job_;
};

}  // namespace agentdesk::platform::win
