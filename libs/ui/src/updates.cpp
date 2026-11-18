// Port of herdr-app src/update.rs (Apache-2.0, commit 321c9a8); line numbers below are that
// file's unless another is named.
#include "agentdesk/ui/updates.hpp"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <condition_variable>
#include <fstream>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

#include <bcrypt.h>
#include <nlohmann/json.hpp>

#include "agentdesk/protocol/frame.hpp"
#include "agentdesk/ui/telemetry.hpp"
#include "system_process.hpp"

namespace agentdesk::ui::updates {

namespace fs = std::filesystem;
using Json = nlohmann::json;

namespace {

// update.rs 13-15, hc.rs 207: a newer core is offered only within this app's generation. Our
// agent-host's generation is the pipe protocol both programs speak (hello v=1).
constexpr std::uint64_t kGeneration = static_cast<std::uint64_t>(protocol::kProtocolVersion);

// 76-101. Kept on the heap and never freed: the checker thread outlives main's statics.
struct Shared {
    Config config;
    std::mutex mutex;
    std::optional<std::string> ready;  // READY
    CoreInfo core;                     // CORE
    std::optional<std::uint64_t> checked;
    std::mutex wake_mutex;
    std::condition_variable wake_cv;
    bool woken = false;
    std::atomic<bool> running{false};
    std::atomic<bool> checking{false};
    bool cleanup_old = false;  // after_update: .old goes once agent-host is the new one
};
Shared& g() {
    static Shared* shared = new Shared();
    return *shared;
}

std::string trim(std::string_view s) {
    const auto a = s.find_first_not_of(" \t\r\n");
    if (a == std::string_view::npos)
        return {};
    return std::string(s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1));
}

std::string u8(const fs::path& p) {
    return detail::to_utf8(p.native());
}

std::optional<std::wstring> env(const wchar_t* name) {
    const DWORD n = ::GetEnvironmentVariableW(name, nullptr, 0);
    if (n == 0)
        return std::nullopt;
    std::wstring v(n, L'\0');
    v.resize(::GetEnvironmentVariableW(name, v.data(), n));
    return v;
}

// 72-74: herdr falls back to releases.herdr.dev. AgentDesk has no server of its own.
std::optional<std::string> releases() {
    if (auto v = env(L"AGENTDESK_RELEASES_URL"); v && !v->empty())
        return detail::to_utf8(*v);
    return std::nullopt;
}

// 47-48: the folder inside a release archive. herdr's tarball holds "herdr-app"; AgentDesk's
// zip holds AgentDesk-<version>-win-x64 (tools/package/package.cpp 83).
std::wstring packed(const std::string& version) {
    return L"AgentDesk-" + detail::to_wide(version) + L"-win-x64";
}

// 384-390
fs::path exe_in(const fs::path& app) {
    return app / L"agent-ui.exe";
}

// 581-584: left by swap: the next start knows it follows an update.
fs::path marker() {
    return g().config.data_dir / L".updated";
}

std::uint64_t unix_now() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
            .count());
}

// 375-382
bool run(const fs::path& program, const std::vector<std::wstring>& args, std::string* error) {
    const auto r = detail::run_process(program, args);
    if (r.success())
        return true;
    *error = "\"" + u8(program) + "\": " + (r.started ? trim(r.err) : std::string("cannot start"));
    return false;
}

// 367-373
bool curl_text(const std::string& url, std::string* text, std::string* error) {
    const auto r =
        detail::run_process(system(L"curl"), {L"-fsSL", L"--max-time", L"30", detail::to_wide(url)});
    if (!r.started) {
        *error = "curl: cannot start";
        return false;
    }
    if (r.exit_code != 0) {
        *error = url + ": " + trim(r.err);
        return false;
    }
    *text = r.out;
    return true;
}

