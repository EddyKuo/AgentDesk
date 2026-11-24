// Pair's rules: the pair's life, the race and the names; see pair.hpp.
#include "agentdesk/ui/pair.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace agentdesk::ui::pair {

const char* side_name(Side s) {
    return s == Side::kLeft ? "left" : "right";
}

std::optional<Side> parse_side(std::string_view s) {
    if (s == "left" || s == "l" || s == "a")
        return Side::kLeft;
    if (s == "right" || s == "r" || s == "b")
        return Side::kRight;
    return std::nullopt;
}

// ---------------------------------------------------------------- Model
bool Model::has(const std::string& session) const {
    return side_of(session).has_value();
}

std::optional<Side> Model::side_of(const std::string& session) const {
    if (session.empty())
        return std::nullopt;
    if (ids_[0] == session)
        return Side::kLeft;
    if (ids_[1] == session)
        return Side::kRight;
    return std::nullopt;
}

bool Model::start() {
    if (phase_ == Phase::kStarting)
        return false;
    phase_ = Phase::kStarting;
    ids_ = {};
    error_.clear();
    return true;
}

void Model::placed(Side s, std::string session) {
    if (phase_ != Phase::kStarting || session.empty())
        return;
    ids_[index(s)] = std::move(session);
    if (!ids_[0].empty() && !ids_[1].empty())
        phase_ = Phase::kRunning;
}

void Model::failed(std::string why) {
    if (phase_ != Phase::kStarting)
        return;
    phase_ = Phase::kSetup;
    error_ = std::move(why);
    // A side that did start keeps running in its tab; the pair just does not form.
    ids_ = {};
}

bool Model::adopt(std::string left, std::string right) {
    if (left.empty() || right.empty() || left == right)
        return false;
    ids_ = {std::move(left), std::move(right)};
    phase_ = Phase::kRunning;
    error_.clear();
    return true;
}

void Model::prune(const std::vector<std::string>& alive) {
    if (phase_ != Phase::kRunning && phase_ != Phase::kHalf)
        return;
    for (auto& id : ids_)
        if (!id.empty() && std::find(alive.begin(), alive.end(), id) == alive.end())
            id.clear();
    if (ids_[0].empty() && ids_[1].empty())
        phase_ = Phase::kSetup;
    else if (ids_[0].empty() || ids_[1].empty())
        phase_ = Phase::kHalf;
}

bool Model::repair(Side s, std::string session) {
    if ((phase_ != Phase::kRunning && phase_ != Phase::kHalf) || session.empty())
        return false;
    auto& mine = ids_[index(s)];
    auto& theirs = ids_[index(other(s))];
    if (session == mine)
        return false;
    if (session == theirs)
        std::swap(mine, theirs);
    else
        mine = std::move(session);
    phase_ = !ids_[0].empty() && !ids_[1].empty() ? Phase::kRunning : Phase::kHalf;
    return true;
}

bool Model::swap() {
    if (phase_ != Phase::kRunning && phase_ != Phase::kHalf)
        return false;
    std::swap(ids_[0], ids_[1]);
    return true;
}

void Model::close() {
    phase_ = Phase::kSetup;
    ids_ = {};
    error_.clear();
}

// ---------------------------------------------------------------- Race
long long Runner::elapsed(long long now_ms) const {
    if (started_ms == 0)
        return 0;
    const long long end = finished_ms != 0 ? finished_ms : now_ms;
    return std::max(0LL, end - started_ms);
}

void Race::reset() {
    runners_ = {};
}

void Race::sent(Side s, long long now_ms) {
    if (result())
        reset();
    Runner& r = runners_[index(s)];
    if (r.running())
        return;  // the turn under way goes on
    r = Runner{};
    r.started_ms = now_ms;
}

void Race::observe(Side s, std::string_view agent_state, long long at_ms, long long now_ms) {
    std::string& last = last_state_[index(s)];
    if (last == agent_state)
        return;
    last = std::string(agent_state);
    const long long at = at_ms > 0 ? at_ms : now_ms;
    Runner& r = runners_[index(s)];
    if (agent_state == "Working") {
        if (result()) {
            reset();  // both had finished: this is a new round
        }
        if (r.started_ms == 0) {
            r.started_ms = at;
        } else if (r.finished_ms != 0) {
            r.finished_ms = 0;  // it carried on after saying it was done
        }
        r.working = true;
    } else if (agent_state == "Done") {
        if (r.running() && r.working)
            r.finished_ms = std::max(at, r.started_ms);
    }
}

void Race::swap() {
    std::swap(runners_[0], runners_[1]);
    std::swap(last_state_[0], last_state_[1]);
}

