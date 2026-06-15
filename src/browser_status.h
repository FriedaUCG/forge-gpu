#ifndef FORGE_GPU_BROWSER_STATUS_H
#define FORGE_GPU_BROWSER_STATUS_H

#include <SDL3/SDL.h>

#include <emscripten.h>

EM_JS(void, SDLGPU_DemoSetBrowserStatus, (const char *message, int frame), {
    const text = UTF8ToString(message);

    document.body.dataset.sdlGpuStatus = text;
    document.body.dataset.sdlGpuFrame = String(frame);

    if (globalThis.__sdlGpuBrowserStatusOverlayEnabled === false) {
        const existing = document.getElementById("sdl-gpu-status");
        if (existing) {
            existing.remove();
        }
        return;
    }

    let status = document.getElementById("sdl-gpu-status");
    if (!status) {
        status = document.createElement("div");
        status.id = "sdl-gpu-status";
        status.style.cssText =
            "position:fixed;left:12px;top:12px;z-index:10;padding:6px 8px;" +
            "background:#111;color:#fff;font:16px system-ui,sans-serif";
        document.body.appendChild(status);
    }

    status.textContent = text;

    if (globalThis.__sdlGpuBrowserStatusLoggingEnabled !== false) {
        console.log("SDL_GPU: " + text);
    }
});

EM_JS(void, SDLGPU_DemoSetBrowserNumberMetric, (const char *key, double value), {
    document.body.dataset[UTF8ToString(key)] = String(value);
});

EM_JS(void, SDLGPU_DemoSetBrowserStatusLoggingEnabled, (int enabled), {
    globalThis.__sdlGpuBrowserStatusLoggingEnabled = !!enabled;
});

static void SDLGPU_DemoPublishWebGPUCounterMetric(
    SDL_PropertiesID props,
    const char *property_name,
    const char *dataset_key)
{
    const Sint64 value = props ? SDL_GetNumberProperty(props, property_name, -1) : -1;
    SDLGPU_DemoSetBrowserNumberMetric(dataset_key, (double)value);
}

static void SDLGPU_DemoPublishWebGPUBindGroupTelemetry(SDL_GPUDevice *device)
{
    SDL_PropertiesID props = SDL_GetGPUDeviceProperties(device);

    SDLGPU_DemoPublishWebGPUCounterMetric(
        props,
        "SDL.internal.gpu.webgpu.bind_group.create.total",
        "sdlGpuWebgpuBindGroupCreateTotal");
    SDLGPU_DemoPublishWebGPUCounterMetric(
        props,
        "SDL.internal.gpu.webgpu.bind_group.set.total",
        "sdlGpuWebgpuBindGroupSetTotal");
    SDLGPU_DemoPublishWebGPUCounterMetric(
        props,
        "SDL.internal.gpu.webgpu.bind_group.clean_apply.total",
        "sdlGpuWebgpuBindGroupCleanApplyTotal");
    SDLGPU_DemoPublishWebGPUCounterMetric(
        props,
        "SDL.internal.gpu.webgpu.bind_group.cache_hit.total",
        "sdlGpuWebgpuBindGroupCacheHitTotal");
    SDLGPU_DemoPublishWebGPUCounterMetric(
        props,
        "SDL.internal.gpu.webgpu.bind_group.cache_miss.total",
        "sdlGpuWebgpuBindGroupCacheMissTotal");
    SDLGPU_DemoPublishWebGPUCounterMetric(
        props,
        "SDL.internal.gpu.webgpu.bind_group.cache_insert_fail.total",
        "sdlGpuWebgpuBindGroupCacheInsertFailTotal");
    SDLGPU_DemoPublishWebGPUCounterMetric(
        props,
        "SDL.internal.gpu.webgpu.bind_group.peak.command_buffer_bind_groups",
        "sdlGpuWebgpuBindGroupPeakCommandBufferBindGroups");
    SDLGPU_DemoPublishWebGPUCounterMetric(
        props,
        "SDL.internal.gpu.webgpu.bind_group.peak.command_buffer_resource_cache_entries",
        "sdlGpuWebgpuBindGroupPeakCommandBufferResourceCacheEntries");
    SDLGPU_DemoPublishWebGPUCounterMetric(
        props,
        "SDL.internal.gpu.webgpu.bind_group.peak.command_buffer_resource_cache_capacity",
        "sdlGpuWebgpuBindGroupPeakCommandBufferResourceCacheCapacity");
}

EM_JS(int, SDLGPU_DemoGetBrowserValidationMode, (void), {
    const params = new URLSearchParams(window.location.search);
    return params.has("validation") && params.get("validation") !== "0" ?
        1 : 0;
});

#endif
