#include "forge_gpu_lessons.h"

#include "forge_gpu_browser_status.h"
#include "forge_gpu_camera.h"
#include "forge_gpu_gpu_helpers.h"
#include "forge_gpu_lesson_common.h"
#include "forge_gpu_math.h"
#include "forge_gpu_shader_layouts.h"
#include "shaders/generated/forge_gpu_lesson_36_shaders.h"
#include "imgui.h"

#include <stddef.h>

#define LESSON36_SHADOW_MAP_SIZE 2048u
#define LESSON36_FAR_PLANE 100.0f
#define LESSON36_MOVE_SPEED 4.0f
#define LESSON36_MOUSE_SENSITIVITY 0.003f
#define LESSON36_PITCH_CLAMP 1.5f
#define LESSON36_CAM_START_X 0.0f
#define LESSON36_CAM_START_Y 2.0f
#define LESSON36_CAM_START_Z 5.0f
#define LESSON36_CAM_START_YAW 0.0f
#define LESSON36_CAM_START_PITCH -0.15f
#define LESSON36_DEPTH_THRESHOLD 0.002f
#define LESSON36_NORMAL_THRESHOLD 0.8f
#define LESSON36_EDGE_THRESHOLD_DISABLED 1000.0f
#define LESSON36_LIGHT_DIR_X 0.4f
#define LESSON36_LIGHT_DIR_Y -0.8f
#define LESSON36_LIGHT_DIR_Z -0.6f
#define LESSON36_LIGHT_INTENSITY 1.2f
#define LESSON36_AMBIENT_STRENGTH 0.15f
#define LESSON36_SCENE_OBJECT_COUNT 9
#define LESSON36_GRID_HALF_SIZE 15.0f
#define LESSON36_GRID_SPACING 1.0f
#define LESSON36_GRID_LINE_WIDTH 0.02f
#define LESSON36_GRID_FADE_DIST 20.0f
#define LESSON36_GRID_INDEX_COUNT 6u
#define LESSON36_SPHERE_RINGS 48
#define LESSON36_SPHERE_SECTORS 72
#define LESSON36_GHOST_POWER 2.0f
#define LESSON36_GHOST_BRIGHTNESS 1.5f
#define LESSON36_SPECULAR_SHININESS 32.0f
#define LESSON36_SPECULAR_STRENGTH 0.5f
#define LESSON36_SHADOW_DEPTH_BIAS_CONSTANT 1.5f
#define LESSON36_SHADOW_DEPTH_BIAS_SLOPE 2.0f
#define LESSON36_LIGHT_DISTANCE 20.0f
#define LESSON36_SHADOW_ORTHO_SIZE 15.0f
#define LESSON36_SHADOW_NEAR 0.1f
#define LESSON36_SHADOW_FAR 50.0f
#define LESSON36_CLEAR_R 0.05f
#define LESSON36_CLEAR_G 0.05f
#define LESSON36_CLEAR_B 0.08f
#define LESSON36_GRID_LINE_R 0.4f
#define LESSON36_GRID_LINE_G 0.4f
#define LESSON36_GRID_LINE_B 0.5f
#define LESSON36_GRID_BG_R 0.08f
#define LESSON36_GRID_BG_G 0.08f
#define LESSON36_GRID_BG_B 0.1f

enum Lesson36RenderMode
{
    LESSON36_RENDER_MODE_EDGE_DETECT = 0,
    LESSON36_RENDER_MODE_XRAY = 1
};

enum Lesson36EdgeSource
{
    LESSON36_EDGE_SOURCE_DEPTH = 0,
    LESSON36_EDGE_SOURCE_NORMAL = 1,
    LESSON36_EDGE_SOURCE_COMBINED = 2,
    LESSON36_EDGE_SOURCE_COUNT = 3
};

struct Lesson36Vertex
{
    float position[3];
    float normal[3];
};

struct Lesson36SceneObject
{
    Vec3 position;
    Vec3 scale;
    Vec3 color;
    bool is_sphere;
    bool is_xray_target;
    bool casts_shadow;
};

struct Lesson36SceneVertUniforms
{
    Mat4 mvp;
    Mat4 model;
    Mat4 light_vp;
    Mat4 model_view;
};

struct Lesson36SceneFragUniforms
{
    float base_color[4];
    float eye_pos[3];
    float ambient;
    float light_dir[4];
    float light_color[3];
    float light_intensity;
    float shininess;
    float specular_str;
    float pad[2];
};

struct Lesson36GridVertUniforms
{
    Mat4 vp;
    Mat4 light_vp;
    Mat4 view;
};

struct Lesson36GridFragUniforms
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
};

struct Lesson36EdgeDetectUniforms
{
    float texel_size[2];
    float depth_threshold;
    float normal_threshold;
    int edge_source;
    int show_debug;
    float pad[2];
};

struct Lesson36MarkVertUniforms
{
    Mat4 mvp;
};

struct Lesson36GhostVertUniforms
{
    Mat4 mvp;
    Mat4 model_view;
};

struct Lesson36GhostFragUniforms
{
    float ghost_color_power[4];
    float ghost_brightness_pad[4];
};

static_assert(sizeof(Lesson36Vertex) == 24, "lesson 36 vertex size must match HLSL layout");
static_assert(sizeof(Lesson36SceneVertUniforms) == 256, "lesson 36 scene vertex uniform size must match HLSL layout");
static_assert(sizeof(Lesson36SceneFragUniforms) == 80, "lesson 36 scene fragment uniform size must match HLSL layout");
static_assert(sizeof(Lesson36GridVertUniforms) == 192, "lesson 36 grid vertex uniform size must match HLSL layout");
static_assert(sizeof(Lesson36GridFragUniforms) == 80, "lesson 36 grid fragment uniform size must match HLSL layout");
static_assert(sizeof(Lesson36EdgeDetectUniforms) == 32, "lesson 36 edge fragment uniform size must match HLSL layout");
static_assert(sizeof(Lesson36MarkVertUniforms) == 64, "lesson 36 mark vertex uniform size must match HLSL layout");
static_assert(sizeof(Lesson36GhostVertUniforms) == 128, "lesson 36 ghost vertex uniform size must match HLSL layout");
static_assert(sizeof(Lesson36GhostFragUniforms) == 32, "lesson 36 ghost fragment uniform size must match strict generated shader layout");

struct Lesson36State
{
    SDL_GPUGraphicsPipeline *shadow_pipeline;
    SDL_GPUGraphicsPipeline *scene_pipeline;
    SDL_GPUGraphicsPipeline *grid_pipeline;
    SDL_GPUGraphicsPipeline *edge_detect_pipeline;
    SDL_GPUGraphicsPipeline *xray_mark_pipeline;
    SDL_GPUGraphicsPipeline *ghost_pipeline;
    SDL_GPUTexture *shadow_depth;
    SDL_GPUTexture *scene_color;
    SDL_GPUTexture *scene_normal;
    SDL_GPUTexture *scene_depth_stencil;
    Uint32 scene_color_width;
    Uint32 scene_color_height;
    Uint32 scene_normal_width;
    Uint32 scene_normal_height;
    Uint32 scene_depth_stencil_width;
    Uint32 scene_depth_stencil_height;
    SDL_GPUSampler *nearest_clamp;
    SDL_GPUSampler *linear_clamp;
    SDL_GPUBuffer *cube_vb;
    SDL_GPUBuffer *cube_ib;
    SDL_GPUBuffer *sphere_vb;
    SDL_GPUBuffer *sphere_ib;
    SDL_GPUBuffer *grid_vb;
    SDL_GPUBuffer *grid_ib;
    Uint32 cube_index_count;
    Uint32 sphere_index_count;
    Lesson36SceneObject scene_objects[LESSON36_SCENE_OBJECT_COUNT];
    Vec3 light_dir;
    SDL_GPUTextureFormat shadow_depth_format;
    SDL_GPUTextureFormat scene_depth_stencil_format;
    Lesson36RenderMode render_mode;
    Lesson36EdgeSource edge_source;
    bool show_debug;
    bool shadow_pass_rendered;
    bool gbuffer_pass_rendered;
    bool edge_pass_rendered;
    bool xray_mark_pass_rendered;
    bool ghost_pass_rendered;
};

