// Usage telemetry: the worker thread that keeps the day's counts on disk, freezes finished days
// into reports and sends them, plus the buckets and name tables that keep reports anonymous.
// Portions adapted from herdr-app (Apache-2.0) and modified; see THIRD_PARTY_NOTICES.md.
#include "agentdesk/ui/telemetry.hpp"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <fstream>
#include <mutex>
#include <thread>
#include <utility>

#include <bcrypt.h>

#include "system_process.hpp"

namespace agentdesk::ui::telemetry {

namespace fs = std::filesystem;
using SteadyClock = std::chrono::steady_clock;
using detail::lower;
using detail::read_file;
using detail::trim;
using detail::unix_now;

namespace {

// How often the thread looks at the clock, counts time and sends.
constexpr auto kTick = std::chrono::seconds(30);
// Events waiting for the thread; beyond this, new ones are dropped rather than waited on.
constexpr std::size_t kQueueLimit = 512;
// HTTP answers that mean the server will never accept this report.
constexpr int kRejected[] = {400, 404, 410, 413, 422};

bool contains(const std::vector<std::string>& list, const std::string& v) {
    return std::find(list.begin(), list.end(), v) != list.end();
}

// Writes a .tmp beside `path`, then moves it over `path`, so readers never see half a file.
bool write_atomic(const fs::path& path, const std::string& text) {
    fs::path tmp = path;
    tmp.replace_extension(L".tmp");
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out)
            return false;
        out << text;
        if (!out)
            return false;
    }
    return ::MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
}

// 64 random bits from the OS's RNG, for the install id and send jitter.
std::uint64_t random_u64() {
    std::uint64_t v = 0;
    ::BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(&v), sizeof v, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    return v;
}

