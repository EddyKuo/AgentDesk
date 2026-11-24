// Tests for the program table: names, icons and tiles for what a terminal runs, the person's
// overrides, and the PowerShell command line round trip.
// Portions adapted from herdr-app (Apache-2.0) and modified; see THIRD_PARTY_NOTICES.md.
#include "agentdesk/ui/programs.hpp"

#include <filesystem>
#include <fstream>

#include <gtest/gtest.h>
#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#define _getpid getpid
#endif

using agentdesk::ui::powershell_line;
using agentdesk::ui::powershell_words;
using agentdesk::ui::ProgramEntry;
using agentdesk::ui::Programs;

namespace {

const std::filesystem::path kTable = std::filesystem::path(AGENTDESK_PROGRAMS_DIR) / "programs.json";

Programs table() {
    return Programs::load(kTable, {});
}

std::string key(std::string_view app, std::vector<std::string> argv) {
    return table().of(app, argv).key;
}

}  // namespace

TEST(Programs, PowershellWordsSurviveQuotes) {
    const std::vector<std::string> words = {"pi", "it's \xE2\x80\x99" "fine\xE2\x80\x99", "", "a  b"};
    const std::string line = powershell_line(words);
    EXPECT_EQ(line, "& 'pi' 'it''s \xE2\x80\x99\xE2\x80\x99" "fine\xE2\x80\x99\xE2\x80\x99' '' 'a  b'");
    EXPECT_EQ(powershell_words(line), words);
}

TEST(Programs, AgentsShellsToolsAndWhatInterpretersRun) {
    EXPECT_EQ(key("claude-code", {"/bin/zsh"}), "claude");
    EXPECT_EQ(key("grok-build", {"/bin/zsh"}), "grok");
    EXPECT_EQ(key("dsh-tui", {"/bin/zsh"}), "dsh-tui");
    EXPECT_EQ(key("", {"dsh-tui"}), "dsh-tui");
    EXPECT_EQ(key("goose", {"/bin/zsh"}), "goose");
    EXPECT_EQ(key("", {"goose"}), "goose");
    EXPECT_EQ(key("", {"agent"}), "cursor");
    EXPECT_EQ(key("agent", {}), "cursor");
    EXPECT_EQ(key("cursor-agent", {}), "cursor");
    EXPECT_EQ(key("newagent", {}), "agent");
    bool cursor_runs_agent = false;
    for (const auto& a : table().startable())
        cursor_runs_agent |= a.key == "cursor" && a.run == std::vector<std::string>{"agent"};
    EXPECT_TRUE(cursor_runs_agent);
    // A person's own key still beats a built-in `match` (python matches python3).
    Programs personal = table();
    ProgramEntry mine;
    mine.name = "Project Python";
    personal.entries()["python3"] = mine;
    EXPECT_EQ(personal.of({}, {"python3"}).key, "python3");
    EXPECT_EQ(personal.of({}, {"python3"}).name, "Project Python");
    EXPECT_EQ(key("", {"/bin/zsh"}), "shell");
    EXPECT_EQ(key("", {"-zsh"}), "shell");
    EXPECT_EQ(key("", {"nvim", "x.rs"}), "nvim");
    EXPECT_EQ(key("", {"vi"}), "vim");
    EXPECT_EQ(key("", {"node", "/x/node_modules/.bin/vite", "dev"}), "vite");
    EXPECT_EQ(key("", {"npx", "vitest"}), "vitest");
    EXPECT_EQ(key("", {"node", "server.js"}), "node");
    EXPECT_EQ(key("", {"python3", "-m", "http.server", "8799"}), "python");
    EXPECT_EQ(key("", {"uv", "run", "pytest"}), "pytest");
    EXPECT_EQ(key("", {"mytool"}), "program");
    EXPECT_EQ(key("", {"/bin/zsh", "-ic", "\"$@\"; exec \"$0\" -i", "/bin/zsh", "vi", "x"}), "vim");
    EXPECT_EQ(key("", {"/usr/bin/fish", "-ic", "command $argv[2..]; exec $argv[1] -i", "/usr/bin/fish", "vi", "x"}),
              "vim");
    EXPECT_EQ(key("", {"/bin/zsh", "-lic", "\"$@\"; exec \"$0\" -li", "/bin/zsh", "cursor-agent"}), "cursor");
    const std::string line = powershell_line({"codex", "fix it"});
    EXPECT_EQ(key("", {"pwsh.exe", "-NoExit", "-Command", line}), "codex");
    EXPECT_EQ(key("", {R"(C:\Program Files\PowerShell\7\pwsh.exe)", "-NoExit", "-Command", line}), "codex");
    EXPECT_EQ(key("", {R"(C:\Program Files\PowerShell\7\pwsh.exe)"}), "shell");
    EXPECT_EQ(key("", {R"(C:\Windows\powershell.exe)"}), "shell");
}

TEST(Programs, EveryEntryHasItsIcon) {
    Programs t = table();
    ASSERT_FALSE(t.entries().empty());
    for (const auto& [k, e] : t.entries()) {
        ASSERT_TRUE(e.icon.has_value()) << k << " has no icon";
        EXPECT_TRUE(std::filesystem::is_regular_file(kTable.parent_path() / *e.icon)) << k << ": " << *e.icon;
    }
}

TEST(Programs, APersonsFileGoesOnTopEntryByEntry) {
    const auto dir = std::filesystem::temp_directory_path() /
                     ("agentdesk-programs-test-" + std::to_string(_getpid()));  // runs side by side
    std::filesystem::create_directories(dir);
    {
        std::ofstream(dir / "programs.json") << R"({"programs": {"claude": {"name": "My Claude"}, "mine": {"icon": "icons/x.svg"}}})";
    }
    std::string error;
    const Programs t = Programs::load(kTable, dir / "programs.json", &error);
    EXPECT_TRUE(error.empty());
    const auto c = t.of({}, {"claude"});
    EXPECT_EQ(c.name, "My Claude");
    EXPECT_EQ(c.icon, "icons/claude.svg");  // kept from the built-in entry
    EXPECT_EQ(t.of({}, {"mine"}).key, "mine");
    {
        std::ofstream(dir / "programs.json") << "{ not json";
    }
    const Programs bad = Programs::load(kTable, dir / "programs.json", &error);
    EXPECT_EQ(error.rfind("programs.json: ", 0), 0u);
    EXPECT_EQ(bad.of({}, {"claude"}).name, table().of({}, {"claude"}).name);
    std::filesystem::remove_all(dir);
}
