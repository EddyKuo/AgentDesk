// agent-ui on macOS (ADR-30): AppKit + Metal + Dear ImGui front end of agent-host. The same role
// as main.cpp on Windows: the window, the event loop and the drawing device; the window contents
// are App (app.cpp and the files that share app_internal.hpp). The UI holds no process or pty:
// closing the window only detaches.
//
// The window has no visible title bar: the content fills it (full-size content view) and the
// system's window buttons (traffic lights) stay at its top-left, over the chrome's top bar.
#import <AppKit/AppKit.h>
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#import <ImageIO/ImageIO.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <execinfo.h>
#include <filesystem>
#include <fstream>
#include <signal.h>
#include <string>
#include <unistd.h>
#include <vector>

#include <imgui.h>
#include <imgui_impl_metal.h>
#include <imgui_impl_osx.h>

#include "agentdesk/ipc/host_launcher.hpp"
#include "agentdesk/ipc/pipe.hpp"
#include "agentdesk/platform/system.hpp"
#include "agentdesk/platform/win/error.hpp"
#include "agentdesk/ui/host_client.hpp"
#include "agentdesk/ui/imgui_adapter.hpp"
#include "agentdesk/ui/keyboard.hpp"
#include "agentdesk/ui/telemetry.hpp"
#include "agentdesk/ui/updates.hpp"
#include "agentdesk/ui/window_place.hpp"
#include "app.hpp"

namespace win = agentdesk::platform::win;
namespace ui = agentdesk::ui;
namespace ipc = agentdesk::ipc;
using Clock = std::chrono::steady_clock;

// Room the traffic lights take at the top-left of the window (points), outside full screen.
static constexpr float kTrafficLightsWidth = 76.0f;

namespace {

// Developer options (automated checks), as on Windows.
struct DevOptions {
    std::string start_profile;
    long long exit_after_ms = 0;
    std::filesystem::path report;
    std::wstring pipe;
    std::vector<std::string> run;
    std::string input;
    std::filesystem::path screenshot;  // --screenshot <png>: the last frame, written at exit
};

DevOptions parse_dev_options(int argc, char** argv) {
    DevOptions o;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--start" && i + 1 < argc)
            o.start_profile = argv[++i];
        else if (a == "--exit-after-ms" && i + 1 < argc)
            o.exit_after_ms = std::stoll(argv[++i]);
        else if (a == "--report" && i + 1 < argc)
            o.report = argv[++i];
        else if (a == "--pipe" && i + 1 < argc)
            o.pipe = win::to_wide(argv[++i]);
        else if (a == "--input" && i + 1 < argc)
            o.input = argv[++i];
        else if (a == "--screenshot" && i + 1 < argc)
            o.screenshot = argv[++i];

        else if (a == "--run" && i + 1 < argc) {
            const std::string all = argv[++i];
            for (std::size_t p = 0; p <= all.size();) {
                const auto q = all.find(';', p);
                const std::string c = all.substr(p, q == std::string::npos ? std::string::npos : q - p);
                if (!c.empty())
                    o.run.push_back(c);
                if (q == std::string::npos)
                    break;
                p = q + 1;
            }
        }
    }
    return o;
}

// A crash leaves ui-crash.txt (data folder) with the signal and the call stack. Sessions keep
// running in the host either way.
int g_crash_fd = -1;

void on_crash(int sig) {
    if (g_crash_fd >= 0) {
        char head[64];
        const int n = std::snprintf(head, sizeof head, "signal %d\n", sig);
        (void)::write(g_crash_fd, head, static_cast<std::size_t>(n));
        void* frames[64];
        const int count = ::backtrace(frames, 64);
        ::backtrace_symbols_fd(frames, count, g_crash_fd);
    }
    ::signal(sig, SIG_DFL);
    ::raise(sig);
}

