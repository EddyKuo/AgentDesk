#pragma once

// Small POSIX helpers shared by the macOS implementation of the platform interfaces.

#include <cerrno>
#include <filesystem>
#include <map>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <unistd.h>

namespace agentdesk::platform::posix {

// Owns one file descriptor.
class UniqueFd {
public:
    UniqueFd() = default;
    explicit UniqueFd(int fd) noexcept : fd_(fd) {}
    ~UniqueFd() { reset(); }
    UniqueFd(const UniqueFd&) = delete;
    UniqueFd& operator=(const UniqueFd&) = delete;
    UniqueFd(UniqueFd&& other) noexcept : fd_(std::exchange(other.fd_, -1)) {}
    UniqueFd& operator=(UniqueFd&& other) noexcept {
        if (this != &other)
            reset(std::exchange(other.fd_, -1));
        return *this;
    }

    [[nodiscard]] int get() const noexcept { return fd_; }
    [[nodiscard]] bool valid() const noexcept { return fd_ >= 0; }
    int release() noexcept { return std::exchange(fd_, -1); }
    void reset(int fd = -1) noexcept {
        if (fd_ >= 0)
            ::close(fd_);
        fd_ = fd;
    }

private:
    int fd_ = -1;
};

// Must run right after the failing call (nothing in between may change errno).
[[noreturn]] inline void throw_errno(const char* what) {
    throw std::system_error(errno, std::generic_category(), what);
}

void set_cloexec(int fd);
// A pipe whose ends are both close-on-exec: {read, write}.
std::pair<UniqueFd, UniqueFd> make_pipe();

// The current environment with overrides applied ("NAME=value"); an empty value removes NAME.
std::vector<std::string> make_environment(const std::map<std::string, std::string>& overrides);

// NULL-terminated char* arrays pointing into strings that must outlive them.
std::vector<char*> c_string_array(std::vector<std::string>& strings);

// Throws std::invalid_argument unless `path` is a regular file this user may execute.
void check_launchable(const std::filesystem::path& path);

// Writes all of `data` to `fd`, retrying on EINTR. False on any other error.
bool write_fully(int fd, const char* data, std::size_t size);

// waitpid() status as an exit code: the status, or 128 + signal number.
int exit_code_from_status(int status);

}  // namespace agentdesk::platform::posix
