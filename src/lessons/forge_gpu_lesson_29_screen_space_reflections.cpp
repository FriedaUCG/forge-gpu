#include "forge_gpu_lessons.h"

#include "forge_gpu_browser_status.h"
#include "forge_gpu_camera.h"
#include "forge_gpu_deferred_scene.h"
#include "forge_gpu_gpu_helpers.h"
#include "forge_gpu_lesson_common.h"
#include "forge_gpu_math.h"
#include "forge_gpu_scene.h"
#include "forge_gpu_shader_layouts.h"
#include "shaders/generated/forge_gpu_lesson_29_shaders.h"
#include "imgui.h"

#include <stddef.h>

#define LESSON29_MODEL_TRUCK 0
#define LESSON29_MODEL_BOX 1
#define LESSON29_MODEL_COUNT 2
#define LESSON29_BOX_COUNT 8
#define LESSON29_FULLSCREEN_VERTICES 6
#define LESSON29_CAMERA_SPEED 5.0f
#define LESSON29_CAMERA_START_X 4.0f
#define LESSON29_CAMERA_START_Y 3.0f
#define LESSON29_CAMERA_START_Z 7.0f
#define LESSON29_CAMERA_START_YAW_DEG 30.0f
#define LESSON29_CAMERA_START_PITCH_DEG -8.0f
#define LESSON29_MOUSE_SENSITIVITY 0.003f
#define LESSON29_PITCH_CLAMP 1.5f
#define LESSON29_LIGHT_DIR_X -0.5f
#define LESSON29_LIGHT_DIR_Y -0.8f
#define LESSON29_LIGHT_DIR_Z -0.5f
#define LESSON29_LIGHT_INTENSITY 0.8f
#define LESSON29_LIGHT_COLOR_R 1.0f
#define LESSON29_LIGHT_COLOR_G 0.95f
#define LESSON29_LIGHT_COLOR_B 0.9f
#define LESSON29_MATERIAL_AMBIENT 0.15f
#define LESSON29_MATERIAL_SHININESS 64.0f
#define LESSON29_MATERIAL_SPECULAR_STRENGTH 0.3f
#define LESSON29_SHADOW_MAP_SIZE 2048u
#define LESSON29_SHADOW_FORMAT SDL_GPU_TEXTUREFORMAT_D32_FLOAT
#define LESSON29_GBUFFER_COLOR_FORMAT SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM
#define LESSON29_GBUFFER_NORMAL_FORMAT SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT
#define LESSON29_GBUFFER_WORLD_POSITION_FORMAT SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT
#define LESSON29_GBUFFER_DEPTH_FORMAT SDL_GPU_TEXTUREFORMAT_D32_FLOAT
#define LESSON29_SSR_FORMAT SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM
#define LESSON29_SHADOW_ORTHO_SIZE 15.0f
#define LESSON29_SHADOW_NEAR 0.1f
#define LESSON29_SHADOW_FAR 50.0f
#define LESSON29_LIGHT_DISTANCE 20.0f
#define LESSON29_PARALLEL_THRESHOLD 0.99f
#define LESSON29_SSR_MAX_DISTANCE 20.0f
#define LESSON29_SSR_STEP_SIZE 0.15f
#define LESSON29_SSR_MAX_STEPS 128
#define LESSON29_SSR_THICKNESS 0.15f
#define LESSON29_SSR_REFLECTION_STRENGTH 0.8f
#define LESSON29_GRID_REFLECTIVITY 0.9f
#define LESSON29_GRID_SPACING 1.0f
#define LESSON29_GRID_LINE_WIDTH 0.02f
#define LESSON29_GRID_FADE_DISTANCE 40.0f
#define LESSON29_MODE_FINAL 0
#define LESSON29_MODE_SSR_ONLY 1
#define LESSON29_MODE_NORMALS 2
#define LESSON29_MODE_DEPTH 3
#define LESSON29_MODE_WORLD_POSITION 4

struct Lesson29GridVertUniforms
{
    Mat4 vp;
    Mat4 view;
    Mat4 light_vp;
};

struct Lesson29GridFragUniforms
{
    float line_color[4];
    float bg_color[4];
    float eye_pos[3];
    float grid_spacing;
    float line_width;
    float fade_distance;
    float ambient;
    float light_intensity;
    float light_dir[4];
    float light_color[3];
    float reflectivity;
};

struct Lesson29SSRUniforms
{
    Mat4 projection;
    Mat4 inv_projection;
    Mat4 view;
    float screen_width;
    float screen_height;
    float step_size;
    float max_distance;
    Sint32 max_steps;
    float thickness;
    float pad[2];
};

struct Lesson29CompositeUniforms
{
    Sint32 display_mode;
    float reflection_str;
    float pad[2];
};