static Lesson36State *lesson36_state(ForgeGpuDemo *demo)
{
    return (Lesson36State *)demo->lesson.private_state;
}

static const char *lesson36_format_name(SDL_GPUTextureFormat format)
{
    switch (format) {
    case SDL_GPU_TEXTUREFORMAT_D16_UNORM:
        return "D16_UNORM";
    case SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT:
        return "D24_UNORM_S8_UINT";
    case SDL_GPU_TEXTUREFORMAT_D32_FLOAT:
        return "D32_FLOAT";
    case SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT:
        return "D32_FLOAT_S8_UINT";
    default:
        return "unknown";
    }
}

static SDL_GPUTextureFormat lesson36_select_shadow_depth_format(SDL_GPUDevice *device)
{
    const SDL_GPUTextureUsageFlags usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;

    if (SDL_GPUTextureSupportsFormat(device, SDL_GPU_TEXTUREFORMAT_D32_FLOAT, SDL_GPU_TEXTURETYPE_2D, usage)) {
        return SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    }
    if (SDL_GPUTextureSupportsFormat(device, SDL_GPU_TEXTUREFORMAT_D16_UNORM, SDL_GPU_TEXTURETYPE_2D, usage)) {
        return SDL_GPU_TEXTUREFORMAT_D16_UNORM;
    }
    return SDL_GPU_TEXTUREFORMAT_INVALID;
}

static SDL_GPUTextureFormat lesson36_select_depth_stencil_format(SDL_GPUDevice *device)
{
    const SDL_GPUTextureUsageFlags usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;

    if (SDL_GPUTextureSupportsFormat(device, SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT, SDL_GPU_TEXTURETYPE_2D, usage)) {
        return SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT;
    }
    if (SDL_GPUTextureSupportsFormat(device, SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT, SDL_GPU_TEXTURETYPE_2D, usage)) {
        return SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT;
    }
    return SDL_GPU_TEXTUREFORMAT_INVALID;
}

static void lesson36_init_camera(ForgeGpuDemo *demo)
{
    demo->lesson.camera_position = { LESSON36_CAM_START_X, LESSON36_CAM_START_Y, LESSON36_CAM_START_Z };
    demo->lesson.camera_yaw = LESSON36_CAM_START_YAW;
    demo->lesson.camera_pitch = LESSON36_CAM_START_PITCH;
    demo->lesson.pitch_clamp = LESSON36_PITCH_CLAMP;
    demo->lesson.mouse_sensitivity = LESSON36_MOUSE_SENSITIVITY;
    demo->lesson.move_speed = LESSON36_MOVE_SPEED;
    demo->lesson.last_ticks = SDL_GetTicks();
}

static Mat4 lesson36_light_view_projection(const Lesson36State *state)
{
    const Vec3 light_pos = vec3_scale(state->light_dir, -LESSON36_LIGHT_DISTANCE);
    const Mat4 light_view = mat4_look_at(light_pos, { 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f });
    const Mat4 light_projection = mat4_orthographic(
        -LESSON36_SHADOW_ORTHO_SIZE,
        LESSON36_SHADOW_ORTHO_SIZE,
        -LESSON36_SHADOW_ORTHO_SIZE,
        LESSON36_SHADOW_ORTHO_SIZE,
        LESSON36_SHADOW_NEAR,
        LESSON36_SHADOW_FAR);
    return mat4_multiply(light_projection, light_view);
}

static Mat4 lesson36_model_matrix(const Lesson36SceneObject *object)
{
    return mat4_multiply(mat4_translate(object->position), mat4_scale_vec3(object->scale));
}

static void lesson36_add_box(
    float cx,
    float cy,
    float cz,
    float hx,
    float hy,
    float hz,
    Lesson36Vertex *vertices,
    Uint32 *vertex_count,
    Uint16 *indices,
    Uint32 *index_count)
{
    const Uint16 base = (Uint16)*vertex_count;
    Uint32 v = *vertex_count;
    Uint32 idx = *index_count;
    const float faces[6][4][3] = {
        { { -hx, -hy, hz }, { hx, -hy, hz }, { hx, hy, hz }, { -hx, hy, hz } },
        { { hx, -hy, -hz }, { -hx, -hy, -hz }, { -hx, hy, -hz }, { hx, hy, -hz } },
        { { hx, -hy, hz }, { hx, -hy, -hz }, { hx, hy, -hz }, { hx, hy, hz } },
        { { -hx, -hy, -hz }, { -hx, -hy, hz }, { -hx, hy, hz }, { -hx, hy, -hz } },
        { { -hx, hy, hz }, { hx, hy, hz }, { hx, hy, -hz }, { -hx, hy, -hz } },
        { { -hx, -hy, -hz }, { hx, -hy, -hz }, { hx, -hy, hz }, { -hx, -hy, hz } },
    };
    const float normals[6][3] = {
        { 0.0f, 0.0f, 1.0f },
        { 0.0f, 0.0f, -1.0f },
        { 1.0f, 0.0f, 0.0f },
        { -1.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f },
        { 0.0f, -1.0f, 0.0f },
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
        const Uint16 fb = (Uint16)(base + face * 4);
        indices[idx++] = (Uint16)(fb + 0);
        indices[idx++] = (Uint16)(fb + 1);
        indices[idx++] = (Uint16)(fb + 2);
        indices[idx++] = (Uint16)(fb + 0);
        indices[idx++] = (Uint16)(fb + 2);
        indices[idx++] = (Uint16)(fb + 3);
    }

    *vertex_count = v;
    *index_count = idx;
}

static void lesson36_generate_cube(
    Lesson36Vertex vertices[24],
    Uint32 *vertex_count,
    Uint16 indices[36],
    Uint32 *index_count)
{
    *vertex_count = 0;
    *index_count = 0;
    lesson36_add_box(0.0f, 0.0f, 0.0f, 0.5f, 0.5f, 0.5f, vertices, vertex_count, indices, index_count);
}

static void lesson36_generate_sphere(Lesson36Vertex *vertices, Uint32 *vertex_count, Uint16 *indices, Uint32 *index_count)
{
    Uint32 v = 0;
    Uint32 idx = 0;

    for (int i = 0; i <= LESSON36_SPHERE_RINGS; i += 1) {
        const float theta = (float)i * FORGE_GPU_PI / (float)LESSON36_SPHERE_RINGS;
        const float sin_t = SDL_sinf(theta);
        const float cos_t = SDL_cosf(theta);

        for (int j = 0; j <= LESSON36_SPHERE_SECTORS; j += 1) {
            const float phi = (float)j * 2.0f * FORGE_GPU_PI / (float)LESSON36_SPHERE_SECTORS;
            const float sin_p = SDL_sinf(phi);
            const float cos_p = SDL_cosf(phi);
            const float x = cos_p * sin_t;
            const float y = cos_t;
            const float z = sin_p * sin_t;

            vertices[v].position[0] = x;
            vertices[v].position[1] = y;
            vertices[v].position[2] = z;
            vertices[v].normal[0] = x;
            vertices[v].normal[1] = y;
            vertices[v].normal[2] = z;
            v += 1;
        }
    }

    for (int i = 0; i < LESSON36_SPHERE_RINGS; i += 1) {
        for (int j = 0; j < LESSON36_SPHERE_SECTORS; j += 1) {
            const Uint16 a = (Uint16)(i * (LESSON36_SPHERE_SECTORS + 1) + j);
            const Uint16 b = (Uint16)(a + (LESSON36_SPHERE_SECTORS + 1));

            if (i != 0) {
                indices[idx++] = a;
                indices[idx++] = (Uint16)(a + 1);
                indices[idx++] = b;
            }
            if (i != LESSON36_SPHERE_RINGS - 1) {
                indices[idx++] = (Uint16)(a + 1);
                indices[idx++] = (Uint16)(b + 1);
                indices[idx++] = b;
            }
        }
    }

    *vertex_count = v;
    *index_count = idx;
}

