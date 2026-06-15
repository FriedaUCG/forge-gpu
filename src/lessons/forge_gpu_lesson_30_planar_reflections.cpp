#include "forge_gpu_lessons.h"

#include "forge_gpu_browser_status.h"
#include "forge_gpu_camera.h"
#include "forge_gpu_deferred_scene.h"
#include "forge_gpu_forward_scene.h"
#include "forge_gpu_gpu_helpers.h"
#include "forge_gpu_lesson_common.h"
#include "forge_gpu_math.h"
#include "forge_gpu_scene.h"
#include "forge_gpu_shader_layouts.h"
#include "shaders/generated/forge_gpu_lesson_30_shaders.h"
#include "imgui.h"

#include <stddef.h>

#define LESSON30_MODEL_BOAT 0
#define LESSON30_MODEL_ROCKS 1
#define LESSON30_MODEL_COUNT 2
#define LESSON30_SHADOW_MAP_SIZE 2048u
#define LESSON30_SHADOW_FORMAT SDL_GPU_TEXTUREFORMAT_D32_FLOAT
#define LESSON30_DEPTH_FORMAT SDL_GPU_TEXTUREFORMAT_D32_FLOAT
#define LESSON30_CAMERA_SPEED 5.0f
#define LESSON30_MOUSE_SENSITIVITY 0.003f
#define LESSON30_PITCH_CLAMP 1.5f
#define LESSON30_CAMERA_START_X 6.0f
#define LESSON30_CAMERA_START_Y 3.0f
#define LESSON30_CAMERA_START_Z 8.0f
#define LESSON30_CAMERA_START_YAW_DEG 30.0f
#define LESSON30_CAMERA_START_PITCH_DEG -10.0f
#define LESSON30_LIGHT_DIR_X -0.4f
#define LESSON30_LIGHT_DIR_Y -0.7f
#define LESSON30_LIGHT_DIR_Z -0.5f
#define LESSON30_LIGHT_INTENSITY 0.9f
#define LESSON30_LIGHT_COLOR_R 1.0f
#define LESSON30_LIGHT_COLOR_G 0.95f
#define LESSON30_LIGHT_COLOR_B 0.85f
#define LESSON30_MATERIAL_AMBIENT 0.2f
#define LESSON30_MATERIAL_SHININESS 64.0f
#define LESSON30_MATERIAL_SPECULAR_STRENGTH 0.3f
#define LESSON30_SHADOW_ORTHO_SIZE 20.0f
#define LESSON30_SHADOW_NEAR 0.1f
#define LESSON30_SHADOW_FAR 60.0f
#define LESSON30_LIGHT_DISTANCE 25.0f
#define LESSON30_PARALLEL_THRESHOLD 0.99f
#define LESSON30_WATER_LEVEL 0.0f
#define LESSON30_WATER_HALF_SIZE 60.0f
#define LESSON30_WATER_TINT_R 0.05f
#define LESSON30_WATER_TINT_G 0.15f
#define LESSON30_WATER_TINT_B 0.20f
#define LESSON30_WATER_TINT_A 1.0f
#define LESSON30_FRESNEL_F0 0.02f
#define LESSON30_FLOOR_Y -2.0f
#define LESSON30_FLOOR_HALF_SIZE 60.0f
#define LESSON30_FLOOR_COLOR_R 0.76f
#define LESSON30_FLOOR_COLOR_G 0.70f
#define LESSON30_FLOOR_COLOR_B 0.50f
#define LESSON30_FLOOR_SHININESS 16.0f
#define LESSON30_FLOOR_SPECULAR_STRENGTH 0.1f
#define LESSON30_ROCK_SCALE 0.66f
#define LESSON30_BOAT_POS_X 3.0f
#define LESSON30_BOAT_POS_Y 0.3f
#define LESSON30_BOAT_POS_Z 3.0f
#define LESSON30_CLEAR_R 0.5f
#define LESSON30_CLEAR_G 0.7f
#define LESSON30_CLEAR_B 0.9f
#define LESSON30_OBLIQUE_EPSILON 1e-6f

struct Lesson30WaterVertUniforms
{
    Mat4 mvp;
};

struct Lesson30WaterFragUniforms
{
    float eye_pos[3];
    float water_level;
    float water_tint[4];
    float fresnel_f0;
    float pad0[3];
    float pad_array[12];
};

struct Lesson30State
{
    GpuSceneData models[LESSON30_MODEL_COUNT];
    SDL_GPUGraphicsPipeline *shadow_pipeline;
    SDL_GPUGraphicsPipeline *scene_pipeline;
    SDL_GPUGraphicsPipeline *scene_reflection_pipeline;
    SDL_GPUGraphicsPipeline *skybox_pipeline;
    SDL_GPUGraphicsPipeline *skybox_reflection_pipeline;
    SDL_GPUGraphicsPipeline *water_pipeline;
    SDL_GPUTexture *reflection_color;
    SDL_GPUTexture *reflection_depth;
    SDL_GPUTexture *main_depth;
    SDL_GPUTexture *shadow_depth;
    SDL_GPUTexture *cubemap_texture;
    SDL_GPUBuffer *floor_vertex_buffer;
    SDL_GPUBuffer *floor_index_buffer;
    SDL_GPUBuffer *water_vertex_buffer;
    SDL_GPUBuffer *water_index_buffer;
    SDL_GPUBuffer *skybox_vertex_buffer;
    SDL_GPUBuffer *skybox_index_buffer;
    Uint32 reflection_width;
    Uint32 reflection_height;
    Uint32 reflection_depth_width;
    Uint32 reflection_depth_height;
    Uint32 main_depth_width;
    Uint32 main_depth_height;
    Mat4 light_vp;
    bool reflection_rendered;
    bool water_rendered;
};

static_assert(sizeof(ForgeGpuShadowVertUniforms) == 64, "lesson 30 shadow vertex uniform size must match HLSL layout");
static_assert(sizeof(ForgeGpuForwardSceneVertUniforms) == 192, "lesson 30 scene vertex uniform size must match HLSL layout");
static_assert(sizeof(ForgeGpuForwardSceneFragUniforms) == 80, "lesson 30 scene fragment uniform size must match HLSL layout");
static_assert(sizeof(ForgeGpuForwardSkyboxVertUniforms) == 64, "lesson 30 skybox vertex uniform size must match HLSL layout");
static_assert(sizeof(Lesson30WaterVertUniforms) == 64, "lesson 30 water vertex uniform size must match HLSL layout");
static_assert(sizeof(Lesson30WaterFragUniforms) == 96, "lesson 30 water fragment uniform size must match strict HLSL cbuffer array layout");

