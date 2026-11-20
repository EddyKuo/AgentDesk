// The AgentDesk Mac layout's rules: menu contents, Dock items and sizes, clock text.
#include "agentdesk/ui/mac_layout.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace agentdesk::ui::mac {

namespace {
constexpr const char* kEllipsis = "\xE2\x80\xA6";
constexpr const char* kDash = " \xE2\x80\x94 ";

MenuItem item(std::string label, std::string command, bool enabled = true, bool checked = false) {
    MenuItem m;
    m.label = std::move(label);
    m.command = std::move(command);
    m.enabled = enabled;
    m.checked = checked;
    return m;
}

MenuItem separator() {
    MenuItem m;
    m.kind = MenuItem::Kind::kSeparator;
    return m;
}

MenuItem header(std::string label) {
    MenuItem m;
    m.kind = MenuItem::Kind::kHeader;
    m.label = std::move(label);
    m.enabled = false;
    return m;
}

std::string dots(const char* label) {
    return std::string(label) + kEllipsis;
}

}  // namespace

const char* menu_title(Menu m) {
    switch (m) {
        case Menu::kApp:
            return "AgentDesk";
        case Menu::kFile:
            return "File";
        case Menu::kEdit:
            return "Edit";
        case Menu::kView:
            return "View";
        case Menu::kWindow:
            return "Window";
        case Menu::kHelp:
            return "Help";
    }
    return "";
}

std::vector<MenuItem> menu_items(Menu m, const MenuContext& c) {
    const bool tab = std::any_of(c.tabs.begin(), c.tabs.end(), [](const MenuTab& t) { return t.active; });
    std::vector<MenuItem> out;
    switch (m) {
        case Menu::kApp:
            out = {item("What's new in AgentDesk", "app.whats_new"),
                   separator(),
                   item(dots("Settings"), "app.settings"),
                   item(dots("Check for updates"), "app.settings:updates"),
                   separator(),
                   item("Hide AgentDesk", "window.minimize"),
                   item("Close window", "window.close"),
                   item(dots("Stop all and quit"), "app.exit_all")};
            break;
        case Menu::kFile:
            out = {item("New tab", "tab.new"),
                   item("New space", "space.new"),
                   item(dots("Start a program"), "agent.new"),
                   separator(),
                   item("Split right", "pane.split:right", tab),
                   item("Split down", "pane.split:down", tab),
                   separator(),
                   item("Close pane", "pane.close", tab),
                   item("Close tab", "tab.close", tab)};
            break;
        case Menu::kEdit:
            out = {item("Copy", "terminal.copy", tab), item("Paste", "terminal.paste", tab),
                   item("Select all", "terminal.select_all", tab), separator(),
                   item(dots("Find"), "terminal.search", tab)};
            break;
        case Menu::kView:
            out.push_back(header("Layout"));
            for (const auto& [name, shown] : c.layouts)
                out.push_back(item(shown, "layout.switch:" + name, true, name == c.layout));
            out.push_back(separator());
            out.push_back(item("Show sidebar", "sidebar.toggle"));
            out.push_back(separator());
            out.push_back(item("Zoom in", "app.zoom_in"));
            out.push_back(item("Zoom out", "app.zoom_out"));
            out.push_back(item("Actual size", "app.zoom_reset"));
            out.push_back(separator());
            out.push_back(item("Command palette", "app.palette"));
            out.push_back(item("Keyboard shortcuts", "app.help"));
            out.push_back(item("Edit keys.json", "app.keys"));
            break;
        case Menu::kWindow: {
            out = {item("Minimize", "window.minimize"), item("Zoom", "window.maximize"),
                   item("Fill the desktop", "window.fill", true, c.fill)};
            if (!c.tabs.empty() && c.space >= 0) {
                out.push_back(separator());
                bool settled = false;
                for (std::size_t i = 0; i < c.tabs.size(); ++i) {
                    const MenuTab& t = c.tabs[i];
                    settled = settled || t.settled;
                    out.push_back(item(t.settled ? t.title + " (in the Dock)" : t.title,
                                       "tab.goto:" + std::to_string(c.space) + ":" + std::to_string(i), true,
                                       t.active));
                }
                out.push_back(item("Show all tabs", "tab.unsettle", settled));
            }
            out.push_back(separator());
            out.push_back(item(dots("Go to"), "app.picker"));
            break;
        }
        case Menu::kHelp:
            out = {item("Welcome", "app.welcome"), item("What's new", "app.whats_new"),
                   item("Keyboard shortcuts", "app.help"), item("Tips for this layout", "layout.tips"),
                   separator(),
                   item(dots("Send feedback"), "app.feedback")};
            break;
    }
    return out;
}

