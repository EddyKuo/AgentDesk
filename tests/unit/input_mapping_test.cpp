// UI input helpers: key mapping, clipboard chords, paste policy, selection text, dropped paths
// and link detection.
#include "agentdesk/ui/input_mapping.hpp"

#include <filesystem>
#include <fstream>

#include <gtest/gtest.h>

#include "agentdesk/terminal/vt_engine.hpp"
#include "agentdesk/ui/links.hpp"

using namespace agentdesk::ui;
using agentdesk::terminal::Key;
using agentdesk::terminal::VtEngine;
using agentdesk::terminal::VtEngineOptions;

TEST(InputMapping, NamedKeysMapAndPrintableKeysDoNot) {
    EXPECT_EQ(map_named_key(ImGuiKey_Enter), Key::kEnter);
    EXPECT_EQ(map_named_key(ImGuiKey_KeypadEnter), Key::kEnter);
    EXPECT_EQ(map_named_key(ImGuiKey_F5), Key::kF5);
    EXPECT_EQ(map_named_key(ImGuiKey_F12), Key::kF12);
    EXPECT_FALSE(map_named_key(ImGuiKey_A).has_value());  // text comes from the char queue
}

// Design doc 3.5: Ctrl+C copies only with a selection; Ctrl+Shift+C/V are fixed.
TEST(InputMapping, ClipboardChords) {
    const Modifiers ctrl{true, false, false};
    const Modifiers ctrl_shift{true, true, false};
    EXPECT_EQ(classify_clipboard_chord(ImGuiKey_C, ctrl, true), ClipboardAction::kCopy);
    EXPECT_EQ(classify_clipboard_chord(ImGuiKey_C, ctrl, false), ClipboardAction::kSendInterrupt);
    EXPECT_EQ(classify_clipboard_chord(ImGuiKey_C, ctrl_shift, false), ClipboardAction::kCopy);
    EXPECT_EQ(classify_clipboard_chord(ImGuiKey_V, ctrl_shift, false), ClipboardAction::kPaste);
    EXPECT_EQ(classify_clipboard_chord(ImGuiKey_V, ctrl, false), ClipboardAction::kNone);
}

// QA-15 groundwork: IME commits and surrogate pairs arrive as text; controls are dropped so a
// Ctrl chord or Enter is never sent twice.
TEST(InputMapping, PrintableQueueDropsControlsAndCombinesSurrogates) {
    const ImWchar chars[] = {L'a', 0x03, 0x6E2C, 0x0D, 0x09};
    EXPECT_EQ(printable_from_queue(chars, 5), U"a測");
    if constexpr (sizeof(ImWchar) == 2) {
        const ImWchar pair[] = {0xD83D, 0xDE00};
        EXPECT_EQ(printable_from_queue(pair, 2), U"\U0001F600");
    }
}

TEST(InputMapping, GridAndCellMath) {
    EXPECT_EQ(grid_for_area(800.0f, 400.0f, 8.0f, 16.0f), (GridSize{25, 100}));
    EXPECT_EQ(grid_for_area(1.0f, 1.0f, 8.0f, 16.0f), (GridSize{1, 2}));  // clamped minimum
    const GridSize g{25, 100};
    EXPECT_EQ(cell_at(17.0f, 33.0f, 0.0f, 0.0f, 8.0f, 16.0f, g), (CellPos{2, 2}));
    EXPECT_EQ(cell_at(-5.0f, 9999.0f, 0.0f, 0.0f, 8.0f, 16.0f, g), (CellPos{24, 0}));
}

TEST(InputMapping, PastePolicy) {
    EXPECT_EQ(check_paste("one line"), PasteCheck::kOk);
    EXPECT_EQ(check_paste("a\nb"), PasteCheck::kNeedsConfirmation);
    EXPECT_EQ(check_paste(std::string("a\0b", 3)), PasteCheck::kRejectedNul);
    EXPECT_EQ(check_paste(std::string(1024 * 1024 + 1, 'x')), PasteCheck::kRejectedTooLarge);
}