// Days since 1970-01-01 UTC.
std::int64_t today_index() {
    return static_cast<std::int64_t>(unix_now() / 86'400);
}

// An environment variable that is set and not blank.
std::optional<std::string> set(const wchar_t* name) {
    auto v = detail::env_var(name);
    if (!v)
        return std::nullopt;
    std::string s = detail::to_utf8(*v);
    if (trim(s).empty())
        return std::nullopt;
    return s;
}

// Why it's off for this run, whatever the setting says: DO_NOT_TRACK, AGENTDESK_TELEMETRY=0, or
// a CI machine.
std::optional<std::string> read_env_off() {
    const auto truthy = [](const std::string& v) {
        const std::string t = lower(trim(v));
        return t == "1" || t == "true" || t == "yes";
    };
    if (const auto v = set(L"DO_NOT_TRACK"); v && truthy(*v))
        return "DO_NOT_TRACK";
    if (const auto v = set(L"AGENTDESK_TELEMETRY")) {
        const std::string t = lower(trim(*v));
        if (t == "0" || t == "false" || t == "off" || t == "no")
            return "AGENTDESK_TELEMETRY";
    }
    for (const wchar_t* k : {L"CI", L"GITHUB_ACTIONS", L"GITLAB_CI", L"CIRCLECI", L"BUILDKITE",
                             L"JENKINS_URL", L"TEAMCITY_VERSION"})
        if (set(k))
            return "CI";
    return std::nullopt;
}

// Read once per run.
const std::optional<std::string>& env_off() {
    static const std::optional<std::string> off = read_env_off();
    return off;
}

// Where reports go, if this build sends at all. AgentDesk has no server of its own, so only
// AGENTDESK_TELEMETRY_URL makes it send.
std::optional<std::string> endpoint() {
    if (auto url = detail::env_var(L"AGENTDESK_TELEMETRY_URL"); url && !url->empty())
        return detail::to_utf8(*url);
    return std::nullopt;
}

struct Event {
    enum class Kind { kObserve, kCommand, kCount, kStartup, kWake, kQuit } kind;
    Observation observation;
    std::string name;
    std::uint64_t ms = 0;
};

// The module's state. Kept on the heap and never freed: the thread may outlive main's statics at
// exit.
struct Shared {
    std::mutex queue_mutex;
    std::condition_variable queue_cv;
    std::deque<std::pair<std::uint64_t, Event>> queue;
    bool started = false;
    bool stop = false;
    bool done = false;  // the thread has returned
    std::atomic<std::uint64_t> epoch{0};
    std::atomic<bool> setting{false};
    std::atomic<bool> on{false};
    std::mutex status_mutex;
    Json status;  // null until published
    std::optional<SteadyClock::time_point> started_at;
    std::mutex clock_mutex;
    Config config;
};
Shared& g() {
    static Shared* shared = new Shared();
    return *shared;
}

// Hands an event to the thread; never waits (a full queue drops it).
void send(Event e) {
    std::lock_guard lock(g().queue_mutex);
    if (!g().started || g().stop || g().queue.size() >= kQueueLimit)
        return;
    g().queue.emplace_back(g().epoch.load(), std::move(e));
    g().queue_cv.notify_one();
}

fs::path dir() {
    return g().config.data_dir / L"telemetry";
}

// The random id (32 hex digits), made on first use and kept until telemetry is turned off.
std::string install_id() {
    const fs::path path = dir() / L"id";
    bool ok = false;
    const std::string id = read_file(path, &ok);
    if (ok && id.size() == 32 && std::all_of(id.begin(), id.end(), [](char c) {
            return std::isxdigit(static_cast<unsigned char>(c)) != 0;
        }))
        return id;
    char buf[33];
    std::snprintf(buf, sizeof buf, "%016llx%016llx", static_cast<unsigned long long>(random_u64()),
                  static_cast<unsigned long long>(random_u64()));
    std::ofstream(path, std::ios::binary | std::ios::trunc) << buf;
    return buf;
}

// POSTs one report with curl (20 s limit); the HTTP status, or none if it never got an answer.
// Turning telemetry off stops a request still in flight.
std::optional<int> post(const std::string& url, const std::string& body) {
    const auto r = detail::run_process(
        detail::system32_exe(L"curl"),
        {L"-sS", L"--max-time", L"20", L"-X", L"POST", L"-H", L"content-type: application/json",
         L"--data-binary", L"@-", L"-o", L"NUL", L"-w", L"%{http_code}", detail::to_wide(url)},
        body, [] { return collecting(); });
    if (!r.started || !collecting())
        return std::nullopt;
    try {
        const int code = std::stoi(trim(r.out));
        if (code > 0)
            return code;
    } catch (...) {
    }
    return std::nullopt;
}

// ---- the thread
class Worker {
public:
    Worker()
        : epoch_(g().epoch.load()),
          setting_shown_(g().setting.load()),
          day_{date(today_index())},
          last_tick_(SteadyClock::now()),
          next_send_(SteadyClock::now() + std::chrono::seconds(20 + random_u64() % 100)) {}

    void run() {
        if (on())
            begin();
        else
            wipe();
        was_on_ = on();
        publish();
        for (;;) {
            std::optional<std::pair<std::uint64_t, Event>> next;
            {
                std::unique_lock lock(g().queue_mutex);
                g().queue_cv.wait_for(lock, kTick, [] { return g().stop || !g().queue.empty(); });
                if (!g().queue.empty()) {
                    next = std::move(g().queue.front());
                    g().queue.pop_front();
                } else if (g().stop) {
                    return;  // stop() was called and everything queued before it is handled
                }
            }
            follow_consent();
            if (next && next->first == epoch_)
                event(std::move(next->second));
            if (SteadyClock::now() - last_tick_ >= kTick)
                tick();
        }
    }

private:
    static bool on() { return collecting(); }

    // Starts or wipes when consent changed since it last looked.
    void follow_consent() {
        const std::uint64_t epoch = g().epoch.load();
        bool changed = false;
        if (epoch != epoch_) {
            epoch_ = epoch;
            wipe();
            was_on_ = false;
            changed = true;
        }
        const bool now_on = on();
        if (now_on != was_on_) {
            was_on_ = now_on;
            if (now_on)
                begin();
            else
                wipe();
            changed = true;
        }
        const bool setting = g().setting.load();
        if (changed || setting != setting_shown_) {
            setting_shown_ = setting;
            publish();
        }
    }

    // Off: nothing kept, nothing sent. On again starts fresh, with a new id.
    void wipe() {
        std::error_code ec;
        fs::remove_all(dir(), ec);
        day_ = Day{date(today_index())};
        install_.reset();
        dirty_ = false;
    }

    // Picks up the day so far, counts this start, and notes an unclean exit (the `running`
    // marker of the previous run is still there).
    void begin() {
        const fs::path d = dir();
        std::error_code ec;
        fs::create_directories(d / L"queue", ec);
        install_ = install_id();
        bool ok = false;
        const std::string text = read_file(d / L"today.json", &ok);
        if (ok) {
            const Json j = Json::parse(text, nullptr, false);
            if (j.is_object()) {
                Day saved = Day::from_json(j);
                if (saved.day == day_.day)
                    day_ = std::move(saved);
                else
                    freeze(saved);
            }
        }
        if (fs::exists(d / L"running", ec))
            ++day_.unclean_exits;
        std::ofstream(d / L"running", std::ios::binary | std::ios::trunc);
        ++day_.startups;
        dirty_ = true;
        save();
    }

    // Folds one event into the day.
    void event(Event e) {
        if (!on() || !was_on_)
            return;
        // An event after midnight belongs to the new day.
        roll();
        switch (e.kind) {
            case Event::Kind::kObserve: {
                foreground_ = e.observation.foreground;
                day_.observe(std::move(e.observation), g().config.shipped_layouts, g().config.shipped_themes);
                if (g().config.core_version)
                    if (auto core = g().config.core_version())
                        day_.core = std::move(core);
                break;
            }
            case Event::Kind::kCommand:
                day_.commands.insert(e.name);
                break;
            case Event::Kind::kCount:
                ++day_.events[e.name];
                break;
            case Event::Kind::kStartup:
                if (day_.startup_ms.size() < 50)
                    day_.startup_ms.push_back(e.ms);
                break;
            case Event::Kind::kWake:
                return;
            case Event::Kind::kQuit: {
                dirty_ = true;
                save();
                std::error_code ec;
                fs::remove(dir() / L"running", ec);
                return;
            }
        }
        dirty_ = true;
    }

    // A new UTC day: the finished one is frozen and the new one starts.
    void roll() {
        const std::string today = date(today_index());
        if (day_.day != today) {
            Day done = std::exchange(day_, Day{today});
            // The new day is on disk before the old one can be sent, so a restart never finds
            // the old one again.
            dirty_ = true;
            save();
            freeze(done);
        }
    }

    // Every kTick: counts open and foreground time, saves, sends and republishes.
    void tick() {
        const auto gap = SteadyClock::now() - last_tick_;
        last_tick_ = SteadyClock::now();
        if (!on())
            return;
        // A long gap is a suspended machine, not use.
        if (gap < kTick * 4) {
            const auto secs =
                static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(gap).count());
            day_.open_secs += secs;
            if (foreground_)
                day_.foreground_secs += secs;
            dirty_ = true;
        }
        roll();
        save();
        send_queued();
        publish();
    }

    void save() {
        if (dirty_ && write_atomic(dir() / L"today.json", day_.to_json().dump()))
            dirty_ = false;
    }

    // A finished day becomes a report that never changes (retries send the same
    // bytes). Each day is frozen once: `frozen` holds the latest.
    void freeze(const Day& day) const {
        if (!install_)
            return;
        const fs::path marker = dir() / L"frozen";
        const std::string latest = trim(read_file(marker));
        if (day.day.empty() || day.day <= latest)
            return;
        if (write_atomic(marker, day.day))
            write_atomic(dir() / L"queue" / (detail::to_wide(day.day) + L".json"),
                         day.report(*install_, g().config).dump());
    }

    // Reports waiting, oldest first; ones older than kKeepDays are deleted.
    [[nodiscard]] std::vector<std::pair<fs::path, std::string>> queued() const {
        const std::string oldest = date(today_index() - kKeepDays);
        std::vector<std::pair<fs::path, std::string>> out;
        std::error_code ec;
        for (const auto& e : fs::directory_iterator(dir() / L"queue", ec)) {
            const fs::path path = e.path();
            const std::string day = detail::to_utf8(path.stem().native());
            if (day < oldest) {
                std::error_code rm;
                fs::remove(path, rm);
                continue;
            }
            out.emplace_back(path, day);
        }
        std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.second < b.second; });
        return out;
    }

    // Sends what is queued. Consecutive failures back off 2, 4, 8, 16, then 24 hours, plus up to
    // 10 minutes of jitter.
    void send_queued() {
        const auto url = endpoint();
        if (!url)
            return;
        if (SteadyClock::now() < next_send_)
            return;
        for (const auto& [path, day] : queued()) {
            if (!on())
                return;
            bool ok = false;
            const std::string body = read_file(path, &ok);
            if (!ok)
                continue;
            const auto code = post(*url, body);
            if (code && *code >= 200 && *code < 300) {
                const Json report = Json::parse(body, nullptr, false);
                const Json sent = {{"at", unix_now()}, {"report", report.is_discarded() ? Json() : report}};
                write_atomic(dir() / L"last-sent.json", sent.dump());
                std::error_code ec;
                fs::remove(path, ec);
                failures_ = 0;
            } else if (code &&
                       std::find(std::begin(kRejected), std::end(kRejected), *code) != std::end(kRejected)) {
                // The server will never take this one (a bad or too old report).
                std::error_code ec;
                fs::remove(path, ec);
            } else {
                ++failures_;
                const std::uint64_t hours = std::uint64_t{1} << std::min<std::uint32_t>(failures_, 5);
                next_send_ =
                    SteadyClock::now() +
                    std::chrono::seconds(std::min<std::uint64_t>(hours, 24) * 3600 + random_u64() % 600);
                return;
            }
        }
    }

    // Refreshes what the settings screen shows; redraws only if it changed.
    void publish() const {
        const bool now_on = on();
        Json queued_reports = Json::array();
        if (now_on) {
            for (const auto& [p, day] : queued()) {
                bool ok = false;
                const Json r = Json::parse(read_file(p, &ok), nullptr, false);
                if (ok && !r.is_discarded())
                    queued_reports.push_back(r);
            }
        }
        Json last;
        if (now_on) {
            bool ok = false;
            const std::string text = read_file(dir() / L"last-sent.json", &ok);
            if (ok) {
                const Json j = Json::parse(text, nullptr, false);
                if (!j.is_discarded())
                    last = j;
            }
        }
        const auto url = endpoint();
        Json status = {
            {"enabled", now_on},
            {"setting", g().setting.load()},
            {"off_because", env_off() ? Json(*env_off()) : Json()},
            {"sends", url.has_value()},
            {"endpoint", url ? Json(*url) : Json()},
            {"draft", install_ && now_on ? day_.report(*install_, g().config) : Json()},
            {"queued", queued_reports},
            {"last_sent", last},
        };
        bool changed = false;
        {
            std::lock_guard lock(g().status_mutex);
            if (g().status != status) {
                g().status = std::move(status);
                changed = true;
            }
        }
        if (changed && g().config.refresh)
            g().config.refresh();
    }

    bool was_on_ = false;
    std::uint64_t epoch_;
    bool setting_shown_;
    Day day_;
    std::optional<std::string> install_;
    bool foreground_ = true;
    SteadyClock::time_point last_tick_;
    bool dirty_ = false;
    SteadyClock::time_point next_send_;
    std::uint32_t failures_ = 0;
};

