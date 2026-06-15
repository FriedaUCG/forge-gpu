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
#include "shaders/generated/forge_gpu_lesson_31_shaders.h"
#include "imgui.h"

#include <stddef.h>

#define LESSON31_SHADOW_MAP_SIZE 2048u
#define LESSON31_DEPTH_FORMAT SDL_GPU_TEXTUREFORMAT_D32_FLOAT
#define LESSON31_FAR_PLANE 200.0f
#define LESSON31_CAMERA_SPEED 5.0f
#define LESSON31_MOUSE_SENSITIVITY 0.003f
#define LESSON31_PITCH_CLAMP 1.5f
#define LESSON31_CAMERA_START_X 0.0f
#define LESSON31_CAMERA_START_Y 20.0f
#define LESSON31_CAMERA_START_Z 45.0f
#define LESSON31_CAMERA_START_YAW_DEG 0.0f
#define LESSON31_CAMERA_START_PITCH_DEG -25.0f
#define LESSON31_LIGHT_DIR_X -0.4f
#define LESSON31_LIGHT_DIR_Y -0.7f
#define LESSON31_LIGHT_DIR_Z -0.5f
#define LESSON31_LIGHT_INTENSITY 0.9f
#define LESSON31_LIGHT_COLOR_R 1.0f
#define LESSON31_LIGHT_COLOR_G 0.95f
#define LESSON31_LIGHT_COLOR_B 0.85f
#define LESSON31_MATERIAL_AMBIENT 0.2f
#define LESSON31_MATERIAL_SHININESS 64.0f
#define LESSON31_MATERIAL_SPECULAR_STRENGTH 0.3f
#define LESSON31_SHADOW_ORTHO_SIZE 50.0f
#define LESSON31_SHADOW_NEAR 0.1f
#define LESSON31_SHADOW_FAR 100.0f
#define LESSON31_LIGHT_DISTANCE 40.0f
#define LESSON31_PARALLEL_THRESHOLD 0.99f
#define LESSON31_GROUND_Y -0.05f
#define LESSON31_GROUND_HALF_SIZE 60.0f
#define LESSON31_GROUND_SHININESS 16.0f
#define LESSON31_GROUND_SPECULAR_STRENGTH 0.1f
#define LESSON31_GROUND_UV_REPEAT 8.0f
#define LESSON31_DIRT_TEXTURE_SIZE 256u
#define LESSON31_DIRT_NOISE_SCALE 8.0f
#define LESSON31_DIRT_NOISE_SEED 42u
#define LESSON31_DIRT_NOISE_OCTAVES 4
#define LESSON31_DIRT_NOISE_LACUNARITY 2.0f
#define LESSON31_DIRT_NOISE_PERSISTENCE 0.5f
#define LESSON31_DIRT_BASE_R 0.35f
#define LESSON31_DIRT_RANGE_R 0.25f
#define LESSON31_DIRT_BASE_G 0.30f
#define LESSON31_DIRT_RANGE_G 0.20f
#define LESSON31_DIRT_BASE_B 0.15f
#define LESSON31_DIRT_RANGE_B 0.10f
#define LESSON31_CLEAR_R 0.5f
#define LESSON31_CLEAR_G 0.7f
#define LESSON31_CLEAR_B 0.9f
#define LESSON31_MAX_ANISOTROPY 8.0f
#define LESSON31_MAX_LOD_UNLIMITED 1000.0f
#define LESSON31_ANIM_SPEED 1.0f
#define LESSON31_TRUCK_DRIVE_SPEED 8.0f
#define LESSON31_TRUCK_Y 0.0f
#define LESSON31_PATH_WAYPOINT_COUNT 14
#define LESSON31_TRACK_MESH_CORNER_15 3
#define LESSON31_TRACK_MESH_LINE_15 5
#define LESSON31_TRACK_MESH_LINE_30 6
#define LESSON31_TRACK_MESH_START_FIN 7
#define LESSON31_TRACK_CX 13.84f
#define LESSON31_TRACK_CZ 21.34f
#define LESSON31_TRACK_OFFSET 1.16f
#define LESSON31_TRACK_PIECE_COUNT 8

struct Lesson31PathWaypoint
{
    Vec3 position;
    float yaw;
};

struct Lesson31TrackPiece
{
    int mesh_index;
    Mat4 transform;
};

struct Lesson31State
{
    GpuSceneData truck;
    GpuSceneData track;
    SDL_GPUGraphicsPipeline *shadow_pipeline;
    SDL_GPUGraphicsPipeline *scene_pipeline;
    SDL_GPUGraphicsPipeline *skybox_pipeline;
    SDL_GPUTexture *shadow_depth;
    SDL_GPUTexture *main_depth;
    SDL_GPUTexture *cubemap_texture;
    SDL_GPUTexture *dirt_texture;
    SDL_GPUBuffer *floor_vertex_buffer;
    SDL_GPUBuffer *floor_index_buffer;
    SDL_GPUBuffer *skybox_vertex_buffer;
    SDL_GPUBuffer *skybox_index_buffer;
    Uint32 main_depth_width;
    Uint32 main_depth_height;
    Mat4 light_vp;
    Lesson31TrackPiece track_pieces[LESSON31_TRACK_PIECE_COUNT];
    float wheel_time;
    float path_distance;
    float total_path_len;
    float seg_cumulative[LESSON31_PATH_WAYPOINT_COUNT];
    float truck_yaw;
    Vec3 truck_pos;
    Uint64 last_animation_counter;
    bool animation_data_ready;
    bool animation_applied;
    bool shadow_pass_rendered;
    bool main_pass_rendered;
};

