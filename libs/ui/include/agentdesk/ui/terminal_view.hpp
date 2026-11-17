#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <imgui.h>

#include "agentdesk/ui/fonts.hpp"
#include "agentdesk/ui/input_mapping.hpp"
#include "agentdesk/ui/terminal_source.hpp"

namespace agentdesk::ui {

struct TerminalTheme {
    ImU32 default_fg = IM_COL32(220, 221, 226, 255);
    ImU32 default_bg = IM_COL32(17, 17, 19, 255);
    ImU32 cursor = IM_COL32(236, 236, 240, 255);
    ImU32 selection = IM_COL32(96, 130, 240, 90);
};

// Window-level shortcuts. A focused terminal owns the whole keyboard, so it recognizes these
// chords itself, does not send them to the program, and hands them to the app.
enum class AppShortcut { kNone, kNewSession, kNextTab, kPrevTab, kClosePane };

// East Asian wide or emoji presentation code point (two cells). Used where text is laid out
// without engine widths (history lines); the live screen uses the engine's cell widths.
bool is_wide_codepoint(char32_t cp);

// Draws one terminal snapshot as a character grid and turns keyboard/mouse/IME/clipboard input
// into abstract input events. Owns no session state beyond view concerns (selection, pending
// resize, paste confirmation). UI thread only.
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
    // Shortcut chord typed into this view since the last call (kNone if none).
    AppShortcut take_shortcut() noexcept { return std::exchange(shortcut_, AppShortcut::kNone); }

    TerminalTheme theme;

private:
    void handle_resize(TerminalSource& source, GridSize wanted, const terminal::ScreenSnapshot& snap);
    void handle_keyboard(const char* str_id, TerminalSource& source, const terminal::ScreenSnapshot& snap);
    void request_paste(TerminalSource& source, std::string text);
    void draw_paste_popup(TerminalSource& source);

    std::optional<CellPos> sel_anchor_;
    std::optional<CellPos> sel_end_;
    bool selecting_ = false;
    std::optional<GridSize> pending_size_;
    std::chrono::steady_clock::time_point pending_since_{};
    std::optional<GridSize> requested_size_;
    std::string paste_pending_;
    bool open_paste_popup_ = false;
    std::string notice_;
    AppShortcut shortcut_ = AppShortcut::kNone;
    // Scrollback (design doc 3.4/3.5): lines of history shown above the live screen. While
    // scrolled, selection works on whole lines keyed by history line_id (or a screen-row key).
    int scroll_offset_ = 0;
    std::optional<std::uint64_t> line_sel_a_;
    std::optional<std::uint64_t> line_sel_b_;
    std::vector<std::pair<std::uint64_t, std::string>> viewport_lines_;  // key, text (scrolled mode)
    std::string selected_text() const;
};

}  // namespace agentdesk::ui
