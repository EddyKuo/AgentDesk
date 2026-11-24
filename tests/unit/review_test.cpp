// The Review layout's rules (libs/ui review.hpp), its layout file, its keys and its commands.
#include "agentdesk/ui/review.hpp"

#include <gtest/gtest.h>

#include "agentdesk/ui/command_names.hpp"
#include "agentdesk/ui/keymap.hpp"
#include "agentdesk/ui/layout.hpp"

namespace agentdesk::ui::review {
namespace {

const std::vector<std::string> kPaths = {"a.txt", "src/b.cpp", "src/c.cpp"};

TEST(Review, NextChangeWalksTheListAndWrapsAtBothEnds) {
    EXPECT_EQ(next_change(kPaths, "a.txt", 1), "src/b.cpp");
    EXPECT_EQ(next_change(kPaths, "src/c.cpp", 1), "a.txt");   // past the last: the first
    EXPECT_EQ(next_change(kPaths, "a.txt", -1), "src/c.cpp");  // before the first: the last
    EXPECT_EQ(next_change(kPaths, "src/b.cpp", -1), "a.txt");
}

TEST(Review, NextChangeWithoutAnOpenFileStartsAtAnEnd) {
    EXPECT_EQ(next_change(kPaths, "", 1), "a.txt");
    EXPECT_EQ(next_change(kPaths, "gone.txt", -1), "src/c.cpp");
    EXPECT_EQ(next_change({}, "a.txt", 1), "");
    EXPECT_EQ(next_change({"only"}, "only", 1), "only");
}

TEST(Review, SplitPathKeepsTheSlashWithTheFolder) {
    EXPECT_EQ(split_path("src/auth/token.ts").dir, "src/auth/");
    EXPECT_EQ(split_path("src/auth/token.ts").name, "token.ts");
    EXPECT_EQ(split_path("notes.md").dir, "");
    EXPECT_EQ(split_path("notes.md").name, "notes.md");
}

TEST(Review, DisplayWidthCountsWideCharactersTwice) {
    EXPECT_EQ(display_width("abc"), 3);
    EXPECT_EQ(display_width("\xE4\xB8\xAD\xE6\x96\x87"), 4);  // 中文
    EXPECT_EQ(display_width("a\xE4\xB8\xAD"
                            "b"),
              4);                                 // a中b
    EXPECT_EQ(display_width("\xEF\xBC\x8C"), 2);  // full-width comma
    EXPECT_EQ(display_width("\xC3\xA9"), 1);      // é
}

TEST(Review, MiddleEllipsisKeepsBothEndsAndFits) {
    EXPECT_EQ(middle_ellipsis("short.txt", 20), "short.txt");
    const std::string cut = middle_ellipsis("src/components/auth/session.ts", 16);
    EXPECT_LE(display_width(cut), 16);
    EXPECT_EQ(cut.substr(0, 4), "src/");
    EXPECT_EQ(cut.substr(cut.size() - 4), "n.ts");
    EXPECT_NE(cut.find("\xE2\x80\xA6"), std::string::npos);
    EXPECT_EQ(middle_ellipsis("abcdef", 2), "\xE2\x80\xA6");  // not even one character a side
}

TEST(Review, MiddleEllipsisNeverSplitsAWideCharacter) {
    // 資料夾/檔案名稱.txt: every kept piece is whole UTF-8 and within the width.
    const std::string path =
        "\xE8\xB3\x87\xE6\x96\x99\xE5\xA4\xBE/\xE6\xAA\x94\xE6\xA1\x88\xE5\x90\x8D\xE7\xA8\xB1.txt";
    for (int cols = 3; cols < display_width(path); ++cols) {
        const std::string cut = middle_ellipsis(path, cols);
        EXPECT_LE(display_width(cut), cols) << cols;
        for (std::size_t i = 0; i < cut.size();) {  // valid UTF-8 lead bytes all along
            const auto c = static_cast<unsigned char>(cut[i]);
            ASSERT_NE(c & 0xC0, 0x80) << cols;
            i += c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
        }
    }
}

TEST(Review, SplitGoesSideBySideFrom760) {
    const Split narrow = review_split(759, 600, 1.0f);
    EXPECT_FALSE(narrow.side_by_side);
    EXPECT_FLOAT_EQ(narrow.list.h, 600 * 0.35f);
    EXPECT_FLOAT_EQ(narrow.diff.y, narrow.list.h + 6);
    const Split wide = review_split(760, 600, 1.0f);
    EXPECT_TRUE(wide.side_by_side);
    EXPECT_FLOAT_EQ(wide.list.w, 260);
    EXPECT_FLOAT_EQ(wide.diff.x, 266);
    EXPECT_FLOAT_EQ(wide.diff.w, 760 - 266);
    const Split big = review_split(1400, 900, 1.0f);
    EXPECT_TRUE(big.side_by_side);
    EXPECT_FLOAT_EQ(big.list.h, 900);
}

TEST(Review, StackedListStaysBetween96And300) {
    EXPECT_FLOAT_EQ(review_split(420, 200, 1.0f).list.h, 96);    // 35 % would be 70
    EXPECT_FLOAT_EQ(review_split(420, 1200, 1.0f).list.h, 300);  // 35 % would be 420
    EXPECT_FLOAT_EQ(review_split(420, 600, 2.0f).list.h, 210);   // real px: 96..300 x 2
    EXPECT_FALSE(review_split(1000, 600, 2.0f).side_by_side);    // 760 design px = 1520 real
}

TEST(Review, ADraggedSplitterWinsWithinBounds) {
    EXPECT_FLOAT_EQ(review_split(420, 600, 1.0f, 150).list.h, 150);
    EXPECT_FLOAT_EQ(review_split(420, 600, 1.0f, 10).list.h, 96);
    EXPECT_FLOAT_EQ(review_split(1000, 600, 1.0f, 400).list.w, 400);
    EXPECT_FLOAT_EQ(review_split(1000, 600, 1.0f, 900).list.w, 500);  // at most half
    EXPECT_FLOAT_EQ(review_split(1000, 600, 1.0f, 50).list.w, 180);
}

TEST(Review, LayoutFileNamesItsBlocks) {
    const auto n = layout::builtin("review");
    for (const char* id : {"tabs", "main", "review-head", "review-changes", "review-commit", "status"})
        EXPECT_NE(n.find(id), nullptr) << id;
    EXPECT_EQ(n.find("review-changes")->mod(), "review-changes");
    EXPECT_EQ(n.find("main")->content.type, layout::Content::Type::kTab);
}

TEST(Review, F8IsBoundOnlyInTheReviewLayout) {
    Keymap k = Keymap::defaults();
    const ImGuiKeyChord f8 = *Keymap::parse("f8");
    const ImGuiKeyChord shift_f8 = *Keymap::parse("shift-f8");
    EXPECT_FALSE(k.global_map().contains(f8));
    EXPECT_FALSE(k.global_map().contains(shift_f8));
    k.set_layout("workspace");
    EXPECT_FALSE(k.global(f8).has_value());  // elsewhere F8 reaches the terminal
    k.set_layout("review");
    EXPECT_EQ(k.global(f8), "review.next_file");
    EXPECT_EQ(k.global(shift_f8), "review.prev_file");
    const auto file = Keymap::default_file();
    EXPECT_EQ(file["layout:review"]["f8"], "review.next_file");
}

TEST(Review, ToggleIsBound) {
    EXPECT_NE(Keymap::defaults().chord_for("review.toggle"), "");  // the key: win/ and posix/ keymap tests
}

TEST(Review, CommandsAreKnown) {
    for (const char* c : {"review.toggle", "review.next_file", "review.prev_file", "review.refresh"})
        EXPECT_TRUE(is_builtin_command(c)) << c;
}

}  // namespace
}  // namespace agentdesk::ui::review
