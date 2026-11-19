#include "agentdesk/platform/win/command_line.hpp"

// <windows.h> (category 1 in .clang-format) must precede <shellapi.h>.
#include <windows.h>

#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <shellapi.h>

using agentdesk::platform::win::build_command_line;
using agentdesk::platform::win::is_batch_file;
using agentdesk::platform::win::quote_arg;

namespace {

// Round-trips through the real Windows parser.
std::vector<std::wstring> parse(const std::wstring& line) {
    int argc = 0;
    LPWSTR* argv = ::CommandLineToArgvW(line.c_str(), &argc);
    std::vector<std::wstring> out(argv, argv + argc);
    ::LocalFree(argv);
    return out;
}

}  // namespace

class QuoteArgTest : public ::testing::TestWithParam<std::wstring> {};

TEST_P(QuoteArgTest, RoundTripsThroughCommandLineToArgvW) {
    const std::wstring arg = GetParam();
    const auto argv = parse(L"prog.exe " + quote_arg(arg));
    ASSERT_EQ(argv.size(), 2u) << quote_arg(arg);
    EXPECT_EQ(argv[1], arg);
}

INSTANTIATE_TEST_SUITE_P(MsDocsTable, QuoteArgTest,
                         ::testing::Values(L"", L"plain", L"with space", L"a\"b", L"a\\\"b", L"a\\\\\"b",
                                           L"trailing\\", L"trailing space\\", L"a\\\\b", L"\"", L"tab\there",
                                           L"中文 參數", L"C:\\Program Files\\x\\"));

TEST(BuildCommandLine, QuotesExecutableWithSpaces) {
    const auto line = build_command_line(L"C:\\Program Files\\App\\app.exe", {L"--name", L"my review"});
    const auto argv = parse(line);
    ASSERT_EQ(argv.size(), 3u);
    EXPECT_EQ(argv[0], L"C:\\Program Files\\App\\app.exe");
    EXPECT_EQ(argv[2], L"my review");
}

TEST(IsBatchFile, DetectsCmdAndBatCaseInsensitive) {
    EXPECT_TRUE(is_batch_file(L"C:\\x\\codex.CMD"));
    EXPECT_TRUE(is_batch_file(L"run.bat"));
    EXPECT_FALSE(is_batch_file(L"claude.exe"));
    EXPECT_FALSE(is_batch_file(L"noext"));
}

// Windows ignores trailing dots and spaces and accepts the ::$DATA stream name: all of these
// start the batch file, so none may slip past the check.
TEST(IsBatchFile, TrailingDotsSpacesAndDataStreamDoNotHideTheExtension) {
    EXPECT_TRUE(is_batch_file(L"run.bat."));
    EXPECT_TRUE(is_batch_file(L"run.cmd "));
    EXPECT_TRUE(is_batch_file(L"C:\\x\\run.Bat. . ."));
    EXPECT_TRUE(is_batch_file(L"run.bat::$DATA"));
    EXPECT_TRUE(is_batch_file(L"run.cmd::$data."));
    EXPECT_FALSE(is_batch_file(L"run.exe."));
    EXPECT_FALSE(is_batch_file(L"..."));
    EXPECT_FALSE(is_batch_file(L""));
}

TEST(CommandLine, LaunchableFileAcceptsFilesAndAppAliasesOnly) {
    wchar_t sys[MAX_PATH]{};
    ASSERT_GT(::GetSystemDirectoryW(sys, MAX_PATH), 0u);
    const std::wstring dir(sys);
    EXPECT_TRUE(agentdesk::platform::win::is_launchable_file(dir + L"\\cmd.exe"));
    EXPECT_FALSE(agentdesk::platform::win::is_launchable_file(dir));
    EXPECT_FALSE(agentdesk::platform::win::is_launchable_file(dir + L"\\no-such-program.exe"));
    // Store apps (PowerShell 7, winget) install app execution aliases: reparse points that
    // std::filesystem does not call regular files but CreateProcess starts.
    wchar_t local[MAX_PATH]{};
    if (::GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH) == 0)
        GTEST_SKIP() << "no LOCALAPPDATA";
    for (const wchar_t* name : {L"pwsh.exe", L"winget.exe", L"wt.exe"}) {
        const std::wstring alias = std::wstring(local) + L"\\Microsoft\\WindowsApps\\" + name;
        if (::GetFileAttributesW(alias.c_str()) != INVALID_FILE_ATTRIBUTES) {
            EXPECT_TRUE(agentdesk::platform::win::is_launchable_file(alias));
            return;
        }
    }
    GTEST_SKIP() << "no app execution alias on this machine";
}
