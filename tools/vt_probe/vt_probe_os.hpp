#pragma once

// What vt_probe does differently per system: vt_probe_win.cpp (a console under ConPTY, wmain) and
// vt_probe_posix.cpp (a Unix pty, main; ADR-30). CMake builds one.

#include <chrono>
#include <string>
#include <string_view>

namespace vt_probe {

void write_out(std::string_view s);
// Raw input (every byte as it comes, no echo, no Ctrl+C signal) and VT output.
void enable_vt_io();
// Whatever arrives on stdin until `deadline` or `stop`, without blocking past the deadline.
std::string read_until(std::chrono::steady_clock::time_point deadline, std::string_view stop = {});
void sleep_ms(unsigned ms);
// The terminal's size as the program sees it.
void console_size(int& cols, int& rows);
// Dies like a crashing agent: an access violation (Windows) or SIGSEGV.
[[noreturn]] void crash();

// The modes, shared (vt_probe.cpp); `args` are the arguments after the mode.
int run(std::string_view mode, const std::string& arg2, const std::string& arg3);

}  // namespace vt_probe
