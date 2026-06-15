#ifndef SDLGPU_FORGE_GPU_IMGUI_H
#define SDLGPU_FORGE_GPU_IMGUI_H

#include "forge_gpu_internal.h"

bool ForgeGpuInitImGui(ForgeGpuDemo *demo);
void ForgeGpuShutdownImGui(ForgeGpuDemo *demo);
bool ForgeGpuProcessImGuiEvent(ForgeGpuDemo *demo, const SDL_Event *event);
void ForgeGpuClearImGuiMouse(ForgeGpuDemo *demo);
bool ForgeGpuPrepareImGui(ForgeGpuDemo *demo, SDL_GPUCommandBuffer *command_buffer, Uint32 width, Uint32 height);
bool ForgeGpuRenderImGui(ForgeGpuDemo *demo, SDL_GPUCommandBuffer *command_buffer, SDL_GPUTexture *swapchain_texture);

#endif /* SDLGPU_FORGE_GPU_IMGUI_H */
