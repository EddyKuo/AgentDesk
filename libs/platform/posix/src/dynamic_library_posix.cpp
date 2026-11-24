// dynamic_library.hpp on macOS.
#include <dlfcn.h>

#include "agentdesk/platform/dynamic_library.hpp"

namespace agentdesk::platform {

// Its symbols stay private to it.
std::unique_ptr<DynamicLibrary> DynamicLibrary::load(const std::filesystem::path& file, std::string* error) {
    void* handle = ::dlopen(file.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr) {
        const char* why = ::dlerror();
        if (error != nullptr)
            *error = "cannot load " + file.filename().string() + " (" +
                     (why != nullptr ? why : "unknown error") + ")";
        return nullptr;
    }
    return std::unique_ptr<DynamicLibrary>(new DynamicLibrary(handle));
}

DynamicLibrary::~DynamicLibrary() {
    ::dlclose(handle_);
}

void* DynamicLibrary::symbol(const char* name) const {
    return ::dlsym(handle_, name);
}

std::string library_file(std::string_view name) {
    return std::string(name) + ".dylib";
}

}  // namespace agentdesk::platform
