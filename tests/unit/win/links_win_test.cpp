// Link tests for Windows: a file: URL on another computer names a UNC path.
#include <gtest/gtest.h>

#include "agentdesk/ui/links.hpp"

using namespace agentdesk::ui;

TEST(Links, FileUrlOnAnotherComputerIsAUncPath) {
    EXPECT_EQ(resolve_link("file://server/share/evil.exe", ""),
              (LinkTarget{true, R"(\\server\share\evil.exe)"}));
    EXPECT_EQ(resolve_link("FILE://server/share/a%20b.exe", ""),
              (LinkTarget{true, R"(\\server\share\a b.exe)"}));
}