static_assert(sizeof(ForgeGpuShadowVertUniforms) == 64, "lesson 31 shadow vertex uniform size must match HLSL layout");
static_assert(sizeof(ForgeGpuForwardSceneVertUniforms) == 192, "lesson 31 scene vertex uniform size must match HLSL layout");
static_assert(sizeof(ForgeGpuForwardSceneFragUniforms) == 80, "lesson 31 scene fragment uniform size must match HLSL layout");
static_assert(sizeof(ForgeGpuForwardSkyboxVertUniforms) == 64, "lesson 31 skybox vertex uniform size must match HLSL layout");

static const Lesson31PathWaypoint kLesson31PathWaypoints[LESSON31_PATH_WAYPOINT_COUNT] = {
    { {  -5.0f, LESSON31_TRUCK_Y,  22.5f },  FORGE_GPU_PI * 0.5f },
    { {   5.0f, LESSON31_TRUCK_Y,  22.5f },  FORGE_GPU_PI * 0.5f },
    { {  12.8f, LESSON31_TRUCK_Y,  20.3f },  FORGE_GPU_PI * 0.75f },
    { {  15.0f, LESSON31_TRUCK_Y,  14.0f },  FORGE_GPU_PI },
    { {  15.0f, LESSON31_TRUCK_Y,   0.0f },  FORGE_GPU_PI },
    { {  15.0f, LESSON31_TRUCK_Y, -14.0f },  FORGE_GPU_PI },
    { {  12.8f, LESSON31_TRUCK_Y, -20.3f }, -FORGE_GPU_PI * 0.75f },
    { {   5.0f, LESSON31_TRUCK_Y, -22.5f }, -FORGE_GPU_PI * 0.5f },
    { {  -5.0f, LESSON31_TRUCK_Y, -22.5f }, -FORGE_GPU_PI * 0.5f },
    { { -12.8f, LESSON31_TRUCK_Y, -20.3f }, -FORGE_GPU_PI * 0.25f },
    { { -15.0f, LESSON31_TRUCK_Y, -14.0f },  0.0f },
    { { -15.0f, LESSON31_TRUCK_Y,   0.0f },  0.0f },
    { { -15.0f, LESSON31_TRUCK_Y,  14.0f },  0.0f },
    { { -12.8f, LESSON31_TRUCK_Y,  20.3f },  FORGE_GPU_PI * 0.25f },
};

static const Uint16 kLesson31QuadIndices[6] = { 0, 2, 1, 0, 3, 2 };

static Lesson31State *lesson31_state(ForgeGpuDemo *demo)
{
    return (Lesson31State *)demo->lesson.private_state;
}

static void lesson31_init_camera(ForgeGpuDemo *demo)
{
    demo->lesson.camera_position = {
        LESSON31_CAMERA_START_X,
        LESSON31_CAMERA_START_Y,
        LESSON31_CAMERA_START_Z
    };
    demo->lesson.camera_yaw = LESSON31_CAMERA_START_YAW_DEG * FORGE_GPU_DEG2RAD;
    demo->lesson.camera_pitch = LESSON31_CAMERA_START_PITCH_DEG * FORGE_GPU_DEG2RAD;
    demo->lesson.pitch_clamp = LESSON31_PITCH_CLAMP;
    demo->lesson.mouse_sensitivity = LESSON31_MOUSE_SENSITIVITY;
    demo->lesson.move_speed = LESSON31_CAMERA_SPEED;
    demo->lesson.last_ticks = SDL_GetTicks();
}

static float lesson31_delta_seconds(ForgeGpuDemo *demo, Lesson31State *state)
{
    const Uint64 now = SDL_GetPerformanceCounter();
    float dt;

    if (demo->validation_mode) {
        return 1.0f / 60.0f;
    }
    if (state->last_animation_counter == 0) {
        state->last_animation_counter = now;
        return 0.0f;
    }

    dt = (float)((double)(now - state->last_animation_counter) / (double)SDL_GetPerformanceFrequency());
    state->last_animation_counter = now;
    return SDL_min(dt, FORGE_GPU_MAX_DELTA_TIME);
}

