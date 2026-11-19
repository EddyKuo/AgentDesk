// Example mod: shows how long each tab's agent has been working ("3m" after the tab title).
// Shows keeping state between events and decorating tabs.
#include <chrono>
#include <map>
#include <string>

#include <agentdesk/mod_sdk.hpp>

namespace {

using Clock = std::chrono::steady_clock;

class TabTimer : public agentdesk::mod::Mod {
public:
    // Remembers when each session started working; forgets it on any other state.
    void on_event(const std::string& event, const agentdesk::mod::Json& ev) override {
        if (event != "agent_state")
            return;
        const std::string session = ev.value("session", "");
        if (ev.value("state", "") == "Working")
            started_.try_emplace(session, Clock::now());
        else
            started_.erase(session);
    }

    // Called while drawing, for each tab (kind "tab", {title, sessions: [ids]}) and agent row
    // (kind "agent"). The returned text is drawn after the title; "" draws nothing.
    std::string decorate(const std::string& kind, const agentdesk::mod::Json& tab) override {
        if (kind != "tab")
            return {};
        // A tab can hold several sessions: time the one that has been working longest.
        bool any = false;
        Clock::time_point oldest{};
        for (const auto& id : tab.value("sessions", agentdesk::mod::Json::array())) {
            const auto it = started_.find(id.get<std::string>());
            if (it != started_.end() && (!any || it->second < oldest)) {
                oldest = it->second;
                any = true;
            }
        }
        if (!any)
            return {};
        const auto s = std::chrono::duration_cast<std::chrono::seconds>(Clock::now() - oldest).count();
        return s < 60 ? std::to_string(s) + "s" : std::to_string(s / 60) + "m";
    }

private:
    std::map<std::string, Clock::time_point> started_;  // session id -> when it began working
};

}  // namespace

AGENTDESK_MOD(TabTimer, "Shows how long each tab's agent has been working")
