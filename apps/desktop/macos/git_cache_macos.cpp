// GitCache's watcher on macOS (ADR-30): the git folders' own files, looked at every second.
#include <atomic>

#include "app_internal.hpp"

namespace agentdesk::desktop {
namespace detail {

struct GitCache::Watch {
    std::atomic<bool> wake{false};  // a new repo to watch
};

void GitCache::WatchDeleter::operator()(Watch* p) const noexcept {
    delete p;
}

GitCache::Watch* GitCache::new_watch() {
    return new Watch;
}

void GitCache::wake_watcher() {
    watch_->wake = true;
}

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
        for (int i = 0; i < 10 && !st.stop_requested() && !watch_->wake.exchange(false); ++i)
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
                if (!machine_of(k).empty() || _stricmp(e.info.common_dir.c_str(), dir.c_str()) != 0 ||
                    e.queued)
                    continue;
                e.queued = true;
                queue_.push_back(k);
            }
        cv_.notify_one();
    }
}

}  // namespace detail
}  // namespace agentdesk::desktop