std::string read_file(const fs::path& p, bool* ok) {
    std::ifstream in(p, std::ios::binary);
    *ok = static_cast<bool>(in);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::string os_error(const std::error_code& ec) {
    return ec.message();
}

// 309-325: downloads, checks and unpacks a release into `work`; returns the app inside.
bool unpack(const std::string& url, const std::string& sha, const fs::path& work, const std::string& version,
            fs::path* app, std::string* error) {
    // herdr: app.tar.gz; ours is the portable zip, which System32's tar (bsdtar) also unpacks.
    const fs::path tarball = work / L"app.zip";
    if (!run(system(L"curl"),
             {L"-fsSL", L"--max-time", L"600", L"-o", tarball.native(), detail::to_wide(url)}, error))
        return false;
    bool ok = false;
    const std::string bytes = read_file(tarball, &ok);
    if (!ok) {
        *error = u8(tarball) + ": cannot read";
        return false;
    }
    std::string want = sha;
    for (auto& c : want)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (sha256(bytes) != want) {
        *error = "the download is damaged (checksum mismatch)";
        return false;
    }
    // herdr: tar -xzf; bsdtar picks the format itself (-z would refuse a zip).
    if (!run(system(L"tar"), {L"-xf", tarball.native(), L"-C", work.native()}, error))
        return false;
    *app = work / packed(version);
    return true;
}

// 221-224
struct CoreRelease {
    std::string version;
    std::optional<std::uint64_t> generation;
};
bool latest_core(CoreRelease* out, std::string* error) {
    const auto base = releases();
    if (!base) {
        *error = "no release server";
        return false;
    }
    std::string text;
    if (!curl_text(*base + "/core/latest.json", &text, error))
        return false;
    const Json j = Json::parse(text, nullptr, false);
    if (!j.is_object() || !j.contains("version") || !j["version"].is_string()) {
        *error = "core/latest.json: missing field `version`";
        return false;
    }
    out->version = j["version"].get<std::string>();
    if (const auto it = j.find("generation"); it != j.end() && it->is_number_unsigned())
        out->generation = it->get<std::uint64_t>();
    return true;
}

// 205-213: looks for a newer agent-host of this app's generation.
bool check_core(std::string* error) {
    std::optional<std::string> have;
    {
        std::lock_guard lock(g().mutex);
        have = g().core.version;
    }
    if (!have)
        return true;
    CoreRelease latest;
    if (!latest_core(&latest, error))
        return false;
    const bool same = latest.generation == kGeneration;
    std::lock_guard lock(g().mutex);
    g().core.ready =
        same && newer(latest.version, *have) ? std::optional<std::string>(latest.version) : std::nullopt;
    return true;
}

// 122-124: a release (never, here) or pointed at a server, and installed.
std::optional<fs::path> checks_app() {
    if (!releases())
        return std::nullopt;
    return root();
}

// 126-129
bool checks_core() {
    return releases().has_value();
}

// 151-155
void sleep_for(std::chrono::seconds d) {
    std::unique_lock lock(g().wake_mutex);
    g().wake_cv.wait_for(lock, d, [] { return g().woken; });
    g().woken = false;
}

void wake_ui() {
    if (g().config.wake)
        g().config.wake();
}

// 520-542 (Windows): PowerShell waits for this process, then starts the app again.
bool relaunch_expected(const std::optional<std::string>& expected) {
    const auto r = root();
    if (!r)
        return false;
    std::wstring exe = exe_in(*r).native();
    std::wstring quoted;
    for (wchar_t c : exe) {
        quoted += c;
        if (c == L'\'')
            quoted += L'\'';
    }
    const std::wstring script = L"Wait-Process -Id " + std::to_wstring(::GetCurrentProcessId()) +
                                L" -ErrorAction SilentlyContinue; Start-Process '" + quoted + L"'";
    // herdr runs `powershell` from PATH; Windows' own copy here.
    const fs::path powershell = [] {
        wchar_t sys[MAX_PATH]{};
        const UINT n = ::GetSystemDirectoryW(sys, MAX_PATH);
        return fs::path(std::wstring(sys, n)) / L"WindowsPowerShell" / L"v1.0" / L"powershell.exe";
    }();
    // 527: HERDR_APP_UPDATE_EXPECTED, ours AGENTDESK_UPDATE_EXPECTED.
    return detail::spawn_detached(powershell, {L"-NoProfile", L"-NonInteractive", L"-Command", script},
                                  expected ? L"AGENTDESK_UPDATE_EXPECTED" : L"",
                                  expected ? detail::to_wide(*expected) : L"");
}

}  // namespace

void configure(Config config) {
    g().config = std::move(config);
}

// ---- pure helpers

