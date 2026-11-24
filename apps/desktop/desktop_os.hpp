#pragma once

// What the window contents ask of the system they run on: files and folders, alert sounds, the
// appearance, dates, the data folders and starting programs. win/desktop_os_win.cpp is the
// Windows implementation, macos/desktop_os_macos.mm the macOS one (ADR-30); CMake builds one.

#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace agentdesk::desktop::os {

// The file or folder in its default app (a folder in Explorer or the Finder).
void open_path(const std::filesystem::path& p);
// A text file in the system's plain editor (Notepad, TextEdit): keys.json.
void open_text_file(const std::filesystem::path& p);
// `file` (an MP3), asynchronously so several may overlap; the system's own alert sound when it
// does not exist. `blocked` picks which system sound.
void play_alert(const std::filesystem::path& file, bool blocked);
// True when the system's apps use the light appearance.
bool system_light();
// Whether the person asked the system for less motion, or for more contrast.
bool reduced_motion();
bool high_contrast();
// "Windows 10.0.26100", "macOS Version 15.5 (Build 24F74)": for feedback.
std::string os_version();
// Unix seconds as the date and time in the person's own format.
std::string local_date_time(std::uint64_t secs);
// The local date and time now. Offsets are minutes east of UTC; std_offset leaves daylight time out.
struct LocalTime {
    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int utc_offset = 0;
    int std_offset = 0;
};
LocalTime local_time();
// The local date and time as YYYYmmdd-HHMMSS, for file names.
std::string local_file_stamp();
// A program a mod asked to start: argv[0] looked up on the PATH, never through a shell, without a
// window, not waited for.
void start_detached(const std::vector<std::wstring>& argv);
// The files a wildcard in `pattern`'s last component matches (ssh's Include).
std::vector<std::filesystem::path> matching_files(const std::filesystem::path& pattern);
// Where the program's files are (fonts, sounds, programs, themes, mods): the executable's folder,
// or the app bundle's Resources.
std::filesystem::path program_files_dir();
// The zoom of the first run, from the system's text size and the screen's width.
float first_run_zoom();
// Whether Settings has the Keyboard page (Option as Alt; macOS only, FR-40).
bool has_keyboard_settings();

}  // namespace agentdesk::desktop::os