// The program names a report may contain: each key of the shipped programs table and the other
// names it matches. Anything else is reported as "other".
struct ProgramNames {
    const char* key;
    std::vector<const char*> also;
};
const std::vector<ProgramNames>& programs() {
    static const std::vector<ProgramNames> table = {
        {"claude", {"claude-code"}},
        {"codex", {}},
        {"pi", {}},
        {"pig", {}},
        {"opencode", {}},
        {"cursor", {"agent", "cursor-agent"}},
        {"copilot", {"github-copilot"}},
        {"amp", {}},
        {"grok", {"grok-build"}},
        {"dsh-tui", {}},
        {"empryo", {}},
        {"goose", {}},
        {"antigravity", {"agy"}},
        {"kimi", {"kimi-code", "kimicodecli"}},
        {"kiro", {"kiro-cli"}},
        {"hermes", {}},
        {"mastracode", {}},
        {"qoder", {"qodercli"}},
        {"muse", {}},
        {"crush", {}},
        {"command-code", {}},
        {"prime-agent", {}},
        {"gemini", {}},
        {"ollama", {}},
        {"omp", {"oh-my-pi"}},
        {"agent", {}},
        {"shell", {}},
        {"program", {}},
        {"vim", {"vi", "view"}},
        {"nvim", {}},
        {"hx", {"helix"}},
        {"emacs", {}},
        {"nano", {}},
        {"less", {"more", "man", "bat"}},
        {"lazygit", {"gitui", "tig"}},
        {"git", {}},
        {"gh", {}},
        {"glab", {}},
        {"htop", {"btop", "top", "btm", "glances"}},
        {"ssh", {"mosh", "et"}},
        {"tmux", {"zellij", "screen"}},
        {"rg", {"grep", "fzf", "fd", "find"}},
        {"curl", {"wget", "http", "httpie", "xh"}},
        {"ffmpeg", {}},
        {"make", {"just", "ninja"}},
        {"cmake", {}},
        {"jupyter", {}},
        {"node", {}},
        {"bun", {"bunx"}},
        {"deno", {}},
        {"python", {"python3", "ipython", "pip", "pip3"}},
        {"uv", {"uvx"}},
        {"poetry", {}},
        {"ruby", {"irb", "bundle", "gem", "rails"}},
        {"go", {}},
        {"cargo", {"rustc", "rustup"}},
        {"java", {"javac", "jshell"}},
        {"gradle", {"gradlew"}},
        {"mvn", {"mvnw"}},
        {"dotnet", {}},
        {"php", {"artisan"}},
        {"composer", {}},
        {"iex", {"elixir", "mix"}},
        {"erl", {}},
        {"ghci", {"ghc", "stack", "cabal"}},
        {"ocaml", {"utop", "dune", "opam"}},
        {"zig", {}},
        {"swift", {}},
        {"kotlin", {"kotlinc"}},
        {"scala", {"sbt"}},
        {"lua", {"luajit"}},
        {"perl", {}},
        {"R", {"Rscript"}},
        {"julia", {}},
        {"dart", {}},
        {"flutter", {}},
        {"tsc", {"tsx", "ts-node"}},
        {"npm", {"npx"}},
        {"pnpm", {}},
        {"yarn", {}},
        {"vite", {}},
        {"next", {}},
        {"astro", {}},
        {"nuxt", {"nuxi"}},
        {"turbo", {}},
        {"webpack", {}},
        {"esbuild", {}},
        {"vitest", {}},
        {"jest", {}},
        {"pytest", {}},
        {"eslint", {}},
        {"prettier", {}},
        {"biome", {}},
        {"ruff", {}},
        {"expo", {}},
        {"tauri", {}},
        {"electron", {}},
        {"prisma", {}},
        {"supabase", {}},
        {"django-admin", {}},
        {"flask", {}},
        {"fastapi", {}},
        {"psql", {"postgres"}},
        {"mysql", {"mariadb"}},
        {"sqlite3", {"sqlite"}},
        {"redis-cli", {"redis-server"}},
        {"mongosh", {"mongo"}},
        {"docker", {"docker-compose", "lazydocker"}},
        {"podman", {}},
        {"kubectl", {"k9s", "minikube", "kind"}},
        {"helm", {}},
        {"terraform", {"tofu"}},
        {"ansible", {"ansible-playbook"}},
        {"gcloud", {}},
        {"vercel", {}},
        {"netlify", {}},
        {"wrangler", {}},
        {"fly", {"flyctl"}},
        {"caddy", {}},
        {"nginx", {}},
        {"nix", {"nix-shell", "nixos-rebuild"}},
        {"brew", {}},
        {"apt", {"apt-get", "dpkg"}},
        {"dnf", {"rpm-ostree"}},
        {"pacman", {"yay", "paru"}},
    };
    return table;
}

}  // namespace

