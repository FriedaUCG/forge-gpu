#include "forge_gpu_processed_scene_renderer.h"

#include "forge_gpu_forward_scene.h"
#include "forge_gpu_gpu_helpers.h"
#include "forge_gpu_lesson_common.h"
#include "forge_gpu_shader_layouts.h"
#include "shaders/generated/forge_gpu_shared_scene_shaders.h"

#include <stddef.h>

#define PROCESSED_SCENE_SHADOW_ORTHO_SIZE 15.0f
#define PROCESSED_SCENE_SHADOW_HEIGHT 20.0f
#define PROCESSED_SCENE_SHADOW_NEAR 0.1f
#define PROCESSED_SCENE_SHADOW_FAR 50.0f
#define PROCESSED_SCENE_SHADOW_BIAS_CONST 2.0f
#define PROCESSED_SCENE_SHADOW_BIAS_SLOPE 2.0f
#define PROCESSED_SCENE_GRID_HALF_SIZE 20.0f
#define PROCESSED_SCENE_GRID_SPACING 1.0f
#define PROCESSED_SCENE_GRID_LINE_WIDTH 0.02f
#define PROCESSED_SCENE_GRID_FADE_DISTANCE 30.0f
#define PROCESSED_SCENE_AMBIENT 0.15f
#define PROCESSED_SCENE_SHININESS 32.0f
#define PROCESSED_SCENE_SPECULAR_STRENGTH 0.5f
#define PROCESSED_SCENE_LIGHT_INTENSITY 1.2f
#define PROCESSED_SCENE_JOINT_BUFFER_SIZE (FORGE_GPU_PROCESSED_SCENE_MAX_SKIN_JOINTS * (Uint32)sizeof(Mat4))
#define PROCESSED_SCENE_MORPH_WEIGHT_EPSILON 0.001f

typedef struct ForgeGpuProcessedSceneModelVertUniforms
{
    Mat4 mvp;
    Mat4 model;
    Mat4 light_vp;
} ForgeGpuProcessedSceneModelVertUniforms;

typedef struct ForgeGpuProcessedSceneModelFragUniforms
{
    float light_dir[4];
    float eye_pos[4];
    float base_color_factor[4];
    float emissive_factor[3];
    float shadow_texel;
    float metallic_factor;
    float roughness_factor;
    float normal_scale;
    float occlusion_strength;
    float shininess;
    float specular_strength;
    float alpha_cutoff;
    float ambient;
} ForgeGpuProcessedSceneModelFragUniforms;

typedef struct ForgeGpuProcessedSceneShadowUniforms
{
    Mat4 light_vp;
} ForgeGpuProcessedSceneShadowUniforms;

typedef struct ForgeGpuProcessedSceneTransparentDraw
{
    ForgeGpuProcessedSceneModel *model;
    Mat4 final_world;
    Uint32 node_index;
    Uint32 submesh_index;
    float sort_depth;
} ForgeGpuProcessedSceneTransparentDraw;

typedef struct ForgeGpuProcessedScenePipelineSet
{
    SDL_GPUGraphicsPipeline *model;
    SDL_GPUGraphicsPipeline *blend;
    SDL_GPUGraphicsPipeline *double_sided;
    SDL_GPUGraphicsPipeline *blend_double_sided;
    SDL_GPUGraphicsPipeline *shadow;
} ForgeGpuProcessedScenePipelineSet;

static const float kProcessedSceneGridLineColor[4] = { 0.4f, 0.4f, 0.5f, 1.0f };
static const float kProcessedSceneGridBgColor[4] = { 0.08f, 0.08f, 0.1f, 1.0f };

static_assert(sizeof(ForgeGpuProcessedSceneModelVertUniforms) == 192, "processed scene model vertex uniform size must match HLSL layout");
static_assert(sizeof(ForgeGpuProcessedSceneModelFragUniforms) == 96, "processed scene model fragment uniform size must match HLSL layout");
static_assert(sizeof(ForgeGpuProcessedSceneShadowUniforms) == 64, "processed scene shadow uniform size must match HLSL layout");

static bool ensure_skinned_pipelines(ForgeGpuDemo *demo, ForgeGpuProcessedSceneRenderer *renderer);
static bool ensure_morph_pipelines(ForgeGpuDemo *demo, ForgeGpuProcessedSceneRenderer *renderer);

static bool count_scene_submesh_draw_capacity(const ForgeGpuProcessedSceneModel *model, Uint32 *out_count)
{
    Uint64 total = 0;

    for (Uint32 node_index = 0; node_index < model->scene.node_count; node_index += 1) {
        const ForgeGpuProcessedSceneNode *node = &model->scene.nodes[node_index];

        if (node->mesh_index < 0 || (Uint32)node->mesh_index >= model->scene.mesh_count) {
            continue;
        }

        total += model->scene.meshes[node->mesh_index].submesh_count;
        if (total > SDL_MAX_UINT32) {
            SDL_SetError("processed scene model transparent draw capacity overflow");
            return false;
        }
    }

    *out_count = (Uint32)total;
    return true;
}

static int SDLCALL compare_transparent_draws(const void *a, const void *b)
{
    const ForgeGpuProcessedSceneTransparentDraw *left = (const ForgeGpuProcessedSceneTransparentDraw *)a;
    const ForgeGpuProcessedSceneTransparentDraw *right = (const ForgeGpuProcessedSceneTransparentDraw *)b;

    if (left->sort_depth < right->sort_depth) {
        return 1;
    }
    if (left->sort_depth > right->sort_depth) {
        return -1;
    }
    if (left->node_index < right->node_index) {
        return -1;
    }
    if (left->node_index > right->node_index) {
        return 1;
    }
    if (left->submesh_index < right->submesh_index) {
        return -1;
    }
    if (left->submesh_index > right->submesh_index) {
        return 1;
    }
    return 0;
}

Vec3 ForgeGpuProcessedSceneLightDir(void)
{
    return vec3_normalize({ 0.4f, 0.8f, 0.6f });
}

Mat4 ForgeGpuProcessedSceneLightViewProjection(void)
{
    const Vec3 light_dir = ForgeGpuProcessedSceneLightDir();
    const Vec3 light_pos = vec3_scale(light_dir, PROCESSED_SCENE_SHADOW_HEIGHT);
    Vec3 up = { 0.0f, 1.0f, 0.0f };

    if (SDL_fabsf(vec3_dot(light_dir, up)) > 0.999f) {
        up = { 1.0f, 0.0f, 0.0f };
    }
    return mat4_multiply(
        mat4_orthographic(
            -PROCESSED_SCENE_SHADOW_ORTHO_SIZE,
            PROCESSED_SCENE_SHADOW_ORTHO_SIZE,
            -PROCESSED_SCENE_SHADOW_ORTHO_SIZE,
            PROCESSED_SCENE_SHADOW_ORTHO_SIZE,
            PROCESSED_SCENE_SHADOW_NEAR,
            PROCESSED_SCENE_SHADOW_FAR),
        mat4_look_at(light_pos, { 0.0f, 0.0f, 0.0f }, up));
}

bool ForgeGpuProcessedSceneJoinModelPath(
    ForgeGpuDemo *demo,
    const char *base_relative,
    const char *file,
    char *path,
    size_t path_size)
{
    char relative[FORGE_GPU_MAX_PATH];
    int written = SDL_snprintf(relative, sizeof(relative), "%s/%s", base_relative, file);

    if (written <= 0 || (size_t)written >= sizeof(relative)) {
        SDL_SetError("processed scene asset path overflow");
        return false;
    }
    return ForgeGpuJoinAssetPath(demo, relative, path, path_size);
}

static Mat4 mat4_from_array(const float values[16])
{
    Mat4 result;

    SDL_memcpy(result.m, values, sizeof(result.m));
    return result;
}

SDL_GPUTexture *ForgeGpuProcessedSceneFindCachedTexture(
    ForgeGpuProcessedSceneModel *model,
    const char *source_path,
    SDL_GPUTextureFormat format)
{
    for (Uint32 i = 0; i < model->texture_cache_count; i += 1) {
        ForgeGpuProcessedSceneTextureCacheEntry *entry = &model->texture_cache[i];
        if (entry->format == format && SDL_strcmp(entry->source_path, source_path) == 0) {
            return entry->texture;
        }
    }
    return nullptr;
}

bool ForgeGpuProcessedSceneCacheTexture(
    ForgeGpuProcessedSceneModel *model,
    const char *source_path,
    SDL_GPUTextureFormat format,
    SDL_GPUTexture *texture,
    bool compressed,
    Uint64 compressed_bytes,
    Uint64 uncompressed_bytes)
{
    ForgeGpuProcessedSceneTextureCacheEntry *entry;

    if (model->texture_cache_count >= SDL_arraysize(model->texture_cache)) {
        SDL_SetError("processed scene texture cache capacity exceeded");
        return false;
    }
    entry = &model->texture_cache[model->texture_cache_count];
    SDL_strlcpy(entry->source_path, source_path, sizeof(entry->source_path));
    entry->format = format;
    entry->texture = texture;
    entry->compressed = compressed;
    entry->compressed_bytes = compressed_bytes;
    entry->uncompressed_bytes = uncompressed_bytes;
    model->texture_cache_count += 1;
    model->vram.total_texture_count += 1;
    if (compressed) {
        model->vram.compressed_texture_count += 1;
    }
    model->vram.compressed_bytes += compressed_bytes;
    model->vram.uncompressed_bytes += uncompressed_bytes;
    return true;
}

SDL_GPUTexture *ForgeGpuProcessedSceneLoadRgbaMaterialTexture(
    ForgeGpuDemo *demo,
    ForgeGpuProcessedSceneRenderer *renderer,
    ForgeGpuProcessedSceneModel *model,
    const char *base_relative,
    const char *source_path,
    SDL_GPUTextureFormat requested_format,
    bool srgb,
    bool normal_map,
    void *userdata)
{
    char path[FORGE_GPU_MAX_PATH];
    Uint32 width = 0;
    Uint32 height = 0;
    SDL_GPUTexture *texture;
    Uint64 estimated_bytes;

    (void)renderer;
    (void)srgb;
    (void)normal_map;
    (void)userdata;

    texture = ForgeGpuProcessedSceneFindCachedTexture(model, source_path, requested_format);
    if (texture) {
        return texture;
    }

    if (!ForgeGpuProcessedSceneJoinModelPath(demo, base_relative, source_path, path, sizeof(path)) ||
        !ForgeGpuValidateProcessedTextureSidecar(path, &width, &height)) {
        return nullptr;
    }

    texture = ForgeGpuLoadRgbaTexturePathWithFormatAndSize(demo, path, true, requested_format, width, height);
    if (!texture) {
        return nullptr;
    }

    estimated_bytes = ForgeGpuEstimateProcessedRgba8MipBytes(width, height);
    if (!ForgeGpuProcessedSceneCacheTexture(
            model,
            source_path,
            requested_format,
            texture,
            false,
            estimated_bytes,
            estimated_bytes)) {
        SDL_ReleaseGPUTexture(demo->device, texture);
        return nullptr;
    }
    return texture;
}

