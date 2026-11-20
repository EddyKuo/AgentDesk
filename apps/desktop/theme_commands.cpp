// The theme feature commands, in one place so commands.cpp only hands them over:
// appearance:<mode>, appearance.toggle, and the theme.* commands.
#include "app_internal.hpp"

namespace agentdesk::desktop {
namespace detail {

std::optional<std::string> exec_theme_command(App::State& s, const std::string& name,
                                              const std::string& arg) {
    if (auto r = exec_appearance_command(s, name, arg))
        return r;
    if (auto r = exec_suggest_command(s, name))
        return r;
    return std::nullopt;
}

}  // namespace detail
}  // namespace agentdesk::desktop