static bool lesson36_create_geometry(ForgeGpuDemo *demo, Lesson36State *state)
{
    Lesson36Vertex cube_vertices[24];
    Uint16 cube_indices[36];
    Uint32 cube_vertex_count = 0;
    Uint32 cube_index_count = 0;
    const Uint32 sphere_max_vertices = (LESSON36_SPHERE_RINGS + 1u) * (LESSON36_SPHERE_SECTORS + 1u);
    const Uint32 sphere_max_indices = LESSON36_SPHERE_RINGS * LESSON36_SPHERE_SECTORS * 6u;
    Lesson36Vertex *sphere_vertices = nullptr;
    Uint16 *sphere_indices = nullptr;
    Uint32 sphere_vertex_count = 0;
    Uint32 sphere_index_count = 0;
    Lesson36Vertex grid_vertices[4] = {
        { { -LESSON36_GRID_HALF_SIZE, 0.0f, -LESSON36_GRID_HALF_SIZE }, { 0.0f, 1.0f, 0.0f } },
        { { LESSON36_GRID_HALF_SIZE, 0.0f, -LESSON36_GRID_HALF_SIZE }, { 0.0f, 1.0f, 0.0f } },
        { { LESSON36_GRID_HALF_SIZE, 0.0f, LESSON36_GRID_HALF_SIZE }, { 0.0f, 1.0f, 0.0f } },
        { { -LESSON36_GRID_HALF_SIZE, 0.0f, LESSON36_GRID_HALF_SIZE }, { 0.0f, 1.0f, 0.0f } },
    };
    Uint16 grid_indices[6] = { 0, 1, 2, 0, 2, 3 };

    lesson36_generate_cube(cube_vertices, &cube_vertex_count, cube_indices, &cube_index_count);
    state->cube_vb = ForgeGpuCreateBufferWithData(demo->device, SDL_GPU_BUFFERUSAGE_VERTEX, cube_vertices, cube_vertex_count * (Uint32)sizeof(Lesson36Vertex));
    state->cube_ib = ForgeGpuCreateBufferWithData(demo->device, SDL_GPU_BUFFERUSAGE_INDEX, cube_indices, cube_index_count * (Uint32)sizeof(Uint16));
    state->cube_index_count = cube_index_count;
    if (!state->cube_vb || !state->cube_ib) {
        return false;
    }

    sphere_vertices = (Lesson36Vertex *)SDL_calloc(sphere_max_vertices, sizeof(*sphere_vertices));
    sphere_indices = (Uint16 *)SDL_calloc(sphere_max_indices, sizeof(*sphere_indices));
    if (!sphere_vertices || !sphere_indices) {
        SDL_free(sphere_vertices);
        SDL_free(sphere_indices);
        SDL_OutOfMemory();
        return false;
    }
    lesson36_generate_sphere(sphere_vertices, &sphere_vertex_count, sphere_indices, &sphere_index_count);
    state->sphere_vb = ForgeGpuCreateBufferWithData(demo->device, SDL_GPU_BUFFERUSAGE_VERTEX, sphere_vertices, sphere_vertex_count * (Uint32)sizeof(Lesson36Vertex));
    state->sphere_ib = ForgeGpuCreateBufferWithData(demo->device, SDL_GPU_BUFFERUSAGE_INDEX, sphere_indices, sphere_index_count * (Uint32)sizeof(Uint16));
    state->sphere_index_count = sphere_index_count;
    SDL_free(sphere_vertices);
    SDL_free(sphere_indices);
    if (!state->sphere_vb || !state->sphere_ib) {
        return false;
    }

    state->grid_vb = ForgeGpuCreateBufferWithData(demo->device, SDL_GPU_BUFFERUSAGE_VERTEX, grid_vertices, sizeof(grid_vertices));
    state->grid_ib = ForgeGpuCreateBufferWithData(demo->device, SDL_GPU_BUFFERUSAGE_INDEX, grid_indices, sizeof(grid_indices));
    return state->grid_vb && state->grid_ib;
}

static void lesson36_init_scene_objects(Lesson36State *state)
{
    state->scene_objects[0] = { { 2.0f, 0.5f, -2.0f }, { 1.0f, 1.0f, 1.0f }, { 0.6f, 0.6f, 0.6f }, false, false, true };
    state->scene_objects[1] = { { -1.5f, 0.5f, -1.0f }, { 1.0f, 1.0f, 1.0f }, { 0.3f, 0.4f, 0.8f }, false, false, true };
    state->scene_objects[2] = { { 0.0f, 0.5f, -3.0f }, { 0.8f, 0.8f, 0.8f }, { 0.8f, 0.3f, 0.3f }, false, false, true };
    state->scene_objects[3] = { { 3.0f, 0.8f, 0.0f }, { 1.2f, 1.2f, 1.2f }, { 0.3f, 0.7f, 0.4f }, false, false, true };
    state->scene_objects[4] = { { -2.5f, 0.7f, -3.5f }, { 0.7f, 0.7f, 0.7f }, { 0.9f, 0.5f, 0.2f }, true, false, true };
    state->scene_objects[5] = { { 1.0f, 0.6f, 1.0f }, { 0.6f, 0.6f, 0.6f }, { 0.2f, 0.7f, 0.7f }, true, false, true };
    state->scene_objects[6] = { { 0.0f, 1.5f, -6.0f }, { 6.0f, 3.0f, 0.3f }, { 0.5f, 0.45f, 0.4f }, false, false, true };
    state->scene_objects[7] = { { -1.5f, 0.8f, -8.0f }, { 1.0f, 1.0f, 1.0f }, { 0.15f, 0.7f, 1.0f }, false, true, true };
    state->scene_objects[8] = { { 1.5f, 1.0f, -8.5f }, { 0.8f, 0.8f, 0.8f }, { 0.15f, 0.7f, 1.0f }, true, true, true };
}

static void lesson36_release_shader(SDL_GPUDevice *device, SDL_GPUShader **shader)
{
    if (*shader) {
        SDL_ReleaseGPUShader(device, *shader);
        *shader = nullptr;
    }
}

static SDL_GPUGraphicsPipeline *lesson36_create_stencil_pipeline(
    ForgeGpuDemo *demo,
    SDL_GPUShader *vertex_shader,
    SDL_GPUShader *fragment_shader,
    const SDL_GPUVertexBufferDescription *vertex_buffer,
    const SDL_GPUVertexAttribute *attributes,
    Uint32 num_attributes,
    SDL_GPUTextureFormat depth_stencil_format,
    bool mark_pass)
{
    SDL_GPUColorTargetDescription color_target;
    SDL_GPUGraphicsPipelineCreateInfo pipeline_info;
    SDL_GPUStencilOpState stencil_state;

    SDL_zero(color_target);
    color_target.format = demo->color_format;
    if (mark_pass) {
        color_target.blend_state.enable_color_write_mask = true;
        color_target.blend_state.color_write_mask = 0;
    } else {
        color_target.blend_state.enable_blend = true;
        color_target.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        color_target.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        color_target.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
        color_target.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        color_target.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        color_target.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    }

    SDL_zero(stencil_state);
    stencil_state.fail_op = SDL_GPU_STENCILOP_KEEP;
    stencil_state.depth_fail_op = mark_pass ? SDL_GPU_STENCILOP_INCREMENT_AND_WRAP : SDL_GPU_STENCILOP_KEEP;
    stencil_state.pass_op = SDL_GPU_STENCILOP_KEEP;
    stencil_state.compare_op = mark_pass ? SDL_GPU_COMPAREOP_ALWAYS : SDL_GPU_COMPAREOP_NOT_EQUAL;

    SDL_zero(pipeline_info);
    pipeline_info.vertex_shader = vertex_shader;
    pipeline_info.fragment_shader = fragment_shader;
    pipeline_info.vertex_input_state.vertex_buffer_descriptions = vertex_buffer;
    pipeline_info.vertex_input_state.num_vertex_buffers = 1;
    pipeline_info.vertex_input_state.vertex_attributes = attributes;
    pipeline_info.vertex_input_state.num_vertex_attributes = num_attributes;
    pipeline_info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pipeline_info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    pipeline_info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    pipeline_info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    pipeline_info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    pipeline_info.depth_stencil_state.enable_depth_test = mark_pass;
    pipeline_info.depth_stencil_state.enable_depth_write = false;
    pipeline_info.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS;
    pipeline_info.depth_stencil_state.enable_stencil_test = true;
    pipeline_info.depth_stencil_state.front_stencil_state = stencil_state;
    pipeline_info.depth_stencil_state.back_stencil_state = stencil_state;
    pipeline_info.depth_stencil_state.compare_mask = 0xFF;
    pipeline_info.depth_stencil_state.write_mask = mark_pass ? 0xFF : 0x00;
    pipeline_info.target_info.color_target_descriptions = &color_target;
    pipeline_info.target_info.num_color_targets = 1;
    pipeline_info.target_info.has_depth_stencil_target = true;
    pipeline_info.target_info.depth_stencil_format = depth_stencil_format;
    return SDL_CreateGPUGraphicsPipeline(demo->device, &pipeline_info);
}

