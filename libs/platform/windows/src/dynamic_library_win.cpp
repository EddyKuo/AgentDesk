// dynamic_library.hpp on Windows.
#include <windows.h>

#include "agentdesk/platform/dynamic_library.hpp"
#include "agentdesk/platform/text.hpp"

namespace agentdesk::platform {

std::unique_ptr<DynamicLibrary> DynamicLibrary::load(const std::filesystem::path& file, std::string* error) {
    HMODULE module = ::LoadLibraryExW(file.c_str(), nullptr,
                                      LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (module == nullptr) {
        const DWORD code = ::GetLastError();
        if (error != nullptr)
            *error = "cannot load " + to_utf8(file.filename().native()) + " (error " + std::to_string(code) + ")";
        return nullptr;
    }
    return std::unique_ptr<DynamicLibrary>(new DynamicLibrary(module));
}

DynamicLibrary::~DynamicLibrary() {
    ::FreeLibrary(static_cast<HMODULE>(handle_));
}

void* DynamicLibrary::symbol(const char* name) const {
    return reinterpret_cast<void*>(::GetProcAddress(static_cast<HMODULE>(handle_), name));
}

std::string library_file(std::string_view name) {
    return std::string(name) + ".dll";
}

}  // namespace agentdesk::platform
