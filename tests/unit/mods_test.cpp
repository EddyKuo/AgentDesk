#include "agentdesk/ui/mods.hpp"

#include <windows.h>

#include <filesystem>
#include <fstream>

#include <gtest/gtest.h>

namespace fs = std::filesystem;
using agentdesk::ui::ModHost;
using agentdesk::ui::ModSession;
using agentdesk::ui::ModWidget;

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
        fs::remove_all(dir_, ec);
    }
    void write(const std::string& mod, const std::string& lua, const std::string& settings = {}) {
        fs::create_directories(dir_ / mod);
        std::ofstream(dir_ / mod / "main.lua", std::ios::binary) << lua;
        if (!settings.empty())
            std::ofstream(dir_ / mod / "settings.json", std::ios::binary) << settings;
    }
    fs::path dir_;
};

}  // namespace

TEST_F(ModsTest, OnlyEnabledModsRunAndDescriptionsAreRead) {
    write("a", "-- Says hello\nagentdesk.command('hi', 'Say hi', function() agentdesk.notify('hi') end)\n");
    write("b", "-- Other\nagentdesk.command('x', 'X', function() end)\n");
    ModHost h(dir_);
    h.load({"a"});
    const auto mods = h.mods();
    ASSERT_EQ(mods.size(), 2u);
    EXPECT_EQ(mods[0].description, "Says hello");
    EXPECT_TRUE(mods[0].loaded);
    EXPECT_FALSE(mods[1].loaded);
    ASSERT_EQ(h.commands().size(), 1u);
    h.run_command("a", "hi");
    const auto out = h.take_output();
    ASSERT_EQ(out.notes.size(), 1u);
    EXPECT_EQ(out.notes[0].title, "hi");
}

TEST_F(ModsTest, SandboxHasNoFilesOrProcesses) {
    write("s", R"(
assert(io == nil, "io")
assert(os.execute == nil, "os.execute")
assert(loadfile == nil and dofile == nil and load == nil and require == nil, "loading")
assert(type(os.time()) == "number")
)");
    ModHost h(dir_);
    h.load({"s"});
    EXPECT_TRUE(h.mods()[0].loaded) << h.mods()[0].error;
}

TEST_F(ModsTest, EventsSessionsAndEffects) {
    write("e", R"(
agentdesk.on("agent_state", function(ev)
  if ev.state == "Blocked" then
    agentdesk.notify(ev.title .. " needs you", ev.session)
    agentdesk.exec({"curl", "-d", ev.title, "https://ntfy.sh/" .. agentdesk.setting("topic", "none")})
  end
  agentdesk.run("agent.focus:" .. agentdesk.focused())
  agentdesk.send(ev.session, "ok", true)
end)
)",
          R"({"topic": "abc"})");
    ModHost h(dir_);
    h.load({"e"});
    ModSession s;
    s.id = "s1";
    s.title = "Claude";
    s.focused = true;
    h.set_sessions({s});
    h.emit("agent_state",
           {{"session", "s1"}, {"state", "Blocked"}, {"previous", "Working"}, {"title", "Claude"}});
    const auto out = h.take_output();
    ASSERT_EQ(out.notes.size(), 1u);
    EXPECT_EQ(out.notes[0].title, "Claude needs you");
    ASSERT_EQ(out.execs.size(), 1u);
    EXPECT_EQ(out.execs[0].back(), L"https://ntfy.sh/abc");
    ASSERT_EQ(out.commands.size(), 1u);
    EXPECT_EQ(out.commands[0], "agent.focus:s1");
    ASSERT_EQ(out.sends.size(), 1u);
    EXPECT_TRUE(out.sends[0].enter);
}

TEST_F(ModsTest, PanelsAndButtonClicks) {
    write("p", R"(
local n = 0
agentdesk.panel{ id = "count", title = "Counter", where = "sidebar", draw = function(ui)
  ui:text("count " .. n, "dim")
  if ui:button("Add") then n = n + 1 end
  ui:progress(0.5, "half")
  ui:row("left", "right")
end }
)");
    ModHost h(dir_);
    h.load({"p"});
    auto panels = h.panels("sidebar");
    ASSERT_EQ(panels.size(), 1u);
    ASSERT_EQ(panels[0].widgets.size(), 4u);
    EXPECT_EQ(panels[0].widgets[0].text, "count 0");
    EXPECT_EQ(panels[0].widgets[1].kind, ModWidget::Kind::kButton);
    h.click("p", "count", "Add");
    (void)h.panels("sidebar");  // the click is seen during this draw
    panels = h.panels("sidebar");
    EXPECT_EQ(panels[0].widgets[0].text, "count 1");
    EXPECT_TRUE(h.panels("details").empty());
}

TEST_F(ModsTest, DecorationsJoinText) {
    write("d1", "agentdesk.decorate('tab', function(t) return #t.sessions .. ' panes' end)");
    write("d2",
          "agentdesk.decorate('tab', function(t) return '' end)\n"
          "agentdesk.decorate('agent', function(s) return s.state end)");
    ModHost h(dir_);
    h.load({"d1", "d2"});
    EXPECT_EQ(h.decorate("tab", {{"title", "1"}, {"sessions", {"a", "b"}}}), "2 panes");
    EXPECT_EQ(h.decorate("agent", {{"state", "Working"}}), "Working");
}

TEST_F(ModsTest, RunawayLoopsAndErrorsStopTheModNotTheApp) {
    write("loop", "agentdesk.on('tick', function() while true do end end)");
    write("bad", "agentdesk.on('tick', function() error('boom') end)");
    write("syntax", "this is not lua");
    ModHost h(dir_);
    h.load({"loop", "bad", "syntax"});
    for (int i = 0; i < 6; ++i)
        h.emit("tick", nlohmann::json::object());
    const auto mods = h.mods();
    EXPECT_FALSE(mods[0].loaded);  // bad: stopped after repeated errors
    EXPECT_NE(mods[0].error.find("boom"), std::string::npos);
    EXPECT_NE(mods[1].error.find("too long"), std::string::npos);
    EXPECT_FALSE(mods[2].loaded);
    EXPECT_FALSE(mods[2].error.empty());
}
