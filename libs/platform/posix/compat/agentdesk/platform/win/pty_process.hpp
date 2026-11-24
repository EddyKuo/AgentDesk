#pragma once

// macOS stand-in for win::PtyProcess (see README.md in this folder): the same interface the host
// uses, implemented on platform::Pty.

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "agentdesk/platform/pty.hpp"
#include "agentdesk/platform/win/conpty_api.hpp"
#include "agentdesk/platform/win/process_tree.hpp"

namespace agentdesk::platform::win {

struct PtySize {
    short cols = 120;
    short rows = 40;
};

struct PtySpawnSpec {
    std::filesystem::path executable;
    std::vector<std::string> args;
    std::filesystem::path cwd;
    std::map<std::string, std::string> env_overrides;
    PtySize size;
};

using PtyOutputSink = std::function<void(std::span<const char>)>;

// What agent-host's handover passes to the new host for one terminal: the pty master and the
// root process (design doc ADR-30). The fd stays owned by the PtyProcess that handed it out.
struct PtyHandles {
    int master_fd = -1;
    std::uint32_t pid = 0;
};

class PtyProcess {
public:
    // on_exit: called once from a platform thread after the root process exited.
    static std::unique_ptr<PtyProcess> spawn(const ConPtyApi& api, const PtySpawnSpec& spec,
                                             PtyOutputSink on_output, std::function<void()> on_exit = {});
    ~PtyProcess();
    PtyProcess(const PtyProcess&) = delete;
    PtyProcess& operator=(const PtyProcess&) = delete;
    PtyProcess(PtyProcess&&) = delete;
    PtyProcess& operator=(PtyProcess&&) = delete;

    void write(std::string_view bytes);
    void resize(PtySize size);
    void interrupt();
    void terminate(unsigned exit_code = 1);

    std::optional<std::uint32_t> wait_exit(std::chrono::milliseconds timeout) const;
    [[nodiscard]] std::uint32_t pid() const noexcept;
    [[nodiscard]] std::uint32_t job_active_processes() const;
    [[nodiscard]] std::string foreground_program() const;
    [[nodiscard]] Foreground foreground() const;
    void close();

    [[nodiscard]] bool closed() const;
    bool pause_reader(std::chrono::milliseconds timeout);
    void resume_reader();
    std::string read_available(std::size_t max);
    std::optional<PtyHandles> handover_handles(std::string* why) const;
    void release_after_handover();
    static std::unique_ptr<PtyProcess> adopt(const ConPtyApi& api, const PtyHandles& handles,
                                             PtyOutputSink on_output, std::function<void()> on_exit = {});

private:
    explicit PtyProcess(std::unique_ptr<platform::Pty> pty);
    std::unique_ptr<platform::Pty> pty_;
};

}  // namespace agentdesk::platform::win
