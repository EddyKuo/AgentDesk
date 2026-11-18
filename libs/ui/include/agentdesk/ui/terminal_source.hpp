#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "agentdesk/host/input.hpp"
#include "agentdesk/terminal/vt_engine.hpp"

namespace agentdesk::ui {

// One scrollback line as text (history.read returns text; design doc 3.7).
struct HistoryLineText {
    std::uint64_t line_id = 0;
    std::string text;
    bool wrapped = false;  // soft-wrapped: continues on the next line
};

// What a TerminalView needs from a session. Implemented by RemoteSession (IPC to agent-host).
class TerminalSource {
public:
    virtual ~TerminalSource() = default;
    [[nodiscard]] virtual std::shared_ptr<const terminal::ScreenSnapshot> snapshot() const = 0;
    virtual void send(host::InputEvent input) = 0;
    virtual void resize(int rows, int cols) = 0;
    [[nodiscard]] virtual bool can_input() const = 0;  // false: read-only (not controller / exited)

    // Scrollback: ask for the newest `lines` history lines; they arrive asynchronously.
    virtual void request_history(std::size_t /*lines*/) {}
    // The terminal's current folder on this computer (links to relative paths); empty: unknown.
    [[nodiscard]] virtual std::string cwd() const { return {}; }
    [[nodiscard]] virtual std::shared_ptr<const std::vector<HistoryLineText>> history() const {
        return nullptr;
    }
};

}  // namespace agentdesk::ui
