#pragma once

// PtyProcess: one child program in a pseudoconsole, its job and the thread that reads its output.

#include <windows.h>

#include <atomic>
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
#include <condition_variable>
#include <vector>

#include "agentdesk/platform/win/conpty_api.hpp"
#include "agentdesk/platform/win/job.hpp"
#include "agentdesk/platform/win/process_tree.hpp"
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

// agent-host's handover (design doc 7.4 OQ-09): everything that keeps one terminal alive. The
// first three are the bundled ConPTY's HPCON members (microsoft/terminal v1.24.11911.0
// src/winconpty/winconpty.h 11-41): the signal pipe (resize), the \Reference handle that keeps
// the console server up, and the OpenConsole process.
struct PtyHandles {
    HANDLE signal = nullptr;
    HANDLE reference = nullptr;
    HANDLE conhost = nullptr;
    HANDLE input_write = nullptr;
    HANDLE output_read = nullptr;
    HANDLE process = nullptr;
    HANDLE job = nullptr;
    DWORD pid = 0;
};

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
    [[nodiscard]] Foreground foreground() const;

    // Closes the pseudoconsole while the reader keeps draining, then joins the reader and
    // releases pipes and the job (which kills any remaining descendants). Idempotent.
    void close();

    // ---- agent-host's handover (design doc 7.4 OQ-09)
    [[nodiscard]] bool closed() const {
        std::lock_guard lock(close_mutex_);
        return closed_;
    }
    // Stops the reader between two reads: every byte it read reached the sink, the rest stays in
    // the pipe. False when it did not stop within `timeout` (then call resume_reader()).
    bool pause_reader(std::chrono::milliseconds timeout);
    // Starts reading again (after pause_reader, or first time for an adopted process).
    void resume_reader();
    // With the reader paused: up to `max` bytes the pipe holds now; never waits.
    std::string read_available(std::size_t max);
    // This process's handles, to duplicate into the new host. nullopt with `why` when this
    // pseudoconsole cannot be handed over (inbox ConPTY, or an HPCON that does not look like the
    // bundled ConPTY's). The handles stay owned by *this.
    std::optional<PtyHandles> handover_handles(std::string* why) const;
    // After the new host confirmed it holds duplicates of every handle: closes ours without
    // ending anything (no ClosePseudoConsole signal beyond closing handles, no TerminateJobObject).
    // The reader must be paused. *this is closed afterwards.
    void release_after_handover();
    // In the new host: takes ownership of handles the old host duplicated into this process and
    // checks them (process id, job membership, kill-on-close, OpenConsole). The reader starts with
    // resume_reader(). Throws when anything does not check out; all handles are then closed.
    static std::unique_ptr<PtyProcess> adopt(const ConPtyApi& api, const PtyHandles& handles,
                                             PtyOutputSink on_output);

private:
    PtyProcess(const ConPtyApi& api, PtyOutputSink on_output);
    void reader_loop() noexcept;
    std::atomic<bool> pause_requested_{false};

    ConPtyApi api_;
    PtyOutputSink on_output_;
    KillOnCloseJob job_;
    UniqueHandle input_write_;
    UniqueHandle output_read_;
    UniqueHandle process_;
    HPCON pcon_ = nullptr;
    DWORD pid_ = 0;
    std::thread reader_;
    // Set by the reader thread when it leaves (EOF or error); close() waits on it with a bound.
    std::mutex reader_mutex_;
    std::condition_variable reader_cv_;
    bool reader_done_ = false;
    mutable std::mutex close_mutex_;  // lock order: close_mutex_ before write_mutex_
    std::mutex write_mutex_;
    bool closed_ = false;
};

// Builds a CREATE_UNICODE_ENVIRONMENT block from the current environment plus overrides,
// sorted case-insensitively. An override with an empty value removes the variable.
std::wstring make_environment_block(const std::map<std::wstring, std::wstring>& overrides);

}  // namespace agentdesk::platform::win
