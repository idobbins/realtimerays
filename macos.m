#import <AppKit/AppKit.h>
#import <QuartzCore/CAMetalLayer.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "camera_defaults.h"
#include "scene_layout.h"

uint32_t rtrSceneKindFromName(const char *scene);

#define RTR_CAMERA_DEFAULT_PITCH RTR_CAMERA_FOREST_DEFAULT_PITCH
#define RTR_CAMERA_DEFAULT_RADIUS RTR_CAMERA_FOREST_DEFAULT_RADIUS
#define RTR_CAMERA_MIN_PITCH 0.08f
#define RTR_CAMERA_MAX_PITCH 1.15f
#define RTR_CAMERA_MIN_RADIUS 1.2f
#define RTR_CAMERA_MAX_RADIUS 9.0f
#define RTR_MOUSE_DRAG_THRESHOLD 4.0f
#define RTR_DEFAULT_SPP 1u
#define RTR_DEFAULT_GI_SPP 2u

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
static uint32_t rtrSettingSpp = RTR_DEFAULT_SPP;
static uint32_t rtrSettingSppMax = 8u;
static uint32_t rtrSettingLightSpp = 1u;
static uint32_t rtrSettingLightSppMax = 2u;
static uint32_t rtrSettingGiSpp = RTR_DEFAULT_GI_SPP;
static uint32_t rtrSettingGiSppMax = 2u;

static float rtrClamp(float value, float lo, float hi)
{
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}

static uint32_t rtrClampU32(uint32_t value, uint32_t lo, uint32_t hi)
{
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}

static uint32_t rtrSettingEnv(const char *name, uint32_t fallback)
{
    const char *value = getenv(name);
    return value && *value ? (uint32_t)strtoul(value, NULL, 10) : fallback;
}

static void rtrPrintSettings(void)
{
    printf("settings: spp %u light %u gi %u\n",
           rtrSettingSpp, rtrSettingLightSpp, rtrSettingGiSpp);
    fflush(stdout);
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
    const uint32_t sceneKind = rtrSceneKindFromName(getenv("RTR_SCENE"));
    if (sceneKind == RTR_SCENE_KIND_CASTLE) {
        rtrCameraPitch = RTR_CAMERA_CASTLE_DEFAULT_PITCH;
        rtrCameraRadius = RTR_CAMERA_CASTLE_DEFAULT_RADIUS;
    } else if (sceneKind == RTR_SCENE_KIND_CITY100K) {
        rtrCameraPitch = RTR_CAMERA_CITY100K_DEFAULT_PITCH;
        rtrCameraRadius = RTR_CAMERA_CITY100K_DEFAULT_RADIUS;
    } else if (sceneKind == RTR_SCENE_KIND_WORLD) {
        rtrCameraPitch = RTR_CAMERA_WORLD_DEFAULT_PITCH;
        rtrCameraRadius = RTR_CAMERA_WORLD_DEFAULT_RADIUS;
    } else {
        rtrCameraPitch = RTR_CAMERA_FOREST_DEFAULT_PITCH;
        rtrCameraRadius = RTR_CAMERA_FOREST_DEFAULT_RADIUS;
    }
    rtrAutoOrbit = 1u;
    rtrMouseDragging = 0u;
    rtrMouseDragActive = 0u;
    rtrMouseDownX = 0.0f;
    rtrMouseDownY = 0.0f;

    /* Interactive ceilings: the renderer sizes its queues and prerecords
     * sample waves for these maxima; env values raise them at launch. */
    rtrSettingSpp =
        rtrClampU32(rtrSettingEnv("RTR_SPP", RTR_DEFAULT_SPP), 1u,
                    rtrSettingSppMax);
    rtrSettingLightSppMax =
        rtrClampU32(rtrSettingEnv("RTR_LIGHT_SPP", 1u), 2u, 4u);
    rtrSettingGiSppMax =
        rtrClampU32(rtrSettingEnv("RTR_GI_SPP", RTR_DEFAULT_GI_SPP), 2u, 4u);
    rtrSettingLightSpp =
        rtrClampU32(rtrSettingEnv("RTR_LIGHT_SPP", 1u), 1u,
                    rtrSettingLightSppMax);
    rtrSettingGiSpp =
        rtrClampU32(rtrSettingEnv("RTR_GI_SPP", RTR_DEFAULT_GI_SPP), 1u,
                    rtrSettingGiSppMax);

    printf("controls: drag orbit, scroll zoom, space auto-orbit, "
           "1-8 spp, l light samples, g gi chains, esc quit\n");
    rtrPrintSettings();
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

void rtrWindowRenderSettings(uint32_t *spp, uint32_t *lightSpp, uint32_t *giSpp)
{
    if (spp) *spp = rtrSettingSpp;
    if (lightSpp) *lightSpp = rtrSettingLightSpp;
    if (giSpp) *giSpp = rtrSettingGiSpp;
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
                    rtrMouseDragging = 0u;
                    rtrMouseDragActive = 0u;
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

            uint32_t rtrIsSetting = 0u;
            if (type == NSEventTypeKeyDown && ![event isARepeat]) {
                NSString *chars =
                    [[event charactersIgnoringModifiers] lowercaseString];
                unichar c = [chars length] > 0 ?
                    [chars characterAtIndex:0] : 0;

                if (c >= '1' && c <= '8') {
                    rtrSettingSpp = rtrClampU32((uint32_t)(c - '0'), 1u,
                                                rtrSettingSppMax);
                    rtrIsSetting = 1u;
                } else if (c == 'l') {
                    rtrSettingLightSpp =
                        rtrSettingLightSpp % rtrSettingLightSppMax + 1u;
                    rtrIsSetting = 1u;
                } else if (c == 'g') {
                    rtrSettingGiSpp =
                        rtrSettingGiSpp % rtrSettingGiSppMax + 1u;
                    rtrIsSetting = 1u;
                }
                if (rtrIsSetting) rtrPrintSettings();
            }

            if (rtrIsSpace && ![event isARepeat]) {
                rtrAutoOrbit = rtrAutoOrbit ? 0u : 1u;
            }
            rtrShouldQuit |= rtrIsEscape;
            if (!(rtrIsEscape || rtrIsSpace || rtrIsSetting))
                [NSApp sendEvent:event];
        }

        rtrShouldQuit |= (uint32_t)(![rtrWindowHandle isVisible]);
    }
    return (int)rtrShouldQuit;
}