static Lesson30State *lesson30_state(ForgeGpuDemo *demo)
{
    return (Lesson30State *)demo->lesson.private_state;
}

static Mat4 lesson30_reflect(float a, float b, float c, float d)
{
    Mat4 result;

    result.m[0] = 1.0f - 2.0f * a * a;
    result.m[1] = -2.0f * a * b;
    result.m[2] = -2.0f * a * c;
    result.m[3] = 0.0f;
    result.m[4] = -2.0f * b * a;
    result.m[5] = 1.0f - 2.0f * b * b;
    result.m[6] = -2.0f * b * c;
    result.m[7] = 0.0f;
    result.m[8] = -2.0f * c * a;
    result.m[9] = -2.0f * c * b;
    result.m[10] = 1.0f - 2.0f * c * c;
    result.m[11] = 0.0f;
    result.m[12] = -2.0f * a * d;
    result.m[13] = -2.0f * b * d;
    result.m[14] = -2.0f * c * d;
    result.m[15] = 1.0f;
    return result;
}

static Mat4 lesson30_transpose(Mat4 matrix)
{
    Mat4 result;

    for (int col = 0; col < 4; col += 1) {
        for (int row = 0; row < 4; row += 1) {
            result.m[col * 4 + row] = matrix.m[row * 4 + col];
        }
    }
    return result;
}

static float lesson30_sign(float value)
{
    if (value > 0.0f) {
        return 1.0f;
    }
    if (value < 0.0f) {
        return -1.0f;
    }
    return 0.0f;
}

static Mat4 lesson30_oblique_near_plane(Mat4 projection, Vec4 clip_plane_view)
{
    Vec4 q;
    Vec4 c;
    float dot;
    float scale;

    q.x = (lesson30_sign(clip_plane_view.x) + projection.m[8]) / projection.m[0];
    q.y = (lesson30_sign(clip_plane_view.y) + projection.m[9]) / projection.m[5];
    q.z = -1.0f;
    q.w = (1.0f + projection.m[10]) / projection.m[14];
    dot =
        clip_plane_view.x * q.x +
        clip_plane_view.y * q.y +
        clip_plane_view.z * q.z +
        clip_plane_view.w * q.w;

    if (SDL_fabsf(dot) < LESSON30_OBLIQUE_EPSILON) {
        return projection;
    }

    scale = 1.0f / dot;
    c.x = clip_plane_view.x * scale;
    c.y = clip_plane_view.y * scale;
    c.z = clip_plane_view.z * scale;
    c.w = clip_plane_view.w * scale;

    projection.m[2] = c.x;
    projection.m[6] = c.y;
    projection.m[10] = c.z;
    projection.m[14] = c.w;
    return projection;
}

static Vec4 lesson30_transform_plane_to_view(Mat4 view)
{
    const Mat4 view_inv_transpose = lesson30_transpose(mat4_inverse(view));
    const float plane[4] = { 0.0f, 1.0f, 0.0f, -LESSON30_WATER_LEVEL };
    Vec4 result;

    result.x = view_inv_transpose.m[0] * plane[0] +
               view_inv_transpose.m[4] * plane[1] +
               view_inv_transpose.m[8] * plane[2] +
               view_inv_transpose.m[12] * plane[3];
    result.y = view_inv_transpose.m[1] * plane[0] +
               view_inv_transpose.m[5] * plane[1] +
               view_inv_transpose.m[9] * plane[2] +
               view_inv_transpose.m[13] * plane[3];
    result.z = view_inv_transpose.m[2] * plane[0] +
               view_inv_transpose.m[6] * plane[1] +
               view_inv_transpose.m[10] * plane[2] +
               view_inv_transpose.m[14] * plane[3];
    result.w = view_inv_transpose.m[3] * plane[0] +
               view_inv_transpose.m[7] * plane[1] +
               view_inv_transpose.m[11] * plane[2] +
               view_inv_transpose.m[15] * plane[3];
    return result;
}

static bool lesson30_ensure_depth_target(
    ForgeGpuDemo *demo,
    SDL_GPUTexture **texture,
    Uint32 *target_width,
    Uint32 *target_height,
    Uint32 width,
    Uint32 height)
{
    SDL_GPUTextureCreateInfo texture_info;
    SDL_GPUTexture *new_texture;

    if (*texture && *target_width == width && *target_height == height) {
        return true;
    }

    SDL_zero(texture_info);
    texture_info.type = SDL_GPU_TEXTURETYPE_2D;
    texture_info.format = LESSON30_DEPTH_FORMAT;
    texture_info.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
    texture_info.width = width;
    texture_info.height = height;
    texture_info.layer_count_or_depth = 1;
    texture_info.num_levels = 1;
    texture_info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    new_texture = SDL_CreateGPUTexture(demo->device, &texture_info);
    if (!new_texture) {
        return false;
    }
    if (*texture) {
        SDL_ReleaseGPUTexture(demo->device, *texture);
    }
    *texture = new_texture;
    *target_width = width;
    *target_height = height;
    return true;
}

static bool lesson30_ensure_targets(ForgeGpuDemo *demo, Uint32 width, Uint32 height)
{
    Lesson30State *state = lesson30_state(demo);

    if (!state) {
        SDL_SetError("lesson 30 internal state is missing");
        return false;
    }
    if (width == 0 || height == 0) {
        SDL_SetError("lesson 30 render target size is invalid");
        return false;
    }
    return ForgeGpuEnsureSampledColorTarget(
               demo,
               &state->reflection_color,
               &state->reflection_width,
               &state->reflection_height,
               width,
               height,
               demo->color_format) &&
           lesson30_ensure_depth_target(
               demo,
               &state->reflection_depth,
               &state->reflection_depth_width,
               &state->reflection_depth_height,
               width,
               height) &&
           lesson30_ensure_depth_target(
               demo,
               &state->main_depth,
               &state->main_depth_width,
               &state->main_depth_height,
               width,
               height);
}