void install_crash_report() {
    const auto file = agentdesk::desktop::data_folder() / "ui-crash.txt";
    // Opened ahead: nothing may allocate inside the handler. Truncated only when a crash writes.
    g_crash_fd = ::open(file.c_str(), O_WRONLY | O_CREAT, 0600);
    for (const int sig : {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT})
        ::signal(sig, on_crash);
}

}  // namespace

// ---------------------------------------------------------------- the shell's state

@class AgentDeskView;

struct Shell {
    NSWindow* window = nil;
    AgentDeskView* view = nil;
    id<MTLDevice> device = nil;
    id<MTLCommandQueue> queue = nil;
    agentdesk::desktop::App* app = nullptr;
    DevOptions dev;
    Clock::time_point launched{};
    Clock::time_point last_tick{};
    int frames_to_draw = 3;
    bool pump_scheduled = false;
    NSTimer* timer = nil;
    bool quitting = false;
    std::vector<std::string> dropped;
    // Dragging the window from the chrome's top bar (window_begin_drag).
    bool dragging = false;
    NSPoint drag_mouse{};
    NSPoint drag_origin{};
    ui::window_place::Rect normal_rect;
};

static Shell g_shell;

static void schedule_pump(int frames);

// ---------------------------------------------------------------- the view

@interface AgentDeskView : MTKView <NSDraggingDestination>
@end

@implementation AgentDeskView

- (BOOL)acceptsFirstResponder {
    return YES;
}

- (BOOL)acceptsFirstMouse:(NSEvent*)event {
    return YES;  // a click on an inactive window also reaches the chrome under it
}

- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender {
    return NSDragOperationCopy;
}

// Files dropped from the Finder: the terminal under the pointer types their paths in.
- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender {
    NSArray<NSURL*>* urls =
        [sender.draggingPasteboard readObjectsForClasses:@[ [NSURL class] ]
                                                 options:@{NSPasteboardURLReadingFileURLsOnlyKey : @YES}];
    for (NSURL* url in urls)
        if (url.path != nil)
            g_shell.dropped.emplace_back(url.path.UTF8String);
    schedule_pump(3);
    return YES;
}

@end

// ---------------------------------------------------------------- window and app delegates

@interface AgentDeskWindowDelegate : NSObject <NSWindowDelegate>
@end

static void render_frame();

// Draws when the loop asks the view to ([view draw]).
@interface AgentDeskRenderer : NSObject <MTKViewDelegate>
@end

@implementation AgentDeskRenderer

- (void)drawInMTKView:(MTKView*)view {
    if (g_shell.app != nullptr && !g_shell.quitting)
        render_frame();
}

- (void)mtkView:(MTKView*)view drawableSizeWillChange:(CGSize)size {
}

@end

static void note_normal_rect() {
    NSWindow* w = g_shell.window;
    if (w == nil || w.zoomed || w.miniaturized || (w.styleMask & NSWindowStyleMaskFullScreen) != 0)
        return;
    const NSRect f = w.frame;
    const CGFloat main_height = NSScreen.screens.count > 0 ? NSScreen.screens[0].frame.size.height : f.size.height;
    g_shell.normal_rect = {static_cast<int>(f.origin.x),
                           static_cast<int>(main_height - f.origin.y - f.size.height),
                           static_cast<int>(f.size.width), static_cast<int>(f.size.height)};
}

@implementation AgentDeskWindowDelegate

// The red button is the window X: detach; the app decides (first-run notice).
- (BOOL)windowShouldClose:(NSWindow*)sender {
    if (g_shell.app != nullptr)
        g_shell.app->on_close_requested();
    schedule_pump(3);
    return NO;
}

- (void)windowDidResize:(NSNotification*)notification {
    note_normal_rect();
    schedule_pump(3);
}

- (void)windowDidMove:(NSNotification*)notification {
    note_normal_rect();
}

- (void)windowDidChangeBackingProperties:(NSNotification*)notification {
    schedule_pump(3);
}