static float lesson31_evaluate_path_yaw(float distance, const float *seg_cumulative)
{
    int seg = 0;
    float seg_start;
    float seg_len;
    float frac;
    int i0;
    int i1;
    Quat q0;
    Quat q1;
    Quat result;

    while (seg < LESSON31_PATH_WAYPOINT_COUNT - 1 && distance >= seg_cumulative[seg]) {
        seg += 1;
    }

    seg_start = seg > 0 ? seg_cumulative[seg - 1] : 0.0f;
    seg_len = seg_cumulative[seg] - seg_start;
    frac = seg_len > 1e-6f ? (distance - seg_start) / seg_len : 0.0f;
    i0 = seg;
    i1 = (seg + 1) % LESSON31_PATH_WAYPOINT_COUNT;
    q0 = quat_from_axis_angle({ 0.0f, 1.0f, 0.0f }, kLesson31PathWaypoints[i0].yaw);
    q1 = quat_from_axis_angle({ 0.0f, 1.0f, 0.0f }, kLesson31PathWaypoints[i1].yaw);
    result = quat_slerp(q0, q1, frac);
    return 2.0f * SDL_atan2f(result.y, result.w);
}

static void lesson31_init_path(Lesson31State *state)
{
    float cumulative = 0.0f;

    for (int i = 0; i < LESSON31_PATH_WAYPOINT_COUNT; i += 1) {
        const Vec3 from = kLesson31PathWaypoints[i].position;
        const Vec3 to = kLesson31PathWaypoints[(i + 1) % LESSON31_PATH_WAYPOINT_COUNT].position;
        cumulative += SDL_sqrtf(vec3_dot(vec3_sub(to, from), vec3_sub(to, from)));
        state->seg_cumulative[i] = cumulative;
    }
    state->total_path_len = cumulative;
    state->path_distance = 0.0f;
    state->truck_pos = kLesson31PathWaypoints[0].position;
    state->truck_yaw = kLesson31PathWaypoints[0].yaw;
}

static Mat4 lesson31_track_piece_transform(float x, float z, float angle)
{
    return mat4_multiply(mat4_translate({ x, 0.0f, z }), mat4_rotate_y(angle));
}

static void lesson31_init_track_pieces(Lesson31State *state)
{
    const float sx = LESSON31_TRACK_CX + LESSON31_TRACK_OFFSET;
    const float tz = LESSON31_TRACK_CZ + LESSON31_TRACK_OFFSET;

    state->track_pieces[0] = { LESSON31_TRACK_MESH_CORNER_15, lesson31_track_piece_transform(LESSON31_TRACK_CX, LESSON31_TRACK_CZ, FORGE_GPU_PI * 0.5f) };
    state->track_pieces[1] = { LESSON31_TRACK_MESH_LINE_30, lesson31_track_piece_transform(sx, 0.0f, 0.0f) };
    state->track_pieces[2] = { LESSON31_TRACK_MESH_CORNER_15, lesson31_track_piece_transform(LESSON31_TRACK_CX, -LESSON31_TRACK_CZ, FORGE_GPU_PI) };
    state->track_pieces[3] = { LESSON31_TRACK_MESH_LINE_15, lesson31_track_piece_transform(0.0f, -tz, FORGE_GPU_PI * 0.5f) };
    state->track_pieces[4] = { LESSON31_TRACK_MESH_CORNER_15, lesson31_track_piece_transform(-LESSON31_TRACK_CX, -LESSON31_TRACK_CZ, -FORGE_GPU_PI * 0.5f) };
    state->track_pieces[5] = { LESSON31_TRACK_MESH_LINE_30, lesson31_track_piece_transform(-sx, 0.0f, 0.0f) };
    state->track_pieces[6] = { LESSON31_TRACK_MESH_CORNER_15, lesson31_track_piece_transform(-LESSON31_TRACK_CX, LESSON31_TRACK_CZ, 0.0f) };
    state->track_pieces[7] = { LESSON31_TRACK_MESH_START_FIN, lesson31_track_piece_transform(0.0f, tz, FORGE_GPU_PI * 0.5f) };
}

static bool lesson31_create_samplers(ForgeGpuDemo *demo)
{
    demo->lesson.samplers[0] = ForgeGpuCreateSamplerWithAddressAndAnisotropy(
        demo->device,
        SDL_GPU_FILTER_LINEAR,
        SDL_GPU_FILTER_LINEAR,
        SDL_GPU_SAMPLERMIPMAPMODE_LINEAR,
        SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
        LESSON31_MAX_LOD_UNLIMITED,
        LESSON31_MAX_ANISOTROPY);
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
        SDL_GPU_SAMPLERMIPMAPMODE_LINEAR,
        SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        LESSON31_MAX_LOD_UNLIMITED);
    return demo->lesson.samplers[0] && demo->lesson.samplers[1] && demo->lesson.samplers[2];
}

