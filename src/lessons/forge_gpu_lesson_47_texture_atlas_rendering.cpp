#include "forge_gpu_lessons.h"

#include "forge_gpu_browser_status.h"
#include "forge_gpu_camera.h"
#include "forge_gpu_gpu_helpers.h"
#include "forge_gpu_imgui.h"
#include "forge_gpu_lesson_common.h"
#include "forge_gpu_processed_scene_renderer.h"
#include "forge_gpu_shader_layouts.h"
#include "forge_gpu_shapes.h"
#include "shaders/generated/forge_gpu_shared_scene_shaders.h"
#include "third_party/cJSON/cJSON.h"

#include <stddef.h>

#include "imgui.h"

#define LESSON47_GRID_COLS 6
#define LESSON47_GRID_ROWS 5
#define LESSON47_MATERIAL_COUNT (LESSON47_GRID_COLS * LESSON47_GRID_ROWS)
#define LESSON47_CUBE_HALF_SIZE 0.45f
#define LESSON47_CUBE_SPACING 1.6f
#define LESSON47_ATLAS_WIDTH 2048
#define LESSON47_ATLAS_HEIGHT 2048
#define LESSON47_MATERIAL_SIZE 256
#define LESSON47_ATLAS_PADDING 4
#define LESSON47_SAMPLER_MAX_LOD 1000.0f
#define LESSON47_FAR_PLANE 200.0f
#define LESSON47_MOVE_SPEED 5.0f
#define LESSON47_MOUSE_SENSITIVITY 0.003f
#define LESSON47_PITCH_CLAMP 1.5f
#define LESSON47_CAM_START_X 0.0f
#define LESSON47_CAM_START_Y 6.0f
#define LESSON47_CAM_START_Z 12.0f
#define LESSON47_CAM_START_YAW 0.0f
#define LESSON47_CAM_START_PITCH (-0.4f)
#define LESSON47_AMBIENT 0.15f
#define LESSON47_SHININESS 32.0f
#define LESSON47_SPECULAR_STRENGTH 0.5f
#define LESSON47_LIGHT_INTENSITY 1.2f
#define LESSON47_SHADOW_BIAS_CONST 2.0f
#define LESSON47_SHADOW_BIAS_SLOPE 2.0f

typedef struct Lesson47TexturedVertex
{
    float position[3];
    float normal[3];
    float uv[2];
} Lesson47TexturedVertex;

typedef struct Lesson47SceneVertUniforms
{
    Mat4 mvp;
    Mat4 model;
    Mat4 light_vp;
} Lesson47SceneVertUniforms;

typedef struct Lesson47TexturedFragUniforms
{
    float uv_transform[4];
    float eye_pos[3];
    float ambient;
    float light_dir[4];
    float light_color[3];
    float light_intensity;
    float shininess;
    float specular_strength;
    float pad0[2];
} Lesson47TexturedFragUniforms;

typedef struct Lesson47ShadowVertUniforms
{
    Mat4 light_vp;
} Lesson47ShadowVertUniforms;

typedef struct Lesson47AtlasEntry
{
    float uv_transform[4];
    int x;
    int y;
    int width;
    int height;
} Lesson47AtlasEntry;

typedef struct Lesson47State
{
    ForgeGpuProcessedSceneRenderer renderer;
    SDL_GPUGraphicsPipeline *textured_pipeline;
    SDL_GPUGraphicsPipeline *shadow_pipeline;
    SDL_GPUBuffer *cube_vertex_buffer;
    SDL_GPUBuffer *cube_index_buffer;
    SDL_GPUTexture *atlas_texture;
    SDL_GPUTexture *material_textures[LESSON47_MATERIAL_COUNT];
    SDL_GPUSampler *material_sampler;
    Lesson47AtlasEntry atlas_entries[LESSON47_MATERIAL_COUNT];
    Uint32 cube_index_count;
    Uint32 bind_count;
    Uint32 draw_calls;
    Uint32 shadow_draw_calls;
    bool use_atlas;
    bool atlas_loaded;
} Lesson47State;

