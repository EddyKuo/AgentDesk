#pragma once

// Loading a mod's library and calling into it guarded against crashes, per system:
// mods_win.cpp (LoadLibraryExW, structured exceptions) and mods_posix.cpp (dlopen, fault
// signals; ADR-30). CMake builds one of them.

#include <cstddef>
#include <filesystem>
#include <string>

#include "agentdesk/mod_api.h"

namespace agentdesk::ui::mods_os {

// 0, or what stopped the call: the exception code (Windows) or 0x80000000 | the signal (macOS).
using Code = unsigned long;

Code seh_init(agentdesk_mod_init_fn fn, const agentdesk_host* h, agentdesk_mod* m, int* rc);
Code seh_event(const agentdesk_mod* m, const char* e, const char* p);
Code seh_panels(const agentdesk_mod* m, const char* where, const char** out);
Code seh_click(const agentdesk_mod* m, const char* panel, const char* widget);
Code seh_decorate(const agentdesk_mod* m, const char* kind, const char* subject, const char** out);
Code seh_command(const agentdesk_mod* m, const char* id);
Code seh_destroy(const agentdesk_mod* m);
// Copies a mod-owned string; a bad pointer counts as a crash.
Code seh_copy(const char* s, std::size_t max, char* buf, std::size_t* len);

// The library's file name in a mod's folder: mod.dll, mod.dylib.
const char* library_file();
// Loads <folder>/<library_file()>; nullptr with `error` set when that fails.
void* load_library(const std::filesystem::path& folder, std::string* error);
void* find_symbol(void* library, const char* name);
void unload_library(void* library);

}  // namespace agentdesk::ui::mods_os