- (void)windowDidEnterFullScreen:(NSNotification*)notification {
    schedule_pump(3);
}

- (void)windowDidExitFullScreen:(NSNotification*)notification {
    schedule_pump(3);
}

@end

@interface AgentDeskAppDelegate : NSObject <NSApplicationDelegate>
@end

@implementation AgentDeskAppDelegate

- (void)applicationDidBecomeActive:(NSNotification*)notification {
    if (g_shell.app != nullptr)
        g_shell.app->on_focus_changed(true);
    schedule_pump(3);
}

- (void)applicationDidResignActive:(NSNotification*)notification {
    if (g_shell.app != nullptr)
        g_shell.app->on_focus_changed(false);
    schedule_pump(3);
}

// Cmd+Q and Quit from the menu: like closing the window (detach), never stopping the sessions.
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication*)sender {
    if (g_shell.quitting)
        return NSTerminateNow;
    if (g_shell.app != nullptr)
        g_shell.app->on_close_requested();
    schedule_pump(3);
    return NSTerminateCancel;
}

// Clicking the Dock icon with the window minimized brings it back.
- (BOOL)applicationShouldHandleReopen:(NSApplication*)sender hasVisibleWindows:(BOOL)flag {
    [g_shell.window makeKeyAndOrderFront:nil];
    return YES;
}

@end

// ---------------------------------------------------------------- window_ops for App

namespace agentdesk::desktop {

bool window_is_maximized(NativeWindow w) {
    NSWindow* win = (__bridge NSWindow*)w;
    return win != nil && (win.zoomed || (win.styleMask & NSWindowStyleMaskFullScreen) != 0);
}

void window_minimize(NativeWindow w) { [(__bridge NSWindow*)w miniaturize:nil]; }

void window_toggle_maximize(NativeWindow w) { [(__bridge NSWindow*)w zoom:nil]; }

void window_close(NativeWindow /*w*/) {
    if (g_shell.app != nullptr)
        g_shell.app->on_close_requested();
    schedule_pump(3);
}

// The press on the chrome's drag area starts a manual move: each drag event moves the window by
// the pointer's travel in screen coordinates, until the button goes up.
void window_begin_drag(NativeWindow w) {
    NSWindow* win = (__bridge NSWindow*)w;
    if (win == nil || (win.styleMask & NSWindowStyleMaskFullScreen) != 0)
        return;
    g_shell.dragging = true;
    g_shell.drag_mouse = [NSEvent mouseLocation];
    g_shell.drag_origin = win.frame.origin;
}

void window_set_title(NativeWindow w, const std::string& utf8) {
    NSString* title = [NSString stringWithUTF8String:utf8.c_str()];
    ((__bridge NSWindow*)w).title = title != nil ? title : @"AgentDesk";
}

bool window_draws_own_buttons() { return false; }

void app_hide(bool others) {
    if (others)
        [NSApp hideOtherApplications:nil];
    else
        [NSApp hide:nil];
}

float window_system_buttons_width() {
    NSWindow* win = g_shell.window;
    return win != nil && (win.styleMask & NSWindowStyleMaskFullScreen) != 0 ? 0.0f : kTrafficLightsWidth;
}

}  // namespace agentdesk::desktop

// ---------------------------------------------------------------- frames

