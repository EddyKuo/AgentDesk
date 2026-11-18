#include "agentdesk/ui/input_mapping.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string_view>

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

bool is_wide_codepoint(char32_t cp) {
    return (cp >= 0x1100 && cp <= 0x115F) || (cp >= 0x2E80 && cp <= 0x303E) ||
           (cp >= 0x3041 && cp <= 0x33FF) || (cp >= 0x3400 && cp <= 0x4DBF) ||
           (cp >= 0x4E00 && cp <= 0x9FFF) || (cp >= 0xA000 && cp <= 0xA4CF) ||
           (cp >= 0xAC00 && cp <= 0xD7A3) || (cp >= 0xF900 && cp <= 0xFAFF) ||
           (cp >= 0xFE30 && cp <= 0xFE4F) || (cp >= 0xFF00 && cp <= 0xFF60) ||
           (cp >= 0xFFE0 && cp <= 0xFFE6) || (cp >= 0x1F300 && cp <= 0x1F64F) ||
           (cp >= 0x1F900 && cp <= 0x1F9FF) || (cp >= 0x20000 && cp <= 0x3FFFD);
}

LineCells cells_from_text(const std::string& s, bool wrapped) {
    LineCells out;
    out.wrapped = wrapped;
    for (std::size_t i = 0; i < s.size();) {
        const auto b0 = static_cast<unsigned char>(s[i]);
        const std::size_t len = b0 < 0x80 ? 1 : (b0 & 0xE0) == 0xC0 ? 2 : (b0 & 0xF0) == 0xE0 ? 3 : 4;
        char32_t cp = len == 1 ? b0 : len == 2 ? (b0 & 0x1Fu) : len == 3 ? (b0 & 0x0Fu) : (b0 & 0x07u);
        for (std::size_t m = 1; m < len && i + m < s.size(); ++m)
            cp = (cp << 6) | (static_cast<unsigned char>(s[i + m]) & 0x3Fu);
        i += len;
        terminal::Cell c;
        c.chars[0] = cp;
        const bool wide = is_wide_codepoint(cp);
        c.width = wide ? 2 : 1;
        out.cells.push_back(c);
        if (wide) {
            terminal::Cell cont;
            cont.width = 0;
            out.cells.push_back(cont);
        }
    }
    return out;
}

std::string selection_text(const std::vector<LineCells>& lines, CellPos a, CellPos b) {
    if (lines.empty())
        return {};
    if (b.row < a.row || (b.row == a.row && b.col < a.col))
        std::swap(a, b);
    const int last = static_cast<int>(lines.size()) - 1;
    std::string out;
    std::size_t content_end = 0;  // end of the last non-blank text: trailing blank lines are trimmed
    for (int row = std::max(a.row, 0); row <= std::min(b.row, last); ++row) {
        const auto& line = lines[static_cast<std::size_t>(row)];
        const int cols = static_cast<int>(line.cells.size());
        const int c0 = row == a.row ? a.col : 0;
        const int c1 = row == b.row ? b.col : cols - 1;
        std::string text;
        for (int col = std::max(c0, 0); col <= std::min(c1, cols - 1); ++col) {
            const auto& cell = line.cells[static_cast<std::size_t>(col)];
            if (cell.width == 0)
                continue;
            if (cell.chars[0] == 0) {
                text.push_back(' ');
                continue;
            }
            for (const char32_t cp : cell.chars) {
                if (cp == 0)
                    break;
                if (cp < 0x20 || cp == 0x7F)
                    continue;
                terminal::append_utf8(text, cp);
            }
        }
        // A soft-wrapped line runs on into the next: no break, its blanks are inside the line.
        const bool joins = line.wrapped && row != b.row;
        if (!joins) {
            while (!text.empty() && text.back() == ' ')
                text.pop_back();
        }
        out += text;
        if (text.find_first_not_of(' ') != std::string::npos)
            content_end = out.size();
        if (row != b.row && !joins)
            out += "\r\n";
    }
    out.resize(content_end);
    return out;
}

std::string selection_text(const terminal::ScreenSnapshot& snap, CellPos a, CellPos b) {
    if (snap.rows <= 0 || snap.cols <= 0)
        return {};
    std::vector<LineCells> lines(static_cast<std::size_t>(snap.rows));
    for (int r = 0; r < snap.rows; ++r) {
        auto& l = lines[static_cast<std::size_t>(r)];
        const auto first = snap.cells.begin() + static_cast<std::ptrdiff_t>(r) * snap.cols;
        l.cells.assign(first, first + snap.cols);
        l.wrapped = snap.row_wrapped(r);
    }
    return selection_text(lines, a, b);
}

int selection_scroll_lines(float y, float cell_h, int rows) {
    if (cell_h <= 0.0f)
        return 0;
    const float height = cell_h * static_cast<float>(rows);
    float outside = 0.0f;
    if (y < 0.0f)
        outside = y;
    else if (y >= height)
        outside = y - height + 1.0f;
    else
        return 0;
    const int n = static_cast<int>(std::clamp(std::ceil(std::abs(outside / cell_h)), 1.0f, 5.0f));
    return outside < 0.0f ? -n : n;
}

std::optional<std::string> quote_paths(const std::vector<std::string>& paths) {
    if (paths.empty())
        return std::nullopt;
    static constexpr std::string_view kPlain = "/._-+=:@%~";
    std::string out;
    for (const auto& p : paths) {
        // Even inside quotes, a newline becomes Enter in an unbracketed paste.
        if (p.empty() || std::any_of(p.begin(), p.end(), [](char c) {
                const auto u = static_cast<unsigned char>(c);
                return u < 0x20 || u == 0x7F;
            }))
            return std::nullopt;
        const bool plain = std::all_of(p.begin(), p.end(), [](char c) {
            const auto u = static_cast<unsigned char>(c);
            return u < 0x80 && (std::isalnum(u) != 0 || kPlain.find(c) != std::string_view::npos);
        });
        if (plain) {
            out += p;
        } else {
            out += '\'';
            for (const char c : p) {
                out += c;
                if (c == '\'')
                    out += '\'';
            }
            out += '\'';
        }
        out += ' ';
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