static bool lesson31_create_geometry(ForgeGpuDemo *demo)
{
    Lesson31State *state = lesson31_state(demo);
    const ForgeGpuMeshVertex floor_vertices[4] = {
        { { -LESSON31_GROUND_HALF_SIZE, LESSON31_GROUND_Y, -LESSON31_GROUND_HALF_SIZE }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f } },
        { {  LESSON31_GROUND_HALF_SIZE, LESSON31_GROUND_Y, -LESSON31_GROUND_HALF_SIZE }, { 0.0f, 1.0f, 0.0f }, { LESSON31_GROUND_UV_REPEAT, 0.0f } },
        { {  LESSON31_GROUND_HALF_SIZE, LESSON31_GROUND_Y,  LESSON31_GROUND_HALF_SIZE }, { 0.0f, 1.0f, 0.0f }, { LESSON31_GROUND_UV_REPEAT, LESSON31_GROUND_UV_REPEAT } },
        { { -LESSON31_GROUND_HALF_SIZE, LESSON31_GROUND_Y,  LESSON31_GROUND_HALF_SIZE }, { 0.0f, 1.0f, 0.0f }, { 0.0f, LESSON31_GROUND_UV_REPEAT } },
    };
    const float skybox_vertices[8 * 3] = {
        -1.0f, -1.0f, -1.0f,
         1.0f, -1.0f, -1.0f,
         1.0f,  1.0f, -1.0f,
        -1.0f,  1.0f, -1.0f,
        -1.0f, -1.0f,  1.0f,
         1.0f, -1.0f,  1.0f,
         1.0f,  1.0f,  1.0f,
        -1.0f,  1.0f,  1.0f
    };
    const Uint16 skybox_indices[36] = {
        0, 2, 1,  0, 3, 2,
        4, 5, 6,  4, 6, 7,
        0, 4, 7,  0, 7, 3,
        1, 2, 6,  1, 6, 5,
        0, 1, 5,  0, 5, 4,
        3, 7, 6,  3, 6, 2,
    };

    state->floor_vertex_buffer = ForgeGpuCreateBufferWithData(demo->device, SDL_GPU_BUFFERUSAGE_VERTEX, floor_vertices, sizeof(floor_vertices));
    state->floor_index_buffer = ForgeGpuCreateBufferWithData(demo->device, SDL_GPU_BUFFERUSAGE_INDEX, kLesson31QuadIndices, sizeof(kLesson31QuadIndices));
    state->skybox_vertex_buffer = ForgeGpuCreateBufferWithData(demo->device, SDL_GPU_BUFFERUSAGE_VERTEX, skybox_vertices, sizeof(skybox_vertices));
    state->skybox_index_buffer = ForgeGpuCreateBufferWithData(demo->device, SDL_GPU_BUFFERUSAGE_INDEX, skybox_indices, sizeof(skybox_indices));
    return state->floor_vertex_buffer &&
           state->floor_index_buffer &&
           state->skybox_vertex_buffer &&
           state->skybox_index_buffer;
}

static bool lesson31_create_shadow_pipeline(ForgeGpuDemo *demo)
{
    Lesson31State *state = lesson31_state(demo);
    SDL_GPUShader *vertex_shader;
    SDL_GPUShader *fragment_shader;
    SDL_GPUVertexBufferDescription vertex_buffer;
    SDL_GPUVertexAttribute attributes[3];

    vertex_shader = ForgeGpuCreateShader(
        demo->device,
        SDL_GPU_SHADERSTAGE_VERTEX,
        lesson31_shadow_vert_wgsl,
        lesson31_shadow_vert_wgsl_size,
        lesson31_shadow_vert_msl,
        lesson31_shadow_vert_msl_size,
        0, 0, 0, 1);
    if (!vertex_shader) {
        return false;
    }
    fragment_shader = ForgeGpuCreateShader(
        demo->device,
        SDL_GPU_SHADERSTAGE_FRAGMENT,
        lesson31_shadow_frag_wgsl,
        lesson31_shadow_frag_wgsl_size,
        lesson31_shadow_frag_msl,
        lesson31_shadow_frag_msl_size,
        0, 0, 0, 0);
    if (!fragment_shader) {
        SDL_ReleaseGPUShader(demo->device, vertex_shader);
        return false;
    }

    ForgeGpuFillMeshVertexInput(&vertex_buffer, attributes);
    state->shadow_pipeline = ForgeGpuCreateLessonGraphicsPipelineWithColorTargetsAndDepthCompare(
        demo,
        vertex_shader,
        fragment_shader,
        SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        nullptr,
        0,
        &vertex_buffer,
        1,
        attributes,
        SDL_arraysize(attributes),
        true,
        LESSON31_DEPTH_FORMAT,
        true,
        true,
        SDL_GPU_COMPAREOP_LESS,
        SDL_GPU_CULLMODE_FRONT,
        0.0f,
        0.0f);

    SDL_ReleaseGPUShader(demo->device, vertex_shader);
    SDL_ReleaseGPUShader(demo->device, fragment_shader);
    return state->shadow_pipeline != nullptr;
}

