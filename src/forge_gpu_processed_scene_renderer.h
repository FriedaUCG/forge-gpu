#ifndef SDLGPU_FORGE_GPU_PROCESSED_SCENE_RENDERER_H
#define SDLGPU_FORGE_GPU_PROCESSED_SCENE_RENDERER_H

#include "forge_gpu_internal.h"
#include "forge_gpu_math.h"
#include "forge_gpu_processed_assets.h"

#define FORGE_GPU_PROCESSED_SCENE_DEPTH_FORMAT SDL_GPU_TEXTUREFORMAT_D32_FLOAT
#define FORGE_GPU_PROCESSED_SCENE_SHADOW_MAP_SIZE 2048u
#define FORGE_GPU_PROCESSED_SCENE_MAX_TEXTURE_CACHE 96
#define FORGE_GPU_PROCESSED_SCENE_MAX_SKIN_JOINTS 256u
#define FORGE_GPU_PROCESSED_SCENE_MORPH_DELTA_STRIDE 16u

typedef struct ForgeGpuProcessedSceneMaterialTextures
{
    SDL_GPUTexture *base_color;
    SDL_GPUTexture *normal;
    SDL_GPUTexture *metallic_roughness;
    SDL_GPUTexture *roughness;
    SDL_GPUTexture *metallic;
    SDL_GPUTexture *occlusion;
    SDL_GPUTexture *emissive;
    bool uses_separate_metallic_roughness;
} ForgeGpuProcessedSceneMaterialTextures;

typedef struct ForgeGpuProcessedSceneVramStats
{
    Uint64 compressed_bytes;
    Uint64 uncompressed_bytes;
    Uint32 compressed_texture_count;
    Uint32 total_texture_count;
} ForgeGpuProcessedSceneVramStats;

typedef struct ForgeGpuProcessedSceneTextureCacheEntry
{
    char source_path[FORGE_GPU_PROCESSED_MATERIAL_PATH_SIZE];
    SDL_GPUTextureFormat format;
    SDL_GPUTexture *texture;
    bool compressed;
    Uint64 compressed_bytes;
    Uint64 uncompressed_bytes;
} ForgeGpuProcessedSceneTextureCacheEntry;

typedef struct ForgeGpuProcessedSceneModel
{
    ForgeGpuProcessedMesh mesh;
    ForgeGpuProcessedMaterialSet materials;
    ForgeGpuProcessedScene scene;
    SDL_GPUBuffer *vertex_buffer;
    SDL_GPUBuffer *index_buffer;
    ForgeGpuProcessedSceneMaterialTextures *material_textures;
    Vec3 *submesh_centroids;
    Uint32 material_texture_count;
    ForgeGpuProcessedSceneTextureCacheEntry texture_cache[FORGE_GPU_PROCESSED_SCENE_MAX_TEXTURE_CACHE];
    Uint32 texture_cache_count;
    Uint32 draw_calls;
    Uint32 shadow_draw_calls;
    Uint32 transparent_draw_calls;
    ForgeGpuProcessedSceneVramStats vram;
} ForgeGpuProcessedSceneModel;

typedef struct ForgeGpuProcessedSceneSkinnedModel
{
    ForgeGpuProcessedSceneModel model;
    ForgeGpuProcessedSkinSet skins;
    ForgeGpuProcessedAnimation animation;
    SDL_GPUBuffer *joint_buffer;
    SDL_GPUTransferBuffer *joint_transfer_buffer;
    Mat4 joint_matrices[FORGE_GPU_PROCESSED_SCENE_MAX_SKIN_JOINTS];
    Sint32 skinned_mesh_node;
    Sint32 current_clip;
    Uint32 active_joint_count;
    Uint32 pending_joint_upload_size;
    float anim_time;
    float anim_speed;
    bool looping;
} ForgeGpuProcessedSceneSkinnedModel;

typedef struct ForgeGpuProcessedSceneMorphModel
{
    ForgeGpuProcessedSceneModel model;
    ForgeGpuProcessedAnimation animation;
    SDL_GPUBuffer *morph_pos_buffer;
    SDL_GPUBuffer *morph_nrm_buffer;
    SDL_GPUTransferBuffer *morph_transfer_buffer;
    float *blended_pos_deltas;
    float *blended_nrm_deltas;
    float morph_weights[FORGE_GPU_PROCESSED_MORPH_MAX_TARGETS];
    Sint32 morph_mesh_node;
    Sint32 current_clip;
    Uint32 morph_target_count;
    Uint32 pending_morph_delta_upload_size;
    float anim_time;
    float anim_speed;
    bool looping;
    bool manual_weights;
} ForgeGpuProcessedSceneMorphModel;