std::vector<DockItem> dock_items(const std::vector<std::string>& space_ids,
                                 const std::vector<std::string>& tab_ids) {
    std::vector<DockItem> out;
    for (std::size_t i = 0; i < space_ids.size(); ++i)
        out.push_back({DockItem::Kind::kSpace, static_cast<int>(i), "s:" + space_ids[i]});
    if (!space_ids.empty())
        out.push_back({DockItem::Kind::kSeparator, -1, "sep:1"});
    for (std::size_t i = 0; i < tab_ids.size(); ++i)
        out.push_back({DockItem::Kind::kTab, static_cast<int>(i), "t:" + tab_ids[i]});
    if (!tab_ids.empty())
        out.push_back({DockItem::Kind::kSeparator, -1, "sep:2"});
    out.push_back({DockItem::Kind::kNew, -1, "new"});
    out.push_back({DockItem::Kind::kSettings, -1, "settings"});
    return out;
}

DockGeometry dock_geometry(const std::vector<DockItem>& items, float tile, float gap, float pad) {
    DockGeometry g;
    float x = pad;
    for (std::size_t i = 0; i < items.size(); ++i) {
        const float w = items[i].tile() ? tile : 1.0f;
        g.x.push_back(x);
        g.w.push_back(w);
        x += w + (i + 1 < items.size() ? gap : 0.0f);
    }
    g.width = x + pad;
    return g;
}

float dock_target_scale(const std::vector<DockItem>& items, int item, int hovered) {
    const int n = static_cast<int>(items.size());
    if (hovered < 0 || hovered >= n || item < 0 || item >= n ||
        !items[static_cast<std::size_t>(item)].tile() || !items[static_cast<std::size_t>(hovered)].tile())
        return 1.0f;
    // Distance in tiles: separators between them do not count.
    const int lo = std::min(item, hovered);
    const int hi = std::max(item, hovered);
    int d = 0;
    for (int i = lo + 1; i <= hi; ++i)
        d += items[static_cast<std::size_t>(i)].tile() ? 1 : 0;
    return d == 0 ? 1.25f : d == 1 ? 1.1f : 1.0f;
}

float approach(float current, float target, float dt) {
    const float k = 1.0f - std::exp(-std::max(0.0f, dt) * 20.0f);
    const float next = current + (target - current) * k;
    return std::abs(target - next) < 0.002f ? target : next;
}

std::string clock_text(int weekday, int hour, int minute) {
    static constexpr std::array<const char*, 7> kDays = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    char buf[32];
    std::snprintf(buf, sizeof buf, "%s %02d:%02d", kDays[static_cast<std::size_t>(((weekday % 7) + 7) % 7)],
                  std::clamp(hour, 0, 23), std::clamp(minute, 0, 59));
    return buf;
}

long long ms_to_next_minute(long long ms_into_minute) {
    const long long into = ((ms_into_minute % 60000) + 60000) % 60000;
    return 60000 - into;
}

AgentCounts count_agents(const std::vector<Attention>& states) {
    AgentCounts c;
    for (const Attention a : states) {
        c.needs += a == Attention::kBlocked ? 1 : 0;
        c.working += a == Attention::kWorking ? 1 : 0;
    }
    return c;
}

std::string window_title(const std::string& space, const std::string& tab) {
    if (space.empty())
        return tab;
    if (tab.empty())
        return space;
    return space + kDash + tab;
}

}  // namespace agentdesk::ui::mac