static bool lesson31_create_scene_pipeline(ForgeGpuDemo *demo)
{
    Lesson31State *state = lesson31_state(demo);
    SDL_GPUShader *vertex_shader;
    SDL_GPUShader *fragment_shader;
    SDL_GPUVertexBufferDescription vertex_buffer;
    SDL_GPUVertexAttribute attributes[3];

    vertex_shader = ForgeGpuCreateShader(
        demo->device,
        SDL_GPU_SHADERSTAGE_VERTEX,
        lesson31_scene_vert_wgsl,
        lesson31_scene_vert_wgsl_size,
        lesson31_scene_vert_msl,
        lesson31_scene_vert_msl_size,
        0, 0, 0, 1);
    if (!vertex_shader) {
        return false;
    }
    fragment_shader = ForgeGpuCreateShaderWithResourceLayout(
        demo->device,
        lesson31_scene_frag_wgsl,
        lesson31_scene_frag_wgsl_size,
        lesson31_scene_frag_msl,
        lesson31_scene_frag_msl_size,
        ForgeGpuShaderLayout_lesson31_scene_frag());
    if (!fragment_shader) {
        SDL_ReleaseGPUShader(demo->device, vertex_shader);
        return false;
    }

    ForgeGpuFillMeshVertexInput(&vertex_buffer, attributes);
    state->scene_pipeline = ForgeGpuCreateLessonGraphicsPipelineWithPrimitive(
        demo,
        vertex_shader,
        fragment_shader,
        SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        &vertex_buffer,
        1,
        attributes,
        SDL_arraysize(attributes),
        1,
        true,
        LESSON31_DEPTH_FORMAT,
        true,
        true,
        SDL_GPU_CULLMODE_BACK,
        0.0f,
        0.0f);

    SDL_ReleaseGPUShader(demo->device, vertex_shader);
    SDL_ReleaseGPUShader(demo->device, fragment_shader);
    return state->scene_pipeline != nullptr;
}

static bool lesson31_create_skybox_pipeline(ForgeGpuDemo *demo)
{
    Lesson31State *state = lesson31_state(demo);
    SDL_GPUShader *vertex_shader;
    SDL_GPUShader *fragment_shader;
    SDL_GPUVertexBufferDescription vertex_buffer;
    SDL_GPUVertexAttribute attribute;

    vertex_shader = ForgeGpuCreateShader(
        demo->device,
        SDL_GPU_SHADERSTAGE_VERTEX,
        lesson31_skybox_vert_wgsl,
        lesson31_skybox_vert_wgsl_size,
        lesson31_skybox_vert_msl,
        lesson31_skybox_vert_msl_size,
        0, 0, 0, 1);
    if (!vertex_shader) {
        return false;
    }
    fragment_shader = ForgeGpuCreateShaderWithResourceLayout(
        demo->device,
        lesson31_skybox_frag_wgsl,
        lesson31_skybox_frag_wgsl_size,
        lesson31_skybox_frag_msl,
        lesson31_skybox_frag_msl_size,
        ForgeGpuShaderLayout_lesson31_skybox_frag());
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
    state->skybox_pipeline = ForgeGpuCreateLessonGraphicsPipelineWithPrimitive(
        demo,
        vertex_shader,
        fragment_shader,
        SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        &vertex_buffer,
        1,
        &attribute,
        1,
        1,
        true,
        LESSON31_DEPTH_FORMAT,
        true,
        false,
        SDL_GPU_CULLMODE_FRONT,
        0.0f,
        0.0f);

    SDL_ReleaseGPUShader(demo->device, vertex_shader);
    SDL_ReleaseGPUShader(demo->device, fragment_shader);
    return state->skybox_pipeline != nullptr;
}

static bool lesson31_create_pipelines(ForgeGpuDemo *demo)
{
    return lesson31_create_shadow_pipeline(demo) &&
           lesson31_create_scene_pipeline(demo) &&
           lesson31_create_skybox_pipeline(demo);
}

static ForgeGpuForwardSceneLighting lesson31_forward_lighting(float shininess, float specular_strength)
{
    ForgeGpuForwardSceneLighting lighting;

    lighting.light_dir = { LESSON31_LIGHT_DIR_X, LESSON31_LIGHT_DIR_Y, LESSON31_LIGHT_DIR_Z };
    lighting.light_color = { LESSON31_LIGHT_COLOR_R, LESSON31_LIGHT_COLOR_G, LESSON31_LIGHT_COLOR_B };
    lighting.light_intensity = LESSON31_LIGHT_INTENSITY;
    lighting.ambient = LESSON31_MATERIAL_AMBIENT;
    lighting.shininess = shininess;
    lighting.specular_strength = specular_strength;
    return lighting;
}

static ForgeGpuForwardSceneDrawInfo lesson31_forward_draw_info(
    ForgeGpuDemo *demo,
    Mat4 cam_vp,
    Vec3 eye_pos,
    float shininess,
    float specular_strength)
{
    Lesson31State *state = lesson31_state(demo);
    ForgeGpuForwardSceneDrawInfo draw_info;

    draw_info.cam_vp = cam_vp;
    draw_info.light_vp = state->light_vp;
    draw_info.eye_pos = eye_pos;
    draw_info.shadow_depth = state->shadow_depth;
    draw_info.fallback_texture = demo->lesson.white_texture;
    draw_info.material_sampler = demo->lesson.samplers[0];
    draw_info.shadow_sampler = demo->lesson.samplers[1];
    draw_info.lighting = lesson31_forward_lighting(shininess, specular_strength);
    return draw_info;
}

