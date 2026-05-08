#import <AppKit/AppKit.h>
#import <QuartzCore/CAMetalLayer.h>
#include "platform.h"

void *surface_layer;
static NSWindow *window;
static uint32_t should_quit;

void rtrInitWindow(uint32_t width, uint32_t height, const char *title)
{
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    [NSApp finishLaunching];

    window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, width, height)
                                         styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskFullSizeContentView)
                                           backing:NSBackingStoreBuffered
                                             defer:NO];
    [window center];
    [window setTitleVisibility:NSWindowTitleHidden];
    [window setTitlebarAppearsTransparent:YES];
    [window setReleasedWhenClosed:NO];
    [window setTitle:[NSString stringWithUTF8String:title]];

    NSView *view = [window contentView];
    CAMetalLayer *layer = [CAMetalLayer layer];
    const CGFloat scale = [window backingScaleFactor];
    [layer setOpaque:YES];
    [layer setContentsScale:scale];
    [layer setDrawableSize:CGSizeMake(width * scale, height * scale)];
    [view setWantsLayer:YES];
    [view setLayer:layer];
    surface_layer = (__bridge void *)layer;

    [window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
}

int rtrPumpEventsOnce(void)
{
    @autoreleasepool {
        for (NSEvent *event;
             !should_quit && (event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                                         untilDate:[NSDate distantPast]
                                                            inMode:NSDefaultRunLoopMode
                                                           dequeue:YES]) != nil;)
        {
            if ([event type] == NSEventTypeKeyDown && [event keyCode] == 53)
                should_quit = 1;
            else
                [NSApp sendEvent:event];
        }
        if (![window isVisible]) should_quit = 1;
    }
    return (int)should_quit;
}
