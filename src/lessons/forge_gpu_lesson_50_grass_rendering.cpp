#include "forge_gpu_lessons.h"

#include "forge_gpu_browser_status.h"
#include "forge_gpu_camera.h"
#include "forge_gpu_gpu_helpers.h"
#include "forge_gpu_lesson_common.h"
#include "forge_gpu_math.h"
#include "forge_gpu_processed_scene_renderer.h"
#include "forge_gpu_shader_layouts.h"
#include "forge_gpu_shapes.h"
#include "shaders/generated/forge_gpu_lesson_50_shaders.h"
#include "shaders/generated/forge_gpu_shared_scene_shaders.h"

#include <stddef.h>

#include "imgui.h"

#define LESSON50_HEIGHTMAP_SIZE 256
#define LESSON50_NOISE_FREQUENCY 1.5f
#define LESSON50_NOISE_OCTAVES 5
#define LESSON50_NOISE_LACUNARITY 2.0f
#define LESSON50_NOISE_PERSISTENCE 0.5f
#define LESSON50_NOISE_SEED 42u
#define LESSON50_DEFAULT_HEIGHT_SCALE 4.0f
#define LESSON50_TERRAIN_SIZE 40.0f
#define LESSON50_TERRAIN_TILE_GRID 8
#define LESSON50_TERRAIN_TILE_COUNT (LESSON50_TERRAIN_TILE_GRID * LESSON50_TERRAIN_TILE_GRID)
#define LESSON50_TERRAIN_LOD_COUNT 4
#define LESSON50_TERRAIN_MORPH_START_FRAC 0.8f
#define LESSON50_GRASS_RING_COUNT 4
#define LESSON50_GRASS_MAX_PER_RING 16384
#define LESSON50_GRASS_BLADE_WIDTH 0.06f
#define LESSON50_GRASS_BLADE_HEIGHT 0.45f
#define LESSON50_GRASS_BLADE_TIP_TAPER 0.8f
#define LESSON50_GRASS_SLOPE_THRESHOLD 0.35f
#define LESSON50_GRASS_MIN_STEP 0.1f
#define LESSON50_GRASS_FRUSTUM_MARGIN 1.2f
#define LESSON50_GRASS_SHADOW_RINGS 2
#define LESSON50_GRASS_FADE_DIST 3.0f
#define LESSON50_DEFAULT_WIND_STRENGTH 0.5f
#define LESSON50_DEFAULT_WIND_SPEED 2.0f
#define LESSON50_DEFAULT_DENSITY_MULT 1.0f
#define LESSON50_DEFAULT_LOD_DIST_MULT 1.0f
#define LESSON50_DEFAULT_SNOW_LINE 0.7f
#define LESSON50_DEFAULT_SLOPE_THRESHOLD 0.3f
#define LESSON50_TERRAIN_TEXTURE_REPEAT 8.0f
#define LESSON50_WIND_DIR_X 0.7071f
#define LESSON50_WIND_DIR_Z 0.7071f
#define LESSON50_FAR_PLANE 200.0f
#define LESSON50_CAM_START_X 0.0f
#define LESSON50_CAM_START_Y 3.0f
#define LESSON50_CAM_START_Z 5.0f
#define LESSON50_CAM_START_YAW 0.0f
#define LESSON50_CAM_START_PITCH (-0.15f)
#define LESSON50_MOVE_SPEED 8.0f
#define LESSON50_MOUSE_SENSITIVITY 0.003f
#define LESSON50_PITCH_CLAMP 1.5f
#define LESSON50_AMBIENT 0.15f
#define LESSON50_LIGHT_INTENSITY 1.2f
#define LESSON50_SHADOW_ORTHO_SIZE 30.0f
#define LESSON50_SHADOW_HEIGHT 20.0f
#define LESSON50_SHADOW_NEAR 0.1f
#define LESSON50_SHADOW_FAR 80.0f
#define LESSON50_SHADOW_BIAS_CONST 2.0f
#define LESSON50_SHADOW_BIAS_SLOPE 2.0f

static const int kLesson50TerrainLodResolutions[LESSON50_TERRAIN_LOD_COUNT] = { 64, 32, 16, 8 };
static const float kLesson50TerrainLodDistances[LESSON50_TERRAIN_LOD_COUNT] = { 15.0f, 30.0f, 50.0f, 9999.0f };
static const float kLesson50GrassRingDistMin[LESSON50_GRASS_RING_COUNT] = { 0.0f, 10.0f, 25.0f, 50.0f };
static const float kLesson50GrassRingDistMax[LESSON50_GRASS_RING_COUNT] = { 10.0f, 25.0f, 50.0f, 80.0f };
static const float kLesson50GrassRingStep[LESSON50_GRASS_RING_COUNT] = { 0.25f, 0.5f, 1.0f, 2.0f };
static const int kLesson50GrassRingSegments[LESSON50_GRASS_RING_COUNT] = { 4, 3, 2, 1 };

typedef struct Lesson50TerrainVertex
{
    float position[3];
    float uv[2];
} Lesson50TerrainVertex;

typedef struct Lesson50TerrainLodVertUniforms
{
    Mat4 vp;
    Mat4 light_vp;
    float terrain_size;
    float height_scale;
    float tile_offset[2];
    float tile_scale;
    float morph_factor;
    float coarse_cell_size;
    float pad;
} Lesson50TerrainLodVertUniforms;

typedef struct Lesson50TerrainFragUniforms
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
} Lesson50TerrainFragUniforms;

typedef struct Lesson50TerrainShadowUniforms
{
    Mat4 light_vp;
    float terrain_size;
    float height_scale;
    float tile_offset[2];
    float tile_scale;
    float morph_factor;
    float coarse_cell_size;
    float pad;
} Lesson50TerrainShadowUniforms;

typedef struct Lesson50GrassVertex
{
    float position[3];
    float height_t;
} Lesson50GrassVertex;

typedef struct Lesson50GrassInstance
{
    float position[3];
    float rotation;
    float scale[2];
    float color[3];
    float pad;
} Lesson50GrassInstance;

typedef struct Lesson50GrassVertUniforms
{
    Mat4 vp;
    Mat4 light_vp;
    float time;
    float wind_strength;
    float wind_speed;
    float pad0;
    float wind_dir[2];
    float pad1[2];
} Lesson50GrassVertUniforms;

typedef struct Lesson50GrassFragUniforms
{
    float light_dir[3];
    float light_intensity;
    float eye_pos[3];
    float ambient;
} Lesson50GrassFragUniforms;

typedef struct Lesson50GrassShadowUniforms
{
    Mat4 light_vp;
    float time;
    float wind_strength;
    float wind_speed;
    float pad0;
    float wind_dir[2];
    float pad1[2];
} Lesson50GrassShadowUniforms;

typedef enum Lesson50DiagnosticMode
{
    LESSON50_DIAGNOSTIC_DEFAULT,
    LESSON50_DIAGNOSTIC_NO_SHADOW,
    LESSON50_DIAGNOSTIC_NO_VARIATION,
    LESSON50_DIAGNOSTIC_NO_GRASS,
    LESSON50_DIAGNOSTIC_FILL_ONLY,
    LESSON50_DIAGNOSTIC_LINE_ONLY,
    LESSON50_DIAGNOSTIC_LINE_ONLY_NO_VARIATION
} Lesson50DiagnosticMode;

typedef struct Lesson50TerrainLodMesh
{
    SDL_GPUBuffer *vertex_buffer;
    SDL_GPUBuffer *tri_index_buffer;
    SDL_GPUBuffer *line_index_buffer;
    Uint32 tri_index_count;
    Uint32 line_index_count;
    Uint32 vertex_count;
} Lesson50TerrainLodMesh;

typedef struct Lesson50TerrainTile
{
    int lod_level;
    float morph_factor;
    float center_x;
    float center_z;
    float tile_scale;
    float offset_x;
    float offset_z;
} Lesson50TerrainTile;

typedef struct Lesson50GrassBladeMesh
{
    SDL_GPUBuffer *vertex_buffer;
    SDL_GPUBuffer *tri_index_buffer;
    SDL_GPUBuffer *line_index_buffer;
    Uint32 tri_index_count;
    Uint32 line_index_count;
    Uint32 vertex_count;
} Lesson50GrassBladeMesh;

typedef struct Lesson50State
{
    ForgeGpuProcessedSceneRenderer renderer;
    SDL_GPUTexture *heightmap_texture;
    SDL_GPUSampler *heightmap_sampler;
    SDL_GPUGraphicsPipeline *terrain_fill_pipeline;
    SDL_GPUGraphicsPipeline *terrain_line_pipeline;
    SDL_GPUGraphicsPipeline *terrain_no_variation_fill_pipeline;
    SDL_GPUGraphicsPipeline *terrain_no_variation_line_pipeline;
    SDL_GPUGraphicsPipeline *terrain_shadow_pipeline;
    SDL_GPUGraphicsPipeline *grass_fill_pipeline;
    SDL_GPUGraphicsPipeline *grass_line_pipeline;
    SDL_GPUGraphicsPipeline *grass_shadow_pipeline;
    Lesson50TerrainLodMesh terrain_lod[LESSON50_TERRAIN_LOD_COUNT];
    Lesson50TerrainTile tiles[LESSON50_TERRAIN_TILE_COUNT];
    Lesson50GrassBladeMesh grass_blades[LESSON50_GRASS_RING_COUNT];
    SDL_GPUBuffer *grass_instance_buffer[LESSON50_GRASS_RING_COUNT];
    SDL_GPUTransferBuffer *grass_instance_upload[LESSON50_GRASS_RING_COUNT];
    Lesson50GrassInstance *grass_cpu[LESSON50_GRASS_RING_COUNT];
    float *heightmap;
    Uint32 grass_count[LESSON50_GRASS_RING_COUNT];
    Uint32 total_grass_count;
    Uint32 terrain_fill_draw_calls;
    Uint32 terrain_line_draw_calls;
    Uint32 terrain_shadow_draw_calls;
    Uint32 grass_fill_draw_calls;
    Uint32 grass_line_draw_calls;
    Uint32 grass_shadow_draw_calls;
    float elapsed_time;
    float height_scale;
    float wind_strength;
    float wind_speed;
    float density_mult;
    float lod_dist_mult;
    float snow_line;
    float slope_threshold;
    Lesson50DiagnosticMode diagnostic_mode;
} Lesson50State;