static_assert(sizeof(Lesson47TexturedVertex) == 32, "lesson 47 textured vertex size must match HLSL layout");
static_assert(sizeof(Lesson47SceneVertUniforms) == 192, "lesson 47 vertex uniform size must match HLSL layout");
static_assert(sizeof(Lesson47TexturedFragUniforms) == 80, "lesson 47 fragment uniform size must match HLSL layout");
static_assert(sizeof(Lesson47ShadowVertUniforms) == 64, "lesson 47 shadow uniform size must match HLSL layout");

static const char *kLesson47MaterialNames[LESSON47_MATERIAL_COUNT] = {
    "bark", "brushed_steel", "cobblestone",
    "copper_patina", "corrugated_metal", "cracked_concrete",
    "diamond_plate", "dirt", "flagstone",
    "grass", "gravel", "hardwood_floor",
    "herringbone_brick", "leather", "marble",
    "old_brick_wall", "parquet", "plywood",
    "red_brick", "roof_tiles", "rough_plaster",
    "rough_rock", "rusted_iron", "sandstone",
    "slate", "smooth_concrete", "stucco",
    "weathered_planks", "white_brick", "woven_fabric"
};

static const float kLesson47UvIdentity[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
static const float kLesson47LightColor[3] = { 1.0f, 0.95f, 0.9f };

static Lesson47State *lesson47_state(ForgeGpuDemo *demo)
{
    return (Lesson47State *)demo->lesson.private_state;
}

static bool lesson47_load_json_file(const char *path, cJSON **out_json)
{
    size_t size = 0;
    char *data = (char *)SDL_LoadFile(path, &size);
    const char *parse_end = nullptr;
    const char *file_end;
    cJSON *json;

    *out_json = nullptr;
    if (!data) {
        SDL_SetError("lesson 47 failed to load '%s': %s", path, SDL_GetError());
        return false;
    }

    json = cJSON_ParseWithLengthOpts(data, size, &parse_end, false);
    file_end = data + size;
    if (json) {
        while (parse_end < file_end && SDL_isspace((unsigned char)*parse_end)) {
            parse_end += 1;
        }
        if (parse_end != file_end) {
            cJSON_Delete(json);
            json = nullptr;
            SDL_SetError("lesson 47 atlas JSON '%s' has trailing non-whitespace data", path);
        }
    }
    SDL_free(data);
    if (!json) {
        if (SDL_GetError()[0] == '\0') {
            SDL_SetError("lesson 47 failed to parse atlas JSON '%s'", path);
        }
        return false;
    }

    *out_json = json;
    return true;
}

static bool lesson47_json_int(const cJSON *object, const char *name, int expected, int *out_value)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);

    if (!cJSON_IsNumber(item)) {
        SDL_SetError("lesson 47 atlas entry is missing numeric '%s'", name);
        return false;
    }
    *out_value = item->valueint;
    if (expected >= 0 && *out_value != expected) {
        SDL_SetError("lesson 47 atlas '%s' is %d, expected %d", name, *out_value, expected);
        return false;
    }
    return true;
}

static bool lesson47_json_float(const cJSON *object, const char *name, float *out_value)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);

    if (!cJSON_IsNumber(item)) {
        SDL_SetError("lesson 47 atlas entry is missing numeric '%s'", name);
        return false;
    }
    *out_value = (float)item->valuedouble;
    return true;
}