static void lesson31_draw_model_scene(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    const GpuSceneData *model,
    Mat4 placement,
    Mat4 cam_vp,
    Vec3 eye_pos)
{
    const ForgeGpuForwardSceneDrawInfo draw_info = lesson31_forward_draw_info(
        demo,
        cam_vp,
        eye_pos,
        LESSON31_MATERIAL_SHININESS,
        LESSON31_MATERIAL_SPECULAR_STRENGTH);

    ForgeGpuDrawForwardSceneModel(command_buffer, render_pass, model, placement, &draw_info);
}

static void lesson31_draw_track_piece_scene(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    int mesh_index,
    Mat4 world,
    Mat4 cam_vp,
    Vec3 eye_pos)
{
    Lesson31State *state = lesson31_state(demo);
    const ForgeGpuForwardSceneDrawInfo draw_info = lesson31_forward_draw_info(
        demo,
        cam_vp,
        eye_pos,
        LESSON31_MATERIAL_SHININESS,
        LESSON31_MATERIAL_SPECULAR_STRENGTH);

    ForgeGpuDrawForwardSceneMesh(command_buffer, render_pass, &state->track, mesh_index, world, &draw_info);
}

static void lesson31_draw_floor(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    Mat4 cam_vp,
    Vec3 eye_pos)
{
    Lesson31State *state = lesson31_state(demo);
    ForgeGpuForwardSceneDrawInfo draw_info;
    GpuMaterial material;

    SDL_zero(material);
    material.base_color[0] = 1.0f;
    material.base_color[1] = 1.0f;
    material.base_color[2] = 1.0f;
    material.base_color[3] = 1.0f;
    material.texture = state->dirt_texture;
    material.has_texture = true;

    draw_info = lesson31_forward_draw_info(
        demo,
        cam_vp,
        eye_pos,
        LESSON31_GROUND_SHININESS,
        LESSON31_GROUND_SPECULAR_STRENGTH);
    ForgeGpuDrawForwardSceneBuffer(
        command_buffer,
        render_pass,
        state->floor_vertex_buffer,
        state->floor_index_buffer,
        SDL_GPU_INDEXELEMENTSIZE_16BIT,
        SDL_arraysize(kLesson31QuadIndices),
        mat4_identity(),
        &material,
        &draw_info);
}

static void lesson31_draw_floor_shadow(
    Lesson31State *state,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass)
{
    ForgeGpuDrawForwardShadowBuffer(
        command_buffer,
        render_pass,
        state->floor_vertex_buffer,
        state->floor_index_buffer,
        SDL_GPU_INDEXELEMENTSIZE_16BIT,
        SDL_arraysize(kLesson31QuadIndices),
        state->light_vp);
}

static void lesson31_draw_track_piece_shadow(
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    const GpuSceneData *model,
    int mesh_index,
    Mat4 world,
    Mat4 light_vp)
{
    ForgeGpuDrawForwardShadowMesh(command_buffer, render_pass, model, mesh_index, world, light_vp);
}

static void lesson31_draw_skybox(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    Mat4 view,
    Mat4 projection)
{
    Lesson31State *state = lesson31_state(demo);

    ForgeGpuDrawForwardSkybox(
        command_buffer,
        render_pass,
        state->cubemap_texture,
        demo->lesson.samplers[2],
        state->skybox_vertex_buffer,
        state->skybox_index_buffer,
        SDL_GPU_INDEXELEMENTSIZE_16BIT,
        36,
        view,
        projection);
}

static bool lesson31_run_shadow_pass(ForgeGpuDemo *demo, SDL_GPUCommandBuffer *command_buffer, Mat4 truck_placement)
{
    Lesson31State *state = lesson31_state(demo);
    SDL_GPURenderPass *render_pass;

    render_pass = ForgeGpuBeginDepthOnlyPass(command_buffer, state->shadow_depth, 1.0f);
    if (!render_pass) {
        return false;
    }
    SDL_BindGPUGraphicsPipeline(render_pass, state->shadow_pipeline);
    ForgeGpuDrawModelShadow(command_buffer, render_pass, &state->truck, truck_placement, state->light_vp);
    for (int i = 0; i < LESSON31_TRACK_PIECE_COUNT; i += 1) {
        lesson31_draw_track_piece_shadow(
            command_buffer,
            render_pass,
            &state->track,
            state->track_pieces[i].mesh_index,
            state->track_pieces[i].transform,
            state->light_vp);
    }
    lesson31_draw_floor_shadow(state, command_buffer, render_pass);
    SDL_EndGPURenderPass(render_pass);
    state->shadow_pass_rendered = true;
    return true;
}