static SDL_GPUGraphicsPipeline *lesson30_create_pipeline(
    ForgeGpuDemo *demo,
    SDL_GPUShader *vertex_shader,
    SDL_GPUShader *fragment_shader,
    const SDL_GPUVertexBufferDescription *vertex_buffer,
    Uint32 num_vertex_buffers,
    const SDL_GPUVertexAttribute *attributes,
    Uint32 num_attributes,
    bool has_color_target,
    bool alpha_blend,
    bool has_depth_target,
    bool depth_test,
    bool depth_write,
    SDL_GPUCompareOp compare_op,
    SDL_GPUCullMode cull_mode,
    SDL_GPUFrontFace front_face)
{
    SDL_GPUColorTargetDescription color_target;
    SDL_GPUGraphicsPipelineCreateInfo pipeline_info;

    SDL_zero(color_target);
    color_target.format = demo->color_format;
    if (alpha_blend) {
        color_target.blend_state.enable_blend = true;
        color_target.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
        color_target.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        color_target.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
        color_target.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        color_target.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        color_target.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
        color_target.blend_state.color_write_mask =
            SDL_GPU_COLORCOMPONENT_R | SDL_GPU_COLORCOMPONENT_G |
            SDL_GPU_COLORCOMPONENT_B | SDL_GPU_COLORCOMPONENT_A;
    }

    SDL_zero(pipeline_info);
    pipeline_info.vertex_shader = vertex_shader;
    pipeline_info.fragment_shader = fragment_shader;
    pipeline_info.vertex_input_state.vertex_buffer_descriptions = vertex_buffer;
    pipeline_info.vertex_input_state.num_vertex_buffers = num_vertex_buffers;
    pipeline_info.vertex_input_state.vertex_attributes = attributes;
    pipeline_info.vertex_input_state.num_vertex_attributes = num_attributes;
    pipeline_info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pipeline_info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    pipeline_info.rasterizer_state.cull_mode = cull_mode;
    pipeline_info.rasterizer_state.front_face = front_face;
    pipeline_info.depth_stencil_state.compare_op = compare_op;
    pipeline_info.depth_stencil_state.enable_depth_test = depth_test;
    pipeline_info.depth_stencil_state.enable_depth_write = depth_write;
    if (has_color_target) {
        pipeline_info.target_info.color_target_descriptions = &color_target;
        pipeline_info.target_info.num_color_targets = 1;
    }
    pipeline_info.target_info.has_depth_stencil_target = has_depth_target;
    pipeline_info.target_info.depth_stencil_format = has_depth_target ? LESSON30_DEPTH_FORMAT : SDL_GPU_TEXTUREFORMAT_INVALID;
    return SDL_CreateGPUGraphicsPipeline(demo->device, &pipeline_info);
}

static bool lesson30_create_shadow_pipeline(ForgeGpuDemo *demo)
{
    Lesson30State *state = lesson30_state(demo);
    SDL_GPUShader *vertex_shader;
    SDL_GPUShader *fragment_shader;
    SDL_GPUVertexBufferDescription vertex_buffer;
    SDL_GPUVertexAttribute attributes[3];

    vertex_shader = ForgeGpuCreateShader(
        demo->device,
        SDL_GPU_SHADERSTAGE_VERTEX,
        lesson30_shadow_vert_wgsl,
        lesson30_shadow_vert_wgsl_size,
        lesson30_shadow_vert_msl,
        lesson30_shadow_vert_msl_size,
        0, 0, 0, 1);
    if (!vertex_shader) {
        return false;
    }
    fragment_shader = ForgeGpuCreateShader(
        demo->device,
        SDL_GPU_SHADERSTAGE_FRAGMENT,
        lesson30_shadow_frag_wgsl,
        lesson30_shadow_frag_wgsl_size,
        lesson30_shadow_frag_msl,
        lesson30_shadow_frag_msl_size,
        0, 0, 0, 0);
    if (!fragment_shader) {
        SDL_ReleaseGPUShader(demo->device, vertex_shader);
        return false;
    }

    ForgeGpuFillMeshVertexInput(&vertex_buffer, attributes);
    state->shadow_pipeline = lesson30_create_pipeline(
        demo,
        vertex_shader,
        fragment_shader,
        &vertex_buffer,
        1,
        attributes,
        SDL_arraysize(attributes),
        false,
        false,
        true,
        true,
        true,
        SDL_GPU_COMPAREOP_LESS,
        SDL_GPU_CULLMODE_FRONT,
        SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE);

    SDL_ReleaseGPUShader(demo->device, vertex_shader);
    SDL_ReleaseGPUShader(demo->device, fragment_shader);
    return state->shadow_pipeline != nullptr;
}

static bool lesson30_create_scene_pipelines(ForgeGpuDemo *demo)
{
    Lesson30State *state = lesson30_state(demo);
    SDL_GPUShader *vertex_shader;
    SDL_GPUShader *fragment_shader;
    SDL_GPUVertexBufferDescription vertex_buffer;
    SDL_GPUVertexAttribute attributes[3];

    vertex_shader = ForgeGpuCreateShader(
        demo->device,
        SDL_GPU_SHADERSTAGE_VERTEX,
        lesson30_scene_vert_wgsl,
        lesson30_scene_vert_wgsl_size,
        lesson30_scene_vert_msl,
        lesson30_scene_vert_msl_size,
        0, 0, 0, 1);
    if (!vertex_shader) {
        return false;
    }
    fragment_shader = ForgeGpuCreateShaderWithResourceLayout(
        demo->device,
        lesson30_scene_frag_wgsl,
        lesson30_scene_frag_wgsl_size,
        lesson30_scene_frag_msl,
        lesson30_scene_frag_msl_size,
        ForgeGpuShaderLayout_lesson30_scene_frag());
    if (!fragment_shader) {
        SDL_ReleaseGPUShader(demo->device, vertex_shader);
        return false;
    }

    ForgeGpuFillMeshVertexInput(&vertex_buffer, attributes);
    state->scene_pipeline = lesson30_create_pipeline(
        demo,
        vertex_shader,
        fragment_shader,
        &vertex_buffer,
        1,
        attributes,
        SDL_arraysize(attributes),
        true,
        false,
        true,
        true,
        true,
        SDL_GPU_COMPAREOP_LESS_OR_EQUAL,
        SDL_GPU_CULLMODE_BACK,
        SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE);
    state->scene_reflection_pipeline = lesson30_create_pipeline(
        demo,
        vertex_shader,
        fragment_shader,
        &vertex_buffer,
        1,
        attributes,
        SDL_arraysize(attributes),
        true,
        false,
        true,
        true,
        true,
        SDL_GPU_COMPAREOP_LESS_OR_EQUAL,
        SDL_GPU_CULLMODE_BACK,
        SDL_GPU_FRONTFACE_CLOCKWISE);

    SDL_ReleaseGPUShader(demo->device, vertex_shader);
    SDL_ReleaseGPUShader(demo->device, fragment_shader);
    return state->scene_pipeline && state->scene_reflection_pipeline;
}

