#pragma once
// Portions adapted from herdr-app (Apache-2.0) and modified; see THIRD_PARTY_NOTICES.md.
// The win98 layout's logic with no drawing in it: pixel math, colours and bevels, menu metrics,
// DOS paths and clock texts, and the rules of Mines and Solitaire. agent-ui draws the windows,
// menus and dialogs that use them (apps/desktop/win98.inl).
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "agentdesk/ui/layout.hpp"

namespace agentdesk::ui::win98 {

// ---------------------------------------------------------------- colors
// 0xRRGGBB.
namespace color {
inline constexpr std::uint32_t kFace = 0xc0c0c0;
inline constexpr std::uint32_t kLight = 0xdfdfdf;
inline constexpr std::uint32_t kHi = 0xffffff;
inline constexpr std::uint32_t kShadow = 0x808080;
inline constexpr std::uint32_t kDark = 0x0a0a0a;
inline constexpr std::uint32_t kText = 0x000000;
inline constexpr std::uint32_t kGray = 0x808080;
inline constexpr std::uint32_t kWhite = 0xffffff;
inline constexpr std::uint32_t kNavy = 0x000080;
inline constexpr std::uint32_t kBlue = 0x1084d0;
inline constexpr std::uint32_t kTitle = 0xffffff;
inline constexpr std::uint32_t kInactive = 0x808080;
inline constexpr std::uint32_t kInactive2 = 0xb5b5b5;
inline constexpr std::uint32_t kInactiveText = 0xc0c0c0;
inline constexpr std::uint32_t kDesktop = 0x008080;
inline constexpr std::uint32_t kTip = 0xffffe1;
inline constexpr std::uint32_t kSelect = 0x000080;
inline constexpr std::uint32_t kSelectText = 0xffffff;
inline constexpr std::uint32_t kDisabled = 0x808080;
}  // namespace color

// ---------------------------------------------------------------- bevels
// Layers from outside in: {right+bottom color, top+left color}.
enum class Bevel { kRaised, kPressed, kWindow, kField, kWell, kBump, kDent, kThin, kFlat };
struct BevelLayer {
    std::uint32_t rb;
    std::uint32_t tl;
};
std::vector<BevelLayer> bevel_layers(Bevel kind);

// n Win98 pixels are n * k pixels on screen; k is the scale (1 Small Fonts, 2 Large Fonts).
// Sizes are worked out in screen pixels so the pixels stay whole at any zoom.
struct Unit {
    float k = 1.0f;
    [[nodiscard]] float operator()(float n) const { return n * k; }
};
// The shared "win98.scale" setting: 2 when it is 2, else 1.
int scale_of(int shared_scale);

// The layout's bars in Win98 pixels.
inline constexpr float kTaskbarPx = 28.0f;
inline constexpr float kDesktopPx = 84.0f;
inline constexpr float kHeaderPx = 18.0f;
inline constexpr float kBorderPx = 3.0f;
// The MS-DOS font: Large Fonts, a crisp 32 px (16 Win98 px); Small, 16 px at the app's zoom.
float terminal_font_px(int scale, float zoom);

// ---------------------------------------------------------------- text
// Index of the access key ("&File" -> 1, the F), or -1: the char after an "&" that is not last.
int access_index(std::string_view label);
// The first "&x" becomes "x".
std::string plain(std::string_view label);
// True when the label holds "&" + ch (compared lowercase): that item's access key is ch.
bool access_matches(std::string_view label, char ch);

// ---------------------------------------------------------------- menus
struct MenuMetrics {
    static constexpr float kRow = 18;
    static constexpr float kStartRow = 34;
    static constexpr float kSep = 9;
    static constexpr float kFrame = 3;
    static constexpr float kBanner = 21;
};
struct MenuRowText {
    std::string label;
    std::string shortcut;
    bool sep = false;
};
struct MenuSize {
    float w = 0;
    float h = 0;
};
MenuSize menu_size(const std::vector<MenuRowText>& items, bool start);

// A floating block (Clippy) sits on an edge of its node, at the start, middle or end of it,
// `margin` from the edges.
using Edge = layout::Edge;  // the layout engine's (layout.hpp)
using Align = layout::Align;
struct FloatPlace {
    Edge edge = Edge::kRight;
    Align align = Align::kCenter;
    bool operator==(const FloatPlace&) const = default;
};
// Dropped with its center at (x, y) in a node w x h: the nearest edge, the third along it.
FloatPlace snap_float(float x, float y, float w, float h);
// Ease out with a small overshoot, so the snap feels like a spring (t in 0..1; 0 at the drop,
// 1 home). A dropped float springs home over kSnapMs.
float spring(float t);
inline constexpr float kSnapMs = 360.0f;
inline constexpr float kDragStart = 4.0f;  // a press starts dragging it after moving this far
// Its top left in the node (origin ox, oy; size w x h) for a block of bw x bh.
std::array<float, 2> float_home(float ox, float oy, float w, float h, float bw, float bh, FloatPlace place,
                                float margin);
using layout::align_name;  // "start", "center", "end"
using layout::edge_name;   // "top", "right", "bottom", "left"
FloatPlace parse_place(std::string_view edge, std::string_view align);

// Each dialog's width in Win98 px (340 when not listed).
float dialog_width(std::string_view kind);

// ---------------------------------------------------------------- paths and names
// For Windows paths: under the home folder "C:\My Documents\...", else the path
// with "C:" in front when it has no drive of its own. Separators become "\", no trailing "\".
std::string dos_path(std::string_view path, std::string_view home);
// The home folder's last part, capitalised ("User" without one).
std::string user_name(std::string_view home);
// The program's name for a Run line: its first word, after the last "/".
std::string run_name(std::string_view command);
// The Run history: this command first, no repeats, at most 12.
std::vector<std::string> run_history(std::vector<std::string> history, const std::string& command);

// ---------------------------------------------------------------- time
struct DateTime {
    int year = 1998;
    int month = 0;    // 0-11
    int day = 1;      // 1-31
    int weekday = 0;  // 0 Sunday
    int hour = 0;     // 0-23
    int minute = 0;
    int second = 0;
    int utc_offset_minutes = 0;  // east of UTC positive
};
std::string clock_text(const DateTime& d);  // "3:07 PM"
// US English long date: "Thursday, November 19, 2026".
std::string long_date(const DateTime& d);
std::string month_name(int month);              // "November"
std::string time_text(const DateTime& d);       // US English: "3:07:09 PM"
std::string time_zone_text(const DateTime& d);  // "UTC+08:00"
// The month's weeks, Sunday first; 0 is an empty cell.
std::vector<std::array<int, 7>> calendar(int year, int month);
// The clock face. Each dot: left, top (Win98 px in the 96 px face), size,
// color (0xRRGGBB).
struct ClockDot {
    int x = 0;
    int y = 0;
    int size = 0;
    std::uint32_t color = 0;
};
std::vector<ClockDot> clock_face(const DateTime& d);

// ---------------------------------------------------------------- Mines
class Mines {
public:
    enum class State { kReady, kPlaying, kLost, kWon };
    struct Cell {
        bool mine = false;
        int n = 0;
        bool open = false;
        int mark = 0;  // 0 none, 1 flag, 2 question mark
    };
    struct Options {
        std::string level = "beginner";
        bool marks = true;
        bool color = true;
        bool sound = true;
        std::map<std::string, int> best;  // level -> seconds
    };
    // A random number in [0, 1).
    using Random = std::function<double()>;