// ---- names and buckets

std::string agent_name(const std::optional<std::string>& app) {
    static const std::map<std::string, std::string> names = [] {
        std::map<std::string, std::string> m;
        // Keys in sorted order; each key's aliases first, then the key itself, so a later key
        // that names an earlier key's alias takes it over.
        std::vector<const ProgramNames*> sorted;
        for (const auto& p : programs())
            sorted.push_back(&p);
        std::sort(sorted.begin(), sorted.end(),
                  [](const ProgramNames* a, const ProgramNames* b) { return std::string(a->key) < b->key; });
        for (const auto* p : sorted) {
            for (const char* alias : p->also)
                m[lower(alias)] = p->key;
            m[lower(p->key)] = p->key;
        }
        // Cursor's CLI is `agent`; keep that alias over the generic `agent` key.
        for (const auto& p : programs())
            if (std::string(p.key) == "cursor")
                for (const char* alias : p.also)
                    m[lower(alias)] = "cursor";
        return m;
    }();
    const std::string a = app ? trim(*app) : std::string();
    if (a.empty())
        return "unknown";
    const auto it = names.find(lower(a));
    return it == names.end() ? std::string("other") : it->second;
}

// Counts as coarse buckets: 0, 1, 2-5, 6-20, 21+.
std::string bucket(std::size_t n) {
    if (n == 0)
        return "0";
    if (n == 1)
        return "1";
    if (n <= 5)
        return "2-5";
    if (n <= 20)
        return "6-20";
    return "21+";
}

