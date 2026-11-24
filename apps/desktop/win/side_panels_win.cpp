// The Files / Changes worker on Windows: tasks plus a change notification on the shown folder.
#include <windows.h>

#include "app_internal.hpp"

namespace agentdesk::desktop {
namespace detail {

void SidePanels::run(std::stop_token st) {
    std::string watching;
    HANDLE change = INVALID_HANDLE_VALUE;
    while (!st.stop_requested()) {
        Task task;
        std::string want;
        {
            std::unique_lock lock(mutex);
            cv_.wait_for(lock, st, std::chrono::milliseconds(150), [&] { return !tasks_.empty(); });
            if (st.stop_requested())
                break;
            want = watch_dir_;
            if (!tasks_.empty()) {
                task = std::move(tasks_.front());
                tasks_.pop_front();
            }
        }
        if (want != watching) {
            if (change != INVALID_HANDLE_VALUE)
                ::FindCloseChangeNotification(change);
            change = INVALID_HANDLE_VALUE;
            try {
                if (!want.empty())
                    change = ::FindFirstChangeNotificationW(
                        win::to_wide(want).c_str(), TRUE,
                        FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
                            FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE);
            } catch (const std::exception&) {  // not watched; the 10 s refresh still runs
            }
            watching = want;
        }
        if (change != INVALID_HANDLE_VALUE && ::WaitForSingleObject(change, 0) == WAIT_OBJECT_0) {
            files_changed_ = true;
            changes_changed_ = true;
            ::FindNextChangeNotification(change);
        }
        if (task.fn) {
            try {
                task.fn();
            } catch (const std::exception& e) {
                task_failed(task.panel, e.what());
            } catch (...) {
                task_failed(task.panel, "unexpected error");
            }
        }
    }
    if (change != INVALID_HANDLE_VALUE)
        ::FindCloseChangeNotification(change);
}

}  // namespace detail
}  // namespace agentdesk::desktop
