#include "forge_gpu_lessons.h"

#include "forge_gpu_browser_status.h"
#include "forge_gpu_camera.h"
#include "forge_gpu_gpu_helpers.h"
#include "forge_gpu_lesson_common.h"
#include "forge_gpu_math.h"
#include "forge_gpu_shader_layouts.h"
#include "shaders/generated/forge_gpu_lesson_34_shaders.h"
#include "imgui.h"

#include <stddef.h>

#define LESSON34_SHADOW_MAP_SIZE 2048u
#define LESSON34_SHADOW_DEPTH_FORMAT SDL_GPU_TEXTUREFORMAT_D32_FLOAT
#define LESSON34_FOV_DEGREES 60.0f
#define LESSON34_NEAR_PLANE 0.1f
#define LESSON34_FAR_PLANE 200.0f
#define LESSON34_MOVE_SPEED 5.0f
#define LESSON34_MOUSE_SENSITIVITY 0.003f
#define LESSON34_PITCH_CLAMP 1.5f
#define LESSON34_GRID_HALF_SIZE 50.0f
#define LESSON34_GRID_INDEX_COUNT 6u
#define LESSON34_PORTAL_WIDTH 2.0f
#define LESSON34_PORTAL_HEIGHT 3.0f
#define LESSON34_PORTAL_THICKNESS 0.2f
#define LESSON34_OUTLINE_SCALE 1.04f
#define LESSON34_STENCIL_PORTAL 1u
#define LESSON34_STENCIL_OUTLINE 2u
#define LESSON34_SPHERE_LAT_SEGS 20
#define LESSON34_SPHERE_LON_SEGS 20
#define LESSON34_CUBE_COUNT 4
#define LESSON34_PORTAL_SPHERE_COUNT 3

struct Lesson34Vertex
{
    float position[3];
    float normal[3];
};

struct Lesson34SceneVertUniforms
{
    Mat4 mvp;
    Mat4 model;
    Mat4 light_vp;
};

struct Lesson34SceneFragUniforms
{
    float base_color[4];
    float eye_pos[3];
    float ambient;
    float light_dir[4];
    float light_color[3];
    float light_intensity;
    float shininess;
    float specular_str;
    float pad1[2];
    float tint[3];
    float pad0;
};

struct Lesson34GridVertUniforms
{
    Mat4 vp;
    Mat4 light_vp;
};

struct Lesson34GridFragUniforms
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
    float pad;
    float tint_color[4];
};

struct Lesson34OutlineFragUniforms
{
    float outline_color[4];
};

struct Lesson34Object
{
    Vec3 position;
    float scale;
    float color[4];
    bool outlined;
    float outline_r;
    float outline_g;
    float outline_b;
};

struct Lesson34State
{
    SDL_GPUGraphicsPipeline *shadow_pipeline;
    SDL_GPUGraphicsPipeline *mask_pipeline;
    SDL_GPUGraphicsPipeline *portal_pipeline;
    SDL_GPUGraphicsPipeline *main_pipeline;
    SDL_GPUGraphicsPipeline *frame_pipeline;
    SDL_GPUGraphicsPipeline *outline_write_pipeline;
    SDL_GPUGraphicsPipeline *outline_draw_pipeline;
    SDL_GPUGraphicsPipeline *grid_pipeline;
    SDL_GPUGraphicsPipeline *grid_portal_pipeline;
    SDL_GPUGraphicsPipeline *debug_pipeline;
    SDL_GPUTexture *shadow_depth;
    SDL_GPUTexture *main_depth;
    SDL_GPUSampler *nearest_clamp;
    SDL_GPUBuffer *cube_vertex_buffer;
    SDL_GPUBuffer *cube_index_buffer;
    SDL_GPUBuffer *sphere_vertex_buffer;
    SDL_GPUBuffer *sphere_index_buffer;
    SDL_GPUBuffer *portal_frame_vertex_buffer;
    SDL_GPUBuffer *portal_frame_index_buffer;
    SDL_GPUBuffer *portal_mask_vertex_buffer;
    SDL_GPUBuffer *portal_mask_index_buffer;
    SDL_GPUBuffer *grid_vertex_buffer;
    SDL_GPUBuffer *grid_index_buffer;
    Uint32 cube_index_count;
    Uint32 sphere_index_count;
    Uint32 portal_frame_index_count;
    Uint32 portal_mask_index_count;
    Uint32 main_depth_width;
    Uint32 main_depth_height;
    SDL_GPUTextureFormat depth_stencil_format;
    Lesson34Object cubes[LESSON34_CUBE_COUNT];
    Lesson34Object portal_spheres[LESSON34_PORTAL_SPHERE_COUNT];
    Vec3 light_dir;
    Mat4 light_vp;
    bool show_stencil_debug;
    bool shadow_pass_rendered;
    bool main_pass_rendered;
};

static_assert(sizeof(Lesson34Vertex) == 24, "lesson 34 vertex size must match HLSL layout");
static_assert(sizeof(Lesson34SceneVertUniforms) == 192, "lesson 34 scene vertex uniform size must match HLSL layout");
static_assert(sizeof(Lesson34SceneFragUniforms) == 96, "lesson 34 scene fragment uniform size must match strict cbuffer layout");
static_assert(sizeof(Lesson34GridVertUniforms) == 128, "lesson 34 grid vertex uniform size must match HLSL layout");
static_assert(sizeof(Lesson34GridFragUniforms) == 96, "lesson 34 grid fragment uniform size must match HLSL layout");
static_assert(sizeof(Lesson34OutlineFragUniforms) == 16, "lesson 34 outline fragment uniform size must match HLSL layout");

static Lesson34State *lesson34_state(ForgeGpuDemo *demo)
{
    return (Lesson34State *)demo->lesson.private_state;
}

static void lesson34_init_camera(ForgeGpuDemo *demo)
{
    demo->lesson.camera_position = { 1.0f, 2.5f, 5.0f };
    demo->lesson.camera_yaw = -0.15f;
    demo->lesson.camera_pitch = -0.25f;
    demo->lesson.pitch_clamp = LESSON34_PITCH_CLAMP;
    demo->lesson.mouse_sensitivity = LESSON34_MOUSE_SENSITIVITY;
    demo->lesson.move_speed = LESSON34_MOVE_SPEED;
    demo->lesson.last_ticks = SDL_GetTicks();
}

static void lesson34_add_box(
    float cx,
    float cy,
    float cz,
    float hx,
    float hy,
    float hz,
    Lesson34Vertex *vertices,
    Uint32 *vertex_count,
    Uint16 *indices,
    Uint32 *index_count)
{
    const Uint16 base = (Uint16)*vertex_count;
    Uint32 v = *vertex_count;
    Uint32 i = *index_count;
    const float faces[6][4][3] = {
        { { -hx, -hy, hz }, { hx, -hy, hz }, { hx, hy, hz }, { -hx, hy, hz } },
        { { hx, -hy, -hz }, { -hx, -hy, -hz }, { -hx, hy, -hz }, { hx, hy, -hz } },
        { { hx, -hy, hz }, { hx, -hy, -hz }, { hx, hy, -hz }, { hx, hy, hz } },
        { { -hx, -hy, -hz }, { -hx, -hy, hz }, { -hx, hy, hz }, { -hx, hy, -hz } },
        { { -hx, hy, hz }, { hx, hy, hz }, { hx, hy, -hz }, { -hx, hy, -hz } },
        { { -hx, -hy, -hz }, { hx, -hy, -hz }, { hx, -hy, hz }, { -hx, -hy, hz } },
    };
    const float normals[6][3] = {
        { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, -1.0f }, { 1.0f, 0.0f, 0.0f },
        { -1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, -1.0f, 0.0f },
    };

    for (int face = 0; face < 6; face += 1) {
        for (int corner = 0; corner < 4; corner += 1) {
            vertices[v].position[0] = cx + faces[face][corner][0];
            vertices[v].position[1] = cy + faces[face][corner][1];
            vertices[v].position[2] = cz + faces[face][corner][2];
            vertices[v].normal[0] = normals[face][0];
            vertices[v].normal[1] = normals[face][1];
            vertices[v].normal[2] = normals[face][2];
            v += 1;
        }
        const Uint16 face_base = (Uint16)(base + face * 4);
        indices[i++] = (Uint16)(face_base + 0);
        indices[i++] = (Uint16)(face_base + 1);
        indices[i++] = (Uint16)(face_base + 2);
        indices[i++] = (Uint16)(face_base + 0);
        indices[i++] = (Uint16)(face_base + 2);
        indices[i++] = (Uint16)(face_base + 3);
    }

    *vertex_count = v;
    *index_count = i;
}

