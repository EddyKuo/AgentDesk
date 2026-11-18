#include "agentdesk/platform/win/process_tree.hpp"

// tlhelp32.h and winternl.h need windows.h first.
#include <algorithm>
#include <cwctype>
#include <map>
#include <set>
#include <vector>

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

std::wstring process_cwd(DWORD pid) {
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
    // x64 layouts: PEB.ProcessParameters at 0x20; RTL_USER_PROCESS_PARAMETERS.CurrentDirectory
    // (CURDIR: UNICODE_STRING DosPath + HANDLE) at 0x38.
    const auto* peb = reinterpret_cast<const std::byte*>(pbi.PebBaseAddress);
    const std::byte* params = nullptr;
    if (!read_remote(h.get(), peb + 0x20, params) || params == nullptr)
        return {};
    UNICODE_STRING dos{};
    if (!read_remote(h.get(), params + 0x38, dos) || dos.Buffer == nullptr || dos.Length == 0 ||
        dos.Length > 32767 * sizeof(wchar_t))
        return {};
    std::wstring path(dos.Length / sizeof(wchar_t), L'\0');
    SIZE_T got = 0;
    if (!::ReadProcessMemory(h.get(), dos.Buffer, path.data(), dos.Length, &got) || got != dos.Length)
        return {};
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
    return out;
}

std::string foreground_program(HANDLE job, DWORD root_pid) {
    return foreground(job, root_pid).program;
}

}  // namespace agentdesk::platform::win