static bool lesson36_create_pipelines(ForgeGpuDemo *demo, Lesson36State *state)
{
    SDL_GPUShader *scene_vertex_shader = nullptr;
    SDL_GPUShader *scene_fragment_shader = nullptr;
    SDL_GPUShader *shadow_vertex_shader = nullptr;
    SDL_GPUShader *shadow_fragment_shader = nullptr;
    SDL_GPUShader *grid_vertex_shader = nullptr;
    SDL_GPUShader *grid_fragment_shader = nullptr;
    SDL_GPUShader *fullscreen_vertex_shader = nullptr;
    SDL_GPUShader *edge_detect_fragment_shader = nullptr;
    SDL_GPUShader *xray_mark_vertex_shader = nullptr;
    SDL_GPUShader *xray_mark_fragment_shader = nullptr;
    SDL_GPUShader *ghost_vertex_shader = nullptr;
    SDL_GPUShader *ghost_fragment_shader = nullptr;
    SDL_GPUVertexBufferDescription full_vertex_buffer;
    SDL_GPUVertexAttribute full_attributes[2];
    SDL_GPUVertexBufferDescription pos_vertex_buffer;
    SDL_GPUVertexAttribute pos_attribute;
    SDL_GPUColorTargetDescription gbuffer_color_targets[2];
    SDL_GPUColorTargetDescription edge_color_target;
    bool ok = false;

    scene_vertex_shader = ForgeGpuCreateShader(demo->device, SDL_GPU_SHADERSTAGE_VERTEX, lesson36_scene_vert_wgsl, lesson36_scene_vert_wgsl_size, lesson36_scene_vert_msl, lesson36_scene_vert_msl_size, 0, 0, 0, 1);
    scene_fragment_shader = ForgeGpuCreateShaderWithResourceLayout(demo->device, lesson36_scene_frag_wgsl, lesson36_scene_frag_wgsl_size, lesson36_scene_frag_msl, lesson36_scene_frag_msl_size, ForgeGpuShaderLayout_lesson36_scene_frag());
    shadow_vertex_shader = ForgeGpuCreateShader(demo->device, SDL_GPU_SHADERSTAGE_VERTEX, lesson36_shadow_vert_wgsl, lesson36_shadow_vert_wgsl_size, lesson36_shadow_vert_msl, lesson36_shadow_vert_msl_size, 0, 0, 0, 1);
    shadow_fragment_shader = ForgeGpuCreateShader(demo->device, SDL_GPU_SHADERSTAGE_FRAGMENT, lesson36_shadow_frag_wgsl, lesson36_shadow_frag_wgsl_size, lesson36_shadow_frag_msl, lesson36_shadow_frag_msl_size, 0, 0, 0, 0);
    grid_vertex_shader = ForgeGpuCreateShader(demo->device, SDL_GPU_SHADERSTAGE_VERTEX, lesson36_grid_vert_wgsl, lesson36_grid_vert_wgsl_size, lesson36_grid_vert_msl, lesson36_grid_vert_msl_size, 0, 0, 0, 1);
    grid_fragment_shader = ForgeGpuCreateShaderWithResourceLayout(demo->device, lesson36_grid_frag_wgsl, lesson36_grid_frag_wgsl_size, lesson36_grid_frag_msl, lesson36_grid_frag_msl_size, ForgeGpuShaderLayout_lesson36_grid_frag());
    fullscreen_vertex_shader = ForgeGpuCreateShader(demo->device, SDL_GPU_SHADERSTAGE_VERTEX, lesson36_fullscreen_vert_wgsl, lesson36_fullscreen_vert_wgsl_size, lesson36_fullscreen_vert_msl, lesson36_fullscreen_vert_msl_size, 0, 0, 0, 0);
    edge_detect_fragment_shader = ForgeGpuCreateShaderWithResourceLayout(demo->device, lesson36_edge_detect_frag_wgsl, lesson36_edge_detect_frag_wgsl_size, lesson36_edge_detect_frag_msl, lesson36_edge_detect_frag_msl_size, ForgeGpuShaderLayout_lesson36_edge_detect_frag());
    xray_mark_vertex_shader = ForgeGpuCreateShader(demo->device, SDL_GPU_SHADERSTAGE_VERTEX, lesson36_xray_mark_vert_wgsl, lesson36_xray_mark_vert_wgsl_size, lesson36_xray_mark_vert_msl, lesson36_xray_mark_vert_msl_size, 0, 0, 0, 1);
    xray_mark_fragment_shader = ForgeGpuCreateShader(demo->device, SDL_GPU_SHADERSTAGE_FRAGMENT, lesson36_xray_mark_frag_wgsl, lesson36_xray_mark_frag_wgsl_size, lesson36_xray_mark_frag_msl, lesson36_xray_mark_frag_msl_size, 0, 0, 0, 0);
    ghost_vertex_shader = ForgeGpuCreateShader(demo->device, SDL_GPU_SHADERSTAGE_VERTEX, lesson36_ghost_vert_wgsl, lesson36_ghost_vert_wgsl_size, lesson36_ghost_vert_msl, lesson36_ghost_vert_msl_size, 0, 0, 0, 1);
    ghost_fragment_shader = ForgeGpuCreateShader(demo->device, SDL_GPU_SHADERSTAGE_FRAGMENT, lesson36_ghost_frag_wgsl, lesson36_ghost_frag_wgsl_size, lesson36_ghost_frag_msl, lesson36_ghost_frag_msl_size, 0, 0, 0, 1);
    if (!scene_vertex_shader || !scene_fragment_shader || !shadow_vertex_shader || !shadow_fragment_shader ||
        !grid_vertex_shader || !grid_fragment_shader || !fullscreen_vertex_shader || !edge_detect_fragment_shader ||
        !xray_mark_vertex_shader || !xray_mark_fragment_shader || !ghost_vertex_shader || !ghost_fragment_shader) {
        goto done;
    }

    SDL_zero(full_vertex_buffer);
    full_vertex_buffer.slot = 0;
    full_vertex_buffer.pitch = sizeof(Lesson36Vertex);
    full_vertex_buffer.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    SDL_zeroa(full_attributes);
    full_attributes[0].location = 0;
    full_attributes[0].buffer_slot = 0;
    full_attributes[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    full_attributes[0].offset = offsetof(Lesson36Vertex, position);
    full_attributes[1].location = 1;
    full_attributes[1].buffer_slot = 0;
    full_attributes[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    full_attributes[1].offset = offsetof(Lesson36Vertex, normal);

    SDL_zero(pos_vertex_buffer);
    pos_vertex_buffer.slot = 0;
    pos_vertex_buffer.pitch = sizeof(Lesson36Vertex);
    pos_vertex_buffer.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    SDL_zero(pos_attribute);
    pos_attribute.location = 0;
    pos_attribute.buffer_slot = 0;
    pos_attribute.format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    pos_attribute.offset = offsetof(Lesson36Vertex, position);

    SDL_zeroa(gbuffer_color_targets);
    gbuffer_color_targets[0].format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    gbuffer_color_targets[1].format = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;

    SDL_zero(edge_color_target);
    edge_color_target.format = demo->color_format;

    state->shadow_pipeline = ForgeGpuCreateLessonGraphicsPipelineWithColorTargetsAndDepthCompare(
        demo, shadow_vertex_shader, shadow_fragment_shader, SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        nullptr, 0, &pos_vertex_buffer, 1, &pos_attribute, 1, true, state->shadow_depth_format,
        true, true, SDL_GPU_COMPAREOP_LESS, SDL_GPU_CULLMODE_NONE,
        LESSON36_SHADOW_DEPTH_BIAS_CONSTANT, LESSON36_SHADOW_DEPTH_BIAS_SLOPE);
    state->scene_pipeline = ForgeGpuCreateLessonGraphicsPipelineWithColorTargetsAndDepthCompare(
        demo, scene_vertex_shader, scene_fragment_shader, SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        gbuffer_color_targets, 2, &full_vertex_buffer, 1, full_attributes, 2, true, state->scene_depth_stencil_format,
        true, true, SDL_GPU_COMPAREOP_LESS, SDL_GPU_CULLMODE_BACK, 0.0f, 0.0f);
    state->grid_pipeline = ForgeGpuCreateLessonGraphicsPipelineWithColorTargetsAndDepthCompare(
        demo, grid_vertex_shader, grid_fragment_shader, SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        gbuffer_color_targets, 2, &pos_vertex_buffer, 1, &pos_attribute, 1, true, state->scene_depth_stencil_format,
        true, true, SDL_GPU_COMPAREOP_LESS_OR_EQUAL, SDL_GPU_CULLMODE_NONE, 0.0f, 0.0f);
    state->edge_detect_pipeline = ForgeGpuCreateLessonGraphicsPipelineWithColorTargetsAndDepthCompare(
        demo, fullscreen_vertex_shader, edge_detect_fragment_shader, SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        &edge_color_target, 1, nullptr, 0, nullptr, 0, false, SDL_GPU_TEXTUREFORMAT_INVALID,
        false, false, SDL_GPU_COMPAREOP_ALWAYS, SDL_GPU_CULLMODE_NONE, 0.0f, 0.0f);
    state->xray_mark_pipeline = lesson36_create_stencil_pipeline(
        demo, xray_mark_vertex_shader, xray_mark_fragment_shader, &pos_vertex_buffer, &pos_attribute, 1,
        state->scene_depth_stencil_format, true);
    state->ghost_pipeline = lesson36_create_stencil_pipeline(
        demo, ghost_vertex_shader, ghost_fragment_shader, &full_vertex_buffer, full_attributes, 2,
        state->scene_depth_stencil_format, false);

    ok = state->shadow_pipeline && state->scene_pipeline && state->grid_pipeline &&
        state->edge_detect_pipeline && state->xray_mark_pipeline && state->ghost_pipeline;

done:
    lesson36_release_shader(demo->device, &scene_vertex_shader);
    lesson36_release_shader(demo->device, &scene_fragment_shader);
    lesson36_release_shader(demo->device, &shadow_vertex_shader);
    lesson36_release_shader(demo->device, &shadow_fragment_shader);
    lesson36_release_shader(demo->device, &grid_vertex_shader);
    lesson36_release_shader(demo->device, &grid_fragment_shader);
    lesson36_release_shader(demo->device, &fullscreen_vertex_shader);
    lesson36_release_shader(demo->device, &edge_detect_fragment_shader);
    lesson36_release_shader(demo->device, &xray_mark_vertex_shader);
    lesson36_release_shader(demo->device, &xray_mark_fragment_shader);
    lesson36_release_shader(demo->device, &ghost_vertex_shader);
    lesson36_release_shader(demo->device, &ghost_fragment_shader);
    return ok;
}

static bool lesson36_ensure_targets(ForgeGpuDemo *demo, Lesson36State *state, Uint32 width, Uint32 height)
{
    if (!ForgeGpuEnsureSampledColorTarget(
            demo,
            &state->scene_color,
            &state->scene_color_width,
            &state->scene_color_height,
            width,
            height,
            SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM)) {
        return false;
    }
    if (!ForgeGpuEnsureSampledColorTarget(
            demo,
            &state->scene_normal,
            &state->scene_normal_width,
            &state->scene_normal_height,
            width,
            height,
            SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT)) {
        return false;
    }
    return ForgeGpuEnsureSampledDepthTarget(
        demo,
        &state->scene_depth_stencil,
        &state->scene_depth_stencil_width,
        &state->scene_depth_stencil_height,
        width,
        height,
        state->scene_depth_stencil_format);
}

static void lesson36_bind_object(SDL_GPURenderPass *render_pass, const Lesson36State *state, const Lesson36SceneObject *object)
{
    SDL_GPUBufferBinding vertex_binding;
    SDL_GPUBufferBinding index_binding;

    if (object->is_sphere) {
        vertex_binding = { state->sphere_vb, 0 };
        index_binding = { state->sphere_ib, 0 };
        SDL_BindGPUVertexBuffers(render_pass, 0, &vertex_binding, 1);
        SDL_BindGPUIndexBuffer(render_pass, &index_binding, SDL_GPU_INDEXELEMENTSIZE_16BIT);
        SDL_DrawGPUIndexedPrimitives(render_pass, state->sphere_index_count, 1, 0, 0, 0);
    } else {
        vertex_binding = { state->cube_vb, 0 };
        index_binding = { state->cube_ib, 0 };
        SDL_BindGPUVertexBuffers(render_pass, 0, &vertex_binding, 1);
        SDL_BindGPUIndexBuffer(render_pass, &index_binding, SDL_GPU_INDEXELEMENTSIZE_16BIT);
        SDL_DrawGPUIndexedPrimitives(render_pass, state->cube_index_count, 1, 0, 0, 0);
    }
}

static void lesson36_draw_shadow_objects(
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    const Lesson36State *state,
    Mat4 light_vp)
{
    for (int i = 0; i < LESSON36_SCENE_OBJECT_COUNT; i += 1) {
        const Lesson36SceneObject *object = &state->scene_objects[i];
        if (!object->casts_shadow) {
            continue;
        }
        const Mat4 model = lesson36_model_matrix(object);
        const Mat4 mvp = mat4_multiply(light_vp, model);
        SDL_PushGPUVertexUniformData(command_buffer, 0, &mvp, sizeof(mvp));
        lesson36_bind_object(render_pass, state, object);
    }
}

static void lesson36_draw_scene_objects(
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    const Lesson36State *state,
    Mat4 view,
    Mat4 view_projection,
    Mat4 light_vp,
    Vec3 eye_pos)
{
    for (int i = 0; i < LESSON36_SCENE_OBJECT_COUNT; i += 1) {
        const Lesson36SceneObject *object = &state->scene_objects[i];
        const Mat4 model = lesson36_model_matrix(object);
        Lesson36SceneVertUniforms vertex_uniforms;
        Lesson36SceneFragUniforms fragment_uniforms;

        vertex_uniforms.mvp = mat4_multiply(view_projection, model);
        vertex_uniforms.model = model;
        vertex_uniforms.light_vp = mat4_multiply(light_vp, model);
        vertex_uniforms.model_view = mat4_multiply(view, model);
        SDL_PushGPUVertexUniformData(command_buffer, 0, &vertex_uniforms, sizeof(vertex_uniforms));

        SDL_zero(fragment_uniforms);
        fragment_uniforms.base_color[0] = object->color.x;
        fragment_uniforms.base_color[1] = object->color.y;
        fragment_uniforms.base_color[2] = object->color.z;
        fragment_uniforms.base_color[3] = 1.0f;
        fragment_uniforms.eye_pos[0] = eye_pos.x;
        fragment_uniforms.eye_pos[1] = eye_pos.y;
        fragment_uniforms.eye_pos[2] = eye_pos.z;
        fragment_uniforms.ambient = LESSON36_AMBIENT_STRENGTH;
        fragment_uniforms.light_dir[0] = state->light_dir.x;
        fragment_uniforms.light_dir[1] = state->light_dir.y;
        fragment_uniforms.light_dir[2] = state->light_dir.z;
        fragment_uniforms.light_color[0] = 1.0f;
        fragment_uniforms.light_color[1] = 1.0f;
        fragment_uniforms.light_color[2] = 1.0f;
        fragment_uniforms.light_intensity = LESSON36_LIGHT_INTENSITY;
        fragment_uniforms.shininess = LESSON36_SPECULAR_SHININESS;
        fragment_uniforms.specular_str = LESSON36_SPECULAR_STRENGTH;
        SDL_PushGPUFragmentUniformData(command_buffer, 0, &fragment_uniforms, sizeof(fragment_uniforms));

        lesson36_bind_object(render_pass, state, object);
    }
}

static void lesson36_draw_grid(
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    const Lesson36State *state,
    Mat4 view,
    Mat4 view_projection,
    Mat4 light_vp,
    Vec3 eye_pos)
{
    Lesson36GridVertUniforms vertex_uniforms;
    Lesson36GridFragUniforms fragment_uniforms;
    SDL_GPUBufferBinding vertex_binding = { state->grid_vb, 0 };
    SDL_GPUBufferBinding index_binding = { state->grid_ib, 0 };

    vertex_uniforms.vp = view_projection;
    vertex_uniforms.light_vp = light_vp;
    vertex_uniforms.view = view;
    SDL_PushGPUVertexUniformData(command_buffer, 0, &vertex_uniforms, sizeof(vertex_uniforms));

    SDL_zero(fragment_uniforms);
    fragment_uniforms.line_color[0] = LESSON36_GRID_LINE_R;
    fragment_uniforms.line_color[1] = LESSON36_GRID_LINE_G;
    fragment_uniforms.line_color[2] = LESSON36_GRID_LINE_B;
    fragment_uniforms.line_color[3] = 1.0f;
    fragment_uniforms.bg_color[0] = LESSON36_GRID_BG_R;
    fragment_uniforms.bg_color[1] = LESSON36_GRID_BG_G;
    fragment_uniforms.bg_color[2] = LESSON36_GRID_BG_B;
    fragment_uniforms.bg_color[3] = 1.0f;
    fragment_uniforms.light_dir[0] = state->light_dir.x;
    fragment_uniforms.light_dir[1] = state->light_dir.y;
    fragment_uniforms.light_dir[2] = state->light_dir.z;
    fragment_uniforms.light_intensity = LESSON36_LIGHT_INTENSITY;
    fragment_uniforms.eye_pos[0] = eye_pos.x;
    fragment_uniforms.eye_pos[1] = eye_pos.y;
    fragment_uniforms.eye_pos[2] = eye_pos.z;
    fragment_uniforms.grid_spacing = LESSON36_GRID_SPACING;
    fragment_uniforms.line_width = LESSON36_GRID_LINE_WIDTH;
    fragment_uniforms.fade_distance = LESSON36_GRID_FADE_DIST;
    fragment_uniforms.ambient = LESSON36_AMBIENT_STRENGTH;
    SDL_PushGPUFragmentUniformData(command_buffer, 0, &fragment_uniforms, sizeof(fragment_uniforms));

    SDL_BindGPUVertexBuffers(render_pass, 0, &vertex_binding, 1);
    SDL_BindGPUIndexBuffer(render_pass, &index_binding, SDL_GPU_INDEXELEMENTSIZE_16BIT);
    SDL_DrawGPUIndexedPrimitives(render_pass, LESSON36_GRID_INDEX_COUNT, 1, 0, 0, 0);
}

static bool lesson36_run_shadow_pass(SDL_GPUCommandBuffer *command_buffer, Lesson36State *state, Mat4 light_vp)
{
    SDL_GPUDepthStencilTargetInfo depth_target;
    SDL_GPURenderPass *render_pass;

    SDL_zero(depth_target);
    depth_target.texture = state->shadow_depth;
    depth_target.load_op = SDL_GPU_LOADOP_CLEAR;
    depth_target.store_op = SDL_GPU_STOREOP_STORE;
    depth_target.clear_depth = 1.0f;
    render_pass = SDL_BeginGPURenderPass(command_buffer, nullptr, 0, &depth_target);
    if (!render_pass) {
        return false;
    }
    SDL_BindGPUGraphicsPipeline(render_pass, state->shadow_pipeline);
    lesson36_draw_shadow_objects(command_buffer, render_pass, state, light_vp);
    SDL_EndGPURenderPass(render_pass);
    state->shadow_pass_rendered = true;
    return true;
}

static bool lesson36_run_gbuffer_pass(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    Lesson36State *state,
    Mat4 view,
    Mat4 view_projection,
    Mat4 light_vp)
{
    SDL_GPUColorTargetInfo color_targets[2];
    SDL_GPUDepthStencilTargetInfo depth_stencil_target;
    SDL_GPUTextureSamplerBinding shadow_binding;
    SDL_GPURenderPass *render_pass;

    SDL_zeroa(color_targets);
    color_targets[0].texture = state->scene_color;
    color_targets[0].load_op = SDL_GPU_LOADOP_CLEAR;
    color_targets[0].store_op = SDL_GPU_STOREOP_STORE;
    color_targets[0].clear_color = { LESSON36_CLEAR_R, LESSON36_CLEAR_G, LESSON36_CLEAR_B, 1.0f };
    color_targets[1].texture = state->scene_normal;
    color_targets[1].load_op = SDL_GPU_LOADOP_CLEAR;
    color_targets[1].store_op = SDL_GPU_STOREOP_STORE;
    color_targets[1].clear_color = { 0.0f, 1.0f, 0.0f, 1.0f };

    SDL_zero(depth_stencil_target);
    depth_stencil_target.texture = state->scene_depth_stencil;
    depth_stencil_target.load_op = SDL_GPU_LOADOP_CLEAR;
    depth_stencil_target.store_op = SDL_GPU_STOREOP_STORE;
    depth_stencil_target.clear_depth = 1.0f;
    depth_stencil_target.stencil_load_op = SDL_GPU_LOADOP_CLEAR;
    depth_stencil_target.stencil_store_op = SDL_GPU_STOREOP_STORE;
    depth_stencil_target.clear_stencil = 0;

    render_pass = SDL_BeginGPURenderPass(command_buffer, color_targets, 2, &depth_stencil_target);
    if (!render_pass) {
        return false;
    }

    SDL_zero(shadow_binding);
    shadow_binding.texture = state->shadow_depth;
    shadow_binding.sampler = state->nearest_clamp;

    SDL_BindGPUGraphicsPipeline(render_pass, state->scene_pipeline);
    SDL_BindGPUFragmentSamplers(render_pass, 0, &shadow_binding, 1);
    lesson36_draw_scene_objects(command_buffer, render_pass, state, view, view_projection, light_vp, demo->lesson.camera_position);

    SDL_BindGPUGraphicsPipeline(render_pass, state->grid_pipeline);
    SDL_BindGPUFragmentSamplers(render_pass, 0, &shadow_binding, 1);
    lesson36_draw_grid(command_buffer, render_pass, state, view, view_projection, light_vp, demo->lesson.camera_position);

    SDL_EndGPURenderPass(render_pass);
    state->gbuffer_pass_rendered = true;
    return true;
}

static bool lesson36_run_edge_pass(
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPUTexture *swapchain_texture,
    Lesson36State *state,
    Uint32 width,
    Uint32 height)
{
    SDL_GPUColorTargetInfo color_target;
    SDL_GPUTextureSamplerBinding samplers[3];
    Lesson36EdgeDetectUniforms uniforms;
    SDL_GPURenderPass *render_pass;

    SDL_zero(color_target);
    color_target.texture = swapchain_texture;
    color_target.load_op = SDL_GPU_LOADOP_CLEAR;
    color_target.store_op = SDL_GPU_STOREOP_STORE;
    color_target.clear_color = { 0.0f, 0.0f, 0.0f, 1.0f };
    render_pass = SDL_BeginGPURenderPass(command_buffer, &color_target, 1, nullptr);
    if (!render_pass) {
        return false;
    }

    SDL_zeroa(samplers);
    samplers[0].texture = state->scene_depth_stencil;
    samplers[0].sampler = state->nearest_clamp;
    samplers[1].texture = state->scene_normal;
    samplers[1].sampler = state->nearest_clamp;
    samplers[2].texture = state->scene_color;
    samplers[2].sampler = state->linear_clamp;

    SDL_zero(uniforms);
    uniforms.texel_size[0] = width > 0 ? 1.0f / (float)width : 0.0f;
    uniforms.texel_size[1] = height > 0 ? 1.0f / (float)height : 0.0f;
    if (state->render_mode == LESSON36_RENDER_MODE_XRAY) {
        uniforms.depth_threshold = LESSON36_EDGE_THRESHOLD_DISABLED;
        uniforms.normal_threshold = LESSON36_EDGE_THRESHOLD_DISABLED;
        uniforms.edge_source = LESSON36_EDGE_SOURCE_COMBINED;
    } else {
        uniforms.depth_threshold = LESSON36_DEPTH_THRESHOLD;
        uniforms.normal_threshold = LESSON36_NORMAL_THRESHOLD;
        uniforms.edge_source = (int)state->edge_source;
    }
    uniforms.show_debug = state->show_debug ? 1 : 0;

    SDL_BindGPUGraphicsPipeline(render_pass, state->edge_detect_pipeline);
    SDL_BindGPUFragmentSamplers(render_pass, 0, samplers, 3);
    SDL_PushGPUFragmentUniformData(command_buffer, 0, &uniforms, sizeof(uniforms));
    SDL_DrawGPUPrimitives(render_pass, 3, 1, 0, 0);
    SDL_EndGPURenderPass(render_pass);
    state->edge_pass_rendered = true;
    return true;
}

static void lesson36_draw_xray_mark_objects(
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    const Lesson36State *state,
    Mat4 view_projection)
{
    for (int i = 0; i < LESSON36_SCENE_OBJECT_COUNT; i += 1) {
        const Lesson36SceneObject *object = &state->scene_objects[i];
        Lesson36MarkVertUniforms uniforms;

        if (!object->is_xray_target) {
            continue;
        }
        uniforms.mvp = mat4_multiply(view_projection, lesson36_model_matrix(object));
        SDL_PushGPUVertexUniformData(command_buffer, 0, &uniforms, sizeof(uniforms));
        lesson36_bind_object(render_pass, state, object);
    }
}

static void lesson36_draw_ghost_objects(
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    const Lesson36State *state,
    Mat4 view,
    Mat4 view_projection)
{
    for (int i = 0; i < LESSON36_SCENE_OBJECT_COUNT; i += 1) {
        const Lesson36SceneObject *object = &state->scene_objects[i];
        const Mat4 model = lesson36_model_matrix(object);
        Lesson36GhostVertUniforms vertex_uniforms;
        Lesson36GhostFragUniforms fragment_uniforms;

        if (!object->is_xray_target) {
            continue;
        }

        vertex_uniforms.mvp = mat4_multiply(view_projection, model);
        vertex_uniforms.model_view = mat4_multiply(view, model);
        SDL_PushGPUVertexUniformData(command_buffer, 0, &vertex_uniforms, sizeof(vertex_uniforms));

        SDL_zero(fragment_uniforms);
        fragment_uniforms.ghost_color_power[0] = object->color.x;
        fragment_uniforms.ghost_color_power[1] = object->color.y;
        fragment_uniforms.ghost_color_power[2] = object->color.z;
        fragment_uniforms.ghost_color_power[3] = LESSON36_GHOST_POWER;
        fragment_uniforms.ghost_brightness_pad[0] = LESSON36_GHOST_BRIGHTNESS;
        SDL_PushGPUFragmentUniformData(command_buffer, 0, &fragment_uniforms, sizeof(fragment_uniforms));

        lesson36_bind_object(render_pass, state, object);
    }
}

static bool lesson36_run_xray_passes(
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPUTexture *swapchain_texture,
    Lesson36State *state,
    Mat4 view,
    Mat4 view_projection)
{
    SDL_GPUColorTargetInfo color_target;
    SDL_GPUDepthStencilTargetInfo depth_stencil_target;
    SDL_GPURenderPass *render_pass;

    SDL_zero(color_target);
    color_target.texture = swapchain_texture;
    color_target.load_op = SDL_GPU_LOADOP_LOAD;
    color_target.store_op = SDL_GPU_STOREOP_STORE;
    SDL_zero(depth_stencil_target);
    depth_stencil_target.texture = state->scene_depth_stencil;
    depth_stencil_target.load_op = SDL_GPU_LOADOP_LOAD;
    depth_stencil_target.store_op = SDL_GPU_STOREOP_STORE;
    depth_stencil_target.stencil_load_op = SDL_GPU_LOADOP_CLEAR;
    depth_stencil_target.stencil_store_op = SDL_GPU_STOREOP_STORE;
    depth_stencil_target.clear_stencil = 0;
    render_pass = SDL_BeginGPURenderPass(command_buffer, &color_target, 1, &depth_stencil_target);
    if (!render_pass) {
        return false;
    }
    SDL_BindGPUGraphicsPipeline(render_pass, state->xray_mark_pipeline);
    lesson36_draw_xray_mark_objects(command_buffer, render_pass, state, view_projection);
    SDL_EndGPURenderPass(render_pass);
    state->xray_mark_pass_rendered = true;

    SDL_zero(color_target);
    color_target.texture = swapchain_texture;
    color_target.load_op = SDL_GPU_LOADOP_LOAD;
    color_target.store_op = SDL_GPU_STOREOP_STORE;
    SDL_zero(depth_stencil_target);
    depth_stencil_target.texture = state->scene_depth_stencil;
    depth_stencil_target.load_op = SDL_GPU_LOADOP_LOAD;
    depth_stencil_target.store_op = SDL_GPU_STOREOP_DONT_CARE;
    depth_stencil_target.stencil_load_op = SDL_GPU_LOADOP_LOAD;
    depth_stencil_target.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
    render_pass = SDL_BeginGPURenderPass(command_buffer, &color_target, 1, &depth_stencil_target);
    if (!render_pass) {
        return false;
    }
    SDL_BindGPUGraphicsPipeline(render_pass, state->ghost_pipeline);
    SDL_SetGPUStencilReference(render_pass, 0);
    lesson36_draw_ghost_objects(command_buffer, render_pass, state, view, view_projection);
    SDL_EndGPURenderPass(render_pass);
    state->ghost_pass_rendered = true;
    return true;
}

bool ForgeGpuRenderLesson36(ForgeGpuDemo *demo, SDL_GPUCommandBuffer *command_buffer, SDL_GPUTexture *swapchain_texture, Uint32 width, Uint32 height)
{
    Lesson36State *state = lesson36_state(demo);
    Mat4 view;
    Mat4 projection;
    Mat4 view_projection;
    Mat4 light_vp;

    if (!state) {
        SDL_SetError("lesson 36 state is missing");
        return false;
    }
    if (!lesson36_ensure_targets(demo, state, width, height)) {
        return false;
    }

    ForgeGpuUpdateCameraFromInput(demo);
    ForgeGpuCameraViewProjection(demo, width, height, LESSON36_FAR_PLANE, &view, &projection);
    view_projection = mat4_multiply(projection, view);
    light_vp = lesson36_light_view_projection(state);

    state->shadow_pass_rendered = false;
    state->gbuffer_pass_rendered = false;
    state->edge_pass_rendered = false;
    state->xray_mark_pass_rendered = false;
    state->ghost_pass_rendered = false;

    if (!lesson36_run_shadow_pass(command_buffer, state, light_vp) ||
        !lesson36_run_gbuffer_pass(demo, command_buffer, state, view, view_projection, light_vp) ||
        !lesson36_run_edge_pass(command_buffer, swapchain_texture, state, width, height)) {
        return false;
    }

    if (state->render_mode == LESSON36_RENDER_MODE_XRAY &&
        !lesson36_run_xray_passes(command_buffer, swapchain_texture, state, view, view_projection)) {
        return false;
    }

    return true;
}

void ForgeGpuDebugLesson36(ForgeGpuDemo *demo)
{
    Lesson36State *state = lesson36_state(demo);
    static const char *const mode_names[] = { "Edge detection", "X-ray" };
    static const char *const edge_source_names[] = { "Depth", "Normal", "Combined" };

    if (!state) {
        return;
    }

    ImGui::Text("Mode: %s", mode_names[state->render_mode]);
    ImGui::Text("Edge source: %s", edge_source_names[state->edge_source]);
    ImGui::Text("Debug G-buffer: %s", state->show_debug ? "on" : "off");
    ImGui::Text("Depth-stencil: %s", lesson36_format_name(state->scene_depth_stencil_format));
    ImGui::Text("Shadow depth: %s", lesson36_format_name(state->shadow_depth_format));
    ImGui::Text("Passes: shadow %d, gbuffer %d, edge %d, mark %d, ghost %d",
        state->shadow_pass_rendered ? 1 : 0,
        state->gbuffer_pass_rendered ? 1 : 0,
        state->edge_pass_rendered ? 1 : 0,
        state->xray_mark_pass_rendered ? 1 : 0,
        state->ghost_pass_rendered ? 1 : 0);
}

void ForgeGpuControlsLesson36(ForgeGpuDemo *demo)
{
    (void)demo;
    ImGui::TextUnformatted("1/2: edge or X-ray mode");
    ImGui::TextUnformatted("E: cycle edge source");
    ImGui::TextUnformatted("V: toggle G-buffer debug");
}

bool ForgeGpuHandleLesson36Event(ForgeGpuDemo *demo, const SDL_Event *event)
{
    Lesson36State *state = lesson36_state(demo);

    if (!state || event->type != SDL_EVENT_KEY_DOWN || event->key.repeat) {
        return false;
    }

    if (event->key.key == SDLK_1) {
        state->render_mode = LESSON36_RENDER_MODE_EDGE_DETECT;
        return true;
    }
    if (event->key.key == SDLK_2) {
        state->render_mode = LESSON36_RENDER_MODE_XRAY;
        return true;
    }
    if (event->key.key == SDLK_E) {
        state->edge_source = (Lesson36EdgeSource)(((int)state->edge_source + 1) % LESSON36_EDGE_SOURCE_COUNT);
        return true;
    }
    if (event->key.key == SDLK_V) {
        state->show_debug = !state->show_debug;
        return true;
    }
    return false;
}

void ForgeGpuExportLesson36Metrics(ForgeGpuDemo *demo)
{
    Lesson36State *state = lesson36_state(demo);

    if (!state) {
        return;
    }

    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson36Mode", (double)state->render_mode);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson36EdgeSource", (double)state->edge_source);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson36DebugView", state->show_debug ? 1.0 : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson36ShadowPass", state->shadow_pass_rendered ? 1.0 : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson36GBufferPass", state->gbuffer_pass_rendered ? 1.0 : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson36EdgePass", state->edge_pass_rendered ? 1.0 : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson36XrayMarkPass", state->xray_mark_pass_rendered ? 1.0 : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson36GhostPass", state->ghost_pass_rendered ? 1.0 : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson36DepthStencilD24S8", state->scene_depth_stencil_format == SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT ? 1.0 : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson36DepthStencilD32S8", state->scene_depth_stencil_format == SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT ? 1.0 : 0.0);
}

