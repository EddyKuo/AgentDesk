#pragma once
// Herdr 98 (herdr-app defaults/mods/win98, commit 321c9a8), the parts with no drawing in them:
// the kit's pixel math and tables (kit.js), the menu metrics (kit.js 322-344), DOS paths and
// clock texts (kit.js 431-437, taskbar.js 36-46, dialog.js 655-691), and the rules of the two
// games, Mines (mines.js) and Solitaire (sol.js). The window, menus and dialogs that use them
// are drawn by agent-ui (apps/desktop/win98.inl).
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace agentdesk::ui::win98 {

// ---------------------------------------------------------------- kit.js 12-32: the colors
// 0xRRGGBB, as written in kit.js.
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

// ---------------------------------------------------------------- kit.js 84-95: the bevels
// Layers from outside in: {right+bottom color, top+left color}.
enum class Bevel { kRaised, kPressed, kWindow, kField, kWell, kBump, kDent, kThin, kFlat };
struct BevelLayer {
    std::uint32_t rb;
    std::uint32_t tl;
};
std::vector<BevelLayer> bevel_layers(Bevel kind);

// kit.js 40-51: n Win98 pixels are n * k pixels on screen; k is the scale (1 Small Fonts, 2 Large
// Fonts). herdr divides by its zoom to get layout px, which its zoom multiplies back: the pixels
// stay whole at any zoom.
struct Unit {
    float k = 1.0f;
    [[nodiscard]] float operator()(float n) const { return n * k; }
};
// shared("win98.scale") === 2 ? 2 : 1
int scale_of(int shared_scale);

// kit.js 441-479 (fitLayout): the layout's bars in Win98 pixels.
inline constexpr float kTaskbarPx = 28.0f;
inline constexpr float kDesktopPx = 84.0f;
inline constexpr float kHeaderPx = 18.0f;
inline constexpr float kBorderPx = 3.0f;
// The MS-DOS font: Large Fonts, a crisp 32 px (16 Win98 px); Small, 16 px at Herdr's zoom.
float terminal_font_px(int scale, float zoom);

// ---------------------------------------------------------------- text (kit.js 55-75)
// Index of the access key ("&File" -> 1, the F), or -1. kit.js 58: an "&" that is not last.
int access_index(std::string_view label);
// kit.js 73-75: the first "&x" becomes "x".
std::string plain(std::string_view label);
// menu.js 326-327: the item whose label holds "&" + ch (lowercase).
bool access_matches(std::string_view label, char ch);

// ---------------------------------------------------------------- menus (kit.js 322-344)
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

// kit.js 373: each dialog's width in Win98 px (340 when not listed).
float dialog_width(std::string_view kind);

// ---------------------------------------------------------------- paths and names
// kit.js 431-437, for Windows paths: under the home folder "C:\My Documents\...", else the path
// with "C:" in front when it has no drive of its own. Separators become "\", no trailing "\".
std::string dos_path(std::string_view path, std::string_view home);
// taskbar.js 61-65: the home folder's last part, capitalised ("User" without one).
std::string user_name(std::string_view home);
// dialog.js 227: the program's name for a Run line: its first word, after the last "/".
std::string run_name(std::string_view command);
// dialog.js 224: the Run history: this command first, no repeats, at most 12.
std::vector<std::string> run_history(std::vector<std::string> history, const std::string& command);

// ---------------------------------------------------------------- time (taskbar.js 36-46)
struct DateTime {
    int year = 1998;
    int month = 0;    // 0-11
    int day = 1;      // 1-31
    int weekday = 0;  // 0 Sunday
    int hour = 0;     // 0-23
    int minute = 0;
    int second = 0;
    int utc_offset_minutes = 0;  // east of UTC positive (-getTimezoneOffset())
};
std::string clock_text(const DateTime& d);  // "3:07 PM"
// toLocaleDateString("en-US", {weekday: long, year: numeric, month: long, day: numeric})
std::string long_date(const DateTime& d);
std::string month_name(int month);              // "November"
std::string time_text(const DateTime& d);       // toLocaleTimeString("en-US"): "3:07:09 PM"
std::string time_zone_text(const DateTime& d);  // dialog.js 688-691: "UTC+08:00"
// dialog.js 656-666: the month's weeks, Sunday first; 0 is an empty cell.
std::vector<std::array<int, 7>> calendar(int year, int month);
// dialog.js 927-958: the clock face. Each dot: left, top (Win98 px in the 96 px face), size,
// color (0xRRGGBB).
struct ClockDot {
    int x = 0;
    int y = 0;
    int size = 0;
    std::uint32_t color = 0;
};
std::vector<ClockDot> clock_face(const DateTime& d);

// ---------------------------------------------------------------- Mines (mines.js)
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
    // A random number in [0, 1) (Math.random).
    using Random = std::function<double()>;

    explicit Mines(Options options = {}, Random random = {});

    // mines.js 6: {cols, rows, mines}; nullopt for an unknown level.
    static std::optional<std::array<int, 3>> level(std::string_view name);

    void reset();  // mines.js 34-42
    [[nodiscard]] std::vector<int> neighbors(int i) const;
    // mines.js 71-104: the first reveal lays the mines away from i and its neighbours and starts
    // the clock at now_ms. Returns the sound to play ("mines-boom", "tada") or "".
    std::string reveal(int i, long long now_ms);
    // mines.js 106-112: a number with as many flags around it reveals its other neighbours.
    std::string chord(int i, long long now_ms);
    // mines.js 173: right-click cycles none, flag, (?) on a closed cell while the game runs.
    void mark(int i);
    // mines.js 64-68: once a second; stops at 999. Returns true when the shown time changed.
    bool tick(long long now_ms);
    // mines.js 114-123: a menu command. "best" is the caller's (a message box).
    void request(const std::string& cmd);
    // mines.js 186: the counter at the left.
    [[nodiscard]] int mines_left() const;
    // mines.js 185: the face: "dead", "cool", "ooh" (a cell held) or "smile".
    [[nodiscard]] std::string face() const;
    // mines.js 122: the Best Times text.
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
    int held = -1;       // the cell pressed and not released (mines.js 40)
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

// mines.js 8-21: the seven-segment counter. The characters ("-" then two digits below zero,
// else three digits, at most 999) and, per character, which of the segments a..g are lit.
std::string counter_chars(int value);
std::array<bool, 7> segments_lit(char ch);
// mines.js 9: each segment's x, y, w, h in the 11 x 23 digit (a..g).
const std::array<std::array<int, 4>, 7>& segment_rects();
// mines.js 7: the number colors 1..8 (index 0 is the face color).
std::uint32_t mines_number_color(int n);

// ---------------------------------------------------------------- Solitaire (sol.js)
class Solitaire {
public:
    static constexpr int kW = 596, kH = 355, kCW = 71, kCH = 96;  // sol.js 6
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

    static int rank(int c) { return c % 13 + 1; }  // sol.js 7-9
    static int suit(int c) { return c / 13; }
    static bool red(int c) { return suit(c) < 2; }

    void deal();                                          // sol.js 223-234
    [[nodiscard]] bool valid_pick(const Loc& loc) const;  // sol.js 251-259
    // sol.js 260-275. True when it moved; "tada" goes to `sound` on a win.
    bool move(const Loc& from, const Loc& to);
    void foundation(const Loc& loc);  // sol.js 276-279: a double-click sends it home
    void stock();                     // sol.js 280-288
    // sol.js 289-300, the part that is not about the pointer: a face-down card at the end of a
    // column turns up (+5); returns true when the press may pick (a valid pick).
    enum class Down { kNothing, kMoved, kTurned, kPick, kUnpick };
    Down down(const Loc& loc);
    [[nodiscard]] static std::optional<Loc> target(float x, float y);  // sol.js 305-309
    void tick_second();                                                // sol.js 242-246
    // sol.js 313-327: one 33 ms frame of the bouncing cards; false when it ended.
    bool animate();
    void command(const std::string& cmd);                        // sol.js 332-340 (deal, undo, autowin)
    [[nodiscard]] std::vector<int> cards(const Loc& loc) const;  // sol.js 250
    [[nodiscard]] std::size_t pile_size(const Loc& loc) const;
    // sol.js 420: the step between face-up cards of a column.
    [[nodiscard]] float tab_step(int i) const;

    Game g;
    int draw = 3;
    bool scoring = true;
    int seconds = 0;
    bool running = false;  // the clock runs (sol.js 241)
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