static bool lesson30_create_skybox_pipelines(ForgeGpuDemo *demo)
{
    Lesson30State *state = lesson30_state(demo);
    SDL_GPUShader *vertex_shader;
    SDL_GPUShader *fragment_shader;
    SDL_GPUVertexBufferDescription vertex_buffer;
    SDL_GPUVertexAttribute attribute;

    vertex_shader = ForgeGpuCreateShader(
        demo->device,
        SDL_GPU_SHADERSTAGE_VERTEX,
        lesson30_skybox_vert_wgsl,
        lesson30_skybox_vert_wgsl_size,
        lesson30_skybox_vert_msl,
        lesson30_skybox_vert_msl_size,
        0, 0, 0, 1);
    if (!vertex_shader) {
        return false;
    }
    fragment_shader = ForgeGpuCreateShaderWithResourceLayout(
        demo->device,
        lesson30_skybox_frag_wgsl,
        lesson30_skybox_frag_wgsl_size,
        lesson30_skybox_frag_msl,
        lesson30_skybox_frag_msl_size,
        ForgeGpuShaderLayout_lesson30_skybox_frag());
    if (!fragment_shader) {
        SDL_ReleaseGPUShader(demo->device, vertex_shader);
        return false;
    }

    SDL_zero(vertex_buffer);
    vertex_buffer.slot = 0;
    vertex_buffer.pitch = sizeof(float) * 3u;
    vertex_buffer.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    SDL_zero(attribute);
    attribute.location = 0;
    attribute.buffer_slot = 0;
    attribute.format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    attribute.offset = 0;
    state->skybox_pipeline = lesson30_create_pipeline(
        demo,
        vertex_shader,
        fragment_shader,
        &vertex_buffer,
        1,
        &attribute,
        1,
        true,
        false,
        true,
        true,
        false,
        SDL_GPU_COMPAREOP_LESS_OR_EQUAL,
        SDL_GPU_CULLMODE_FRONT,
        SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE);
    state->skybox_reflection_pipeline = lesson30_create_pipeline(
        demo,
        vertex_shader,
        fragment_shader,
        &vertex_buffer,
        1,
        &attribute,
        1,
        true,
        false,
        true,
        true,
        false,
        SDL_GPU_COMPAREOP_LESS_OR_EQUAL,
        SDL_GPU_CULLMODE_FRONT,
        SDL_GPU_FRONTFACE_CLOCKWISE);

    SDL_ReleaseGPUShader(demo->device, vertex_shader);
    SDL_ReleaseGPUShader(demo->device, fragment_shader);
    return state->skybox_pipeline && state->skybox_reflection_pipeline;
}

static bool lesson30_create_water_pipeline(ForgeGpuDemo *demo)
{
    Lesson30State *state = lesson30_state(demo);
    SDL_GPUShader *vertex_shader;
    SDL_GPUShader *fragment_shader;
    SDL_GPUVertexBufferDescription vertex_buffer;
    SDL_GPUVertexAttribute attributes[3];

    vertex_shader = ForgeGpuCreateShader(
        demo->device,
        SDL_GPU_SHADERSTAGE_VERTEX,
        lesson30_water_vert_wgsl,
        lesson30_water_vert_wgsl_size,
        lesson30_water_vert_msl,
        lesson30_water_vert_msl_size,
        0, 0, 0, 1);
    if (!vertex_shader) {
        return false;
    }
    fragment_shader = ForgeGpuCreateShaderWithResourceLayout(
        demo->device,
        lesson30_water_frag_wgsl,
        lesson30_water_frag_wgsl_size,
        lesson30_water_frag_msl,
        lesson30_water_frag_msl_size,
        ForgeGpuShaderLayout_lesson30_water_frag());
    if (!fragment_shader) {
        SDL_ReleaseGPUShader(demo->device, vertex_shader);
        return false;
    }

    ForgeGpuFillMeshVertexInput(&vertex_buffer, attributes);
    state->water_pipeline = lesson30_create_pipeline(
        demo,
        vertex_shader,
        fragment_shader,
        &vertex_buffer,
        1,
        attributes,
        SDL_arraysize(attributes),
        true,
        true,
        true,
        true,
        false,
        SDL_GPU_COMPAREOP_LESS_OR_EQUAL,
        SDL_GPU_CULLMODE_NONE,
        SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE);

    SDL_ReleaseGPUShader(demo->device, vertex_shader);
    SDL_ReleaseGPUShader(demo->device, fragment_shader);
    return state->water_pipeline != nullptr;
}

static bool lesson30_create_pipelines(ForgeGpuDemo *demo)
{
    return lesson30_create_shadow_pipeline(demo) &&
           lesson30_create_scene_pipelines(demo) &&
           lesson30_create_skybox_pipelines(demo) &&
           lesson30_create_water_pipeline(demo);
}

static bool lesson30_create_samplers(ForgeGpuDemo *demo)
{
    demo->lesson.samplers[0] = ForgeGpuCreateSamplerWithAddressAndAnisotropy(
        demo->device,
        SDL_GPU_FILTER_LINEAR,
        SDL_GPU_FILTER_LINEAR,
        SDL_GPU_SAMPLERMIPMAPMODE_LINEAR,
        SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
        1000.0f,
        4.0f);
    demo->lesson.samplers[1] = ForgeGpuCreateSamplerWithAddress(
        demo->device,
        SDL_GPU_FILTER_NEAREST,
        SDL_GPU_FILTER_NEAREST,
        SDL_GPU_SAMPLERMIPMAPMODE_NEAREST,
        SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        0.0f);
    demo->lesson.samplers[2] = ForgeGpuCreateSamplerWithAddress(
        demo->device,
        SDL_GPU_FILTER_LINEAR,
        SDL_GPU_FILTER_LINEAR,
        SDL_GPU_SAMPLERMIPMAPMODE_NEAREST,
        SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        0.0f);
    demo->lesson.samplers[3] = ForgeGpuCreateSamplerWithAddress(
        demo->device,
        SDL_GPU_FILTER_LINEAR,
        SDL_GPU_FILTER_LINEAR,
        SDL_GPU_SAMPLERMIPMAPMODE_LINEAR,
        SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        1000.0f);
    return demo->lesson.samplers[0] &&
           demo->lesson.samplers[1] &&
           demo->lesson.samplers[2] &&
           demo->lesson.samplers[3];
}

