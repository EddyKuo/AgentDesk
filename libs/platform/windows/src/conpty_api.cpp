#include "agentdesk/platform/win/conpty_api.hpp"

#include <system_error>

#include "agentdesk/platform/win/error.hpp"

namespace agentdesk::platform::win {

namespace {

template <typename Fn>
Fn get_proc(HMODULE module, const char* name) {
    FARPROC proc = ::GetProcAddress(module, name);
    if (proc == nullptr) {
        throw_last_error(name);
    }
    // Win32 boundary: GetProcAddress returns an untyped function pointer.
    return reinterpret_cast<Fn>(reinterpret_cast<void*>(proc));
}

ConPtyApi from_module(HMODULE module, bool bundled, std::wstring source) {
    ConPtyApi api;
    api.create = get_proc<CreatePseudoConsoleFn>(module, "CreatePseudoConsole");
    api.resize = get_proc<ResizePseudoConsoleFn>(module, "ResizePseudoConsole");
    api.close = get_proc<ClosePseudoConsoleFn>(module, "ClosePseudoConsole");
    api.bundled = bundled;
    api.source = std::move(source);
    return api;
}

}  // namespace

ConPtyApi load_system_conpty() {
    HMODULE kernel = ::GetModuleHandleW(L"kernel32.dll");
    if (kernel == nullptr) {
        throw_last_error("GetModuleHandleW(kernel32)");
    }
    return from_module(kernel, false, L"kernel32.dll");
}

ConPtyApi load_conpty(const std::filesystem::path& exe_dir) {
    const auto dll = exe_dir / L"conpty" / L"conpty.dll";
    std::error_code ec;
    if (!std::filesystem::is_regular_file(dll, ec)) {
        return load_system_conpty();
    }
    // Absolute path: avoids DLL search-order hijacking. The module stays loaded for the
    // process lifetime because HPCONs created from it may outlive any scope here.
    HMODULE module = ::LoadLibraryExW(dll.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (module == nullptr) {
        throw_last_error("LoadLibraryExW(conpty.dll)");
    }
    return from_module(module, true, dll.wstring());
}

}  // namespace agentdesk::platform::win
