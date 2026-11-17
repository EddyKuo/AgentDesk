#include "agentdesk/ui/terminal_view.hpp"

#include <algorithm>
#include <array>

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

}  // namespace

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

    // Clipboard chords first: they decide what Ctrl+C means.
    for (const ImGuiKey key : {ImGuiKey_C, ImGuiKey_V}) {
        if (!adapter::key_pressed_owned(key, str_id))
            continue;
        switch (classify_clipboard_chord(key, mods, has_selection())) {
            case ClipboardAction::kCopy:
                ImGui::SetClipboardText(selection_text(snap, *sel_anchor_, *sel_end_).c_str());
                sel_anchor_.reset();
                sel_end_.reset();
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
            source.send(host::KeyInput{*mapped, to_terminal_mods(mods)});
        }
    }
    if (adapter::key_pressed_owned(ImGuiKey_F12, str_id)) {
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
            source.send(host::TextInput{std::u32string(1, *ch), to_terminal_mods(mods)});
        }
    }

    // Printable text (including committed IME strings) only from the character queue.
    if (!mods.ctrl || alt_gr) {
        auto text = printable_from_queue(io.InputQueueCharacters.Data, io.InputQueueCharacters.Size);
        if (!text.empty())
            source.send(host::TextInput{std::move(text), terminal::kModNone});
    }
}

bool TerminalView::draw(const char* str_id, TerminalSource& source, const FontSet& fonts,
                        bool request_focus) {
    const auto snap_ptr = source.snapshot();
    if (!snap_ptr)
        return false;
    const terminal::ScreenSnapshot& snap = *snap_ptr;

    if (fonts.mono != nullptr)
        ImGui::PushFont(fonts.mono, 0.0f);
    const float cell_w = ImGui::CalcTextSize("M").x;
    const float cell_h = ImGui::GetTextLineHeight();

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    handle_resize(source, grid_for_area(avail.x, avail.y, cell_w, cell_h), snap);

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 area_max(origin.x + std::max(avail.x, cell_w * 2), origin.y + std::max(avail.y, cell_h));
    const auto item = adapter::grid_item(str_id, origin, area_max, request_focus);
    if (!item.visible) {
        if (fonts.mono != nullptr)
            ImGui::PopFont();
        return false;
    }

    const GridSize grid{snap.rows, snap.cols};
    // Mouse selection on the visible screen (line-id based history selection comes with M2 scrollback).
    if (item.clicked) {
        sel_anchor_ = cell_at(ImGui::GetIO().MousePos.x, ImGui::GetIO().MousePos.y, origin.x, origin.y,
                              cell_w, cell_h, grid);
        sel_end_.reset();
        selecting_ = true;
    }
    if (selecting_) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            const auto p = cell_at(ImGui::GetIO().MousePos.x, ImGui::GetIO().MousePos.y, origin.x, origin.y,
                                   cell_w, cell_h, grid);
            if (p != *sel_anchor_ || sel_end_)
                sel_end_ = p;
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
    for (int row = first_row; row < last_row; ++row) {
        const float y = origin.y + static_cast<float>(row) * cell_h;
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
        // Text: ASCII runs in one call; anything else positioned per cell so wide glyphs land on
        // their two-cell slot regardless of the font's advance (skill §6).
        col = 0;
        while (col < snap.cols) {
            const auto& cell = snap.at(row, col);
            const ImU32 fg = colors_of(cell, theme).fg;
            const float x = origin.x + static_cast<float>(col) * cell_w;
            if (cell.width == 0 || cell.chars[0] == 0 || cell.chars[0] == U' ') {
                ++col;
                continue;
            }
            if (cell.chars[0] < 0x80 && cell.chars[1] == 0) {
                run.clear();
                int end = col;
                while (end < snap.cols) {
                    const auto& c = snap.at(row, end);
                    if (c.width != 1 || c.chars[1] != 0 || c.chars[0] >= 0x80 ||
                        colors_of(c, theme).fg != fg || c.attrs != cell.attrs) {
                        break;
                    }
                    run.push_back(c.chars[0] == 0 ? ' ' : static_cast<char>(c.chars[0]));
                    ++end;
                }
                dl->AddText(ImVec2(x, y), fg, run.data(), run.data() + run.size());
                if (cell.attrs & terminal::kAttrUnderline) {
                    dl->AddLine(ImVec2(x, y + cell_h - 1),
                                ImVec2(origin.x + static_cast<float>(end) * cell_w, y + cell_h - 1), fg);
                }
                col = end;
                continue;
            }
            run.clear();
            for (const char32_t cp : cell.chars) {
                if (cp == 0)
                    break;
                terminal::append_utf8(run, cp);
            }
            dl->AddText(ImVec2(x, y), fg, run.data(), run.data() + run.size());
            col += std::max<int>(1, cell.width);
        }
    }

    if (has_selection()) {
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
                       origin.y + static_cast<float>(snap.cursor.row) * cell_h);
    if (snap.cursor.visible && snap.cursor.row < snap.rows && snap.cursor.col < snap.cols) {
        const ImVec2 caret_max(caret.x + cell_w, caret.y + cell_h);
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
    if (fonts.mono != nullptr)
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