static bool lesson30_create_geometry(ForgeGpuDemo *demo)
{
    Lesson30State *state = lesson30_state(demo);
    const ForgeGpuMeshVertex floor_vertices[4] = {
        { { -LESSON30_FLOOR_HALF_SIZE, LESSON30_FLOOR_Y, -LESSON30_FLOOR_HALF_SIZE }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f } },
        { {  LESSON30_FLOOR_HALF_SIZE, LESSON30_FLOOR_Y, -LESSON30_FLOOR_HALF_SIZE }, { 0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f } },
        { {  LESSON30_FLOOR_HALF_SIZE, LESSON30_FLOOR_Y,  LESSON30_FLOOR_HALF_SIZE }, { 0.0f, 1.0f, 0.0f }, { 1.0f, 1.0f } },
        { { -LESSON30_FLOOR_HALF_SIZE, LESSON30_FLOOR_Y,  LESSON30_FLOOR_HALF_SIZE }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 1.0f } },
    };
    const ForgeGpuMeshVertex water_vertices[4] = {
        { { -LESSON30_WATER_HALF_SIZE, LESSON30_WATER_LEVEL, -LESSON30_WATER_HALF_SIZE }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f } },
        { {  LESSON30_WATER_HALF_SIZE, LESSON30_WATER_LEVEL, -LESSON30_WATER_HALF_SIZE }, { 0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f } },
        { {  LESSON30_WATER_HALF_SIZE, LESSON30_WATER_LEVEL,  LESSON30_WATER_HALF_SIZE }, { 0.0f, 1.0f, 0.0f }, { 1.0f, 1.0f } },
        { { -LESSON30_WATER_HALF_SIZE, LESSON30_WATER_LEVEL,  LESSON30_WATER_HALF_SIZE }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 1.0f } },
    };
    const Uint16 quad_indices[6] = { 0, 1, 2, 0, 2, 3 };
    const float skybox_vertices[24] = {
        -1.0f, -1.0f, -1.0f,
         1.0f, -1.0f, -1.0f,
         1.0f,  1.0f, -1.0f,
        -1.0f,  1.0f, -1.0f,
        -1.0f, -1.0f,  1.0f,
         1.0f, -1.0f,  1.0f,
         1.0f,  1.0f,  1.0f,
        -1.0f,  1.0f,  1.0f,
    };
    const Uint16 skybox_indices[36] = {
        0, 2, 1, 0, 3, 2,
        4, 5, 6, 4, 6, 7,
        0, 4, 7, 0, 7, 3,
        1, 2, 6, 1, 6, 5,
        0, 1, 5, 0, 5, 4,
        3, 7, 6, 3, 6, 2,
    };

    state->floor_vertex_buffer = ForgeGpuCreateBufferWithData(
        demo->device,
        SDL_GPU_BUFFERUSAGE_VERTEX,
        floor_vertices,
        sizeof(floor_vertices));
    state->floor_index_buffer = ForgeGpuCreateBufferWithData(
        demo->device,
        SDL_GPU_BUFFERUSAGE_INDEX,
        quad_indices,
        sizeof(quad_indices));
    state->water_vertex_buffer = ForgeGpuCreateBufferWithData(
        demo->device,
        SDL_GPU_BUFFERUSAGE_VERTEX,
        water_vertices,
        sizeof(water_vertices));
    state->water_index_buffer = ForgeGpuCreateBufferWithData(
        demo->device,
        SDL_GPU_BUFFERUSAGE_INDEX,
        quad_indices,
        sizeof(quad_indices));
    state->skybox_vertex_buffer = ForgeGpuCreateBufferWithData(
        demo->device,
        SDL_GPU_BUFFERUSAGE_VERTEX,
        skybox_vertices,
        sizeof(skybox_vertices));
    state->skybox_index_buffer = ForgeGpuCreateBufferWithData(
        demo->device,
        SDL_GPU_BUFFERUSAGE_INDEX,
        skybox_indices,
        sizeof(skybox_indices));
    return state->floor_vertex_buffer &&
           state->floor_index_buffer &&
           state->water_vertex_buffer &&
           state->water_index_buffer &&
           state->skybox_vertex_buffer &&
           state->skybox_index_buffer;
}

static void lesson30_init_camera(ForgeGpuDemo *demo)
{
    demo->lesson.camera_position = {
        LESSON30_CAMERA_START_X,
        LESSON30_CAMERA_START_Y,
        LESSON30_CAMERA_START_Z
    };
    demo->lesson.camera_yaw = LESSON30_CAMERA_START_YAW_DEG * FORGE_GPU_DEG2RAD;
    demo->lesson.camera_pitch = LESSON30_CAMERA_START_PITCH_DEG * FORGE_GPU_DEG2RAD;
    demo->lesson.mouse_sensitivity = LESSON30_MOUSE_SENSITIVITY;
    demo->lesson.pitch_clamp = LESSON30_PITCH_CLAMP;
    demo->lesson.move_speed = LESSON30_CAMERA_SPEED;
    demo->lesson.last_ticks = SDL_GetTicks();
}

static ForgeGpuForwardSceneLighting lesson30_forward_lighting(float shininess, float specular_strength)
{
    ForgeGpuForwardSceneLighting lighting;

    lighting.light_dir = { LESSON30_LIGHT_DIR_X, LESSON30_LIGHT_DIR_Y, LESSON30_LIGHT_DIR_Z };
    lighting.light_color = { LESSON30_LIGHT_COLOR_R, LESSON30_LIGHT_COLOR_G, LESSON30_LIGHT_COLOR_B };
    lighting.light_intensity = LESSON30_LIGHT_INTENSITY;
    lighting.ambient = LESSON30_MATERIAL_AMBIENT;
    lighting.shininess = shininess;
    lighting.specular_strength = specular_strength;
    return lighting;
}

static ForgeGpuForwardSceneDrawInfo lesson30_forward_draw_info(
    ForgeGpuDemo *demo,
    Mat4 cam_vp,
    Vec3 eye_pos,
    float shininess,
    float specular_strength)
{
    Lesson30State *state = lesson30_state(demo);
    ForgeGpuForwardSceneDrawInfo draw_info;

    draw_info.cam_vp = cam_vp;
    draw_info.light_vp = state->light_vp;
    draw_info.eye_pos = eye_pos;
    draw_info.shadow_depth = state->shadow_depth;
    draw_info.fallback_texture = demo->lesson.white_texture;
    draw_info.material_sampler = demo->lesson.samplers[0];
    draw_info.shadow_sampler = demo->lesson.samplers[1];
    draw_info.lighting = lesson30_forward_lighting(shininess, specular_strength);
    return draw_info;
}

static void lesson30_draw_model_scene(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    const GpuSceneData *model,
    Mat4 placement,
    Mat4 cam_vp,
    Vec3 eye_pos)
{
    const ForgeGpuForwardSceneDrawInfo draw_info = lesson30_forward_draw_info(
        demo,
        cam_vp,
        eye_pos,
        LESSON30_MATERIAL_SHININESS,
        LESSON30_MATERIAL_SPECULAR_STRENGTH);

    ForgeGpuDrawForwardSceneModel(command_buffer, render_pass, model, placement, &draw_info);
}

