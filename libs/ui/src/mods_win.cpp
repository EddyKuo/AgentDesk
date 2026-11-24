// mods_os.hpp on Windows: calls guarded by __try.
#include <windows.h>

#include "mods_os.hpp"

namespace agentdesk::ui::mods_os {

// ---- calls into a mod, guarded against crashes (structured exceptions). These functions hold
// no C++ objects, so __try is allowed; a fault returns its exception code instead of ending
// agent-ui.exe.
Code seh_init(agentdesk_mod_init_fn fn, const agentdesk_host* h, agentdesk_mod* m, int* rc) {
    __try {
        *rc = fn(h, m);
        return 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return GetExceptionCode();
    }
}
Code seh_event(const agentdesk_mod* m, const char* e, const char* p) {
    __try {
        m->on_event(m->self, e, p);
        return 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return GetExceptionCode();
    }
}
Code seh_panels(const agentdesk_mod* m, const char* where, const char** out) {
    __try {
        *out = m->panels(m->self, where);
        return 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return GetExceptionCode();
    }
}
Code seh_click(const agentdesk_mod* m, const char* panel, const char* widget) {
    __try {
        m->click(m->self, panel, widget);
        return 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return GetExceptionCode();
    }
}
Code seh_decorate(const agentdesk_mod* m, const char* kind, const char* subject, const char** out) {
    __try {
        *out = m->decorate(m->self, kind, subject);
        return 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return GetExceptionCode();
    }
}
Code seh_command(const agentdesk_mod* m, const char* id) {
    __try {
        m->command(m->self, id);
        return 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return GetExceptionCode();
    }
}
Code seh_destroy(const agentdesk_mod* m) {
    __try {
        m->destroy(m->self);
        return 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return GetExceptionCode();
    }
}
// Copies a mod-owned string; a bad pointer counts as a crash.
Code seh_copy(const char* s, std::size_t max, char* buf, std::size_t* len) {
    __try {
        std::size_t n = 0;
        while (n < max && s[n] != '\0') {
            buf[n] = s[n];
            ++n;
        }
        *len = n;
        return 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return GetExceptionCode();
    }
}

}  // namespace agentdesk::ui::mods_os
