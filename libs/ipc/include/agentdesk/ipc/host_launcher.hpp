#pragma once

// Finding or starting agent-host, and reaching a host on another machine through a command.

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "agentdesk/ipc/pipe.hpp"

namespace agentdesk::ipc {

struct HostConnection {
    std::unique_ptr<PipeClient> client;
    std::shared_ptr<void> transport;  // keeps a stream transport (ssh process) alive
    std::string error;                // why the connection failed, for the person
    std::string host_instance_id;
    std::string host_version;  // "" from a host older than the field
    // Instance ids whose sessions this host took over in a self-update (design doc 7.4 OQ-09).
    std::vector<std::string> host_succeeds;
    bool started_host = false;
    bool host_dies_with_launcher = false;  // breakaway denied inside a kill-on-close job
};

// Connects to agent-host.exe, starting it (detached) when it is not running, then performs
// the hello handshake. Returns an empty client when the host cannot be reached in time.
HostConnection connect_or_start_host(const std::filesystem::path& host_exe, const std::string& pipe_name,
                                     std::chrono::milliseconds timeout, bool allow_start = true);

// Connects through a command whose stdin/stdout carry the protocol, e.g.
//   ssh -T -o BatchMode=yes <target> agentctl bridge
// The command runs without a window inside a kill-on-close job; it ends with the connection.
// Performs the hello handshake. On failure `error` holds the command's own message (ssh says
// why: host key, password, agentctl not found).
HostConnection connect_over_command(const std::vector<std::wstring>& argv, std::chrono::milliseconds timeout);

// ssh.exe argv for a machine: `target` is user@host or an ssh config alias; `remote_ctl` is the
// remote agentctl (on PATH by default).
std::vector<std::wstring> ssh_bridge_argv(const std::string& target, const std::string& remote_ctl);

// agent-host.exe next to the current executable.
std::filesystem::path sibling_executable(const wchar_t* file_name);

}  // namespace agentdesk::ipc