// Durations as coarse buckets, in whole minutes.
std::string time_bucket(std::uint64_t secs) {
    const std::uint64_t m = secs / 60;
    if (m == 0)
        return "0";
    if (m <= 14)
        return "<15m";
    if (m <= 59)
        return "15m-1h";
    if (m <= 239)
        return "1-4h";
    if (m <= 479)
        return "4-8h";
    return "8h+";
}

// Startup time buckets.
std::string startup_bucket(std::uint64_t ms) {
    if (ms <= 499)
        return "<0.5s";
    if (ms <= 999)
        return "0.5-1s";
    if (ms <= 1999)
        return "1-2s";
    if (ms <= 4999)
        return "2-5s";
    return "5s+";
}

// Only release-shaped versions pass: three numbers of up to four digits, optionally with an
// alpha/beta/rc/preview label and a number. A program-name prefix (below) and a leading "v"
// are dropped.
std::string version(std::string_view in) {
    std::string v = trim(in);
    if (v.rfind("hc ", 0) == 0)
        v = v.substr(3);
    while (!v.empty() && v.front() == 'v')
        v.erase(v.begin());
    const auto number = [](std::string_view s) {
        return !s.empty() && s.size() <= 4 &&
               std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; });
    };
    const auto dash = v.find('-');
    const std::string core = dash == std::string::npos ? v : v.substr(0, dash);
    std::vector<std::string> parts;
    for (std::size_t p = 0;;) {
        const auto q = core.find('.', p);
        parts.push_back(core.substr(p, q == std::string::npos ? std::string::npos : q - p));
        if (q == std::string::npos)
            break;
        p = q + 1;
    }
    bool pre_ok = true;
    if (dash != std::string::npos) {
        const std::string pre = v.substr(dash + 1);
        const auto dot = pre.find('.');
        const std::string label = dot == std::string::npos ? pre : pre.substr(0, dot);
        pre_ok = (label == "alpha" || label == "beta" || label == "rc" || label == "preview") &&
                 (dot == std::string::npos || number(std::string_view(pre).substr(dot + 1)));
    }
    if (parts.size() == 3 &&
        std::all_of(parts.begin(), parts.end(), [&](const std::string& p) { return number(p); }) && pre_ok)
        return v;
    return "other";
}

