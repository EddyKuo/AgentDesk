#pragma once
// Portions adapted from herdr-app (Apache-2.0) and modified; see THIRD_PARTY_NOTICES.md.
// Where agent-ui's window opens: last time's place when it still fits a connected monitor,
// otherwise a size that suits the monitor (its work area, DPI and the person's zoom).
// The rules are pure so they can be unit-tested; monitors() and the file helpers touch Windows.
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace agentdesk::ui::window_place {

// Physical pixels in virtual-screen coordinates (what a Per-Monitor-V2 window sees).
struct Rect {
    int x = 0, y = 0, w = 0, h = 0;
    bool operator==(const Rect&) const = default;
};

struct Monitor {
    std::string name;  // device name, e.g. \\.\DISPLAY1
    Rect bounds;
    Rect work;          // bounds minus the taskbar
    unsigned dpi = 96;  // effective DPI: 96 = 100 %, 144 = 150 %
};

// What window.json keeps. Sizes are in DIPs (1/96 inch) so they survive a scale change;
// x, y are the window's offset from its monitor's top-left corner, also in DIPs.
struct Saved {
    bool maximized = false;
    std::string monitor;
    double x = 0, y = 0, w = 0, h = 0;
};

struct Place {
    Rect rect;  // the normal (not maximized) rect
    bool maximized = false;
};

// Size of a first start, before scaling: the layouts are designed around this window.
inline constexpr double kDesignWidth = 1280;
inline constexpr double kDesignHeight = 820;
// Never smaller than this (DIPs), and never larger than the work area.
inline constexpr double kMinWidth = 480;
inline constexpr double kMinHeight = 320;
// A computed default stays between these fractions of the work area: a big desktop does not get
// a postage stamp, a small laptop screen keeps a margin around the window.
inline constexpr double kMinFraction = 0.5;
inline constexpr double kMaxFraction = 0.9;

// The default window for `m`: the design size times `zoom`, kept within kMinFraction..kMaxFraction
// of the work area, centred in it.
[[nodiscard]] Rect default_rect(const Monitor& m, float zoom);

// The window to open: `saved` on its monitor when that monitor is still connected and the saved
// corner lies on it; otherwise the saved size (or the default) centred on monitors[fallback].
// `monitors` must not be empty; fallback is clamped to it.
[[nodiscard]] Place restore(const std::optional<Saved>& saved, const std::vector<Monitor>& monitors,
                            std::size_t fallback, float zoom);

// What to keep for a window whose normal rect is `normal`, on the monitor `on`.
[[nodiscard]] Saved capture(const Rect& normal, bool maximized, const Monitor& on);

[[nodiscard]] std::optional<Saved> parse(const std::string& json);
[[nodiscard]] std::string format(const Saved& s);

// Windows side: the connected monitors, and the index of the one under the mouse pointer.
[[nodiscard]] std::vector<Monitor> monitors(std::size_t* under_cursor = nullptr);
// The monitor showing most of `r` (the nearest one when `r` is off every screen).
[[nodiscard]] const Monitor& monitor_for(const Rect& r, const std::vector<Monitor>& monitors);

[[nodiscard]] std::optional<Saved> load(const std::filesystem::path& file);
void save(const std::filesystem::path& file, const Saved& s);

}  // namespace agentdesk::ui::window_place
