#pragma once

#include <windows.h>

#include <chrono>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "agentdesk/platform/win/conpty_api.hpp"
#include "agentdesk/platform/win/job.hpp"
#include "agentdesk/platform/win/unique_handle.hpp"

namespace agentdesk::platform::win {

struct PtySize {
    short cols = 120;
    short rows = 40;
};

struct PtySpawnSpec {
    std::filesystem::path executable;  // must be an .exe; batch files are rejected
    std::vector<std::wstring> args;
    std::filesystem::path cwd;                           // must exist; never silently replaced
    std::map<std::wstring, std::wstring> env_overrides;  // merged over the current environment
    PtySize size;
};

// Called on the reader thread with each chunk in order. Must not block for long.
using PtyOutputSink = std::function<void(std::span<const char>)>;

// One child process attached to a pseudoconsole, inside a kill-on-close job.
// Threading: the reader thread is owned here; write()/resize()/interrupt() may be called from
// any single thread at a time (the caller serializes them); close() must not run on the
// reader thread or a UI thread.
class PtyProcess {
public:
    static std::unique_ptr<PtyProcess> spawn(const ConPtyApi& api, const PtySpawnSpec& spec,
                                             PtyOutputSink on_output);
    ~PtyProcess();
    PtyProcess(const PtyProcess&) = delete;
    PtyProcess& operator=(const PtyProcess&) = delete;
    PtyProcess(PtyProcess&&) = delete;
    PtyProcess& operator=(PtyProcess&&) = delete;

    void write(std::string_view bytes);  // blocking WriteFile into the ConPTY input
    void resize(PtySize size);
    void interrupt();                    // sends 0x03 (Ctrl+C) through the input pipe
    void terminate(UINT exit_code = 1);  // TerminateJobObject: whole process tree

    // Waits for the root process; returns its exit code when it exited within the timeout.
    std::optional<DWORD> wait_exit(std::chrono::milliseconds timeout) const;
    [[nodiscard]] DWORD pid() const noexcept { return pid_; }
    [[nodiscard]] HANDLE process_handle() const noexcept { return process_.get(); }
    [[nodiscard]] DWORD job_active_processes() const { return job_.active_processes(); }
    // What runs in the terminal now (see process_tree.hpp). The job handle lives as long as *this.
    [[nodiscard]] std::string foreground_program() const;

    // Closes the pseudoconsole while the reader keeps draining, then joins the reader and
    // releases pipes and the job (which kills any remaining descendants). Idempotent.
    void close();

private:
    PtyProcess(const ConPtyApi& api, PtyOutputSink on_output);
    void reader_loop() noexcept;

    ConPtyApi api_;
    PtyOutputSink on_output_;
    KillOnCloseJob job_;
    UniqueHandle input_write_;
    UniqueHandle output_read_;
    UniqueHandle process_;
    HPCON pcon_ = nullptr;
    DWORD pid_ = 0;
    std::thread reader_;
    std::mutex close_mutex_;  // lock order: close_mutex_ before write_mutex_
    std::mutex write_mutex_;
    bool closed_ = false;
};

// Builds a CREATE_UNICODE_ENVIRONMENT block from the current environment plus overrides,
// sorted case-insensitively. An override with an empty value removes the variable.
std::wstring make_environment_block(const std::map<std::wstring, std::wstring>& overrides);

}  // namespace agentdesk::platform::win