struct Lesson29State
{
    GpuSceneData models[LESSON29_MODEL_COUNT];
    SDL_GPUGraphicsPipeline *shadow_pipeline;
    SDL_GPUGraphicsPipeline *scene_pipeline;
    SDL_GPUGraphicsPipeline *grid_pipeline;
    SDL_GPUGraphicsPipeline *ssr_pipeline;
    SDL_GPUGraphicsPipeline *composite_pipeline;
    SDL_GPUTexture *scene_color;
    SDL_GPUTexture *view_normals;
    SDL_GPUTexture *world_position;
    SDL_GPUTexture *scene_depth;
    SDL_GPUTexture *ssr_output;
    SDL_GPUTexture *shadow_depth;
    Uint32 scene_color_width;
    Uint32 scene_color_height;
    Uint32 view_normals_width;
    Uint32 view_normals_height;
    Uint32 world_position_width;
    Uint32 world_position_height;
    Uint32 scene_depth_width;
    Uint32 scene_depth_height;
    Uint32 ssr_output_width;
    Uint32 ssr_output_height;
    Uint32 target_width;
    Uint32 target_height;
    ForgeGpuBoxPlacement box_placements[LESSON29_BOX_COUNT];
    Mat4 light_vp;
    Sint32 display_mode;
};

static_assert(sizeof(ForgeGpuDeferredSceneVertUniforms) == 256, "lesson 29 scene vertex uniform size must match HLSL layout");
static_assert(sizeof(ForgeGpuDeferredSceneFragUniforms) == 80, "lesson 29 scene fragment uniform size must match HLSL layout");
static_assert(sizeof(ForgeGpuShadowVertUniforms) == 64, "lesson 29 shadow vertex uniform size must match HLSL layout");
static_assert(sizeof(Lesson29GridVertUniforms) == 192, "lesson 29 grid vertex uniform size must match HLSL layout");
static_assert(sizeof(Lesson29GridFragUniforms) == 96, "lesson 29 grid fragment uniform size must match HLSL layout");
static_assert(sizeof(Lesson29SSRUniforms) == 224, "lesson 29 SSR uniform size must match HLSL layout");
static_assert(sizeof(Lesson29CompositeUniforms) == 16, "lesson 29 composite uniform size must match HLSL layout");

static Lesson29State *lesson29_state(ForgeGpuDemo *demo)
{
    return (Lesson29State *)demo->lesson.private_state;
}

static const char *lesson29_mode_name(Sint32 mode)
{
    switch (mode) {
    case LESSON29_MODE_SSR_ONLY:
        return "SSR only";
    case LESSON29_MODE_NORMALS:
        return "View-space normals";
    case LESSON29_MODE_DEPTH:
        return "Depth";
    case LESSON29_MODE_WORLD_POSITION:
        return "World position";
    case LESSON29_MODE_FINAL:
    default:
        return "Final";
    }
}

static bool lesson29_ensure_targets(ForgeGpuDemo *demo, Uint32 width, Uint32 height)
{
    Lesson29State *state = lesson29_state(demo);
    ForgeGpuSampledColorTargetSlot color_targets[4];

    if (!state) {
        SDL_SetError("lesson 29 internal state is missing");
        return false;
    }
    color_targets[0] = { &state->scene_color, &state->scene_color_width, &state->scene_color_height, LESSON29_GBUFFER_COLOR_FORMAT };
    color_targets[1] = { &state->view_normals, &state->view_normals_width, &state->view_normals_height, LESSON29_GBUFFER_NORMAL_FORMAT };
    color_targets[2] = { &state->world_position, &state->world_position_width, &state->world_position_height, LESSON29_GBUFFER_WORLD_POSITION_FORMAT };
    color_targets[3] = { &state->ssr_output, &state->ssr_output_width, &state->ssr_output_height, LESSON29_SSR_FORMAT };
    if (!ForgeGpuEnsureSampledColorTargetSlots(demo, color_targets, SDL_arraysize(color_targets), width, height)) {
        return false;
    }
    if (!ForgeGpuEnsureSampledDepthTarget(
            demo, &state->scene_depth, &state->scene_depth_width, &state->scene_depth_height,
            width, height, LESSON29_GBUFFER_DEPTH_FORMAT)) {
        return false;
    }
    state->target_width = width;
    state->target_height = height;
    return true;
}

