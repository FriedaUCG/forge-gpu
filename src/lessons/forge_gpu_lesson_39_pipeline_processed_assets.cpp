#include "forge_gpu_lessons.h"

#include "forge_gpu_browser_status.h"
#include "forge_gpu_camera.h"
#include "forge_gpu_forward_scene.h"
#include "forge_gpu_gpu_helpers.h"
#include "forge_gpu_lesson_common.h"
#include "forge_gpu_math.h"
#include "forge_gpu_processed_assets.h"
#include "forge_gpu_shader_layouts.h"
#include "shaders/generated/forge_gpu_lesson_39_shaders.h"
#include "imgui.h"

#include <stddef.h>

#define LESSON39_SHADOW_MAP_SIZE 2048u
#define LESSON39_DEPTH_FORMAT SDL_GPU_TEXTUREFORMAT_D32_FLOAT
#define LESSON39_FOV_DEGREES 60.0f
#define LESSON39_NEAR_PLANE 0.1f
#define LESSON39_FAR_PLANE 200.0f
#define LESSON39_MOVE_SPEED 4.0f
#define LESSON39_MOUSE_SENSITIVITY 0.003f
#define LESSON39_PITCH_CLAMP 1.5f
#define LESSON39_CAM_START_X 0.25f
#define LESSON39_CAM_START_Y 0.25f
#define LESSON39_CAM_START_Z 0.8f
#define LESSON39_CAM_START_YAW_DEG 0.0f
#define LESSON39_CAM_START_PITCH_DEG -10.0f
#define LESSON39_LIGHT_DIR_X 0.6f
#define LESSON39_LIGHT_DIR_Y 1.0f
#define LESSON39_LIGHT_DIR_Z 0.4f
#define LESSON39_SCENE_AMBIENT 0.12f
#define LESSON39_SCENE_SHININESS 64.0f
#define LESSON39_SCENE_SPECULAR_STRENGTH 0.4f
#define LESSON39_GRID_HALF_SIZE 20.0f
#define LESSON39_GRID_SPACING 0.1f
#define LESSON39_GRID_LINE_WIDTH 0.02f
#define LESSON39_GRID_FADE_DISTANCE 15.0f
#define LESSON39_GRID_AMBIENT 0.15f
#define LESSON39_CLEAR_R 0.02f
#define LESSON39_CLEAR_G 0.02f
#define LESSON39_CLEAR_B 0.03f
#define LESSON39_BOX_OFFSET_X 0.5f
#define LESSON39_BOX_SCALE 0.15f
#define LESSON39_WATER_Y_OFFSET 0.13f
#define LESSON39_BOX_Y_OFFSET 0.075f
#define LESSON39_DIVIDER_WIDTH 2
#define LESSON39_LOD_DIST_0 3.0f
#define LESSON39_LOD_DIST_1 8.0f

enum Lesson39Mode
{
    LESSON39_MODE_PIPELINE = 0,
    LESSON39_MODE_RAW = 1,
    LESSON39_MODE_SPLIT = 2
};

typedef struct Lesson39SceneVertUniforms
{
    Mat4 mvp;
    Mat4 model;
    Mat4 light_vp;
} Lesson39SceneVertUniforms;

typedef struct Lesson39SceneFragUniforms
{
    float light_dir[4];
    float eye_pos[4];
    float shadow_texel;
    float shininess;
    float ambient;
    float specular_strength;
} Lesson39SceneFragUniforms;

typedef struct Lesson39ShadowUniforms
{
    Mat4 mvp;
} Lesson39ShadowUniforms;

typedef struct Lesson39GridVertUniforms
{
    Mat4 vp;
} Lesson39GridVertUniforms;

typedef struct Lesson39GridFragUniforms
{
    float line_color[4];
    float bg_color[4];
    float light_dir[4];
    float eye_pos[4];
    Mat4 light_vp;
    float grid_spacing;
    float line_width;
    float fade_distance;
    float ambient;
    float shininess;
    float specular_strength;
    float shadow_texel;
    float pad0;
} Lesson39GridFragUniforms;

typedef struct Lesson39ProcessedModel
{
    ForgeGpuProcessedMesh mesh;
    ForgeGpuProcessedMaterialSet materials;
    SDL_GPUBuffer *vertex_buffer;
    SDL_GPUBuffer *index_buffer;
    SDL_GPUTexture *diffuse_texture;
    SDL_GPUTexture *normal_texture;
    Uint32 current_lod;
} Lesson39ProcessedModel;

typedef struct Lesson39RawModel
{
    SDL_GPUBuffer *vertex_buffer;
    SDL_GPUBuffer *index_buffer;
    SDL_GPUTexture *diffuse_texture;
    Uint32 index_count;
    Uint32 vertex_count;
} Lesson39RawModel;

typedef struct Lesson39State
{
    SDL_GPUGraphicsPipeline *pipeline_scene_processed;
    SDL_GPUGraphicsPipeline *pipeline_scene_raw;
    SDL_GPUGraphicsPipeline *pipeline_shadow_processed;
    SDL_GPUGraphicsPipeline *pipeline_shadow_raw;
    SDL_GPUGraphicsPipeline *pipeline_sky;
    SDL_GPUGraphicsPipeline *pipeline_grid;
    SDL_GPUTexture *shadow_depth_texture;
    SDL_GPUTexture *main_depth_texture;
    Uint32 main_depth_width;
    Uint32 main_depth_height;
    SDL_GPUTexture *white_texture;
    SDL_GPUTexture *flat_normal_texture;
    SDL_GPUSampler *diffuse_sampler;
    SDL_GPUSampler *normal_sampler;
    SDL_GPUSampler *shadow_sampler;
    SDL_GPUBuffer *grid_vertex_buffer;
    SDL_GPUBuffer *grid_index_buffer;
    Lesson39ProcessedModel pipe_water;
    Lesson39ProcessedModel pipe_box;
    Lesson39RawModel raw_water;
    Lesson39RawModel raw_box;
    int render_mode;
    int lod_override;
    bool lod_auto;
    bool shadow_pass_rendered;
    bool main_pass_rendered;
    bool pipeline_path_drawn;
    bool raw_path_drawn;
    bool split_rendered;
    bool processed_assets_loaded;
} Lesson39State;

static_assert(sizeof(Lesson39SceneVertUniforms) == 192, "lesson 39 scene vertex uniform size must match HLSL layout");
static_assert(sizeof(Lesson39SceneFragUniforms) == 48, "lesson 39 scene fragment uniform size must match HLSL layout");
static_assert(sizeof(Lesson39ShadowUniforms) == 64, "lesson 39 shadow uniform size must match HLSL layout");
static_assert(sizeof(Lesson39GridVertUniforms) == 64, "lesson 39 grid vertex uniform size must match HLSL layout");
static_assert(sizeof(Lesson39GridFragUniforms) == 160, "lesson 39 grid fragment uniform size must match HLSL layout");

static Lesson39State *lesson39_state(ForgeGpuDemo *demo)
{
    return (Lesson39State *)demo->lesson.private_state;
}

static float lesson39_vec3_length(Vec3 value)
{
    return SDL_sqrtf(vec3_dot(value, value));
}

static Vec3 lesson39_light_dir(void)
{
    Vec3 light = { LESSON39_LIGHT_DIR_X, LESSON39_LIGHT_DIR_Y, LESSON39_LIGHT_DIR_Z };
    return vec3_normalize(light);
}

