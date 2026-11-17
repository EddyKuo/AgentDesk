#pragma once

#include <windows.h>

#include <filesystem>
#include <string>

namespace agentdesk::platform::win {

using CreatePseudoConsoleFn = HRESULT(WINAPI*)(COORD, HANDLE, HANDLE, DWORD, HPCON*);
using ResizePseudoConsoleFn = HRESULT(WINAPI*)(HPCON, COORD);
using ClosePseudoConsoleFn = void(WINAPI*)(HPCON);

// create/resize/close for one HPCON must all come from the same module.
struct ConPtyApi {
    CreatePseudoConsoleFn create = nullptr;
    ResizePseudoConsoleFn resize = nullptr;
    ClosePseudoConsoleFn close = nullptr;
    bool bundled = false;  // true: Microsoft.Windows.Console.ConPTY next to the exe
    std::wstring source;   // module path or "kernel32.dll" for diagnostics
};

// Inbox ConPTY from kernel32.
ConPtyApi load_system_conpty();

// <dir>\conpty\conpty.dll when present (absolute path, no DLL search order); otherwise the
// inbox ConPTY. A present-but-broken bundled DLL throws instead of silently falling back.
ConPtyApi load_conpty(const std::filesystem::path& exe_dir);

}  // namespace agentdesk::platform::win
