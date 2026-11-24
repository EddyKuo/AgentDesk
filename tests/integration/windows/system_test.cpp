// Folders, identifiers, hashes, file replacement, shells and libraries on Windows.
#include "agentdesk/platform/system.hpp"

#include <array>
#include <fstream>
#include <regex>
#include <sstream>

#include <windows.h>

#include <gtest/gtest.h>

#include "agentdesk/platform/dynamic_library.hpp"
#include "agentdesk/platform/process.hpp"
#include "agentdesk/platform/text.hpp"

namespace ap = agentdesk::platform;
namespace fs = std::filesystem;

namespace {

std::string text_of(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

fs::path temp_file(const char* tag) {
    return fs::temp_directory_path() / (std::string("agentdesk-") + tag + "-" + ap::new_uuid());
}

}  // namespace

TEST(System, EnvironmentVariables) {
    EXPECT_FALSE(ap::env_var("AGENTDESK_SYSTEM_TEST").has_value());
    const std::wstring longer(1000, L'\x4E2D');
    ::SetEnvironmentVariableW(L"AGENTDESK_SYSTEM_TEST", longer.c_str());
    ASSERT_TRUE(ap::env_var("AGENTDESK_SYSTEM_TEST").has_value());
    EXPECT_EQ(ap::env_var("AGENTDESK_SYSTEM_TEST")->size(), 3000u) << "long and not ASCII";
    ap::unset_env_var("AGENTDESK_SYSTEM_TEST");
    EXPECT_FALSE(ap::env_var("AGENTDESK_SYSTEM_TEST").has_value());
}

TEST(System, FoldersAndNames) {
    EXPECT_TRUE(ap::home_dir().is_absolute());
    EXPECT_TRUE(fs::is_regular_file(ap::executable_path()));
    EXPECT_TRUE(fs::is_directory(ap::runtime_dir()));
    EXPECT_FALSE(ap::computer_name().empty());
    EXPECT_EQ(ap::program_file("agent-host"), fs::path(L"agent-host.exe"));
    EXPECT_EQ(ap::current_pid(), ::GetCurrentProcessId());
}

TEST(System, DataDirHonorsTheOverride) {
    const auto before = ap::env_var("AGENTDESK_DATA_DIR");
    const auto dir = temp_file("data");
    ::SetEnvironmentVariableW(L"AGENTDESK_DATA_DIR", dir.c_str());
    EXPECT_EQ(ap::data_dir(), dir);
    EXPECT_TRUE(fs::is_directory(dir));
    ap::unset_env_var("AGENTDESK_DATA_DIR");
    EXPECT_EQ(ap::data_dir().filename(), fs::path(L"AgentDesk"));
    if (before)
        ::SetEnvironmentVariableW(L"AGENTDESK_DATA_DIR", ap::to_wide(*before).c_str());
    fs::remove_all(dir);
}

TEST(System, UuidsAndRandomBytes) {
    const auto a = ap::new_uuid();
    EXPECT_TRUE(std::regex_match(a, std::regex("[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}")))
        << a;
    EXPECT_NE(a, ap::new_uuid());
    std::array<std::byte, 64> x{};
    std::array<std::byte, 64> y{};
    ap::random_bytes(x);
    ap::random_bytes(y);
    EXPECT_NE(x, y);
}

TEST(System, Sha256) {
    const std::string abc = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
    EXPECT_EQ(ap::sha256_hex("abc"), abc);
    EXPECT_EQ(ap::sha256_hex(""), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    const auto file = temp_file("sha");
    std::ofstream(file, std::ios::binary) << "abc";
    EXPECT_EQ(ap::sha256_file_hex(file), abc);
    fs::remove(file);
    EXPECT_THROW(ap::sha256_file_hex(file), std::system_error);
}

TEST(System, ReplaceFileKeepsABackupOnlyWhenAsked) {
    const auto target = temp_file("target");
    const auto temp = temp_file("temp");
    auto backup = target;
    backup += L".bak";
    std::ofstream(temp) << "one";
    ap::replace_file_atomically(temp, target, backup);  // no target yet: nothing to keep
    EXPECT_EQ(text_of(target), "one");
    EXPECT_FALSE(fs::exists(backup));
    std::ofstream(temp) << "two";
    ap::replace_file_atomically(temp, target, backup);
    EXPECT_EQ(text_of(target), "two");
    EXPECT_EQ(text_of(backup), "one");
    std::ofstream(temp) << "three";
    ap::replace_file_atomically(temp, target);
    EXPECT_EQ(text_of(target), "three");
    EXPECT_EQ(text_of(backup), "one");
    EXPECT_FALSE(fs::exists(temp));
    EXPECT_THROW(ap::replace_file_atomically(temp, target), std::system_error);
    EXPECT_EQ(text_of(target), "three") << "a failed replacement changes nothing";
    fs::remove(target);
    fs::remove(backup);
}

TEST(System, ShellsAreOfferedInOrder) {
    const auto shells = ap::detect_shells();
    ASSERT_FALSE(shells.empty());
    bool cmd = false;
    for (const auto& s : shells) {
        EXPECT_TRUE(ap::is_launchable_file(s.executable)) << s.id;
        EXPECT_FALSE(cmd && !s.id.starts_with("wsl-")) << "only WSL distributions come after cmd";
        cmd = cmd || s.id == "cmd";
    }
    EXPECT_TRUE(cmd);
}

TEST(System, LibrariesLoadAndSayWhyNot) {
    EXPECT_EQ(ap::library_file("mod"), "mod.dll");
    wchar_t sys[MAX_PATH]{};
    ::GetSystemDirectoryW(sys, MAX_PATH);
    std::string error;
    const auto lib = ap::DynamicLibrary::load(fs::path(sys) / L"version.dll", &error);
    ASSERT_NE(lib, nullptr) << error;
    EXPECT_NE(lib->symbol("GetFileVersionInfoW"), nullptr);
    EXPECT_EQ(lib->symbol("AgentDeskNoSuchFunction"), nullptr);
    EXPECT_EQ(ap::DynamicLibrary::load(fs::path(sys) / L"agentdesk-none.dll", &error), nullptr);
    EXPECT_NE(error.find("agentdesk-none.dll"), std::string::npos);
}
