// The Inbox layout's rules; see inbox.hpp.
#include "agentdesk/ui/inbox.hpp"

#include <algorithm>
#include <cstdio>

namespace agentdesk::ui::inbox {

namespace {
constexpr std::size_t kMaxAnswers = 9;  // keys 1-9
constexpr std::size_t kMaxLabel = 40;   // bytes; longer labels are cut (buttons stay small)
constexpr std::size_t kMaxSend = 4096;  // bytes

// Cuts `s` to at most `n` bytes without splitting a UTF-8 sequence.
std::string cut_utf8(std::string s, std::size_t n) {
    if (s.size() <= n)
        return s;
    std::size_t end = n;
    while (end > 0 && (static_cast<unsigned char>(s[end]) & 0xC0u) == 0x80u)
        --end;
    s.resize(end);
    return s;
}
}  // namespace

// ---------------------------------------------------------------- order
std::string order_name(std::string_view value) {
    return value == "oldest" ? std::string("oldest") : std::string("newest");
}

std::vector<std::string> order(std::vector<Waiting> list, std::string_view how) {
    const bool oldest = order_name(how) == "oldest";
    std::stable_sort(list.begin(), list.end(), [&](const Waiting& a, const Waiting& b) {
        if (a.since != b.since)
            return oldest ? a.since < b.since : a.since > b.since;
        return oldest ? a.seq < b.seq : a.seq > b.seq;
    });
    std::vector<std::string> out;
    out.reserve(list.size());
    for (auto& w : list)
        out.push_back(std::move(w.id));
    return out;
}

// ---------------------------------------------------------------- snooze
bool still_snoozed(const Snooze& snoozed, long long since, std::string_view message, std::string_view kind) {
    return snoozed.since == since && snoozed.message == message && snoozed.kind == kind;
}

// ---------------------------------------------------------------- quick answers
const AnswerTable& default_answers() {
    // Both CLIs ask for approval with a short menu; these are the keys their documentation names
    // for the first and the last choice. To verify against the real CLIs (IMPL-round2-inbox.md).
    static const AnswerTable k = {
        {"claude", {{"Yes", "1", false, {"permission"}}, {"No", "\x1b", false, {"permission"}}}},
        {"codex", {{"Yes", "y", false, {"permission"}}, {"No", "\x1b", false, {"permission"}}}},
    };
    return k;
}

AnswerTable parse_answers(const nlohmann::json& j, std::vector<std::string>* errors) {
    AnswerTable out;
    auto fail = [&](const std::string& what) {
        if (errors != nullptr)
            errors->push_back("inbox_answers: " + what);
    };
    if (j.is_null())
        return out;
    if (!j.is_object()) {
        fail("expected an object of program -> answers");
        return out;
    }
    for (const auto& [program, list] : j.items()) {
        if (!list.is_array()) {
            fail(program + ": expected a list of answers");
            continue;
        }
        std::vector<QuickAnswer> answers;
        for (std::size_t i = 0; i < list.size(); ++i) {
            const auto& a = list[i];
            const std::string where = program + "[" + std::to_string(i) + "]";
            if (!a.is_object()) {
                fail(where + ": expected an object");
                continue;
            }
            QuickAnswer q;
            const auto label = a.find("label");
            const auto send = a.find("send");
            const auto enter = a.find("enter");
            const auto kinds = a.find("kinds");
            if (send != a.end() && !send->is_string()) {
                fail(where + ": \"send\" must be text");
                continue;
            }
            if (enter != a.end() && !enter->is_boolean()) {
                fail(where + ": \"enter\" must be true or false");
                continue;
            }
            q.send = send != a.end() ? cut_utf8(send->get<std::string>(), kMaxSend) : std::string();
            q.enter = enter != a.end() && enter->get<bool>();
            if (q.send.empty() && !q.enter) {
                fail(where + ": nothing to send");
                continue;
            }
            if (label != a.end() && label->is_string())
                q.label = cut_utf8(label->get<std::string>(), kMaxLabel);
            if (q.label.empty())
                q.label = describe_send(q.send, q.enter);
            if (kinds != a.end()) {
                if (kinds->is_string()) {
                    q.kinds.push_back(kinds->get<std::string>());
                } else if (kinds->is_array()) {
                    for (const auto& k : *kinds)
                        if (k.is_string())
                            q.kinds.push_back(k.get<std::string>());
                } else {
                    fail(where + ": \"kinds\" must be a list of permission, question, auth");
                }
            }
            if (answers.size() == kMaxAnswers) {
                fail(program + ": more than 9 answers, the rest are ignored");
                break;
            }
            answers.push_back(std::move(q));
        }
        out[program] = std::move(answers);
    }
    return out;
}

nlohmann::json answers_to_json(const AnswerTable& table) {
    nlohmann::json j = nlohmann::json::object();
    for (const auto& [program, list] : table) {
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& q : list) {
            nlohmann::json a = {{"label", q.label}, {"send", q.send}};
            if (q.enter)
                a["enter"] = true;
            if (!q.kinds.empty())
                a["kinds"] = q.kinds;
            arr.push_back(std::move(a));
        }
        j[program] = std::move(arr);
    }
    return j;
}

