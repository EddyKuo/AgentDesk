#pragma once

// Calling into a mod guarded against crashes, per system: mods_win.cpp (structured exceptions) and
// mods_posix.cpp (fault signals; ADR-30). CMake builds one of them.

#include <cstddef>

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

}  // namespace agentdesk::ui::mods_os
