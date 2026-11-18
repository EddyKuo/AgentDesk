// Example mod: a sidebar panel with how many agents need you, are working, or are idle/done.
#include <string>
#include <vector>

#include <agentdesk/mod_sdk.hpp>

namespace {

class AgentSummary : public agentdesk::mod::Mod {
public:
    std::vector<agentdesk::mod::Panel> panels(const std::string& where) override {
        if (where != "sidebar")
            return {};
        int blocked = 0, working = 0, rest = 0;
        for (const auto& s : host().sessions()) {
            const std::string state = s.value("state", "");
            if (state == "Blocked")
                ++blocked;
            else if (state == "Working")
                ++working;
            else if (state == "Idle" || state == "Done")
                ++rest;
        }
        agentdesk::mod::Panel p("summary", "Agents now");
        p.row("Need you", std::to_string(blocked), blocked > 0 ? "bad" : "dim");
        p.row("Working", std::to_string(working), working > 0 ? "warn" : "dim");
        p.row("Idle / done", std::to_string(rest), "dim");
        return {p};
    }
};

}  // namespace

AGENTDESK_MOD(AgentSummary, "Sidebar panel: how many agents are working, waiting or done")
