#pragma once

// macOS (ADR-30): the system services the window contents use where Windows code calls Win32
// (opening files and folders, sounds, the system appearance, dates), plus the MSVC C runtime
// names the shared code uses, on the macOS C library. Included by app_internal.hpp on macOS only.

#include <cstdint>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <string>

#include <strings.h>

inline int _stricmp(const char* a, const char* b) { return ::strcasecmp(a, b); }
inline int _strnicmp(const char* a, const char* b, std::size_t n) { return ::strncasecmp(a, b, n); }
// MSVC's argument order: (result, time).
inline int localtime_s(std::tm* out, const std::time_t* t) { return ::localtime_r(t, out) != nullptr ? 0 : 1; }
inline std::time_t _mkgmtime(std::tm* tm) { return ::timegm(tm); }
// Used with numeric conversions only, where the _s variant takes the same arguments.
template <class... Args>
int sscanf_s(const char* s, const char* format, Args... args) {
    return std::sscanf(s, format, args...);  // NOLINT(cert-err34-c)
}

namespace agentdesk::desktop::macos {

// The file or folder in its default app (a folder in the Finder).
void open_path(const std::filesystem::path& p);
// A text file in TextEdit (keys.json), as Notepad on Windows.
void open_text_file(const std::filesystem::path& p);
// An audio file (MP3), asynchronously; several may overlap.
void play_sound_file(const std::filesystem::path& p);
// The system alert sound, when no file can be played.
void play_system_alert();
// True when the system appearance is Dark.
bool system_dark();
// "macOS 14.5 (23F79)".
std::string os_version();
// Unix seconds as the date and time in the person's own format.
std::string local_date_time(std::uint64_t secs);
// Where the program's files are: Contents/Resources in an app bundle, else the program's folder.
std::filesystem::path resources_dir();

}  // namespace agentdesk::desktop::macos
