// Herdr 98's rules (herdr-app defaults/mods/win98: kit.js, taskbar.js, dialog.js, mines.js,
// sol.js) as ported to agentdesk::ui::win98.
#include "agentdesk/ui/win98.hpp"

#include <gtest/gtest.h>

using namespace agentdesk::ui::win98;

namespace {

// A fixed Math.random: always the same value.
Mines::Random fixed(double v) {
    return [v] { return v; };
}

// A repeatable Math.random.
Mines::Random lcg(unsigned seed) {
    return [state = seed]() mutable {
        state = state * 1664525u + 1013904223u;
        return static_cast<double>(state >> 8) / static_cast<double>(1u << 24);
    };
}

// The first seed whose first click (cell 0) leaves the game running.
unsigned playing_seed() {
    for (unsigned seed = 1;; ++seed) {
        Mines m({}, lcg(seed));
        m.reveal(0, 0);
        if (m.state() == Mines::State::kPlaying)
            return seed;
    }
}

}  // namespace

// kit.js 84-95.
TEST(Win98Kit, BevelLayersFromOutsideIn) {
    const auto raised = bevel_layers(Bevel::kRaised);
    ASSERT_EQ(raised.size(), 2u);
    EXPECT_EQ(raised[0].rb, color::kDark);
    EXPECT_EQ(raised[0].tl, color::kHi);
    EXPECT_EQ(raised[1].rb, color::kShadow);
    EXPECT_EQ(raised[1].tl, color::kLight);
    EXPECT_EQ(bevel_layers(Bevel::kWell).size(), 1u);
    EXPECT_TRUE(bevel_layers(Bevel::kFlat).empty());
    const auto window = bevel_layers(Bevel::kWindow);
    EXPECT_EQ(window[0].tl, color::kLight);
    EXPECT_EQ(window[1].tl, color::kHi);
}

// kit.js 40-51, 451-452.
TEST(Win98Kit, PixelsAndScale) {
    EXPECT_EQ(scale_of(2), 2);
    EXPECT_EQ(scale_of(1), 1);
    EXPECT_EQ(scale_of(0), 1);
    EXPECT_EQ(scale_of(3), 1);
    const Unit u{2.0f};
    EXPECT_FLOAT_EQ(u(11), 22.0f);
    EXPECT_FLOAT_EQ(terminal_font_px(1, 1.5f), 16.0f);
    EXPECT_FLOAT_EQ(terminal_font_px(2, 1.0f), 32.0f);
    EXPECT_FLOAT_EQ(terminal_font_px(2, 2.0f), 16.0f);  // 32 px on screen at zoom 2
}

// kit.js 55-75, menu.js 326-327.
TEST(Win98Kit, AccessKeys) {
    EXPECT_EQ(access_index("&File"), 0);
    EXPECT_EQ(access_index("Sh&ut Down..."), 2);
    EXPECT_EQ(access_index("Start"), -1);
    EXPECT_EQ(access_index("Taskbar &"), -1);  // a last "&" is no access key
    EXPECT_EQ(plain("Sh&ut Down..."), "Shut Down...");
    EXPECT_EQ(plain("&Taskbar & Start Menu..."), "Taskbar & Start Menu...");
    EXPECT_TRUE(access_matches("S&ettings", 'E'));
    EXPECT_FALSE(access_matches("S&ettings", 's'));
}

// kit.js 326-344.
TEST(Win98Kit, MenuSize) {
    // A popup: 28 + longest * 6 + (shortcuts: keys * 6 + 24) + 20, at least 120; rows 18, seps 9,
    // the frame 3 above and below.
    const auto m = menu_size({{"&Restore", ""}, {"", "", true}, {"&Close", "Alt+F4"}}, false);
    EXPECT_FLOAT_EQ(m.w, 28.0f + 7 * 6 + (6 * 6 + 24) + 20);
    EXPECT_FLOAT_EQ(m.h, 2 * 18 + 9 + 6);
    // The Start menu: rows 34, banner 21, at least 180.
    const auto s = menu_size({{"Herdr Update", ""}, {"", "", true}, {"Sh&ut Down...", ""}}, true);
    EXPECT_FLOAT_EQ(s.w, 180.0f);
    EXPECT_FLOAT_EQ(s.h, 2 * 34 + 9 + 6);
    EXPECT_FLOAT_EQ(dialog_width("welcome"), 560.0f);
    EXPECT_FLOAT_EQ(dialog_width("nope"), 340.0f);
}

