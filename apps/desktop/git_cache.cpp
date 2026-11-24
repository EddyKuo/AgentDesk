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
        a.push_back(win::to_utf8(x));
    if (const auto res =
            remote ? remote(machine, "git.run", Json{{"cwd", cwd}, {"args", a}}) : std::nullopt) {
        r.started = true;
        r.exit_code = static_cast<unsigned long>(safe::int_or(*res, "exit_code", 1));
        r.output = safe::string_or(*res, "output");
        r.error = safe::string_or(*res, "error");
    }
    return r;
}

GitCache::~GitCache() {
    worker_.request_stop();
    watcher_.request_stop();
    cv_.notify_all();
#ifdef _WIN32
    if (wake_ != nullptr)
        ::SetEvent(wake_);
#endif
    watcher_.join();
    worker_.join();
#ifdef _WIN32
    if (wake_ != nullptr)
        ::CloseHandle(wake_);
#endif
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

// A Windows path cannot hold '|', so the first one ends the machine name.
std::string GitCache::key_of(const std::string& dir, const std::string& machine) {
    return machine.empty() ? dir : machine + "|" + dir;
}

std::string GitCache::machine_of(const std::string& key) {
    const auto bar = key.find('|');
    return bar == std::string::npos ? std::string() : key.substr(0, bar);
}

std::string GitCache::dir_of(const std::string& key) {
    const auto bar = key.find('|');
    return bar == std::string::npos ? key : key.substr(bar + 1);
}

#ifdef _WIN32
void GitCache::watch_loop(std::stop_token st) {
    std::map<std::string, HANDLE> handles;
    while (!st.stop_requested()) {
        {
            std::set<std::string> wanted;
            {
                std::lock_guard lock(mutex_);
                const auto now = Clock::now();
                for (const auto& [k, e] : entries_)
                    if (machine_of(k).empty() && e.info.repo && !e.info.common_dir.empty() &&
                        now - e.used < std::chrono::minutes(10))
                        wanted.insert(e.info.common_dir);
            }
            for (auto it = handles.begin(); it != handles.end();) {
                if (!wanted.contains(it->first)) {
                    ::FindCloseChangeNotification(it->second);
                    it = handles.erase(it);
                } else {
                    ++it;
                }
            }
            for (const auto& dir : wanted) {
                if (handles.size() >= 62 || handles.contains(dir))
                    continue;
                std::wstring wide;
                try {  // git printed it: not always valid UTF-8; such a folder is not watched
                    wide = win::to_wide(dir);
                } catch (const std::exception&) {
                    continue;
                }
                const HANDLE h = ::FindFirstChangeNotificationW(wide.c_str(), TRUE,
                                                                FILE_NOTIFY_CHANGE_FILE_NAME |
                                                                    FILE_NOTIFY_CHANGE_DIR_NAME |
                                                                    FILE_NOTIFY_CHANGE_LAST_WRITE);
                if (h != INVALID_HANDLE_VALUE)
                    handles[dir] = h;
            }
        }
        std::vector<HANDLE> wait{wake_};
        std::vector<std::string> dirs{""};
        for (const auto& [d, h] : handles) {
            wait.push_back(h);
            dirs.push_back(d);
        }
        const DWORD r = ::WaitForMultipleObjects(static_cast<DWORD>(wait.size()), wait.data(), FALSE, 2000);
        if (st.stop_requested())
            break;
        if (r == WAIT_FAILED)
            std::this_thread::sleep_for(std::chrono::seconds(1));
        if (r <= WAIT_OBJECT_0 || r >= WAIT_OBJECT_0 + wait.size())
            continue;
        const std::string dir = dirs[r - WAIT_OBJECT_0];
        // A git command writes many files: let it finish, then look once.
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        ::FindNextChangeNotification(handles[dir]);
        std::lock_guard lock(mutex_);
        for (auto& [k, e] : entries_) {
            if (!machine_of(k).empty() || _stricmp(e.info.common_dir.c_str(), dir.c_str()) != 0 || e.queued)
                continue;
            e.queued = true;
            queue_.push_back(k);
        }
        cv_.notify_one();
    }
    for (const auto& [d, h] : handles)
        ::FindCloseChangeNotification(h);
}
#else
// macOS: the git folders' own files are checked every second (HEAD, index, refs, logs/HEAD, the
// packed refs): a change there is what the Windows change notification reports, and a git command
// writes many of them, so it is looked at once after it settled.
void GitCache::watch_loop(std::stop_token st) {
    std::map<std::string, std::filesystem::file_time_type> seen;
    const auto stamp = [](const std::string& dir) {
        std::filesystem::file_time_type newest{};
        std::error_code ec;
        for (const char* f : {"HEAD", "index", "packed-refs", "logs/HEAD", "refs/heads", "FETCH_HEAD"}) {
            const auto t = std::filesystem::last_write_time(std::filesystem::path(dir) / f, ec);
            if (!ec && t > newest)
                newest = t;
        }
        return newest;
    };
    while (!st.stop_requested()) {
        for (int i = 0; i < 10 && !st.stop_requested() && !wake_flag_.exchange(false); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (st.stop_requested())
            break;
        std::set<std::string> wanted;
        {
            std::lock_guard lock(mutex_);
            const auto now = Clock::now();
            for (const auto& [k, e] : entries_)
                if (machine_of(k).empty() && e.info.repo && !e.info.common_dir.empty() &&
                    now - e.used < std::chrono::minutes(10))
                    wanted.insert(e.info.common_dir);
        }
        std::erase_if(seen, [&](const auto& kv) { return !wanted.contains(kv.first); });
        std::vector<std::string> changed;
        for (const auto& dir : wanted) {
            const auto t = stamp(dir);
            const auto it = seen.find(dir);
            if (it == seen.end()) {
                seen[dir] = t;
            } else if (it->second != t) {
                it->second = t;
                changed.push_back(dir);
            }
        }
        if (changed.empty())
            continue;
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        std::lock_guard lock(mutex_);
        for (const auto& dir : changed)
            for (auto& [k, e] : entries_) {
                if (!machine_of(k).empty() || _stricmp(e.info.common_dir.c_str(), dir.c_str()) != 0 || e.queued)
                    continue;
                e.queued = true;
                queue_.push_back(k);
            }
        cv_.notify_one();
    }
}
#endif

std::string GitCache::trim(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' '))
        s.pop_back();
    return s;
}