static bool load_material_textures_for_material(
    ForgeGpuDemo *demo,
    ForgeGpuProcessedSceneRenderer *renderer,
    ForgeGpuProcessedSceneModel *model,
    const char *base_relative,
    const ForgeGpuProcessedMaterial *material,
    ForgeGpuProcessedSceneMaterialTextures *textures,
    ForgeGpuProcessedSceneTextureLoadFn texture_loader,
    void *texture_loader_userdata,
    bool allow_separate_metallic_roughness)
{
    bool has_packed_metallic_roughness;
    bool has_separate_metallic_roughness;
    bool use_separate_metallic_roughness;

    if (!material || !textures) {
        SDL_InvalidParamError("ForgeGpuProcessedSceneLoadPbrMaterialTextures");
        return false;
    }
    if (!texture_loader) {
        texture_loader = ForgeGpuProcessedSceneLoadRgbaMaterialTexture;
    }
    if (material->alpha_mode == FORGE_GPU_PROCESSED_ALPHA_MASK) {
        SDL_SetError("processed scene renderer does not yet import the source alpha-mask shadow shader path");
        return false;
    }

    has_packed_metallic_roughness = material->metallic_roughness_texture[0] != '\0';
    has_separate_metallic_roughness =
        material->roughness_texture[0] != '\0' || material->metallic_texture[0] != '\0';
    if (has_packed_metallic_roughness && has_separate_metallic_roughness) {
        SDL_SetError("processed scene material must not mix packed and separate metallic-roughness textures");
        return false;
    }
    use_separate_metallic_roughness = has_separate_metallic_roughness;
    if (!allow_separate_metallic_roughness && has_separate_metallic_roughness) {
        SDL_SetError("processed scene renderer current shader path expects packed metallic-roughness textures");
        return false;
    }

    textures->base_color = renderer->white_texture;
    textures->normal = renderer->flat_normal_texture;
    textures->metallic_roughness = renderer->white_texture;
    textures->roughness = renderer->white_texture;
    textures->metallic = renderer->white_texture;
    textures->occlusion = renderer->white_texture;
    textures->emissive = renderer->black_texture;
    textures->uses_separate_metallic_roughness = use_separate_metallic_roughness;

    if (material->base_color_texture[0] != '\0') {
        textures->base_color = texture_loader(
            demo, renderer, model, base_relative, material->base_color_texture,
            SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB, true, false, texture_loader_userdata);
        if (!textures->base_color) {
            return false;
        }
    }
    if (material->normal_texture[0] != '\0') {
        textures->normal = texture_loader(
            demo, renderer, model, base_relative, material->normal_texture,
            SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, false, true, texture_loader_userdata);
        if (!textures->normal) {
            return false;
        }
    }
    if (material->metallic_roughness_texture[0] != '\0') {
        textures->metallic_roughness = texture_loader(
            demo, renderer, model, base_relative, material->metallic_roughness_texture,
            SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, false, false, texture_loader_userdata);
        if (!textures->metallic_roughness) {
            return false;
        }
    }
    if (use_separate_metallic_roughness && material->roughness_texture[0] != '\0') {
        textures->roughness = texture_loader(
            demo, renderer, model, base_relative, material->roughness_texture,
            SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, false, false, texture_loader_userdata);
        if (!textures->roughness) {
            return false;
        }
    }
    if (use_separate_metallic_roughness && material->metallic_texture[0] != '\0') {
        textures->metallic = texture_loader(
            demo, renderer, model, base_relative, material->metallic_texture,
            SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, false, false, texture_loader_userdata);
        if (!textures->metallic) {
            return false;
        }
    }
    if (material->occlusion_texture[0] != '\0') {
        textures->occlusion = texture_loader(
            demo, renderer, model, base_relative, material->occlusion_texture,
            SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, false, false, texture_loader_userdata);
        if (!textures->occlusion) {
            return false;
        }
    }
    if (material->emissive_texture[0] != '\0') {
        textures->emissive = texture_loader(
            demo, renderer, model, base_relative, material->emissive_texture,
            SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB, true, false, texture_loader_userdata);
        if (!textures->emissive) {
            return false;
        }
    }
    return true;
}

bool ForgeGpuProcessedSceneLoadPbrMaterialTextures(
    ForgeGpuDemo *demo,
    ForgeGpuProcessedSceneRenderer *renderer,
    ForgeGpuProcessedSceneModel *model,
    const char *base_relative,
    const ForgeGpuProcessedMaterial *material,
    ForgeGpuProcessedSceneMaterialTextures *textures)
{
    return load_material_textures_for_material(
        demo, renderer, model, base_relative, material, textures, nullptr, nullptr, true);
}

static bool load_material_textures(
    ForgeGpuDemo *demo,
    ForgeGpuProcessedSceneRenderer *renderer,
    ForgeGpuProcessedSceneModel *model,
    const char *base_relative,
    ForgeGpuProcessedSceneTextureLoadFn texture_loader,
    void *texture_loader_userdata)
{
    model->material_texture_count = model->materials.material_count;
    if (model->material_texture_count == 0) {
        return true;
    }

    model->material_textures = (ForgeGpuProcessedSceneMaterialTextures *)SDL_calloc(
        model->material_texture_count, sizeof(*model->material_textures));
    if (!model->material_textures) {
        SDL_OutOfMemory();
        return false;
    }

    for (Uint32 i = 0; i < model->material_texture_count; i += 1) {
        const ForgeGpuProcessedMaterial *material = &model->materials.materials[i];
        ForgeGpuProcessedSceneMaterialTextures *textures = &model->material_textures[i];

        if (!load_material_textures_for_material(
                demo, renderer, model, base_relative, material, textures,
                texture_loader, texture_loader_userdata, false)) {
            return false;
        }
    }
    return true;
}

static bool compute_submesh_centroids(ForgeGpuProcessedSceneModel *model)
{
    const Uint8 *vertices = (const Uint8 *)model->mesh.vertices;

    model->submesh_centroids = (Vec3 *)SDL_calloc(model->mesh.submesh_count, sizeof(*model->submesh_centroids));
    if (!model->submesh_centroids) {
        SDL_OutOfMemory();
        return false;
    }

    for (Uint32 submesh_index = 0; submesh_index < model->mesh.submesh_count; submesh_index += 1) {
        const ForgeGpuProcessedSubmesh *submesh = &model->mesh.submeshes[submesh_index];
        const Uint32 first_index = submesh->index_offset / sizeof(Uint32);
        Vec3 sum = { 0.0f, 0.0f, 0.0f };

        if (submesh->index_count == 0) {
            continue;
        }
        for (Uint32 i = 0; i < submesh->index_count; i += 1) {
            const Uint32 vertex_index = model->mesh.indices[first_index + i];
            float position[3];

            SDL_memcpy(position, vertices + (size_t)vertex_index * model->mesh.vertex_stride, sizeof(position));
            sum.x += position[0];
            sum.y += position[1];
            sum.z += position[2];
        }
        model->submesh_centroids[submesh_index] = vec3_scale(sum, 1.0f / (float)submesh->index_count);
    }
    return true;
}

bool ForgeGpuProcessedSceneLoadModel(
    ForgeGpuDemo *demo,
    ForgeGpuProcessedSceneRenderer *renderer,
    ForgeGpuProcessedSceneModel *model,
    const char *base_relative,
    const char *stem,
    ForgeGpuProcessedSceneTextureLoadFn texture_loader,
    void *texture_loader_userdata)
{
    char path[FORGE_GPU_MAX_PATH];
    char filename[128];
    size_t vertex_size;
    size_t index_size;

    SDL_zero(*model);

    if (SDL_snprintf(filename, sizeof(filename), "%s.fscene", stem) <= 0 ||
        !ForgeGpuProcessedSceneJoinModelPath(demo, base_relative, filename, path, sizeof(path)) ||
        !ForgeGpuLoadProcessedSceneV1(path, &model->scene)) {
        goto fail;
    }
    if (SDL_snprintf(filename, sizeof(filename), "%s.fmesh", stem) <= 0 ||
        !ForgeGpuProcessedSceneJoinModelPath(demo, base_relative, filename, path, sizeof(path)) ||
        !ForgeGpuLoadProcessedMesh(path, &model->mesh)) {
        goto fail;
    }
    if (!ForgeGpuProcessedMeshHasTangents(&model->mesh)) {
        SDL_SetError("processed scene mesh '%s' is missing tangent data", stem);
        goto fail;
    }
    if (SDL_snprintf(filename, sizeof(filename), "%s.fmat", stem) <= 0 ||
        !ForgeGpuProcessedSceneJoinModelPath(demo, base_relative, filename, path, sizeof(path)) ||
        !ForgeGpuLoadProcessedMaterials(path, &model->materials)) {
        goto fail;
    }
    if (!ForgeGpuValidateProcessedSceneModelReferences(&model->scene, &model->mesh, &model->materials, stem)) {
        goto fail;
    }

    vertex_size = (size_t)model->mesh.vertex_count * model->mesh.vertex_stride;
    index_size = (size_t)model->mesh.total_index_count * sizeof(Uint32);
    if (vertex_size > SDL_MAX_UINT32 || index_size > SDL_MAX_UINT32) {
        SDL_SetError("processed scene mesh '%s' is too large for demo upload", stem);
        goto fail;
    }

    model->vertex_buffer = ForgeGpuCreateBufferWithData(
        demo->device, SDL_GPU_BUFFERUSAGE_VERTEX, model->mesh.vertices, (Uint32)vertex_size);
    model->index_buffer = ForgeGpuCreateBufferWithData(
        demo->device, SDL_GPU_BUFFERUSAGE_INDEX, model->mesh.indices, (Uint32)index_size);
    if (!model->vertex_buffer || !model->index_buffer) {
        goto fail;
    }
    if (!compute_submesh_centroids(model) ||
        !load_material_textures(demo, renderer, model, base_relative, texture_loader, texture_loader_userdata)) {
        goto fail;
    }
    return true;

fail:
    ForgeGpuProcessedSceneDestroyModel(demo->device, model);
    return false;
}

static bool find_single_skinned_mesh_node(const ForgeGpuProcessedSceneSkinnedModel *model, Sint32 *out_node)
{
    Uint32 skinned_mesh_nodes = 0;
    Sint32 skinned_node = -1;

    for (Uint32 node_index = 0; node_index < model->model.scene.node_count; node_index += 1) {
        const ForgeGpuProcessedSceneNode *node = &model->model.scene.nodes[node_index];

        if (node->mesh_index < 0) {
            continue;
        }
        if (node->skin_index < 0) {
            SDL_SetError("processed skinned scene node %u has a mesh but no skin", (unsigned)node_index);
            return false;
        }
        if (node->skin_index != 0) {
            SDL_SetError("processed skinned scene node %u uses skin %d; the current single-palette path expects skin 0", (unsigned)node_index, (int)node->skin_index);
            return false;
        }
        skinned_node = (Sint32)node_index;
        skinned_mesh_nodes += 1;
    }

    if (skinned_mesh_nodes != 1) {
        SDL_SetError("processed skinned scene expected exactly one skinned mesh node, got %u", (unsigned)skinned_mesh_nodes);
        return false;
    }

    *out_node = skinned_node;
    return true;
}

bool ForgeGpuProcessedSceneLoadSkinnedModel(
    ForgeGpuDemo *demo,
    ForgeGpuProcessedSceneRenderer *renderer,
    ForgeGpuProcessedSceneSkinnedModel *model,
    const char *base_relative,
    const char *stem,
    ForgeGpuProcessedSceneTextureLoadFn texture_loader,
    void *texture_loader_userdata)
{
    SDL_GPUTransferBufferCreateInfo transfer_info;
    char path[FORGE_GPU_MAX_PATH];
    char filename[128];

    SDL_zero(*model);
    model->skinned_mesh_node = -1;
    model->current_clip = 0;
    model->anim_speed = 1.0f;
    model->looping = true;

    if (!ForgeGpuProcessedSceneLoadModel(
            demo, renderer, &model->model, base_relative, stem,
            texture_loader, texture_loader_userdata)) {
        return false;
    }
    if (model->model.mesh.vertex_stride != FORGE_GPU_PROCESSED_VERTEX_STRIDE_SKINNED_TANGENTS ||
        !ForgeGpuProcessedMeshIsSkinned(&model->model.mesh) ||
        !ForgeGpuProcessedMeshHasTangents(&model->model.mesh)) {
        SDL_SetError("processed scene model '%s' is not a skinned+tangent 72-byte mesh", stem);
        goto fail;
    }
    if (!ensure_skinned_pipelines(demo, renderer)) {
        goto fail;
    }

    if (SDL_snprintf(filename, sizeof(filename), "%s.fskin", stem) <= 0 ||
        !ForgeGpuProcessedSceneJoinModelPath(demo, base_relative, filename, path, sizeof(path)) ||
        !ForgeGpuLoadProcessedSkins(path, &model->skins) ||
        !ForgeGpuValidateProcessedSceneSkinReferences(&model->model.scene, &model->model.mesh, &model->skins, stem)) {
        goto fail;
    }
    if (model->skins.skin_count != 1) {
        SDL_SetError("processed scene skinned model '%s' expected exactly one skin, got %u", stem, (unsigned)model->skins.skin_count);
        goto fail;
    }
    if (!find_single_skinned_mesh_node(model, &model->skinned_mesh_node)) {
        goto fail;
    }

    if (SDL_snprintf(filename, sizeof(filename), "%s.fanim", stem) <= 0 ||
        !ForgeGpuProcessedSceneJoinModelPath(demo, base_relative, filename, path, sizeof(path)) ||
        !ForgeGpuLoadProcessedAnimation(path, &model->animation) ||
        !ForgeGpuValidateProcessedSceneAnimationReferences(&model->model.scene, &model->animation, stem)) {
        goto fail;
    }

    for (Uint32 i = 0; i < SDL_arraysize(model->joint_matrices); i += 1) {
        model->joint_matrices[i] = mat4_identity();
    }
    model->joint_buffer = ForgeGpuCreateBufferWithData(
        demo->device,
        SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ,
        model->joint_matrices,
        PROCESSED_SCENE_JOINT_BUFFER_SIZE);
    if (!model->joint_buffer) {
        goto fail;
    }

    SDL_zero(transfer_info);
    transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transfer_info.size = PROCESSED_SCENE_JOINT_BUFFER_SIZE;
    model->joint_transfer_buffer = SDL_CreateGPUTransferBuffer(demo->device, &transfer_info);
    if (!model->joint_transfer_buffer) {
        goto fail;
    }
    return true;

fail:
    ForgeGpuProcessedSceneDestroySkinnedModel(demo->device, model);
    return false;
}

