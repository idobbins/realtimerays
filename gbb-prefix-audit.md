# `gbb*` Prefix Audit

Generated: 2026-05-08

Search pattern used:

```sh
rg -n "\\bgbb[A-Za-z0-9_]*\\b" .
```

## Summary

Found 4 `gbb*` symbols across 13 references in 4 source files.

| Symbol | Kind | Notes |
| --- | --- | --- |
| `gbbInitWindow` | platform API function | Declared in `src/platform.h`; implemented for Windows and macOS; called from `src/main.c`. |
| `gbbShutdownWindow` | platform API function | Declared in `src/platform.h`; implemented for Windows and macOS; no direct call sites found. |
| `gbbPumpEventsOnce` | platform API function | Declared in `src/platform.h`; implemented for Windows and macOS; called from `src/main.c`. |
| `gbbWindowProc` | Windows-only static callback | Defined in `src/windows_platform.c`; assigned to `WNDCLASSA.lpfnWndProc`; no direct call sites found. |

## Direct call sites

| Symbol | File | Line | Code |
| --- | --- | ---: | --- |
| `gbbInitWindow` | `src/main.c` | 109 | `gbbInitWindow(1280u, 720u, APPLICATION_NAME);` |
| `gbbPumpEventsOnce` | `src/main.c` | 293 | `while (gbbPumpEventsOnce() == 0)` |

## Declarations, definitions, and callback references

| Symbol | File | Line | Usage | Code |
| --- | --- | ---: | --- | --- |
| `gbbInitWindow` | `src/platform.h` | 16 | declaration | `int gbbInitWindow(uint32_t width, uint32_t height, const char* title);` |
| `gbbShutdownWindow` | `src/platform.h` | 17 | declaration | `void gbbShutdownWindow(void);` |
| `gbbPumpEventsOnce` | `src/platform.h` | 18 | declaration | `int gbbPumpEventsOnce(void);` |
| `gbbWindowProc` | `src/windows_platform.c` | 11 | definition | `static LRESULT CALLBACK gbbWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)` |
| `gbbInitWindow` | `src/windows_platform.c` | 44 | definition | `int gbbInitWindow(uint32_t width, uint32_t height, const char* title)` |
| `gbbWindowProc` | `src/windows_platform.c` | 54 | callback reference | `window_class.lpfnWndProc   = gbbWindowProc;` |
| `gbbShutdownWindow` | `src/windows_platform.c` | 75 | definition | `void gbbShutdownWindow(void)` |
| `gbbPumpEventsOnce` | `src/windows_platform.c` | 90 | definition | `int gbbPumpEventsOnce(void)` |
| `gbbInitWindow` | `src/macos_platform.m` | 11 | definition | `int gbbInitWindow(uint32_t width, uint32_t height, const char* title)` |
| `gbbShutdownWindow` | `src/macos_platform.m` | 45 | definition | `void gbbShutdownWindow(void)` |
| `gbbPumpEventsOnce` | `src/macos_platform.m` | 55 | definition | `int gbbPumpEventsOnce(void)` |

## Complete occurrence list

```text
src/main.c:109:    gbbInitWindow(1280u, 720u, APPLICATION_NAME);
src/main.c:293:    while (gbbPumpEventsOnce() == 0)
src/platform.h:16:int gbbInitWindow(uint32_t width, uint32_t height, const char* title);
src/platform.h:17:void gbbShutdownWindow(void);
src/platform.h:18:int gbbPumpEventsOnce(void);
src/windows_platform.c:11:static LRESULT CALLBACK gbbWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
src/windows_platform.c:44:int gbbInitWindow(uint32_t width, uint32_t height, const char* title)
src/windows_platform.c:54:    window_class.lpfnWndProc   = gbbWindowProc;
src/windows_platform.c:75:void gbbShutdownWindow(void)
src/windows_platform.c:90:int gbbPumpEventsOnce(void)
src/macos_platform.m:11:int gbbInitWindow(uint32_t width, uint32_t height, const char* title)
src/macos_platform.m:45:void gbbShutdownWindow(void)
src/macos_platform.m:55:int gbbPumpEventsOnce(void)
```
