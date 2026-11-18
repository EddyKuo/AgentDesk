#include "agentdesk/ui/terminal_view.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <set>

#include "agentdesk/ui/clipboard.hpp"
#include "agentdesk/ui/imgui_adapter.hpp"

namespace agentdesk::ui {

namespace {

using Clock = std::chrono::steady_clock;
constexpr auto kResizeDebounce = std::chrono::milliseconds(50);  // design doc 3.5
constexpr std::uint64_t kScreenKey = 1ull << 62;                 // screen rows: kScreenKey + row
constexpr auto kDragScrollEvery = std::chrono::milliseconds(50);  // herdr src/terminal.rs:1174

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
    auto pick = [&](bool def, std::int8_t ansi, const terminal::Rgb& rgb, ImU32 fallback) {
        if (def)
            return fallback;
        if (ansi >= 0 && theme.ansi != nullptr)
            return (*theme.ansi)[static_cast<std::size_t>(ansi)];
        return to_color(rgb);
    };
    CellColors out{pick(cell.default_fg, cell.fg_ansi, cell.fg, theme.default_fg),
                   pick(cell.default_bg, cell.bg_ansi, cell.bg, theme.default_bg)};
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

char32_t next_cp(const std::string& s, std::size_t& i) {
    const auto b0 = static_cast<unsigned char>(s[i]);
    const std::size_t len = b0 < 0x80 ? 1 : (b0 & 0xE0) == 0xC0 ? 2 : (b0 & 0xF0) == 0xE0 ? 3 : 4;
    char32_t cp = len == 1 ? b0 : len == 2 ? (b0 & 0x1Fu) : len == 3 ? (b0 & 0x0Fu) : (b0 & 0x07u);
    for (std::size_t m = 1; m < len && i + m < s.size(); ++m)
        cp = (cp << 6) | (static_cast<unsigned char>(s[i + m]) & 0x3Fu);
    i += len;
    return cp;
}

// Cells taken by s[0, bytes).
int columns_of(const std::string& s, std::size_t bytes) {
    int cols = 0;
    for (std::size_t i = 0; i < std::min(bytes, s.size());)
        cols += is_wide_codepoint(next_cp(s, i)) ? 2 : 1;
    return cols;
}

std::string lower_ascii(std::string s) {
    for (char& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool is_word_char(char32_t c) {
    if (c == 0 || c == U' ' || c == U'\t')
        return false;
    for (const char32_t d : std::u32string_view(U"()[]{}<>\"'`,;|"))
        if (c == d)
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

// Plain-text line (history) laid out on the grid with the approximate width table.
void draw_text_line(ImDrawList* dl, ImFont* font, float size, ImFontBaked* baked, float x0, float y,
                    float cell_w, int max_cols, ImU32 fg, const std::string& text, std::string& scratch) {
    int col = 0;
    for (std::size_t i = 0; i < text.size() && col < max_cols;) {
        const char32_t cp = next_cp(text, i);
        terminal::Cell c;
        c.chars[0] = cp;
        const int w = is_wide_codepoint(cp) ? 2 : 1;
        c.width = static_cast<std::uint8_t>(w);
        if (cp != U' ')
            draw_cell_text(dl, font, size, baked, x0 + static_cast<float>(col) * cell_w, y,
                           cell_w * static_cast<float>(w), fg, c, scratch);
        col += w;
    }
}

// Combined lines of one frame: fetched history (oldest first), then the screen rows. Keys name a
// line across frames: a history line_id, or kScreenKey + screen row.
struct Lines {
    const std::vector<HistoryLineText>* hist = nullptr;
    const terminal::ScreenSnapshot* snap = nullptr;

    [[nodiscard]] int available() const { return hist != nullptr ? static_cast<int>(hist->size()) : 0; }
    [[nodiscard]] int total() const { return available() + snap->rows; }
    [[nodiscard]] std::uint64_t key(int idx) const {
        if (idx < available())
            return (*hist)[static_cast<std::size_t>(idx)].line_id;
        return kScreenKey + static_cast<std::uint64_t>(idx - available());
    }
    [[nodiscard]] int index(std::uint64_t key) const {
        if (key >= kScreenKey)
            return available() + std::min(static_cast<int>(key - kScreenKey), snap->rows - 1);
        if (hist == nullptr)
            return 0;
        const auto it = std::lower_bound(hist->begin(), hist->end(), key,
                                         [](const HistoryLineText& l, std::uint64_t k) { return l.line_id < k; });
        return static_cast<int>(it - hist->begin());
    }
    [[nodiscard]] bool wrapped(int idx) const {
        if (idx < available())
            return (*hist)[static_cast<std::size_t>(idx)].wrapped;
        return snap->row_wrapped(idx - available());
    }
    [[nodiscard]] LineCells cells(int idx) const {
        if (idx < available()) {
            const auto& l = (*hist)[static_cast<std::size_t>(idx)];
            return cells_from_text(l.text, l.wrapped);
        }
        const int r = idx - available();
        LineCells out;
        const auto first = snap->cells.begin() + static_cast<std::ptrdiff_t>(r) * snap->cols;
        out.cells.assign(first, first + snap->cols);
        out.wrapped = snap->row_wrapped(r);
        return out;
    }
};

}  // namespace

std::optional<Clock::time_point> TerminalView::next_deadline() const {
    if (pending_size_ && pending_size_ != requested_size_)
        return pending_since_ + kResizeDebounce;
    if (selecting_)
        return drag_scrolled_at_ + kDragScrollEvery;  // keeps scrolling while held past an edge
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

void TerminalView::clear_selection() {
    sel_a_.reset();
    sel_b_.reset();
    selecting_ = false;
}

void TerminalView::copy_selection(const terminal::ScreenSnapshot& snap) {
    if (!sel_a_ || !sel_b_)
        return;
    const Lines lines{hist_.get(), &snap};
    int ia = lines.index(sel_a_->key);
    int ib = lines.index(sel_b_->key);
    CellPos a{0, sel_a_->col};
    CellPos b{0, sel_b_->col};
    if (ib < ia || (ib == ia && b.col < a.col)) {
        std::swap(ia, ib);
        std::swap(a, b);
    }
    std::vector<LineCells> cells;
    for (int i = ia; i <= ib; ++i)
        cells.push_back(lines.cells(i));
    b.row = ib - ia;
    const std::string text = selection_text(cells, a, b);
    if (!text.empty())
        ImGui::SetClipboardText(text.c_str());
}

void TerminalView::paste_paths(TerminalSource& source, const std::vector<std::string>& paths) {
    if (const auto text = quote_paths(paths))
        request_paste(source, *text);
    else
        notice_ = "Paths containing control characters cannot be pasted safely";
}

void TerminalView::paste_clipboard(TerminalSource& source, bool full) {
    const ClipboardContent clip = read_clipboard();
    if (full && !clip.files.empty()) {
        paste_paths(source, clip.files);  // copied files paste as if dropped
        return;
    }
    if (clip.text) {
        // Windows console programs expect the terminal to paste clipboard text.
        request_paste(source, *clip.text);
        return;
    }
    if (full && !clip.image)
        return;
    // An image alone (a screenshot): Ctrl+V, so the program reads the clipboard itself.
    scroll_offset_ = 0;
    source.send(host::TextInput{std::u32string(1, U'v'), terminal::kModCtrl});
}

void TerminalView::run_command(const std::string& cmd, TerminalSource& source,
                               const terminal::ScreenSnapshot& snap) {
    if (cmd.rfind("send:", 0) == 0) {
        scroll_offset_ = 0;
        source.send(host::RawInput{cmd.substr(5)});
    } else if (cmd == "terminal.copy") {
        copy_selection(snap);
    } else if (cmd == "terminal.paste") {
        paste_clipboard(source, true);
    } else if (cmd == "terminal.paste_image") {
        paste_clipboard(source, false);
    } else if (cmd == "terminal.search") {
        open_search();
    } else if (cmd == "terminal.select_all") {
        // The screen and its history (herdr src/vt.rs:1144). Key 0 is before every history line.
        clear_selection();
        source.request_history(static_cast<std::size_t>(kMaxScrollback));
        sel_a_ = SelPoint{0, 0};
        sel_b_ = SelPoint{kScreenKey + static_cast<std::uint64_t>(std::max(0, snap.rows - 1)), snap.cols - 1};
    } else if (cmd == "terminal.scroll:-page") {
        scroll_offset_ = std::min(scroll_offset_ + snap.rows, kMaxScrollback);
    } else if (cmd == "terminal.scroll:page") {
        scroll_offset_ = std::max(0, scroll_offset_ - snap.rows);
    } else {
        commands_.push_back(cmd);  // the app's command
    }
}

void TerminalView::handle_keyboard(const char* str_id, TerminalSource& source,
                                   const terminal::ScreenSnapshot& snap) {
    const ImGuiIO& io = ImGui::GetIO();
    const Modifiers mods{io.KeyCtrl, io.KeyShift, io.KeyAlt};
    const bool alt_gr = mods.ctrl && mods.alt;  // AltGr arrives as Ctrl+Alt with a printable char
    ImGuiKeyChord held = 0;
    if (mods.ctrl)
        held |= ImGuiMod_Ctrl;
    if (mods.shift)
        held |= ImGuiMod_Shift;
    if (mods.alt)
        held |= ImGuiMod_Alt;

    // Bound chords first: global ones go to the app, terminal ones run here. Neither reaches
    // the program.
    std::set<int> consumed;
    if (keymap != nullptr && held != 0) {
        for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; ++k) {
            const auto key = static_cast<ImGuiKey>(k);
            if ((key >= ImGuiKey_LeftCtrl && key <= ImGuiKey_RightSuper) ||
                !adapter::key_pressed_owned(key, str_id))
                continue;
            const auto chord = static_cast<ImGuiKeyChord>(held | key);
            // herdr: terminal section > layout section > global.
            if (const auto t = keymap->terminal(chord)) {
                if (*t != "terminal") {  // "terminal": the program gets the key
                    run_command(*t, source, snap);
                    consumed.insert(k);
                }
            } else if (const auto g = keymap->global(chord)) {
                commands_.push_back(*g);
                consumed.insert(k);
            }
        }
    } else if (keymap != nullptr) {
        for (const ImGuiKey key : {ImGuiKey_F2}) {  // unmodified bound keys (F2 rename)
            if (!adapter::key_pressed_owned(key, str_id))
                continue;
            if (const auto g = keymap->global(key)) {
                commands_.push_back(*g);
                consumed.insert(key);
            }
        }
    }

    // Ctrl+C copies when something is selected, otherwise it is an interrupt for the program.
    if (!consumed.contains(ImGuiKey_C) && adapter::key_pressed_owned(ImGuiKey_C, str_id) &&
        classify_clipboard_chord(ImGuiKey_C, mods, has_selection()) == ClipboardAction::kCopy) {
        copy_selection(snap);
        clear_selection();
        consumed.insert(ImGuiKey_C);
    }

    for (const ImGuiKey key : kNamedKeys) {
        if (consumed.contains(key) || !adapter::key_pressed_owned(key, str_id))
            continue;
        if (const auto mapped = map_named_key(key)) {
            scroll_offset_ = 0;
            clear_selection();  // typing clears a selection (herdr src/terminal.rs:1025-1028)
            source.send(host::KeyInput{*mapped, to_terminal_mods(mods)});
        }
    }
    if (!consumed.contains(ImGuiKey_F12) && adapter::key_pressed_owned(ImGuiKey_F12, str_id)) {
        scroll_offset_ = 0;
        clear_selection();
        source.send(host::KeyInput{terminal::Key::kF12, to_terminal_mods(mods)});
    }

    // Ctrl/Alt chords (Ctrl+C without selection lands here as an interrupt byte).
    if ((mods.ctrl || mods.alt) && !alt_gr) {
        for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; ++k) {
            const auto key = static_cast<ImGuiKey>(k);
            const auto ch = chord_char(key);
            if (!ch || consumed.contains(k) || !adapter::key_pressed_owned(key, str_id))
                continue;
            scroll_offset_ = 0;
            clear_selection();
            source.send(host::TextInput{std::u32string(1, *ch), to_terminal_mods(mods)});
        }
    }

    // Printable text (including committed IME strings) only from the character queue.
    if (!mods.ctrl || alt_gr) {
        auto text = printable_from_queue(io.InputQueueCharacters.Data, io.InputQueueCharacters.Size);
        if (!text.empty()) {
            scroll_offset_ = 0;
            clear_selection();
        }
        source.send(host::TextInput{std::move(text), terminal::kModNone});
    }
}

void TerminalView::draw_context_menu(TerminalSource& source, const terminal::ScreenSnapshot& snap) {
    if (open_context_menu_) {
        ImGui::OpenPopup("##terminal-menu");
        open_context_menu_ = false;
    }
    if (menu_rows) {  // herdr menu: rows drawn by the app, 220 px wide
        const float k = ImGui::GetFontSize() / 13.0f;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(5 * k, 5 * k));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
        ImGui::SetNextWindowSizeConstraints(ImVec2(220 * k, 0), ImVec2(FLT_MAX, FLT_MAX));
        const bool open = ImGui::BeginPopup("##terminal-menu");
        ImGui::PopStyleVar(2);
        if (!open)
            return;
        if (menu_rows())
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    if (!ImGui::BeginPopup("##terminal-menu"))
        return;
    auto chord = [&](const char* cmd) {
        static std::string s;
        s = keymap != nullptr ? keymap->chord_for(cmd) : std::string();
        return s.c_str();
    };
    if (ImGui::MenuItem("Copy", chord("terminal.copy"), false, has_selection()))
        copy_selection(snap);
    if (ImGui::MenuItem("Paste", chord("terminal.paste"), false, source.can_input()))
        run_command("terminal.paste", source, snap);
    if (ImGui::MenuItem("Select all", chord("terminal.select_all")))
        run_command("terminal.select_all", source, snap);
    if (ImGui::MenuItem("Find...", chord("terminal.search")))
        open_search();
    ImGui::Separator();
    if (ImGui::MenuItem("Split right", chord("pane.split:right")))
        commands_.emplace_back("pane.split:right");
    if (ImGui::MenuItem("Split down", chord("pane.split:down")))
        commands_.emplace_back("pane.split:down");
    if (ImGui::MenuItem("Zoom pane", chord("pane.zoom")))
        commands_.emplace_back("pane.zoom");
    if (ImGui::MenuItem("Rename pane..."))
        commands_.emplace_back("pane.rename");
    // herdr pane menu: where right-clicks go (only matters while the program reads the mouse).
    if (ImGui::MenuItem(right_click_to_program ? "Use AgentDesk right-click menu" : "Send right-clicks to pane"))
        commands_.emplace_back(right_click_to_program ? "terminal.right_click:menu" : "terminal.right_click:program");
    if (!parent_name.empty() && ImGui::MenuItem(("Send back to " + parent_name).c_str()))
        commands_.emplace_back("pane.park");
    ImGui::Separator();
    if (ImGui::MenuItem("Close pane", chord("pane.close")))
        commands_.emplace_back("pane.close");
    ImGui::EndPopup();
}

void TerminalView::update_search(const std::vector<HistoryLineText>* hist,
                                 const terminal::ScreenSnapshot& snap) {
    const std::string query = lower_ascii(search_buf_);
    const std::size_t hist_n = hist != nullptr ? hist->size() : 0;
    if (query == search_query_ && hist_n == search_lines_seen_ && snap.seq == search_screen_seq_)
        return;
    const bool query_changed = query != search_query_;
    search_query_ = query;
    search_lines_seen_ = hist_n;
    search_screen_seq_ = snap.seq;
    matches_.clear();
    if (query.empty()) {
        match_index_ = -1;
        return;
    }
    auto scan = [&](int line, const std::string& text) {
        const std::string low = lower_ascii(text);
        for (std::size_t pos = low.find(query); pos != std::string::npos; pos = low.find(query, pos + 1)) {
            const int col = columns_of(text, pos);
            const int len = columns_of(text.substr(pos, query.size()), query.size());
            matches_.push_back({line, col, std::max(1, len)});
        }
    };
    for (std::size_t i = 0; i < hist_n; ++i)
        scan(static_cast<int>(i), (*hist)[i].text);
    for (int r = 0; r < snap.rows; ++r)
        scan(static_cast<int>(hist_n) + r, snap.row_text(r));
    if (matches_.empty()) {
        match_index_ = -1;
    } else if (query_changed || match_index_ < 0 || match_index_ >= static_cast<int>(matches_.size())) {
        match_index_ = static_cast<int>(matches_.size()) - 1;  // newest first, like a terminal find
        jump_to_match_ = true;
    }
}

void TerminalView::draw_search_bar(ImVec2 area_min, ImVec2 area_max) {
    const float w = std::min(360.0f * ImGui::GetStyle().FontScaleDpi, area_max.x - area_min.x - 16);
    char name[64];
    std::snprintf(name, sizeof name, "##find%08X", ImGui::GetID("find"));
    ImGui::SetNextWindowPos(ImVec2(area_max.x - w - 8, area_min.y + 8));
    ImGui::SetNextWindowSize(ImVec2(w, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 6));
    ImGui::Begin(name, nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDocking);
    ImGui::PopStyleVar();
    if (search_focus_) {
        ImGui::SetKeyboardFocusHere();
        search_focus_ = false;
    }
    char count[32];
    if (matches_.empty())
        std::snprintf(count, sizeof count, search_buf_[0] != 0 ? "0/0" : "");
    else
        std::snprintf(count, sizeof count, "%d/%d", match_index_ + 1, static_cast<int>(matches_.size()));
    const float right = ImGui::CalcTextSize("00000/00000").x + ImGui::GetFrameHeight() * 3 + 24;
    ImGui::SetNextItemWidth(std::max(80.0f, w - right));
    ImGui::InputTextWithHint("##q", "Find in terminal", search_buf_, sizeof search_buf_);
    const bool input_active = ImGui::IsItemActive();
    auto step = [&](int d) {
        if (matches_.empty())
            return;
        const int n = static_cast<int>(matches_.size());
        match_index_ = ((match_index_ + d) % n + n) % n;
        jump_to_match_ = true;
    };
    if (input_active && ImGui::IsKeyPressed(ImGuiKey_Enter))
        step(ImGui::GetIO().KeyShift ? 1 : -1);  // Enter: older match (up), Shift+Enter: newer
    ImGui::SameLine();
    ImGui::TextDisabled("%s", count);
    ImGui::SameLine();
    if (ImGui::ArrowButton("##up", ImGuiDir_Up))
        step(-1);
    ImGui::SameLine();
    if (ImGui::ArrowButton("##down", ImGuiDir_Down))
        step(1);
    ImGui::SameLine();
    const bool close = ImGui::Button("x") || (input_active && ImGui::IsKeyPressed(ImGuiKey_Escape));
    ImGui::End();
    if (close) {
        search_open_ = false;
        search_buf_[0] = '\0';
        matches_.clear();
        match_index_ = -1;
        search_query_.clear();
        refocus_ = true;
    }
}

void TerminalView::forward_mouse(TerminalSource& source, const terminal::ScreenSnapshot& snap, bool hovered,
                                 CellPos cell) {
    ImGuiIO& io = ImGui::GetIO();
    std::uint8_t mods = terminal::kModNone;
    if (io.KeyCtrl)
        mods |= terminal::kModCtrl;
    if (io.KeyAlt)
        mods |= terminal::kModAlt;
    const bool reporting = snap.mouse != terminal::MouseMode::kNone && !io.KeyShift;
    auto send = [&](int button, terminal::MouseAction action) {
        source.send(host::MouseInput{cell.row, cell.col, button, action, mods});
    };
    // ImGui buttons 0/1/2 = left/right/middle; terminal buttons 1/3/2.
    static constexpr std::pair<ImGuiMouseButton, int> kButtons[] = {
        {ImGuiMouseButton_Left, 1}, {ImGuiMouseButton_Middle, 2}, {ImGuiMouseButton_Right, 3}};
    for (const auto& [ib, tb] : kButtons) {
        const std::uint8_t bit = static_cast<std::uint8_t>(1u << tb);
        if (tb == 3 && !right_click_to_program)
            continue;  // right-click opens AgentDesk's menu
        if (reporting && hovered && ImGui::IsMouseClicked(ib)) {
            mouse_buttons_ |= bit;
            send(tb, terminal::MouseAction::kPress);
        }
        if ((mouse_buttons_ & bit) != 0 && ImGui::IsMouseReleased(ib)) {
            mouse_buttons_ = static_cast<std::uint8_t>(mouse_buttons_ & ~bit);
            send(tb, terminal::MouseAction::kRelease);
        }
    }
    if (!reporting)
        return;
    if (hovered && io.MouseWheel != 0.0f) {
        const int n = std::max(1, static_cast<int>(std::round(std::abs(io.MouseWheel))));
        for (int i = 0; i < n; ++i)
            send(io.MouseWheel > 0 ? 4 : 5, terminal::MouseAction::kPress);
    }
    // Motion: while a button is held (drag mode) or always (any-motion mode), once per cell.
    const bool want_move = snap.mouse == terminal::MouseMode::kMove ||
                           (snap.mouse == terminal::MouseMode::kDrag && mouse_buttons_ != 0);
    if (want_move && (hovered || mouse_buttons_ != 0) && last_mouse_cell_ != cell)
        send(0, terminal::MouseAction::kMove);
    last_mouse_cell_ = cell;
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
    // herdr terminal_cell_width / terminal_line_height multipliers (0.8..2); text sits centred
    // vertically in a taller cell.
    const float cell_w =
        std::max(1.0f, std::round(baked->GetCharAdvance(static_cast<ImWchar>('M')) * std::clamp(cell_width, 0.8f, 2.0f)));
    const float text_h = std::round(ImGui::GetTextLineHeight());
    const float cell_h = std::max(1.0f, std::round(text_h * std::clamp(line_height, 0.8f, 2.0f)));
    const float text_dy = std::floor((cell_h - text_h) * 0.5f);

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    handle_resize(source, grid_for_area(avail.x, avail.y, cell_w, cell_h), snap);

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 area_max(origin.x + std::max(avail.x, cell_w * 2), origin.y + std::max(avail.y, cell_h));
    const auto item = adapter::grid_item(str_id, origin, area_max, request_focus || refocus_);
    refocus_ = false;
    if (!item.visible) {
        ImGui::PopFont();
        return false;
    }

    const GridSize grid{snap.rows, snap.cols};
    ImGuiIO& io = ImGui::GetIO();
    hovered_ = item.hovered;
    hist_ = source.history();
    for (auto& c : std::exchange(requested_, {}))
        run_command(c, source, snap);
    for (auto& paths : std::exchange(dropped_, {})) {
        if (source.can_input())
            paste_paths(source, paths);
    }

    // ---- mouse to the program (it turned reporting on; Shift keeps the mouse for AgentDesk)
    const bool report_mouse = snap.mouse != terminal::MouseMode::kNone && !io.KeyShift && scroll_offset_ == 0;
    // Wheel in a full-screen program that does not read the mouse: arrow keys, like xterm's
    // alternate scroll mode (there is no history to scroll on the alternate screen).
    const bool wheel_as_keys = !report_mouse && snap.alt_screen && snap.mouse == terminal::MouseMode::kNone &&
                               !io.KeyShift && scroll_offset_ == 0;
    if (item.hovered && io.MouseWheel != 0.0f && wheel_as_keys) {
        const int n = std::max(1, static_cast<int>(std::round(std::abs(io.MouseWheel) * 3.0f * scroll_speed)));
        for (int i = 0; i < n; ++i)
            source.send(host::KeyInput{io.MouseWheel > 0 ? terminal::Key::kUp : terminal::Key::kDown});
    }

    // ---- history window: combined lines = fetched history (oldest first) + screen rows.
    if (item.hovered && io.MouseWheel != 0.0f && !report_mouse && !wheel_as_keys) {
        scroll_offset_ = std::clamp(scroll_offset_ + static_cast<int>(io.MouseWheel * 3.0f * scroll_speed), 0,
                                    kMaxScrollback);
    }
    if (scroll_offset_ > 0 || search_open_) {
        source.request_history(static_cast<std::size_t>(
            search_open_ ? kMaxScrollback : std::min(scroll_offset_ + snap.rows * 2, kMaxScrollback)));
    }
    const auto hist = hist_;
    const Lines lines{hist.get(), &snap};
    const int available = lines.available();
    last_available_ = available;
    scroll_offset_ = std::min(scroll_offset_, available + snap.rows);  // a little past: fetch more
    if (search_open_)
        update_search(hist.get(), snap);
    if (jump_to_match_ && match_index_ >= 0 && match_index_ < static_cast<int>(matches_.size())) {
        jump_to_match_ = false;
        const int line = matches_[static_cast<std::size_t>(match_index_)].line;
        scroll_offset_ =
            line >= available ? 0 : std::clamp(available - line, 0, available);  // match on top row
    }
    const int offset = std::min(scroll_offset_, available);
    const int top = available - offset;  // combined index of the first view row
    const CellPos mouse_cell = cell_at(io.MousePos.x, io.MousePos.y, origin.x, origin.y, cell_w, cell_h, grid);

    // ---- Ctrl+hover links (herdr src/terminal.rs:1141-1166, 1486-1489): OSC 8 links, then URLs
    // and file paths in the text. Opened only by the person's own Ctrl+click (2521-2528).
    const bool link_key = io.KeyCtrl && !io.KeyShift && !io.KeyAlt;
    const int mouse_line = top + mouse_cell.row;
    if (!link_key || !item.hovered) {
        hover_link_ = HoverLink{};
    } else if (hover_link_.line != mouse_line || hover_link_.col != mouse_cell.col || hover_link_.seq != snap.seq ||
               hover_link_.available != available) {
        HoverLink h;
        h.line = mouse_line;
        h.col = mouse_cell.col;
        h.seq = snap.seq;
        h.available = available;
        const std::string cwd = source.cwd();
        bool osc8 = false;
        if (mouse_line >= available) {
            const int r = mouse_line - available;
            for (const auto& l : snap.links) {
                if (l.row == r && mouse_cell.col >= l.col0 && mouse_cell.col < l.col1) {
                    osc8 = true;
                    if (auto t = resolve_link(l.uri, cwd)) {
                        h.target = std::move(t);
                        h.spans.push_back({mouse_line, {l.col0, l.col1}});
                    }
                    break;
                }
            }
        }
        // Text of lines [first, last], joined: in joined lines only an existing file proves the
        // join right.
        auto try_lines = [&](int first, int last) {
            std::string text;
            std::vector<std::pair<int, std::pair<int, int>>> where;  // per byte: line, cells
            std::size_t at = std::string::npos;
            for (int i = first; i <= last; ++i) {
                const LineCells lc = lines.cells(i);
                for (int c = 0; c < static_cast<int>(lc.cells.size()); ++c) {
                    const auto& cell = lc.cells[static_cast<std::size_t>(c)];
                    if (cell.width == 0)
                        continue;
                    const int span = std::max<int>(1, cell.width);
                    if (i == mouse_line && mouse_cell.col >= c && mouse_cell.col < c + span)
                        at = text.size();
                    const std::size_t before = text.size();
                    if (cell.chars[0] == 0) {
                        text.push_back(' ');
                    } else {
                        for (const char32_t cp : cell.chars) {
                            if (cp == 0)
                                break;
                            terminal::append_utf8(text, cp);
                        }
                    }
                    for (std::size_t b = before; b < text.size(); ++b)
                        where.push_back({i, {c, c + span}});
                }
            }
            if (at == std::string::npos)
                return false;
            const auto range = find_link(text, at);
            if (!range)
                return false;
            auto t = resolve_link(text.substr(range->first, range->second - range->first), cwd);
            if (!t || (first != last && !t->file))
                return false;
            h.target = std::move(t);
            for (std::size_t b = range->first; b < range->second; ++b) {
                const auto& [line, cells] = where[b];
                if (!h.spans.empty() && h.spans.back().first == line)
                    h.spans.back().second.second = std::max(h.spans.back().second.second, cells.second);
                else
                    h.spans.push_back({line, cells});
            }
            return true;
        };
        if (!osc8 && mouse_line >= 0 && mouse_line < lines.total()) {
            int first = mouse_line;
            int last = mouse_line;
            while (first > 0 && lines.wrapped(first - 1) && mouse_line - first < 8)
                --first;
            while (last + 1 < lines.total() && lines.wrapped(last) && last - mouse_line < 8)
                ++last;
            if (!try_lines(first, last) && first != last)
                try_lines(mouse_line, mouse_line);
        }
        hover_link_ = std::move(h);
    }
    bool link_clicked = false;
    if (link_key && item.hovered && hover_link_.target) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            open_link(*hover_link_.target);
            link_clicked = true;
        }
    }

