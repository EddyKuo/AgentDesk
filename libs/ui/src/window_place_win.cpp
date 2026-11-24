// The connected monitors for window_place on Windows (macOS: macos/window_place_macos.mm).
#include <windows.h>
// shellscalingapi.h needs windows.h first.
#include <shellscalingapi.h>

#include "agentdesk/ui/window_place.hpp"

namespace agentdesk::ui::window_place {

std::vector<Monitor> monitors(std::size_t* under_cursor) {
    struct Found {
        std::vector<Monitor> list;
        HMONITOR cursor = nullptr;
        std::size_t cursor_index = 0;
    } found;
    POINT pt{};
    ::GetCursorPos(&pt);
    found.cursor = ::MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY);
    ::EnumDisplayMonitors(
        nullptr, nullptr,
        [](HMONITOR h, HDC, LPRECT, LPARAM data) -> BOOL {
            auto& f = *reinterpret_cast<Found*>(data);  // the Found passed below
            MONITORINFOEXW mi{};
            mi.cbSize = sizeof(mi);
            if (::GetMonitorInfoW(h, &mi) == FALSE)
                return TRUE;
            UINT dx = 96, dy = 96;
            if (FAILED(::GetDpiForMonitor(h, MDT_EFFECTIVE_DPI, &dx, &dy)))
                dx = 96;
            Monitor m;
            const int n = ::WideCharToMultiByte(CP_UTF8, 0, mi.szDevice, -1, nullptr, 0, nullptr, nullptr);
            if (n > 1) {
                m.name.resize(static_cast<std::size_t>(n - 1));
                ::WideCharToMultiByte(CP_UTF8, 0, mi.szDevice, -1, m.name.data(), n, nullptr, nullptr);
            }
            const auto& b = mi.rcMonitor;
            const auto& w = mi.rcWork;
            m.bounds = {b.left, b.top, b.right - b.left, b.bottom - b.top};
            m.work = {w.left, w.top, w.right - w.left, w.bottom - w.top};
            m.dpi = dx;
            if (h == f.cursor)
                f.cursor_index = f.list.size();
            f.list.push_back(std::move(m));
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&found));
    if (found.list.empty()) {  // no monitor reported: a plain 1920x1080 at 100 %
        Monitor m;
        m.bounds = m.work = {0, 0, 1920, 1080};
        found.list.push_back(m);
    }
    if (under_cursor != nullptr)
        *under_cursor = found.cursor_index;
    return found.list;
}

const Monitor& monitor_for(const Rect& r, const std::vector<Monitor>& list) {
    const RECT rc{r.x, r.y, r.x + r.w, r.y + r.h};
    MONITORINFOEXW mi{};
    mi.cbSize = sizeof(mi);
    if (::GetMonitorInfoW(::MonitorFromRect(&rc, MONITOR_DEFAULTTONEAREST), &mi) != FALSE)
        for (const auto& m : list)
            if (m.bounds == Rect{mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
                                 mi.rcMonitor.bottom - mi.rcMonitor.top})
                return m;
    return list.front();
}

}  // namespace agentdesk::ui::window_place