static bool lesson29_create_shadow_pipeline(ForgeGpuDemo *demo)
{
    Lesson29State *state = lesson29_state(demo);
    SDL_GPUShader *vertex_shader;
    SDL_GPUShader *fragment_shader;
    SDL_GPUVertexBufferDescription vertex_buffer;
    SDL_GPUVertexAttribute attributes[3];

    vertex_shader = ForgeGpuCreateShader(
        demo->device,
        SDL_GPU_SHADERSTAGE_VERTEX,
        lesson29_shadow_vert_wgsl,
        lesson29_shadow_vert_wgsl_size,
        lesson29_shadow_vert_msl,
        lesson29_shadow_vert_msl_size,
        0, 0, 0, 1);
    if (!vertex_shader) {
        return false;
    }
    fragment_shader = ForgeGpuCreateShader(
        demo->device,
        SDL_GPU_SHADERSTAGE_FRAGMENT,
        lesson29_shadow_frag_wgsl,
        lesson29_shadow_frag_wgsl_size,
        lesson29_shadow_frag_msl,
        lesson29_shadow_frag_msl_size,
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
        LESSON29_SHADOW_FORMAT,
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

static bool lesson29_create_scene_pipeline(ForgeGpuDemo *demo)
{
    Lesson29State *state = lesson29_state(demo);
    SDL_GPUShader *vertex_shader;
    SDL_GPUShader *fragment_shader;
    SDL_GPUVertexBufferDescription vertex_buffer;
    SDL_GPUVertexAttribute attributes[3];
    SDL_GPUColorTargetDescription color_targets[3];

    vertex_shader = ForgeGpuCreateShader(
        demo->device,
        SDL_GPU_SHADERSTAGE_VERTEX,
        lesson29_scene_vert_wgsl,
        lesson29_scene_vert_wgsl_size,
        lesson29_scene_vert_msl,
        lesson29_scene_vert_msl_size,
        0, 0, 0, 1);
    if (!vertex_shader) {
        return false;
    }
    fragment_shader = ForgeGpuCreateShaderWithResourceLayout(
        demo->device,
        lesson29_scene_frag_wgsl,
        lesson29_scene_frag_wgsl_size,
        lesson29_scene_frag_msl,
        lesson29_scene_frag_msl_size,
        ForgeGpuShaderLayout_lesson29_scene_frag());
    if (!fragment_shader) {
        SDL_ReleaseGPUShader(demo->device, vertex_shader);
        return false;
    }

    ForgeGpuFillMeshVertexInput(&vertex_buffer, attributes);
    SDL_zeroa(color_targets);
    color_targets[0].format = LESSON29_GBUFFER_COLOR_FORMAT;
    color_targets[1].format = LESSON29_GBUFFER_NORMAL_FORMAT;
    color_targets[2].format = LESSON29_GBUFFER_WORLD_POSITION_FORMAT;
    state->scene_pipeline = ForgeGpuCreateLessonGraphicsPipelineWithColorTargetsAndDepthCompare(
        demo,
        vertex_shader,
        fragment_shader,
        SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        color_targets,
        SDL_arraysize(color_targets),
        &vertex_buffer,
        1,
        attributes,
        SDL_arraysize(attributes),
        true,
        LESSON29_GBUFFER_DEPTH_FORMAT,
        true,
        true,
        SDL_GPU_COMPAREOP_LESS,
        SDL_GPU_CULLMODE_BACK,
        0.0f,
        0.0f);

    SDL_ReleaseGPUShader(demo->device, vertex_shader);
    SDL_ReleaseGPUShader(demo->device, fragment_shader);
    return state->scene_pipeline != nullptr;
}

static bool lesson29_create_grid_pipeline(ForgeGpuDemo *demo)
{
    Lesson29State *state = lesson29_state(demo);
    SDL_GPUShader *vertex_shader;
    SDL_GPUShader *fragment_shader;
    SDL_GPUVertexBufferDescription vertex_buffer;
    SDL_GPUVertexAttribute attribute;
    SDL_GPUColorTargetDescription color_targets[3];

    vertex_shader = ForgeGpuCreateShader(
        demo->device,
        SDL_GPU_SHADERSTAGE_VERTEX,
        lesson29_grid_vert_wgsl,
        lesson29_grid_vert_wgsl_size,
        lesson29_grid_vert_msl,
        lesson29_grid_vert_msl_size,
        0, 0, 0, 1);
    if (!vertex_shader) {
        return false;
    }
    fragment_shader = ForgeGpuCreateShaderWithResourceLayout(
        demo->device,
        lesson29_grid_frag_wgsl,
        lesson29_grid_frag_wgsl_size,
        lesson29_grid_frag_msl,
        lesson29_grid_frag_msl_size,
        ForgeGpuShaderLayout_lesson29_grid_frag());
    if (!fragment_shader) {
        SDL_ReleaseGPUShader(demo->device, vertex_shader);
        return false;
    }

    SDL_zero(vertex_buffer);
    vertex_buffer.slot = 0;
    vertex_buffer.pitch = sizeof(GridVertex);
    vertex_buffer.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    SDL_zero(attribute);
    attribute.location = 0;
    attribute.buffer_slot = 0;
    attribute.format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    attribute.offset = offsetof(GridVertex, position);
    SDL_zeroa(color_targets);
    color_targets[0].format = LESSON29_GBUFFER_COLOR_FORMAT;
    color_targets[1].format = LESSON29_GBUFFER_NORMAL_FORMAT;
    color_targets[2].format = LESSON29_GBUFFER_WORLD_POSITION_FORMAT;
    state->grid_pipeline = ForgeGpuCreateLessonGraphicsPipelineWithColorTargetsAndDepthCompare(
        demo,
        vertex_shader,
        fragment_shader,
        SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        color_targets,
        SDL_arraysize(color_targets),
        &vertex_buffer,
        1,
        &attribute,
        1,
        true,
        LESSON29_GBUFFER_DEPTH_FORMAT,
        true,
        true,
        SDL_GPU_COMPAREOP_LESS,
        SDL_GPU_CULLMODE_NONE,
        0.0f,
        0.0f);

    SDL_ReleaseGPUShader(demo->device, vertex_shader);
    SDL_ReleaseGPUShader(demo->device, fragment_shader);
    return state->grid_pipeline != nullptr;
}

static bool lesson29_create_fullscreen_pipelines(ForgeGpuDemo *demo)
{
    Lesson29State *state = lesson29_state(demo);
    SDL_GPUShader *vertex_shader;
    SDL_GPUShader *ssr_shader;
    SDL_GPUShader *composite_shader;

    vertex_shader = ForgeGpuCreateShader(
        demo->device,
        SDL_GPU_SHADERSTAGE_VERTEX,
        lesson29_fullscreen_vert_wgsl,
        lesson29_fullscreen_vert_wgsl_size,
        lesson29_fullscreen_vert_msl,
        lesson29_fullscreen_vert_msl_size,
        0, 0, 0, 0);
    if (!vertex_shader) {
        return false;
    }
    ssr_shader = ForgeGpuCreateShaderWithResourceLayout(
        demo->device,
        lesson29_ssr_frag_wgsl,
        lesson29_ssr_frag_wgsl_size,
        lesson29_ssr_frag_msl,
        lesson29_ssr_frag_msl_size,
        ForgeGpuShaderLayout_lesson29_ssr_frag());
    if (!ssr_shader) {
        SDL_ReleaseGPUShader(demo->device, vertex_shader);
        return false;
    }
    composite_shader = ForgeGpuCreateShaderWithResourceLayout(
        demo->device,
        lesson29_composite_frag_wgsl,
        lesson29_composite_frag_wgsl_size,
        lesson29_composite_frag_msl,
        lesson29_composite_frag_msl_size,
        ForgeGpuShaderLayout_lesson29_composite_frag());
    if (!composite_shader) {
        SDL_ReleaseGPUShader(demo->device, ssr_shader);
        SDL_ReleaseGPUShader(demo->device, vertex_shader);
        return false;
    }

    state->ssr_pipeline = ForgeGpuCreateFullscreenPostprocessPipeline(demo, vertex_shader, ssr_shader, LESSON29_SSR_FORMAT, false);
    state->composite_pipeline = ForgeGpuCreateFullscreenPostprocessPipeline(demo, vertex_shader, composite_shader, demo->color_format, false);

    SDL_ReleaseGPUShader(demo->device, composite_shader);
    SDL_ReleaseGPUShader(demo->device, ssr_shader);
    SDL_ReleaseGPUShader(demo->device, vertex_shader);
    return state->ssr_pipeline && state->composite_pipeline;
}

static bool lesson29_create_pipelines(ForgeGpuDemo *demo)
{
    return lesson29_create_shadow_pipeline(demo) &&
           lesson29_create_scene_pipeline(demo) &&
           lesson29_create_grid_pipeline(demo) &&
           lesson29_create_fullscreen_pipelines(demo);
}

static bool lesson29_create_samplers(ForgeGpuDemo *demo)
{
    LessonState *lesson = &demo->lesson;

    lesson->samplers[0] = ForgeGpuCreateSamplerWithAddressAndAnisotropy(
        demo->device,
        SDL_GPU_FILTER_LINEAR,
        SDL_GPU_FILTER_LINEAR,
        SDL_GPU_SAMPLERMIPMAPMODE_LINEAR,
        SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
        1000.0f,
        4.0f);
    lesson->samplers[1] = ForgeGpuCreateSamplerWithAddress(
        demo->device,
        SDL_GPU_FILTER_NEAREST,
        SDL_GPU_FILTER_NEAREST,
        SDL_GPU_SAMPLERMIPMAPMODE_NEAREST,
        SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        0.0f);
    lesson->samplers[2] = ForgeGpuCreateSamplerWithAddress(
        demo->device,
        SDL_GPU_FILTER_LINEAR,
        SDL_GPU_FILTER_LINEAR,
        SDL_GPU_SAMPLERMIPMAPMODE_NEAREST,
        SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        0.0f);
    return lesson->samplers[0] && lesson->samplers[1] && lesson->samplers[2];
}

static void lesson29_init_box_placements(Lesson29State *state)
{
    const Vec3 positions[LESSON29_BOX_COUNT] = {
        { -3.5f, 0.5f,  2.0f },
        { -2.5f, 0.5f,  0.5f },
        {  3.0f, 0.5f, -2.0f },
        { -1.0f, 0.5f, -3.0f },
        { -3.5f, 1.5f,  2.0f },
        {  4.0f, 0.5f,  1.5f },
        { -4.5f, 0.5f, -1.0f },
        {  2.0f, 0.5f,  3.5f }
    };
    const float rotations[LESSON29_BOX_COUNT] = {
        0.3f, 1.1f, 0.7f, 2.0f, 0.9f, 1.5f, 0.2f, 2.5f
    };

    for (int i = 0; i < LESSON29_BOX_COUNT; i += 1) {
        state->box_placements[i].position = positions[i];
        state->box_placements[i].y_rotation = rotations[i];
    }
}

static void lesson29_init_light(Lesson29State *state)
{
    state->light_vp = ForgeGpuComputeDirectionalLightViewProjection(
        { LESSON29_LIGHT_DIR_X, LESSON29_LIGHT_DIR_Y, LESSON29_LIGHT_DIR_Z },
        LESSON29_LIGHT_DISTANCE,
        LESSON29_SHADOW_ORTHO_SIZE,
        LESSON29_SHADOW_NEAR,
        LESSON29_SHADOW_FAR,
        LESSON29_PARALLEL_THRESHOLD);
}

static void lesson29_init_camera(ForgeGpuDemo *demo)
{
    LessonState *lesson = &demo->lesson;

    lesson->camera_position = {
        LESSON29_CAMERA_START_X,
        LESSON29_CAMERA_START_Y,
        LESSON29_CAMERA_START_Z
    };
    lesson->camera_yaw = LESSON29_CAMERA_START_YAW_DEG * FORGE_GPU_DEG2RAD;
    lesson->camera_pitch = LESSON29_CAMERA_START_PITCH_DEG * FORGE_GPU_DEG2RAD;
    lesson->mouse_sensitivity = LESSON29_MOUSE_SENSITIVITY;
    lesson->pitch_clamp = LESSON29_PITCH_CLAMP;
    lesson->move_speed = LESSON29_CAMERA_SPEED;
    lesson->last_ticks = SDL_GetTicks();
}

static void lesson29_draw_grid(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    Mat4 cam_vp,
    Mat4 view)
{
    Lesson29State *state = lesson29_state(demo);
    Lesson29GridVertUniforms vertex_uniforms;
    Lesson29GridFragUniforms fragment_uniforms;
    SDL_GPUTextureSamplerBinding shadow_binding;
    SDL_GPUBufferBinding vertex_binding;
    SDL_GPUBufferBinding index_binding;

    vertex_uniforms.vp = cam_vp;
    vertex_uniforms.view = view;
    vertex_uniforms.light_vp = state->light_vp;
    SDL_PushGPUVertexUniformData(command_buffer, 0, &vertex_uniforms, sizeof(vertex_uniforms));

    SDL_zero(fragment_uniforms);
    fragment_uniforms.line_color[0] = 0.15f;
    fragment_uniforms.line_color[1] = 0.55f;
    fragment_uniforms.line_color[2] = 0.85f;
    fragment_uniforms.line_color[3] = 1.0f;
    fragment_uniforms.bg_color[0] = 0.04f;
    fragment_uniforms.bg_color[1] = 0.04f;
    fragment_uniforms.bg_color[2] = 0.08f;
    fragment_uniforms.bg_color[3] = 1.0f;
    fragment_uniforms.eye_pos[0] = demo->lesson.camera_position.x;
    fragment_uniforms.eye_pos[1] = demo->lesson.camera_position.y;
    fragment_uniforms.eye_pos[2] = demo->lesson.camera_position.z;
    fragment_uniforms.grid_spacing = LESSON29_GRID_SPACING;
    fragment_uniforms.line_width = LESSON29_GRID_LINE_WIDTH;
    fragment_uniforms.fade_distance = LESSON29_GRID_FADE_DISTANCE;
    fragment_uniforms.ambient = LESSON29_MATERIAL_AMBIENT;
    fragment_uniforms.light_intensity = LESSON29_LIGHT_INTENSITY;
    fragment_uniforms.light_dir[0] = LESSON29_LIGHT_DIR_X;
    fragment_uniforms.light_dir[1] = LESSON29_LIGHT_DIR_Y;
    fragment_uniforms.light_dir[2] = LESSON29_LIGHT_DIR_Z;
    fragment_uniforms.light_color[0] = LESSON29_LIGHT_COLOR_R;
    fragment_uniforms.light_color[1] = LESSON29_LIGHT_COLOR_G;
    fragment_uniforms.light_color[2] = LESSON29_LIGHT_COLOR_B;
    fragment_uniforms.reflectivity = LESSON29_GRID_REFLECTIVITY;
    SDL_PushGPUFragmentUniformData(command_buffer, 0, &fragment_uniforms, sizeof(fragment_uniforms));

    SDL_zero(shadow_binding);
    shadow_binding.texture = state->shadow_depth;
    shadow_binding.sampler = demo->lesson.samplers[1];
    SDL_BindGPUFragmentSamplers(render_pass, 0, &shadow_binding, 1);

    SDL_zero(vertex_binding);
    vertex_binding.buffer = demo->lesson.vertex_buffer;
    SDL_zero(index_binding);
    index_binding.buffer = demo->lesson.index_buffer;
    SDL_BindGPUVertexBuffers(render_pass, 0, &vertex_binding, 1);
    SDL_BindGPUIndexBuffer(render_pass, &index_binding, SDL_GPU_INDEXELEMENTSIZE_16BIT);
    SDL_DrawGPUIndexedPrimitives(render_pass, 6, 1, 0, 0, 0);
}

static bool lesson29_run_shadow_pass(ForgeGpuDemo *demo, SDL_GPUCommandBuffer *command_buffer)
{
    Lesson29State *state = lesson29_state(demo);
    SDL_GPURenderPass *render_pass;

    render_pass = ForgeGpuBeginDepthOnlyPass(command_buffer, state->shadow_depth, 1.0f);
    if (!render_pass) {
        return false;
    }

    SDL_BindGPUGraphicsPipeline(render_pass, state->shadow_pipeline);
    ForgeGpuDrawShadowedBoxScene(
        command_buffer,
        render_pass,
        &state->models[LESSON29_MODEL_TRUCK],
        &state->models[LESSON29_MODEL_BOX],
        state->box_placements,
        LESSON29_BOX_COUNT,
        state->light_vp);
    SDL_EndGPURenderPass(render_pass);
    return true;
}

static bool lesson29_run_geometry_pass(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    Mat4 cam_vp,
    Mat4 view)
{
    Lesson29State *state = lesson29_state(demo);
    ForgeGpuColorTargetAttachment color_targets[3];
    ForgeGpuDeferredSceneDrawInfo draw_info;
    SDL_GPURenderPass *render_pass;

    SDL_zeroa(color_targets);
    color_targets[0].texture = state->scene_color;
    color_targets[0].clear_color = { 0.008f, 0.008f, 0.026f, 1.0f };
    color_targets[1].texture = state->view_normals;
    color_targets[1].clear_color = { 0.0f, 0.0f, 0.0f, 0.0f };
    color_targets[2].texture = state->world_position;
    color_targets[2].clear_color = { 0.0f, 0.0f, 0.0f, 0.0f };

    render_pass = ForgeGpuBeginColorDepthPass(command_buffer, color_targets, SDL_arraysize(color_targets), state->scene_depth, 1.0f);
    if (!render_pass) {
        return false;
    }

    SDL_BindGPUGraphicsPipeline(render_pass, state->grid_pipeline);
    lesson29_draw_grid(demo, command_buffer, render_pass, cam_vp, view);

    SDL_BindGPUGraphicsPipeline(render_pass, state->scene_pipeline);
    SDL_zero(draw_info);
    draw_info.cam_vp = cam_vp;
    draw_info.view = view;
    draw_info.light_vp = state->light_vp;
    draw_info.shadow_depth = state->shadow_depth;
    draw_info.material_sampler = demo->lesson.samplers[0];
    draw_info.shadow_sampler = demo->lesson.samplers[1];
    draw_info.lighting.light_dir = { LESSON29_LIGHT_DIR_X, LESSON29_LIGHT_DIR_Y, LESSON29_LIGHT_DIR_Z };
    draw_info.lighting.light_color = { LESSON29_LIGHT_COLOR_R, LESSON29_LIGHT_COLOR_G, LESSON29_LIGHT_COLOR_B };
    draw_info.lighting.light_intensity = LESSON29_LIGHT_INTENSITY;
    draw_info.lighting.ambient = LESSON29_MATERIAL_AMBIENT;
    draw_info.lighting.shininess = LESSON29_MATERIAL_SHININESS;
    draw_info.lighting.specular_strength = LESSON29_MATERIAL_SPECULAR_STRENGTH;
    ForgeGpuDrawDeferredBoxScene(
        demo,
        command_buffer,
        render_pass,
        &state->models[LESSON29_MODEL_TRUCK],
        &state->models[LESSON29_MODEL_BOX],
        state->box_placements,
        LESSON29_BOX_COUNT,
        &draw_info);

    SDL_EndGPURenderPass(render_pass);
    return true;
}

static bool lesson29_run_ssr_pass(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    Uint32 width,
    Uint32 height,
    Mat4 projection,
    Mat4 inv_projection,
    Mat4 view)
{
    Lesson29State *state = lesson29_state(demo);
    SDL_GPUTextureSamplerBinding bindings[4];
    Lesson29SSRUniforms uniforms;

    SDL_zeroa(bindings);
    bindings[0].texture = state->scene_color;
    bindings[0].sampler = demo->lesson.samplers[2];
    bindings[1].texture = state->scene_depth;
    bindings[1].sampler = demo->lesson.samplers[1];
    bindings[2].texture = state->view_normals;
    bindings[2].sampler = demo->lesson.samplers[1];
    bindings[3].texture = state->world_position;
    bindings[3].sampler = demo->lesson.samplers[1];

    SDL_zero(uniforms);
    uniforms.projection = projection;
    uniforms.inv_projection = inv_projection;
    uniforms.view = view;
    uniforms.screen_width = (float)width;
    uniforms.screen_height = (float)height;
    uniforms.step_size = LESSON29_SSR_STEP_SIZE;
    uniforms.max_distance = LESSON29_SSR_MAX_DISTANCE;
    uniforms.max_steps = LESSON29_SSR_MAX_STEPS;
    uniforms.thickness = LESSON29_SSR_THICKNESS;

    return ForgeGpuRunFullscreenPostprocessPass(
        command_buffer,
        state->ssr_output,
        SDL_GPU_LOADOP_CLEAR,
        { 0.0f, 0.0f, 0.0f, 0.0f },
        state->ssr_pipeline,
        bindings,
        SDL_arraysize(bindings),
        &uniforms,
        sizeof(uniforms),
        LESSON29_FULLSCREEN_VERTICES);
}

static bool lesson29_run_composite_pass(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPUTexture *swapchain_texture)
{
    Lesson29State *state = lesson29_state(demo);
    SDL_GPUTextureSamplerBinding bindings[5];
    Lesson29CompositeUniforms uniforms;

    SDL_zeroa(bindings);
    bindings[0].texture = state->scene_color;
    bindings[0].sampler = demo->lesson.samplers[2];
    bindings[1].texture = state->ssr_output;
    bindings[1].sampler = demo->lesson.samplers[2];
    bindings[2].texture = state->scene_depth;
    bindings[2].sampler = demo->lesson.samplers[1];
    bindings[3].texture = state->view_normals;
    bindings[3].sampler = demo->lesson.samplers[1];
    bindings[4].texture = state->world_position;
    bindings[4].sampler = demo->lesson.samplers[1];

    SDL_zero(uniforms);
    uniforms.display_mode = state->display_mode;
    uniforms.reflection_str = LESSON29_SSR_REFLECTION_STRENGTH;

    return ForgeGpuRunFullscreenPostprocessPass(
        command_buffer,
        swapchain_texture,
        SDL_GPU_LOADOP_DONT_CARE,
        { 0.0f, 0.0f, 0.0f, 1.0f },
        state->composite_pipeline,
        bindings,
        SDL_arraysize(bindings),
        &uniforms,
        sizeof(uniforms),
        LESSON29_FULLSCREEN_VERTICES);
}

bool ForgeGpuCreateLesson29(ForgeGpuDemo *demo)
{
    Lesson29State *state;

    if (!SDL_GPUTextureSupportsFormat(
            demo->device,
            LESSON29_GBUFFER_COLOR_FORMAT,
            SDL_GPU_TEXTURETYPE_2D,
            SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER)) {
        SDL_SetError("lesson 29 requires sampled R8G8B8A8_UNORM color targets");
        return false;
    }
    if (!SDL_GPUTextureSupportsFormat(
            demo->device,
            LESSON29_GBUFFER_NORMAL_FORMAT,
            SDL_GPU_TEXTURETYPE_2D,
            SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER)) {
        SDL_SetError("lesson 29 requires sampled R16G16B16A16_FLOAT normal targets");
        return false;
    }
    if (!SDL_GPUTextureSupportsFormat(
            demo->device,
            LESSON29_GBUFFER_WORLD_POSITION_FORMAT,
            SDL_GPU_TEXTURETYPE_2D,
            SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER)) {
        SDL_SetError("lesson 29 requires sampled R16G16B16A16_FLOAT world-position targets");
        return false;
    }
    if (!SDL_GPUTextureSupportsFormat(
            demo->device,
            LESSON29_GBUFFER_DEPTH_FORMAT,
            SDL_GPU_TEXTURETYPE_2D,
            SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER)) {
        SDL_SetError("lesson 29 requires sampled D32_FLOAT depth targets");
        return false;
    }
    if (!SDL_GPUTextureSupportsFormat(
            demo->device,
            LESSON29_SSR_FORMAT,
            SDL_GPU_TEXTURETYPE_2D,
            SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER)) {
        SDL_SetError("lesson 29 requires sampled R8G8B8A8_UNORM SSR output targets");
        return false;
    }

    state = (Lesson29State *)SDL_calloc(1, sizeof(*state));
    if (!state) {
        SDL_OutOfMemory();
        return false;
    }
    demo->lesson.private_state = state;
    state->display_mode = LESSON29_MODE_FINAL;

    demo->lesson.white_texture = ForgeGpuCreateWhiteTexture(demo->device);
    if (!demo->lesson.white_texture ||
        !lesson29_create_samplers(demo) ||
        !ForgeGpuCreateGridBuffers(demo) ||
        !ForgeGpuLoadSceneModel(demo, &state->models[LESSON29_MODEL_TRUCK], "models/CesiumMilkTruck/CesiumMilkTruck.gltf") ||
        !ForgeGpuLoadSceneModel(demo, &state->models[LESSON29_MODEL_BOX], "models/BoxTextured/BoxTextured.gltf") ||
        !lesson29_create_pipelines(demo)) {
        return false;
    }
    state->shadow_depth = ForgeGpuCreateSampledDepthTexture(demo, LESSON29_SHADOW_MAP_SIZE, LESSON29_SHADOW_MAP_SIZE, LESSON29_SHADOW_FORMAT);
    if (!state->shadow_depth) {
        return false;
    }

    lesson29_init_box_placements(state);
    lesson29_init_light(state);
    lesson29_init_camera(demo);
    return true;
}

bool ForgeGpuRenderLesson29(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPUTexture *swapchain_texture,
    Uint32 width,
    Uint32 height)
{
    Mat4 view;
    Mat4 projection;
    Mat4 cam_vp;
    Mat4 inv_projection;

    if (!lesson29_ensure_targets(demo, width, height)) {
        return false;
    }

    ForgeGpuUpdateCameraFromInput(demo);
    ForgeGpuCameraViewProjection(demo, width, height, 100.0f, &view, &projection);
    cam_vp = mat4_multiply(projection, view);
    inv_projection = mat4_inverse(projection);

    return lesson29_run_shadow_pass(demo, command_buffer) &&
           lesson29_run_geometry_pass(demo, command_buffer, cam_vp, view) &&
           lesson29_run_ssr_pass(demo, command_buffer, width, height, projection, inv_projection, view) &&
           lesson29_run_composite_pass(demo, command_buffer, swapchain_texture);
}

void ForgeGpuDebugLesson29(ForgeGpuDemo *demo)
{
    Lesson29State *state = lesson29_state(demo);

    if (!state) {
        return;
    }
    ImGui::Text("Mode: %s", lesson29_mode_name(state->display_mode));
    ImGui::Text("Targets: %ux%u", state->target_width, state->target_height);
    ImGui::Text("G-buffer: R8G8B8A8 color, R16G16B16A16 normals/world position, D32 depth");
    ImGui::Text("SSR: R8G8B8A8 output, %d max steps", LESSON29_SSR_MAX_STEPS);
}

void ForgeGpuControlsLesson29(ForgeGpuDemo *demo)
{
    (void)demo;
    ImGui::Text("1 final, 2 SSR only, 3 normals, 4 depth, 5 world position");
}

bool ForgeGpuHandleLesson29Event(ForgeGpuDemo *demo, const SDL_Event *event)
{
    Lesson29State *state = lesson29_state(demo);

    if (!state || event->type != SDL_EVENT_KEY_DOWN || event->key.repeat) {
        return false;
    }
    switch (event->key.key) {
    case SDLK_1:
        state->display_mode = LESSON29_MODE_FINAL;
        return true;
    case SDLK_2:
        state->display_mode = LESSON29_MODE_SSR_ONLY;
        return true;
    case SDLK_3:
        state->display_mode = LESSON29_MODE_NORMALS;
        return true;
    case SDLK_4:
        state->display_mode = LESSON29_MODE_DEPTH;
        return true;
    case SDLK_5:
        state->display_mode = LESSON29_MODE_WORLD_POSITION;
        return true;
    default:
        break;
    }
    return false;
}

void ForgeGpuExportLesson29Metrics(ForgeGpuDemo *demo)
{
    Lesson29State *state = lesson29_state(demo);

    if (!state) {
        return;
    }
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuSsr", 1.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuSsrDisplayMode", (double)state->display_mode);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuSsrTargetsReady", state->scene_color && state->view_normals && state->world_position && state->scene_depth && state->ssr_output ? 1.0 : 0.0);
}

void ForgeGpuDestroyLesson29(ForgeGpuDemo *demo)
{
    Lesson29State *state = lesson29_state(demo);

    if (!state) {
        return;
    }

    for (int i = 0; i < LESSON29_MODEL_COUNT; i += 1) {
        ForgeGpuFreeSceneData(demo, &state->models[i]);
    }
    if (state->shadow_depth) {
        SDL_ReleaseGPUTexture(demo->device, state->shadow_depth);
    }
    if (state->ssr_output) {
        SDL_ReleaseGPUTexture(demo->device, state->ssr_output);
    }
    if (state->scene_depth) {
        SDL_ReleaseGPUTexture(demo->device, state->scene_depth);
    }
    if (state->world_position) {
        SDL_ReleaseGPUTexture(demo->device, state->world_position);
    }
    if (state->view_normals) {
        SDL_ReleaseGPUTexture(demo->device, state->view_normals);
    }
    if (state->scene_color) {
        SDL_ReleaseGPUTexture(demo->device, state->scene_color);
    }
    if (state->composite_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, state->composite_pipeline);
    }
    if (state->ssr_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, state->ssr_pipeline);
    }
    if (state->grid_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, state->grid_pipeline);
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