static void lesson30_draw_floor(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    Mat4 cam_vp,
    Vec3 eye_pos)
{
    Lesson30State *state = lesson30_state(demo);
    ForgeGpuForwardSceneDrawInfo draw_info;
    GpuMaterial material;

    SDL_zero(material);
    material.base_color[0] = LESSON30_FLOOR_COLOR_R;
    material.base_color[1] = LESSON30_FLOOR_COLOR_G;
    material.base_color[2] = LESSON30_FLOOR_COLOR_B;
    material.base_color[3] = 1.0f;

    draw_info = lesson30_forward_draw_info(
        demo,
        cam_vp,
        eye_pos,
        LESSON30_FLOOR_SHININESS,
        LESSON30_FLOOR_SPECULAR_STRENGTH);
    ForgeGpuDrawForwardSceneBuffer(
        command_buffer,
        render_pass,
        state->floor_vertex_buffer,
        state->floor_index_buffer,
        SDL_GPU_INDEXELEMENTSIZE_16BIT,
        SDL_arraysize(kForgeGpuQuadIndices),
        mat4_identity(),
        &material,
        &draw_info);
}

static void lesson30_draw_floor_shadow(
    Lesson30State *state,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass)
{
    ForgeGpuDrawForwardShadowBuffer(
        command_buffer,
        render_pass,
        state->floor_vertex_buffer,
        state->floor_index_buffer,
        SDL_GPU_INDEXELEMENTSIZE_16BIT,
        SDL_arraysize(kForgeGpuQuadIndices),
        state->light_vp);
}

static void lesson30_draw_skybox(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    Mat4 view,
    Mat4 projection)
{
    Lesson30State *state = lesson30_state(demo);

    ForgeGpuDrawForwardSkybox(
        command_buffer,
        render_pass,
        state->cubemap_texture,
        demo->lesson.samplers[3],
        state->skybox_vertex_buffer,
        state->skybox_index_buffer,
        SDL_GPU_INDEXELEMENTSIZE_16BIT,
        SDL_arraysize(kForgeGpuCubeIndices),
        view,
        projection);
}

static bool lesson30_run_shadow_pass(ForgeGpuDemo *demo, SDL_GPUCommandBuffer *command_buffer, Mat4 boat_placement, Mat4 rocks_placement)
{
    Lesson30State *state = lesson30_state(demo);
    SDL_GPURenderPass *render_pass;

    render_pass = ForgeGpuBeginDepthOnlyPass(command_buffer, state->shadow_depth, 1.0f);
    if (!render_pass) {
        return false;
    }
    SDL_BindGPUGraphicsPipeline(render_pass, state->shadow_pipeline);
    ForgeGpuDrawModelShadow(command_buffer, render_pass, &state->models[LESSON30_MODEL_BOAT], boat_placement, state->light_vp);
    ForgeGpuDrawModelShadow(command_buffer, render_pass, &state->models[LESSON30_MODEL_ROCKS], rocks_placement, state->light_vp);
    lesson30_draw_floor_shadow(state, command_buffer, render_pass);
    SDL_EndGPURenderPass(render_pass);
    return true;
}

static bool lesson30_run_reflection_pass(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    Mat4 reflected_vp,
    Mat4 reflected_view,
    Mat4 projection,
    Mat4 boat_placement,
    Mat4 rocks_placement,
    Vec3 reflected_eye)
{
    Lesson30State *state = lesson30_state(demo);
    SDL_GPUColorTargetInfo color_target;
    SDL_GPUDepthStencilTargetInfo depth_target;
    SDL_GPURenderPass *render_pass;

    SDL_zero(color_target);
    color_target.texture = state->reflection_color;
    color_target.load_op = SDL_GPU_LOADOP_CLEAR;
    color_target.store_op = SDL_GPU_STOREOP_STORE;
    color_target.clear_color = { LESSON30_CLEAR_R, LESSON30_CLEAR_G, LESSON30_CLEAR_B, 1.0f };
    SDL_zero(depth_target);
    depth_target.texture = state->reflection_depth;
    depth_target.load_op = SDL_GPU_LOADOP_CLEAR;
    depth_target.store_op = SDL_GPU_STOREOP_DONT_CARE;
    depth_target.clear_depth = 1.0f;

    render_pass = SDL_BeginGPURenderPass(command_buffer, &color_target, 1, &depth_target);
    if (!render_pass) {
        return false;
    }

    SDL_BindGPUGraphicsPipeline(render_pass, state->scene_reflection_pipeline);
    lesson30_draw_model_scene(demo, command_buffer, render_pass, &state->models[LESSON30_MODEL_BOAT], boat_placement, reflected_vp, reflected_eye);
    lesson30_draw_model_scene(demo, command_buffer, render_pass, &state->models[LESSON30_MODEL_ROCKS], rocks_placement, reflected_vp, reflected_eye);

    SDL_BindGPUGraphicsPipeline(render_pass, state->skybox_reflection_pipeline);
    lesson30_draw_skybox(demo, command_buffer, render_pass, reflected_view, projection);

    SDL_EndGPURenderPass(render_pass);
    state->reflection_rendered = true;
    return true;
}

static bool lesson30_run_main_pass(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPUTexture *swapchain_texture,
    Mat4 cam_vp,
    Mat4 view,
    Mat4 projection,
    Mat4 boat_placement,
    Mat4 rocks_placement)
{
    Lesson30State *state = lesson30_state(demo);
    SDL_GPUColorTargetInfo color_target;
    SDL_GPUDepthStencilTargetInfo depth_target;
    SDL_GPURenderPass *render_pass;

    SDL_zero(color_target);
    color_target.texture = swapchain_texture;
    color_target.load_op = SDL_GPU_LOADOP_CLEAR;
    color_target.store_op = SDL_GPU_STOREOP_STORE;
    color_target.clear_color = { LESSON30_CLEAR_R, LESSON30_CLEAR_G, LESSON30_CLEAR_B, 1.0f };
    SDL_zero(depth_target);
    depth_target.texture = state->main_depth;
    depth_target.load_op = SDL_GPU_LOADOP_CLEAR;
    depth_target.store_op = SDL_GPU_STOREOP_STORE;
    depth_target.clear_depth = 1.0f;

    render_pass = SDL_BeginGPURenderPass(command_buffer, &color_target, 1, &depth_target);
    if (!render_pass) {
        return false;
    }

    SDL_BindGPUGraphicsPipeline(render_pass, state->scene_pipeline);
    lesson30_draw_floor(demo, command_buffer, render_pass, cam_vp, demo->lesson.camera_position);
    lesson30_draw_model_scene(demo, command_buffer, render_pass, &state->models[LESSON30_MODEL_BOAT], boat_placement, cam_vp, demo->lesson.camera_position);
    lesson30_draw_model_scene(demo, command_buffer, render_pass, &state->models[LESSON30_MODEL_ROCKS], rocks_placement, cam_vp, demo->lesson.camera_position);

    SDL_BindGPUGraphicsPipeline(render_pass, state->skybox_pipeline);
    lesson30_draw_skybox(demo, command_buffer, render_pass, view, projection);

    SDL_EndGPURenderPass(render_pass);
    return true;
}

