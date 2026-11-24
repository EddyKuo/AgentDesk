#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <string>

#include <libproc.h>
#include <sys/proc_info.h>
#include <sys/sysctl.h>

#include "proc_info.hpp"

namespace agentdesk::platform::posix {

namespace {

bool is_zombie(pid_t pid) {
    proc_bsdinfo info{};
    if (::proc_pidinfo(pid, PROC_PIDTBSDINFO, 0, &info, sizeof(info)) != static_cast<int>(sizeof(info)))
        return true;  // gone, or not ours to read: not counted as running in the terminal
    return info.pbi_status == SZOMB;
}

std::string program_name(pid_t pid) {
    std::array<char, PROC_PIDPATHINFO_MAXSIZE> path{};
    if (::proc_pidpath(pid, path.data(), static_cast<std::uint32_t>(path.size())) <= 0)
        return {};
    std::string name = std::filesystem::path(path.data()).stem().string();
    std::transform(name.begin(), name.end(), name.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return name;
}

std::string current_folder(pid_t pid) {
    proc_vnodepathinfo info{};
    if (::proc_pidinfo(pid, PROC_PIDVNODEPATHINFO, 0, &info, sizeof(info)) != static_cast<int>(sizeof(info)))
        return {};
    return info.pvi_cdir.vip_path;
}

// KERN_PROCARGS2: int argc, the exec path, NUL padding, then argc NUL-terminated words.
std::vector<std::string> arguments(pid_t pid) {
    int mib[3] = {CTL_KERN, KERN_PROCARGS2, static_cast<int>(pid)};
    std::size_t size = 0;
    if (::sysctl(mib, 3, nullptr, &size, nullptr, 0) != 0 || size < sizeof(int))
        return {};
    std::string buf(size, '\0');
    if (::sysctl(mib, 3, buf.data(), &size, nullptr, 0) != 0 || size < sizeof(int))
        return {};
    buf.resize(size);
    int argc = 0;
    std::memcpy(&argc, buf.data(), sizeof(argc));
    std::size_t pos = sizeof(argc);
    pos = buf.find('\0', pos);  // skip the exec path
    if (pos == std::string::npos)
        return {};
    while (pos < buf.size() && buf[pos] == '\0')
        ++pos;
    std::vector<std::string> out;
    while (static_cast<int>(out.size()) < argc && pos < buf.size()) {
        const auto end = buf.find('\0', pos);
        if (end == std::string::npos)
            break;
        out.push_back(buf.substr(pos, end - pos));
        pos = end + 1;
    }
    return out;
}

}  // namespace

std::vector<pid_t> process_group_members(pid_t group) {
    if (group <= 0)
        return {};
    const int bytes = ::proc_listpids(PROC_PGRP_ONLY, static_cast<std::uint32_t>(group), nullptr, 0);
    if (bytes <= 0)
        return {};
    std::vector<pid_t> pids(static_cast<std::size_t>(bytes) / sizeof(pid_t) + 16);
    const int got = ::proc_listpids(PROC_PGRP_ONLY, static_cast<std::uint32_t>(group), pids.data(),
                                    static_cast<int>(pids.size() * sizeof(pid_t)));
    if (got <= 0)
        return {};
    pids.resize(static_cast<std::size_t>(got) / sizeof(pid_t));
    std::erase_if(pids, [](pid_t p) { return p <= 0 || is_zombie(p); });
    std::sort(pids.begin(), pids.end());
    return pids;
}

std::vector<pid_t> terminal_members(dev_t tty) {
    if (tty == 0)
        return {};
    const auto dev = static_cast<std::uint32_t>(tty);
    const int bytes = ::proc_listpids(PROC_TTY_ONLY, dev, nullptr, 0);
    if (bytes <= 0)
        return {};
    std::vector<pid_t> pids(static_cast<std::size_t>(bytes) / sizeof(pid_t) + 16);
    const int got = ::proc_listpids(PROC_TTY_ONLY, dev, pids.data(), static_cast<int>(pids.size() * sizeof(pid_t)));
    if (got <= 0)
        return {};
    pids.resize(static_cast<std::size_t>(got) / sizeof(pid_t));
    std::erase_if(pids, [](pid_t p) { return p <= 0 || is_zombie(p); });
    std::sort(pids.begin(), pids.end());
    return pids;
}

ForegroundInfo describe_process(pid_t pid) {
    ForegroundInfo info;
    info.program = program_name(pid);
    info.cwd = current_folder(pid);
    info.argv = arguments(pid);
    return info;
}

}  // namespace agentdesk::platform::posix
