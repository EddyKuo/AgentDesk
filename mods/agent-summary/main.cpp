// Example mod: a sidebar panel with how many agents need you, are working, or are idle/done.
// Shows the simplest kind of mod: no state of its own, a panel rebuilt from host().sessions().
#include <string>
#include <vector>

#include <agentdesk/mod_sdk.hpp>

namespace {

class AgentSummary : public agentdesk::mod::Mod {
public:
    // AgentDesk asks for panels each time it draws a place that can hold them ("sidebar" or
    // "details"), so keep this cheap and return nothing for places the mod does not use.
    std::vector<agentdesk::mod::Panel> panels(const std::string& where) override {
        if (where != "sidebar")
            return {};
        int blocked = 0, working = 0, rest = 0;
        // Each session is a JSON object; "state" is the agent state: Working, Blocked, Idle,
        // Done or Unknown.
        for (const auto& s : host().sessions()) {
            const std::string state = s.value("state", "");
            if (state == "Blocked")
                ++blocked;
            else if (state == "Working")
                ++working;
            else if (state == "Idle" || state == "Done")
                ++rest;
        }
        // The panel id must stay the same between calls; the title is what the person sees.
        // The last argument of row() is a colour role: "", "dim", "accent", "good", "warn", "bad".
        agentdesk::mod::Panel p("summary", "Agents now");
        p.row("Need you", std::to_string(blocked), blocked > 0 ? "bad" : "dim");
        p.row("Working", std::to_string(working), working > 0 ? "warn" : "dim");
        p.row("Idle / done", std::to_string(rest), "dim");
        return {p};
    }
};

}  // namespace

// Exports the mod's entry point; the text is the description shown in Settings > Mods.
AGENTDESK_MOD(AgentSummary, "Sidebar panel: how many agents are working, waiting or done")
