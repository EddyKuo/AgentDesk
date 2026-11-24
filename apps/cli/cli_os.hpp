#pragma once

// What agentctl does differently per system: cli_win.cpp (wmain, console handles, the Named Pipe)
// and cli_posix.cpp (main, file descriptors, the Unix socket; ADR-30). CMake builds one.

#include <cstddef>
#include <string>

namespace agentdesk::cli {

// The command line, as UTF-16 (Windows) or converted from UTF-8; main.cpp. The entry point in
// cli_win.cpp / cli_posix.cpp calls it.
int agentctl_main(int argc, wchar_t** argv);

namespace os {

// An environment variable as UTF-8; "" when unset.
std::string env(const wchar_t* name);
// Up to `max` bytes of stdin, until it ends.
std::string read_stdin(std::size_t max);
// Copies stdin to the host's endpoint and its answers to stdout, byte for byte, until either
// side ends, then exits the process with 0. Returns false only when the endpoint cannot be opened.
bool bridge_stdio(const std::string& pipe);

}  // namespace os

}  // namespace agentdesk::cli
