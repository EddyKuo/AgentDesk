#pragma once

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>

#include "agentdesk/ipc/pipe.hpp"

namespace agentdesk::ipc {

struct HostConnection {
    std::unique_ptr<PipeClient> client;
    std::string host_instance_id;
    bool started_host = false;
    bool host_dies_with_launcher = false;  // breakaway denied inside a kill-on-close job
};

// Connects to agent-host.exe, starting it (detached) when it is not running, then performs
// the hello handshake. Returns an empty client when the host cannot be reached in time.
HostConnection connect_or_start_host(const std::filesystem::path& host_exe, const std::wstring& pipe_name,
                                     std::chrono::milliseconds timeout, bool allow_start = true);

// agent-host.exe next to the current executable.
std::filesystem::path sibling_executable(const wchar_t* file_name);

}  // namespace agentdesk::ipc
