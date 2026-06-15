#include "forge_gpu_lessons.h"

#include "forge_gpu_browser_status.h"
#include "forge_gpu_camera.h"
#include "forge_gpu_gpu_helpers.h"
#include "forge_gpu_lesson_common.h"
#include "forge_gpu_math.h"
#include "forge_gpu_shader_layouts.h"
#include "shaders/generated/forge_gpu_lesson_37_shaders.h"
#include "imgui.h"

#include <stddef.h>

#define LESSON37_SHADOW_MAP_SIZE 2048u
#define LESSON37_SHADOW_DEPTH_FORMAT SDL_GPU_TEXTUREFORMAT_D32_FLOAT
#define LESSON37_FAR_PLANE 200.0f
#define LESSON37_MOVE_SPEED 5.0f
#define LESSON37_MOUSE_SENSITIVITY 0.003f
#define LESSON37_PITCH_CLAMP 1.5f
#define LESSON37_GRID_HALF_SIZE 50.0f
#define LESSON37_GRID_INDEX_COUNT 6u
#define LESSON37_OBJECT_COUNT 10
#define LESSON37_SPHERE_LAT_SEGS 20
#define LESSON37_SPHERE_LON_SEGS 20
#define LESSON37_STENCIL_OUTLINE 200u
#define LESSON37_OUTLINE_SCALE 1.04f
#define LESSON37_CROSSHAIR_SIZE 0.02f
#define LESSON37_CROSSHAIR_THICK 0.002f
#define LESSON37_CROSSHAIR_VERTS 16
#define LESSON37_CROSSHAIR_INDICES 24

enum Lesson37Shape
{
    LESSON37_SHAPE_CUBE = 0,
    LESSON37_SHAPE_SPHERE = 1
};

enum Lesson37PickMethod
{
    LESSON37_PICK_COLOR_ID = 0,
    LESSON37_PICK_STENCIL_ID_UNSUPPORTED = 1
};

struct Lesson37Vertex
{
    float position[3];
    float normal[3];
};

struct Lesson37CrosshairVertex
{
    float position[2];
    float color[4];
};

struct Lesson37SceneVertUniforms
{
    Mat4 mvp;
    Mat4 model;
    Mat4 light_vp;
};

struct Lesson37SceneFragUniforms
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

struct Lesson37GridVertUniforms
{
    Mat4 vp;
    Mat4 light_vp;
};

struct Lesson37GridFragUniforms
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

struct Lesson37OutlineFragUniforms
{
    float outline_color[4];
};

struct Lesson37IdVertUniforms
{
    Mat4 mvp;
};

struct Lesson37IdFragUniforms
{
    float id_color[4];
};

struct Lesson37Object
{
    const char *name;
    Lesson37Shape shape;
    Vec3 position;
    float scale;
    float color[4];
};

struct Lesson37State
{
    SDL_GPUGraphicsPipeline *shadow_pipeline;
    SDL_GPUGraphicsPipeline *scene_pipeline;
    SDL_GPUGraphicsPipeline *grid_pipeline;
    SDL_GPUGraphicsPipeline *id_pipeline;
    SDL_GPUGraphicsPipeline *crosshair_pipeline;
    SDL_GPUGraphicsPipeline *outline_write_pipeline;
    SDL_GPUGraphicsPipeline *outline_draw_pipeline;
    SDL_GPUTexture *shadow_depth;
    SDL_GPUTexture *main_depth;
    SDL_GPUTexture *id_texture;
    SDL_GPUTexture *id_depth;
    Uint32 target_width;
    Uint32 target_height;
    SDL_GPUSampler *nearest_clamp;
    SDL_GPUBuffer *cube_vb;
    SDL_GPUBuffer *cube_ib;
    SDL_GPUBuffer *sphere_vb;
    SDL_GPUBuffer *sphere_ib;
    SDL_GPUBuffer *grid_vb;
    SDL_GPUBuffer *grid_ib;
    SDL_GPUBuffer *crosshair_vb;
    SDL_GPUBuffer *crosshair_ib;
    Uint32 cube_index_count;
    Uint32 sphere_index_count;
    SDL_GPUTextureFormat depth_stencil_format;
    SDL_GPUTransferBuffer *pick_readback;
    Lesson37Object objects[LESSON37_OBJECT_COUNT];
    Vec3 light_dir;
    Mat4 light_vp;
    Lesson37PickMethod pick_method;
    int selected_object;
    bool pick_requested;
    bool pick_at_center;
    Uint32 pick_x;
    Uint32 pick_y;
    bool readback_pending;
    bool validation_pick_requested;
    bool shadow_pass_rendered;
    bool scene_pass_rendered;
    bool color_id_pass_rendered;
    bool outline_pass_rendered;
    bool pick_readback_completed;
    bool last_stencil_pick_rejected;
    Uint32 stencil_rejected_count;
};

static_assert(sizeof(Lesson37Vertex) == 24, "lesson 37 vertex size must match HLSL layout");
static_assert(sizeof(Lesson37CrosshairVertex) == 24, "lesson 37 crosshair vertex size must match HLSL layout");
static_assert(sizeof(Lesson37SceneVertUniforms) == 192, "lesson 37 scene vertex uniform size must match HLSL layout");
static_assert(sizeof(Lesson37SceneFragUniforms) == 96, "lesson 37 scene fragment uniform size must match strict cbuffer layout");
static_assert(sizeof(Lesson37GridVertUniforms) == 128, "lesson 37 grid vertex uniform size must match HLSL layout");
static_assert(sizeof(Lesson37GridFragUniforms) == 96, "lesson 37 grid fragment uniform size must match HLSL layout");
static_assert(sizeof(Lesson37OutlineFragUniforms) == 16, "lesson 37 outline fragment uniform size must match HLSL layout");
static_assert(sizeof(Lesson37IdVertUniforms) == 64, "lesson 37 ID vertex uniform size must match HLSL layout");
static_assert(sizeof(Lesson37IdFragUniforms) == 16, "lesson 37 ID fragment uniform size must match HLSL layout");

static Lesson37State *lesson37_state(ForgeGpuDemo *demo)
{
    return (Lesson37State *)demo->lesson.private_state;
}

static void lesson37_init_camera(ForgeGpuDemo *demo)
{
    demo->lesson.camera_position = { 0.0f, 2.0f, 3.0f };
    demo->lesson.camera_yaw = -0.15f;
    demo->lesson.camera_pitch = -0.25f;
    demo->lesson.pitch_clamp = LESSON37_PITCH_CLAMP;
    demo->lesson.mouse_sensitivity = LESSON37_MOUSE_SENSITIVITY;
    demo->lesson.move_speed = LESSON37_MOVE_SPEED;
    demo->lesson.last_ticks = SDL_GetTicks();
}

static SDL_GPUTextureFormat lesson37_select_depth_stencil_format(SDL_GPUDevice *device)
{
    if (SDL_GPUTextureSupportsFormat(device, SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT, SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET)) {
        return SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT;
    }
    if (SDL_GPUTextureSupportsFormat(device, SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT, SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET)) {
        return SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT;
    }
    return SDL_GPU_TEXTUREFORMAT_INVALID;
}

static const char *lesson37_format_name(SDL_GPUTextureFormat format)
{
    switch (format) {
    case SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT:
        return "D24_UNORM_S8_UINT";
    case SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT:
        return "D32_FLOAT_S8_UINT";
    default:
        return "unknown";
    }
}