static bool lesson31_run_main_pass(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPUTexture *swapchain_texture,
    Mat4 cam_vp,
    Mat4 view,
    Mat4 projection,
    Mat4 truck_placement)
{
    Lesson31State *state = lesson31_state(demo);
    SDL_GPUColorTargetInfo color_target;
    SDL_GPUDepthStencilTargetInfo depth_target;
    SDL_GPURenderPass *render_pass;

    SDL_zero(color_target);
    color_target.texture = swapchain_texture;
    color_target.load_op = SDL_GPU_LOADOP_CLEAR;
    color_target.store_op = SDL_GPU_STOREOP_STORE;
    color_target.clear_color = { LESSON31_CLEAR_R, LESSON31_CLEAR_G, LESSON31_CLEAR_B, 1.0f };
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
    lesson31_draw_floor(demo, command_buffer, render_pass, cam_vp, demo->lesson.camera_position);
    lesson31_draw_model_scene(demo, command_buffer, render_pass, &state->truck, truck_placement, cam_vp, demo->lesson.camera_position);
    for (int i = 0; i < LESSON31_TRACK_PIECE_COUNT; i += 1) {
        lesson31_draw_track_piece_scene(
            demo,
            command_buffer,
            render_pass,
            state->track_pieces[i].mesh_index,
            state->track_pieces[i].transform,
            cam_vp,
            demo->lesson.camera_position);
    }

    SDL_BindGPUGraphicsPipeline(render_pass, state->skybox_pipeline);
    lesson31_draw_skybox(demo, command_buffer, render_pass, view, projection);

    SDL_EndGPURenderPass(render_pass);
    state->main_pass_rendered = true;
    return true;
}

bool ForgeGpuCreateLesson31(ForgeGpuDemo *demo)
{
    Lesson31State *state;
    ForgeGpuSceneLoadRequirements truck_requirements;

    if (!SDL_GPUTextureSupportsFormat(
            demo->device,
            LESSON31_DEPTH_FORMAT,
            SDL_GPU_TEXTURETYPE_2D,
            SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER)) {
        SDL_SetError("lesson 31 requires sampled D32_FLOAT depth targets");
        return false;
    }
    if (!SDL_GPUTextureSupportsFormat(
            demo->device,
            SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB,
            SDL_GPU_TEXTURETYPE_CUBE,
            SDL_GPU_TEXTUREUSAGE_SAMPLER)) {
        SDL_SetError("lesson 31 requires sampled sRGB cube textures");
        return false;
    }

    state = (Lesson31State *)SDL_calloc(1, sizeof(*state));
    if (!state) {
        SDL_OutOfMemory();
        return false;
    }
    demo->lesson.private_state = state;
    demo->lesson.white_texture = ForgeGpuCreateWhiteTexture(demo->device);
    state->cubemap_texture = ForgeGpuLoadCubeTexture(demo, "skyboxes/citrus-orchard");
    state->dirt_texture = ForgeGpuCreateNoiseDirtTexture(
        demo->device,
        LESSON31_DIRT_TEXTURE_SIZE,
        LESSON31_DIRT_NOISE_SCALE,
        LESSON31_DIRT_NOISE_SEED,
        LESSON31_DIRT_NOISE_OCTAVES,
        LESSON31_DIRT_NOISE_LACUNARITY,
        LESSON31_DIRT_NOISE_PERSISTENCE,
        LESSON31_DIRT_BASE_R,
        LESSON31_DIRT_RANGE_R,
        LESSON31_DIRT_BASE_G,
        LESSON31_DIRT_RANGE_G,
        LESSON31_DIRT_BASE_B,
        LESSON31_DIRT_RANGE_B);
    state->shadow_depth = ForgeGpuCreateSampledDepthTexture(
        demo,
        LESSON31_SHADOW_MAP_SIZE,
        LESSON31_SHADOW_MAP_SIZE,
        LESSON31_DEPTH_FORMAT);

    SDL_zero(truck_requirements);
    truck_requirements.required_features = FORGE_GPU_SCENE_FEATURE_NODE_HIERARCHY | FORGE_GPU_SCENE_FEATURE_ANIMATIONS;
    if (!demo->lesson.white_texture ||
        !state->cubemap_texture ||
        !state->dirt_texture ||
        !state->shadow_depth ||
        !lesson31_create_samplers(demo) ||
        !lesson31_create_geometry(demo) ||
        !ForgeGpuLoadSceneModelWithRequirements(demo, &state->truck, "models/CesiumMilkTruck/CesiumMilkTruck.gltf", &truck_requirements) ||
        !ForgeGpuLoadSceneModel(demo, &state->track, "models/TransformAnimations/track/scene.gltf") ||
        !lesson31_create_pipelines(demo)) {
        return false;
    }

    state->animation_data_ready =
        state->truck.loaded.animation_count > 0 &&
        state->truck.loaded.node_count > 0 &&
        state->truck.loaded.root_node_count > 0;
    lesson31_init_path(state);
    lesson31_init_track_pieces(state);
    state->light_vp = ForgeGpuComputeDirectionalLightViewProjection(
        { LESSON31_LIGHT_DIR_X, LESSON31_LIGHT_DIR_Y, LESSON31_LIGHT_DIR_Z },
        LESSON31_LIGHT_DISTANCE,
        LESSON31_SHADOW_ORTHO_SIZE,
        LESSON31_SHADOW_NEAR,
        LESSON31_SHADOW_FAR,
        LESSON31_PARALLEL_THRESHOLD);
    lesson31_init_camera(demo);
    return true;
}

