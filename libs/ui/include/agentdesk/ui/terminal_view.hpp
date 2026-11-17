#pragma once

#include <chrono>
#include <optional>
#include <string>

#include <imgui.h>

#include "agentdesk/ui/fonts.hpp"
#include "agentdesk/ui/input_mapping.hpp"
#include "agentdesk/ui/terminal_source.hpp"

namespace agentdesk::ui {

struct TerminalTheme {
    ImU32 default_fg = IM_COL32(204, 204, 204, 255);
    ImU32 default_bg = IM_COL32(12, 12, 12, 255);
    ImU32 cursor = IM_COL32(230, 230, 230, 255);
    ImU32 selection = IM_COL32(60, 110, 200, 110);
};

// Draws one terminal snapshot as a character grid and turns keyboard/mouse/IME/clipboard input
// into abstract input events. Owns no session state beyond view concerns (selection, pending
// resize, paste confirmation). UI thread only.
class TerminalView {
public:
    // Returns true when the view is focused (owns the keyboard) this frame.
    bool draw(const char* str_id, TerminalSource& source, const FontSet& fonts, bool request_focus);

    // Earliest time a redraw is needed without new input (pending debounced resize).
    [[nodiscard]] std::optional<std::chrono::steady_clock::time_point> next_deadline() const;
    [[nodiscard]] bool has_selection() const { return sel_anchor_.has_value() && sel_end_.has_value(); }

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
};

}  // namespace agentdesk::ui