// kit.js 431-437, taskbar.js 61-65, dialog.js 224-227.
TEST(Win98Kit, PathsAndNames) {
    EXPECT_EQ(dos_path("", "C:\\Users\\ann"), "C:\\");
    EXPECT_EQ(dos_path("C:\\Users\\ann", "C:\\Users\\ann"), "C:\\My Documents");
    EXPECT_EQ(dos_path("c:\\users\\ann\\src\\x\\", "C:\\Users\\Ann"), "C:\\My Documents\\src\\x");
    EXPECT_EQ(dos_path("D:\\src", "C:\\Users\\ann"), "D:\\src");
    EXPECT_EQ(dos_path("/home/ann/x", "/home/ann"), "C:\\My Documents\\x");
    EXPECT_EQ(dos_path("/tmp", "/home/ann"), "C:\\tmp");
    EXPECT_EQ(user_name("C:\\Users\\ann"), "Ann");
    EXPECT_EQ(user_name(""), "User");
    EXPECT_EQ(run_name("  /usr/bin/claude --resume"), "claude");
    EXPECT_EQ(run_name("C:\\tools\\codex.exe"), "codex.exe");
    const auto h = run_history({"a", "b", "c"}, "b");
    EXPECT_EQ(h, (std::vector<std::string>{"b", "a", "c"}));
    std::vector<std::string> many;
    for (int i = 0; i < 20; ++i)
        many.push_back(std::to_string(i));
    EXPECT_EQ(run_history(many, "x").size(), 12u);
}

// taskbar.js 36-46, dialog.js 655-691.
TEST(Win98Kit, ClockTexts) {
    DateTime d;
    d.year = 2026;
    d.month = 10;
    d.day = 19;
    d.weekday = 4;
    d.hour = 0;
    d.minute = 5;
    d.second = 9;
    EXPECT_EQ(clock_text(d), "12:05 AM");
    d.hour = 13;
    EXPECT_EQ(clock_text(d), "1:05 PM");
    EXPECT_EQ(time_text(d), "1:05:09 PM");
    EXPECT_EQ(long_date(d), "Thursday, November 19, 2026");
    d.utc_offset_minutes = 480;
    EXPECT_EQ(time_zone_text(d), "UTC+08:00");
    d.utc_offset_minutes = -330;
    EXPECT_EQ(time_zone_text(d), "UTC-05:30");
    // November 2026 starts on a Sunday: 30 days, 5 weeks.
    const auto weeks = calendar(2026, 10);
    ASSERT_EQ(weeks.size(), 5u);
    EXPECT_EQ(weeks[0][0], 1);
    EXPECT_EQ(weeks[4][1], 30);
    EXPECT_EQ(weeks[4][2], 0);
    // February 2024: Thursday the 1st, 29 days.
    const auto feb = calendar(2024, 1);
    EXPECT_EQ(feb[0][4], 1);
    EXPECT_EQ(feb.back()[4], 29);
}

// dialog.js 940-946: twelve 4 px ticks on a radius of 40 around (48, 48), navy every third.
TEST(Win98Kit, ClockFaceTicks) {
    DateTime d;
    const auto dots = clock_face(d);
    ASSERT_GE(dots.size(), 12u);
    EXPECT_EQ(dots[0].x, 46);
    EXPECT_EQ(dots[0].y, 6);
    EXPECT_EQ(dots[0].color, color::kNavy);
    EXPECT_EQ(dots[3].x, 86);
    EXPECT_EQ(dots[3].y, 46);
    EXPECT_EQ(dots[1].color, 0x008080u);
}