// QA-11: CJK, combining and wide characters copy without control codes and in column order.
TEST(InputMapping, SelectionTextHandlesWideAndCombining) {
    VtEngineOptions o;
    o.rows = 3;
    o.cols = 20;
    VtEngine e(o);
    e.feed("ab中e\xCC\x81x\r\n\x1b[31mred\x1b[0m line");
    const auto snap = e.snapshot();
    EXPECT_EQ(selection_text(snap, {0, 0}, {0, 19}), "ab中e\xCC\x81x");
    EXPECT_EQ(selection_text(snap, {0, 3}, {0, 2}), "中");  // reversed order, wide char kept whole
    // columns: a=0 b=1 中=2..3 é=4 x=5
    EXPECT_EQ(selection_text(snap, {0, 5}, {1, 2}), "x\r\nred");
}

// Copying a selection: soft-wrapped rows join, trailing blank lines go.
TEST(InputMapping, SelectionTextJoinsSoftWrapsAndTrims) {
    VtEngineOptions o;
    o.rows = 5;
    o.cols = 6;
    VtEngine e(o);
    e.feed("hello world\r\nab  \r\n");
    const auto snap = e.snapshot();
    EXPECT_EQ(selection_text(snap, {0, 0}, {4, 5}), "hello world\r\nab");
    // History lines laid out from text select by cells too.
    std::vector<LineCells> lines = {cells_from_text("x中y", true), cells_from_text("z", false)};
    EXPECT_EQ(lines[0].cells.size(), 4u);
    EXPECT_EQ(selection_text(lines, {0, 1}, {1, 0}), "中yz");
}

TEST(InputMapping, SelectionDragScrollsOneToFiveLines) {
    EXPECT_EQ(selection_scroll_lines(10.0f, 20.0f, 10), 0);
    EXPECT_EQ(selection_scroll_lines(-1.0f, 20.0f, 10), -1);
    EXPECT_EQ(selection_scroll_lines(-45.0f, 20.0f, 10), -3);
    EXPECT_EQ(selection_scroll_lines(-500.0f, 20.0f, 10), -5);
    EXPECT_EQ(selection_scroll_lines(200.0f, 20.0f, 10), 1);
    EXPECT_EQ(selection_scroll_lines(260.0f, 20.0f, 10), 4);
}

// Dropped files are pasted as paths: plain paths as they are, the rest in
// single quotes with ' doubled, a trailing space, never Enter.
TEST(InputMapping, QuotePathsForPaste) {
    EXPECT_EQ(*quote_paths({"C:/work/a.txt"}), "C:/work/a.txt ");
    EXPECT_EQ(*quote_paths({"C:\\Users\\me\\O'Brien notes.txt", "D:/x"}),
              "'C:\\Users\\me\\O''Brien notes.txt' D:/x ");
    EXPECT_EQ(*quote_paths({"C:/中文/a.png"}), "'C:/中文/a.png' ");
    EXPECT_FALSE(quote_paths({}).has_value());
    EXPECT_FALSE(quote_paths({"a\nb"}).has_value());
}

// Link detection: URLs without trailing punctuation, rooted and bare
// paths without a trailing colon.
TEST(Links, FindsUrlsAndPaths) {
    auto link = [](const std::string& text, std::size_t at) -> std::string {
        const auto r = find_link(text, at);
        return r ? text.substr(r->first, r->second - r->first) : std::string("<none>");
    };
    EXPECT_EQ(link("see https://example.com/a?b=1, then", 8), "https://example.com/a?b=1");
    EXPECT_EQ(link("see https://example.com/a?b=1, then", 2), "<none>");
    EXPECT_EQ(link("docs (https://en.wikipedia.org/wiki/Foo_(bar)).", 10), "https://en.wikipedia.org/wiki/Foo_(bar)");
    EXPECT_EQ(link("edit ./src/main.cpp:42: done", 8), "./src/main.cpp:42");
    EXPECT_EQ(link("edit src/main.cpp here", 6), "src/main.cpp");
    EXPECT_EQ(link("open ~/notes.md", 7), "~/notes.md");
    EXPECT_EQ(link("at C:\\work\\x.txt.", 5), "C:\\work\\x.txt.");
    EXPECT_EQ(link("plain words only", 3), "<none>");
    EXPECT_EQ(link("a/b without dot", 1), "<none>");
}

