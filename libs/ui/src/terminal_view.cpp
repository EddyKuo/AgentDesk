#include "agentdesk/ui/terminal_view.hpp"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>

#include "agentdesk/ui/imgui_adapter.hpp"

namespace agentdesk::ui {

namespace {

using Clock = std::chrono::steady_clock;
constexpr auto kResizeDebounce = std::chrono::milliseconds(50);  // design doc 3.5

constexpr std::array<ImGuiKey, 26> kNamedKeys = {
    ImGuiKey_Enter,   ImGuiKey_KeypadEnter, ImGuiKey_Tab,       ImGuiKey_Backspace,  ImGuiKey_Escape,
    ImGuiKey_UpArrow, ImGuiKey_DownArrow,   ImGuiKey_LeftArrow, ImGuiKey_RightArrow, ImGuiKey_Insert,
    ImGuiKey_Delete,  ImGuiKey_Home,        ImGuiKey_End,       ImGuiKey_PageUp,     ImGuiKey_PageDown,
    ImGuiKey_F1,      ImGuiKey_F2,          ImGuiKey_F3,        ImGuiKey_F4,         ImGuiKey_F5,
    ImGuiKey_F6,      ImGuiKey_F7,          ImGuiKey_F8,        ImGuiKey_F9,         ImGuiKey_F10,
    ImGuiKey_F11,
};

ImU32 to_color(const terminal::Rgb& c) {
    return IM_COL32(c.r, c.g, c.b, 255);
}

struct CellColors {
    ImU32 fg;
    ImU32 bg;
};

CellColors colors_of(const terminal::Cell& cell, const TerminalTheme& theme) {
    CellColors out{cell.default_fg ? theme.default_fg : to_color(cell.fg),
                   cell.default_bg ? theme.default_bg : to_color(cell.bg)};
    if (cell.attrs & terminal::kAttrReverse)
        std::swap(out.fg, out.bg);
    if (cell.attrs & terminal::kAttrConceal)
        out.fg = out.bg;
    return out;
}

bool in_selection(CellPos p, CellPos a, CellPos b) {
    if (b.row < a.row || (b.row == a.row && b.col < a.col))
        std::swap(a, b);
    if (p.row < a.row || p.row > b.row)
        return false;
    if (p.row == a.row && p.col < a.col)
        return false;
    if (p.row == b.row && p.col > b.col)
        return false;
    return true;
}

// Draws one cell's text so it fills its slot: a glyph narrower than the slot is centered (CJK
// in a two-cell slot, full-width punctuation), never drawn at the font's own advance.
void draw_cell_text(ImDrawList* dl, ImFont* font, float size, ImFontBaked* baked, float x, float y,
                    float slot_w, ImU32 fg, const terminal::Cell& cell, std::string& scratch) {
    // ImWchar is 32-bit (vcpkg imgui feature wchar32), so emoji outside the BMP render too.
    if (cell.chars[1] == 0 && static_cast<char32_t>(static_cast<ImWchar>(cell.chars[0])) == cell.chars[0]) {
        const auto c = static_cast<ImWchar>(cell.chars[0]);
        float adv = baked->GetCharAdvance(c);
        float draw_size = size;
        float dy = 0.0f;
        if (slot_w > adv * 1.15f && cell.width == 2) {
            // CJK faces draw ideographs well inside their em box; at the Latin size they look
            // spaced out in a two-cell slot. Scale up to fill ~94% of the slot, keeping the
            // baseline (ascent ~80% of the size) where the Latin text has it.
            const float k = std::min(1.3f, slot_w * 0.94f / adv);
            draw_size = size * k;
            adv *= k;
            dy = -(draw_size - size) * 0.8f;
        } else if (adv > slot_w * 1.05f) {
            // A fallback symbol wider than its slot: shrink it into the cell instead of letting it
            // overlap the next character.
            const float k = slot_w * 0.95f / adv;
            draw_size = size * k;
            adv *= k;
            dy = (size - draw_size) * 0.5f;
        }
        const float dx = adv < slot_w ? std::floor((slot_w - adv) * 0.5f) : 0.0f;
        font->RenderChar(dl, draw_size, ImVec2(x + dx, y + dy), fg, c);
        return;
    }
    scratch.clear();  // combining sequence or astral plane: let ImGui lay it out
    for (const char32_t cp : cell.chars) {
        if (cp == 0)
            break;
        terminal::append_utf8(scratch, cp);
    }
    const float adv =
        font->CalcTextSizeA(size, FLT_MAX, 0.0f, scratch.data(), scratch.data() + scratch.size()).x;
    const float dx = adv < slot_w ? std::floor((slot_w - adv) * 0.5f) : 0.0f;
    dl->AddText(font, size, ImVec2(x + dx, y), fg, scratch.data(), scratch.data() + scratch.size());
}

}  // namespace

bool is_wide_codepoint(char32_t cp) {
    return (cp >= 0x1100 && cp <= 0x115F) || (cp >= 0x2E80 && cp <= 0x303E) ||
           (cp >= 0x3041 && cp <= 0x33FF) || (cp >= 0x3400 && cp <= 0x4DBF) ||
           (cp >= 0x4E00 && cp <= 0x9FFF) || (cp >= 0xA000 && cp <= 0xA4CF) ||
           (cp >= 0xAC00 && cp <= 0xD7A3) || (cp >= 0xF900 && cp <= 0xFAFF) ||
           (cp >= 0xFE30 && cp <= 0xFE4F) || (cp >= 0xFF00 && cp <= 0xFF60) ||
           (cp >= 0xFFE0 && cp <= 0xFFE6) || (cp >= 0x1F300 && cp <= 0x1F64F) ||
           (cp >= 0x1F900 && cp <= 0x1F9FF) || (cp >= 0x20000 && cp <= 0x3FFFD);
}

std::string TerminalView::selected_text() const {
    if (!line_sel_a_ || !line_sel_b_)
        return {};
    std::string out;
    bool inside = false;
    for (const auto& [key, text] : viewport_lines_) {
        const bool edge = key == *line_sel_a_ || key == *line_sel_b_;
        if (edge || inside) {
            if (!out.empty())
                out += "\r\n";
            out += text;
        }
        if (edge && *line_sel_a_ != *line_sel_b_)
            inside = !inside;
        else if (edge)
            break;
    }
    return out;
}

std::optional<Clock::time_point> TerminalView::next_deadline() const {
    if (pending_size_ && pending_size_ != requested_size_)
        return pending_since_ + kResizeDebounce;
    return std::nullopt;
}

void TerminalView::handle_resize(TerminalSource& source, GridSize wanted,
                                 const terminal::ScreenSnapshot& snap) {
    const GridSize current{snap.rows, snap.cols};
    if (wanted == current && (!requested_size_ || *requested_size_ == wanted)) {
        pending_size_.reset();
        return;
    }
    const auto now = Clock::now();
    if (!pending_size_ || *pending_size_ != wanted) {
        pending_size_ = wanted;
        pending_since_ = now;
        return;
    }
    if (now - pending_since_ >= kResizeDebounce && requested_size_ != wanted) {
        source.resize(wanted.rows, wanted.cols);
        requested_size_ = wanted;
    }
}

void TerminalView::request_paste(TerminalSource& source, std::string text) {
    switch (check_paste(text)) {
        case PasteCheck::kOk:
            source.send(host::PasteInput{std::move(text)});
            break;
        case PasteCheck::kNeedsConfirmation:
            paste_pending_ = std::move(text);
            open_paste_popup_ = true;
            break;
        case PasteCheck::kRejectedNul:
            notice_ = "Paste rejected: contains NUL";
            break;
        case PasteCheck::kRejectedTooLarge:
            notice_ = "Paste rejected: larger than 1 MiB";
            break;
    }
}

void TerminalView::draw_paste_popup(TerminalSource& source) {
    if (open_paste_popup_) {
        ImGui::OpenPopup("Confirm multi-line paste");
        open_paste_popup_ = false;
    }
    if (ImGui::BeginPopupModal("Confirm multi-line paste", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        const auto lines = std::count(paste_pending_.begin(), paste_pending_.end(), '\n') + 1;
        ImGui::Text("Paste %d lines (%zu bytes) into the terminal?", static_cast<int>(lines),
                    paste_pending_.size());
        ImGui::BeginChild("preview", ImVec2(560, 160), ImGuiChildFlags_Borders);
        ImGui::TextUnformatted(paste_pending_.data(),
                               paste_pending_.data() + std::min<std::size_t>(paste_pending_.size(), 4096));
        ImGui::EndChild();
        if (ImGui::Button("Paste")) {
            source.send(host::PasteInput{std::move(paste_pending_)});
            paste_pending_.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            paste_pending_.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void TerminalView::handle_keyboard(const char* str_id, TerminalSource& source,
                                   const terminal::ScreenSnapshot& snap) {
    const ImGuiIO& io = ImGui::GetIO();
    const Modifiers mods{io.KeyCtrl, io.KeyShift, io.KeyAlt};
    const bool alt_gr = mods.ctrl && mods.alt;  // AltGr arrives as Ctrl+Alt with a printable char

    // Window shortcuts are not sent to the program.
    if (mods.ctrl && !mods.alt) {
        if (mods.shift && adapter::key_pressed_owned(ImGuiKey_T, str_id)) {
            shortcut_ = AppShortcut::kNewSession;
            return;
        }
        if (mods.shift && adapter::key_pressed_owned(ImGuiKey_W, str_id)) {
            shortcut_ = AppShortcut::kClosePane;
            return;
        }
        if (adapter::key_pressed_owned(ImGuiKey_Tab, str_id)) {
            shortcut_ = mods.shift ? AppShortcut::kPrevTab : AppShortcut::kNextTab;
            return;
        }
    }

    // Clipboard chords first: they decide what Ctrl+C means.
    for (const ImGuiKey key : {ImGuiKey_C, ImGuiKey_V}) {
        if (!adapter::key_pressed_owned(key, str_id))
            continue;
        switch (classify_clipboard_chord(key, mods, has_selection())) {
            case ClipboardAction::kCopy:
                ImGui::SetClipboardText(line_sel_a_ ? selected_text().c_str()
                                                    : selection_text(snap, *sel_anchor_, *sel_end_).c_str());
                sel_anchor_.reset();
                sel_end_.reset();
                line_sel_a_.reset();
                line_sel_b_.reset();
                return;
            case ClipboardAction::kPaste:
                if (const char* clip = ImGui::GetClipboardText(); clip != nullptr)
                    request_paste(source, clip);
                return;
            case ClipboardAction::kSendInterrupt:
            case ClipboardAction::kNone:
                break;
        }
    }

    for (const ImGuiKey key : kNamedKeys) {
        if (!adapter::key_pressed_owned(key, str_id))
            continue;
        if (const auto mapped = map_named_key(key)) {
            scroll_offset_ = 0;
            source.send(host::KeyInput{*mapped, to_terminal_mods(mods)});
        }
    }
    if (adapter::key_pressed_owned(ImGuiKey_F12, str_id)) {
        scroll_offset_ = 0;
        source.send(host::KeyInput{terminal::Key::kF12, to_terminal_mods(mods)});
    }

    // Ctrl/Alt chords (Ctrl+C without selection lands here as an interrupt byte).
    if ((mods.ctrl || mods.alt) && !alt_gr) {
        for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; ++k) {
            const auto key = static_cast<ImGuiKey>(k);
            const auto ch = chord_char(key);
            if (!ch || !adapter::key_pressed_owned(key, str_id))
                continue;
            if (mods.ctrl && mods.shift && (key == ImGuiKey_C || key == ImGuiKey_V))
                continue;
            scroll_offset_ = 0;
            source.send(host::TextInput{std::u32string(1, *ch), to_terminal_mods(mods)});
        }
    }

    // Printable text (including committed IME strings) only from the character queue.
    if (!mods.ctrl || alt_gr) {
        auto text = printable_from_queue(io.InputQueueCharacters.Data, io.InputQueueCharacters.Size);
        if (!text.empty())
            scroll_offset_ = 0;
        source.send(host::TextInput{std::move(text), terminal::kModNone});
    }
}

bool TerminalView::draw(const char* str_id, TerminalSource& source, const FontSet& fonts,
                        bool request_focus) {
    const auto snap_ptr = source.snapshot();
    if (!snap_ptr)
        return false;
    const terminal::ScreenSnapshot& snap = *snap_ptr;

    ImGui::PushFont(fonts.mono, fonts.mono_size);
    ImFont* font = ImGui::GetFont();
    const float font_size = ImGui::GetFontSize();
    ImFontBaked* baked = ImGui::GetFontBaked();
    // The real advance, not CalcTextSize (which rounds widths up: 12.09 px became 13 px and the
    // cursor drifted right of the text). The mono font uses PixelSnapH, so this is integral.
    const float cell_w = std::max(1.0f, std::round(baked->GetCharAdvance(static_cast<ImWchar>('M'))));
    const float cell_h = std::round(ImGui::GetTextLineHeight());

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    handle_resize(source, grid_for_area(avail.x, avail.y, cell_w, cell_h), snap);

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 area_max(origin.x + std::max(avail.x, cell_w * 2), origin.y + std::max(avail.y, cell_h));
    const auto item = adapter::grid_item(str_id, origin, area_max, request_focus);
    if (!item.visible) {
        ImGui::PopFont();
        return false;
    }

    const GridSize grid{snap.rows, snap.cols};
    ImGuiIO& io = ImGui::GetIO();

    // Mouse wheel scrolls into history (fetched asynchronously, newest 200 lines max).
    if (item.hovered && io.MouseWheel != 0.0f) {
        scroll_offset_ = std::clamp(scroll_offset_ + static_cast<int>(io.MouseWheel * 3.0f), 0, 200);
    }
    const auto hist = source.history();
    if (scroll_offset_ > 0)
        source.request_history(static_cast<std::size_t>(scroll_offset_ + snap.rows));
    const int available = hist ? static_cast<int>(hist->size()) : 0;
    const int offset = std::min({scroll_offset_, available, snap.rows});
    if (offset == 0 && scroll_offset_ > 0 && hist)
        scroll_offset_ = std::min(scroll_offset_, available);

    constexpr std::uint64_t kScreenKey = 1ull << 62;  // screen rows: kScreenKey + row
    auto row_key = [&](int view_row) -> std::uint64_t {
        if (view_row < offset)
            return (*hist)[static_cast<std::size_t>(available - offset + view_row)].line_id;
        return kScreenKey + static_cast<std::uint64_t>(view_row - offset);
    };
    if (offset > 0) {
        viewport_lines_.clear();
        for (int r = 0; r < snap.rows; ++r) {
            viewport_lines_.emplace_back(
                row_key(r), r < offset ? (*hist)[static_cast<std::size_t>(available - offset + r)].text
                                       : snap.row_text(r - offset));
        }
    }

    if (item.clicked) {
        const auto p = cell_at(io.MousePos.x, io.MousePos.y, origin.x, origin.y, cell_w, cell_h, grid);
        sel_anchor_.reset();
        sel_end_.reset();
        line_sel_a_.reset();
        line_sel_b_.reset();
        if (offset > 0) {
            line_sel_a_ = row_key(p.row);  // scrolled: whole-line selection by line_id
        } else {
            sel_anchor_ = p;
        }
        selecting_ = true;
    }
    if (selecting_) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            const auto p = cell_at(io.MousePos.x, io.MousePos.y, origin.x, origin.y, cell_w, cell_h, grid);
            if (line_sel_a_) {
                line_sel_b_ = row_key(p.row);
            } else if (sel_anchor_ && (p != *sel_anchor_ || sel_end_)) {
                sel_end_ = p;
            }
        } else {
            selecting_ = false;
        }
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(origin, area_max, true);
    dl->AddRectFilled(origin, area_max, theme.default_bg);

    const int first_row = std::max(0, static_cast<int>((dl->GetClipRectMin().y - origin.y) / cell_h));
    const int last_row =
        std::min(snap.rows, static_cast<int>((dl->GetClipRectMax().y - origin.y) / cell_h) + 1);
    std::string run;
    for (int view_row = 0; view_row < offset; ++view_row) {
        // History is plain text: lay it out on the grid with an approximate width table.
        const auto& line = (*hist)[static_cast<std::size_t>(available - offset + view_row)];
        const float y = origin.y + static_cast<float>(view_row) * cell_h;
        int hcol = 0;
        for (std::size_t i = 0; i < line.text.size() && hcol < snap.cols;) {
            const auto b0 = static_cast<unsigned char>(line.text[i]);
            const std::size_t len = b0 < 0x80 ? 1 : (b0 & 0xE0) == 0xC0 ? 2 : (b0 & 0xF0) == 0xE0 ? 3 : 4;
            char32_t cp = len == 1 ? b0 : len == 2 ? (b0 & 0x1Fu) : len == 3 ? (b0 & 0x0Fu) : (b0 & 0x07u);
            for (std::size_t m = 1; m < len && i + m < line.text.size(); ++m)
                cp = (cp << 6) | (static_cast<unsigned char>(line.text[i + m]) & 0x3Fu);
            i += len;
            terminal::Cell hc;
            hc.chars[0] = cp;
            const int w = is_wide_codepoint(cp) ? 2 : 1;
            if (cp != U' ') {
                draw_cell_text(dl, font, font_size, baked, origin.x + static_cast<float>(hcol) * cell_w, y,
                               cell_w * static_cast<float>(w), theme.default_fg, hc, run);
            }
            hcol += w;
        }
    }
    for (int row = first_row; row < last_row - offset; ++row) {
        const float y = origin.y + static_cast<float>(row + offset) * cell_h;
        // Backgrounds as merged runs of equal colour.
        int col = 0;
        while (col < snap.cols) {
            const ImU32 bg = colors_of(snap.at(row, col), theme).bg;
            int end = col + 1;
            while (end < snap.cols && colors_of(snap.at(row, end), theme).bg == bg)
                ++end;
            if (bg != theme.default_bg) {
                dl->AddRectFilled(ImVec2(origin.x + static_cast<float>(col) * cell_w, y),
                                  ImVec2(origin.x + static_cast<float>(end) * cell_w, y + cell_h), bg);
            }
            col = end;
        }
        // Text: every cell at its own grid position (skill §6), so a glyph whose advance differs
        // from the cell width (CJK, full-width punctuation, symbols) can never shift the rest.
        col = 0;
        while (col < snap.cols) {
            const auto& cell = snap.at(row, col);
            const int span = std::max<int>(1, cell.width);
            const float x = origin.x + static_cast<float>(col) * cell_w;
            if (cell.width != 0 && cell.chars[0] != 0) {
                const ImU32 fg = colors_of(cell, theme).fg;
                if (cell.chars[0] != U' ') {
                    draw_cell_text(dl, font, font_size, baked, x, y, cell_w * static_cast<float>(span), fg,
                                   cell, run);
                }
                if (cell.attrs & terminal::kAttrUnderline) {
                    dl->AddLine(ImVec2(x, y + cell_h - 1),
                                ImVec2(x + cell_w * static_cast<float>(span), y + cell_h - 1), fg);
                }
            }
            col += cell.width == 0 ? 1 : span;
        }
    }

    if (line_sel_a_ && line_sel_b_) {
        bool inside = false;
        for (int r = 0; r < snap.rows; ++r) {
            const auto key = row_key(r);
            const bool edge = key == *line_sel_a_ || key == *line_sel_b_;
            if (edge || inside) {
                const float y = origin.y + static_cast<float>(r) * cell_h;
                dl->AddRectFilled(ImVec2(origin.x, y), ImVec2(area_max.x, y + cell_h), theme.selection);
            }
            if (edge && *line_sel_a_ != *line_sel_b_)
                inside = !inside;
        }
    }
    if (sel_anchor_ && sel_end_) {
        for (int row = first_row; row < last_row; ++row) {
            for (int c = 0; c < snap.cols; ++c) {
                if (!in_selection({row, c}, *sel_anchor_, *sel_end_))
                    continue;
                const ImVec2 a(origin.x + static_cast<float>(c) * cell_w,
                               origin.y + static_cast<float>(row) * cell_h);
                dl->AddRectFilled(a, ImVec2(a.x + cell_w, a.y + cell_h), theme.selection);
            }
        }
    }

    const ImVec2 caret(origin.x + static_cast<float>(snap.cursor.col) * cell_w,
                       origin.y + static_cast<float>(snap.cursor.row + offset) * cell_h);
    if (offset > 0) {
        const std::string tag = "scrollback +" + std::to_string(offset) + (available >= 200 ? " (max)" : "");
        const ImVec2 sz = ImGui::CalcTextSize(tag.c_str());
        dl->AddRectFilled(ImVec2(area_max.x - sz.x - 8, origin.y), ImVec2(area_max.x, origin.y + cell_h),
                          IM_COL32(60, 60, 90, 220));
        dl->AddText(ImVec2(area_max.x - sz.x - 4, origin.y), IM_COL32(230, 230, 255, 255), tag.c_str());
    }
    if (offset == 0 && snap.cursor.visible && snap.cursor.row < snap.rows && snap.cursor.col < snap.cols) {
        // On the left half of a wide character the cursor covers both cells.
        const int cursor_span = std::max<int>(1, snap.at(snap.cursor.row, snap.cursor.col).width);
        const ImVec2 caret_max(caret.x + cell_w * static_cast<float>(cursor_span), caret.y + cell_h);
        if (item.active) {
            dl->AddRectFilled(caret, caret_max, (theme.cursor & 0x00FFFFFF) | 0x90000000);
        } else {
            dl->AddRect(caret, caret_max, theme.cursor);
        }
    }
    dl->PopClipRect();

    if (item.active) {
        adapter::set_ime_caret(caret, cell_h);
        if (source.can_input())
            handle_keyboard(str_id, source, snap);
    }
    ImGui::PopFont();

    draw_paste_popup(source);
    if (!notice_.empty()) {
        ImGui::SetTooltip("%s", notice_.c_str());
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            notice_.clear();
    }
    return item.active;
}

}  // namespace agentdesk::ui
