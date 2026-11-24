// Paths as text: the same answers on every system, because the path may be another machine's.
#include "agentdesk/ui/path_text.hpp"

#include <filesystem>

#include <gtest/gtest.h>

namespace pt = agentdesk::ui::path_text;

TEST(PathText, ThePathSaysItsSystem) {
    EXPECT_TRUE(pt::is_windows("C:/src"));
    EXPECT_TRUE(pt::is_windows("d:\\src"));
    EXPECT_TRUE(pt::is_windows("//server/share/x"));
    EXPECT_TRUE(pt::is_windows("\\\\wsl.localhost\\Ubuntu\\home"));
    EXPECT_FALSE(pt::is_windows("/Users/me"));
    EXPECT_FALSE(pt::is_windows("///Users/me"));
    EXPECT_FALSE(pt::is_windows("src/a"));
    EXPECT_FALSE(pt::is_windows(""));
}

TEST(PathText, AbsolutePathsOfEitherSystem) {
    EXPECT_TRUE(pt::is_absolute("C:/src"));
    EXPECT_TRUE(pt::is_absolute("/Users/me"));
    EXPECT_TRUE(pt::is_absolute("//server/share"));
    EXPECT_FALSE(pt::is_absolute("C:src"));
    EXPECT_FALSE(pt::is_absolute("../.git"));
    EXPECT_FALSE(pt::is_absolute(""));
}

// What git prints (forward slashes on every system) becomes the path's own style.
TEST(PathText, NativeKeepsPosixPathsAndTurnsWindowsOnes) {
    EXPECT_EQ(pt::native("C:/src/repo/"), "C:\\src\\repo");
    EXPECT_EQ(pt::native("C:/"), "C:\\");
    EXPECT_EQ(pt::native("//server/share/repo"), "\\\\server\\share\\repo");
    EXPECT_EQ(pt::native("/Users/me/repo"), "/Users/me/repo");
    EXPECT_EQ(pt::native("/Users/me/repo/"), "/Users/me/repo");
    EXPECT_EQ(pt::native("/Users/me/a\\b"), "/Users/me/a\\b");
    EXPECT_EQ(pt::native("/"), "/");
    EXPECT_EQ(pt::native(""), "");
}

TEST(PathText, JoinFollowsTheRoot) {
    EXPECT_EQ(pt::join("C:\\repo", "src/a.cpp"), "C:\\repo\\src\\a.cpp");
    EXPECT_EQ(pt::join("C:/repo/", "a"), "C:/repo/a");
    EXPECT_EQ(pt::join("C:\\", "a"), "C:\\a");
    EXPECT_EQ(pt::join("/Users/me/repo", "src/a.cpp"), "/Users/me/repo/src/a.cpp");
    EXPECT_EQ(pt::join("/", "a"), "/a");
    EXPECT_EQ(pt::join("/r", "odd\\name"), "/r/odd\\name");
    EXPECT_EQ(pt::join("/r", ""), "/r");
}

// `git rev-parse --git-common-dir` may answer relative to the folder it ran in.
TEST(PathText, ResolveTakesAwayDots) {
    EXPECT_EQ(pt::resolve("/Users/me/repo", ".git"), "/Users/me/repo/.git");
    EXPECT_EQ(pt::resolve("/Users/me/repo/src/x", "../../.git"), "/Users/me/repo/.git");
    EXPECT_EQ(pt::resolve("/Users/me/repo", "/srv/main/.git"), "/srv/main/.git");
    EXPECT_EQ(pt::resolve("C:\\repo\\src", "../.git"), "C:\\repo\\.git");
    EXPECT_EQ(pt::resolve("C:\\repo", "C:/main/.git"), "C:\\main\\.git");
    EXPECT_EQ(pt::resolve("//server/share/repo", "../../../.git"), "\\\\server\\share\\.git");
    EXPECT_EQ(pt::resolve("/a", "../../.."), "/");
}

TEST(PathText, ParentStopsAtTheRoot) {
    EXPECT_EQ(pt::parent("/Users/me/.git"), "/Users/me");
    EXPECT_EQ(pt::parent("/a"), "/");
    EXPECT_EQ(pt::parent("/"), "/");
    EXPECT_EQ(pt::parent("C:\\repo\\.git"), "C:\\repo");
    EXPECT_EQ(pt::parent("C:\\a"), "C:\\");
    EXPECT_EQ(pt::parent("//server/share/.git"), "\\\\server\\share");
}

// A folder of this computer, written as git prints it, is still that folder in native form.
TEST(PathText, ALocalFolderIsStillThatFolder) {
    const auto u8 = std::filesystem::temp_directory_path().generic_u8string();
    const std::string text = pt::native(std::string(u8.begin(), u8.end()));
    EXPECT_TRUE(std::filesystem::is_directory(std::filesystem::path(std::u8string(text.begin(), text.end()))))
        << text;
    EXPECT_EQ(pt::separator(text), pt::local_separator());
}
