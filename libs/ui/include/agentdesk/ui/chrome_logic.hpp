#pragma once
// Portions adapted from herdr-app (Apache-2.0) and modified; see THIRD_PARTY_NOTICES.md.
// Rules behind the chrome overlays (context menus, the "Go to" picker), kept apart from the
// drawing so they can be unit-tested.
#include <string>
#include <vector>

#include "agentdesk/ui/workspace.hpp"

namespace agentdesk::ui {

// Keyboard in a context menu: the first row starts
// highlighted, Up/Down move with wrap-around, Enter runs the highlighted row. Rows are counted
// as they are drawn, so a frame's keys use the count of the frame before.
class MenuNav {
public:
    // A menu just opened.
    void open();
    // Once per frame, before the rows.
    void frame(bool up, bool down, bool enter);
    // Once per enabled row, in order. `pointed`: the pointer moved onto it (it takes the
    // highlight). Returns true when the row is highlighted; `*run` is set when Enter chose it.
    bool row(bool pointed, bool* run);
    [[nodiscard]] int at() const { return at_; }

private:
    int at_ = 0;
    int rows_ = 0;       // counted this frame
    int last_rows_ = 0;  // counted the frame before
    bool enter_ = false;
};

// One space as the picker sees it.
struct PickerSpace {
    bool status = false;   // any terminal in it has a status, idle included
    bool blocked = false;  // its status is "blocked" (one needs you)
    int tabs = 0;          // its tabs left after the filter
};

struct PickerPlan {
    std::vector<int> busy;       // spaces with a status, those that need you first
    std::vector<int> quiet;      // spaces without one
    bool earlier_row = false;    // the "earlier: N quiet spaces" row
    bool quiet_listed = false;   // the quiet spaces are listed under it
};

// busy = has a status and tabs left; quiet = no status and tabs left;
// busy sorted with blocked first (stable); the "earlier" row whenever there is a quiet space,
// its spaces listed when expanded or while filtering.
PickerPlan plan_picker(const std::vector<PickerSpace>& spaces, bool filtering, bool show_earlier);

// The space and index of the tab with id `tab_id` (tabs are closed and renamed by id).
PaneLoc find_tab(const Workspace& ws, const std::string& tab_id);

}  // namespace agentdesk::ui
