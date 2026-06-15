#include "forge_gpu_lessons.h"

#include "forge_gpu_browser_status.h"
#include "forge_gpu_camera.h"
#include "forge_gpu_forward_scene.h"
#include "forge_gpu_gpu_helpers.h"
#include "forge_gpu_lesson_common.h"
#include "forge_gpu_math.h"
#include "forge_gpu_scene.h"
#include "forge_gpu_shader_layouts.h"
#include "shaders/generated/forge_gpu_lesson_35_shaders.h"
#include "imgui.h"

#include <stddef.h>

#define LESSON35_SHADOW_MAP_SIZE 2048u
#define LESSON35_FOV_DEGREES 60.0f
#define LESSON35_NEAR_PLANE 0.1f
#define LESSON35_FAR_PLANE 200.0f
#define LESSON35_MOVE_SPEED 5.0f
#define LESSON35_MOUSE_SENSITIVITY 0.003f
#define LESSON35_PITCH_CLAMP 1.5f
#define LESSON35_GRID_HALF_SIZE 50.0f
#define LESSON35_CLEAR_R 0.05f
#define LESSON35_CLEAR_G 0.05f
#define LESSON35_CLEAR_B 0.08f
#define LESSON35_BASE_COLOR_GREY 0.6f
#define LESSON35_AMBIENT_SCENE 0.12f
#define LESSON35_AMBIENT_GRID 0.15f
#define LESSON35_SHININESS 64.0f
#define LESSON35_SPECULAR_STR 0.4f
#define LESSON35_GRID_LINE_COLOR_R 0.4f
#define LESSON35_GRID_LINE_COLOR_G 0.4f
#define LESSON35_GRID_LINE_COLOR_B 0.5f
#define LESSON35_GRID_BG_COLOR_R 0.08f
#define LESSON35_GRID_BG_COLOR_G 0.08f
#define LESSON35_GRID_BG_COLOR_B 0.12f
#define LESSON35_GRID_SPACING 1.0f
#define LESSON35_GRID_LINE_WIDTH 0.02f
#define LESSON35_GRID_FADE_DIST 40.0f
#define LESSON35_LIGHT_ORTHO_SIZE 15.0f
#define LESSON35_LIGHT_ORTHO_NEAR 0.1f
#define LESSON35_LIGHT_ORTHO_FAR 60.0f
#define LESSON35_LIGHT_DISTANCE 30.0f
#define LESSON35_DECAL_SURFACE_DIST 0.9f
#define LESSON35_DECAL_SIZE_MIN 0.15f
#define LESSON35_DECAL_SIZE_RANGE 0.35f
#define LESSON35_DECAL_DEPTH_RATIO 0.6f
#define LESSON35_CAM_START_X 0.0f
#define LESSON35_CAM_START_Y 7.0f
#define LESSON35_CAM_START_Z 14.0f
#define LESSON35_CAM_START_YAW 0.0f
#define LESSON35_CAM_START_PITCH -0.4f
#define LESSON35_MAX_DECALS 120
#define LESSON35_DECAL_TEX_SIZE 128u
#define LESSON35_NUM_DECAL_SHAPES 8
#define LESSON35_DECAL_SEED 0xDECA1u
#define LESSON35_SUZANNE_COUNT 6
#define LESSON35_DECALS_PER_OBJECT 12
#define LESSON35_CUBE_INDEX_COUNT 36u
#define LESSON35_RING_RADIUS 5.0f
#define LESSON35_FLOOR_DECAL_COUNT 15
#define LESSON35_FLOOR_DECAL_OFFSET 0.05f
#define LESSON35_FLOOR_SIZE_BASE 0.4f
#define LESSON35_FLOOR_SIZE_RANGE 0.8f
#define LESSON35_FLOOR_SCATTER 7.0f

struct Lesson35Vertex
{
    float position[3];
    float normal[3];
};

struct Lesson35SceneVertUniforms
{
    Mat4 mvp;
    Mat4 model;
    Mat4 light_vp;
};

struct Lesson35SceneFragUniforms
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

struct Lesson35DecalVertUniforms
{
    Mat4 mvp;
};

struct Lesson35DecalFragUniforms
{
    Mat4 inv_vp;
    Mat4 inv_decal_model;
    Mat4 light_vp;
    float screen_size[2];
    float near_plane;
    float far_plane;
    float decal_tint[4];
    float eye_pos[3];
    float ambient;
    float light_dir[3];
    float light_intensity;
    float light_color[3];
    float shininess;
    float specular_str;
    float pad[3];
};

struct Lesson35SceneObject
{
    Vec3 position;
    float scale;
    float rotation_y;
};

struct Lesson35Decal
{
    Vec3 position;
    Quat orientation;
    float size[3];
    int tex_index;
    float tint[4];
};

struct Lesson35State
{
    SDL_GPUGraphicsPipeline *shadow_pipeline;
    SDL_GPUGraphicsPipeline *scene_pipeline;
    SDL_GPUGraphicsPipeline *grid_pipeline;
    SDL_GPUGraphicsPipeline *decal_pipeline;
    GpuSceneData suzanne;
    SDL_GPUBuffer *cube_vertex_buffer;
    SDL_GPUBuffer *cube_index_buffer;
    SDL_GPUBuffer *grid_vertex_buffer;
    SDL_GPUBuffer *grid_index_buffer;
    SDL_GPUTexture *shadow_depth;
    SDL_GPUTexture *scene_depth;
    SDL_GPUTexture *scene_normal;
    SDL_GPUTexture *decal_textures[LESSON35_NUM_DECAL_SHAPES];
    SDL_GPUSampler *nearest_clamp;
    SDL_GPUSampler *linear_clamp;
    Uint32 scene_depth_width;
    Uint32 scene_depth_height;
    Uint32 scene_normal_width;
    Uint32 scene_normal_height;
    SDL_GPUTextureFormat scene_depth_format;
    SDL_GPUTextureFormat shadow_depth_format;
    Lesson35SceneObject suzannes[LESSON35_SUZANNE_COUNT];
    Lesson35Decal decals[LESSON35_MAX_DECALS];
    int decal_count;
    Vec3 light_dir;
    Mat4 light_vp;
    bool shadow_pass_rendered;
    bool scene_pass_rendered;
    bool decal_pass_rendered;
};

static_assert(sizeof(Lesson35Vertex) == 24, "lesson 35 vertex size must match HLSL layout");
static_assert(sizeof(Lesson35SceneVertUniforms) == 192, "lesson 35 scene vertex uniform size must match HLSL layout");
static_assert(sizeof(Lesson35SceneFragUniforms) == 80, "lesson 35 scene fragment uniform size must match HLSL layout");
static_assert(sizeof(Lesson35DecalVertUniforms) == 64, "lesson 35 decal vertex uniform size must match HLSL layout");
static_assert(sizeof(Lesson35DecalFragUniforms) == 288, "lesson 35 decal fragment uniform size must match strict cbuffer layout");