bool newer(std::string_view a, std::string_view b) {
    struct Parts {
        std::vector<std::uint64_t> core;
        std::optional<std::string> pre;
    };
    const auto parts = [](std::string_view v) {
        while (!v.empty() && v.front() == 'v')
            v.remove_prefix(1);
        Parts p;
        const auto dash = v.find('-');
        const std::string_view core = dash == std::string_view::npos ? v : v.substr(0, dash);
        if (dash != std::string_view::npos)
            p.pre = std::string(v.substr(dash + 1));
        for (std::size_t at = 0;;) {
            const auto dot = core.find('.', at);
            const std::string_view n =
                core.substr(at, dot == std::string_view::npos ? std::string_view::npos : dot - at);
            std::uint64_t value = 0;
            bool ok = !n.empty();
            for (char c : n) {
                if (c < '0' || c > '9') {
                    ok = false;
                    break;
                }
                value = value * 10 + static_cast<std::uint64_t>(c - '0');
            }
            p.core.push_back(ok ? value : 0);  // parse().unwrap_or(0)
            if (dot == std::string_view::npos)
                break;
            at = dot + 1;
        }
        return p;
    };
    const Parts pa = parts(a);
    const Parts pb = parts(b);
    if (pa.core == pb.core) {
        if (!pa.pre && pb.pre)
            return true;
        if (pa.pre && pb.pre)
            return *pa.pre > *pb.pre;
        return false;
    }
    // Vec<u64> comparison: lexicographic.
    return pa.core > pb.core;
}

std::optional<std::uint64_t> checked_at(bool succeeded, std::uint64_t now) {
    return succeeded ? std::optional<std::uint64_t>(now) : std::nullopt;
}

std::optional<std::string> startup_target(std::string_view running, const std::optional<std::string>& staged,
                                          const std::optional<std::string>& relaunched) {
    if (relaunched)
        return std::nullopt;
    if (staged && newer(*staged, running))
        return staged;
    return std::nullopt;
}

std::string sha256(std::string_view bytes) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    std::string out;
    if (::BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0)
        return out;
    unsigned char digest[32]{};
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (::BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) == 0) {
        std::size_t at = 0;
        do {
            const ULONG n = static_cast<ULONG>(std::min<std::size_t>(bytes.size() - at, 1u << 30));
            ::BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<char*>(bytes.data() + at)), n, 0);
            at += n;
        } while (at < bytes.size());
        ::BCryptFinishHash(hash, digest, sizeof digest, 0);
        ::BCryptDestroyHash(hash);
        static constexpr char kHex[] = "0123456789abcdef";
        for (unsigned char b : digest) {
            out += kHex[b >> 4];
            out += kHex[b & 15];
        }
    }
    ::BCryptCloseAlgorithmProvider(alg, 0);
    return out;
}

fs::path beside(const fs::path& root, std::wstring_view suffix) {
    std::wstring name = root.filename().native();
    name += suffix;
    return root.parent_path() / name;
}

fs::path system(std::wstring_view name) {
    wchar_t sys[MAX_PATH]{};
    const DWORD n = ::GetEnvironmentVariableW(L"SystemRoot", sys, MAX_PATH);
    if (n == 0 || n >= MAX_PATH)
        return fs::path(std::wstring(name) + L".exe");
    return fs::path(std::wstring(sys, n)) / L"System32" / (std::wstring(name) + L".exe");
}

std::string target() {
    return "windows-x86_64";
}

// 34-45: the release folder holds the binary and its icon; cargo's target/ doesn't. Ours: the
// zip's folder holds THIRD_PARTY_NOTICES.md (package.cpp 171); a build folder doesn't.
std::optional<fs::path> find_root(const fs::path& exe) {
    std::error_code ec;
    const fs::path real = fs::canonical(exe, ec);
    if (ec)
        return std::nullopt;
    const fs::path dir = real.parent_path();
    if (fs::is_regular_file(dir / L"THIRD_PARTY_NOTICES.md", ec))
        return dir;
    return std::nullopt;
}

// ---- steps

