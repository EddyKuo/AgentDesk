#include "agentdesk/ui/input_mapping.hpp"

#include <gtest/gtest.h>

#include "agentdesk/terminal/vt_engine.hpp"

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
