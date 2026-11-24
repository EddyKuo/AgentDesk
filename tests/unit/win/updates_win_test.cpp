// Updater tests for Windows: an install is the folder unpacked from the zip, next to its stages.
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
    EXPECT_EQ(up::beside(L"C:\\Apps\\AgentDesk-0.1.0-win-x64", L".new"),
              fs::path(L"C:\\Apps\\AgentDesk-0.1.0-win-x64.new"));
    EXPECT_EQ(up::target(), "windows-x86_64");
    EXPECT_EQ(up::system(L"curl").filename(), fs::path(L"curl.exe"));
}

// Only a folder from the zip (it holds THIRD_PARTY_NOTICES.md) is an install.
TEST(Updates, OnlyTheZipFolderIsAnInstall) {
    const fs::path work = temp_dir("agentdesk-update-root");
    fs::remove_all(work);
    fs::create_directories(work);
    std::ofstream(work / L"agent-ui.exe", std::ios::binary) << "x";
    EXPECT_FALSE(up::find_root(work / L"agent-ui.exe").has_value());
    std::ofstream(work / L"THIRD_PARTY_NOTICES.md", std::ios::binary) << "x";
    const auto r = up::find_root(work / L"agent-ui.exe");
    ASSERT_TRUE(r.has_value());
    EXPECT_TRUE(fs::equivalent(*r, work));
    fs::remove_all(work);
}
