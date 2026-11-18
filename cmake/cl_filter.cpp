// cl_filter: runs cl.exe and rewrites its localized "/showIncludes" note to a fixed ASCII prefix
// ("Note: including file:"), which the Ninja rules expect (CMAKE_CL_SHOWINCLUDES_PREFIX).
//
// Why: this machine has only the zh-TW compiler resources (VSLANG=1033 cannot switch them), and
// cl writes that note sometimes in the console's OEM code page and sometimes in UTF-8. Ninja
// matches the prefix byte for byte, so mismatched lines dropped every header dependency of that
// file and later builds linked stale objects. Built at configure time (cmake/AgentDeskClFilter);
// nothing is installed.
//
//   cl_filter <cl.exe> <args...>
// Everything else passes through unchanged; the exit code is cl's.
#include <windows.h>

#include <string>
#include <vector>

namespace {

constexpr char kAscii[] = "Note: including file:";

std::string narrow(const std::wstring& w, UINT cp) {
    if (w.empty())
        return {};
    const int n = ::WideCharToMultiByte(cp, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(n), '\0');
    ::WideCharToMultiByte(cp, 0, w.data(), static_cast<int>(w.size()), out.data(), n, nullptr, nullptr);
    return out;
}

// The localized prefixes to look for: the zh-TW text, and English, in UTF-8 and the OEM/ANSI
// pages (whichever cl picked for this run).
std::vector<std::string> prefixes() {
    const std::wstring texts[] = {L"注意: 包含檔案:", L"Note: including file:"};
    std::vector<std::string> out;
    for (const auto& t : texts)
        for (const UINT cp : {static_cast<UINT>(CP_UTF8), ::GetOEMCP(), ::GetACP()})
            out.push_back(narrow(t, cp));
    return out;
}

std::wstring quote(const std::wstring& a) {
    if (!a.empty() && a.find_first_of(L" \t\"") == std::wstring::npos)
        return a;
    std::wstring out = L"\"";
    int slashes = 0;
    for (const wchar_t c : a) {
        if (c == L'\\') {
            ++slashes;
            continue;
        }
        out.append(static_cast<std::size_t>(c == L'"' ? slashes * 2 + 1 : slashes), L'\\');
        slashes = 0;
        out += c;
    }
    out.append(static_cast<std::size_t>(slashes * 2), L'\\');
    return out + L"\"";
}

void write_all(HANDLE h, const std::string& s) {
    DWORD done = 0;
    std::size_t at = 0;
    while (at < s.size() && ::WriteFile(h, s.data() + at, static_cast<DWORD>(s.size() - at), &done, nullptr) && done > 0)
        at += done;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 2)
        return 2;
    std::wstring cmd;
    for (int i = 1; i < argc; ++i)
        cmd += (i > 1 ? L" " : L"") + quote(argv[i]);

    SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
    HANDLE rd = nullptr;
    HANDLE wr = nullptr;
    if (!::CreatePipe(&rd, &wr, &sa, 0))
        return 3;
    ::SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW si{};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = ::GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = wr;
    si.hStdError = ::GetStdHandle(STD_ERROR_HANDLE);
    PROCESS_INFORMATION pi{};
    if (!::CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &si, &pi)) {
        ::CloseHandle(rd);
        ::CloseHandle(wr);
        return 4;
    }
    ::CloseHandle(wr);
    ::CloseHandle(pi.hThread);

    const auto known = prefixes();
    HANDLE out = ::GetStdHandle(STD_OUTPUT_HANDLE);
    std::string pending;
    auto emit_line = [&](std::string line) {
        for (const auto& p : known)
            if (!p.empty() && line.compare(0, p.size(), p) == 0) {
                line = kAscii + line.substr(p.size());
                break;
            }
        write_all(out, line);
    };
    char buf[8192];
    DWORD n = 0;
    while (::ReadFile(rd, buf, sizeof buf, &n, nullptr) && n > 0) {
        pending.append(buf, n);
        std::size_t nl = 0;
        while ((nl = pending.find('\n')) != std::string::npos) {
            emit_line(pending.substr(0, nl + 1));
            pending.erase(0, nl + 1);
        }
    }
    if (!pending.empty())
        emit_line(pending);
    ::CloseHandle(rd);
    ::WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    ::GetExitCodeProcess(pi.hProcess, &code);
    ::CloseHandle(pi.hProcess);
    return static_cast<int>(code);
}
