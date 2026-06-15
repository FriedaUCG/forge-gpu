#ifndef SDLGPU_FORGE_GPU_SCENE_H
#define SDLGPU_FORGE_GPU_SCENE_H

#include "forge_gpu_internal.h"

void ForgeGpuFreeGpuScene(ForgeGpuDemo *demo);
void ForgeGpuFreeSceneData(ForgeGpuDemo *demo, GpuSceneData *scene);
bool ForgeGpuUploadLoadedSceneToGpu(ForgeGpuDemo *demo);
bool ForgeGpuUploadSceneDataToGpu(ForgeGpuDemo *demo, GpuSceneData *scene);
bool ForgeGpuRunSceneTextureCacheSelfTest(ForgeGpuDemo *demo, const char *texture_path);
bool ForgeGpuRunNormalMapSceneSelfTest(ForgeGpuDemo *demo, const char *scene_path);

#endif /* SDLGPU_FORGE_GPU_SCENE_H */