    explicit Mines(Options options = {}, Random random = {});

    // {cols, rows, mines}; nullopt for an unknown level.
    static std::optional<std::array<int, 3>> level(std::string_view name);

    void reset();  // a new game on the current level
    [[nodiscard]] std::vector<int> neighbors(int i) const;
    // The first reveal lays the mines away from i and its neighbours and starts
    // the clock at now_ms. Returns the sound to play ("mines-boom", "tada") or "".
    std::string reveal(int i, long long now_ms);
    // A number with as many flags around it reveals its other neighbours.
    std::string chord(int i, long long now_ms);
    // Right-click cycles none, flag, (?) on a closed cell while the game runs.
    void mark(int i);
    // Once a second; stops at 999. Returns true when the shown time changed.
    bool tick(long long now_ms);
    // A menu command. "best" is the caller's (a message box).
    void request(const std::string& cmd);
    // The counter at the left.
    [[nodiscard]] int mines_left() const;
    // The face: "dead", "cool", "ooh" (a cell held) or "smile".
    [[nodiscard]] std::string face() const;
    // The Best Times text.
    [[nodiscard]] std::string best_text() const;

    [[nodiscard]] int cols() const { return cols_; }
    [[nodiscard]] int rows() const { return rows_; }
    [[nodiscard]] int total() const { return total_; }
    [[nodiscard]] State state() const { return state_; }
    [[nodiscard]] int seconds() const { return seconds_; }
    [[nodiscard]] int boom() const { return boom_; }
    [[nodiscard]] bool running() const { return timer_; }
    [[nodiscard]] const std::vector<Cell>& cells() const { return cells_; }
    std::vector<Cell>& cells() { return cells_; }
    [[nodiscard]] const Options& options() const { return options_; }
    Options& options() { return options_; }
    int held = -1;       // the cell pressed and not released
    bool saved = false;  // the options changed (best time, level): the caller persists them

private:
    void start(int i, long long now_ms);
    Options options_;
    Random random_;
    int cols_ = 9, rows_ = 9, total_ = 10;
    std::vector<Cell> cells_;
    State state_ = State::kReady;
    int seconds_ = 0;
    int boom_ = -1;
    long long started_ = 0;
    bool timer_ = false;
};

// The seven-segment counter. The characters ("-" then two digits below zero,
// else three digits, at most 999) and, per character, which of the segments a..g are lit.
std::string counter_chars(int value);
std::array<bool, 7> segments_lit(char ch);
// Each segment's x, y, w, h in the 11 x 23 digit (a..g).
const std::array<std::array<int, 4>, 7>& segment_rects();
// The number colors 1..8 (index 0 is the face color).
std::uint32_t mines_number_color(int n);

// ---------------------------------------------------------------- Solitaire
class Solitaire {
public:
    static constexpr int kW = 596, kH = 355, kCW = 71, kCH = 96;  // board and card size, px
    enum class Pile { kTab, kHome, kWaste };
    struct Loc {
        Pile type = Pile::kTab;
        int i = 0;
        int n = 0;
        bool operator==(const Loc&) const = default;
    };
    struct TabCard {
        int c = 0;
        bool up = false;
    };
    struct Game {
        std::vector<int> stock;
        std::vector<int> waste;
        std::array<std::vector<TabCard>, 7> tab;
        std::array<std::vector<int>, 4> home;
        int score = 0;
    };
    struct Flying {
        int c = 0;
        float x = 0, y = 0, vx = 0, vy = 0;
    };
    struct Trail {
        int c = 0;
        float x = 0, y = 0;
    };
    using Random = std::function<double()>;

