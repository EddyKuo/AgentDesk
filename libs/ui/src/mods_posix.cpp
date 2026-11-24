// mods_os.hpp on macOS (ADR-30): mod.dylib loaded by absolute path, calls guarded by fault signals.
#include <csetjmp>
#include <csignal>

#include <dlfcn.h>

#include "mods_os.hpp"

namespace agentdesk::ui::mods_os {

namespace {

// ---- calls into a mod, guarded against crashes (macOS): while a mod's code runs, a fault signal
// (SIGSEGV, SIGBUS, SIGILL, SIGFPE) jumps back here and the call reports it like the Windows
// exception code, instead of ending agent-ui. Best effort, as there: the mod is turned off and its
// code stays loaded. These functions call only C function pointers, so no destructor is skipped.

thread_local sigjmp_buf* t_guard = nullptr;

void on_fault(int sig) {
    if (t_guard != nullptr)
        siglongjmp(*t_guard, sig);
    std::signal(sig, SIG_DFL);  // not inside a mod: crash as usual
    std::raise(sig);
}

template <class F>
Code guarded(F&& f) {
    static constexpr int kSignals[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE};
    struct sigaction sa{};
    sa.sa_handler = on_fault;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_NODEFER;
    struct sigaction old[4]{};
    for (int i = 0; i < 4; ++i)
        sigaction(kSignals[i], &sa, &old[i]);
    sigjmp_buf jb;
    sigjmp_buf* const outer = t_guard;
    Code code = 0;
    if (const int sig = sigsetjmp(jb, 1); sig == 0) {
        t_guard = &jb;
        f();
    } else {
        code = 0x80000000ul | static_cast<Code>(sig);
    }
    t_guard = outer;
    for (int i = 0; i < 4; ++i)
        sigaction(kSignals[i], &old[i], nullptr);
    return code;
}

}  // namespace

Code seh_init(agentdesk_mod_init_fn fn, const agentdesk_host* h, agentdesk_mod* m, int* rc) {
    return guarded([&] { *rc = fn(h, m); });
}
Code seh_event(const agentdesk_mod* m, const char* e, const char* p) {
    return guarded([&] { m->on_event(m->self, e, p); });
}
Code seh_panels(const agentdesk_mod* m, const char* where, const char** out) {
    return guarded([&] { *out = m->panels(m->self, where); });
}
Code seh_click(const agentdesk_mod* m, const char* panel, const char* widget) {
    return guarded([&] { m->click(m->self, panel, widget); });
}
Code seh_decorate(const agentdesk_mod* m, const char* kind, const char* subject, const char** out) {
    return guarded([&] { *out = m->decorate(m->self, kind, subject); });
}
Code seh_command(const agentdesk_mod* m, const char* id) {
    return guarded([&] { m->command(m->self, id); });
}
Code seh_destroy(const agentdesk_mod* m) {
    return guarded([&] { m->destroy(m->self); });
}
// Copies a mod-owned string; a bad pointer counts as a crash.
Code seh_copy(const char* s, std::size_t max, char* buf, std::size_t* len) {
    return guarded([&] {
        std::size_t n = 0;
        while (n < max && s[n] != '\0') {
            buf[n] = s[n];
            ++n;
        }
        *len = n;
    });
}

const char* library_file() {
    return "mod.dylib";
}

// Its symbols stay private to it.
void* load_library(const std::filesystem::path& folder, std::string* error) {
    void* dll = ::dlopen((folder / "mod.dylib").c_str(), RTLD_NOW | RTLD_LOCAL);
    if (dll == nullptr) {
        const char* why = ::dlerror();
        *error = std::string("cannot load mod.dylib (") + (why != nullptr ? why : "unknown error") + ")";
    }
    return dll;
}

void* find_symbol(void* library, const char* name) {
    return ::dlsym(library, name);
}

void unload_library(void* library) {
    ::dlclose(library);
}

}  // namespace agentdesk::ui::mods_os