// mines.js 8-21.
TEST(Win98Mines, Counter) {
    EXPECT_EQ(counter_chars(10), "010");
    EXPECT_EQ(counter_chars(1234), "999");
    EXPECT_EQ(counter_chars(-3), "-03");
    EXPECT_EQ(counter_chars(-150), "-99");
    const auto one = segments_lit('1');
    EXPECT_FALSE(one[0]);
    EXPECT_TRUE(one[1]);
    EXPECT_TRUE(one[2]);
    const auto minus = segments_lit('-');
    EXPECT_TRUE(minus[6]);
    EXPECT_FALSE(minus[0]);
    EXPECT_EQ(segment_rects()[0], (std::array<int, 4>{2, 1, 7, 2}));
    EXPECT_EQ(mines_number_color(1), 0x0000ffu);
}

// mines.js 6, 34-69: the first click is never a mine, nor next to one.
TEST(Win98Mines, FirstRevealIsSafeAndStartsTheClock) {
    Mines m({}, fixed(0.5));
    EXPECT_EQ(m.cols(), 9);
    EXPECT_EQ(m.total(), 10);
    EXPECT_EQ(m.state(), Mines::State::kReady);
    const int center = 4 * 9 + 4;
    m.reveal(center, 1000);
    EXPECT_NE(m.state(), Mines::State::kLost);
    EXPECT_FALSE(m.cells()[static_cast<std::size_t>(center)].mine);
    int mines = 0;
    for (const auto& c : m.cells())
        mines += c.mine ? 1 : 0;
    EXPECT_EQ(mines, 10);
    for (const int n : m.neighbors(center))
        EXPECT_FALSE(m.cells()[static_cast<std::size_t>(n)].mine);
    EXPECT_EQ(m.cells()[static_cast<std::size_t>(center)].n, 0);
    EXPECT_TRUE(m.running());
    EXPECT_TRUE(m.tick(3500));
    EXPECT_EQ(m.seconds(), 2);
}

TEST(Win98Mines, MarksBoomAndWin) {
    const unsigned seed = playing_seed();
    Mines m({}, lcg(seed));
    // A board where the first click does not open everything.
    m.reveal(0, 0);
    ASSERT_EQ(m.state(), Mines::State::kPlaying);
    int mine = -1;
    for (int i = 0; i < 81; ++i)
        if (m.cells()[static_cast<std::size_t>(i)].mine) {
            mine = i;
            break;
        }
    ASSERT_GE(mine, 0);
    m.mark(mine);
    EXPECT_EQ(m.cells()[static_cast<std::size_t>(mine)].mark, 1);
    EXPECT_EQ(m.mines_left(), 9);
    EXPECT_EQ(m.reveal(mine, 10), "");  // a flagged cell does not open
    m.mark(mine);
    EXPECT_EQ(m.cells()[static_cast<std::size_t>(mine)].mark, 2);  // (?) with marks on
    m.mark(mine);
    EXPECT_EQ(m.cells()[static_cast<std::size_t>(mine)].mark, 0);
    EXPECT_EQ(m.face(), "smile");
    m.held = 3;
    EXPECT_EQ(m.face(), "ooh");
    // Open every safe cell: a win, every mine flagged, best time kept.
    Mines w({}, lcg(seed));
    w.reveal(0, 0);
    std::string sound;
    for (int i = 0; i < 81; ++i)
        if (!w.cells()[static_cast<std::size_t>(i)].mine)
            if (auto s = w.reveal(i, 42000); !s.empty())
                sound = s;
    EXPECT_EQ(w.state(), Mines::State::kWon);
    EXPECT_EQ(sound, "tada");
    EXPECT_EQ(w.face(), "cool");
    EXPECT_EQ(w.mines_left(), 0);
    EXPECT_EQ(w.options().best.at("beginner"), 42);
    EXPECT_TRUE(w.saved);
    EXPECT_NE(w.best_text().find("Beginner: 42 seconds"), std::string::npos);
    EXPECT_NE(w.best_text().find("Expert: 999 seconds"), std::string::npos);
    // A mine: lost, boom there, the sound.
    Mines l({}, lcg(seed));
    l.reveal(0, 0);
    EXPECT_EQ(l.reveal(mine, 5), "mines-boom");
    EXPECT_EQ(l.state(), Mines::State::kLost);
    EXPECT_EQ(l.boom(), mine);
    EXPECT_EQ(l.face(), "dead");
    EXPECT_FALSE(l.running());
}