static_assert(sizeof(Lesson50TerrainVertex) == 20, "lesson 50 terrain vertex size must match HLSL layout");
static_assert(sizeof(Lesson50TerrainLodVertUniforms) == 160, "lesson 50 terrain LOD vertex uniform size must match HLSL layout");
static_assert(sizeof(Lesson50TerrainFragUniforms) == 64, "lesson 50 terrain fragment uniform size must match HLSL layout");
static_assert(sizeof(Lesson50TerrainShadowUniforms) == 96, "lesson 50 terrain shadow uniform size must match HLSL layout");
static_assert(sizeof(Lesson50GrassVertex) == 16, "lesson 50 grass vertex size must match HLSL layout");
static_assert(sizeof(Lesson50GrassInstance) == 40, "lesson 50 grass instance size must match HLSL layout");
static_assert(sizeof(Lesson50GrassVertUniforms) == 160, "lesson 50 grass vertex uniform size must match HLSL layout");
static_assert(sizeof(Lesson50GrassFragUniforms) == 32, "lesson 50 grass fragment uniform size must match HLSL layout");
static_assert(sizeof(Lesson50GrassShadowUniforms) == 96, "lesson 50 grass shadow uniform size must match HLSL layout");

static Lesson50State *lesson50_state(ForgeGpuDemo *demo)
{
    return (Lesson50State *)demo->lesson.private_state;
}

static bool lesson50_draws_shadow(const Lesson50State *state)
{
    return state->diagnostic_mode != LESSON50_DIAGNOSTIC_NO_SHADOW;
}

static bool lesson50_draws_grass(const Lesson50State *state)
{
    return state->diagnostic_mode != LESSON50_DIAGNOSTIC_NO_GRASS;
}

static bool lesson50_uses_no_variation_pipeline(const Lesson50State *state)
{
    return state->diagnostic_mode == LESSON50_DIAGNOSTIC_NO_VARIATION ||
        state->diagnostic_mode == LESSON50_DIAGNOSTIC_LINE_ONLY_NO_VARIATION;
}

static bool lesson50_uses_line_rendering(const Lesson50State *state)
{
    return state->diagnostic_mode == LESSON50_DIAGNOSTIC_LINE_ONLY ||
        state->diagnostic_mode == LESSON50_DIAGNOSTIC_LINE_ONLY_NO_VARIATION;
}

static bool lesson50_uses_full_viewport(const Lesson50State *state)
{
    return state->diagnostic_mode == LESSON50_DIAGNOSTIC_FILL_ONLY ||
        lesson50_uses_line_rendering(state);
}

static void lesson50_release_shader(SDL_GPUDevice *device, SDL_GPUShader **shader)
{
    if (*shader) {
        SDL_ReleaseGPUShader(device, *shader);
        *shader = nullptr;
    }
}

static Uint32 lesson50_hash2d(Uint32 x, Uint32 y)
{
    return forge_gpu_hash_wang(x ^ forge_gpu_hash_wang(y));
}

static float lesson50_sample_height_at(const float *hmap, int size, float world_x, float world_z)
{
    const float u = (world_x + LESSON50_TERRAIN_SIZE) / (2.0f * LESSON50_TERRAIN_SIZE);
    const float v = (world_z + LESSON50_TERRAIN_SIZE) / (2.0f * LESSON50_TERRAIN_SIZE);
    float tx = u * (float)(size - 1);
    float ty = v * (float)(size - 1);

    tx = SDL_max(0.0f, SDL_min(tx, (float)(size - 1)));
    ty = SDL_max(0.0f, SDL_min(ty, (float)(size - 1)));

    const int x0 = (int)SDL_floorf(tx);
    const int y0 = (int)SDL_floorf(ty);
    const int x1 = SDL_min(x0 + 1, size - 1);
    const int y1 = SDL_min(y0 + 1, size - 1);
    const float fx = tx - (float)x0;
    const float fy = ty - (float)y0;
    const float h00 = hmap[y0 * size + x0];
    const float h10 = hmap[y0 * size + x1];
    const float h01 = hmap[y1 * size + x0];
    const float h11 = hmap[y1 * size + x1];
    const float h0 = h00 + (h10 - h00) * fx;
    const float h1 = h01 + (h11 - h01) * fx;

    return h0 + (h1 - h0) * fy;
}

static float lesson50_compute_slope_at(const float *hmap, int size, float height_scale, float world_x, float world_z)
{
    const float texel_world = (2.0f * LESSON50_TERRAIN_SIZE) / (float)(size - 1);
    const float h_l = lesson50_sample_height_at(hmap, size, world_x - texel_world, world_z);
    const float h_r = lesson50_sample_height_at(hmap, size, world_x + texel_world, world_z);
    const float h_d = lesson50_sample_height_at(hmap, size, world_x, world_z - texel_world);
    const float h_u = lesson50_sample_height_at(hmap, size, world_x, world_z + texel_world);
    const float dx = (h_r - h_l) * height_scale * 0.5f / texel_world;
    const float dz = (h_u - h_d) * height_scale * 0.5f / texel_world;

    return SDL_sqrtf(dx * dx + dz * dz);
}

static Uint32 lesson50_build_line_indices(const Uint32 *tri_indices, Uint32 tri_count, Uint32 *out_line)
{
    Uint32 n = 0;

    for (Uint32 i = 0; i < tri_count; i += 3) {
        const Uint32 a = tri_indices[i];
        const Uint32 b = tri_indices[i + 1];
        const Uint32 c = tri_indices[i + 2];

        out_line[n++] = a;
        out_line[n++] = b;
        out_line[n++] = b;
        out_line[n++] = c;
        out_line[n++] = c;
        out_line[n++] = a;
    }
    return n;
}

static Mat4 lesson50_light_view_projection(void)
{
    const Vec3 light_dir = ForgeGpuProcessedSceneLightDir();
    const Vec3 light_pos = vec3_scale(light_dir, LESSON50_SHADOW_HEIGHT);
    Vec3 up = { 0.0f, 1.0f, 0.0f };

    if (SDL_fabsf(vec3_dot(light_dir, up)) > 0.999f) {
        up = { 1.0f, 0.0f, 0.0f };
    }
    return mat4_multiply(
        mat4_orthographic(
            -LESSON50_SHADOW_ORTHO_SIZE,
            LESSON50_SHADOW_ORTHO_SIZE,
            -LESSON50_SHADOW_ORTHO_SIZE,
            LESSON50_SHADOW_ORTHO_SIZE,
            LESSON50_SHADOW_NEAR,
            LESSON50_SHADOW_FAR),
        mat4_look_at(light_pos, { 0.0f, 0.0f, 0.0f }, up));
}

static bool lesson50_create_terrain_meshes(ForgeGpuDemo *demo, Lesson50State *state)
{
    for (int lod = 0; lod < LESSON50_TERRAIN_LOD_COUNT; lod += 1) {
        ForgeGpuShapeMesh plane;
        Lesson50TerrainVertex *vertices = nullptr;
        Uint32 *line_indices = nullptr;
        const int res = kLesson50TerrainLodResolutions[lod];
        bool ok = false;

        SDL_zero(plane);
        if (!ForgeGpuCreatePlaneShapeMesh(res - 1, res - 1, &plane)) {
            return false;
        }

        vertices = (Lesson50TerrainVertex *)SDL_calloc((size_t)plane.vertex_count, sizeof(*vertices));
        if (!vertices) {
            SDL_OutOfMemory();
            ForgeGpuFreeShapeMesh(&plane);
            return false;
        }
        for (int i = 0; i < plane.vertex_count; i += 1) {
            vertices[i].position[0] = plane.positions[i * 3 + 0];
            vertices[i].position[1] = plane.positions[i * 3 + 1];
            vertices[i].position[2] = plane.positions[i * 3 + 2];
            vertices[i].uv[0] = plane.uvs[i * 2 + 0];
            vertices[i].uv[1] = plane.uvs[i * 2 + 1];
        }

        line_indices = (Uint32 *)SDL_malloc((size_t)plane.index_count * 2u * sizeof(*line_indices));
        if (!line_indices) {
            SDL_OutOfMemory();
            SDL_free(vertices);
            ForgeGpuFreeShapeMesh(&plane);
            return false;
        }

        state->terrain_lod[lod].vertex_count = (Uint32)plane.vertex_count;
        state->terrain_lod[lod].tri_index_count = (Uint32)plane.index_count;
        state->terrain_lod[lod].line_index_count = lesson50_build_line_indices(
            plane.indices,
            (Uint32)plane.index_count,
            line_indices);
        state->terrain_lod[lod].vertex_buffer = ForgeGpuCreateBufferWithData(
            demo->device,
            SDL_GPU_BUFFERUSAGE_VERTEX,
            vertices,
            (Uint32)((size_t)plane.vertex_count * sizeof(*vertices)));
        state->terrain_lod[lod].tri_index_buffer = ForgeGpuCreateBufferWithData(
            demo->device,
            SDL_GPU_BUFFERUSAGE_INDEX,
            plane.indices,
            (Uint32)((size_t)plane.index_count * sizeof(*plane.indices)));
        state->terrain_lod[lod].line_index_buffer = ForgeGpuCreateBufferWithData(
            demo->device,
            SDL_GPU_BUFFERUSAGE_INDEX,
            line_indices,
            (Uint32)((size_t)state->terrain_lod[lod].line_index_count * sizeof(*line_indices)));

        ok = state->terrain_lod[lod].vertex_buffer &&
            state->terrain_lod[lod].tri_index_buffer &&
            state->terrain_lod[lod].line_index_buffer;
        SDL_free(line_indices);
        SDL_free(vertices);
        ForgeGpuFreeShapeMesh(&plane);
        if (!ok) {
            return false;
        }
    }
    return true;
}

