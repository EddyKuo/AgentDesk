#pragma once
// Mission Control's rules, without drawing: how many columns and rows the wall of agent tiles
// takes, which agents get a tile and in what order, the filter, the last lines of a terminal's
// screen for a tile's body, moving the keyboard selection, and cleaning an agent's message.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "agentdesk/terminal/vt_engine.hpp"
#include "agentdesk/ui/workspace.hpp"

namespace agentdesk::ui::mission {

// ---- the grid
struct GridShape {
    int cols = 0;
    int rows = 0;
    float tile_w = 0;
    float tile_h = 0;
    bool scroll = false;  // the rows are taller than `h` together: the wall scrolls
    float content_h = 0;  // every row and the gaps between
};

// `count` tiles in a `w` x `h` area, `gap` apart: 1 tile 1x1, 2 2x1, 3-4 2x2, 5-6 3x2, 7-9 3x3,
// 10-12 4x3, more 4 columns (never more than `max_cols`). A tile is at least `min_w` x `min_h`:
// columns are dropped until tiles are wide enough (down to one), and when the rows do not fit the
// wall scrolls, with as many rows on screen as fit at the minimum height, stretched to fill it.
GridShape wall_grid(int count, float w, float h, float gap, float min_w, float min_h, int max_cols = 4);

// ---- order
struct WallAgent {
    std::string id;
    std::string parent;        // the agent that started it ("" = none)
    int rank = 0;              // attention_rank of the agent with its sub-agents
    long long since = 0;       // ms of the last change
    long long seq = 0;         // creation order
    bool unreachable = false;  // its machine is not connected
};

// The tiles, in order: reachable agents most urgent first (needs you, done, working, idle,
// others; then the latest change, then the newest, as priority_before), agents on machines that
// do not answer after them. A sub-agent whose parent is in `agents` gets no tile of its own unless
// `sub_agents`; then it follows its parent (and its own sub-agents follow it).
std::vector<std::string> wall_order(const std::vector<WallAgent>& agents, bool sub_agents);

// The filters: "all", "active" (needs you, working or done), "blocked", "working", "done".
bool filter_matches(std::string_view filter, Attention a);
// A filter name as written in ui.json or a command; anything unknown reads as "all".
std::string filter_name(std::string_view value);

// ---- the tile's body
// A run of cells of one colour on a line of the tail.
struct TailSegment {
    int col = 0;       // first column
    int cols = 0;      // columns it covers (a wide character covers two)
    std::string text;  // UTF-8
    bool default_fg = true;
    int ansi = -1;          // 0-15: one of the theme's 16 colours, else -1
    std::uint32_t rgb = 0;  // 0xRRGGBB when not the default (for ANSI: the standard colour)
    bool bold = false;
    bool wide = false;  // one wide character on its own (drawn centred on its two columns)
};
struct TailLine {
    int row = 0;  // screen row it came from
    std::vector<TailSegment> segments;
};

// The last `max_lines` lines of the screen that say something, top to bottom: blank rows and rows
// of only box-drawing characters (U+2500-U+257F, an agent's input frame) and spaces are skipped.
// Each line is cut into runs of one colour, without its trailing blanks. Only the visible screen
// is read (also on the alternate screen), never the history.
std::vector<TailLine> screen_tail(const terminal::ScreenSnapshot& snap, int max_lines);

// Lines of `line_h` that fit in `body_h`, at most `max_lines`.
int tail_line_budget(float body_h, float line_h, int max_lines = 12);

// ---- keyboard
enum class Nav { kLeft, kRight, kUp, kDown, kHome, kEnd };
// The selected tile after `nav` on a grid of `count` tiles, `cols` wide; -1 (none yet) selects the
// first. Down from a row above a shorter last row lands on that row's last tile.
int move_selection(int current, int count, int cols, Nav nav);

// ---- text
// An agent's message as a single line that is safe to show: line breaks and tabs become spaces,
// control characters and the invisible and direction-changing marks (U+200B-U+200F,
// U+202A-U+202E, U+2060-U+2069, U+FEFF) are dropped, invalid UTF-8 becomes U+FFFD, and runs of
// spaces shrink to one.
std::string clean_message(std::string_view text);

}  // namespace agentdesk::ui::mission