// Writes the drawable's pixels (BGRA) as a PNG: the automated checks' screenshot, which needs no
// screen-recording permission because it reads our own frame back from the GPU.
static void write_png(id<MTLTexture> tex, const std::filesystem::path& file) {
    const NSUInteger w = tex.width;
    const NSUInteger h = tex.height;
    std::vector<std::uint8_t> pixels(w * h * 4);
    [tex getBytes:pixels.data() bytesPerRow:w * 4 fromRegion:MTLRegionMake2D(0, 0, w, h) mipmapLevel:0];
    CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGContextRef ctx = CGBitmapContextCreate(pixels.data(), w, h, 8, w * 4, space,
                                             static_cast<uint32_t>(kCGImageAlphaPremultipliedFirst) | static_cast<uint32_t>(kCGBitmapByteOrder32Little));
    CGImageRef image = CGBitmapContextCreateImage(ctx);
    NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:file.c_str()]];
    if (CGImageDestinationRef dest =
            CGImageDestinationCreateWithURL((__bridge CFURLRef)url, (__bridge CFStringRef)UTTypePNG.identifier, 1, nullptr)) {
        CGImageDestinationAddImage(dest, image, nullptr);
        CGImageDestinationFinalize(dest);
        CFRelease(dest);
    }
    CGImageRelease(image);
    CGContextRelease(ctx);
    CGColorSpaceRelease(space);
}

// One frame, from the view's drawInMTKView: (MTKView hands out a fresh drawable for each `draw`
// and releases it afterwards; drawing outside it would present the same drawable again and again,
// and the screen would stop changing). `g_capture`: also write the frame for --screenshot.
static bool g_capture = false;

static void render_frame() {
    const bool capture = g_capture;
    AgentDeskView* view = g_shell.view;
    MTLRenderPassDescriptor* pass = view.currentRenderPassDescriptor;
    if (pass == nil)
        return;
    const ImVec4 bg = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
    pass.colorAttachments[0].clearColor = MTLClearColorMake(bg.x, bg.y, bg.z, 1.0);
    id<MTLCommandBuffer> commands = [g_shell.queue commandBuffer];
    ImGui_ImplMetal_NewFrame(pass);
    ImGui_ImplOSX_NewFrame(view);
    g_shell.app->before_new_frame();
    ImGui::NewFrame();
    g_shell.app->frame();
    ImGui::Render();
    id<MTLRenderCommandEncoder> encoder = [commands renderCommandEncoderWithDescriptor:pass];
    ImGui_ImplMetal_RenderDrawData(ImGui::GetDrawData(), commands, encoder);
    [encoder endEncoding];
    id<CAMetalDrawable> drawable = view.currentDrawable;
    if (drawable != nil)
        [commands presentDrawable:drawable];
    [commands commit];
    if (capture && drawable != nil) {
        [commands waitUntilCompleted];
        write_png(drawable.texture, g_shell.dev.screenshot);
    }
}

static void finish_and_exit();

// One turn of the loop: the app's work, then a frame when one is due; the next turn is scheduled
// at the next frame (while frames are due) or the app's next deadline.
static void pump() {
    g_shell.pump_scheduled = false;
    if (g_shell.quitting || g_shell.app == nullptr)
        return;
    auto& app = *g_shell.app;
    if (g_shell.dev.exit_after_ms > 0 &&
        Clock::now() - g_shell.launched > std::chrono::milliseconds(g_shell.dev.exit_after_ms)) {
        if (!g_shell.dev.screenshot.empty()) {
            @autoreleasepool {
                g_capture = true;
                [g_shell.view draw];
                g_capture = false;
            }
        }
        finish_and_exit();
        return;
    }
    if (!g_shell.dropped.empty())
        app.on_files_dropped(std::exchange(g_shell.dropped, {}));
    if (app.exit_requested()) {
        finish_and_exit();
        return;
    }
    const bool visible = g_shell.window.occlusionState & NSWindowOcclusionStateVisible;
    if (const auto now = Clock::now(); visible || now - g_shell.last_tick >= std::chrono::milliseconds(100)) {
        g_shell.last_tick = now;
        app.tick();  // sessions, attention, mods, persistence: drawn or not
    }
    if (g_shell.frames_to_draw > 0 && visible) {
        @autoreleasepool {
            [g_shell.view draw];
        }
        --g_shell.frames_to_draw;
    } else if (!visible) {
        g_shell.frames_to_draw = 0;
    }
    if (app.exit_requested()) {
        finish_and_exit();
        return;
    }
    long long wait = g_shell.frames_to_draw > 0 ? 0 : std::clamp<long long>(app.next_wakeup_ms(), 0, 1000);
    if (!visible)
        wait = std::max<long long>(wait, 100);
    if (g_shell.dev.exit_after_ms > 0)
        wait = std::min<long long>(wait, 100);
    [g_shell.timer invalidate];
    g_shell.timer = nil;
    if (wait <= 0) {
        schedule_pump(0);
    } else {
        g_shell.timer = [NSTimer scheduledTimerWithTimeInterval:static_cast<double>(wait) / 1000.0
                                                        repeats:NO
                                                          block:^(NSTimer*) {
                                                            g_shell.frames_to_draw =
                                                                std::max(g_shell.frames_to_draw, 2);
                                                            schedule_pump(0);
                                                          }];
    }
}

