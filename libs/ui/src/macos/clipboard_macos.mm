// macOS (ADR-30): what the general pasteboard holds, for the terminal's paste: copied files
// (Finder), text, or an image (a screenshot).
#import <AppKit/AppKit.h>

#include "agentdesk/ui/clipboard.hpp"

namespace agentdesk::ui {

ClipboardContent read_clipboard() {
    ClipboardContent out;
    @autoreleasepool {
        NSPasteboard* pb = [NSPasteboard generalPasteboard];
        NSArray<NSURL*>* urls = [pb readObjectsForClasses:@[ [NSURL class] ]
                                                  options:@{NSPasteboardURLReadingFileURLsOnlyKey : @YES}];
        for (NSURL* url in urls) {
            if (url.fileURL && url.path != nil)
                out.files.emplace_back(url.path.UTF8String);
        }
        if (NSString* text = [pb stringForType:NSPasteboardTypeString]; text != nil)
            out.text = std::string(text.UTF8String != nullptr ? text.UTF8String : "");
        out.image = [pb availableTypeFromArray:@[ NSPasteboardTypePNG, NSPasteboardTypeTIFF ]] != nil;
    }
    return out;
}

}  // namespace agentdesk::ui
