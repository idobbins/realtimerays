#include <stdint.h>

int rtrInitWindow(uint32_t width, uint32_t height, const char *title);
void *rtrWindowSurface(void);
int rtrPumpEventsOnce(void);
int rtrVulkanInit(void *windowSurface);
void rtrVulkanFrame(void);

int main(void)
{
    rtrInitWindow(1280u, 720u, "realtimerays");
    if (rtrVulkanInit(rtrWindowSurface())) return 1;

    while (rtrPumpEventsOnce() == 0) {
        rtrVulkanFrame();
    }

    return 0;
}