static bool find_single_morph_mesh_node(const ForgeGpuProcessedSceneMorphModel *model, Sint32 *out_node)
{
    Uint32 morph_mesh_nodes = 0;
    Sint32 morph_node = -1;

    for (Uint32 node_index = 0; node_index < model->model.scene.node_count; node_index += 1) {
        const ForgeGpuProcessedSceneNode *node = &model->model.scene.nodes[node_index];

        if (node->mesh_index < 0) {
            continue;
        }
        if (node->skin_index >= 0) {
            SDL_SetError("processed morph scene node %u has both mesh and skin data", (unsigned)node_index);
            return false;
        }
        morph_node = (Sint32)node_index;
        morph_mesh_nodes += 1;
    }

    if (morph_mesh_nodes != 1) {
        SDL_SetError("processed morph scene expected exactly one mesh node, got %u", (unsigned)morph_mesh_nodes);
        return false;
    }

    *out_node = morph_node;
    return true;
}

bool ForgeGpuProcessedSceneLoadMorphModel(
    ForgeGpuDemo *demo,
    ForgeGpuProcessedSceneRenderer *renderer,
    ForgeGpuProcessedSceneMorphModel *model,
    const char *base_relative,
    const char *stem,
    bool has_materials,
    ForgeGpuProcessedSceneTextureLoadFn texture_loader,
    void *texture_loader_userdata)
{
    SDL_GPUTransferBufferCreateInfo transfer_info;
    char path[FORGE_GPU_MAX_PATH];
    char filename[128];
    size_t vertex_size;
    size_t index_size;
    Uint64 delta_size64;
    Uint32 delta_size;

    SDL_zero(*model);
    model->morph_mesh_node = -1;
    model->current_clip = 0;
    model->anim_speed = 1.0f;
    model->looping = true;

    if (SDL_snprintf(filename, sizeof(filename), "%s.fscene", stem) <= 0 ||
        !ForgeGpuProcessedSceneJoinModelPath(demo, base_relative, filename, path, sizeof(path)) ||
        !ForgeGpuLoadProcessedSceneV1(path, &model->model.scene)) {
        goto fail;
    }
    if (SDL_snprintf(filename, sizeof(filename), "%s.fmesh", stem) <= 0 ||
        !ForgeGpuProcessedSceneJoinModelPath(demo, base_relative, filename, path, sizeof(path)) ||
        !ForgeGpuLoadProcessedMesh(path, &model->model.mesh)) {
        goto fail;
    }
    if (model->model.mesh.vertex_stride != FORGE_GPU_PROCESSED_VERTEX_STRIDE_TANGENTS ||
        !ForgeGpuProcessedMeshHasTangents(&model->model.mesh) ||
        !ForgeGpuProcessedMeshHasMorphs(&model->model.mesh) ||
        ForgeGpuProcessedMeshIsSkinned(&model->model.mesh)) {
        SDL_SetError("processed scene model '%s' is not a pure morph+tangent 48-byte mesh", stem);
        goto fail;
    }
    if (!ensure_morph_pipelines(demo, renderer)) {
        goto fail;
    }

    if (has_materials) {
        if (SDL_snprintf(filename, sizeof(filename), "%s.fmat", stem) <= 0 ||
            !ForgeGpuProcessedSceneJoinModelPath(demo, base_relative, filename, path, sizeof(path)) ||
            !ForgeGpuLoadProcessedMaterials(path, &model->model.materials)) {
            goto fail;
        }
    }
    if (!ForgeGpuValidateProcessedSceneModelReferences(&model->model.scene, &model->model.mesh, &model->model.materials, stem)) {
        goto fail;
    }
    if (!find_single_morph_mesh_node(model, &model->morph_mesh_node)) {
        goto fail;
    }

    if (SDL_snprintf(filename, sizeof(filename), "%s.fanim", stem) <= 0 ||
        !ForgeGpuProcessedSceneJoinModelPath(demo, base_relative, filename, path, sizeof(path)) ||
        !ForgeGpuLoadProcessedAnimation(path, &model->animation) ||
        !ForgeGpuValidateProcessedSceneAnimationReferences(&model->model.scene, &model->animation, stem)) {
        goto fail;
    }

    model->morph_target_count = model->model.mesh.morph_target_count;
    for (Uint32 i = 0; i < model->morph_target_count; i += 1) {
        model->morph_weights[i] = model->model.mesh.morph_targets[i].default_weight;
    }

    vertex_size = (size_t)model->model.mesh.vertex_count * model->model.mesh.vertex_stride;
    index_size = (size_t)model->model.mesh.total_index_count * sizeof(Uint32);
    delta_size64 =
        (Uint64)model->model.mesh.vertex_count *
        (Uint64)FORGE_GPU_PROCESSED_SCENE_MORPH_DELTA_STRIDE;
    if (vertex_size > SDL_MAX_UINT32 ||
        index_size > SDL_MAX_UINT32 ||
        delta_size64 > SDL_MAX_UINT32 ||
        delta_size64 > SDL_MAX_UINT32 / 2u) {
        SDL_SetError("processed scene morph model '%s' is too large for demo upload", stem);
        goto fail;
    }
    delta_size = (Uint32)delta_size64;

    model->model.vertex_buffer = ForgeGpuCreateBufferWithData(
        demo->device, SDL_GPU_BUFFERUSAGE_VERTEX, model->model.mesh.vertices, (Uint32)vertex_size);
    model->model.index_buffer = ForgeGpuCreateBufferWithData(
        demo->device, SDL_GPU_BUFFERUSAGE_INDEX, model->model.mesh.indices, (Uint32)index_size);
    if (!model->model.vertex_buffer || !model->model.index_buffer) {
        goto fail;
    }
    if (!compute_submesh_centroids(&model->model) ||
        !load_material_textures(demo, renderer, &model->model, base_relative, texture_loader, texture_loader_userdata)) {
        goto fail;
    }

    model->blended_pos_deltas = (float *)SDL_calloc(1, delta_size);
    model->blended_nrm_deltas = (float *)SDL_calloc(1, delta_size);
    if (!model->blended_pos_deltas || !model->blended_nrm_deltas) {
        SDL_OutOfMemory();
        goto fail;
    }
    model->morph_pos_buffer = ForgeGpuCreateBufferWithData(
        demo->device,
        SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ,
        model->blended_pos_deltas,
        delta_size);
    model->morph_nrm_buffer = ForgeGpuCreateBufferWithData(
        demo->device,
        SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ,
        model->blended_nrm_deltas,
        delta_size);
    if (!model->morph_pos_buffer || !model->morph_nrm_buffer) {
        goto fail;
    }

    SDL_zero(transfer_info);
    transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transfer_info.size = delta_size * 2u;
    model->morph_transfer_buffer = SDL_CreateGPUTransferBuffer(demo->device, &transfer_info);
    if (!model->morph_transfer_buffer) {
        goto fail;
    }

    return true;

fail:
    ForgeGpuProcessedSceneDestroyMorphModel(demo->device, model);
    return false;
}

static void release_shader(SDL_GPUDevice *device, SDL_GPUShader **shader)
{
    if (*shader) {
        SDL_ReleaseGPUShader(device, *shader);
        *shader = nullptr;
    }
}

