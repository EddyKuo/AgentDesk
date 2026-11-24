#include "posix_util.hpp"

#include <stdexcept>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include "agentdesk/platform/process.hpp"

extern char** environ;  // NOLINT(readability-redundant-declaration): not declared by unistd.h on macOS

namespace agentdesk::platform::posix {

void set_cloexec(int fd) {
    const int flags = ::fcntl(fd, F_GETFD);
    if (flags < 0 || ::fcntl(fd, F_SETFD, flags | FD_CLOEXEC) < 0)
        throw_errno("fcntl(FD_CLOEXEC)");
}

std::pair<UniqueFd, UniqueFd> make_pipe() {
    int fds[2] = {-1, -1};
    if (::pipe(fds) != 0)
        throw_errno("pipe");
    UniqueFd rd(fds[0]);
    UniqueFd wr(fds[1]);
    set_cloexec(rd.get());
    set_cloexec(wr.get());
    return {std::move(rd), std::move(wr)};
}

std::vector<std::string> make_environment(const std::map<std::string, std::string>& overrides) {
    std::map<std::string, std::string> env;
    for (char** e = environ; e != nullptr && *e != nullptr; ++e) {
        const std::string entry(*e);
        const auto eq = entry.find('=');
        if (eq == std::string::npos || eq == 0)
            continue;
        env[entry.substr(0, eq)] = entry.substr(eq + 1);
    }
    for (const auto& [name, value] : overrides) {
        if (value.empty())
            env.erase(name);
        else
            env[name] = value;
    }
    std::vector<std::string> out;
    out.reserve(env.size());
    for (const auto& [name, value] : env)
        out.push_back(name + "=" + value);
    return out;
}

std::vector<char*> c_string_array(std::vector<std::string>& strings) {
    std::vector<char*> out;
    out.reserve(strings.size() + 1);
    for (auto& s : strings)
        out.push_back(s.data());
    out.push_back(nullptr);
    return out;
}

void check_launchable(const std::filesystem::path& path) {
    if (!is_launchable_file(path))
        throw std::invalid_argument("not an executable file: " + path.string());
}

bool write_fully(int fd, const char* data, std::size_t size) {
    while (size > 0) {
        const ssize_t n = ::write(fd, data, size);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return false;
        }
        data += n;
        size -= static_cast<std::size_t>(n);
    }
    return true;
}

int exit_code_from_status(int status) {
    if (WIFEXITED(status))
        return WEXITSTATUS(status);
    if (WIFSIGNALED(status))
        return 128 + WTERMSIG(status);
    return -1;
}

}  // namespace agentdesk::platform::posix
