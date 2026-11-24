#pragma once

#include <mutex>
#include <optional>

#include <sys/types.h>

#include "agentdesk/platform/process.hpp"
#include "posix_util.hpp"

namespace agentdesk::platform {

struct Process::Impl {
    pid_t pid = -1;
    bool own_group = false;
    posix::UniqueFd in_write;  // child's stdin
    posix::UniqueFd out_read;  // child's stdout (and stderr when merged)
    std::mutex mutex;
    std::optional<int> exit_code;

    // Reaps the child without blocking; true once it has exited.
    bool poll_exit();
    void signal(int sig) const;
};

}  // namespace agentdesk::platform