static bool lesson50_create_grass_meshes(ForgeGpuDemo *demo, Lesson50State *state)
{
    for (int ring = 0; ring < LESSON50_GRASS_RING_COUNT; ring += 1) {
        const int segments = kLesson50GrassRingSegments[ring];
        const int vertex_count = 2 * (segments + 1);
        const int tri_index_count = 6 * segments;
        Lesson50GrassVertex *vertices = nullptr;
        Uint32 *tri_indices = nullptr;
        Uint32 *line_indices = nullptr;
        bool ok = false;

        vertices = (Lesson50GrassVertex *)SDL_calloc((size_t)vertex_count, sizeof(*vertices));
        tri_indices = (Uint32 *)SDL_calloc((size_t)tri_index_count, sizeof(*tri_indices));
        line_indices = (Uint32 *)SDL_malloc((size_t)tri_index_count * 2u * sizeof(*line_indices));
        if (!vertices || !tri_indices || !line_indices) {
            SDL_OutOfMemory();
            SDL_free(line_indices);
            SDL_free(tri_indices);
            SDL_free(vertices);
            return false;
        }

        for (int i = 0; i <= segments; i += 1) {
            const float t = (float)i / (float)segments;
            const float half_w = 0.5f * (1.0f - t * LESSON50_GRASS_BLADE_TIP_TAPER);
            vertices[2 * i + 0].position[0] = -half_w;
            vertices[2 * i + 0].position[1] = t;
            vertices[2 * i + 0].position[2] = 0.0f;
            vertices[2 * i + 0].height_t = t;
            vertices[2 * i + 1].position[0] = half_w;
            vertices[2 * i + 1].position[1] = t;
            vertices[2 * i + 1].position[2] = 0.0f;
            vertices[2 * i + 1].height_t = t;
        }

        for (int i = 0, idx = 0; i < segments; i += 1) {
            const int bl = 2 * i;
            const int br = 2 * i + 1;
            const int tl = 2 * (i + 1);
            const int tr = 2 * (i + 1) + 1;

            tri_indices[idx++] = (Uint32)bl;
            tri_indices[idx++] = (Uint32)tl;
            tri_indices[idx++] = (Uint32)br;
            tri_indices[idx++] = (Uint32)br;
            tri_indices[idx++] = (Uint32)tl;
            tri_indices[idx++] = (Uint32)tr;
        }

        state->grass_blades[ring].vertex_count = (Uint32)vertex_count;
        state->grass_blades[ring].tri_index_count = (Uint32)tri_index_count;
        state->grass_blades[ring].line_index_count = lesson50_build_line_indices(
            tri_indices,
            (Uint32)tri_index_count,
            line_indices);
        state->grass_blades[ring].vertex_buffer = ForgeGpuCreateBufferWithData(
            demo->device,
            SDL_GPU_BUFFERUSAGE_VERTEX,
            vertices,
            (Uint32)((size_t)vertex_count * sizeof(*vertices)));
        state->grass_blades[ring].tri_index_buffer = ForgeGpuCreateBufferWithData(
            demo->device,
            SDL_GPU_BUFFERUSAGE_INDEX,
            tri_indices,
            (Uint32)((size_t)tri_index_count * sizeof(*tri_indices)));
        state->grass_blades[ring].line_index_buffer = ForgeGpuCreateBufferWithData(
            demo->device,
            SDL_GPU_BUFFERUSAGE_INDEX,
            line_indices,
            (Uint32)((size_t)state->grass_blades[ring].line_index_count * sizeof(*line_indices)));

        ok = state->grass_blades[ring].vertex_buffer &&
            state->grass_blades[ring].tri_index_buffer &&
            state->grass_blades[ring].line_index_buffer;
        SDL_free(line_indices);
        SDL_free(tri_indices);
        SDL_free(vertices);
        if (!ok) {
            return false;
        }
    }
    return true;
}

static bool lesson50_create_instance_buffers(ForgeGpuDemo *demo, Lesson50State *state)
{
    const Uint32 buffer_size = (Uint32)((size_t)LESSON50_GRASS_MAX_PER_RING * sizeof(Lesson50GrassInstance));

    for (int ring = 0; ring < LESSON50_GRASS_RING_COUNT; ring += 1) {
        SDL_GPUBufferCreateInfo buffer_info;
        SDL_GPUTransferBufferCreateInfo transfer_info;

        state->grass_cpu[ring] = (Lesson50GrassInstance *)SDL_calloc(LESSON50_GRASS_MAX_PER_RING, sizeof(*state->grass_cpu[ring]));
        if (!state->grass_cpu[ring]) {
            SDL_OutOfMemory();
            return false;
        }

        SDL_zero(buffer_info);
        buffer_info.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
        buffer_info.size = buffer_size;
        state->grass_instance_buffer[ring] = SDL_CreateGPUBuffer(demo->device, &buffer_info);

        SDL_zero(transfer_info);
        transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        transfer_info.size = buffer_size;
        state->grass_instance_upload[ring] = SDL_CreateGPUTransferBuffer(demo->device, &transfer_info);

        if (!state->grass_instance_buffer[ring] || !state->grass_instance_upload[ring]) {
            return false;
        }
    }
    return true;
}

static bool lesson50_create_samplers(ForgeGpuDemo *demo, Lesson50State *state)
{
    state->heightmap_sampler = ForgeGpuCreateSamplerWithAddress(
        demo->device,
        SDL_GPU_FILTER_LINEAR,
        SDL_GPU_FILTER_LINEAR,
        SDL_GPU_SAMPLERMIPMAPMODE_NEAREST,
        SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        0.0f);
    return state->heightmap_sampler != nullptr;
}

