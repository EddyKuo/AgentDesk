// The Inbox layout's pure rules: card order, snoozing, quick-answer configuration, the keyboard's
// selection, the texts, and that its commands, keys, layout and tips are all known.
#include "agentdesk/ui/inbox.hpp"

#include <algorithm>

#include <gtest/gtest.h>

#include "agentdesk/ui/command_names.hpp"
#include "agentdesk/ui/keymap.hpp"
#include "agentdesk/ui/layout.hpp"
#include "agentdesk/ui/layout_styles.hpp"
#include "agentdesk/ui/layout_tips.hpp"

namespace inbox = agentdesk::ui::inbox;
using nlohmann::json;

TEST(Inbox, NewestWaitingComesFirstByDefault) {
    const std::vector<inbox::Waiting> list = {{"a", 100, 0}, {"b", 300, 1}, {"c", 200, 2}};
    EXPECT_EQ(inbox::order(list, "newest"), (std::vector<std::string>{"b", "c", "a"}));
    EXPECT_EQ(inbox::order(list, ""), (std::vector<std::string>{"b", "c", "a"}));
    EXPECT_EQ(inbox::order(list, "oldest"), (std::vector<std::string>{"a", "c", "b"}));
}

TEST(Inbox, EqualTimesKeepCreationOrder) {
    const std::vector<inbox::Waiting> list = {{"a", 5, 0}, {"b", 5, 1}, {"c", 5, 2}};
    EXPECT_EQ(inbox::order(list, "newest"), (std::vector<std::string>{"c", "b", "a"}));
    EXPECT_EQ(inbox::order(list, "oldest"), (std::vector<std::string>{"a", "b", "c"}));
}

TEST(Inbox, OrderNameReadsOnlyTheTwoValues) {
    EXPECT_EQ(inbox::order_name("oldest"), "oldest");
    EXPECT_EQ(inbox::order_name("newest"), "newest");
    EXPECT_EQ(inbox::order_name("Oldest"), "newest");
    EXPECT_EQ(inbox::order_name(""), "newest");
}

TEST(Inbox, SnoozeHoldsUntilTheWaitOrTheQuestionChanges) {
    const inbox::Snooze z{1000, "Allow npm test?", "permission"};
    EXPECT_TRUE(inbox::still_snoozed(z, 1000, "Allow npm test?", "permission"));
    // The agent moved on and came back: a new wait.
    EXPECT_FALSE(inbox::still_snoozed(z, 2000, "Allow npm test?", "permission"));
    // Still waiting, but asking something else.
    EXPECT_FALSE(inbox::still_snoozed(z, 1000, "Allow rm -rf build?", "permission"));
    EXPECT_FALSE(inbox::still_snoozed(z, 1000, "Allow npm test?", "question"));
}

TEST(Inbox, DefaultAnswersOnlyForPermissionPromptsOfKnownPrograms) {
    const inbox::AnswerTable none;
    const auto claude = inbox::answers_for(none, "claude", "permission");
    ASSERT_EQ(claude.size(), 2u);
    EXPECT_EQ(claude[0].label, "Yes");
    EXPECT_EQ(claude[0].send, "1");
    EXPECT_EQ(claude[1].send, "\x1b");
    EXPECT_EQ(inbox::answers_for(none, "codex", "permission").size(), 2u);
    EXPECT_TRUE(inbox::answers_for(none, "claude", "question").empty());
    // A report without a kind: the program's answers are offered.
    EXPECT_EQ(inbox::answers_for(none, "claude", "").size(), 2u);
    EXPECT_TRUE(inbox::answers_for(none, "gemini", "permission").empty());
}

TEST(Inbox, PersonsAnswersReplaceTheDefaultsAndStarCoversTheRest) {
    std::vector<std::string> errors;
    const auto mine = inbox::parse_answers(json::parse(R"({
        "claude": [{"label": "Allow", "send": "1"}, {"label": "Explain", "send": "why?", "enter": true,
                    "kinds": ["question"]}],
        "*": [{"label": "Continue", "send": "", "enter": true}]
    })"),
                                           &errors);
    EXPECT_TRUE(errors.empty());
    const auto claude = inbox::answers_for(mine, "claude", "permission");
    ASSERT_EQ(claude.size(), 1u);
    EXPECT_EQ(claude[0].label, "Allow");
    const auto q = inbox::answers_for(mine, "claude", "question");
    ASSERT_EQ(q.size(), 2u);
    EXPECT_TRUE(q[1].enter);
    // Codex keeps its shipped answers; a program with none gets "*".
    EXPECT_EQ(inbox::answers_for(mine, "codex", "permission")[0].send, "y");
    const auto other = inbox::answers_for(mine, "gemini", "auth");
    ASSERT_EQ(other.size(), 1u);
    EXPECT_EQ(other[0].label, "Continue");
    // An empty list turns a program's answers off.
    EXPECT_TRUE(
        inbox::answers_for(inbox::parse_answers(json::parse(R"({"claude": []})")), "claude", "").empty());
}