std::string date(std::int64_t days) {
    const std::int64_t z = days + 719'468;
    const std::int64_t era = (z >= 0 ? z : z - 146'096) / 146'097;
    const std::int64_t doe = z - era * 146'097;
    const std::int64_t yoe = (doe - doe / 1460 + doe / 36'524 - doe / 146'096) / 365;
    const std::int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const std::int64_t mp = (5 * doy + 2) / 153;
    const std::int64_t d = doy - (153 * mp + 2) / 5 + 1;
    const std::int64_t m = mp < 10 ? mp + 3 : mp - 9;
    const std::int64_t y = yoe + era * 400 + (m <= 2 ? 1 : 0);
    char buf[32];
    std::snprintf(buf, sizeof buf, "%04lld-%02lld-%02lld", static_cast<long long>(y),
                  static_cast<long long>(m), static_cast<long long>(d));
    return buf;
}

// ---- the day

// Folds an observation into the day: maxima for counts, sets for names; unknown layouts count
// as "custom" and frame mods are left out.
void Day::observe(Observation o, const std::vector<std::string>& layout_names,
                  const std::vector<std::string>& themes) {
    for (const auto& a : o.agents)
        agents.insert(agent_name(a));
    agents_open = std::max(agents_open, o.agents.size());
    agents_working = std::max(agents_working, o.agents_working);
    spaces = std::max(spaces, o.spaces);
    tabs = std::max(tabs, o.tabs);
    terminals = std::max(terminals, o.terminals);
    layouts.insert(contains(layout_names, o.layout) ? o.layout : std::string("custom"));
    for (auto& m : o.mods)
        if (m.rfind("frame-", 0) != 0)
            mods.insert(std::move(m));
    if (o.welcome && (*o.welcome == "finished" || *o.welcome == "skipped"))
        welcome = o.welcome;
    theme_custom = theme_custom || !contains(themes, o.theme);
}

// The keys are written in this order, and the report shows and is sent in that order.
Json Day::report(const std::string& install, const Config& config) const {
    std::vector<std::string> builtin;
    std::size_t edited = 0;
    std::size_t custom = mods.size();
    if (config.mod_kinds)
        std::tie(builtin, edited, custom) = config.mod_kinds(mods);
    std::vector<std::uint64_t> ms = startup_ms;
    std::sort(ms.begin(), ms.end());
    return Json{
        {"v", kVersion},
        {"install", install},
        {"day", day},
        {"app",
         {
             {"version", version(config.app_version)},
             {"os", "windows"},
             {"arch", "x86_64"},
             {"channel", "stable"},
         }},
        {"core", {{"version", core ? Json(version(*core)) : Json()}}},
        {"usage",
         {
             {"open", time_bucket(open_secs)},
             {"foreground", time_bucket(foreground_secs)},
             {"startups", std::min<std::uint32_t>(startups, 99)},
             {"startup", ms.empty() ? Json() : Json(startup_bucket(ms[ms.size() / 2]))},
             {"unclean_exits", std::min<std::uint32_t>(unclean_exits, 99)},
         }},
        {"agents", agents},
        {"agents_open", bucket(agents_open)},
        {"agents_working", bucket(agents_working)},
        {"spaces", bucket(spaces)},
        {"tabs", bucket(tabs)},
        {"terminals", bucket(terminals)},
        {"layouts", layouts},
        {"mods", {{"builtin", builtin}, {"builtin_edited", edited}, {"custom", custom}}},
        {"commands", commands},
        {"custom", {{"keys", config.keys_custom ? config.keys_custom() : false}, {"theme", theme_custom}}},
        {"welcome", welcome.value_or("none")},
        {"events", events},
    };
}

Json Day::to_json() const {
    return Json{{"day", day},
                {"open_secs", open_secs},
                {"foreground_secs", foreground_secs},
                {"startups", startups},
                {"startup_ms", startup_ms},
                {"unclean_exits", unclean_exits},
                {"agents", agents},
                {"agents_open", agents_open},
                {"agents_working", agents_working},
                {"spaces", spaces},
                {"tabs", tabs},
                {"terminals", terminals},
                {"layouts", layouts},
                {"mods", mods},
                {"commands", commands},
                {"theme_custom", theme_custom},
                {"core", core ? Json(*core) : Json()},
                {"welcome", welcome ? Json(*welcome) : Json()},
                {"events", events}};
}

// Anything missing or of the wrong kind keeps its default.
Day Day::from_json(const Json& j) {
    Day d;
    const auto str = [&](const char* k, std::string& into) {
        if (const auto it = j.find(k); it != j.end() && it->is_string())
            into = it->get<std::string>();
    };
    const auto num = [&](const char* k, auto& into) {
        if (const auto it = j.find(k); it != j.end() && it->is_number_unsigned())
            into = it->get<std::remove_reference_t<decltype(into)>>();
    };
    const auto strings = [&](const char* k, std::set<std::string>& into) {
        if (const auto it = j.find(k); it != j.end() && it->is_array())
            for (const auto& v : *it)
                if (v.is_string())
                    into.insert(v.get<std::string>());
    };
    str("day", d.day);
    num("open_secs", d.open_secs);
    num("foreground_secs", d.foreground_secs);
    num("startups", d.startups);
    if (const auto it = j.find("startup_ms"); it != j.end() && it->is_array())
        for (const auto& v : *it)
            if (v.is_number_unsigned())
                d.startup_ms.push_back(v.get<std::uint64_t>());
    num("unclean_exits", d.unclean_exits);
    strings("agents", d.agents);
    num("agents_open", d.agents_open);
    num("agents_working", d.agents_working);
    num("spaces", d.spaces);
    num("tabs", d.tabs);
    num("terminals", d.terminals);
    strings("layouts", d.layouts);
    strings("mods", d.mods);
    strings("commands", d.commands);
    if (const auto it = j.find("theme_custom"); it != j.end() && it->is_boolean())
        d.theme_custom = it->get<bool>();
    if (const auto it = j.find("core"); it != j.end() && it->is_string())
        d.core = it->get<std::string>();
    if (const auto it = j.find("welcome"); it != j.end() && it->is_string())
        d.welcome = it->get<std::string>();
    if (const auto it = j.find("events"); it != j.end() && it->is_object())
        for (const auto& [k, v] : it->items())
            if (v.is_number_unsigned())
                d.events[k] = v.get<std::uint32_t>();
    return d;
}

// ---- the module

bool collecting() {
    return g().on.load();
}

void observe(Observation o) {
    if (collecting())
        send(Event{Event::Kind::kObserve, std::move(o), {}, 0});
}

void command(std::string name) {
    if (collecting())
        send(Event{Event::Kind::kCommand, {}, std::move(name), 0});
}

void count(std::string name) {
    if (collecting())
        send(Event{Event::Kind::kCount, {}, std::move(name), 0});
}

void window_open() {
    std::optional<SteadyClock::time_point> t;
    {
        std::lock_guard lock(g().clock_mutex);
        t = g().started_at;
    }
    if (collecting() && t)
        send(Event{
            Event::Kind::kStartup,
            {},
            {},
            static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(SteadyClock::now() - *t).count())});
}

