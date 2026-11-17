#include "agentdesk/ui/input_mapping.hpp"

#include <algorithm>
#include <cmath>

namespace agentdesk::ui {

using terminal::Key;

std::uint8_t to_terminal_mods(Modifiers m) {
    std::uint8_t out = terminal::kModNone;
    if (m.ctrl)
        out |= terminal::kModCtrl;
    if (m.shift)
        out |= terminal::kModShift;
    if (m.alt)
        out |= terminal::kModAlt;
    return out;
}

std::optional<Key> map_named_key(ImGuiKey key) {
    switch (key) {
        case ImGuiKey_Enter:
        case ImGuiKey_KeypadEnter:
            return Key::kEnter;
        case ImGuiKey_Tab:
            return Key::kTab;
        case ImGuiKey_Backspace:
            return Key::kBackspace;
        case ImGuiKey_Escape:
            return Key::kEscape;
        case ImGuiKey_UpArrow:
            return Key::kUp;
        case ImGuiKey_DownArrow:
            return Key::kDown;
        case ImGuiKey_LeftArrow:
            return Key::kLeft;
        case ImGuiKey_RightArrow:
            return Key::kRight;
        case ImGuiKey_Insert:
            return Key::kInsert;
        case ImGuiKey_Delete:
            return Key::kDelete;
        case ImGuiKey_Home:
            return Key::kHome;
        case ImGuiKey_End:
            return Key::kEnd;
        case ImGuiKey_PageUp:
            return Key::kPageUp;
        case ImGuiKey_PageDown:
            return Key::kPageDown;
        default:
            break;
    }
    if (key >= ImGuiKey_F1 && key <= ImGuiKey_F12) {
        return static_cast<Key>(static_cast<int>(Key::kF1) + (key - ImGuiKey_F1));
    }
    return std::nullopt;
}

std::optional<char32_t> chord_char(ImGuiKey key) {
    if (key >= ImGuiKey_A && key <= ImGuiKey_Z)
        return static_cast<char32_t>(U'a' + (key - ImGuiKey_A));
    if (key >= ImGuiKey_0 && key <= ImGuiKey_9)
        return static_cast<char32_t>(U'0' + (key - ImGuiKey_0));
    switch (key) {
        case ImGuiKey_Space:
            return U' ';
        case ImGuiKey_LeftBracket:
            return U'[';
        case ImGuiKey_RightBracket:
            return U']';
        case ImGuiKey_Backslash:
            return U'\\';
        case ImGuiKey_Minus:
            return U'-';
        case ImGuiKey_Slash:
            return U'/';
        default:
            return std::nullopt;
    }
}

ClipboardAction classify_clipboard_chord(ImGuiKey key, Modifiers m, bool has_selection) {
    if (!m.ctrl || m.alt)
        return ClipboardAction::kNone;
    if (m.shift && key == ImGuiKey_C)
        return ClipboardAction::kCopy;
    if (m.shift && key == ImGuiKey_V)
        return ClipboardAction::kPaste;
    if (!m.shift && key == ImGuiKey_C)
        return has_selection ? ClipboardAction::kCopy : ClipboardAction::kSendInterrupt;
    return ClipboardAction::kNone;
}

std::u32string printable_from_queue(const ImWchar* chars, int count) {
    std::u32string out;
    for (int i = 0; i < count; ++i) {
        const auto c = static_cast<char32_t>(chars[i]);
        // ImWchar is 32-bit in the vcpkg build (IMGUI_USE_WCHAR32 off -> 16-bit): handle both.
        if (c >= 0xD800 && c <= 0xDBFF && i + 1 < count) {
            const auto lo = static_cast<char32_t>(chars[i + 1]);
            if (lo >= 0xDC00 && lo <= 0xDFFF) {
                out.push_back(0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00));
                ++i;
                continue;
            }
        }
        if (c < 0x20 || c == 0x7F || (c >= 0xD800 && c <= 0xDFFF) || c == 0xFFFD) {
            continue;  // control characters come from key events; lone surrogates are invalid
        }
        out.push_back(c);
    }
    return out;
}

GridSize grid_for_area(float width, float height, float cell_w, float cell_h) {
    GridSize g;
    if (cell_w <= 0.0f || cell_h <= 0.0f)
        return g;
    g.cols = std::clamp(static_cast<int>(std::floor(width / cell_w)), 2, 1000);
    g.rows = std::clamp(static_cast<int>(std::floor(height / cell_h)), 1, 500);
    return g;
}

CellPos cell_at(float x, float y, float origin_x, float origin_y, float cell_w, float cell_h, GridSize grid) {
    CellPos p;
    if (cell_w <= 0.0f || cell_h <= 0.0f)
        return p;
    p.col = std::clamp(static_cast<int>(std::floor((x - origin_x) / cell_w)), 0, grid.cols - 1);
    p.row = std::clamp(static_cast<int>(std::floor((y - origin_y) / cell_h)), 0, grid.rows - 1);
    return p;
}

std::string selection_text(const terminal::ScreenSnapshot& snap, CellPos a, CellPos b) {
    if (snap.rows <= 0 || snap.cols <= 0)
        return {};
    if (b.row < a.row || (b.row == a.row && b.col < a.col))
        std::swap(a, b);
    std::string out;
    for (int row = std::max(a.row, 0); row <= std::min(b.row, snap.rows - 1); ++row) {
        const int c0 = row == a.row ? a.col : 0;
        const int c1 = row == b.row ? b.col : snap.cols - 1;
        std::string line;
        for (int col = std::max(c0, 0); col <= std::min(c1, snap.cols - 1); ++col) {
            const auto& cell = snap.at(row, col);
            if (cell.width == 0)
                continue;
            if (cell.chars[0] == 0) {
                line.push_back(' ');
                continue;
            }
            for (const char32_t cp : cell.chars) {
                if (cp == 0)
                    break;
                if (cp < 0x20 || cp == 0x7F)
                    continue;
                terminal::append_utf8(line, cp);
            }
        }
        while (!line.empty() && line.back() == ' ')
            line.pop_back();
        out += line;
        if (row != b.row)
            out += "\r\n";
    }
    return out;
}

PasteCheck check_paste(const std::string& utf8) {
    if (utf8.size() > 1024 * 1024)
        return PasteCheck::kRejectedTooLarge;
    if (utf8.find('\0') != std::string::npos)
        return PasteCheck::kRejectedNul;
    if (utf8.find('\n') != std::string::npos || utf8.find('\r') != std::string::npos) {
        return PasteCheck::kNeedsConfirmation;
    }
    return PasteCheck::kOk;
}

}  // namespace agentdesk::ui
