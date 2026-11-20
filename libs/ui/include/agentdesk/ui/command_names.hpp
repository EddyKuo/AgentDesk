#pragma once
// The names of the commands agent-ui runs itself (palette, keys.json, menus, --run). Kept here,
// apart from the window, so tests can check that a menu only names real commands.
#include <string_view>

namespace agentdesk::ui {

// True for a command name AgentDesk handles itself (the part before any ":<argument>").
bool is_builtin_command(std::string_view name);

}  // namespace agentdesk::ui