TEST(Links, ResolvesUrlsAndExistingFilesOnly) {
    const auto dir = std::filesystem::temp_directory_path() / "agentdesk_links_test";
    std::filesystem::create_directories(dir / "src");
    { std::ofstream(dir / "src" / "main.cpp") << "x"; }
    const std::string cwd = dir.string();
    EXPECT_EQ(resolve_link("https://example.com", cwd), (LinkTarget{false, "https://example.com"}));
    EXPECT_FALSE(resolve_link("ms-msdt:/id", cwd).has_value());  // only Ghostty's link schemes
    const auto f = resolve_link("src/main.cpp:42:7", cwd);
    ASSERT_TRUE(f.has_value());
    EXPECT_TRUE(f->file);
    EXPECT_EQ(std::filesystem::path(f->target), dir / "src" / "main.cpp");
    EXPECT_TRUE(resolve_link("./src/main.cpp.", cwd).has_value());  // the sentence's period
    EXPECT_FALSE(resolve_link("src/missing.cpp", cwd).has_value());
    EXPECT_FALSE(resolve_link("src/main.cpp", "").has_value());  // relative needs the folder
    std::filesystem::remove_all(dir);
}

// Every spelling of a file: URL resolves to the file it names (so open_link shows programs in
// Explorer), and one that names nothing here is refused rather than handed to the shell.
TEST(Links, FileUrlsInEverySpellingResolveToFiles) {
    const auto dir = std::filesystem::temp_directory_path() / "agentdesk_links_file_url";
    std::filesystem::create_directories(dir);
    const auto exe = dir / "evil.exe";
    {
        std::ofstream(exe) << "x";
    }
    std::string url_path = exe.generic_string();  // C:/Users/.../evil.exe
#ifndef _WIN32
    url_path.erase(0, 1);  // macOS: /var/... is spelled after the URL's own slash
#endif
    for (const std::string& url : {"FILE:///" + url_path, "file:/" + url_path, "File://localhost/" + url_path,
                                   "file:///" + url_path}) {
        const auto t = resolve_link(url, "");
        ASSERT_TRUE(t.has_value()) << url;
        EXPECT_TRUE(t->file) << url;
        EXPECT_EQ(std::filesystem::path(t->target), exe) << url;
    }
#ifdef _WIN32
    EXPECT_EQ(resolve_link("file://server/share/evil.exe", ""),
              (LinkTarget{true, R"(\\server\share\evil.exe)"}));
    EXPECT_EQ(resolve_link("FILE://server/share/a%20b.exe", ""),
              (LinkTarget{true, R"(\\server\share\a b.exe)"}));
#else
    // macOS: another host's file is not one this computer can open.
    EXPECT_FALSE(resolve_link("file://server/share/evil.exe", "").has_value());
#endif
    EXPECT_FALSE(resolve_link("FILE:///C:/no/such/evil.exe", "").has_value());
    EXPECT_FALSE(resolve_link("file:C:/x/evil.exe", "").has_value());    // no path at all
    EXPECT_FALSE(resolve_link("file:///C:/x/%zz.exe", "").has_value());  // bad escape
    EXPECT_FALSE(resolve_link("file:///C:/x/a%00.exe", "").has_value());
    std::filesystem::remove_all(dir);
}