std::string GitCache::norm(std::string p) {
    for (auto& c : p)
        if (c == '/')
            c = '\\';
    while (p.size() > 3 && p.back() == '\\')
        p.pop_back();
    return p;
}

GitInfo GitCache::query(const std::string& key) {
    GitInfo g;
    const std::string machine = machine_of(key);
    const std::string dir = dir_of(key);
    const std::filesystem::path cwd(win::to_wide(dir));
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
    g.top = norm(lines[0]);
    std::filesystem::path common(win::to_wide(lines[1]));
    if (common.is_relative())
        common = cwd / common;
    common = machine.empty() ? std::filesystem::weakly_canonical(common, ec) : common.lexically_normal();
    g.common_top = norm(win::to_utf8(common.parent_path().native()));
    g.common_dir = norm(win::to_utf8(common.native()));
    g.worktree = _stricmp(g.common_top.c_str(), g.top.c_str()) != 0;
    g.branch = lines[2];
    const auto c = git({L"rev-list", L"--left-right", L"--count", L"HEAD...@{upstream}"});
    if (c.started && c.exit_code == 0) {
        std::stringstream cs(c.output);
        cs >> g.ahead >> g.behind;
    }
    return g;
}

void GitCache::run(std::stop_token st) {
    while (!st.stop_requested()) {
        std::string dir;
        {
            std::unique_lock lock(mutex_);
            cv_.wait(lock, st, [&] { return !queue_.empty(); });
            if (st.stop_requested())
                return;
            dir = queue_.front();
            queue_.pop_front();
        }
        GitInfo info;
        try {  // a path or git output that does not convert, an odd answer: not a repo
            info = query(dir);
        } catch (const std::exception&) {
            info = {};
        }
        std::lock_guard lock(mutex_);
        auto& e = entries_[dir];
        const bool new_dir = e.info.common_dir != info.common_dir;
        e.info = info;
        e.at = Clock::now();
        e.queued = false;
#ifdef _WIN32
        if (new_dir && wake_ != nullptr)
            ::SetEvent(wake_);  // a repo to watch
#else
        if (new_dir)
            wake_flag_ = true;
#endif
    }
}

}  // namespace detail

}  // namespace agentdesk::desktop