static const float kLesson35DecalColors[][4] = {
    { 0.31f, 0.76f, 0.97f, 1.0f },
    { 1.00f, 0.44f, 0.26f, 1.0f },
    { 0.40f, 0.73f, 0.42f, 1.0f },
    { 0.67f, 0.28f, 0.74f, 1.0f },
    { 1.00f, 0.84f, 0.31f, 1.0f },
};

static Lesson35State *lesson35_state(ForgeGpuDemo *demo)
{
    return (Lesson35State *)demo->lesson.private_state;
}

static bool lesson35_format_has_stencil(SDL_GPUTextureFormat format)
{
    return format == SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT ||
           format == SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT;
}

static const char *lesson35_format_name(SDL_GPUTextureFormat format)
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

static SDL_GPUTextureFormat lesson35_select_shadow_depth_format(SDL_GPUDevice *device)
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

static SDL_GPUTextureFormat lesson35_select_scene_depth_format(SDL_GPUDevice *device)
{
    const SDL_GPUTextureUsageFlags usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;

    if (SDL_GPUTextureSupportsFormat(device, SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT, SDL_GPU_TEXTURETYPE_2D, usage)) {
        return SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT;
    }
    if (SDL_GPUTextureSupportsFormat(device, SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT, SDL_GPU_TEXTURETYPE_2D, usage)) {
        return SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT;
    }
    if (SDL_GPUTextureSupportsFormat(device, SDL_GPU_TEXTUREFORMAT_D32_FLOAT, SDL_GPU_TEXTURETYPE_2D, usage)) {
        return SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    }
    return SDL_GPU_TEXTUREFORMAT_INVALID;
}

static void lesson35_init_camera(ForgeGpuDemo *demo)
{
    demo->lesson.camera_position = { LESSON35_CAM_START_X, LESSON35_CAM_START_Y, LESSON35_CAM_START_Z };
    demo->lesson.camera_yaw = LESSON35_CAM_START_YAW;
    demo->lesson.camera_pitch = LESSON35_CAM_START_PITCH;
    demo->lesson.pitch_clamp = LESSON35_PITCH_CLAMP;
    demo->lesson.mouse_sensitivity = LESSON35_MOUSE_SENSITIVITY;
    demo->lesson.move_speed = LESSON35_MOVE_SPEED;
    demo->lesson.last_ticks = SDL_GetTicks();
}

static float lesson35_smoothstep(float edge0, float edge1, float x)
{
    float t = (x - edge0) / (edge1 - edge0);

    if (t < 0.0f) {
        t = 0.0f;
    } else if (t > 1.0f) {
        t = 1.0f;
    }
    return t * t * (3.0f - 2.0f * t);
}

static void lesson35_generate_cube(
    Lesson35Vertex vertices[24],
    Uint32 *vertex_count,
    Uint16 indices[36],
    Uint32 *index_count)
{
    const float h = 0.5f;
    Uint32 v = 0;
    Uint32 idx = 0;
    const float faces[6][4][3] = {
        { { -h, -h, h }, { h, -h, h }, { h, h, h }, { -h, h, h } },
        { { h, -h, -h }, { -h, -h, -h }, { -h, h, -h }, { h, h, -h } },
        { { h, -h, h }, { h, -h, -h }, { h, h, -h }, { h, h, h } },
        { { -h, -h, -h }, { -h, -h, h }, { -h, h, h }, { -h, h, -h } },
        { { -h, h, h }, { h, h, h }, { h, h, -h }, { -h, h, -h } },
        { { -h, -h, -h }, { h, -h, -h }, { h, -h, h }, { -h, -h, h } },
    };
    const float normals[6][3] = {
        { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, -1.0f }, { 1.0f, 0.0f, 0.0f },
        { -1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, -1.0f, 0.0f },
    };

    for (int face = 0; face < 6; face += 1) {
        for (int corner = 0; corner < 4; corner += 1) {
            vertices[v].position[0] = faces[face][corner][0];
            vertices[v].position[1] = faces[face][corner][1];
            vertices[v].position[2] = faces[face][corner][2];
            vertices[v].normal[0] = normals[face][0];
            vertices[v].normal[1] = normals[face][1];
            vertices[v].normal[2] = normals[face][2];
            v += 1;
        }
        const Uint16 base = (Uint16)(face * 4);
        indices[idx++] = (Uint16)(base + 0);
        indices[idx++] = (Uint16)(base + 1);
        indices[idx++] = (Uint16)(base + 2);
        indices[idx++] = (Uint16)(base + 0);
        indices[idx++] = (Uint16)(base + 2);
        indices[idx++] = (Uint16)(base + 3);
    }

    *vertex_count = v;
    *index_count = idx;
}