static void lesson34_generate_cube(
    Lesson34Vertex vertices[24],
    Uint32 *vertex_count,
    Uint16 indices[36],
    Uint32 *index_count)
{
    *vertex_count = 0;
    *index_count = 0;
    lesson34_add_box(0.0f, 0.0f, 0.0f, 0.5f, 0.5f, 0.5f, vertices, vertex_count, indices, index_count);
}

static void lesson34_generate_sphere(
    Lesson34Vertex *vertices,
    Uint32 *vertex_count,
    Uint16 *indices,
    Uint32 *index_count)
{
    Uint32 v = 0;
    Uint32 idx = 0;

    for (int lat = 0; lat <= LESSON34_SPHERE_LAT_SEGS; lat += 1) {
        const float theta = (float)lat * FORGE_GPU_PI / (float)LESSON34_SPHERE_LAT_SEGS;
        const float sin_theta = SDL_sinf(theta);
        const float cos_theta = SDL_cosf(theta);

        for (int lon = 0; lon <= LESSON34_SPHERE_LON_SEGS; lon += 1) {
            const float phi = (float)lon * 2.0f * FORGE_GPU_PI / (float)LESSON34_SPHERE_LON_SEGS;
            const float sin_phi = SDL_sinf(phi);
            const float cos_phi = SDL_cosf(phi);
            const float x = cos_phi * sin_theta;
            const float y = cos_theta;
            const float z = sin_phi * sin_theta;

            vertices[v].position[0] = x;
            vertices[v].position[1] = y;
            vertices[v].position[2] = z;
            vertices[v].normal[0] = x;
            vertices[v].normal[1] = y;
            vertices[v].normal[2] = z;
            v += 1;
        }
    }

    for (int lat = 0; lat < LESSON34_SPHERE_LAT_SEGS; lat += 1) {
        for (int lon = 0; lon < LESSON34_SPHERE_LON_SEGS; lon += 1) {
            const Uint16 a = (Uint16)(lat * (LESSON34_SPHERE_LON_SEGS + 1) + lon);
            const Uint16 b = (Uint16)(a + LESSON34_SPHERE_LON_SEGS + 1);

            if (lat != 0) {
                indices[idx++] = a;
                indices[idx++] = b;
                indices[idx++] = (Uint16)(a + 1);
            }
            if (lat != LESSON34_SPHERE_LAT_SEGS - 1) {
                indices[idx++] = (Uint16)(a + 1);
                indices[idx++] = b;
                indices[idx++] = (Uint16)(b + 1);
            }
        }
    }

    *vertex_count = v;
    *index_count = idx;
}

static void lesson34_generate_portal_frame(
    Lesson34Vertex vertices[96],
    Uint32 *vertex_count,
    Uint16 indices[144],
    Uint32 *index_count)
{
    const float half_width = LESSON34_PORTAL_WIDTH * 0.5f;
    const float height = LESSON34_PORTAL_HEIGHT;
    const float thickness = LESSON34_PORTAL_THICKNESS;
    const float half_thickness = thickness * 0.5f;
    const float half_depth = thickness * 0.5f;

    *vertex_count = 0;
    *index_count = 0;
    lesson34_add_box(-(half_width + half_thickness), height * 0.5f, 0.0f, half_thickness, height * 0.5f, half_depth, vertices, vertex_count, indices, index_count);
    lesson34_add_box(half_width + half_thickness, height * 0.5f, 0.0f, half_thickness, height * 0.5f, half_depth, vertices, vertex_count, indices, index_count);
    lesson34_add_box(0.0f, height + half_thickness, 0.0f, half_width + thickness, half_thickness, half_depth, vertices, vertex_count, indices, index_count);
    lesson34_add_box(0.0f, half_thickness * 0.5f, 0.0f, half_width + thickness, half_thickness * 0.5f, half_depth, vertices, vertex_count, indices, index_count);
}

static void lesson34_generate_portal_mask(
    Lesson34Vertex vertices[4],
    Uint32 *vertex_count,
    Uint16 indices[6],
    Uint32 *index_count)
{
    const float half_width = LESSON34_PORTAL_WIDTH * 0.5f;
    const float height = LESSON34_PORTAL_HEIGHT;

    vertices[0] = { { -half_width, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } };
    vertices[1] = { { half_width, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } };
    vertices[2] = { { half_width, height, 0.0f }, { 0.0f, 0.0f, 1.0f } };
    vertices[3] = { { -half_width, height, 0.0f }, { 0.0f, 0.0f, 1.0f } };

    indices[0] = 0;
    indices[1] = 1;
    indices[2] = 2;
    indices[3] = 0;
    indices[4] = 2;
    indices[5] = 3;
    *vertex_count = 4;
    *index_count = 6;
}

static SDL_GPUTextureFormat lesson34_select_depth_stencil_format(SDL_GPUDevice *device)
{
    if (SDL_GPUTextureSupportsFormat(
            device,
            SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT,
            SDL_GPU_TEXTURETYPE_2D,
            SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET)) {
        return SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT;
    }
    if (SDL_GPUTextureSupportsFormat(
            device,
            SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT,
            SDL_GPU_TEXTURETYPE_2D,
            SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET)) {
        return SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT;
    }
    return SDL_GPU_TEXTUREFORMAT_INVALID;
}

static bool lesson34_ensure_main_depth(ForgeGpuDemo *demo, Lesson34State *state, Uint32 width, Uint32 height)
{
    SDL_GPUTextureCreateInfo texture_info;
    SDL_GPUTexture *new_texture;

    if (state->main_depth && state->main_depth_width == width && state->main_depth_height == height) {
        return true;
    }

    SDL_zero(texture_info);
    texture_info.type = SDL_GPU_TEXTURETYPE_2D;
    texture_info.format = state->depth_stencil_format;
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

    if (state->main_depth) {
        SDL_ReleaseGPUTexture(demo->device, state->main_depth);
    }
    state->main_depth = new_texture;
    state->main_depth_width = width;
    state->main_depth_height = height;
    return true;
}

