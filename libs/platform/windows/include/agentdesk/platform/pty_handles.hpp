#pragma once

// What agent-host's handover (design doc 7.4 OQ-09) passes to the new host for one terminal on
// Windows; the POSIX platform has its own header with this name. Members are HANDLE values, kept
// as void* so that this header does not bring in windows.h.

#include <cstdint>

namespace agentdesk::platform {

// Everything that keeps one terminal alive. The first three are the bundled ConPTY's HPCON
// members (microsoft/terminal v1.24.11911.0 src/winconpty/winconpty.h 11-41): the signal pipe
// (resize), the \Reference handle that keeps the console server up, and the OpenConsole process.
struct PtyHandles {
    void* signal = nullptr;
    void* reference = nullptr;
    void* conhost = nullptr;
    void* input_write = nullptr;
    void* output_read = nullptr;
    void* process = nullptr;
    void* job = nullptr;
    std::uint32_t pid = 0;
};

}  // namespace agentdesk::platform
