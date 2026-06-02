#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int rtrInitWindow(uint32_t width, uint32_t height, const char *title);
void *rtrWindowSurface(void);
int rtrPumpEventsOnce(void);
int rtrVulkanInit(void *windowSurface);
void rtrVulkanFrame(void);
int rtrVulkanWriteFrames(FILE *out, uint32_t width, uint32_t height, uint32_t frames, uint32_t fps);

static uint32_t rtrU32Arg(const char *value, uint32_t fallback)
{
    const uint32_t parsed = (uint32_t)strtoul(value, NULL, 10);
    return parsed ? parsed : fallback;
}

int main(int argc, char **argv)
{
    if (argc >= 2 && strcmp(argv[1], "--xpost-raw") == 0) {
        const uint32_t width = argc >= 3 ? rtrU32Arg(argv[2], 1920u) : 1920u;
        const uint32_t height = argc >= 4 ? rtrU32Arg(argv[3], 1080u) : 1080u;
        const uint32_t frames = argc >= 5 ? rtrU32Arg(argv[4], 900u) : 900u;
        const uint32_t fps = argc >= 6 ? rtrU32Arg(argv[5], 30u) : 30u;
        return rtrVulkanWriteFrames(stdout, width, height, frames, fps);
    }

    rtrInitWindow(1920u, 1080u, "realtimerays");
    if (rtrVulkanInit(rtrWindowSurface())) return 1;

    while (rtrPumpEventsOnce() == 0) {
        rtrVulkanFrame();
    }

    return 0;
}
