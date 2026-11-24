// VtEngine on top of libvterm: callbacks, input encoding, history, OSC handling, snapshots and
// the handover state format.
// Portions adapted from herdr-app (Apache-2.0) and modified; see THIRD_PARTY_NOTICES.md.
#include "agentdesk/terminal/vt_engine.hpp"

#include <algorithm>
#include <cstdlib>
#include <map>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>

#include <vterm.h>
#include <vterm_agentdesk.h>

#include "agentdesk/terminal/bytes.hpp"

namespace agentdesk::terminal {

namespace {

constexpr std::size_t kMaxTitleBytes = 1024;
constexpr std::size_t kMaxCwdBytes = 4096;
constexpr std::size_t kMaxPasteBytes = 1024 * 1024;
constexpr std::uint32_t kContinuation = 0xFFFFFFFFu;
constexpr std::size_t kMaxLinks = 1024;
constexpr std::size_t kMaxLinkBytes = 2048;

VTermModifier to_vterm_mods(std::uint8_t mods) {
    unsigned m = VTERM_MOD_NONE;
    if (mods & kModShift)
        m |= VTERM_MOD_SHIFT;
    if (mods & kModAlt)
        m |= VTERM_MOD_ALT;
    if (mods & kModCtrl)
        m |= VTERM_MOD_CTRL;
    return static_cast<VTermModifier>(m);
}

VTermKey to_vterm_key(Key key) {
    switch (key) {
        case Key::kEnter:
            return VTERM_KEY_ENTER;
        case Key::kTab:
            return VTERM_KEY_TAB;
        case Key::kBackspace:
            return VTERM_KEY_BACKSPACE;
        case Key::kEscape:
            return VTERM_KEY_ESCAPE;
        case Key::kUp:
            return VTERM_KEY_UP;
        case Key::kDown:
            return VTERM_KEY_DOWN;
        case Key::kLeft:
            return VTERM_KEY_LEFT;
        case Key::kRight:
            return VTERM_KEY_RIGHT;
        case Key::kInsert:
            return VTERM_KEY_INS;
        case Key::kDelete:
            return VTERM_KEY_DEL;
        case Key::kHome:
            return VTERM_KEY_HOME;
        case Key::kEnd:
            return VTERM_KEY_END;
        case Key::kPageUp:
            return VTERM_KEY_PAGEUP;
        case Key::kPageDown:
            return VTERM_KEY_PAGEDOWN;
        default:
            break;
    }
    const int n = static_cast<int>(key) - static_cast<int>(Key::kF1) + 1;
    return static_cast<VTermKey>(VTERM_KEY_FUNCTION(n));
}

// Removes control characters from text the program asked us to display (titles).
std::string sanitize_display_text(std::string_view in, std::size_t cap) {
    std::string out;
    out.reserve(std::min(in.size(), cap));
    for (const char c : in) {
        const auto u = static_cast<unsigned char>(c);
        if (u < 0x20 || u == 0x7F) {
            continue;
        }
        if (out.size() >= cap) {
            break;
        }
        out.push_back(c);
    }
    return out;
}

// Number of trailing bytes that form an incomplete UTF-8 sequence (0 when the tail is complete).
std::size_t incomplete_utf8_tail(std::string_view data) {
    const std::size_t limit = std::min<std::size_t>(3, data.size());
    for (std::size_t i = 1; i <= limit; ++i) {
        const auto b = static_cast<unsigned char>(data[data.size() - i]);
        if ((b & 0xC0) == 0x80) {
            continue;  // continuation byte: keep looking for the lead byte
        }
        if (b < 0xC0) {
            return 0;  // ASCII: nothing pending
        }
        const std::size_t need = b >= 0xF0 ? 4 : (b >= 0xE0 ? 3 : 2);
        return need > i ? i : 0;
    }
    return 0;
}

// What one history line counts against the history byte limit.
std::size_t history_line_bytes(const HistoryLine& line) {
    return line.cells.size() * sizeof(Cell) + sizeof(HistoryLine);
}

// An embedded bracketed-paste end marker would let pasted text escape the bracket: strip it
// until none is left. Removing one can join the bytes around it into a new one ("\x1b[20" +
// marker + "1~"), so the search steps back far enough to see that.
void strip_paste_end_markers(std::string& body) {
    static constexpr std::string_view kEnd = "\x1b[201~";
    for (auto pos = body.find(kEnd); pos != std::string::npos;) {
        body.erase(pos, kEnd.size());
        pos = body.find(kEnd, pos >= kEnd.size() - 1 ? pos - (kEnd.size() - 1) : 0);
    }
}

// Every line break (LF or CRLF) as Enter (CR).
std::string line_breaks_as_enter(const std::string& body) {
    std::string cr;
    cr.reserve(body.size());
    for (std::size_t i = 0; i < body.size(); ++i) {
        if (body[i] == '\r' && i + 1 < body.size() && body[i + 1] == '\n') {
            cr.push_back('\r');
            ++i;
        } else {
            cr.push_back(body[i] == '\n' ? '\r' : body[i]);
        }
    }
    return cr;
}

}  // namespace

void append_utf8(std::string& out, char32_t cp) {
    const auto u = static_cast<std::uint32_t>(cp);
    if (u < 0x80) {
        out.push_back(static_cast<char>(u));
    } else if (u < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (u >> 6)));
        out.push_back(static_cast<char>(0x80 | (u & 0x3F)));
    } else if (u < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (u >> 12)));
        out.push_back(static_cast<char>(0x80 | ((u >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (u & 0x3F)));
    } else if (u <= 0x10FFFF) {
        out.push_back(static_cast<char>(0xF0 | (u >> 18)));
        out.push_back(static_cast<char>(0x80 | ((u >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((u >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (u & 0x3F)));
    }
}

std::string ScreenSnapshot::row_text(int row) const {
    std::string out;
    for (int col = 0; col < cols; ++col) {
        const Cell& cell = at(row, col);
        if (cell.width == 0) {
            continue;
        }
        if (cell.chars[0] == 0) {
            out.push_back(' ');
            continue;
        }
        for (const char32_t cp : cell.chars) {
            if (cp == 0) {
                break;
            }
            append_utf8(out, cp);
        }
    }
    while (!out.empty() && out.back() == ' ') {
        out.pop_back();
    }
    return out;
}

// libvterm callbacks, routed to the owning engine.
struct VtEngine::Callbacks {
    static VtEngine& self(void* user) { return *static_cast<VtEngine*>(user); }

    static void output(const char* bytes, size_t len, void* user) {
        self(user).pty_input_.append(bytes, len);
    }

    static int damage(VTermRect, void* user) {
        self(user).bump();
        return 1;
    }
    static int moverect(VTermRect dest, VTermRect src, void* user) {
        auto& e = self(user);
        // Full-width moves are scrolls: OSC 8 links ride along with their rows.
        if (src.start_col == 0 && src.end_col == e.cols_ && dest.start_col == 0 && dest.end_col == e.cols_)
            e.move_links(src.start_row, src.end_row, dest.start_row, dest.end_row,
                         dest.start_row - src.start_row);
        e.bump();
        return 1;
    }
    static int movecursor(VTermPos pos, VTermPos, int visible, void* user) {
        auto& e = self(user);
        e.cursor_.row = pos.row;
        e.cursor_.col = pos.col;
        e.cursor_.visible = visible != 0;
        e.bump();
        return 1;
    }
    static int settermprop(VTermProp prop, VTermValue* val, void* user) {
        auto& e = self(user);
        switch (prop) {
            case VTERM_PROP_CURSORVISIBLE:
                e.cursor_.visible = val->boolean != 0;
                break;
            case VTERM_PROP_ALTSCREEN:
                e.alt_screen_ = val->boolean != 0;
                e.links_.clear();
                e.open_link_.reset();
                break;
            case VTERM_PROP_MOUSE:
                e.mouse_mode_ = val->number == VTERM_PROP_MOUSE_CLICK  ? MouseMode::kClick
                                : val->number == VTERM_PROP_MOUSE_DRAG ? MouseMode::kDrag
                                : val->number == VTERM_PROP_MOUSE_MOVE ? MouseMode::kMove
                                                                       : MouseMode::kNone;
                break;
            case VTERM_PROP_TITLE: {
                const VTermStringFragment& f = val->string;
                if (f.initial) {
                    e.title_pending_.clear();
                }
                if (e.title_pending_.size() < kMaxTitleBytes * 4) {
                    e.title_pending_.append(f.str, f.len);
                }
                if (f.final) {
                    e.title_ = sanitize_display_text(e.title_pending_, kMaxTitleBytes);
                    e.title_pending_.clear();
                }
                break;
            }
            default:
                return 0;
        }
        e.bump();
        return 1;
    }
    static int bell(void* user) {
        auto& e = self(user);
        if (e.events_.on_bell) {
            e.events_.on_bell();
        }
        return 1;
    }
    static int resize(int, int, void* user) {
        self(user).bump();
        return 1;
    }
    static int sb_pushline(int cols, const VTermScreenCell* cells, void* user) {
        self(user).push_history(cols, cells);
        return 1;
    }
    static int sb_popline(int cols, VTermScreenCell* cells, void* user) {
        return self(user).pop_history(cols, cells) ? 1 : 0;
    }
    static int sb_clear(void* user) {
        auto& e = self(user);
        e.history_.clear();
        e.history_bytes_ = 0;
        e.bump();
        return 1;
    }
    static int osc(int command, VTermStringFragment frag, void* user) {
        auto& e = self(user);
        if (frag.initial) {
            e.osc_.clear();
            e.osc_overflow_ = false;
            e.osc_command_ = command;
        }
        if (!e.osc_overflow_) {
            if (e.osc_.size() + frag.len > e.options_.max_osc_bytes) {
                e.osc_overflow_ = true;  // discard the whole sequence, never use a truncated one
                e.osc_.clear();
            } else {
                e.osc_.append(frag.str, frag.len);
            }
        }
        if (frag.final && !e.osc_overflow_) {
            e.handle_osc(e.osc_command_, std::exchange(e.osc_, {}));
        }
        return 1;
    }
};

void VtEngine::VTermDeleter::operator()(VTerm* vt) const noexcept {
    vterm_free(vt);
}

VtEngine::VtEngine(const VtEngineOptions& options, EngineEvents events)
    : vt_(vterm_new(options.rows, options.cols)),
      options_(options),
      events_(std::move(events)),
      rows_(options.rows),
      cols_(options.cols) {
    if (!vt_) {
        throw std::runtime_error("vterm_new failed");
    }
    vterm_set_utf8(vt_.get(), 1);
    vterm_output_set_callback(vt_.get(), &Callbacks::output, this);
    screen_ = vterm_obtain_screen(vt_.get());

    static const VTermScreenCallbacks kScreenCallbacks = {
        &Callbacks::damage,      &Callbacks::moverect,   &Callbacks::movecursor,
        &Callbacks::settermprop, &Callbacks::bell,       &Callbacks::resize,
        &Callbacks::sb_pushline, &Callbacks::sb_popline, &Callbacks::sb_clear,
    };
    vterm_screen_set_callbacks(screen_, &kScreenCallbacks, this);

    static const VTermStateFallbacks kFallbacks = {
        nullptr, nullptr, &Callbacks::osc, nullptr, nullptr, nullptr, nullptr,
    };
    vterm_screen_set_unrecognised_fallbacks(screen_, &kFallbacks, this);

    vterm_screen_enable_altscreen(screen_, 1);
    // Row merging (not SCROLL): scrolls reach moverect as they happen, so OSC 8 link positions
    // recorded mid-write stay in step with the screen.
    vterm_screen_set_damage_merge(screen_, VTERM_DAMAGE_ROW);
    vterm_screen_reset(screen_, 1);
    // OSC 52 stays disabled: no selection callbacks are registered (design doc 3.9).
}

VtEngine::~VtEngine() = default;

void VtEngine::feed(std::string_view bytes) {
    if (bytes.empty()) {
        return;
    }
    watch_win32_mode(bytes);
    // libvterm 0.3.3 emits U+FFFD when a UTF-8 sequence is split across two
    // vterm_input_write calls and control characters follow (reproduced, see M0 report).
    // Hold back at most 3 trailing bytes of an incomplete sequence and prepend them to the
    // next chunk: bytes are delayed, never dropped or reordered.
    std::string joined;
    std::string_view data = bytes;
    if (!utf8_carry_.empty()) {
        joined = std::exchange(utf8_carry_, {});
        joined.append(bytes);
        data = joined;
    }
    const std::size_t hold = incomplete_utf8_tail(data);
    const std::string_view ready = data.substr(0, data.size() - hold);
    utf8_carry_.assign(data.substr(data.size() - hold));
    if (!ready.empty()) {
        vterm_input_write(vt_.get(), ready.data(), ready.size());
        vterm_screen_flush_damage(screen_);
    }
}

void VtEngine::flush() {
    if (!utf8_carry_.empty()) {
        const std::string pending = std::exchange(utf8_carry_, {});
        vterm_input_write(vt_.get(), pending.data(), pending.size());
        vterm_screen_flush_damage(screen_);
    }
}

void VtEngine::resize(int rows, int cols) {
    rows = std::clamp(rows, 1, 500);
    cols = std::clamp(cols, 2, 1000);
    if (rows == rows_ && cols == cols_) {
        return;
    }
    rows_ = rows;
    cols_ = cols;
    vterm_set_size(vt_.get(), rows, cols);
    vterm_screen_flush_damage(screen_);
    links_.clear();
    open_link_.reset();
    ++generation_;
    bump();
}

// win32-input-mode (DEC mode 9001, which ConPTY requests): libvterm ignores the mode, so the
// request is read from the output as it streams by, including one split across writes. The last
// set or reset in the stream wins.
void VtEngine::watch_win32_mode(std::string_view bytes) {
    static constexpr std::string_view kOn = "\x1b[?9001h";
    static constexpr std::string_view kOff = "\x1b[?9001l";
    constexpr std::size_t kKeep = kOn.size() - 1;
    auto latest = [](std::string_view b) -> std::optional<bool> {
        const auto on = b.rfind(kOn);
        const auto off = b.rfind(kOff);
        if (on != std::string_view::npos)
            return off == std::string_view::npos || on > off;
        if (off != std::string_view::npos)
            return false;
        return std::nullopt;
    };
    std::string edge = std::exchange(win32_tail_, {});
    edge.append(bytes.substr(0, std::min(bytes.size(), kKeep)));
    if (auto on = latest(bytes); on.has_value())
        win32_on_ = *on;
    else if (auto e = latest(edge); e.has_value())
        win32_on_ = *e;
    const std::string_view end = bytes.size() >= kKeep ? bytes : std::string_view(edge);
    win32_tail_.assign(end.substr(end.size() - std::min(end.size(), kKeep)));
}

namespace {

// A key pressed and released as win32-input-mode events,
// `CSI Vk;Sc;Uc;Kd;Cs;Rc _` (Shift 0x10, left Ctrl 0x08, left Alt 0x02 held, repeat 1).
std::string win32_key(unsigned vk, unsigned scan, unsigned typed, std::uint8_t mods) {
    const unsigned held = ((mods & kModShift) ? 0x10u : 0u) | ((mods & kModCtrl) ? 0x08u : 0u) |
                          ((mods & kModAlt) ? 0x02u : 0u);
    std::string out;
    for (const int down : {1, 0}) {
        out += "\x1b[" + std::to_string(vk) + ';' + std::to_string(scan) + ';' + std::to_string(typed) + ';' +
               std::to_string(down) + ';' + std::to_string(held) + ";1_";
    }
    return out;
}

}  // namespace

void VtEngine::key(Key key, std::uint8_t mods) {
    // With win32-input-mode on, modified Enter and Escape go as
    // win32 key events (Shift+Enter reaches the program; ConPTY would otherwise merge a bare
    // Escape with the next key into an Alt chord).
    if (win32_on_) {
        if (key == Key::kEnter && (mods & (kModShift | kModCtrl | kModAlt)) != 0) {
            pty_input_ += win32_key(13, 28, (mods & kModCtrl) ? 10 : 13, mods);
            return;
        }
        if (key == Key::kEscape) {
            pty_input_ += win32_key(27, 1, 27, mods);
            return;
        }
    }
    vterm_keyboard_key(vt_.get(), to_vterm_key(key), to_vterm_mods(mods));
}

void VtEngine::text(char32_t codepoint, std::uint8_t mods) {
    vterm_keyboard_unichar(vt_.get(), static_cast<uint32_t>(codepoint), to_vterm_mods(mods));
}

void VtEngine::mouse(int row, int col, int button, MouseAction action, std::uint8_t mods) {
    if (mouse_mode_ == MouseMode::kNone)
        return;
    row = std::clamp(row, 0, std::max(0, rows_ - 1));
    col = std::clamp(col, 0, std::max(0, cols_ - 1));
    const VTermModifier m = to_vterm_mods(mods);
    // libvterm reports a move only when a button is held (drag mode) or in any-motion mode.
    vterm_mouse_move(vt_.get(), row, col, m);
    if (action != MouseAction::kMove && button >= 1 && button <= 5)
        vterm_mouse_button(vt_.get(), button, action == MouseAction::kPress, m);
}

void VtEngine::paste(std::string_view utf8) {
    if (utf8.size() > kMaxPasteBytes) {
        throw std::invalid_argument("paste exceeds 1 MiB");
    }
    if (utf8.find('\0') != std::string_view::npos) {
        throw std::invalid_argument("paste contains NUL");
    }
    // Bracketed paste markers are emitted only when the program enabled mode 2004.
    const std::size_t before = pty_input_.size();
    vterm_keyboard_start_paste(vt_.get());
    const bool bracketed = pty_input_.size() != before;
    std::string body(utf8);
    if (bracketed)
        strip_paste_end_markers(body);
    else
        body = line_breaks_as_enter(body);  // a program without bracketed paste sees Enter
    pty_input_.append(body);
    vterm_keyboard_end_paste(vt_.get());
}

std::string VtEngine::take_pty_input() {
    return std::exchange(pty_input_, {});
}

Cell VtEngine::convert(const void* vterm_cell) const {
    VTermScreenCell c = *static_cast<const VTermScreenCell*>(vterm_cell);
    Cell out;
    if (c.chars[0] == kContinuation) {
        out.width = 0;
        return out;
    }
    for (std::size_t i = 0; i < kMaxCharsPerCell && c.chars[i] != 0; ++i) {
        out.chars[i] = static_cast<char32_t>(c.chars[i]);
    }
    out.width = static_cast<std::uint8_t>(c.width <= 0 ? 1 : c.width);
    if (c.attrs.bold)
        out.attrs |= kAttrBold;
    if (c.attrs.italic)
        out.attrs |= kAttrItalic;
    if (c.attrs.underline)
        out.attrs |= kAttrUnderline;
    if (c.attrs.reverse)
        out.attrs |= kAttrReverse;
    if (c.attrs.strike)
        out.attrs |= kAttrStrike;
    if (c.attrs.blink)
        out.attrs |= kAttrBlink;
    if (c.attrs.conceal)
        out.attrs |= kAttrConceal;
    out.default_fg = VTERM_COLOR_IS_DEFAULT_FG(&c.fg);
    out.default_bg = VTERM_COLOR_IS_DEFAULT_BG(&c.bg);
    if (!out.default_fg && VTERM_COLOR_IS_INDEXED(&c.fg) && c.fg.indexed.idx < 16)
        out.fg_ansi = static_cast<std::int8_t>(c.fg.indexed.idx);
    if (!out.default_bg && VTERM_COLOR_IS_INDEXED(&c.bg) && c.bg.indexed.idx < 16)
        out.bg_ansi = static_cast<std::int8_t>(c.bg.indexed.idx);
    vterm_screen_convert_color_to_rgb(screen_, &c.fg);
    vterm_screen_convert_color_to_rgb(screen_, &c.bg);
    // Canonical form: default colours carry no RGB (the renderer uses the theme), so snapshots
    // compare equal after a protocol round trip.
    if (!out.default_fg)
        out.fg = Rgb{c.fg.rgb.red, c.fg.rgb.green, c.fg.rgb.blue};
    if (!out.default_bg)
        out.bg = Rgb{c.bg.rgb.red, c.bg.rgb.green, c.bg.rgb.blue};
    return out;
}

void VtEngine::push_history(int cols, const void* vterm_cells) {
    const auto* cells = static_cast<const VTermScreenCell*>(vterm_cells);
    HistoryLine line;
    line.line_id = next_line_id_++;
    line.cells.reserve(static_cast<std::size_t>(cols));
    for (int i = 0; i < cols; ++i) {
        line.cells.push_back(convert(&cells[i]));
    }
    // libvterm has already shifted its line info when the screen pushes the top row: row 0's
    // continuation flag now says whether the pushed row ran on into it (one-line scrolls; a
    // multi-line scroll keeps the flag of its last pushed row only).
    if (rows_ > 0)
        line.wrapped = vterm_state_get_lineinfo(vterm_obtain_state(vt_.get()), 0)->continuation != 0;
    history_bytes_ += history_line_bytes(line);
    history_.push_back(std::move(line));
    // Whichever limit is reached first evicts the oldest lines (design doc 3.4).
    while (!history_.empty() &&
           (history_.size() > options_.history.max_lines || history_bytes_ > options_.history.max_bytes)) {
        history_bytes_ -= history_line_bytes(history_.front());
        history_.pop_front();
        history_truncated_ = true;
    }
}

bool VtEngine::pop_history(int cols, void* vterm_cells) {
    if (history_.empty()) {
        return false;
    }
    auto* cells = static_cast<VTermScreenCell*>(vterm_cells);
    const HistoryLine& line = history_.back();
    for (int i = 0; i < cols; ++i) {
        VTermScreenCell& dst = cells[i];
        dst = VTermScreenCell{};
        const Cell* src = static_cast<std::size_t>(i) < line.cells.size()
                              ? &line.cells[static_cast<std::size_t>(i)]
                              : nullptr;
        if (src == nullptr) {
            dst.width = 1;
            vterm_color_rgb(&dst.fg, 0, 0, 0);
            dst.fg.type |= VTERM_COLOR_DEFAULT_FG;
            vterm_color_rgb(&dst.bg, 0, 0, 0);
            dst.bg.type |= VTERM_COLOR_DEFAULT_BG;
            continue;
        }
        if (src->width == 0) {
            dst.chars[0] = kContinuation;
        } else {
            for (std::size_t k = 0; k < kMaxCharsPerCell; ++k) {
                dst.chars[k] = static_cast<uint32_t>(src->chars[k]);
            }
        }
        dst.width = static_cast<char>(src->width == 0 ? 1 : src->width);
        dst.attrs.bold = (src->attrs & kAttrBold) ? 1 : 0;
        dst.attrs.italic = (src->attrs & kAttrItalic) ? 1 : 0;
        dst.attrs.underline = (src->attrs & kAttrUnderline) ? 1 : 0;
        dst.attrs.reverse = (src->attrs & kAttrReverse) ? 1 : 0;
        dst.attrs.strike = (src->attrs & kAttrStrike) ? 1 : 0;
        dst.attrs.blink = (src->attrs & kAttrBlink) ? 1 : 0;
        dst.attrs.conceal = (src->attrs & kAttrConceal) ? 1 : 0;
        vterm_color_rgb(&dst.fg, src->fg.r, src->fg.g, src->fg.b);
        if (src->default_fg)
            dst.fg.type |= VTERM_COLOR_DEFAULT_FG;
        vterm_color_rgb(&dst.bg, src->bg.r, src->bg.g, src->bg.b);
        if (src->default_bg)
            dst.bg.type |= VTERM_COLOR_DEFAULT_BG;
    }
    history_bytes_ -= history_line_bytes(line);
    history_.pop_back();
    return true;
}

std::uint64_t VtEngine::first_history_line_id() const noexcept {
    return history_.empty() ? next_line_id_ : history_.front().line_id;
}

void VtEngine::handle_osc(int command, std::string body) {
    switch (command) {
        case 7501:
            handle_status_osc(body);
            return;
        case 133:
            if (!body.empty() && body.front() == 'A') {
                status_.on_shell_prompt();
                if (events_.on_shell_prompt)
                    events_.on_shell_prompt();
            }
            return;
        case 7:
            if (body.size() <= kMaxCwdBytes && body.starts_with("file://")) {
                cwd_uri_ = sanitize_display_text(body, kMaxCwdBytes);
            }
            return;
        case 8:
            handle_link_osc(body);
            return;
        default:
            return;
    }
}

void VtEngine::handle_status_osc(const std::string& body) {
    const auto msg = parse_osc7501(body);
    if (!msg) {
        return;  // invalid reports are discarded whole (spec)
    }
    if (std::holds_alternative<StatusQuery>(*msg)) {
        // Same queue as DA replies; libvterm parses in byte order, so a query sent before `CSI c`
        // is answered before the DA1 reply.
        pty_input_.append(osc7501_query_reply());
        status_query_answered_ = true;
        return;
    }
    const auto& report = std::get<StatusReport>(*msg);
    status_.apply(report);
    if (events_.on_status) {
        events_.on_status(report);
    }
}

void VtEngine::handle_link_osc(const std::string& body) {
    // OSC 8 ; params ; URI  (empty URI closes the link). Only remembered for an explicit
    // Ctrl+click by the person; output never opens anything (design doc 3.9).
    const auto semi = body.find(';');
    if (semi == std::string::npos)
        return;
    std::string uri = body.substr(semi + 1);
    if (open_link_ && cursor_.row >= open_link_->first.row) {
        // The open link covers the cells from where it opened to the cursor, one entry per row.
        const CursorState from = open_link_->first;
        const std::string target = std::move(open_link_->second);
        open_link_.reset();
        for (int row = from.row; row <= cursor_.row && row < rows_; ++row) {
            const int c0 = row == from.row ? from.col : 0;
            const int c1 = row == cursor_.row ? cursor_.col : cols_;
            if (c1 <= c0)
                continue;
            if (links_.size() >= kMaxLinks)
                links_.erase(links_.begin());
            links_.push_back({Hyperlink{row, c0, c1, target}, cells_text(row, c0, c1)});
        }
    }
    open_link_.reset();
    if (!uri.empty() && uri.size() <= kMaxLinkBytes &&
        std::none_of(uri.begin(), uri.end(), [](char c) { return static_cast<unsigned char>(c) < 0x20; }))
        open_link_.emplace(cursor_, std::move(uri));
}

std::string VtEngine::cells_text(int row, int col0, int col1) const {
    std::string out;
    for (int col = std::max(0, col0); col < std::min(col1, cols_); ++col) {
        VTermScreenCell cell{};
        vterm_screen_get_cell(screen_, VTermPos{row, col}, &cell);
        for (std::size_t i = 0; i < kMaxCharsPerCell && cell.chars[i] != 0 && cell.chars[i] != kContinuation;
             ++i)
            append_utf8(out, static_cast<char32_t>(cell.chars[i]));
        out.push_back('\x1f');  // cell separator, so blanks count
    }
    return out;
}

void VtEngine::move_links(int src_row0, int src_row1, int dest_row0, int dest_row1, int dy) {
    std::erase_if(links_, [&](TrackedLink& t) {
        const int row = t.link.row;
        if (row >= src_row0 && row < src_row1) {
            t.link.row = row + dy;
            return t.link.row < 0 || t.link.row >= rows_;
        }
        return row >= dest_row0 && row < dest_row1;  // overwritten by the moved rows
    });
    if (open_link_ && open_link_->first.row >= src_row0 && open_link_->first.row < src_row1)
        open_link_->first.row += dy;
}

void VtEngine::on_process_exit() {
    flush();
    status_.on_process_exit();
}

ScreenSnapshot VtEngine::snapshot() const {
    ScreenSnapshot snap;
    snap.rows = rows_;
    snap.cols = cols_;
    snap.cursor = cursor_;
    snap.alt_screen = alt_screen_;
    snap.mouse = mouse_mode_;
    snap.title = title_;
    snap.generation = generation_;
    snap.seq = seq_;
    snap.cells.reserve(static_cast<std::size_t>(rows_) * static_cast<std::size_t>(cols_));
    for (int row = 0; row < rows_; ++row) {
        for (int col = 0; col < cols_; ++col) {
            VTermScreenCell cell{};
            vterm_screen_get_cell(screen_, VTermPos{row, col}, &cell);
            snap.cells.push_back(convert(&cell));
        }
    }
    // A row is soft-wrapped when the next row is marked as its continuation.
    const VTermState* state = vterm_obtain_state(vt_.get());
    snap.wrapped.assign(static_cast<std::size_t>(rows_), 0);
    for (int row = 0; row + 1 < rows_; ++row)
        snap.wrapped[static_cast<std::size_t>(row)] =
            vterm_state_get_lineinfo(state, row + 1)->continuation ? 1 : 0;
    for (const auto& t : links_) {
        if (t.link.row < rows_ && cells_text(t.link.row, t.link.col0, t.link.col1) == t.text)
            snap.links.push_back(t.link);
    }
    std::sort(snap.links.begin(), snap.links.end(), [](const Hyperlink& a, const Hyperlink& b) {
        return std::tie(a.row, a.col0) < std::tie(b.row, b.col0);
    });
    return snap;
}

// ---- agent-host handover (design doc 7.4 OQ-09)

namespace {

constexpr std::uint32_t kStateMagic = 0x53454441u;  // "ADES"
constexpr std::uint32_t kStateFormat = 1;
constexpr std::size_t kCellBytes = kMaxCharsPerCell * 4 + 12;

void put_cell(ByteWriter& w, const Cell& c) {
    for (const char32_t cp : c.chars)
        w.u32(static_cast<std::uint32_t>(cp));
    w.u8(c.width);
    w.u8(c.attrs);
    w.boolean(c.default_fg);
    w.boolean(c.default_bg);
    w.u8(c.fg.r);
    w.u8(c.fg.g);
    w.u8(c.fg.b);
    w.u8(c.bg.r);
    w.u8(c.bg.g);
    w.u8(c.bg.b);
    w.u8(static_cast<std::uint8_t>(c.fg_ansi));
    w.u8(static_cast<std::uint8_t>(c.bg_ansi));
}

Cell get_cell(ByteReader& r) {
    Cell c;
    for (auto& cp : c.chars)
        cp = static_cast<char32_t>(r.u32());
    c.width = r.u8();
    c.attrs = r.u8();
    c.default_fg = r.boolean();
    c.default_bg = r.boolean();
    c.fg.r = r.u8();
    c.fg.g = r.u8();
    c.fg.b = r.u8();
    c.bg.r = r.u8();
    c.bg.g = r.u8();
    c.bg.b = r.u8();
    c.fg_ansi = static_cast<std::int8_t>(r.u8());
    c.bg_ansi = static_cast<std::int8_t>(r.u8());
    return c;
}

void put_report(ByteWriter& w, const StatusReport& s) {
    w.u8(static_cast<std::uint8_t>(s.state));
    w.str(s.id);
    w.u8(static_cast<std::uint8_t>(s.kind));
    w.boolean(s.progress.has_value());
    w.u8(s.progress.value_or(0));
    w.str(s.app);
    w.str(s.title);
    w.str(s.msg);
}

void put_link(ByteWriter& w, const Hyperlink& l) {
    w.i32(l.row);
    w.i32(l.col0);
    w.i32(l.col1);
    w.str(l.uri);
}

Hyperlink get_link(ByteReader& r) {
    Hyperlink l;
    l.row = r.i32();
    l.col0 = r.i32();
    l.col1 = r.i32();
    l.uri = r.str();
    return l;
}

void put_cursor(ByteWriter& w, const CursorState& c) {
    w.i32(c.row);
    w.i32(c.col);
    w.boolean(c.visible);
}

CursorState get_cursor(ByteReader& r) {
    CursorState c;
    c.row = r.i32();
    c.col = r.i32();
    c.visible = r.boolean();
    return c;
}

StatusReport get_report(ByteReader& r) {
    StatusReport s;
    const auto state = r.u8();
    if (state > static_cast<std::uint8_t>(StatusState::kClear))
        r.fail();
    s.state = static_cast<StatusState>(state);
    s.id = r.str();
    const auto kind = r.u8();
    if (kind > static_cast<std::uint8_t>(StatusKind::kAuth))
        r.fail();
    s.kind = static_cast<StatusKind>(kind);
    const bool has_progress = r.boolean();
    const auto progress = r.u8();
    if (has_progress)
        s.progress = progress;
    s.app = r.str();
    s.title = r.str();
    s.msg = r.str();
    return s;
}

}  // namespace

bool VtEngine::at_handover_point() const {
    return vterm_agentdesk_at_ground(vt_.get()) != 0;
}

std::string VtEngine::save_state() const {
    char* raw = nullptr;
    const std::size_t n = vterm_agentdesk_save(vt_.get(), &raw);
    if (n == 0)
        return {};
    std::string vterm_state(raw, n);
    std::free(raw);

    ByteWriter w;
    w.u32(kStateMagic);
    w.u32(kStateFormat);
    w.i32(rows_);
    w.i32(cols_);
    w.str(vterm_state);
    w.str(pty_input_);
    w.str(utf8_carry_);
    w.u64(history_.size());
    for (const auto& line : history_) {
        w.u64(line.line_id);
        w.boolean(line.wrapped);
        w.u64(line.cells.size());
        for (const auto& c : line.cells)
            put_cell(w, c);
    }
    w.u64(history_bytes_);
    w.u64(next_line_id_);
    w.boolean(history_truncated_);
    put_cursor(w, cursor_);
    w.boolean(alt_screen_);
    w.u8(static_cast<std::uint8_t>(mouse_mode_));
    w.boolean(win32_on_);
    w.str(win32_tail_);
    w.u64(links_.size());
    for (const auto& t : links_) {
        put_link(w, t.link);
        w.str(t.text);
    }
    w.boolean(open_link_.has_value());
    if (open_link_) {
        put_cursor(w, open_link_->first);
        w.str(open_link_->second);
    }
    w.str(title_);
    w.str(title_pending_);
    w.str(osc_);
    w.i32(osc_command_);
    w.boolean(osc_overflow_);
    w.str(cwd_uri_);
    w.u64(status_.counter());
    w.u64(status_.records().size());
    for (const auto& [key, record] : status_.records()) {
        w.str(key);
        w.u64(record.updated_order);
        put_report(w, record.report);
    }
    w.boolean(status_query_answered_);
    w.u64(generation_);
    w.u64(seq_);
    w.u32(kStateMagic);
    return w.take();
}

std::unique_ptr<VtEngine> VtEngine::restore(const VtEngineOptions& options, EngineEvents events,
                                            std::string_view state) {
    ByteReader r(state);
    const auto bad = [] { return std::runtime_error("not a terminal state this agent-host reads"); };
    if (r.u32() != kStateMagic || r.u32() != kStateFormat)
        throw bad();
    VtEngineOptions sized = options;
    sized.rows = r.i32();
    sized.cols = r.i32();
    if (!r.ok() || sized.rows < 1 || sized.rows > 500 || sized.cols < 2 || sized.cols > 1000)
        throw bad();
    auto e = std::make_unique<VtEngine>(sized, std::move(events));
    const std::string vterm_state = r.str();
    if (!r.ok() || vterm_agentdesk_load(e->vt_.get(), vterm_state.data(), vterm_state.size()) == 0)
        throw bad();
    e->pty_input_ = r.str();
    e->utf8_carry_ = r.str();
    const auto lines = r.count(17);
    for (std::uint64_t i = 0; i < lines && r.ok(); ++i) {
        HistoryLine line;
        line.line_id = r.u64();
        line.wrapped = r.boolean();
        const auto cells = r.count(kCellBytes);
        line.cells.reserve(static_cast<std::size_t>(cells));
        for (std::uint64_t c = 0; c < cells && r.ok(); ++c)
            line.cells.push_back(get_cell(r));
        e->history_.push_back(std::move(line));
    }
    e->history_bytes_ = static_cast<std::size_t>(r.u64());
    e->next_line_id_ = r.u64();
    e->history_truncated_ = r.boolean();
    e->cursor_ = get_cursor(r);
    e->alt_screen_ = r.boolean();
    const auto mouse = r.u8();
    if (mouse > static_cast<std::uint8_t>(MouseMode::kMove))
        r.fail();
    e->mouse_mode_ = static_cast<MouseMode>(mouse);
    e->win32_on_ = r.boolean();
    e->win32_tail_ = r.str();
    const auto links = r.count(28);
    for (std::uint64_t i = 0; i < links && r.ok(); ++i) {
        TrackedLink t;
        t.link = get_link(r);
        t.text = r.str();
        e->links_.push_back(std::move(t));
    }
    if (r.boolean()) {
        const CursorState at = get_cursor(r);
        std::string uri = r.str();
        e->open_link_.emplace(at, std::move(uri));
    }
    e->title_ = r.str();
    e->title_pending_ = r.str();
    e->osc_ = r.str();
    e->osc_command_ = r.i32();
    e->osc_overflow_ = r.boolean();
    e->cwd_uri_ = r.str();
    const auto counter = r.u64();
    std::map<std::string, StatusRecord> records;
    const auto record_count = r.count(8);
    for (std::uint64_t i = 0; i < record_count && r.ok(); ++i) {
        std::string key = r.str();
        StatusRecord record;
        record.updated_order = r.u64();
        record.report = get_report(r);
        records.emplace(std::move(key), std::move(record));
    }
    e->status_.restore(std::move(records), counter);
    e->status_query_answered_ = r.boolean();
    e->generation_ = r.u64();
    e->seq_ = r.u64();
    if (r.u32() != kStateMagic || !r.ok() || !r.at_end())
        throw bad();
    return e;
}

}  // namespace agentdesk::terminal
