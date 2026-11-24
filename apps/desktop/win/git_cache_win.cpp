// GitCache's watcher on Windows: a change notification on each repo's git folder.
// Portions adapted from herdr-app (Apache-2.0) and modified; see THIRD_PARTY_NOTICES.md.
#include <windows.h>

#include "app_internal.hpp"

namespace agentdesk::desktop {
namespace detail {

struct GitCache::Watch {
    HANDLE wake = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);  // a new repo to watch, or stop
    ~Watch() {
        if (wake != nullptr)
            ::CloseHandle(wake);
    }
};

void GitCache::WatchDeleter::operator()(Watch* p) const noexcept {
    delete p;
}

GitCache::Watch* GitCache::new_watch() {
    return new Watch;
}

void GitCache::wake_watcher() {
    if (watch_->wake != nullptr)
        ::SetEvent(watch_->wake);
}

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
        std::vector<HANDLE> wait{watch_->wake};
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

}  // namespace detail
}  // namespace agentdesk::desktop
