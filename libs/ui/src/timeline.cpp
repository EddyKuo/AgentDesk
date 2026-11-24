// The Timeline's rules: state marks to bars, packing thin bars, axis ticks, row order, parsing.
#include "agentdesk/ui/timeline.hpp"

#include <algorithm>
#include <cstdio>
#include <functional>
#include <map>
#include <set>

namespace agentdesk::ui::timeline {

namespace {
constexpr long long kMinute = 60LL * 1000;
constexpr long long kHour = 60 * kMinute;
constexpr long long kDay = 24 * kHour;
}  // namespace

Kind kind_of(const std::string& agent, const std::string& process) {
    if (process == "Exited" || process == "Failed" || process == "Lost")
        return Kind::kExited;
    if (agent == "Working")
        return Kind::kWorking;
    if (agent == "Blocked")
        return Kind::kBlocked;
    if (agent == "Done")
        return Kind::kDone;
    if (agent == "Idle")
        return Kind::kIdle;
    if (agent == "Error")
        return Kind::kError;
    return Kind::kUnknown;
}

int attention_rank(Kind k) {
    switch (k) {
        case Kind::kBlocked:
            return 6;
        case Kind::kError:
            return 5;
        case Kind::kDone:
            return 4;
        case Kind::kWorking:
            return 3;
        case Kind::kIdle:
            return 2;
        case Kind::kExited:
            return 1;
        case Kind::kUnknown:
            break;
    }
    return 0;
}

const char* kind_label(Kind k) {
    switch (k) {
        case Kind::kWorking:
            return "Working";
        case Kind::kBlocked:
            return "Needs you";
        case Kind::kDone:
            return "Done";
        case Kind::kIdle:
            return "Idle";
        case Kind::kError:
            return "Error";
        case Kind::kExited:
            return "Exited";
        case Kind::kUnknown:
            break;
    }
    return "No status";
}

std::vector<Segment> segments(const std::vector<Mark>& marks, long long from_ms, long long to_ms,
                              int* dropped) {
    std::vector<Segment> out;
    int bad = 0;
    std::vector<const Mark*> ordered;
    for (const auto& m : marks) {
        if (!ordered.empty() && m.at_ms < ordered.back()->at_ms) {
            ++bad;  // the clock went back: this mark cannot be placed
            continue;
        }
        ordered.push_back(&m);
    }
    std::string last_known;
    for (std::size_t i = 0; i < ordered.size(); ++i) {
        const Mark& m = *ordered[i];
        const Kind k = kind_of(m.agent, m.process);
        const long long start = m.at_ms;
        const long long end = i + 1 < ordered.size() ? ordered[i + 1]->at_ms : to_ms;
        const long long a = std::max(start, from_ms);
        const long long b = std::min(end, to_ms);
        if (b > a || (k == Kind::kExited && start >= from_ms && start <= to_ms && a == b)) {
            Segment s{a, std::max(a, b), k, k == Kind::kUnknown ? last_known : std::string(), 1};
            out.push_back(std::move(s));
        }
        if (k != Kind::kUnknown && k != Kind::kExited)
            last_known = m.agent;
    }
    if (dropped != nullptr)
        *dropped = bad;
    return out;
}

std::vector<Segment> pack(const std::vector<Segment>& in, double px_per_ms, float min_px) {
    std::vector<Segment> out;
    bool open_short = false;  // out.back() gathers short stretches and is still too thin itself
    auto width = [&](const Segment& s) { return static_cast<double>(s.to_ms - s.from_ms) * px_per_ms; };
    for (const auto& s : in) {
        if (width(s) >= min_px) {
            out.push_back(s);
            open_short = false;
            continue;
        }
        if (open_short && !out.empty() && out.back().to_ms == s.from_ms) {
            Segment& g = out.back();
            g.to_ms = s.to_ms;
            if (attention_rank(s.kind) > attention_rank(g.kind)) {
                g.kind = s.kind;
                g.last_known = s.last_known;
            }
            g.merged += s.merged;
            open_short = width(g) < min_px;
        } else {
            out.push_back(s);
            open_short = true;
        }
    }
    return out;
}

std::string clock_text(long long at_ms, long long utc_offset_ms) {
    long long minutes = ((at_ms + utc_offset_ms) / kMinute) % (24 * 60);
    if (minutes < 0)
        minutes += 24 * 60;
    char buf[8];
    std::snprintf(buf, sizeof buf, "%02lld:%02lld", minutes / 60, minutes % 60);
    return buf;
}

std::vector<Tick> ticks(long long from_ms, long long to_ms, float width, long long utc_offset_ms,
                        float min_gap) {
    std::vector<Tick> out;
    const long long span = to_ms - from_ms;
    if (span <= 0 || width <= 0)
        return out;
    long long step = span <= 15 * kMinute + kMinute ? 2 * kMinute
                     : span <= kHour + kMinute      ? 15 * kMinute
                     : span <= 4 * kHour + kMinute  ? 30 * kMinute
                                                    : 3 * kHour;
    while (static_cast<double>(width) * static_cast<double>(step) / static_cast<double>(span) < min_gap &&
           step < kDay)
        step *= 2;
    // The first whole step (in local time) at or after from_ms.
    const long long local = from_ms + utc_offset_ms;
    long long t = (local / step) * step;
    if (t < local)
        t += step;
    for (t -= utc_offset_ms; t <= to_ms; t += step) {
        const float x =
            static_cast<float>(static_cast<double>(t - from_ms) / static_cast<double>(span) * width);
        out.push_back({t, x, clock_text(t, utc_offset_ms)});
    }
    return out;
}

long long range_ms(const std::string& range) {
    if (range == "15m")
        return 15 * kMinute;
    if (range == "4h")
        return 4 * kHour;
    if (range == "24h")
        return kDay;
    return kHour;
}

namespace {
const std::vector<std::string> kRanges = {"15m", "1h", "4h", "24h"};
std::size_t range_index(const std::string& r) {
    const auto it = std::find(kRanges.begin(), kRanges.end(), r);
    return it == kRanges.end() ? 1 : static_cast<std::size_t>(it - kRanges.begin());
}
}  // namespace

std::string shorter_range(const std::string& range) {
    const std::size_t i = range_index(range);
    return kRanges[i == 0 ? 0 : i - 1];
}

std::string longer_range(const std::string& range) {
    return kRanges[std::min(range_index(range) + 1, kRanges.size() - 1)];
}

std::string duration_text(long long ms) {
    ms = std::max(0LL, ms);
    if (ms < kMinute)
        return std::to_string(ms / 1000) + "s";
    if (ms < kHour)
        return std::to_string(ms / kMinute) + "m";
    if (ms < kDay) {
        char buf[16];
        std::snprintf(buf, sizeof buf, "%lldh %02lldm", ms / kHour, (ms % kHour) / kMinute);
        return buf;
    }
    return std::to_string(ms / kHour) + "h";
}

std::vector<Row> rows(const std::vector<RowInput>& in) {
    std::map<std::string, const RowInput*> by_id;
    for (const auto& r : in)
        by_id[r.id] = &r;
    std::map<std::string, std::vector<const RowInput*>> kids;
    std::vector<const RowInput*> top;
    for (const auto& r : in) {
        if (!r.parent.empty() && r.parent != r.id && by_id.contains(r.parent))
            kids[r.parent].push_back(&r);
        else
            top.push_back(&r);
    }
    auto before = [](const RowInput* a, const RowInput* b) {
        if (a->needs_you != b->needs_you)
            return a->needs_you;
        if (a->last_change_ms != b->last_change_ms)
            return a->last_change_ms > b->last_change_ms;
        return a->id < b->id;
    };
    std::vector<Row> out;
    std::set<std::string> placed;
    std::function<void(const RowInput*, int)> place = [&](const RowInput* r, int depth) {
        if (!placed.insert(r->id).second)
            return;  // a parent loop: each row once
        out.push_back({r->id, depth});
        auto it = kids.find(r->id);
        if (it == kids.end())
            return;
        auto list = it->second;
        std::sort(list.begin(), list.end(), before);
        for (const auto* k : list)
            place(k, depth + 1);
    };
    std::sort(top.begin(), top.end(), before);
    for (const auto* r : top)
        place(r, 0);
    // Rows only reachable through a loop of parents: still listed.
    for (const auto& r : in)
        if (!placed.contains(r.id))
            place(&r, 0);
    return out;
}

std::optional<HostSession> parse_session(const nlohmann::json& j, long long offset_ms) {
    if (!j.is_object())
        return std::nullopt;
    auto str = [&](const char* k) {
        const auto it = j.find(k);
        return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
    };
    auto flag = [&](const char* k) {
        const auto it = j.find(k);
        return it != j.end() && it->is_boolean() && it->get<bool>();
    };
    HostSession s;
    s.id = str("id");
    if (s.id.empty())
        return std::nullopt;
    s.name = str("name");
    s.program = str("program");
    s.profile = str("profile");
    s.parent = str("parent");
    s.ended = flag("ended");
    s.truncated = flag("truncated");
    s.adopted = flag("adopted");
    if (const auto m = j.find("marks"); m != j.end() && m->is_array()) {
        for (const auto& k : *m) {
            if (!k.is_array() || k.size() < 3 || !k[0].is_number_integer() || !k[1].is_string() ||
                !k[2].is_string())
                continue;
            s.marks.push_back(
                {k[0].get<long long>() + offset_ms, k[1].get<std::string>(), k[2].get<std::string>()});
        }
    }
    return s;
}

}  // namespace agentdesk::ui::timeline