static Uint32 lesson39_select_lod(float distance, Uint32 lod_count)
{
    if (lod_count <= 1 || distance < LESSON39_LOD_DIST_0) {
        return 0;
    }
    if (lod_count <= 2 || distance < LESSON39_LOD_DIST_1) {
        return 1;
    }
    return 2;
}

static bool lesson39_processed_texture_path(
    ForgeGpuDemo *demo,
    const char *source_path,
    char *path,
    size_t path_size)
{
    char relative_path[FORGE_GPU_MAX_PATH];
    const char *basename = ForgeGpuProcessedBasename(source_path);
    int written;

    if (!basename || basename[0] == '\0') {
        return false;
    }
    written = SDL_snprintf(relative_path, sizeof(relative_path), "processed/39-pipeline-processed-assets/%s", basename);
    if (written <= 0 || (size_t)written >= sizeof(relative_path)) {
        SDL_SetError("lesson 39 processed texture path overflow");
        return false;
    }
    return ForgeGpuJoinAssetPath(demo, relative_path, path, path_size);
}

static SDL_GPUTexture *lesson39_load_required_texture(
    ForgeGpuDemo *demo,
    const char *path,
    SDL_GPUTextureFormat format)
{
    Uint32 width = 0;
    Uint32 height = 0;

    if (!ForgeGpuValidateProcessedTextureSidecar(path, &width, &height)) {
        return nullptr;
    }
    return ForgeGpuLoadRgbaTexturePathWithFormatAndSize(demo, path, true, format, width, height);
}

static bool lesson39_load_processed_model(
    ForgeGpuDemo *demo,
    const char *mesh_relative,
    const char *material_relative,
    Lesson39ProcessedModel *model,
    SDL_GPUTexture *white_texture,
    SDL_GPUTexture *flat_normal_texture)
{
    char path[FORGE_GPU_MAX_PATH];
    char texture_path[FORGE_GPU_MAX_PATH];
    size_t vertex_size;
    size_t index_size;
    const ForgeGpuProcessedMaterial *material;

    SDL_zero(*model);
    model->diffuse_texture = white_texture;
    model->normal_texture = flat_normal_texture;

    if (!ForgeGpuJoinAssetPath(demo, mesh_relative, path, sizeof(path)) ||
        !ForgeGpuLoadProcessedMesh(path, &model->mesh)) {
        return false;
    }
    if (!ForgeGpuProcessedMeshHasTangents(&model->mesh)) {
        SDL_SetError("lesson 39 processed mesh is missing tangent data");
        return false;
    }
    vertex_size = (size_t)model->mesh.vertex_count * model->mesh.vertex_stride;
    index_size = (size_t)model->mesh.total_index_count * sizeof(Uint32);
    if (vertex_size > SDL_MAX_UINT32 || index_size > SDL_MAX_UINT32) {
        SDL_SetError("lesson 39 processed mesh is too large for demo upload");
        return false;
    }
    model->vertex_buffer = ForgeGpuCreateBufferWithData(demo->device, SDL_GPU_BUFFERUSAGE_VERTEX, model->mesh.vertices, (Uint32)vertex_size);
    model->index_buffer = ForgeGpuCreateBufferWithData(demo->device, SDL_GPU_BUFFERUSAGE_INDEX, model->mesh.indices, (Uint32)index_size);
    if (!model->vertex_buffer || !model->index_buffer) {
        return false;
    }

    if (!ForgeGpuJoinAssetPath(demo, material_relative, path, sizeof(path)) ||
        !ForgeGpuLoadProcessedMaterials(path, &model->materials) ||
        model->materials.material_count == 0) {
        return false;
    }
    material = &model->materials.materials[0];
    if (material->base_color_texture[0] != '\0') {
        if (!lesson39_processed_texture_path(demo, material->base_color_texture, texture_path, sizeof(texture_path))) {
            return false;
        }
        model->diffuse_texture = lesson39_load_required_texture(demo, texture_path, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB);
        if (!model->diffuse_texture) {
            return false;
        }
    }
    if (material->normal_texture[0] != '\0') {
        if (!lesson39_processed_texture_path(demo, material->normal_texture, texture_path, sizeof(texture_path))) {
            return false;
        }
        model->normal_texture = lesson39_load_required_texture(demo, texture_path, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
        if (!model->normal_texture) {
            return false;
        }
    }
    return true;
}

static bool lesson39_upload_raw_primitive(
    ForgeGpuDemo *demo,
    const ForgeGpuScenePrimitive *primitive,
    Lesson39RawModel *model)
{
    Uint32 *indices32 = nullptr;
    const void *index_data = primitive->indices;
    size_t vertex_size = (size_t)primitive->vertex_count * sizeof(ForgeGpuMeshVertex);
    size_t index_size = (size_t)primitive->index_count * sizeof(Uint32);

    if (primitive->vertex_count == 0 || primitive->index_count == 0 || !primitive->indices ||
        vertex_size > SDL_MAX_UINT32 || index_size > SDL_MAX_UINT32) {
        SDL_SetError("lesson 39 raw primitive is invalid");
        return false;
    }

    if (primitive->index_stride == sizeof(Uint16)) {
        const Uint16 *src = (const Uint16 *)primitive->indices;
        indices32 = (Uint32 *)SDL_malloc(index_size);
        if (!indices32) {
            SDL_SetError("lesson 39 raw index conversion allocation failed");
            return false;
        }
        for (Uint32 i = 0; i < primitive->index_count; i += 1) {
            indices32[i] = src[i];
        }
        index_data = indices32;
    } else if (primitive->index_stride != sizeof(Uint32)) {
        SDL_SetError("lesson 39 raw primitive uses unsupported index stride");
        return false;
    }

    model->vertex_buffer = ForgeGpuCreateBufferWithData(demo->device, SDL_GPU_BUFFERUSAGE_VERTEX, primitive->vertices, (Uint32)vertex_size);
    model->index_buffer = ForgeGpuCreateBufferWithData(demo->device, SDL_GPU_BUFFERUSAGE_INDEX, index_data, (Uint32)index_size);
    SDL_free(indices32);
    if (!model->vertex_buffer || !model->index_buffer) {
        return false;
    }
    model->vertex_count = primitive->vertex_count;
    model->index_count = primitive->index_count;
    return true;
}

static bool lesson39_load_raw_model(
    ForgeGpuDemo *demo,
    const char *scene_relative,
    const char *texture_relative,
    Lesson39RawModel *model)
{
    char path[FORGE_GPU_MAX_PATH];
    ForgeGpuLoadedScene scene;
    bool ok = false;

    SDL_zero(*model);
    SDL_zero(scene);
    if (!ForgeGpuJoinAssetPath(demo, scene_relative, path, sizeof(path)) ||
        !ForgeGpuLoadGltfSceneWithRequirements(path, nullptr, &scene) ||
        scene.primitive_count <= 0) {
        goto done;
    }
    if (!lesson39_upload_raw_primitive(demo, &scene.primitives[0], model)) {
        goto done;
    }
    if (!ForgeGpuJoinAssetPath(demo, texture_relative, path, sizeof(path))) {
        goto done;
    }
    model->diffuse_texture = ForgeGpuLoadRgbaTexturePathWithFormat(
        demo, path, true, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB);
    if (!model->diffuse_texture) {
        goto done;
    }
    ok = true;

done:
    ForgeGpuFreeLoadedScene(&scene);
    return ok;
}

static bool lesson39_create_shadow_sampler(ForgeGpuDemo *demo, Lesson39State *state)
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
    state->shadow_sampler = SDL_CreateGPUSampler(demo->device, &sampler_info);
    return state->shadow_sampler != nullptr;
}

