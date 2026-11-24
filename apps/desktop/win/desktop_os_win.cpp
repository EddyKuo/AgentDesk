// desktop_os.hpp on Windows.
#include <windows.h>

#include <mmsystem.h>
#include <shellapi.h>
// GetDpiForMonitor; needs windows.h first.
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cwchar>
#include <thread>

#include <shellscalingapi.h>

#include "agentdesk/platform/process.hpp"
#include "agentdesk/platform/win/error.hpp"
#include "desktop_os.hpp"

namespace agentdesk::desktop::os {

namespace win = platform::win;

void open_path(const std::filesystem::path& p) {
    ::ShellExecuteW(nullptr, L"open", p.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void open_text_file(const std::filesystem::path& p) {
    ::ShellExecuteW(nullptr, L"open", L"notepad.exe", p.c_str(), nullptr, SW_SHOWNORMAL);
}

// Played through MCI on a thread with a unique alias so sounds can overlap.
void play_alert(const std::filesystem::path& file, bool blocked) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(file, ec)) {
        ::PlaySoundW(blocked ? L"SystemExclamation" : L"SystemAsterisk", nullptr,
                     SND_ALIAS | SND_ASYNC | SND_NODEFAULT);
        return;
    }
    static std::atomic<int> counter{0};
    std::thread([file, id = ++counter] {
        const std::wstring alias = L"agentdesk" + std::to_wstring(id);
        const std::wstring open = L"open \"" + file.native() + L"\" type mpegvideo alias " + alias;
        if (::mciSendStringW(open.c_str(), nullptr, 0, nullptr) == 0) {
            ::mciSendStringW((L"play " + alias + L" wait").c_str(), nullptr, 0, nullptr);
            ::mciSendStringW((L"close " + alias).c_str(), nullptr, 0, nullptr);
        }
    }).detach();
}

bool system_light() {
    DWORD v = 0;
    DWORD size = sizeof v;
    return ::RegGetValueW(HKEY_CURRENT_USER,
                          L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                          L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &v, &size) == ERROR_SUCCESS &&
           v != 0;
}

// Settings > Accessibility > Visual effects > Animation effects, and the contrast themes.
bool reduced_motion() {
    BOOL animate = TRUE;
    return ::SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animate, 0) != FALSE && animate == FALSE;
}

bool high_contrast() {
    HIGHCONTRASTW hc{};
    hc.cbSize = sizeof(hc);
    return ::SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(hc), &hc, 0) != FALSE &&
           (hc.dwFlags & HCF_HIGHCONTRASTON) != 0;
}

std::string os_version() {
    OSVERSIONINFOEXW os{};
    os.dwOSVersionInfoSize = sizeof os;
    // RtlGetVersion reports the real Windows version; GetVersionEx is capped by the manifest.
    using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOEXW*);
    if (auto fn_ptr = reinterpret_cast<RtlGetVersionFn>(
            ::GetProcAddress(::GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion")))
        fn_ptr(&os);
    return "Windows " + std::to_string(os.dwMajorVersion) + "." + std::to_string(os.dwMinorVersion) + "." +
           std::to_string(os.dwBuildNumber);
}

std::string local_date_time(std::uint64_t secs) {
    ULARGE_INTEGER u{};
    u.QuadPart = (secs + 11644473600ull) * 10000000ull;  // 100 ns ticks since 1601
    FILETIME ft{u.LowPart, u.HighPart};
    FILETIME local{};
    SYSTEMTIME st{};
    ::FileTimeToLocalFileTime(&ft, &local);
    ::FileTimeToSystemTime(&local, &st);
    wchar_t d[64]{};
    wchar_t t[64]{};
    ::GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_SHORTDATE, &st, nullptr, d, 64, nullptr);
    ::GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &st, nullptr, t, 64);
    return win::to_utf8(std::wstring(d) + L", " + t);
}

// Windows counts its biases west of UTC.
LocalTime local_time() {
    SYSTEMTIME st{};
    ::GetLocalTime(&st);
    TIME_ZONE_INFORMATION tz{};
    const DWORD r = ::GetTimeZoneInformation(&tz);
    LocalTime n;
    n.year = st.wYear;
    n.month = st.wMonth;
    n.day = st.wDay;
    n.hour = st.wHour;
    n.minute = st.wMinute;
    n.utc_offset = -static_cast<int>(tz.Bias + (r == TIME_ZONE_ID_DAYLIGHT ? tz.DaylightBias : tz.StandardBias));
    n.std_offset = -static_cast<int>(tz.Bias + tz.StandardBias);
    return n;
}

std::string local_file_stamp() {
    SYSTEMTIME t{};
    ::GetLocalTime(&t);
    wchar_t name[32]{};
    std::swprintf(name, 32, L"%04u%02u%02u-%02u%02u%02u", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute,
                  t.wSecond);
    return win::to_utf8(name);
}

void start_detached(const std::vector<std::wstring>& argv) {
    if (argv.empty())
        return;
    const auto exe = platform::find_executable(win::to_utf8(argv[0]));
    if (!exe)
        return;
    platform::ProcessSpec spec;
    spec.executable = *exe;
    for (std::size_t i = 1; i < argv.size(); ++i)
        spec.args.push_back(win::to_utf8(argv[i]));
    spec.pipe_stdout = false;
    spec.end_with_parent = false;
    try {
        (void)platform::Process::spawn(spec);
    } catch (const std::exception&) {  // not started: nothing to tell the mod
    }
}

std::vector<std::filesystem::path> matching_files(const std::filesystem::path& p) {
    std::vector<std::filesystem::path> found;
    WIN32_FIND_DATAW fd{};
    const HANDLE h = ::FindFirstFileW(p.c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
                found.push_back(p.parent_path() / fd.cFileName);
        } while (::FindNextFileW(h, &fd));
        ::FindClose(h);
    }
    return found;
}

std::filesystem::path program_files_dir() {
    wchar_t exe[MAX_PATH]{};
    ::GetModuleFileNameW(nullptr, exe, MAX_PATH);
    return std::filesystem::path(exe).parent_path();
}

// The first run's zoom: Windows' text size (Accessibility TextScaleFactor, 100..225 %), x1.15 when
// the primary screen is 2400 DIPs or wider, rounded to tenths. DIPs, not pixels: Windows' display
// scale already draws everything bigger, so a 4K screen at 200 % (1920 DIPs) gets no extra.
float first_run_zoom() {
    DWORD factor = 100, size = sizeof(factor);
    ::RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Accessibility", L"TextScaleFactor",
                   RRF_RT_REG_DWORD, nullptr, &factor, &size);
    float z = std::clamp(static_cast<float>(factor) / 100.0f, 1.0f, 2.0f);
    UINT dpi = 96, dpi_y = 96;
    const HMONITOR primary = ::MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    if (FAILED(::GetDpiForMonitor(primary, MDT_EFFECTIVE_DPI, &dpi, &dpi_y)) || dpi == 0)
        dpi = 96;
    if (::GetSystemMetrics(SM_CXSCREEN) * 96.0 / dpi >= 2400.0)
        z *= 1.15f;
    return std::clamp(std::round(z * 10.0f) / 10.0f, 0.5f, 3.0f);
}

bool has_keyboard_settings() {
    return false;
}

}  // namespace agentdesk::desktop::os
