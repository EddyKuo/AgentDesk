// GitCache: git facts per folder (top, worktree, branch, ahead/behind), asked on a background
// thread and refreshed when the repo's git folder changes. Also git on another machine.
// Portions adapted from herdr-app (Apache-2.0) and modified; see THIRD_PARTY_NOTICES.md.
#include "app_internal.hpp"

namespace agentdesk::desktop {
namespace detail {

host::ToolResult remote_git(const RemoteCall& remote, const std::string& machine, const std::string& cwd,
                            const std::vector<std::wstring>& args) {
    host::ToolResult r;
    Json a = Json::array();
    for (const auto& x : args)
        a.push_back(platform::to_utf8(x));
    if (const auto res =
            remote ? remote(machine, "git.run", Json{{"cwd", cwd}, {"args", a}}) : std::nullopt) {
        r.started = true;
        r.exit_code = static_cast<unsigned long>(safe::int_or(*res, "exit_code", 1));
        r.output = safe::string_or(*res, "output");
        r.error = safe::string_or(*res, "error");
    }
    return r;
}

GitCache::GitCache()
    : watch_(new_watch()),
      worker_([this](std::stop_token st) { run(st); }),
      watcher_([this](std::stop_token st) { watch_loop(st); }) {}

GitCache::~GitCache() {
    worker_.request_stop();
    watcher_.request_stop();
    cv_.notify_all();
    wake_watcher();
    watcher_.join();
    worker_.join();
}

GitInfo GitCache::get(const std::string& dir, const std::string& machine) {
    if (dir.empty())
        return {};
    const std::string key = key_of(dir, machine);
    std::lock_guard lock(mutex_);
    auto& e = entries_[key];
    const auto now = Clock::now();
    e.used = now;
    // The 60 s look again happens only while the window is in front.
    if (!e.queued && (e.at == Clock::time_point{} || (active_ && now - e.at > std::chrono::seconds(60)))) {
        e.queued = true;
        queue_.push_back(key);
        cv_.notify_one();
    }
    return e.info;
}

GitInfo GitCache::with_changes(const std::string& dir, const std::string& machine) {
    if (dir.empty())
        return {};
    {
        std::lock_guard lock(mutex_);
        auto& e = entries_[key_of(dir, machine)];
        if (!e.changes) {
            e.changes = true;
            e.at = Clock::time_point{};  // look again now, with the counts
        }
    }
    return get(dir, machine);
}

void GitCache::invalidate(const std::string& dir) {
    std::lock_guard lock(mutex_);
    if (auto it = entries_.find(dir); it != entries_.end())
        it->second.at = Clock::time_point{};
}

void GitCache::invalidate_repo(const std::string& dir, const std::string& machine) {
    if (dir.empty())
        return;
    std::lock_guard lock(mutex_);
    const std::string key = key_of(dir, machine);
    std::string common;
    if (const auto it = entries_.find(key); it != entries_.end()) {
        it->second.at = Clock::time_point{};
        common = it->second.info.common_top;
    } else {
        // A folder nobody asked about yet: the repo of a known checkout that holds it.
        for (const auto& [k, e] : entries_) {
            const std::string& top = e.info.top;
            if (machine_of(k) == machine && e.info.repo && dir.size() >= top.size() &&
                _strnicmp(dir.c_str(), top.c_str(), top.size()) == 0 &&
                (dir.size() == top.size() || dir[top.size()] == '\\' || dir[top.size()] == '/')) {
                common = e.info.common_top;
                break;
            }
        }
    }
    if (common.empty())
        return;
    for (auto& [k, e] : entries_)
        if (machine_of(k) == machine && e.info.repo &&
            _stricmp(e.info.common_top.c_str(), common.c_str()) == 0)
            e.at = Clock::time_point{};
}

// A machine name never starts a path and cannot hold '|', so the first one ends it; a path on this
// computer may hold one (on macOS).
std::string GitCache::key_of(const std::string& dir, const std::string& machine) {
    return machine.empty() ? dir : machine + "|" + dir;
}

std::string GitCache::machine_of(const std::string& key) {
    const auto bar = key.find('|');
    return bar == std::string::npos || ui::path_text::is_absolute(key) ? std::string() : key.substr(0, bar);
}

std::string GitCache::dir_of(const std::string& key) {
    const auto bar = key.find('|');
    return bar == std::string::npos || ui::path_text::is_absolute(key) ? key : key.substr(bar + 1);
}

// watch_loop is per system: win/git_cache_win.cpp, macos/git_cache_macos.cpp.

std::string GitCache::trim(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' '))
        s.pop_back();
    return s;
}

