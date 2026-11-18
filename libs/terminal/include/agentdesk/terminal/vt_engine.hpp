#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "agentdesk/terminal/program_status.hpp"

struct VTerm;
struct VTermScreen;

namespace agentdesk::terminal {

struct Rgb {
    std::uint8_t r = 0, g = 0, b = 0;
    friend bool operator==(const Rgb&, const Rgb&) = default;
};

enum CellAttr : std::uint8_t {
    kAttrBold = 1u << 0,
    kAttrItalic = 1u << 1,
    kAttrUnderline = 1u << 2,
    kAttrReverse = 1u << 3,
    kAttrStrike = 1u << 4,
    kAttrBlink = 1u << 5,
    kAttrConceal = 1u << 6,
};

inline constexpr std::size_t kMaxCharsPerCell = 6;  // VTERM_MAX_CHARS_PER_CELL

struct Cell {
    std::array<char32_t, kMaxCharsPerCell> chars{};  // 0-terminated; all zero = blank
    std::uint8_t width = 1;                          // 1 or 2; 0 = right half of a wide char
    std::uint8_t attrs = 0;
    bool default_fg = true;
    bool default_bg = true;
    Rgb fg;
    Rgb bg;
    friend bool operator==(const Cell&, const Cell&) = default;
};

struct CursorState {
    int row = 0;
    int col = 0;
    bool visible = true;
    friend bool operator==(const CursorState&, const CursorState&) = default;
};

// Immutable copy of the visible screen. `generation` changes on resize/reset; `seq` grows with
// every change, so consumers can detect gaps (design doc 3.7).
// Mouse reporting the program turned on. Only then do clicks and the wheel go to it.
enum class MouseMode : std::uint8_t { kNone, kClick, kDrag, kMove };

enum class MouseAction : std::uint8_t { kPress, kRelease, kMove };

// An OSC 8 hyperlink shown on one screen row: cells [col0, col1) open `uri`.
struct Hyperlink {
    int row = 0;
    int col0 = 0;
    int col1 = 0;
    std::string uri;
    friend bool operator==(const Hyperlink&, const Hyperlink&) = default;
};

struct ScreenSnapshot {
    int rows = 0;
    int cols = 0;
    std::vector<Cell> cells;  // row-major, rows * cols
    CursorState cursor;
    bool alt_screen = false;
    MouseMode mouse = MouseMode::kNone;  // what the program asked to receive (DECSET 1000/1002/1003)
    std::string title;
    std::uint64_t generation = 0;
    std::uint64_t seq = 0;
    // Per row: 1 when the row was soft-wrapped (its text continues on the next row). Copying
    // joins such rows without a line break (herdr vt.rs selection_text, unwrap). May be empty
    // (treated as all 0).
    std::vector<std::uint8_t> wrapped;
    std::vector<Hyperlink> links;  // OSC 8 links on the screen, by row

    [[nodiscard]] bool row_wrapped(int row) const {
        return row >= 0 && static_cast<std::size_t>(row) < wrapped.size() &&
               wrapped[static_cast<std::size_t>(row)] != 0;
    }
    [[nodiscard]] const Cell& at(int row, int col) const {
        return cells[static_cast<std::size_t>(row) * static_cast<std::size_t>(cols) +
                     static_cast<std::size_t>(col)];
    }
    // UTF-8 text of one row (continuation cells skipped, trailing blanks trimmed).
    [[nodiscard]] std::string row_text(int row) const;
};

struct HistoryLine {
    std::uint64_t line_id = 0;
    std::vector<Cell> cells;
    bool wrapped = false;  // continues on the next line (soft wrap)
};

struct HistoryLimits {
    std::size_t max_lines = 10'000;
    std::size_t max_bytes = 16u * 1024u * 1024u;
};

struct VtEngineOptions {
    int rows = 40;
    int cols = 120;
    HistoryLimits history;
    std::size_t max_osc_bytes = 64 * 1024;  // per OSC sequence; larger ones are discarded whole
};

enum class Key {
    kEnter,
    kTab,
    kBackspace,
    kEscape,
    kUp,
    kDown,
    kLeft,
    kRight,
    kInsert,
    kDelete,
    kHome,
    kEnd,
    kPageUp,
    kPageDown,
    kF1,
    kF2,
    kF3,
    kF4,
    kF5,
    kF6,
    kF7,
    kF8,
    kF9,
    kF10,
    kF11,
    kF12,
};

enum Modifier : std::uint8_t {
    kModNone = 0,
    kModShift = 1u << 0,
    kModAlt = 1u << 1,
    kModCtrl = 1u << 2,
};

// Event raised while feeding output. Called synchronously inside feed().
struct EngineEvents {
    std::function<void(const StatusReport&)> on_status;  // after the store was updated
    std::function<void()> on_bell;
};

// Wraps libvterm. Single-threaded: one owner (the session actor) feeds output, encodes input
// and takes snapshots. Every byte must be fed in order; nothing may be dropped.
class VtEngine {
public:
    explicit VtEngine(const VtEngineOptions& options = {}, EngineEvents events = {});
    ~VtEngine();
    VtEngine(const VtEngine&) = delete;
    VtEngine& operator=(const VtEngine&) = delete;
    VtEngine(VtEngine&&) = delete;
    VtEngine& operator=(VtEngine&&) = delete;

