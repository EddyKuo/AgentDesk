#pragma once

#include <chrono>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <imgui.h>

#include "agentdesk/ui/fonts.hpp"
#include "agentdesk/ui/input_mapping.hpp"
#include "agentdesk/ui/keymap.hpp"
#include "agentdesk/ui/terminal_source.hpp"

namespace agentdesk::ui {

struct TerminalTheme {
    ImU32 default_fg = IM_COL32(220, 221, 226, 255);
    ImU32 default_bg = IM_COL32(17, 17, 19, 255);
    ImU32 cursor = IM_COL32(236, 236, 240, 255);
    ImU32 selection = IM_COL32(96, 130, 240, 90);
    ImU32 match = IM_COL32(230, 182, 84, 110);
    ImU32 match_current = IM_COL32(240, 140, 60, 190);
    ImU32 scrollbar = IM_COL32(120, 120, 135, 120);
};

// East Asian wide or emoji presentation code point (two cells). Used where text is laid out
// without engine widths (history lines); the live screen uses the engine's cell widths.
bool is_wide_codepoint(char32_t cp);

// Most history lines a view fetches for scrolling and search (paged, 200 per request).
inline constexpr int kMaxScrollback = 5000;

// Draws one terminal snapshot as a character grid and turns keyboard/mouse/IME/clipboard input
// into abstract input events. Owns no session state beyond view concerns (selection, scroll,
// search, pending resize, paste confirmation). UI thread only.
class TerminalView {
public:
    // Returns true when the view is focused (owns the keyboard) this frame.
    bool draw(const char* str_id, TerminalSource& source, const FontSet& fonts, bool request_focus);

    // Earliest time a redraw is needed without new input (pending debounced resize).
    [[nodiscard]] std::optional<std::chrono::steady_clock::time_point> next_deadline() const;
    [[nodiscard]] bool has_selection() const {
        return (sel_anchor_.has_value() && sel_end_.has_value()) ||
               (line_sel_a_.has_value() && line_sel_b_.has_value());
    }
    [[nodiscard]] int scroll_offset() const noexcept { return scroll_offset_; }
    // App commands typed into this view (global key chords, context menu), oldest first.
    std::vector<std::string> take_commands() { return std::exchange(commands_, {}); }
    // A terminal.* command to run on the next draw (from the palette or a window key).
    void request(std::string command) { requested_.push_back(std::move(command)); }
    void open_search() {
        search_open_ = true;
        search_focus_ = true;
    }

    TerminalTheme theme;
    const Keymap* keymap = nullptr;  // global chords are reported, terminal chords run here
    bool copy_on_select = true;
    float scroll_speed = 1.0f;
    std::string parent_name;   // sub-agent: its parent's name ("Send back to ..." in the menu)
    float line_height = 1.0f;  // cell height multiplier (herdr terminal_line_height)
    float cell_width = 1.0f;   // cell width multiplier (herdr terminal_cell_width)
    // herdr terminal.right_click: when the program reads the mouse, right-clicks go to it instead
    // of opening the menu. Shift+right-click always opens the menu.
    bool right_click_to_program = false;
    // Draws the right-click menu's rows inside the open popup (the app's herdr menu); empty uses
    // a plain built-in menu. Return true to close the menu.
    std::function<bool()> menu_rows;

private:
    void handle_resize(TerminalSource& source, GridSize wanted, const terminal::ScreenSnapshot& snap);
    void handle_keyboard(const char* str_id, TerminalSource& source, const terminal::ScreenSnapshot& snap);
    // terminal.* commands and "send:<bytes>"; anything else goes to the app.
    void run_command(const std::string& cmd, TerminalSource& source, const terminal::ScreenSnapshot& snap);
    void copy_selection(const terminal::ScreenSnapshot& snap);
    void request_paste(TerminalSource& source, std::string text);
    void draw_paste_popup(TerminalSource& source);
    void draw_context_menu(TerminalSource& source, const terminal::ScreenSnapshot& snap);
    void draw_search_bar(ImVec2 area_min, ImVec2 area_max);
    void update_search(const std::vector<HistoryLineText>* hist, const terminal::ScreenSnapshot& snap);
    void clear_selection();
    // Program mouse reporting (DECSET 1000/1002/1003): returns true when the event went to it.
    void forward_mouse(TerminalSource& source, const terminal::ScreenSnapshot& snap, bool hovered, CellPos cell);
    std::uint8_t mouse_buttons_ = 0;  // bit per button 1..3 pressed inside and not yet released
    std::optional<CellPos> last_mouse_cell_;

    std::optional<CellPos> sel_anchor_;
    std::optional<CellPos> sel_end_;
    bool selecting_ = false;
    std::optional<GridSize> pending_size_;
    std::chrono::steady_clock::time_point pending_since_{};
    std::optional<GridSize> requested_size_;
    std::string paste_pending_;
    bool open_paste_popup_ = false;
    bool open_context_menu_ = false;
    std::string notice_;
    std::vector<std::string> commands_;
    std::vector<std::string> requested_;
    // Scrollback: lines of history above the live screen (0 = live). While scrolled, selection
    // works on whole lines keyed by history line_id (or a screen-row key).
    int scroll_offset_ = 0;
    bool dragging_scrollbar_ = false;
    std::optional<std::uint64_t> line_sel_a_;
    std::optional<std::uint64_t> line_sel_b_;
    std::vector<std::pair<std::uint64_t, std::string>> viewport_lines_;  // key, text (scrolled mode)
    std::string selected_text() const;
    // Search over history + screen (case-insensitive). Lines are indexed history first.
    struct Match {
        int line = 0;
        int col = 0;
        int len = 0;
    };
    bool search_open_ = false;
    bool search_focus_ = false;
    bool refocus_ = false;
    char search_buf_[256] = {};
    std::string search_query_;
    std::size_t search_lines_seen_ = 0;
    std::uint64_t search_screen_seq_ = 0;
    std::vector<Match> matches_;
    int match_index_ = -1;
    bool jump_to_match_ = false;
    int last_available_ = 0;
};

}  // namespace agentdesk::ui
