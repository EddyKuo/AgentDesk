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
// clang-format off
constexpr std::array<ModStyle, 18> kModStyles{{
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
    {"focus-strip", "focus"},  // Focus mode's tree
    {"mission-bar", "mission"},
    {"mission-wall", "mission"},
    {"review-head", "review"},
    {"review-changes", "review"},
    {"review-commit", "review"},
    {"garden-bar", "garden"},
    {"garden-scene", "garden"},
}};
// clang-format on

struct Named {
    std::string_view layout;
    std::string_view shown;
};
constexpr std::array<Named, 7> kNames{{{"workspace", "AgentDesk"},
                                       {"terminals", "Terminals"},
                                       {"mac", "AgentDesk Mac"},
                                       {"mission", "Mission Control"},
                                       {"review", "Review"},
                                       {"classic", "AgentDesk Classic"},
                                       {"garden", "Garden"}}};

// The fun layouts.
constexpr std::array<std::string_view, 2> kFunLayouts{{"classic", "garden"}};
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

bool is_fun_layout(std::string_view layout) {
    for (const auto f : kFunLayouts)
        if (f == layout)
            return true;
    return false;
}

bool is_listed(std::string_view layout, bool fun_layouts, std::string_view current) {
    return fun_layouts || layout == current || !is_fun_layout(layout);
}

std::string next_listed(const std::vector<std::string>& names, std::string_view current, bool fun_layouts) {
    const auto n = names.size();
    std::size_t at = n;
    for (std::size_t i = 0; i < n; ++i)
        if (names[i] == current)
            at = i;
    if (at == n) {
        for (const auto& name : names)
            if (is_listed(name, fun_layouts))
                return name;
        return names.empty() ? std::string() : names.front();
    }
    for (std::size_t k = 1; k <= n; ++k) {
        const std::string& cand = names[(at + k) % n];
        // The current one only counts as listed for its own card, not for the cycle.
        if (is_listed(cand, fun_layouts))
            return cand;
    }
    return std::string(current);
}

std::optional<Replacement> replacement_for(std::string_view removed_layout) {
    if (removed_layout == "tiling")
        return Replacement{"terminals", true};
    if (removed_layout == "cockpit")
        return Replacement{"workspace", false};
    return std::nullopt;
}

bool has_clock(std::string_view style) {
    return style == "mac" || style == "classic";
}

PaneLook pane_look(std::string_view style, const layout::PaneFrame* frame) {
    PaneLook l;
    if (style == "terminals") {
        l.radius = 12.0f;
        l.header = 38.0f;
    } else if (style == "classic") {
        l.radius = 0.0f;
        l.header = 20.0f;
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
    return {false, bottom_block};
}

bool effects_allowed(std::string_view setting, bool light_theme, bool high_contrast) {
    return setting != "off" && !light_theme && !high_contrast;
}

}  // namespace agentdesk::ui::styles
