#pragma once

// Pure helpers between ImGui input and terminal input. No ImGui context needed: unit tested.
// Portions adapted from herdr-app (Apache-2.0) and modified; see THIRD_PARTY_NOTICES.md.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <imgui.h>

#include "agentdesk/host/input.hpp"

namespace agentdesk::ui {

struct Modifiers {
    bool ctrl = false;
    bool shift = false;
    bool alt = false;
};

std::uint8_t to_terminal_mods(Modifiers m);

// Named (non-text) keys. Printable text must come from the character queue only, otherwise
// the same character is sent twice (skill rd/frontend-imgui §5).
std::optional<terminal::Key> map_named_key(ImGuiKey key);

// Ctrl/Alt + letter or digit: ImGui gives a key event (and Win32 also queues a control
// character, which callers drop). Returns the base character to encode with modifiers.
std::optional<char32_t> chord_char(ImGuiKey key);

enum class ClipboardAction { kNone, kCopy, kPaste, kSendInterrupt };

// Ctrl+C copies when there is a selection, otherwise it is an interrupt for the program.
// Ctrl+Shift+C always copies, Ctrl+Shift+V pastes (design doc 3.5).
ClipboardAction classify_clipboard_chord(ImGuiKey key, Modifiers m, bool has_selection);

// Decodes UTF-16 code units queued by ImGui into code points (pairs combined, controls dropped).
std::u32string printable_from_queue(const ImWchar* chars, int count);

struct GridSize {
    int rows = 1;
    int cols = 2;
    friend bool operator==(const GridSize&, const GridSize&) = default;
};

GridSize grid_for_area(float width, float height, float cell_w, float cell_h);

struct CellPos {
    int row = 0;
    int col = 0;
    friend bool operator==(const CellPos&, const CellPos&) = default;
};

CellPos cell_at(float x, float y, float origin_x, float origin_y, float cell_w, float cell_h, GridSize grid);

// East Asian wide or emoji presentation code point (two cells). Used where text is laid out
// without engine widths (history lines); the live screen uses the engine's cell widths.
bool is_wide_codepoint(char32_t cp);

// One line of cells, from the live screen or laid out from a history line's text.
struct LineCells {
    std::vector<terminal::Cell> cells;
    bool wrapped = false;  // soft-wrapped: continues on the next line
};
LineCells cells_from_text(const std::string& utf8, bool wrapped);

// Text of a linear selection between two cells (inclusive, any order) over consecutive lines
// (CellPos::row indexes `lines`), copied the way Ghostty's formatter does with unwrap and trim:
// soft-wrapped lines join without a break, trailing blanks of each line and trailing blank lines
// are trimmed, lines end with "\r\n". Continuation cells of wide characters
// are skipped. Never contains control characters.
std::string selection_text(const std::vector<LineCells>& lines, CellPos a, CellPos b);
// The same over one snapshot's rows.
std::string selection_text(const terminal::ScreenSnapshot& snap, CellPos a, CellPos b);

// Lines to scroll while a selection drag is held past the edge:
// 0 inside; otherwise 1..5 lines toward the pointer (negative = up), every 50 ms.
int selection_scroll_lines(float y, float cell_h, int rows);

// Paths typed in as a paste, quoted the way PowerShell/cmd read them, joined by spaces with a
// trailing space, never Enter.
// Empty when there is no path or one has control characters.
std::optional<std::string> quote_paths(const std::vector<std::string>& paths);

// Pasted text policy (design doc 3.5): NUL rejected, at most 1 MiB, multi-line needs preview.
enum class PasteCheck { kOk, kNeedsConfirmation, kRejectedNul, kRejectedTooLarge };
PasteCheck check_paste(const std::string& utf8);

}  // namespace agentdesk::ui