bool ForgeGpuRenderLesson31(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPUTexture *swapchain_texture,
    Uint32 width,
    Uint32 height)
{
    Lesson31State *state = lesson31_state(demo);
    float dt;
    float step;
    Mat4 truck_placement;
    Mat4 view;
    Mat4 projection;
    Mat4 cam_vp;

    if (!state) {
        SDL_SetError("lesson 31 internal state is missing");
        return false;
    }
    if (!ForgeGpuEnsureSampledDepthTarget(
            demo,
            &state->main_depth,
            &state->main_depth_width,
            &state->main_depth_height,
            width,
            height,
            LESSON31_DEPTH_FORMAT)) {
        return false;
    }

    ForgeGpuUpdateCameraFromInput(demo);
    dt = lesson31_delta_seconds(demo, state);

    if (state->truck.loaded.animation_count > 0) {
        state->wheel_time += dt * LESSON31_ANIM_SPEED;
        if (!ForgeGpuApplySceneAnimation(&state->truck.loaded, 0, state->wheel_time, true)) {
            return false;
        }
        state->animation_applied = true;
    }

    step = LESSON31_TRUCK_DRIVE_SPEED * dt;
    state->path_distance += step;
    if (state->path_distance >= state->total_path_len) {
        state->path_distance -= state->total_path_len;
    }
    state->truck_yaw = lesson31_evaluate_path_yaw(state->path_distance, state->seg_cumulative);
    state->truck_pos.x += SDL_sinf(state->truck_yaw) * step;
    state->truck_pos.z += SDL_cosf(state->truck_yaw) * step;
    state->truck_pos.y = LESSON31_TRUCK_Y;

    if (!ForgeGpuRecomputeSceneWorldTransforms(&state->truck.loaded)) {
        return false;
    }

    truck_placement = mat4_multiply(mat4_translate(state->truck_pos), mat4_rotate_y(state->truck_yaw));
    ForgeGpuCameraViewProjection(demo, width, height, LESSON31_FAR_PLANE, &view, &projection);
    cam_vp = mat4_multiply(projection, view);
    state->shadow_pass_rendered = false;
    state->main_pass_rendered = false;

    return lesson31_run_shadow_pass(demo, command_buffer, truck_placement) &&
           lesson31_run_main_pass(demo, command_buffer, swapchain_texture, cam_vp, view, projection, truck_placement);
}

void ForgeGpuDebugLesson31(ForgeGpuDemo *demo)
{
    Lesson31State *state = lesson31_state(demo);

    if (!state) {
        return;
    }
    ImGui::Text("Truck animation: %s", state->animation_applied ? "active" : "pending");
    ImGui::Text("Wheel time: %.2fs", state->wheel_time);
    ImGui::Text("Path distance: %.2f / %.2f", state->path_distance, state->total_path_len);
    ImGui::Text("Track pieces: %d", LESSON31_TRACK_PIECE_COUNT);
    ImGui::Text("Shadow: D32 2048x2048");
}

void ForgeGpuExportLesson31Metrics(ForgeGpuDemo *demo)
{
    Lesson31State *state = lesson31_state(demo);

    if (!state) {
        return;
    }
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuTransformAnimations", 1.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuTransformAnimationDataReady", state->animation_data_ready ? 1.0 : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuTransformAnimationApplied", state->animation_applied ? 1.0 : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuTransformTrackPieces", (double)LESSON31_TRACK_PIECE_COUNT);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuTransformPassesRendered", state->shadow_pass_rendered && state->main_pass_rendered ? 1.0 : 0.0);
}

void ForgeGpuDestroyLesson31(ForgeGpuDemo *demo)
{
    Lesson31State *state = lesson31_state(demo);

    if (!state) {
        return;
    }
    ForgeGpuFreeSceneData(demo, &state->track);
    ForgeGpuFreeSceneData(demo, &state->truck);
    if (state->skybox_index_buffer) {
        SDL_ReleaseGPUBuffer(demo->device, state->skybox_index_buffer);
    }
    if (state->skybox_vertex_buffer) {
        SDL_ReleaseGPUBuffer(demo->device, state->skybox_vertex_buffer);
    }
    if (state->floor_index_buffer) {
        SDL_ReleaseGPUBuffer(demo->device, state->floor_index_buffer);
    }
    if (state->floor_vertex_buffer) {
        SDL_ReleaseGPUBuffer(demo->device, state->floor_vertex_buffer);
    }
    if (state->dirt_texture) {
        SDL_ReleaseGPUTexture(demo->device, state->dirt_texture);
    }
    if (state->cubemap_texture) {
        SDL_ReleaseGPUTexture(demo->device, state->cubemap_texture);
    }
    if (state->main_depth) {
        SDL_ReleaseGPUTexture(demo->device, state->main_depth);
    }
    if (state->shadow_depth) {
        SDL_ReleaseGPUTexture(demo->device, state->shadow_depth);
    }
    if (state->skybox_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, state->skybox_pipeline);
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
