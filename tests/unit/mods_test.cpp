#include "agentdesk/ui/mods.hpp"

#include <windows.h>

#include <filesystem>
#include <fstream>

#include <gtest/gtest.h>

namespace fs = std::filesystem;
using agentdesk::ui::ModHost;
using agentdesk::ui::ModSession;
using agentdesk::ui::ModWidget;

// Mods are C++ DLLs (ADR-17). AGENTDESK_TEST_MOD is tests/fixtures/mods/test_mod.cpp built as
// mod.dll; each test copies it into a fresh mods folder.
namespace {

class ModsTest : public ::testing::Test {
protected:
    void SetUp() override {
        dir_ = fs::temp_directory_path() / (L"agentdesk-mods-" + std::to_wstring(::GetCurrentProcessId()) +
                                            L"-" + std::to_wstring(::GetTickCount64()));
        fs::create_directories(dir_);
    }
    void TearDown() override {
        std::error_code ec;
        fs::remove_all(dir_, ec);  // DLLs still loaded by a ModHost are left behind; harmless
    }
    void install(const std::string& mod, const std::string& settings = {}, const std::string& mod_json = {}) {
        fs::create_directories(dir_ / mod);
        fs::copy_file(AGENTDESK_TEST_MOD, dir_ / mod / "mod.dll", fs::copy_options::overwrite_existing);
        if (!settings.empty())
            std::ofstream(dir_ / mod / "settings.json", std::ios::binary) << settings;
        if (!mod_json.empty())
            std::ofstream(dir_ / mod / "mod.json", std::ios::binary) << mod_json;
    }
    fs::path dir_;
};

}  // namespace

TEST_F(ModsTest, OnlyEnabledModsLoadAndDescriptionsAreRead) {
    install("a", R"({"greeting": "hello"})");
    install("b", {}, R"({"description": "From mod.json"})");
    ModHost h(dir_);
    h.load({"a"});
    const auto mods = h.mods();
    ASSERT_EQ(mods.size(), 2u);
    EXPECT_TRUE(mods[0].loaded) << mods[0].error;
    EXPECT_EQ(mods[0].description, "Test mod");  // from the DLL
    EXPECT_FALSE(mods[1].loaded);
    EXPECT_EQ(mods[1].description, "From mod.json");  // known without loading the DLL
    ASSERT_EQ(h.commands().size(), 4u);
    h.run_command("a", "hi");
    const auto out = h.take_output();
    ASSERT_EQ(out.notes.size(), 1u);
    EXPECT_EQ(out.notes[0].title, "hi");
    EXPECT_EQ(out.notes[0].body, "hello");  // settings.json
}

TEST_F(ModsTest, EventsSessionsAndEffects) {
    install("e");
    ModHost h(dir_);
    h.load({"e"});
    ModSession s;
    s.id = "s1";
    s.title = "Claude";
    s.focused = true;
    h.set_sessions({s});
    h.emit("agent_state", {{"session", "s1"}, {"state", "Blocked"}, {"previous", "Working"}, {"title", "Claude"}});
    h.run_command("e", "act");
    const auto out = h.take_output();
    ASSERT_EQ(out.notes.size(), 1u);
    EXPECT_EQ(out.notes[0].body, "s1=Blocked");
    ASSERT_EQ(out.commands.size(), 1u);
    EXPECT_EQ(out.commands[0], "tab.new");
    ASSERT_EQ(out.sends.size(), 1u);
    EXPECT_EQ(out.sends[0].session, "s1");
    EXPECT_TRUE(out.sends[0].enter);
    ASSERT_EQ(out.execs.size(), 1u);
    EXPECT_EQ(out.execs[0].front(), L"curl");
}

TEST_F(ModsTest, PanelsAndButtonClicks) {
    install("p");
    ModHost h(dir_);
    h.load({"p"});
    ModSession s;
    s.id = "s1";
    h.set_sessions({s, s});
    auto panels = h.panels("sidebar");
    ASSERT_EQ(panels.size(), 1u);
    EXPECT_EQ(panels[0].mod, "p");
    EXPECT_EQ(panels[0].title, "Test panel");
    ASSERT_EQ(panels[0].widgets.size(), 6u);
    EXPECT_EQ(panels[0].widgets[0].text, "sessions 2");
    EXPECT_EQ(panels[0].widgets[1].kind, ModWidget::Kind::kRow);
    EXPECT_EQ(panels[0].widgets[1].text2, "right");
    EXPECT_FLOAT_EQ(panels[0].widgets[2].value, 0.5f);
    EXPECT_EQ(panels[0].widgets[3].kind, ModWidget::Kind::kButton);
    EXPECT_EQ(panels[0].widgets[3].id, "b1");
    h.click("p", "p1", "b1");
    panels = h.panels("sidebar");
    EXPECT_EQ(panels[0].widgets[5].text, "clicks 1");
    EXPECT_TRUE(h.panels("details").empty());
}

TEST_F(ModsTest, DecorationsJoinText) {
    install("d1");
    install("d2");
    ModHost h(dir_);
    h.load({"d1", "d2"});
    EXPECT_EQ(h.decorate("tab", {{"title", "x"}, {"sessions", {"a"}}}), "T:x T:x");
    EXPECT_EQ(h.decorate("agent", {{"state", "Working"}}), "");
}

TEST_F(ModsTest, CrashesAndExceptionsStopTheModNotTheApp) {
    install("c");
    install("t");
    ModHost h(dir_);
    h.load({"c", "t"});
    h.run_command("t", "throw");  // caught inside the SDK, reported, mod keeps running
    auto out = h.take_output();
    ASSERT_EQ(out.notes.size(), 1u);
    EXPECT_EQ(out.notes[0].title, "Mod error");
    EXPECT_TRUE(h.mods()[1].loaded);

    h.run_command("c", "crash");  // access violation inside the DLL
    const auto mods = h.mods();
    EXPECT_FALSE(mods[0].loaded);
    EXPECT_NE(mods[0].error.find("crashed"), std::string::npos) << mods[0].error;
    EXPECT_EQ(h.commands().size(), 4u);  // only the healthy mod's commands are left
    h.emit("tick", nlohmann::json::object());  // the crashed mod is not called again
}

TEST_F(ModsTest, OldLuaModsAndBadDllsReportErrors) {
    fs::create_directories(dir_ / "old");
    std::ofstream(dir_ / "old" / "main.lua", std::ios::binary) << "-- old\n";
    fs::create_directories(dir_ / "junk");
    std::ofstream(dir_ / "junk" / "mod.dll", std::ios::binary) << "not a dll";
    ModHost h(dir_);
    h.load({"old", "junk"});
    const auto mods = h.mods();
    ASSERT_EQ(mods.size(), 2u);
    EXPECT_FALSE(mods[0].loaded);
    EXPECT_NE(mods[0].error.find("cannot load"), std::string::npos) << mods[0].error;  // junk
    EXPECT_FALSE(mods[1].loaded);
    EXPECT_NE(mods[1].error.find("Lua"), std::string::npos);  // old
}
