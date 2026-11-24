// macOS (ADR-30): the system services of desktop_os.hpp, on AppKit.
#import <AppKit/AppKit.h>

#include "agentdesk/platform/system.hpp"
#include "desktop_os.hpp"

namespace agentdesk::desktop::macos {

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

void play_sound_file(const std::filesystem::path& p) {
    dispatch_async(dispatch_get_main_queue(), ^{
      @autoreleasepool {
          NSString* path = [NSString stringWithUTF8String:p.c_str()];
          NSSound* sound = path != nil ? [[NSSound alloc] initWithContentsOfFile:path byReference:YES] : nil;
          if (sound == nil || ![sound play])
              NSBeep();
          // NSSound keeps itself alive while playing.
      }
    });
}

void play_system_alert() {
    dispatch_async(dispatch_get_main_queue(), ^{
      NSBeep();
    });
}

bool system_dark() {
    @autoreleasepool {
        NSString* style = [[NSUserDefaults standardUserDefaults] stringForKey:@"AppleInterfaceStyle"];
        return style != nil && [style caseInsensitiveCompare:@"Dark"] == NSOrderedSame;
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

std::filesystem::path resources_dir() {
    const auto dir = platform::executable_path().parent_path();
    std::error_code ec;
    if (const auto res = dir.parent_path() / "Resources";
        dir.filename() == "MacOS" && std::filesystem::is_directory(res, ec))
        return res;
    return dir;
}

}  // namespace agentdesk::desktop::macos
