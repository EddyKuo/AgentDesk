#pragma once
// The Inbox layout's rules, without drawing: which agents get a card and in what order, how a
// snoozed card comes back, the quick answers a card offers (ui.json "inbox_answers" over the
// shipped defaults), the keyboard's selection, and the short texts the cards and the bar show.
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace agentdesk::ui::inbox {

// ---- order
// One agent that waits for the person (or finished): `since` is when it took that state (any
// clock, larger is later), `seq` breaks ties (creation order).
struct Waiting {
    std::string id;
    long long since = 0;
    long long seq = 0;
};
// "newest" (default) or "oldest", as written in ui.json "inbox_order"; anything else is "newest".
std::string order_name(std::string_view value);
// Ids in card order: the most recent first ("newest"), or the one waiting longest first
// ("oldest"); equal times keep creation order (the newer agent first for "newest").
std::vector<std::string> order(std::vector<Waiting> list, std::string_view order);

// ---- snooze
// What a card looked like when it was snoozed: it stays hidden while the agent is still in the
// same wait (same `since`) asking the same thing (same `message` and `kind`). A new question, or
// the agent leaving the state and coming back, shows the card again.
struct Snooze {
    long long since = 0;
    std::string message;
    std::string kind;
};
bool still_snoozed(const Snooze& snoozed, long long since, std::string_view message, std::string_view kind);

// ---- quick answers
// A button on a card: `label` on it, `send` written to the terminal as raw bytes (JSON escapes
// give control keys: "\u001b" is Esc), then Enter when `enter`. `kinds` limits it to reports of
// those kinds ("permission", "question", "auth"); empty: any.
struct QuickAnswer {
    std::string label;
    std::string send;
    bool enter = false;
    std::vector<std::string> kinds;
    bool operator==(const QuickAnswer&) const = default;
};
// Program key (programs.json: "claude", "codex", ...; "*" for any program without its own
// entry) -> its answers, in button order.
using AnswerTable = std::map<std::string, std::vector<QuickAnswer>>;

// The shipped answers. Only for programs whose approval prompt keys are documented, and only for
// permission reports (Claude Code: 1 = Yes, Esc = No; Codex: y = Yes, n = No).
const AnswerTable& default_answers();
// ui.json "inbox_answers": {"<program>": [{"label", "send", "enter"?, "kinds"?}, ...], ...}. A
// malformed entry is skipped with a line in `errors`; an answer needs a label and something to
// send (text or enter). At most 9 answers per program (keys 1-9).
AnswerTable parse_answers(const nlohmann::json& j, std::vector<std::string>* errors = nullptr);
nlohmann::json answers_to_json(const AnswerTable& table);
// The answers a card shows for `program` and a report of `kind` ("" when the report had none:
// every answer of the program then): the person's entry for the program, else the shipped one,
// else the person's "*" entry.
std::vector<QuickAnswer> answers_for(const AnswerTable& mine, std::string_view program,
                                     std::string_view kind);
// How a quick answer is shown when it has no label of its own ("Esc", "Enter", "y").
std::string describe_send(std::string_view send, bool enter);

// ---- keyboard
// The card selected after moving `delta` (-1 up, +1 down) from `current` among `count` cards;
// -1 (none yet) selects the first. Stops at the ends.
int move_selection(int current, int count, int delta);
// The card selected after the one at `removed` went away (it was answered or snoozed): the card
// that took its place, else the one above; -1 when none is left.
int after_removal(int removed, int count_after);

// ---- texts
// How long an agent has waited, short: "now" (under 10 s), "40s", "3m", "1h 5m", "2d".
std::string waited(long long ms);
// The empty Inbox's line: "3 agents working, 2 idle", "1 agent working", "No agents running".
std::string summary(int working, int idle);
// "Permission", "Question", "Auth": a report's kind for a chip ("" for none).
std::string kind_label(std::string_view kind);

}  // namespace agentdesk::ui::inbox
