// links_os.hpp on Windows, with open_link (ShellExecuteW).
#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cwctype>
#include <filesystem>
#include <thread>

#include <objbase.h>
#include <shellapi.h>

#include "agentdesk/platform/win/error.hpp"
#include "agentdesk/ui/links.hpp"
#include "links_os.hpp"

namespace agentdesk::ui {

namespace fs = std::filesystem;

namespace links_os {

// Every spelling counts (FILE:///C:/x, file:/C:/x, file://server/share/x); a host other than
// this computer names a UNC path (\\server\share\x).
std::optional<std::string> local_file_path(std::string bytes, const std::string& host) {
    std::replace(bytes.begin(), bytes.end(), '/', '\\');
    // /C:/Users/... names C:\Users\... (also after this computer's name, as OSC 8 writes it)
    if (bytes.size() > 2 && bytes[2] == ':' && std::isalpha(static_cast<unsigned char>(bytes[1])) != 0)
        return bytes.substr(1);
    if (!host.empty())
        return "\\\\" + host + bytes;  // file://server/share/x names \\server\share\x
    return bytes;
}

}  // namespace links_os

void open_link(const LinkTarget& target) {
    std::wstring wide;
    try {
        wide = platform::win::to_wide(target.target);
    } catch (const std::exception&) {
        return;
    }
    if (wide.empty() || wide.find(L'\0') != std::wstring::npos)
        return;
    std::wstring args;
    std::wstring file = wide;
    if (target.file) {
        // A program or script is shown in Explorer, never run from a click on terminal text.
        // Windows drops trailing periods and spaces (evil.exe. runs evil.exe), and a colon past
        // the drive names a stream: neither may hide the real extension.
        std::wstring name = wide;
        while (!name.empty() && (name.back() == L'.' || name.back() == L' '))
            name.pop_back();
        if (name.find(L':', 2) != std::wstring::npos)
            return;
        std::wstring ext = fs::path(name).extension().wstring();
        for (auto& ch : ext)
            ch = static_cast<wchar_t>(std::towlower(ch));
        static constexpr std::wstring_view kRun[] = {L".exe", L".com", L".bat", L".cmd", L".ps1", L".psm1",
                                                     L".vbs", L".vbe", L".js",  L".jse", L".wsf", L".wsh",
                                                     L".msi", L".msp", L".scr", L".lnk", L".pif", L".hta",
                                                     L".cpl", L".reg", L".url"};
        if (std::find(std::begin(kRun), std::end(kRun), ext) != std::end(kRun)) {
            args = L"/select,\"" + wide + L"\"";
            file = L"explorer.exe";
        }
    }
    // ShellExecuteW can pump messages: never re-enter the UI while it handles a click.
    std::thread([file = std::move(file), args = std::move(args)] {
        const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        ShellExecuteW(nullptr, nullptr, file.c_str(), args.empty() ? nullptr : args.c_str(), nullptr,
                      SW_SHOWNORMAL);
        if (SUCCEEDED(init))
            CoUninitialize();
    }).detach();
}

}  // namespace agentdesk::ui
