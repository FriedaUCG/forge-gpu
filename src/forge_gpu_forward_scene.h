#ifndef SDLGPU_FORGE_GPU_FORWARD_SCENE_H
#define SDLGPU_FORGE_GPU_FORWARD_SCENE_H

#include "forge_gpu_internal.h"

typedef struct ForgeGpuForwardSceneVertUniforms
{
    Mat4 mvp;
    Mat4 model;
    Mat4 light_vp;
} ForgeGpuForwardSceneVertUniforms;

typedef struct ForgeGpuForwardSceneFragUniforms
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
} ForgeGpuForwardSceneFragUniforms;

typedef struct ForgeGpuForwardSkyboxVertUniforms
{
    Mat4 vp_no_translation;
} ForgeGpuForwardSkyboxVertUniforms;

typedef struct ForgeGpuShadowedGridVertUniforms
{
    Mat4 vp;
    Mat4 light_vp;
} ForgeGpuShadowedGridVertUniforms;

typedef struct ForgeGpuShadowedGridFragUniforms
{
    float line_color[4];
    float bg_color[4];
    float light_dir[3];
    float light_intensity;
    float eye_pos[3];
    float grid_spacing;
    float line_width;
    float fade_distance;
    float ambient;
    float pad0;
} ForgeGpuShadowedGridFragUniforms;

typedef struct ForgeGpuForwardSceneLighting
{
    Vec3 light_dir;
    Vec3 light_color;
    float light_intensity;
    float ambient;
    float shininess;
    float specular_strength;
} ForgeGpuForwardSceneLighting;

typedef struct ForgeGpuForwardSceneDrawInfo
{
    Mat4 cam_vp;
    Mat4 light_vp;
    Vec3 eye_pos;
    SDL_GPUTexture *shadow_depth;
    SDL_GPUTexture *fallback_texture;
    SDL_GPUSampler *material_sampler;
    SDL_GPUSampler *shadow_sampler;
    ForgeGpuForwardSceneLighting lighting;
} ForgeGpuForwardSceneDrawInfo;

typedef struct ForgeGpuShadowedGridDrawInfo
{
    Mat4 vp;
    Mat4 light_vp;
    Vec3 light_dir;
    Vec3 eye_pos;
    float light_intensity;
    float line_color[4];
    float bg_color[4];
    float grid_spacing;
    float line_width;
    float fade_distance;
    float ambient;
    SDL_GPUTexture *shadow_depth;
    SDL_GPUSampler *shadow_sampler;
} ForgeGpuShadowedGridDrawInfo;

bool ForgeGpuCreateShadowedGridBuffers(
    SDL_GPUDevice *device,
    float half_size,
    float y,
    SDL_GPUBuffer **vertex_buffer,
    SDL_GPUBuffer **index_buffer);
void ForgeGpuDrawForwardSceneModel(
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    const GpuSceneData *model,
    Mat4 placement,
    const ForgeGpuForwardSceneDrawInfo *draw_info);
void ForgeGpuDrawForwardSceneMesh(
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    const GpuSceneData *model,
    int mesh_index,
    Mat4 model_matrix,
    const ForgeGpuForwardSceneDrawInfo *draw_info);
void ForgeGpuDrawForwardSceneBuffer(
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    SDL_GPUBuffer *vertex_buffer,
    SDL_GPUBuffer *index_buffer,
    SDL_GPUIndexElementSize index_type,
    Uint32 index_count,
    Mat4 model_matrix,
    const GpuMaterial *material,
    const ForgeGpuForwardSceneDrawInfo *draw_info);
void ForgeGpuDrawForwardShadowMesh(
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    const GpuSceneData *model,
    int mesh_index,
    Mat4 model_matrix,
    Mat4 light_vp);
void ForgeGpuDrawForwardShadowBuffer(
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    SDL_GPUBuffer *vertex_buffer,
    SDL_GPUBuffer *index_buffer,
    SDL_GPUIndexElementSize index_type,
    Uint32 index_count,
    Mat4 light_mvp);
void ForgeGpuDrawForwardSkybox(
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    SDL_GPUTexture *cubemap_texture,
    SDL_GPUSampler *sampler,
    SDL_GPUBuffer *vertex_buffer,
    SDL_GPUBuffer *index_buffer,
    SDL_GPUIndexElementSize index_type,
    Uint32 index_count,
    Mat4 view,
    Mat4 projection);
void ForgeGpuDrawShadowedGrid(
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    SDL_GPUGraphicsPipeline *pipeline,
    SDL_GPUBuffer *vertex_buffer,
    SDL_GPUBuffer *index_buffer,
    const ForgeGpuShadowedGridDrawInfo *draw_info);

#endif /* SDLGPU_FORGE_GPU_FORWARD_SCENE_H */