static bool lesson47_load_atlas_metadata(ForgeGpuDemo *demo, Lesson47State *state)
{
    char path[FORGE_GPU_MAX_PATH];
    cJSON *root = nullptr;
    const cJSON *entries;
    int root_value;
    bool ok = false;

    if (!ForgeGpuJoinAssetPath(demo, "textures/47-texture-atlas-rendering/atlas.json", path, sizeof(path))) {
        return false;
    }
    if (!lesson47_load_json_file(path, &root)) {
        return false;
    }
    if (!cJSON_IsObject(root) ||
        !lesson47_json_int(root, "version", 1, &root_value) ||
        !lesson47_json_int(root, "width", LESSON47_ATLAS_WIDTH, &root_value) ||
        !lesson47_json_int(root, "height", LESSON47_ATLAS_HEIGHT, &root_value) ||
        !lesson47_json_int(root, "padding", LESSON47_ATLAS_PADDING, &root_value)) {
        goto done;
    }

    entries = cJSON_GetObjectItemCaseSensitive(root, "entries");
    if (!cJSON_IsObject(entries)) {
        SDL_SetError("lesson 47 atlas JSON is missing entries object");
        goto done;
    }

    for (int i = 0; i < LESSON47_MATERIAL_COUNT; i += 1) {
        Lesson47AtlasEntry *entry = &state->atlas_entries[i];
        const cJSON *json_entry = cJSON_GetObjectItemCaseSensitive(entries, kLesson47MaterialNames[i]);

        if (!cJSON_IsObject(json_entry)) {
            SDL_SetError("lesson 47 atlas JSON is missing material '%s'", kLesson47MaterialNames[i]);
            goto done;
        }
        if (!lesson47_json_int(json_entry, "x", -1, &entry->x) ||
            !lesson47_json_int(json_entry, "y", -1, &entry->y) ||
            !lesson47_json_int(json_entry, "width", LESSON47_MATERIAL_SIZE, &entry->width) ||
            !lesson47_json_int(json_entry, "height", LESSON47_MATERIAL_SIZE, &entry->height) ||
            !lesson47_json_float(json_entry, "u_offset", &entry->uv_transform[0]) ||
            !lesson47_json_float(json_entry, "v_offset", &entry->uv_transform[1]) ||
            !lesson47_json_float(json_entry, "u_scale", &entry->uv_transform[2]) ||
            !lesson47_json_float(json_entry, "v_scale", &entry->uv_transform[3])) {
            goto done;
        }
    }

    state->atlas_loaded = true;
    ok = true;

done:
    cJSON_Delete(root);
    return ok;
}

static void lesson47_release_shader(SDL_GPUDevice *device, SDL_GPUShader **shader)
{
    if (*shader) {
        SDL_ReleaseGPUShader(device, *shader);
        *shader = nullptr;
    }
}

