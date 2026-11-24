// The app's self-updater and the agent-host update it starts (see updates.hpp for the flow).
// Portions adapted from herdr-app (Apache-2.0) and modified; see THIRD_PARTY_NOTICES.md.
#include "agentdesk/ui/updates.hpp"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <fstream>
#include <mutex>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "agentdesk/protocol/frame.hpp"
#include "agentdesk/ui/telemetry.hpp"
#include "system_process.hpp"
#include "updates_os.hpp"

namespace agentdesk::ui::updates {

namespace fs = std::filesystem;
using Json = nlohmann::json;
using detail::read_file;
using detail::trim;

namespace {

// A newer agent-host is offered only when it belongs to this app's generation, which is the pipe
// protocol version both programs speak (hello v=1).
constexpr std::uint64_t kGeneration = static_cast<std::uint64_t>(protocol::kProtocolVersion);

// Download limits handed to curl, in seconds: a manifest, and a release archive or agent-host.exe.
constexpr const wchar_t* kManifestTimeout = L"30";
constexpr const wchar_t* kDownloadTimeout = L"600";

// What `--version` prints before the version number.
constexpr std::string_view kUiPrefix = "agent-ui ";
constexpr std::string_view kHostPrefix = "agent-host ";

// The updater's state. Kept on the heap and never freed: the checker thread outlives main's
// statics.
struct Shared {
    Config config;
    std::mutex mutex;
    // Held while <install>.new is checked, staged or swapped in: the checker thread stages while
    // the UI thread may apply. Recursive: apply() holds it across staged_version() and swap().
    // Taken before `mutex`, never after.
    std::recursive_mutex stage_mutex;
    std::optional<std::string> ready;  // a staged app version waiting for a restart
    CoreInfo core;
    std::optional<std::uint64_t> checked;
    std::mutex wake_mutex;
    std::condition_variable wake_cv;
    bool woken = false;
    std::atomic<bool> running{false};
    std::atomic<bool> checking{false};
    bool cleanup_old = false;  // after_update: .old goes once agent-host is the new one
    std::wstring pipe;         // set_pipe: the agent-host this window updates
};
Shared& g() {
    static Shared* shared = new Shared();
    return *shared;
}

std::string u8(const fs::path& p) {
    return detail::to_utf8(p.wstring());
}

std::string os_error(const std::error_code& ec) {
    return ec.message();
}

// A string member of a JSON object; nullopt when `j` is not an object, or the member is missing
// or not a string.
std::optional<std::string> string_field(const Json& j, const char* key) {
    if (!j.is_object())
        return std::nullopt;
    const auto it = j.find(key);
    if (it == j.end() || !it->is_string())
        return std::nullopt;
    return it->get<std::string>();
}

// The manifest's entry for this machine (targets.<target()>), if it has one.
const Json* target_entry(const Json& manifest) {
    const auto targets = manifest.find("targets");
    if (targets == manifest.end() || !targets->is_object())
        return nullptr;
    const auto entry = targets->find(target());
    if (entry == targets->end() || !entry->is_object())
        return nullptr;
    return &*entry;
}

// "agent-host 0.2.0" -> "0.2.0"; anything without the prefix is kept as it is.
std::string without_host_prefix(const std::string& text) {
    return text.rfind(kHostPrefix, 0) == 0 ? text.substr(kHostPrefix.size()) : text;
}

// The version `<exe> --version` prints after `prefix`; nullopt when it fails or says otherwise.
std::optional<std::string> reported_version(const fs::path& exe, std::string_view prefix) {
    const auto r = detail::run_process(exe, {L"--version"});
    if (!r.success())
        return std::nullopt;
    const std::string text = trim(r.out);
    if (text.rfind(prefix, 0) != 0)
        return std::nullopt;
    return text.substr(prefix.size());
}

// The release server, only when AGENTDESK_RELEASES_URL names one: AgentDesk has no default server.
std::optional<std::string> releases() {
    if (auto v = detail::env_var(L"AGENTDESK_RELEASES_URL"); v && !v->empty())
        return detail::to_utf8(*v);
    return std::nullopt;
}

// The folder inside a release zip (tools/package names it this way).
std::wstring packed(const std::string& version) {
    return os::packed(version);
}

fs::path exe_in(const fs::path& app) {
    return os::ui_exe_in(app);
}

// Left by swap(): the next start knows it follows an update.
fs::path marker() {
    return g().config.data_dir / L".updated";
}

// Runs a tool to completion; on failure `error` says which tool and why.
bool run(const fs::path& program, const std::vector<std::wstring>& args, std::string* error) {
    const auto r = detail::run_process(program, args);
    if (r.success())
        return true;
    *error = "\"" + u8(program) + "\": " + (r.started ? trim(r.err) : std::string("cannot start"));
    return false;
}

bool curl_text(const std::string& url, std::string* text, std::string* error) {
    const auto r = detail::run_process(system(L"curl"),
                                       {L"-fsSL", L"--max-time", kManifestTimeout, detail::to_wide(url)});
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

bool curl_file(const std::string& url, const fs::path& to, std::string* error) {
    return run(system(L"curl"),
               {L"-fsSL", L"--max-time", kDownloadTimeout, L"-o", to.wstring(), detail::to_wide(url)}, error);
}

// The file's sha256 matches `expected` (any case). False also when it cannot be read.
bool checksum_matches(const fs::path& file, const std::string& expected) {
    bool ok = false;
    const std::string bytes = read_file(file, &ok);
    return ok && sha256(bytes) == detail::lower(expected);
}

// Downloads, checks and unpacks a release zip into `work`; `app` is the folder inside it.
bool unpack(const std::string& url, const std::string& sha, const fs::path& work, const std::string& version,
            fs::path* app, std::string* error) {
    // System32's tar (bsdtar) unpacks the portable zip as well.
    const fs::path archive = work / L"app.zip";
    if (!curl_file(url, archive, error))
        return false;
    bool ok = false;
    const std::string bytes = read_file(archive, &ok);
    if (!ok) {
        *error = u8(archive) + ": cannot read";
        return false;
    }
    if (sha256(bytes) != detail::lower(sha)) {
        *error = "the download is damaged (checksum mismatch)";
        return false;
    }
    // bsdtar picks the format itself; -z would refuse a zip.
    if (!run(system(L"tar"), {L"-xf", archive.wstring(), L"-C", work.wstring()}, error))
        return false;
    *app = work / packed(version);
    return true;
}

// core/latest.json: the newest agent-host, and where `agentctl update` downloads it for this
// machine (the same `targets` shape as app/latest.json).
struct CoreRelease {
    std::string version;
    std::optional<std::uint64_t> generation;
    std::string url;
    std::string sha256;
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
    const auto version = string_field(j, "version");
    if (!version) {
        *error = "core/latest.json: missing field `version`";
        return false;
    }
    out->version = *version;
    if (const auto it = j.find("generation"); it != j.end() && it->is_number_unsigned())
        out->generation = it->get<std::uint64_t>();
    if (const Json* entry = target_entry(j)) {
        if (auto url = string_field(*entry, "url"))
            out->url = std::move(*url);
        if (auto sha = string_field(*entry, "sha256"))
            out->sha256 = std::move(*sha);
    }
    return true;
}

// Looks for a newer agent-host of this app's generation; sets CoreInfo::ready.
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

// The install folder, when this copy updates itself: installed and pointed at a server.
std::optional<fs::path> checks_app() {
    if (!releases())
        return std::nullopt;
    return root();
}

bool checks_core() {
    return releases().has_value();
}

// Sleeps for `d`, or until check_now() wakes the checker.
void sleep_for(std::chrono::seconds d) {
    std::unique_lock lock(g().wake_mutex);
    g().wake_cv.wait_for(lock, d, [] { return g().woken; });
    g().woken = false;
}

void wake_ui() {
    if (g().config.wake)
        g().config.wake();
}

// Starts the app again once this process has exited (os::relaunch).
bool relaunch_expected(const std::optional<std::string>& expected) {
    const auto r = root();
    return r && os::relaunch(*r, expected);
}

// Moves a verified download from `unpacked` to <root>.new and writes its receipt.
bool put_stage(const fs::path& root, const fs::path& unpacked, const std::string& version,
               std::string* error) {
    // The download (minutes) ran unlocked in .download; only putting it in place as .new waits
    // for an apply() in progress.
    std::lock_guard stage(g().stage_mutex);
    const fs::path staged = beside(root, L".new");
    const fs::path receipt = beside(root, L".new.json");
    std::error_code ec;
    fs::remove(receipt, ec);
    fs::remove_all(staged, ec);
    ec.clear();
    fs::rename(unpacked, staged, ec);
    if (ec) {
        *error = u8(staged) + ": " + os_error(ec);
        return false;
    }
    // The receipt sits outside the folder: writing it must not change what was verified.
    std::ofstream out(receipt, std::ios::binary | std::ios::trunc);
    out << Json{{"version", version}}.dump();
    if (!out) {
        *error = u8(receipt) + ": cannot write";
        return false;
    }
    return true;
}

// Downloads `url` into <root>.download, checks it and stages it as `version`.
bool download_and_stage(const fs::path& root, const std::string& url, const std::string& sha,
                        const std::string& version, std::string* error) {
    const fs::path work = beside(root, L".download");
    std::error_code ec;
    fs::remove_all(work, ec);
    fs::create_directories(work, ec);
    if (ec) {
        *error = u8(work) + ": " + os_error(ec);
        return false;
    }
    fs::path unpacked;
    bool done = unpack(url, sha, work, version, &unpacked, error);
    if (done && version_of(unpacked) != version) {
        *error = "the download doesn't run as " + version;
        done = false;
    }
    if (done)
        done = put_stage(root, unpacked, version, error);
    fs::remove_all(work, ec);
    return done;
}

// `agentctl update --exe <file>`: an agent-host already on disk (the one the app's zip brought).
CoreStaged stage_core_from_file(const CoreStageRequest& req) {
    CoreStaged s;
    const fs::path& from = *req.from;
    const auto v = host_version_of(from);
    if (!v) {
        s.error = u8(from) + " is not an agent-host";
        return s;
    }
    s.ok = true;
    s.version = req.have;
    if (newer(*v, req.have) || (req.at_least && newer(*req.at_least, req.have))) {
        if (req.at_least && newer(*req.at_least, *v)) {
            s.ok = false;
            s.error = u8(from) + " is agent-host " + *v + ", older than " + *req.at_least;
            return s;
        }
        s.exe = from;
        s.version = *v;
    }
    return s;
}

// Downloads `latest` beside the running agent-host, checks it (sha256, `--version`), then puts it
// in its place. A running program file can be renamed but not replaced, so the running one goes
// to agent-host.exe.old. Fills `s.error` and returns false on failure, with nothing moved.
bool replace_host_exe(const fs::path& host_exe, const CoreRelease& latest, CoreStaged& s) {
    const fs::path staged = beside(host_exe, L".new");
    const fs::path old = beside(host_exe, L".old");
    std::error_code ec;
    fs::remove(staged, ec);
    if (!curl_file(latest.url, staged, &s.error))
        return false;
    if (!checksum_matches(staged, latest.sha256)) {
        fs::remove(staged, ec);
        s.error = "the download is damaged (checksum mismatch)";
        return false;
    }
    if (host_version_of(staged) != latest.version) {
        fs::remove(staged, ec);
        s.error = "the download doesn't run as " + latest.version;
        return false;
    }
    fs::remove(old, ec);
    if (fs::exists(old, ec)) {
        fs::remove(staged, ec);
        s.error = "an earlier agent-host still runs from " + u8(old);
        return false;
    }
    fs::rename(host_exe, old, ec);
    if (ec) {
        fs::remove(staged, ec);
        s.error = u8(host_exe) + ": " + os_error(ec);
        return false;
    }
    fs::rename(staged, host_exe, ec);
    if (ec) {
        s.error = u8(host_exe) + ": " + os_error(ec);
        std::error_code rollback;
        fs::rename(old, host_exe, rollback);
        return false;
    }
    return true;
}

// `agentctl update [--version X]`: the latest agent-host from the release server.
CoreStaged stage_core_from_release(const CoreStageRequest& req) {
    CoreStaged s;
    if (!releases()) {
        // A build without a release server looks for no updates.
        s.error = "no release server";
        s.hint = "set AGENTDESK_RELEASES_URL";
        return s;
    }
    CoreRelease latest;
    if (!latest_core(&latest, &s.error))
        return s;
    if (latest.generation != kGeneration) {
        s.error = "core/latest.json offers agent-host for another AgentDesk";
        return s;
    }
    s.ok = true;
    s.version = req.have;
    const std::string wanted = req.at_least.value_or(latest.version);
    if (!newer(latest.version, req.have) && !newer(wanted, req.have))
        return s;  // up to date
    s.ok = false;
    if (newer(wanted, latest.version)) {
        s.error = "agent-host " + wanted + " is not released";
        return s;
    }
    if (latest.url.empty() || latest.sha256.empty()) {
        s.error = "core/latest.json has no agent-host for " + target();
        return s;
    }
    if (!replace_host_exe(req.host_exe, latest, s))
        return s;
    s.ok = true;
    s.swapped = true;
    s.exe = req.host_exe;
    s.version = latest.version;
    return s;
}

}  // namespace

void configure(Config config) {
    g().config = std::move(config);
}

// ---- pure helpers

namespace {

// SemVer 11.4: dot-separated identifiers left to right; numeric ones compare numerically and
// rank below alphanumeric ones; with equal leading identifiers, more identifiers rank higher.
bool prerelease_greater(std::string_view a, std::string_view b) {
    const auto split = [](std::string_view s) {
        std::vector<std::string_view> ids;
        for (std::size_t at = 0;;) {
            const auto dot = s.find('.', at);
            ids.push_back(s.substr(at, dot == std::string_view::npos ? std::string_view::npos : dot - at));
            if (dot == std::string_view::npos)
                return ids;
            at = dot + 1;
        }
    };
    const auto numeric = [](std::string_view id) {
        return !id.empty() && std::all_of(id.begin(), id.end(), [](char c) { return c >= '0' && c <= '9'; });
    };
    const auto ia = split(a);
    const auto ib = split(b);
    for (std::size_t i = 0; i < ia.size() && i < ib.size(); ++i) {
        const std::string_view x = ia[i];
        const std::string_view y = ib[i];
        const bool nx = numeric(x);
        const bool ny = numeric(y);
        if (nx && ny) {
            // Compare as numbers of any length: strip leading zeros, then longer is larger.
            const auto strip_zeros = [](std::string_view n) {
                const auto first = n.find_first_not_of('0');
                return first == std::string_view::npos ? std::string_view() : n.substr(first);
            };
            const std::string_view tx = strip_zeros(x);
            const std::string_view ty = strip_zeros(y);
            if (tx.size() != ty.size())
                return tx.size() > ty.size();
            if (tx != ty)
                return tx > ty;
            continue;
        }
        if (nx != ny)
            return ny;  // the alphanumeric one is greater
        if (x != y)
            return x > y;  // ASCII order
    }
    return ia.size() > ib.size();
}

struct VersionParts {
    std::vector<std::uint64_t> core;  // major.minor.patch...; a part that is not a number is 0
    std::optional<std::string> pre;
};

VersionParts version_parts(std::string_view v) {
    while (!v.empty() && v.front() == 'v')
        v.remove_prefix(1);
    v = v.substr(0, v.find('+'));  // build metadata does not order versions (SemVer 10)
    VersionParts p;
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
        p.core.push_back(ok ? value : 0);
        if (dot == std::string_view::npos)
            break;
        at = dot + 1;
    }
    return p;
}

}  // namespace

bool newer(std::string_view a, std::string_view b) {
    const VersionParts pa = version_parts(a);
    const VersionParts pb = version_parts(b);
    if (pa.core == pb.core) {
        if (!pa.pre && pb.pre)
            return true;
        if (pa.pre && pb.pre)
            return prerelease_greater(*pa.pre, *pb.pre);
        return false;
    }
    return pa.core > pb.core;  // lexicographic, part by part
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

fs::path beside(const fs::path& root, std::wstring_view suffix) {
    std::wstring name = root.filename().wstring();
    name += suffix;
    return root.parent_path() / name;
}

fs::path system(std::wstring_view name) {
    return detail::system32_exe(name);
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
    const auto version = string_field(manifest, "version");
    if (!version) {
        f.error = path + ": no version";
        return f;
    }
    f.ok = true;
    if (!newer(*version, running))
        return f;
    // What's new ships inside the build, so there are no release notes to fetch.
    {
        std::lock_guard stage(g().stage_mutex);
        if (staged_version(root) == *version) {
            f.version = version;
            return f;
        }
    }
    const Json* entry = target_entry(manifest);
    const auto url = entry != nullptr ? string_field(*entry, "url") : std::nullopt;
    const auto sha = entry != nullptr ? string_field(*entry, "sha256") : std::nullopt;
    if (!url || !sha)
        return f;  // no build for this machine: nothing to stage
    if (std::string error; !download_and_stage(root, *url, *sha, *version, &error)) {
        f.ok = false;
        f.error = error;
        return f;
    }
    f.version = version;
    return f;
}

std::optional<std::string> version_of(const fs::path& app) {
    return reported_version(exe_in(app), kUiPrefix);
}

std::optional<std::string> staged_version(const fs::path& root) {
    const fs::path staged = beside(root, L".new");
    const fs::path receipt = beside(root, L".new.json");
    bool ok = false;
    const std::string text = read_file(receipt, &ok);
    if (!ok)
        return std::nullopt;
    const auto version = string_field(Json::parse(text, nullptr, false), "version");
    if (!version)
        return std::nullopt;
    if (version_of(staged) != *version) {
        // The staged executable does not match its verified receipt: discard both.
        std::error_code ec;
        fs::remove(receipt, ec);
        fs::remove_all(staged, ec);
        return std::nullopt;
    }
    return version;
}

bool swap(const fs::path& root, std::string* error) {
    std::lock_guard stage(g().stage_mutex);
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
        if (rollback) {
            // Neither version is in place: say where the old one is; callers do not relaunch.
            *error += "; the previous version could not be put back either (" + os_error(rollback) +
                      "): it is in " + u8(old);
        }
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

// Read once: after a swap this process's own file is somewhere else.
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
        // After an app update, the previous install goes once agent-host is the updated one.
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
        // The first look waits 10 s, so it never competes with the window opening.
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
                g().checked = checked_at(succeeded, detail::unix_now());
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
    // Is `real` inside the install? Compared component by component, ignoring case.
    auto a = real.begin();
    auto b = r->begin();
    for (; b != r->end(); ++a, ++b)
        if (a == real.end() || !os::same_component(*a, *b))
            return;
    const auto home = detail::home_folder();
    if (home)
        fs::current_path(*home, ec);
}

bool apply_on_startup() {
    const auto expected = detail::env_var(L"AGENTDESK_UPDATE_EXPECTED");
    if (expected) {
        // Do not leak the guard into agent-host's environment and every terminal it starts.
        detail::unset_env_var(L"AGENTDESK_UPDATE_EXPECTED");
        return false;  // relaunched as this version, or not: never retried
    }
    const auto r = checks_app();
    if (!r)
        return false;
    std::optional<std::string> version;
    {
        // The stage checked is the stage swapped in (the checker thread may be staging).
        std::lock_guard stage(g().stage_mutex);
        const auto staged = staged_version(*r);
        version = startup_target(g().config.version, staged, std::nullopt);
        if (!version)
            return false;
        std::string error;
        if (!swap(*r, &error))
            return false;  // never relaunched: the install may not be the new version
    }
    return relaunch_expected(version);
}

bool apply(std::string* error) {
    const auto r = root();
    if (!r)
        return false;
    std::lock_guard stage(g().stage_mutex);
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
    const auto r = root();
    if (!r)
        return;
    // The zip brought its own agent-host: the running one hands its terminals to that one
    // (`agentctl update --exe`), then .old goes. set_core also removes .old once agent-host is
    // this version.
    {
        std::lock_guard lock(g().mutex);
        g().cleanup_old = true;
        g().core.updating = true;
    }
    std::thread([root = *r] {
        std::string now;
        std::string error;
        const bool ok = run_core_update(
            {L"--exe", os::host_exe_in(root).wstring()},
            &now, &error);
        {
            std::lock_guard lock(g().mutex);
            auto& core = g().core;
            core.updating = false;
            if (ok) {
                std::error_code remove_error;
                fs::remove_all(beside(root, L".old"), remove_error);
                core.version = without_host_prefix(now);
                core.error.reset();
            } else {
                core.error = error;
            }
        }
        wake_ui();
    }).detach();
}

// ---- agent-host's own update (design doc 7.4 OQ-09)

void set_pipe(std::wstring pipe) {
    std::lock_guard lock(g().mutex);
    g().pipe = std::move(pipe);
}

bool run_core_update(const std::vector<std::wstring>& extra, std::string* now, std::string* error) {
    // `agentctl update --json [--pipe P] [extra]`, stdin closed.
    std::vector<std::wstring> args = {L"update", L"--json"};
    {
        std::lock_guard lock(g().mutex);
        if (!g().pipe.empty())
            args.insert(args.end(), {L"--pipe", g().pipe});
    }
    args.insert(args.end(), extra.begin(), extra.end());
    const fs::path ctl = os::ctl_exe_beside(g().config.exe.parent_path());
    const auto r = detail::run_process(ctl, args);
    if (!r.started) {
        *error = "cannot run agentctl update";
        return false;
    }
    if (r.exit_code == 0) {
        // The reply's "now" field, else "the latest".
        *now = string_field(Json::parse(r.out, nullptr, false), "now").value_or("the latest");
        return true;
    }
    // stderr's error.message (plus " (hint)"), else stderr as text.
    const Json err = Json::parse(r.err, nullptr, false);
    const Json* e =
        err.is_object() && err.contains("error") && err["error"].is_object() ? &err["error"] : nullptr;
    const auto message = e != nullptr ? string_field(*e, "message") : std::nullopt;
    if (message) {
        *error = *message;
        if (const auto hint = string_field(*e, "hint"))
            *error += " (" + *hint + ")";
    } else {
        *error = trim(r.err);
    }
    return false;
}

void update_core() {
    {
        std::lock_guard lock(g().mutex);
        auto& c = g().core;
        if (c.updating || c.managed)
            return;
        c.updating = true;
        c.error.reset();
    }
    wake_ui();
    std::thread([] {
        std::string now;
        std::string error;
        const bool ok = run_core_update({}, &now, &error);
        {
            std::lock_guard lock(g().mutex);
            auto& c = g().core;
            c.updating = false;
            if (ok) {
                c.version = without_host_prefix(now);
                c.ready.reset();
            } else {
                c.error = error;
            }
        }
        telemetry::count(ok ? "core_update_ok" : "core_update_failed");
        wake_ui();
    }).detach();
}

std::optional<std::string> host_version_of(const fs::path& exe) {
    return reported_version(exe, kHostPrefix);
}

CoreStaged stage_core(const CoreStageRequest& req) {
    return req.from ? stage_core_from_file(req) : stage_core_from_release(req);
}

bool unstage_core(const fs::path& host_exe) {
    const fs::path old = beside(host_exe, L".old");
    std::error_code ec;
    if (!fs::exists(old, ec))
        return false;
    // The new program may still be closing (its process was just ended): retry for up to 2 s.
    for (int i = 0; i < 20; ++i) {
        ec.clear();
        fs::remove(host_exe, ec);
        if (!ec || !fs::exists(host_exe))
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    ec.clear();
    fs::rename(old, host_exe, ec);
    return !ec;
}

}  // namespace agentdesk::ui::updates
