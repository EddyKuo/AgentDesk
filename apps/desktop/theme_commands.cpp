// The theme features' glue: picking this frame's palette (the layout's theme for the appearance,
// then the tint by agent state), and their commands, so commands.cpp and frame.cpp only hand over
// to here.
#include "app_internal.hpp"

namespace agentdesk::desktop {
namespace detail {

void update_palette(App::State& s) {
    static const Palette* shown_base = nullptr;
    const auto pair = layout_theme_pair(s.settings);
    const Palette* base =
        effective_palette(resolved_appearance(s), pair.first, pair.second, s.layout().theme);
    const AmbientResult tinted = ambient_palette(s, base, false);
    if (base != shown_base) {  // another theme (or its other kind): the whole style again
        shown_base = base;
        g_pal = tinted.palette;
        apply_style(s.dpi, ui_scale(s.settings));
    } else if (tinted.palette != g_pal || tinted.recolor) {  // only the colours moved
        g_pal = tinted.palette;
        apply_style_colors();
    }
}

std::optional<std::string> exec_theme_command(App::State& s, const std::string& name,
                                              const std::string& arg) {
    if (auto r = exec_appearance_command(s, name, arg))
        return r;
    if (auto r = exec_suggest_command(s, name))
        return r;
    if (name == "theme.ambient") {
        if (arg != "off" && arg != "subtle")
            return std::string("theme.ambient is off or subtle");
        s.settings.ambient = arg;
        save_settings(s.settings);
        return std::string();
    }
    return std::nullopt;
}

}  // namespace detail
}  // namespace agentdesk::desktop
