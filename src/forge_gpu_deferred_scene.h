#ifndef SDLGPU_FORGE_GPU_DEFERRED_SCENE_H
#define SDLGPU_FORGE_GPU_DEFERRED_SCENE_H

#include "forge_gpu_internal.h"

typedef struct ForgeGpuBoxPlacement
{
    Vec3 position;
    float y_rotation;
} ForgeGpuBoxPlacement;

typedef struct ForgeGpuShadowVertUniforms
{
    Mat4 light_mvp;
} ForgeGpuShadowVertUniforms;

typedef struct ForgeGpuDeferredSceneVertUniforms
{
    Mat4 mvp;
    Mat4 model;
    Mat4 view;
    Mat4 light_vp;
} ForgeGpuDeferredSceneVertUniforms;

typedef struct ForgeGpuDeferredSceneFragUniforms
{
    float base_color[4];
    float eye_pos[3];
    float has_texture;
    float ambient;
    float shininess;
    float specular_str;
    float pad0;
    float light_dir[4];
    float light_color[3];
    float light_intensity;
} ForgeGpuDeferredSceneFragUniforms;

typedef struct ForgeGpuDeferredSceneLighting
{
    Vec3 light_dir;
    Vec3 light_color;
    float light_intensity;
    float ambient;
    float shininess;
    float specular_strength;
} ForgeGpuDeferredSceneLighting;

typedef struct ForgeGpuDeferredSceneDrawInfo
{
    Mat4 cam_vp;
    Mat4 view;
    Mat4 light_vp;
    SDL_GPUTexture *shadow_depth;
    SDL_GPUSampler *material_sampler;
    SDL_GPUSampler *shadow_sampler;
    ForgeGpuDeferredSceneLighting lighting;
} ForgeGpuDeferredSceneDrawInfo;

Mat4 ForgeGpuComputeDirectionalLightViewProjection(
    Vec3 light_dir,
    float light_distance,
    float ortho_size,
    float near_plane,
    float far_plane,
    float parallel_threshold);
Mat4 ForgeGpuBoxPlacementMatrix(const ForgeGpuBoxPlacement *placement);
void ForgeGpuDrawModelShadow(
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    const GpuSceneData *model,
    Mat4 placement,
    Mat4 light_vp);
void ForgeGpuDrawDeferredSceneModel(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    const GpuSceneData *model,
    Mat4 placement,
    const ForgeGpuDeferredSceneDrawInfo *draw_info);
void ForgeGpuDrawShadowedBoxScene(
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    const GpuSceneData *primary_model,
    const GpuSceneData *box_model,
    const ForgeGpuBoxPlacement *box_placements,
    int box_count,
    Mat4 light_vp);
void ForgeGpuDrawDeferredBoxScene(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    const GpuSceneData *primary_model,
    const GpuSceneData *box_model,
    const ForgeGpuBoxPlacement *box_placements,
    int box_count,
    const ForgeGpuDeferredSceneDrawInfo *draw_info);

#endif /* SDLGPU_FORGE_GPU_DEFERRED_SCENE_H */
