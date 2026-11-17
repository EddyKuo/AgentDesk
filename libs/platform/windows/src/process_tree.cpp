#include "agentdesk/platform/win/process_tree.hpp"

// tlhelp32.h needs windows.h first.
#include <algorithm>
#include <cwctype>
#include <map>
#include <set>
#include <vector>

#include <tlhelp32.h>

#include "agentdesk/platform/win/error.hpp"

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

}  // namespace

std::string foreground_program(HANDLE job, DWORD root_pid) {
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

    auto ignored = [](const std::wstring& exe) { return exe == L"conhost.exe" || exe == L"openconsole.exe"; };
    // Depth from the root; the job list is in creation order, so later entries are newer.
    std::wstring best;
    int best_depth = -1;
    for (const DWORD pid : pids) {
        const auto it = procs.find(pid);
        if (it == procs.end() || ignored(it->second.exe))
            continue;
        int depth = 0;
        DWORD cur = pid;
        bool reaches_root = cur == root_pid;
        for (int guard = 0; guard < 64 && !reaches_root; ++guard) {
            const auto p = procs.find(cur);
            if (p == procs.end())
                break;
            cur = p->second.parent;
            ++depth;
            reaches_root = cur == root_pid;
        }
        if (!reaches_root)
            continue;
        if (depth >= best_depth) {
            best_depth = depth;
            best = it->second.exe;
        }
    }
    if (best.size() > 4 && best.ends_with(L".exe"))
        best.resize(best.size() - 4);
    return to_utf8(best);
}

}  // namespace agentdesk::platform::win
