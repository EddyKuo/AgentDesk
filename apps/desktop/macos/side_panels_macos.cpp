// The Files / Changes worker on macOS (ADR-30): tasks plus a look at the shown folder's times.
#include "app_internal.hpp"

namespace agentdesk::desktop {
namespace detail {

// macOS: the watched folder is checked for changes by its modification time (and the times of its
// direct entries) about every 150 ms between tasks, standing in for the Windows change notification.
void SidePanels::run(std::stop_token st) {
    std::string watching;
    std::filesystem::file_time_type last{};
    auto stamp = [](const std::string& dir) {
        std::filesystem::file_time_type newest{};
        std::error_code ec;
        newest = std::filesystem::last_write_time(dir, ec);
        int n = 0;
        for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
            if (++n > 2000)
                break;
            const auto t = e.last_write_time(ec);
            if (!ec && t > newest)
                newest = t;
        }
        return newest;
    };
    auto checked = std::chrono::steady_clock::now();
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
            watching = want;
            last = watching.empty() ? std::filesystem::file_time_type{} : stamp(watching);
        } else if (!watching.empty() &&
                   std::chrono::steady_clock::now() - checked > std::chrono::milliseconds(500)) {
            checked = std::chrono::steady_clock::now();
            if (const auto t = stamp(watching); t != last) {
                last = t;
                files_changed_ = true;
                changes_changed_ = true;
            }
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
}

}  // namespace detail
}  // namespace agentdesk::desktop