static void lesson39_release_shader(SDL_GPUDevice *device, SDL_GPUShader **shader)
{
    if (*shader) {
        SDL_ReleaseGPUShader(device, *shader);
        *shader = nullptr;
    }
}

static void lesson39_fill_raw_vertex_input(
    SDL_GPUVertexBufferDescription *vertex_buffer,
    SDL_GPUVertexAttribute attributes[3])
{
    SDL_zero(*vertex_buffer);
    vertex_buffer->slot = 0;
    vertex_buffer->pitch = sizeof(ForgeGpuMeshVertex);
    vertex_buffer->input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

    SDL_memset(attributes, 0, sizeof(SDL_GPUVertexAttribute) * 3);
    attributes[0].location = 0;
    attributes[0].buffer_slot = 0;
    attributes[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    attributes[0].offset = offsetof(ForgeGpuMeshVertex, position);
    attributes[1].location = 1;
    attributes[1].buffer_slot = 0;
    attributes[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    attributes[1].offset = offsetof(ForgeGpuMeshVertex, normal);
    attributes[2].location = 2;
    attributes[2].buffer_slot = 0;
    attributes[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    attributes[2].offset = offsetof(ForgeGpuMeshVertex, uv);
}

static void lesson39_fill_processed_vertex_input(
    SDL_GPUVertexBufferDescription *vertex_buffer,
    SDL_GPUVertexAttribute attributes[4])
{
    lesson39_fill_raw_vertex_input(vertex_buffer, attributes);
    vertex_buffer->pitch = FORGE_GPU_PROCESSED_VERTEX_STRIDE_TANGENTS;
    attributes[3].location = 3;
    attributes[3].buffer_slot = 0;
    attributes[3].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
    attributes[3].offset = 32;
}

static bool lesson39_create_pipelines(ForgeGpuDemo *demo, Lesson39State *state)
{
    SDL_GPUShader *pipe_vs = nullptr;
    SDL_GPUShader *pipe_fs = nullptr;
    SDL_GPUShader *raw_vs = nullptr;
    SDL_GPUShader *raw_fs = nullptr;
    SDL_GPUShader *shadow_vs = nullptr;
    SDL_GPUShader *shadow_fs = nullptr;
    SDL_GPUShader *sky_vs = nullptr;
    SDL_GPUShader *sky_fs = nullptr;
    SDL_GPUShader *grid_vs = nullptr;
    SDL_GPUShader *grid_fs = nullptr;
    SDL_GPUColorTargetDescription color_target;
    SDL_GPUVertexBufferDescription processed_vb;
    SDL_GPUVertexAttribute processed_attrs[4];
    SDL_GPUVertexBufferDescription raw_vb;
    SDL_GPUVertexAttribute raw_attrs[3];
    SDL_GPUVertexBufferDescription grid_vb;
    SDL_GPUVertexAttribute grid_attr;
    bool ok = false;

    pipe_vs = ForgeGpuCreateShader(demo->device, SDL_GPU_SHADERSTAGE_VERTEX,
        lesson39_scene_pipeline_vert_wgsl, lesson39_scene_pipeline_vert_wgsl_size,
        lesson39_scene_pipeline_vert_msl, lesson39_scene_pipeline_vert_msl_size,
        0, 0, 0, 1);
    pipe_fs = ForgeGpuCreateShaderWithResourceLayout(demo->device,
        lesson39_scene_pipeline_frag_wgsl, lesson39_scene_pipeline_frag_wgsl_size,
        lesson39_scene_pipeline_frag_msl, lesson39_scene_pipeline_frag_msl_size,
        ForgeGpuShaderLayout_lesson39_scene_pipeline_frag());
    raw_vs = ForgeGpuCreateShader(demo->device, SDL_GPU_SHADERSTAGE_VERTEX,
        lesson39_scene_raw_vert_wgsl, lesson39_scene_raw_vert_wgsl_size,
        lesson39_scene_raw_vert_msl, lesson39_scene_raw_vert_msl_size,
        0, 0, 0, 1);
    raw_fs = ForgeGpuCreateShaderWithResourceLayout(demo->device,
        lesson39_scene_raw_frag_wgsl, lesson39_scene_raw_frag_wgsl_size,
        lesson39_scene_raw_frag_msl, lesson39_scene_raw_frag_msl_size,
        ForgeGpuShaderLayout_lesson39_scene_raw_frag());
    shadow_vs = ForgeGpuCreateShader(demo->device, SDL_GPU_SHADERSTAGE_VERTEX,
        lesson39_shadow_vert_wgsl, lesson39_shadow_vert_wgsl_size,
        lesson39_shadow_vert_msl, lesson39_shadow_vert_msl_size,
        0, 0, 0, 1);
    shadow_fs = ForgeGpuCreateShader(demo->device, SDL_GPU_SHADERSTAGE_FRAGMENT,
        lesson39_shadow_frag_wgsl, lesson39_shadow_frag_wgsl_size,
        lesson39_shadow_frag_msl, lesson39_shadow_frag_msl_size,
        0, 0, 0, 0);
    sky_vs = ForgeGpuCreateShader(demo->device, SDL_GPU_SHADERSTAGE_VERTEX,
        lesson39_sky_vert_wgsl, lesson39_sky_vert_wgsl_size,
        lesson39_sky_vert_msl, lesson39_sky_vert_msl_size,
        0, 0, 0, 0);
    sky_fs = ForgeGpuCreateShader(demo->device, SDL_GPU_SHADERSTAGE_FRAGMENT,
        lesson39_sky_frag_wgsl, lesson39_sky_frag_wgsl_size,
        lesson39_sky_frag_msl, lesson39_sky_frag_msl_size,
        0, 0, 0, 0);
    grid_vs = ForgeGpuCreateShader(demo->device, SDL_GPU_SHADERSTAGE_VERTEX,
        lesson39_grid_vert_wgsl, lesson39_grid_vert_wgsl_size,
        lesson39_grid_vert_msl, lesson39_grid_vert_msl_size,
        0, 0, 0, 1);
    grid_fs = ForgeGpuCreateShaderWithResourceLayout(demo->device,
        lesson39_grid_frag_wgsl, lesson39_grid_frag_wgsl_size,
        lesson39_grid_frag_msl, lesson39_grid_frag_msl_size,
        ForgeGpuShaderLayout_lesson39_grid_frag());
    if (!pipe_vs || !pipe_fs || !raw_vs || !raw_fs || !shadow_vs || !shadow_fs ||
        !sky_vs || !sky_fs || !grid_vs || !grid_fs) {
        goto done;
    }

    SDL_zero(color_target);
    color_target.format = demo->color_format;

    lesson39_fill_processed_vertex_input(&processed_vb, processed_attrs);
    lesson39_fill_raw_vertex_input(&raw_vb, raw_attrs);

    state->pipeline_scene_processed = ForgeGpuCreateLessonGraphicsPipelineWithColorTargetsAndDepthCompare(
        demo, pipe_vs, pipe_fs, SDL_GPU_PRIMITIVETYPE_TRIANGLELIST, &color_target, 1,
        &processed_vb, 1, processed_attrs, 4,
        true, LESSON39_DEPTH_FORMAT, true, true, SDL_GPU_COMPAREOP_LESS,
        SDL_GPU_CULLMODE_BACK, 0.0f, 0.0f);
    state->pipeline_scene_raw = ForgeGpuCreateLessonGraphicsPipelineWithColorTargetsAndDepthCompare(
        demo, raw_vs, raw_fs, SDL_GPU_PRIMITIVETYPE_TRIANGLELIST, &color_target, 1,
        &raw_vb, 1, raw_attrs, 3,
        true, LESSON39_DEPTH_FORMAT, true, true, SDL_GPU_COMPAREOP_LESS,
        SDL_GPU_CULLMODE_BACK, 0.0f, 0.0f);
    state->pipeline_shadow_processed = ForgeGpuCreateLessonGraphicsPipelineWithColorTargetsAndDepthCompare(
        demo, shadow_vs, shadow_fs, SDL_GPU_PRIMITIVETYPE_TRIANGLELIST, nullptr, 0,
        &processed_vb, 1, processed_attrs, 1,
        true, LESSON39_DEPTH_FORMAT, true, true, SDL_GPU_COMPAREOP_LESS,
        SDL_GPU_CULLMODE_BACK, 2.0f, 2.0f);
    state->pipeline_shadow_raw = ForgeGpuCreateLessonGraphicsPipelineWithColorTargetsAndDepthCompare(
        demo, shadow_vs, shadow_fs, SDL_GPU_PRIMITIVETYPE_TRIANGLELIST, nullptr, 0,
        &raw_vb, 1, raw_attrs, 1,
        true, LESSON39_DEPTH_FORMAT, true, true, SDL_GPU_COMPAREOP_LESS,
        SDL_GPU_CULLMODE_BACK, 2.0f, 2.0f);
    state->pipeline_sky = ForgeGpuCreateLessonGraphicsPipelineWithColorTargetsAndDepthCompare(
        demo, sky_vs, sky_fs, SDL_GPU_PRIMITIVETYPE_TRIANGLELIST, &color_target, 1,
        nullptr, 0, nullptr, 0,
        true, LESSON39_DEPTH_FORMAT, true, false, SDL_GPU_COMPAREOP_LESS_OR_EQUAL,
        SDL_GPU_CULLMODE_NONE, 0.0f, 0.0f);

    SDL_zero(grid_vb);
    grid_vb.slot = 0;
    grid_vb.pitch = sizeof(GridVertex);
    grid_vb.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    SDL_zero(grid_attr);
    grid_attr.location = 0;
    grid_attr.buffer_slot = 0;
    grid_attr.format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    grid_attr.offset = 0;
    state->pipeline_grid = ForgeGpuCreateLessonGraphicsPipelineWithColorTargetsAndDepthCompare(
        demo, grid_vs, grid_fs, SDL_GPU_PRIMITIVETYPE_TRIANGLELIST, &color_target, 1,
        &grid_vb, 1, &grid_attr, 1,
        true, LESSON39_DEPTH_FORMAT, true, false, SDL_GPU_COMPAREOP_LESS,
        SDL_GPU_CULLMODE_NONE, 0.0f, 0.0f);

    ok = state->pipeline_scene_processed && state->pipeline_scene_raw &&
        state->pipeline_shadow_processed && state->pipeline_shadow_raw &&
        state->pipeline_sky && state->pipeline_grid;

done:
    lesson39_release_shader(demo->device, &grid_fs);
    lesson39_release_shader(demo->device, &grid_vs);
    lesson39_release_shader(demo->device, &sky_fs);
    lesson39_release_shader(demo->device, &sky_vs);
    lesson39_release_shader(demo->device, &shadow_fs);
    lesson39_release_shader(demo->device, &shadow_vs);
    lesson39_release_shader(demo->device, &raw_fs);
    lesson39_release_shader(demo->device, &raw_vs);
    lesson39_release_shader(demo->device, &pipe_fs);
    lesson39_release_shader(demo->device, &pipe_vs);
    return ok;
}

static void lesson39_bind_buffer_pair(
    SDL_GPURenderPass *render_pass,
    SDL_GPUBuffer *vertex_buffer,
    SDL_GPUBuffer *index_buffer)
{
    SDL_GPUBufferBinding vertex_binding;
    SDL_GPUBufferBinding index_binding;

    SDL_zero(vertex_binding);
    vertex_binding.buffer = vertex_buffer;
    SDL_BindGPUVertexBuffers(render_pass, 0, &vertex_binding, 1);

    SDL_zero(index_binding);
    index_binding.buffer = index_buffer;
    SDL_BindGPUIndexBuffer(render_pass, &index_binding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
}

static void lesson39_push_scene_fragment_uniforms(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    Vec3 light_dir)
{
    Lesson39SceneFragUniforms uniforms;

    SDL_zero(uniforms);
    uniforms.light_dir[0] = light_dir.x;
    uniforms.light_dir[1] = light_dir.y;
    uniforms.light_dir[2] = light_dir.z;
    uniforms.eye_pos[0] = demo->lesson.camera_position.x;
    uniforms.eye_pos[1] = demo->lesson.camera_position.y;
    uniforms.eye_pos[2] = demo->lesson.camera_position.z;
    uniforms.shadow_texel = 1.0f / (float)LESSON39_SHADOW_MAP_SIZE;
    uniforms.shininess = LESSON39_SCENE_SHININESS;
    uniforms.ambient = LESSON39_SCENE_AMBIENT;
    uniforms.specular_strength = LESSON39_SCENE_SPECULAR_STRENGTH;
    SDL_PushGPUFragmentUniformData(command_buffer, 0, &uniforms, sizeof(uniforms));
}

static void lesson39_draw_processed_model(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    Lesson39State *state,
    Lesson39ProcessedModel *model,
    Mat4 model_matrix,
    Mat4 camera_vp,
    Mat4 light_vp,
    Vec3 light_dir,
    bool shadow_pass)
{
    Uint32 lod;
    Uint32 index_count;
    Uint32 first_index;

    if (!model->vertex_buffer || !model->index_buffer) {
        return;
    }
    lod = model->current_lod < model->mesh.lod_count ? model->current_lod : 0;
    index_count = ForgeGpuProcessedMeshLodIndexCount(&model->mesh, lod);
    first_index = ForgeGpuProcessedMeshLodFirstIndex(&model->mesh, lod);
    if (index_count == 0) {
        return;
    }

    if (shadow_pass) {
        Lesson39ShadowUniforms uniforms;

        SDL_BindGPUGraphicsPipeline(render_pass, state->pipeline_shadow_processed);
        uniforms.mvp = mat4_multiply(light_vp, model_matrix);
        SDL_PushGPUVertexUniformData(command_buffer, 0, &uniforms, sizeof(uniforms));
    } else {
        Lesson39SceneVertUniforms vertex_uniforms;
        SDL_GPUTextureSamplerBinding bindings[3];

        SDL_BindGPUGraphicsPipeline(render_pass, state->pipeline_scene_processed);
        vertex_uniforms.mvp = mat4_multiply(camera_vp, model_matrix);
        vertex_uniforms.model = model_matrix;
        vertex_uniforms.light_vp = light_vp;
        SDL_PushGPUVertexUniformData(command_buffer, 0, &vertex_uniforms, sizeof(vertex_uniforms));
        lesson39_push_scene_fragment_uniforms(demo, command_buffer, light_dir);

        SDL_zeroa(bindings);
        bindings[0].texture = model->diffuse_texture;
        bindings[0].sampler = state->diffuse_sampler;
        bindings[1].texture = model->normal_texture;
        bindings[1].sampler = state->normal_sampler;
        bindings[2].texture = state->shadow_depth_texture;
        bindings[2].sampler = state->shadow_sampler;
        SDL_BindGPUFragmentSamplers(render_pass, 0, bindings, SDL_arraysize(bindings));
        state->pipeline_path_drawn = true;
    }

    lesson39_bind_buffer_pair(render_pass, model->vertex_buffer, model->index_buffer);
    SDL_DrawGPUIndexedPrimitives(render_pass, index_count, 1, first_index, 0, 0);
}

static void lesson39_draw_raw_model(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    Lesson39State *state,
    Lesson39RawModel *model,
    Mat4 model_matrix,
    Mat4 camera_vp,
    Mat4 light_vp,
    Vec3 light_dir,
    bool shadow_pass)
{
    if (!model->vertex_buffer || !model->index_buffer || model->index_count == 0) {
        return;
    }

    if (shadow_pass) {
        Lesson39ShadowUniforms uniforms;

        SDL_BindGPUGraphicsPipeline(render_pass, state->pipeline_shadow_raw);
        uniforms.mvp = mat4_multiply(light_vp, model_matrix);
        SDL_PushGPUVertexUniformData(command_buffer, 0, &uniforms, sizeof(uniforms));
    } else {
        Lesson39SceneVertUniforms vertex_uniforms;
        SDL_GPUTextureSamplerBinding bindings[2];

        SDL_BindGPUGraphicsPipeline(render_pass, state->pipeline_scene_raw);
        vertex_uniforms.mvp = mat4_multiply(camera_vp, model_matrix);
        vertex_uniforms.model = model_matrix;
        vertex_uniforms.light_vp = light_vp;
        SDL_PushGPUVertexUniformData(command_buffer, 0, &vertex_uniforms, sizeof(vertex_uniforms));
        lesson39_push_scene_fragment_uniforms(demo, command_buffer, light_dir);

        SDL_zeroa(bindings);
        bindings[0].texture = model->diffuse_texture;
        bindings[0].sampler = state->diffuse_sampler;
        bindings[1].texture = state->shadow_depth_texture;
        bindings[1].sampler = state->shadow_sampler;
        SDL_BindGPUFragmentSamplers(render_pass, 0, bindings, SDL_arraysize(bindings));
        state->raw_path_drawn = true;
    }

    lesson39_bind_buffer_pair(render_pass, model->vertex_buffer, model->index_buffer);
    SDL_DrawGPUIndexedPrimitives(render_pass, model->index_count, 1, 0, 0, 0);
}

static void lesson39_draw_sky_and_grid(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    Lesson39State *state,
    Mat4 camera_vp,
    Mat4 light_vp,
    Vec3 light_dir,
    const SDL_GPUViewport *viewport,
    const SDL_Rect *scissor)
{
    SDL_GPUBufferBinding vertex_binding;
    SDL_GPUBufferBinding index_binding;
    SDL_GPUTextureSamplerBinding shadow_binding;
    Lesson39GridVertUniforms vertex_uniforms;
    Lesson39GridFragUniforms fragment_uniforms;

    SDL_SetGPUViewport(render_pass, viewport);
    SDL_SetGPUScissor(render_pass, scissor);

    SDL_BindGPUGraphicsPipeline(render_pass, state->pipeline_sky);
    SDL_DrawGPUPrimitives(render_pass, 3, 1, 0, 0);

    SDL_BindGPUGraphicsPipeline(render_pass, state->pipeline_grid);
    vertex_uniforms.vp = camera_vp;
    SDL_PushGPUVertexUniformData(command_buffer, 0, &vertex_uniforms, sizeof(vertex_uniforms));

    SDL_zero(fragment_uniforms);
    fragment_uniforms.line_color[0] = 0.068f;
    fragment_uniforms.line_color[1] = 0.534f;
    fragment_uniforms.line_color[2] = 0.932f;
    fragment_uniforms.line_color[3] = 1.0f;
    fragment_uniforms.bg_color[0] = 0.014f;
    fragment_uniforms.bg_color[1] = 0.014f;
    fragment_uniforms.bg_color[2] = 0.045f;
    fragment_uniforms.bg_color[3] = 0.5f;
    fragment_uniforms.light_dir[0] = light_dir.x;
    fragment_uniforms.light_dir[1] = light_dir.y;
    fragment_uniforms.light_dir[2] = light_dir.z;
    fragment_uniforms.eye_pos[0] = demo->lesson.camera_position.x;
    fragment_uniforms.eye_pos[1] = demo->lesson.camera_position.y;
    fragment_uniforms.eye_pos[2] = demo->lesson.camera_position.z;
    fragment_uniforms.light_vp = light_vp;
    fragment_uniforms.grid_spacing = LESSON39_GRID_SPACING;
    fragment_uniforms.line_width = LESSON39_GRID_LINE_WIDTH;
    fragment_uniforms.fade_distance = LESSON39_GRID_FADE_DISTANCE;
    fragment_uniforms.ambient = LESSON39_GRID_AMBIENT;
    fragment_uniforms.shininess = 32.0f;
    fragment_uniforms.specular_strength = 0.2f;
    fragment_uniforms.shadow_texel = 1.0f / (float)LESSON39_SHADOW_MAP_SIZE;
    SDL_PushGPUFragmentUniformData(command_buffer, 0, &fragment_uniforms, sizeof(fragment_uniforms));

    SDL_zero(shadow_binding);
    shadow_binding.texture = state->shadow_depth_texture;
    shadow_binding.sampler = state->shadow_sampler;
    SDL_BindGPUFragmentSamplers(render_pass, 0, &shadow_binding, 1);

    SDL_zero(vertex_binding);
    vertex_binding.buffer = state->grid_vertex_buffer;
    SDL_BindGPUVertexBuffers(render_pass, 0, &vertex_binding, 1);

    SDL_zero(index_binding);
    index_binding.buffer = state->grid_index_buffer;
    SDL_BindGPUIndexBuffer(render_pass, &index_binding, SDL_GPU_INDEXELEMENTSIZE_16BIT);
    SDL_DrawGPUIndexedPrimitives(render_pass, 6, 1, 0, 0, 0);
}

static void lesson39_update_lod(ForgeGpuDemo *demo, Lesson39State *state)
{
    const float distance = lesson39_vec3_length(demo->lesson.camera_position);

    if (state->lod_auto) {
        state->pipe_water.current_lod = lesson39_select_lod(distance, state->pipe_water.mesh.lod_count);
    } else if (state->lod_override >= 0) {
        state->pipe_water.current_lod = (Uint32)state->lod_override;
        if (state->pipe_water.current_lod >= state->pipe_water.mesh.lod_count) {
            state->pipe_water.current_lod = state->pipe_water.mesh.lod_count - 1;
        }
    }
    state->pipe_box.current_lod = 0;
}

static bool lesson39_run_shadow_pass(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    Lesson39State *state,
    Mat4 light_vp,
    Mat4 water_model,
    Mat4 box_model)
{
    SDL_GPUDepthStencilTargetInfo depth_target;
    SDL_GPURenderPass *render_pass;
    SDL_GPUViewport viewport;
    Vec3 light_dir = lesson39_light_dir();

    SDL_zero(depth_target);
    depth_target.texture = state->shadow_depth_texture;
    depth_target.load_op = SDL_GPU_LOADOP_CLEAR;
    depth_target.store_op = SDL_GPU_STOREOP_STORE;
    depth_target.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
    depth_target.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
    depth_target.clear_depth = 1.0f;
    depth_target.cycle = true;

    render_pass = SDL_BeginGPURenderPass(command_buffer, nullptr, 0, &depth_target);
    if (!render_pass) {
        return false;
    }
    SDL_zero(viewport);
    viewport.w = (float)LESSON39_SHADOW_MAP_SIZE;
    viewport.h = (float)LESSON39_SHADOW_MAP_SIZE;
    viewport.max_depth = 1.0f;
    SDL_SetGPUViewport(render_pass, &viewport);

    lesson39_draw_processed_model(demo, command_buffer, render_pass, state, &state->pipe_water, water_model, light_vp, light_vp, light_dir, true);
    lesson39_draw_processed_model(demo, command_buffer, render_pass, state, &state->pipe_box, box_model, light_vp, light_vp, light_dir, true);
    SDL_EndGPURenderPass(render_pass);
    state->shadow_pass_rendered = true;
    return true;
}

static void lesson39_set_full_viewport(SDL_GPUViewport *viewport, SDL_Rect *scissor, Uint32 width, Uint32 height)
{
    SDL_zero(*viewport);
    viewport->w = (float)width;
    viewport->h = (float)height;
    viewport->max_depth = 1.0f;
    scissor->x = 0;
    scissor->y = 0;
    scissor->w = (int)width;
    scissor->h = (int)height;
}

static bool lesson39_run_main_pass(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPUTexture *swapchain_texture,
    Lesson39State *state,
    Uint32 width,
    Uint32 height,
    Mat4 view,
    Mat4 light_vp,
    Mat4 water_model,
    Mat4 box_model)
{
    SDL_GPUColorTargetInfo color_target;
    SDL_GPUDepthStencilTargetInfo depth_target;
    SDL_GPURenderPass *render_pass;
    Vec3 light_dir = lesson39_light_dir();

    SDL_zero(color_target);
    color_target.texture = swapchain_texture;
    color_target.load_op = SDL_GPU_LOADOP_CLEAR;
    color_target.store_op = SDL_GPU_STOREOP_STORE;
    color_target.clear_color = { LESSON39_CLEAR_R, LESSON39_CLEAR_G, LESSON39_CLEAR_B, 1.0f };

    SDL_zero(depth_target);
    depth_target.texture = state->main_depth_texture;
    depth_target.load_op = SDL_GPU_LOADOP_CLEAR;
    depth_target.store_op = SDL_GPU_STOREOP_DONT_CARE;
    depth_target.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
    depth_target.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
    depth_target.clear_depth = 1.0f;
    depth_target.cycle = true;

    render_pass = SDL_BeginGPURenderPass(command_buffer, &color_target, 1, &depth_target);
    if (!render_pass) {
        return false;
    }

    if (state->render_mode == LESSON39_MODE_SPLIT) {
        const Uint32 half_width = width / 2u;
        const int divider_half = LESSON39_DIVIDER_WIDTH / 2;
        const Uint32 left_width = half_width > (Uint32)divider_half ? half_width - (Uint32)divider_half : half_width;
        const Uint32 right_x = half_width + (Uint32)divider_half;
        const Uint32 right_width = width > right_x ? width - right_x : 0;
        SDL_GPUViewport viewport;
        SDL_Rect scissor;
        Mat4 projection;
        Mat4 camera_vp;

        SDL_zero(viewport);
        viewport.w = (float)left_width;
        viewport.h = (float)height;
        viewport.max_depth = 1.0f;
        scissor.x = 0;
        scissor.y = 0;
        scissor.w = (int)left_width;
        scissor.h = (int)height;
        projection = mat4_perspective(LESSON39_FOV_DEGREES * FORGE_GPU_DEG2RAD, height > 0 ? (float)left_width / (float)height : 1.0f, LESSON39_NEAR_PLANE, LESSON39_FAR_PLANE);
        camera_vp = mat4_multiply(projection, view);
        lesson39_draw_sky_and_grid(demo, command_buffer, render_pass, state, camera_vp, light_vp, light_dir, &viewport, &scissor);
        lesson39_draw_processed_model(demo, command_buffer, render_pass, state, &state->pipe_water, water_model, camera_vp, light_vp, light_dir, false);
        lesson39_draw_processed_model(demo, command_buffer, render_pass, state, &state->pipe_box, box_model, camera_vp, light_vp, light_dir, false);

        SDL_zero(viewport);
        viewport.x = (float)right_x;
        viewport.w = (float)right_width;
        viewport.h = (float)height;
        viewport.max_depth = 1.0f;
        scissor.x = (int)right_x;
        scissor.y = 0;
        scissor.w = (int)right_width;
        scissor.h = (int)height;
        projection = mat4_perspective(LESSON39_FOV_DEGREES * FORGE_GPU_DEG2RAD, height > 0 ? (float)right_width / (float)height : 1.0f, LESSON39_NEAR_PLANE, LESSON39_FAR_PLANE);
        camera_vp = mat4_multiply(projection, view);
        lesson39_draw_sky_and_grid(demo, command_buffer, render_pass, state, camera_vp, light_vp, light_dir, &viewport, &scissor);
        lesson39_draw_raw_model(demo, command_buffer, render_pass, state, &state->raw_water, water_model, camera_vp, light_vp, light_dir, false);
        lesson39_draw_raw_model(demo, command_buffer, render_pass, state, &state->raw_box, box_model, camera_vp, light_vp, light_dir, false);
        state->split_rendered = true;
    } else {
        SDL_GPUViewport viewport;
        SDL_Rect scissor;
        Mat4 projection = mat4_perspective(
            LESSON39_FOV_DEGREES * FORGE_GPU_DEG2RAD,
            height > 0 ? (float)width / (float)height : 1.0f,
            LESSON39_NEAR_PLANE,
            LESSON39_FAR_PLANE);
        Mat4 camera_vp = mat4_multiply(projection, view);

        lesson39_set_full_viewport(&viewport, &scissor, width, height);
        lesson39_draw_sky_and_grid(demo, command_buffer, render_pass, state, camera_vp, light_vp, light_dir, &viewport, &scissor);
        if (state->render_mode == LESSON39_MODE_PIPELINE) {
            lesson39_draw_processed_model(demo, command_buffer, render_pass, state, &state->pipe_water, water_model, camera_vp, light_vp, light_dir, false);
            lesson39_draw_processed_model(demo, command_buffer, render_pass, state, &state->pipe_box, box_model, camera_vp, light_vp, light_dir, false);
        } else {
            lesson39_draw_raw_model(demo, command_buffer, render_pass, state, &state->raw_water, water_model, camera_vp, light_vp, light_dir, false);
            lesson39_draw_raw_model(demo, command_buffer, render_pass, state, &state->raw_box, box_model, camera_vp, light_vp, light_dir, false);
        }
    }

    SDL_EndGPURenderPass(render_pass);
    state->main_pass_rendered = true;
    return true;
}

bool ForgeGpuCreateLesson39(ForgeGpuDemo *demo)
{
    Lesson39State *state;
    Uint8 flat_normal_pixel[4] = { 128, 128, 255, 255 };

    state = (Lesson39State *)SDL_calloc(1, sizeof(*state));
    if (!state) {
        return false;
    }
    demo->lesson.private_state = state;

    state->white_texture = ForgeGpuCreateWhiteTexture(demo->device);
    state->flat_normal_texture = ForgeGpuCreateRgba8TextureFromPixels(demo->device, 1, 1, flat_normal_pixel, false);
    state->diffuse_sampler = ForgeGpuCreateSamplerWithAddressAndAnisotropy(
        demo->device,
        SDL_GPU_FILTER_LINEAR,
        SDL_GPU_FILTER_LINEAR,
        SDL_GPU_SAMPLERMIPMAPMODE_LINEAR,
        SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
        1000.0f,
        1.0f);
    state->normal_sampler = ForgeGpuCreateSamplerWithAddress(
        demo->device,
        SDL_GPU_FILTER_LINEAR,
        SDL_GPU_FILTER_LINEAR,
        SDL_GPU_SAMPLERMIPMAPMODE_LINEAR,
        SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        1000.0f);
    state->shadow_depth_texture = ForgeGpuCreateSampledDepthTexture(
        demo,
        LESSON39_SHADOW_MAP_SIZE,
        LESSON39_SHADOW_MAP_SIZE,
        LESSON39_DEPTH_FORMAT);
    if (!state->white_texture || !state->flat_normal_texture || !state->diffuse_sampler ||
        !state->normal_sampler || !state->shadow_depth_texture ||
        !lesson39_create_shadow_sampler(demo, state) ||
        !lesson39_create_pipelines(demo, state)) {
        return false;
    }

    if (!lesson39_load_processed_model(
            demo,
            "processed/39-pipeline-processed-assets/WaterBottle.fmesh",
            "processed/39-pipeline-processed-assets/WaterBottle.fmat",
            &state->pipe_water,
            state->white_texture,
            state->flat_normal_texture) ||
        !lesson39_load_processed_model(
            demo,
            "processed/39-pipeline-processed-assets/BoxTextured.fmesh",
            "processed/39-pipeline-processed-assets/BoxTextured.fmat",
            &state->pipe_box,
            state->white_texture,
            state->flat_normal_texture) ||
        !lesson39_load_raw_model(
            demo,
            "models/WaterBottle/WaterBottle.gltf",
            "models/WaterBottle/WaterBottle_baseColor.png",
            &state->raw_water) ||
        !lesson39_load_raw_model(
            demo,
            "models/BoxTextured/BoxTextured.gltf",
            "models/BoxTextured/CesiumLogoFlat.png",
            &state->raw_box) ||
        !ForgeGpuCreateShadowedGridBuffers(
            demo->device,
            LESSON39_GRID_HALF_SIZE,
            0.0f,
            &state->grid_vertex_buffer,
            &state->grid_index_buffer)) {
        return false;
    }
    state->processed_assets_loaded = true;

    demo->lesson.camera_position = { LESSON39_CAM_START_X, LESSON39_CAM_START_Y, LESSON39_CAM_START_Z };
    demo->lesson.camera_yaw = LESSON39_CAM_START_YAW_DEG * FORGE_GPU_DEG2RAD;
    demo->lesson.camera_pitch = LESSON39_CAM_START_PITCH_DEG * FORGE_GPU_DEG2RAD;
    demo->lesson.pitch_clamp = LESSON39_PITCH_CLAMP;
    demo->lesson.mouse_sensitivity = LESSON39_MOUSE_SENSITIVITY;
    demo->lesson.move_speed = LESSON39_MOVE_SPEED;
    demo->lesson.last_ticks = SDL_GetTicks();

    state->render_mode = LESSON39_MODE_SPLIT;
    state->lod_override = -1;
    state->lod_auto = true;
    return true;
}

bool ForgeGpuRenderLesson39(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPUTexture *swapchain_texture,
    Uint32 width,
    Uint32 height)
{
    Lesson39State *state = lesson39_state(demo);
    Quat orientation;
    Mat4 view;
    Mat4 light_view;
    Mat4 light_projection;
    Mat4 light_vp;
    Mat4 water_model;
    Mat4 box_model;
    Vec3 light_dir;
    Vec3 light_center = { 0.0f, 0.0f, 0.0f };
    Vec3 light_pos;

    if (!state) {
        return false;
    }

    ForgeGpuUpdateCameraFromInput(demo);
    lesson39_update_lod(demo, state);

    if (!ForgeGpuEnsureSampledDepthTarget(
            demo,
            &state->main_depth_texture,
            &state->main_depth_width,
            &state->main_depth_height,
            width,
            height,
            LESSON39_DEPTH_FORMAT)) {
        return false;
    }

    state->shadow_pass_rendered = false;
    state->main_pass_rendered = false;
    state->pipeline_path_drawn = false;
    state->raw_path_drawn = false;
    state->split_rendered = false;

    orientation = quat_from_euler(demo->lesson.camera_yaw, demo->lesson.camera_pitch, 0.0f);
    view = mat4_view_from_quat(demo->lesson.camera_position, orientation);
    light_dir = lesson39_light_dir();
    light_pos = vec3_add(light_center, vec3_scale(light_dir, 5.0f));
    light_view = mat4_look_at(light_pos, light_center, { 0.0f, 1.0f, 0.0f });
    light_projection = mat4_orthographic(-2.0f, 2.0f, -2.0f, 2.0f, 0.1f, 10.0f);
    light_vp = mat4_multiply(light_projection, light_view);
    water_model = mat4_translate({ 0.0f, LESSON39_WATER_Y_OFFSET, 0.0f });
    box_model = mat4_multiply(
        mat4_translate({ LESSON39_BOX_OFFSET_X, LESSON39_BOX_Y_OFFSET, 0.0f }),
        mat4_scale_vec3({ LESSON39_BOX_SCALE, LESSON39_BOX_SCALE, LESSON39_BOX_SCALE }));

    if (!lesson39_run_shadow_pass(demo, command_buffer, state, light_vp, water_model, box_model)) {
        return false;
    }
    return lesson39_run_main_pass(
        demo,
        command_buffer,
        swapchain_texture,
        state,
        width,
        height,
        view,
        light_vp,
        water_model,
        box_model);
}

void ForgeGpuDebugLesson39(ForgeGpuDemo *demo)
{
    Lesson39State *state = lesson39_state(demo);

    if (!state) {
        return;
    }
    ImGui::Text("Mode: %s", state->render_mode == LESSON39_MODE_PIPELINE ? "pipeline" :
        state->render_mode == LESSON39_MODE_RAW ? "raw" : "split");
    ImGui::Text("Water LOD: %u / %u%s",
        state->pipe_water.current_lod,
        state->pipe_water.mesh.lod_count,
        state->lod_auto ? " (auto)" : "");
    ImGui::Text("Water processed: %u vertices, %u indices",
        state->pipe_water.mesh.vertex_count,
        state->pipe_water.mesh.total_index_count);
    ImGui::Text("Box processed: %u vertices, %u indices",
        state->pipe_box.mesh.vertex_count,
        state->pipe_box.mesh.total_index_count);
    ImGui::Text("Shadow pass: %s", state->shadow_pass_rendered ? "yes" : "no");
    ImGui::Text("Main pass: %s", state->main_pass_rendered ? "yes" : "no");
}

void ForgeGpuControlsLesson39(ForgeGpuDemo *demo)
{
    Lesson39State *state = lesson39_state(demo);

    if (!state) {
        return;
    }
    ImGui::TextUnformatted("1: pipeline path, 2: raw path, 3: split comparison");
    ImGui::TextUnformatted("L: cycle WaterBottle LOD override");
    ImGui::RadioButton("Pipeline", &state->render_mode, LESSON39_MODE_PIPELINE);
    ImGui::SameLine();
    ImGui::RadioButton("Raw", &state->render_mode, LESSON39_MODE_RAW);
    ImGui::SameLine();
    ImGui::RadioButton("Split", &state->render_mode, LESSON39_MODE_SPLIT);
    if (ImGui::Button("Cycle LOD")) {
        if (state->lod_override < 0) {
            state->lod_override = 0;
            state->lod_auto = false;
        } else {
            state->lod_override += 1;
            if ((Uint32)state->lod_override >= state->pipe_water.mesh.lod_count) {
                state->lod_override = -1;
                state->lod_auto = true;
            }
        }
    }
}

bool ForgeGpuHandleLesson39Event(ForgeGpuDemo *demo, const SDL_Event *event)
{
    Lesson39State *state = lesson39_state(demo);

    if (!state || event->type != SDL_EVENT_KEY_DOWN || event->key.repeat) {
        return false;
    }
    if (event->key.key == SDLK_1) {
        state->render_mode = LESSON39_MODE_PIPELINE;
        return true;
    }
    if (event->key.key == SDLK_2) {
        state->render_mode = LESSON39_MODE_RAW;
        return true;
    }
    if (event->key.key == SDLK_3) {
        state->render_mode = LESSON39_MODE_SPLIT;
        return true;
    }
    if (event->key.key == SDLK_L) {
        if (state->lod_override < 0) {
            state->lod_override = 0;
            state->lod_auto = false;
        } else {
            state->lod_override += 1;
            if ((Uint32)state->lod_override >= state->pipe_water.mesh.lod_count) {
                state->lod_override = -1;
                state->lod_auto = true;
            }
        }
        return true;
    }
    return false;
}

void ForgeGpuExportLesson39Metrics(ForgeGpuDemo *demo)
{
    Lesson39State *state = lesson39_state(demo);

    if (!state) {
        return;
    }
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson39ShadowPass", state->shadow_pass_rendered ? 1.0 : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson39MainPass", state->main_pass_rendered ? 1.0 : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson39PipelinePath", state->pipeline_path_drawn ? 1.0 : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson39RawPath", state->raw_path_drawn ? 1.0 : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson39RenderMode", (double)state->render_mode);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson39SplitMode", state->split_rendered ? 1.0 : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson39LodAuto", state->lod_auto ? 1.0 : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson39WaterLodOverride", (double)state->lod_override);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson39WaterLod", (double)state->pipe_water.current_lod);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson39WaterLodCount", (double)state->pipe_water.mesh.lod_count);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson39ProcessedAssets", state->processed_assets_loaded ? 1.0 : 0.0);
    ForgeGpuBrowserSetNumberMetric(
        "sdlGpuForgeGpuLesson39ComparisonSamplerPath",
        state->shadow_sampler && state->shadow_pass_rendered && state->main_pass_rendered ? 1.0 : 0.0);
}

static void lesson39_release_texture_if_owned(
    SDL_GPUDevice *device,
    SDL_GPUTexture **texture,
    SDL_GPUTexture *fallback_a,
    SDL_GPUTexture *fallback_b)
{
    if (*texture && *texture != fallback_a && *texture != fallback_b) {
        SDL_ReleaseGPUTexture(device, *texture);
    }
    *texture = nullptr;
}

static void lesson39_destroy_processed_model(
    SDL_GPUDevice *device,
    Lesson39ProcessedModel *model,
    SDL_GPUTexture *white_texture,
    SDL_GPUTexture *flat_normal_texture)
{
    lesson39_release_texture_if_owned(device, &model->diffuse_texture, white_texture, flat_normal_texture);
    lesson39_release_texture_if_owned(device, &model->normal_texture, white_texture, flat_normal_texture);
    if (model->index_buffer) {
        SDL_ReleaseGPUBuffer(device, model->index_buffer);
    }
    if (model->vertex_buffer) {
        SDL_ReleaseGPUBuffer(device, model->vertex_buffer);
    }
    ForgeGpuFreeProcessedMaterials(&model->materials);
    ForgeGpuFreeProcessedMesh(&model->mesh);
    SDL_zero(*model);
}

static void lesson39_destroy_raw_model(SDL_GPUDevice *device, Lesson39RawModel *model)
{
    if (model->diffuse_texture) {
        SDL_ReleaseGPUTexture(device, model->diffuse_texture);
    }
    if (model->index_buffer) {
        SDL_ReleaseGPUBuffer(device, model->index_buffer);
    }
    if (model->vertex_buffer) {
        SDL_ReleaseGPUBuffer(device, model->vertex_buffer);
    }
    SDL_zero(*model);
}

void ForgeGpuDestroyLesson39(ForgeGpuDemo *demo)
{
    Lesson39State *state = lesson39_state(demo);

    if (!state) {
        return;
    }

    lesson39_destroy_raw_model(demo->device, &state->raw_box);
    lesson39_destroy_raw_model(demo->device, &state->raw_water);
    lesson39_destroy_processed_model(demo->device, &state->pipe_box, state->white_texture, state->flat_normal_texture);
    lesson39_destroy_processed_model(demo->device, &state->pipe_water, state->white_texture, state->flat_normal_texture);

    if (state->grid_index_buffer) {
        SDL_ReleaseGPUBuffer(demo->device, state->grid_index_buffer);
    }
    if (state->grid_vertex_buffer) {
        SDL_ReleaseGPUBuffer(demo->device, state->grid_vertex_buffer);
    }
    if (state->shadow_sampler) {
        SDL_ReleaseGPUSampler(demo->device, state->shadow_sampler);
    }
    if (state->normal_sampler) {
        SDL_ReleaseGPUSampler(demo->device, state->normal_sampler);
    }
    if (state->diffuse_sampler) {
        SDL_ReleaseGPUSampler(demo->device, state->diffuse_sampler);
    }
    if (state->main_depth_texture) {
        SDL_ReleaseGPUTexture(demo->device, state->main_depth_texture);
    }
    if (state->shadow_depth_texture) {
        SDL_ReleaseGPUTexture(demo->device, state->shadow_depth_texture);
    }
    if (state->flat_normal_texture) {
        SDL_ReleaseGPUTexture(demo->device, state->flat_normal_texture);
    }
    if (state->white_texture) {
        SDL_ReleaseGPUTexture(demo->device, state->white_texture);
    }
    if (state->pipeline_grid) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, state->pipeline_grid);
    }
    if (state->pipeline_sky) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, state->pipeline_sky);
    }
    if (state->pipeline_shadow_raw) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, state->pipeline_shadow_raw);
    }
    if (state->pipeline_shadow_processed) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, state->pipeline_shadow_processed);
    }
    if (state->pipeline_scene_raw) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, state->pipeline_scene_raw);
    }
    if (state->pipeline_scene_processed) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, state->pipeline_scene_processed);
    }
    SDL_free(state);
    demo->lesson.private_state = nullptr;
}
