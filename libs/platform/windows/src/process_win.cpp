// process.hpp on Windows: child processes with pipes, background processes, PATH lookup.
#include <windows.h>

#include "agentdesk/platform/process.hpp"
#include "agentdesk/platform/win/command_line.hpp"

namespace agentdesk::platform {

bool is_launchable_file(const std::filesystem::path& path) {
    return win::is_launchable_file(path.native());
}

bool is_batch_file(const std::filesystem::path& path) {
    return win::is_batch_file(path.native());
}

}  // namespace agentdesk::platform
