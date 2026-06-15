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
#include "shaders/generated/forge_gpu_shared_scene_shaders.h"

#include <stddef.h>

#include "imgui.h"

#define LESSON48_HEIGHTMAP_SIZE 256
#define LESSON48_TERRAIN_SIZE 20.0f
#define LESSON48_NOISE_SEED 42u
#define LESSON48_NOISE_OCTAVES 6
#define LESSON48_NOISE_FREQUENCY 2.0f
#define LESSON48_NOISE_LACUNARITY 2.0f
#define LESSON48_NOISE_PERSISTENCE 0.5f
#define LESSON48_TERRAIN_HEIGHT_SCALE 8.0f
#define LESSON48_TREE_COUNT_MAX 2000
#define LESSON48_TREE_BILLBOARD_W 0.3f
#define LESSON48_TREE_BILLBOARD_H 0.6f
#define LESSON48_SLOPE_TREE_THRESHOLD 0.4f
#define LESSON48_DEFAULT_SNOW_LINE 0.7f
#define LESSON48_DEFAULT_SLOPE_THRESHOLD 0.3f
#define LESSON48_DEFAULT_TEXTURE_REPEAT 8.0f
#define LESSON48_FAR_PLANE 200.0f
#define LESSON48_MOVE_SPEED 5.0f
#define LESSON48_MOUSE_SENSITIVITY 0.003f
#define LESSON48_PITCH_CLAMP 1.5f
#define LESSON48_CAM_START_X 0.0f
#define LESSON48_CAM_START_Y 12.0f
#define LESSON48_CAM_START_Z 18.0f
#define LESSON48_CAM_START_YAW 0.0f
#define LESSON48_CAM_START_PITCH (-0.45f)
#define LESSON48_AMBIENT 0.15f
#define LESSON48_SHININESS 32.0f
#define LESSON48_SPECULAR_STRENGTH 0.5f
#define LESSON48_LIGHT_INTENSITY 1.2f
#define LESSON48_SHADOW_BIAS_CONST 2.0f
#define LESSON48_SHADOW_BIAS_SLOPE 2.0f

typedef struct Lesson48TerrainVertex
{
    float position[3];
    float uv[2];
} Lesson48TerrainVertex;

typedef struct Lesson48TreeVertex
{
    float position[3];
    float normal[3];
} Lesson48TreeVertex;

typedef struct Lesson48TreeInstance
{
    Mat4 transform;
    float color[4];
} Lesson48TreeInstance;

typedef struct Lesson48TerrainVertUniforms
{
    Mat4 vp;
    Mat4 light_vp;
    float terrain_size;
    float height_scale;
    float pad[2];
} Lesson48TerrainVertUniforms;

typedef struct Lesson48TerrainShadowVertUniforms
{
    Mat4 light_vp;
    float terrain_size;
    float height_scale;
    float pad[2];
} Lesson48TerrainShadowVertUniforms;

typedef struct Lesson48TerrainFragUniforms
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
} Lesson48TerrainFragUniforms;

typedef struct Lesson48InstancedVertUniforms
{
    Mat4 vp;
    Mat4 light_vp;
    Mat4 node_world;
} Lesson48InstancedVertUniforms;

typedef struct Lesson48InstancedShadowVertUniforms
{
    Mat4 light_vp;
    Mat4 node_world;
} Lesson48InstancedShadowVertUniforms;

typedef struct Lesson48TreeFragUniforms
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
} Lesson48TreeFragUniforms;

typedef enum Lesson48DiagnosticMode
{
    LESSON48_DIAGNOSTIC_DEFAULT,
    LESSON48_DIAGNOSTIC_NO_SHADOW,
    LESSON48_DIAGNOSTIC_NO_VARIATION,
    LESSON48_DIAGNOSTIC_TERRAIN_ONLY,
    LESSON48_DIAGNOSTIC_TREES_ONLY
} Lesson48DiagnosticMode;

typedef struct Lesson48State
{
    ForgeGpuProcessedSceneRenderer renderer;
    SDL_GPUGraphicsPipeline *terrain_pipeline;
    SDL_GPUGraphicsPipeline *terrain_no_variation_pipeline;
    SDL_GPUGraphicsPipeline *terrain_shadow_pipeline;
    SDL_GPUGraphicsPipeline *tree_pipeline;
    SDL_GPUGraphicsPipeline *tree_shadow_pipeline;
    SDL_GPUBuffer *terrain_vertex_buffer;
    SDL_GPUBuffer *terrain_index_buffer;
    SDL_GPUBuffer *tree_vertex_buffer;
    SDL_GPUBuffer *tree_index_buffer;
    SDL_GPUBuffer *tree_instance_buffer;
    SDL_GPUTransferBuffer *tree_instance_upload;
    SDL_GPUTexture *heightmap_texture;
    SDL_GPUSampler *heightmap_sampler;
    float *heightmap;
    Lesson48TreeInstance *tree_instances;
    Uint32 terrain_index_count;
    Uint32 tree_index_count;
    Uint32 tree_count;
    Uint32 terrain_draw_calls;
    Uint32 tree_draw_calls;
    Uint32 shadow_draw_calls;
    float height_scale;
    float snow_line;
    float slope_threshold;
    Lesson48DiagnosticMode diagnostic_mode;
    bool tree_dirty;
} Lesson48State;