static void fill_model_vertex_input(
    SDL_GPUVertexBufferDescription *vertex_buffer,
    SDL_GPUVertexAttribute attributes[4])
{
    SDL_zero(*vertex_buffer);
    vertex_buffer->slot = 0;
    vertex_buffer->pitch = FORGE_GPU_PROCESSED_VERTEX_STRIDE_TANGENTS;
    vertex_buffer->input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

    SDL_memset(attributes, 0, 4 * sizeof(*attributes));
    attributes[0].location = 0;
    attributes[0].buffer_slot = 0;
    attributes[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    attributes[0].offset = 0;
    attributes[1].location = 1;
    attributes[1].buffer_slot = 0;
    attributes[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    attributes[1].offset = 12;
    attributes[2].location = 2;
    attributes[2].buffer_slot = 0;
    attributes[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    attributes[2].offset = 24;
    attributes[3].location = 3;
    attributes[3].buffer_slot = 0;
    attributes[3].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
    attributes[3].offset = 32;
}

static void fill_skinned_model_vertex_input(
    SDL_GPUVertexBufferDescription *vertex_buffer,
    SDL_GPUVertexAttribute attributes[6])
{
    SDL_zero(*vertex_buffer);
    vertex_buffer->slot = 0;
    vertex_buffer->pitch = FORGE_GPU_PROCESSED_VERTEX_STRIDE_SKINNED_TANGENTS;
    vertex_buffer->input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

    SDL_memset(attributes, 0, 6 * sizeof(*attributes));
    attributes[0].location = 0;
    attributes[0].buffer_slot = 0;
    attributes[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    attributes[0].offset = 0;
    attributes[1].location = 1;
    attributes[1].buffer_slot = 0;
    attributes[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    attributes[1].offset = 12;
    attributes[2].location = 2;
    attributes[2].buffer_slot = 0;
    attributes[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    attributes[2].offset = 24;
    attributes[3].location = 3;
    attributes[3].buffer_slot = 0;
    attributes[3].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
    attributes[3].offset = 32;
    attributes[4].location = 4;
    attributes[4].buffer_slot = 0;
    attributes[4].format = SDL_GPU_VERTEXELEMENTFORMAT_USHORT4;
    attributes[4].offset = 48;
    attributes[5].location = 5;
    attributes[5].buffer_slot = 0;
    attributes[5].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
    attributes[5].offset = 56;
}

static void fill_grid_vertex_input(
    SDL_GPUVertexBufferDescription *vertex_buffer,
    SDL_GPUVertexAttribute *attribute)
{
    SDL_zero(*vertex_buffer);
    vertex_buffer->slot = 0;
    vertex_buffer->pitch = sizeof(GridVertex);
    vertex_buffer->input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

    SDL_zero(*attribute);
    attribute->location = 0;
    attribute->buffer_slot = 0;
    attribute->format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    attribute->offset = 0;
}

static bool create_model_shadow_sampler(ForgeGpuDemo *demo, ForgeGpuProcessedSceneRenderer *renderer)
{
    SDL_GPUSamplerCreateInfo sampler_info;

    SDL_zero(sampler_info);
    sampler_info.min_filter = SDL_GPU_FILTER_LINEAR;
    sampler_info.mag_filter = SDL_GPU_FILTER_LINEAR;
    sampler_info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    sampler_info.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler_info.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler_info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler_info.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
    sampler_info.enable_compare = true;
    renderer->model_shadow_sampler = SDL_CreateGPUSampler(demo->device, &sampler_info);
    return renderer->model_shadow_sampler != nullptr;
}

static SDL_GPUGraphicsPipeline *create_model_pipeline_variant(
    ForgeGpuDemo *demo,
    SDL_GPUShader *vertex_shader,
    SDL_GPUShader *fragment_shader,
    const SDL_GPUColorTargetDescription *color_target,
    const SDL_GPUVertexBufferDescription *vertex_buffer,
    const SDL_GPUVertexAttribute *attributes,
    Uint32 num_attributes,
    SDL_GPUCullMode cull_mode,
    bool blend,
    bool depth_write)
{
    SDL_GPUColorTargetDescription target;
    SDL_GPUGraphicsPipelineCreateInfo pipeline_info;

    target = *color_target;
    if (blend) {
        target.blend_state.enable_blend = true;
        target.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
        target.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        target.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
        target.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        target.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        target.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    }

    SDL_zero(pipeline_info);
    pipeline_info.vertex_shader = vertex_shader;
    pipeline_info.fragment_shader = fragment_shader;
    pipeline_info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pipeline_info.vertex_input_state.vertex_buffer_descriptions = vertex_buffer;
    pipeline_info.vertex_input_state.num_vertex_buffers = 1;
    pipeline_info.vertex_input_state.vertex_attributes = attributes;
    pipeline_info.vertex_input_state.num_vertex_attributes = num_attributes;
    pipeline_info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    pipeline_info.rasterizer_state.cull_mode = cull_mode;
    pipeline_info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    pipeline_info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    pipeline_info.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS;
    pipeline_info.depth_stencil_state.enable_depth_test = true;
    pipeline_info.depth_stencil_state.enable_depth_write = depth_write;
    pipeline_info.target_info.color_target_descriptions = &target;
    pipeline_info.target_info.num_color_targets = 1;
    pipeline_info.target_info.has_depth_stencil_target = true;
    pipeline_info.target_info.depth_stencil_format = FORGE_GPU_PROCESSED_SCENE_DEPTH_FORMAT;
    return SDL_CreateGPUGraphicsPipeline(demo->device, &pipeline_info);
}

SDL_GPUGraphicsPipeline *ForgeGpuProcessedSceneCreateModelPipeline(
    ForgeGpuDemo *demo,
    SDL_GPUShader *vertex_shader,
    SDL_GPUShader *fragment_shader)
{
    SDL_GPUColorTargetDescription color_target;
    SDL_GPUVertexBufferDescription vertex_buffer;
    SDL_GPUVertexAttribute attributes[4];

    SDL_zero(color_target);
    color_target.format = demo->color_format;
    fill_model_vertex_input(&vertex_buffer, attributes);
    return create_model_pipeline_variant(
        demo, vertex_shader, fragment_shader, &color_target, &vertex_buffer,
        attributes, SDL_arraysize(attributes), SDL_GPU_CULLMODE_BACK, false, true);
}

static bool create_pipelines(ForgeGpuDemo *demo, ForgeGpuProcessedSceneRenderer *renderer)
{
    SDL_GPUShader *model_vs = nullptr;
    SDL_GPUShader *model_fs = nullptr;
    SDL_GPUShader *shadow_vs = nullptr;
    SDL_GPUShader *shadow_fs = nullptr;
    SDL_GPUShader *grid_vs = nullptr;
    SDL_GPUShader *grid_fs = nullptr;
    SDL_GPUShader *sky_vs = nullptr;
    SDL_GPUShader *sky_fs = nullptr;
    SDL_GPUColorTargetDescription color_target;
    SDL_GPUVertexBufferDescription model_vertex_buffer;
    SDL_GPUVertexAttribute model_attributes[4];
    SDL_GPUVertexBufferDescription grid_vertex_buffer;
    SDL_GPUVertexAttribute grid_attribute;
    bool ok = false;

    model_vs = ForgeGpuCreateShader(demo->device, SDL_GPU_SHADERSTAGE_VERTEX,
        forge_scene_model_vert_wgsl, forge_scene_model_vert_wgsl_size,
        forge_scene_model_vert_msl, forge_scene_model_vert_msl_size,
        0, 0, 0, 1);
    model_fs = ForgeGpuCreateShaderWithResourceLayout(demo->device,
        forge_scene_model_frag_wgsl, forge_scene_model_frag_wgsl_size,
        forge_scene_model_frag_msl, forge_scene_model_frag_msl_size,
        ForgeGpuShaderLayout_forge_scene_model_frag());
    shadow_vs = ForgeGpuCreateShader(demo->device, SDL_GPU_SHADERSTAGE_VERTEX,
        forge_scene_shadow_vert_wgsl, forge_scene_shadow_vert_wgsl_size,
        forge_scene_shadow_vert_msl, forge_scene_shadow_vert_msl_size,
        0, 0, 0, 1);
    shadow_fs = ForgeGpuCreateShader(demo->device, SDL_GPU_SHADERSTAGE_FRAGMENT,
        forge_scene_shadow_frag_wgsl, forge_scene_shadow_frag_wgsl_size,
        forge_scene_shadow_frag_msl, forge_scene_shadow_frag_msl_size,
        0, 0, 0, 0);
    grid_vs = ForgeGpuCreateShader(demo->device, SDL_GPU_SHADERSTAGE_VERTEX,
        forge_scene_grid_vert_wgsl, forge_scene_grid_vert_wgsl_size,
        forge_scene_grid_vert_msl, forge_scene_grid_vert_msl_size,
        0, 0, 0, 1);
    grid_fs = ForgeGpuCreateShaderWithResourceLayout(demo->device,
        forge_scene_grid_frag_wgsl, forge_scene_grid_frag_wgsl_size,
        forge_scene_grid_frag_msl, forge_scene_grid_frag_msl_size,
        ForgeGpuShaderLayout_forge_scene_grid_frag());
    sky_vs = ForgeGpuCreateShader(demo->device, SDL_GPU_SHADERSTAGE_VERTEX,
        forge_scene_sky_vert_wgsl, forge_scene_sky_vert_wgsl_size,
        forge_scene_sky_vert_msl, forge_scene_sky_vert_msl_size,
        0, 0, 0, 0);
    sky_fs = ForgeGpuCreateShader(demo->device, SDL_GPU_SHADERSTAGE_FRAGMENT,
        forge_scene_sky_frag_wgsl, forge_scene_sky_frag_wgsl_size,
        forge_scene_sky_frag_msl, forge_scene_sky_frag_msl_size,
        0, 0, 0, 0);
    if (!model_vs || !model_fs || !shadow_vs || !shadow_fs ||
        !grid_vs || !grid_fs || !sky_vs || !sky_fs) {
        goto done;
    }

    SDL_zero(color_target);
    color_target.format = demo->color_format;

    fill_model_vertex_input(&model_vertex_buffer, model_attributes);
    renderer->model_pipeline = create_model_pipeline_variant(
        demo, model_vs, model_fs, &color_target, &model_vertex_buffer, model_attributes,
        SDL_arraysize(model_attributes), SDL_GPU_CULLMODE_BACK, false, true);
    renderer->model_blend_pipeline = create_model_pipeline_variant(
        demo, model_vs, model_fs, &color_target, &model_vertex_buffer, model_attributes,
        SDL_arraysize(model_attributes), SDL_GPU_CULLMODE_BACK, true, false);
    renderer->model_double_pipeline = create_model_pipeline_variant(
        demo, model_vs, model_fs, &color_target, &model_vertex_buffer, model_attributes,
        SDL_arraysize(model_attributes), SDL_GPU_CULLMODE_NONE, false, true);
    renderer->model_blend_double_pipeline = create_model_pipeline_variant(
        demo, model_vs, model_fs, &color_target, &model_vertex_buffer, model_attributes,
        SDL_arraysize(model_attributes), SDL_GPU_CULLMODE_NONE, true, false);
    renderer->shadow_pipeline = ForgeGpuCreateLessonGraphicsPipelineWithColorTargetsAndDepthCompare(
        demo, shadow_vs, shadow_fs, SDL_GPU_PRIMITIVETYPE_TRIANGLELIST, nullptr, 0,
        &model_vertex_buffer, 1, model_attributes, 1,
        true, FORGE_GPU_PROCESSED_SCENE_DEPTH_FORMAT, true, true, SDL_GPU_COMPAREOP_LESS,
        SDL_GPU_CULLMODE_NONE, PROCESSED_SCENE_SHADOW_BIAS_CONST, PROCESSED_SCENE_SHADOW_BIAS_SLOPE);

    fill_grid_vertex_input(&grid_vertex_buffer, &grid_attribute);
    color_target.blend_state.enable_blend = true;
    color_target.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    color_target.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    color_target.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
    color_target.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    color_target.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    color_target.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    renderer->grid_pipeline = ForgeGpuCreateLessonGraphicsPipelineWithColorTargetsAndDepthCompare(
        demo, grid_vs, grid_fs, SDL_GPU_PRIMITIVETYPE_TRIANGLELIST, &color_target, 1,
        &grid_vertex_buffer, 1, &grid_attribute, 1,
        true, FORGE_GPU_PROCESSED_SCENE_DEPTH_FORMAT, true, true, SDL_GPU_COMPAREOP_LESS_OR_EQUAL,
        SDL_GPU_CULLMODE_NONE, 0.0f, 0.0f);

    SDL_zero(color_target);
    color_target.format = demo->color_format;
    renderer->sky_pipeline = ForgeGpuCreateLessonGraphicsPipelineWithColorTargetsAndDepthCompare(
        demo, sky_vs, sky_fs, SDL_GPU_PRIMITIVETYPE_TRIANGLELIST, &color_target, 1,
        nullptr, 0, nullptr, 0,
        true, FORGE_GPU_PROCESSED_SCENE_DEPTH_FORMAT, true, false, SDL_GPU_COMPAREOP_LESS_OR_EQUAL,
        SDL_GPU_CULLMODE_NONE, 0.0f, 0.0f);

    ok = renderer->model_pipeline && renderer->model_blend_pipeline &&
        renderer->model_double_pipeline && renderer->model_blend_double_pipeline &&
        renderer->shadow_pipeline && renderer->grid_pipeline && renderer->sky_pipeline;

done:
    release_shader(demo->device, &sky_fs);
    release_shader(demo->device, &sky_vs);
    release_shader(demo->device, &grid_fs);
    release_shader(demo->device, &grid_vs);
    release_shader(demo->device, &shadow_fs);
    release_shader(demo->device, &shadow_vs);
    release_shader(demo->device, &model_fs);
    release_shader(demo->device, &model_vs);
    return ok;
}

static void release_skinned_pipelines(ForgeGpuDemo *demo, ForgeGpuProcessedSceneRenderer *renderer)
{
    if (renderer->skinned_shadow_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, renderer->skinned_shadow_pipeline);
        renderer->skinned_shadow_pipeline = nullptr;
    }
    if (renderer->skinned_model_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, renderer->skinned_model_pipeline);
        renderer->skinned_model_pipeline = nullptr;
    }
    if (renderer->skinned_model_blend_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, renderer->skinned_model_blend_pipeline);
        renderer->skinned_model_blend_pipeline = nullptr;
    }
    if (renderer->skinned_model_double_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, renderer->skinned_model_double_pipeline);
        renderer->skinned_model_double_pipeline = nullptr;
    }
    if (renderer->skinned_model_blend_double_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, renderer->skinned_model_blend_double_pipeline);
        renderer->skinned_model_blend_double_pipeline = nullptr;
    }
}

static bool ensure_skinned_pipelines(ForgeGpuDemo *demo, ForgeGpuProcessedSceneRenderer *renderer)
{
    SDL_GPUShader *model_fs = nullptr;
    SDL_GPUShader *shadow_fs = nullptr;
    SDL_GPUShader *skinned_model_vs = nullptr;
    SDL_GPUShader *skinned_shadow_vs = nullptr;
    SDL_GPUColorTargetDescription color_target;
    SDL_GPUVertexBufferDescription vertex_buffer;
    SDL_GPUVertexAttribute attributes[6];
    bool ok = false;

    if (renderer->skinned_model_pipeline && renderer->skinned_model_blend_pipeline &&
        renderer->skinned_model_double_pipeline && renderer->skinned_model_blend_double_pipeline &&
        renderer->skinned_shadow_pipeline) {
        return true;
    }
    release_skinned_pipelines(demo, renderer);

    model_fs = ForgeGpuCreateShaderWithResourceLayout(demo->device,
        forge_scene_model_frag_wgsl, forge_scene_model_frag_wgsl_size,
        forge_scene_model_frag_msl, forge_scene_model_frag_msl_size,
        ForgeGpuShaderLayout_forge_scene_model_frag());
    shadow_fs = ForgeGpuCreateShader(demo->device, SDL_GPU_SHADERSTAGE_FRAGMENT,
        forge_scene_shadow_frag_wgsl, forge_scene_shadow_frag_wgsl_size,
        forge_scene_shadow_frag_msl, forge_scene_shadow_frag_msl_size,
        0, 0, 0, 0);
    skinned_model_vs = ForgeGpuCreateShaderWithResourceLayout(demo->device,
        forge_scene_skinned_vert_wgsl, forge_scene_skinned_vert_wgsl_size,
        forge_scene_skinned_vert_msl, forge_scene_skinned_vert_msl_size,
        ForgeGpuShaderLayout_forge_scene_skinned_vert());
    skinned_shadow_vs = ForgeGpuCreateShaderWithResourceLayout(demo->device,
        forge_scene_skinned_shadow_vert_wgsl, forge_scene_skinned_shadow_vert_wgsl_size,
        forge_scene_skinned_shadow_vert_msl, forge_scene_skinned_shadow_vert_msl_size,
        ForgeGpuShaderLayout_forge_scene_skinned_shadow_vert());
    if (!model_fs || !shadow_fs || !skinned_model_vs || !skinned_shadow_vs) {
        goto done;
    }

    SDL_zero(color_target);
    color_target.format = demo->color_format;
    fill_skinned_model_vertex_input(&vertex_buffer, attributes);
    renderer->skinned_model_pipeline = create_model_pipeline_variant(
        demo, skinned_model_vs, model_fs, &color_target, &vertex_buffer, attributes,
        SDL_arraysize(attributes), SDL_GPU_CULLMODE_BACK, false, true);
    renderer->skinned_model_blend_pipeline = create_model_pipeline_variant(
        demo, skinned_model_vs, model_fs, &color_target, &vertex_buffer, attributes,
        SDL_arraysize(attributes), SDL_GPU_CULLMODE_BACK, true, false);
    renderer->skinned_model_double_pipeline = create_model_pipeline_variant(
        demo, skinned_model_vs, model_fs, &color_target, &vertex_buffer, attributes,
        SDL_arraysize(attributes), SDL_GPU_CULLMODE_NONE, false, true);
    renderer->skinned_model_blend_double_pipeline = create_model_pipeline_variant(
        demo, skinned_model_vs, model_fs, &color_target, &vertex_buffer, attributes,
        SDL_arraysize(attributes), SDL_GPU_CULLMODE_NONE, true, false);
    renderer->skinned_shadow_pipeline = ForgeGpuCreateLessonGraphicsPipelineWithColorTargetsAndDepthCompare(
        demo, skinned_shadow_vs, shadow_fs, SDL_GPU_PRIMITIVETYPE_TRIANGLELIST, nullptr, 0,
        &vertex_buffer, 1, attributes, SDL_arraysize(attributes),
        true, FORGE_GPU_PROCESSED_SCENE_DEPTH_FORMAT, true, true, SDL_GPU_COMPAREOP_LESS,
        SDL_GPU_CULLMODE_NONE, PROCESSED_SCENE_SHADOW_BIAS_CONST, PROCESSED_SCENE_SHADOW_BIAS_SLOPE);

    ok = renderer->skinned_model_pipeline && renderer->skinned_model_blend_pipeline &&
        renderer->skinned_model_double_pipeline && renderer->skinned_model_blend_double_pipeline &&
        renderer->skinned_shadow_pipeline;

done:
    release_shader(demo->device, &skinned_shadow_vs);
    release_shader(demo->device, &skinned_model_vs);
    release_shader(demo->device, &shadow_fs);
    release_shader(demo->device, &model_fs);
    if (!ok) {
        release_skinned_pipelines(demo, renderer);
    }
    return ok;
}

static void release_morph_pipelines(ForgeGpuDemo *demo, ForgeGpuProcessedSceneRenderer *renderer)
{
    if (renderer->morph_shadow_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, renderer->morph_shadow_pipeline);
        renderer->morph_shadow_pipeline = nullptr;
    }
    if (renderer->morph_model_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, renderer->morph_model_pipeline);
        renderer->morph_model_pipeline = nullptr;
    }
    if (renderer->morph_model_blend_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, renderer->morph_model_blend_pipeline);
        renderer->morph_model_blend_pipeline = nullptr;
    }
    if (renderer->morph_model_double_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, renderer->morph_model_double_pipeline);
        renderer->morph_model_double_pipeline = nullptr;
    }
    if (renderer->morph_model_blend_double_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, renderer->morph_model_blend_double_pipeline);
        renderer->morph_model_blend_double_pipeline = nullptr;
    }
}