std::vector<QuickAnswer> answers_for(const AnswerTable& mine, std::string_view program,
                                     std::string_view kind) {
    const std::vector<QuickAnswer>* list = nullptr;
    const std::string key(program);
    if (const auto it = mine.find(key); it != mine.end())
        list = &it->second;
    else if (const auto d = default_answers().find(key); d != default_answers().end())
        list = &d->second;
    else if (const auto any = mine.find("*"); any != mine.end())
        list = &any->second;
    std::vector<QuickAnswer> out;
    if (list == nullptr)
        return out;
    for (const auto& q : *list) {
        const bool fits = kind.empty() || q.kinds.empty() ||
                          std::find(q.kinds.begin(), q.kinds.end(), kind) != q.kinds.end();
        if (fits && out.size() < kMaxAnswers)
            out.push_back(q);
    }
    return out;
}

std::string describe_send(std::string_view send, bool enter) {
    std::string out;
    for (const char c : send) {
        const auto u = static_cast<unsigned char>(c);
        std::string part;
        if (u == 0x1B)
            part = "Esc";
        else if (u == '\r' || u == '\n')
            part = "Enter";
        else if (u == '\t')
            part = "Tab";
        else if (u == 0x7F || u == 0x08)
            part = "Backspace";
        else if (u > 0 && u < 0x20)
            part = std::string("Ctrl+") + static_cast<char>('A' + u - 1);
        else if (u == ' ')
            part = "Space";
        if (!part.empty()) {
            out += (out.empty() || out.back() == ' ' ? "" : " ") + part + " ";
        } else {
            out += c;
        }
    }
    while (!out.empty() && out.back() == ' ')
        out.pop_back();
    if (enter)
        out += out.empty() ? "Enter" : " Enter";
    return cut_utf8(out, kMaxLabel);
}

// ---------------------------------------------------------------- keyboard
int move_selection(int current, int count, int delta) {
    if (count <= 0)
        return -1;
    if (current < 0 || current >= count)
        return 0;
    return std::clamp(current + delta, 0, count - 1);
}

int after_removal(int removed, int count_after) {
    if (count_after <= 0)
        return -1;
    return std::clamp(removed, 0, count_after - 1);
}

// ---------------------------------------------------------------- texts
std::string waited(long long ms) {
    if (ms < 10'000)
        return "now";
    const long long s = ms / 1000;
    if (s < 60)
        return std::to_string(s) + "s";
    const long long m = s / 60;
    if (m < 60)
        return std::to_string(m) + "m";
    const long long h = m / 60;
    if (h < 24)
        return m % 60 == 0 ? std::to_string(h) + "h"
                           : std::to_string(h) + "h " + std::to_string(m % 60) + "m";
    return std::to_string(h / 24) + "d";
}

std::string summary(int working, int idle) {
    auto agents = [](int n) { return std::to_string(n) + (n == 1 ? " agent" : " agents"); };
    if (working <= 0 && idle <= 0)
        return "No agents running";
    if (working > 0 && idle > 0)
        return agents(working) + " working, " + std::to_string(idle) + " idle";
    if (working > 0)
        return agents(working) + " working";
    return agents(idle) + " idle";
}

std::string kind_label(std::string_view kind) {
    if (kind == "permission")
        return "Permission";
    if (kind == "question")
        return "Question";
    if (kind == "auth")
        return "Sign-in";
    return {};
}

}  // namespace agentdesk::ui::inbox
