#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdint.h>

extern void *surface_layer;

void rtrInitWindow(uint32_t width, uint32_t height, const char *title);
int  rtrPumpEventsOnce(void);

#endif