static void lesson47_fill_textured_vertex_input(
    SDL_GPUVertexBufferDescription *vertex_buffer,
    SDL_GPUVertexAttribute attributes[3])
{
    SDL_zero(*vertex_buffer);
    vertex_buffer->slot = 0;
    vertex_buffer->pitch = sizeof(Lesson47TexturedVertex);
    vertex_buffer->input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

    SDL_memset(attributes, 0, 3 * sizeof(*attributes));
    attributes[0].location = 0;
    attributes[0].buffer_slot = 0;
    attributes[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    attributes[0].offset = offsetof(Lesson47TexturedVertex, position);
    attributes[1].location = 1;
    attributes[1].buffer_slot = 0;
    attributes[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    attributes[1].offset = offsetof(Lesson47TexturedVertex, normal);
    attributes[2].location = 2;
    attributes[2].buffer_slot = 0;
    attributes[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    attributes[2].offset = offsetof(Lesson47TexturedVertex, uv);
}

static bool lesson47_create_pipelines(ForgeGpuDemo *demo, Lesson47State *state)
{
    SDL_GPUShader *textured_vs = nullptr;
    SDL_GPUShader *textured_fs = nullptr;
    SDL_GPUShader *shadow_vs = nullptr;
    SDL_GPUShader *shadow_fs = nullptr;
    SDL_GPUColorTargetDescription color_target;
    SDL_GPUVertexBufferDescription vertex_buffer;
    SDL_GPUVertexAttribute attributes[3];
    bool ok = false;

    textured_vs = ForgeGpuCreateShader(demo->device, SDL_GPU_SHADERSTAGE_VERTEX,
        forge_scene_textured_vert_wgsl, forge_scene_textured_vert_wgsl_size,
        forge_scene_textured_vert_msl, forge_scene_textured_vert_msl_size,
        0, 0, 0, 1);
    textured_fs = ForgeGpuCreateShaderWithResourceLayout(demo->device,
        forge_scene_textured_frag_wgsl, forge_scene_textured_frag_wgsl_size,
        forge_scene_textured_frag_msl, forge_scene_textured_frag_msl_size,
        ForgeGpuShaderLayout_forge_scene_textured_frag());
    shadow_vs = ForgeGpuCreateShader(demo->device, SDL_GPU_SHADERSTAGE_VERTEX,
        forge_scene_shadow_vert_wgsl, forge_scene_shadow_vert_wgsl_size,
        forge_scene_shadow_vert_msl, forge_scene_shadow_vert_msl_size,
        0, 0, 0, 1);
    shadow_fs = ForgeGpuCreateShader(demo->device, SDL_GPU_SHADERSTAGE_FRAGMENT,
        forge_scene_shadow_frag_wgsl, forge_scene_shadow_frag_wgsl_size,
        forge_scene_shadow_frag_msl, forge_scene_shadow_frag_msl_size,
        0, 0, 0, 0);
    if (!textured_vs || !textured_fs || !shadow_vs || !shadow_fs) {
        goto done;
    }

    SDL_zero(color_target);
    color_target.format = demo->color_format;
    lesson47_fill_textured_vertex_input(&vertex_buffer, attributes);

    state->textured_pipeline = ForgeGpuCreateLessonGraphicsPipelineWithColorTargetsAndDepthCompare(
        demo, textured_vs, textured_fs, SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        &color_target, 1,
        &vertex_buffer, 1, attributes, 3,
        true, FORGE_GPU_PROCESSED_SCENE_DEPTH_FORMAT, true, true,
        SDL_GPU_COMPAREOP_LESS,
        SDL_GPU_CULLMODE_BACK, 0.0f, 0.0f);
    state->shadow_pipeline = ForgeGpuCreateLessonGraphicsPipelineWithColorTargetsAndDepthCompare(
        demo, shadow_vs, shadow_fs, SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        nullptr, 0,
        &vertex_buffer, 1, attributes, 1,
        true, FORGE_GPU_PROCESSED_SCENE_DEPTH_FORMAT, true, true,
        SDL_GPU_COMPAREOP_LESS,
        SDL_GPU_CULLMODE_NONE,
        LESSON47_SHADOW_BIAS_CONST,
        LESSON47_SHADOW_BIAS_SLOPE);

    ok = state->textured_pipeline && state->shadow_pipeline;

done:
    lesson47_release_shader(demo->device, &shadow_fs);
    lesson47_release_shader(demo->device, &shadow_vs);
    lesson47_release_shader(demo->device, &textured_fs);
    lesson47_release_shader(demo->device, &textured_vs);
    return ok;
}

static bool lesson47_upload_cube(ForgeGpuDemo *demo, Lesson47State *state)
{
    ForgeGpuShapeMesh cube;
    Lesson47TexturedVertex *vertices = nullptr;
    bool ok = false;

    SDL_zero(cube);
    if (!ForgeGpuCreateCubeShapeMesh(1, 1, &cube)) {
        return false;
    }
    if (!cube.positions || !cube.normals || !cube.uvs || !cube.indices ||
        cube.vertex_count <= 0 || cube.index_count <= 0) {
        SDL_SetError("lesson 47 cube shape is empty");
        goto done;
    }

    vertices = (Lesson47TexturedVertex *)SDL_calloc((size_t)cube.vertex_count, sizeof(*vertices));
    if (!vertices) {
        SDL_OutOfMemory();
        goto done;
    }
    for (int i = 0; i < cube.vertex_count; i += 1) {
        vertices[i].position[0] = cube.positions[i * 3 + 0];
        vertices[i].position[1] = cube.positions[i * 3 + 1];
        vertices[i].position[2] = cube.positions[i * 3 + 2];
        vertices[i].normal[0] = cube.normals[i * 3 + 0];
        vertices[i].normal[1] = cube.normals[i * 3 + 1];
        vertices[i].normal[2] = cube.normals[i * 3 + 2];
        vertices[i].uv[0] = cube.uvs[i * 2 + 0];
        vertices[i].uv[1] = cube.uvs[i * 2 + 1];
    }

    state->cube_vertex_buffer = ForgeGpuCreateBufferWithData(
        demo->device,
        SDL_GPU_BUFFERUSAGE_VERTEX,
        vertices,
        (Uint32)((size_t)cube.vertex_count * sizeof(*vertices)));
    state->cube_index_buffer = ForgeGpuCreateBufferWithData(
        demo->device,
        SDL_GPU_BUFFERUSAGE_INDEX,
        cube.indices,
        (Uint32)((size_t)cube.index_count * sizeof(*cube.indices)));
    if (!state->cube_vertex_buffer || !state->cube_index_buffer) {
        goto done;
    }

    state->cube_index_count = (Uint32)cube.index_count;
    ok = true;

done:
    SDL_free(vertices);
    ForgeGpuFreeShapeMesh(&cube);
    return ok;
}

static bool lesson47_load_textures(ForgeGpuDemo *demo, Lesson47State *state)
{
    char path[FORGE_GPU_MAX_PATH];

    if (!ForgeGpuJoinAssetPath(demo, "textures/47-texture-atlas-rendering/atlas.png", path, sizeof(path))) {
        return false;
    }
    state->atlas_texture = ForgeGpuLoadRgbaTexturePathWithFormatAndSize(
        demo,
        path,
        true,
        SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB,
        LESSON47_ATLAS_WIDTH,
        LESSON47_ATLAS_HEIGHT);
    if (!state->atlas_texture) {
        return false;
    }

    for (int i = 0; i < LESSON47_MATERIAL_COUNT; i += 1) {
        char relative[FORGE_GPU_MAX_PATH];

        if (SDL_snprintf(
                relative,
                sizeof(relative),
                "textures/47-texture-atlas-rendering/materials/%s.png",
                kLesson47MaterialNames[i]) >= (int)sizeof(relative)) {
            SDL_SetError("lesson 47 material asset path too long");
            return false;
        }
        if (!ForgeGpuJoinAssetPath(demo, relative, path, sizeof(path))) {
            return false;
        }
        state->material_textures[i] = ForgeGpuLoadRgbaTexturePathWithFormatAndSize(
            demo,
            path,
            true,
            SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB,
            LESSON47_MATERIAL_SIZE,
            LESSON47_MATERIAL_SIZE);
        if (!state->material_textures[i]) {
            return false;
        }
    }

    return true;
}

static Mat4 lesson47_cube_model(int material_index)
{
    const int col = material_index % LESSON47_GRID_COLS;
    const int row = material_index / LESSON47_GRID_COLS;
    const float grid_offset_x = (float)(LESSON47_GRID_COLS - 1) * LESSON47_CUBE_SPACING * 0.5f;
    const float grid_offset_z = (float)(LESSON47_GRID_ROWS - 1) * LESSON47_CUBE_SPACING * 0.5f;
    const float x = (float)col * LESSON47_CUBE_SPACING - grid_offset_x;
    const float z = (float)row * LESSON47_CUBE_SPACING - grid_offset_z;
    const float y = LESSON47_CUBE_HALF_SIZE;

    return mat4_multiply(
        mat4_translate({ x, y, z }),
        mat4_scale_vec3({ LESSON47_CUBE_HALF_SIZE, LESSON47_CUBE_HALF_SIZE, LESSON47_CUBE_HALF_SIZE }));
}

static void lesson47_bind_cube(SDL_GPURenderPass *render_pass, Lesson47State *state)
{
    SDL_GPUBufferBinding vertex_binding;
    SDL_GPUBufferBinding index_binding;

    SDL_zero(vertex_binding);
    vertex_binding.buffer = state->cube_vertex_buffer;
    SDL_BindGPUVertexBuffers(render_pass, 0, &vertex_binding, 1);

    SDL_zero(index_binding);
    index_binding.buffer = state->cube_index_buffer;
    SDL_BindGPUIndexBuffer(render_pass, &index_binding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
}

static void lesson47_fill_fragment_uniforms(
    ForgeGpuDemo *demo,
    const float uv_transform[4],
    Lesson47TexturedFragUniforms *uniforms)
{
    const Vec3 light_dir = ForgeGpuProcessedSceneLightDir();

    SDL_zero(*uniforms);
    SDL_memcpy(uniforms->uv_transform, uv_transform, sizeof(uniforms->uv_transform));
    uniforms->eye_pos[0] = demo->lesson.camera_position.x;
    uniforms->eye_pos[1] = demo->lesson.camera_position.y;
    uniforms->eye_pos[2] = demo->lesson.camera_position.z;
    uniforms->ambient = LESSON47_AMBIENT;
    uniforms->light_dir[0] = light_dir.x;
    uniforms->light_dir[1] = light_dir.y;
    uniforms->light_dir[2] = light_dir.z;
    uniforms->light_color[0] = kLesson47LightColor[0];
    uniforms->light_color[1] = kLesson47LightColor[1];
    uniforms->light_color[2] = kLesson47LightColor[2];
    uniforms->light_intensity = LESSON47_LIGHT_INTENSITY;
    uniforms->shininess = LESSON47_SHININESS;
    uniforms->specular_strength = LESSON47_SPECULAR_STRENGTH;
}

static void lesson47_draw_one_cube(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    Lesson47State *state,
    Mat4 camera_vp,
    Mat4 light_vp,
    int material_index,
    const float uv_transform[4])
{
    const Mat4 model = lesson47_cube_model(material_index);
    Lesson47SceneVertUniforms vertex_uniforms;
    Lesson47TexturedFragUniforms fragment_uniforms;

    vertex_uniforms.mvp = mat4_multiply(camera_vp, model);
    vertex_uniforms.model = model;
    vertex_uniforms.light_vp = mat4_multiply(light_vp, model);
    SDL_PushGPUVertexUniformData(command_buffer, 0, &vertex_uniforms, sizeof(vertex_uniforms));

    lesson47_fill_fragment_uniforms(demo, uv_transform, &fragment_uniforms);
    SDL_PushGPUFragmentUniformData(command_buffer, 0, &fragment_uniforms, sizeof(fragment_uniforms));

    SDL_DrawGPUIndexedPrimitives(render_pass, state->cube_index_count, 1, 0, 0, 0);
    state->draw_calls += 1;
}

static bool lesson47_draw_shadow_pass(
    SDL_GPUCommandBuffer *command_buffer,
    Lesson47State *state,
    Mat4 light_vp)
{
    SDL_GPURenderPass *render_pass = ForgeGpuProcessedSceneBeginShadowPass(command_buffer, &state->renderer);

    if (!render_pass) {
        return false;
    }

    SDL_BindGPUGraphicsPipeline(render_pass, state->shadow_pipeline);
    lesson47_bind_cube(render_pass, state);
    for (int i = 0; i < LESSON47_MATERIAL_COUNT; i += 1) {
        Lesson47ShadowVertUniforms uniforms;
        const Mat4 model = lesson47_cube_model(i);

        uniforms.light_vp = mat4_multiply(light_vp, model);
        SDL_PushGPUVertexUniformData(command_buffer, 0, &uniforms, sizeof(uniforms));
        SDL_DrawGPUIndexedPrimitives(render_pass, state->cube_index_count, 1, 0, 0, 0);
        state->shadow_draw_calls += 1;
    }
    SDL_EndGPURenderPass(render_pass);
    state->renderer.shadow_pass_rendered = true;
    return true;
}

static void lesson47_bind_textured_resources(
    SDL_GPURenderPass *render_pass,
    Lesson47State *state,
    SDL_GPUTexture *texture)
{
    SDL_GPUTextureSamplerBinding bindings[2];

    SDL_zeroa(bindings);
    bindings[0].texture = state->renderer.shadow_depth;
    bindings[0].sampler = state->renderer.grid_shadow_sampler;
    bindings[1].texture = texture;
    bindings[1].sampler = state->material_sampler;
    SDL_BindGPUFragmentSamplers(render_pass, 0, bindings, SDL_arraysize(bindings));
}

static void lesson47_draw_cubes(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    Lesson47State *state,
    Mat4 camera_vp,
    Mat4 light_vp)
{
    state->bind_count = 0;
    state->draw_calls = 0;

    SDL_BindGPUGraphicsPipeline(render_pass, state->textured_pipeline);
    lesson47_bind_cube(render_pass, state);

    if (state->use_atlas) {
        lesson47_bind_textured_resources(render_pass, state, state->atlas_texture);
        state->bind_count = 1;
        for (int i = 0; i < LESSON47_MATERIAL_COUNT; i += 1) {
            lesson47_draw_one_cube(
                demo,
                command_buffer,
                render_pass,
                state,
                camera_vp,
                light_vp,
                i,
                state->atlas_entries[i].uv_transform);
        }
    } else {
        for (int i = 0; i < LESSON47_MATERIAL_COUNT; i += 1) {
            lesson47_bind_textured_resources(render_pass, state, state->material_textures[i]);
            state->bind_count += 1;
            lesson47_draw_one_cube(
                demo,
                command_buffer,
                render_pass,
                state,
                camera_vp,
                light_vp,
                i,
                kLesson47UvIdentity);
        }
    }
}

bool ForgeGpuCreateLesson47(ForgeGpuDemo *demo)
{
    Lesson47State *state = (Lesson47State *)SDL_calloc(1, sizeof(*state));

    if (!state) {
        SDL_OutOfMemory();
        return false;
    }
    demo->lesson.private_state = state;
    state->use_atlas = true;

    if (!ForgeGpuProcessedSceneRendererCreate(demo, &state->renderer) ||
        !lesson47_create_pipelines(demo, state) ||
        !lesson47_upload_cube(demo, state) ||
        !lesson47_load_atlas_metadata(demo, state) ||
        !lesson47_load_textures(demo, state)) {
        return false;
    }

    state->material_sampler = ForgeGpuCreateSamplerWithAddress(
        demo->device,
        SDL_GPU_FILTER_LINEAR,
        SDL_GPU_FILTER_LINEAR,
        SDL_GPU_SAMPLERMIPMAPMODE_LINEAR,
        SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        LESSON47_SAMPLER_MAX_LOD);
    if (!state->material_sampler) {
        return false;
    }

    demo->lesson.camera_position = { LESSON47_CAM_START_X, LESSON47_CAM_START_Y, LESSON47_CAM_START_Z };
    demo->lesson.camera_yaw = LESSON47_CAM_START_YAW;
    demo->lesson.camera_pitch = LESSON47_CAM_START_PITCH;
    demo->lesson.move_speed = LESSON47_MOVE_SPEED;
    demo->lesson.mouse_sensitivity = LESSON47_MOUSE_SENSITIVITY;
    demo->lesson.pitch_clamp = LESSON47_PITCH_CLAMP;
    demo->lesson.last_ticks = SDL_GetTicks();
    return true;
}

bool ForgeGpuRenderLesson47(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPUTexture *swapchain_texture,
    Uint32 width,
    Uint32 height)
{
    Lesson47State *state = lesson47_state(demo);
    Mat4 view;
    Mat4 projection;
    Mat4 camera_vp;
    Mat4 light_vp;
    SDL_GPURenderPass *render_pass;

    if (!state) {
        return false;
    }
    if (!ForgeGpuProcessedSceneRendererEnsureMainDepth(demo, &state->renderer, width, height)) {
        return false;
    }

    ForgeGpuProcessedSceneRendererBeginFrame(&state->renderer);
    state->shadow_draw_calls = 0;
    ForgeGpuUpdateCameraFromInput(demo);
    ForgeGpuCameraViewProjection(demo, width, height, LESSON47_FAR_PLANE, &view, &projection);
    camera_vp = mat4_multiply(projection, view);
    light_vp = ForgeGpuProcessedSceneLightViewProjection();

    if (!lesson47_draw_shadow_pass(command_buffer, state, light_vp)) {
        return false;
    }

    render_pass = ForgeGpuProcessedSceneBeginMainPass(command_buffer, &state->renderer, swapchain_texture);
    if (!render_pass) {
        return false;
    }
    ForgeGpuProcessedSceneDrawGrid(demo, command_buffer, render_pass, &state->renderer, camera_vp, light_vp);
    lesson47_draw_cubes(demo, command_buffer, render_pass, state, camera_vp, light_vp);
    SDL_EndGPURenderPass(render_pass);
    state->renderer.main_pass_rendered = true;
    return true;
}

void ForgeGpuDebugLesson47(ForgeGpuDemo *demo)
{
    Lesson47State *state = lesson47_state(demo);

    if (!state) {
        return;
    }
    ImGui::Text("Mode: %s", state->use_atlas ? "Atlas" : "Individual textures");
    ImGui::Text("Unique textures: %u", state->bind_count);
    ImGui::Text("Draw calls: %u", state->draw_calls);
    ImGui::Text("Shadow draw calls: %u", state->shadow_draw_calls);
    ImGui::Text("Atlas: %dx%d", LESSON47_ATLAS_WIDTH, LESSON47_ATLAS_HEIGHT);
    ImGui::Text("Materials: %d", LESSON47_MATERIAL_COUNT);
}

void ForgeGpuControlsLesson47(ForgeGpuDemo *demo)
{
    Lesson47State *state = lesson47_state(demo);

    if (!state) {
        return;
    }
    ImGui::Checkbox("Use atlas (Tab)", &state->use_atlas);
}

bool ForgeGpuHandleLesson47Event(ForgeGpuDemo *demo, const SDL_Event *event)
{
    Lesson47State *state = lesson47_state(demo);

    if (!state || event->type != SDL_EVENT_KEY_DOWN || event->key.repeat) {
        return false;
    }
    if (event->key.key == SDLK_TAB) {
        state->use_atlas = !state->use_atlas;
        return true;
    }
    return false;
}

void ForgeGpuExportLesson47Metrics(ForgeGpuDemo *demo)
{
    Lesson47State *state = lesson47_state(demo);

    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson47Complete", state ? 1.0 : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson47MaterialCount", (double)LESSON47_MATERIAL_COUNT);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson47AtlasWidth", (double)LESSON47_ATLAS_WIDTH);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson47AtlasHeight", (double)LESSON47_ATLAS_HEIGHT);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson47AtlasLoaded", state && state->atlas_loaded ? 1.0 : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson47AtlasMode", state && state->use_atlas ? 1.0 : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson47BindCount", state ? (double)state->bind_count : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson47DrawCalls", state ? (double)state->draw_calls : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson47ShadowDrawCalls", state ? (double)state->shadow_draw_calls : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson47ShadowPass", state && state->renderer.shadow_pass_rendered ? 1.0 : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson47MainPass", state && state->renderer.main_pass_rendered ? 1.0 : 0.0);
}

void ForgeGpuDestroyLesson47(ForgeGpuDemo *demo)
{
    Lesson47State *state = lesson47_state(demo);

    if (!state) {
        return;
    }
    if (state->material_sampler) {
        SDL_ReleaseGPUSampler(demo->device, state->material_sampler);
    }
    for (int i = 0; i < LESSON47_MATERIAL_COUNT; i += 1) {
        if (state->material_textures[i]) {
            SDL_ReleaseGPUTexture(demo->device, state->material_textures[i]);
        }
    }
    if (state->atlas_texture) {
        SDL_ReleaseGPUTexture(demo->device, state->atlas_texture);
    }
    if (state->cube_index_buffer) {
        SDL_ReleaseGPUBuffer(demo->device, state->cube_index_buffer);
    }
    if (state->cube_vertex_buffer) {
        SDL_ReleaseGPUBuffer(demo->device, state->cube_vertex_buffer);
    }
    if (state->shadow_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, state->shadow_pipeline);
    }
    if (state->textured_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, state->textured_pipeline);
    }
    ForgeGpuProcessedSceneRendererDestroy(demo, &state->renderer);
    SDL_free(state);
    demo->lesson.private_state = nullptr;
}