void ForgeGpuDestroyLesson36(ForgeGpuDemo *demo)
{
    Lesson36State *state = lesson36_state(demo);

    if (!state) {
        return;
    }

    if (state->shadow_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, state->shadow_pipeline);
    }
    if (state->scene_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, state->scene_pipeline);
    }
    if (state->grid_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, state->grid_pipeline);
    }
    if (state->edge_detect_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, state->edge_detect_pipeline);
    }
    if (state->xray_mark_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, state->xray_mark_pipeline);
    }
    if (state->ghost_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, state->ghost_pipeline);
    }
    if (state->shadow_depth) {
        SDL_ReleaseGPUTexture(demo->device, state->shadow_depth);
    }
    if (state->scene_color) {
        SDL_ReleaseGPUTexture(demo->device, state->scene_color);
    }
    if (state->scene_normal) {
        SDL_ReleaseGPUTexture(demo->device, state->scene_normal);
    }
    if (state->scene_depth_stencil) {
        SDL_ReleaseGPUTexture(demo->device, state->scene_depth_stencil);
    }
    if (state->nearest_clamp) {
        SDL_ReleaseGPUSampler(demo->device, state->nearest_clamp);
    }
    if (state->linear_clamp) {
        SDL_ReleaseGPUSampler(demo->device, state->linear_clamp);
    }
    if (state->cube_vb) {
        SDL_ReleaseGPUBuffer(demo->device, state->cube_vb);
    }
    if (state->cube_ib) {
        SDL_ReleaseGPUBuffer(demo->device, state->cube_ib);
    }
    if (state->sphere_vb) {
        SDL_ReleaseGPUBuffer(demo->device, state->sphere_vb);
    }
    if (state->sphere_ib) {
        SDL_ReleaseGPUBuffer(demo->device, state->sphere_ib);
    }
    if (state->grid_vb) {
        SDL_ReleaseGPUBuffer(demo->device, state->grid_vb);
    }
    if (state->grid_ib) {
        SDL_ReleaseGPUBuffer(demo->device, state->grid_ib);
    }

    SDL_free(state);
    demo->lesson.private_state = nullptr;
}

