// A stand-in agent-ui.exe for the update tests. `--version` answers "agent-ui <fake-version.txt next to it>";
// started any other way it appends a line to launched.txt next to it and ends, so a relaunch can
// be seen without opening a window.
#include <windows.h>

#include <fstream>
#include <sstream>
#include <string>

int wmain(int argc, wchar_t** argv) {
    wchar_t exe[MAX_PATH]{};
    const DWORD n = ::GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring dir(exe, n);
    dir = dir.substr(0, dir.find_last_of(L'\\'));
    if (argc == 2 && std::wstring(argv[1]) == L"--version") {
        std::ifstream in(dir + L"\\fake-version.txt", std::ios::binary);
        if (!in)
            return 1;
        std::stringstream ss;
        ss << in.rdbuf();
        std::string v = ss.str();
        while (!v.empty() && (v.back() == '\n' || v.back() == '\r'))
            v.pop_back();
        const std::string line = "agent-ui " + v + "\n";
        DWORD written = 0;
        ::WriteFile(::GetStdHandle(STD_OUTPUT_HANDLE), line.data(), static_cast<DWORD>(line.size()), &written,
                    nullptr);
        return 0;
    }
    std::ofstream(dir + L"\\launched.txt", std::ios::binary | std::ios::app) << "launched\n";
    return 0;
}
