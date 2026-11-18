// Example mod: a phone notification through ntfy.sh when an agent needs you or finishes.
// Put your topic in settings.json next to mod.dll ({"topic": "..."}), then enable the mod in
// Settings > Mods. The message goes out with curl (part of Windows), started without a shell.
#include <string>

#include <agentdesk/mod_sdk.hpp>

namespace {

class NtfyNotify : public agentdesk::mod::Mod {
public:
    NtfyNotify() : topic_(host().setting("topic", "").get<std::string>()) {
        host().add_command("test", "Send a test notification");
    }

    void on_event(const std::string& event, const agentdesk::mod::Json& ev) override {
        if (event != "agent_state" || topic_.empty())
            return;
        const std::string state = ev.value("state", "");
        if (state == "Blocked" || (ev.value("previous", "") == "Working" && state == "Idle"))
            send(ev.value("title", "agent") + (state == "Blocked" ? " needs you" : " finished"));
    }

    void command(const std::string& id) override {
        if (id != "test")
            return;
        if (topic_.empty())
            host().notify("ntfy: no topic", "Put {\"topic\": \"...\"} in mods\\ntfy-notify\\settings.json");
        else
            send("AgentDesk test");
    }

private:
    void send(const std::string& text) const {
        host().exec({"curl", "-s", "-d", text, "https://ntfy.sh/" + topic_});
    }
    std::string topic_;
};

}  // namespace

AGENTDESK_MOD(NtfyNotify, "Phone notification through ntfy.sh when an agent needs you")