static bool ensure_morph_pipelines(ForgeGpuDemo *demo, ForgeGpuProcessedSceneRenderer *renderer)
{
    SDL_GPUShader *model_fs = nullptr;
    SDL_GPUShader *shadow_fs = nullptr;
    SDL_GPUShader *morph_model_vs = nullptr;
    SDL_GPUShader *morph_shadow_vs = nullptr;
    SDL_GPUColorTargetDescription color_target;
    SDL_GPUVertexBufferDescription vertex_buffer;
    SDL_GPUVertexAttribute attributes[4];
    SDL_GPUVertexAttribute shadow_attribute;
    bool ok = false;

    if (renderer->morph_model_pipeline && renderer->morph_model_blend_pipeline &&
        renderer->morph_model_double_pipeline && renderer->morph_model_blend_double_pipeline &&
        renderer->morph_shadow_pipeline) {
        return true;
    }
    release_morph_pipelines(demo, renderer);

    model_fs = ForgeGpuCreateShaderWithResourceLayout(demo->device,
        forge_scene_model_frag_wgsl, forge_scene_model_frag_wgsl_size,
        forge_scene_model_frag_msl, forge_scene_model_frag_msl_size,
        ForgeGpuShaderLayout_forge_scene_model_frag());
    shadow_fs = ForgeGpuCreateShader(demo->device, SDL_GPU_SHADERSTAGE_FRAGMENT,
        forge_scene_shadow_frag_wgsl, forge_scene_shadow_frag_wgsl_size,
        forge_scene_shadow_frag_msl, forge_scene_shadow_frag_msl_size,
        0, 0, 0, 0);
    morph_model_vs = ForgeGpuCreateShaderWithResourceLayout(demo->device,
        forge_scene_morph_vert_wgsl, forge_scene_morph_vert_wgsl_size,
        forge_scene_morph_vert_msl, forge_scene_morph_vert_msl_size,
        ForgeGpuShaderLayout_forge_scene_morph_vert());
    morph_shadow_vs = ForgeGpuCreateShaderWithResourceLayout(demo->device,
        forge_scene_morph_shadow_vert_wgsl, forge_scene_morph_shadow_vert_wgsl_size,
        forge_scene_morph_shadow_vert_msl, forge_scene_morph_shadow_vert_msl_size,
        ForgeGpuShaderLayout_forge_scene_morph_shadow_vert());
    if (!model_fs || !shadow_fs || !morph_model_vs || !morph_shadow_vs) {
        goto done;
    }

    SDL_zero(color_target);
    color_target.format = demo->color_format;
    fill_model_vertex_input(&vertex_buffer, attributes);
    renderer->morph_model_pipeline = create_model_pipeline_variant(
        demo, morph_model_vs, model_fs, &color_target, &vertex_buffer, attributes,
        SDL_arraysize(attributes), SDL_GPU_CULLMODE_BACK, false, true);
    renderer->morph_model_blend_pipeline = create_model_pipeline_variant(
        demo, morph_model_vs, model_fs, &color_target, &vertex_buffer, attributes,
        SDL_arraysize(attributes), SDL_GPU_CULLMODE_BACK, true, false);
    renderer->morph_model_double_pipeline = create_model_pipeline_variant(
        demo, morph_model_vs, model_fs, &color_target, &vertex_buffer, attributes,
        SDL_arraysize(attributes), SDL_GPU_CULLMODE_NONE, false, true);
    renderer->morph_model_blend_double_pipeline = create_model_pipeline_variant(
        demo, morph_model_vs, model_fs, &color_target, &vertex_buffer, attributes,
        SDL_arraysize(attributes), SDL_GPU_CULLMODE_NONE, true, false);

    SDL_zero(shadow_attribute);
    shadow_attribute.location = 0;
    shadow_attribute.buffer_slot = 0;
    shadow_attribute.format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    shadow_attribute.offset = 0;
    renderer->morph_shadow_pipeline = ForgeGpuCreateLessonGraphicsPipelineWithColorTargetsAndDepthCompare(
        demo, morph_shadow_vs, shadow_fs, SDL_GPU_PRIMITIVETYPE_TRIANGLELIST, nullptr, 0,
        &vertex_buffer, 1, &shadow_attribute, 1,
        true, FORGE_GPU_PROCESSED_SCENE_DEPTH_FORMAT, true, true, SDL_GPU_COMPAREOP_LESS,
        SDL_GPU_CULLMODE_NONE, PROCESSED_SCENE_SHADOW_BIAS_CONST, PROCESSED_SCENE_SHADOW_BIAS_SLOPE);

    ok = renderer->morph_model_pipeline && renderer->morph_model_blend_pipeline &&
        renderer->morph_model_double_pipeline && renderer->morph_model_blend_double_pipeline &&
        renderer->morph_shadow_pipeline;

done:
    release_shader(demo->device, &morph_shadow_vs);
    release_shader(demo->device, &morph_model_vs);
    release_shader(demo->device, &shadow_fs);
    release_shader(demo->device, &model_fs);
    if (!ok) {
        release_morph_pipelines(demo, renderer);
    }
    return ok;
}

bool ForgeGpuProcessedSceneRendererCreate(ForgeGpuDemo *demo, ForgeGpuProcessedSceneRenderer *renderer)
{
    Uint8 flat_normal_pixel[4] = { 128, 128, 255, 255 };
    Uint8 black_pixel[4] = { 0, 0, 0, 255 };

    SDL_zero(*renderer);
    renderer->transparency_sorting = true;

    if (!SDL_GPUTextureSupportsFormat(
            demo->device,
            FORGE_GPU_PROCESSED_SCENE_DEPTH_FORMAT,
            SDL_GPU_TEXTURETYPE_2D,
            SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER)) {
        SDL_SetError("processed scene renderer requires sampled D32_FLOAT depth textures");
        return false;
    }

    renderer->white_texture = ForgeGpuCreateWhiteTexture(demo->device);
    renderer->black_texture = ForgeGpuCreateRgba8TextureFromPixels(demo->device, 1, 1, black_pixel, false);
    renderer->flat_normal_texture = ForgeGpuCreateRgba8TextureFromPixels(demo->device, 1, 1, flat_normal_pixel, false);
    renderer->material_sampler = ForgeGpuCreateSamplerWithAddressAndAnisotropy(
        demo->device,
        SDL_GPU_FILTER_LINEAR,
        SDL_GPU_FILTER_LINEAR,
        SDL_GPU_SAMPLERMIPMAPMODE_LINEAR,
        SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
        0.0f,
        1.0f);
    renderer->normal_sampler = ForgeGpuCreateSamplerWithAddress(
        demo->device,
        SDL_GPU_FILTER_LINEAR,
        SDL_GPU_FILTER_LINEAR,
        SDL_GPU_SAMPLERMIPMAPMODE_LINEAR,
        SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
        0.0f);
    renderer->grid_shadow_sampler = ForgeGpuCreateSamplerWithAddress(
        demo->device,
        SDL_GPU_FILTER_NEAREST,
        SDL_GPU_FILTER_NEAREST,
        SDL_GPU_SAMPLERMIPMAPMODE_NEAREST,
        SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        0.0f);
    renderer->shadow_depth = ForgeGpuCreateSampledDepthTexture(
        demo,
        FORGE_GPU_PROCESSED_SCENE_SHADOW_MAP_SIZE,
        FORGE_GPU_PROCESSED_SCENE_SHADOW_MAP_SIZE,
        FORGE_GPU_PROCESSED_SCENE_DEPTH_FORMAT);

    return renderer->white_texture && renderer->black_texture && renderer->flat_normal_texture &&
        renderer->material_sampler && renderer->normal_sampler && renderer->grid_shadow_sampler &&
        renderer->shadow_depth && create_model_shadow_sampler(demo, renderer) &&
        create_pipelines(demo, renderer) &&
        ForgeGpuCreateShadowedGridBuffers(
            demo->device,
            PROCESSED_SCENE_GRID_HALF_SIZE,
            0.0f,
            &renderer->grid_vertex_buffer,
            &renderer->grid_index_buffer);
}

bool ForgeGpuProcessedSceneRendererEnsureMainDepth(
    ForgeGpuDemo *demo,
    ForgeGpuProcessedSceneRenderer *renderer,
    Uint32 width,
    Uint32 height)
{
    return ForgeGpuEnsureSampledDepthTarget(
        demo,
        &renderer->main_depth,
        &renderer->main_depth_width,
        &renderer->main_depth_height,
        width,
        height,
        FORGE_GPU_PROCESSED_SCENE_DEPTH_FORMAT);
}