static_assert(sizeof(Lesson48TerrainVertex) == 20, "lesson 48 terrain vertex size must match HLSL layout");
static_assert(sizeof(Lesson48TreeVertex) == 24, "lesson 48 tree vertex size must match HLSL layout");
static_assert(sizeof(Lesson48TreeInstance) == 80, "lesson 48 tree instance size must match HLSL layout");
static_assert(sizeof(Lesson48TerrainVertUniforms) == 144, "lesson 48 terrain vertex uniform size must match HLSL layout");
static_assert(sizeof(Lesson48TerrainShadowVertUniforms) == 80, "lesson 48 terrain shadow uniform size must match HLSL layout");
static_assert(sizeof(Lesson48TerrainFragUniforms) == 64, "lesson 48 terrain fragment uniform size must match HLSL layout");
static_assert(sizeof(Lesson48InstancedVertUniforms) == 192, "lesson 48 tree vertex uniform size must match HLSL layout");
static_assert(sizeof(Lesson48InstancedShadowVertUniforms) == 128, "lesson 48 tree shadow uniform size must match HLSL layout");
static_assert(sizeof(Lesson48TreeFragUniforms) == 80, "lesson 48 tree fragment uniform size must match HLSL layout");

static Lesson48State *lesson48_state(ForgeGpuDemo *demo)
{
    return (Lesson48State *)demo->lesson.private_state;
}

static bool lesson48_draws_terrain(const Lesson48State *state)
{
    return state->diagnostic_mode != LESSON48_DIAGNOSTIC_TREES_ONLY;
}

static bool lesson48_draws_trees(const Lesson48State *state)
{
    return state->diagnostic_mode != LESSON48_DIAGNOSTIC_TERRAIN_ONLY;
}

static bool lesson48_draws_shadow(const Lesson48State *state)
{
    return state->diagnostic_mode != LESSON48_DIAGNOSTIC_NO_SHADOW;
}

static void lesson48_release_shader(SDL_GPUDevice *device, SDL_GPUShader **shader)
{
    if (*shader) {
        SDL_ReleaseGPUShader(device, *shader);
        *shader = nullptr;
    }
}

static float lesson48_slope_at(const float *hmap, int size, int x, int y, float height_scale)
{
    const int xm = x > 0 ? x - 1 : 0;
    const int xp = x < size - 1 ? x + 1 : size - 1;
    const int ym = y > 0 ? y - 1 : 0;
    const int yp = y < size - 1 ? y + 1 : size - 1;
    const float dx = (hmap[y * size + xp] - hmap[y * size + xm]) * 0.5f;
    const float dy = (hmap[yp * size + x] - hmap[ym * size + x]) * 0.5f;
    const float texel_spacing = (LESSON48_TERRAIN_SIZE * 2.0f) / (float)(size - 1);
    const float scale = height_scale / texel_spacing;

    return SDL_sqrtf(dx * dx + dy * dy) * scale;
}

static Uint32 lesson48_place_trees(
    const float *hmap,
    int size,
    float slope_threshold,
    float height_scale,
    Lesson48TreeInstance *instances,
    int max_count)
{
    int count = 0;
    int step = size / (int)SDL_sqrtf((float)max_count);

    if (step < 2) {
        step = 2;
    }

    for (int ty = step; ty < size - step && count < max_count; ty += step) {
        for (int tx = step; tx < size - step && count < max_count; tx += step) {
            const float slope = lesson48_slope_at(hmap, size, tx, ty, height_scale);
            if (slope >= slope_threshold) {
                continue;
            }

            const Uint32 hash = forge_gpu_hash_wang((Uint32)(ty * size + tx));
            const float jx = ((float)(hash & 0xffu) / 255.0f - 0.5f) * (float)step;
            const float jy = ((float)((hash >> 8) & 0xffu) / 255.0f - 0.5f) * (float)step;
            int fx = (int)((float)tx + jx);
            int fy = (int)((float)ty + jy);

            fx = SDL_clamp(fx, 0, size - 1);
            fy = SDL_clamp(fy, 0, size - 1);

            const float wx = ((float)fx / (float)(size - 1)) * 2.0f * LESSON48_TERRAIN_SIZE - LESSON48_TERRAIN_SIZE;
            const float wz = ((float)fy / (float)(size - 1)) * 2.0f * LESSON48_TERRAIN_SIZE - LESSON48_TERRAIN_SIZE;
            const float wy = hmap[fy * size + fx] * height_scale;
            const Mat4 translate = mat4_translate({ wx, wy, wz });
            const Mat4 scale = mat4_scale_vec3({
                LESSON48_TREE_BILLBOARD_W * 0.5f,
                LESSON48_TREE_BILLBOARD_H * 0.5f,
                LESSON48_TREE_BILLBOARD_W * 0.5f });
            const float green_var = 0.35f + ((float)((hash >> 16) & 0xffu) / 255.0f) * 0.2f;
            const float dark_var = 0.05f + ((float)((hash >> 24) & 0xffu) / 255.0f) * 0.1f;

            instances[count].transform = mat4_multiply(translate, scale);
            instances[count].color[0] = dark_var;
            instances[count].color[1] = green_var;
            instances[count].color[2] = dark_var * 0.5f;
            instances[count].color[3] = 1.0f;
            count += 1;
        }
    }

    return (Uint32)count;
}