static void lesson35_set_pixel(Uint8 *pixels, int x, int y, int w, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
{
    const int index = (y * w + x) * 4;

    pixels[index + 0] = r;
    pixels[index + 1] = g;
    pixels[index + 2] = b;
    pixels[index + 3] = a;
}

static void lesson35_generate_decal_texture(int shape, Uint8 *pixels)
{
    const int size = (int)LESSON35_DECAL_TEX_SIZE;
    const float inv_size = 1.0f / (float)size;

    SDL_memset(pixels, 0, (size_t)size * (size_t)size * 4u);
    for (int y = 0; y < size; y += 1) {
        for (int x = 0; x < size; x += 1) {
            const float nx = ((float)x + 0.5f) * inv_size * 2.0f - 1.0f;
            const float ny = ((float)y + 0.5f) * inv_size * 2.0f - 1.0f;
            float dist = 0.0f;
            float alpha = 0.0f;

            switch (shape) {
            case 0:
                dist = SDL_sqrtf(nx * nx + ny * ny);
                alpha = 1.0f - lesson35_smoothstep(0.7f, 0.8f, dist);
                break;
            case 1: {
                const float px = SDL_fabsf(nx) * 1.1f;
                const float py = -ny * 1.1f + 0.3f;
                const float heart = px * px + py * py;
                const float h_val = heart - px * SDL_sqrtf(SDL_fabsf(py));
                alpha = 1.0f - lesson35_smoothstep(-0.05f, 0.05f, h_val - 0.5f);
                break;
            }
            case 2: {
                const float angle = SDL_atan2f(ny, nx);
                const float r = SDL_sqrtf(nx * nx + ny * ny);
                const float star_r = 0.3f + 0.5f * SDL_fabsf(SDL_cosf(angle * 2.5f));
                alpha = 1.0f - lesson35_smoothstep(star_r - 0.05f, star_r + 0.05f, r);
                break;
            }
            case 3: {
                const int cx = (int)((nx * 0.5f + 0.5f) * 4.0f);
                const int cy = (int)((ny * 0.5f + 0.5f) * 4.0f);
                const float check = ((cx + cy) % 2 == 0) ? 1.0f : 0.0f;
                dist = SDL_sqrtf(nx * nx + ny * ny);
                alpha = check * (1.0f - lesson35_smoothstep(0.75f, 0.85f, dist));
                break;
            }
            case 4: {
                dist = SDL_sqrtf(nx * nx + ny * ny);
                const float inner = 1.0f - lesson35_smoothstep(0.45f, 0.55f, dist);
                const float outer = 1.0f - lesson35_smoothstep(0.7f, 0.8f, dist);
                alpha = outer - inner;
                if (alpha < 0.0f) {
                    alpha = 0.0f;
                }
                break;
            }
            case 5:
                dist = SDL_fabsf(nx) + SDL_fabsf(ny);
                alpha = 1.0f - lesson35_smoothstep(0.65f, 0.75f, dist);
                break;
            case 6: {
                const float arm_x = SDL_fabsf(nx) < 0.2f ? 1.0f : 0.0f;
                const float arm_y = SDL_fabsf(ny) < 0.2f ? 1.0f : 0.0f;
                const float cross = (arm_x > 0.0f || arm_y > 0.0f) ? 1.0f : 0.0f;
                dist = SDL_sqrtf(nx * nx + ny * ny);
                alpha = cross * (1.0f - lesson35_smoothstep(0.7f, 0.8f, dist));
                break;
            }
            case 7: {
                const float tri_y = ny + 0.3f;
                const float tri_edge = SDL_fabsf(nx) - (0.8f - tri_y * 0.8f);
                const float top = tri_y - 0.8f;
                const float bottom = -tri_y - 0.3f;
                float d_max = tri_edge;
                if (top > d_max) {
                    d_max = top;
                }
                if (bottom > d_max) {
                    d_max = bottom;
                }
                alpha = 1.0f - lesson35_smoothstep(-0.03f, 0.03f, d_max);
                break;
            }
            default:
                break;
            }

            if (alpha > 0.01f) {
                lesson35_set_pixel(pixels, x, y, size, 255, 255, 255, (Uint8)(alpha * 255.0f + 0.5f));
            }
        }
    }
}

static Mat4 lesson35_decal_rotation_basis(Vec3 forward, Vec3 up)
{
    Vec3 right;
    Mat4 rot = mat4_identity();

    if (SDL_fabsf(vec3_dot(forward, up)) > 0.99f) {
        up = { 1.0f, 0.0f, 0.0f };
    }
    right = vec3_normalize(vec3_cross(up, forward));
    up = vec3_cross(forward, right);

    rot.m[0] = right.x;
    rot.m[1] = right.y;
    rot.m[2] = right.z;
    rot.m[4] = up.x;
    rot.m[5] = up.y;
    rot.m[6] = up.z;
    rot.m[8] = forward.x;
    rot.m[9] = forward.y;
    rot.m[10] = forward.z;
    return rot;
}

static void lesson35_generate_decals(Lesson35State *state)
{
    Uint32 hash = forge_gpu_hash_wang(LESSON35_DECAL_SEED);

    state->decal_count = 0;
    for (int obj = 0; obj < LESSON35_SUZANNE_COUNT; obj += 1) {
        const Lesson35SceneObject *so = &state->suzannes[obj];

        for (int d = 0; d < LESSON35_DECALS_PER_OBJECT && state->decal_count < LESSON35_MAX_DECALS; d += 1) {
            Lesson35Decal *decal = &state->decals[state->decal_count];
            hash = forge_gpu_hash_wang(hash);
            const float theta = SDL_acosf(1.0f - 2.0f * forge_gpu_hash_to_float(hash));
            hash = forge_gpu_hash_wang(hash);
            const float phi = 2.0f * FORGE_GPU_PI * forge_gpu_hash_to_float(hash);
            const float sin_t = SDL_sinf(theta);
            const float dir_x = sin_t * SDL_cosf(phi);
            const float dir_y = sin_t * SDL_sinf(phi);
            const float dir_z = SDL_cosf(theta);
            const float surface_dist = LESSON35_DECAL_SURFACE_DIST * so->scale;
            Vec3 forward = { -dir_x, -dir_y, -dir_z };
            const Vec3 up = { 0.0f, 1.0f, 0.0f };
            const Mat4 rot = lesson35_decal_rotation_basis(forward, up);
            float sz;

            decal->position = {
                so->position.x + dir_x * surface_dist,
                so->position.y + dir_y * surface_dist,
                so->position.z + dir_z * surface_dist
            };

            hash = forge_gpu_hash_wang(hash);
            decal->orientation = quat_normalize(quat_multiply(
                quat_from_axis_angle(forward, forge_gpu_hash_to_float(hash) * 2.0f * FORGE_GPU_PI),
                quat_from_mat4(rot)));

            hash = forge_gpu_hash_wang(hash);
            sz = (LESSON35_DECAL_SIZE_MIN + forge_gpu_hash_to_float(hash) * LESSON35_DECAL_SIZE_RANGE) * so->scale;
            decal->size[0] = sz;
            decal->size[1] = sz * LESSON35_DECAL_DEPTH_RATIO;
            decal->size[2] = sz;

            hash = forge_gpu_hash_wang(hash);
            decal->tex_index = (int)(forge_gpu_hash_to_float(hash) * (float)LESSON35_NUM_DECAL_SHAPES) % LESSON35_NUM_DECAL_SHAPES;
            hash = forge_gpu_hash_wang(hash);
            const int color_index = (int)(forge_gpu_hash_to_float(hash) * (float)SDL_arraysize(kLesson35DecalColors)) % (int)SDL_arraysize(kLesson35DecalColors);
            SDL_memcpy(decal->tint, kLesson35DecalColors[color_index], sizeof(decal->tint));
            state->decal_count += 1;
        }
    }

    for (int f = 0; f < LESSON35_FLOOR_DECAL_COUNT && state->decal_count < LESSON35_MAX_DECALS; f += 1) {
        Lesson35Decal *decal = &state->decals[state->decal_count];
        const Vec3 forward = { 0.0f, -1.0f, 0.0f };
        Mat4 rot = mat4_identity();
        float sz;

        rot.m[0] = 1.0f;
        rot.m[1] = 0.0f;
        rot.m[2] = 0.0f;
        rot.m[4] = 0.0f;
        rot.m[5] = 0.0f;
        rot.m[6] = -1.0f;
        rot.m[8] = forward.x;
        rot.m[9] = forward.y;
        rot.m[10] = forward.z;

        hash = forge_gpu_hash_wang(hash);
        const float fx = forge_gpu_hash_to_sfloat(hash) * LESSON35_FLOOR_SCATTER;
        hash = forge_gpu_hash_wang(hash);
        const float fz = forge_gpu_hash_to_sfloat(hash) * LESSON35_FLOOR_SCATTER;
        decal->position = { fx, LESSON35_FLOOR_DECAL_OFFSET, fz };

        hash = forge_gpu_hash_wang(hash);
        decal->orientation = quat_normalize(quat_multiply(
            quat_from_axis_angle({ 0.0f, 1.0f, 0.0f }, forge_gpu_hash_to_float(hash) * 2.0f * FORGE_GPU_PI),
            quat_from_mat4(rot)));

        hash = forge_gpu_hash_wang(hash);
        sz = LESSON35_FLOOR_SIZE_BASE + forge_gpu_hash_to_float(hash) * LESSON35_FLOOR_SIZE_RANGE;
        decal->size[0] = sz;
        decal->size[1] = sz * LESSON35_DECAL_DEPTH_RATIO;
        decal->size[2] = sz;

        hash = forge_gpu_hash_wang(hash);
        decal->tex_index = (int)(forge_gpu_hash_to_float(hash) * (float)LESSON35_NUM_DECAL_SHAPES) % LESSON35_NUM_DECAL_SHAPES;
        hash = forge_gpu_hash_wang(hash);
        const int color_index = (int)(forge_gpu_hash_to_float(hash) * (float)SDL_arraysize(kLesson35DecalColors)) % (int)SDL_arraysize(kLesson35DecalColors);
        SDL_memcpy(decal->tint, kLesson35DecalColors[color_index], sizeof(decal->tint));
        state->decal_count += 1;
    }
}

static bool lesson35_create_geometry(ForgeGpuDemo *demo, Lesson35State *state)
{
    Lesson35Vertex cube_vertices[24];
    Uint16 cube_indices[36];
    Uint32 cube_vertex_count = 0;
    Uint32 cube_index_count = 0;

    lesson35_generate_cube(cube_vertices, &cube_vertex_count, cube_indices, &cube_index_count);
    state->cube_vertex_buffer = ForgeGpuCreateBufferWithData(demo->device, SDL_GPU_BUFFERUSAGE_VERTEX, cube_vertices, cube_vertex_count * (Uint32)sizeof(*cube_vertices));
    state->cube_index_buffer = ForgeGpuCreateBufferWithData(demo->device, SDL_GPU_BUFFERUSAGE_INDEX, cube_indices, cube_index_count * (Uint32)sizeof(*cube_indices));
    if (!ForgeGpuCreateShadowedGridBuffers(demo->device, LESSON35_GRID_HALF_SIZE, 0.0f, &state->grid_vertex_buffer, &state->grid_index_buffer)) {
        return false;
    }
    return state->cube_vertex_buffer && state->cube_index_buffer && state->grid_vertex_buffer && state->grid_index_buffer;
}

static bool lesson35_create_decal_textures(ForgeGpuDemo *demo, Lesson35State *state)
{
    Uint8 *pixels = (Uint8 *)SDL_malloc((size_t)LESSON35_DECAL_TEX_SIZE * (size_t)LESSON35_DECAL_TEX_SIZE * 4u);

    if (!pixels) {
        SDL_OutOfMemory();
        return false;
    }
    for (int i = 0; i < LESSON35_NUM_DECAL_SHAPES; i += 1) {
        lesson35_generate_decal_texture(i, pixels);
        state->decal_textures[i] = ForgeGpuCreateRgba8TextureFromPixels(
            demo->device,
            LESSON35_DECAL_TEX_SIZE,
            LESSON35_DECAL_TEX_SIZE,
            pixels,
            false);
        if (!state->decal_textures[i]) {
            SDL_free(pixels);
            return false;
        }
    }
    SDL_free(pixels);
    return true;
}

static bool lesson35_ensure_targets(ForgeGpuDemo *demo, Lesson35State *state, Uint32 width, Uint32 height)
{
    if (!ForgeGpuEnsureSampledDepthTarget(
            demo,
            &state->scene_depth,
            &state->scene_depth_width,
            &state->scene_depth_height,
            width,
            height,
            state->scene_depth_format)) {
        return false;
    }

    return ForgeGpuEnsureSampledColorTarget(
        demo,
        &state->scene_normal,
        &state->scene_normal_width,
        &state->scene_normal_height,
        width,
        height,
        SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
}

static void lesson35_fill_scene_vertex_uniforms(Lesson35SceneVertUniforms *uniforms, Mat4 view_projection, Mat4 light_vp, Mat4 model)
{
    uniforms->mvp = mat4_multiply(view_projection, model);
    uniforms->model = model;
    uniforms->light_vp = mat4_multiply(light_vp, model);
}

static void lesson35_fill_scene_fragment_uniforms(Lesson35SceneFragUniforms *uniforms, Vec3 eye_pos, Vec3 light_dir)
{
    SDL_zero(*uniforms);
    uniforms->base_color[0] = LESSON35_BASE_COLOR_GREY;
    uniforms->base_color[1] = LESSON35_BASE_COLOR_GREY;
    uniforms->base_color[2] = LESSON35_BASE_COLOR_GREY;
    uniforms->base_color[3] = 1.0f;
    uniforms->eye_pos[0] = eye_pos.x;
    uniforms->eye_pos[1] = eye_pos.y;
    uniforms->eye_pos[2] = eye_pos.z;
    uniforms->ambient = LESSON35_AMBIENT_SCENE;
    uniforms->light_dir[0] = light_dir.x;
    uniforms->light_dir[1] = light_dir.y;
    uniforms->light_dir[2] = light_dir.z;
    uniforms->light_color[0] = 1.0f;
    uniforms->light_color[1] = 1.0f;
    uniforms->light_color[2] = 1.0f;
    uniforms->light_intensity = 1.0f;
    uniforms->shininess = LESSON35_SHININESS;
    uniforms->specular_str = LESSON35_SPECULAR_STR;
}

static void lesson35_release_shader(SDL_GPUDevice *device, SDL_GPUShader **shader)
{
    if (*shader) {
        SDL_ReleaseGPUShader(device, *shader);
        *shader = nullptr;
    }
}

static SDL_GPUGraphicsPipeline *lesson35_create_decal_pipeline(
    ForgeGpuDemo *demo,
    SDL_GPUShader *vertex_shader,
    SDL_GPUShader *fragment_shader,
    const SDL_GPUVertexBufferDescription *vertex_buffer,
    const SDL_GPUVertexAttribute *attributes)
{
    SDL_GPUColorTargetDescription color_target;
    SDL_GPUGraphicsPipelineCreateInfo pipeline_info;

    SDL_zero(color_target);
    color_target.format = demo->color_format;
    color_target.blend_state.enable_blend = true;
    color_target.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    color_target.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    color_target.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
    color_target.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    color_target.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
    color_target.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;

    SDL_zero(pipeline_info);
    pipeline_info.vertex_shader = vertex_shader;
    pipeline_info.fragment_shader = fragment_shader;
    pipeline_info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pipeline_info.vertex_input_state.vertex_buffer_descriptions = vertex_buffer;
    pipeline_info.vertex_input_state.num_vertex_buffers = 1;
    pipeline_info.vertex_input_state.vertex_attributes = attributes;
    pipeline_info.vertex_input_state.num_vertex_attributes = 2;
    pipeline_info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    pipeline_info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_FRONT;
    pipeline_info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    pipeline_info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    pipeline_info.target_info.color_target_descriptions = &color_target;
    pipeline_info.target_info.num_color_targets = 1;
    return SDL_CreateGPUGraphicsPipeline(demo->device, &pipeline_info);
}

static bool lesson35_create_pipelines(ForgeGpuDemo *demo, Lesson35State *state)
{
    SDL_GPUShader *scene_vertex_shader = nullptr;
    SDL_GPUShader *scene_fragment_shader = nullptr;
    SDL_GPUShader *shadow_vertex_shader = nullptr;
    SDL_GPUShader *shadow_fragment_shader = nullptr;
    SDL_GPUShader *grid_vertex_shader = nullptr;
    SDL_GPUShader *grid_fragment_shader = nullptr;
    SDL_GPUShader *decal_vertex_shader = nullptr;
    SDL_GPUShader *decal_fragment_shader = nullptr;
    SDL_GPUVertexBufferDescription mesh_vertex_buffer;
    SDL_GPUVertexAttribute mesh_attributes[3];
    SDL_GPUVertexBufferDescription shadow_vertex_buffer;
    SDL_GPUVertexAttribute shadow_attribute;
    SDL_GPUVertexBufferDescription grid_vertex_buffer;
    SDL_GPUVertexAttribute grid_attribute;
    SDL_GPUVertexBufferDescription decal_vertex_buffer;
    SDL_GPUVertexAttribute decal_attributes[2];
    SDL_GPUColorTargetDescription scene_color_targets[2];
    bool ok = false;

    scene_vertex_shader = ForgeGpuCreateShader(
        demo->device, SDL_GPU_SHADERSTAGE_VERTEX,
        lesson35_scene_vert_wgsl, lesson35_scene_vert_wgsl_size,
        lesson35_scene_vert_msl, lesson35_scene_vert_msl_size,
        0, 0, 0, 1);
    scene_fragment_shader = ForgeGpuCreateShaderWithResourceLayout(
        demo->device,
        lesson35_scene_frag_wgsl, lesson35_scene_frag_wgsl_size,
        lesson35_scene_frag_msl, lesson35_scene_frag_msl_size,
        ForgeGpuShaderLayout_lesson35_scene_frag());
    shadow_vertex_shader = ForgeGpuCreateShader(
        demo->device, SDL_GPU_SHADERSTAGE_VERTEX,
        lesson35_shadow_vert_wgsl, lesson35_shadow_vert_wgsl_size,
        lesson35_shadow_vert_msl, lesson35_shadow_vert_msl_size,
        0, 0, 0, 1);
    shadow_fragment_shader = ForgeGpuCreateShader(
        demo->device, SDL_GPU_SHADERSTAGE_FRAGMENT,
        lesson35_shadow_frag_wgsl, lesson35_shadow_frag_wgsl_size,
        lesson35_shadow_frag_msl, lesson35_shadow_frag_msl_size,
        0, 0, 0, 0);
    grid_vertex_shader = ForgeGpuCreateShader(
        demo->device, SDL_GPU_SHADERSTAGE_VERTEX,
        lesson35_grid_vert_wgsl, lesson35_grid_vert_wgsl_size,
        lesson35_grid_vert_msl, lesson35_grid_vert_msl_size,
        0, 0, 0, 1);
    grid_fragment_shader = ForgeGpuCreateShaderWithResourceLayout(
        demo->device,
        lesson35_grid_frag_wgsl, lesson35_grid_frag_wgsl_size,
        lesson35_grid_frag_msl, lesson35_grid_frag_msl_size,
        ForgeGpuShaderLayout_lesson35_grid_frag());
    decal_vertex_shader = ForgeGpuCreateShader(
        demo->device, SDL_GPU_SHADERSTAGE_VERTEX,
        lesson35_decal_vert_wgsl, lesson35_decal_vert_wgsl_size,
        lesson35_decal_vert_msl, lesson35_decal_vert_msl_size,
        0, 0, 0, 1);
    decal_fragment_shader = ForgeGpuCreateShaderWithResourceLayout(
        demo->device,
        lesson35_decal_frag_wgsl, lesson35_decal_frag_wgsl_size,
        lesson35_decal_frag_msl, lesson35_decal_frag_msl_size,
        ForgeGpuShaderLayout_lesson35_decal_frag());
    if (!scene_vertex_shader || !scene_fragment_shader || !shadow_vertex_shader || !shadow_fragment_shader ||
        !grid_vertex_shader || !grid_fragment_shader || !decal_vertex_shader || !decal_fragment_shader) {
        goto done;
    }

    ForgeGpuFillMeshVertexInput(&mesh_vertex_buffer, mesh_attributes);
    SDL_zero(shadow_vertex_buffer);
    shadow_vertex_buffer.slot = 0;
    shadow_vertex_buffer.pitch = sizeof(ForgeGpuMeshVertex);
    shadow_vertex_buffer.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    SDL_zero(shadow_attribute);
    shadow_attribute.location = 0;
    shadow_attribute.buffer_slot = 0;
    shadow_attribute.format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    shadow_attribute.offset = offsetof(ForgeGpuMeshVertex, position);

    SDL_zero(grid_vertex_buffer);
    grid_vertex_buffer.slot = 0;
    grid_vertex_buffer.pitch = sizeof(GridVertex);
    grid_vertex_buffer.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    SDL_zero(grid_attribute);
    grid_attribute.location = 0;
    grid_attribute.buffer_slot = 0;
    grid_attribute.format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    grid_attribute.offset = 0;

    SDL_zero(decal_vertex_buffer);
    decal_vertex_buffer.slot = 0;
    decal_vertex_buffer.pitch = sizeof(Lesson35Vertex);
    decal_vertex_buffer.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    SDL_zeroa(decal_attributes);
    decal_attributes[0].location = 0;
    decal_attributes[0].buffer_slot = 0;
    decal_attributes[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    decal_attributes[0].offset = offsetof(Lesson35Vertex, position);
    decal_attributes[1].location = 1;
    decal_attributes[1].buffer_slot = 0;
    decal_attributes[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    decal_attributes[1].offset = offsetof(Lesson35Vertex, normal);

    SDL_zeroa(scene_color_targets);
    scene_color_targets[0].format = demo->color_format;
    scene_color_targets[1].format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;

    state->shadow_pipeline = ForgeGpuCreateLessonGraphicsPipelineWithColorTargetsAndDepthCompare(
        demo, shadow_vertex_shader, shadow_fragment_shader,
        SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        nullptr, 0,
        &shadow_vertex_buffer, 1, &shadow_attribute, 1,
        true, state->shadow_depth_format,
        true, true, SDL_GPU_COMPAREOP_LESS,
        SDL_GPU_CULLMODE_BACK, 0.0f, 0.0f);
    state->scene_pipeline = ForgeGpuCreateLessonGraphicsPipelineWithColorTargetsAndDepthCompare(
        demo, scene_vertex_shader, scene_fragment_shader,
        SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        scene_color_targets, 2,
        &mesh_vertex_buffer, 1, mesh_attributes, 3,
        true, state->scene_depth_format,
        true, true, SDL_GPU_COMPAREOP_LESS,
        SDL_GPU_CULLMODE_BACK, 0.0f, 0.0f);
    state->grid_pipeline = ForgeGpuCreateLessonGraphicsPipelineWithColorTargetsAndDepthCompare(
        demo, grid_vertex_shader, grid_fragment_shader,
        SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        scene_color_targets, 2,
        &grid_vertex_buffer, 1, &grid_attribute, 1,
        true, state->scene_depth_format,
        true, true, SDL_GPU_COMPAREOP_LESS_OR_EQUAL,
        SDL_GPU_CULLMODE_NONE, 0.0f, 0.0f);
    state->decal_pipeline = lesson35_create_decal_pipeline(demo, decal_vertex_shader, decal_fragment_shader, &decal_vertex_buffer, decal_attributes);

    ok = state->shadow_pipeline && state->scene_pipeline && state->grid_pipeline && state->decal_pipeline;

done:
    lesson35_release_shader(demo->device, &scene_vertex_shader);
    lesson35_release_shader(demo->device, &scene_fragment_shader);
    lesson35_release_shader(demo->device, &shadow_vertex_shader);
    lesson35_release_shader(demo->device, &shadow_fragment_shader);
    lesson35_release_shader(demo->device, &grid_vertex_shader);
    lesson35_release_shader(demo->device, &grid_fragment_shader);
    lesson35_release_shader(demo->device, &decal_vertex_shader);
    lesson35_release_shader(demo->device, &decal_fragment_shader);
    return ok;
}

static Mat4 lesson35_suzanne_model(const Lesson35SceneObject *object)
{
    return mat4_multiply(
        mat4_translate(object->position),
        mat4_multiply(mat4_rotate_y(object->rotation_y), mat4_scale(object->scale)));
}

static Mat4 lesson35_decal_model(const Lesson35Decal *decal)
{
    const Vec3 scale = {
        decal->size[0] * 2.0f,
        decal->size[1] * 2.0f,
        decal->size[2] * 2.0f
    };

    return mat4_multiply(
        mat4_translate(decal->position),
        mat4_multiply(quat_to_mat4(decal->orientation), mat4_scale_vec3(scale)));
}

static void lesson35_bind_suzanne(SDL_GPURenderPass *render_pass, const GpuPrimitive *primitive)
{
    SDL_GPUBufferBinding vertex_binding;
    SDL_GPUBufferBinding index_binding;

    SDL_zero(vertex_binding);
    vertex_binding.buffer = primitive->vertex_buffer;
    SDL_BindGPUVertexBuffers(render_pass, 0, &vertex_binding, 1);
    SDL_zero(index_binding);
    index_binding.buffer = primitive->index_buffer;
    SDL_BindGPUIndexBuffer(render_pass, &index_binding, primitive->index_type);
}

static void lesson35_draw_shadow_suzannes(
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    Lesson35State *state)
{
    const GpuPrimitive *primitive = &state->suzanne.primitives[0];

    lesson35_bind_suzanne(render_pass, primitive);
    for (int i = 0; i < LESSON35_SUZANNE_COUNT; i += 1) {
        const Mat4 model = lesson35_suzanne_model(&state->suzannes[i]);
        const Mat4 shadow_mvp = mat4_multiply(state->light_vp, model);

        SDL_PushGPUVertexUniformData(command_buffer, 0, &shadow_mvp, sizeof(shadow_mvp));
        SDL_DrawGPUIndexedPrimitives(render_pass, primitive->index_count, 1, 0, 0, 0);
    }
}

static void lesson35_draw_scene_suzannes(
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    Lesson35State *state,
    Mat4 view_projection,
    Vec3 eye_pos)
{
    const GpuPrimitive *primitive = &state->suzanne.primitives[0];
    Lesson35SceneFragUniforms fragment_uniforms;

    lesson35_bind_suzanne(render_pass, primitive);
    lesson35_fill_scene_fragment_uniforms(&fragment_uniforms, eye_pos, state->light_dir);

    for (int i = 0; i < LESSON35_SUZANNE_COUNT; i += 1) {
        Lesson35SceneVertUniforms vertex_uniforms;
        const Mat4 model = lesson35_suzanne_model(&state->suzannes[i]);

        lesson35_fill_scene_vertex_uniforms(&vertex_uniforms, view_projection, state->light_vp, model);
        SDL_PushGPUVertexUniformData(command_buffer, 0, &vertex_uniforms, sizeof(vertex_uniforms));
        SDL_PushGPUFragmentUniformData(command_buffer, 0, &fragment_uniforms, sizeof(fragment_uniforms));
        SDL_DrawGPUIndexedPrimitives(render_pass, primitive->index_count, 1, 0, 0, 0);
    }
}

static void lesson35_draw_grid(
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    Lesson35State *state,
    Mat4 view_projection,
    Vec3 eye_pos)
{
    ForgeGpuShadowedGridDrawInfo draw_info;

    SDL_zero(draw_info);
    draw_info.vp = view_projection;
    draw_info.light_vp = state->light_vp;
    draw_info.light_dir = state->light_dir;
    draw_info.eye_pos = eye_pos;
    draw_info.light_intensity = 1.0f;
    draw_info.line_color[0] = LESSON35_GRID_LINE_COLOR_R;
    draw_info.line_color[1] = LESSON35_GRID_LINE_COLOR_G;
    draw_info.line_color[2] = LESSON35_GRID_LINE_COLOR_B;
    draw_info.line_color[3] = 1.0f;
    draw_info.bg_color[0] = LESSON35_GRID_BG_COLOR_R;
    draw_info.bg_color[1] = LESSON35_GRID_BG_COLOR_G;
    draw_info.bg_color[2] = LESSON35_GRID_BG_COLOR_B;
    draw_info.bg_color[3] = 1.0f;
    draw_info.grid_spacing = LESSON35_GRID_SPACING;
    draw_info.line_width = LESSON35_GRID_LINE_WIDTH;
    draw_info.fade_distance = LESSON35_GRID_FADE_DIST;
    draw_info.ambient = LESSON35_AMBIENT_GRID;
    draw_info.shadow_depth = state->shadow_depth;
    draw_info.shadow_sampler = state->nearest_clamp;
    ForgeGpuDrawShadowedGrid(command_buffer, render_pass, state->grid_pipeline, state->grid_vertex_buffer, state->grid_index_buffer, &draw_info);
}

static void lesson35_draw_decals(
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    Lesson35State *state,
    Mat4 view_projection,
    Mat4 inv_view_projection,
    Vec3 eye_pos,
    Uint32 width,
    Uint32 height)
{
    SDL_GPUBufferBinding vertex_binding;
    SDL_GPUBufferBinding index_binding;
    SDL_GPUTextureSamplerBinding samplers[4];

    SDL_BindGPUGraphicsPipeline(render_pass, state->decal_pipeline);
    SDL_zero(vertex_binding);
    vertex_binding.buffer = state->cube_vertex_buffer;
    SDL_BindGPUVertexBuffers(render_pass, 0, &vertex_binding, 1);
    SDL_zero(index_binding);
    index_binding.buffer = state->cube_index_buffer;
    SDL_BindGPUIndexBuffer(render_pass, &index_binding, SDL_GPU_INDEXELEMENTSIZE_16BIT);

    SDL_zeroa(samplers);
    samplers[0].texture = state->scene_depth;
    samplers[0].sampler = state->nearest_clamp;
    samplers[2].texture = state->shadow_depth;
    samplers[2].sampler = state->nearest_clamp;
    samplers[3].texture = state->scene_normal;
    samplers[3].sampler = state->nearest_clamp;

    for (int i = 0; i < state->decal_count; i += 1) {
        const Lesson35Decal *decal = &state->decals[i];
        const Mat4 decal_model = lesson35_decal_model(decal);
        Lesson35DecalVertUniforms vertex_uniforms;
        Lesson35DecalFragUniforms fragment_uniforms;

        vertex_uniforms.mvp = mat4_multiply(view_projection, decal_model);
        SDL_PushGPUVertexUniformData(command_buffer, 0, &vertex_uniforms, sizeof(vertex_uniforms));

        SDL_zero(fragment_uniforms);
        fragment_uniforms.inv_vp = inv_view_projection;
        fragment_uniforms.inv_decal_model = mat4_inverse(decal_model);
        fragment_uniforms.light_vp = state->light_vp;
        fragment_uniforms.screen_size[0] = (float)width;
        fragment_uniforms.screen_size[1] = (float)height;
        fragment_uniforms.near_plane = LESSON35_NEAR_PLANE;
        fragment_uniforms.far_plane = LESSON35_FAR_PLANE;
        SDL_memcpy(fragment_uniforms.decal_tint, decal->tint, sizeof(fragment_uniforms.decal_tint));
        fragment_uniforms.eye_pos[0] = eye_pos.x;
        fragment_uniforms.eye_pos[1] = eye_pos.y;
        fragment_uniforms.eye_pos[2] = eye_pos.z;
        fragment_uniforms.ambient = LESSON35_AMBIENT_SCENE;
        fragment_uniforms.light_dir[0] = state->light_dir.x;
        fragment_uniforms.light_dir[1] = state->light_dir.y;
        fragment_uniforms.light_dir[2] = state->light_dir.z;
        fragment_uniforms.light_intensity = 1.0f;
        fragment_uniforms.light_color[0] = 1.0f;
        fragment_uniforms.light_color[1] = 1.0f;
        fragment_uniforms.light_color[2] = 1.0f;
        fragment_uniforms.shininess = LESSON35_SHININESS;
        fragment_uniforms.specular_str = LESSON35_SPECULAR_STR;
        SDL_PushGPUFragmentUniformData(command_buffer, 0, &fragment_uniforms, sizeof(fragment_uniforms));

        samplers[1].texture = state->decal_textures[decal->tex_index];
        samplers[1].sampler = state->linear_clamp;
        SDL_BindGPUFragmentSamplers(render_pass, 0, samplers, SDL_arraysize(samplers));
        SDL_DrawGPUIndexedPrimitives(render_pass, LESSON35_CUBE_INDEX_COUNT, 1, 0, 0, 0);
    }
}

bool ForgeGpuRenderLesson35(ForgeGpuDemo *demo, SDL_GPUCommandBuffer *command_buffer, SDL_GPUTexture *swapchain_texture, Uint32 width, Uint32 height)
{
    Lesson35State *state = lesson35_state(demo);
    Mat4 view;
    Mat4 projection;
    Mat4 view_projection;
    Mat4 inv_view_projection;
    SDL_GPUDepthStencilTargetInfo shadow_depth_target;
    SDL_GPUColorTargetInfo scene_color_targets[2];
    SDL_GPUDepthStencilTargetInfo scene_depth_target;
    SDL_GPUTextureSamplerBinding shadow_binding;
    SDL_GPUColorTargetInfo decal_color_target;
    SDL_GPURenderPass *render_pass;

    if (!state || state->suzanne.primitive_count <= 0) {
        SDL_SetError("lesson 35 state is missing");
        return false;
    }
    if (!lesson35_ensure_targets(demo, state, width, height)) {
        return false;
    }

    ForgeGpuUpdateCameraFromInput(demo);
    ForgeGpuCameraViewProjection(demo, width, height, LESSON35_FAR_PLANE, &view, &projection);
    view_projection = mat4_multiply(projection, view);
    inv_view_projection = mat4_inverse(view_projection);

    state->shadow_pass_rendered = false;
    state->scene_pass_rendered = false;
    state->decal_pass_rendered = false;

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
    lesson35_draw_shadow_suzannes(command_buffer, render_pass, state);
    SDL_EndGPURenderPass(render_pass);
    state->shadow_pass_rendered = true;

    SDL_zeroa(scene_color_targets);
    scene_color_targets[0].texture = swapchain_texture;
    scene_color_targets[0].load_op = SDL_GPU_LOADOP_CLEAR;
    scene_color_targets[0].store_op = SDL_GPU_STOREOP_STORE;
    scene_color_targets[0].clear_color = { LESSON35_CLEAR_R, LESSON35_CLEAR_G, LESSON35_CLEAR_B, 1.0f };
    scene_color_targets[1].texture = state->scene_normal;
    scene_color_targets[1].load_op = SDL_GPU_LOADOP_CLEAR;
    scene_color_targets[1].store_op = SDL_GPU_STOREOP_STORE;
    scene_color_targets[1].clear_color = { 0.5f, 1.0f, 0.5f, 1.0f };
    SDL_zero(scene_depth_target);
    scene_depth_target.texture = state->scene_depth;
    scene_depth_target.load_op = SDL_GPU_LOADOP_CLEAR;
    scene_depth_target.store_op = SDL_GPU_STOREOP_STORE;
    scene_depth_target.clear_depth = 1.0f;
    scene_depth_target.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
    scene_depth_target.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
    if (lesson35_format_has_stencil(state->scene_depth_format)) {
        scene_depth_target.stencil_load_op = SDL_GPU_LOADOP_CLEAR;
        scene_depth_target.stencil_store_op = SDL_GPU_STOREOP_STORE;
        scene_depth_target.clear_stencil = 0;
    }

    render_pass = SDL_BeginGPURenderPass(command_buffer, scene_color_targets, 2, &scene_depth_target);
    if (!render_pass) {
        return false;
    }
    SDL_zero(shadow_binding);
    shadow_binding.texture = state->shadow_depth;
    shadow_binding.sampler = state->nearest_clamp;

    SDL_BindGPUGraphicsPipeline(render_pass, state->scene_pipeline);
    SDL_BindGPUFragmentSamplers(render_pass, 0, &shadow_binding, 1);
    lesson35_draw_scene_suzannes(command_buffer, render_pass, state, view_projection, demo->lesson.camera_position);

    lesson35_draw_grid(command_buffer, render_pass, state, view_projection, demo->lesson.camera_position);
    SDL_EndGPURenderPass(render_pass);
    state->scene_pass_rendered = true;

    SDL_zero(decal_color_target);
    decal_color_target.texture = swapchain_texture;
    decal_color_target.load_op = SDL_GPU_LOADOP_LOAD;
    decal_color_target.store_op = SDL_GPU_STOREOP_STORE;
    render_pass = SDL_BeginGPURenderPass(command_buffer, &decal_color_target, 1, nullptr);
    if (!render_pass) {
        return false;
    }
    lesson35_draw_decals(command_buffer, render_pass, state, view_projection, inv_view_projection, demo->lesson.camera_position, width, height);
    SDL_EndGPURenderPass(render_pass);
    state->decal_pass_rendered = true;
    return true;
}

void ForgeGpuDebugLesson35(ForgeGpuDemo *demo)
{
    Lesson35State *state = lesson35_state(demo);

    if (!state) {
        return;
    }
    ImGui::Text("Decals: %d", state->decal_count);
    ImGui::Text("Scene depth: %s", lesson35_format_name(state->scene_depth_format));
    ImGui::Text("Shadow depth: %s", lesson35_format_name(state->shadow_depth_format));
    ImGui::Text("Passes: shadow %s, scene %s, decals %s",
        state->shadow_pass_rendered ? "yes" : "no",
        state->scene_pass_rendered ? "yes" : "no",
        state->decal_pass_rendered ? "yes" : "no");
}

void ForgeGpuExportLesson35Metrics(ForgeGpuDemo *demo)
{
#if defined(SDL_PLATFORM_EMSCRIPTEN)
    Lesson35State *state = lesson35_state(demo);

    if (!state) {
        return;
    }
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson35DecalCount", (double)state->decal_count);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson35ShadowPass", state->shadow_pass_rendered ? 1.0 : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson35ScenePass", state->scene_pass_rendered ? 1.0 : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson35DecalPass", state->decal_pass_rendered ? 1.0 : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson35SceneDepthD24S8", state->scene_depth_format == SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT ? 1.0 : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson35SceneDepthD32S8", state->scene_depth_format == SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT ? 1.0 : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson35SceneDepthD32", state->scene_depth_format == SDL_GPU_TEXTUREFORMAT_D32_FLOAT ? 1.0 : 0.0);
#else
    (void)demo;
#endif
}

bool ForgeGpuCreateLesson35(ForgeGpuDemo *demo)
{
    Lesson35State *state = (Lesson35State *)SDL_calloc(1, sizeof(*state));

    if (!state) {
        SDL_OutOfMemory();
        return false;
    }
    demo->lesson.private_state = state;

    state->shadow_depth_format = lesson35_select_shadow_depth_format(demo->device);
    state->scene_depth_format = lesson35_select_scene_depth_format(demo->device);
    if (state->shadow_depth_format == SDL_GPU_TEXTUREFORMAT_INVALID ||
        state->scene_depth_format == SDL_GPU_TEXTUREFORMAT_INVALID) {
        SDL_SetError("lesson 35 requires sampled shadow and scene depth formats");
        goto fail;
    }

    if (!ForgeGpuLoadSceneModel(demo, &state->suzanne, "models/Suzanne/Suzanne.gltf")) {
        goto fail;
    }
    if (state->suzanne.primitive_count <= 0) {
        SDL_SetError("lesson 35 Suzanne asset has no primitives");
        goto fail;
    }

    state->shadow_depth = ForgeGpuCreateSampledDepthTexture(demo, LESSON35_SHADOW_MAP_SIZE, LESSON35_SHADOW_MAP_SIZE, state->shadow_depth_format);
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

    if (!lesson35_create_pipelines(demo, state) ||
        !lesson35_create_geometry(demo, state) ||
        !lesson35_create_decal_textures(demo, state)) {
        goto fail;
    }

    {
        const float base_scales[LESSON35_SUZANNE_COUNT] = { 1.0f, 0.8f, 1.2f, 0.9f, 1.1f, 0.7f };
        for (int i = 0; i < LESSON35_SUZANNE_COUNT; i += 1) {
            const float angle = (float)i / (float)LESSON35_SUZANNE_COUNT * 2.0f * FORGE_GPU_PI;
            state->suzannes[i].position = {
                LESSON35_RING_RADIUS * SDL_sinf(angle),
                1.0f,
                LESSON35_RING_RADIUS * SDL_cosf(angle)
            };
            state->suzannes[i].scale = base_scales[i];
            state->suzannes[i].rotation_y = angle + FORGE_GPU_PI;
        }
    }
    lesson35_generate_decals(state);

    state->light_dir = vec3_normalize({ 0.4f, -0.8f, -0.6f });
    state->light_vp = ForgeGpuComputeTargetedDirectionalLightViewProjection(
        state->light_dir,
        LESSON35_LIGHT_DISTANCE,
        { 0.0f, 0.0f, 0.0f },
        LESSON35_LIGHT_ORTHO_SIZE,
        LESSON35_LIGHT_ORTHO_NEAR,
        LESSON35_LIGHT_ORTHO_FAR,
        0.99f);

    lesson35_init_camera(demo);
    return true;

fail:
    ForgeGpuDestroyLesson35(demo);
    return false;
}

void ForgeGpuDestroyLesson35(ForgeGpuDemo *demo)
{
    Lesson35State *state = lesson35_state(demo);

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
    if (state->decal_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, state->decal_pipeline);
    }
    if (state->cube_vertex_buffer) {
        SDL_ReleaseGPUBuffer(demo->device, state->cube_vertex_buffer);
    }
    if (state->cube_index_buffer) {
        SDL_ReleaseGPUBuffer(demo->device, state->cube_index_buffer);
    }
    if (state->grid_vertex_buffer) {
        SDL_ReleaseGPUBuffer(demo->device, state->grid_vertex_buffer);
    }
    if (state->grid_index_buffer) {
        SDL_ReleaseGPUBuffer(demo->device, state->grid_index_buffer);
    }
    if (state->shadow_depth) {
        SDL_ReleaseGPUTexture(demo->device, state->shadow_depth);
    }
    if (state->scene_depth) {
        SDL_ReleaseGPUTexture(demo->device, state->scene_depth);
    }
    if (state->scene_normal) {
        SDL_ReleaseGPUTexture(demo->device, state->scene_normal);
    }
    for (int i = 0; i < LESSON35_NUM_DECAL_SHAPES; i += 1) {
        if (state->decal_textures[i]) {
            SDL_ReleaseGPUTexture(demo->device, state->decal_textures[i]);
        }
    }
    if (state->nearest_clamp) {
        SDL_ReleaseGPUSampler(demo->device, state->nearest_clamp);
    }
    if (state->linear_clamp) {
        SDL_ReleaseGPUSampler(demo->device, state->linear_clamp);
    }
    ForgeGpuFreeSceneData(demo, &state->suzanne);
    SDL_free(state);
    demo->lesson.private_state = nullptr;
}