// mines.js 106-123.
TEST(Win98Mines, ChordAndRequests) {
    Mines m({}, lcg(playing_seed()));
    m.reveal(0, 0);
    // Find an open number whose mines we flag, then chord it.
    for (int i = 0; i < 81; ++i) {
        const auto& c = m.cells()[static_cast<std::size_t>(i)];
        if (!c.open || c.n == 0)
            continue;
        for (const int j : m.neighbors(i))
            if (m.cells()[static_cast<std::size_t>(j)].mine &&
                m.cells()[static_cast<std::size_t>(j)].mark != 1)
                m.mark(j);
        m.chord(i, 100);
        for (const int j : m.neighbors(i)) {
            const auto& n = m.cells()[static_cast<std::size_t>(j)];
            EXPECT_TRUE(n.mine ? n.mark == 1 : n.open);
        }
        break;
    }
    m.request("expert");
    EXPECT_EQ(m.cols(), 30);
    EXPECT_EQ(m.rows(), 16);
    EXPECT_EQ(m.total(), 99);
    EXPECT_EQ(m.state(), Mines::State::kReady);
    m.request("marks");
    EXPECT_FALSE(m.options().marks);
    m.mark(0);
    m.mark(0);
    EXPECT_EQ(m.cells()[0].mark, 0);  // none, flag, none without (?)
    Mines::Options bad;
    bad.level = "huge";
    EXPECT_EQ(Mines(bad).options().level, "beginner");
}

// sol.js 223-234.
TEST(Win98Solitaire, DealSevenColumns) {
    Solitaire s(fixed(0.0));
    for (int i = 0; i < 7; ++i) {
        ASSERT_EQ(s.g.tab[static_cast<std::size_t>(i)].size(), static_cast<std::size_t>(i + 1));
        for (int j = 0; j <= i; ++j)
            EXPECT_EQ(s.g.tab[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)].up, j == i);
    }
    EXPECT_EQ(s.g.stock.size(), 52u - 28u);
    EXPECT_TRUE(s.g.waste.empty());
    EXPECT_EQ(Solitaire::rank(0), 1);
    EXPECT_EQ(Solitaire::rank(12), 13);
    EXPECT_TRUE(Solitaire::red(13));
    EXPECT_FALSE(Solitaire::red(26));
}

// sol.js 251-288.
TEST(Win98Solitaire, RulesAndScoring) {
    Solitaire s(fixed(0.0));
    s.g = {};
    // Column 0: a red king; column 1: a black queen; column 2: an ace of spades.
    s.g.tab[0] = {{12, true}};       // K of suit 0 (red)
    s.g.tab[1] = {{26 + 11, true}};  // Q of suit 2 (black)
    s.g.tab[2] = {{3 * 13, true}};   // A of suit 3
    s.g.tab[3] = {{5, false}, {26 + 5, true}};
    EXPECT_TRUE(s.valid_pick({Solitaire::Pile::kTab, 1, 0}));
    EXPECT_TRUE(s.move({Solitaire::Pile::kTab, 1, 0}, {Solitaire::Pile::kTab, 0, 0}));
    EXPECT_EQ(s.g.tab[0].size(), 2u);
    EXPECT_TRUE(s.running);
    ASSERT_TRUE(s.undo.has_value());
    // A queen on a queen: no.
    s.g.tab[4] = {{11, true}};
    EXPECT_FALSE(s.move({Solitaire::Pile::kTab, 4, 0}, {Solitaire::Pile::kTab, 0, 0}));
    // Ace home: +10. Double-click sends it to the first foundation that takes it.
    s.foundation({Solitaire::Pile::kTab, 2, 0});
    EXPECT_EQ(s.g.home[0].size(), 1u);
    EXPECT_EQ(s.g.score, 10);
    // Turning the face-down card left at the end of a column: +5.
    s.g.tab[3].pop_back();
    EXPECT_EQ(s.down({Solitaire::Pile::kTab, 3, 0}), Solitaire::Down::kTurned);
    EXPECT_TRUE(s.g.tab[3][0].up);
    EXPECT_EQ(s.g.score, 15);
    // Stock: draw three, then turn the waste over (-20 with draw three).
    s.g.stock = {1, 2, 3, 4};
    s.g.waste.clear();
    s.stock();
    EXPECT_EQ(s.g.waste, (std::vector<int>{4, 3, 2}));
    s.stock();
    EXPECT_EQ(s.g.waste.size(), 4u);
    s.stock();
    EXPECT_TRUE(s.g.waste.empty());
    EXPECT_EQ(s.g.stock, (std::vector<int>{1, 2, 3, 4}));
    EXPECT_EQ(s.g.score, 15 - 20);
    // Undo goes back one step.
    s.command("undo");
    EXPECT_EQ(s.g.waste.size(), 4u);
    // The clock: -2 every 10 s with standard scoring.
    const int before = s.g.score;
    for (int i = 0; i < 10; ++i)
        s.tick_second();
    EXPECT_EQ(s.g.score, before - 2);
}

