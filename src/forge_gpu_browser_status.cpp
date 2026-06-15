#include "forge_gpu_browser_status.h"

#include <SDL3/SDL.h>

#if defined(SDL_PLATFORM_EMSCRIPTEN)
#include "browser_status.h"

#include <emscripten.h>

EM_JS(int, SDLGPU_ForgeBrowserHasPointerLock, (), {
    const canvas = Module['canvas'];
    const locked = document.pointerLockElement ||
        document.mozPointerLockElement ||
        document.webkitPointerLockElement;
    return canvas && locked === canvas ? 1 : 0;
});
#endif

void ForgeGpuBrowserSetStatus(const char *message, int frame)
{
#if defined(SDL_PLATFORM_EMSCRIPTEN)
    SDLGPU_DemoSetBrowserStatus(message, frame);
#else
    (void)message;
    (void)frame;
#endif
}

void ForgeGpuBrowserSetNumberMetric(const char *key, double value)
{
#if defined(SDL_PLATFORM_EMSCRIPTEN)
    SDLGPU_DemoSetBrowserNumberMetric(key, value);
#else
    (void)key;
    (void)value;
#endif
}

int ForgeGpuBrowserGetValidationMode(void)
{
#if defined(SDL_PLATFORM_EMSCRIPTEN)
    return SDLGPU_DemoGetBrowserValidationMode();
#else
    return 0;
#endif
}

int ForgeGpuBrowserHasPointerLock(void)
{
#if defined(SDL_PLATFORM_EMSCRIPTEN)
    return SDLGPU_ForgeBrowserHasPointerLock();
#else
    return 0;
#endif
}
