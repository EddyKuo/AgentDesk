#include "agentdesk/terminal/vt_engine.hpp"

#include <algorithm>
#include <stdexcept>

#include <vterm.h>

namespace agentdesk::terminal {

namespace {

constexpr std::size_t kMaxTitleBytes = 1024;
constexpr std::size_t kMaxCwdBytes = 4096;
constexpr std::size_t kMaxPasteBytes = 1024 * 1024;
constexpr std::uint32_t kContinuation = 0xFFFFFFFFu;

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
    static int moverect(VTermRect, VTermRect, void* user) {
        self(user).bump();
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
    vterm_screen_set_damage_merge(screen_, VTERM_DAMAGE_SCROLL);
    vterm_screen_reset(screen_, 1);
    // OSC 52 stays disabled: no selection callbacks are registered (design doc 3.9).
}

VtEngine::~VtEngine() = default;

// Number of trailing bytes that form an incomplete UTF-8 sequence (0 when the tail is complete).
static std::size_t incomplete_utf8_tail(std::string_view data) {
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

void VtEngine::feed(std::string_view bytes) {
    if (bytes.empty()) {
        return;
    }
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
    ++generation_;
    bump();
}

void VtEngine::key(Key key, std::uint8_t mods) {
    vterm_keyboard_key(vt_.get(), to_vterm_key(key), to_vterm_mods(mods));
}

void VtEngine::text(char32_t codepoint, std::uint8_t mods) {
    vterm_keyboard_unichar(vt_.get(), static_cast<uint32_t>(codepoint), to_vterm_mods(mods));
}

void VtEngine::paste(std::string_view utf8) {
    if (utf8.size() > kMaxPasteBytes) {
        throw std::invalid_argument("paste exceeds 1 MiB");
    }
    if (utf8.find('\0') != std::string_view::npos) {
        throw std::invalid_argument("paste contains NUL");
    }
    // Bracketed paste markers are emitted only when the program enabled mode 2004.
    vterm_keyboard_start_paste(vt_.get());
    std::string body(utf8);
    // An embedded end marker would let pasted text escape the bracket: strip it.
    static constexpr std::string_view kEnd = "\x1b[201~";
    for (auto pos = body.find(kEnd); pos != std::string::npos; pos = body.find(kEnd, pos)) {
        body.erase(pos, kEnd.size());
    }
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
    vterm_screen_convert_color_to_rgb(screen_, &c.fg);
    vterm_screen_convert_color_to_rgb(screen_, &c.bg);
    out.fg = Rgb{c.fg.rgb.red, c.fg.rgb.green, c.fg.rgb.blue};
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
    history_bytes_ += line.cells.size() * sizeof(Cell) + sizeof(HistoryLine);
    history_.push_back(std::move(line));
    // Whichever limit is reached first evicts the oldest lines (design doc 3.4).
    while (!history_.empty() &&
           (history_.size() > options_.history.max_lines || history_bytes_ > options_.history.max_bytes)) {
        history_bytes_ -= history_.front().cells.size() * sizeof(Cell) + sizeof(HistoryLine);
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
    history_bytes_ -= line.cells.size() * sizeof(Cell) + sizeof(HistoryLine);
    history_.pop_back();
    return true;
}

std::uint64_t VtEngine::first_history_line_id() const noexcept {
    return history_.empty() ? next_line_id_ : history_.front().line_id;
}

void VtEngine::handle_osc(int command, std::string body) {
    switch (command) {
        case 7501: {
            const auto msg = parse_osc7501(body);
            if (!msg) {
                return;  // invalid reports are discarded whole (spec)
            }
            if (std::holds_alternative<StatusQuery>(*msg)) {
                // Same queue as DA replies; libvterm parses in byte order, so a query sent
                // before `CSI c` is answered before the DA1 reply.
                pty_input_.append(osc7501_query_reply());
                status_query_answered_ = true;
                return;
            }
            const auto& report = std::get<StatusReport>(*msg);
            status_.apply(report);
            if (events_.on_status) {
                events_.on_status(report);
            }
            return;
        }
        case 133:
            if (!body.empty() && body.front() == 'A') {
                status_.on_shell_prompt();
            }
            return;
        case 7:
            if (body.size() <= kMaxCwdBytes && body.starts_with("file://")) {
                cwd_uri_ = sanitize_display_text(body, kMaxCwdBytes);
            }
            return;
        default:
            return;  // OSC 8 and others: ignored in M0
    }
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
    return snap;
}

}  // namespace agentdesk::terminal