bool ForgeGpuCreateLesson36(ForgeGpuDemo *demo)
{
    Lesson36State *state = (Lesson36State *)SDL_calloc(1, sizeof(*state));

    if (!state) {
        SDL_OutOfMemory();
        return false;
    }
    demo->lesson.private_state = state;

    state->shadow_depth_format = lesson36_select_shadow_depth_format(demo->device);
    state->scene_depth_stencil_format = lesson36_select_depth_stencil_format(demo->device);
    if (state->shadow_depth_format == SDL_GPU_TEXTUREFORMAT_INVALID) {
        SDL_SetError("lesson 36 requires a sampled depth format for shadow maps");
        goto fail;
    }
    if (state->scene_depth_stencil_format == SDL_GPU_TEXTUREFORMAT_INVALID) {
        SDL_SetError("lesson 36 requires a sampled depth-stencil format");
        goto fail;
    }

    state->shadow_depth = ForgeGpuCreateSampledDepthTexture(demo, LESSON36_SHADOW_MAP_SIZE, LESSON36_SHADOW_MAP_SIZE, state->shadow_depth_format);
    state->nearest_clamp = ForgeGpuCreateSamplerWithAddress(
        demo->device,
        SDL_GPU_FILTER_NEAREST,
        SDL_GPU_FILTER_NEAREST,
        SDL_GPU_SAMPLERMIPMAPMODE_NEAREST,
        SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        0.0f);
    state->linear_clamp = ForgeGpuCreateSamplerWithAddress(
        demo->device,
        SDL_GPU_FILTER_LINEAR,
        SDL_GPU_FILTER_LINEAR,
        SDL_GPU_SAMPLERMIPMAPMODE_NEAREST,
        SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        0.0f);
    if (!state->shadow_depth || !state->nearest_clamp || !state->linear_clamp) {
        goto fail;
    }

    if (!lesson36_create_geometry(demo, state) || !lesson36_create_pipelines(demo, state)) {
        goto fail;
    }

    lesson36_init_scene_objects(state);
    state->light_dir = vec3_normalize({ LESSON36_LIGHT_DIR_X, LESSON36_LIGHT_DIR_Y, LESSON36_LIGHT_DIR_Z });
    state->render_mode = LESSON36_RENDER_MODE_EDGE_DETECT;
    state->edge_source = LESSON36_EDGE_SOURCE_COMBINED;
    lesson36_init_camera(demo);
    return true;

fail:
    ForgeGpuDestroyLesson36(demo);
    return false;
}
