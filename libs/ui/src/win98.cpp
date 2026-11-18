// Herdr 98's rules and numbers, ported from herdr-app defaults/mods/win98 (commit 321c9a8,
// Apache-2.0); each piece names its file and lines.
#include "agentdesk/ui/win98.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <random>
#include <set>

namespace agentdesk::ui::win98 {

namespace {

// Math.random when no source is given.
double default_random() {
    static std::mt19937_64 gen{std::random_device{}()};
    return std::uniform_real_distribution<double>(0.0, 1.0)(gen);
}

// JavaScript's Math.round: halves go up (toward +infinity).
int js_round(double v) {
    return static_cast<int>(std::floor(v + 0.5));
}

std::size_t utf8_length(std::string_view s) {
    std::size_t n = 0;
    for (const char c : s)
        n += (static_cast<unsigned char>(c) & 0xC0) != 0x80 ? 1 : 0;
    return n;
}

std::string pad(int v, int width) {
    std::string s = std::to_string(v);
    while (static_cast<int>(s.size()) < width)
        s.insert(s.begin(), '0');
    return s;
}

}  // namespace

// ---------------------------------------------------------------- kit.js

// kit.js 84-95.
std::vector<BevelLayer> bevel_layers(Bevel kind) {
    using namespace color;
    switch (kind) {
        case Bevel::kRaised:
            return {{kDark, kHi}, {kShadow, kLight}};
        case Bevel::kPressed:
            return {{kHi, kDark}, {kLight, kShadow}};
        case Bevel::kWindow:
            return {{kDark, kLight}, {kShadow, kHi}};
        case Bevel::kField:
            return {{kHi, kShadow}, {kLight, kDark}};
        case Bevel::kWell:
            return {{kHi, kShadow}};
        case Bevel::kBump:
            return {{kShadow, kHi}};
        case Bevel::kDent:
            return {{kHi, kShadow}};
        case Bevel::kThin:
            return {{kShadow, kHi}};
        case Bevel::kFlat:
            return {};
    }
    return {};
}

// kit.js 40-43.
int scale_of(int shared_scale) {
    return shared_scale == 2 ? 2 : 1;
}

// kit.js 451-452: scale 2 gives want(16) (16 * 2 / zoom layout px, 32 px on screen); scale 1
// gives 16 layout px, which the zoom scales as usual.
float terminal_font_px(int scale, float zoom) {
    return scale == 2 ? 32.0f / (zoom > 0 ? zoom : 1.0f) : 16.0f;
}

// kit.js 57-58.
int access_index(std::string_view label) {
    const auto amp = label.find('&');
    if (amp == std::string_view::npos || amp + 1 >= label.size())
        return -1;
    return static_cast<int>(amp);
}

// kit.js 73-75: String.replace(/&(.)/, "$1"), the first one only.
std::string plain(std::string_view label) {
    std::string s(label);
    const auto amp = s.find('&');
    if (amp != std::string::npos && amp + 1 < s.size())
        s.erase(amp, 1);
    return s;
}

// menu.js 326-327.
bool access_matches(std::string_view label, char ch) {
    std::string low(label);
    for (auto& c : low)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    const char want[3] = {'&', static_cast<char>(std::tolower(static_cast<unsigned char>(ch))), '\0'};
    return low.find(want) != std::string::npos;
}

// kit.js 326-344.
MenuSize menu_size(const std::vector<MenuRowText>& items, bool start) {
    int rows = 0, seps = 0;
    std::size_t longest = 0, keys = 0;
    for (const auto& it : items) {
        if (it.sep) {
            ++seps;
            continue;
        }
        ++rows;
        longest = std::max(longest, utf8_length(plain(it.label)));
        keys = std::max(keys, utf8_length(it.shortcut));
    }
    const float row = start ? MenuMetrics::kStartRow : MenuMetrics::kRow;
    const float l = static_cast<float>(longest), k = static_cast<float>(keys);
    const float w = start ? std::max(180.0f, MenuMetrics::kBanner + 48 + l * 6 + 24)
                          : std::max(120.0f, 28 + l * 6 + (keys != 0 ? k * 6 + 24 : 0.0f) + 20);
    const float h = static_cast<float>(rows) * row + static_cast<float>(seps) * MenuMetrics::kSep + MenuMetrics::kFrame * 2;
    return {w, h};
}

// kit.js 373.
float dialog_width(std::string_view kind) {
    static const std::map<std::string_view, int> kW = {
        {"prompt", 340},  {"confirm", 330}, {"run", 350},     {"shutdown", 330}, {"about", 380},
        {"taskman", 360}, {"display", 400}, {"mycomputer", 420}, {"bin", 380},  {"message", 330},
        {"network", 380}, {"welcome", 560}, {"logoff", 300},  {"datetime", 330}};
    const auto it = kW.find(kind);
    return it == kW.end() ? 340.0f : static_cast<float>(it->second);
}

// kit.js 431-437. herdr's paths start with "/" (no drive); a Windows path that has a drive keeps
// it, one that does not gets herdr's "C:".
std::string dos_path(std::string_view path, std::string_view home) {
    if (path.empty())
        return "C:\\";
    std::string p(path);
    for (auto& c : p)
        if (c == '/')
            c = '\\';
    std::string h(home);
    for (auto& c : h)
        if (c == '/')
            c = '\\';
    while (!h.empty() && h.back() == '\\')
        h.pop_back();
    auto lower = [](std::string s) {
        for (auto& c : s)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };
    const std::string lp = lower(p), lh = lower(h);
    if (!h.empty() && (lp == lh || (lp.size() > lh.size() && lp.compare(0, lh.size(), lh) == 0 && lp[lh.size()] == '\\')))
        p = "C:\\My Documents" + p.substr(h.size());
    else if (!(p.size() >= 2 && p[1] == ':'))
        p = "C:" + p;
    if (!p.empty() && p.back() == '\\')
        p.pop_back();
    return p.empty() ? std::string("C:\\") : p;
}

// taskbar.js 61-65.
std::string user_name(std::string_view home) {
    std::string last;
    std::string cur;
    for (const char c : home) {
        if (c == '\\' || c == '/') {
            if (!cur.empty())
                last = cur;
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.empty())
        last = cur;
    if (last.empty())
        return "User";
    last[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(last[0])));
    return last;
}

// dialog.js 227: cmd.split(/\s+/)[0].split("/").pop(); "\" is a separator here too.
std::string run_name(std::string_view command) {
    std::size_t a = 0;
    while (a < command.size() && std::isspace(static_cast<unsigned char>(command[a])) != 0)
        ++a;
    std::size_t b = a;
    while (b < command.size() && std::isspace(static_cast<unsigned char>(command[b])) == 0)
        ++b;
    std::string first(command.substr(a, b - a));
    const auto slash = first.find_last_of("/\\");
    return slash == std::string::npos ? first : first.substr(slash + 1);
}

// dialog.js 224.
std::vector<std::string> run_history(std::vector<std::string> history, const std::string& command) {
    std::vector<std::string> out = {command};
    for (auto& h : history)
        if (h != command)
            out.push_back(std::move(h));
    if (out.size() > 12)
        out.resize(12);
    return out;
}

// ---------------------------------------------------------------- time

// taskbar.js 36-42.
std::string clock_text(const DateTime& d) {
    int h = d.hour;
    const bool pm = h >= 12;
    h = h % 12 == 0 ? 12 : h % 12;
    return std::to_string(h) + ":" + pad(d.minute, 2) + (pm ? " PM" : " AM");
}

std::string month_name(int month) {
    static const char* const kMonths[] = {"January", "February", "March",     "April",   "May",      "June",
                                          "July",    "August",   "September", "October", "November", "December"};
    return kMonths[std::clamp(month, 0, 11)];
}

// taskbar.js 44-46 (en-US long date).
std::string long_date(const DateTime& d) {
    static const char* const kDays[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
    return std::string(kDays[std::clamp(d.weekday, 0, 6)]) + ", " + month_name(d.month) + " " + std::to_string(d.day) + ", " +
           std::to_string(d.year);
}

// dialog.js 687: toLocaleTimeString("en-US").
std::string time_text(const DateTime& d) {
    int h = d.hour % 12 == 0 ? 12 : d.hour % 12;
    return std::to_string(h) + ":" + pad(d.minute, 2) + ":" + pad(d.second, 2) + (d.hour >= 12 ? " PM" : " AM");
}

// dialog.js 688-691.
std::string time_zone_text(const DateTime& d) {
    const int offset = d.utc_offset_minutes;
    const int a = std::abs(offset);
    return std::string("UTC") + (offset < 0 ? "-" : "+") + pad(a / 60, 2) + ":" + pad(a % 60, 2);
}

// dialog.js 656-666.
std::vector<std::array<int, 7>> calendar(int year, int month) {
    // Day of the week of the 1st (Sakamoto), and the month's length.
    static const int kT[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    const int m = month + 1;
    int y = year;
    if (m < 3)
        y -= 1;
    const int first = (y + y / 4 - y / 100 + y / 400 + kT[m - 1] + 1) % 7;
    static const int kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int days = kDays[std::clamp(month, 0, 11)];
    if (month == 1 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0))
        days = 29;
    std::vector<int> cells(static_cast<std::size_t>(first), 0);
    for (int i = 1; i <= days; ++i)
        cells.push_back(i);
    while (cells.size() % 7 != 0)
        cells.push_back(0);
    std::vector<std::array<int, 7>> weeks;
    for (std::size_t i = 0; i < cells.size(); i += 7) {
        std::array<int, 7> w{};
        for (std::size_t j = 0; j < 7; ++j)
            w[j] = cells[i + j];
        weeks.push_back(w);
    }
    return weeks;
}

// dialog.js 927-958.
std::vector<ClockDot> clock_face(const DateTime& d) {
    constexpr double kSize = 96;
    constexpr double kPi = std::numbers::pi;
    std::vector<ClockDot> out;
    for (int i = 0; i < 12; ++i) {
        const double r = i * 30 * kPi / 180;
        const double x = kSize / 2 + std::sin(r) * 40 - 2;
        const double y = kSize / 2 - std::cos(r) * 40 - 2;
        out.push_back({js_round(x), js_round(y), 4, i % 3 == 0 ? color::kNavy : 0x008080u});
    }
    auto dots = [&](double deg, int len, int size, std::uint32_t c) {
        const double r = deg * kPi / 180;
        for (int i = 0; i < len; i += size) {
            const double x = kSize / 2 + std::sin(r) * i - size / 2.0;
            const double y = kSize / 2 - std::cos(r) * i - size / 2.0;
            out.push_back({js_round(x), js_round(y), size, c});
        }
    };
    const int h = d.hour % 12;
    dots(h * 30 + d.minute / 2.0, 24, 3, color::kText);
    dots(d.minute * 6, 34, 2, color::kText);
    dots(d.second * 6, 36, 1, 0xff0000);
    return out;
}

// ---------------------------------------------------------------- Mines (mines.js)

Mines::Mines(Options options, Random random) : options_(std::move(options)), random_(std::move(random)) {
    if (!random_)
        random_ = default_random;
    if (!level(options_.level))  // mines.js 27
        options_.level = "beginner";
    reset();
}

// mines.js 6.
std::optional<std::array<int, 3>> Mines::level(std::string_view name) {
    if (name == "beginner")
        return std::array<int, 3>{9, 9, 10};
    if (name == "intermediate")
        return std::array<int, 3>{16, 16, 40};
    if (name == "expert")
        return std::array<int, 3>{30, 16, 99};
    return std::nullopt;
}

// mines.js 34-42.
void Mines::reset() {
    timer_ = false;
    const auto l = *level(options_.level);
    cols_ = l[0];
    rows_ = l[1];
    total_ = l[2];
    cells_.assign(static_cast<std::size_t>(cols_ * rows_), Cell{});
    state_ = State::kReady;
    seconds_ = 0;
    held = -1;
    boom_ = -1;
}

// mines.js 44-51.
std::vector<int> Mines::neighbors(int i) const {
    std::vector<int> result;
    const int x = i % cols_, y = i / cols_;
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx)
            if ((dx != 0 || dy != 0) && x + dx >= 0 && x + dx < cols_ && y + dy >= 0 && y + dy < rows_)
                result.push_back((y + dy) * cols_ + x + dx);
    return result;
}

// mines.js 53-69.
void Mines::start(int i, long long now_ms) {
    std::set<int> excluded = {i};
    for (const int n : neighbors(i))
        excluded.insert(n);
    std::vector<int> pool;
    for (int j = 0; j < static_cast<int>(cells_.size()); ++j)
        if (!excluded.contains(j))
            pool.push_back(j);
    for (int n = 0; n < total_ && n < static_cast<int>(pool.size()); ++n) {
        const int j = n + static_cast<int>(std::floor(random_() * static_cast<double>(static_cast<int>(pool.size()) - n)));
        std::swap(pool[static_cast<std::size_t>(n)], pool[static_cast<std::size_t>(j)]);
        cells_[static_cast<std::size_t>(pool[static_cast<std::size_t>(n)])].mine = true;
    }
    for (int j = 0; j < static_cast<int>(cells_.size()); ++j) {
        int n = 0;
        for (const int k : neighbors(j))
            n += cells_[static_cast<std::size_t>(k)].mine ? 1 : 0;
        cells_[static_cast<std::size_t>(j)].n = n;
    }
    state_ = State::kPlaying;
    started_ = now_ms;
    timer_ = true;
}

// mines.js 71-104.
std::string Mines::reveal(int i, long long now_ms) {
    if (i < 0 || i >= static_cast<int>(cells_.size()))
        return {};
    {
        const Cell& c = cells_[static_cast<std::size_t>(i)];
        if (c.open || c.mark == 1 || state_ == State::kLost || state_ == State::kWon)
            return {};
    }
    if (state_ == State::kReady)
        start(i, now_ms);
    if (cells_[static_cast<std::size_t>(i)].mine) {
        boom_ = i;
        state_ = State::kLost;
        held = -1;
        timer_ = false;
        return options_.sound ? "mines-boom" : "";
    }
    std::vector<int> todo = {i};
    while (!todo.empty()) {
        const int j = todo.back();
        todo.pop_back();
        Cell& cell = cells_[static_cast<std::size_t>(j)];
        if (cell.open || cell.mark == 1 || cell.mine)
            continue;
        cell.open = true;
        cell.mark = 0;
        if (cell.n == 0)
            for (const int k : neighbors(j))
                todo.push_back(k);
    }
    if (std::all_of(cells_.begin(), cells_.end(), [](const Cell& c) { return c.mine || c.open; })) {
        state_ = State::kWon;
        held = -1;
        seconds_ = std::min(999, static_cast<int>((now_ms - started_) / 1000));
        timer_ = false;
        for (auto& c : cells_)
            if (c.mine)
                c.mark = 1;
        const auto best = options_.best.find(options_.level);
        if (best == options_.best.end() || seconds_ < best->second) {
            options_.best[options_.level] = seconds_;
            saved = true;
        }
        return options_.sound ? "tada" : "";
    }
    return {};
}

// mines.js 106-112.
std::string Mines::chord(int i, long long now_ms) {
    if (i < 0 || i >= static_cast<int>(cells_.size()))
        return {};
    const Cell& c = cells_[static_cast<std::size_t>(i)];
    if (!c.open || c.n == 0 || state_ != State::kPlaying)
        return {};
    const auto near = neighbors(i);
    int flags = 0;
    for (const int j : near)
        flags += cells_[static_cast<std::size_t>(j)].mark == 1 ? 1 : 0;
    std::string sound;
    if (flags == c.n)
        for (const int j : near)
            if (auto s = reveal(j, now_ms); !s.empty())
                sound = s;
    return sound;
}

// mines.js 173.
void Mines::mark(int i) {
    if (i < 0 || i >= static_cast<int>(cells_.size()))
        return;
    Cell& c = cells_[static_cast<std::size_t>(i)];
    if (!c.open && (state_ == State::kReady || state_ == State::kPlaying))
        c.mark = (c.mark + 1) % (options_.marks ? 3 : 2);
}

// mines.js 64-68.
bool Mines::tick(long long now_ms) {
    if (!timer_)
        return false;
    const int before = seconds_;
    seconds_ = std::min(999, static_cast<int>((now_ms - started_) / 1000));
    if (seconds_ == 999)
        timer_ = false;
    return seconds_ != before;
}

// mines.js 114-123.
void Mines::request(const std::string& cmd) {
    if (level(cmd)) {
        options_.level = cmd;
        saved = true;
        reset();
    } else if (cmd == "new") {
        reset();
    } else if (cmd == "marks" || cmd == "sound" || cmd == "color") {
        bool& v = cmd == "marks" ? options_.marks : cmd == "sound" ? options_.sound : options_.color;
        v = !v;
        if (cmd == "marks" && !options_.marks)
            for (auto& c : cells_)
                if (c.mark == 2)
                    c.mark = 0;
        saved = true;
    } else if (cmd == "exit") {
        reset();
    }
}

int Mines::mines_left() const {
    int flags = 0;
    for (const auto& c : cells_)
        flags += c.mark == 1 ? 1 : 0;
    return total_ - flags;
}

// mines.js 185.
std::string Mines::face() const {
    if (state_ == State::kLost)
        return "dead";
    if (state_ == State::kWon)
        return "cool";
    return held >= 0 ? "ooh" : "smile";
}

// mines.js 122.
std::string Mines::best_text() const {
    std::string out;
    for (const char* name : {"Beginner", "Intermediate", "Expert"}) {
        std::string key = name;
        for (auto& c : key)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        const auto it = options_.best.find(key);
        if (!out.empty())
            out += "\n";
        out += std::string(name) + ": " + std::to_string(it == options_.best.end() ? 999 : it->second) + " seconds";
    }
    return out;
}

// mines.js 16.
std::string counter_chars(int value) {
    if (value < 0)
        return "-" + pad(std::min(99, -value), 2);
    return pad(std::min(999, value), 3);
}

// mines.js 8, 18.
std::array<bool, 7> segments_lit(char ch) {
    static const char* const kSegments[] = {"abcdef", "bc",      "abdeg", "abcdg",   "bcfg",
                                            "acdfg",  "acdefg", "abc",   "abcdefg", "abcdfg"};
    const std::string_view lit = ch == '-' ? std::string_view("g")
                                 : ch >= '0' && ch <= '9' ? std::string_view(kSegments[ch - '0'])
                                                          : std::string_view();
    std::array<bool, 7> out{};
    for (int i = 0; i < 7; ++i)
        out[static_cast<std::size_t>(i)] = lit.find(static_cast<char>('a' + i)) != std::string_view::npos;
    return out;
}

const std::array<std::array<int, 4>, 7>& segment_rects() {
    static const std::array<std::array<int, 4>, 7> kRects = {{{2, 1, 7, 2},
                                                              {9, 3, 2, 7},
                                                              {9, 12, 2, 7},
                                                              {2, 19, 7, 2},
                                                              {0, 12, 2, 7},
                                                              {0, 3, 2, 7},
                                                              {2, 10, 7, 2}}};
    return kRects;
}

std::uint32_t mines_number_color(int n) {
    static const std::uint32_t kColors[] = {color::kFace, 0x0000ff, 0x008000, 0xff0000, 0x000080,
                                            0x800000,     0x008080, 0x000000, 0x808080};
    return kColors[std::clamp(n, 0, 8)];
}

// ---------------------------------------------------------------- Solitaire (sol.js)

Solitaire::Solitaire(Random random) : random_(std::move(random)) {
    if (!random_)
        random_ = default_random;
    deal();
}

// sol.js 223-234 (with stop(), 235-238).
void Solitaire::deal() {
    animating = false;
    std::vector<int> cards(52);
    for (int i = 0; i < 52; ++i)
        cards[static_cast<std::size_t>(i)] = i;
    for (int i = 51; i > 0; --i) {
        const int j = static_cast<int>(std::floor(random_() * (i + 1)));
        std::swap(cards[static_cast<std::size_t>(i)], cards[static_cast<std::size_t>(j)]);
    }
    g = Game{};
    for (int i = 0; i < 7; ++i) {
        for (int j = 0; j <= i; ++j)
            g.tab[static_cast<std::size_t>(i)].push_back({cards[static_cast<std::size_t>(j)], j == i});
        cards.erase(cards.begin(), cards.begin() + i + 1);
    }
    g.stock = cards;
    seconds = 0;
    running = false;
    won = false;
    undo.reset();
    pick.reset();
    trails.clear();
}

std::size_t Solitaire::pile_size(const Loc& loc) const {
    switch (loc.type) {
        case Pile::kTab:
            return g.tab[static_cast<std::size_t>(loc.i)].size();
        case Pile::kHome:
            return g.home[static_cast<std::size_t>(loc.i)].size();
        case Pile::kWaste:
            return g.waste.size();
    }
    return 0;
}

// sol.js 250.
std::vector<int> Solitaire::cards(const Loc& loc) const {
    std::vector<int> out;
    const auto from = static_cast<std::size_t>(std::max(0, loc.n));
    if (loc.type == Pile::kTab) {
        const auto& p = g.tab[static_cast<std::size_t>(loc.i)];
        for (std::size_t j = from; j < p.size(); ++j)
            out.push_back(p[j].c);
    } else {
        const auto& p = loc.type == Pile::kHome ? g.home[static_cast<std::size_t>(loc.i)] : g.waste;
        for (std::size_t j = from; j < p.size(); ++j)
            out.push_back(p[j]);
    }
    return out;
}

// sol.js 251-259.
bool Solitaire::valid_pick(const Loc& loc) const {
    const int size = static_cast<int>(pile_size(loc));
    if (loc.n < 0 || size == 0)
        return false;
    if (loc.type != Pile::kTab)
        return loc.n == size - 1;
    const auto& p = g.tab[static_cast<std::size_t>(loc.i)];
    if (loc.n >= size || !p[static_cast<std::size_t>(loc.n)].up)
        return false;
    for (int j = loc.n + 1; j < size; ++j) {
        const auto& a = p[static_cast<std::size_t>(j)];
        const auto& b = p[static_cast<std::size_t>(j - 1)];
        if (!a.up || red(a.c) == red(b.c) || rank(a.c) + 1 != rank(b.c))
            return false;
    }
    return true;
}

// sol.js 248.
void Solitaire::save() {
    undo = g;
    begin();
}

// sol.js 239-247: the clock starts (the caller ticks it every second).
void Solitaire::begin() {
    if (won)
        return;
    running = true;
}

// sol.js 242-246.
void Solitaire::tick_second() {
    ++seconds;
    if (scoring && seconds % 10 == 0)
        g.score -= 2;
}

// sol.js 260-275.
bool Solitaire::move(const Loc& from, const Loc& to) {
    if (!valid_pick(from) || (from.type == to.type && (from.i == to.i || to.type == Pile::kHome)))
        return false;
    const auto moving = cards(from);
    const int c = moving.front();
    if (to.type == Pile::kHome) {
        const auto& dest = g.home[static_cast<std::size_t>(to.i)];
        if (moving.size() != 1 || rank(c) != static_cast<int>(dest.size()) + 1 ||
            (!dest.empty() && suit(dest.back()) != suit(c)))
            return false;
    } else if (to.type == Pile::kTab) {
        const auto& dest = g.tab[static_cast<std::size_t>(to.i)];
        if (dest.empty() ? rank(c) != 13
                         : !dest.back().up || red(dest.back().c) == red(c) || rank(dest.back().c) != rank(c) + 1)
            return false;
    } else {
        return false;
    }
    save();
    switch (from.type) {
        case Pile::kTab:
            g.tab[static_cast<std::size_t>(from.i)].resize(static_cast<std::size_t>(from.n));
            break;
        case Pile::kHome:
            g.home[static_cast<std::size_t>(from.i)].resize(static_cast<std::size_t>(from.n));
            break;
        case Pile::kWaste:
            g.waste.resize(static_cast<std::size_t>(from.n));
            break;
    }
    if (to.type == Pile::kTab)
        for (const int x : moving)
            g.tab[static_cast<std::size_t>(to.i)].push_back({x, true});
    else
        for (const int x : moving)
            g.home[static_cast<std::size_t>(to.i)].push_back(x);
    if (scoring)
        g.score += to.type == Pile::kHome ? 10 : from.type == Pile::kHome ? -15 : from.type == Pile::kWaste ? 5 : 0;
    pick.reset();
    if (std::all_of(g.home.begin(), g.home.end(), [](const auto& p) { return p.size() == 13; }))
        victory();
    return true;
}

// sol.js 276-279.
void Solitaire::foundation(const Loc& loc) {
    if (!valid_pick(loc) || cards(loc).size() != 1)
        return;
    for (int i = 0; i < 4; ++i)
        if (move(loc, {Pile::kHome, i, 0}))
            return;
}

// sol.js 280-288.
void Solitaire::stock() {
    if (animating) {
        animating = false;
        return;
    }
    if (won || (g.stock.empty() && g.waste.empty()))
        return;
    save();
    pick.reset();
    if (!g.stock.empty()) {
        for (int i = 0; i < draw && !g.stock.empty(); ++i) {
            g.waste.push_back(g.stock.back());
            g.stock.pop_back();
        }
    } else {
        g.stock.assign(g.waste.rbegin(), g.waste.rend());
        g.waste.clear();
        if (scoring)
            g.score -= draw == 1 ? 100 : 20;
    }
}

// sol.js 289-300.
Solitaire::Down Solitaire::down(const Loc& loc) {
    if (animating) {
        animating = false;
        return Down::kNothing;
    }
    if (pick && move(*pick, loc))
        return Down::kMoved;
    if (loc.type == Pile::kTab) {
        auto& p = g.tab[static_cast<std::size_t>(loc.i)];
        if (loc.n >= 0 && loc.n < static_cast<int>(p.size()) && !p[static_cast<std::size_t>(loc.n)].up) {
            if (loc.n == static_cast<int>(p.size()) - 1) {
                save();
                p[static_cast<std::size_t>(loc.n)].up = true;
                if (scoring)
                    g.score += 5;
                return Down::kTurned;
            }
            return Down::kNothing;
        }
    }
    if (!valid_pick(loc)) {
        pick.reset();
        return Down::kUnpick;
    }
    return Down::kPick;
}

// sol.js 305-309.
std::optional<Solitaire::Loc> Solitaire::target(float x, float y) {
    if (y < 116) {
        for (int i = 0; i < 4; ++i)
            if (x >= 258 + i * 82 && x <= 329 + i * 82)
                return Loc{Pile::kHome, i, 0};
    }
    if (y >= 122) {
        const int i = static_cast<int>(std::floor((x - 12) / 82));
        if (i >= 0 && i < 7 && x <= 83 + i * 82)
            return Loc{Pile::kTab, i, 0};
    }
    return std::nullopt;
}

// sol.js 310-312.
void Solitaire::victory() {
    running = false;
    won = true;
    pick.reset();
    sound = "tada";
    trails.clear();
    frame_ = 0;
    launched_ = 0;
    flying_.reset();
    animating = true;
}

// sol.js 313-327.
bool Solitaire::animate() {
    if (!animating)
        return false;
    ++frame_;
    if (!flying_) {
        if (launched_ == 52) {
            animating = false;
            return false;
        }
        const int n = launched_++;
        flying_ = Flying{(n % 4) * 13 + 12 - n / 4, static_cast<float>(258 + (n % 4) * 82), 10.0f,
                         static_cast<float>(-3 - random_() * 4), -2.0f};
    }
    Flying& f = *flying_;
    f.x += f.vx;
    f.y += f.vy;
    f.vy += 0.7f;
    if (f.y >= kH - kCH) {
        f.y = kH - kCH;
        f.vy = -std::max(4.0f, std::abs(f.vy) * 0.8f);
    }
    trails.push_back({f.c, f.x, f.y});
    if (trails.size() > 300)
        trails.erase(trails.begin());
    if (f.x < -kCW)
        flying_.reset();
    if (frame_ > 5200)
        animating = false;
    return true;
}

// sol.js 332-340 (the commands that change the game).
void Solitaire::command(const std::string& cmd) {
    if (cmd == "deal")
        deal();
    if (cmd == "undo" && undo) {
        g = *undo;
        undo.reset();
        pick.reset();
        trails.clear();
        animating = false;
        won = false;
        begin();
    }
    if (cmd == "autowin") {
        for (int s = 0; s < 4; ++s) {
            g.home[static_cast<std::size_t>(s)].clear();
            for (int r = 0; r < 13; ++r)
                g.home[static_cast<std::size_t>(s)].push_back(s * 13 + r);
        }
        g.stock.clear();
        g.waste.clear();
        for (auto& t : g.tab)
            t.clear();
        victory();
    }
}

// sol.js 419-420.
float Solitaire::tab_step(int i) const {
    const auto& p = g.tab[static_cast<std::size_t>(i)];
    int face = 0;
    for (const auto& c : p)
        face += c.up ? 1 : 0;
    const int down = static_cast<int>(p.size()) - face;
    return std::min(18.0f, static_cast<float>(kH - 126 - kCH - down * 7) / static_cast<float>(std::max(1, face - 1)));
}

}  // namespace agentdesk::ui::win98
