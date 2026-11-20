// Where agent-ui's window opens, and what window.json keeps between runs.
// Portions adapted from herdr-app (Apache-2.0) and modified; see THIRD_PARTY_NOTICES.md.
#include "agentdesk/ui/window_place.hpp"

#include <windows.h>
// shellscalingapi.h needs windows.h first.
#include <shellscalingapi.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

namespace agentdesk::ui::window_place {

namespace {

using Json = nlohmann::json;

double scale_of(const Monitor& m) {
    return m.dpi > 0 ? m.dpi / 96.0 : 1.0;
}

int to_px(double dip, double scale) {
    return static_cast<int>(std::lround(dip * scale));
}

// Width and height in DIPs, kept between the minimum window and the work area.
void clamp_size(double& w, double& h, double work_w, double work_h) {
    w = std::clamp(w, std::min(kMinWidth, work_w), work_w);
    h = std::clamp(h, std::min(kMinHeight, work_h), work_h);
}

Rect centred(const Monitor& m, double w_dip, double h_dip) {
    const double s = scale_of(m);
    clamp_size(w_dip, h_dip, m.work.w / s, m.work.h / s);
    const int w = std::min(to_px(w_dip, s), m.work.w);
    const int h = std::min(to_px(h_dip, s), m.work.h);
    return {m.work.x + (m.work.w - w) / 2, m.work.y + (m.work.h - h) / 2, w, h};
}

bool contains(const Rect& r, int x, int y) {
    return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}

// Moves `r` (already no larger than `work`) so all of it is inside `work`.
Rect shift_into(Rect r, const Rect& work) {
    r.x = std::clamp(r.x, work.x, work.x + work.w - r.w);
    r.y = std::clamp(r.y, work.y, work.y + work.h - r.h);
    return r;
}

}  // namespace

Rect default_rect(const Monitor& m, float zoom) {
    const double s = scale_of(m);
    const double work_w = m.work.w / s;
    const double work_h = m.work.h / s;
    const double z = zoom > 0.0f ? zoom : 1.0;
    const double w = std::clamp(kDesignWidth * z, work_w * kMinFraction, work_w * kMaxFraction);
    const double h = std::clamp(kDesignHeight * z, work_h * kMinFraction, work_h * kMaxFraction);
    return centred(m, w, h);
}

Place restore(const std::optional<Saved>& saved, const std::vector<Monitor>& monitors, std::size_t fallback,
              float zoom) {
    const Monitor& home = monitors[std::min(fallback, monitors.size() - 1)];
    if (!saved || saved->w <= 0 || saved->h <= 0)
        return {default_rect(home, zoom), false};
    const auto same = std::find_if(monitors.begin(), monitors.end(),
                                   [&](const Monitor& m) { return m.name == saved->monitor; });
    if (same != monitors.end()) {
        const double s = scale_of(*same);
        const int x = same->bounds.x + to_px(saved->x, s);
        const int y = same->bounds.y + to_px(saved->y, s);
        if (contains(same->bounds, x, y)) {
            double w = saved->w, h = saved->h;
            clamp_size(w, h, same->work.w / s, same->work.h / s);
            const Rect r{x, y, std::min(to_px(w, s), same->work.w), std::min(to_px(h, s), same->work.h)};
            return {shift_into(r, same->work), saved->maximized};
        }
    }
    return {centred(home, saved->w, saved->h), saved->maximized};
}

Saved capture(const Rect& normal, bool maximized, const Monitor& on) {
    const double s = scale_of(on);
    return {maximized, on.name, (normal.x - on.bounds.x) / s, (normal.y - on.bounds.y) / s, normal.w / s,
            normal.h / s};
}

std::optional<Saved> parse(const std::string& json) {
    const Json j = Json::parse(json, nullptr, false);
    if (!j.is_object())
        return std::nullopt;
    const auto b = j.find("bounds");
    if (b == j.end() || !b->is_array() || b->size() != 4)
        return std::nullopt;
    for (const auto& v : *b)
        if (!v.is_number())
            return std::nullopt;
    Saved s;
    s.maximized = j.value("state", std::string()) == "maximized";
    if (const auto m = j.find("monitor"); m != j.end() && m->is_string())
        s.monitor = m->get<std::string>();
    s.x = (*b)[0].get<double>();
    s.y = (*b)[1].get<double>();
    s.w = (*b)[2].get<double>();
    s.h = (*b)[3].get<double>();
    return s;
}

std::string format(const Saved& s) {
    const auto r = [](double v) { return std::round(v * 10.0) / 10.0; };  // tenths of a DIP
    Json j{{"state", s.maximized ? "maximized" : "windowed"},
           {"monitor", s.monitor},
           {"bounds", {r(s.x), r(s.y), r(s.w), r(s.h)}}};
    return j.dump(2) + "\n";
}

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

std::optional<Saved> load(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in)
        return std::nullopt;
    std::stringstream ss;
    ss << in.rdbuf();
    return parse(ss.str());
}

void save(const std::filesystem::path& file, const Saved& s) {
    // A .tmp beside the file, then moved over it, so a crash never leaves half a file.
    auto tmp = file;
    tmp.replace_extension(L".tmp");
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out)
            return;
        out << format(s);
        if (!out)
            return;
    }
    ::MoveFileExW(tmp.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING);
}

}  // namespace agentdesk::ui::window_place
