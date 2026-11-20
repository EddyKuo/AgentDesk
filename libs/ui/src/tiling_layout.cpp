// The Tiling layout's rules; see tiling_layout.hpp.
#include "agentdesk/ui/tiling_layout.hpp"

#include <algorithm>
#include <charconv>

namespace agentdesk::ui::tiling {

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

bool apply_gaps(layout::Node& tree, float gaps, bool on) {
    const float g = on ? clamp_gaps(gaps) : 0.0f;
    for (const layout::Node* c : tree.walk()) {
        if (!c->is_block() || c->content.type != layout::Content::Type::kTab)
            continue;
        layout::Node* n = tree.find(c->id);
        if (n == nullptr)
            return false;
        n->padding = layout::Padding{g, g, true};
        if (!n->content.pane)
            n->content.pane = layout::PaneFrame{};
        n->content.pane->gap = g;
        return true;
    }
    return false;
}

bool show_titles(std::string_view mode, int panes) {
    if (mode == "always")
        return true;
    if (mode == "never")
        return false;
    return panes >= 2;
}

BarFit fit_bar(const BarWidths& w, float available) {
    BarFit f;
    auto used = [&] {
        return w.lead + (f.short_tabs ? w.tabs_short : w.tabs_full) + (f.title ? w.title : 0.0f) + w.counts +
               (f.clock ? w.clock : 0.0f);
    };
    if (used() > available)
        f.title = false;
    if (used() > available)
        f.short_tabs = true;
    if (used() > available)
        f.clock = false;
    f.tabs_room = std::max(0.0f, available - (used() - (f.short_tabs ? w.tabs_short : w.tabs_full)));
    return f;
}

SpaceChips space_chips(int count, int active) {
    SpaceChips c;
    for (int i = 0; i < count && i < 9; ++i)
        c.shown.push_back(i);
    if (active >= 9 && active < count)
        c.shown.push_back(active);
    c.more = count - static_cast<int>(c.shown.size());
    return c;
}

}  // namespace agentdesk::ui::tiling