TEST(Inbox, MalformedAnswersAreSkippedWithAReason) {
    std::vector<std::string> errors;
    const auto t = inbox::parse_answers(json::parse(R"({
        "claude": [{"label": "No send"}, {"send": 5}, "text", {"send": "\u001b"}, {"send": "y", "enter": "yes"}],
        "codex": {"label": "x"}
    })"),
                                        &errors);
    ASSERT_EQ(t.at("claude").size(), 1u);
    EXPECT_EQ(t.at("claude")[0].label, "Esc");  // no label: named after what it sends
    EXPECT_FALSE(t.contains("codex"));
    EXPECT_EQ(errors.size(), 5u);
    errors.clear();
    EXPECT_TRUE(inbox::parse_answers(json::array(), &errors).empty());
    EXPECT_EQ(errors.size(), 1u);
    EXPECT_TRUE(inbox::parse_answers(json()).empty());
}

TEST(Inbox, AtMostNineAnswersAndLabelsAreCut) {
    json list = json::array();
    for (int i = 0; i < 12; ++i)
        list.push_back({{"label", std::string(60, 'x')}, {"send", std::to_string(i)}});
    std::vector<std::string> errors;
    const auto t = inbox::parse_answers(json{{"claude", list}}, &errors);
    EXPECT_EQ(t.at("claude").size(), 9u);
    EXPECT_EQ(t.at("claude")[0].label.size(), 40u);
    EXPECT_EQ(errors.size(), 1u);
}

TEST(Inbox, AnswersRoundTripThroughJson) {
    const inbox::AnswerTable t = {{"claude", {{"Yes", "1", false, {"permission"}}, {"Go", "go", true, {}}}}};
    EXPECT_EQ(inbox::parse_answers(inbox::answers_to_json(t)), t);
}

TEST(Inbox, DescribeSendNamesControlKeys) {
    EXPECT_EQ(inbox::describe_send("\x1b", false), "Esc");
    EXPECT_EQ(inbox::describe_send("y", false), "y");
    EXPECT_EQ(inbox::describe_send("", true), "Enter");
    EXPECT_EQ(inbox::describe_send("\x03", false), "Ctrl+C");
    EXPECT_EQ(inbox::describe_send("ok", true), "ok Enter");
}

TEST(Inbox, KeyboardSelectionStopsAtTheEnds) {
    EXPECT_EQ(inbox::move_selection(-1, 3, 1), 0);
    EXPECT_EQ(inbox::move_selection(0, 3, -1), 0);
    EXPECT_EQ(inbox::move_selection(1, 3, 1), 2);
    EXPECT_EQ(inbox::move_selection(2, 3, 1), 2);
    EXPECT_EQ(inbox::move_selection(5, 3, 1), 0);
    EXPECT_EQ(inbox::move_selection(0, 0, 1), -1);
    // The card that took the answered card's place, else the one above.
    EXPECT_EQ(inbox::after_removal(1, 3), 1);
    EXPECT_EQ(inbox::after_removal(2, 2), 1);
    EXPECT_EQ(inbox::after_removal(0, 0), -1);
}

TEST(Inbox, Texts) {
    EXPECT_EQ(inbox::waited(3'000), "now");
    EXPECT_EQ(inbox::waited(40'000), "40s");
    EXPECT_EQ(inbox::waited(3 * 60'000 + 5'000), "3m");
    EXPECT_EQ(inbox::waited(65 * 60'000), "1h 5m");
    EXPECT_EQ(inbox::waited(2 * 3'600'000), "2h");
    EXPECT_EQ(inbox::waited(50LL * 3'600'000), "2d");
    EXPECT_EQ(inbox::summary(3, 2), "3 agents working, 2 idle");
    EXPECT_EQ(inbox::summary(1, 0), "1 agent working");
    EXPECT_EQ(inbox::summary(0, 1), "1 agent idle");
    EXPECT_EQ(inbox::summary(0, 0), "No agents running");
    EXPECT_EQ(inbox::kind_label("permission"), "Permission");
    EXPECT_EQ(inbox::kind_label("auth"), "Sign-in");
    EXPECT_EQ(inbox::kind_label(""), "");
}

TEST(Inbox, CommandsKeyLayoutAndTipsAreKnown) {
    for (const char* c : {"inbox.toggle", "inbox.open", "inbox.snooze", "inbox.dismiss", "inbox.answer",
                          "inbox.reply", "inbox.order", "inbox.unsnooze"})
        EXPECT_TRUE(agentdesk::ui::is_builtin_command(c)) << c;
    const agentdesk::ui::Keymap k = agentdesk::ui::Keymap::defaults();
    EXPECT_EQ(k.chord_for("inbox.toggle"), "Ctrl+Shift+I");
    namespace lay = agentdesk::ui::layout;
    const auto names = lay::builtin_names();
    EXPECT_NE(std::find(names.begin(), names.end(), "inbox"), names.end());
    EXPECT_EQ(agentdesk::ui::styles::style_of_tree(lay::builtin("inbox")), "inbox");
    EXPECT_EQ(agentdesk::ui::styles::display_name("inbox"), "Inbox");
    EXPECT_FALSE(agentdesk::ui::styles::is_fun_layout("inbox"));
    const auto card = agentdesk::ui::tips::card("inbox");
    ASSERT_TRUE(card.has_value());
    EXPECT_GE(card->tips.size(), 4u);
}