// sol.js 305-309, 313-327, 339.
TEST(Win98Solitaire, TargetsAndVictory) {
    const auto home = Solitaire::target(260, 50);
    ASSERT_TRUE(home);
    EXPECT_EQ(home->type, Solitaire::Pile::kHome);
    EXPECT_EQ(home->i, 0);
    const auto tab = Solitaire::target(12 + 82 * 3 + 10, 200);
    ASSERT_TRUE(tab);
    EXPECT_EQ(tab->type, Solitaire::Pile::kTab);
    EXPECT_EQ(tab->i, 3);
    EXPECT_FALSE(Solitaire::target(90, 200));  // between columns
    EXPECT_FALSE(Solitaire::target(10, 118));
    Solitaire s(fixed(0.0));
    s.command("autowin");
    EXPECT_TRUE(s.won);
    EXPECT_EQ(s.sound, "tada");
    EXPECT_TRUE(s.animating);
    int frames = 0;
    while (s.animate() && frames < 10000)
        ++frames;
    EXPECT_FALSE(s.animating);
    EXPECT_LE(s.trails.size(), 300u);
    EXPECT_GT(frames, 52);
}

// herdr src/layout.rs 759-765 (its own test) and src/window/floats.rs 418-421.
TEST(Win98Float, SnapsToTheNearestEdgeAndThird) {
    EXPECT_EQ(snap_float(10, 400, 1200, 800), (FloatPlace{Edge::kLeft, Align::kCenter}));
    EXPECT_EQ(snap_float(1190, 20, 1200, 800), (FloatPlace{Edge::kRight, Align::kStart}));
    EXPECT_EQ(snap_float(600, 5, 1200, 800), (FloatPlace{Edge::kTop, Align::kCenter}));
    EXPECT_EQ(snap_float(1000, 790, 1200, 800), (FloatPlace{Edge::kBottom, Align::kEnd}));
    EXPECT_EQ(snap_float(-50, 900, 1200, 800).align, Align::kEnd);
    // win98.json: Clippy on the left, at the end (the bottom), 12 px from the edges.
    const auto p = float_home(0, 0, 1000, 800, 100, 200, parse_place("left", "end"), 12);
    EXPECT_FLOAT_EQ(p[0], 12);
    EXPECT_FLOAT_EQ(p[1], 800 - 12 - 200);
    const auto c = float_home(0, 0, 1000, 800, 100, 200, parse_place("right", "center"), 12);
    EXPECT_FLOAT_EQ(c[0], 1000 - 12 - 100);
    EXPECT_FLOAT_EQ(c[1], 300);
    EXPECT_STREQ(edge_name(parse_place("bottom", "").edge), "bottom");
    EXPECT_STREQ(align_name(parse_place("x", "start").align), "start");
}