    explicit Solitaire(Random random = {});

    static int rank(int c) { return c % 13 + 1; }  // card c: 0..51, 13 per suit
    static int suit(int c) { return c / 13; }
    static bool red(int c) { return suit(c) < 2; }

    void deal();
    [[nodiscard]] bool valid_pick(const Loc& loc) const;
    // True when it moved; "tada" goes to `sound` on a win.
    bool move(const Loc& from, const Loc& to);
    void foundation(const Loc& loc);  // a double-click sends it home
    void stock();                     // a click on the stock
    // A press, without the pointer handling: a face-down card at the end of a
    // column turns up (+5); returns true when the press may pick (a valid pick).
    enum class Down { kNothing, kMoved, kTurned, kPick, kUnpick };
    Down down(const Loc& loc);
    [[nodiscard]] static std::optional<Loc> target(float x, float y);  // pile under (x, y)
    void tick_second();
    // One 33 ms frame of the bouncing cards; false when it ended.
    bool animate();
    void command(const std::string& cmd);                        // deal, undo, autowin
    [[nodiscard]] std::vector<int> cards(const Loc& loc) const;
    [[nodiscard]] std::size_t pile_size(const Loc& loc) const;
    // The step between face-up cards of a column.
    [[nodiscard]] float tab_step(int i) const;

    Game g;
    int draw = 3;
    bool scoring = true;
    int seconds = 0;
    bool running = false;  // the clock runs
    bool won = false;
    bool animating = false;
    std::optional<Game> undo;
    std::optional<Loc> pick;
    std::vector<Trail> trails;
    std::string sound;  // to play: "tada"

private:
    void save();
    void victory();
    void begin();
    Random random_;
    int frame_ = 0;
    int launched_ = 0;
    std::optional<Flying> flying_;
};

}  // namespace agentdesk::ui::win98