// Turning telemetry off moves to a new epoch: events from before are ignored by the thread.
void set_setting(bool on) {
    g().setting.store(on);
    const bool now = on && !env_off().has_value();
    if (g().on.exchange(now) && !now)
        g().epoch.fetch_add(1);
    send(Event{Event::Kind::kWake, {}, {}, 0});
}

Json status() {
    std::lock_guard lock(g().status_mutex);
    return g().status;
}

void quit() {
    if (collecting())
        send(Event{Event::Kind::kQuit, {}, {}, 0});
}

void clock() {
    std::lock_guard lock(g().clock_mutex);
    if (!g().started_at)
        g().started_at = SteadyClock::now();
}

void start(Config config, bool setting) {
    {
        std::lock_guard lock(g().queue_mutex);
        if (g().started)
            return;
        g().started = true;
    }
    g().config = std::move(config);
    g().setting.store(setting);
    g().on.store(setting && !env_off().has_value());
    std::thread([] {
        Worker().run();
        std::lock_guard lock(g().queue_mutex);
        g().done = true;
        g().queue_cv.notify_all();
    }).detach();
}

// What was already handed over (such as a quit) is handled first, waiting at most 250 ms so a
// quit never feels slow; after that the process may go.
void stop() {
    std::unique_lock lock(g().queue_mutex);
    if (!g().started)
        return;
    g().stop = true;
    g().queue_cv.notify_all();
    g().queue_cv.wait_for(lock, std::chrono::milliseconds(250), [] { return g().done; });
}

}  // namespace agentdesk::ui::telemetry
