#import <AppKit/AppKit.h>
#import <QuartzCore/CAMetalLayer.h>
#include <stdint.h>
#include <math.h>

#define RTR_CAMERA_DEFAULT_YAW 0.35f
#define RTR_CAMERA_DEFAULT_PITCH 0.473f
#define RTR_CAMERA_DEFAULT_RADIUS 2.35f
#define RTR_CAMERA_MIN_PITCH 0.08f
#define RTR_CAMERA_MAX_PITCH 1.15f
#define RTR_CAMERA_MIN_RADIUS 1.2f
#define RTR_CAMERA_MAX_RADIUS 9.0f
#define RTR_MOUSE_DRAG_THRESHOLD 4.0f

static NSWindow *rtrWindowHandle = nil;
static void *rtrSurfaceLayer = NULL;
static uint32_t rtrShouldQuit = 0u;
static float rtrMouseX = -1.0f;
static float rtrMouseY = -1.0f;
static float rtrCameraYaw = RTR_CAMERA_DEFAULT_YAW;
static float rtrCameraPitch = RTR_CAMERA_DEFAULT_PITCH;
static float rtrCameraRadius = RTR_CAMERA_DEFAULT_RADIUS;
static uint32_t rtrAutoOrbit = 1u;
static uint32_t rtrMouseDragging = 0u;
static uint32_t rtrMouseDragActive = 0u;
static float rtrMouseDownX = 0.0f;
static float rtrMouseDownY = 0.0f;
static uint32_t rtrImpulsePending = 0u;
static float rtrImpulseX = 0.0f;
static float rtrImpulseY = 0.0f;

static float rtrClamp(float value, float lo, float hi)
{
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}

static void rtrEnterManualOrbit(void)
{
    rtrAutoOrbit = 0u;
}

static void rtrUpdateMouseFromEvent(NSEvent *event)
{
    NSView *view = [rtrWindowHandle contentView];
    NSPoint p = [view convertPoint:[event locationInWindow] fromView:nil];
    const CGFloat scale = [rtrWindowHandle backingScaleFactor];

    rtrMouseX = (float)(p.x * scale);
    rtrMouseY = (float)(([view bounds].size.height - p.y) * scale);
}

int rtrInitWindow(uint32_t width, uint32_t height, const char *title)
{
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    [NSApp finishLaunching];

    NSScreen *screen = [NSScreen mainScreen];
    const CGFloat scale = screen ? [screen backingScaleFactor] : 1.0;
    const CGFloat windowWidth = (CGFloat)width / scale;
    const CGFloat windowHeight = (CGFloat)height / scale;

    rtrWindowHandle = [[NSWindow alloc] initWithContentRect:NSMakeRect(0.0, 0.0, windowWidth, windowHeight)
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
    rtrMouseX = -1.0f;
    rtrMouseY = -1.0f;
    rtrCameraYaw = RTR_CAMERA_DEFAULT_YAW;
    rtrCameraPitch = RTR_CAMERA_DEFAULT_PITCH;
    rtrCameraRadius = RTR_CAMERA_DEFAULT_RADIUS;
    rtrAutoOrbit = 1u;
    rtrMouseDragging = 0u;
    rtrMouseDragActive = 0u;
    rtrMouseDownX = 0.0f;
    rtrMouseDownY = 0.0f;
    rtrImpulsePending = 0u;
    rtrImpulseX = 0.0f;
    rtrImpulseY = 0.0f;
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

void rtrWindowCamera(uint32_t *autoOrbit, float *yaw, float *pitch, float *radius)
{
    if (autoOrbit) *autoOrbit = rtrAutoOrbit;
    if (yaw) *yaw = rtrCameraYaw;
    if (pitch) *pitch = rtrCameraPitch;
    if (radius) *radius = rtrCameraRadius;
}

void rtrWindowSetCameraYaw(float yaw)
{
    rtrCameraYaw = yaw;
}

int rtrWindowConsumeImpulse(float *x, float *y)
{
    if (!rtrImpulsePending) return 0;

    if (x) *x = rtrImpulseX;
    if (y) *y = rtrImpulseY;
    rtrImpulsePending = 0u;
    return 1;
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
            const uint32_t rtrIsSpace =
                (uint32_t)(type == NSEventTypeKeyDown && [event keyCode] == 49);
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
                const float oldMouseX = rtrMouseX;
                const float oldMouseY = rtrMouseY;

                rtrUpdateMouseFromEvent(event);

                if (type == NSEventTypeLeftMouseDown) {
                    rtrMouseDragging = 1u;
                    rtrMouseDragActive = 0u;
                    rtrMouseDownX = rtrMouseX;
                    rtrMouseDownY = rtrMouseY;
                } else if (type == NSEventTypeLeftMouseUp) {
                    const float dx = rtrMouseX - rtrMouseDownX;
                    const float dy = rtrMouseY - rtrMouseDownY;
                    const uint32_t click =
                        rtrMouseDragging &&
                        !rtrMouseDragActive &&
                        dx * dx + dy * dy <=
                            RTR_MOUSE_DRAG_THRESHOLD * RTR_MOUSE_DRAG_THRESHOLD;
                    if (click) {
                        rtrImpulseX = rtrMouseX;
                        rtrImpulseY = rtrMouseY;
                        rtrImpulsePending = 1u;
                    }
                    rtrMouseDragging = 0u;
                    rtrMouseDragActive = 0u;
                } else if (type == NSEventTypeRightMouseDown) {
                    rtrImpulseX = rtrMouseX;
                    rtrImpulseY = rtrMouseY;
                    rtrImpulsePending = 1u;
                } else if (type == NSEventTypeLeftMouseDragged && rtrMouseDragging &&
                           oldMouseX >= 0.0f && oldMouseY >= 0.0f) {
                    const float dx = rtrMouseX - oldMouseX;
                    const float dy = rtrMouseY - oldMouseY;
                    const float dragX = rtrMouseX - rtrMouseDownX;
                    const float dragY = rtrMouseY - rtrMouseDownY;

                    if (!rtrMouseDragActive &&
                        dragX * dragX + dragY * dragY >
                            RTR_MOUSE_DRAG_THRESHOLD * RTR_MOUSE_DRAG_THRESHOLD) {
                        rtrEnterManualOrbit();
                        rtrMouseDragActive = 1u;
                    }

                    if (rtrMouseDragActive) {
                        rtrCameraYaw += dx * 0.006f;
                        rtrCameraPitch = rtrClamp(rtrCameraPitch - dy * 0.004f,
                                                  RTR_CAMERA_MIN_PITCH,
                                                  RTR_CAMERA_MAX_PITCH);
                    }
                }
            } else if (type == NSEventTypeScrollWheel) {
                const float dy = (float)[event scrollingDeltaY];
                rtrCameraRadius = rtrClamp(rtrCameraRadius * expf(-dy * 0.045f),
                                           RTR_CAMERA_MIN_RADIUS,
                                           RTR_CAMERA_MAX_RADIUS);
            }

            if (rtrIsSpace && ![event isARepeat]) {
                rtrAutoOrbit = rtrAutoOrbit ? 0u : 1u;
            }
            rtrShouldQuit |= rtrIsEscape;
            if (!(rtrIsEscape || rtrIsSpace)) [NSApp sendEvent:event];
        }

        rtrShouldQuit |= (uint32_t)(![rtrWindowHandle isVisible]);
    }
    return (int)rtrShouldQuit;
}