typedef struct ForgeGpuProcessedSceneRenderer
{
    SDL_GPUGraphicsPipeline *model_pipeline;
    SDL_GPUGraphicsPipeline *model_blend_pipeline;
    SDL_GPUGraphicsPipeline *model_double_pipeline;
    SDL_GPUGraphicsPipeline *model_blend_double_pipeline;
    SDL_GPUGraphicsPipeline *skinned_model_pipeline;
    SDL_GPUGraphicsPipeline *skinned_model_blend_pipeline;
    SDL_GPUGraphicsPipeline *skinned_model_double_pipeline;
    SDL_GPUGraphicsPipeline *skinned_model_blend_double_pipeline;
    SDL_GPUGraphicsPipeline *morph_model_pipeline;
    SDL_GPUGraphicsPipeline *morph_model_blend_pipeline;
    SDL_GPUGraphicsPipeline *morph_model_double_pipeline;
    SDL_GPUGraphicsPipeline *morph_model_blend_double_pipeline;
    SDL_GPUGraphicsPipeline *shadow_pipeline;
    SDL_GPUGraphicsPipeline *skinned_shadow_pipeline;
    SDL_GPUGraphicsPipeline *morph_shadow_pipeline;
    SDL_GPUGraphicsPipeline *grid_pipeline;
    SDL_GPUGraphicsPipeline *sky_pipeline;
    SDL_GPUTexture *shadow_depth;
    SDL_GPUTexture *main_depth;
    Uint32 main_depth_width;
    Uint32 main_depth_height;
    SDL_GPUTexture *white_texture;
    SDL_GPUTexture *black_texture;
    SDL_GPUTexture *flat_normal_texture;
    SDL_GPUSampler *material_sampler;
    SDL_GPUSampler *normal_sampler;
    SDL_GPUSampler *model_shadow_sampler;
    SDL_GPUSampler *grid_shadow_sampler;
    SDL_GPUBuffer *grid_vertex_buffer;
    SDL_GPUBuffer *grid_index_buffer;
    bool transparency_sorting;
    bool shadow_pass_rendered;
    bool main_pass_rendered;
} ForgeGpuProcessedSceneRenderer;

typedef SDL_GPUTexture *(*ForgeGpuProcessedSceneTextureLoadFn)(
    ForgeGpuDemo *demo,
    ForgeGpuProcessedSceneRenderer *renderer,
    ForgeGpuProcessedSceneModel *model,
    const char *base_relative,
    const char *source_path,
    SDL_GPUTextureFormat requested_format,
    bool srgb,
    bool normal_map,
    void *userdata);

bool ForgeGpuProcessedSceneRendererCreate(ForgeGpuDemo *demo, ForgeGpuProcessedSceneRenderer *renderer);
void ForgeGpuProcessedSceneRendererDestroy(ForgeGpuDemo *demo, ForgeGpuProcessedSceneRenderer *renderer);
bool ForgeGpuProcessedSceneRendererEnsureMainDepth(
    ForgeGpuDemo *demo,
    ForgeGpuProcessedSceneRenderer *renderer,
    Uint32 width,
    Uint32 height);
void ForgeGpuProcessedSceneRendererBeginFrame(ForgeGpuProcessedSceneRenderer *renderer);
Vec3 ForgeGpuProcessedSceneLightDir(void);
Mat4 ForgeGpuProcessedSceneLightViewProjection(void);
SDL_GPURenderPass *ForgeGpuProcessedSceneBeginShadowPass(
    SDL_GPUCommandBuffer *command_buffer,
    ForgeGpuProcessedSceneRenderer *renderer);
SDL_GPURenderPass *ForgeGpuProcessedSceneBeginMainPass(
    SDL_GPUCommandBuffer *command_buffer,
    ForgeGpuProcessedSceneRenderer *renderer,
    SDL_GPUTexture *swapchain_texture);
void ForgeGpuProcessedSceneDrawGrid(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    ForgeGpuProcessedSceneRenderer *renderer,
    Mat4 camera_vp,
    Mat4 light_vp);
SDL_GPUGraphicsPipeline *ForgeGpuProcessedSceneCreateModelPipeline(
    ForgeGpuDemo *demo,
    SDL_GPUShader *vertex_shader,
    SDL_GPUShader *fragment_shader);
bool ForgeGpuProcessedSceneDrawModel(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    ForgeGpuProcessedSceneRenderer *renderer,
    ForgeGpuProcessedSceneModel *model,
    Mat4 placement,
    Mat4 camera_vp,
    Mat4 light_vp,
    bool shadow_pass);
bool ForgeGpuProcessedSceneDrawModelWithPipeline(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    ForgeGpuProcessedSceneRenderer *renderer,
    ForgeGpuProcessedSceneModel *model,
    SDL_GPUGraphicsPipeline *model_pipeline,
    Mat4 placement,
    Mat4 camera_vp,
    Mat4 light_vp);
bool ForgeGpuProcessedSceneLoadPbrMaterialTextures(
    ForgeGpuDemo *demo,
    ForgeGpuProcessedSceneRenderer *renderer,
    ForgeGpuProcessedSceneModel *model,
    const char *base_relative,
    const ForgeGpuProcessedMaterial *material,
    ForgeGpuProcessedSceneMaterialTextures *textures);