Found check_from(const fs::path& root, const std::string& releases_url, const std::string& running) {
    Found f;
    const std::string path = "app/latest.json";
    std::string text;
    if (!curl_text(releases_url + "/" + path, &text, &f.error))
        return f;
    const Json manifest = Json::parse(text, nullptr, false);
    if (manifest.is_discarded()) {
        f.error = path + ": not JSON";
        return f;
    }
    if (!manifest.is_object() || !manifest.contains("version") || !manifest["version"].is_string()) {
        f.error = path + ": no version";
        return f;
    }
    const std::string version = manifest["version"].get<std::string>();
    f.ok = true;
    if (!newer(version, running))
        return f;
    // 277-282: release notes are fetched for What's new there; AgentDesk's What's new ships in the
    // build, so there is nothing to fetch.
    const fs::path staged = beside(root, L".new");
    if (staged_version(root) == version) {
        f.version = version;
        return f;
    }
    const Json* entry = nullptr;
    if (const auto t = manifest.find("targets"); t != manifest.end() && t->is_object())
        if (const auto e = t->find(target()); e != t->end() && e->is_object())
            entry = &*e;
    if (entry == nullptr || !entry->contains("url") || !(*entry)["url"].is_string() ||
        !entry->contains("sha256") || !(*entry)["sha256"].is_string())
        return f;
    const std::string url = (*entry)["url"].get<std::string>();
    const std::string sha = (*entry)["sha256"].get<std::string>();
    const fs::path work = beside(root, L".download");
    std::error_code ec;
    fs::remove_all(work, ec);
    fs::create_directories(work, ec);
    if (ec) {
        f.ok = false;
        f.error = u8(work) + ": " + os_error(ec);
        return f;
    }
    std::string error;
    fs::path unpacked;
    bool done = unpack(url, sha, work, version, &unpacked, &error);
    if (done && version_of(unpacked) != version) {
        error = "the download doesn't run as " + version;
        done = false;
    }
    if (done) {
        const fs::path receipt = beside(root, L".new.json");
        fs::remove(receipt, ec);
        fs::remove_all(staged, ec);
        ec.clear();
        fs::rename(unpacked, staged, ec);
        if (ec) {
            error = u8(staged) + ": " + os_error(ec);
            done = false;
        } else {
            // Outside the folder: writing a receipt must not change what was verified.
            std::ofstream out(receipt, std::ios::binary | std::ios::trunc);
            out << Json{{"version", version}}.dump();
            if (!out) {
                error = u8(receipt) + ": cannot write";
                done = false;
            }
        }
    }
    fs::remove_all(work, ec);
    if (!done) {
        f.ok = false;
        f.error = error;
        return f;
    }
    f.version = version;
    return f;
}

std::optional<std::string> version_of(const fs::path& app) {
    const auto r = detail::run_process(exe_in(app), {L"--version"});
    if (!r.success())
        return std::nullopt;
    const std::string text = trim(r.out);
    // herdr-app's answer is "herdr-app 0.2.0"; ours "agent-ui 0.2.0".
    const std::string prefix = "agent-ui ";
    if (text.rfind(prefix, 0) != 0)
        return std::nullopt;
    return text.substr(prefix.size());
}

std::optional<std::string> staged_version(const fs::path& root) {
    const fs::path staged = beside(root, L".new");
    const fs::path receipt = beside(root, L".new.json");
    bool ok = false;
    const std::string text = read_file(receipt, &ok);
    if (!ok)
        return std::nullopt;
    const Json j = Json::parse(text, nullptr, false);
    if (!j.is_object() || !j.contains("version") || !j["version"].is_string())
        return std::nullopt;
    const std::string version = j["version"].get<std::string>();
    if (version_of(staged) != version) {
        // staged executable does not match its verified manifest; discarding it
        std::error_code ec;
        fs::remove(receipt, ec);
        fs::remove_all(staged, ec);
        return std::nullopt;
    }
    return version;
}

bool swap(const fs::path& root, std::string* error) {
    const fs::path staged = beside(root, L".new");
    const fs::path old = beside(root, L".old");
    std::error_code ec;
    fs::remove_all(old, ec);
    ec.clear();
    fs::rename(root, old, ec);
    if (ec) {
        *error = os_error(ec);  // cannot move the install aside
        return false;
    }
    fs::rename(staged, root, ec);
    if (ec) {
        *error = os_error(ec);  // cannot put the new version in place
        std::error_code rollback;
        fs::rename(old, root, rollback);
        return false;
    }
    fs::remove(beside(root, L".new.json"), ec);
    {
        std::lock_guard lock(g().mutex);
        g().ready.reset();
    }
    if (!g().config.data_dir.empty())
        std::ofstream(marker(), std::ios::binary | std::ios::trunc);
    return true;
}

// ---- the module

// 27-32: read once: after a swap this process's own file is elsewhere.
std::optional<fs::path> root() {
    static const std::optional<fs::path> r = find_root(g().config.exe);
    return r;
}

std::optional<std::string> ready() {
    std::lock_guard lock(g().mutex);
    return g().ready;
}

CoreInfo core() {
    std::lock_guard lock(g().mutex);
    return g().core;
}

