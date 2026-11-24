#pragma once

// The new host's side of a handover (agent-host --takeover): it gets every session, live, from the
// host it replaces (design doc 7.4 OQ-09, ADR-30).

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "agentdesk/host/session_manager.hpp"
#include "agentdesk/platform/pty_handles.hpp"
#include "agentdesk/terminal/bytes.hpp"

namespace agentdesk::host {

// The host we take over from and the private channel to it, which it made and passed to us.
class OldHost {
public:
    // From the value of --takeover. Windows: our two pipe ends and the old process, as handle values;
    // elsewhere the old host's pid, with the channel as fd 3.
    static std::unique_ptr<OldHost> from_takeover(const std::string& argument);
    ~OldHost();
    OldHost(const OldHost&) = delete;
    OldHost& operator=(const OldHost&) = delete;
    OldHost(OldHost&&) = delete;
    OldHost& operator=(OldHost&&) = delete;

    bool read_exact(char* out, std::size_t len);
    bool write_all(std::string_view bytes);
    // One line ("go"); nullopt when the channel ended first.
    std::optional<std::string> read_line();
    // The next terminal of the state being read; nullopt when it did not arrive.
    std::optional<platform::PtyHandles> read_handles(terminal::ByteReader& r);
    // True once the old process has exited.
    bool wait_exit(std::chrono::milliseconds timeout);

    struct Impl;

private:
    explicit OldHost(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

// Reads what the old host sent, adopts every session (nothing runs yet) and answers "ready", or
// answers "error <why>" and throws.
std::shared_ptr<SessionManager> receive_handover(HostConfig config, OldHost& old_host);
// After "ready": true once the old host said "go" or has exited (it let go of everything); false
// when it went away without letting go (then call abandon_adopted()).
bool await_handover_go(OldHost& old_host);

}  // namespace agentdesk::host
