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