static bool lesson30_run_water_pass(ForgeGpuDemo *demo, SDL_GPUCommandBuffer *command_buffer, SDL_GPUTexture *swapchain_texture, Mat4 cam_vp)
{
    Lesson30State *state = lesson30_state(demo);
    SDL_GPUColorTargetInfo color_target;
    SDL_GPUDepthStencilTargetInfo depth_target;
    SDL_GPURenderPass *render_pass;
    Lesson30WaterVertUniforms vertex_uniforms;
    Lesson30WaterFragUniforms fragment_uniforms;
    SDL_GPUTextureSamplerBinding sampler_binding;
    SDL_GPUBufferBinding vertex_binding;
    SDL_GPUBufferBinding index_binding;

    SDL_zero(color_target);
    color_target.texture = swapchain_texture;
    color_target.load_op = SDL_GPU_LOADOP_LOAD;
    color_target.store_op = SDL_GPU_STOREOP_STORE;
    SDL_zero(depth_target);
    depth_target.texture = state->main_depth;
    depth_target.load_op = SDL_GPU_LOADOP_LOAD;
    depth_target.store_op = SDL_GPU_STOREOP_DONT_CARE;

    render_pass = SDL_BeginGPURenderPass(command_buffer, &color_target, 1, &depth_target);
    if (!render_pass) {
        return false;
    }

    SDL_BindGPUGraphicsPipeline(render_pass, state->water_pipeline);
    vertex_uniforms.mvp = cam_vp;
    SDL_PushGPUVertexUniformData(command_buffer, 0, &vertex_uniforms, sizeof(vertex_uniforms));

    SDL_zero(fragment_uniforms);
    fragment_uniforms.eye_pos[0] = demo->lesson.camera_position.x;
    fragment_uniforms.eye_pos[1] = demo->lesson.camera_position.y;
    fragment_uniforms.eye_pos[2] = demo->lesson.camera_position.z;
    fragment_uniforms.water_level = LESSON30_WATER_LEVEL;
    fragment_uniforms.water_tint[0] = LESSON30_WATER_TINT_R;
    fragment_uniforms.water_tint[1] = LESSON30_WATER_TINT_G;
    fragment_uniforms.water_tint[2] = LESSON30_WATER_TINT_B;
    fragment_uniforms.water_tint[3] = LESSON30_WATER_TINT_A;
    fragment_uniforms.fresnel_f0 = LESSON30_FRESNEL_F0;
    SDL_PushGPUFragmentUniformData(command_buffer, 0, &fragment_uniforms, sizeof(fragment_uniforms));

    SDL_zero(sampler_binding);
    sampler_binding.texture = state->reflection_color;
    sampler_binding.sampler = demo->lesson.samplers[2];
    SDL_BindGPUFragmentSamplers(render_pass, 0, &sampler_binding, 1);

    SDL_zero(vertex_binding);
    vertex_binding.buffer = state->water_vertex_buffer;
    SDL_BindGPUVertexBuffers(render_pass, 0, &vertex_binding, 1);
    SDL_zero(index_binding);
    index_binding.buffer = state->water_index_buffer;
    SDL_BindGPUIndexBuffer(render_pass, &index_binding, SDL_GPU_INDEXELEMENTSIZE_16BIT);
    SDL_DrawGPUIndexedPrimitives(render_pass, SDL_arraysize(kForgeGpuQuadIndices), 1, 0, 0, 0);

    SDL_EndGPURenderPass(render_pass);
    state->water_rendered = true;
    return true;
}

bool ForgeGpuCreateLesson30(ForgeGpuDemo *demo)
{
    Lesson30State *state;

    if (!SDL_GPUTextureSupportsFormat(
            demo->device,
            demo->color_format,
            SDL_GPU_TEXTURETYPE_2D,
            SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER)) {
        SDL_SetError("lesson 30 requires the swapchain color format as a sampled color target");
        return false;
    }
    if (!SDL_GPUTextureSupportsFormat(
            demo->device,
            LESSON30_SHADOW_FORMAT,
            SDL_GPU_TEXTURETYPE_2D,
            SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER)) {
        SDL_SetError("lesson 30 requires sampled D32_FLOAT depth targets");
        return false;
    }
    if (!SDL_GPUTextureSupportsFormat(
            demo->device,
            SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB,
            SDL_GPU_TEXTURETYPE_CUBE,
            SDL_GPU_TEXTUREUSAGE_SAMPLER)) {
        SDL_SetError("lesson 30 requires sampled sRGB cube textures");
        return false;
    }

    state = (Lesson30State *)SDL_calloc(1, sizeof(*state));
    if (!state) {
        SDL_OutOfMemory();
        return false;
    }
    demo->lesson.private_state = state;
    demo->lesson.white_texture = ForgeGpuCreateWhiteTexture(demo->device);
    state->cubemap_texture = ForgeGpuLoadCubeTexture(demo, "skyboxes/citrus-orchard");
    state->shadow_depth = ForgeGpuCreateSampledDepthTexture(
        demo,
        LESSON30_SHADOW_MAP_SIZE,
        LESSON30_SHADOW_MAP_SIZE,
        LESSON30_SHADOW_FORMAT);
    if (!demo->lesson.white_texture ||
        !state->cubemap_texture ||
        !state->shadow_depth ||
        !lesson30_create_samplers(demo) ||
        !lesson30_create_geometry(demo) ||
        !ForgeGpuLoadSceneModel(demo, &state->models[LESSON30_MODEL_BOAT], "models/PlanarReflections/boat/scene.gltf") ||
        !ForgeGpuLoadSceneModel(demo, &state->models[LESSON30_MODEL_ROCKS], "models/PlanarReflections/rocks/scene.gltf") ||
        !lesson30_create_pipelines(demo)) {
        return false;
    }

    state->light_vp = ForgeGpuComputeDirectionalLightViewProjection(
        { LESSON30_LIGHT_DIR_X, LESSON30_LIGHT_DIR_Y, LESSON30_LIGHT_DIR_Z },
        LESSON30_LIGHT_DISTANCE,
        LESSON30_SHADOW_ORTHO_SIZE,
        LESSON30_SHADOW_NEAR,
        LESSON30_SHADOW_FAR,
        LESSON30_PARALLEL_THRESHOLD);
    lesson30_init_camera(demo);
    return true;
}