// Asks for another turn of the loop (any thread). `frames`: at least that many frames to draw.
static void schedule_pump(int frames) {
    if (![NSThread isMainThread]) {
        dispatch_async(dispatch_get_main_queue(), ^{
          schedule_pump(frames);
        });
        return;
    }
    g_shell.frames_to_draw = std::max(g_shell.frames_to_draw, frames);
    if (g_shell.pump_scheduled)
        return;
    g_shell.pump_scheduled = true;
    dispatch_async(dispatch_get_main_queue(), ^{
      pump();
    });
}

static void save_place() {
    namespace place = ui::window_place;
    if (g_shell.normal_rect.w <= 0 || g_shell.normal_rect.h <= 0)
        return;
    const auto monitors = place::monitors();
    place::save(agentdesk::desktop::data_folder() / "window.json",
                place::capture(g_shell.normal_rect, g_shell.window.zoomed,
                               place::monitor_for(g_shell.normal_rect, monitors)));
}

static void finish_and_exit() {
    if (g_shell.quitting)
        return;
    g_shell.quitting = true;
    [g_shell.timer invalidate];
    auto& app = *g_shell.app;
    if (g_shell.dev.exit_after_ms <= 0)
        save_place();
    app.on_quit();
    if (!g_shell.dev.report.empty())
        std::ofstream(g_shell.dev.report, std::ios::binary) << app.report().dump(2) << '\n';
    [NSApp stop:nil];
    // stop: takes effect after the next event; post one so run returns now.
    [NSApp postEvent:[NSEvent otherEventWithType:NSEventTypeApplicationDefined
                                        location:NSZeroPoint
                                   modifierFlags:0
                                       timestamp:0
                                    windowNumber:0
                                         context:nil
                                         subtype:0
                                           data1:0
                                           data2:0]
             atStart:YES];
}

// The Option keys (FR-40): which are down is noted for "left" / "right"; while Option acts as Alt,
// a key typed with it carries no text, so the terminal sends ESC + the key instead of the
// character Option would type (å for Option+A).
static NSEvent* option_as_alt(NSEvent* event) {
    if (event.type != NSEventTypeKeyDown && event.type != NSEventTypeFlagsChanged)
        return event;
    const NSUInteger raw = event.modifierFlags;
    // NX_DEVICELALTKEYMASK / NX_DEVICERALTKEYMASK: the left and right Option keys.
    ui::keyboard::note_option_keys((raw & 0x20) != 0, (raw & 0x40) != 0);
    if (event.type != NSEventTypeKeyDown || (raw & NSEventModifierFlagOption) == 0 ||
        (raw & NSEventModifierFlagCommand) != 0 || !ui::keyboard::option_is_alt())
        return event;
    return [NSEvent keyEventWithType:event.type
                            location:event.locationInWindow
                       modifierFlags:event.modifierFlags
                           timestamp:event.timestamp
                        windowNumber:event.windowNumber
                             context:nil
                          characters:@""
         charactersIgnoringModifiers:event.charactersIgnoringModifiers
                           isARepeat:event.isARepeat
                             keyCode:event.keyCode];
}