void ForgeGpuProcessedSceneRendererBeginFrame(ForgeGpuProcessedSceneRenderer *renderer)
{
    renderer->shadow_pass_rendered = false;
    renderer->main_pass_rendered = false;
}

SDL_GPURenderPass *ForgeGpuProcessedSceneBeginShadowPass(
    SDL_GPUCommandBuffer *command_buffer,
    ForgeGpuProcessedSceneRenderer *renderer)
{
    return ForgeGpuBeginDepthOnlyPass(command_buffer, renderer->shadow_depth, 1.0f);
}

SDL_GPURenderPass *ForgeGpuProcessedSceneBeginMainPass(
    SDL_GPUCommandBuffer *command_buffer,
    ForgeGpuProcessedSceneRenderer *renderer,
    SDL_GPUTexture *swapchain_texture)
{
    const ForgeGpuColorTargetAttachment color_target = {
        swapchain_texture,
        { 0.15f, 0.15f, 0.20f, 1.0f }
    };

    SDL_GPURenderPass *render_pass = ForgeGpuBeginColorDepthPass(command_buffer, &color_target, 1, renderer->main_depth, 1.0f);
    if (render_pass) {
        SDL_BindGPUGraphicsPipeline(render_pass, renderer->sky_pipeline);
        SDL_DrawGPUPrimitives(render_pass, 3, 1, 0, 0);
    }

    return render_pass;
}

static void bind_model_buffers(SDL_GPURenderPass *render_pass, const ForgeGpuProcessedSceneModel *model)
{
    SDL_GPUBufferBinding vertex_binding;
    SDL_GPUBufferBinding index_binding;

    SDL_zero(vertex_binding);
    vertex_binding.buffer = model->vertex_buffer;
    SDL_BindGPUVertexBuffers(render_pass, 0, &vertex_binding, 1);

    SDL_zero(index_binding);
    index_binding.buffer = model->index_buffer;
    SDL_BindGPUIndexBuffer(render_pass, &index_binding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
}

static void bind_model_textures(
    SDL_GPURenderPass *render_pass,
    ForgeGpuProcessedSceneRenderer *renderer,
    const ForgeGpuProcessedSceneMaterialTextures *textures)
{
    SDL_GPUTextureSamplerBinding bindings[6];

    SDL_zeroa(bindings);
    bindings[0].texture = textures ? textures->base_color : renderer->white_texture;
    bindings[0].sampler = renderer->material_sampler;
    bindings[1].texture = textures ? textures->normal : renderer->flat_normal_texture;
    bindings[1].sampler = renderer->normal_sampler;
    bindings[2].texture = textures ? textures->metallic_roughness : renderer->white_texture;
    bindings[2].sampler = renderer->material_sampler;
    bindings[3].texture = textures ? textures->occlusion : renderer->white_texture;
    bindings[3].sampler = renderer->material_sampler;
    bindings[4].texture = textures ? textures->emissive : renderer->black_texture;
    bindings[4].sampler = renderer->material_sampler;
    bindings[5].texture = renderer->shadow_depth;
    bindings[5].sampler = renderer->model_shadow_sampler;
    SDL_BindGPUFragmentSamplers(render_pass, 0, bindings, SDL_arraysize(bindings));
}

static void bind_model_pbr_textures(
    SDL_GPURenderPass *render_pass,
    ForgeGpuProcessedSceneRenderer *renderer,
    const ForgeGpuProcessedSceneMaterialTextures *textures)
{
    SDL_GPUTextureSamplerBinding bindings[8];

    SDL_zeroa(bindings);
    bindings[0].texture = textures ? textures->base_color : renderer->white_texture;
    bindings[0].sampler = renderer->material_sampler;
    bindings[1].texture = textures ? textures->normal : renderer->flat_normal_texture;
    bindings[1].sampler = renderer->normal_sampler;
    bindings[2].texture = textures ? textures->metallic_roughness : renderer->white_texture;
    bindings[2].sampler = renderer->material_sampler;
    bindings[3].texture = textures ? textures->occlusion : renderer->white_texture;
    bindings[3].sampler = renderer->material_sampler;
    bindings[4].texture = textures ? textures->emissive : renderer->black_texture;
    bindings[4].sampler = renderer->material_sampler;
    bindings[5].texture = renderer->shadow_depth;
    bindings[5].sampler = renderer->model_shadow_sampler;
    bindings[6].texture = textures ? textures->roughness : renderer->white_texture;
    bindings[6].sampler = renderer->material_sampler;
    bindings[7].texture = textures ? textures->metallic : renderer->white_texture;
    bindings[7].sampler = renderer->material_sampler;
    SDL_BindGPUFragmentSamplers(render_pass, 0, bindings, SDL_arraysize(bindings));
}

static SDL_GPUGraphicsPipeline *select_model_pipeline(
    const ForgeGpuProcessedScenePipelineSet *pipelines,
    const ForgeGpuProcessedMaterial *material)
{
    if (material && material->alpha_mode == FORGE_GPU_PROCESSED_ALPHA_BLEND) {
        return material->double_sided ? pipelines->blend_double_sided : pipelines->blend;
    }
    return material && material->double_sided ? pipelines->double_sided : pipelines->model;
}

static void fill_fragment_uniforms(
    ForgeGpuDemo *demo,
    const ForgeGpuProcessedMaterial *material,
    ForgeGpuProcessedSceneModelFragUniforms *uniforms)
{
    const Vec3 light_dir = ForgeGpuProcessedSceneLightDir();

    SDL_zero(*uniforms);
    uniforms->light_dir[0] = light_dir.x;
    uniforms->light_dir[1] = light_dir.y;
    uniforms->light_dir[2] = light_dir.z;
    uniforms->eye_pos[0] = demo->lesson.camera_position.x;
    uniforms->eye_pos[1] = demo->lesson.camera_position.y;
    uniforms->eye_pos[2] = demo->lesson.camera_position.z;
    uniforms->base_color_factor[0] = 1.0f;
    uniforms->base_color_factor[1] = 1.0f;
    uniforms->base_color_factor[2] = 1.0f;
    uniforms->base_color_factor[3] = 1.0f;
    uniforms->metallic_factor = 0.0f;
    uniforms->roughness_factor = 1.0f;
    uniforms->normal_scale = 1.0f;
    uniforms->occlusion_strength = 1.0f;
    uniforms->alpha_cutoff = 0.0f;

    if (material) {
        SDL_memcpy(uniforms->base_color_factor, material->base_color_factor, sizeof(uniforms->base_color_factor));
        SDL_memcpy(uniforms->emissive_factor, material->emissive_factor, sizeof(uniforms->emissive_factor));
        uniforms->metallic_factor = material->metallic_factor;
        uniforms->roughness_factor = material->roughness_factor;
        uniforms->normal_scale = material->normal_scale;
        uniforms->occlusion_strength = material->occlusion_strength;
        uniforms->alpha_cutoff =
            material->alpha_mode == FORGE_GPU_PROCESSED_ALPHA_MASK ? material->alpha_cutoff : 0.0f;
    }

    uniforms->shadow_texel = 1.0f / (float)FORGE_GPU_PROCESSED_SCENE_SHADOW_MAP_SIZE;
    uniforms->shininess = PROCESSED_SCENE_SHININESS;
    uniforms->specular_strength = PROCESSED_SCENE_SPECULAR_STRENGTH;
    uniforms->ambient = PROCESSED_SCENE_AMBIENT;
}

static bool draw_model_internal(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    ForgeGpuProcessedSceneRenderer *renderer,
    ForgeGpuProcessedSceneModel *model,
    Mat4 placement,
    Mat4 camera_vp,
    Mat4 light_vp,
    bool shadow_pass,
    const ForgeGpuProcessedScenePipelineSet *pipelines,
    SDL_GPUBuffer *const *vertex_storage_buffers,
    Uint32 main_vertex_storage_buffer_count,
    Uint32 shadow_vertex_storage_buffer_count)
{
    ForgeGpuProcessedSceneTransparentDraw *transparent_draws = nullptr;
    Uint32 transparent_draw_count = 0;
    Uint32 transparent_draw_capacity = 0;
    Quat camera_orientation;
    Vec3 camera_forward;
    Uint32 vertex_storage_buffer_count;

    if (!model->vertex_buffer || !model->index_buffer || model->mesh.lod_count == 0) {
        return true;
    }

    camera_orientation = quat_from_euler(demo->lesson.camera_yaw, demo->lesson.camera_pitch, 0.0f);
    camera_forward = quat_forward(camera_orientation);
    bind_model_buffers(render_pass, model);
    vertex_storage_buffer_count = shadow_pass ? shadow_vertex_storage_buffer_count : main_vertex_storage_buffer_count;
    if (vertex_storage_buffers && vertex_storage_buffer_count > 0) {
        SDL_BindGPUVertexStorageBuffers(render_pass, 0, vertex_storage_buffers, vertex_storage_buffer_count);
    }

    for (Uint32 node_index = 0; node_index < model->scene.node_count; node_index += 1) {
        const ForgeGpuProcessedSceneNode *node = &model->scene.nodes[node_index];
        const ForgeGpuProcessedSceneMesh *scene_mesh;
        Mat4 node_world;
        Mat4 final_world;

        if (node->mesh_index < 0) {
            continue;
        }
        if ((Uint32)node->mesh_index >= model->scene.mesh_count) {
            continue;
        }

        scene_mesh = &model->scene.meshes[node->mesh_index];
        node_world = mat4_from_array(node->world_transform);
        final_world = mat4_multiply(placement, node_world);

        for (Uint32 submesh_offset = 0; submesh_offset < scene_mesh->submesh_count; submesh_offset += 1) {
            const Uint32 submesh_index = scene_mesh->first_submesh + submesh_offset;
            const ForgeGpuProcessedSubmesh *submesh;
            const ForgeGpuProcessedMaterial *material = nullptr;
            const ForgeGpuProcessedSceneMaterialTextures *textures = nullptr;

            if (submesh_index >= model->mesh.submesh_count) {
                continue;
            }
            submesh = &model->mesh.submeshes[submesh_index];
            if (submesh->index_count == 0) {
                continue;
            }
            if (submesh->material_index >= 0 &&
                (Uint32)submesh->material_index < model->materials.material_count) {
                material = &model->materials.materials[submesh->material_index];
                if ((Uint32)submesh->material_index < model->material_texture_count) {
                    textures = &model->material_textures[submesh->material_index];
                }
            }

            if (shadow_pass) {
                ForgeGpuProcessedSceneShadowUniforms uniforms;

                if (material && material->alpha_mode == FORGE_GPU_PROCESSED_ALPHA_BLEND) {
                    continue;
                }
                SDL_BindGPUGraphicsPipeline(render_pass, pipelines->shadow);
                uniforms.light_vp = mat4_multiply(light_vp, final_world);
                SDL_PushGPUVertexUniformData(command_buffer, 0, &uniforms, sizeof(uniforms));
                model->shadow_draw_calls += 1;
            } else {
                ForgeGpuProcessedSceneModelVertUniforms vertex_uniforms;
                ForgeGpuProcessedSceneModelFragUniforms fragment_uniforms;

                if (material && material->alpha_mode == FORGE_GPU_PROCESSED_ALPHA_BLEND && renderer->transparency_sorting) {
                    ForgeGpuProcessedSceneTransparentDraw *draw;
                    Vec3 centroid = submesh_index < model->mesh.submesh_count ?
                        model->submesh_centroids[submesh_index] : Vec3{ 0.0f, 0.0f, 0.0f };
                    Vec4 world = mat4_multiply_vec4(
                        final_world,
                        vec4_create(centroid.x, centroid.y, centroid.z, 1.0f));
                    Vec3 world_position = { world.x, world.y, world.z };

                    if (!transparent_draws) {
                        if (!count_scene_submesh_draw_capacity(model, &transparent_draw_capacity)) {
                            return false;
                        }
                        if (transparent_draw_capacity == 0) {
                            SDL_SetError("processed scene model has no transparent draw capacity");
                            return false;
                        }
                        transparent_draws = (ForgeGpuProcessedSceneTransparentDraw *)SDL_calloc(
                            transparent_draw_capacity,
                            sizeof(*transparent_draws));
                        if (!transparent_draws) {
                            SDL_OutOfMemory();
                            return false;
                        }
                    }
                    if (transparent_draw_count >= transparent_draw_capacity) {
                        SDL_SetError("processed scene transparent draw capacity exceeded");
                        SDL_free(transparent_draws);
                        return false;
                    }

                    draw = &transparent_draws[transparent_draw_count];
                    draw->model = model;
                    draw->final_world = final_world;
                    draw->node_index = node_index;
                    draw->submesh_index = submesh_index;
                    draw->sort_depth = vec3_dot(vec3_sub(world_position, demo->lesson.camera_position), camera_forward);
                    transparent_draw_count += 1;
                    continue;
                }

                SDL_BindGPUGraphicsPipeline(render_pass, select_model_pipeline(pipelines, material));
                vertex_uniforms.mvp = mat4_multiply(camera_vp, final_world);
                vertex_uniforms.model = final_world;
                vertex_uniforms.light_vp = mat4_multiply(light_vp, final_world);
                SDL_PushGPUVertexUniformData(command_buffer, 0, &vertex_uniforms, sizeof(vertex_uniforms));

                fill_fragment_uniforms(demo, material, &fragment_uniforms);
                SDL_PushGPUFragmentUniformData(command_buffer, 0, &fragment_uniforms, sizeof(fragment_uniforms));
                bind_model_textures(render_pass, renderer, textures);
                model->draw_calls += 1;
            }

            SDL_DrawGPUIndexedPrimitives(
                render_pass,
                submesh->index_count,
                1,
                submesh->index_offset / sizeof(Uint32),
                0,
                0);
        }
    }

    if (!shadow_pass && transparent_draw_count > 0) {
        SDL_qsort(
            transparent_draws,
            (size_t)transparent_draw_count,
            sizeof(*transparent_draws),
            compare_transparent_draws);

        for (Uint32 i = 0; i < transparent_draw_count; i += 1) {
            const ForgeGpuProcessedSceneTransparentDraw *draw = &transparent_draws[i];
            const ForgeGpuProcessedSubmesh *submesh = &draw->model->mesh.submeshes[draw->submesh_index];
            const ForgeGpuProcessedMaterial *material = nullptr;
            const ForgeGpuProcessedSceneMaterialTextures *textures = nullptr;
            ForgeGpuProcessedSceneModelVertUniforms vertex_uniforms;
            ForgeGpuProcessedSceneModelFragUniforms fragment_uniforms;

            if (submesh->material_index >= 0 &&
                (Uint32)submesh->material_index < draw->model->materials.material_count) {
                material = &draw->model->materials.materials[submesh->material_index];
                if ((Uint32)submesh->material_index < draw->model->material_texture_count) {
                    textures = &draw->model->material_textures[submesh->material_index];
                }
            }

            SDL_BindGPUGraphicsPipeline(render_pass, select_model_pipeline(pipelines, material));
            vertex_uniforms.mvp = mat4_multiply(camera_vp, draw->final_world);
            vertex_uniforms.model = draw->final_world;
            vertex_uniforms.light_vp = mat4_multiply(light_vp, draw->final_world);
            SDL_PushGPUVertexUniformData(command_buffer, 0, &vertex_uniforms, sizeof(vertex_uniforms));

            fill_fragment_uniforms(demo, material, &fragment_uniforms);
            SDL_PushGPUFragmentUniformData(command_buffer, 0, &fragment_uniforms, sizeof(fragment_uniforms));
            bind_model_textures(render_pass, renderer, textures);
            SDL_DrawGPUIndexedPrimitives(
                render_pass,
                submesh->index_count,
                1,
                submesh->index_offset / sizeof(Uint32),
                0,
                0);
            draw->model->draw_calls += 1;
            draw->model->transparent_draw_calls += 1;
        }
    }
    SDL_free(transparent_draws);
    return true;
}

bool ForgeGpuProcessedSceneDrawModel(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    ForgeGpuProcessedSceneRenderer *renderer,
    ForgeGpuProcessedSceneModel *model,
    Mat4 placement,
    Mat4 camera_vp,
    Mat4 light_vp,
    bool shadow_pass)
{
    const ForgeGpuProcessedScenePipelineSet pipelines = {
        renderer->model_pipeline,
        renderer->model_blend_pipeline,
        renderer->model_double_pipeline,
        renderer->model_blend_double_pipeline,
        renderer->shadow_pipeline
    };

    return draw_model_internal(
        demo, command_buffer, render_pass, renderer, model,
        placement, camera_vp, light_vp, shadow_pass, &pipelines,
        nullptr, 0, 0);
}

bool ForgeGpuProcessedSceneDrawModelWithPipeline(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    ForgeGpuProcessedSceneRenderer *renderer,
    ForgeGpuProcessedSceneModel *model,
    SDL_GPUGraphicsPipeline *model_pipeline,
    Mat4 placement,
    Mat4 camera_vp,
    Mat4 light_vp)
{
    const ForgeGpuProcessedScenePipelineSet pipelines = {
        model_pipeline,
        model_pipeline,
        model_pipeline,
        model_pipeline,
        renderer->shadow_pipeline
    };

    if (!model_pipeline) {
        SDL_SetError("processed scene custom model pipeline is missing");
        return false;
    }
    return draw_model_internal(
        demo, command_buffer, render_pass, renderer, model,
        placement, camera_vp, light_vp, false, &pipelines,
        nullptr, 0, 0);
}

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
    const float *emissive_factor_override)
{
    if (!model_pipeline) {
        SDL_SetError("processed scene PBR model pipeline is missing");
        return false;
    }
    if (!material || !textures) {
        SDL_InvalidParamError("ForgeGpuProcessedSceneDrawModelWithPbrMaterial");
        return false;
    }
    if (material->alpha_mode != FORGE_GPU_PROCESSED_ALPHA_OPAQUE || material->double_sided) {
        SDL_SetError("processed scene PBR material draw currently expects opaque single-sided materials");
        return false;
    }
    if (!model->vertex_buffer || !model->index_buffer || model->mesh.lod_count == 0) {
        return true;
    }

    SDL_BindGPUGraphicsPipeline(render_pass, model_pipeline);
    bind_model_buffers(render_pass, model);

    for (Uint32 node_index = 0; node_index < model->scene.node_count; node_index += 1) {
        const ForgeGpuProcessedSceneNode *node = &model->scene.nodes[node_index];
        const ForgeGpuProcessedSceneMesh *scene_mesh;
        Mat4 node_world;
        Mat4 final_world;

        if (node->mesh_index < 0 || (Uint32)node->mesh_index >= model->scene.mesh_count) {
            continue;
        }

        scene_mesh = &model->scene.meshes[node->mesh_index];
        node_world = mat4_from_array(node->world_transform);
        final_world = mat4_multiply(placement, node_world);

        for (Uint32 submesh_offset = 0; submesh_offset < scene_mesh->submesh_count; submesh_offset += 1) {
            const Uint32 submesh_index = scene_mesh->first_submesh + submesh_offset;
            const ForgeGpuProcessedSubmesh *submesh;
            ForgeGpuProcessedSceneModelVertUniforms vertex_uniforms;
            ForgeGpuProcessedSceneModelFragUniforms fragment_uniforms;

            if (submesh_index >= model->mesh.submesh_count) {
                continue;
            }
            submesh = &model->mesh.submeshes[submesh_index];
            if (submesh->index_count == 0) {
                continue;
            }

            vertex_uniforms.mvp = mat4_multiply(camera_vp, final_world);
            vertex_uniforms.model = final_world;
            vertex_uniforms.light_vp = mat4_multiply(light_vp, final_world);
            SDL_PushGPUVertexUniformData(command_buffer, 0, &vertex_uniforms, sizeof(vertex_uniforms));

            fill_fragment_uniforms(demo, material, &fragment_uniforms);
            fragment_uniforms.shininess = textures->uses_separate_metallic_roughness ? 1.0f : 0.0f;
            if (emissive_factor_override) {
                SDL_memcpy(fragment_uniforms.emissive_factor, emissive_factor_override, sizeof(fragment_uniforms.emissive_factor));
            }
            SDL_PushGPUFragmentUniformData(command_buffer, 0, &fragment_uniforms, sizeof(fragment_uniforms));
            bind_model_pbr_textures(render_pass, renderer, textures);

            SDL_DrawGPUIndexedPrimitives(
                render_pass,
                submesh->index_count,
                1,
                submesh->index_offset / sizeof(Uint32),
                0,
                0);
            model->draw_calls += 1;
        }
    }
    return true;
}