static void lesson50_fill_terrain_vertex_input(SDL_GPUVertexBufferDescription *vertex_buffer, SDL_GPUVertexAttribute attributes[2])
{
    SDL_zero(*vertex_buffer);
    vertex_buffer->slot = 0;
    vertex_buffer->pitch = sizeof(Lesson50TerrainVertex);
    vertex_buffer->input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

    SDL_memset(attributes, 0, 2 * sizeof(*attributes));
    attributes[0].location = 0;
    attributes[0].buffer_slot = 0;
    attributes[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    attributes[0].offset = offsetof(Lesson50TerrainVertex, position);
    attributes[1].location = 1;
    attributes[1].buffer_slot = 0;
    attributes[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    attributes[1].offset = offsetof(Lesson50TerrainVertex, uv);
}

static void lesson50_fill_grass_vertex_input(SDL_GPUVertexBufferDescription vertex_buffers[2], SDL_GPUVertexAttribute attributes[6])
{
    SDL_memset(vertex_buffers, 0, 2 * sizeof(*vertex_buffers));
    vertex_buffers[0].slot = 0;
    vertex_buffers[0].pitch = sizeof(Lesson50GrassVertex);
    vertex_buffers[0].input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    vertex_buffers[1].slot = 1;
    vertex_buffers[1].pitch = sizeof(Lesson50GrassInstance);
    vertex_buffers[1].input_rate = SDL_GPU_VERTEXINPUTRATE_INSTANCE;

    SDL_memset(attributes, 0, 6 * sizeof(*attributes));
    attributes[0].location = 0;
    attributes[0].buffer_slot = 0;
    attributes[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    attributes[0].offset = offsetof(Lesson50GrassVertex, position);
    attributes[1].location = 1;
    attributes[1].buffer_slot = 0;
    attributes[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT;
    attributes[1].offset = offsetof(Lesson50GrassVertex, height_t);
    attributes[2].location = 2;
    attributes[2].buffer_slot = 1;
    attributes[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    attributes[2].offset = offsetof(Lesson50GrassInstance, position);
    attributes[3].location = 3;
    attributes[3].buffer_slot = 1;
    attributes[3].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT;
    attributes[3].offset = offsetof(Lesson50GrassInstance, rotation);
    attributes[4].location = 4;
    attributes[4].buffer_slot = 1;
    attributes[4].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    attributes[4].offset = offsetof(Lesson50GrassInstance, scale);
    attributes[5].location = 5;
    attributes[5].buffer_slot = 1;
    attributes[5].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    attributes[5].offset = offsetof(Lesson50GrassInstance, color);
}

static bool lesson50_create_pipelines(ForgeGpuDemo *demo, Lesson50State *state)
{
    SDL_GPUShader *terrain_vs = nullptr;
    SDL_GPUShader *terrain_fs = nullptr;
    SDL_GPUShader *terrain_no_variation_fs = nullptr;
    SDL_GPUShader *terrain_shadow_vs = nullptr;
    SDL_GPUShader *grass_vs = nullptr;
    SDL_GPUShader *grass_fs = nullptr;
    SDL_GPUShader *grass_shadow_vs = nullptr;
    SDL_GPUShader *shadow_fs = nullptr;
    SDL_GPUColorTargetDescription color_target;
    SDL_GPUVertexBufferDescription terrain_vb;
    SDL_GPUVertexAttribute terrain_attrs[2];
    SDL_GPUVertexBufferDescription grass_vbs[2];
    SDL_GPUVertexAttribute grass_attrs[6];
    bool ok = false;

    SDL_zero(color_target);
    color_target.format = demo->color_format;

    terrain_vs = ForgeGpuCreateShaderWithResourceLayout(
        demo->device,
        lesson50_terrain_lod_vert_wgsl,
        lesson50_terrain_lod_vert_wgsl_size,
        lesson50_terrain_lod_vert_msl,
        lesson50_terrain_lod_vert_msl_size,
        ForgeGpuShaderLayout_lesson50_terrain_lod_vert());
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
        lesson50_terrain_lod_shadow_vert_wgsl,
        lesson50_terrain_lod_shadow_vert_wgsl_size,
        lesson50_terrain_lod_shadow_vert_msl,
        lesson50_terrain_lod_shadow_vert_msl_size,
        ForgeGpuShaderLayout_lesson50_terrain_lod_shadow_vert());
    grass_vs = ForgeGpuCreateShaderWithResourceLayout(
        demo->device,
        lesson50_grass_vert_wgsl,
        lesson50_grass_vert_wgsl_size,
        lesson50_grass_vert_msl,
        lesson50_grass_vert_msl_size,
        ForgeGpuShaderLayout_lesson50_grass_vert());
    grass_fs = ForgeGpuCreateShaderWithResourceLayout(
        demo->device,
        lesson50_grass_frag_wgsl,
        lesson50_grass_frag_wgsl_size,
        lesson50_grass_frag_msl,
        lesson50_grass_frag_msl_size,
        ForgeGpuShaderLayout_lesson50_grass_frag());
    grass_shadow_vs = ForgeGpuCreateShaderWithResourceLayout(
        demo->device,
        lesson50_grass_shadow_vert_wgsl,
        lesson50_grass_shadow_vert_wgsl_size,
        lesson50_grass_shadow_vert_msl,
        lesson50_grass_shadow_vert_msl_size,
        ForgeGpuShaderLayout_lesson50_grass_shadow_vert());
    shadow_fs = ForgeGpuCreateShader(
        demo->device,
        SDL_GPU_SHADERSTAGE_FRAGMENT,
        forge_scene_shadow_frag_wgsl,
        forge_scene_shadow_frag_wgsl_size,
        forge_scene_shadow_frag_msl,
        forge_scene_shadow_frag_msl_size,
        0, 0, 0, 0);
    if (!terrain_vs || !terrain_fs || !terrain_no_variation_fs || !terrain_shadow_vs || !grass_vs || !grass_fs || !grass_shadow_vs || !shadow_fs) {
        goto done;
    }

    lesson50_fill_terrain_vertex_input(&terrain_vb, terrain_attrs);
    state->terrain_fill_pipeline = ForgeGpuCreateLessonGraphicsPipelineWithColorTargetsAndDepthCompare(
        demo, terrain_vs, terrain_fs, SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        &color_target, 1, &terrain_vb, 1, terrain_attrs, SDL_arraysize(terrain_attrs),
        true, FORGE_GPU_PROCESSED_SCENE_DEPTH_FORMAT, true, true, SDL_GPU_COMPAREOP_LESS,
        SDL_GPU_CULLMODE_BACK, 0.0f, 0.0f);
    state->terrain_line_pipeline = ForgeGpuCreateLessonGraphicsPipelineWithColorTargetsAndDepthCompare(
        demo, terrain_vs, terrain_fs, SDL_GPU_PRIMITIVETYPE_LINELIST,
        &color_target, 1, &terrain_vb, 1, terrain_attrs, SDL_arraysize(terrain_attrs),
        true, FORGE_GPU_PROCESSED_SCENE_DEPTH_FORMAT, true, true, SDL_GPU_COMPAREOP_LESS,
        SDL_GPU_CULLMODE_NONE, 0.0f, 0.0f);
    state->terrain_no_variation_fill_pipeline = ForgeGpuCreateLessonGraphicsPipelineWithColorTargetsAndDepthCompare(
        demo, terrain_vs, terrain_no_variation_fs, SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        &color_target, 1, &terrain_vb, 1, terrain_attrs, SDL_arraysize(terrain_attrs),
        true, FORGE_GPU_PROCESSED_SCENE_DEPTH_FORMAT, true, true, SDL_GPU_COMPAREOP_LESS,
        SDL_GPU_CULLMODE_BACK, 0.0f, 0.0f);
    state->terrain_no_variation_line_pipeline = ForgeGpuCreateLessonGraphicsPipelineWithColorTargetsAndDepthCompare(
        demo, terrain_vs, terrain_no_variation_fs, SDL_GPU_PRIMITIVETYPE_LINELIST,
        &color_target, 1, &terrain_vb, 1, terrain_attrs, SDL_arraysize(terrain_attrs),
        true, FORGE_GPU_PROCESSED_SCENE_DEPTH_FORMAT, true, true, SDL_GPU_COMPAREOP_LESS,
        SDL_GPU_CULLMODE_NONE, 0.0f, 0.0f);
    state->terrain_shadow_pipeline = ForgeGpuCreateLessonGraphicsPipelineWithColorTargetsAndDepthCompare(
        demo, terrain_shadow_vs, shadow_fs, SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        nullptr, 0, &terrain_vb, 1, terrain_attrs, SDL_arraysize(terrain_attrs),
        true, FORGE_GPU_PROCESSED_SCENE_DEPTH_FORMAT, true, true, SDL_GPU_COMPAREOP_LESS,
        SDL_GPU_CULLMODE_BACK, LESSON50_SHADOW_BIAS_CONST, LESSON50_SHADOW_BIAS_SLOPE);

    lesson50_fill_grass_vertex_input(grass_vbs, grass_attrs);
    state->grass_fill_pipeline = ForgeGpuCreateLessonGraphicsPipelineWithColorTargetsAndDepthCompare(
        demo, grass_vs, grass_fs, SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        &color_target, 1, grass_vbs, SDL_arraysize(grass_vbs), grass_attrs, SDL_arraysize(grass_attrs),
        true, FORGE_GPU_PROCESSED_SCENE_DEPTH_FORMAT, true, true, SDL_GPU_COMPAREOP_LESS,
        SDL_GPU_CULLMODE_NONE, 0.0f, 0.0f);
    state->grass_line_pipeline = ForgeGpuCreateLessonGraphicsPipelineWithColorTargetsAndDepthCompare(
        demo, grass_vs, grass_fs, SDL_GPU_PRIMITIVETYPE_LINELIST,
        &color_target, 1, grass_vbs, SDL_arraysize(grass_vbs), grass_attrs, SDL_arraysize(grass_attrs),
        true, FORGE_GPU_PROCESSED_SCENE_DEPTH_FORMAT, true, true, SDL_GPU_COMPAREOP_LESS,
        SDL_GPU_CULLMODE_NONE, 0.0f, 0.0f);
    state->grass_shadow_pipeline = ForgeGpuCreateLessonGraphicsPipelineWithColorTargetsAndDepthCompare(
        demo, grass_shadow_vs, shadow_fs, SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        nullptr, 0, grass_vbs, SDL_arraysize(grass_vbs), grass_attrs, SDL_arraysize(grass_attrs),
        true, FORGE_GPU_PROCESSED_SCENE_DEPTH_FORMAT, true, true, SDL_GPU_COMPAREOP_LESS,
        SDL_GPU_CULLMODE_NONE, LESSON50_SHADOW_BIAS_CONST, LESSON50_SHADOW_BIAS_SLOPE);

    ok = state->terrain_fill_pipeline &&
        state->terrain_line_pipeline &&
        state->terrain_no_variation_fill_pipeline &&
        state->terrain_no_variation_line_pipeline &&
        state->terrain_shadow_pipeline &&
        state->grass_fill_pipeline &&
        state->grass_line_pipeline &&
        state->grass_shadow_pipeline;

done:
    lesson50_release_shader(demo->device, &terrain_vs);
    lesson50_release_shader(demo->device, &terrain_fs);
    lesson50_release_shader(demo->device, &terrain_no_variation_fs);
    lesson50_release_shader(demo->device, &terrain_shadow_vs);
    lesson50_release_shader(demo->device, &grass_vs);
    lesson50_release_shader(demo->device, &grass_fs);
    lesson50_release_shader(demo->device, &grass_shadow_vs);
    lesson50_release_shader(demo->device, &shadow_fs);
    return ok;
}

static void lesson50_update_terrain_tile_lods(Lesson50State *state, Vec3 cam_pos)
{
    const float tile_world_size = (2.0f * LESSON50_TERRAIN_SIZE) / (float)LESSON50_TERRAIN_TILE_GRID;
    const float lod_mult = state->lod_dist_mult;

    for (int row = 0; row < LESSON50_TERRAIN_TILE_GRID; row += 1) {
        for (int col = 0; col < LESSON50_TERRAIN_TILE_GRID; col += 1) {
            Lesson50TerrainTile *tile = &state->tiles[row * LESSON50_TERRAIN_TILE_GRID + col];
            const float dx = cam_pos.x - (-LESSON50_TERRAIN_SIZE + ((float)col + 0.5f) * tile_world_size);
            const float dz = cam_pos.z - (-LESSON50_TERRAIN_SIZE + ((float)row + 0.5f) * tile_world_size);
            const float dist = SDL_sqrtf(dx * dx + dz * dz);

            tile->tile_scale = tile_world_size;
            tile->offset_x = -LESSON50_TERRAIN_SIZE + ((float)col + 0.5f) * tile_world_size;
            tile->offset_z = -LESSON50_TERRAIN_SIZE + ((float)row + 0.5f) * tile_world_size;
            tile->center_x = tile->offset_x;
            tile->center_z = tile->offset_z;
            tile->lod_level = LESSON50_TERRAIN_LOD_COUNT - 1;

            for (int lod = 0; lod < LESSON50_TERRAIN_LOD_COUNT - 1; lod += 1) {
                if (dist < kLesson50TerrainLodDistances[lod] * lod_mult) {
                    tile->lod_level = lod;
                    break;
                }
            }

            if (tile->lod_level < LESSON50_TERRAIN_LOD_COUNT - 1) {
                const float threshold = kLesson50TerrainLodDistances[tile->lod_level] * lod_mult;
                const float morph_start = threshold * LESSON50_TERRAIN_MORPH_START_FRAC;
                const float range = threshold - morph_start;
                tile->morph_factor = range > 0.001f ? (dist - morph_start) / range : 0.0f;
                tile->morph_factor = SDL_max(0.0f, SDL_min(tile->morph_factor, 1.0f));
            } else {
                tile->morph_factor = 0.0f;
            }
        }
    }
}

static void lesson50_place_grass_ring(Lesson50State *state, int ring, Vec3 cam_pos, Mat4 vp)
{
    const float min_dist = kLesson50GrassRingDistMin[ring] * state->lod_dist_mult;
    const float max_dist = kLesson50GrassRingDistMax[ring] * state->lod_dist_mult;
    float step = kLesson50GrassRingStep[ring] / state->density_mult;
    Lesson50GrassInstance *out = state->grass_cpu[ring];
    Uint32 count = 0;

    step = SDL_max(step, LESSON50_GRASS_MIN_STEP);
    const float start_x = SDL_floorf((cam_pos.x - max_dist) / step) * step;
    const float start_z = SDL_floorf((cam_pos.z - max_dist) / step) * step;
    const float end_x = cam_pos.x + max_dist;
    const float end_z = cam_pos.z + max_dist;

    for (float gz = start_z; gz <= end_z; gz += step) {
        for (float gx = start_x; gx <= end_x; gx += step) {
            if (count >= LESSON50_GRASS_MAX_PER_RING) {
                state->grass_count[ring] = count;
                return;
            }
            if (gx < -LESSON50_TERRAIN_SIZE || gx > LESSON50_TERRAIN_SIZE ||
                gz < -LESSON50_TERRAIN_SIZE || gz > LESSON50_TERRAIN_SIZE) {
                continue;
            }

            const float dx = gx - cam_pos.x;
            const float dz = gz - cam_pos.z;
            const float dist = SDL_sqrtf(dx * dx + dz * dz);
            if (dist < min_dist || dist > max_dist) {
                continue;
            }

            const float h_sample = lesson50_sample_height_at(state->heightmap, LESSON50_HEIGHTMAP_SIZE, gx, gz);
            const float world_y = h_sample * state->height_scale;
            const Vec4 clip = mat4_multiply_vec4(vp, vec4_create(gx, world_y, gz, 1.0f));
            if (clip.w <= 0.001f) {
                continue;
            }
            const float ndc_x = clip.x / clip.w;
            const float ndc_y = clip.y / clip.w;
            if (ndc_x < -LESSON50_GRASS_FRUSTUM_MARGIN || ndc_x > LESSON50_GRASS_FRUSTUM_MARGIN ||
                ndc_y < -LESSON50_GRASS_FRUSTUM_MARGIN || ndc_y > LESSON50_GRASS_FRUSTUM_MARGIN) {
                continue;
            }

            const float slope = lesson50_compute_slope_at(
                state->heightmap,
                LESSON50_HEIGHTMAP_SIZE,
                state->height_scale,
                gx,
                gz);
            if (slope > state->slope_threshold) {
                continue;
            }

            const Uint32 hash = lesson50_hash2d(
                (Uint32)(int)(gx * 100.0f + 50000.0f),
                (Uint32)(int)(gz * 100.0f + 50000.0f));
            const float jx = (forge_gpu_hash_to_float(forge_gpu_hash_wang(hash)) - 0.5f) * step * 0.8f;
            const float jz = (forge_gpu_hash_to_float(forge_gpu_hash_wang(hash ^ 0x12345678u)) - 0.5f) * step * 0.8f;
            const float final_x = gx + jx;
            const float final_z = gz + jz;

            if (final_x < -LESSON50_TERRAIN_SIZE || final_x > LESSON50_TERRAIN_SIZE ||
                final_z < -LESSON50_TERRAIN_SIZE || final_z > LESSON50_TERRAIN_SIZE) {
                continue;
            }

            const float final_h = lesson50_sample_height_at(
                state->heightmap,
                LESSON50_HEIGHTMAP_SIZE,
                final_x,
                final_z) * state->height_scale;
            const float rot = forge_gpu_hash_to_float(forge_gpu_hash_wang(hash ^ 0xABCDEF01u)) * 2.0f * FORGE_GPU_PI;
            const float w_var = 0.8f + forge_gpu_hash_to_float(forge_gpu_hash_wang(hash ^ 0x87654321u)) * 0.4f;
            const float h_var = 0.7f + forge_gpu_hash_to_float(forge_gpu_hash_wang(hash ^ 0xFEDCBA98u)) * 0.6f;
            float fade = 1.0f;
            float r;
            float g;
            float b;
            const float color_hash = forge_gpu_hash_to_float(forge_gpu_hash_wang(hash ^ 0x11223344u));

            if (dist > max_dist - LESSON50_GRASS_FADE_DIST) {
                fade = 1.0f - (dist - (max_dist - LESSON50_GRASS_FADE_DIST)) / LESSON50_GRASS_FADE_DIST;
                fade = SDL_max(0.0f, fade);
            }

            if (color_hash < 0.6f) {
                r = 0.15f + color_hash * 0.1f;
                g = 0.30f + color_hash * 0.2f;
                b = 0.08f + color_hash * 0.08f;
            } else if (color_hash < 0.85f) {
                const float t = (color_hash - 0.6f) / 0.25f;
                r = 0.25f + t * 0.15f;
                g = 0.35f + t * 0.10f;
                b = 0.08f + t * 0.04f;
            } else {
                const float t = (color_hash - 0.85f) / 0.15f;
                r = 0.30f + t * 0.10f;
                g = 0.22f + t * 0.05f;
                b = 0.10f + t * 0.03f;
            }

            out[count].position[0] = final_x;
            out[count].position[1] = final_h;
            out[count].position[2] = final_z;
            out[count].rotation = rot;
            out[count].scale[0] = LESSON50_GRASS_BLADE_WIDTH * w_var;
            out[count].scale[1] = LESSON50_GRASS_BLADE_HEIGHT * h_var * fade;
            out[count].color[0] = r;
            out[count].color[1] = g;
            out[count].color[2] = b;
            out[count].pad = 0.0f;
            count += 1;
        }
    }

    state->grass_count[ring] = count;
}

static bool lesson50_upload_instances(ForgeGpuDemo *demo, SDL_GPUCommandBuffer *command_buffer, Lesson50State *state)
{
    SDL_GPUCopyPass *copy_pass = SDL_BeginGPUCopyPass(command_buffer);

    if (!copy_pass) {
        return false;
    }

    for (int ring = 0; ring < LESSON50_GRASS_RING_COUNT; ring += 1) {
        if (state->grass_count[ring] > 0) {
            SDL_GPUTransferBufferLocation source;
            SDL_GPUBufferRegion destination;
            const Uint32 size = (Uint32)((size_t)state->grass_count[ring] * sizeof(*state->grass_cpu[ring]));
            void *mapped = SDL_MapGPUTransferBuffer(demo->device, state->grass_instance_upload[ring], true);

            if (!mapped) {
                SDL_EndGPUCopyPass(copy_pass);
                return false;
            }
            SDL_memcpy(mapped, state->grass_cpu[ring], size);
            SDL_UnmapGPUTransferBuffer(demo->device, state->grass_instance_upload[ring]);

            SDL_zero(source);
            source.transfer_buffer = state->grass_instance_upload[ring];
            SDL_zero(destination);
            destination.buffer = state->grass_instance_buffer[ring];
            destination.size = size;
            SDL_UploadToGPUBuffer(copy_pass, &source, &destination, true);
        }
    }

    SDL_EndGPUCopyPass(copy_pass);
    return true;
}

static void lesson50_bind_terrain_mesh(SDL_GPURenderPass *render_pass, Lesson50TerrainLodMesh *mesh, bool use_lines)
{
    SDL_GPUBufferBinding vertex_binding;
    SDL_GPUBufferBinding index_binding;

    SDL_zero(vertex_binding);
    vertex_binding.buffer = mesh->vertex_buffer;
    SDL_BindGPUVertexBuffers(render_pass, 0, &vertex_binding, 1);

    SDL_zero(index_binding);
    index_binding.buffer = use_lines ? mesh->line_index_buffer : mesh->tri_index_buffer;
    SDL_BindGPUIndexBuffer(render_pass, &index_binding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
}

static void lesson50_set_viewport_and_scissor(SDL_GPURenderPass *render_pass, Uint32 x, Uint32 y, Uint32 width, Uint32 height)
{
    SDL_GPUViewport viewport;
    SDL_Rect scissor;

    SDL_zero(viewport);
    viewport.x = (float)x;
    viewport.y = (float)y;
    viewport.w = (float)width;
    viewport.h = (float)height;
    viewport.min_depth = 0.0f;
    viewport.max_depth = 1.0f;
    SDL_SetGPUViewport(render_pass, &viewport);

    scissor.x = (int)x;
    scissor.y = (int)y;
    scissor.w = (int)width;
    scissor.h = (int)height;
    SDL_SetGPUScissor(render_pass, &scissor);
}

static void lesson50_draw_terrain_viewport(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    Lesson50State *state,
    Mat4 camera_vp,
    Mat4 light_vp,
    bool use_lines)
{
    const Vec3 light_dir = ForgeGpuProcessedSceneLightDir();
    SDL_GPUTextureSamplerBinding vertex_sampler;
    SDL_GPUTextureSamplerBinding fragment_samplers[2];
    Lesson50TerrainFragUniforms fragment_uniforms;

    if (lesson50_uses_no_variation_pipeline(state)) {
        SDL_BindGPUGraphicsPipeline(
            render_pass,
            use_lines ?
                state->terrain_no_variation_line_pipeline :
                state->terrain_no_variation_fill_pipeline);
    } else {
        SDL_BindGPUGraphicsPipeline(render_pass, use_lines ? state->terrain_line_pipeline : state->terrain_fill_pipeline);
    }

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

    SDL_zero(fragment_uniforms);
    fragment_uniforms.light_dir[0] = light_dir.x;
    fragment_uniforms.light_dir[1] = light_dir.y;
    fragment_uniforms.light_dir[2] = light_dir.z;
    fragment_uniforms.light_intensity = LESSON50_LIGHT_INTENSITY;
    fragment_uniforms.eye_pos[0] = demo->lesson.camera_position.x;
    fragment_uniforms.eye_pos[1] = demo->lesson.camera_position.y;
    fragment_uniforms.eye_pos[2] = demo->lesson.camera_position.z;
    fragment_uniforms.ambient = LESSON50_AMBIENT;
    fragment_uniforms.height_scale = state->height_scale;
    fragment_uniforms.terrain_size = LESSON50_TERRAIN_SIZE;
    fragment_uniforms.texture_repeat = LESSON50_TERRAIN_TEXTURE_REPEAT;
    fragment_uniforms.snow_line = state->snow_line;
    fragment_uniforms.slope_threshold = state->slope_threshold;
    SDL_PushGPUFragmentUniformData(command_buffer, 0, &fragment_uniforms, sizeof(fragment_uniforms));

    for (int i = 0; i < LESSON50_TERRAIN_TILE_COUNT; i += 1) {
        const Lesson50TerrainTile *tile = &state->tiles[i];
        const int lod = tile->lod_level;
        Lesson50TerrainLodMesh *mesh = &state->terrain_lod[lod];
        const int coarse_res = lod < LESSON50_TERRAIN_LOD_COUNT - 1 ?
            kLesson50TerrainLodResolutions[lod + 1] :
            kLesson50TerrainLodResolutions[lod];
        const float global_coarse_cell = (1.0f / (float)(coarse_res - 1)) / (float)LESSON50_TERRAIN_TILE_GRID;
        Lesson50TerrainLodVertUniforms vertex_uniforms;

        SDL_zero(vertex_uniforms);
        vertex_uniforms.vp = camera_vp;
        vertex_uniforms.light_vp = light_vp;
        vertex_uniforms.terrain_size = LESSON50_TERRAIN_SIZE;
        vertex_uniforms.height_scale = state->height_scale;
        vertex_uniforms.tile_offset[0] = tile->offset_x;
        vertex_uniforms.tile_offset[1] = tile->offset_z;
        vertex_uniforms.tile_scale = tile->tile_scale;
        vertex_uniforms.morph_factor = tile->morph_factor;
        vertex_uniforms.coarse_cell_size = global_coarse_cell;
        SDL_PushGPUVertexUniformData(command_buffer, 0, &vertex_uniforms, sizeof(vertex_uniforms));

        lesson50_bind_terrain_mesh(render_pass, mesh, use_lines);
        SDL_DrawGPUIndexedPrimitives(render_pass, use_lines ? mesh->line_index_count : mesh->tri_index_count, 1, 0, 0, 0);
        if (use_lines) {
            state->terrain_line_draw_calls += 1;
        } else {
            state->terrain_fill_draw_calls += 1;
        }
    }
}

static void lesson50_draw_grass_viewport(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    Lesson50State *state,
    Mat4 camera_vp,
    Mat4 light_vp,
    bool use_lines)
{
    const Vec3 light_dir = ForgeGpuProcessedSceneLightDir();
    SDL_GPUTextureSamplerBinding shadow_sampler;
    Lesson50GrassVertUniforms vertex_uniforms;
    Lesson50GrassFragUniforms fragment_uniforms;

    SDL_BindGPUGraphicsPipeline(render_pass, use_lines ? state->grass_line_pipeline : state->grass_fill_pipeline);

    SDL_zero(shadow_sampler);
    shadow_sampler.texture = state->renderer.shadow_depth;
    shadow_sampler.sampler = state->renderer.grid_shadow_sampler;
    SDL_BindGPUFragmentSamplers(render_pass, 0, &shadow_sampler, 1);

    SDL_zero(vertex_uniforms);
    vertex_uniforms.vp = camera_vp;
    vertex_uniforms.light_vp = light_vp;
    vertex_uniforms.time = state->elapsed_time;
    vertex_uniforms.wind_strength = state->wind_strength;
    vertex_uniforms.wind_speed = state->wind_speed;
    vertex_uniforms.wind_dir[0] = LESSON50_WIND_DIR_X;
    vertex_uniforms.wind_dir[1] = LESSON50_WIND_DIR_Z;
    SDL_PushGPUVertexUniformData(command_buffer, 0, &vertex_uniforms, sizeof(vertex_uniforms));

    SDL_zero(fragment_uniforms);
    fragment_uniforms.light_dir[0] = light_dir.x;
    fragment_uniforms.light_dir[1] = light_dir.y;
    fragment_uniforms.light_dir[2] = light_dir.z;
    fragment_uniforms.light_intensity = LESSON50_LIGHT_INTENSITY;
    fragment_uniforms.eye_pos[0] = demo->lesson.camera_position.x;
    fragment_uniforms.eye_pos[1] = demo->lesson.camera_position.y;
    fragment_uniforms.eye_pos[2] = demo->lesson.camera_position.z;
    fragment_uniforms.ambient = LESSON50_AMBIENT;
    SDL_PushGPUFragmentUniformData(command_buffer, 0, &fragment_uniforms, sizeof(fragment_uniforms));

    for (int ring = 0; ring < LESSON50_GRASS_RING_COUNT; ring += 1) {
        Lesson50GrassBladeMesh *mesh = &state->grass_blades[ring];
        SDL_GPUBufferBinding vertex_bindings[2];
        SDL_GPUBufferBinding index_binding;

        if (state->grass_count[ring] == 0) {
            continue;
        }

        SDL_zeroa(vertex_bindings);
        vertex_bindings[0].buffer = mesh->vertex_buffer;
        vertex_bindings[1].buffer = state->grass_instance_buffer[ring];
        SDL_BindGPUVertexBuffers(render_pass, 0, vertex_bindings, SDL_arraysize(vertex_bindings));
        SDL_zero(index_binding);
        index_binding.buffer = use_lines ? mesh->line_index_buffer : mesh->tri_index_buffer;
        SDL_BindGPUIndexBuffer(render_pass, &index_binding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
        SDL_DrawGPUIndexedPrimitives(render_pass, use_lines ? mesh->line_index_count : mesh->tri_index_count, state->grass_count[ring], 0, 0, 0);
        if (use_lines) {
            state->grass_line_draw_calls += 1;
        } else {
            state->grass_fill_draw_calls += 1;
        }
    }
}

static void lesson50_draw_terrain_shadow(
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    Lesson50State *state,
    Mat4 light_vp)
{
    SDL_GPUTextureSamplerBinding vertex_sampler;

    SDL_BindGPUGraphicsPipeline(render_pass, state->terrain_shadow_pipeline);
    SDL_zero(vertex_sampler);
    vertex_sampler.texture = state->heightmap_texture;
    vertex_sampler.sampler = state->heightmap_sampler;
    SDL_BindGPUVertexSamplers(render_pass, 0, &vertex_sampler, 1);

    for (int i = 0; i < LESSON50_TERRAIN_TILE_COUNT; i += 1) {
        const Lesson50TerrainTile *tile = &state->tiles[i];
        const int lod = tile->lod_level;
        Lesson50TerrainLodMesh *mesh = &state->terrain_lod[lod];
        const int coarse_res = lod < LESSON50_TERRAIN_LOD_COUNT - 1 ?
            kLesson50TerrainLodResolutions[lod + 1] :
            kLesson50TerrainLodResolutions[lod];
        const float global_coarse_cell = (1.0f / (float)(coarse_res - 1)) / (float)LESSON50_TERRAIN_TILE_GRID;
        Lesson50TerrainShadowUniforms uniforms;

        SDL_zero(uniforms);
        uniforms.light_vp = light_vp;
        uniforms.terrain_size = LESSON50_TERRAIN_SIZE;
        uniforms.height_scale = state->height_scale;
        uniforms.tile_offset[0] = tile->offset_x;
        uniforms.tile_offset[1] = tile->offset_z;
        uniforms.tile_scale = tile->tile_scale;
        uniforms.morph_factor = tile->morph_factor;
        uniforms.coarse_cell_size = global_coarse_cell;
        SDL_PushGPUVertexUniformData(command_buffer, 0, &uniforms, sizeof(uniforms));

        lesson50_bind_terrain_mesh(render_pass, mesh, false);
        SDL_DrawGPUIndexedPrimitives(render_pass, mesh->tri_index_count, 1, 0, 0, 0);
        state->terrain_shadow_draw_calls += 1;
    }
}

static void lesson50_draw_grass_shadow(
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    Lesson50State *state,
    Mat4 light_vp)
{
    Lesson50GrassShadowUniforms uniforms;

    SDL_BindGPUGraphicsPipeline(render_pass, state->grass_shadow_pipeline);

    SDL_zero(uniforms);
    uniforms.light_vp = light_vp;
    uniforms.time = state->elapsed_time;
    uniforms.wind_strength = state->wind_strength;
    uniforms.wind_speed = state->wind_speed;
    uniforms.wind_dir[0] = LESSON50_WIND_DIR_X;
    uniforms.wind_dir[1] = LESSON50_WIND_DIR_Z;
    SDL_PushGPUVertexUniformData(command_buffer, 0, &uniforms, sizeof(uniforms));

    for (int ring = 0; ring < LESSON50_GRASS_SHADOW_RINGS && ring < LESSON50_GRASS_RING_COUNT; ring += 1) {
        Lesson50GrassBladeMesh *mesh = &state->grass_blades[ring];
        SDL_GPUBufferBinding vertex_bindings[2];
        SDL_GPUBufferBinding index_binding;

        if (state->grass_count[ring] == 0) {
            continue;
        }

        SDL_zeroa(vertex_bindings);
        vertex_bindings[0].buffer = mesh->vertex_buffer;
        vertex_bindings[1].buffer = state->grass_instance_buffer[ring];
        SDL_BindGPUVertexBuffers(render_pass, 0, vertex_bindings, SDL_arraysize(vertex_bindings));
        SDL_zero(index_binding);
        index_binding.buffer = mesh->tri_index_buffer;
        SDL_BindGPUIndexBuffer(render_pass, &index_binding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
        SDL_DrawGPUIndexedPrimitives(render_pass, mesh->tri_index_count, state->grass_count[ring], 0, 0, 0);
        state->grass_shadow_draw_calls += 1;
    }
}

bool ForgeGpuCreateLesson50(ForgeGpuDemo *demo)
{
    Lesson50State *state;

    if (!SDL_GPUTextureSupportsFormat(
            demo->device,
            SDL_GPU_TEXTUREFORMAT_R32_FLOAT,
            SDL_GPU_TEXTURETYPE_2D,
            SDL_GPU_TEXTUREUSAGE_SAMPLER)) {
        SDL_SetError("lesson 50 requires sampled R32_FLOAT heightmaps; shader metadata requests filterable texture/sampler binding validation");
        return false;
    }

    state = (Lesson50State *)SDL_calloc(1, sizeof(*state));
    if (!state) {
        SDL_OutOfMemory();
        return false;
    }
    demo->lesson.private_state = state;
    state->height_scale = LESSON50_DEFAULT_HEIGHT_SCALE;
    state->wind_strength = LESSON50_DEFAULT_WIND_STRENGTH;
    state->wind_speed = LESSON50_DEFAULT_WIND_SPEED;
    state->density_mult = LESSON50_DEFAULT_DENSITY_MULT;
    state->lod_dist_mult = LESSON50_DEFAULT_LOD_DIST_MULT;
    state->snow_line = LESSON50_DEFAULT_SNOW_LINE;
    state->slope_threshold = LESSON50_DEFAULT_SLOPE_THRESHOLD;

    state->heightmap = (float *)SDL_malloc((size_t)LESSON50_HEIGHTMAP_SIZE * LESSON50_HEIGHTMAP_SIZE * sizeof(float));
    if (!state->heightmap) {
        SDL_OutOfMemory();
        return false;
    }
    forge_gpu_generate_normalized_fbm_heightmap(
        state->heightmap,
        LESSON50_HEIGHTMAP_SIZE,
        LESSON50_NOISE_SEED,
        LESSON50_NOISE_FREQUENCY,
        LESSON50_NOISE_OCTAVES,
        LESSON50_NOISE_LACUNARITY,
        LESSON50_NOISE_PERSISTENCE,
        1e-6f,
        1.0f);
    state->heightmap_texture = ForgeGpuCreateR32FloatTextureFromPixels(
        demo->device,
        LESSON50_HEIGHTMAP_SIZE,
        LESSON50_HEIGHTMAP_SIZE,
        state->heightmap);
    if (!state->heightmap_texture) {
        return false;
    }

    if (!ForgeGpuProcessedSceneRendererCreate(demo, &state->renderer) ||
        !lesson50_create_samplers(demo, state) ||
        !lesson50_create_terrain_meshes(demo, state) ||
        !lesson50_create_grass_meshes(demo, state) ||
        !lesson50_create_instance_buffers(demo, state) ||
        !lesson50_create_pipelines(demo, state)) {
        return false;
    }

    demo->lesson.camera_position = { LESSON50_CAM_START_X, LESSON50_CAM_START_Y, LESSON50_CAM_START_Z };
    demo->lesson.camera_yaw = LESSON50_CAM_START_YAW;
    demo->lesson.camera_pitch = LESSON50_CAM_START_PITCH;
    demo->lesson.move_speed = LESSON50_MOVE_SPEED;
    demo->lesson.mouse_sensitivity = LESSON50_MOUSE_SENSITIVITY;
    demo->lesson.pitch_clamp = LESSON50_PITCH_CLAMP;
    demo->lesson.last_ticks = SDL_GetTicks();
    return true;
}

bool ForgeGpuRenderLesson50(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPUTexture *swapchain_texture,
    Uint32 width,
    Uint32 height)
{
    Lesson50State *state = lesson50_state(demo);
    Mat4 view;
    Mat4 projection;
    Mat4 camera_vp;
    Mat4 light_vp;
    SDL_GPURenderPass *render_pass;
    const Uint32 half_width = SDL_max(width / 2u, 1u);
    Uint32 projection_width;

    if (!state) {
        SDL_SetError("lesson 50 internal state is missing");
        return false;
    }
    if (!ForgeGpuProcessedSceneRendererEnsureMainDepth(demo, &state->renderer, width, height)) {
        return false;
    }
    projection_width = lesson50_uses_full_viewport(state) ? SDL_max(width, 1u) : half_width;

    ForgeGpuProcessedSceneRendererBeginFrame(&state->renderer);
    state->terrain_fill_draw_calls = 0;
    state->terrain_line_draw_calls = 0;
    state->terrain_shadow_draw_calls = 0;
    state->grass_fill_draw_calls = 0;
    state->grass_line_draw_calls = 0;
    state->grass_shadow_draw_calls = 0;
    state->total_grass_count = 0;
    state->elapsed_time = ForgeGpuFrameTimeSeconds(demo);

    ForgeGpuUpdateCameraFromInput(demo);
    ForgeGpuCameraViewProjection(demo, projection_width, height, LESSON50_FAR_PLANE, &view, &projection);
    camera_vp = mat4_multiply(projection, view);
    light_vp = lesson50_light_view_projection();

    lesson50_update_terrain_tile_lods(state, demo->lesson.camera_position);
    if (lesson50_draws_grass(state)) {
        for (int ring = 0; ring < LESSON50_GRASS_RING_COUNT; ring += 1) {
            lesson50_place_grass_ring(state, ring, demo->lesson.camera_position, camera_vp);
            state->total_grass_count += state->grass_count[ring];
        }
    } else {
        for (int ring = 0; ring < LESSON50_GRASS_RING_COUNT; ring += 1) {
            state->grass_count[ring] = 0;
        }
    }
    if (!lesson50_upload_instances(demo, command_buffer, state)) {
        return false;
    }

    render_pass = ForgeGpuProcessedSceneBeginShadowPass(command_buffer, &state->renderer);
    if (!render_pass) {
        return false;
    }
    if (lesson50_draws_shadow(state)) {
        lesson50_draw_terrain_shadow(command_buffer, render_pass, state, light_vp);
        if (lesson50_draws_grass(state)) {
            lesson50_draw_grass_shadow(command_buffer, render_pass, state, light_vp);
        }
    }
    SDL_EndGPURenderPass(render_pass);
    state->renderer.shadow_pass_rendered = true;

    render_pass = ForgeGpuProcessedSceneBeginMainPass(command_buffer, &state->renderer, swapchain_texture);
    if (!render_pass) {
        return false;
    }

    if (lesson50_uses_full_viewport(state)) {
        const bool use_lines = lesson50_uses_line_rendering(state);

        lesson50_set_viewport_and_scissor(render_pass, 0, 0, width, height);
        lesson50_draw_terrain_viewport(demo, command_buffer, render_pass, state, camera_vp, light_vp, use_lines);
        if (lesson50_draws_grass(state)) {
            lesson50_draw_grass_viewport(demo, command_buffer, render_pass, state, camera_vp, light_vp, use_lines);
        }
    } else {
        lesson50_set_viewport_and_scissor(render_pass, 0, 0, half_width, height);
        lesson50_draw_terrain_viewport(demo, command_buffer, render_pass, state, camera_vp, light_vp, false);
        if (lesson50_draws_grass(state)) {
            lesson50_draw_grass_viewport(demo, command_buffer, render_pass, state, camera_vp, light_vp, false);
        }

        lesson50_set_viewport_and_scissor(render_pass, half_width, 0, width - half_width, height);
        lesson50_draw_terrain_viewport(demo, command_buffer, render_pass, state, camera_vp, light_vp, true);
        if (lesson50_draws_grass(state)) {
            lesson50_draw_grass_viewport(demo, command_buffer, render_pass, state, camera_vp, light_vp, true);
        }
    }

    lesson50_set_viewport_and_scissor(render_pass, 0, 0, width, height);
    SDL_EndGPURenderPass(render_pass);
    state->renderer.main_pass_rendered = true;
    return true;
}

void ForgeGpuDebugLesson50(ForgeGpuDemo *demo)
{
    Lesson50State *state = lesson50_state(demo);

    if (!state) {
        return;
    }
    ImGui::Text("Heightmap: %ux%u R32_FLOAT", LESSON50_HEIGHTMAP_SIZE, LESSON50_HEIGHTMAP_SIZE);
    ImGui::Text("Terrain tiles: %u (%u LODs)", LESSON50_TERRAIN_TILE_COUNT, LESSON50_TERRAIN_LOD_COUNT);
    ImGui::Text("Grass: %u total  rings [%u, %u, %u, %u]",
        state->total_grass_count,
        state->grass_count[0],
        state->grass_count[1],
        state->grass_count[2],
        state->grass_count[3]);
    ImGui::Text("Draws: terrain fill %u, line %u, shadow %u",
        state->terrain_fill_draw_calls,
        state->terrain_line_draw_calls,
        state->terrain_shadow_draw_calls);
    ImGui::Text("Draws: grass fill %u, line %u, shadow %u",
        state->grass_fill_draw_calls,
        state->grass_line_draw_calls,
        state->grass_shadow_draw_calls);
}

void ForgeGpuControlsLesson50(ForgeGpuDemo *demo)
{
    Lesson50State *state = lesson50_state(demo);

    if (!state) {
        return;
    }
    ImGui::SliderFloat("Wind Strength", &state->wind_strength, 0.0f, 2.0f);
    ImGui::SliderFloat("Wind Speed", &state->wind_speed, 0.5f, 5.0f);
    ImGui::SliderFloat("Grass Density", &state->density_mult, 0.25f, 2.0f);
    ImGui::SliderFloat("LOD Distance", &state->lod_dist_mult, 0.5f, 2.0f);
    ImGui::SliderFloat("Height Scale", &state->height_scale, 1.0f, 10.0f);
    ImGui::SliderFloat("Snow Line", &state->snow_line, 0.0f, 1.0f);
    ImGui::SliderFloat("Slope Threshold", &state->slope_threshold, 0.05f, 0.6f);
}

bool ForgeGpuHandleLesson50Event(ForgeGpuDemo *demo, const SDL_Event *event)
{
    Lesson50State *state = lesson50_state(demo);

    if (!state || !demo->validation_mode ||
        event->type != SDL_EVENT_KEY_DOWN ||
        event->key.repeat) {
        return false;
    }

    switch (event->key.key) {
    case SDLK_S:
        state->diagnostic_mode = LESSON50_DIAGNOSTIC_NO_SHADOW;
        return true;
    case SDLK_V:
        state->diagnostic_mode = LESSON50_DIAGNOSTIC_NO_VARIATION;
        return true;
    case SDLK_G:
        state->diagnostic_mode = LESSON50_DIAGNOSTIC_NO_GRASS;
        return true;
    case SDLK_F:
        state->diagnostic_mode = LESSON50_DIAGNOSTIC_FILL_ONLY;
        return true;
    case SDLK_L:
        state->diagnostic_mode = LESSON50_DIAGNOSTIC_LINE_ONLY;
        return true;
    case SDLK_X:
        state->diagnostic_mode = LESSON50_DIAGNOSTIC_LINE_ONLY_NO_VARIATION;
        return true;
    default:
        return false;
    }
}

void ForgeGpuExportLesson50Metrics(ForgeGpuDemo *demo)
{
    Lesson50State *state = lesson50_state(demo);

    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson50Complete", state ? 1.0 : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson50HeightmapSize", (double)LESSON50_HEIGHTMAP_SIZE);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson50TerrainTileCount", (double)LESSON50_TERRAIN_TILE_COUNT);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson50TerrainLodCount", (double)LESSON50_TERRAIN_LOD_COUNT);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson50GrassRingCount", (double)LESSON50_GRASS_RING_COUNT);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson50GrassShadowRings", (double)LESSON50_GRASS_SHADOW_RINGS);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson50TotalGrassCount", state ? (double)state->total_grass_count : 0.0);
    for (int ring = 0; ring < LESSON50_GRASS_RING_COUNT; ring += 1) {
        char key[64];
        SDL_snprintf(key, sizeof(key), "sdlGpuForgeGpuLesson50GrassRing%dCount", ring);
        ForgeGpuBrowserSetNumberMetric(key, state ? (double)state->grass_count[ring] : 0.0);
    }
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson50TerrainFillDrawCalls", state ? (double)state->terrain_fill_draw_calls : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson50TerrainLineDrawCalls", state ? (double)state->terrain_line_draw_calls : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson50TerrainShadowDrawCalls", state ? (double)state->terrain_shadow_draw_calls : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson50GrassFillDrawCalls", state ? (double)state->grass_fill_draw_calls : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson50GrassLineDrawCalls", state ? (double)state->grass_line_draw_calls : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson50GrassShadowDrawCalls", state ? (double)state->grass_shadow_draw_calls : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson50ShadowPass", state && state->renderer.shadow_pass_rendered ? 1.0 : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson50MainPass", state && state->renderer.main_pass_rendered ? 1.0 : 0.0);
}

void ForgeGpuDestroyLesson50(ForgeGpuDemo *demo)
{
    Lesson50State *state = lesson50_state(demo);

    if (!state) {
        return;
    }
    if (state->terrain_fill_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, state->terrain_fill_pipeline);
    }
    if (state->terrain_line_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, state->terrain_line_pipeline);
    }
    if (state->terrain_no_variation_fill_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, state->terrain_no_variation_fill_pipeline);
    }
    if (state->terrain_no_variation_line_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, state->terrain_no_variation_line_pipeline);
    }
    if (state->terrain_shadow_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, state->terrain_shadow_pipeline);
    }
    if (state->grass_fill_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, state->grass_fill_pipeline);
    }
    if (state->grass_line_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, state->grass_line_pipeline);
    }
    if (state->grass_shadow_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, state->grass_shadow_pipeline);
    }
    for (int lod = 0; lod < LESSON50_TERRAIN_LOD_COUNT; lod += 1) {
        if (state->terrain_lod[lod].vertex_buffer) {
            SDL_ReleaseGPUBuffer(demo->device, state->terrain_lod[lod].vertex_buffer);
        }
        if (state->terrain_lod[lod].tri_index_buffer) {
            SDL_ReleaseGPUBuffer(demo->device, state->terrain_lod[lod].tri_index_buffer);
        }
        if (state->terrain_lod[lod].line_index_buffer) {
            SDL_ReleaseGPUBuffer(demo->device, state->terrain_lod[lod].line_index_buffer);
        }
    }
    for (int ring = 0; ring < LESSON50_GRASS_RING_COUNT; ring += 1) {
        if (state->grass_blades[ring].vertex_buffer) {
            SDL_ReleaseGPUBuffer(demo->device, state->grass_blades[ring].vertex_buffer);
        }
        if (state->grass_blades[ring].tri_index_buffer) {
            SDL_ReleaseGPUBuffer(demo->device, state->grass_blades[ring].tri_index_buffer);
        }
        if (state->grass_blades[ring].line_index_buffer) {
            SDL_ReleaseGPUBuffer(demo->device, state->grass_blades[ring].line_index_buffer);
        }
        if (state->grass_instance_buffer[ring]) {
            SDL_ReleaseGPUBuffer(demo->device, state->grass_instance_buffer[ring]);
        }
        if (state->grass_instance_upload[ring]) {
            SDL_ReleaseGPUTransferBuffer(demo->device, state->grass_instance_upload[ring]);
        }
        SDL_free(state->grass_cpu[ring]);
    }
    if (state->heightmap_texture) {
        SDL_ReleaseGPUTexture(demo->device, state->heightmap_texture);
    }
    if (state->heightmap_sampler) {
        SDL_ReleaseGPUSampler(demo->device, state->heightmap_sampler);
    }
    SDL_free(state->heightmap);
    ForgeGpuProcessedSceneRendererDestroy(demo, &state->renderer);
    SDL_free(state);
    demo->lesson.private_state = nullptr;
}
