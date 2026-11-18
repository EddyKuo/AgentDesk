// agent-host's self-update moves each terminal's whole VtEngine to the new host (design doc 7.4
// OQ-09). Exactness check: an engine restored from save_state() must be indistinguishable from
// the original, now and after both are fed the same further bytes and input.
#include "agentdesk/terminal/vt_engine.hpp"

#include <string>

#include <gtest/gtest.h>

using agentdesk::terminal::Key;
using agentdesk::terminal::kModNone;
using agentdesk::terminal::kModShift;
using agentdesk::terminal::MouseAction;
using agentdesk::terminal::ScreenSnapshot;
using agentdesk::terminal::VtEngine;
using agentdesk::terminal::VtEngineOptions;

namespace {

VtEngineOptions options() {
    VtEngineOptions o;
    o.rows = 12;
    o.cols = 40;
    return o;
}

void expect_same(const VtEngine& a, const VtEngine& b) {
    const ScreenSnapshot sa = a.snapshot();
    const ScreenSnapshot sb = b.snapshot();
    ASSERT_EQ(sa.rows, sb.rows);
    ASSERT_EQ(sa.cols, sb.cols);
    for (int r = 0; r < sa.rows; ++r)
        for (int c = 0; c < sa.cols; ++c)
            ASSERT_EQ(sa.at(r, c), sb.at(r, c)) << "cell " << r << "," << c << " row: " << sa.row_text(r);
    EXPECT_EQ(sa.cursor, sb.cursor);
    EXPECT_EQ(sa.alt_screen, sb.alt_screen);
    EXPECT_EQ(sa.mouse, sb.mouse);
    EXPECT_EQ(sa.title, sb.title);
    EXPECT_EQ(sa.generation, sb.generation);
    EXPECT_EQ(sa.seq, sb.seq);
    EXPECT_EQ(sa.wrapped, sb.wrapped);
    EXPECT_EQ(sa.links, sb.links);
    ASSERT_EQ(a.history().size(), b.history().size());
    for (std::size_t i = 0; i < a.history().size(); ++i) {
        EXPECT_EQ(a.history()[i].line_id, b.history()[i].line_id);
        EXPECT_EQ(a.history()[i].wrapped, b.history()[i].wrapped);
        EXPECT_EQ(a.history()[i].cells, b.history()[i].cells);
    }
    EXPECT_EQ(a.first_history_line_id(), b.first_history_line_id());
    EXPECT_EQ(a.history_bytes(), b.history_bytes());
    EXPECT_EQ(a.history_truncated(), b.history_truncated());
    EXPECT_EQ(a.cwd_uri(), b.cwd_uri());
    EXPECT_EQ(a.win32_input(), b.win32_input());
    EXPECT_EQ(a.status_query_answered(), b.status_query_answered());
    ASSERT_EQ(a.status().records().size(), b.status().records().size());
    const auto* ra = a.status().root();
    const auto* rb = b.status().root();
    ASSERT_EQ(ra == nullptr, rb == nullptr);
    if (ra != nullptr) {
        EXPECT_EQ(ra->report.state, rb->report.state);
        EXPECT_EQ(ra->report.msg, rb->report.msg);
        EXPECT_EQ(ra->updated_order, rb->updated_order);
    }
}

// Everything a program leaves behind that later bytes depend on.
const char* const kSetup =
    "\x1b]0;handover title\x07"
    "\x1b]7;file://box/C:/work\x07"
    "\x1b]7501;state=working:msg=dGhpbmtpbmc\x07"
    "\x1b[38;5;202mindexed \x1b[38;2;10;20;30;48;5;4mrgb\x1b[0m \x1b[1;3;4mbold italic\x1b[0m\r\n"
    "\xe6\xbc\xa2\xe5\xad\x97 wide e\xcc\x81 combined\r\n"
    "\x1b]8;;https://example.com\x07link\x1b]8;;\x07 after\r\n"
    "\x1b[?9001h"                 // win32-input-mode
    "\x1b[?1h\x1b[?2004h"         // cursor keys, bracketed paste
    "\x1b[?1002h\x1b[?1006h"      // drag mouse, SGR encoding
    "\x1b(0lqqk\x1b(B\r\n"        // DEC line drawing, then back
    "\x1b[3g\x1b[1;5H\x1bH\x1b[1;13H\x1bH"  // clear tabs, set two
    "\x1b[1;1H\x1b[5;7r"          // scroll region rows 5..7
    "\x1b[6;3H\x1b[31;42m\x1b" "7" // pen + saved cursor (DECSC)
    "\x1b[0m\x1b[9;1H";

std::string many_lines(int from, int count) {
    std::string out;
    for (int i = from; i < from + count; ++i)
        out += "line " + std::to_string(i) + " \x1b[3" + std::to_string(i % 8) + "mcolour\x1b[0m\r\n";
    return out;
}

// Continues: uses the region, tabs, charset, saved cursor, pen and pending wrap set before.
const char* const kContinue =
    "\x1b[6;1Hregion a\nregion b\nregion c\nregion d\n"  // scrolls inside rows 5..7
    "\x1b" "8tab\there\tand there"                        // DECRC: back to 6;3 with red on green
    "\x1b(0xx\x1b(B"
    "\x1b[r\x1b[12;1H";

}  // namespace

