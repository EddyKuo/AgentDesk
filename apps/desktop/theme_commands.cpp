// The theme features' glue: picking this frame's palette (the layout's theme for the appearance,
// the theme editor's draft in its place while the editor is open, then the tint by agent state),
// and their commands, so commands.cpp and frame.cpp only hand over to here.
#include "app_internal.hpp"

namespace agentdesk::desktop {
namespace detail {

void update_palette(App::State& s) {
    static const Palette* shown_base = nullptr;
    const auto pair = layout_theme_pair(s.settings);
    const Palette* base =
        effective_palette(resolved_appearance(s), pair.first, pair.second, s.layout().theme);
    bool draft_changed = false;
    if (const Palette* draft = theme_editor_draft(s, &draft_changed))
        base = draft;  // the whole window previews the theme being edited
    const AmbientResult tinted = ambient_palette(s, base, theme_editor_open(s));
    if (base != shown_base) {  // another theme (or its other kind): the whole style again
        shown_base = base;
        g_pal = tinted.palette;
        apply_style(s.dpi, ui_scale(s.settings));
    } else if (tinted.palette != g_pal || tinted.recolor || draft_changed) {  // only the colours moved
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
    if (auto r = exec_theme_editor_command(s, name, arg))
        return r;
    return std::nullopt;
}

}  // namespace detail
}  // namespace agentdesk::desktop
