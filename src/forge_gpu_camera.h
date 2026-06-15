#ifndef SDLGPU_FORGE_GPU_CAMERA_H
#define SDLGPU_FORGE_GPU_CAMERA_H

#include "forge_gpu_internal.h"

bool ForgeGpuLessonUsesCameraInput(int lesson_index);
void ForgeGpuUpdateCameraFromInput(ForgeGpuDemo *demo);
void ForgeGpuCameraViewProjection(ForgeGpuDemo *demo, Uint32 width, Uint32 height, float far_plane, Mat4 *out_view, Mat4 *out_projection);

#endif /* SDLGPU_FORGE_GPU_CAMERA_H */
