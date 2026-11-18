#include "agentdesk/ui/chrome_logic.hpp"

#include <algorithm>

namespace agentdesk::ui {

void MenuNav::open() {
    at_ = 0;
    rows_ = 0;
    last_rows_ = 0;
    enter_ = false;
}

// herdr menu/main.js 132-134: down = (at + 1) % n, up = (at + n - 1) % n, enter = choose(at).
void MenuNav::frame(bool up, bool down, bool enter) {
    last_rows_ = rows_;
    rows_ = 0;
    const int n = last_rows_;
    if (n > 0) {
        at_ = std::clamp(at_, 0, n - 1);
        if (down)
            at_ = (at_ + 1) % n;
        if (up)
            at_ = (at_ + n - 1) % n;
    }
    enter_ = enter && n > 0;
}

bool MenuNav::row(bool pointed, bool* run) {
    const int i = rows_++;
    if (pointed)
        at_ = i;
    const bool on = i == at_;
    if (run != nullptr)
        *run = on && enter_;
    return on;
}

PickerPlan plan_picker(const std::vector<PickerSpace>& spaces, bool filtering, bool show_earlier) {
    PickerPlan p;
    for (int i = 0; i < static_cast<int>(spaces.size()); ++i) {
        const auto& s = spaces[static_cast<std::size_t>(i)];
        if (s.tabs <= 0)
            continue;
        (s.status ? p.busy : p.quiet).push_back(i);
    }
    // busy.sort((a, b) => (b.w.status === "blocked") - (a.w.status === "blocked")): stable.
    std::stable_sort(p.busy.begin(), p.busy.end(), [&](int a, int b) {
        return spaces[static_cast<std::size_t>(a)].blocked && !spaces[static_cast<std::size_t>(b)].blocked;
    });
    p.earlier_row = !p.quiet.empty();
    p.quiet_listed = p.earlier_row && (show_earlier || filtering);
    return p;
}

PaneLoc find_tab(const Workspace& ws, const std::string& tab_id) {
    for (int s = 0; s < static_cast<int>(ws.spaces.size()); ++s) {
        const auto& tabs = ws.spaces[static_cast<std::size_t>(s)].tabs;
        for (int t = 0; t < static_cast<int>(tabs.size()); ++t)
            if (tabs[static_cast<std::size_t>(t)].id == tab_id)
                return {s, t};
    }
    return {};
}

}  // namespace agentdesk::ui
