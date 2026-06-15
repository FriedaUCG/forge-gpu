#ifndef SDLGPU_FORGE_GPU_LESSON_COMMON_H
#define SDLGPU_FORGE_GPU_LESSON_COMMON_H

#include "forge_gpu_internal.h"

extern const Uint16 kForgeGpuQuadIndices[6];
extern const LessonVertex3Color kForgeGpuCubeVertices[24];
extern const Uint16 kForgeGpuCubeIndices[36];
extern const GridVertex kForgeGpuGridVertices[4];
extern const Uint16 kForgeGpuGridIndices[6];

#define FORGE_GPU_BLOOM_MIP_COUNT 5

typedef struct ForgeGpuBloomChain
{
    SDL_GPUTexture *mips[FORGE_GPU_BLOOM_MIP_COUNT];
    Uint32 widths[FORGE_GPU_BLOOM_MIP_COUNT];
    Uint32 heights[FORGE_GPU_BLOOM_MIP_COUNT];
} ForgeGpuBloomChain;

typedef struct ForgeGpuSampledColorTargetSlot
{
    SDL_GPUTexture **texture;
    Uint32 *width;
    Uint32 *height;
    SDL_GPUTextureFormat format;
} ForgeGpuSampledColorTargetSlot;

typedef struct ForgeGpuColorTargetAttachment
{
    SDL_GPUTexture *texture;
    SDL_FColor clear_color;
} ForgeGpuColorTargetAttachment;

void ForgeGpuDestroySharedLessonResources(ForgeGpuDemo *demo);
bool ForgeGpuAcquireCameraMouse(ForgeGpuDemo *demo, float x, float y);
bool ForgeGpuReleaseCameraMouse(ForgeGpuDemo *demo);
void ForgeGpuSyncCameraMouseCapture(ForgeGpuDemo *demo);
bool ForgeGpuEventIsPlusKey(const SDL_Event *event);
bool ForgeGpuEventIsMinusKey(const SDL_Event *event);

bool ForgeGpuRenderDefaultLessonPass(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPUTexture *swapchain_texture,
    Uint32 width,
    Uint32 height,
    ForgeGpuLessonRenderPassFn draw);

bool ForgeGpuEnsureSampledColorTarget(
    ForgeGpuDemo *demo,
    SDL_GPUTexture **texture,
    Uint32 *texture_width,
    Uint32 *texture_height,
    Uint32 width,
    Uint32 height,
    SDL_GPUTextureFormat format);
bool ForgeGpuEnsureSampledDepthTarget(
    ForgeGpuDemo *demo,
    SDL_GPUTexture **texture,
    Uint32 *texture_width,
    Uint32 *texture_height,
    Uint32 width,
    Uint32 height,
    SDL_GPUTextureFormat format);
bool ForgeGpuEnsureSampledColorTargetSlots(
    ForgeGpuDemo *demo,
    const ForgeGpuSampledColorTargetSlot *slots,
    Uint32 num_slots,
    Uint32 width,
    Uint32 height);
SDL_GPUTexture *ForgeGpuCreateSampledDepthTexture(
    ForgeGpuDemo *demo,
    Uint32 width,
    Uint32 height,
    SDL_GPUTextureFormat format);
SDL_GPURenderPass *ForgeGpuBeginDepthOnlyPass(
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPUTexture *depth_texture,
    float clear_depth);
SDL_GPURenderPass *ForgeGpuBeginColorDepthPass(
    SDL_GPUCommandBuffer *command_buffer,
    const ForgeGpuColorTargetAttachment *color_targets,
    Uint32 num_color_targets,
    SDL_GPUTexture *depth_texture,
    float clear_depth);

SDL_GPUGraphicsPipeline *ForgeGpuCreateLessonGraphicsPipeline(
    ForgeGpuDemo *demo,
    SDL_GPUShader *vertex_shader,
    SDL_GPUShader *fragment_shader,
    const SDL_GPUVertexBufferDescription *vertex_buffers,
    Uint32 num_vertex_buffers,
    const SDL_GPUVertexAttribute *vertex_attributes,
    Uint32 num_vertex_attributes,
    Uint32 num_color_targets,
    bool has_depth_target,
    SDL_GPUTextureFormat depth_format,
    bool depth_test,
    bool depth_write,
    SDL_GPUCullMode cull_mode,
    float depth_bias_constant,
    float depth_bias_slope);
