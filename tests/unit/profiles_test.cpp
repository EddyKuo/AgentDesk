// Profile detection on every host start: new programs are added once, removed ones stay removed,
// and missing programs are hidden without leaving profiles.json.
#include <algorithm>
#include <set>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "agentdesk/host/store.hpp"

using agentdesk::host::available_profiles;
using agentdesk::host::Profile;
using agentdesk::host::ProfileFile;
using agentdesk::host::sync_detected_profiles;

namespace {

Profile shell(const std::string& id) {
    return {id, id, std::filesystem::path("/bin") / id, {"-l"}};
}
Profile program(const std::string& id, const std::string& exe) {
    return {id, id, std::filesystem::path(exe), {}};
}

std::vector<std::string> ids(const std::vector<Profile>& ps) {
    std::vector<std::string> out;
    for (const auto& p : ps)
        out.push_back(p.id);
    return out;
}

}  // namespace

TEST(Profiles, ANewFileTakesEverythingFound) {
    ProfileFile f;
    f.detected.emplace();
    const std::vector<Profile> found{program("claude-native", "/usr/local/bin/claude"), shell("zsh"), shell("bash")};
    EXPECT_TRUE(sync_detected_profiles(f, found));
    EXPECT_EQ(ids(f.profiles), (std::vector<std::string>{"claude-native", "zsh", "bash"}));
    EXPECT_EQ(*f.detected, (std::set<std::string>{"claude-native", "zsh", "bash"}));
    EXPECT_FALSE(sync_detected_profiles(f, found)) << "nothing new the second time";
}

TEST(Profiles, ProgramsInstalledLaterAreAddedOnce) {
    ProfileFile f;
    f.detected = std::set<std::string>{"claude-native", "zsh"};
    f.profiles = {program("claude-native", "/usr/local/bin/claude"), shell("zsh")};
    const std::vector<Profile> found{program("claude-native", "/usr/local/bin/claude"),
                                     program("codex-native", "/opt/homebrew/bin/codex"), shell("zsh")};
    EXPECT_TRUE(sync_detected_profiles(f, found));
    EXPECT_EQ(ids(f.profiles), (std::vector<std::string>{"claude-native", "zsh", "codex-native"}));
    // The person removes Codex: it is not added back.
    f.profiles.pop_back();
    EXPECT_FALSE(sync_detected_profiles(f, found));
    EXPECT_EQ(ids(f.profiles), (std::vector<std::string>{"claude-native", "zsh"}));
}

TEST(Profiles, AnOlderFileIsBroughtUpToDateOnce) {
    // macOS, first written by a version that offered every shell in /etc/shells.
    ProfileFile f;
    f.wsl_detected = true;
    f.profiles = {program("claude-native", "/usr/local/bin/claude"), shell("zsh"), shell("bash"), shell("csh"),
                  shell("dash"), shell("ksh"), shell("sh"), shell("tcsh")};
    f.profiles.push_back({"ksh-work", "ksh for work", "/bin/ksh", {"-l"}});  // made by hand: kept
    Profile own_sh = shell("sh");
    own_sh.args = {};  // changed by the person: kept
    f.profiles[6] = own_sh;
    const std::vector<Profile> found{program("claude-native", "/usr/local/bin/claude"),
                                     program("codex-native", "/opt/homebrew/bin/codex"), shell("zsh"),
                                     shell("bash")};
    EXPECT_TRUE(sync_detected_profiles(f, found));
    EXPECT_EQ(ids(f.profiles),
              (std::vector<std::string>{"claude-native", "zsh", "bash", "sh", "ksh-work", "codex-native"}));
    ASSERT_TRUE(f.detected);
    EXPECT_TRUE(f.detected->contains("codex-native"));
    EXPECT_FALSE(sync_detected_profiles(f, found));
}

TEST(Profiles, RemovedWslDistributionsStayRemoved) {
    const std::vector<Profile> found{program("pwsh", "C:/pwsh.exe"), program("wsl-Ubuntu", "C:/wsl.exe")};
    ProfileFile added_before;  // wsl_detected: the person removed Ubuntu
    added_before.wsl_detected = true;
    added_before.profiles = {program("pwsh", "C:/pwsh.exe")};
    sync_detected_profiles(added_before, found);
    EXPECT_EQ(ids(added_before.profiles), (std::vector<std::string>{"pwsh"}));
    ProfileFile never_added;  // written before WSL profiles existed
    never_added.profiles = {program("pwsh", "C:/pwsh.exe")};
    sync_detected_profiles(never_added, found);
    EXPECT_EQ(ids(never_added.profiles), (std::vector<std::string>{"pwsh", "wsl-Ubuntu"}));
}

TEST(Profiles, MissingProgramsAreHiddenNotForgotten) {
    const std::vector<Profile> ps{program("claude-native", "/usr/local/bin/claude"),
                                  program("codex-native", "/gone/codex"), shell("zsh")};
    const auto shown = available_profiles(ps, [](const Profile& p) { return p.id != "codex-native"; });
    EXPECT_EQ(ids(shown), (std::vector<std::string>{"claude-native", "zsh"}));
    EXPECT_EQ(ps.size(), 3u);
}
