#pragma once

// The old host's side of a handover, per system: starting the new host and the private channel to
// it. handover_win.cpp (two anonymous pipes, handles duplicated into the new process) and
// handover_posix.cpp (a socketpair, pty masters as SCM_RIGHTS; ADR-30). CMake builds one. The new
// host's side is OldHost in handover.hpp, implemented in the same files.

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "agentdesk/platform/pty_handles.hpp"
#include "agentdesk/terminal/bytes.hpp"

namespace agentdesk::host {

class NewHost {
public:
    // `exe --takeover <its end of the channel> --pipe <pipe_name> --data-dir <data_dir>`, inheriting
    // that end and nothing else, outliving this process. Throws when it does not start.
    static std::unique_ptr<NewHost> start(const std::filesystem::path& exe, const std::string& pipe_name,
                                          const std::filesystem::path& data_dir);
    ~NewHost();  // closes our end; the process carries on
    NewHost(const NewHost&) = delete;
    NewHost& operator=(const NewHost&) = delete;
    NewHost(NewHost&&) = delete;
    NewHost& operator=(NewHost&&) = delete;

    bool write_all(std::string_view bytes);
    // One line ("ready ...", "error ..."); nullopt when the channel ended first.
    std::optional<std::string> read_line();

    // A frozen terminal goes to the new host: Windows duplicates its handles into the process (and
    // changes `h` to them); elsewhere its pty master is sent by send_terminals(). Throws on failure.
    void add_terminal(platform::PtyHandles& h);
    // What add_terminal() made of it, for the state the new host reads (OldHost::read_handles).
    static void write_handles(terminal::ByteWriter& w, const platform::PtyHandles& h);
    // After the state: the terminals themselves, in the order they were added.
    bool send_terminals();

    void kill();  // at once; also ends a read_line() in another thread
    void end();   // kill() and wait: what was passed to it has closed with it
    void close_channel();

    struct Impl;

private:
    explicit NewHost(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

}  // namespace agentdesk::host