SDL_GPUGraphicsPipeline *ForgeGpuCreateLessonGraphicsPipelineWithPrimitive(
    ForgeGpuDemo *demo,
    SDL_GPUShader *vertex_shader,
    SDL_GPUShader *fragment_shader,
    SDL_GPUPrimitiveType primitive_type,
    const SDL_GPUVertexBufferDescription *vertex_buffers,
    Uint32 num_vertex_buffers,
    const SDL_GPUVertexAttribute *vertex_attributes,
    Uint32 num_vertex_attributes,
    Uint32 num_color_targets,
    bool has_depth_target,
    SDL_GPUTextureFormat depth_format,
    bool depth_test,
    bool depth_write,
    SDL_GPUCullMode cull_mode,
    float depth_bias_constant,
    float depth_bias_slope);
SDL_GPUGraphicsPipeline *ForgeGpuCreateLessonGraphicsPipelineWithColorTargets(
    ForgeGpuDemo *demo,
    SDL_GPUShader *vertex_shader,
    SDL_GPUShader *fragment_shader,
    SDL_GPUPrimitiveType primitive_type,
    const SDL_GPUColorTargetDescription *color_target_descriptions,
    Uint32 num_color_targets,
    const SDL_GPUVertexBufferDescription *vertex_buffers,
    Uint32 num_vertex_buffers,
    const SDL_GPUVertexAttribute *vertex_attributes,
    Uint32 num_vertex_attributes,
    bool has_depth_target,
    SDL_GPUTextureFormat depth_format,
    bool depth_test,
    bool depth_write,
    SDL_GPUCullMode cull_mode,
    float depth_bias_constant,
    float depth_bias_slope);
SDL_GPUGraphicsPipeline *ForgeGpuCreateLessonGraphicsPipelineWithColorFormat(
    ForgeGpuDemo *demo,
    SDL_GPUShader *vertex_shader,
    SDL_GPUShader *fragment_shader,
    SDL_GPUPrimitiveType primitive_type,
    SDL_GPUTextureFormat color_format,
    const SDL_GPUVertexBufferDescription *vertex_buffers,
    Uint32 num_vertex_buffers,
    const SDL_GPUVertexAttribute *vertex_attributes,
    Uint32 num_vertex_attributes,
    Uint32 num_color_targets,
    bool has_depth_target,
    SDL_GPUTextureFormat depth_format,
    bool depth_test,
    bool depth_write,
    SDL_GPUCullMode cull_mode,
    float depth_bias_constant,
    float depth_bias_slope);
SDL_GPUGraphicsPipeline *ForgeGpuCreateLessonGraphicsPipelineWithColorTargetsAndDepthCompare(
    ForgeGpuDemo *demo,
    SDL_GPUShader *vertex_shader,
    SDL_GPUShader *fragment_shader,
    SDL_GPUPrimitiveType primitive_type,
    const SDL_GPUColorTargetDescription *color_target_descriptions,
    Uint32 num_color_targets,
    const SDL_GPUVertexBufferDescription *vertex_buffers,
    Uint32 num_vertex_buffers,
    const SDL_GPUVertexAttribute *vertex_attributes,
    Uint32 num_vertex_attributes,
    bool has_depth_target,
    SDL_GPUTextureFormat depth_format,
    bool depth_test,
    bool depth_write,
    SDL_GPUCompareOp depth_compare_op,
    SDL_GPUCullMode cull_mode,
    float depth_bias_constant,
    float depth_bias_slope);
SDL_GPUGraphicsPipeline *ForgeGpuCreateFullscreenPostprocessPipeline(
    ForgeGpuDemo *demo,
    SDL_GPUShader *vertex_shader,
    SDL_GPUShader *fragment_shader,
    SDL_GPUTextureFormat color_format,
    bool additive_blend);
bool ForgeGpuRunFullscreenPostprocessPass(
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPUTexture *target_texture,
    SDL_GPULoadOp load_op,
    SDL_FColor clear_color,
    SDL_GPUGraphicsPipeline *pipeline,
    const SDL_GPUTextureSamplerBinding *fragment_samplers,
    Uint32 num_fragment_samplers,
    const void *fragment_uniform_data,
    Uint32 fragment_uniform_size,
    Uint32 vertex_count);
