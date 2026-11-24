// "auto" Option-as-Alt: on for the US and ABC keyboard layouts, where Option characters are rarely
// typed, off elsewhere (German, French, an IME...). The active input source is asked each time,
// so switching it takes effect at once.
#import <Carbon/Carbon.h>

#include <string>

namespace agentdesk::ui::keyboard {

bool layout_types_with_option_as_alt() {
    TISInputSourceRef source = TISCopyCurrentKeyboardInputSource();
    if (source == nullptr)
        return false;
    bool on = false;
    if (auto id = static_cast<CFStringRef>(TISGetInputSourceProperty(source, kTISPropertyInputSourceID))) {
        char buf[256]{};
        if (CFStringGetCString(id, buf, sizeof buf, kCFStringEncodingUTF8)) {
            const std::string s = buf;
            on = s == "com.apple.keylayout.US" || s == "com.apple.keylayout.USInternational" ||
                 s == "com.apple.keylayout.ABC" || s == "com.apple.keylayout.ABC-India";
        }
    }
    CFRelease(source);
    return on;
}

}  // namespace agentdesk::ui::keyboard
