// Ports of herdr-app src/update.rs tests (update.rs 637-903) that need no child process.
#include "agentdesk/ui/updates.hpp"

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <sstream>

#include <gtest/gtest.h>

namespace fs = std::filesystem;
namespace up = agentdesk::ui::updates;

namespace {

fs::path temp_dir(const wchar_t* name) {
    return fs::temp_directory_path() / (std::wstring(name) + L"-" + std::to_wstring(::GetCurrentProcessId()));
}

std::string read(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

}  // namespace

// update.rs 872-880
TEST(Updates, VersionsCompareLikeSemver) {
    EXPECT_TRUE(up::newer("0.2.0", "0.1.9"));
    EXPECT_TRUE(up::newer("0.10.0", "0.9.0"));
    EXPECT_TRUE(up::newer("1.0.0", "1.0.0-rc.1"));
    EXPECT_FALSE(up::newer("1.0.0-rc.1", "1.0.0"));
    EXPECT_FALSE(up::newer("0.1.0", "0.1.0"));
    EXPECT_FALSE(up::newer("0.0.9", "0.1.0"));
}

// SemVer 11.4: prerelease identifiers compare one by one, numeric ones as numbers.
TEST(Updates, PrereleaseIdentifiersCompareLikeSemver) {
    EXPECT_TRUE(up::newer("1.0.0-rc.10", "1.0.0-rc.9"));
    EXPECT_FALSE(up::newer("1.0.0-rc.9", "1.0.0-rc.10"));
    EXPECT_TRUE(up::newer("1.0.0-rc.2", "1.0.0-rc.1"));
    EXPECT_FALSE(up::newer("1.0.0-rc.1", "1.0.0-rc.1"));
    EXPECT_FALSE(up::newer("1.0.0-rc.01", "1.0.0-rc.1"));  // equal numbers
    // The SemVer 11 example order: each is newer than the one before it.
    const char* order[] = {"1.0.0-alpha",  "1.0.0-alpha.1", "1.0.0-alpha.beta", "1.0.0-beta",
                           "1.0.0-beta.2", "1.0.0-beta.11", "1.0.0-rc.1",       "1.0.0"};
    for (std::size_t i = 1; i < std::size(order); ++i) {
        EXPECT_TRUE(up::newer(order[i], order[i - 1])) << order[i] << " > " << order[i - 1];
        EXPECT_FALSE(up::newer(order[i - 1], order[i])) << order[i - 1] << " < " << order[i];
    }
    EXPECT_TRUE(up::newer("1.0.0-rc.100000000000000000000001", "1.0.0-rc.99999999999999999999999"));
    EXPECT_FALSE(up::newer("1.0.0+build.2", "1.0.0+build.1"));  // build metadata does not count
}

// update.rs 899-902
TEST(Updates, Sha256IsHex) {
    EXPECT_EQ(up::sha256("abc"), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

// update.rs 676-714 (the part about the recorded time; the request half is an integration test).
TEST(Updates, FailedCheckClearsSuccessAndRecoveryRecordsANewTime) {
    auto checked = up::checked_at(true, 100);
    EXPECT_EQ(checked, 100u);
    checked = up::checked_at(false, 200);
    EXPECT_FALSE(checked.has_value());
    checked = up::checked_at(true, 300);
    EXPECT_EQ(checked, 300u);
}

// update.rs 716-725
TEST(Updates, StartupAppliesOnlyANewerVersionAndNeverRelaunchesTwice) {
    EXPECT_EQ(up::startup_target("0.1.7", std::string("0.1.8"), std::nullopt),
              std::optional<std::string>("0.1.8"));
    for (const auto& staged : {std::optional<std::string>(), std::optional<std::string>("0.1.6"),
                               std::optional<std::string>("0.1.7")})
        EXPECT_FALSE(up::startup_target("0.1.7", staged, std::nullopt).has_value());
    for (const char* expected : {"0.1.7", "0.1.8", "0.1.9"})
        EXPECT_FALSE(up::startup_target("0.1.7", std::string("0.1.8"), std::string(expected)).has_value());
}

// update.rs 778-790
TEST(Updates, FailedInstallRestoresThePreviousVersion) {
    const fs::path work = temp_dir(L"agentdesk-update-rollback");
    fs::remove_all(work);
    const fs::path root = work / L"AgentDesk-0.1.0-win-x64";
    fs::create_directories(root);
    std::ofstream(root / L"previous", std::ios::binary) << "keep me";
    // The missing stage fails only after the installed folder was moved.
    std::string error;
    EXPECT_FALSE(up::swap(root, &error));
    EXPECT_FALSE(error.empty());
    EXPECT_EQ(read(root / L"previous"), "keep me");
    EXPECT_FALSE(fs::exists(up::beside(root, L".old")));
    fs::remove_all(work);
}

// update.rs 50-59: next to the install, by name.
TEST(Updates, StagesLiveBesideTheInstall) {
    EXPECT_EQ(up::beside(L"C:\\Apps\\AgentDesk-0.1.0-win-x64", L".new"),
              fs::path(L"C:\\Apps\\AgentDesk-0.1.0-win-x64.new"));
    EXPECT_EQ(up::target(), "windows-x86_64");
    EXPECT_EQ(up::system(L"curl").filename(), fs::path(L"curl.exe"));
}

// update.rs 34-45: only a folder from the zip (it holds THIRD_PARTY_NOTICES.md) is an install.
TEST(Updates, OnlyTheZipFolderIsAnInstall) {
    const fs::path work = temp_dir(L"agentdesk-update-root");
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