// ---------------------------------------------------------------- startup

static void build_menu() {
    NSMenu* bar = [[NSMenu alloc] init];
    NSMenuItem* app_item = [[NSMenuItem alloc] init];
    [bar addItem:app_item];
    NSMenu* app_menu = [[NSMenu alloc] initWithTitle:@"AgentDesk"];
    [app_menu addItemWithTitle:@"Hide AgentDesk" action:@selector(hide:) keyEquivalent:@"h"];
    [app_menu addItem:[NSMenuItem separatorItem]];
    [app_menu addItemWithTitle:@"Quit AgentDesk" action:@selector(terminate:) keyEquivalent:@"q"];
    app_item.submenu = app_menu;
    NSMenuItem* window_item = [[NSMenuItem alloc] init];
    [bar addItem:window_item];
    NSMenu* window_menu = [[NSMenu alloc] initWithTitle:@"Window"];
    [window_menu addItemWithTitle:@"Minimize" action:@selector(performMiniaturize:) keyEquivalent:@"m"];
    [window_menu addItemWithTitle:@"Zoom" action:@selector(performZoom:) keyEquivalent:@""];
    window_item.submenu = window_menu;
    NSApp.mainMenu = bar;
    NSApp.windowsMenu = window_menu;
}

static ui::window_place::Place start_place(bool automated) {
    namespace place = ui::window_place;
    if (automated)
        return {{100, 100, 1400, 900}, false};
    std::size_t under_cursor = 0;
    const auto monitors = place::monitors(&under_cursor);
    return place::restore(place::load(agentdesk::desktop::data_folder() / "window.json"), monitors, under_cursor,
                          agentdesk::desktop::saved_zoom());
}

