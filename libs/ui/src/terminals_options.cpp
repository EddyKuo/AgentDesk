// The Terminals layout's options and keyboard tools; see terminals_options.hpp.
#include "agentdesk/ui/terminals_options.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>

namespace agentdesk::ui::terminals {

const char* split_direction(float w, float h) {
    return w > h * kWideRatio ? "right" : "down";
}

ResizeAction resize_key(Key key, bool shift) {
    const char* dir = nullptr;
    switch (key) {
        case Key::kH:
        case Key::kLeft:
            dir = "left";
            break;
        case Key::kJ:
        case Key::kDown:
            dir = "down";
            break;
        case Key::kK:
        case Key::kUp:
            dir = "up";
            break;
        case Key::kL:
        case Key::kRight:
            dir = "right";
            break;
        case Key::kEscape:
        case Key::kEnter:
            return {true, {}};
    }
    std::string cmd = std::string("pane.resize:") + dir;
    if (shift)
        cmd += ":10";
    return {false, cmd};
}

std::optional<std::pair<std::string, float>> parse_resize(std::string_view arg) {
    const auto colon = arg.find(':');
    std::string dir(arg.substr(0, colon));
    if (colon == std::string_view::npos)
        return std::make_pair(dir, 40.0f);
    const std::string_view num = arg.substr(colon + 1);
    int v = 0;
    const auto [end, ec] = std::from_chars(num.data(), num.data() + num.size(), v);
    if (ec != std::errc() || end != num.data() + num.size() || v <= 0)
        return std::nullopt;
    return std::make_pair(dir, static_cast<float>(v));
}

float clamp_gaps(float gaps) {
    return std::clamp(gaps, 0.0f, 24.0f);
}

bool is_default(const Options& o) {
    return !o.square && o.gaps_on && clamp_gaps(o.gaps) == kDefaultGaps;
}

bool apply_options(layout::Node& tree, const Options& o) {
    const float g = o.gaps_on ? clamp_gaps(o.gaps) : 0.0f;
    for (const layout::Node* c : tree.walk()) {
        if (!c->is_block() || c->content.type != layout::Content::Type::kTab)
            continue;
        layout::Node* n = tree.find(c->id);
        if (n == nullptr)
            return false;
        if (!n->content.pane)
            n->content.pane = layout::PaneFrame{};
        n->content.pane->gap = g;
        if (o.square)
            n->content.pane->radius = 0.0f;
        // Around the bar and the panes: three quarters of the gap, on whole pixels.
        const float pad = std::round(g * 0.75f);
        tree.padding = layout::Padding{pad, pad, true};
        tree.gap = g;
        return true;
    }
    return false;
}

}  // namespace agentdesk::ui::terminals
