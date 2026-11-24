// desktop_os.hpp on macOS (ADR-30), on AppKit.
#import <AppKit/AppKit.h>

#include <fnmatch.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <ctime>

#include "agentdesk/platform/process.hpp"
#include "agentdesk/platform/system.hpp"
#include "agentdesk/platform/win/error.hpp"
#include "agentdesk/ui/window_place.hpp"
#include "desktop_os.hpp"

namespace agentdesk::desktop::os {

namespace win = platform::win;

namespace {

NSURL* file_url(const std::filesystem::path& p) {
    NSString* s = [NSString stringWithUTF8String:p.c_str()];
    return s != nil ? [NSURL fileURLWithPath:s] : nil;
}

}  // namespace

void open_path(const std::filesystem::path& p) {
    @autoreleasepool {
        if (NSURL* url = file_url(p))
            [[NSWorkspace sharedWorkspace] openURL:url];
    }
}

void open_text_file(const std::filesystem::path& p) {
    @autoreleasepool {
        NSURL* url = file_url(p);
        NSURL* textedit = [[NSWorkspace sharedWorkspace] URLForApplicationWithBundleIdentifier:@"com.apple.TextEdit"];
        if (url == nil)
            return;
        if (textedit == nil) {
            [[NSWorkspace sharedWorkspace] openURL:url];
            return;
        }
        [[NSWorkspace sharedWorkspace] openURLs:@[ url ]
                           withApplicationAtURL:textedit
                                  configuration:[NSWorkspaceOpenConfiguration configuration]
                              completionHandler:nil];
    }
}

// NSSound plays on the main thread; the path is copied into the block, which runs later.
void play_alert(const std::filesystem::path& file, bool /*blocked*/) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(file, ec)) {
        dispatch_async(dispatch_get_main_queue(), ^{
          NSBeep();
        });
        return;
    }
    NSString* path = [NSString stringWithUTF8String:file.c_str()];
    dispatch_async(dispatch_get_main_queue(), ^{
      @autoreleasepool {
          NSSound* sound = path != nil ? [[NSSound alloc] initWithContentsOfFile:path byReference:YES] : nil;
          if (sound == nil || ![sound play])
              NSBeep();
          // NSSound keeps itself alive while playing.
      }
    });
}

bool system_light() {
    @autoreleasepool {
        NSString* style = [[NSUserDefaults standardUserDefaults] stringForKey:@"AppleInterfaceStyle"];
        return style == nil || [style caseInsensitiveCompare:@"Dark"] != NSOrderedSame;
    }
}

// System Settings > Accessibility > Display: Reduce motion, Increase contrast.
bool reduced_motion() {
    @autoreleasepool {
        return [NSWorkspace sharedWorkspace].accessibilityDisplayShouldReduceMotion;
    }
}

bool high_contrast() {
    @autoreleasepool {
        return [NSWorkspace sharedWorkspace].accessibilityDisplayShouldIncreaseContrast;
    }
}

std::string os_version() {
    @autoreleasepool {
        return std::string("macOS ") + [NSProcessInfo processInfo].operatingSystemVersionString.UTF8String;
    }
}

std::string local_date_time(std::uint64_t secs) {
    @autoreleasepool {
        NSDate* date = [NSDate dateWithTimeIntervalSince1970:static_cast<double>(secs)];
        NSString* text = [NSDateFormatter localizedStringFromDate:date
                                                        dateStyle:NSDateFormatterShortStyle
                                                        timeStyle:NSDateFormatterShortStyle];
        return text != nil ? std::string(text.UTF8String) : std::string();
    }
}

// tm_gmtoff is seconds east of UTC, daylight time included.
LocalTime local_time() {
    const std::time_t now = std::time(nullptr);
    std::tm t{};
    ::localtime_r(&now, &t);
    LocalTime n;
    n.year = t.tm_year + 1900;
    n.month = t.tm_mon + 1;
    n.day = t.tm_mday;
    n.hour = t.tm_hour;
    n.minute = t.tm_min;
    n.utc_offset = static_cast<int>(t.tm_gmtoff / 60);
    @autoreleasepool {
        const NSTimeInterval dst = [NSTimeZone localTimeZone].daylightSavingTimeOffset;
        n.std_offset = n.utc_offset - static_cast<int>(dst / 60.0);
    }
    return n;
}

std::string local_file_stamp() {
    const std::time_t now = std::time(nullptr);
    std::tm t{};
    ::localtime_r(&now, &t);
    char name[32]{};
    std::strftime(name, sizeof name, "%Y%m%d-%H%M%S", &t);
    return name;
}

bool replace_file(const std::filesystem::path& tmp, const std::filesystem::path& target, std::error_code* why) {
    std::error_code ec;
    std::filesystem::rename(tmp, target, ec);  // rename(2) replaces the target in one step
    if (why != nullptr)
        *why = ec;
    return !ec;
}

std::string env(const char* name) {
    const char* v = std::getenv(name);
    return v != nullptr ? std::string(v) : std::string();
}

std::filesystem::path home_dir() {
    return platform::home_dir();
}

// Looked up on the login shell's PATH, started in its own session.
void start_detached(const std::vector<std::wstring>& argv) {
    if (argv.empty())
        return;
    try {
        const auto exe = platform::find_on_login_path(win::to_utf8(argv[0]));
        if (!exe)
            return;
        std::vector<std::string> args;
        for (std::size_t i = 1; i < argv.size(); ++i)
            args.push_back(win::to_utf8(argv[i]));
        platform::spawn_detached(*exe, args);
    } catch (const std::exception&) {
    }
}

std::vector<std::filesystem::path> matching_files(const std::filesystem::path& p) {
    std::vector<std::filesystem::path> found;
    std::error_code ec;
    const std::string pattern = p.filename().string();
    for (const auto& e : std::filesystem::directory_iterator(p.parent_path(), ec))
        if (!e.is_directory(ec) && ::fnmatch(pattern.c_str(), e.path().filename().c_str(), 0) == 0)
            found.push_back(e.path());
    return found;
}

// AGENTDESK_DATA_DIR, else ~/Library/Application Support/AgentDesk.
std::filesystem::path data_dir() {
    try {
        return platform::data_dir();
    } catch (const std::exception&) {
        return std::filesystem::temp_directory_path() / "AgentDesk";
    }
}

// Contents/Resources in an app bundle, else the program's folder (a build folder).
std::filesystem::path program_files_dir() {
    const auto dir = platform::executable_path().parent_path();
    std::error_code ec;
    if (const auto res = dir.parent_path() / "Resources";
        dir.filename() == "MacOS" && std::filesystem::is_directory(res, ec))
        return res;
    return dir;
}

// 1.0, x1.15 on a screen 2400 points wide or wider (in logical units, as on Windows: Retina
// already draws everything sharper, not bigger).
float first_run_zoom() {
    float z = 1.0f;
    if (const auto monitors = ui::window_place::monitors(); !monitors.empty() && monitors.front().bounds.w >= 2400)
        z *= 1.15f;
    return std::clamp(std::round(z * 10.0f) / 10.0f, 0.5f, 3.0f);
}

bool has_keyboard_settings() {
    return true;
}

}  // namespace agentdesk::desktop::os
