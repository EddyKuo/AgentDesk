// Mod DLL used by tests/unit/mods_test.cpp: exercises every part of the mod interface, plus
// commands that misbehave (crash, throw) to check that AgentDesk survives them.
#include <stdexcept>
#include <string>
#include <vector>

#include <agentdesk/mod_sdk.hpp>

namespace {

class TestMod : public agentdesk::mod::Mod {
public:
    TestMod() {
        host().add_command("hi", "Say hi");
        host().add_command("crash", "Crash");
        host().add_command("throw", "Throw");
        host().add_command("act", "Act");
    }
    void on_event(const std::string& event, const agentdesk::mod::Json& p) override {
        if (event == "agent_state")
            host().notify("state", p.value("session", "") + "=" + p.value("state", ""));
    }
    std::vector<agentdesk::mod::Panel> panels(const std::string& where) override {
        if (where != "sidebar")
            return {};
        agentdesk::mod::Panel p("p1", "Test panel");
        p.text("sessions " + std::to_string(host().sessions().size()))
            .row("left", "right", "good")
            .progress(0.5f, "half")
            .button("b1", "Press")
            .separator()
            .badge("clicks " + std::to_string(clicks_));
        return {p};
    }
    void click(const std::string& panel, const std::string& widget) override {
        if (panel == "p1" && widget == "b1")
            ++clicks_;
    }
    std::string decorate(const std::string& kind, const agentdesk::mod::Json& subject) override {
        return kind == "tab" ? "T:" + subject.value("title", "") : std::string();
    }
    void command(const std::string& id) override {
        if (id == "hi") {
            host().notify("hi", host().setting("greeting", "none").get<std::string>());
        } else if (id == "act") {
            host().run("tab.new");
            host().send("s1", "ls", true);
            host().exec({"curl", "-s", "https://example.invalid"});
        } else if (id == "throw") {
            throw std::runtime_error("boom");
        } else if (id == "crash") {
            volatile int* p = nullptr;
            *p = 1;
        }
    }

private:
    int clicks_ = 0;
};

}  // namespace

AGENTDESK_MOD(TestMod, "Test mod")
