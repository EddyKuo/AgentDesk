#pragma once

// The ConPTY entry points, loaded from the bundled conpty.dll when present, else kernel32.

#include <windows.h>

#include <filesystem>
#include <string>

namespace agentdesk::platform::win {

using CreatePseudoConsoleFn = HRESULT(WINAPI*)(COORD, HANDLE, HANDLE, DWORD, HPCON*);
using ResizePseudoConsoleFn = HRESULT(WINAPI*)(HPCON, COORD);
using ClosePseudoConsoleFn = void(WINAPI*)(HPCON);
// ConptyPackPseudoConsole(process, reference, signal, &hpcon): Windows Terminal's handoff entry
// point (microsoft/terminal v1.24.11911.0 src/winconpty/winconpty.cpp 597-620).
using PackPseudoConsoleFn = HRESULT(WINAPI*)(HANDLE, HANDLE, HANDLE, HPCON*);

// create/resize/close for one HPCON must all come from the same module.
struct ConPtyApi {
    CreatePseudoConsoleFn create = nullptr;
    ResizePseudoConsoleFn resize = nullptr;
    ClosePseudoConsoleFn close = nullptr;
    // Bundled ConPTY only (the inbox one has no such export): agent-host's handover rebuilds an
    // HPCON in the new host from the handles the old host duplicated (design doc 7.4 OQ-09).
    PackPseudoConsoleFn pack = nullptr;
    bool bundled = false;  // true: Microsoft.Windows.Console.ConPTY next to the exe
    std::wstring source;   // module path or "kernel32.dll" for diagnostics
};

// Inbox ConPTY from kernel32.
ConPtyApi load_system_conpty();

// <dir>\conpty\conpty.dll when present (absolute path, no DLL search order); otherwise the
// inbox ConPTY. A present-but-broken bundled DLL throws instead of silently falling back.
ConPtyApi load_conpty(const std::filesystem::path& exe_dir);

}  // namespace agentdesk::platform::win