bool ForgeGpuRenderLesson30(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPUTexture *swapchain_texture,
    Uint32 width,
    Uint32 height)
{
    Lesson30State *state = lesson30_state(demo);
    Mat4 view;
    Mat4 projection;
    Mat4 cam_vp;
    Mat4 boat_placement;
    Mat4 rocks_placement;
    Mat4 reflected_view;
    Mat4 reflected_vp;
    Vec3 reflected_eye;
    bool camera_above_water;

    if (!state) {
        SDL_SetError("lesson 30 internal state is missing");
        return false;
    }
    if (!lesson30_ensure_targets(demo, width, height)) {
        return false;
    }

    ForgeGpuUpdateCameraFromInput(demo);
    ForgeGpuCameraViewProjection(demo, width, height, 200.0f, &view, &projection);
    cam_vp = mat4_multiply(projection, view);
    boat_placement = mat4_translate({ LESSON30_BOAT_POS_X, LESSON30_BOAT_POS_Y, LESSON30_BOAT_POS_Z });
    rocks_placement = mat4_scale(LESSON30_ROCK_SCALE);
    camera_above_water = demo->lesson.camera_position.y > LESSON30_WATER_LEVEL;
    state->reflection_rendered = false;
    state->water_rendered = false;

    reflected_eye = demo->lesson.camera_position;
    reflected_view = view;
    reflected_vp = cam_vp;
    if (camera_above_water) {
        const Mat4 reflect_matrix = lesson30_reflect(0.0f, 1.0f, 0.0f, -LESSON30_WATER_LEVEL);
        const Vec4 clip_plane_view = lesson30_transform_plane_to_view(mat4_multiply(view, reflect_matrix));
        const Mat4 oblique_projection = lesson30_oblique_near_plane(projection, clip_plane_view);

        reflected_eye.y = 2.0f * LESSON30_WATER_LEVEL - reflected_eye.y;
        reflected_view = mat4_multiply(view, reflect_matrix);
        reflected_vp = mat4_multiply(oblique_projection, reflected_view);
    }

    if (!lesson30_run_shadow_pass(demo, command_buffer, boat_placement, rocks_placement)) {
        return false;
    }
    if (camera_above_water &&
        !lesson30_run_reflection_pass(
            demo,
            command_buffer,
            reflected_vp,
            reflected_view,
            projection,
            boat_placement,
            rocks_placement,
            reflected_eye)) {
        return false;
    }
    if (!lesson30_run_main_pass(
            demo,
            command_buffer,
            swapchain_texture,
            cam_vp,
            view,
            projection,
            boat_placement,
            rocks_placement)) {
        return false;
    }
    if (camera_above_water && !lesson30_run_water_pass(demo, command_buffer, swapchain_texture, cam_vp)) {
        return false;
    }
    return true;
}

void ForgeGpuDebugLesson30(ForgeGpuDemo *demo)
{
    Lesson30State *state = lesson30_state(demo);

    if (!state) {
        return;
    }
    ImGui::Text("Targets: %ux%u", state->reflection_width, state->reflection_height);
    ImGui::Text("Reflection: %s", state->water_rendered ? "active" : "underwater guard");
    ImGui::Text("Shadow: D32 2048x2048");
}

void ForgeGpuExportLesson30Metrics(ForgeGpuDemo *demo)
{
    Lesson30State *state = lesson30_state(demo);

    if (!state) {
        return;
    }
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuPlanarReflections", 1.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuPlanarTargetsReady",
        state->reflection_color && state->reflection_depth && state->main_depth && state->shadow_depth ? 1.0 : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuPlanarReflectionRendered", state->reflection_rendered ? 1.0 : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuPlanarWaterRendered", state->water_rendered ? 1.0 : 0.0);
}

void ForgeGpuDestroyLesson30(ForgeGpuDemo *demo)
{
    Lesson30State *state = lesson30_state(demo);

    if (!state) {
        return;
    }
    for (int i = 0; i < LESSON30_MODEL_COUNT; i += 1) {
        ForgeGpuFreeSceneData(demo, &state->models[i]);
    }
    if (state->skybox_index_buffer) {
        SDL_ReleaseGPUBuffer(demo->device, state->skybox_index_buffer);
    }
    if (state->skybox_vertex_buffer) {
        SDL_ReleaseGPUBuffer(demo->device, state->skybox_vertex_buffer);
    }
    if (state->water_index_buffer) {
        SDL_ReleaseGPUBuffer(demo->device, state->water_index_buffer);
    }
    if (state->water_vertex_buffer) {
        SDL_ReleaseGPUBuffer(demo->device, state->water_vertex_buffer);
    }
    if (state->floor_index_buffer) {
        SDL_ReleaseGPUBuffer(demo->device, state->floor_index_buffer);
    }
    if (state->floor_vertex_buffer) {
        SDL_ReleaseGPUBuffer(demo->device, state->floor_vertex_buffer);
    }
    if (state->cubemap_texture) {
        SDL_ReleaseGPUTexture(demo->device, state->cubemap_texture);
    }
    if (state->shadow_depth) {
        SDL_ReleaseGPUTexture(demo->device, state->shadow_depth);
    }
    if (state->main_depth) {
        SDL_ReleaseGPUTexture(demo->device, state->main_depth);
    }
    if (state->reflection_depth) {
        SDL_ReleaseGPUTexture(demo->device, state->reflection_depth);
    }
    if (state->reflection_color) {
        SDL_ReleaseGPUTexture(demo->device, state->reflection_color);
    }
    if (state->water_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, state->water_pipeline);
    }
    if (state->skybox_reflection_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, state->skybox_reflection_pipeline);
    }
    if (state->skybox_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, state->skybox_pipeline);
    }
    if (state->scene_reflection_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, state->scene_reflection_pipeline);
    }
    if (state->scene_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, state->scene_pipeline);
    }
    if (state->shadow_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, state->shadow_pipeline);
    }
    SDL_free(state);
    demo->lesson.private_state = nullptr;
}
