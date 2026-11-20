// The command names agent-ui runs itself; see command_names.hpp.
#include "agentdesk/ui/command_names.hpp"

#include <set>
#include <string>

namespace agentdesk::ui {

bool is_builtin_command(std::string_view name) {
    // clang-format off
    static const std::set<std::string, std::less<>> kNames = {
        "agent.focus", "agent.keep", "agent.new", "agent.next", "agent.peek", "agent.prev", "app.exit_all",
        "app.feedback", "app.help", "app.keys", "app.palette", "app.picker", "app.reset", "app.settings",
        "app.welcome", "app.whats_new", "app.zoom_in", "app.zoom_out", "app.zoom_reset", "details.toggle",
        "launch", "layout", "layout.clone", "layout.reset", "layout.switch", "layout.toggle", "machine.add",
        "mod", "mod.clone", "mod.open", "mods.folder", "mods.reload", "pane.close", "pane.go", "pane.park",
        "pane.rename", "pane.resize", "pane.split", "pane.swap", "pane.zoom", "panel", "session.focus",
        "session.force_stop", "session.interrupt", "session.restart", "session.stop", "sidebar.rail",
        "sidebar.toggle", "space.close", "space.move", "space.new", "space.next", "space.prev", "space.rename",
        "space.select", "tab.close", "tab.goto", "tab.move", "tab.new", "tab.next", "tab.prev", "tab.rename",
        "tab.select", "tab.settle", "tab.unsettle", "terminal.copy", "terminal.kill", "terminal.open",
        "terminal.park", "terminal.paste", "terminal.paste_image", "terminal.peek", "terminal.pin",
        "terminal.right_click", "terminal.scroll", "terminal.search", "terminal.select_all", "theme",
        "window.close", "window.fill", "window.maximize", "window.minimize", "worktree.new", "worktree.open",
        "worktree.remove", "app.quit", "overlay.close", "app.check_updates", "app.update_core", "app.update",
        // Theme features: appearance by schedule, suggested themes, tint by agent state, the editor.
        "appearance", "appearance.toggle", "theme.suggested", "theme.ambient", "theme.new", "theme.edit",
        "theme.folder", "theme.delete"};
    // clang-format on
    return kNames.contains(name);
}

}  // namespace agentdesk::ui
