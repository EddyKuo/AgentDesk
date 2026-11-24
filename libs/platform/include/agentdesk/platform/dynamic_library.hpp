#pragma once

// A library loaded while the program runs (mods): the platform-neutral interface
// (docs/design/platform-abstraction.md 5.4).

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace agentdesk::platform {

class DynamicLibrary {
public:
    // `file` is an absolute path; nullptr with `error` set when it cannot be loaded. Windows looks for
    // its dependencies in its own folder and System32 only, never the current folder or PATH.
    static std::unique_ptr<DynamicLibrary> load(const std::filesystem::path& file, std::string* error);
    ~DynamicLibrary();  // unloads it
    DynamicLibrary(const DynamicLibrary&) = delete;
    DynamicLibrary& operator=(const DynamicLibrary&) = delete;
    DynamicLibrary(DynamicLibrary&&) = delete;
    DynamicLibrary& operator=(DynamicLibrary&&) = delete;

    [[nodiscard]] void* symbol(const char* name) const;  // nullptr when it has none

private:
    explicit DynamicLibrary(void* handle) : handle_(handle) {}
    void* handle_;
};

// The file a library called `name` has on this system: mod.dll, mod.dylib.
std::string library_file(std::string_view name);

}  // namespace agentdesk::platform
