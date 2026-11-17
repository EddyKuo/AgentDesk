#pragma once

#include <memory>

#include "agentdesk/host/input.hpp"
#include "agentdesk/terminal/vt_engine.hpp"

namespace agentdesk::ui {

// What a TerminalView needs from a session. M0: in-process host::Session. M2+: IPC client.
class TerminalSource {
public:
    virtual ~TerminalSource() = default;
    [[nodiscard]] virtual std::shared_ptr<const terminal::ScreenSnapshot> snapshot() const = 0;
    virtual void send(host::InputEvent input) = 0;
    virtual void resize(int rows, int cols) = 0;
    [[nodiscard]] virtual bool can_input() const = 0;  // false: read-only (not controller / exited)
};

}  // namespace agentdesk::ui