TEST(VtHandover, RestoredEngineIsIdenticalAndContinuesIdentically) {
    VtEngine a(options());
    a.feed(many_lines(1, 30));  // history
    a.feed(kSetup);
    a.feed(std::string(39, 'x') + "y");  // last column: libvterm's pending wrap (phantom)
    ASSERT_TRUE(a.at_handover_point());
    const std::string state = a.save_state();
    ASSERT_FALSE(state.empty());

    auto b = VtEngine::restore(options(), {}, state);
    expect_same(a, *b);

    for (VtEngine* e : {&a, b.get()}) {
        e->feed("z");  // completes the deferred wrap on the next row
        e->feed(kContinue);
        e->feed(many_lines(100, 20));
        e->key(Key::kUp);                       // cursor-key mode: SS3 A
        e->key(Key::kEnter, kModShift);         // win32-input-mode
        e->paste("pasted\ntext");               // bracketed
        e->mouse(2, 3, 1, MouseAction::kPress);  // SGR drag mode
        e->mouse(2, 4, 1, MouseAction::kMove);
        e->feed("\x1b[c");                      // DA1 reply
    }
    expect_same(a, *b);
    EXPECT_EQ(a.take_pty_input(), b->take_pty_input());
}

TEST(VtHandover, AlternateScreenAndPrimaryBehindItSurvive) {
    VtEngine a(options());
    a.feed(many_lines(1, 15));
    a.feed("primary text\x1b[?1049h\x1b[2J\x1b[Halt screen text\x1b[5;5Hhere");
    ASSERT_TRUE(a.snapshot().alt_screen);
    auto b = VtEngine::restore(options(), {}, a.save_state());
    expect_same(a, *b);
    for (VtEngine* e : {&a, b.get()})
        e->feed("\x1b[?1049lback on primary");  // leaving shows the primary screen kept behind
    expect_same(a, *b);
    EXPECT_NE(b->snapshot().row_text(b->snapshot().cursor.row).find("primary textback on primary"),
              std::string::npos);
}

TEST(VtHandover, ResizeAfterRestorePullsTheSameHistory) {
    VtEngine a(options());
    a.feed(many_lines(1, 40));
    auto b = VtEngine::restore(options(), {}, a.save_state());
    for (VtEngine* e : {&a, b.get()})
        e->resize(20, 50);  // taller: lines come back from history
    expect_same(a, *b);
}

TEST(VtHandover, OnlyBetweenSequences) {
    VtEngine e(options());
    e.feed("abc\x1b[3");
    EXPECT_FALSE(e.at_handover_point());
    EXPECT_TRUE(e.save_state().empty());
    e.feed("1m");
    EXPECT_TRUE(e.at_handover_point());
    e.feed("\x1b]0;half a title");
    EXPECT_FALSE(e.at_handover_point());
    e.feed("\x07");
    EXPECT_TRUE(e.at_handover_point());
    // A UTF-8 character split between writes is not a parser state: it travels as the carry.
    e.feed("\xe6\xbc");
    EXPECT_TRUE(e.at_handover_point());
    auto b = VtEngine::restore(options(), {}, e.save_state());
    for (VtEngine* x : {&e, b.get()})
        x->feed("\xa2!");
    expect_same(e, *b);
    EXPECT_NE(b->snapshot().row_text(0).find("\xe6\xbc\xa2!"), std::string::npos);
}

TEST(VtHandover, DamagedStateIsRefused) {
    VtEngine e(options());
    e.feed("hello");
    std::string state = e.save_state();
    EXPECT_THROW(VtEngine::restore(options(), {}, state.substr(0, state.size() - 1)), std::runtime_error);
    std::string flipped = state;
    flipped[0] = 'X';
    EXPECT_THROW(VtEngine::restore(options(), {}, flipped), std::runtime_error);
    EXPECT_THROW(VtEngine::restore(options(), {}, ""), std::runtime_error);
    EXPECT_NO_THROW(VtEngine::restore(options(), {}, state));
}
