// Layout styles: the mod name -> style table and the per-style defaults; see layout_styles.hpp.
#include "agentdesk/ui/layout_styles.hpp"

#include <array>

namespace agentdesk::ui::styles {

namespace {
struct ModStyle {
    std::string_view mod;
    std::string_view style;
};

// Every mod name a style's renderer draws, and the style it selects.
constexpr std::array<ModStyle, 16> kModStyles{{
    {"mac-menubar", "mac"},
    {"mac-dock", "mac"},
    {"mac-window-title", "mac"},
    {"terminals-bar", "terminals"},
    {"terminals-pane", "terminals"},
    {"classic-menubar", "classic"},
    {"classic-tabs", "classic"},
    {"classic-status", "classic"},
    {"classic-sidebar", "classic"},
    {"classic-pane", "classic"},
    {"tiling-bar", "tiling"},
    {"tiling-pane", "tiling"},
    {"cockpit-header", "cockpit"},
    {"cockpit-gauges", "cockpit"},
    {"cockpit-roster", "cockpit"},
    {"cockpit-log", "cockpit"},
}};

struct Named {
    std::string_view layout;
    std::string_view shown;
};
constexpr std::array<Named, 6> kNames{{{"workspace", "AgentDesk"},
                                       {"terminals", "Terminals"},
                                       {"mac", "AgentDesk Mac"},
                                       {"classic", "AgentDesk Classic"},
                                       {"tiling", "AgentDesk Tiling"},
                                       {"cockpit", "AgentDesk Cockpit"}}};
}  // namespace

std::string_view style_of_mod(std::string_view mod) {
    for (const auto& m : kModStyles)
        if (m.mod == mod)
            return m.style;
    return {};
}

std::string style_of_tree(const layout::Node& tree) {
    std::string first;
    for (const layout::Node* n : tree.walk()) {
        std::string_view found = style_of_mod(n->mod());
        if (found.empty() && n->is_block() && n->content.type == layout::Content::Type::kTab &&
            n->content.pane && n->content.pane->header)
            found = style_of_mod(n->content.pane->header->mod());
        if (found == "mac")
            return "mac";
        if (first.empty())
            first = std::string(found);
    }
    return first;
}

std::string display_name(std::string_view layout) {
    for (const auto& n : kNames)
        if (n.layout == layout)
            return std::string(n.shown);
    return std::string(layout);
}

bool has_clock(std::string_view style) {
    return style == "mac" || style == "classic" || style == "tiling" || style == "cockpit";
}

PaneLook pane_look(std::string_view style, const layout::PaneFrame* frame) {
    PaneLook l;
    if (style == "terminals") {
        l.radius = 12.0f;
        l.header = 38.0f;
    } else if (style == "tiling") {
        l.radius = 0.0f;
        l.focus = {2.0f, "accent"};
        l.header = 18.0f;
    } else if (style == "classic") {
        l.radius = 0.0f;
        l.header = 20.0f;
    } else if (style == "cockpit") {
        l.radius = 4.0f;
    }
    if (frame == nullptr)
        return l;
    if (frame->radius)
        l.radius = *frame->radius;
    if (frame->border)
        l.border = *frame->border;
    if (frame->focus)
        l.focus = *frame->focus;
    if (frame->header && frame->header->height)
        l.header = *frame->header->height;
    if (frame->gap)
        l.gap = *frame->gap;
    return l;
}

ToastArea toast_area(std::string_view style, float bottom_block) {
    if (style == "mac")
        return {true, 28.0f + 10.0f};  // under the menu bar
    if (style == "classic")
        return {false, 22.0f};  // over the status line
    if (style == "cockpit")
        return {false, bottom_block};
    return {};
}

bool effects_allowed(std::string_view setting, bool light_theme, bool high_contrast) {
    return setting != "off" && !light_theme && !high_contrast;
}

std::optional<std::pair<std::string, std::string>> suggested_themes(std::string_view style) {
    if (style == "classic")
        return std::make_pair(std::string("phosphor-green"), std::string("phosphor-paper"));
    if (style == "cockpit")
        return std::make_pair(std::string("cockpit-night"), std::string("cockpit-day"));
    return std::nullopt;
}

}  // namespace agentdesk::ui::styles
