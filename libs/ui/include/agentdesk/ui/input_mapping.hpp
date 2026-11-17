#pragma once

// Pure helpers between ImGui input and terminal input. No ImGui context needed: unit tested.

#include <cstdint>
#include <optional>
#include <string>

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

// Text of a linear selection between two cells (inclusive, any order) on one snapshot.
// Continuation cells of wide characters are skipped; rows end with "\r\n" except the last;
// trailing blanks of each row are trimmed. Never contains control characters.
std::string selection_text(const terminal::ScreenSnapshot& snap, CellPos a, CellPos b);

// Pasted text policy (design doc 3.5): NUL rejected, at most 1 MiB, multi-line needs preview.
enum class PasteCheck { kOk, kNeedsConfirmation, kRejectedNul, kRejectedTooLarge };
PasteCheck check_paste(const std::string& utf8);

}  // namespace agentdesk::ui