void set_core(std::optional<std::string> version, std::optional<std::string> managed) {
    bool cleanup = false;
    {
        std::lock_guard lock(g().mutex);
        auto& c = g().core;
        if (version && c.ready && !newer(*c.ready, *version))
            c.ready.reset();
        c.version = std::move(version);
        c.managed = std::move(managed);
        if (c.managed)
            c.error.reset();
        // 607-610: the previous install goes once agent-host is the updated one.
        if (g().cleanup_old && c.version == g().config.version) {
            g().cleanup_old = false;
            cleanup = true;
        }
    }
    if (cleanup)
        if (const auto r = root()) {
            std::error_code ec;
            fs::remove_all(beside(*r, L".old"), ec);
        }
}

Status status() {
    Status s;
    s.app_version = g().config.version;
    s.app_ready = ready();
    s.app_updates = checks_app().has_value();
    s.core = core();
    {
        std::lock_guard lock(g().mutex);
        s.checked = g().checked;
    }
    s.checks = g().running.load();
    s.checking = g().checking.load();
    return s;
}

void check_now() {
    if (g().running.load())
        g().checking.store(true);
    {
        std::lock_guard lock(g().wake_mutex);
        g().woken = true;
    }
    g().wake_cv.notify_all();
}

void start() {
    const auto app = checks_app();
    const bool core_too = checks_core();
    if (!app && !core_too)
        return;  // a build from source: no updates
    g().running.store(true);
    g().checking.store(true);
    std::thread([app, core_too] {
        sleep_for(std::chrono::seconds(10));
        for (;;) {
            g().checking.store(true);
            wake_ui();
            auto wait = kEvery;
            bool succeeded = true;
            const auto base = releases();
            if (app && base) {
                const Found found = check_from(*app, *base, g().config.version);
                if (found.ok) {
                    if (found.version) {
                        std::lock_guard lock(g().mutex);
                        g().ready = found.version;
                    }
                } else {
                    telemetry::count("app_update_failed");
                    wait = kRetry;
                    succeeded = false;
                }
            }
            std::string error;
            if (core_too && !check_core(&error)) {
                wait = kRetry;
                succeeded = false;
            }
            {
                std::lock_guard lock(g().mutex);
                g().checked = checked_at(succeeded, unix_now());
            }
            g().checking.store(false);
            wake_ui();
            sleep_for(wait);
        }
    }).detach();
}

void leave_install_folder() {
    const auto r = root();
    if (!r)
        return;
    std::error_code ec;
    const fs::path cwd = fs::current_path(ec);
    if (ec)
        return;
    const fs::path real = fs::canonical(cwd, ec);
    if (ec)
        return;
    // starts_with on path components
    auto a = real.begin();
    auto b = r->begin();
    for (; b != r->end(); ++a, ++b)
        if (a == real.end() || _wcsicmp(a->c_str(), b->c_str()) != 0)
            return;
    const auto home = env(L"USERPROFILE");
    if (home)
        fs::current_path(*home, ec);
}

bool apply_on_startup() {
    const auto expected = env(L"AGENTDESK_UPDATE_EXPECTED");
    if (expected) {
        // Do not leak the guard into agent-host's environment and every terminal it starts.
        ::SetEnvironmentVariableW(L"AGENTDESK_UPDATE_EXPECTED", nullptr);
        return false;  // relaunched as this version, or not: never retried
    }
    const auto r = checks_app();
    if (!r)
        return false;
    const auto staged = staged_version(*r);
    const auto version = startup_target(g().config.version, staged, std::nullopt);
    if (!version)
        return false;
    std::string error;
    if (!swap(*r, &error))
        return false;
    return relaunch_expected(version);
}

bool apply(std::string* error) {
    const auto r = root();
    if (!r)
        return false;
    const auto staged = staged_version(*r);
    if (ready() != staged || !staged || !newer(*staged, g().config.version))
        return false;
    return swap(*r, error);
}

void relaunch() {
    relaunch_expected(std::nullopt);
}

void after_update() {
    if (g().config.data_dir.empty())
        return;
    std::error_code ec;
    if (!fs::remove(marker(), ec))
        return;
    // 591-621: herdr brings herdr-core up to date (`hc update`), then removes .old. agent-host
    // came in the same zip: it is up to date once the running one is this version (set_core).
    bool now = false;
    {
        std::lock_guard lock(g().mutex);
        if (g().core.version == g().config.version)
            now = true;
        else
            g().cleanup_old = true;
    }
    if (now)
        if (const auto r = root())
            fs::remove_all(beside(*r, L".old"), ec);
}

}  // namespace agentdesk::ui::updates