static bool lesson48_create_terrain_mesh(ForgeGpuDemo *demo, Lesson48State *state)
{
    ForgeGpuShapeMesh plane;
    Lesson48TerrainVertex *vertices = nullptr;
    Uint16 *indices = nullptr;
    bool ok = false;

    SDL_zero(plane);
    if (!ForgeGpuCreatePlaneShapeMesh(LESSON48_HEIGHTMAP_SIZE - 1, LESSON48_HEIGHTMAP_SIZE - 1, &plane)) {
        return false;
    }
    if ((Uint32)plane.vertex_count > (Uint32)SDL_MAX_UINT16 + 1u) {
        SDL_SetError("lesson 48 terrain has too many vertices for 16-bit indices");
        goto done;
    }

    vertices = (Lesson48TerrainVertex *)SDL_calloc((size_t)plane.vertex_count, sizeof(*vertices));
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

static bool lesson48_create_tree_mesh(ForgeGpuDemo *demo, Lesson48State *state)
{
    const Lesson48TreeVertex vertices[8] = {
        { { -1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } },
        { { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } },
        { { 1.0f, 2.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } },
        { { -1.0f, 2.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } },
        { { 0.0f, 0.0f, -1.0f }, { 1.0f, 0.0f, 0.0f } },
        { { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f } },
        { { 0.0f, 2.0f, 1.0f }, { 1.0f, 0.0f, 0.0f } },
        { { 0.0f, 2.0f, -1.0f }, { 1.0f, 0.0f, 0.0f } },
    };
    const Uint32 indices[24] = {
        0, 1, 2, 0, 2, 3,
        0, 2, 1, 0, 3, 2,
        4, 5, 6, 4, 6, 7,
        4, 6, 5, 4, 7, 6,
    };
    SDL_GPUBufferCreateInfo instance_buffer_info;
    SDL_GPUTransferBufferCreateInfo transfer_info;

    state->tree_vertex_buffer = ForgeGpuCreateBufferWithData(
        demo->device,
        SDL_GPU_BUFFERUSAGE_VERTEX,
        vertices,
        sizeof(vertices));
    state->tree_index_buffer = ForgeGpuCreateBufferWithData(
        demo->device,
        SDL_GPU_BUFFERUSAGE_INDEX,
        indices,
        sizeof(indices));
    state->tree_index_count = SDL_arraysize(indices);
    if (!state->tree_vertex_buffer || !state->tree_index_buffer) {
        return false;
    }

    SDL_zero(instance_buffer_info);
    instance_buffer_info.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
    instance_buffer_info.size = (Uint32)(sizeof(Lesson48TreeInstance) * LESSON48_TREE_COUNT_MAX);
    state->tree_instance_buffer = SDL_CreateGPUBuffer(demo->device, &instance_buffer_info);
    if (!state->tree_instance_buffer) {
        return false;
    }

    SDL_zero(transfer_info);
    transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transfer_info.size = instance_buffer_info.size;
    state->tree_instance_upload = SDL_CreateGPUTransferBuffer(demo->device, &transfer_info);
    return state->tree_instance_upload != nullptr;
}

static void lesson48_fill_terrain_vertex_input(
    SDL_GPUVertexBufferDescription *vertex_buffer,
    SDL_GPUVertexAttribute attributes[2])
{
    SDL_zero(*vertex_buffer);
    vertex_buffer->slot = 0;
    vertex_buffer->pitch = sizeof(Lesson48TerrainVertex);
    vertex_buffer->input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

    SDL_memset(attributes, 0, sizeof(*attributes) * 2);
    attributes[0].location = 0;
    attributes[0].buffer_slot = 0;
    attributes[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    attributes[0].offset = offsetof(Lesson48TerrainVertex, position);
    attributes[1].location = 1;
    attributes[1].buffer_slot = 0;
    attributes[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    attributes[1].offset = offsetof(Lesson48TerrainVertex, uv);
}

static void lesson48_fill_tree_vertex_input(
    SDL_GPUVertexBufferDescription vertex_buffers[2],
    SDL_GPUVertexAttribute attributes[7])
{
    SDL_memset(vertex_buffers, 0, sizeof(*vertex_buffers) * 2);
    vertex_buffers[0].slot = 0;
    vertex_buffers[0].pitch = sizeof(Lesson48TreeVertex);
    vertex_buffers[0].input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    vertex_buffers[1].slot = 1;
    vertex_buffers[1].pitch = sizeof(Lesson48TreeInstance);
    vertex_buffers[1].input_rate = SDL_GPU_VERTEXINPUTRATE_INSTANCE;
    vertex_buffers[1].instance_step_rate = 0;

    SDL_memset(attributes, 0, sizeof(*attributes) * 7);
    attributes[0].location = 0;
    attributes[0].buffer_slot = 0;
    attributes[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    attributes[0].offset = offsetof(Lesson48TreeVertex, position);
    attributes[1].location = 1;
    attributes[1].buffer_slot = 0;
    attributes[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    attributes[1].offset = offsetof(Lesson48TreeVertex, normal);
    for (int i = 0; i < 4; i += 1) {
        attributes[2 + i].location = (Uint32)(2 + i);
        attributes[2 + i].buffer_slot = 1;
        attributes[2 + i].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
        attributes[2 + i].offset = (Uint32)(sizeof(float) * 4 * i);
    }
    attributes[6].location = 6;
    attributes[6].buffer_slot = 1;
    attributes[6].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
    attributes[6].offset = offsetof(Lesson48TreeInstance, color);
}

static void lesson48_fill_tree_shadow_vertex_input(
    SDL_GPUVertexBufferDescription vertex_buffers[2],
    SDL_GPUVertexAttribute attributes[5])
{
    SDL_memset(vertex_buffers, 0, sizeof(*vertex_buffers) * 2);
    vertex_buffers[0].slot = 0;
    vertex_buffers[0].pitch = sizeof(Lesson48TreeVertex);
    vertex_buffers[0].input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    vertex_buffers[1].slot = 1;
    vertex_buffers[1].pitch = sizeof(Lesson48TreeInstance);
    vertex_buffers[1].input_rate = SDL_GPU_VERTEXINPUTRATE_INSTANCE;
    vertex_buffers[1].instance_step_rate = 0;

    SDL_memset(attributes, 0, sizeof(*attributes) * 5);
    attributes[0].location = 0;
    attributes[0].buffer_slot = 0;
    attributes[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    attributes[0].offset = offsetof(Lesson48TreeVertex, position);
    for (int i = 0; i < 4; i += 1) {
        attributes[1 + i].location = (Uint32)(1 + i);
        attributes[1 + i].buffer_slot = 1;
        attributes[1 + i].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
        attributes[1 + i].offset = (Uint32)(sizeof(float) * 4 * i);
    }
}

static bool lesson48_create_pipelines(ForgeGpuDemo *demo, Lesson48State *state)
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

    lesson48_fill_terrain_vertex_input(&terrain_vb, terrain_attrs);
    state->terrain_pipeline = ForgeGpuCreateLessonGraphicsPipelineWithColorTargetsAndDepthCompare(
        demo,
        terrain_vs,
        terrain_fs,
        SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        &color_target,
        1,
        &terrain_vb,
        1,
        terrain_attrs,
        SDL_arraysize(terrain_attrs),
        true,
        FORGE_GPU_PROCESSED_SCENE_DEPTH_FORMAT,
        true,
        true,
        SDL_GPU_COMPAREOP_LESS,
        SDL_GPU_CULLMODE_BACK,
        0.0f,
        0.0f);
    state->terrain_no_variation_pipeline = ForgeGpuCreateLessonGraphicsPipelineWithColorTargetsAndDepthCompare(
        demo,
        terrain_vs,
        terrain_no_variation_fs,
        SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        &color_target,
        1,
        &terrain_vb,
        1,
        terrain_attrs,
        SDL_arraysize(terrain_attrs),
        true,
        FORGE_GPU_PROCESSED_SCENE_DEPTH_FORMAT,
        true,
        true,
        SDL_GPU_COMPAREOP_LESS,
        SDL_GPU_CULLMODE_BACK,
        0.0f,
        0.0f);
    state->terrain_shadow_pipeline = ForgeGpuCreateLessonGraphicsPipelineWithColorTargetsAndDepthCompare(
        demo,
        terrain_shadow_vs,
        shadow_fs,
        SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        nullptr,
        0,
        &terrain_vb,
        1,
        terrain_attrs,
        SDL_arraysize(terrain_attrs),
        true,
        FORGE_GPU_PROCESSED_SCENE_DEPTH_FORMAT,
        true,
        true,
        SDL_GPU_COMPAREOP_LESS,
        SDL_GPU_CULLMODE_BACK,
        LESSON48_SHADOW_BIAS_CONST,
        LESSON48_SHADOW_BIAS_SLOPE);

    lesson48_fill_tree_vertex_input(tree_vbs, tree_attrs);
    state->tree_pipeline = ForgeGpuCreateLessonGraphicsPipelineWithColorTargetsAndDepthCompare(
        demo,
        tree_vs,
        tree_fs,
        SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        &color_target,
        1,
        tree_vbs,
        SDL_arraysize(tree_vbs),
        tree_attrs,
        SDL_arraysize(tree_attrs),
        true,
        FORGE_GPU_PROCESSED_SCENE_DEPTH_FORMAT,
        true,
        true,
        SDL_GPU_COMPAREOP_LESS,
        SDL_GPU_CULLMODE_BACK,
        0.0f,
        0.0f);

    lesson48_fill_tree_shadow_vertex_input(tree_shadow_vbs, tree_shadow_attrs);
    state->tree_shadow_pipeline = ForgeGpuCreateLessonGraphicsPipelineWithColorTargetsAndDepthCompare(
        demo,
        tree_shadow_vs,
        shadow_fs,
        SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        nullptr,
        0,
        tree_shadow_vbs,
        SDL_arraysize(tree_shadow_vbs),
        tree_shadow_attrs,
        SDL_arraysize(tree_shadow_attrs),
        true,
        FORGE_GPU_PROCESSED_SCENE_DEPTH_FORMAT,
        true,
        true,
        SDL_GPU_COMPAREOP_LESS,
        SDL_GPU_CULLMODE_NONE,
        LESSON48_SHADOW_BIAS_CONST,
        LESSON48_SHADOW_BIAS_SLOPE);

    ok = state->terrain_pipeline && state->terrain_no_variation_pipeline && state->terrain_shadow_pipeline && state->tree_pipeline && state->tree_shadow_pipeline;

done:
    lesson48_release_shader(demo->device, &terrain_vs);
    lesson48_release_shader(demo->device, &terrain_fs);
    lesson48_release_shader(demo->device, &terrain_no_variation_fs);
    lesson48_release_shader(demo->device, &terrain_shadow_vs);
    lesson48_release_shader(demo->device, &tree_vs);
    lesson48_release_shader(demo->device, &tree_fs);
    lesson48_release_shader(demo->device, &tree_shadow_vs);
    lesson48_release_shader(demo->device, &shadow_fs);
    return ok;
}

static bool lesson48_create_samplers(ForgeGpuDemo *demo, Lesson48State *state)
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

static bool lesson48_update_tree_instances(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    Lesson48State *state)
{
    void *mapped;
    SDL_GPUCopyPass *copy_pass;
    SDL_GPUTransferBufferLocation source;
    SDL_GPUBufferRegion destination;

    if (!state->tree_dirty) {
        return true;
    }

    state->tree_count = lesson48_place_trees(
        state->heightmap,
        LESSON48_HEIGHTMAP_SIZE,
        LESSON48_SLOPE_TREE_THRESHOLD,
        state->height_scale,
        state->tree_instances,
        LESSON48_TREE_COUNT_MAX);

    if (state->tree_count == 0) {
        state->tree_dirty = false;
        return true;
    }

    mapped = SDL_MapGPUTransferBuffer(demo->device, state->tree_instance_upload, true);
    if (!mapped) {
        return false;
    }
    SDL_memcpy(mapped, state->tree_instances, (size_t)state->tree_count * sizeof(*state->tree_instances));
    SDL_UnmapGPUTransferBuffer(demo->device, state->tree_instance_upload);

    copy_pass = SDL_BeginGPUCopyPass(command_buffer);
    if (!copy_pass) {
        return false;
    }

    SDL_zero(source);
    source.transfer_buffer = state->tree_instance_upload;
    SDL_zero(destination);
    destination.buffer = state->tree_instance_buffer;
    destination.size = (Uint32)((size_t)state->tree_count * sizeof(*state->tree_instances));
    SDL_UploadToGPUBuffer(copy_pass, &source, &destination, true);
    SDL_EndGPUCopyPass(copy_pass);
    state->tree_dirty = false;
    return true;
}

static void lesson48_bind_terrain_buffers(SDL_GPURenderPass *render_pass, Lesson48State *state)
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

static void lesson48_bind_tree_buffers(SDL_GPURenderPass *render_pass, Lesson48State *state)
{
    SDL_GPUBufferBinding vertex_bindings[2];
    SDL_GPUBufferBinding index_binding;

    SDL_zeroa(vertex_bindings);
    vertex_bindings[0].buffer = state->tree_vertex_buffer;
    vertex_bindings[1].buffer = state->tree_instance_buffer;
    SDL_BindGPUVertexBuffers(render_pass, 0, vertex_bindings, SDL_arraysize(vertex_bindings));

    SDL_zero(index_binding);
    index_binding.buffer = state->tree_index_buffer;
    SDL_BindGPUIndexBuffer(render_pass, &index_binding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
}

static void lesson48_draw_terrain_shadow(
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    Lesson48State *state,
    Mat4 light_vp)
{
    SDL_GPUTextureSamplerBinding vertex_sampler;
    Lesson48TerrainShadowVertUniforms uniforms;

    SDL_BindGPUGraphicsPipeline(render_pass, state->terrain_shadow_pipeline);

    SDL_zero(vertex_sampler);
    vertex_sampler.texture = state->heightmap_texture;
    vertex_sampler.sampler = state->heightmap_sampler;
    SDL_BindGPUVertexSamplers(render_pass, 0, &vertex_sampler, 1);

    lesson48_bind_terrain_buffers(render_pass, state);

    uniforms.light_vp = light_vp;
    uniforms.terrain_size = LESSON48_TERRAIN_SIZE;
    uniforms.height_scale = state->height_scale;
    uniforms.pad[0] = 0.0f;
    uniforms.pad[1] = 0.0f;
    SDL_PushGPUVertexUniformData(command_buffer, 0, &uniforms, sizeof(uniforms));
    SDL_DrawGPUIndexedPrimitives(render_pass, state->terrain_index_count, 1, 0, 0, 0);
    state->shadow_draw_calls += 1;
}

static void lesson48_draw_tree_shadow(
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    Lesson48State *state,
    Mat4 light_vp)
{
    Lesson48InstancedShadowVertUniforms uniforms;

    if (state->tree_count == 0) {
        return;
    }

    SDL_BindGPUGraphicsPipeline(render_pass, state->tree_shadow_pipeline);
    lesson48_bind_tree_buffers(render_pass, state);
    uniforms.light_vp = light_vp;
    uniforms.node_world = mat4_identity();
    SDL_PushGPUVertexUniformData(command_buffer, 0, &uniforms, sizeof(uniforms));
    SDL_DrawGPUIndexedPrimitives(render_pass, state->tree_index_count, state->tree_count, 0, 0, 0);
    state->shadow_draw_calls += 1;
}

static void lesson48_draw_terrain(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    Lesson48State *state,
    Mat4 camera_vp,
    Mat4 light_vp)
{
    const Vec3 light_dir = ForgeGpuProcessedSceneLightDir();
    SDL_GPUTextureSamplerBinding vertex_sampler;
    SDL_GPUTextureSamplerBinding fragment_samplers[2];
    Lesson48TerrainVertUniforms vertex_uniforms;
    Lesson48TerrainFragUniforms fragment_uniforms;

    SDL_BindGPUGraphicsPipeline(
        render_pass,
        state->diagnostic_mode == LESSON48_DIAGNOSTIC_NO_VARIATION ?
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

    lesson48_bind_terrain_buffers(render_pass, state);

    vertex_uniforms.vp = camera_vp;
    vertex_uniforms.light_vp = light_vp;
    vertex_uniforms.terrain_size = LESSON48_TERRAIN_SIZE;
    vertex_uniforms.height_scale = state->height_scale;
    vertex_uniforms.pad[0] = 0.0f;
    vertex_uniforms.pad[1] = 0.0f;
    SDL_PushGPUVertexUniformData(command_buffer, 0, &vertex_uniforms, sizeof(vertex_uniforms));

    SDL_zero(fragment_uniforms);
    fragment_uniforms.light_dir[0] = light_dir.x;
    fragment_uniforms.light_dir[1] = light_dir.y;
    fragment_uniforms.light_dir[2] = light_dir.z;
    fragment_uniforms.light_intensity = LESSON48_LIGHT_INTENSITY;
    fragment_uniforms.eye_pos[0] = demo->lesson.camera_position.x;
    fragment_uniforms.eye_pos[1] = demo->lesson.camera_position.y;
    fragment_uniforms.eye_pos[2] = demo->lesson.camera_position.z;
    fragment_uniforms.ambient = LESSON48_AMBIENT;
    fragment_uniforms.height_scale = state->height_scale;
    fragment_uniforms.terrain_size = LESSON48_TERRAIN_SIZE;
    fragment_uniforms.texture_repeat = LESSON48_DEFAULT_TEXTURE_REPEAT;
    fragment_uniforms.snow_line = state->snow_line;
    fragment_uniforms.slope_threshold = state->slope_threshold;
    SDL_PushGPUFragmentUniformData(command_buffer, 0, &fragment_uniforms, sizeof(fragment_uniforms));

    SDL_DrawGPUIndexedPrimitives(render_pass, state->terrain_index_count, 1, 0, 0, 0);
    state->terrain_draw_calls += 1;
}

static void lesson48_draw_trees(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    Lesson48State *state,
    Mat4 camera_vp,
    Mat4 light_vp)
{
    static const float kLightColor[3] = { 1.0f, 0.95f, 0.9f };
    const Vec3 light_dir = ForgeGpuProcessedSceneLightDir();
    SDL_GPUTextureSamplerBinding shadow_sampler;
    Lesson48InstancedVertUniforms vertex_uniforms;
    Lesson48TreeFragUniforms fragment_uniforms;

    if (state->tree_count == 0) {
        return;
    }

    SDL_BindGPUGraphicsPipeline(render_pass, state->tree_pipeline);

    SDL_zero(shadow_sampler);
    shadow_sampler.texture = state->renderer.shadow_depth;
    shadow_sampler.sampler = state->renderer.grid_shadow_sampler;
    SDL_BindGPUFragmentSamplers(render_pass, 0, &shadow_sampler, 1);

    lesson48_bind_tree_buffers(render_pass, state);

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
    fragment_uniforms.ambient = LESSON48_AMBIENT;
    fragment_uniforms.light_dir[0] = light_dir.x;
    fragment_uniforms.light_dir[1] = light_dir.y;
    fragment_uniforms.light_dir[2] = light_dir.z;
    fragment_uniforms.light_dir[3] = 0.0f;
    SDL_memcpy(fragment_uniforms.light_color, kLightColor, sizeof(fragment_uniforms.light_color));
    fragment_uniforms.light_intensity = LESSON48_LIGHT_INTENSITY;
    fragment_uniforms.shininess = LESSON48_SHININESS;
    fragment_uniforms.specular_strength = LESSON48_SPECULAR_STRENGTH;
    SDL_PushGPUFragmentUniformData(command_buffer, 0, &fragment_uniforms, sizeof(fragment_uniforms));

    SDL_DrawGPUIndexedPrimitives(render_pass, state->tree_index_count, state->tree_count, 0, 0, 0);
    state->tree_draw_calls += 1;
}

bool ForgeGpuCreateLesson48(ForgeGpuDemo *demo)
{
    Lesson48State *state;

    if (!SDL_GPUTextureSupportsFormat(
            demo->device,
            SDL_GPU_TEXTUREFORMAT_R32_FLOAT,
            SDL_GPU_TEXTURETYPE_2D,
            SDL_GPU_TEXTUREUSAGE_SAMPLER)) {
        SDL_SetError("lesson 48 requires sampled R32_FLOAT heightmaps; shader metadata requests filterable texture/sampler binding validation");
        return false;
    }

    state = (Lesson48State *)SDL_calloc(1, sizeof(*state));
    if (!state) {
        SDL_OutOfMemory();
        return false;
    }
    demo->lesson.private_state = state;
    state->height_scale = LESSON48_TERRAIN_HEIGHT_SCALE;
    state->snow_line = LESSON48_DEFAULT_SNOW_LINE;
    state->slope_threshold = LESSON48_DEFAULT_SLOPE_THRESHOLD;

    state->heightmap = (float *)SDL_malloc((size_t)LESSON48_HEIGHTMAP_SIZE * LESSON48_HEIGHTMAP_SIZE * sizeof(float));
    state->tree_instances = (Lesson48TreeInstance *)SDL_calloc(LESSON48_TREE_COUNT_MAX, sizeof(*state->tree_instances));
    if (!state->heightmap || !state->tree_instances) {
        SDL_OutOfMemory();
        return false;
    }
    forge_gpu_generate_normalized_fbm_heightmap(
        state->heightmap,
        LESSON48_HEIGHTMAP_SIZE,
        LESSON48_NOISE_SEED,
        LESSON48_NOISE_FREQUENCY,
        LESSON48_NOISE_OCTAVES,
        LESSON48_NOISE_LACUNARITY,
        LESSON48_NOISE_PERSISTENCE,
        1e-6f,
        1.0f);
    state->heightmap_texture = ForgeGpuCreateR32FloatTextureFromPixels(
        demo->device,
        LESSON48_HEIGHTMAP_SIZE,
        LESSON48_HEIGHTMAP_SIZE,
        state->heightmap);
    if (!state->heightmap_texture) {
        return false;
    }

    if (!ForgeGpuProcessedSceneRendererCreate(demo, &state->renderer) ||
        !lesson48_create_samplers(demo, state) ||
        !lesson48_create_terrain_mesh(demo, state) ||
        !lesson48_create_tree_mesh(demo, state) ||
        !lesson48_create_pipelines(demo, state)) {
        return false;
    }

    state->tree_dirty = true;
    demo->lesson.camera_position = { LESSON48_CAM_START_X, LESSON48_CAM_START_Y, LESSON48_CAM_START_Z };
    demo->lesson.camera_yaw = LESSON48_CAM_START_YAW;
    demo->lesson.camera_pitch = LESSON48_CAM_START_PITCH;
    demo->lesson.move_speed = LESSON48_MOVE_SPEED;
    demo->lesson.mouse_sensitivity = LESSON48_MOUSE_SENSITIVITY;
    demo->lesson.pitch_clamp = LESSON48_PITCH_CLAMP;
    demo->lesson.last_ticks = SDL_GetTicks();
    return true;
}

bool ForgeGpuRenderLesson48(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPUTexture *swapchain_texture,
    Uint32 width,
    Uint32 height)
{
    Lesson48State *state = lesson48_state(demo);
    Mat4 view;
    Mat4 projection;
    Mat4 camera_vp;
    Mat4 light_vp;
    SDL_GPURenderPass *render_pass;

    if (!state) {
        SDL_SetError("lesson 48 internal state is missing");
        return false;
    }
    if (!ForgeGpuProcessedSceneRendererEnsureMainDepth(demo, &state->renderer, width, height)) {
        return false;
    }

    ForgeGpuProcessedSceneRendererBeginFrame(&state->renderer);
    state->terrain_draw_calls = 0;
    state->tree_draw_calls = 0;
    state->shadow_draw_calls = 0;
    ForgeGpuUpdateCameraFromInput(demo);
    ForgeGpuCameraViewProjection(demo, width, height, LESSON48_FAR_PLANE, &view, &projection);
    camera_vp = mat4_multiply(projection, view);
    light_vp = ForgeGpuProcessedSceneLightViewProjection();

    if (!lesson48_update_tree_instances(demo, command_buffer, state)) {
        return false;
    }

    render_pass = ForgeGpuProcessedSceneBeginShadowPass(command_buffer, &state->renderer);
    if (!render_pass) {
        return false;
    }
    if (lesson48_draws_shadow(state)) {
        if (lesson48_draws_terrain(state)) {
            lesson48_draw_terrain_shadow(command_buffer, render_pass, state, light_vp);
        }
        if (lesson48_draws_trees(state)) {
            lesson48_draw_tree_shadow(command_buffer, render_pass, state, light_vp);
        }
    }
    SDL_EndGPURenderPass(render_pass);
    state->renderer.shadow_pass_rendered = true;

    render_pass = ForgeGpuProcessedSceneBeginMainPass(command_buffer, &state->renderer, swapchain_texture);
    if (!render_pass) {
        return false;
    }
    if (lesson48_draws_terrain(state)) {
        lesson48_draw_terrain(demo, command_buffer, render_pass, state, camera_vp, light_vp);
    }
    if (lesson48_draws_trees(state)) {
        lesson48_draw_trees(demo, command_buffer, render_pass, state, camera_vp, light_vp);
    }
    SDL_EndGPURenderPass(render_pass);
    state->renderer.main_pass_rendered = true;
    return true;
}

void ForgeGpuDebugLesson48(ForgeGpuDemo *demo)
{
    Lesson48State *state = lesson48_state(demo);

    if (!state) {
        return;
    }
    ImGui::Text("Heightmap: %ux%u R32_FLOAT", LESSON48_HEIGHTMAP_SIZE, LESSON48_HEIGHTMAP_SIZE);
    ImGui::Text("Terrain indices: %u", state->terrain_index_count);
    ImGui::Text("Trees: %u", state->tree_count);
    ImGui::Text("Terrain draws: %u", state->terrain_draw_calls);
    ImGui::Text("Tree draws: %u", state->tree_draw_calls);
    ImGui::Text("Shadow draws: %u", state->shadow_draw_calls);
}

void ForgeGpuControlsLesson48(ForgeGpuDemo *demo)
{
    Lesson48State *state = lesson48_state(demo);
    bool height_changed = false;

    if (!state) {
        return;
    }
    height_changed = ImGui::SliderFloat("Height Scale", &state->height_scale, 1.0f, 20.0f);
    ImGui::SliderFloat("Snow Line", &state->snow_line, 0.0f, 1.0f);
    ImGui::SliderFloat("Slope Threshold", &state->slope_threshold, 0.05f, 1.0f);
    if (height_changed) {
        state->tree_dirty = true;
    }
}

bool ForgeGpuHandleLesson48Event(ForgeGpuDemo *demo, const SDL_Event *event)
{
    Lesson48State *state = lesson48_state(demo);

    if (!state || !demo->validation_mode ||
        event->type != SDL_EVENT_KEY_DOWN ||
        event->key.repeat) {
        return false;
    }

    switch (event->key.key) {
    case SDLK_S:
        state->diagnostic_mode = LESSON48_DIAGNOSTIC_NO_SHADOW;
        return true;
    case SDLK_V:
        state->diagnostic_mode = LESSON48_DIAGNOSTIC_NO_VARIATION;
        return true;
    case SDLK_T:
        state->diagnostic_mode = LESSON48_DIAGNOSTIC_TERRAIN_ONLY;
        return true;
    case SDLK_R:
        state->diagnostic_mode = LESSON48_DIAGNOSTIC_TREES_ONLY;
        return true;
    default:
        return false;
    }
}

void ForgeGpuExportLesson48Metrics(ForgeGpuDemo *demo)
{
    Lesson48State *state = lesson48_state(demo);

    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson48Complete", state ? 1.0 : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson48HeightmapSize", (double)LESSON48_HEIGHTMAP_SIZE);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson48TerrainIndexCount", state ? (double)state->terrain_index_count : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson48TreeCount", state ? (double)state->tree_count : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson48TerrainDrawCalls", state ? (double)state->terrain_draw_calls : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson48TreeDrawCalls", state ? (double)state->tree_draw_calls : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson48ShadowDrawCalls", state ? (double)state->shadow_draw_calls : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson48ShadowPass", state && state->renderer.shadow_pass_rendered ? 1.0 : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson48MainPass", state && state->renderer.main_pass_rendered ? 1.0 : 0.0);
}

void ForgeGpuDestroyLesson48(ForgeGpuDemo *demo)
{
    Lesson48State *state = lesson48_state(demo);

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
    if (state->tree_instance_buffer) {
        SDL_ReleaseGPUBuffer(demo->device, state->tree_instance_buffer);
    }
    if (state->tree_instance_upload) {
        SDL_ReleaseGPUTransferBuffer(demo->device, state->tree_instance_upload);
    }
    if (state->heightmap_texture) {
        SDL_ReleaseGPUTexture(demo->device, state->heightmap_texture);
    }
    if (state->heightmap_sampler) {
        SDL_ReleaseGPUSampler(demo->device, state->heightmap_sampler);
    }
    SDL_free(state->heightmap);
    SDL_free(state->tree_instances);
    ForgeGpuProcessedSceneRendererDestroy(demo, &state->renderer);
    SDL_free(state);
    demo->lesson.private_state = nullptr;
}
