#include "agentdesk/platform/win/process_tree.hpp"

// tlhelp32.h and winternl.h need windows.h first.
#include <algorithm>
#include <cwctype>
#include <map>
#include <memory>
#include <set>
#include <vector>

#include <shellapi.h>
#include <tlhelp32.h>
#include <winternl.h>

#include "agentdesk/platform/win/error.hpp"
#include "agentdesk/platform/win/unique_handle.hpp"

namespace agentdesk::platform::win {

namespace {

std::vector<DWORD> job_pids(HANDLE job) {
    struct List {
        JOBOBJECT_BASIC_PROCESS_ID_LIST header;
        ULONG_PTR more[255];
    } list{};
    list.header.NumberOfAssignedProcesses = 256;
    if (!::QueryInformationJobObject(job, JobObjectBasicProcessIdList, &list, sizeof(list), nullptr) &&
        ::GetLastError() != ERROR_MORE_DATA) {
        return {};
    }
    std::vector<DWORD> out;
    for (DWORD i = 0; i < list.header.NumberOfProcessIdsInList && i < 256; ++i)
        out.push_back(static_cast<DWORD>(list.header.ProcessIdList[i]));
    return out;
}

std::wstring lower(std::wstring s) {
    for (auto& c : s)
        c = static_cast<wchar_t>(std::towlower(c));
    return s;
}

std::wstring strip_exe(std::wstring s) {
    if (s.size() > 4 && s.ends_with(L".exe"))
        s.resize(s.size() - 4);
    return s;
}

using NtQueryInformationProcessFn = LONG(NTAPI*)(HANDLE, PROCESSINFOCLASS, PVOID, ULONG, PULONG);

NtQueryInformationProcessFn nt_query() {
    static const auto fn = reinterpret_cast<NtQueryInformationProcessFn>(
        reinterpret_cast<void*>(::GetProcAddress(::GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationProcess")));
    return fn;
}

template <typename T>
bool read_remote(HANDLE process, const void* address, T& out) {
    SIZE_T got = 0;
    return ::ReadProcessMemory(process, address, &out, sizeof(T), &got) && got == sizeof(T);
}

}  // namespace

bool is_shell_name(const std::wstring& name) {
    static const std::set<std::wstring> kShells = {L"pwsh",  L"powershell", L"cmd",    L"bash",   L"zsh",
                                                   L"fish",  L"sh",         L"dash",   L"nu",     L"elvish",
                                                   L"xonsh", L"tcsh",       L"csh",    L"ksh",    L"ion"};
    return kShells.contains(strip_exe(lower(name)));
}

namespace {

// One UNICODE_STRING inside RTL_USER_PROCESS_PARAMETERS of a 64-bit process (x64 offsets:
// PEB.ProcessParameters at 0x20; CurrentDirectory.DosPath at 0x38, CommandLine at 0x70).
std::wstring read_process_parameter(DWORD pid, std::size_t offset) {
    const auto query = nt_query();
    if (query == nullptr)
        return {};
    UniqueHandle h(::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, pid));
    if (!h)
        return {};
    BOOL wow64 = FALSE;
    if (!::IsWow64Process(h.get(), &wow64) || wow64)
        return {};  // the 32-bit PEB layout differs
    PROCESS_BASIC_INFORMATION pbi{};
    ULONG len = 0;
    if (query(h.get(), ProcessBasicInformation, &pbi, sizeof(pbi), &len) != 0 || pbi.PebBaseAddress == nullptr)
        return {};
    const auto* peb = reinterpret_cast<const std::byte*>(pbi.PebBaseAddress);
    const std::byte* params = nullptr;
    if (!read_remote(h.get(), peb + 0x20, params) || params == nullptr)
        return {};
    UNICODE_STRING str{};
    if (!read_remote(h.get(), params + offset, str) || str.Buffer == nullptr || str.Length == 0 ||
        str.Length > 32767 * sizeof(wchar_t))
        return {};
    std::wstring out(str.Length / sizeof(wchar_t), L'\0');
    SIZE_T got = 0;
    if (!::ReadProcessMemory(h.get(), str.Buffer, out.data(), str.Length, &got) || got != str.Length)
        return {};
    return out;
}

std::vector<std::string> split_command_line(const std::wstring& line) {
    std::vector<std::string> out;
    if (line.empty())
        return out;
    int n = 0;
    // Freed even when to_utf8 throws (a lone surrogate in another program's command line).
    const std::unique_ptr<wchar_t*, decltype(&::LocalFree)> words(::CommandLineToArgvW(line.c_str(), &n),
                                                                  &::LocalFree);
    if (!words)
        return out;
    for (int i = 0; i < n; ++i)
        out.push_back(to_utf8(words.get()[i]));
    return out;
}

}  // namespace

std::wstring process_command_line(DWORD pid) {
    return read_process_parameter(pid, 0x70);
}

std::wstring process_cwd(DWORD pid) {
    std::wstring path = read_process_parameter(pid, 0x38);
    if (path.size() > 3 && path.back() == L'\\')
        path.pop_back();
    return path;
}

Foreground foreground(HANDLE job, DWORD root_pid) {
    const auto pids = job_pids(job);
    if (pids.empty())
        return {};
    const std::set<DWORD> in_job(pids.begin(), pids.end());
    struct Proc {
        DWORD parent = 0;
        std::wstring exe;
    };
    std::map<DWORD, Proc> procs;
    HANDLE snap = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return {};
    PROCESSENTRY32W pe{sizeof(pe)};
    for (BOOL ok = ::Process32FirstW(snap, &pe); ok; ok = ::Process32NextW(snap, &pe)) {
        if (in_job.contains(pe.th32ProcessID))
            procs[pe.th32ProcessID] = {pe.th32ParentProcessID, lower(pe.szExeFile)};
    }
    ::CloseHandle(snap);
    const auto root = procs.find(root_pid);
    if (root == procs.end())
        return {};
    auto ignored = [](const std::wstring& exe) { return exe == L"conhost.exe" || exe == L"openconsole.exe"; };
    DWORD front = root_pid;
    if (is_shell_name(root->second.exe)) {
        // The job list is in creation order: the last direct child is the newest.
        for (const DWORD pid : pids) {
            const auto it = procs.find(pid);
            if (pid != root_pid && it != procs.end() && it->second.parent == root_pid && !ignored(it->second.exe))
                front = pid;
        }
    }
    Foreground out;
    out.program = to_utf8(strip_exe(procs[front].exe));
    std::wstring cwd = process_cwd(front);
    if (cwd.empty() && front != root_pid)
        cwd = process_cwd(root_pid);
    out.cwd = to_utf8(cwd);
    out.argv = split_command_line(process_command_line(front));
    return out;
}

std::string foreground_program(HANDLE job, DWORD root_pid) {
    return foreground(job, root_pid).program;
}

}  // namespace agentdesk::platform::win
