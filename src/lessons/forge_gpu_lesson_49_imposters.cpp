#include "forge_gpu_lessons.h"

#include "forge_gpu_browser_status.h"
#include "forge_gpu_camera.h"
#include "forge_gpu_gpu_helpers.h"
#include "forge_gpu_imgui.h"
#include "forge_gpu_lesson_common.h"
#include "forge_gpu_math.h"
#include "forge_gpu_processed_scene_renderer.h"
#include "forge_gpu_shader_layouts.h"
#include "forge_gpu_shapes.h"
#include "shaders/generated/forge_gpu_lesson_49_shaders.h"
#include "shaders/generated/forge_gpu_shared_scene_shaders.h"

#include <stddef.h>

#include "imgui.h"

#define LESSON49_ATLAS_FRAMES 8
#define LESSON49_ATLAS_COLS 4
#define LESSON49_ATLAS_ROWS 2
#define LESSON49_ATLAS_FRAME_SIZE 256u
#define LESSON49_ATLAS_WIDTH (LESSON49_ATLAS_COLS * LESSON49_ATLAS_FRAME_SIZE)
#define LESSON49_ATLAS_HEIGHT (LESSON49_ATLAS_ROWS * LESSON49_ATLAS_FRAME_SIZE)

#define LESSON49_LOD_DIST_DEFAULT 16.0f
#define LESSON49_LOD_DIST_MIN 3.0f
#define LESSON49_LOD_DIST_MAX 40.0f
#define LESSON49_LOD_FADE_BAND 3.0f
#define LESSON49_LOD_FADE_RANGE_MIN 0.1f
#define LESSON49_LOD_NEAR_CLAMP 1.0f

#define LESSON49_HEIGHTMAP_SIZE 256
#define LESSON49_TERRAIN_SIZE 20.0f
#define LESSON49_TERRAIN_HEIGHT_SCALE 8.0f
#define LESSON49_DEFAULT_SNOW_LINE 0.7f
#define LESSON49_DEFAULT_SLOPE_THRESHOLD 0.3f
#define LESSON49_DEFAULT_TEXTURE_REPEAT 8.0f
#define LESSON49_NOISE_OCTAVES 6
#define LESSON49_NOISE_FREQUENCY 10.0f
#define LESSON49_NOISE_LACUNARITY 2.0f
#define LESSON49_NOISE_PERSISTENCE 0.5f
#define LESSON49_NOISE_SEED 42u
#define LESSON49_HEIGHTMAP_RANGE_MIN 1e-6f

#define LESSON49_TREE_COUNT_MAX 2000
#define LESSON49_TREE_TRUNK_RADIUS 0.15f
#define LESSON49_TREE_TRUNK_HEIGHT 0.5f
#define LESSON49_TREE_CROWN_RADIUS 0.5f
#define LESSON49_TREE_CROWN_HEIGHT 0.8f
#define LESSON49_TREE_TOTAL_HEIGHT (LESSON49_TREE_TRUNK_HEIGHT + LESSON49_TREE_CROWN_HEIGHT)
#define LESSON49_TREE_TRUNK_SIDES 12
#define LESSON49_TREE_TRUNK_SEGS 1
#define LESSON49_TREE_CROWN_SLICES 12
#define LESSON49_TREE_CROWN_SEGS 4
#define LESSON49_SLOPE_TREE_THRESHOLD 0.4f
#define LESSON49_TREE_SCALE_MIN 0.8f
#define LESSON49_TREE_SCALE_RANGE 0.4f

#define LESSON49_BAKE_VIEW_DIST 5.0f
#define LESSON49_BAKE_ORTHO_MARGIN 0.05f
#define LESSON49_BAKE_NEAR_CLIP 0.1f
#define LESSON49_BAKE_AMBIENT 0.3f
#define LESSON49_BAKE_LIGHT_X 0.5f
#define LESSON49_BAKE_LIGHT_Y 0.8f
#define LESSON49_BAKE_LIGHT_Z 0.3f
#define LESSON49_TRUNK_COLOR_R 0.55f
#define LESSON49_TRUNK_COLOR_G 0.40f
#define LESSON49_TRUNK_COLOR_B 0.25f
#define LESSON49_CROWN_COLOR_R 0.85f
#define LESSON49_CROWN_COLOR_G 0.85f
#define LESSON49_CROWN_COLOR_B 0.85f

#define LESSON49_FAR_PLANE 200.0f
#define LESSON49_MOVE_SPEED 5.0f
#define LESSON49_MOUSE_SENSITIVITY 0.003f
#define LESSON49_PITCH_CLAMP 1.5f
#define LESSON49_CAM_START_X 0.0f
#define LESSON49_CAM_START_Y 12.0f
#define LESSON49_CAM_START_Z 18.0f
#define LESSON49_CAM_START_YAW 0.0f
#define LESSON49_CAM_START_PITCH (-0.45f)

#define LESSON49_AMBIENT 0.15f
#define LESSON49_SHININESS 32.0f
#define LESSON49_SPECULAR_STRENGTH 0.5f
#define LESSON49_LIGHT_INTENSITY 1.2f
#define LESSON49_SHADOW_BIAS_CONST 2.0f
#define LESSON49_SHADOW_BIAS_SLOPE 2.0f
#define LESSON49_TAU 6.28318530717958647692f

typedef struct Lesson49TerrainVertex
{
    float position[3];
    float uv[2];
} Lesson49TerrainVertex;

typedef struct Lesson49TreeVertex
{
    float position[3];
    float normal[3];
} Lesson49TreeVertex;

typedef struct Lesson49TreeInstance
{
    Mat4 transform;
    float color[4];
} Lesson49TreeInstance;

typedef struct Lesson49ImposterInstance
{
    Mat4 transform;
    float atlas_uv[4];
    float alpha;
    float color[3];
} Lesson49ImposterInstance;

typedef struct Lesson49TreePosition
{
    Vec3 world_pos;
    float scale;
    float color[3];
} Lesson49TreePosition;

typedef struct Lesson49TerrainVertUniforms
{
    Mat4 vp;
    Mat4 light_vp;
    float terrain_size;
    float height_scale;
    float pad[2];
} Lesson49TerrainVertUniforms;

typedef struct Lesson49TerrainShadowVertUniforms
{
    Mat4 light_vp;
    float terrain_size;
    float height_scale;
    float pad[2];
} Lesson49TerrainShadowVertUniforms;

typedef struct Lesson49TerrainFragUniforms
{
    float light_dir[3];
    float light_intensity;
    float eye_pos[3];
    float ambient;
    float height_scale;
    float terrain_size;
    float texture_repeat;
    float snow_line;
    float slope_threshold;
    float pad[3];
} Lesson49TerrainFragUniforms;

typedef struct Lesson49InstancedVertUniforms
{
    Mat4 vp;
    Mat4 light_vp;
    Mat4 node_world;
} Lesson49InstancedVertUniforms;

typedef struct Lesson49InstancedShadowVertUniforms
{
    Mat4 light_vp;
    Mat4 node_world;
} Lesson49InstancedShadowVertUniforms;

typedef struct Lesson49TreeFragUniforms
{
    float base_color[4];
    float eye_pos[3];
    float ambient;
    float light_dir[4];
    float light_color[3];
    float light_intensity;
    float shininess;
    float specular_strength;
    float pad[2];
} Lesson49TreeFragUniforms;

typedef struct Lesson49BakeVertUniforms
{
    Mat4 mvp;
} Lesson49BakeVertUniforms;

typedef struct Lesson49BakeFragUniforms
{
    float base_color[4];
    float light_dir[3];
    float ambient;
} Lesson49BakeFragUniforms;

typedef struct Lesson49ImposterVertUniforms
{
    Mat4 vp;
    float cam_pos[3];
    float pad;
} Lesson49ImposterVertUniforms;

typedef enum Lesson49DiagnosticMode
{
    LESSON49_DIAGNOSTIC_DEFAULT,
    LESSON49_DIAGNOSTIC_NO_SHADOW,
    LESSON49_DIAGNOSTIC_NO_VARIATION,
    LESSON49_DIAGNOSTIC_TERRAIN_ONLY,
    LESSON49_DIAGNOSTIC_NEAR_TREES_ONLY,
    LESSON49_DIAGNOSTIC_IMPOSTERS_ONLY
} Lesson49DiagnosticMode;

typedef struct Lesson49State
{
    ForgeGpuProcessedSceneRenderer renderer;
    SDL_GPUGraphicsPipeline *terrain_pipeline;
    SDL_GPUGraphicsPipeline *terrain_no_variation_pipeline;
    SDL_GPUGraphicsPipeline *terrain_shadow_pipeline;
    SDL_GPUGraphicsPipeline *tree_pipeline;
    SDL_GPUGraphicsPipeline *tree_shadow_pipeline;
    SDL_GPUGraphicsPipeline *imposter_pipeline;
    SDL_GPUBuffer *terrain_vertex_buffer;
    SDL_GPUBuffer *terrain_index_buffer;
    SDL_GPUBuffer *tree_vertex_buffer;
    SDL_GPUBuffer *tree_index_buffer;
    SDL_GPUBuffer *quad_vertex_buffer;
    SDL_GPUBuffer *quad_index_buffer;
    SDL_GPUBuffer *near_instance_buffer;
    SDL_GPUTransferBuffer *near_instance_upload;
    SDL_GPUBuffer *far_instance_buffer;
    SDL_GPUTransferBuffer *far_instance_upload;
    SDL_GPUTexture *heightmap_texture;
    SDL_GPUSampler *heightmap_sampler;
    SDL_GPUTexture *atlas_texture;
    SDL_GPUSampler *atlas_sampler;
    float *heightmap;
    Lesson49TreePosition *tree_positions;
    Lesson49TreeInstance *near_instances;
    Lesson49ImposterInstance *far_instances;
    Uint32 terrain_index_count;
    Uint32 tree_index_count;
    Uint32 trunk_index_count;
    Uint32 tree_count;
    Uint32 near_count;
    Uint32 far_count;
    Uint32 terrain_draw_calls;
    Uint32 near_draw_calls;
    Uint32 far_draw_calls;
    Uint32 shadow_draw_calls;
    float lod_dist;
    Lesson49DiagnosticMode diagnostic_mode;
    bool atlas_baked;
} Lesson49State;