namespace {
// `git status --porcelain=v1 -z` records: "XY path\0", a rename or copy followed by its old path.
// Counts the files and gathers the untracked ones.
int count_status(const std::string& out, std::vector<std::string>& untracked) {
    int files = 0;
    std::size_t i = 0;
    while (i + 3 <= out.size()) {
        const std::size_t z = out.find('\0', i);
        if (z == std::string::npos)
            break;
        const std::string rec = out.substr(i, z - i);
        i = z + 1;
        if (rec.size() < 4)
            continue;
        ++files;
        if (rec[0] == '?' && rec[1] == '?')
            untracked.push_back(rec.substr(3));
        if (rec[0] == 'R' || rec[0] == 'C') {  // skip the old path
            const std::size_t z2 = out.find('\0', i);
            i = z2 == std::string::npos ? out.size() : z2 + 1;
        }
    }
    return files;
}

// `git diff --numstat -z`: "added\tremoved\tpath\0"; binary files say "-".
void add_numstat(const std::string& out, int& added, int& removed) {
    std::size_t i = 0;
    while (i < out.size()) {
        const std::size_t z = out.find('\0', i);
        const std::string rec = out.substr(i, z == std::string::npos ? std::string::npos : z - i);
        i = z == std::string::npos ? out.size() : z + 1;
        const std::size_t t1 = rec.find('\t');
        const std::size_t t2 = t1 == std::string::npos ? t1 : rec.find('\t', t1 + 1);
        if (t2 == std::string::npos || rec[0] == '-')
            continue;
        added += std::atoi(rec.substr(0, t1).c_str());
        removed += std::atoi(rec.substr(t1 + 1, t2 - t1 - 1).c_str());
    }
}

// Lines in a new file (each counts as added), for up to 200 files of up to 1 MiB on this computer.
int untracked_lines(const std::filesystem::path& top, const std::vector<std::string>& files) {
    int lines = 0;
    std::size_t n = 0;
    for (const auto& rel : files) {
        if (++n > 200)
            break;
        std::error_code ec;
        const auto p = top / std::filesystem::path(platform::to_wide(rel));
        if (!std::filesystem::is_regular_file(p, ec) || std::filesystem::file_size(p, ec) > (1u << 20))
            continue;
        std::ifstream in(p, std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (text.find('\0') != std::string::npos)
            continue;  // binary
        lines += static_cast<int>(std::count(text.begin(), text.end(), '\n') +
                                  (!text.empty() && text.back() != '\n' ? 1 : 0));
    }
    return lines;
}
}  // namespace

GitInfo GitCache::query(const std::string& key, bool changes) {
    GitInfo g;
    const std::string machine = machine_of(key);
    const std::string dir = dir_of(key);
    const std::filesystem::path cwd(platform::to_wide(dir));
    std::error_code ec;
    if (machine.empty() && !std::filesystem::is_directory(cwd, ec))
        return g;
    auto git = [&](std::vector<std::wstring> args) {
        if (!machine.empty())
            return remote_git(remote, machine, dir, args);
        args.insert(args.begin(), {L"git", L"--no-optional-locks", L"-C", cwd.wstring()});
        return host::run_tool(args, cwd, std::chrono::seconds(10));
    };
    const auto r = git({L"rev-parse", L"--show-toplevel", L"--git-common-dir", L"--abbrev-ref", L"HEAD"});
    if (!r.started || r.exit_code != 0)
        return g;
    std::vector<std::string> lines;
    std::stringstream ss(r.output);
    for (std::string l; std::getline(ss, l);)
        lines.push_back(trim(l));
    if (lines.size() < 3)
        return g;
    g.repo = true;
    // git prints forward slashes everywhere; another machine's paths are that system's, so only
    // this computer's go through std::filesystem (which also resolves links).
    namespace pt = ui::path_text;
    g.top = pt::native(lines[0]);
    g.common_dir = pt::resolve(dir, lines[1]);
    if (machine.empty()) {
        const auto real = std::filesystem::weakly_canonical(std::filesystem::path(platform::to_wide(g.common_dir)), ec);
        if (!ec)
            g.common_dir = pt::native(platform::to_utf8(real.native()));
    }
    g.common_top = pt::parent(g.common_dir);
    g.worktree = _stricmp(g.common_top.c_str(), g.top.c_str()) != 0;
    g.branch = lines[2];
    const auto c = git({L"rev-list", L"--left-right", L"--count", L"HEAD...@{upstream}"});
    if (c.started && c.exit_code == 0) {
        std::stringstream cs(c.output);
        cs >> g.ahead >> g.behind;
    }
    if (changes) {
        const auto st = git({L"status", L"--porcelain=v1", L"-z",
                             L"--untracked-files=all"});
        if (st.started && st.exit_code == 0) {
            std::vector<std::string> untracked;
            g.changed_files = count_status(st.output, untracked);
            g.lines_added = 0;
            g.lines_removed = 0;
            // Against HEAD (staged and not); a repo without a commit yet has only new files.
            const auto d = git({L"diff", L"HEAD", L"--numstat", L"-z", L"--no-renames"});
            if (d.started && d.exit_code == 0)
                add_numstat(d.output, g.lines_added, g.lines_removed);
            if (machine.empty() && !untracked.empty())
                g.lines_added += untracked_lines(std::filesystem::path(platform::to_wide(g.top)), untracked);
        }
    }
    return g;
}

void GitCache::run(std::stop_token st) {
    while (!st.stop_requested()) {
        std::string dir;
        bool changes = false;
        {
            std::unique_lock lock(mutex_);
            cv_.wait(lock, st, [&] { return !queue_.empty(); });
            if (st.stop_requested())
                return;
            dir = queue_.front();
            queue_.pop_front();
            changes = entries_[dir].changes;
        }
        GitInfo info;
        try {  // a path or git output that does not convert, an odd answer: not a repo
            info = query(dir, changes);
        } catch (const std::exception&) {
            info = {};
        }
        std::lock_guard lock(mutex_);
        auto& e = entries_[dir];
        const bool new_dir = e.info.common_dir != info.common_dir;
        e.info = info;
        e.at = Clock::now();
        e.queued = false;
        if (new_dir)
            wake_watcher();  // a repo to watch
    }
}

}  // namespace detail

}  // namespace agentdesk::desktop
