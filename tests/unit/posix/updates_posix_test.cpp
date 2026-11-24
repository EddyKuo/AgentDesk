// Updater tests for macOS (ADR-30): an install is the AgentDesk.app bundle.
#include <filesystem>
#include <fstream>

#include <gtest/gtest.h>

#include "../test_util.hpp"
#include "agentdesk/ui/updates.hpp"

namespace fs = std::filesystem;
namespace up = agentdesk::ui::updates;

namespace {

fs::path temp_dir(const char* name) {
    return fs::temp_directory_path() / (std::string(name) + "-" + agentdesk::test::unique_suffix());
}

}  // namespace

// Next to the install, by name.
TEST(Updates, StagesLiveBesideTheInstall) {
    EXPECT_EQ(up::beside("/Applications/AgentDesk.app", L".new"),
              fs::path("/Applications/AgentDesk.app.new"));
    EXPECT_EQ(up::target(), "darwin-aarch64");
    EXPECT_EQ(up::system(L"curl"), fs::path("/usr/bin/curl"));
}

// macOS: only an app bundle with its notices in Contents/Resources is an install.
TEST(Updates, OnlyTheAppBundleIsAnInstall) {
    const fs::path work = temp_dir("agentdesk-update-root");
    fs::remove_all(work);
    const fs::path app = work / "AgentDesk.app";
    fs::create_directories(app / "Contents" / "MacOS");
    fs::create_directories(app / "Contents" / "Resources");
    const fs::path exe = app / "Contents" / "MacOS" / "agent-ui";
    std::ofstream(exe, std::ios::binary) << "x";
    EXPECT_FALSE(up::find_root(exe).has_value());
    std::ofstream(app / "Contents" / "Resources" / "THIRD_PARTY_NOTICES.md", std::ios::binary) << "x";
    const auto r = up::find_root(exe);
    ASSERT_TRUE(r.has_value());
    EXPECT_TRUE(fs::equivalent(*r, app));
    fs::remove_all(work);
}