static_assert(sizeof(Lesson49TerrainVertex) == 20, "lesson 49 terrain vertex size must match HLSL layout");
static_assert(sizeof(Lesson49TreeVertex) == 24, "lesson 49 tree vertex size must match HLSL layout");
static_assert(sizeof(Lesson49TreeInstance) == 80, "lesson 49 tree instance size must match HLSL layout");
static_assert(sizeof(Lesson49ImposterInstance) == 96, "lesson 49 imposter instance size must match HLSL layout");
static_assert(sizeof(Lesson49TerrainVertUniforms) == 144, "lesson 49 terrain vertex uniform size must match HLSL layout");
static_assert(sizeof(Lesson49TerrainShadowVertUniforms) == 80, "lesson 49 terrain shadow uniform size must match HLSL layout");
static_assert(sizeof(Lesson49TerrainFragUniforms) == 64, "lesson 49 terrain fragment uniform size must match HLSL layout");
static_assert(sizeof(Lesson49InstancedVertUniforms) == 192, "lesson 49 tree vertex uniform size must match HLSL layout");
static_assert(sizeof(Lesson49InstancedShadowVertUniforms) == 128, "lesson 49 tree shadow uniform size must match HLSL layout");
static_assert(sizeof(Lesson49TreeFragUniforms) == 80, "lesson 49 tree fragment uniform size must match HLSL layout");
static_assert(sizeof(Lesson49BakeVertUniforms) == 64, "lesson 49 bake vertex uniform size must match HLSL layout");
static_assert(sizeof(Lesson49BakeFragUniforms) == 32, "lesson 49 bake fragment uniform size must match HLSL layout");
static_assert(sizeof(Lesson49ImposterVertUniforms) == 80, "lesson 49 imposter vertex uniform size must match HLSL layout");

static Lesson49State *lesson49_state(ForgeGpuDemo *demo)
{
    return (Lesson49State *)demo->lesson.private_state;
}

static bool lesson49_draws_terrain(const Lesson49State *state)
{
    return state->diagnostic_mode != LESSON49_DIAGNOSTIC_NEAR_TREES_ONLY &&
        state->diagnostic_mode != LESSON49_DIAGNOSTIC_IMPOSTERS_ONLY;
}

static bool lesson49_draws_near_trees(const Lesson49State *state)
{
    return state->diagnostic_mode != LESSON49_DIAGNOSTIC_TERRAIN_ONLY &&
        state->diagnostic_mode != LESSON49_DIAGNOSTIC_IMPOSTERS_ONLY;
}

static bool lesson49_draws_imposters(const Lesson49State *state)
{
    return state->diagnostic_mode != LESSON49_DIAGNOSTIC_TERRAIN_ONLY &&
        state->diagnostic_mode != LESSON49_DIAGNOSTIC_NEAR_TREES_ONLY;
}

static bool lesson49_draws_shadow(const Lesson49State *state)
{
    return state->diagnostic_mode != LESSON49_DIAGNOSTIC_NO_SHADOW &&
        state->diagnostic_mode != LESSON49_DIAGNOSTIC_IMPOSTERS_ONLY;
}

static void lesson49_release_shader(SDL_GPUDevice *device, SDL_GPUShader **shader)
{
    if (*shader) {
        SDL_ReleaseGPUShader(device, *shader);
        *shader = nullptr;
    }
}

static float lesson49_slope_at(const float *hmap, int size, int x, int y)
{
    const int x0 = x > 0 ? x - 1 : 0;
    const int x1 = x < size - 1 ? x + 1 : size - 1;
    const int y0 = y > 0 ? y - 1 : 0;
    const int y1 = y < size - 1 ? y + 1 : size - 1;
    const float dx = (hmap[y * size + x1] - hmap[y * size + x0]) * 0.5f;
    const float dy = (hmap[y1 * size + x] - hmap[y0 * size + x]) * 0.5f;
    return SDL_sqrtf(dx * dx + dy * dy);
}

static Uint32 lesson49_place_trees(
    const float *hmap,
    int size,
    Lesson49TreePosition *out,
    Uint32 max_count)
{
    Uint32 count = 0;
    int step = (int)SDL_sqrtf((float)(size * size) / (float)max_count);

    if (step < 1) {
        step = 1;
    }
    for (int fy = step / 2; fy < size && count < max_count; fy += step) {
        for (int fx = step / 2; fx < size && count < max_count; fx += step) {
            const float slope = lesson49_slope_at(hmap, size, fx, fy);
            if (slope > LESSON49_SLOPE_TREE_THRESHOLD) {
                continue;
            }

            const Uint32 hash = (Uint32)(fx * 73856093u) ^ (Uint32)(fy * 19349663u);
            const float jx = ((float)(hash & 0xffu) / 255.0f - 0.5f) * (float)step;
            const float jy = ((float)((hash >> 8) & 0xffu) / 255.0f - 0.5f) * (float)step;
            const int sx = fx + (int)jx;
            const int sy = fy + (int)jy;

            if (sx < 0 || sx >= size || sy < 0 || sy >= size) {
                continue;
            }
            if (lesson49_slope_at(hmap, size, sx, sy) > LESSON49_SLOPE_TREE_THRESHOLD) {
                continue;
            }

            const float wx = ((float)sx / (float)(size - 1)) * 2.0f * LESSON49_TERRAIN_SIZE - LESSON49_TERRAIN_SIZE;
            const float wz = ((float)sy / (float)(size - 1)) * 2.0f * LESSON49_TERRAIN_SIZE - LESSON49_TERRAIN_SIZE;
            const float wy = hmap[sy * size + sx] * LESSON49_TERRAIN_HEIGHT_SCALE;
            const float scale_var = LESSON49_TREE_SCALE_MIN + ((float)((hash >> 12) & 0xffu) / 255.0f) * LESSON49_TREE_SCALE_RANGE;
            const float t = (float)((hash >> 16) & 0xffu) / 255.0f;
            float r;
            float g;
            float b;

            if (t < 0.33f) {
                const float f = t / 0.33f;
                r = 0.15f + f * 0.35f;
                g = 0.45f + f * 0.15f;
                b = 0.10f - f * 0.05f;
            } else if (t < 0.66f) {
                const float f = (t - 0.33f) / 0.33f;
                r = 0.50f + f * 0.35f;
                g = 0.60f - f * 0.15f;
                b = 0.05f;
            } else {
                const float f = (t - 0.66f) / 0.34f;
                r = 0.85f - f * 0.15f;
                g = 0.45f - f * 0.25f;
                b = 0.05f + f * 0.02f;
            }

            out[count].world_pos = { wx, wy, wz };
            out[count].scale = scale_var;
            out[count].color[0] = r;
            out[count].color[1] = g;
            out[count].color[2] = b;
            count += 1;
        }
    }
    return count;
}

static void lesson49_classify_instances(Lesson49State *state, Vec3 camera_pos)
{
    const float near_dist = SDL_max(state->lod_dist - LESSON49_LOD_FADE_BAND, LESSON49_LOD_NEAR_CLAMP);
    const float far_dist = state->lod_dist + LESSON49_LOD_FADE_BAND;
    const float fade_range = SDL_max(far_dist - near_dist, LESSON49_LOD_FADE_RANGE_MIN);
    const float cell_u = 1.0f / (float)LESSON49_ATLAS_COLS;
    const float cell_v = 1.0f / (float)LESSON49_ATLAS_ROWS;

    state->near_count = 0;
    state->far_count = 0;
    for (Uint32 i = 0; i < state->tree_count; i += 1) {
        const Lesson49TreePosition *tree = &state->tree_positions[i];
        const Vec3 diff = vec3_sub(camera_pos, tree->world_pos);
        const float dist = SDL_sqrtf(vec3_dot(diff, diff));
        const Mat4 transform = mat4_multiply(
            mat4_translate(tree->world_pos),
            mat4_scale(tree->scale));

        if (dist < far_dist && state->near_count < LESSON49_TREE_COUNT_MAX) {
            Lesson49TreeInstance *near_instance = &state->near_instances[state->near_count++];
            near_instance->transform = transform;
            near_instance->color[0] = tree->color[0];
            near_instance->color[1] = tree->color[1];
            near_instance->color[2] = tree->color[2];
            near_instance->color[3] = 1.0f;
        }
        if (dist > near_dist && state->far_count < LESSON49_TREE_COUNT_MAX) {
            float alpha = 1.0f;
            float angle;
            float frame_f;
            int frame;
            int col;
            int row;
            Lesson49ImposterInstance *far_instance = &state->far_instances[state->far_count++];

            if (dist < far_dist) {
                alpha = (dist - near_dist) / fade_range;
            }
            angle = SDL_atan2f(diff.x, diff.z);
            if (angle < 0.0f) {
                angle += LESSON49_TAU;
            }
            frame_f = angle / LESSON49_TAU * (float)LESSON49_ATLAS_FRAMES;
            frame = ((int)(frame_f + 0.5f)) % LESSON49_ATLAS_FRAMES;
            col = frame % LESSON49_ATLAS_COLS;
            row = frame / LESSON49_ATLAS_COLS;

            far_instance->transform = transform;
            far_instance->atlas_uv[0] = (float)col * cell_u;
            far_instance->atlas_uv[1] = (float)row * cell_v;
            far_instance->atlas_uv[2] = cell_u;
            far_instance->atlas_uv[3] = cell_v;
            far_instance->alpha = alpha;
            far_instance->color[0] = tree->color[0];
            far_instance->color[1] = tree->color[1];
            far_instance->color[2] = tree->color[2];
        }
    }
}

static bool lesson49_create_terrain_mesh(ForgeGpuDemo *demo, Lesson49State *state)
{
    ForgeGpuShapeMesh plane;
    Lesson49TerrainVertex *vertices = nullptr;
    Uint16 *indices = nullptr;
    bool ok = false;

    SDL_zero(plane);
    if (!ForgeGpuCreatePlaneShapeMesh(LESSON49_HEIGHTMAP_SIZE - 1, LESSON49_HEIGHTMAP_SIZE - 1, &plane)) {
        return false;
    }
    if ((Uint32)plane.vertex_count > (Uint32)SDL_MAX_UINT16 + 1u) {
        SDL_SetError("lesson 49 terrain has too many vertices for 16-bit indices");
        goto done;
    }

    vertices = (Lesson49TerrainVertex *)SDL_calloc((size_t)plane.vertex_count, sizeof(*vertices));
    indices = (Uint16 *)SDL_calloc((size_t)plane.index_count, sizeof(*indices));
    if (!vertices || !indices) {
        SDL_OutOfMemory();
        goto done;
    }

    for (int i = 0; i < plane.vertex_count; i += 1) {
        vertices[i].position[0] = plane.positions[i * 3 + 0];
        vertices[i].position[1] = plane.positions[i * 3 + 1];
        vertices[i].position[2] = plane.positions[i * 3 + 2];
        vertices[i].uv[0] = plane.uvs[i * 2 + 0];
        vertices[i].uv[1] = plane.uvs[i * 2 + 1];
    }
    for (int i = 0; i < plane.index_count; i += 1) {
        indices[i] = (Uint16)plane.indices[i];
    }

    state->terrain_vertex_buffer = ForgeGpuCreateBufferWithData(
        demo->device,
        SDL_GPU_BUFFERUSAGE_VERTEX,
        vertices,
        (Uint32)((size_t)plane.vertex_count * sizeof(*vertices)));
    state->terrain_index_buffer = ForgeGpuCreateBufferWithData(
        demo->device,
        SDL_GPU_BUFFERUSAGE_INDEX,
        indices,
        (Uint32)((size_t)plane.index_count * sizeof(*indices)));
    if (!state->terrain_vertex_buffer || !state->terrain_index_buffer) {
        goto done;
    }
    state->terrain_index_count = (Uint32)plane.index_count;
    ok = true;

done:
    SDL_free(vertices);
    SDL_free(indices);
    ForgeGpuFreeShapeMesh(&plane);
    return ok;
}