bool Race::active() const {
    return runners_[0].started_ms != 0 || runners_[1].started_ms != 0;
}

std::optional<Side> Race::first_finished() const {
    const bool l = runners_[0].finished(), r = runners_[1].finished();
    if (l && runners_[1].running())
        return Side::kLeft;
    if (r && runners_[0].running())
        return Side::kRight;
    if (l && r) {
        const auto res = result();
        return res ? res->winner : std::nullopt;
    }
    return std::nullopt;
}

std::optional<Result> Race::result() const {
    if (!runners_[0].finished() || !runners_[1].finished())
        return std::nullopt;
    Result res;
    res.elapsed = {runners_[0].elapsed(0), runners_[1].elapsed(0)};
    const long long d = res.elapsed[0] - res.elapsed[1];
    res.margin_ms = std::llabs(d);
    if (res.margin_ms >= kTieMs)
        res.winner = d < 0 ? Side::kLeft : Side::kRight;
    return res;
}

// ---------------------------------------------------------------- text
std::string format_duration(long long ms) {
    const long long sec = std::max(0LL, ms) / 1000;
    char buf[32];
    if (sec < 60)
        std::snprintf(buf, sizeof buf, "%llds", sec);
    else if (sec < 3600)
        std::snprintf(buf, sizeof buf, "%lldm %02llds", sec / 60, sec % 60);
    else
        std::snprintf(buf, sizeof buf, "%lldh %02lldm", sec / 3600, (sec / 60) % 60);
    return buf;
}

std::string headline(const Result& r, std::string_view left_name, std::string_view right_name) {
    if (!r.winner)
        return "A dead heat";
    const std::string_view who = *r.winner == Side::kLeft ? left_name : right_name;
    return std::string(who) + " finished first, " + format_duration(r.margin_ms) + " ahead";
}

std::string changes_text(int files, int added, int removed) {
    if (files < 0)
        return "Reading changes...";
    if (files == 0)
        return "No changes";
    char buf[64];
    std::snprintf(buf, sizeof buf, "%d file%s  +%d -%d", files, files == 1 ? "" : "s", added, removed);
    return buf;
}

// ---------------------------------------------------------------- setup
std::string branch_name(std::string_view program, Side s, std::string_view stamp) {
    std::string p;
    for (const char c : program) {
        const auto u = static_cast<unsigned char>(c);
        if (std::isalnum(u) != 0 && u < 0x80)
            p += static_cast<char>(std::tolower(u));
    }
    if (p.empty())
        p = "agent";
    return "pair/" + std::string(stamp) + "-" + p + (s == Side::kLeft ? "-a" : "-b");
}

std::string stamp_of(int month, int day, int hour, int minute) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "%02d%02d-%02d%02d", month, day, hour, minute);
    return buf;
}

namespace {
std::string lower(std::string_view s) {
    std::string out;
    for (const char c : s)
        out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

// The first profile whose id or name holds `word` (case aside).
std::string find_profile(const std::vector<std::pair<std::string, std::string>>& profiles,
                         std::string_view word) {
    for (const auto& [id, name] : profiles)
        if (lower(id).find(word) != std::string::npos || lower(name).find(word) != std::string::npos)
            return id;
    return {};
}
}  // namespace

std::pair<std::string, std::string> default_profiles(
    const std::vector<std::pair<std::string, std::string>>& profiles) {
    if (profiles.empty())
        return {};
    const std::string claude = find_profile(profiles, "claude");
    const std::string codex = find_profile(profiles, "codex");
    if (!claude.empty() && !codex.empty())
        return {claude, codex};
    if (!claude.empty())
        return {claude, claude};
    if (!codex.empty())
        return {codex, codex};
    return {profiles.front().first, profiles.size() > 1 ? profiles[1].first : profiles.front().first};
}

std::string session_name(std::string_view profile_name, Side s) {
    return std::string(profile_name) + (s == Side::kLeft ? " (A)" : " (B)");
}

// ---------------------------------------------------------------- commands
std::optional<Target> parse_target(std::string_view arg) {
    if (arg.empty() || arg == "both")
        return Target::kBoth;
    if (arg == "left")
        return Target::kLeft;
    if (arg == "right")
        return Target::kRight;
    return std::nullopt;
}

const std::vector<std::string>& command_names() {
    static const std::vector<std::string> k = {"pair.toggle", "pair.start",  "pair.send",  "pair.prompt",
                                               "pair.swap",   "pair.repair", "pair.close", "pair.keep",
                                               "pair.review", "pair.dismiss"};
    return k;
}

}  // namespace agentdesk::ui::pair