void ForgeGpuReleaseBloomChain(ForgeGpuDemo *demo, ForgeGpuBloomChain *chain);
bool ForgeGpuEnsureBloomChain(
    ForgeGpuDemo *demo,
    ForgeGpuBloomChain *chain,
    SDL_GPUTexture *hdr_target,
    Uint32 hdr_width,
    Uint32 hdr_height,
    SDL_GPUTextureFormat format);
bool ForgeGpuRunBloomChain(
    SDL_GPUCommandBuffer *command_buffer,
    ForgeGpuBloomChain *chain,
    SDL_GPUTexture *hdr_target,
    Uint32 hdr_width,
    Uint32 hdr_height,
    SDL_GPUGraphicsPipeline *downsample_pipeline,
    SDL_GPUGraphicsPipeline *upsample_pipeline,
    SDL_GPUSampler *bloom_sampler,
    float threshold,
    Uint32 vertex_count);
bool ForgeGpuRunHdrBloomTonemapPass(
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPUTexture *swapchain_texture,
    SDL_GPUTexture *hdr_target,
    SDL_GPUSampler *hdr_sampler,
    const ForgeGpuBloomChain *chain,
    SDL_GPUSampler *bloom_sampler,
    SDL_GPUGraphicsPipeline *tonemap_pipeline,
    const void *fragment_uniform_data,
    Uint32 fragment_uniform_size,
    Uint32 vertex_count);
void ForgeGpuComputeCascadeSplits(
    float near_plane,
    float far_plane,
    float splits[FORGE_GPU_SHADOW_CASCADE_COUNT]);
Mat4 ForgeGpuComputeCascadeLightViewProjection(
    Mat4 inv_cam_vp,
    float split_near,
    float split_far,
    float cam_near,
    float cam_far,
    Vec3 light_dir);
Mat4 ForgeGpuComputeTargetedDirectionalLightViewProjection(
    Vec3 light_dir,
    float light_distance,
    Vec3 target,
    float ortho_size,
    float near_plane,
    float far_plane,
    float parallel_threshold);
void ForgeGpuComputeCascadeLightViewProjections(
    Mat4 inv_cam_vp,
    float cam_near,
    float cam_far,
    Vec3 light_dir,
    float splits[FORGE_GPU_SHADOW_CASCADE_COUNT],
    Mat4 light_vp[FORGE_GPU_SHADOW_CASCADE_COUNT]);

bool ForgeGpuCreateGridBuffers(ForgeGpuDemo *demo);
void ForgeGpuFillMeshVertexInput(
    SDL_GPUVertexBufferDescription *vertex_buffer,
    SDL_GPUVertexAttribute attributes[3]);
bool ForgeGpuCreateSphereMeshBuffers(
    ForgeGpuDemo *demo,
    float radius,
    int stacks,
    int slices,
    SDL_GPUBuffer **vertex_buffer,
    SDL_GPUBuffer **index_buffer,
    Uint32 *index_count);
bool ForgeGpuLoadLessonScene(ForgeGpuDemo *demo, const char *relative_path);
bool ForgeGpuLoadLessonSceneWithRequirements(
    ForgeGpuDemo *demo,
    const char *relative_path,
    const ForgeGpuSceneLoadRequirements *requirements);
bool ForgeGpuLoadSceneModel(ForgeGpuDemo *demo, GpuSceneData *model, const char *relative_path);
bool ForgeGpuLoadSceneModelWithRequirements(
    ForgeGpuDemo *demo,
    GpuSceneData *model,
    const char *relative_path,
    const ForgeGpuSceneLoadRequirements *requirements);
const GpuMaterial *ForgeGpuModelMaterialOrDefault(const GpuSceneData *model, int material_index, GpuMaterial *fallback);
const GpuMaterial *ForgeGpuSceneMaterialOrDefault(const LessonState *lesson, int material_index, GpuMaterial *fallback);

void ForgeGpuFillGridFragmentUniforms(GridFragUniforms *uniforms, const Vec3 *light_dir, const Vec3 *eye_pos);
void ForgeGpuDrawBasicGrid(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    SDL_GPUGraphicsPipeline *pipeline,
    Mat4 vp,
    const Vec3 *light_dir);
void ForgeGpuRenderLoadedScene(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    Uint32 width,
    Uint32 height,
    SDL_GPUGraphicsPipeline *pipeline,
    bool lighting);

#endif /* SDLGPU_FORGE_GPU_LESSON_COMMON_H */