static void lesson49_copy_tree_shape_vertices(
    Lesson49TreeVertex *vertices,
    const ForgeGpuShapeMesh *shape,
    Uint32 dst_offset,
    Vec3 scale,
    float y_offset)
{
    for (int i = 0; i < shape->vertex_count; i += 1) {
        const int src3 = i * 3;
        Lesson49TreeVertex *dst = &vertices[dst_offset + (Uint32)i];
        Vec3 normal = {
            shape->normals[src3 + 0] * scale.x,
            shape->normals[src3 + 1],
            shape->normals[src3 + 2] * scale.z
        };

        dst->position[0] = shape->positions[src3 + 0] * scale.x;
        dst->position[1] = shape->positions[src3 + 1] * scale.y + y_offset;
        dst->position[2] = shape->positions[src3 + 2] * scale.z;
        normal = vec3_normalize(normal);
        dst->normal[0] = normal.x;
        dst->normal[1] = normal.y;
        dst->normal[2] = normal.z;
    }
}

static bool lesson49_create_tree_mesh(ForgeGpuDemo *demo, Lesson49State *state)
{
    ForgeGpuShapeMesh trunk;
    ForgeGpuShapeMesh crown;
    Lesson49TreeVertex *vertices = nullptr;
    Uint32 *indices = nullptr;
    Uint32 total_vertex_count;
    Uint32 total_index_count;
    bool ok = false;

    SDL_zero(trunk);
    SDL_zero(crown);
    if (!ForgeGpuCreateCylinderShapeMesh(LESSON49_TREE_TRUNK_SIDES, LESSON49_TREE_TRUNK_SEGS, &trunk) ||
        !ForgeGpuCreateConeShapeMesh(LESSON49_TREE_CROWN_SLICES, LESSON49_TREE_CROWN_SEGS, &crown)) {
        goto done;
    }

    total_vertex_count = (Uint32)(trunk.vertex_count + crown.vertex_count);
    total_index_count = (Uint32)(trunk.index_count + crown.index_count);
    vertices = (Lesson49TreeVertex *)SDL_calloc(total_vertex_count, sizeof(*vertices));
    indices = (Uint32 *)SDL_calloc(total_index_count, sizeof(*indices));
    if (!vertices || !indices) {
        SDL_OutOfMemory();
        goto done;
    }

    lesson49_copy_tree_shape_vertices(
        vertices,
        &trunk,
        0,
        { LESSON49_TREE_TRUNK_RADIUS, LESSON49_TREE_TRUNK_HEIGHT * 0.5f, LESSON49_TREE_TRUNK_RADIUS },
        LESSON49_TREE_TRUNK_HEIGHT * 0.5f);
    lesson49_copy_tree_shape_vertices(
        vertices,
        &crown,
        (Uint32)trunk.vertex_count,
        { LESSON49_TREE_CROWN_RADIUS, LESSON49_TREE_CROWN_HEIGHT * 0.5f, LESSON49_TREE_CROWN_RADIUS },
        LESSON49_TREE_TRUNK_HEIGHT + LESSON49_TREE_CROWN_HEIGHT * 0.5f);

    for (int i = 0; i < trunk.index_count; i += 1) {
        indices[i] = trunk.indices[i];
    }
    for (int i = 0; i < crown.index_count; i += 1) {
        indices[trunk.index_count + i] = crown.indices[i] + (Uint32)trunk.vertex_count;
    }

    state->tree_vertex_buffer = ForgeGpuCreateBufferWithData(
        demo->device,
        SDL_GPU_BUFFERUSAGE_VERTEX,
        vertices,
        (Uint32)((size_t)total_vertex_count * sizeof(*vertices)));
    state->tree_index_buffer = ForgeGpuCreateBufferWithData(
        demo->device,
        SDL_GPU_BUFFERUSAGE_INDEX,
        indices,
        (Uint32)((size_t)total_index_count * sizeof(*indices)));
    if (!state->tree_vertex_buffer || !state->tree_index_buffer) {
        goto done;
    }

    state->trunk_index_count = (Uint32)trunk.index_count;
    state->tree_index_count = total_index_count;
    ok = true;

done:
    SDL_free(vertices);
    SDL_free(indices);
    ForgeGpuFreeShapeMesh(&trunk);
    ForgeGpuFreeShapeMesh(&crown);
    return ok;
}

static bool lesson49_create_quad_mesh(ForgeGpuDemo *demo, Lesson49State *state)
{
    const Lesson49TreeVertex vertices[4] = {
        { { -0.5f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } },
        { { 0.5f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } },
        { { 0.5f, 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } },
        { { -0.5f, 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } },
    };
    const Uint32 indices[6] = { 0, 1, 2, 0, 2, 3 };

    state->quad_vertex_buffer = ForgeGpuCreateBufferWithData(
        demo->device,
        SDL_GPU_BUFFERUSAGE_VERTEX,
        vertices,
        sizeof(vertices));
    state->quad_index_buffer = ForgeGpuCreateBufferWithData(
        demo->device,
        SDL_GPU_BUFFERUSAGE_INDEX,
        indices,
        sizeof(indices));
    return state->quad_vertex_buffer && state->quad_index_buffer;
}

static bool lesson49_create_instance_buffers(ForgeGpuDemo *demo, Lesson49State *state)
{
    SDL_GPUBufferCreateInfo buffer_info;
    SDL_GPUTransferBufferCreateInfo transfer_info;

    SDL_zero(buffer_info);
    buffer_info.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
    buffer_info.size = (Uint32)(LESSON49_TREE_COUNT_MAX * sizeof(Lesson49TreeInstance));
    state->near_instance_buffer = SDL_CreateGPUBuffer(demo->device, &buffer_info);

    SDL_zero(transfer_info);
    transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transfer_info.size = buffer_info.size;
    state->near_instance_upload = SDL_CreateGPUTransferBuffer(demo->device, &transfer_info);

    buffer_info.size = (Uint32)(LESSON49_TREE_COUNT_MAX * sizeof(Lesson49ImposterInstance));
    state->far_instance_buffer = SDL_CreateGPUBuffer(demo->device, &buffer_info);
    transfer_info.size = buffer_info.size;
    state->far_instance_upload = SDL_CreateGPUTransferBuffer(demo->device, &transfer_info);

    return state->near_instance_buffer &&
        state->near_instance_upload &&
        state->far_instance_buffer &&
        state->far_instance_upload;
}

