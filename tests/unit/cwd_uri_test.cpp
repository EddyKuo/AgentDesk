// OSC 7 file:// URIs to Windows paths, and the URIs that must be refused.
#include "agentdesk/terminal/cwd_uri.hpp"

#include <gtest/gtest.h>

using agentdesk::terminal::windows_path_from_cwd_uri;

namespace {

std::string map(std::string_view uri) {
    return windows_path_from_cwd_uri(uri, "DESKTOP-1");
}

}  // namespace

TEST(CwdUri, WslDistroFoldersMapToWslLocalhost) {
    EXPECT_EQ(map("file://Ubuntu/home/eddy/repo"), R"(\\wsl.localhost\Ubuntu\home\eddy\repo)");
    EXPECT_EQ(map("file://Ubuntu-22.04/"), R"(\\wsl.localhost\Ubuntu-22.04\)");
    EXPECT_EQ(map("file://Ubuntu/home/a%20b"), R"(\\wsl.localhost\Ubuntu\home\a b)");
}

TEST(CwdUri, WindowsDrivesMapToDriveLetters) {
    EXPECT_EQ(map("file://Ubuntu/mnt/d/src/AgentDesk"), R"(D:\src\AgentDesk)");
    EXPECT_EQ(map("file://Ubuntu/mnt/c"), R"(C:\)");
    EXPECT_EQ(map("file://DESKTOP-1/C:/Users/x"), R"(C:\Users\x)");
    EXPECT_EQ(map("file:///d/work"), R"(D:\work)");  // MSYS / Git Bash on this PC
    EXPECT_EQ(map("file://desktop-1/c"), R"(C:\)");
}

TEST(CwdUri, UnmappableUrisGiveEmpty) {
    EXPECT_EQ(map(""), "");
    EXPECT_EQ(map("http://Ubuntu/home"), "");
    EXPECT_EQ(map("file://Ubuntu"), "");
    EXPECT_EQ(map("file://Ubuntu/home/../etc"), "");        // no climbing out
    EXPECT_EQ(map("file://Ubuntu/home/%0a"), "");           // control characters
    EXPECT_EQ(map("file://Ubuntu/home/%zz"), "");           // bad escape
    EXPECT_EQ(map("file://Ubuntu/a\\b"), "");               // backslash in a Linux path
    EXPECT_EQ(map("file://bad host/home"), "");             // not a distribution name
    EXPECT_EQ(map("file://localhost/usr/lib"), "");         // local but not a drive
    EXPECT_EQ(map("file://DESKTOP-1/C:x"), "");
}

TEST(CwdUri, MntWithoutDriveLetterStaysInsideWsl) {
    EXPECT_EQ(map("file://Ubuntu/mnt/wsl/x"), R"(\\wsl.localhost\Ubuntu\mnt\wsl\x)");
}

TEST(CwdUri, PosixPathsFromThisComputer) {
    using agentdesk::terminal::posix_path_from_cwd_uri;
    EXPECT_EQ(posix_path_from_cwd_uri("file://mac/Users/me/src%20x", "mac.local"), "/Users/me/src x");
    EXPECT_EQ(posix_path_from_cwd_uri("file:///tmp/", ""), "/tmp");
    EXPECT_EQ(posix_path_from_cwd_uri("file://localhost/", "mac"), "/");
    EXPECT_EQ(posix_path_from_cwd_uri("file://other/Users/me", "mac"), "");
    EXPECT_EQ(posix_path_from_cwd_uri("file://mac/Users/../etc", "mac"), "");
    EXPECT_EQ(posix_path_from_cwd_uri("http://mac/x", "mac"), "");
}
