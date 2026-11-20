#pragma once
// The rules behind the AgentDesk Classic layout (a character-cell interface in one colour): the
// chrome's cell grid, text measured in cells, the state marks, the tab line's truncation, the
// monochrome colour mapping for terminals and where scan lines go. No drawing here
// (apps/desktop/classic_layout.cpp draws), so it can be unit-tested.
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <imgui.h>

#include "agentdesk/ui/workspace.hpp"

namespace agentdesk::ui::classic {

// ---------------------------------------------------------------- the cell grid
// The chrome's cells: as wide as the font's advance rounded to a whole pixel, 20 design px high.
struct Grid {
    float cell_w = 8.0f;
    float cell_h = 20.0f;
    // Whole cells that fit in `width`; what is left over stays on the right.
    [[nodiscard]] int cols(float width) const;
    [[nodiscard]] float remainder(float width) const;
};
// `advance`: the font's advance at the chrome text size (screen px); `scale`: screen px per design px.
Grid grid_for(float advance, float scale);

// Cells a UTF-8 text takes: 2 for a wide (CJK) character, 1 for the rest.
int text_cols(std::string_view utf8);
// The text cut to at most `cols` cells, never through a wide character; "…" (one cell) marks a
// cut.
std::string cut_cols(std::string_view utf8, int cols);

// ---------------------------------------------------------------- states
// A state as text, so it reads without colour: needs you "[!]", working "[~]", done "[+]",
// idle "[.]", unknown "[?]".
const char* status_glyph(Attention a);
// The working spinner's frame (| / - \), one step every 125 ms.
char spinner_frame(long long ms);

// ---------------------------------------------------------------- the tab line
// Labels ("1 claude [!]") for a line of `cols` cells, `sep` cells between tabs. When they do not
// fit, the oldest tabs other than the current one shrink to their number and "…" ("1…"), one at a
// time from the left; the current tab always stays whole.
std::vector<std::string> fit_tabs(const std::vector<std::string>& labels, int active, int cols, int sep = 3);

// ---------------------------------------------------------------- monochrome
// A ramp from the terminal's background (dark end) to its foreground pushed 15% toward white
// (bright end).
struct MonoRamp {
    ImU32 dark = 0;
    ImU32 bright = 0;
    bool operator==(const MonoRamp&) const = default;
};
MonoRamp mono_ramp(ImU32 background, ImU32 foreground);
// Relative luminance, 0..1 (sRGB, as WCAG measures it).
float luminance(ImU32 c);
// WCAG contrast ratio between two colours (1..21).
float contrast(ImU32 a, ImU32 b);
// One colour put on the ramp by its luminance L: text at 0.35 + 0.65 L of the way (even dark text
// stays visible), backgrounds at 0.45 L (they stay dark). Mixed in linear light.
ImU32 mono_color(ImU32 c, bool foreground, const MonoRamp& r);
// A cell's pair: each colour mapped unless it is the terminal's default (kept as is); text too
// close to its background (luminance within 0.35) moves to the far end of the ramp; then reverse
// video swaps them.
struct CellPair {
    ImU32 fg = 0;
    ImU32 bg = 0;
};
CellPair mono_cell(ImU32 fg, ImU32 bg, bool fg_default, bool bg_default, bool reverse, const MonoRamp& r);

// ---------------------------------------------------------------- scan lines
// The scan lines over [top, bottom): one every 3 design px, 1 design px high, each starting on a
// whole screen pixel and at least one pixel high, so non-integer zooms show no moiré. `scale`:
// screen px per design px. Returns (y, height) pairs.
std::vector<std::pair<float, float>> scanlines(float top, float bottom, float scale);

// ---------------------------------------------------------------- narrow windows
// Under 80 cells the menu bar keeps only AGENTDESK and View (the rest go under AGENTDESK) and the
// clock goes first, then the counts.
struct BarFit {
    bool all_menus = true;
    bool clock = true;
    bool counts = true;
};
// `left_full` / `left_short`: cells the buttons and menus take with all menus / with two.
BarFit fit_bar(int cols, int left_full, int left_short, int counts_cols, int clock_cols);

}  // namespace agentdesk::ui::classic