    if ((report_mouse || mouse_buttons_ != 0) && !link_clicked)
        forward_mouse(source, snap, item.hovered, mouse_cell);

    // ---- scrollbar (only when there is history to scroll to)
    const float bar_w = std::max(6.0f, std::round(cell_w * 0.5f));
    const float track_h = area_max.y - origin.y;
    const int total = available + snap.rows;
    const float thumb_h =
        available > 0 ? std::max(cell_h, track_h * static_cast<float>(snap.rows) / total) : 0;
    const bool over_bar = available > 0 && io.MousePos.x >= area_max.x - bar_w * 1.5f &&
                          io.MousePos.x <= area_max.x && io.MousePos.y >= origin.y &&
                          io.MousePos.y <= area_max.y;
    if (item.clicked && over_bar)
        dragging_scrollbar_ = true;
    if (dragging_scrollbar_) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && available > 0) {
            const float frac = std::clamp(
                (io.MousePos.y - origin.y - thumb_h * 0.5f) / std::max(1.0f, track_h - thumb_h), 0.0f, 1.0f);
            scroll_offset_ = available - static_cast<int>(std::round(frac * static_cast<float>(available)));
        } else {
            dragging_scrollbar_ = false;
        }
    }

    // ---- mouse selection: click, drag, double-click word, triple-click line. Cells of history
    // lines select the same way as the screen's.
    auto point_at = [&](CellPos p) { return SelPoint{lines.key(top + p.row), p.col}; };
    if (item.clicked && !over_bar && !report_mouse && !link_clicked) {
        const CellPos p = mouse_cell;
        const int idx = top + p.row;
        clear_selection();
        const int clicks = io.MouseClickedCount[ImGuiMouseButton_Left];
        if (clicks == 2) {
            const LineCells lc = lines.cells(idx);
            const int n = static_cast<int>(lc.cells.size());
            auto ch = [&](int c) -> char32_t {
                if (c < 0 || c >= n)
                    return 0;
                const auto& cell = lc.cells[static_cast<std::size_t>(c)];
                if (cell.width == 0 && c > 0)
                    return lc.cells[static_cast<std::size_t>(c - 1)].chars[0];  // right half of a wide char
                return cell.chars[0];
            };
            int a = p.col;
            int b = p.col;
            while (a > 0 && is_word_char(ch(a - 1)))
                --a;
            while (b + 1 < n && is_word_char(ch(b + 1)))
                ++b;
            if (is_word_char(ch(p.col))) {
                sel_a_ = SelPoint{lines.key(idx), a};
                sel_b_ = SelPoint{lines.key(idx), b};
            }
        } else if (clicks >= 3) {
            // The whole line, through its soft wraps.
            int first = idx;
            int last = idx;
            while (first > 0 && lines.wrapped(first - 1))
                --first;
            while (last + 1 < lines.total() && lines.wrapped(last))
                ++last;
            sel_a_ = SelPoint{lines.key(first), 0};
            sel_b_ = SelPoint{lines.key(last), std::max(snap.cols, static_cast<int>(lines.cells(last).cells.size())) - 1};
        } else {
            sel_a_ = point_at(p);
            selecting_ = true;
            drag_scrolled_at_ = Clock::now();
        }
        if (!selecting_ && copy_on_select && has_selection())
            copy_selection(snap);
    }
    if (selecting_) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            // Held past the top or bottom edge: 1-5 lines every 50 ms (herdr src/terminal.rs:1172-1192).
            const auto now = Clock::now();
            if (now - drag_scrolled_at_ >= kDragScrollEvery) {
                drag_scrolled_at_ = now;
                const int n = selection_scroll_lines(io.MousePos.y - origin.y, cell_h, snap.rows);
                if (n < 0)
                    scroll_offset_ = std::min(scroll_offset_ - n, kMaxScrollback);
                else if (n > 0)
                    scroll_offset_ = std::max(0, scroll_offset_ - n);
            }
            const SelPoint end = point_at(mouse_cell);
            if (sel_a_ && (end != *sel_a_ || sel_b_))
                sel_b_ = end;
        } else {
            selecting_ = false;
            if (copy_on_select && has_selection())
                copy_selection(snap);
        }
    }
    if (item.hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && !(report_mouse && right_click_to_program))
        open_context_menu_ = true;
    // Middle-click pastes when the program doesn't take it (herdr src/terminal.rs:2567-2576).
    if (item.hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle) && !report_mouse && source.can_input())
        paste_clipboard(source, true);

    // ---- paint
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(origin, area_max, true);
    dl->AddRectFilled(origin, area_max, theme.default_bg);

    std::string run;
    for (int view_row = 0; view_row < offset && view_row < snap.rows; ++view_row) {
        const float y = origin.y + static_cast<float>(view_row) * cell_h;
        draw_text_line(dl, font, font_size, baked, origin.x, y + text_dy, cell_w, snap.cols, theme.default_fg,
                       (*hist)[static_cast<std::size_t>(top + view_row)].text, run);
    }
    const int first_row =
        std::max(0, static_cast<int>((dl->GetClipRectMin().y - origin.y) / cell_h) - offset);
    const int last_row =
        std::min(snap.rows - offset, static_cast<int>((dl->GetClipRectMax().y - origin.y) / cell_h) + 1);
    for (int row = first_row; row < last_row; ++row) {
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
                if (cell.chars[0] != U' ')
                    draw_cell_text(dl, font, font_size, baked, x, y + text_dy, cell_w * static_cast<float>(span), fg,
                                   cell, run);
                if (cell.attrs & terminal::kAttrUnderline) {
                    dl->AddLine(ImVec2(x, y + cell_h - 1),
                                ImVec2(x + cell_w * static_cast<float>(span), y + cell_h - 1), fg);
                }
            }
            col += cell.width == 0 ? 1 : span;
        }
    }

    // Search matches.
    for (int i = 0; i < static_cast<int>(matches_.size()); ++i) {
        const auto& m = matches_[static_cast<std::size_t>(i)];
        const int r = m.line - top;
        if (r < 0 || r >= snap.rows)
            continue;
        const ImVec2 a(origin.x + static_cast<float>(m.col) * cell_w,
                       origin.y + static_cast<float>(r) * cell_h);
        dl->AddRectFilled(a, ImVec2(a.x + static_cast<float>(m.len) * cell_w, a.y + cell_h),
                          i == match_index_ ? theme.match_current : theme.match, 2.0f);
    }

    if (sel_a_ && sel_b_) {
        int ia = lines.index(sel_a_->key);
        int ib = lines.index(sel_b_->key);
        int ca = sel_a_->col;
        int cb = sel_b_->col;
        if (ib < ia || (ib == ia && cb < ca)) {
            std::swap(ia, ib);
            std::swap(ca, cb);
        }
        for (int r = 0; r < snap.rows; ++r) {
            const int idx = top + r;
            if (idx < ia || idx > ib)
                continue;
            const int c0 = std::max(0, idx == ia ? ca : 0);
            const int c1 = std::min(snap.cols - 1, idx == ib ? cb : snap.cols - 1);
            if (c1 < c0)
                continue;
            const float y = origin.y + static_cast<float>(r) * cell_h;
            dl->AddRectFilled(ImVec2(origin.x + static_cast<float>(c0) * cell_w, y),
                              ImVec2(origin.x + static_cast<float>(c1 + 1) * cell_w, y + cell_h), theme.selection);
        }
    }
    if (link_key && item.hovered && hover_link_.target) {
        for (const auto& [line, cols] : hover_link_.spans) {
            const int r = line - top;
            if (r < 0 || r >= snap.rows)
                continue;
            const float y = origin.y + static_cast<float>(r + 1) * cell_h - 1.0f;
            dl->AddLine(ImVec2(origin.x + static_cast<float>(cols.first) * cell_w, y),
                        ImVec2(origin.x + static_cast<float>(cols.second) * cell_w, y), theme.default_fg);
        }
    }

    const ImVec2 caret(origin.x + static_cast<float>(snap.cursor.col) * cell_w,
                       origin.y + static_cast<float>(snap.cursor.row + offset) * cell_h);
    if (offset == 0 && snap.cursor.visible && snap.cursor.row < snap.rows && snap.cursor.col < snap.cols) {
        // On the left half of a wide character the cursor covers both cells.
        const int cursor_span = std::max<int>(1, snap.at(snap.cursor.row, snap.cursor.col).width);
        const ImVec2 caret_max(caret.x + cell_w * static_cast<float>(cursor_span), caret.y + cell_h);
        if (item.active)
            dl->AddRectFilled(caret, caret_max, (theme.cursor & 0x00FFFFFF) | 0x90000000);
        else
            dl->AddRect(caret, caret_max, theme.cursor);
    }
    if (available > 0 && (offset > 0 || over_bar || dragging_scrollbar_)) {
        const float frac = available > 0 ? static_cast<float>(top) / static_cast<float>(available) : 1.0f;
        const float ty = origin.y + (track_h - thumb_h) * frac;
        dl->AddRectFilled(ImVec2(area_max.x - bar_w, ty), ImVec2(area_max.x - 2, ty + thumb_h),
                          theme.scrollbar, bar_w * 0.5f);
    }
    dl->PopClipRect();

    if (item.active) {
        adapter::set_ime_caret(caret, cell_h);
        if (source.can_input())
            handle_keyboard(str_id, source, snap);
        else if (keymap != nullptr) {  // read-only: app shortcuts still work
            const ImGuiIO& kio = ImGui::GetIO();
            ImGuiKeyChord held = 0;
            if (kio.KeyCtrl)
                held |= ImGuiMod_Ctrl;
            if (kio.KeyShift)
                held |= ImGuiMod_Shift;
            if (kio.KeyAlt)
                held |= ImGuiMod_Alt;
            for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; ++k) {
                const auto key = static_cast<ImGuiKey>(k);
                if ((key >= ImGuiKey_LeftCtrl && key <= ImGuiKey_RightSuper) ||
                    !adapter::key_pressed_owned(key, str_id))
                    continue;
                if (const auto g = keymap->global(static_cast<ImGuiKeyChord>(held | key)))
                    commands_.push_back(*g);
            }
        }
    }
    ImGui::PopFont();

    if (search_open_)
        draw_search_bar(origin, area_max);
    draw_context_menu(source, snap);
    draw_paste_popup(source);
    if (!notice_.empty()) {
        ImGui::SetTooltip("%s", notice_.c_str());
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            notice_.clear();
    }
    return item.active;
}

}  // namespace agentdesk::ui
