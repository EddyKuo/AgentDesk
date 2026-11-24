// macOS (ADR-30): the connected screens for window_place. AppKit works in points with the origin
// at the bottom-left of the main screen; window_place works top-down, so y is flipped against the
// main screen's height. Sizes stay in points and the DPI is reported as 96: a point is already
// the "DIP" window_place scales by, and the window itself is sized in points.
#import <AppKit/AppKit.h>

#include <cmath>

#include "agentdesk/ui/window_place.hpp"

namespace agentdesk::ui::window_place {

namespace {

Rect flipped(NSRect r, CGFloat main_height) {
    return {static_cast<int>(std::lround(r.origin.x)),
            static_cast<int>(std::lround(main_height - r.origin.y - r.size.height)),
            static_cast<int>(std::lround(r.size.width)), static_cast<int>(std::lround(r.size.height))};
}

}  // namespace

std::vector<Monitor> monitors(std::size_t* under_cursor) {
    std::vector<Monitor> list;
    std::size_t cursor_index = 0;
    @autoreleasepool {
        NSArray<NSScreen*>* screens = [NSScreen screens];
        const CGFloat main_height = screens.count > 0 ? screens[0].frame.size.height : 0;
        const NSPoint mouse = [NSEvent mouseLocation];
        for (NSScreen* screen in screens) {
            Monitor m;
            m.name = screen.localizedName != nil ? std::string(screen.localizedName.UTF8String) : std::string();
            m.bounds = flipped(screen.frame, main_height);
            m.work = flipped(screen.visibleFrame, main_height);
            m.dpi = 96;
            if (NSPointInRect(mouse, screen.frame))
                cursor_index = list.size();
            list.push_back(std::move(m));
        }
    }
    if (list.empty()) {  // no screen reported: a plain 1440x900
        Monitor m;
        m.bounds = m.work = {0, 0, 1440, 900};
        list.push_back(m);
    }
    if (under_cursor != nullptr)
        *under_cursor = cursor_index;
    return list;
}

const Monitor& monitor_for(const Rect& r, const std::vector<Monitor>& list) {
    // The screen with the largest overlap; the nearest centre when none overlaps.
    const Monitor* best = &list.front();
    long best_area = -1;
    double best_dist = 1e300;
    const double cx = r.x + r.w / 2.0;
    const double cy = r.y + r.h / 2.0;
    for (const auto& m : list) {
        const int x0 = std::max(r.x, m.bounds.x);
        const int y0 = std::max(r.y, m.bounds.y);
        const int x1 = std::min(r.x + r.w, m.bounds.x + m.bounds.w);
        const int y1 = std::min(r.y + r.h, m.bounds.y + m.bounds.h);
        const long area = x1 > x0 && y1 > y0 ? static_cast<long>(x1 - x0) * (y1 - y0) : 0;
        const double dx = cx - (m.bounds.x + m.bounds.w / 2.0);
        const double dy = cy - (m.bounds.y + m.bounds.h / 2.0);
        const double dist = dx * dx + dy * dy;
        if (area > best_area || (area == 0 && best_area == 0 && dist < best_dist)) {
            best = &m;
            best_area = area;
            best_dist = dist;
        }
    }
    return *best;
}

}  // namespace agentdesk::ui::window_place
