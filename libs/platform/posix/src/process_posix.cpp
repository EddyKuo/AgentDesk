// Child processes through posix_spawn. POSIX_SPAWN_CLOEXEC_DEFAULT makes the child inherit only
// the descriptors named in the file actions.
#include <algorithm>
#include <array>
#include <cstdlib>
#include <stdexcept>
#include <thread>

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "process_impl.hpp"

namespace agentdesk::platform {

using posix::UniqueFd;

namespace {

// RAII for the spawn attribute and file action objects.
struct SpawnSetup {
    posix_spawnattr_t attr{};
    posix_spawn_file_actions_t actions{};
    SpawnSetup() {
        posix_spawnattr_init(&attr);
        posix_spawn_file_actions_init(&actions);
    }
    ~SpawnSetup() {
        posix_spawn_file_actions_destroy(&actions);
        posix_spawnattr_destroy(&attr);
    }
    SpawnSetup(const SpawnSetup&) = delete;
    SpawnSetup& operator=(const SpawnSetup&) = delete;
    SpawnSetup(SpawnSetup&&) = delete;
    SpawnSetup& operator=(SpawnSetup&&) = delete;

    // Default signal handling and an empty mask in the child; `flags` adds to that.
    void set_flags(short flags) {
        sigset_t all;
        sigfillset(&all);
        posix_spawnattr_setsigdefault(&attr, &all);
        sigset_t none;
        sigemptyset(&none);
        posix_spawnattr_setsigmask(&attr, &none);
        posix_spawnattr_setflags(&attr, static_cast<short>(flags | POSIX_SPAWN_CLOEXEC_DEFAULT |
                                                           POSIX_SPAWN_SETSIGDEF | POSIX_SPAWN_SETSIGMASK));
    }
};

pid_t do_spawn(const std::filesystem::path& exe, const std::vector<std::string>& args,
               const std::map<std::string, std::string>& env_overrides, SpawnSetup& setup) {
    std::vector<std::string> argv_strings;
    argv_strings.push_back(exe.string());
    argv_strings.insert(argv_strings.end(), args.begin(), args.end());
    std::vector<std::string> env_strings = posix::make_environment(env_overrides);
    std::vector<char*> argv = posix::c_string_array(argv_strings);
    std::vector<char*> envp = posix::c_string_array(env_strings);
    pid_t pid = -1;
    const int err = ::posix_spawn(&pid, exe.c_str(), &setup.actions, &setup.attr, argv.data(), envp.data());
    if (err != 0)
        throw std::system_error(err, std::generic_category(), "posix_spawn " + exe.string());
    return pid;
}

void reap_in_background(pid_t pid) {
    std::thread([pid] {
        int status = 0;
        while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {
        }
    }).detach();
}

}  // namespace

bool Process::Impl::poll_exit() {
    std::lock_guard lock(mutex);
    if (exit_code)
        return true;
    int status = 0;
    const pid_t r = ::waitpid(pid, &status, WNOHANG);
    if (r == pid) {
        exit_code = posix::exit_code_from_status(status);
        return true;
    }
    if (r < 0 && errno == ECHILD) {
        exit_code = -1;  // reaped elsewhere
        return true;
    }
    return false;
}

void Process::Impl::signal(int sig) const {
    if (own_group)
        ::killpg(pid, sig);
    else
        ::kill(pid, sig);
}

Process::Process(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

Process::~Process() {
    impl_->in_write.reset();
    impl_->out_read.reset();
    if (impl_->poll_exit())
        return;
    if (impl_->own_group) {
        impl_->signal(SIGKILL);
        wait(std::chrono::seconds(5));
        if (!impl_->exit_code)
            reap_in_background(impl_->pid);
    } else {
        reap_in_background(impl_->pid);
    }
}

std::unique_ptr<Process> Process::spawn(const ProcessSpec& spec) {
    posix::check_launchable(spec.executable);
    auto impl = std::make_unique<Impl>();
    SpawnSetup setup;
    setup.set_flags(spec.end_with_parent ? POSIX_SPAWN_SETPGROUP : 0);
    if (spec.end_with_parent)
        posix_spawnattr_setpgroup(&setup.attr, 0);

    UniqueFd child_in;
    UniqueFd child_out;
    if (spec.pipe_stdin) {
        auto [rd, wr] = posix::make_pipe();
        child_in = std::move(rd);
        impl->in_write = std::move(wr);
        posix_spawn_file_actions_adddup2(&setup.actions, child_in.get(), 0);
    } else {
        posix_spawn_file_actions_addopen(&setup.actions, 0, "/dev/null", O_RDONLY, 0);
    }
    if (spec.pipe_stdout) {
        auto [rd, wr] = posix::make_pipe();
        child_out = std::move(wr);
        impl->out_read = std::move(rd);
        posix_spawn_file_actions_adddup2(&setup.actions, child_out.get(), 1);
    } else {
        posix_spawn_file_actions_addopen(&setup.actions, 1, "/dev/null", O_WRONLY, 0);
    }
    if (spec.merge_stderr && spec.pipe_stdout)
        posix_spawn_file_actions_adddup2(&setup.actions, child_out.get(), 2);
    else if (spec.stderr_fd >= 0)
        posix_spawn_file_actions_adddup2(&setup.actions, spec.stderr_fd, 2);
    else
        posix_spawn_file_actions_addopen(&setup.actions, 2, "/dev/null", O_WRONLY, 0);
    if (!spec.cwd.empty())
        posix_spawn_file_actions_addchdir_np(&setup.actions, spec.cwd.c_str());

    impl->pid = do_spawn(spec.executable, spec.args, spec.env_overrides, setup);
    impl->own_group = spec.end_with_parent;
    return std::unique_ptr<Process>(new Process(std::move(impl)));
}

bool Process::write(std::string_view bytes) {
    if (!impl_->in_write.valid())
        return false;
    return posix::write_fully(impl_->in_write.get(), bytes.data(), bytes.size());
}

void Process::close_stdin() {
    impl_->in_write.reset();
}

std::optional<std::string> Process::read_some(std::size_t max) {
    if (!impl_->out_read.valid() || max == 0)
        return std::nullopt;
    std::string buf(max, '\0');
    for (;;) {
        const ssize_t n = ::read(impl_->out_read.get(), buf.data(), max);
        if (n > 0) {
            buf.resize(static_cast<std::size_t>(n));
            return buf;
        }
        if (n < 0 && errno == EINTR)
            continue;
        return std::nullopt;
    }
}

std::optional<int> Process::wait(std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    auto step = std::chrono::milliseconds(1);
    for (;;) {
        if (impl_->poll_exit()) {
            std::lock_guard lock(impl_->mutex);
            return impl_->exit_code;
        }
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline)
            return std::nullopt;
        std::this_thread::sleep_for(std::min<std::chrono::steady_clock::duration>(step, deadline - now));
        step = std::min(step * 2, std::chrono::milliseconds(50));
    }
}

void Process::kill() {
    if (!impl_->poll_exit())
        impl_->signal(SIGKILL);
}

std::uint32_t Process::pid() const noexcept {
    return static_cast<std::uint32_t>(impl_->pid);
}

RunResult run_capture(const ProcessSpec& spec, std::chrono::milliseconds timeout) {
    ProcessSpec s = spec;
    s.pipe_stdout = true;
    s.end_with_parent = true;
    auto child = Process::spawn(s);
    const int fd = child->impl().out_read.get();
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    RunResult result;
    std::array<char, 16 * 1024> buf{};
    for (;;) {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now());
        if (left.count() <= 0) {
            result.timed_out = true;
            break;
        }
        pollfd p{fd, POLLIN, 0};
        const int r = ::poll(&p, 1, static_cast<int>(left.count()));
        if (r < 0 && errno == EINTR)
            continue;
        if (r <= 0) {
            result.timed_out = r == 0;
            break;
        }
        const ssize_t n = ::read(fd, buf.data(), buf.size());
        if (n > 0) {
            result.out.append(buf.data(), static_cast<std::size_t>(n));
            continue;
        }
        if (n < 0 && errno == EINTR)
            continue;
        break;  // EOF
    }
    if (result.timed_out) {
        child->kill();
        child->wait(std::chrono::seconds(5));
        return result;
    }
    const auto left =
        std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
    const auto code = child->wait(std::max(left, std::chrono::milliseconds(0)));
    if (!code) {
        result.timed_out = true;
        child->kill();
        child->wait(std::chrono::seconds(5));
        return result;
    }
    result.exit_code = *code;
    return result;
}