static SDL_GPUGraphicsPipeline *lesson34_create_pipeline(
    ForgeGpuDemo *demo,
    SDL_GPUShader *vertex_shader,
    SDL_GPUShader *fragment_shader,
    const SDL_GPUVertexBufferDescription *vertex_buffer,
    Uint32 num_vertex_buffers,
    const SDL_GPUVertexAttribute *attributes,
    Uint32 num_attributes,
    Uint32 num_color_targets,
    bool color_write,
    bool alpha_blend,
    bool has_depth_stencil_target,
    SDL_GPUTextureFormat depth_stencil_format,
    bool depth_test,
    bool depth_write,
    SDL_GPUCompareOp depth_compare_op,
    bool stencil_test,
    SDL_GPUCompareOp stencil_compare_op,
    SDL_GPUStencilOp stencil_pass_op,
    Uint8 stencil_compare_mask,
    Uint8 stencil_write_mask,
    SDL_GPUCullMode cull_mode)
{
    SDL_GPUColorTargetDescription color_target;
    SDL_GPUGraphicsPipelineCreateInfo pipeline_info;

    SDL_zero(color_target);
    color_target.format = demo->color_format;
    if (!color_write) {
        color_target.blend_state.enable_color_write_mask = true;
        color_target.blend_state.color_write_mask = 0;
    }
    if (alpha_blend) {
        color_target.blend_state.enable_blend = true;
        color_target.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
        color_target.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        color_target.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
        color_target.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        color_target.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
        color_target.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
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
    pipeline_info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    pipeline_info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    pipeline_info.depth_stencil_state.enable_depth_test = depth_test;
    pipeline_info.depth_stencil_state.enable_depth_write = depth_write;
    pipeline_info.depth_stencil_state.compare_op = depth_compare_op;
    pipeline_info.depth_stencil_state.enable_stencil_test = stencil_test;
    if (stencil_test) {
        pipeline_info.depth_stencil_state.front_stencil_state.compare_op = stencil_compare_op;
        pipeline_info.depth_stencil_state.front_stencil_state.pass_op = stencil_pass_op;
        pipeline_info.depth_stencil_state.front_stencil_state.fail_op = SDL_GPU_STENCILOP_KEEP;
        pipeline_info.depth_stencil_state.front_stencil_state.depth_fail_op = SDL_GPU_STENCILOP_KEEP;
        pipeline_info.depth_stencil_state.back_stencil_state = pipeline_info.depth_stencil_state.front_stencil_state;
        pipeline_info.depth_stencil_state.compare_mask = stencil_compare_mask;
        pipeline_info.depth_stencil_state.write_mask = stencil_write_mask;
    }
    if (num_color_targets > 0) {
        pipeline_info.target_info.color_target_descriptions = &color_target;
    }
    pipeline_info.target_info.num_color_targets = num_color_targets;
    pipeline_info.target_info.has_depth_stencil_target = has_depth_stencil_target;
    pipeline_info.target_info.depth_stencil_format = depth_stencil_format;

    return SDL_CreateGPUGraphicsPipeline(demo->device, &pipeline_info);
}

static bool lesson34_create_pipelines(ForgeGpuDemo *demo)
{
    Lesson34State *state = lesson34_state(demo);
    SDL_GPUShader *scene_vertex_shader = nullptr;
    SDL_GPUShader *scene_fragment_shader = nullptr;
    SDL_GPUShader *shadow_vertex_shader = nullptr;
    SDL_GPUShader *shadow_fragment_shader = nullptr;
    SDL_GPUShader *grid_vertex_shader = nullptr;
    SDL_GPUShader *grid_fragment_shader = nullptr;
    SDL_GPUShader *outline_fragment_shader = nullptr;
    SDL_GPUVertexBufferDescription scene_vertex_buffer;
    SDL_GPUVertexAttribute scene_attributes[2];
    SDL_GPUVertexBufferDescription grid_vertex_buffer;
    SDL_GPUVertexAttribute grid_attribute;
    bool ok = false;

    scene_vertex_shader = ForgeGpuCreateShader(
        demo->device, SDL_GPU_SHADERSTAGE_VERTEX,
        lesson34_scene_vert_wgsl, lesson34_scene_vert_wgsl_size,
        lesson34_scene_vert_msl, lesson34_scene_vert_msl_size,
        0, 0, 0, 1);
    scene_fragment_shader = ForgeGpuCreateShaderWithResourceLayout(
        demo->device,
        lesson34_scene_frag_wgsl, lesson34_scene_frag_wgsl_size,
        lesson34_scene_frag_msl, lesson34_scene_frag_msl_size,
        ForgeGpuShaderLayout_lesson34_scene_frag());
    shadow_vertex_shader = ForgeGpuCreateShader(
        demo->device, SDL_GPU_SHADERSTAGE_VERTEX,
        lesson34_shadow_vert_wgsl, lesson34_shadow_vert_wgsl_size,
        lesson34_shadow_vert_msl, lesson34_shadow_vert_msl_size,
        0, 0, 0, 1);
    shadow_fragment_shader = ForgeGpuCreateShader(
        demo->device, SDL_GPU_SHADERSTAGE_FRAGMENT,
        lesson34_shadow_frag_wgsl, lesson34_shadow_frag_wgsl_size,
        lesson34_shadow_frag_msl, lesson34_shadow_frag_msl_size,
        0, 0, 0, 0);
    grid_vertex_shader = ForgeGpuCreateShader(
        demo->device, SDL_GPU_SHADERSTAGE_VERTEX,
        lesson34_grid_vert_wgsl, lesson34_grid_vert_wgsl_size,
        lesson34_grid_vert_msl, lesson34_grid_vert_msl_size,
        0, 0, 0, 1);
    grid_fragment_shader = ForgeGpuCreateShaderWithResourceLayout(
        demo->device,
        lesson34_grid_frag_wgsl, lesson34_grid_frag_wgsl_size,
        lesson34_grid_frag_msl, lesson34_grid_frag_msl_size,
        ForgeGpuShaderLayout_lesson34_grid_frag());
    outline_fragment_shader = ForgeGpuCreateShader(
        demo->device, SDL_GPU_SHADERSTAGE_FRAGMENT,
        lesson34_outline_frag_wgsl, lesson34_outline_frag_wgsl_size,
        lesson34_outline_frag_msl, lesson34_outline_frag_msl_size,
        0, 0, 0, 1);
    if (!scene_vertex_shader || !scene_fragment_shader || !shadow_vertex_shader || !shadow_fragment_shader ||
        !grid_vertex_shader || !grid_fragment_shader || !outline_fragment_shader) {
        goto done;
    }

    SDL_zero(scene_vertex_buffer);
    scene_vertex_buffer.slot = 0;
    scene_vertex_buffer.pitch = sizeof(Lesson34Vertex);
    scene_vertex_buffer.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    SDL_zeroa(scene_attributes);
    scene_attributes[0].location = 0;
    scene_attributes[0].buffer_slot = 0;
    scene_attributes[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    scene_attributes[0].offset = offsetof(Lesson34Vertex, position);
    scene_attributes[1].location = 1;
    scene_attributes[1].buffer_slot = 0;
    scene_attributes[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    scene_attributes[1].offset = offsetof(Lesson34Vertex, normal);

    SDL_zero(grid_vertex_buffer);
    grid_vertex_buffer.slot = 0;
    grid_vertex_buffer.pitch = sizeof(Vec3);
    grid_vertex_buffer.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    SDL_zero(grid_attribute);
    grid_attribute.location = 0;
    grid_attribute.buffer_slot = 0;
    grid_attribute.format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    grid_attribute.offset = 0;

    state->shadow_pipeline = lesson34_create_pipeline(
        demo, shadow_vertex_shader, shadow_fragment_shader,
        &scene_vertex_buffer, 1, scene_attributes, 2,
        0, true, false, true, LESSON34_SHADOW_DEPTH_FORMAT,
        true, true, SDL_GPU_COMPAREOP_LESS,
        false, SDL_GPU_COMPAREOP_ALWAYS, SDL_GPU_STENCILOP_KEEP, 0, 0,
        SDL_GPU_CULLMODE_BACK);
    state->mask_pipeline = lesson34_create_pipeline(
        demo, scene_vertex_shader, scene_fragment_shader,
        &scene_vertex_buffer, 1, scene_attributes, 2,
        1, false, false, true, state->depth_stencil_format,
        true, false, SDL_GPU_COMPAREOP_LESS_OR_EQUAL,
        true, SDL_GPU_COMPAREOP_ALWAYS, SDL_GPU_STENCILOP_REPLACE, 0xFF, 0xFF,
        SDL_GPU_CULLMODE_BACK);
    state->portal_pipeline = lesson34_create_pipeline(
        demo, scene_vertex_shader, scene_fragment_shader,
        &scene_vertex_buffer, 1, scene_attributes, 2,
        1, true, false, true, state->depth_stencil_format,
        true, true, SDL_GPU_COMPAREOP_LESS,
        true, SDL_GPU_COMPAREOP_EQUAL, SDL_GPU_STENCILOP_KEEP, 0xFF, 0x00,
        SDL_GPU_CULLMODE_BACK);
    state->main_pipeline = lesson34_create_pipeline(
        demo, scene_vertex_shader, scene_fragment_shader,
        &scene_vertex_buffer, 1, scene_attributes, 2,
        1, true, false, true, state->depth_stencil_format,
        true, true, SDL_GPU_COMPAREOP_LESS,
        false, SDL_GPU_COMPAREOP_ALWAYS, SDL_GPU_STENCILOP_KEEP, 0, 0,
        SDL_GPU_CULLMODE_BACK);
    state->frame_pipeline = lesson34_create_pipeline(
        demo, scene_vertex_shader, scene_fragment_shader,
        &scene_vertex_buffer, 1, scene_attributes, 2,
        1, true, false, true, state->depth_stencil_format,
        true, true, SDL_GPU_COMPAREOP_LESS,
        false, SDL_GPU_COMPAREOP_ALWAYS, SDL_GPU_STENCILOP_KEEP, 0, 0,
        SDL_GPU_CULLMODE_BACK);
    state->outline_write_pipeline = lesson34_create_pipeline(
        demo, scene_vertex_shader, scene_fragment_shader,
        &scene_vertex_buffer, 1, scene_attributes, 2,
        1, true, false, true, state->depth_stencil_format,
        true, true, SDL_GPU_COMPAREOP_LESS_OR_EQUAL,
        true, SDL_GPU_COMPAREOP_ALWAYS, SDL_GPU_STENCILOP_REPLACE, 0xFF, 0xFF,
        SDL_GPU_CULLMODE_BACK);
    state->outline_draw_pipeline = lesson34_create_pipeline(
        demo, scene_vertex_shader, outline_fragment_shader,
        &scene_vertex_buffer, 1, scene_attributes, 2,
        1, true, false, true, state->depth_stencil_format,
        false, false, SDL_GPU_COMPAREOP_ALWAYS,
        true, SDL_GPU_COMPAREOP_NOT_EQUAL, SDL_GPU_STENCILOP_KEEP, 0xFF, 0x00,
        SDL_GPU_CULLMODE_NONE);
    state->grid_pipeline = lesson34_create_pipeline(
        demo, grid_vertex_shader, grid_fragment_shader,
        &grid_vertex_buffer, 1, &grid_attribute, 1,
        1, true, false, true, state->depth_stencil_format,
        true, true, SDL_GPU_COMPAREOP_LESS_OR_EQUAL,
        true, SDL_GPU_COMPAREOP_NOT_EQUAL, SDL_GPU_STENCILOP_KEEP, 0xFF, 0x00,
        SDL_GPU_CULLMODE_NONE);
    state->grid_portal_pipeline = lesson34_create_pipeline(
        demo, grid_vertex_shader, grid_fragment_shader,
        &grid_vertex_buffer, 1, &grid_attribute, 1,
        1, true, false, true, state->depth_stencil_format,
        true, true, SDL_GPU_COMPAREOP_LESS_OR_EQUAL,
        true, SDL_GPU_COMPAREOP_EQUAL, SDL_GPU_STENCILOP_KEEP, 0xFF, 0x00,
        SDL_GPU_CULLMODE_NONE);
    state->debug_pipeline = lesson34_create_pipeline(
        demo, scene_vertex_shader, outline_fragment_shader,
        &scene_vertex_buffer, 1, scene_attributes, 2,
        1, true, true, false, SDL_GPU_TEXTUREFORMAT_INVALID,
        false, false, SDL_GPU_COMPAREOP_ALWAYS,
        false, SDL_GPU_COMPAREOP_ALWAYS, SDL_GPU_STENCILOP_KEEP, 0, 0,
        SDL_GPU_CULLMODE_NONE);

    ok = state->shadow_pipeline && state->mask_pipeline && state->portal_pipeline && state->main_pipeline &&
        state->frame_pipeline && state->outline_write_pipeline && state->outline_draw_pipeline &&
        state->grid_pipeline && state->grid_portal_pipeline && state->debug_pipeline;

done:
    if (scene_vertex_shader) {
        SDL_ReleaseGPUShader(demo->device, scene_vertex_shader);
    }
    if (scene_fragment_shader) {
        SDL_ReleaseGPUShader(demo->device, scene_fragment_shader);
    }
    if (shadow_vertex_shader) {
        SDL_ReleaseGPUShader(demo->device, shadow_vertex_shader);
    }
    if (shadow_fragment_shader) {
        SDL_ReleaseGPUShader(demo->device, shadow_fragment_shader);
    }
    if (grid_vertex_shader) {
        SDL_ReleaseGPUShader(demo->device, grid_vertex_shader);
    }
    if (grid_fragment_shader) {
        SDL_ReleaseGPUShader(demo->device, grid_fragment_shader);
    }
    if (outline_fragment_shader) {
        SDL_ReleaseGPUShader(demo->device, outline_fragment_shader);
    }
    return ok;
}

static bool lesson34_create_geometry(ForgeGpuDemo *demo)
{
    Lesson34State *state = lesson34_state(demo);
    Uint32 vertex_count = 0;
    Uint32 index_count = 0;

    {
        Lesson34Vertex vertices[24];
        Uint16 indices[36];

        lesson34_generate_cube(vertices, &vertex_count, indices, &index_count);
        state->cube_vertex_buffer = ForgeGpuCreateBufferWithData(demo->device, SDL_GPU_BUFFERUSAGE_VERTEX, vertices, (Uint32)(vertex_count * sizeof(*vertices)));
        state->cube_index_buffer = ForgeGpuCreateBufferWithData(demo->device, SDL_GPU_BUFFERUSAGE_INDEX, indices, (Uint32)(index_count * sizeof(*indices)));
        state->cube_index_count = index_count;
    }
    {
        Lesson34Vertex vertices[(LESSON34_SPHERE_LAT_SEGS + 1) * (LESSON34_SPHERE_LON_SEGS + 1)];
        Uint16 indices[LESSON34_SPHERE_LAT_SEGS * LESSON34_SPHERE_LON_SEGS * 6];

        lesson34_generate_sphere(vertices, &vertex_count, indices, &index_count);
        state->sphere_vertex_buffer = ForgeGpuCreateBufferWithData(demo->device, SDL_GPU_BUFFERUSAGE_VERTEX, vertices, (Uint32)(vertex_count * sizeof(*vertices)));
        state->sphere_index_buffer = ForgeGpuCreateBufferWithData(demo->device, SDL_GPU_BUFFERUSAGE_INDEX, indices, (Uint32)(index_count * sizeof(*indices)));
        state->sphere_index_count = index_count;
    }
    {
        Lesson34Vertex vertices[96];
        Uint16 indices[144];

        lesson34_generate_portal_frame(vertices, &vertex_count, indices, &index_count);
        state->portal_frame_vertex_buffer = ForgeGpuCreateBufferWithData(demo->device, SDL_GPU_BUFFERUSAGE_VERTEX, vertices, (Uint32)(vertex_count * sizeof(*vertices)));
        state->portal_frame_index_buffer = ForgeGpuCreateBufferWithData(demo->device, SDL_GPU_BUFFERUSAGE_INDEX, indices, (Uint32)(index_count * sizeof(*indices)));
        state->portal_frame_index_count = index_count;
    }
    {
        Lesson34Vertex vertices[4];
        Uint16 indices[6];

        lesson34_generate_portal_mask(vertices, &vertex_count, indices, &index_count);
        state->portal_mask_vertex_buffer = ForgeGpuCreateBufferWithData(demo->device, SDL_GPU_BUFFERUSAGE_VERTEX, vertices, (Uint32)(vertex_count * sizeof(*vertices)));
        state->portal_mask_index_buffer = ForgeGpuCreateBufferWithData(demo->device, SDL_GPU_BUFFERUSAGE_INDEX, indices, (Uint32)(index_count * sizeof(*indices)));
        state->portal_mask_index_count = index_count;
    }
    {
        const Vec3 vertices[4] = {
            { -LESSON34_GRID_HALF_SIZE, 0.0f, -LESSON34_GRID_HALF_SIZE },
            { LESSON34_GRID_HALF_SIZE, 0.0f, -LESSON34_GRID_HALF_SIZE },
            { LESSON34_GRID_HALF_SIZE, 0.0f, LESSON34_GRID_HALF_SIZE },
            { -LESSON34_GRID_HALF_SIZE, 0.0f, LESSON34_GRID_HALF_SIZE },
        };
        const Uint16 indices[6] = { 0, 1, 2, 0, 2, 3 };

        state->grid_vertex_buffer = ForgeGpuCreateBufferWithData(demo->device, SDL_GPU_BUFFERUSAGE_VERTEX, vertices, sizeof(vertices));
        state->grid_index_buffer = ForgeGpuCreateBufferWithData(demo->device, SDL_GPU_BUFFERUSAGE_INDEX, indices, sizeof(indices));
    }

    return state->cube_vertex_buffer && state->cube_index_buffer &&
        state->sphere_vertex_buffer && state->sphere_index_buffer &&
        state->portal_frame_vertex_buffer && state->portal_frame_index_buffer &&
        state->portal_mask_vertex_buffer && state->portal_mask_index_buffer &&
        state->grid_vertex_buffer && state->grid_index_buffer;
}

static void lesson34_init_scene(Lesson34State *state)
{
    state->cubes[0] = { { 2.0f, 0.5f, -3.0f }, 1.0f, { 0.8f, 0.2f, 0.2f, 1.0f }, false, 0.0f, 0.0f, 0.0f };
    state->cubes[1] = { { -1.0f, 0.5f, -2.0f }, 1.0f, { 0.2f, 0.3f, 0.8f, 1.0f }, true, 1.0f, 1.0f, 0.0f };
    state->cubes[2] = { { -1.0f, 1.5f, -2.0f }, 0.8f, { 0.2f, 0.8f, 0.8f, 1.0f }, false, 0.0f, 0.0f, 0.0f };
    state->cubes[3] = { { 3.0f, 0.5f, 1.0f }, 1.2f, { 0.2f, 0.7f, 0.3f, 1.0f }, true, 0.0f, 1.0f, 0.3f };

    state->portal_spheres[0] = { { 0.0f, 1.0f, -7.0f }, 0.8f, { 1.0f, 0.8f, 0.2f, 1.0f }, false, 0.0f, 0.0f, 0.0f };
    state->portal_spheres[1] = { { -1.5f, 0.6f, -6.0f }, 0.6f, { 0.8f, 0.2f, 0.8f, 1.0f }, false, 0.0f, 0.0f, 0.0f };
    state->portal_spheres[2] = { { 1.2f, 0.5f, -8.0f }, 0.5f, { 0.2f, 0.8f, 0.8f, 1.0f }, false, 0.0f, 0.0f, 0.0f };

    state->light_dir = vec3_normalize({ 0.4f, -0.8f, -0.6f });
    {
        const Vec3 light_position = vec3_scale(state->light_dir, -30.0f);
        const Mat4 light_view = mat4_look_at(light_position, { 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f });
        const Mat4 light_projection = mat4_orthographic(-15.0f, 15.0f, -15.0f, 15.0f, 0.1f, 60.0f);
        state->light_vp = mat4_multiply(light_projection, light_view);
    }
}

static Mat4 lesson34_object_model(const Lesson34Object *object)
{
    return mat4_multiply(mat4_translate(object->position), mat4_scale(object->scale));
}

static void lesson34_push_scene_vertex_uniforms(
    SDL_GPUCommandBuffer *command_buffer,
    Mat4 view_projection,
    Mat4 light_vp,
    Mat4 model)
{
    Lesson34SceneVertUniforms uniforms;

    uniforms.mvp = mat4_multiply(view_projection, model);
    uniforms.model = model;
    uniforms.light_vp = mat4_multiply(light_vp, model);
    SDL_PushGPUVertexUniformData(command_buffer, 0, &uniforms, sizeof(uniforms));
}

static void lesson34_fill_scene_fragment_uniforms(
    Lesson34SceneFragUniforms *uniforms,
    const float color[4],
    Vec3 eye_position,
    Vec3 light_dir,
    float ambient,
    float light_r,
    float light_g,
    float light_b,
    float light_intensity,
    float shininess,
    float specular_strength,
    float tint_r,
    float tint_g,
    float tint_b)
{
    SDL_zero(*uniforms);
    uniforms->base_color[0] = color[0];
    uniforms->base_color[1] = color[1];
    uniforms->base_color[2] = color[2];
    uniforms->base_color[3] = color[3];
    uniforms->eye_pos[0] = eye_position.x;
    uniforms->eye_pos[1] = eye_position.y;
    uniforms->eye_pos[2] = eye_position.z;
    uniforms->ambient = ambient;
    uniforms->light_dir[0] = light_dir.x;
    uniforms->light_dir[1] = light_dir.y;
    uniforms->light_dir[2] = light_dir.z;
    uniforms->light_dir[3] = 0.0f;
    uniforms->light_color[0] = light_r;
    uniforms->light_color[1] = light_g;
    uniforms->light_color[2] = light_b;
    uniforms->light_intensity = light_intensity;
    uniforms->shininess = shininess;
    uniforms->specular_str = specular_strength;
    uniforms->tint[0] = tint_r;
    uniforms->tint[1] = tint_g;
    uniforms->tint[2] = tint_b;
}

static void lesson34_bind_indexed_geometry(
    SDL_GPURenderPass *render_pass,
    SDL_GPUBuffer *vertex_buffer,
    SDL_GPUBuffer *index_buffer)
{
    SDL_GPUBufferBinding vertex_binding = { vertex_buffer, 0 };
    SDL_GPUBufferBinding index_binding = { index_buffer, 0 };

    SDL_BindGPUVertexBuffers(render_pass, 0, &vertex_binding, 1);
    SDL_BindGPUIndexBuffer(render_pass, &index_binding, SDL_GPU_INDEXELEMENTSIZE_16BIT);
}

static void lesson34_draw_scene_object(
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    SDL_GPUBuffer *vertex_buffer,
    SDL_GPUBuffer *index_buffer,
    Uint32 index_count,
    Mat4 view_projection,
    Mat4 light_vp,
    Vec3 eye_position,
    Vec3 light_dir,
    const Lesson34Object *object,
    float ambient,
    float light_r,
    float light_g,
    float light_b,
    float light_intensity,
    float shininess,
    float specular_strength,
    float tint_r,
    float tint_g,
    float tint_b)
{
    Lesson34SceneFragUniforms fragment_uniforms;
    const Mat4 model = lesson34_object_model(object);

    lesson34_push_scene_vertex_uniforms(command_buffer, view_projection, light_vp, model);
    lesson34_fill_scene_fragment_uniforms(
        &fragment_uniforms,
        object->color,
        eye_position,
        light_dir,
        ambient,
        light_r,
        light_g,
        light_b,
        light_intensity,
        shininess,
        specular_strength,
        tint_r,
        tint_g,
        tint_b);
    SDL_PushGPUFragmentUniformData(command_buffer, 0, &fragment_uniforms, sizeof(fragment_uniforms));
    lesson34_bind_indexed_geometry(render_pass, vertex_buffer, index_buffer);
    SDL_DrawGPUIndexedPrimitives(render_pass, index_count, 1, 0, 0, 0);
}

static void lesson34_draw_shadow_geometry(
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    SDL_GPUBuffer *vertex_buffer,
    SDL_GPUBuffer *index_buffer,
    Uint32 index_count,
    Mat4 light_vp,
    Mat4 model)
{
    const Mat4 shadow_mvp = mat4_multiply(light_vp, model);

    SDL_PushGPUVertexUniformData(command_buffer, 0, &shadow_mvp, sizeof(shadow_mvp));
    lesson34_bind_indexed_geometry(render_pass, vertex_buffer, index_buffer);
    SDL_DrawGPUIndexedPrimitives(render_pass, index_count, 1, 0, 0, 0);
}

static void lesson34_draw_debug_geometry(
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    SDL_GPUBuffer *vertex_buffer,
    SDL_GPUBuffer *index_buffer,
    Uint32 index_count,
    Mat4 view_projection,
    Mat4 light_vp,
    Mat4 model,
    const float color[4])
{
    Lesson34OutlineFragUniforms fragment_uniforms;

    lesson34_push_scene_vertex_uniforms(command_buffer, view_projection, light_vp, model);
    SDL_memcpy(fragment_uniforms.outline_color, color, sizeof(fragment_uniforms.outline_color));
    SDL_PushGPUFragmentUniformData(command_buffer, 0, &fragment_uniforms, sizeof(fragment_uniforms));
    lesson34_bind_indexed_geometry(render_pass, vertex_buffer, index_buffer);
    SDL_DrawGPUIndexedPrimitives(render_pass, index_count, 1, 0, 0, 0);
}

static void lesson34_draw_grid(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    Lesson34State *state,
    Mat4 view_projection,
    const SDL_GPUTextureSamplerBinding *shadow_binding,
    bool portal_grid)
{
    Lesson34GridVertUniforms vertex_uniforms;
    Lesson34GridFragUniforms fragment_uniforms;

    vertex_uniforms.vp = view_projection;
    vertex_uniforms.light_vp = state->light_vp;

    SDL_zero(fragment_uniforms);
    fragment_uniforms.line_color[0] = 0.35f;
    fragment_uniforms.line_color[1] = 0.35f;
    fragment_uniforms.line_color[2] = 0.4f;
    fragment_uniforms.line_color[3] = 1.0f;
    fragment_uniforms.bg_color[0] = 0.08f;
    fragment_uniforms.bg_color[1] = 0.08f;
    fragment_uniforms.bg_color[2] = 0.1f;
    fragment_uniforms.bg_color[3] = 1.0f;
    fragment_uniforms.light_dir[0] = state->light_dir.x;
    fragment_uniforms.light_dir[1] = state->light_dir.y;
    fragment_uniforms.light_dir[2] = state->light_dir.z;
    fragment_uniforms.light_intensity = 0.6f;
    fragment_uniforms.eye_pos[0] = demo->lesson.camera_position.x;
    fragment_uniforms.eye_pos[1] = demo->lesson.camera_position.y;
    fragment_uniforms.eye_pos[2] = demo->lesson.camera_position.z;
    fragment_uniforms.grid_spacing = 1.0f;
    fragment_uniforms.line_width = 0.02f;
    fragment_uniforms.fade_distance = 40.0f;
    fragment_uniforms.ambient = 0.15f;
    fragment_uniforms.tint_color[0] = portal_grid ? 1.3f : 1.0f;
    fragment_uniforms.tint_color[1] = portal_grid ? 0.9f : 1.0f;
    fragment_uniforms.tint_color[2] = portal_grid ? 0.6f : 1.0f;
    fragment_uniforms.tint_color[3] = 1.0f;

    SDL_BindGPUGraphicsPipeline(render_pass, portal_grid ? state->grid_portal_pipeline : state->grid_pipeline);
    SDL_SetGPUStencilReference(render_pass, LESSON34_STENCIL_PORTAL);
    SDL_BindGPUFragmentSamplers(render_pass, 0, shadow_binding, 1);
    SDL_PushGPUVertexUniformData(command_buffer, 0, &vertex_uniforms, sizeof(vertex_uniforms));
    SDL_PushGPUFragmentUniformData(command_buffer, 0, &fragment_uniforms, sizeof(fragment_uniforms));
    lesson34_bind_indexed_geometry(render_pass, state->grid_vertex_buffer, state->grid_index_buffer);
    SDL_DrawGPUIndexedPrimitives(render_pass, LESSON34_GRID_INDEX_COUNT, 1, 0, 0, 0);
}

bool ForgeGpuCreateLesson34(ForgeGpuDemo *demo)
{
    Lesson34State *state = (Lesson34State *)SDL_calloc(1, sizeof(*state));

    if (!state) {
        SDL_OutOfMemory();
        return false;
    }
    demo->lesson.private_state = state;

    state->depth_stencil_format = lesson34_select_depth_stencil_format(demo->device);
    if (state->depth_stencil_format == SDL_GPU_TEXTUREFORMAT_INVALID) {
        SDL_SetError("lesson 34 requires a depth-stencil target format with stencil");
        goto fail;
    }
    if (!SDL_GPUTextureSupportsFormat(
            demo->device,
            LESSON34_SHADOW_DEPTH_FORMAT,
            SDL_GPU_TEXTURETYPE_2D,
            SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER)) {
        SDL_SetError("lesson 34 requires sampled D32_FLOAT depth targets");
        goto fail;
    }

    state->shadow_depth = ForgeGpuCreateSampledDepthTexture(demo, LESSON34_SHADOW_MAP_SIZE, LESSON34_SHADOW_MAP_SIZE, LESSON34_SHADOW_DEPTH_FORMAT);
    state->nearest_clamp = ForgeGpuCreateSamplerWithAddress(
        demo->device,
        SDL_GPU_FILTER_NEAREST,
        SDL_GPU_FILTER_NEAREST,
        SDL_GPU_SAMPLERMIPMAPMODE_NEAREST,
        SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        0.0f);
    if (!state->shadow_depth || !state->nearest_clamp) {
        goto fail;
    }

    lesson34_init_camera(demo);
    lesson34_init_scene(state);
    if (lesson34_create_pipelines(demo) && lesson34_create_geometry(demo)) {
        return true;
    }

fail:
    ForgeGpuDestroyLesson34(demo);
    return false;
}

bool ForgeGpuRenderLesson34(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPUTexture *swapchain_texture,
    Uint32 width,
    Uint32 height)
{
    Lesson34State *state = lesson34_state(demo);
    const Vec3 portal_position = { 0.0f, 0.0f, -5.0f };
    const float aspect = height > 0 ? (float)width / (float)height : 1.0f;
    ForgeGpuUpdateCameraFromInput(demo);

    const Quat camera_orientation = quat_from_euler(demo->lesson.camera_yaw, demo->lesson.camera_pitch, 0.0f);
    const Mat4 view = mat4_view_from_quat(demo->lesson.camera_position, camera_orientation);
    const Mat4 projection = mat4_perspective(LESSON34_FOV_DEGREES * FORGE_GPU_DEG2RAD, aspect, LESSON34_NEAR_PLANE, LESSON34_FAR_PLANE);
    const Mat4 view_projection = mat4_multiply(projection, view);
    SDL_GPUTextureSamplerBinding shadow_binding;
    SDL_GPUDepthStencilTargetInfo shadow_depth_target;
    SDL_GPUColorTargetInfo color_target;
    SDL_GPUDepthStencilTargetInfo depth_stencil_target;
    SDL_GPURenderPass *render_pass;

    if (!state) {
        SDL_SetError("lesson 34 state is missing");
        return false;
    }
    if (!lesson34_ensure_main_depth(demo, state, width, height)) {
        return false;
    }

    SDL_zero(shadow_depth_target);
    shadow_depth_target.texture = state->shadow_depth;
    shadow_depth_target.load_op = SDL_GPU_LOADOP_CLEAR;
    shadow_depth_target.store_op = SDL_GPU_STOREOP_STORE;
    shadow_depth_target.clear_depth = 1.0f;
    render_pass = SDL_BeginGPURenderPass(command_buffer, nullptr, 0, &shadow_depth_target);
    if (!render_pass) {
        return false;
    }
    SDL_BindGPUGraphicsPipeline(render_pass, state->shadow_pipeline);
    for (int i = 0; i < LESSON34_CUBE_COUNT; i += 1) {
        lesson34_draw_shadow_geometry(
            command_buffer,
            render_pass,
            state->cube_vertex_buffer,
            state->cube_index_buffer,
            state->cube_index_count,
            state->light_vp,
            lesson34_object_model(&state->cubes[i]));
    }
    lesson34_draw_shadow_geometry(
        command_buffer,
        render_pass,
        state->portal_frame_vertex_buffer,
        state->portal_frame_index_buffer,
        state->portal_frame_index_count,
        state->light_vp,
        mat4_translate(portal_position));
    SDL_EndGPURenderPass(render_pass);
    state->shadow_pass_rendered = true;

    shadow_binding.texture = state->shadow_depth;
    shadow_binding.sampler = state->nearest_clamp;

    SDL_zero(color_target);
    color_target.texture = swapchain_texture;
    color_target.load_op = SDL_GPU_LOADOP_CLEAR;
    color_target.store_op = SDL_GPU_STOREOP_STORE;
    color_target.clear_color = { 0.05f, 0.05f, 0.08f, 1.0f };
    SDL_zero(depth_stencil_target);
    depth_stencil_target.texture = state->main_depth;
    depth_stencil_target.load_op = SDL_GPU_LOADOP_CLEAR;
    depth_stencil_target.store_op = SDL_GPU_STOREOP_STORE;
    depth_stencil_target.clear_depth = 1.0f;
    depth_stencil_target.stencil_load_op = SDL_GPU_LOADOP_CLEAR;
    depth_stencil_target.stencil_store_op = SDL_GPU_STOREOP_STORE;
    depth_stencil_target.clear_stencil = 0;
    render_pass = SDL_BeginGPURenderPass(command_buffer, &color_target, 1, &depth_stencil_target);
    if (!render_pass) {
        return false;
    }

    SDL_BindGPUGraphicsPipeline(render_pass, state->main_pipeline);
    SDL_BindGPUFragmentSamplers(render_pass, 0, &shadow_binding, 1);
    for (int i = 0; i < LESSON34_CUBE_COUNT; i += 1) {
        lesson34_draw_scene_object(
            command_buffer,
            render_pass,
            state->cube_vertex_buffer,
            state->cube_index_buffer,
            state->cube_index_count,
            view_projection,
            state->light_vp,
            demo->lesson.camera_position,
            state->light_dir,
            &state->cubes[i],
            0.12f,
            1.0f,
            1.0f,
            1.0f,
            1.0f,
            64.0f,
            0.4f,
            0.0f,
            0.0f,
            0.0f);
    }

    SDL_BindGPUGraphicsPipeline(render_pass, state->mask_pipeline);
    SDL_SetGPUStencilReference(render_pass, LESSON34_STENCIL_PORTAL);
    SDL_BindGPUFragmentSamplers(render_pass, 0, &shadow_binding, 1);
    {
        Lesson34SceneFragUniforms fragment_uniforms;
        const Mat4 model = mat4_translate(portal_position);

        SDL_zero(fragment_uniforms);
        lesson34_push_scene_vertex_uniforms(command_buffer, view_projection, state->light_vp, model);
        SDL_PushGPUFragmentUniformData(command_buffer, 0, &fragment_uniforms, sizeof(fragment_uniforms));
        lesson34_bind_indexed_geometry(render_pass, state->portal_mask_vertex_buffer, state->portal_mask_index_buffer);
        SDL_DrawGPUIndexedPrimitives(render_pass, state->portal_mask_index_count, 1, 0, 0, 0);
    }

    SDL_BindGPUGraphicsPipeline(render_pass, state->portal_pipeline);
    SDL_SetGPUStencilReference(render_pass, LESSON34_STENCIL_PORTAL);
    SDL_BindGPUFragmentSamplers(render_pass, 0, &shadow_binding, 1);
    for (int i = 0; i < LESSON34_PORTAL_SPHERE_COUNT; i += 1) {
        lesson34_draw_scene_object(
            command_buffer,
            render_pass,
            state->sphere_vertex_buffer,
            state->sphere_index_buffer,
            state->sphere_index_count,
            view_projection,
            state->light_vp,
            demo->lesson.camera_position,
            state->light_dir,
            &state->portal_spheres[i],
            0.15f,
            1.0f,
            0.95f,
            0.85f,
            1.2f,
            32.0f,
            0.5f,
            0.3f,
            0.15f,
            0.0f);
    }

    lesson34_draw_grid(demo, command_buffer, render_pass, state, view_projection, &shadow_binding, false);
    lesson34_draw_grid(demo, command_buffer, render_pass, state, view_projection, &shadow_binding, true);

    SDL_BindGPUGraphicsPipeline(render_pass, state->frame_pipeline);
    SDL_BindGPUFragmentSamplers(render_pass, 0, &shadow_binding, 1);
    {
        Lesson34Object frame_object = { portal_position, 1.0f, { 0.5f, 0.5f, 0.5f, 1.0f }, false, 0.0f, 0.0f, 0.0f };
        lesson34_draw_scene_object(
            command_buffer,
            render_pass,
            state->portal_frame_vertex_buffer,
            state->portal_frame_index_buffer,
            state->portal_frame_index_count,
            view_projection,
            state->light_vp,
            demo->lesson.camera_position,
            state->light_dir,
            &frame_object,
            0.15f,
            1.0f,
            1.0f,
            1.0f,
            1.0f,
            16.0f,
            0.3f,
            0.0f,
            0.0f,
            0.0f);
    }

    SDL_BindGPUFragmentSamplers(render_pass, 0, &shadow_binding, 1);
    for (int i = 0; i < LESSON34_CUBE_COUNT; i += 1) {
        const Lesson34Object *cube = &state->cubes[i];
        if (!cube->outlined) {
            continue;
        }

        SDL_BindGPUGraphicsPipeline(render_pass, state->outline_write_pipeline);
        SDL_SetGPUStencilReference(render_pass, LESSON34_STENCIL_OUTLINE);
        lesson34_draw_scene_object(
            command_buffer,
            render_pass,
            state->cube_vertex_buffer,
            state->cube_index_buffer,
            state->cube_index_count,
            view_projection,
            state->light_vp,
            demo->lesson.camera_position,
            state->light_dir,
            cube,
            0.12f,
            1.0f,
            1.0f,
            1.0f,
            1.0f,
            64.0f,
            0.4f,
            0.0f,
            0.0f,
            0.0f);

        SDL_BindGPUGraphicsPipeline(render_pass, state->outline_draw_pipeline);
        SDL_SetGPUStencilReference(render_pass, LESSON34_STENCIL_OUTLINE);
        {
            Lesson34OutlineFragUniforms outline_uniforms;
            const Mat4 outline_model = mat4_multiply(mat4_translate(cube->position), mat4_scale(cube->scale * LESSON34_OUTLINE_SCALE));

            lesson34_push_scene_vertex_uniforms(command_buffer, view_projection, state->light_vp, outline_model);
            outline_uniforms.outline_color[0] = cube->outline_r;
            outline_uniforms.outline_color[1] = cube->outline_g;
            outline_uniforms.outline_color[2] = cube->outline_b;
            outline_uniforms.outline_color[3] = 1.0f;
            SDL_PushGPUFragmentUniformData(command_buffer, 0, &outline_uniforms, sizeof(outline_uniforms));
            lesson34_bind_indexed_geometry(render_pass, state->cube_vertex_buffer, state->cube_index_buffer);
            SDL_DrawGPUIndexedPrimitives(render_pass, state->cube_index_count, 1, 0, 0, 0);
        }
    }

    SDL_EndGPURenderPass(render_pass);
    state->main_pass_rendered = true;

    if (state->show_stencil_debug) {
        SDL_GPUColorTargetInfo debug_color_target;
        SDL_GPURenderPass *debug_pass;

        SDL_zero(debug_color_target);
        debug_color_target.texture = swapchain_texture;
        debug_color_target.load_op = SDL_GPU_LOADOP_LOAD;
        debug_color_target.store_op = SDL_GPU_STOREOP_STORE;
        debug_pass = SDL_BeginGPURenderPass(command_buffer, &debug_color_target, 1, nullptr);
        if (!debug_pass) {
            return false;
        }
        SDL_BindGPUGraphicsPipeline(debug_pass, state->debug_pipeline);
        {
            const float portal_tint[4] = { 0.8f, 0.1f, 0.1f, 0.35f };
            lesson34_draw_debug_geometry(
                command_buffer,
                debug_pass,
                state->portal_mask_vertex_buffer,
                state->portal_mask_index_buffer,
                state->portal_mask_index_count,
                view_projection,
                state->light_vp,
                mat4_translate(portal_position),
                portal_tint);
        }
        for (int i = 0; i < LESSON34_CUBE_COUNT; i += 1) {
            if (!state->cubes[i].outlined) {
                continue;
            }
            const float outline_tint[4] = { 0.1f, 0.8f, 0.2f, 0.35f };
            lesson34_draw_debug_geometry(
                command_buffer,
                debug_pass,
                state->cube_vertex_buffer,
                state->cube_index_buffer,
                state->cube_index_count,
                view_projection,
                state->light_vp,
                lesson34_object_model(&state->cubes[i]),
                outline_tint);
        }
        SDL_EndGPURenderPass(debug_pass);
    }

    return true;
}

void ForgeGpuDebugLesson34(ForgeGpuDemo *demo)
{
    Lesson34State *state = lesson34_state(demo);

    if (!state) {
        return;
    }
    ImGui::Text("Depth-stencil: %s",
        state->depth_stencil_format == SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT ? "D24_UNORM_S8_UINT" : "D32_FLOAT_S8_UINT");
    ImGui::Text("Shadow map: 2048x2048 D32_FLOAT");
    ImGui::Text("Stencil overlay: %s", state->show_stencil_debug ? "on" : "off");
    ImGui::Text("Shadow pass: %s", state->shadow_pass_rendered ? "rendered" : "pending");
    ImGui::Text("Main pass: %s", state->main_pass_rendered ? "rendered" : "pending");
}

void ForgeGpuControlsLesson34(ForgeGpuDemo *demo)
{
    (void)demo;
    ImGui::TextUnformatted("V: toggle stencil debug overlay");
}

bool ForgeGpuHandleLesson34Event(ForgeGpuDemo *demo, const SDL_Event *event)
{
    Lesson34State *state = lesson34_state(demo);

    if (!state || event->type != SDL_EVENT_KEY_DOWN || event->key.repeat) {
        return false;
    }
    if (event->key.scancode == SDL_SCANCODE_V) {
        state->show_stencil_debug = !state->show_stencil_debug;
        return true;
    }
    return false;
}

void ForgeGpuExportLesson34Metrics(ForgeGpuDemo *demo)
{
#if defined(SDL_PLATFORM_EMSCRIPTEN)
    Lesson34State *state = lesson34_state(demo);

    if (!state) {
        return;
    }
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson34ShadowPass", state->shadow_pass_rendered ? 1.0 : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson34MainPass", state->main_pass_rendered ? 1.0 : 0.0);
    ForgeGpuBrowserSetNumberMetric(
        "sdlGpuForgeGpuLesson34DepthStencilD24S8",
        state->depth_stencil_format == SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT ? 1.0 : 0.0);
#else
    (void)demo;
#endif
}

void ForgeGpuDestroyLesson34(ForgeGpuDemo *demo)
{
    Lesson34State *state = lesson34_state(demo);

    if (!state) {
        return;
    }

    if (state->shadow_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, state->shadow_pipeline);
    }
    if (state->mask_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, state->mask_pipeline);
    }
    if (state->portal_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, state->portal_pipeline);
    }
    if (state->main_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, state->main_pipeline);
    }
    if (state->frame_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, state->frame_pipeline);
    }
    if (state->outline_write_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, state->outline_write_pipeline);
    }
    if (state->outline_draw_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, state->outline_draw_pipeline);
    }
    if (state->grid_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, state->grid_pipeline);
    }
    if (state->grid_portal_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, state->grid_portal_pipeline);
    }
    if (state->debug_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, state->debug_pipeline);
    }
    if (state->shadow_depth) {
        SDL_ReleaseGPUTexture(demo->device, state->shadow_depth);
    }
    if (state->main_depth) {
        SDL_ReleaseGPUTexture(demo->device, state->main_depth);
    }
    if (state->nearest_clamp) {
        SDL_ReleaseGPUSampler(demo->device, state->nearest_clamp);
    }
    if (state->cube_vertex_buffer) {
        SDL_ReleaseGPUBuffer(demo->device, state->cube_vertex_buffer);
    }
    if (state->cube_index_buffer) {
        SDL_ReleaseGPUBuffer(demo->device, state->cube_index_buffer);
    }
    if (state->sphere_vertex_buffer) {
        SDL_ReleaseGPUBuffer(demo->device, state->sphere_vertex_buffer);
    }
    if (state->sphere_index_buffer) {
        SDL_ReleaseGPUBuffer(demo->device, state->sphere_index_buffer);
    }
    if (state->portal_frame_vertex_buffer) {
        SDL_ReleaseGPUBuffer(demo->device, state->portal_frame_vertex_buffer);
    }
    if (state->portal_frame_index_buffer) {
        SDL_ReleaseGPUBuffer(demo->device, state->portal_frame_index_buffer);
    }
    if (state->portal_mask_vertex_buffer) {
        SDL_ReleaseGPUBuffer(demo->device, state->portal_mask_vertex_buffer);
    }
    if (state->portal_mask_index_buffer) {
        SDL_ReleaseGPUBuffer(demo->device, state->portal_mask_index_buffer);
    }
    if (state->grid_vertex_buffer) {
        SDL_ReleaseGPUBuffer(demo->device, state->grid_vertex_buffer);
    }
    if (state->grid_index_buffer) {
        SDL_ReleaseGPUBuffer(demo->device, state->grid_index_buffer);
    }

    SDL_free(state);
    demo->lesson.private_state = nullptr;
}