static void lesson49_fill_terrain_vertex_input(
    SDL_GPUVertexBufferDescription *vertex_buffer,
    SDL_GPUVertexAttribute attributes[2])
{
    SDL_zero(*vertex_buffer);
    vertex_buffer->slot = 0;
    vertex_buffer->pitch = sizeof(Lesson49TerrainVertex);
    vertex_buffer->input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

    SDL_memset(attributes, 0, sizeof(*attributes) * 2);
    attributes[0].location = 0;
    attributes[0].buffer_slot = 0;
    attributes[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    attributes[0].offset = offsetof(Lesson49TerrainVertex, position);
    attributes[1].location = 1;
    attributes[1].buffer_slot = 0;
    attributes[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    attributes[1].offset = offsetof(Lesson49TerrainVertex, uv);
}

static void lesson49_fill_tree_vertex_input(
    SDL_GPUVertexBufferDescription vertex_buffers[2],
    SDL_GPUVertexAttribute attributes[7])
{
    SDL_memset(vertex_buffers, 0, sizeof(*vertex_buffers) * 2);
    vertex_buffers[0].slot = 0;
    vertex_buffers[0].pitch = sizeof(Lesson49TreeVertex);
    vertex_buffers[0].input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    vertex_buffers[1].slot = 1;
    vertex_buffers[1].pitch = sizeof(Lesson49TreeInstance);
    vertex_buffers[1].input_rate = SDL_GPU_VERTEXINPUTRATE_INSTANCE;

    SDL_memset(attributes, 0, sizeof(*attributes) * 7);
    attributes[0].location = 0;
    attributes[0].buffer_slot = 0;
    attributes[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    attributes[0].offset = offsetof(Lesson49TreeVertex, position);
    attributes[1].location = 1;
    attributes[1].buffer_slot = 0;
    attributes[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    attributes[1].offset = offsetof(Lesson49TreeVertex, normal);
    for (int i = 0; i < 4; i += 1) {
        attributes[2 + i].location = (Uint32)(2 + i);
        attributes[2 + i].buffer_slot = 1;
        attributes[2 + i].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
        attributes[2 + i].offset = (Uint32)(sizeof(float) * 4 * i);
    }
    attributes[6].location = 6;
    attributes[6].buffer_slot = 1;
    attributes[6].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
    attributes[6].offset = offsetof(Lesson49TreeInstance, color);
}

static void lesson49_fill_tree_shadow_vertex_input(
    SDL_GPUVertexBufferDescription vertex_buffers[2],
    SDL_GPUVertexAttribute attributes[5])
{
    SDL_memset(vertex_buffers, 0, sizeof(*vertex_buffers) * 2);
    vertex_buffers[0].slot = 0;
    vertex_buffers[0].pitch = sizeof(Lesson49TreeVertex);
    vertex_buffers[0].input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    vertex_buffers[1].slot = 1;
    vertex_buffers[1].pitch = sizeof(Lesson49TreeInstance);
    vertex_buffers[1].input_rate = SDL_GPU_VERTEXINPUTRATE_INSTANCE;

    SDL_memset(attributes, 0, sizeof(*attributes) * 5);
    attributes[0].location = 0;
    attributes[0].buffer_slot = 0;
    attributes[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    attributes[0].offset = offsetof(Lesson49TreeVertex, position);
    for (int i = 0; i < 4; i += 1) {
        attributes[1 + i].location = (Uint32)(1 + i);
        attributes[1 + i].buffer_slot = 1;
        attributes[1 + i].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
        attributes[1 + i].offset = (Uint32)(sizeof(float) * 4 * i);
    }
}

static void lesson49_fill_imposter_vertex_input(
    SDL_GPUVertexBufferDescription vertex_buffers[2],
    SDL_GPUVertexAttribute attributes[8])
{
    SDL_memset(vertex_buffers, 0, sizeof(*vertex_buffers) * 2);
    vertex_buffers[0].slot = 0;
    vertex_buffers[0].pitch = sizeof(Lesson49TreeVertex);
    vertex_buffers[0].input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    vertex_buffers[1].slot = 1;
    vertex_buffers[1].pitch = sizeof(Lesson49ImposterInstance);
    vertex_buffers[1].input_rate = SDL_GPU_VERTEXINPUTRATE_INSTANCE;

    SDL_memset(attributes, 0, sizeof(*attributes) * 8);
    attributes[0].location = 0;
    attributes[0].buffer_slot = 0;
    attributes[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    attributes[0].offset = offsetof(Lesson49TreeVertex, position);
    attributes[1].location = 1;
    attributes[1].buffer_slot = 0;
    attributes[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    attributes[1].offset = offsetof(Lesson49TreeVertex, normal);
    for (int i = 0; i < 4; i += 1) {
        attributes[2 + i].location = (Uint32)(2 + i);
        attributes[2 + i].buffer_slot = 1;
        attributes[2 + i].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
        attributes[2 + i].offset = (Uint32)(sizeof(float) * 4 * i);
    }
    attributes[6].location = 6;
    attributes[6].buffer_slot = 1;
    attributes[6].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
    attributes[6].offset = offsetof(Lesson49ImposterInstance, atlas_uv);
    attributes[7].location = 7;
    attributes[7].buffer_slot = 1;
    attributes[7].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
    attributes[7].offset = offsetof(Lesson49ImposterInstance, alpha);
}

static bool lesson49_create_scene_pipelines(ForgeGpuDemo *demo, Lesson49State *state)
{
    SDL_GPUShader *terrain_vs = nullptr;
    SDL_GPUShader *terrain_fs = nullptr;
    SDL_GPUShader *terrain_no_variation_fs = nullptr;
    SDL_GPUShader *terrain_shadow_vs = nullptr;
    SDL_GPUShader *tree_vs = nullptr;
    SDL_GPUShader *tree_fs = nullptr;
    SDL_GPUShader *tree_shadow_vs = nullptr;
    SDL_GPUShader *shadow_fs = nullptr;
    SDL_GPUColorTargetDescription color_target;
    SDL_GPUVertexBufferDescription terrain_vb;
    SDL_GPUVertexAttribute terrain_attrs[2];
    SDL_GPUVertexBufferDescription tree_vbs[2];
    SDL_GPUVertexAttribute tree_attrs[7];
    SDL_GPUVertexBufferDescription tree_shadow_vbs[2];
    SDL_GPUVertexAttribute tree_shadow_attrs[5];
    bool ok = false;

    SDL_zero(color_target);
    color_target.format = demo->color_format;

    terrain_vs = ForgeGpuCreateShaderWithResourceLayout(
        demo->device,
        forge_scene_terrain_vert_wgsl,
        forge_scene_terrain_vert_wgsl_size,
        forge_scene_terrain_vert_msl,
        forge_scene_terrain_vert_msl_size,
        ForgeGpuShaderLayout_forge_scene_terrain_vert());
    terrain_fs = ForgeGpuCreateShaderWithResourceLayout(
        demo->device,
        forge_scene_terrain_frag_wgsl,
        forge_scene_terrain_frag_wgsl_size,
        forge_scene_terrain_frag_msl,
        forge_scene_terrain_frag_msl_size,
        ForgeGpuShaderLayout_forge_scene_terrain_frag());
    terrain_no_variation_fs = ForgeGpuCreateShaderWithResourceLayout(
        demo->device,
        forge_scene_terrain_no_variation_frag_wgsl,
        forge_scene_terrain_no_variation_frag_wgsl_size,
        forge_scene_terrain_no_variation_frag_msl,
        forge_scene_terrain_no_variation_frag_msl_size,
        ForgeGpuShaderLayout_forge_scene_terrain_no_variation_frag());
    terrain_shadow_vs = ForgeGpuCreateShaderWithResourceLayout(
        demo->device,
        forge_scene_terrain_shadow_vert_wgsl,
        forge_scene_terrain_shadow_vert_wgsl_size,
        forge_scene_terrain_shadow_vert_msl,
        forge_scene_terrain_shadow_vert_msl_size,
        ForgeGpuShaderLayout_forge_scene_terrain_shadow_vert());
    tree_vs = ForgeGpuCreateShader(
        demo->device,
        SDL_GPU_SHADERSTAGE_VERTEX,
        forge_scene_tree_colored_vert_wgsl,
        forge_scene_tree_colored_vert_wgsl_size,
        forge_scene_tree_colored_vert_msl,
        forge_scene_tree_colored_vert_msl_size,
        0, 0, 0, 1);
    tree_fs = ForgeGpuCreateShaderWithResourceLayout(
        demo->device,
        forge_scene_tree_colored_frag_wgsl,
        forge_scene_tree_colored_frag_wgsl_size,
        forge_scene_tree_colored_frag_msl,
        forge_scene_tree_colored_frag_msl_size,
        ForgeGpuShaderLayout_forge_scene_tree_colored_frag());
    tree_shadow_vs = ForgeGpuCreateShader(
        demo->device,
        SDL_GPU_SHADERSTAGE_VERTEX,
        forge_scene_tree_shadow_vert_wgsl,
        forge_scene_tree_shadow_vert_wgsl_size,
        forge_scene_tree_shadow_vert_msl,
        forge_scene_tree_shadow_vert_msl_size,
        0, 0, 0, 1);
    shadow_fs = ForgeGpuCreateShader(
        demo->device,
        SDL_GPU_SHADERSTAGE_FRAGMENT,
        forge_scene_shadow_frag_wgsl,
        forge_scene_shadow_frag_wgsl_size,
        forge_scene_shadow_frag_msl,
        forge_scene_shadow_frag_msl_size,
        0, 0, 0, 0);
    if (!terrain_vs || !terrain_fs || !terrain_no_variation_fs || !terrain_shadow_vs || !tree_vs || !tree_fs || !tree_shadow_vs || !shadow_fs) {
        goto done;
    }

    lesson49_fill_terrain_vertex_input(&terrain_vb, terrain_attrs);
    state->terrain_pipeline = ForgeGpuCreateLessonGraphicsPipelineWithColorTargetsAndDepthCompare(
        demo, terrain_vs, terrain_fs, SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        &color_target, 1, &terrain_vb, 1, terrain_attrs, SDL_arraysize(terrain_attrs),
        true, FORGE_GPU_PROCESSED_SCENE_DEPTH_FORMAT, true, true, SDL_GPU_COMPAREOP_LESS,
        SDL_GPU_CULLMODE_BACK, 0.0f, 0.0f);
    state->terrain_no_variation_pipeline = ForgeGpuCreateLessonGraphicsPipelineWithColorTargetsAndDepthCompare(
        demo, terrain_vs, terrain_no_variation_fs, SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        &color_target, 1, &terrain_vb, 1, terrain_attrs, SDL_arraysize(terrain_attrs),
        true, FORGE_GPU_PROCESSED_SCENE_DEPTH_FORMAT, true, true, SDL_GPU_COMPAREOP_LESS,
        SDL_GPU_CULLMODE_BACK, 0.0f, 0.0f);
    state->terrain_shadow_pipeline = ForgeGpuCreateLessonGraphicsPipelineWithColorTargetsAndDepthCompare(
        demo, terrain_shadow_vs, shadow_fs, SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        nullptr, 0, &terrain_vb, 1, terrain_attrs, SDL_arraysize(terrain_attrs),
        true, FORGE_GPU_PROCESSED_SCENE_DEPTH_FORMAT, true, true, SDL_GPU_COMPAREOP_LESS,
        SDL_GPU_CULLMODE_BACK, LESSON49_SHADOW_BIAS_CONST, LESSON49_SHADOW_BIAS_SLOPE);

    lesson49_fill_tree_vertex_input(tree_vbs, tree_attrs);
    state->tree_pipeline = ForgeGpuCreateLessonGraphicsPipelineWithColorTargetsAndDepthCompare(
        demo, tree_vs, tree_fs, SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        &color_target, 1, tree_vbs, SDL_arraysize(tree_vbs), tree_attrs, SDL_arraysize(tree_attrs),
        true, FORGE_GPU_PROCESSED_SCENE_DEPTH_FORMAT, true, true, SDL_GPU_COMPAREOP_LESS,
        SDL_GPU_CULLMODE_BACK, 0.0f, 0.0f);

    lesson49_fill_tree_shadow_vertex_input(tree_shadow_vbs, tree_shadow_attrs);
    state->tree_shadow_pipeline = ForgeGpuCreateLessonGraphicsPipelineWithColorTargetsAndDepthCompare(
        demo, tree_shadow_vs, shadow_fs, SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        nullptr, 0, tree_shadow_vbs, SDL_arraysize(tree_shadow_vbs), tree_shadow_attrs, SDL_arraysize(tree_shadow_attrs),
        true, FORGE_GPU_PROCESSED_SCENE_DEPTH_FORMAT, true, true, SDL_GPU_COMPAREOP_LESS,
        SDL_GPU_CULLMODE_BACK, LESSON49_SHADOW_BIAS_CONST, LESSON49_SHADOW_BIAS_SLOPE);

    ok = state->terrain_pipeline && state->terrain_no_variation_pipeline && state->terrain_shadow_pipeline && state->tree_pipeline && state->tree_shadow_pipeline;

done:
    lesson49_release_shader(demo->device, &terrain_vs);
    lesson49_release_shader(demo->device, &terrain_fs);
    lesson49_release_shader(demo->device, &terrain_no_variation_fs);
    lesson49_release_shader(demo->device, &terrain_shadow_vs);
    lesson49_release_shader(demo->device, &tree_vs);
    lesson49_release_shader(demo->device, &tree_fs);
    lesson49_release_shader(demo->device, &tree_shadow_vs);
    lesson49_release_shader(demo->device, &shadow_fs);
    return ok;
}

static bool lesson49_create_imposter_pipeline(ForgeGpuDemo *demo, Lesson49State *state)
{
    SDL_GPUShader *vertex_shader = nullptr;
    SDL_GPUShader *fragment_shader = nullptr;
    SDL_GPUVertexBufferDescription vertex_buffers[2];
    SDL_GPUVertexAttribute attributes[8];
    SDL_GPUColorTargetBlendState blend;
    SDL_GPUColorTargetDescription color_target;
    SDL_GPUGraphicsPipelineCreateInfo pipeline_info;
    bool ok = false;

    vertex_shader = ForgeGpuCreateShader(
        demo->device,
        SDL_GPU_SHADERSTAGE_VERTEX,
        lesson49_imposter_vert_wgsl,
        lesson49_imposter_vert_wgsl_size,
        lesson49_imposter_vert_msl,
        lesson49_imposter_vert_msl_size,
        0, 0, 0, 1);
    fragment_shader = ForgeGpuCreateShaderWithResourceLayout(
        demo->device,
        lesson49_imposter_frag_wgsl,
        lesson49_imposter_frag_wgsl_size,
        lesson49_imposter_frag_msl,
        lesson49_imposter_frag_msl_size,
        ForgeGpuShaderLayout_lesson49_imposter_frag());
    if (!vertex_shader || !fragment_shader) {
        goto done;
    }

    lesson49_fill_imposter_vertex_input(vertex_buffers, attributes);

    SDL_zero(blend);
    blend.enable_blend = true;
    blend.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    blend.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    blend.color_blend_op = SDL_GPU_BLENDOP_ADD;
    blend.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    blend.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    blend.alpha_blend_op = SDL_GPU_BLENDOP_ADD;

    SDL_zero(color_target);
    color_target.format = demo->color_format;
    color_target.blend_state = blend;

    SDL_zero(pipeline_info);
    pipeline_info.vertex_shader = vertex_shader;
    pipeline_info.fragment_shader = fragment_shader;
    pipeline_info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pipeline_info.vertex_input_state.vertex_buffer_descriptions = vertex_buffers;
    pipeline_info.vertex_input_state.num_vertex_buffers = SDL_arraysize(vertex_buffers);
    pipeline_info.vertex_input_state.vertex_attributes = attributes;
    pipeline_info.vertex_input_state.num_vertex_attributes = SDL_arraysize(attributes);
    pipeline_info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    pipeline_info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    pipeline_info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    pipeline_info.depth_stencil_state.enable_depth_test = true;
    pipeline_info.depth_stencil_state.enable_depth_write = false;
    pipeline_info.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS;
    pipeline_info.target_info.color_target_descriptions = &color_target;
    pipeline_info.target_info.num_color_targets = 1;
    pipeline_info.target_info.has_depth_stencil_target = true;
    pipeline_info.target_info.depth_stencil_format = FORGE_GPU_PROCESSED_SCENE_DEPTH_FORMAT;

    state->imposter_pipeline = SDL_CreateGPUGraphicsPipeline(demo->device, &pipeline_info);
    ok = state->imposter_pipeline != nullptr;

done:
    lesson49_release_shader(demo->device, &vertex_shader);
    lesson49_release_shader(demo->device, &fragment_shader);
    return ok;
}

static bool lesson49_create_samplers(ForgeGpuDemo *demo, Lesson49State *state)
{
    state->heightmap_sampler = ForgeGpuCreateSamplerWithAddress(
        demo->device,
        SDL_GPU_FILTER_LINEAR,
        SDL_GPU_FILTER_LINEAR,
        SDL_GPU_SAMPLERMIPMAPMODE_NEAREST,
        SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        0.0f);
    state->atlas_sampler = ForgeGpuCreateSamplerWithAddress(
        demo->device,
        SDL_GPU_FILTER_LINEAR,
        SDL_GPU_FILTER_LINEAR,
        SDL_GPU_SAMPLERMIPMAPMODE_NEAREST,
        SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        0.0f);
    return state->heightmap_sampler && state->atlas_sampler;
}

static bool lesson49_create_atlas_texture(ForgeGpuDemo *demo, Lesson49State *state)
{
    SDL_GPUTextureCreateInfo texture_info;

    SDL_zero(texture_info);
    texture_info.type = SDL_GPU_TEXTURETYPE_2D;
    texture_info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    texture_info.width = LESSON49_ATLAS_WIDTH;
    texture_info.height = LESSON49_ATLAS_HEIGHT;
    texture_info.layer_count_or_depth = 1;
    texture_info.num_levels = 1;
    texture_info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    state->atlas_texture = SDL_CreateGPUTexture(demo->device, &texture_info);
    return state->atlas_texture != nullptr;
}

static bool lesson49_bake_imposter_atlas(ForgeGpuDemo *demo, Lesson49State *state)
{
    SDL_GPUTexture *bake_depth = nullptr;
    SDL_GPUGraphicsPipeline *bake_pipeline = nullptr;
    SDL_GPUShader *bake_vs = nullptr;
    SDL_GPUShader *bake_fs = nullptr;
    SDL_GPUTextureCreateInfo depth_info;
    SDL_GPUVertexBufferDescription vertex_buffer;
    SDL_GPUVertexAttribute attributes[2];
    SDL_GPUColorTargetDescription color_target;
    SDL_GPUGraphicsPipelineCreateInfo pipeline_info;
    SDL_GPUCommandBuffer *command_buffer = nullptr;
    SDL_GPURenderPass *render_pass = nullptr;
    SDL_GPUColorTargetInfo color_info;
    SDL_GPUDepthStencilTargetInfo depth_target_info;
    float half_w;
    float half_h;
    Mat4 projection;
    Vec3 tree_center;
    Vec3 up;
    Vec3 bake_light;
    bool ok = false;

    SDL_zero(depth_info);
    depth_info.type = SDL_GPU_TEXTURETYPE_2D;
    depth_info.format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    depth_info.width = LESSON49_ATLAS_WIDTH;
    depth_info.height = LESSON49_ATLAS_HEIGHT;
    depth_info.layer_count_or_depth = 1;
    depth_info.num_levels = 1;
    depth_info.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
    bake_depth = SDL_CreateGPUTexture(demo->device, &depth_info);
    if (!bake_depth) {
        return false;
    }

    bake_vs = ForgeGpuCreateShader(
        demo->device,
        SDL_GPU_SHADERSTAGE_VERTEX,
        lesson49_imposter_bake_vert_wgsl,
        lesson49_imposter_bake_vert_wgsl_size,
        lesson49_imposter_bake_vert_msl,
        lesson49_imposter_bake_vert_msl_size,
        0, 0, 0, 1);
    bake_fs = ForgeGpuCreateShader(
        demo->device,
        SDL_GPU_SHADERSTAGE_FRAGMENT,
        lesson49_imposter_bake_frag_wgsl,
        lesson49_imposter_bake_frag_wgsl_size,
        lesson49_imposter_bake_frag_msl,
        lesson49_imposter_bake_frag_msl_size,
        0, 0, 0, 1);
    if (!bake_vs || !bake_fs) {
        goto done;
    }

    SDL_zero(vertex_buffer);
    vertex_buffer.slot = 0;
    vertex_buffer.pitch = sizeof(Lesson49TreeVertex);
    vertex_buffer.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    SDL_zeroa(attributes);
    attributes[0].location = 0;
    attributes[0].buffer_slot = 0;
    attributes[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    attributes[0].offset = offsetof(Lesson49TreeVertex, position);
    attributes[1].location = 1;
    attributes[1].buffer_slot = 0;
    attributes[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    attributes[1].offset = offsetof(Lesson49TreeVertex, normal);

    SDL_zero(color_target);
    color_target.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;

    SDL_zero(pipeline_info);
    pipeline_info.vertex_shader = bake_vs;
    pipeline_info.fragment_shader = bake_fs;
    pipeline_info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pipeline_info.vertex_input_state.vertex_buffer_descriptions = &vertex_buffer;
    pipeline_info.vertex_input_state.num_vertex_buffers = 1;
    pipeline_info.vertex_input_state.vertex_attributes = attributes;
    pipeline_info.vertex_input_state.num_vertex_attributes = SDL_arraysize(attributes);
    pipeline_info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_BACK;
    pipeline_info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    pipeline_info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    pipeline_info.depth_stencil_state.enable_depth_test = true;
    pipeline_info.depth_stencil_state.enable_depth_write = true;
    pipeline_info.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS;
    pipeline_info.target_info.color_target_descriptions = &color_target;
    pipeline_info.target_info.num_color_targets = 1;
    pipeline_info.target_info.has_depth_stencil_target = true;
    pipeline_info.target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    bake_pipeline = SDL_CreateGPUGraphicsPipeline(demo->device, &pipeline_info);
    if (!bake_pipeline) {
        goto done;
    }

    command_buffer = SDL_AcquireGPUCommandBuffer(demo->device);
    if (!command_buffer) {
        goto done;
    }

    SDL_zero(color_info);
    color_info.texture = state->atlas_texture;
    color_info.load_op = SDL_GPU_LOADOP_CLEAR;
    color_info.store_op = SDL_GPU_STOREOP_STORE;
    color_info.clear_color = { 0.0f, 0.0f, 0.0f, 0.0f };
    SDL_zero(depth_target_info);
    depth_target_info.texture = bake_depth;
    depth_target_info.load_op = SDL_GPU_LOADOP_CLEAR;
    depth_target_info.store_op = SDL_GPU_STOREOP_DONT_CARE;
    depth_target_info.clear_depth = 1.0f;
    render_pass = SDL_BeginGPURenderPass(command_buffer, &color_info, 1, &depth_target_info);
    if (!render_pass) {
        SDL_CancelGPUCommandBuffer(command_buffer);
        command_buffer = nullptr;
        goto done;
    }

    SDL_BindGPUGraphicsPipeline(render_pass, bake_pipeline);
    {
        SDL_GPUBufferBinding vertex_binding;
        SDL_GPUBufferBinding index_binding;

        SDL_zero(vertex_binding);
        vertex_binding.buffer = state->tree_vertex_buffer;
        SDL_BindGPUVertexBuffers(render_pass, 0, &vertex_binding, 1);
        SDL_zero(index_binding);
        index_binding.buffer = state->tree_index_buffer;
        SDL_BindGPUIndexBuffer(render_pass, &index_binding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
    }

    half_w = LESSON49_TREE_CROWN_RADIUS + LESSON49_BAKE_ORTHO_MARGIN;
    half_h = LESSON49_TREE_TOTAL_HEIGHT * 0.5f + LESSON49_BAKE_ORTHO_MARGIN;
    projection = mat4_orthographic(
        -half_w,
        half_w,
        -half_h,
        half_h,
        LESSON49_BAKE_NEAR_CLIP,
        LESSON49_BAKE_VIEW_DIST * 2.0f);
    tree_center = { 0.0f, LESSON49_TREE_TOTAL_HEIGHT * 0.5f, 0.0f };
    up = { 0.0f, 1.0f, 0.0f };
    bake_light = vec3_normalize({ LESSON49_BAKE_LIGHT_X, LESSON49_BAKE_LIGHT_Y, LESSON49_BAKE_LIGHT_Z });

    for (int i = 0; i < LESSON49_ATLAS_FRAMES; i += 1) {
        const float angle = (float)i * (LESSON49_TAU / (float)LESSON49_ATLAS_FRAMES);
        const Vec3 eye = {
            SDL_sinf(angle) * LESSON49_BAKE_VIEW_DIST,
            LESSON49_TREE_TOTAL_HEIGHT * 0.5f,
            SDL_cosf(angle) * LESSON49_BAKE_VIEW_DIST
        };
        const Mat4 view = mat4_look_at(eye, tree_center, up);
        const int cell_x = (i % LESSON49_ATLAS_COLS) * (int)LESSON49_ATLAS_FRAME_SIZE;
        const int cell_y = (i / LESSON49_ATLAS_COLS) * (int)LESSON49_ATLAS_FRAME_SIZE;
        SDL_GPUViewport viewport;
        SDL_Rect scissor;
        Lesson49BakeVertUniforms vertex_uniforms;
        Lesson49BakeFragUniforms fragment_uniforms;

        SDL_zero(viewport);
        viewport.x = (float)cell_x;
        viewport.y = (float)cell_y;
        viewport.w = (float)LESSON49_ATLAS_FRAME_SIZE;
        viewport.h = (float)LESSON49_ATLAS_FRAME_SIZE;
        viewport.min_depth = 0.0f;
        viewport.max_depth = 1.0f;
        SDL_SetGPUViewport(render_pass, &viewport);

        scissor.x = cell_x;
        scissor.y = cell_y;
        scissor.w = (int)LESSON49_ATLAS_FRAME_SIZE;
        scissor.h = (int)LESSON49_ATLAS_FRAME_SIZE;
        SDL_SetGPUScissor(render_pass, &scissor);

        vertex_uniforms.mvp = mat4_multiply(projection, view);
        SDL_PushGPUVertexUniformData(command_buffer, 0, &vertex_uniforms, sizeof(vertex_uniforms));

        fragment_uniforms.base_color[0] = LESSON49_TRUNK_COLOR_R;
        fragment_uniforms.base_color[1] = LESSON49_TRUNK_COLOR_G;
        fragment_uniforms.base_color[2] = LESSON49_TRUNK_COLOR_B;
        fragment_uniforms.base_color[3] = 1.0f;
        fragment_uniforms.light_dir[0] = bake_light.x;
        fragment_uniforms.light_dir[1] = bake_light.y;
        fragment_uniforms.light_dir[2] = bake_light.z;
        fragment_uniforms.ambient = LESSON49_BAKE_AMBIENT;
        SDL_PushGPUFragmentUniformData(command_buffer, 0, &fragment_uniforms, sizeof(fragment_uniforms));
        SDL_DrawGPUIndexedPrimitives(render_pass, state->trunk_index_count, 1, 0, 0, 0);

        fragment_uniforms.base_color[0] = LESSON49_CROWN_COLOR_R;
        fragment_uniforms.base_color[1] = LESSON49_CROWN_COLOR_G;
        fragment_uniforms.base_color[2] = LESSON49_CROWN_COLOR_B;
        SDL_PushGPUFragmentUniformData(command_buffer, 0, &fragment_uniforms, sizeof(fragment_uniforms));
        SDL_DrawGPUIndexedPrimitives(
            render_pass,
            state->tree_index_count - state->trunk_index_count,
            1,
            state->trunk_index_count,
            0,
            0);
    }

    SDL_EndGPURenderPass(render_pass);
    render_pass = nullptr;
    if (!SDL_SubmitGPUCommandBuffer(command_buffer)) {
        command_buffer = nullptr;
        goto done;
    }
    command_buffer = nullptr;
    if (!SDL_WaitForGPUIdle(demo->device)) {
        goto done;
    }
    state->atlas_baked = true;
    ok = true;

done:
    if (render_pass) {
        SDL_EndGPURenderPass(render_pass);
    }
    if (command_buffer) {
        SDL_CancelGPUCommandBuffer(command_buffer);
    }
    if (bake_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, bake_pipeline);
    }
    lesson49_release_shader(demo->device, &bake_vs);
    lesson49_release_shader(demo->device, &bake_fs);
    if (bake_depth) {
        SDL_ReleaseGPUTexture(demo->device, bake_depth);
    }
    return ok;
}

static bool lesson49_upload_instances(ForgeGpuDemo *demo, SDL_GPUCommandBuffer *command_buffer, Lesson49State *state)
{
    SDL_GPUCopyPass *copy_pass;
    void *mapped;

    copy_pass = SDL_BeginGPUCopyPass(command_buffer);
    if (!copy_pass) {
        return false;
    }

    if (state->near_count > 0) {
        SDL_GPUTransferBufferLocation source;
        SDL_GPUBufferRegion destination;
        const Uint32 size = (Uint32)((size_t)state->near_count * sizeof(*state->near_instances));

        mapped = SDL_MapGPUTransferBuffer(demo->device, state->near_instance_upload, true);
        if (!mapped) {
            SDL_EndGPUCopyPass(copy_pass);
            return false;
        }
        SDL_memcpy(mapped, state->near_instances, size);
        SDL_UnmapGPUTransferBuffer(demo->device, state->near_instance_upload);

        SDL_zero(source);
        source.transfer_buffer = state->near_instance_upload;
        SDL_zero(destination);
        destination.buffer = state->near_instance_buffer;
        destination.size = size;
        SDL_UploadToGPUBuffer(copy_pass, &source, &destination, true);
    }

    if (state->far_count > 0) {
        SDL_GPUTransferBufferLocation source;
        SDL_GPUBufferRegion destination;
        const Uint32 size = (Uint32)((size_t)state->far_count * sizeof(*state->far_instances));

        mapped = SDL_MapGPUTransferBuffer(demo->device, state->far_instance_upload, true);
        if (!mapped) {
            SDL_EndGPUCopyPass(copy_pass);
            return false;
        }
        SDL_memcpy(mapped, state->far_instances, size);
        SDL_UnmapGPUTransferBuffer(demo->device, state->far_instance_upload);

        SDL_zero(source);
        source.transfer_buffer = state->far_instance_upload;
        SDL_zero(destination);
        destination.buffer = state->far_instance_buffer;
        destination.size = size;
        SDL_UploadToGPUBuffer(copy_pass, &source, &destination, true);
    }

    SDL_EndGPUCopyPass(copy_pass);
    return true;
}

static void lesson49_bind_terrain_buffers(SDL_GPURenderPass *render_pass, Lesson49State *state)
{
    SDL_GPUBufferBinding vertex_binding;
    SDL_GPUBufferBinding index_binding;

    SDL_zero(vertex_binding);
    vertex_binding.buffer = state->terrain_vertex_buffer;
    SDL_BindGPUVertexBuffers(render_pass, 0, &vertex_binding, 1);
    SDL_zero(index_binding);
    index_binding.buffer = state->terrain_index_buffer;
    SDL_BindGPUIndexBuffer(render_pass, &index_binding, SDL_GPU_INDEXELEMENTSIZE_16BIT);
}

static void lesson49_bind_tree_buffers(SDL_GPURenderPass *render_pass, SDL_GPUBuffer *instance_buffer, Lesson49State *state)
{
    SDL_GPUBufferBinding vertex_bindings[2];
    SDL_GPUBufferBinding index_binding;

    SDL_zeroa(vertex_bindings);
    vertex_bindings[0].buffer = state->tree_vertex_buffer;
    vertex_bindings[1].buffer = instance_buffer;
    SDL_BindGPUVertexBuffers(render_pass, 0, vertex_bindings, SDL_arraysize(vertex_bindings));
    SDL_zero(index_binding);
    index_binding.buffer = state->tree_index_buffer;
    SDL_BindGPUIndexBuffer(render_pass, &index_binding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
}

static void lesson49_draw_terrain_shadow(
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    Lesson49State *state,
    Mat4 light_vp)
{
    SDL_GPUTextureSamplerBinding vertex_sampler;
    Lesson49TerrainShadowVertUniforms uniforms;

    SDL_BindGPUGraphicsPipeline(render_pass, state->terrain_shadow_pipeline);
    SDL_zero(vertex_sampler);
    vertex_sampler.texture = state->heightmap_texture;
    vertex_sampler.sampler = state->heightmap_sampler;
    SDL_BindGPUVertexSamplers(render_pass, 0, &vertex_sampler, 1);
    lesson49_bind_terrain_buffers(render_pass, state);

    uniforms.light_vp = light_vp;
    uniforms.terrain_size = LESSON49_TERRAIN_SIZE;
    uniforms.height_scale = LESSON49_TERRAIN_HEIGHT_SCALE;
    uniforms.pad[0] = 0.0f;
    uniforms.pad[1] = 0.0f;
    SDL_PushGPUVertexUniformData(command_buffer, 0, &uniforms, sizeof(uniforms));
    SDL_DrawGPUIndexedPrimitives(render_pass, state->terrain_index_count, 1, 0, 0, 0);
    state->shadow_draw_calls += 1;
}

static void lesson49_draw_tree_shadow(
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    Lesson49State *state,
    Mat4 light_vp)
{
    Lesson49InstancedShadowVertUniforms uniforms;

    if (state->near_count == 0) {
        return;
    }
    SDL_BindGPUGraphicsPipeline(render_pass, state->tree_shadow_pipeline);
    lesson49_bind_tree_buffers(render_pass, state->near_instance_buffer, state);
    uniforms.light_vp = light_vp;
    uniforms.node_world = mat4_identity();
    SDL_PushGPUVertexUniformData(command_buffer, 0, &uniforms, sizeof(uniforms));
    SDL_DrawGPUIndexedPrimitives(render_pass, state->tree_index_count, state->near_count, 0, 0, 0);
    state->shadow_draw_calls += 1;
}

static void lesson49_draw_terrain(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    Lesson49State *state,
    Mat4 camera_vp,
    Mat4 light_vp)
{
    const Vec3 light_dir = ForgeGpuProcessedSceneLightDir();
    SDL_GPUTextureSamplerBinding vertex_sampler;
    SDL_GPUTextureSamplerBinding fragment_samplers[2];
    Lesson49TerrainVertUniforms vertex_uniforms;
    Lesson49TerrainFragUniforms fragment_uniforms;

    SDL_BindGPUGraphicsPipeline(
        render_pass,
        state->diagnostic_mode == LESSON49_DIAGNOSTIC_NO_VARIATION ?
            state->terrain_no_variation_pipeline :
            state->terrain_pipeline);
    SDL_zero(vertex_sampler);
    vertex_sampler.texture = state->heightmap_texture;
    vertex_sampler.sampler = state->heightmap_sampler;
    SDL_BindGPUVertexSamplers(render_pass, 0, &vertex_sampler, 1);

    SDL_zeroa(fragment_samplers);
    fragment_samplers[0].texture = state->heightmap_texture;
    fragment_samplers[0].sampler = state->heightmap_sampler;
    fragment_samplers[1].texture = state->renderer.shadow_depth;
    fragment_samplers[1].sampler = state->renderer.grid_shadow_sampler;
    SDL_BindGPUFragmentSamplers(render_pass, 0, fragment_samplers, SDL_arraysize(fragment_samplers));

    lesson49_bind_terrain_buffers(render_pass, state);

    vertex_uniforms.vp = camera_vp;
    vertex_uniforms.light_vp = light_vp;
    vertex_uniforms.terrain_size = LESSON49_TERRAIN_SIZE;
    vertex_uniforms.height_scale = LESSON49_TERRAIN_HEIGHT_SCALE;
    vertex_uniforms.pad[0] = 0.0f;
    vertex_uniforms.pad[1] = 0.0f;
    SDL_PushGPUVertexUniformData(command_buffer, 0, &vertex_uniforms, sizeof(vertex_uniforms));

    SDL_zero(fragment_uniforms);
    fragment_uniforms.light_dir[0] = light_dir.x;
    fragment_uniforms.light_dir[1] = light_dir.y;
    fragment_uniforms.light_dir[2] = light_dir.z;
    fragment_uniforms.light_intensity = LESSON49_LIGHT_INTENSITY;
    fragment_uniforms.eye_pos[0] = demo->lesson.camera_position.x;
    fragment_uniforms.eye_pos[1] = demo->lesson.camera_position.y;
    fragment_uniforms.eye_pos[2] = demo->lesson.camera_position.z;
    fragment_uniforms.ambient = LESSON49_AMBIENT;
    fragment_uniforms.height_scale = LESSON49_TERRAIN_HEIGHT_SCALE;
    fragment_uniforms.terrain_size = LESSON49_TERRAIN_SIZE;
    fragment_uniforms.texture_repeat = LESSON49_DEFAULT_TEXTURE_REPEAT;
    fragment_uniforms.snow_line = LESSON49_DEFAULT_SNOW_LINE;
    fragment_uniforms.slope_threshold = LESSON49_DEFAULT_SLOPE_THRESHOLD;
    SDL_PushGPUFragmentUniformData(command_buffer, 0, &fragment_uniforms, sizeof(fragment_uniforms));

    SDL_DrawGPUIndexedPrimitives(render_pass, state->terrain_index_count, 1, 0, 0, 0);
    state->terrain_draw_calls += 1;
}

static void lesson49_draw_near_trees(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    Lesson49State *state,
    Mat4 camera_vp,
    Mat4 light_vp)
{
    static const float kLightColor[3] = { 1.0f, 0.95f, 0.9f };
    const Vec3 light_dir = ForgeGpuProcessedSceneLightDir();
    SDL_GPUTextureSamplerBinding shadow_sampler;
    Lesson49InstancedVertUniforms vertex_uniforms;
    Lesson49TreeFragUniforms fragment_uniforms;

    if (state->near_count == 0) {
        return;
    }
    SDL_BindGPUGraphicsPipeline(render_pass, state->tree_pipeline);
    SDL_zero(shadow_sampler);
    shadow_sampler.texture = state->renderer.shadow_depth;
    shadow_sampler.sampler = state->renderer.grid_shadow_sampler;
    SDL_BindGPUFragmentSamplers(render_pass, 0, &shadow_sampler, 1);
    lesson49_bind_tree_buffers(render_pass, state->near_instance_buffer, state);

    vertex_uniforms.vp = camera_vp;
    vertex_uniforms.light_vp = light_vp;
    vertex_uniforms.node_world = mat4_identity();
    SDL_PushGPUVertexUniformData(command_buffer, 0, &vertex_uniforms, sizeof(vertex_uniforms));

    SDL_zero(fragment_uniforms);
    fragment_uniforms.base_color[0] = 1.0f;
    fragment_uniforms.base_color[1] = 1.0f;
    fragment_uniforms.base_color[2] = 1.0f;
    fragment_uniforms.base_color[3] = 1.0f;
    fragment_uniforms.eye_pos[0] = demo->lesson.camera_position.x;
    fragment_uniforms.eye_pos[1] = demo->lesson.camera_position.y;
    fragment_uniforms.eye_pos[2] = demo->lesson.camera_position.z;
    fragment_uniforms.ambient = LESSON49_AMBIENT;
    fragment_uniforms.light_dir[0] = light_dir.x;
    fragment_uniforms.light_dir[1] = light_dir.y;
    fragment_uniforms.light_dir[2] = light_dir.z;
    fragment_uniforms.light_dir[3] = 0.0f;
    SDL_memcpy(fragment_uniforms.light_color, kLightColor, sizeof(fragment_uniforms.light_color));
    fragment_uniforms.light_intensity = LESSON49_LIGHT_INTENSITY;
    fragment_uniforms.shininess = LESSON49_SHININESS;
    fragment_uniforms.specular_strength = LESSON49_SPECULAR_STRENGTH;
    SDL_PushGPUFragmentUniformData(command_buffer, 0, &fragment_uniforms, sizeof(fragment_uniforms));

    SDL_DrawGPUIndexedPrimitives(render_pass, state->tree_index_count, state->near_count, 0, 0, 0);
    state->near_draw_calls += 1;
}

static void lesson49_draw_imposters(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    Lesson49State *state,
    Mat4 camera_vp)
{
    SDL_GPUTextureSamplerBinding atlas_sampler;
    SDL_GPUBufferBinding vertex_bindings[2];
    SDL_GPUBufferBinding index_binding;
    Lesson49ImposterVertUniforms uniforms;

    if (state->far_count == 0) {
        return;
    }

    SDL_BindGPUGraphicsPipeline(render_pass, state->imposter_pipeline);
    uniforms.vp = camera_vp;
    uniforms.cam_pos[0] = demo->lesson.camera_position.x;
    uniforms.cam_pos[1] = demo->lesson.camera_position.y;
    uniforms.cam_pos[2] = demo->lesson.camera_position.z;
    uniforms.pad = 0.0f;
    SDL_PushGPUVertexUniformData(command_buffer, 0, &uniforms, sizeof(uniforms));

    SDL_zero(atlas_sampler);
    atlas_sampler.texture = state->atlas_texture;
    atlas_sampler.sampler = state->atlas_sampler;
    SDL_BindGPUFragmentSamplers(render_pass, 0, &atlas_sampler, 1);

    SDL_zeroa(vertex_bindings);
    vertex_bindings[0].buffer = state->quad_vertex_buffer;
    vertex_bindings[1].buffer = state->far_instance_buffer;
    SDL_BindGPUVertexBuffers(render_pass, 0, vertex_bindings, SDL_arraysize(vertex_bindings));
    SDL_zero(index_binding);
    index_binding.buffer = state->quad_index_buffer;
    SDL_BindGPUIndexBuffer(render_pass, &index_binding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
    SDL_DrawGPUIndexedPrimitives(render_pass, 6, state->far_count, 0, 0, 0);
    state->far_draw_calls += 1;
}

bool ForgeGpuCreateLesson49(ForgeGpuDemo *demo)
{
    Lesson49State *state;

    if (!SDL_GPUTextureSupportsFormat(
            demo->device,
            SDL_GPU_TEXTUREFORMAT_R32_FLOAT,
            SDL_GPU_TEXTURETYPE_2D,
            SDL_GPU_TEXTUREUSAGE_SAMPLER)) {
        SDL_SetError("lesson 49 requires sampled R32_FLOAT heightmaps; shader metadata requests filterable texture/sampler binding validation");
        return false;
    }
    if (!SDL_GPUTextureSupportsFormat(
            demo->device,
            SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
            SDL_GPU_TEXTURETYPE_2D,
            SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER)) {
        SDL_SetError("lesson 49 requires RGBA8 imposter atlas color-target sampling");
        return false;
    }

    state = (Lesson49State *)SDL_calloc(1, sizeof(*state));
    if (!state) {
        SDL_OutOfMemory();
        return false;
    }
    demo->lesson.private_state = state;
    state->lod_dist = LESSON49_LOD_DIST_DEFAULT;

    state->heightmap = (float *)SDL_malloc((size_t)LESSON49_HEIGHTMAP_SIZE * LESSON49_HEIGHTMAP_SIZE * sizeof(float));
    state->tree_positions = (Lesson49TreePosition *)SDL_calloc(LESSON49_TREE_COUNT_MAX, sizeof(*state->tree_positions));
    state->near_instances = (Lesson49TreeInstance *)SDL_calloc(LESSON49_TREE_COUNT_MAX, sizeof(*state->near_instances));
    state->far_instances = (Lesson49ImposterInstance *)SDL_calloc(LESSON49_TREE_COUNT_MAX, sizeof(*state->far_instances));
    if (!state->heightmap || !state->tree_positions || !state->near_instances || !state->far_instances) {
        SDL_OutOfMemory();
        return false;
    }

    forge_gpu_generate_normalized_fbm_heightmap(
        state->heightmap,
        LESSON49_HEIGHTMAP_SIZE,
        LESSON49_NOISE_SEED,
        LESSON49_NOISE_FREQUENCY,
        LESSON49_NOISE_OCTAVES,
        LESSON49_NOISE_LACUNARITY,
        LESSON49_NOISE_PERSISTENCE,
        LESSON49_HEIGHTMAP_RANGE_MIN,
        1.0f);
    state->heightmap_texture = ForgeGpuCreateR32FloatTextureFromPixels(
        demo->device,
        LESSON49_HEIGHTMAP_SIZE,
        LESSON49_HEIGHTMAP_SIZE,
        state->heightmap);
    if (!state->heightmap_texture) {
        return false;
    }
    state->tree_count = lesson49_place_trees(
        state->heightmap,
        LESSON49_HEIGHTMAP_SIZE,
        state->tree_positions,
        LESSON49_TREE_COUNT_MAX);

    if (!ForgeGpuProcessedSceneRendererCreate(demo, &state->renderer) ||
        !lesson49_create_samplers(demo, state) ||
        !lesson49_create_terrain_mesh(demo, state) ||
        !lesson49_create_tree_mesh(demo, state) ||
        !lesson49_create_quad_mesh(demo, state) ||
        !lesson49_create_instance_buffers(demo, state) ||
        !lesson49_create_atlas_texture(demo, state) ||
        !lesson49_create_scene_pipelines(demo, state) ||
        !lesson49_create_imposter_pipeline(demo, state) ||
        !lesson49_bake_imposter_atlas(demo, state)) {
        return false;
    }

    demo->lesson.camera_position = { LESSON49_CAM_START_X, LESSON49_CAM_START_Y, LESSON49_CAM_START_Z };
    demo->lesson.camera_yaw = LESSON49_CAM_START_YAW;
    demo->lesson.camera_pitch = LESSON49_CAM_START_PITCH;
    demo->lesson.move_speed = LESSON49_MOVE_SPEED;
    demo->lesson.mouse_sensitivity = LESSON49_MOUSE_SENSITIVITY;
    demo->lesson.pitch_clamp = LESSON49_PITCH_CLAMP;
    demo->lesson.last_ticks = SDL_GetTicks();
    return true;
}

bool ForgeGpuRenderLesson49(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPUTexture *swapchain_texture,
    Uint32 width,
    Uint32 height)
{
    Lesson49State *state = lesson49_state(demo);
    Mat4 view;
    Mat4 projection;
    Mat4 camera_vp;
    Mat4 light_vp;
    SDL_GPURenderPass *render_pass;

    if (!state) {
        SDL_SetError("lesson 49 internal state is missing");
        return false;
    }
    if (!ForgeGpuProcessedSceneRendererEnsureMainDepth(demo, &state->renderer, width, height)) {
        return false;
    }

    ForgeGpuProcessedSceneRendererBeginFrame(&state->renderer);
    state->terrain_draw_calls = 0;
    state->near_draw_calls = 0;
    state->far_draw_calls = 0;
    state->shadow_draw_calls = 0;

    ForgeGpuUpdateCameraFromInput(demo);
    ForgeGpuCameraViewProjection(demo, width, height, LESSON49_FAR_PLANE, &view, &projection);
    camera_vp = mat4_multiply(projection, view);
    light_vp = ForgeGpuProcessedSceneLightViewProjection();

    lesson49_classify_instances(state, demo->lesson.camera_position);
    if (!lesson49_upload_instances(demo, command_buffer, state)) {
        return false;
    }

    render_pass = ForgeGpuProcessedSceneBeginShadowPass(command_buffer, &state->renderer);
    if (!render_pass) {
        return false;
    }
    if (lesson49_draws_shadow(state)) {
        if (lesson49_draws_terrain(state)) {
            lesson49_draw_terrain_shadow(command_buffer, render_pass, state, light_vp);
        }
        if (lesson49_draws_near_trees(state)) {
            lesson49_draw_tree_shadow(command_buffer, render_pass, state, light_vp);
        }
    }
    SDL_EndGPURenderPass(render_pass);
    state->renderer.shadow_pass_rendered = true;

    render_pass = ForgeGpuProcessedSceneBeginMainPass(command_buffer, &state->renderer, swapchain_texture);
    if (!render_pass) {
        return false;
    }
    if (lesson49_draws_terrain(state)) {
        lesson49_draw_terrain(demo, command_buffer, render_pass, state, camera_vp, light_vp);
    }
    if (lesson49_draws_near_trees(state)) {
        lesson49_draw_near_trees(demo, command_buffer, render_pass, state, camera_vp, light_vp);
    }
    if (lesson49_draws_imposters(state)) {
        lesson49_draw_imposters(demo, command_buffer, render_pass, state, camera_vp);
    }
    SDL_EndGPURenderPass(render_pass);
    state->renderer.main_pass_rendered = true;
    return true;
}

void ForgeGpuDebugLesson49(ForgeGpuDemo *demo)
{
    Lesson49State *state = lesson49_state(demo);

    if (!state) {
        return;
    }
    ImGui::Text("Heightmap: %ux%u R32_FLOAT", LESSON49_HEIGHTMAP_SIZE, LESSON49_HEIGHTMAP_SIZE);
    ImGui::Text("Atlas: %ux%u RGBA8 (%u frames)", LESSON49_ATLAS_WIDTH, LESSON49_ATLAS_HEIGHT, LESSON49_ATLAS_FRAMES);
    ImGui::Text("Terrain indices: %u", state->terrain_index_count);
    ImGui::Text("Tree indices: %u (trunk %u)", state->tree_index_count, state->trunk_index_count);
    ImGui::Text("Trees: %u  Near: %u  Far: %u", state->tree_count, state->near_count, state->far_count);
    ImGui::Text("Draws: terrain %u, near %u, far %u, shadow %u",
        state->terrain_draw_calls,
        state->near_draw_calls,
        state->far_draw_calls,
        state->shadow_draw_calls);
}

void ForgeGpuControlsLesson49(ForgeGpuDemo *demo)
{
    Lesson49State *state = lesson49_state(demo);

    if (!state) {
        return;
    }
    ImGui::SliderFloat("LOD Distance", &state->lod_dist, LESSON49_LOD_DIST_MIN, LESSON49_LOD_DIST_MAX);
}

bool ForgeGpuHandleLesson49Event(ForgeGpuDemo *demo, const SDL_Event *event)
{
    Lesson49State *state = lesson49_state(demo);

    if (!state || !demo->validation_mode ||
        event->type != SDL_EVENT_KEY_DOWN ||
        event->key.repeat) {
        return false;
    }

    switch (event->key.key) {
    case SDLK_S:
        state->diagnostic_mode = LESSON49_DIAGNOSTIC_NO_SHADOW;
        return true;
    case SDLK_V:
        state->diagnostic_mode = LESSON49_DIAGNOSTIC_NO_VARIATION;
        return true;
    case SDLK_T:
        state->diagnostic_mode = LESSON49_DIAGNOSTIC_TERRAIN_ONLY;
        return true;
    case SDLK_N:
        state->diagnostic_mode = LESSON49_DIAGNOSTIC_NEAR_TREES_ONLY;
        return true;
    case SDLK_I:
        state->diagnostic_mode = LESSON49_DIAGNOSTIC_IMPOSTERS_ONLY;
        return true;
    default:
        return false;
    }
}

void ForgeGpuExportLesson49Metrics(ForgeGpuDemo *demo)
{
    Lesson49State *state = lesson49_state(demo);

    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson49Complete", state ? 1.0 : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson49HeightmapSize", (double)LESSON49_HEIGHTMAP_SIZE);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson49AtlasWidth", (double)LESSON49_ATLAS_WIDTH);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson49AtlasHeight", (double)LESSON49_ATLAS_HEIGHT);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson49AtlasBaked", state && state->atlas_baked ? 1.0 : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson49TerrainIndexCount", state ? (double)state->terrain_index_count : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson49TreeIndexCount", state ? (double)state->tree_index_count : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson49TreeCount", state ? (double)state->tree_count : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson49NearCount", state ? (double)state->near_count : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson49FarCount", state ? (double)state->far_count : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson49TerrainDrawCalls", state ? (double)state->terrain_draw_calls : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson49NearDrawCalls", state ? (double)state->near_draw_calls : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson49FarDrawCalls", state ? (double)state->far_draw_calls : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson49ShadowDrawCalls", state ? (double)state->shadow_draw_calls : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson49ShadowPass", state && state->renderer.shadow_pass_rendered ? 1.0 : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson49MainPass", state && state->renderer.main_pass_rendered ? 1.0 : 0.0);
}

void ForgeGpuDestroyLesson49(ForgeGpuDemo *demo)
{
    Lesson49State *state = lesson49_state(demo);

    if (!state) {
        return;
    }
    if (state->terrain_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, state->terrain_pipeline);
    }
    if (state->terrain_no_variation_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, state->terrain_no_variation_pipeline);
    }
    if (state->terrain_shadow_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, state->terrain_shadow_pipeline);
    }
    if (state->tree_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, state->tree_pipeline);
    }
    if (state->tree_shadow_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, state->tree_shadow_pipeline);
    }
    if (state->imposter_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, state->imposter_pipeline);
    }
    if (state->terrain_vertex_buffer) {
        SDL_ReleaseGPUBuffer(demo->device, state->terrain_vertex_buffer);
    }
    if (state->terrain_index_buffer) {
        SDL_ReleaseGPUBuffer(demo->device, state->terrain_index_buffer);
    }
    if (state->tree_vertex_buffer) {
        SDL_ReleaseGPUBuffer(demo->device, state->tree_vertex_buffer);
    }
    if (state->tree_index_buffer) {
        SDL_ReleaseGPUBuffer(demo->device, state->tree_index_buffer);
    }
    if (state->quad_vertex_buffer) {
        SDL_ReleaseGPUBuffer(demo->device, state->quad_vertex_buffer);
    }
    if (state->quad_index_buffer) {
        SDL_ReleaseGPUBuffer(demo->device, state->quad_index_buffer);
    }
    if (state->near_instance_buffer) {
        SDL_ReleaseGPUBuffer(demo->device, state->near_instance_buffer);
    }
    if (state->near_instance_upload) {
        SDL_ReleaseGPUTransferBuffer(demo->device, state->near_instance_upload);
    }
    if (state->far_instance_buffer) {
        SDL_ReleaseGPUBuffer(demo->device, state->far_instance_buffer);
    }
    if (state->far_instance_upload) {
        SDL_ReleaseGPUTransferBuffer(demo->device, state->far_instance_upload);
    }
    if (state->heightmap_texture) {
        SDL_ReleaseGPUTexture(demo->device, state->heightmap_texture);
    }
    if (state->heightmap_sampler) {
        SDL_ReleaseGPUSampler(demo->device, state->heightmap_sampler);
    }
    if (state->atlas_texture) {
        SDL_ReleaseGPUTexture(demo->device, state->atlas_texture);
    }
    if (state->atlas_sampler) {
        SDL_ReleaseGPUSampler(demo->device, state->atlas_sampler);
    }
    SDL_free(state->heightmap);
    SDL_free(state->tree_positions);
    SDL_free(state->near_instances);
    SDL_free(state->far_instances);
    ForgeGpuProcessedSceneRendererDestroy(demo, &state->renderer);
    SDL_free(state);
    demo->lesson.private_state = nullptr;
}