static void lesson37_add_box(
    float cx,
    float cy,
    float cz,
    float hx,
    float hy,
    float hz,
    Lesson37Vertex *vertices,
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

static void lesson37_generate_cube(
    Lesson37Vertex vertices[24],
    Uint32 *vertex_count,
    Uint16 indices[36],
    Uint32 *index_count)
{
    *vertex_count = 0;
    *index_count = 0;
    lesson37_add_box(0.0f, 0.0f, 0.0f, 0.5f, 0.5f, 0.5f, vertices, vertex_count, indices, index_count);
}

static void lesson37_generate_sphere(
    Lesson37Vertex *vertices,
    Uint32 *vertex_count,
    Uint16 *indices,
    Uint32 *index_count)
{
    Uint32 v = 0;
    Uint32 idx = 0;

    for (int lat = 0; lat <= LESSON37_SPHERE_LAT_SEGS; lat += 1) {
        const float theta = (float)lat * FORGE_GPU_PI / (float)LESSON37_SPHERE_LAT_SEGS;
        const float sin_theta = SDL_sinf(theta);
        const float cos_theta = SDL_cosf(theta);

        for (int lon = 0; lon <= LESSON37_SPHERE_LON_SEGS; lon += 1) {
            const float phi = (float)lon * 2.0f * FORGE_GPU_PI / (float)LESSON37_SPHERE_LON_SEGS;
            const float sin_phi = SDL_sinf(phi);
            const float cos_phi = SDL_cosf(phi);
            const float x = cos_phi * sin_theta;
            const float y = cos_theta;
            const float z = sin_phi * sin_theta;

            vertices[v].position[0] = x * 0.5f;
            vertices[v].position[1] = y * 0.5f;
            vertices[v].position[2] = z * 0.5f;
            vertices[v].normal[0] = x;
            vertices[v].normal[1] = y;
            vertices[v].normal[2] = z;
            v += 1;
        }
    }

    for (int lat = 0; lat < LESSON37_SPHERE_LAT_SEGS; lat += 1) {
        for (int lon = 0; lon < LESSON37_SPHERE_LON_SEGS; lon += 1) {
            const Uint16 a = (Uint16)(lat * (LESSON37_SPHERE_LON_SEGS + 1) + lon);
            const Uint16 b = (Uint16)(a + LESSON37_SPHERE_LON_SEGS + 1);

            if (lat != 0) {
                indices[idx++] = a;
                indices[idx++] = b;
                indices[idx++] = (Uint16)(a + 1);
            }
            if (lat != LESSON37_SPHERE_LAT_SEGS - 1) {
                indices[idx++] = (Uint16)(a + 1);
                indices[idx++] = b;
                indices[idx++] = (Uint16)(b + 1);
            }
        }
    }

    *vertex_count = v;
    *index_count = idx;
}

static void lesson37_set_crosshair_vertex(Lesson37CrosshairVertex *vertex, float x, float y)
{
    vertex->position[0] = x;
    vertex->position[1] = y;
    vertex->color[0] = 1.0f;
    vertex->color[1] = 1.0f;
    vertex->color[2] = 1.0f;
    vertex->color[3] = 0.8f;
}

static bool lesson37_create_crosshair_buffers(ForgeGpuDemo *demo, Lesson37State *state)
{
    Lesson37CrosshairVertex vertices[LESSON37_CROSSHAIR_VERTS];
    Uint16 indices[LESSON37_CROSSHAIR_INDICES];
    const float size = LESSON37_CROSSHAIR_SIZE;
    const float thick = LESSON37_CROSSHAIR_THICK;
    const float gap = LESSON37_CROSSHAIR_THICK * 2.0f;
    int v = 0;
    int i = 0;

    lesson37_set_crosshair_vertex(&vertices[v++], -size, -thick);
    lesson37_set_crosshair_vertex(&vertices[v++], -gap, -thick);
    lesson37_set_crosshair_vertex(&vertices[v++], -gap, thick);
    lesson37_set_crosshair_vertex(&vertices[v++], -size, thick);
    indices[i++] = 0; indices[i++] = 1; indices[i++] = 2; indices[i++] = 0; indices[i++] = 2; indices[i++] = 3;

    lesson37_set_crosshair_vertex(&vertices[v++], gap, -thick);
    lesson37_set_crosshair_vertex(&vertices[v++], size, -thick);
    lesson37_set_crosshair_vertex(&vertices[v++], size, thick);
    lesson37_set_crosshair_vertex(&vertices[v++], gap, thick);
    indices[i++] = 4; indices[i++] = 5; indices[i++] = 6; indices[i++] = 4; indices[i++] = 6; indices[i++] = 7;

    lesson37_set_crosshair_vertex(&vertices[v++], -thick, gap);
    lesson37_set_crosshair_vertex(&vertices[v++], thick, gap);
    lesson37_set_crosshair_vertex(&vertices[v++], thick, size);
    lesson37_set_crosshair_vertex(&vertices[v++], -thick, size);
    indices[i++] = 8; indices[i++] = 9; indices[i++] = 10; indices[i++] = 8; indices[i++] = 10; indices[i++] = 11;

    lesson37_set_crosshair_vertex(&vertices[v++], -thick, -size);
    lesson37_set_crosshair_vertex(&vertices[v++], thick, -size);
    lesson37_set_crosshair_vertex(&vertices[v++], thick, -gap);
    lesson37_set_crosshair_vertex(&vertices[v++], -thick, -gap);
    indices[i++] = 12; indices[i++] = 13; indices[i++] = 14; indices[i++] = 12; indices[i++] = 14; indices[i++] = 15;

    state->crosshair_vb = ForgeGpuCreateBufferWithData(demo->device, SDL_GPU_BUFFERUSAGE_VERTEX, vertices, sizeof(vertices));
    state->crosshair_ib = ForgeGpuCreateBufferWithData(demo->device, SDL_GPU_BUFFERUSAGE_INDEX, indices, sizeof(indices));
    return state->crosshair_vb && state->crosshair_ib;
}

static bool lesson37_create_geometry(ForgeGpuDemo *demo, Lesson37State *state)
{
    Lesson37Vertex cube_vertices[24];
    Uint16 cube_indices[36];
    Uint32 cube_vertex_count = 0;
    Uint32 cube_index_count = 0;
    Lesson37Vertex sphere_vertices[(LESSON37_SPHERE_LAT_SEGS + 1) * (LESSON37_SPHERE_LON_SEGS + 1)];
    Uint16 sphere_indices[LESSON37_SPHERE_LAT_SEGS * LESSON37_SPHERE_LON_SEGS * 6];
    Uint32 sphere_vertex_count = 0;
    Uint32 sphere_index_count = 0;
    const Vec3 grid_vertices[4] = {
        { -LESSON37_GRID_HALF_SIZE, 0.0f, -LESSON37_GRID_HALF_SIZE },
        {  LESSON37_GRID_HALF_SIZE, 0.0f, -LESSON37_GRID_HALF_SIZE },
        {  LESSON37_GRID_HALF_SIZE, 0.0f,  LESSON37_GRID_HALF_SIZE },
        { -LESSON37_GRID_HALF_SIZE, 0.0f,  LESSON37_GRID_HALF_SIZE },
    };
    const Uint16 grid_indices[LESSON37_GRID_INDEX_COUNT] = { 0, 1, 2, 0, 2, 3 };

    lesson37_generate_cube(cube_vertices, &cube_vertex_count, cube_indices, &cube_index_count);
    lesson37_generate_sphere(sphere_vertices, &sphere_vertex_count, sphere_indices, &sphere_index_count);

    state->cube_index_count = cube_index_count;
    state->sphere_index_count = sphere_index_count;
    state->cube_vb = ForgeGpuCreateBufferWithData(demo->device, SDL_GPU_BUFFERUSAGE_VERTEX, cube_vertices, cube_vertex_count * sizeof(Lesson37Vertex));
    state->cube_ib = ForgeGpuCreateBufferWithData(demo->device, SDL_GPU_BUFFERUSAGE_INDEX, cube_indices, cube_index_count * sizeof(Uint16));
    state->sphere_vb = ForgeGpuCreateBufferWithData(demo->device, SDL_GPU_BUFFERUSAGE_VERTEX, sphere_vertices, sphere_vertex_count * sizeof(Lesson37Vertex));
    state->sphere_ib = ForgeGpuCreateBufferWithData(demo->device, SDL_GPU_BUFFERUSAGE_INDEX, sphere_indices, sphere_index_count * sizeof(Uint16));
    state->grid_vb = ForgeGpuCreateBufferWithData(demo->device, SDL_GPU_BUFFERUSAGE_VERTEX, grid_vertices, sizeof(grid_vertices));
    state->grid_ib = ForgeGpuCreateBufferWithData(demo->device, SDL_GPU_BUFFERUSAGE_INDEX, grid_indices, sizeof(grid_indices));
    return state->cube_vb && state->cube_ib && state->sphere_vb && state->sphere_ib && state->grid_vb && state->grid_ib &&
        lesson37_create_crosshair_buffers(demo, state);
}

static SDL_GPUGraphicsPipeline *lesson37_create_pipeline(
    ForgeGpuDemo *demo,
    SDL_GPUShader *vertex_shader,
    SDL_GPUShader *fragment_shader,
    const SDL_GPUVertexBufferDescription *vertex_buffer,
    const SDL_GPUVertexAttribute *attributes,
    Uint32 num_attributes,
    SDL_GPUTextureFormat color_format,
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
    color_target.format = color_format;
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
    pipeline_info.vertex_input_state.num_vertex_buffers = 1;
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
    if (color_format != SDL_GPU_TEXTUREFORMAT_INVALID) {
        pipeline_info.target_info.color_target_descriptions = &color_target;
        pipeline_info.target_info.num_color_targets = 1;
    }
    pipeline_info.target_info.has_depth_stencil_target = has_depth_stencil_target;
    pipeline_info.target_info.depth_stencil_format = depth_stencil_format;

    return SDL_CreateGPUGraphicsPipeline(demo->device, &pipeline_info);
}

static bool lesson37_create_pipelines(ForgeGpuDemo *demo)
{
    Lesson37State *state = lesson37_state(demo);
    SDL_GPUShader *scene_vertex_shader = nullptr;
    SDL_GPUShader *scene_fragment_shader = nullptr;
    SDL_GPUShader *shadow_vertex_shader = nullptr;
    SDL_GPUShader *shadow_fragment_shader = nullptr;
    SDL_GPUShader *grid_vertex_shader = nullptr;
    SDL_GPUShader *grid_fragment_shader = nullptr;
    SDL_GPUShader *outline_fragment_shader = nullptr;
    SDL_GPUShader *id_vertex_shader = nullptr;
    SDL_GPUShader *id_fragment_shader = nullptr;
    SDL_GPUShader *crosshair_vertex_shader = nullptr;
    SDL_GPUShader *crosshair_fragment_shader = nullptr;
    SDL_GPUVertexBufferDescription scene_vertex_buffer;
    SDL_GPUVertexAttribute scene_attributes[2];
    SDL_GPUVertexBufferDescription grid_vertex_buffer;
    SDL_GPUVertexAttribute grid_attribute;
    SDL_GPUVertexBufferDescription crosshair_vertex_buffer;
    SDL_GPUVertexAttribute crosshair_attributes[2];
    bool ok = false;

    scene_vertex_shader = ForgeGpuCreateShader(demo->device, SDL_GPU_SHADERSTAGE_VERTEX, lesson37_scene_vert_wgsl, lesson37_scene_vert_wgsl_size, lesson37_scene_vert_msl, lesson37_scene_vert_msl_size, 0, 0, 0, 1);
    scene_fragment_shader = ForgeGpuCreateShaderWithResourceLayout(demo->device, lesson37_scene_frag_wgsl, lesson37_scene_frag_wgsl_size, lesson37_scene_frag_msl, lesson37_scene_frag_msl_size, ForgeGpuShaderLayout_lesson37_scene_frag());
    shadow_vertex_shader = ForgeGpuCreateShader(demo->device, SDL_GPU_SHADERSTAGE_VERTEX, lesson37_shadow_vert_wgsl, lesson37_shadow_vert_wgsl_size, lesson37_shadow_vert_msl, lesson37_shadow_vert_msl_size, 0, 0, 0, 1);
    shadow_fragment_shader = ForgeGpuCreateShader(demo->device, SDL_GPU_SHADERSTAGE_FRAGMENT, lesson37_shadow_frag_wgsl, lesson37_shadow_frag_wgsl_size, lesson37_shadow_frag_msl, lesson37_shadow_frag_msl_size, 0, 0, 0, 0);
    grid_vertex_shader = ForgeGpuCreateShader(demo->device, SDL_GPU_SHADERSTAGE_VERTEX, lesson37_grid_vert_wgsl, lesson37_grid_vert_wgsl_size, lesson37_grid_vert_msl, lesson37_grid_vert_msl_size, 0, 0, 0, 1);
    grid_fragment_shader = ForgeGpuCreateShaderWithResourceLayout(demo->device, lesson37_grid_frag_wgsl, lesson37_grid_frag_wgsl_size, lesson37_grid_frag_msl, lesson37_grid_frag_msl_size, ForgeGpuShaderLayout_lesson37_grid_frag());
    outline_fragment_shader = ForgeGpuCreateShader(demo->device, SDL_GPU_SHADERSTAGE_FRAGMENT, lesson37_outline_frag_wgsl, lesson37_outline_frag_wgsl_size, lesson37_outline_frag_msl, lesson37_outline_frag_msl_size, 0, 0, 0, 1);
    id_vertex_shader = ForgeGpuCreateShader(demo->device, SDL_GPU_SHADERSTAGE_VERTEX, lesson37_id_pass_vert_wgsl, lesson37_id_pass_vert_wgsl_size, lesson37_id_pass_vert_msl, lesson37_id_pass_vert_msl_size, 0, 0, 0, 1);
    id_fragment_shader = ForgeGpuCreateShader(demo->device, SDL_GPU_SHADERSTAGE_FRAGMENT, lesson37_id_pass_frag_wgsl, lesson37_id_pass_frag_wgsl_size, lesson37_id_pass_frag_msl, lesson37_id_pass_frag_msl_size, 0, 0, 0, 1);
    crosshair_vertex_shader = ForgeGpuCreateShader(demo->device, SDL_GPU_SHADERSTAGE_VERTEX, lesson37_crosshair_vert_wgsl, lesson37_crosshair_vert_wgsl_size, lesson37_crosshair_vert_msl, lesson37_crosshair_vert_msl_size, 0, 0, 0, 0);
    crosshair_fragment_shader = ForgeGpuCreateShader(demo->device, SDL_GPU_SHADERSTAGE_FRAGMENT, lesson37_crosshair_frag_wgsl, lesson37_crosshair_frag_wgsl_size, lesson37_crosshair_frag_msl, lesson37_crosshair_frag_msl_size, 0, 0, 0, 0);
    if (!scene_vertex_shader || !scene_fragment_shader || !shadow_vertex_shader || !shadow_fragment_shader ||
        !grid_vertex_shader || !grid_fragment_shader || !outline_fragment_shader || !id_vertex_shader ||
        !id_fragment_shader || !crosshair_vertex_shader || !crosshair_fragment_shader) {
        goto done;
    }

    SDL_zero(scene_vertex_buffer);
    scene_vertex_buffer.slot = 0;
    scene_vertex_buffer.pitch = sizeof(Lesson37Vertex);
    scene_vertex_buffer.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    SDL_zeroa(scene_attributes);
    scene_attributes[0].location = 0;
    scene_attributes[0].buffer_slot = 0;
    scene_attributes[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    scene_attributes[0].offset = offsetof(Lesson37Vertex, position);
    scene_attributes[1].location = 1;
    scene_attributes[1].buffer_slot = 0;
    scene_attributes[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    scene_attributes[1].offset = offsetof(Lesson37Vertex, normal);

    SDL_zero(grid_vertex_buffer);
    grid_vertex_buffer.slot = 0;
    grid_vertex_buffer.pitch = sizeof(Vec3);
    grid_vertex_buffer.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    SDL_zero(grid_attribute);
    grid_attribute.location = 0;
    grid_attribute.buffer_slot = 0;
    grid_attribute.format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    grid_attribute.offset = 0;

    SDL_zero(crosshair_vertex_buffer);
    crosshair_vertex_buffer.slot = 0;
    crosshair_vertex_buffer.pitch = sizeof(Lesson37CrosshairVertex);
    crosshair_vertex_buffer.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    SDL_zeroa(crosshair_attributes);
    crosshair_attributes[0].location = 0;
    crosshair_attributes[0].buffer_slot = 0;
    crosshair_attributes[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    crosshair_attributes[0].offset = offsetof(Lesson37CrosshairVertex, position);
    crosshair_attributes[1].location = 1;
    crosshair_attributes[1].buffer_slot = 0;
    crosshair_attributes[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
    crosshair_attributes[1].offset = offsetof(Lesson37CrosshairVertex, color);

    state->shadow_pipeline = lesson37_create_pipeline(
        demo, shadow_vertex_shader, shadow_fragment_shader, &scene_vertex_buffer, scene_attributes, 2,
        SDL_GPU_TEXTUREFORMAT_INVALID, false, true, LESSON37_SHADOW_DEPTH_FORMAT,
        true, true, SDL_GPU_COMPAREOP_LESS,
        false, SDL_GPU_COMPAREOP_ALWAYS, SDL_GPU_STENCILOP_KEEP, 0, 0, SDL_GPU_CULLMODE_BACK);
    state->scene_pipeline = lesson37_create_pipeline(
        demo, scene_vertex_shader, scene_fragment_shader, &scene_vertex_buffer, scene_attributes, 2,
        demo->color_format, false, true, state->depth_stencil_format,
        true, true, SDL_GPU_COMPAREOP_LESS,
        false, SDL_GPU_COMPAREOP_ALWAYS, SDL_GPU_STENCILOP_KEEP, 0, 0, SDL_GPU_CULLMODE_BACK);
    state->grid_pipeline = lesson37_create_pipeline(
        demo, grid_vertex_shader, grid_fragment_shader, &grid_vertex_buffer, &grid_attribute, 1,
        demo->color_format, false, true, state->depth_stencil_format,
        true, true, SDL_GPU_COMPAREOP_LESS_OR_EQUAL,
        false, SDL_GPU_COMPAREOP_ALWAYS, SDL_GPU_STENCILOP_KEEP, 0, 0, SDL_GPU_CULLMODE_NONE);
    state->id_pipeline = lesson37_create_pipeline(
        demo, id_vertex_shader, id_fragment_shader, &scene_vertex_buffer, scene_attributes, 2,
        SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, false, true, state->depth_stencil_format,
        true, true, SDL_GPU_COMPAREOP_LESS,
        false, SDL_GPU_COMPAREOP_ALWAYS, SDL_GPU_STENCILOP_KEEP, 0, 0, SDL_GPU_CULLMODE_BACK);
    state->crosshair_pipeline = lesson37_create_pipeline(
        demo, crosshair_vertex_shader, crosshair_fragment_shader, &crosshair_vertex_buffer, crosshair_attributes, 2,
        demo->color_format, true, true, state->depth_stencil_format,
        false, false, SDL_GPU_COMPAREOP_ALWAYS,
        false, SDL_GPU_COMPAREOP_ALWAYS, SDL_GPU_STENCILOP_KEEP, 0, 0, SDL_GPU_CULLMODE_NONE);
    state->outline_write_pipeline = lesson37_create_pipeline(
        demo, scene_vertex_shader, scene_fragment_shader, &scene_vertex_buffer, scene_attributes, 2,
        demo->color_format, false, true, state->depth_stencil_format,
        true, true, SDL_GPU_COMPAREOP_LESS_OR_EQUAL,
        true, SDL_GPU_COMPAREOP_ALWAYS, SDL_GPU_STENCILOP_REPLACE, 0xFF, 0xFF, SDL_GPU_CULLMODE_BACK);
    state->outline_draw_pipeline = lesson37_create_pipeline(
        demo, scene_vertex_shader, outline_fragment_shader, &scene_vertex_buffer, scene_attributes, 2,
        demo->color_format, false, true, state->depth_stencil_format,
        false, false, SDL_GPU_COMPAREOP_ALWAYS,
        true, SDL_GPU_COMPAREOP_NOT_EQUAL, SDL_GPU_STENCILOP_KEEP, 0xFF, 0x00, SDL_GPU_CULLMODE_NONE);

    ok = state->shadow_pipeline && state->scene_pipeline && state->grid_pipeline && state->id_pipeline &&
        state->crosshair_pipeline && state->outline_write_pipeline && state->outline_draw_pipeline;

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
    if (id_vertex_shader) {
        SDL_ReleaseGPUShader(demo->device, id_vertex_shader);
    }
    if (id_fragment_shader) {
        SDL_ReleaseGPUShader(demo->device, id_fragment_shader);
    }
    if (crosshair_vertex_shader) {
        SDL_ReleaseGPUShader(demo->device, crosshair_vertex_shader);
    }
    if (crosshair_fragment_shader) {
        SDL_ReleaseGPUShader(demo->device, crosshair_fragment_shader);
    }
    return ok;
}

static bool lesson37_create_shadow_depth(ForgeGpuDemo *demo, Lesson37State *state)
{
    SDL_GPUTextureCreateInfo texture_info;
    SDL_zero(texture_info);
    texture_info.type = SDL_GPU_TEXTURETYPE_2D;
    texture_info.format = LESSON37_SHADOW_DEPTH_FORMAT;
    texture_info.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    texture_info.width = LESSON37_SHADOW_MAP_SIZE;
    texture_info.height = LESSON37_SHADOW_MAP_SIZE;
    texture_info.layer_count_or_depth = 1;
    texture_info.num_levels = 1;
    texture_info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    state->shadow_depth = SDL_CreateGPUTexture(demo->device, &texture_info);
    return state->shadow_depth != nullptr;
}

static bool lesson37_ensure_targets(ForgeGpuDemo *demo, Lesson37State *state, Uint32 width, Uint32 height)
{
    SDL_GPUTextureCreateInfo texture_info;
    SDL_GPUTexture *main_depth;
    SDL_GPUTexture *id_texture;
    SDL_GPUTexture *id_depth;

    if (width == 0 || height == 0) {
        SDL_SetError("lesson 37 target size is zero");
        return false;
    }
    if (state->main_depth && state->target_width == width && state->target_height == height) {
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
    main_depth = SDL_CreateGPUTexture(demo->device, &texture_info);
    if (!main_depth) {
        return false;
    }

    SDL_zero(texture_info);
    texture_info.type = SDL_GPU_TEXTURETYPE_2D;
    texture_info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    texture_info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    texture_info.width = width;
    texture_info.height = height;
    texture_info.layer_count_or_depth = 1;
    texture_info.num_levels = 1;
    texture_info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    id_texture = SDL_CreateGPUTexture(demo->device, &texture_info);
    if (!id_texture) {
        SDL_ReleaseGPUTexture(demo->device, main_depth);
        return false;
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
    id_depth = SDL_CreateGPUTexture(demo->device, &texture_info);
    if (!id_depth) {
        SDL_ReleaseGPUTexture(demo->device, id_texture);
        SDL_ReleaseGPUTexture(demo->device, main_depth);
        return false;
    }

    if (state->main_depth) {
        SDL_ReleaseGPUTexture(demo->device, state->main_depth);
    }
    if (state->id_texture) {
        SDL_ReleaseGPUTexture(demo->device, state->id_texture);
    }
    if (state->id_depth) {
        SDL_ReleaseGPUTexture(demo->device, state->id_depth);
    }
    state->main_depth = main_depth;
    state->id_texture = id_texture;
    state->id_depth = id_depth;
    state->target_width = width;
    state->target_height = height;
    return true;
}

static Mat4 lesson37_model_matrix(const Lesson37Object *object, float scale_factor)
{
    return mat4_multiply(mat4_translate(object->position), mat4_scale(object->scale * scale_factor));
}

static void lesson37_bind_object(SDL_GPURenderPass *render_pass, const Lesson37State *state, const Lesson37Object *object)
{
    SDL_GPUBufferBinding vertex_binding;
    SDL_GPUBufferBinding index_binding;

    SDL_zero(vertex_binding);
    vertex_binding.buffer = object->shape == LESSON37_SHAPE_CUBE ? state->cube_vb : state->sphere_vb;
    SDL_BindGPUVertexBuffers(render_pass, 0, &vertex_binding, 1);

    SDL_zero(index_binding);
    index_binding.buffer = object->shape == LESSON37_SHAPE_CUBE ? state->cube_ib : state->sphere_ib;
    SDL_BindGPUIndexBuffer(render_pass, &index_binding, SDL_GPU_INDEXELEMENTSIZE_16BIT);
}

static Uint32 lesson37_object_index_count(const Lesson37State *state, const Lesson37Object *object)
{
    return object->shape == LESSON37_SHAPE_CUBE ? state->cube_index_count : state->sphere_index_count;
}

static void lesson37_index_to_color(int index, float color[4])
{
    const int id = index + 1;
    color[0] = (float)((id >> 0) & 0xFF) / 255.0f;
    color[1] = (float)((id >> 8) & 0xFF) / 255.0f;
    color[2] = 0.0f;
    color[3] = 1.0f;
}

static int lesson37_color_to_index(const Uint8 *pixel)
{
    const int id = (int)pixel[0] | ((int)pixel[1] << 8);
    const int index = id - 1;

    if (id == 0 || index < 0 || index >= LESSON37_OBJECT_COUNT) {
        return -1;
    }
    return index;
}

static void lesson37_decode_pending_readback(ForgeGpuDemo *demo, Lesson37State *state)
{
    const Uint8 *pixel;

    if (!state->readback_pending) {
        return;
    }
    if (!SDL_WaitForGPUIdle(demo->device)) {
        SDL_Log("lesson 37 pick readback wait failed: %s", SDL_GetError());
        state->readback_pending = false;
        return;
    }

    pixel = (const Uint8 *)SDL_MapGPUTransferBuffer(demo->device, state->pick_readback, false);
    if (!pixel) {
        SDL_Log("lesson 37 pick readback map failed: %s", SDL_GetError());
        state->readback_pending = false;
        return;
    }
    state->selected_object = lesson37_color_to_index(pixel);
    state->pick_readback_completed = true;
    SDL_UnmapGPUTransferBuffer(demo->device, state->pick_readback);
    state->readback_pending = false;
}

static void lesson37_schedule_pick(Lesson37State *state, Uint32 x, Uint32 y, bool at_center)
{
    if (state->readback_pending) {
        return;
    }
    state->pick_requested = true;
    state->pick_at_center = at_center;
    state->pick_x = x;
    state->pick_y = y;
    state->last_stencil_pick_rejected = false;
}

static bool lesson37_run_shadow_pass(SDL_GPUCommandBuffer *command_buffer, Lesson37State *state)
{
    SDL_GPUDepthStencilTargetInfo depth_target;
    SDL_GPURenderPass *render_pass;

    SDL_zero(depth_target);
    depth_target.texture = state->shadow_depth;
    depth_target.load_op = SDL_GPU_LOADOP_CLEAR;
    depth_target.store_op = SDL_GPU_STOREOP_STORE;
    depth_target.clear_depth = 1.0f;
    depth_target.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
    depth_target.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
    depth_target.cycle = true;
    render_pass = SDL_BeginGPURenderPass(command_buffer, nullptr, 0, &depth_target);
    if (!render_pass) {
        return false;
    }

    SDL_BindGPUGraphicsPipeline(render_pass, state->shadow_pipeline);
    for (int i = 0; i < LESSON37_OBJECT_COUNT; i += 1) {
        const Lesson37Object *object = &state->objects[i];
        Mat4 model = lesson37_model_matrix(object, 1.0f);
        Mat4 light_mvp = mat4_multiply(state->light_vp, model);

        SDL_PushGPUVertexUniformData(command_buffer, 0, &light_mvp, sizeof(light_mvp));
        lesson37_bind_object(render_pass, state, object);
        SDL_DrawGPUIndexedPrimitives(render_pass, lesson37_object_index_count(state, object), 1, 0, 0, 0);
    }

    SDL_EndGPURenderPass(render_pass);
    state->shadow_pass_rendered = true;
    return true;
}

static void lesson37_push_scene_uniforms(
    SDL_GPUCommandBuffer *command_buffer,
    const Lesson37State *state,
    const Lesson37Object *object,
    Mat4 view_projection,
    Vec3 eye_pos,
    float model_scale,
    const float *tint)
{
    Lesson37SceneVertUniforms vertex_uniforms;
    Lesson37SceneFragUniforms fragment_uniforms;
    const Mat4 model = lesson37_model_matrix(object, model_scale);

    vertex_uniforms.mvp = mat4_multiply(view_projection, model);
    vertex_uniforms.model = model;
    vertex_uniforms.light_vp = mat4_multiply(state->light_vp, model);
    SDL_PushGPUVertexUniformData(command_buffer, 0, &vertex_uniforms, sizeof(vertex_uniforms));

    SDL_zero(fragment_uniforms);
    SDL_memcpy(fragment_uniforms.base_color, object->color, sizeof(fragment_uniforms.base_color));
    fragment_uniforms.eye_pos[0] = eye_pos.x;
    fragment_uniforms.eye_pos[1] = eye_pos.y;
    fragment_uniforms.eye_pos[2] = eye_pos.z;
    fragment_uniforms.ambient = 0.12f;
    fragment_uniforms.light_dir[0] = state->light_dir.x;
    fragment_uniforms.light_dir[1] = state->light_dir.y;
    fragment_uniforms.light_dir[2] = state->light_dir.z;
    fragment_uniforms.light_color[0] = 1.0f;
    fragment_uniforms.light_color[1] = 1.0f;
    fragment_uniforms.light_color[2] = 1.0f;
    fragment_uniforms.light_intensity = 1.0f;
    fragment_uniforms.shininess = 64.0f;
    fragment_uniforms.specular_str = 0.4f;
    if (tint) {
        fragment_uniforms.tint[0] = tint[0];
        fragment_uniforms.tint[1] = tint[1];
        fragment_uniforms.tint[2] = tint[2];
    }
    SDL_PushGPUFragmentUniformData(command_buffer, 0, &fragment_uniforms, sizeof(fragment_uniforms));
}

static void lesson37_draw_scene_objects(
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    const Lesson37State *state,
    Mat4 view_projection,
    Vec3 eye_pos)
{
    SDL_GPUTextureSamplerBinding shadow_binding;

    SDL_zero(shadow_binding);
    shadow_binding.texture = state->shadow_depth;
    shadow_binding.sampler = state->nearest_clamp;
    SDL_BindGPUFragmentSamplers(render_pass, 0, &shadow_binding, 1);

    for (int i = 0; i < LESSON37_OBJECT_COUNT; i += 1) {
        const Lesson37Object *object = &state->objects[i];
        lesson37_push_scene_uniforms(command_buffer, state, object, view_projection, eye_pos, 1.0f, nullptr);
        lesson37_bind_object(render_pass, state, object);
        SDL_DrawGPUIndexedPrimitives(render_pass, lesson37_object_index_count(state, object), 1, 0, 0, 0);
    }
}

static void lesson37_draw_grid(
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    const Lesson37State *state,
    Mat4 view_projection,
    Vec3 eye_pos)
{
    Lesson37GridVertUniforms vertex_uniforms;
    Lesson37GridFragUniforms fragment_uniforms;
    SDL_GPUTextureSamplerBinding shadow_binding;
    SDL_GPUBufferBinding vertex_binding;
    SDL_GPUBufferBinding index_binding;

    SDL_BindGPUGraphicsPipeline(render_pass, state->grid_pipeline);

    vertex_uniforms.vp = view_projection;
    vertex_uniforms.light_vp = state->light_vp;
    SDL_PushGPUVertexUniformData(command_buffer, 0, &vertex_uniforms, sizeof(vertex_uniforms));

    SDL_zero(fragment_uniforms);
    fragment_uniforms.line_color[0] = 0.35f;
    fragment_uniforms.line_color[1] = 0.35f;
    fragment_uniforms.line_color[2] = 0.40f;
    fragment_uniforms.line_color[3] = 1.0f;
    fragment_uniforms.bg_color[0] = 0.08f;
    fragment_uniforms.bg_color[1] = 0.08f;
    fragment_uniforms.bg_color[2] = 0.10f;
    fragment_uniforms.bg_color[3] = 1.0f;
    fragment_uniforms.light_dir[0] = state->light_dir.x;
    fragment_uniforms.light_dir[1] = state->light_dir.y;
    fragment_uniforms.light_dir[2] = state->light_dir.z;
    fragment_uniforms.light_intensity = 1.0f;
    fragment_uniforms.eye_pos[0] = eye_pos.x;
    fragment_uniforms.eye_pos[1] = eye_pos.y;
    fragment_uniforms.eye_pos[2] = eye_pos.z;
    fragment_uniforms.grid_spacing = 1.0f;
    fragment_uniforms.line_width = 0.02f;
    fragment_uniforms.fade_distance = 40.0f;
    fragment_uniforms.ambient = 0.15f;
    fragment_uniforms.tint_color[0] = 1.0f;
    fragment_uniforms.tint_color[1] = 1.0f;
    fragment_uniforms.tint_color[2] = 1.0f;
    fragment_uniforms.tint_color[3] = 1.0f;
    SDL_PushGPUFragmentUniformData(command_buffer, 0, &fragment_uniforms, sizeof(fragment_uniforms));

    SDL_zero(shadow_binding);
    shadow_binding.texture = state->shadow_depth;
    shadow_binding.sampler = state->nearest_clamp;
    SDL_BindGPUFragmentSamplers(render_pass, 0, &shadow_binding, 1);

    SDL_zero(vertex_binding);
    vertex_binding.buffer = state->grid_vb;
    SDL_BindGPUVertexBuffers(render_pass, 0, &vertex_binding, 1);
    SDL_zero(index_binding);
    index_binding.buffer = state->grid_ib;
    SDL_BindGPUIndexBuffer(render_pass, &index_binding, SDL_GPU_INDEXELEMENTSIZE_16BIT);
    SDL_DrawGPUIndexedPrimitives(render_pass, LESSON37_GRID_INDEX_COUNT, 1, 0, 0, 0);
}

static void lesson37_draw_crosshair(SDL_GPURenderPass *render_pass, const Lesson37State *state)
{
    SDL_GPUBufferBinding vertex_binding;
    SDL_GPUBufferBinding index_binding;

    SDL_BindGPUGraphicsPipeline(render_pass, state->crosshair_pipeline);
    SDL_zero(vertex_binding);
    vertex_binding.buffer = state->crosshair_vb;
    SDL_BindGPUVertexBuffers(render_pass, 0, &vertex_binding, 1);
    SDL_zero(index_binding);
    index_binding.buffer = state->crosshair_ib;
    SDL_BindGPUIndexBuffer(render_pass, &index_binding, SDL_GPU_INDEXELEMENTSIZE_16BIT);
    SDL_DrawGPUIndexedPrimitives(render_pass, LESSON37_CROSSHAIR_INDICES, 1, 0, 0, 0);
}

static bool lesson37_run_scene_pass(
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPUTexture *swapchain_texture,
    Lesson37State *state,
    Mat4 view_projection,
    Vec3 eye_pos)
{
    SDL_GPUColorTargetInfo color_target;
    SDL_GPUDepthStencilTargetInfo depth_target;
    SDL_GPURenderPass *render_pass;

    SDL_zero(color_target);
    color_target.texture = swapchain_texture;
    color_target.load_op = SDL_GPU_LOADOP_CLEAR;
    color_target.store_op = SDL_GPU_STOREOP_STORE;
    color_target.clear_color.r = 0.05f;
    color_target.clear_color.g = 0.05f;
    color_target.clear_color.b = 0.08f;
    color_target.clear_color.a = 1.0f;

    SDL_zero(depth_target);
    depth_target.texture = state->main_depth;
    depth_target.load_op = SDL_GPU_LOADOP_CLEAR;
    depth_target.store_op = SDL_GPU_STOREOP_STORE;
    depth_target.clear_depth = 1.0f;
    depth_target.stencil_load_op = SDL_GPU_LOADOP_CLEAR;
    depth_target.stencil_store_op = SDL_GPU_STOREOP_STORE;
    depth_target.clear_stencil = 0;
    depth_target.cycle = true;

    render_pass = SDL_BeginGPURenderPass(command_buffer, &color_target, 1, &depth_target);
    if (!render_pass) {
        return false;
    }
    SDL_BindGPUGraphicsPipeline(render_pass, state->scene_pipeline);
    lesson37_draw_scene_objects(command_buffer, render_pass, state, view_projection, eye_pos);
    lesson37_draw_grid(command_buffer, render_pass, state, view_projection, eye_pos);
    lesson37_draw_crosshair(render_pass, state);
    SDL_EndGPURenderPass(render_pass);
    state->scene_pass_rendered = true;
    return true;
}

static bool lesson37_run_id_pass(
    SDL_GPUCommandBuffer *command_buffer,
    Lesson37State *state,
    Mat4 view_projection,
    Uint32 width,
    Uint32 height)
{
    SDL_GPUColorTargetInfo color_target;
    SDL_GPUDepthStencilTargetInfo depth_target;
    SDL_GPURenderPass *render_pass;
    SDL_GPUCopyPass *copy_pass;
    SDL_GPUTextureRegion source;
    SDL_GPUTextureTransferInfo destination;

    SDL_zero(color_target);
    color_target.texture = state->id_texture;
    color_target.load_op = SDL_GPU_LOADOP_CLEAR;
    color_target.store_op = SDL_GPU_STOREOP_STORE;
    color_target.clear_color.r = 0.0f;
    color_target.clear_color.g = 0.0f;
    color_target.clear_color.b = 0.0f;
    color_target.clear_color.a = 1.0f;

    SDL_zero(depth_target);
    depth_target.texture = state->id_depth;
    depth_target.load_op = SDL_GPU_LOADOP_CLEAR;
    depth_target.store_op = SDL_GPU_STOREOP_STORE;
    depth_target.clear_depth = 1.0f;
    depth_target.stencil_load_op = SDL_GPU_LOADOP_CLEAR;
    depth_target.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
    depth_target.clear_stencil = 0;
    depth_target.cycle = true;

    render_pass = SDL_BeginGPURenderPass(command_buffer, &color_target, 1, &depth_target);
    if (!render_pass) {
        return false;
    }
    SDL_BindGPUGraphicsPipeline(render_pass, state->id_pipeline);
    for (int i = 0; i < LESSON37_OBJECT_COUNT; i += 1) {
        const Lesson37Object *object = &state->objects[i];
        Lesson37IdVertUniforms vertex_uniforms;
        Lesson37IdFragUniforms fragment_uniforms;

        vertex_uniforms.mvp = mat4_multiply(view_projection, lesson37_model_matrix(object, 1.0f));
        SDL_PushGPUVertexUniformData(command_buffer, 0, &vertex_uniforms, sizeof(vertex_uniforms));
        lesson37_index_to_color(i, fragment_uniforms.id_color);
        SDL_PushGPUFragmentUniformData(command_buffer, 0, &fragment_uniforms, sizeof(fragment_uniforms));
        lesson37_bind_object(render_pass, state, object);
        SDL_DrawGPUIndexedPrimitives(render_pass, lesson37_object_index_count(state, object), 1, 0, 0, 0);
    }
    SDL_EndGPURenderPass(render_pass);

    copy_pass = SDL_BeginGPUCopyPass(command_buffer);
    if (!copy_pass) {
        return false;
    }
    SDL_zero(source);
    source.texture = state->id_texture;
    source.x = SDL_min(state->pick_x, width - 1);
    source.y = SDL_min(state->pick_y, height - 1);
    source.w = 1;
    source.h = 1;
    source.d = 1;
    SDL_zero(destination);
    destination.transfer_buffer = state->pick_readback;
    destination.pixels_per_row = 1;
    destination.rows_per_layer = 1;
    SDL_DownloadFromGPUTexture(copy_pass, &source, &destination);
    SDL_EndGPUCopyPass(copy_pass);

    state->color_id_pass_rendered = true;
    state->readback_pending = true;
    return true;
}

static bool lesson37_run_outline_pass(
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPUTexture *swapchain_texture,
    Lesson37State *state,
    Mat4 view_projection,
    Vec3 eye_pos)
{
    const int selected = state->selected_object;
    const Lesson37Object *object;
    SDL_GPUColorTargetInfo color_target;
    SDL_GPUDepthStencilTargetInfo depth_target;
    SDL_GPURenderPass *render_pass;
    if (selected < 0 || selected >= LESSON37_OBJECT_COUNT) {
        return true;
    }
    object = &state->objects[selected];

    SDL_zero(color_target);
    color_target.texture = swapchain_texture;
    color_target.load_op = SDL_GPU_LOADOP_LOAD;
    color_target.store_op = SDL_GPU_STOREOP_STORE;
    SDL_zero(depth_target);
    depth_target.texture = state->main_depth;
    depth_target.load_op = SDL_GPU_LOADOP_LOAD;
    depth_target.store_op = SDL_GPU_STOREOP_STORE;
    depth_target.stencil_load_op = SDL_GPU_LOADOP_LOAD;
    depth_target.stencil_store_op = SDL_GPU_STOREOP_STORE;
    render_pass = SDL_BeginGPURenderPass(command_buffer, &color_target, 1, &depth_target);
    if (!render_pass) {
        return false;
    }

    SDL_BindGPUGraphicsPipeline(render_pass, state->outline_write_pipeline);
    SDL_SetGPUStencilReference(render_pass, LESSON37_STENCIL_OUTLINE);
    lesson37_push_scene_uniforms(command_buffer, state, object, view_projection, eye_pos, 1.0f, nullptr);
    {
        SDL_GPUTextureSamplerBinding shadow_binding;
        SDL_zero(shadow_binding);
        shadow_binding.texture = state->shadow_depth;
        shadow_binding.sampler = state->nearest_clamp;
        SDL_BindGPUFragmentSamplers(render_pass, 0, &shadow_binding, 1);
    }
    lesson37_bind_object(render_pass, state, object);
    SDL_DrawGPUIndexedPrimitives(render_pass, lesson37_object_index_count(state, object), 1, 0, 0, 0);

    SDL_BindGPUGraphicsPipeline(render_pass, state->outline_draw_pipeline);
    SDL_SetGPUStencilReference(render_pass, LESSON37_STENCIL_OUTLINE);
    {
        Lesson37SceneVertUniforms vertex_uniforms;
        Lesson37OutlineFragUniforms fragment_uniforms;
        const Mat4 model = lesson37_model_matrix(object, LESSON37_OUTLINE_SCALE);

        vertex_uniforms.mvp = mat4_multiply(view_projection, model);
        vertex_uniforms.model = model;
        vertex_uniforms.light_vp = mat4_multiply(state->light_vp, model);
        SDL_PushGPUVertexUniformData(command_buffer, 0, &vertex_uniforms, sizeof(vertex_uniforms));
        fragment_uniforms.outline_color[0] = 1.0f;
        fragment_uniforms.outline_color[1] = 0.85f;
        fragment_uniforms.outline_color[2] = 0.0f;
        fragment_uniforms.outline_color[3] = 1.0f;
        SDL_PushGPUFragmentUniformData(command_buffer, 0, &fragment_uniforms, sizeof(fragment_uniforms));
    }
    lesson37_bind_object(render_pass, state, object);
    SDL_DrawGPUIndexedPrimitives(render_pass, lesson37_object_index_count(state, object), 1, 0, 0, 0);
    SDL_EndGPURenderPass(render_pass);
    state->outline_pass_rendered = true;
    return true;
}

static void lesson37_init_objects(Lesson37State *state)
{
#define LESSON37_OBJ(i, nm, sh, px, py, pz, sc, cr, cg, cb) \
    do { \
        state->objects[i].name = nm; \
        state->objects[i].shape = sh; \
        state->objects[i].position = { px, py, pz }; \
        state->objects[i].scale = sc; \
        state->objects[i].color[0] = cr; \
        state->objects[i].color[1] = cg; \
        state->objects[i].color[2] = cb; \
        state->objects[i].color[3] = 1.0f; \
    } while (0)
    LESSON37_OBJ(0, "Red Cube", LESSON37_SHAPE_CUBE, -3.0f, 0.5f, -2.0f, 1.0f, 0.85f, 0.20f, 0.15f);
    LESSON37_OBJ(1, "Blue Cube", LESSON37_SHAPE_CUBE, 2.0f, 0.5f, -1.0f, 0.8f, 0.15f, 0.40f, 0.85f);
    LESSON37_OBJ(2, "Green Cube", LESSON37_SHAPE_CUBE, 0.0f, 0.75f, -4.0f, 1.5f, 0.20f, 0.75f, 0.25f);
    LESSON37_OBJ(3, "Yellow Cube", LESSON37_SHAPE_CUBE, -1.5f, 0.5f, -5.0f, 0.7f, 0.85f, 0.80f, 0.15f);
    LESSON37_OBJ(4, "Purple Sphere", LESSON37_SHAPE_SPHERE, 3.0f, 0.6f, -3.0f, 0.6f, 0.60f, 0.25f, 0.75f);
    LESSON37_OBJ(5, "Orange Sphere", LESSON37_SHAPE_SPHERE, -2.0f, 0.4f, -3.5f, 0.4f, 0.90f, 0.50f, 0.15f);
    LESSON37_OBJ(6, "Cyan Cube", LESSON37_SHAPE_CUBE, 1.0f, 1.0f, -2.5f, 0.6f, 0.15f, 0.80f, 0.80f);
    LESSON37_OBJ(7, "Pink Sphere", LESSON37_SHAPE_SPHERE, -0.5f, 0.5f, -1.5f, 0.5f, 0.90f, 0.40f, 0.60f);
    LESSON37_OBJ(8, "White Cube", LESSON37_SHAPE_CUBE, 2.5f, 0.4f, -5.0f, 0.8f, 0.85f, 0.85f, 0.85f);
    LESSON37_OBJ(9, "Teal Sphere", LESSON37_SHAPE_SPHERE, -3.0f, 0.6f, -4.5f, 0.6f, 0.20f, 0.70f, 0.65f);
#undef LESSON37_OBJ
}

bool ForgeGpuCreateLesson37(ForgeGpuDemo *demo)
{
    Lesson37State *state;
    SDL_GPUSamplerCreateInfo sampler_info;
    SDL_GPUTransferBufferCreateInfo transfer_info;

    state = (Lesson37State *)SDL_calloc(1, sizeof(*state));
    if (!state) {
        SDL_SetError("failed to allocate lesson 37 state");
        return false;
    }
    demo->lesson.private_state = state;
    lesson37_init_camera(demo);

    state->depth_stencil_format = lesson37_select_depth_stencil_format(demo->device);
    if (state->depth_stencil_format == SDL_GPU_TEXTUREFORMAT_INVALID) {
        SDL_SetError("lesson 37 requires a depth-stencil target format");
        return false;
    }
    if (!SDL_GPUTextureSupportsFormat(demo->device, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET)) {
        SDL_SetError("lesson 37 requires R8G8B8A8_UNORM color targets");
        return false;
    }

    SDL_zero(sampler_info);
    sampler_info.min_filter = SDL_GPU_FILTER_NEAREST;
    sampler_info.mag_filter = SDL_GPU_FILTER_NEAREST;
    sampler_info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    sampler_info.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler_info.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler_info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    state->nearest_clamp = SDL_CreateGPUSampler(demo->device, &sampler_info);
    if (!state->nearest_clamp) {
        return false;
    }

    SDL_zero(transfer_info);
    transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    transfer_info.size = 4;
    state->pick_readback = SDL_CreateGPUTransferBuffer(demo->device, &transfer_info);
    if (!state->pick_readback) {
        return false;
    }

    state->light_dir = vec3_normalize({ -0.4f, -0.8f, -0.3f });
    {
        const Vec3 light_pos = vec3_scale(state->light_dir, -30.0f);
        const Mat4 light_view = mat4_look_at(light_pos, { 0.0f, 0.0f, -3.0f }, { 0.0f, 1.0f, 0.0f });
        const Mat4 light_proj = mat4_orthographic(-12.0f, 12.0f, -12.0f, 12.0f, 0.1f, 60.0f);
        state->light_vp = mat4_multiply(light_proj, light_view);
    }
    state->selected_object = -1;
    state->pick_method = LESSON37_PICK_COLOR_ID;
    lesson37_init_objects(state);

    if (!lesson37_create_shadow_depth(demo, state) || !lesson37_create_geometry(demo, state) || !lesson37_create_pipelines(demo)) {
        return false;
    }
    return true;
}

bool ForgeGpuRenderLesson37(ForgeGpuDemo *demo, SDL_GPUCommandBuffer *command_buffer, SDL_GPUTexture *swapchain_texture, Uint32 width, Uint32 height)
{
    Lesson37State *state = lesson37_state(demo);
    Mat4 view;
    Mat4 projection;
    Mat4 view_projection;

    if (!state) {
        SDL_SetError("lesson 37 state is missing");
        return false;
    }

    lesson37_decode_pending_readback(demo, state);
    if (!lesson37_ensure_targets(demo, state, width, height)) {
        return false;
    }

    ForgeGpuUpdateCameraFromInput(demo);
    ForgeGpuCameraViewProjection(demo, width, height, LESSON37_FAR_PLANE, &view, &projection);
    view_projection = mat4_multiply(projection, view);

    state->shadow_pass_rendered = false;
    state->scene_pass_rendered = false;
    state->color_id_pass_rendered = false;
    state->outline_pass_rendered = false;

    if (demo->validation_mode && !state->validation_pick_requested && !state->pick_readback_completed) {
        lesson37_schedule_pick(state, (width * 42u) / 100u, (height * 47u) / 100u, false);
        state->validation_pick_requested = true;
    }
    if (state->pick_requested && state->pick_at_center) {
        state->pick_x = width / 2u;
        state->pick_y = height / 2u;
    }

    if (!lesson37_run_shadow_pass(command_buffer, state) ||
        !lesson37_run_scene_pass(command_buffer, swapchain_texture, state, view_projection, demo->lesson.camera_position)) {
        return false;
    }

    if (state->pick_requested) {
        if (state->pick_method == LESSON37_PICK_COLOR_ID) {
            if (!lesson37_run_id_pass(command_buffer, state, view_projection, width, height)) {
                return false;
            }
        } else {
            state->last_stencil_pick_rejected = true;
            state->stencil_rejected_count += 1;
        }
        state->pick_requested = false;
    }

    return lesson37_run_outline_pass(command_buffer, swapchain_texture, state, view_projection, demo->lesson.camera_position);
}

void ForgeGpuDebugLesson37(ForgeGpuDemo *demo)
{
    Lesson37State *state = lesson37_state(demo);
    static const char *const method_names[] = {
        "Color-ID",
        "Stencil-ID unsupported"
    };

    if (!state) {
        return;
    }

    ImGui::Text("Pick method: %s", method_names[(int)state->pick_method]);
    ImGui::Text("Selected: %s", state->selected_object >= 0 ? state->objects[state->selected_object].name : "none");
    ImGui::Text("Depth-stencil: %s", lesson37_format_name(state->depth_stencil_format));
    ImGui::Text("Readback: %s", state->pick_readback_completed ? "completed" : "pending/none");
    if (state->pick_method == LESSON37_PICK_STENCIL_ID_UNSUPPORTED) {
        ImGui::TextUnformatted("Stencil-ID readback is gated: SDL_GPU/WebGPU cannot portably download a stencil byte from combined depth-stencil targets.");
    }
    ImGui::Text("Passes: shadow %d, scene %d, color-id %d, outline %d",
        state->shadow_pass_rendered ? 1 : 0,
        state->scene_pass_rendered ? 1 : 0,
        state->color_id_pass_rendered ? 1 : 0,
        state->outline_pass_rendered ? 1 : 0);
}

void ForgeGpuControlsLesson37(ForgeGpuDemo *demo)
{
    (void)demo;
    ImGui::TextUnformatted("Left click: pick object");
    ImGui::TextUnformatted("Captured mouse: pick at crosshair");
    ImGui::TextUnformatted("Tab: toggle color-ID/stencil-ID mode");
}

bool ForgeGpuHandleLesson37Event(ForgeGpuDemo *demo, const SDL_Event *event)
{
    Lesson37State *state = lesson37_state(demo);

    if (!state) {
        return false;
    }
    if (event->type == SDL_EVENT_KEY_DOWN && !event->key.repeat && event->key.key == SDLK_TAB) {
        state->pick_method = state->pick_method == LESSON37_PICK_COLOR_ID ?
            LESSON37_PICK_STENCIL_ID_UNSUPPORTED : LESSON37_PICK_COLOR_ID;
        return true;
    }
    if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN && event->button.button == SDL_BUTTON_LEFT) {
        if (demo->lesson.mouse_captured) {
            lesson37_schedule_pick(state, 0, 0, true);
            return true;
        }
        lesson37_schedule_pick(state, (Uint32)SDL_max(0.0f, event->button.x), (Uint32)SDL_max(0.0f, event->button.y), false);
        return false;
    }
    return false;
}

void ForgeGpuExportLesson37Metrics(ForgeGpuDemo *demo)
{
    Lesson37State *state = lesson37_state(demo);

    if (!state) {
        return;
    }

    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson37PickMethod", (double)state->pick_method);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson37SelectedObject", (double)state->selected_object);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson37ShadowPass", state->shadow_pass_rendered ? 1.0 : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson37ScenePass", state->scene_pass_rendered ? 1.0 : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson37ColorIdPass", state->color_id_pass_rendered ? 1.0 : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson37OutlinePass", state->outline_pass_rendered ? 1.0 : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson37PickReadback", state->pick_readback_completed ? 1.0 : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson37StencilIdSupported", 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson37LastStencilPickRejected", state->last_stencil_pick_rejected ? 1.0 : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson37StencilRejectedCount", (double)state->stencil_rejected_count);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson37UsesD24S8", state->depth_stencil_format == SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT ? 1.0 : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson37UsesD32S8", state->depth_stencil_format == SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT ? 1.0 : 0.0);
}

void ForgeGpuDestroyLesson37(ForgeGpuDemo *demo)
{
    Lesson37State *state = lesson37_state(demo);

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
    if (state->id_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, state->id_pipeline);
    }
    if (state->crosshair_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, state->crosshair_pipeline);
    }
    if (state->outline_write_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, state->outline_write_pipeline);
    }
    if (state->outline_draw_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, state->outline_draw_pipeline);
    }
    if (state->shadow_depth) {
        SDL_ReleaseGPUTexture(demo->device, state->shadow_depth);
    }
    if (state->main_depth) {
        SDL_ReleaseGPUTexture(demo->device, state->main_depth);
    }
    if (state->id_texture) {
        SDL_ReleaseGPUTexture(demo->device, state->id_texture);
    }
    if (state->id_depth) {
        SDL_ReleaseGPUTexture(demo->device, state->id_depth);
    }
    if (state->nearest_clamp) {
        SDL_ReleaseGPUSampler(demo->device, state->nearest_clamp);
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
    if (state->crosshair_vb) {
        SDL_ReleaseGPUBuffer(demo->device, state->crosshair_vb);
    }
    if (state->crosshair_ib) {
        SDL_ReleaseGPUBuffer(demo->device, state->crosshair_ib);
    }
    if (state->pick_readback) {
        SDL_ReleaseGPUTransferBuffer(demo->device, state->pick_readback);
    }
    SDL_free(state);
    demo->lesson.private_state = nullptr;
}
