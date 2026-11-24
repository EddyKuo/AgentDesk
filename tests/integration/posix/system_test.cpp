// Folders, identifiers, hashes and shells on macOS.
#include "agentdesk/platform/system.hpp"

#include <cstdlib>
#include <fstream>
#include <regex>

#include <gtest/gtest.h>
#include <unistd.h>

namespace ap = agentdesk::platform;

TEST(PosixSystem, DataDirHonorsTheOverride) {
    const auto dir = std::filesystem::temp_directory_path() / ("agentdesk-data-" + ap::new_uuid());
    ::setenv("AGENTDESK_DATA_DIR", dir.c_str(), 1);
    EXPECT_EQ(ap::data_dir(), dir);
    EXPECT_TRUE(std::filesystem::is_directory(dir));
    ::unsetenv("AGENTDESK_DATA_DIR");
    std::filesystem::remove_all(dir);
    EXPECT_EQ(ap::data_dir(), ap::home_dir() / "Library" / "Application Support" / "AgentDesk");
}

TEST(PosixSystem, FoldersAndNames) {
    EXPECT_TRUE(ap::home_dir().is_absolute());
    EXPECT_TRUE(std::filesystem::is_regular_file(ap::executable_path()));
    EXPECT_TRUE(std::filesystem::is_directory(ap::runtime_dir()));
    EXPECT_FALSE(ap::computer_name().empty());
}

TEST(PosixSystem, UuidsAreLowerCaseAndDistinct) {
    const auto a = ap::new_uuid();
    const auto b = ap::new_uuid();
    EXPECT_TRUE(std::regex_match(
        a, std::regex("[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}")))
        << a;
    EXPECT_NE(a, b);
}

TEST(PosixSystem, RandomBytesAreFilled) {
    std::array<std::byte, 64> a{};
    std::array<std::byte, 64> b{};
    ap::random_bytes(a);
    ap::random_bytes(b);
    EXPECT_NE(a, b);
}

TEST(PosixSystem, Sha256OfAFile) {
    const auto file = std::filesystem::temp_directory_path() / ("agentdesk-sha-" + ap::new_uuid());
    std::ofstream(file, std::ios::binary) << "abc";
    EXPECT_EQ(ap::sha256_file_hex(file), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    std::filesystem::remove(file);
    EXPECT_THROW(ap::sha256_file_hex(file), std::system_error);
}

TEST(PosixSystem, ReplaceFileAtomically) {
    const auto dir = std::filesystem::temp_directory_path();
    const auto target = dir / ("agentdesk-target-" + ap::new_uuid());
    const auto temp = dir / ("agentdesk-temp-" + ap::new_uuid());
    std::ofstream(target) << "old";
    std::ofstream(temp) << "new";
    ap::replace_file_atomically(temp, target);
    std::ifstream in(target);
    std::string text;
    in >> text;
    EXPECT_EQ(text, "new");
    EXPECT_FALSE(std::filesystem::exists(temp));
    std::filesystem::remove(target);
}

TEST(PosixSystem, ShellsStartWithTheLoginShell) {
    const auto shells = ap::detect_shells();
    ASSERT_FALSE(shells.empty());
    for (const auto& s : shells) {
        EXPECT_TRUE(s.executable.is_absolute());
        EXPECT_EQ(s.args, std::vector<std::string>{"-l"});
    }
    bool has_zsh = false;
    for (const auto& s : shells)
        has_zsh = has_zsh || s.id == "zsh";
    EXPECT_TRUE(has_zsh);
}
