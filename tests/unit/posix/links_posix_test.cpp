// Link tests for macOS (ADR-30): POSIX paths, and ~ from HOME.
#include <cstdlib>

#include <gtest/gtest.h>

#include "agentdesk/ui/links.hpp"

using namespace agentdesk::ui;

TEST(Links, FileUrlOnAnotherComputerDoesNotOpen) {
    // Another host's file is not one this computer can open.
    EXPECT_FALSE(resolve_link("file://server/share/evil.exe", "").has_value());
}

// ~/ and $HOME name the home folder (HOME, not Windows' USERPROFILE).
TEST(Links, TildeIsTheHomeFolder) {
    const char* home = std::getenv("HOME");
    ASSERT_NE(home, nullptr);
    const auto t = resolve_link("~/", "");
    ASSERT_TRUE(t.has_value());
    EXPECT_TRUE(t->file);
    const auto v = resolve_link("$HOME", "");
    ASSERT_TRUE(v.has_value());
}