int main(int argc, char** argv) {
    // `agent-ui --version`: the updater asks a downloaded copy this way, before any window.
    if (argc == 2 && std::string(argv[1]) == "--version") {
        std::printf("agent-ui %s\n", AGENTDESK_VERSION_STRING);
        return 0;
    }
    install_crash_report();
    ui::updates::configure({AGENTDESK_VERSION_STRING, agentdesk::platform::executable_path(),
                            agentdesk::desktop::data_folder(), [] { schedule_pump(2); }});
    ui::updates::leave_install_folder();
    if (ui::updates::apply_on_startup())
        return 0;
    ui::telemetry::clock();

    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        AgentDeskAppDelegate* app_delegate = [[AgentDeskAppDelegate alloc] init];
        NSApp.delegate = app_delegate;
        build_menu();

        g_shell.dev = parse_dev_options(argc, argv);
        const bool automated = g_shell.dev.exit_after_ms > 0;
        const auto place = start_place(automated);
        const CGFloat main_height = NSScreen.screens.count > 0 ? NSScreen.screens[0].frame.size.height : 900;
        const NSRect frame = NSMakeRect(place.rect.x, main_height - place.rect.y - place.rect.h, place.rect.w,
                                        place.rect.h);
        g_shell.normal_rect = place.rect;

        NSWindow* window =
            [[NSWindow alloc] initWithContentRect:frame
                                        styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                                  NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable |
                                                  NSWindowStyleMaskFullSizeContentView
                                          backing:NSBackingStoreBuffered
                                            defer:NO];
        window.titlebarAppearsTransparent = YES;
        window.titleVisibility = NSWindowTitleHidden;
        window.title = @"AgentDesk";
        window.minSize = NSMakeSize(ui::window_place::kMinWidth, ui::window_place::kMinHeight);
        window.collectionBehavior = NSWindowCollectionBehaviorFullScreenPrimary;
        window.releasedWhenClosed = NO;
        AgentDeskWindowDelegate* window_delegate = [[AgentDeskWindowDelegate alloc] init];
        window.delegate = window_delegate;
        g_shell.window = window;

        g_shell.device = MTLCreateSystemDefaultDevice();
        if (g_shell.device == nil) {
            NSAlert* alert = [[NSAlert alloc] init];
            alert.messageText = @"Metal is not available";
            [alert runModal];
            return 1;
        }
        g_shell.queue = [g_shell.device newCommandQueue];
        AgentDeskView* view = [[AgentDeskView alloc] initWithFrame:frame device:g_shell.device];
        view.paused = YES;  // drawn by pump() through [view draw], not by a display link
        AgentDeskRenderer* renderer = [[AgentDeskRenderer alloc] init];
        view.delegate = renderer;
        view.enableSetNeedsDisplay = NO;
        view.colorPixelFormat = MTLPixelFormatBGRA8Unorm;
        view.framebufferOnly = g_shell.dev.screenshot.empty();  // readable only for --screenshot
        [view registerForDraggedTypes:@[ NSPasteboardTypeFileURL ]];
        window.contentView = view;
        g_shell.view = view;

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;  // layout lives in agent-host (ui.state), settings in ui.json
        ui::adapter::disable_nav_windowing();
        ImGui_ImplOSX_Init(view);
        // Zhuyin / Pinyin / Kana compositions are drawn by the terminal at its caret.
        ImGui_ImplOSX_SetMarkedTextCallback([](const char* utf8) {
            ui::keyboard::set_preedit(utf8 != nullptr ? utf8 : "");
            schedule_pump(2);
        });
        ImGui_ImplMetal_Init(g_shell.device);

        // Any input wakes the loop; a drag the chrome started moves the window.
        [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskAny
                                              handler:^NSEvent*(NSEvent* event) {
                                                if (g_shell.dragging) {
                                                    if (event.type == NSEventTypeLeftMouseDragged) {
                                                        const NSPoint m = [NSEvent mouseLocation];
                                                        [g_shell.window
                                                            setFrameOrigin:NSMakePoint(g_shell.drag_origin.x + m.x -
                                                                                           g_shell.drag_mouse.x,
                                                                                       g_shell.drag_origin.y + m.y -
                                                                                           g_shell.drag_mouse.y)];
                                                    } else if (event.type == NSEventTypeLeftMouseUp) {
                                                        g_shell.dragging = false;
                                                    }
                                                }
                                                schedule_pump(3);
                                                return option_as_alt(event);
                                              }];

        ui::HostClientOptions hopts;
        hopts.host_exe = ipc::sibling_executable(L"agent-host");
        hopts.pipe_name = g_shell.dev.pipe.empty() ? ipc::default_pipe_name() : g_shell.dev.pipe;
        ui::updates::set_pipe(g_shell.dev.pipe);
        hopts.wake = [] { schedule_pump(2); };

        agentdesk::desktop::AppConfig cfg;
        cfg.hwnd = (__bridge void*)window;
        cfg.wake = [] { schedule_pump(2); };
        cfg.start_profile = g_shell.dev.start_profile;
        cfg.run_commands = g_shell.dev.run;
        cfg.input_script = g_shell.dev.input;
        if (!g_shell.dev.report.empty())
            cfg.shot_signal = std::filesystem::path(g_shell.dev.report).concat(".shot");
        cfg.automated = automated;
        auto app = std::make_unique<agentdesk::desktop::App>(cfg, std::make_unique<ui::HostClient>(hopts));
        g_shell.app = app.get();
        app->load_fonts();
        // Sizes are in points; ImGui rasterizes fonts at the backing scale itself.
        app->apply_scale(1.0f);

        [window makeKeyAndOrderFront:nil];
        if (place.maximized)
            [window zoom:nil];
        [NSApp activateIgnoringOtherApps:YES];
        g_shell.launched = Clock::now();
        schedule_pump(3);
        [NSApp run];

        g_shell.app = nullptr;
        ImGui_ImplMetal_Shutdown();
        ImGui_ImplOSX_Shutdown();
        ImGui::DestroyContext();
        app.reset();
        [window orderOut:nil];
    }
    return 0;
}