bool ForgeGpuProcessedSceneDrawSkinnedModel(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    ForgeGpuProcessedSceneRenderer *renderer,
    ForgeGpuProcessedSceneSkinnedModel *model,
    Mat4 placement,
    Mat4 camera_vp,
    Mat4 light_vp,
    bool shadow_pass)
{
    const ForgeGpuProcessedScenePipelineSet pipelines = {
        renderer->skinned_model_pipeline,
        renderer->skinned_model_blend_pipeline,
        renderer->skinned_model_double_pipeline,
        renderer->skinned_model_blend_double_pipeline,
        renderer->skinned_shadow_pipeline
    };
    SDL_GPUBuffer *vertex_storage_buffers[1];

    if (!model) {
        return true;
    }
    vertex_storage_buffers[0] = model->joint_buffer;
    return draw_model_internal(
        demo, command_buffer, render_pass, renderer, &model->model,
        placement, camera_vp, light_vp, shadow_pass, &pipelines,
        vertex_storage_buffers, 1, 1);
}

void ForgeGpuProcessedSceneDrawGrid(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    ForgeGpuProcessedSceneRenderer *renderer,
    Mat4 camera_vp,
    Mat4 light_vp)
{
    ForgeGpuShadowedGridDrawInfo grid_info;

    SDL_zero(grid_info);
    grid_info.vp = camera_vp;
    grid_info.light_vp = light_vp;
    grid_info.light_dir = ForgeGpuProcessedSceneLightDir();
    grid_info.eye_pos = demo->lesson.camera_position;
    grid_info.light_intensity = PROCESSED_SCENE_LIGHT_INTENSITY;
    SDL_memcpy(grid_info.line_color, kProcessedSceneGridLineColor, sizeof(grid_info.line_color));
    SDL_memcpy(grid_info.bg_color, kProcessedSceneGridBgColor, sizeof(grid_info.bg_color));
    grid_info.grid_spacing = PROCESSED_SCENE_GRID_SPACING;
    grid_info.line_width = PROCESSED_SCENE_GRID_LINE_WIDTH;
    grid_info.fade_distance = PROCESSED_SCENE_GRID_FADE_DISTANCE;
    grid_info.ambient = PROCESSED_SCENE_AMBIENT;
    grid_info.shadow_depth = renderer->shadow_depth;
    grid_info.shadow_sampler = renderer->grid_shadow_sampler;

    ForgeGpuDrawShadowedGrid(
        command_buffer,
        render_pass,
        renderer->grid_pipeline,
        renderer->grid_vertex_buffer,
        renderer->grid_index_buffer,
        &grid_info);
}

void ForgeGpuProcessedSceneResetModelDrawCounts(ForgeGpuProcessedSceneModel *model)
{
    model->draw_calls = 0;
    model->shadow_draw_calls = 0;
    model->transparent_draw_calls = 0;
}

void ForgeGpuProcessedSceneResetSkinnedModelDrawCounts(ForgeGpuProcessedSceneSkinnedModel *model)
{
    if (!model) {
        return;
    }
    ForgeGpuProcessedSceneResetModelDrawCounts(&model->model);
}

void ForgeGpuProcessedSceneResetMorphModelDrawCounts(ForgeGpuProcessedSceneMorphModel *model)
{
    if (!model) {
        return;
    }
    ForgeGpuProcessedSceneResetModelDrawCounts(&model->model);
}