    void feed(std::string_view bytes);
    // Feeds bytes held back from an incomplete trailing UTF-8 sequence (end of stream).
    void flush();
    void resize(int rows, int cols);

    // Input encoding according to the modes the program set (cursor keys, bracketed paste).
    void key(Key key, std::uint8_t mods = kModNone);
    void text(char32_t codepoint, std::uint8_t mods = kModNone);
    void paste(std::string_view utf8);
    // Mouse event at a cell (0-based), encoded per the program's mouse mode and protocol
    // (X10/UTF-8/SGR). button: 1 left, 2 middle, 3 right, 4/5 wheel up/down, 0 for a plain move.
    // Ignored while the program has mouse reporting off; moves are sent only when it asked.
    void mouse(int row, int col, int button, MouseAction action, std::uint8_t mods = kModNone);
    [[nodiscard]] MouseMode mouse_mode() const noexcept { return mouse_mode_; }
    // The program asked for win32-input-mode (CSI ?9001h, ConPTY does): modified Enter and
    // Escape are then sent as win32 key events (herdr src/win32_keys.rs).
    [[nodiscard]] bool win32_input() const noexcept { return win32_on_; }

    // Bytes to write to the PTY input, in order: query replies (DA/DSR/CPR/OSC 7501) and
    // encoded input. The caller must write them before any later input.
    std::string take_pty_input();

    [[nodiscard]] ScreenSnapshot snapshot() const;
    [[nodiscard]] std::uint64_t seq() const noexcept { return seq_; }
    [[nodiscard]] std::size_t pending_utf8_bytes() const noexcept { return utf8_carry_.size(); }
    [[nodiscard]] std::uint64_t generation() const noexcept { return generation_; }
    [[nodiscard]] const std::deque<HistoryLine>& history() const noexcept { return history_; }
    [[nodiscard]] std::uint64_t first_history_line_id() const noexcept;
    [[nodiscard]] std::size_t history_bytes() const noexcept { return history_bytes_; }
    [[nodiscard]] bool history_truncated() const noexcept { return history_truncated_; }
    [[nodiscard]] const ProgramStatusStore& status() const noexcept { return status_; }
    [[nodiscard]] const std::string& cwd_uri() const noexcept { return cwd_uri_; }
    [[nodiscard]] bool status_query_answered() const noexcept { return status_query_answered_; }

    // The root program exited: apply OSC 7501 lifetime rules.
    void on_process_exit();

private:
    struct Callbacks;
    friend struct Callbacks;

    void bump() noexcept { ++seq_; }
    void push_history(int cols, const void* vterm_cells);
    bool pop_history(int cols, void* vterm_cells);
    void handle_osc(int command, std::string body);
    void watch_win32_mode(std::string_view bytes);
    void move_links(int src_row0, int src_row1, int dest_row0, int dest_row1, int dy);
    [[nodiscard]] std::string cells_text(int row, int col0, int col1) const;
    [[nodiscard]] Cell convert(const void* vterm_cell) const;

    struct VTermDeleter {
        void operator()(VTerm* vt) const noexcept;
    };
    std::unique_ptr<VTerm, VTermDeleter> vt_;
    VTermScreen* screen_ = nullptr;
    VtEngineOptions options_;
    EngineEvents events_;
    std::string pty_input_;
    std::string utf8_carry_;
    std::deque<HistoryLine> history_;
    std::size_t history_bytes_ = 0;
    std::uint64_t next_line_id_ = 1;
    bool history_truncated_ = false;
    CursorState cursor_;
    bool alt_screen_ = false;
    MouseMode mouse_mode_ = MouseMode::kNone;
    bool win32_on_ = false;
    std::string win32_tail_;  // end of the last write, for a request split across two
    struct TrackedLink {
        Hyperlink link;
        std::string text;  // the cells' text when the link closed; overwritten cells drop it
    };
    std::vector<TrackedLink> links_;
    std::optional<std::pair<CursorState, std::string>> open_link_;  // OSC 8 opened at, uri
    std::string title_;
    std::string title_pending_;
    std::string osc_;
    int osc_command_ = 0;
    bool osc_overflow_ = false;
    std::string cwd_uri_;
    ProgramStatusStore status_;
    bool status_query_answered_ = false;
    int rows_ = 0;
    int cols_ = 0;
    std::uint64_t generation_ = 1;
    std::uint64_t seq_ = 0;
};

// Encodes a code point as UTF-8 (helper shared with tests and the renderer).
void append_utf8(std::string& out, char32_t cp);

}  // namespace agentdesk::terminal
