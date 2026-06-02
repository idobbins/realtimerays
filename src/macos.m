#import <AppKit/AppKit.h>
#import <QuartzCore/CAMetalLayer.h>
#include <stdint.h>

static NSWindow *rtrWindowHandle = nil;
static void *rtrSurfaceLayer = NULL;
static uint32_t rtrShouldQuit = 0u;
static float rtrMouseX = -1.0f;
static float rtrMouseY = -1.0f;

int rtrInitWindow(uint32_t width, uint32_t height, const char *title)
{
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    [NSApp finishLaunching];

    NSScreen *screen = [NSScreen mainScreen];
    const CGFloat scale = screen ? [screen backingScaleFactor] : 1.0;
    const CGFloat pointWidth = (CGFloat)width / scale;
    const CGFloat pointHeight = (CGFloat)height / scale;

    rtrWindowHandle = [[NSWindow alloc] initWithContentRect:NSMakeRect(0.0, 0.0, pointWidth, pointHeight)
                                                  styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable)
                                                    backing:NSBackingStoreBuffered
                                                      defer:NO];
    [rtrWindowHandle center];

    NSView *view = [rtrWindowHandle contentView];
    rtrSurfaceLayer = (void *)[CAMetalLayer layer];
    if (!(rtrWindowHandle && view && rtrSurfaceLayer)) return 1;

    [rtrWindowHandle setReleasedWhenClosed:NO];
    [rtrWindowHandle setTitle:title ? [NSString stringWithUTF8String:title] : @""];
    [rtrWindowHandle setAcceptsMouseMovedEvents:YES];
    [view setWantsLayer:YES];
    [(CAMetalLayer *)rtrSurfaceLayer setOpaque:YES];
    [(CAMetalLayer *)rtrSurfaceLayer setContentsScale:scale];
    [(CAMetalLayer *)rtrSurfaceLayer setDrawableSize:CGSizeMake((CGFloat)width, (CGFloat)height)];
    [view setLayer:(CAMetalLayer *)rtrSurfaceLayer];
    [rtrWindowHandle makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
    rtrShouldQuit = 0u;
    return 0;
}

void rtrShutdownWindow(void)
{
    rtrShouldQuit = 1u;
}

void *rtrWindowSurface(void)
{
    return rtrSurfaceLayer;
}

void rtrWindowMouse(float *x, float *y)
{
    if (x) *x = rtrMouseX;
    if (y) *y = rtrMouseY;
}

int rtrPumpEventsOnce(void)
{
    @autoreleasepool {
        NSEvent *event = nil;
        if (!rtrWindowHandle) return 1;

        while (!rtrShouldQuit &&
               (event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                           untilDate:[NSDate distantPast]
                                              inMode:NSDefaultRunLoopMode
                                             dequeue:YES]) != nil) {
            const NSEventType type = [event type];
            const uint32_t rtrIsEscape =
                (uint32_t)(type == NSEventTypeKeyDown && [event keyCode] == 53);
            if (type == NSEventTypeMouseMoved ||
                type == NSEventTypeLeftMouseDown ||
                type == NSEventTypeLeftMouseUp ||
                type == NSEventTypeLeftMouseDragged ||
                type == NSEventTypeRightMouseDown ||
                type == NSEventTypeRightMouseUp ||
                type == NSEventTypeRightMouseDragged ||
                type == NSEventTypeOtherMouseDown ||
                type == NSEventTypeOtherMouseUp ||
                type == NSEventTypeOtherMouseDragged) {
                NSView *view = [rtrWindowHandle contentView];
                NSPoint p = [view convertPoint:[event locationInWindow] fromView:nil];
                const CGFloat scale = [rtrWindowHandle backingScaleFactor];
                rtrMouseX = (float)(p.x * scale);
                rtrMouseY = (float)(([view bounds].size.height - p.y) * scale);
            }
            rtrShouldQuit |= rtrIsEscape;
            if (!rtrIsEscape) [NSApp sendEvent:event];
        }

        rtrShouldQuit |= (uint32_t)(![rtrWindowHandle isVisible]);
    }
    return (int)rtrShouldQuit;
}