bool ForgeGpuProcessedSceneUpdateSkinnedAnimation(
    ForgeGpuDemo *demo,
    ForgeGpuProcessedSceneSkinnedModel *model,
    float delta_time_seconds)
{
    ForgeGpuProcessedAnimationClip *clip;
    void *mapped;

    if (!model || model->animation.clip_count == 0 || model->current_clip < 0 ||
        (Uint32)model->current_clip >= model->animation.clip_count) {
        return true;
    }

    clip = &model->animation.clips[model->current_clip];
    model->anim_time += delta_time_seconds * model->anim_speed;
    if (model->looping && clip->duration > 0.0f) {
        model->anim_time = SDL_fmodf(model->anim_time, clip->duration);
        if (model->anim_time < 0.0f) {
            model->anim_time += clip->duration;
        }
    }

    if (!ForgeGpuApplyProcessedSceneAnimation(&model->model.scene, clip, model->anim_time, model->looping) ||
        !ForgeGpuRecomputeProcessedSceneWorldTransforms(&model->model.scene) ||
        !ForgeGpuComputeProcessedSkinJointMatrices(
            &model->model.scene,
            &model->skins.skins[0],
            model->skinned_mesh_node,
            (float *)model->joint_matrices,
            SDL_arraysize(model->joint_matrices),
            &model->active_joint_count)) {
        return false;
    }

    if (model->active_joint_count == 0) {
        model->pending_joint_upload_size = 0;
        return true;
    }

    model->pending_joint_upload_size = model->active_joint_count * (Uint32)sizeof(model->joint_matrices[0]);
    mapped = SDL_MapGPUTransferBuffer(demo->device, model->joint_transfer_buffer, true);
    if (!mapped) {
        return false;
    }
    SDL_memcpy(mapped, model->joint_matrices, model->pending_joint_upload_size);
    SDL_UnmapGPUTransferBuffer(demo->device, model->joint_transfer_buffer);
    return true;
}

bool ForgeGpuProcessedSceneUploadSkinnedJoints(
    SDL_GPUCopyPass *copy_pass,
    ForgeGpuProcessedSceneSkinnedModel *model)
{
    SDL_GPUTransferBufferLocation source;
    SDL_GPUBufferRegion destination;

    if (!model || model->pending_joint_upload_size == 0) {
        return true;
    }

    SDL_zero(source);
    source.transfer_buffer = model->joint_transfer_buffer;

    SDL_zero(destination);
    destination.buffer = model->joint_buffer;
    destination.size = model->pending_joint_upload_size;

    SDL_UploadToGPUBuffer(copy_pass, &source, &destination, true);
    model->pending_joint_upload_size = 0;
    return true;
}

bool ForgeGpuProcessedSceneUpdateMorphAnimation(
    ForgeGpuDemo *demo,
    ForgeGpuProcessedSceneMorphModel *model,
    float delta_time_seconds)
{
    ForgeGpuProcessedAnimationClip *clip = nullptr;
    Uint32 delta_size;
    void *mapped;

    if (!model || model->morph_target_count == 0 ||
        !model->blended_pos_deltas || !model->blended_nrm_deltas) {
        return true;
    }

    if (!model->manual_weights && model->animation.clip_count > 0 &&
        model->current_clip >= 0 && (Uint32)model->current_clip < model->animation.clip_count) {
        clip = &model->animation.clips[model->current_clip];
        model->anim_time += delta_time_seconds * model->anim_speed;
        if (model->looping && clip->duration > 0.0f) {
            model->anim_time = SDL_fmodf(model->anim_time, clip->duration);
            if (model->anim_time < 0.0f) {
                model->anim_time += clip->duration;
            }
        }

        for (Uint32 i = 0; i < model->morph_target_count; i += 1) {
            model->morph_weights[i] = model->model.mesh.morph_targets[i].default_weight;
        }
        if (!ForgeGpuEvaluateProcessedMorphWeights(
                clip,
                model->morph_mesh_node,
                model->anim_time,
                model->looping,
                model->morph_weights,
                model->morph_target_count)) {
            return false;
        }
    }

    delta_size = model->model.mesh.vertex_count * FORGE_GPU_PROCESSED_SCENE_MORPH_DELTA_STRIDE;
    SDL_memset(model->blended_pos_deltas, 0, delta_size);
    SDL_memset(model->blended_nrm_deltas, 0, delta_size);

    for (Uint32 target_index = 0; target_index < model->morph_target_count; target_index += 1) {
        const ForgeGpuProcessedMorphTarget *target = &model->model.mesh.morph_targets[target_index];
        const float weight = model->morph_weights[target_index];

        if (weight > -PROCESSED_SCENE_MORPH_WEIGHT_EPSILON &&
            weight < PROCESSED_SCENE_MORPH_WEIGHT_EPSILON) {
            continue;
        }
        if (target->position_deltas) {
            for (Uint32 vertex_index = 0; vertex_index < model->model.mesh.vertex_count; vertex_index += 1) {
                model->blended_pos_deltas[(size_t)vertex_index * 4u + 0u] += weight * target->position_deltas[(size_t)vertex_index * 3u + 0u];
                model->blended_pos_deltas[(size_t)vertex_index * 4u + 1u] += weight * target->position_deltas[(size_t)vertex_index * 3u + 1u];
                model->blended_pos_deltas[(size_t)vertex_index * 4u + 2u] += weight * target->position_deltas[(size_t)vertex_index * 3u + 2u];
            }
        }
        if (target->normal_deltas) {
            for (Uint32 vertex_index = 0; vertex_index < model->model.mesh.vertex_count; vertex_index += 1) {
                model->blended_nrm_deltas[(size_t)vertex_index * 4u + 0u] += weight * target->normal_deltas[(size_t)vertex_index * 3u + 0u];
                model->blended_nrm_deltas[(size_t)vertex_index * 4u + 1u] += weight * target->normal_deltas[(size_t)vertex_index * 3u + 1u];
                model->blended_nrm_deltas[(size_t)vertex_index * 4u + 2u] += weight * target->normal_deltas[(size_t)vertex_index * 3u + 2u];
            }
        }
    }

    mapped = SDL_MapGPUTransferBuffer(demo->device, model->morph_transfer_buffer, true);
    if (!mapped) {
        return false;
    }
    SDL_memcpy(mapped, model->blended_pos_deltas, delta_size);
    SDL_memcpy((Uint8 *)mapped + delta_size, model->blended_nrm_deltas, delta_size);
    SDL_UnmapGPUTransferBuffer(demo->device, model->morph_transfer_buffer);
    model->pending_morph_delta_upload_size = delta_size;
    return true;
}

bool ForgeGpuProcessedSceneUploadMorphDeltas(
    SDL_GPUCopyPass *copy_pass,
    ForgeGpuProcessedSceneMorphModel *model)
{
    SDL_GPUTransferBufferLocation source;
    SDL_GPUBufferRegion destination;

    if (!model || model->pending_morph_delta_upload_size == 0) {
        return true;
    }

    SDL_zero(source);
    source.transfer_buffer = model->morph_transfer_buffer;

    SDL_zero(destination);
    destination.buffer = model->morph_pos_buffer;
    destination.size = model->pending_morph_delta_upload_size;
    SDL_UploadToGPUBuffer(copy_pass, &source, &destination, true);

    source.offset = model->pending_morph_delta_upload_size;
    destination.buffer = model->morph_nrm_buffer;
    SDL_UploadToGPUBuffer(copy_pass, &source, &destination, true);

    model->pending_morph_delta_upload_size = 0;
    return true;
}

void ForgeGpuProcessedSceneDestroyModel(SDL_GPUDevice *device, ForgeGpuProcessedSceneModel *model)
{
    for (Uint32 i = 0; i < model->texture_cache_count; i += 1) {
        if (model->texture_cache[i].texture) {
            SDL_ReleaseGPUTexture(device, model->texture_cache[i].texture);
        }
    }
    SDL_free(model->submesh_centroids);
    SDL_free(model->material_textures);
    if (model->index_buffer) {
        SDL_ReleaseGPUBuffer(device, model->index_buffer);
    }
    if (model->vertex_buffer) {
        SDL_ReleaseGPUBuffer(device, model->vertex_buffer);
    }
    ForgeGpuFreeProcessedScene(&model->scene);
    ForgeGpuFreeProcessedMaterials(&model->materials);
    ForgeGpuFreeProcessedMesh(&model->mesh);
    SDL_zero(*model);
}

bool ForgeGpuProcessedSceneDrawMorphModel(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    ForgeGpuProcessedSceneRenderer *renderer,
    ForgeGpuProcessedSceneMorphModel *model,
    Mat4 placement,
    Mat4 camera_vp,
    Mat4 light_vp,
    bool shadow_pass)
{
    const ForgeGpuProcessedScenePipelineSet pipelines = {
        renderer->morph_model_pipeline,
        renderer->morph_model_blend_pipeline,
        renderer->morph_model_double_pipeline,
        renderer->morph_model_blend_double_pipeline,
        renderer->morph_shadow_pipeline
    };
    SDL_GPUBuffer *vertex_storage_buffers[2];

    if (!model) {
        return true;
    }

    vertex_storage_buffers[0] = model->morph_pos_buffer;
    vertex_storage_buffers[1] = model->morph_nrm_buffer;
    return draw_model_internal(
        demo, command_buffer, render_pass, renderer, &model->model,
        placement, camera_vp, light_vp, shadow_pass, &pipelines,
        vertex_storage_buffers, 2, 1);
}

void ForgeGpuProcessedSceneDestroySkinnedModel(SDL_GPUDevice *device, ForgeGpuProcessedSceneSkinnedModel *model)
{
    if (!model) {
        return;
    }
    if (model->joint_transfer_buffer) {
        SDL_ReleaseGPUTransferBuffer(device, model->joint_transfer_buffer);
    }
    if (model->joint_buffer) {
        SDL_ReleaseGPUBuffer(device, model->joint_buffer);
    }
    ForgeGpuFreeProcessedAnimation(&model->animation);
    ForgeGpuFreeProcessedSkins(&model->skins);
    ForgeGpuProcessedSceneDestroyModel(device, &model->model);
    SDL_zero(*model);
}

void ForgeGpuProcessedSceneDestroyMorphModel(SDL_GPUDevice *device, ForgeGpuProcessedSceneMorphModel *model)
{
    if (!model) {
        return;
    }
    if (model->morph_transfer_buffer) {
        SDL_ReleaseGPUTransferBuffer(device, model->morph_transfer_buffer);
    }
    if (model->morph_nrm_buffer) {
        SDL_ReleaseGPUBuffer(device, model->morph_nrm_buffer);
    }
    if (model->morph_pos_buffer) {
        SDL_ReleaseGPUBuffer(device, model->morph_pos_buffer);
    }
    SDL_free(model->blended_nrm_deltas);
    SDL_free(model->blended_pos_deltas);
    ForgeGpuFreeProcessedAnimation(&model->animation);
    ForgeGpuProcessedSceneDestroyModel(device, &model->model);
    SDL_zero(*model);
}

void ForgeGpuProcessedSceneRendererDestroy(ForgeGpuDemo *demo, ForgeGpuProcessedSceneRenderer *renderer)
{
    if (renderer->grid_index_buffer) {
        SDL_ReleaseGPUBuffer(demo->device, renderer->grid_index_buffer);
    }
    if (renderer->grid_vertex_buffer) {
        SDL_ReleaseGPUBuffer(demo->device, renderer->grid_vertex_buffer);
    }
    if (renderer->grid_shadow_sampler) {
        SDL_ReleaseGPUSampler(demo->device, renderer->grid_shadow_sampler);
    }
    if (renderer->model_shadow_sampler) {
        SDL_ReleaseGPUSampler(demo->device, renderer->model_shadow_sampler);
    }
    if (renderer->normal_sampler) {
        SDL_ReleaseGPUSampler(demo->device, renderer->normal_sampler);
    }
    if (renderer->material_sampler) {
        SDL_ReleaseGPUSampler(demo->device, renderer->material_sampler);
    }
    if (renderer->main_depth) {
        SDL_ReleaseGPUTexture(demo->device, renderer->main_depth);
    }
    if (renderer->shadow_depth) {
        SDL_ReleaseGPUTexture(demo->device, renderer->shadow_depth);
    }
    if (renderer->flat_normal_texture) {
        SDL_ReleaseGPUTexture(demo->device, renderer->flat_normal_texture);
    }
    if (renderer->black_texture) {
        SDL_ReleaseGPUTexture(demo->device, renderer->black_texture);
    }
    if (renderer->white_texture) {
        SDL_ReleaseGPUTexture(demo->device, renderer->white_texture);
    }
    if (renderer->sky_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, renderer->sky_pipeline);
    }
    if (renderer->grid_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, renderer->grid_pipeline);
    }
    if (renderer->shadow_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, renderer->shadow_pipeline);
    }
    release_morph_pipelines(demo, renderer);
    release_skinned_pipelines(demo, renderer);
    if (renderer->model_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, renderer->model_pipeline);
    }
    if (renderer->model_blend_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, renderer->model_blend_pipeline);
    }
    if (renderer->model_double_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, renderer->model_double_pipeline);
    }
    if (renderer->model_blend_double_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, renderer->model_blend_double_pipeline);
    }
    SDL_zero(*renderer);
}