DetachedResult spawn_detached(const std::filesystem::path& exe, const std::vector<std::string>& args,
                              const std::map<std::string, std::string>& env_overrides) {
    posix::check_launchable(exe);
    SpawnSetup setup;
    setup.set_flags(POSIX_SPAWN_SETSID);
    posix_spawn_file_actions_addopen(&setup.actions, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&setup.actions, 1, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&setup.actions, 2, "/dev/null", O_WRONLY, 0);
    const pid_t pid = do_spawn(exe, args, env_overrides, setup);
    reap_in_background(pid);  // no zombie while the launcher keeps running
    DetachedResult r;
    r.pid = static_cast<std::uint32_t>(pid);
    return r;
}

bool is_launchable_file(const std::filesystem::path& path) {
    struct stat st{};
    if (path.empty() || ::stat(path.c_str(), &st) != 0)
        return false;
    return S_ISREG(st.st_mode) && ::access(path.c_str(), X_OK) == 0;
}

std::optional<std::filesystem::path> find_executable(std::string_view name) {
    if (name.empty())
        return std::nullopt;
    if (name.find('/') != std::string_view::npos) {
        const std::filesystem::path p(name);
        if (is_launchable_file(p))
            return p;
        return std::nullopt;
    }
    const char* env = std::getenv("PATH");
    const std::string path = env != nullptr && *env != '\0' ? env : "/usr/bin:/bin:/usr/sbin:/sbin";
    std::size_t start = 0;
    while (start <= path.size()) {
        const auto end = std::min(path.find(':', start), path.size());
        const std::string dir = path.substr(start, end - start);
        if (!dir.empty()) {
            const auto candidate = std::filesystem::path(dir) / std::string(name);
            if (is_launchable_file(candidate))
                return candidate;
        }
        start = end + 1;
    }
    return std::nullopt;
}

bool is_batch_file(const std::filesystem::path& /*path*/) {
    return false;
}

}  // namespace agentdesk::platform