bool ForgeGpuProcessedSceneDrawModelWithPbrMaterial(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    ForgeGpuProcessedSceneRenderer *renderer,
    ForgeGpuProcessedSceneModel *model,
    SDL_GPUGraphicsPipeline *model_pipeline,
    Mat4 placement,
    Mat4 camera_vp,
    Mat4 light_vp,
    const ForgeGpuProcessedMaterial *material,
    const ForgeGpuProcessedSceneMaterialTextures *textures,
    const float *emissive_factor_override);
bool ForgeGpuProcessedSceneDrawSkinnedModel(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    ForgeGpuProcessedSceneRenderer *renderer,
    ForgeGpuProcessedSceneSkinnedModel *model,
    Mat4 placement,
    Mat4 camera_vp,
    Mat4 light_vp,
    bool shadow_pass);
bool ForgeGpuProcessedSceneLoadModel(
    ForgeGpuDemo *demo,
    ForgeGpuProcessedSceneRenderer *renderer,
    ForgeGpuProcessedSceneModel *model,
    const char *base_relative,
    const char *stem,
    ForgeGpuProcessedSceneTextureLoadFn texture_loader,
    void *texture_loader_userdata);
bool ForgeGpuProcessedSceneLoadSkinnedModel(
    ForgeGpuDemo *demo,
    ForgeGpuProcessedSceneRenderer *renderer,
    ForgeGpuProcessedSceneSkinnedModel *model,
    const char *base_relative,
    const char *stem,
    ForgeGpuProcessedSceneTextureLoadFn texture_loader,
    void *texture_loader_userdata);
bool ForgeGpuProcessedSceneLoadMorphModel(
    ForgeGpuDemo *demo,
    ForgeGpuProcessedSceneRenderer *renderer,
    ForgeGpuProcessedSceneMorphModel *model,
    const char *base_relative,
    const char *stem,
    bool has_materials,
    ForgeGpuProcessedSceneTextureLoadFn texture_loader,
    void *texture_loader_userdata);
void ForgeGpuProcessedSceneDestroyModel(SDL_GPUDevice *device, ForgeGpuProcessedSceneModel *model);
void ForgeGpuProcessedSceneDestroySkinnedModel(SDL_GPUDevice *device, ForgeGpuProcessedSceneSkinnedModel *model);
void ForgeGpuProcessedSceneDestroyMorphModel(SDL_GPUDevice *device, ForgeGpuProcessedSceneMorphModel *model);
void ForgeGpuProcessedSceneResetModelDrawCounts(ForgeGpuProcessedSceneModel *model);
void ForgeGpuProcessedSceneResetSkinnedModelDrawCounts(ForgeGpuProcessedSceneSkinnedModel *model);
void ForgeGpuProcessedSceneResetMorphModelDrawCounts(ForgeGpuProcessedSceneMorphModel *model);
bool ForgeGpuProcessedSceneUpdateSkinnedAnimation(
    ForgeGpuDemo *demo,
    ForgeGpuProcessedSceneSkinnedModel *model,
    float delta_time_seconds);
bool ForgeGpuProcessedSceneUploadSkinnedJoints(
    SDL_GPUCopyPass *copy_pass,
    ForgeGpuProcessedSceneSkinnedModel *model);
bool ForgeGpuProcessedSceneUpdateMorphAnimation(
    ForgeGpuDemo *demo,
    ForgeGpuProcessedSceneMorphModel *model,
    float delta_time_seconds);
bool ForgeGpuProcessedSceneUploadMorphDeltas(
    SDL_GPUCopyPass *copy_pass,
    ForgeGpuProcessedSceneMorphModel *model);
bool ForgeGpuProcessedSceneDrawMorphModel(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    ForgeGpuProcessedSceneRenderer *renderer,
    ForgeGpuProcessedSceneMorphModel *model,
    Mat4 placement,
    Mat4 camera_vp,
    Mat4 light_vp,
    bool shadow_pass);
bool ForgeGpuProcessedSceneJoinModelPath(
    ForgeGpuDemo *demo,
    const char *base_relative,
    const char *file,
    char *path,
    size_t path_size);
SDL_GPUTexture *ForgeGpuProcessedSceneFindCachedTexture(
    ForgeGpuProcessedSceneModel *model,
    const char *source_path,
    SDL_GPUTextureFormat format);
bool ForgeGpuProcessedSceneCacheTexture(
    ForgeGpuProcessedSceneModel *model,
    const char *source_path,
    SDL_GPUTextureFormat format,
    SDL_GPUTexture *texture,
    bool compressed,
    Uint64 compressed_bytes,
    Uint64 uncompressed_bytes);
SDL_GPUTexture *ForgeGpuProcessedSceneLoadRgbaMaterialTexture(
    ForgeGpuDemo *demo,
    ForgeGpuProcessedSceneRenderer *renderer,
    ForgeGpuProcessedSceneModel *model,
    const char *base_relative,
    const char *source_path,
    SDL_GPUTextureFormat requested_format,
    bool srgb,
    bool normal_map,
    void *userdata);

#endif /* SDLGPU_FORGE_GPU_PROCESSED_SCENE_RENDERER_H */
