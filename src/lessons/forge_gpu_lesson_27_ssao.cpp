#include "forge_gpu_lessons.h"

#include "forge_gpu_browser_status.h"
#include "forge_gpu_camera.h"
#include "forge_gpu_deferred_scene.h"
#include "forge_gpu_gpu_helpers.h"
#include "forge_gpu_lesson_common.h"
#include "forge_gpu_math.h"
#include "forge_gpu_scene.h"
#include "forge_gpu_shader_layouts.h"
#include "shaders/generated/forge_gpu_lesson_27_shaders.h"
#include "imgui.h"

#include <stddef.h>

#define LESSON27_MODEL_TRUCK 0
#define LESSON27_MODEL_BOX 1
#define LESSON27_MODEL_COUNT 2
#define LESSON27_BOX_COUNT 5
#define LESSON27_FULLSCREEN_VERTICES 6
#define LESSON27_CAMERA_SPEED 5.0f
#define LESSON27_CAMERA_START_X 2.0f
#define LESSON27_CAMERA_START_Y 1.5f
#define LESSON27_CAMERA_START_Z 3.5f
#define LESSON27_CAMERA_START_YAW_DEG 30.0f
#define LESSON27_CAMERA_START_PITCH_DEG -8.0f
#define LESSON27_MOUSE_SENSITIVITY 0.003f
#define LESSON27_PITCH_CLAMP 1.5f
#define LESSON27_LIGHT_DIR_X -0.5f
#define LESSON27_LIGHT_DIR_Y -0.8f
#define LESSON27_LIGHT_DIR_Z -0.5f
#define LESSON27_LIGHT_INTENSITY 0.8f
#define LESSON27_LIGHT_COLOR_R 1.0f
#define LESSON27_LIGHT_COLOR_G 0.95f
#define LESSON27_LIGHT_COLOR_B 0.9f
#define LESSON27_MATERIAL_AMBIENT 0.15f
#define LESSON27_MATERIAL_SHININESS 64.0f
#define LESSON27_MATERIAL_SPECULAR_STRENGTH 0.3f
#define LESSON27_SHADOW_MAP_SIZE 2048u
#define LESSON27_SHADOW_FORMAT SDL_GPU_TEXTUREFORMAT_D32_FLOAT
#define LESSON27_GBUFFER_COLOR_FORMAT SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM
#define LESSON27_GBUFFER_NORMAL_FORMAT SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT
#define LESSON27_GBUFFER_DEPTH_FORMAT SDL_GPU_TEXTUREFORMAT_D32_FLOAT
#define LESSON27_SSAO_FORMAT SDL_GPU_TEXTUREFORMAT_R8_UNORM
#define LESSON27_NOISE_FORMAT SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT
#define LESSON27_SHADOW_ORTHO_SIZE 15.0f
#define LESSON27_SHADOW_NEAR 0.1f
#define LESSON27_SHADOW_FAR 50.0f
#define LESSON27_LIGHT_DISTANCE 20.0f
#define LESSON27_PARALLEL_THRESHOLD 0.99f
#define LESSON27_SSAO_KERNEL_SIZE 64
#define LESSON27_SSAO_RADIUS 0.5f
#define LESSON27_SSAO_BIAS 0.025f
#define LESSON27_NOISE_TEX_SIZE 4u
#define LESSON27_SSAO_DEFAULT_SEED 12345u
#define LESSON27_SSAO_EPSILON 0.0001f
#define LESSON27_SSAO_SCALE_START 0.1f
#define LESSON27_SSAO_SCALE_RANGE 0.9f
#define LESSON27_SSAO_SCALE_MIN 0.01f
#define LESSON27_NOISE_DEFAULT_SEED 67890u
#define LESSON27_NOISE_EPSILON 0.0001f
#define LESSON27_GRID_SPACING 1.0f
#define LESSON27_GRID_LINE_WIDTH 0.02f
#define LESSON27_GRID_FADE_DISTANCE 40.0f
#define LESSON27_MODE_AO_ONLY 0
#define LESSON27_MODE_WITH_AO 1
#define LESSON27_MODE_NO_AO 2

struct Lesson27GridVertUniforms
{
    Mat4 vp;
    Mat4 view;
    Mat4 light_vp;
};

struct Lesson27GridFragUniforms
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
    float pad;
};

struct Lesson27SSAOUniforms
{
    float samples[LESSON27_SSAO_KERNEL_SIZE * 4];
    Mat4 projection;
    Mat4 inv_projection;
    float noise_scale[2];
    float radius;
    float bias;
    Sint32 use_ign_jitter;
    float pad[3];
};

struct Lesson27BlurUniforms
{
    float texel_size[2];
    float pad[2];
};

struct Lesson27CompositeUniforms
{
    Sint32 display_mode;
    Sint32 use_dither;
    float pad[2];
};

struct Lesson27State
{
    GpuSceneData models[LESSON27_MODEL_COUNT];
    SDL_GPUGraphicsPipeline *shadow_pipeline;
    SDL_GPUGraphicsPipeline *scene_pipeline;
    SDL_GPUGraphicsPipeline *grid_pipeline;
    SDL_GPUGraphicsPipeline *ssao_pipeline;
    SDL_GPUGraphicsPipeline *blur_pipeline;
    SDL_GPUGraphicsPipeline *composite_pipeline;
    SDL_GPUTexture *scene_color;
    SDL_GPUTexture *view_normals;
    SDL_GPUTexture *scene_depth;
    SDL_GPUTexture *ssao_raw;
    SDL_GPUTexture *ssao_blurred;
    SDL_GPUTexture *shadow_depth;
    SDL_GPUTexture *noise_texture;
    Uint32 scene_color_width;
    Uint32 scene_color_height;
    Uint32 view_normals_width;
    Uint32 view_normals_height;
    Uint32 scene_depth_width;
    Uint32 scene_depth_height;
    Uint32 ssao_raw_width;
    Uint32 ssao_raw_height;
    Uint32 ssao_blurred_width;
    Uint32 ssao_blurred_height;
    Uint32 target_width;
    Uint32 target_height;
    ForgeGpuBoxPlacement box_placements[LESSON27_BOX_COUNT];
    Mat4 light_vp;
    float ssao_kernel[LESSON27_SSAO_KERNEL_SIZE * 4];
    Sint32 display_mode;
    bool use_ign_jitter;
    bool use_dither;
};

static_assert(sizeof(ForgeGpuDeferredSceneVertUniforms) == 256, "lesson 27 scene vertex uniform size must match HLSL layout");
static_assert(sizeof(ForgeGpuDeferredSceneFragUniforms) == 80, "lesson 27 scene fragment uniform size must match HLSL layout");
static_assert(sizeof(ForgeGpuShadowVertUniforms) == 64, "lesson 27 shadow vertex uniform size must match HLSL layout");
static_assert(sizeof(Lesson27GridVertUniforms) == 192, "lesson 27 grid vertex uniform size must match HLSL layout");
static_assert(sizeof(Lesson27GridFragUniforms) == 96, "lesson 27 grid fragment uniform size must match HLSL layout");
static_assert(sizeof(Lesson27SSAOUniforms) == 1184, "lesson 27 SSAO uniform size must match HLSL layout");
static_assert(sizeof(Lesson27BlurUniforms) == 16, "lesson 27 blur uniform size must match HLSL layout");
static_assert(sizeof(Lesson27CompositeUniforms) == 16, "lesson 27 composite uniform size must match HLSL layout");

static Lesson27State *lesson27_state(ForgeGpuDemo *demo)
{
    return (Lesson27State *)demo->lesson.private_state;
}

static const char *lesson27_mode_name(Sint32 mode)
{
    switch (mode) {
    case LESSON27_MODE_WITH_AO:
        return "Scene with AO";
    case LESSON27_MODE_NO_AO:
        return "Scene without AO";
    case LESSON27_MODE_AO_ONLY:
    default:
        return "AO only";
    }
}

static Uint32 lesson27_hash_pcg(Uint32 input)
{
    const Uint32 state = input * 747796405u + 2891336453u;
    const Uint32 word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

static float lesson27_hash_to_float(Uint32 h)
{
    return (float)(h >> 8) * (1.0f / 16777216.0f);
}

static float lesson27_hash_to_sfloat(Uint32 h)
{
    return lesson27_hash_to_float(h) * 2.0f - 1.0f;
}

static void lesson27_generate_ssao_kernel(float *kernel)
{
    Uint32 seed = LESSON27_SSAO_DEFAULT_SEED;

    for (int i = 0; i < LESSON27_SSAO_KERNEL_SIZE; i += 1) {
        seed = lesson27_hash_pcg(seed);
        float x = lesson27_hash_to_sfloat(seed);
        seed = lesson27_hash_pcg(seed);
        float y = lesson27_hash_to_sfloat(seed);
        seed = lesson27_hash_pcg(seed);
        float z = lesson27_hash_to_float(seed);
        float length = SDL_sqrtf(x * x + y * y + z * z);
        const float t = (float)i / (float)LESSON27_SSAO_KERNEL_SIZE;
        float scale = LESSON27_SSAO_SCALE_START + LESSON27_SSAO_SCALE_RANGE * t * t;

        if (length < LESSON27_SSAO_EPSILON) {
            length = 1.0f;
        }
        x /= length;
        y /= length;
        z /= length;

        seed = lesson27_hash_pcg(seed);
        scale *= lesson27_hash_to_float(seed);
        if (scale < LESSON27_SSAO_SCALE_MIN) {
            scale = LESSON27_SSAO_SCALE_MIN;
        }

        kernel[i * 4 + 0] = x * scale;
        kernel[i * 4 + 1] = y * scale;
        kernel[i * 4 + 2] = z * scale;
        kernel[i * 4 + 3] = 0.0f;
    }
}

static bool lesson27_upload_float_texture(
    ForgeGpuDemo *demo,
    SDL_GPUTexture *texture,
    const float *pixels,
    Uint32 width,
    Uint32 height)
{
    const Uint32 total_bytes = width * height * 4u * (Uint32)sizeof(float);
    SDL_GPUTransferBufferCreateInfo transfer_info;
    SDL_GPUTransferBuffer *transfer_buffer;
    void *mapped;
    SDL_GPUCommandBuffer *command_buffer;
    SDL_GPUCopyPass *copy_pass;
    SDL_GPUTextureTransferInfo source;
    SDL_GPUTextureRegion destination;

    SDL_zero(transfer_info);
    transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transfer_info.size = total_bytes;
    transfer_buffer = SDL_CreateGPUTransferBuffer(demo->device, &transfer_info);
    if (!transfer_buffer) {
        return false;
    }

    mapped = SDL_MapGPUTransferBuffer(demo->device, transfer_buffer, false);
    if (!mapped) {
        SDL_ReleaseGPUTransferBuffer(demo->device, transfer_buffer);
        return false;
    }
    SDL_memcpy(mapped, pixels, total_bytes);
    SDL_UnmapGPUTransferBuffer(demo->device, transfer_buffer);

    command_buffer = SDL_AcquireGPUCommandBuffer(demo->device);
    if (!command_buffer) {
        SDL_ReleaseGPUTransferBuffer(demo->device, transfer_buffer);
        return false;
    }
    copy_pass = SDL_BeginGPUCopyPass(command_buffer);
    if (!copy_pass) {
        SDL_ReleaseGPUTransferBuffer(demo->device, transfer_buffer);
        SDL_CancelGPUCommandBuffer(command_buffer);
        return false;
    }

    SDL_zero(source);
    source.transfer_buffer = transfer_buffer;
    SDL_zero(destination);
    destination.texture = texture;
    destination.w = width;
    destination.h = height;
    destination.d = 1;
    SDL_UploadToGPUTexture(copy_pass, &source, &destination, false);
    SDL_EndGPUCopyPass(copy_pass);

    if (!SDL_SubmitGPUCommandBuffer(command_buffer)) {
        SDL_ReleaseGPUTransferBuffer(demo->device, transfer_buffer);
        return false;
    }
    SDL_ReleaseGPUTransferBuffer(demo->device, transfer_buffer);
    return true;
}

static SDL_GPUTexture *lesson27_create_noise_texture(ForgeGpuDemo *demo)
{
    float noise[LESSON27_NOISE_TEX_SIZE * LESSON27_NOISE_TEX_SIZE * 4u];
    Uint32 seed = LESSON27_NOISE_DEFAULT_SEED;
    SDL_GPUTextureCreateInfo texture_info;
    SDL_GPUTexture *texture;

    for (Uint32 i = 0; i < LESSON27_NOISE_TEX_SIZE * LESSON27_NOISE_TEX_SIZE; i += 1) {
        seed = lesson27_hash_pcg(seed);
        float x = lesson27_hash_to_sfloat(seed);
        seed = lesson27_hash_pcg(seed);
        float y = lesson27_hash_to_sfloat(seed);
        float length = SDL_sqrtf(x * x + y * y);

        if (length < LESSON27_NOISE_EPSILON) {
            x = 1.0f;
            y = 0.0f;
            length = 1.0f;
        }
        noise[i * 4 + 0] = x / length;
        noise[i * 4 + 1] = y / length;
        noise[i * 4 + 2] = 0.0f;
        noise[i * 4 + 3] = 0.0f;
    }

    SDL_zero(texture_info);
    texture_info.type = SDL_GPU_TEXTURETYPE_2D;
    texture_info.format = LESSON27_NOISE_FORMAT;
    texture_info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    texture_info.width = LESSON27_NOISE_TEX_SIZE;
    texture_info.height = LESSON27_NOISE_TEX_SIZE;
    texture_info.layer_count_or_depth = 1;
    texture_info.num_levels = 1;
    texture_info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    texture = SDL_CreateGPUTexture(demo->device, &texture_info);
    if (!texture) {
        return nullptr;
    }
    if (!lesson27_upload_float_texture(demo, texture, noise, LESSON27_NOISE_TEX_SIZE, LESSON27_NOISE_TEX_SIZE)) {
        SDL_ReleaseGPUTexture(demo->device, texture);
        return nullptr;
    }
    return texture;
}

static bool lesson27_ensure_targets(ForgeGpuDemo *demo, Uint32 width, Uint32 height)
{
    Lesson27State *state = lesson27_state(demo);
    ForgeGpuSampledColorTargetSlot color_targets[4];

    if (!state) {
        SDL_SetError("lesson 27 internal state is missing");
        return false;
    }
    color_targets[0] = { &state->scene_color, &state->scene_color_width, &state->scene_color_height, LESSON27_GBUFFER_COLOR_FORMAT };
    color_targets[1] = { &state->view_normals, &state->view_normals_width, &state->view_normals_height, LESSON27_GBUFFER_NORMAL_FORMAT };
    color_targets[2] = { &state->ssao_raw, &state->ssao_raw_width, &state->ssao_raw_height, LESSON27_SSAO_FORMAT };
    color_targets[3] = { &state->ssao_blurred, &state->ssao_blurred_width, &state->ssao_blurred_height, LESSON27_SSAO_FORMAT };
    if (!ForgeGpuEnsureSampledColorTargetSlots(demo, color_targets, SDL_arraysize(color_targets), width, height)) {
        return false;
    }
    if (!ForgeGpuEnsureSampledDepthTarget(
            demo, &state->scene_depth, &state->scene_depth_width, &state->scene_depth_height,
            width, height, LESSON27_GBUFFER_DEPTH_FORMAT)) {
        return false;
    }
    state->target_width = width;
    state->target_height = height;
    return true;
}

static bool lesson27_create_shadow_pipeline(ForgeGpuDemo *demo)
{
    Lesson27State *state = lesson27_state(demo);
    SDL_GPUShader *vertex_shader;
    SDL_GPUShader *fragment_shader;
    SDL_GPUVertexBufferDescription vertex_buffer;
    SDL_GPUVertexAttribute attributes[3];

    vertex_shader = ForgeGpuCreateShader(
        demo->device,
        SDL_GPU_SHADERSTAGE_VERTEX,
        lesson27_shadow_vert_wgsl,
        lesson27_shadow_vert_wgsl_size,
        lesson27_shadow_vert_msl,
        lesson27_shadow_vert_msl_size,
        0, 0, 0, 1);
    if (!vertex_shader) {
        return false;
    }
    fragment_shader = ForgeGpuCreateShader(
        demo->device,
        SDL_GPU_SHADERSTAGE_FRAGMENT,
        lesson27_shadow_frag_wgsl,
        lesson27_shadow_frag_wgsl_size,
        lesson27_shadow_frag_msl,
        lesson27_shadow_frag_msl_size,
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
        LESSON27_SHADOW_FORMAT,
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

static bool lesson27_create_scene_pipeline(ForgeGpuDemo *demo)
{
    Lesson27State *state = lesson27_state(demo);
    SDL_GPUShader *vertex_shader;
    SDL_GPUShader *fragment_shader;
    SDL_GPUVertexBufferDescription vertex_buffer;
    SDL_GPUVertexAttribute attributes[3];
    SDL_GPUColorTargetDescription color_targets[2];

    vertex_shader = ForgeGpuCreateShader(
        demo->device,
        SDL_GPU_SHADERSTAGE_VERTEX,
        lesson27_scene_vert_wgsl,
        lesson27_scene_vert_wgsl_size,
        lesson27_scene_vert_msl,
        lesson27_scene_vert_msl_size,
        0, 0, 0, 1);
    if (!vertex_shader) {
        return false;
    }
    fragment_shader = ForgeGpuCreateShaderWithResourceLayout(
        demo->device,
        lesson27_scene_frag_wgsl,
        lesson27_scene_frag_wgsl_size,
        lesson27_scene_frag_msl,
        lesson27_scene_frag_msl_size,
        ForgeGpuShaderLayout_lesson27_scene_frag());
    if (!fragment_shader) {
        SDL_ReleaseGPUShader(demo->device, vertex_shader);
        return false;
    }

    ForgeGpuFillMeshVertexInput(&vertex_buffer, attributes);
    SDL_zeroa(color_targets);
    color_targets[0].format = LESSON27_GBUFFER_COLOR_FORMAT;
    color_targets[1].format = LESSON27_GBUFFER_NORMAL_FORMAT;
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
        LESSON27_GBUFFER_DEPTH_FORMAT,
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

static bool lesson27_create_grid_pipeline(ForgeGpuDemo *demo)
{
    Lesson27State *state = lesson27_state(demo);
    SDL_GPUShader *vertex_shader;
    SDL_GPUShader *fragment_shader;
    SDL_GPUVertexBufferDescription vertex_buffer;
    SDL_GPUVertexAttribute attribute;
    SDL_GPUColorTargetDescription color_targets[2];

    vertex_shader = ForgeGpuCreateShader(
        demo->device,
        SDL_GPU_SHADERSTAGE_VERTEX,
        lesson27_grid_vert_wgsl,
        lesson27_grid_vert_wgsl_size,
        lesson27_grid_vert_msl,
        lesson27_grid_vert_msl_size,
        0, 0, 0, 1);
    if (!vertex_shader) {
        return false;
    }
    fragment_shader = ForgeGpuCreateShaderWithResourceLayout(
        demo->device,
        lesson27_grid_frag_wgsl,
        lesson27_grid_frag_wgsl_size,
        lesson27_grid_frag_msl,
        lesson27_grid_frag_msl_size,
        ForgeGpuShaderLayout_lesson27_grid_frag());
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
    color_targets[0].format = LESSON27_GBUFFER_COLOR_FORMAT;
    color_targets[1].format = LESSON27_GBUFFER_NORMAL_FORMAT;
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
        LESSON27_GBUFFER_DEPTH_FORMAT,
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

static bool lesson27_create_fullscreen_pipelines(ForgeGpuDemo *demo)
{
    Lesson27State *state = lesson27_state(demo);
    SDL_GPUShader *vertex_shader;
    SDL_GPUShader *ssao_shader;
    SDL_GPUShader *blur_shader;
    SDL_GPUShader *composite_shader;

    vertex_shader = ForgeGpuCreateShader(
        demo->device,
        SDL_GPU_SHADERSTAGE_VERTEX,
        lesson27_fullscreen_vert_wgsl,
        lesson27_fullscreen_vert_wgsl_size,
        lesson27_fullscreen_vert_msl,
        lesson27_fullscreen_vert_msl_size,
        0, 0, 0, 0);
    if (!vertex_shader) {
        return false;
    }
    ssao_shader = ForgeGpuCreateShaderWithResourceLayout(
        demo->device,
        lesson27_ssao_frag_wgsl,
        lesson27_ssao_frag_wgsl_size,
        lesson27_ssao_frag_msl,
        lesson27_ssao_frag_msl_size,
        ForgeGpuShaderLayout_lesson27_ssao_frag());
    if (!ssao_shader) {
        SDL_ReleaseGPUShader(demo->device, vertex_shader);
        return false;
    }
    blur_shader = ForgeGpuCreateShader(
        demo->device,
        SDL_GPU_SHADERSTAGE_FRAGMENT,
        lesson27_blur_frag_wgsl,
        lesson27_blur_frag_wgsl_size,
        lesson27_blur_frag_msl,
        lesson27_blur_frag_msl_size,
        1, 0, 0, 1);
    if (!blur_shader) {
        SDL_ReleaseGPUShader(demo->device, ssao_shader);
        SDL_ReleaseGPUShader(demo->device, vertex_shader);
        return false;
    }
    composite_shader = ForgeGpuCreateShader(
        demo->device,
        SDL_GPU_SHADERSTAGE_FRAGMENT,
        lesson27_composite_frag_wgsl,
        lesson27_composite_frag_wgsl_size,
        lesson27_composite_frag_msl,
        lesson27_composite_frag_msl_size,
        2, 0, 0, 1);
    if (!composite_shader) {
        SDL_ReleaseGPUShader(demo->device, blur_shader);
        SDL_ReleaseGPUShader(demo->device, ssao_shader);
        SDL_ReleaseGPUShader(demo->device, vertex_shader);
        return false;
    }

    state->ssao_pipeline = ForgeGpuCreateFullscreenPostprocessPipeline(demo, vertex_shader, ssao_shader, LESSON27_SSAO_FORMAT, false);
    state->blur_pipeline = ForgeGpuCreateFullscreenPostprocessPipeline(demo, vertex_shader, blur_shader, LESSON27_SSAO_FORMAT, false);
    state->composite_pipeline = ForgeGpuCreateFullscreenPostprocessPipeline(demo, vertex_shader, composite_shader, demo->color_format, false);

    SDL_ReleaseGPUShader(demo->device, composite_shader);
    SDL_ReleaseGPUShader(demo->device, blur_shader);
    SDL_ReleaseGPUShader(demo->device, ssao_shader);
    SDL_ReleaseGPUShader(demo->device, vertex_shader);
    return state->ssao_pipeline && state->blur_pipeline && state->composite_pipeline;
}

static bool lesson27_create_pipelines(ForgeGpuDemo *demo)
{
    return lesson27_create_shadow_pipeline(demo) &&
           lesson27_create_scene_pipeline(demo) &&
           lesson27_create_grid_pipeline(demo) &&
           lesson27_create_fullscreen_pipelines(demo);
}

static bool lesson27_create_samplers(ForgeGpuDemo *demo)
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
        SDL_GPU_FILTER_NEAREST,
        SDL_GPU_FILTER_NEAREST,
        SDL_GPU_SAMPLERMIPMAPMODE_NEAREST,
        SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
        0.0f);
    lesson->samplers[3] = ForgeGpuCreateSamplerWithAddress(
        demo->device,
        SDL_GPU_FILTER_LINEAR,
        SDL_GPU_FILTER_LINEAR,
        SDL_GPU_SAMPLERMIPMAPMODE_NEAREST,
        SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        0.0f);
    return lesson->samplers[0] && lesson->samplers[1] && lesson->samplers[2] && lesson->samplers[3];
}

static void lesson27_init_box_placements(Lesson27State *state)
{
    const Vec3 positions[LESSON27_BOX_COUNT] = {
        { -3.5f, 0.5f,  2.0f },
        { -2.5f, 0.5f,  0.5f },
        {  3.0f, 0.5f, -2.0f },
        { -1.0f, 0.5f, -3.0f },
        { -3.5f, 1.5f,  2.0f }
    };
    const float rotations[LESSON27_BOX_COUNT] = {
        0.3f, 1.1f, 0.7f, 2.0f, 0.9f
    };

    for (int i = 0; i < LESSON27_BOX_COUNT; i += 1) {
        state->box_placements[i].position = positions[i];
        state->box_placements[i].y_rotation = rotations[i];
    }
}

static void lesson27_init_light(Lesson27State *state)
{
    state->light_vp = ForgeGpuComputeDirectionalLightViewProjection(
        { LESSON27_LIGHT_DIR_X, LESSON27_LIGHT_DIR_Y, LESSON27_LIGHT_DIR_Z },
        LESSON27_LIGHT_DISTANCE,
        LESSON27_SHADOW_ORTHO_SIZE,
        LESSON27_SHADOW_NEAR,
        LESSON27_SHADOW_FAR,
        LESSON27_PARALLEL_THRESHOLD);
}

static void lesson27_init_camera(ForgeGpuDemo *demo)
{
    LessonState *lesson = &demo->lesson;

    lesson->camera_position = {
        LESSON27_CAMERA_START_X,
        LESSON27_CAMERA_START_Y,
        LESSON27_CAMERA_START_Z
    };
    lesson->camera_yaw = LESSON27_CAMERA_START_YAW_DEG * FORGE_GPU_DEG2RAD;
    lesson->camera_pitch = LESSON27_CAMERA_START_PITCH_DEG * FORGE_GPU_DEG2RAD;
    lesson->mouse_sensitivity = LESSON27_MOUSE_SENSITIVITY;
    lesson->pitch_clamp = LESSON27_PITCH_CLAMP;
    lesson->move_speed = LESSON27_CAMERA_SPEED;
    lesson->last_ticks = SDL_GetTicks();
}

static void lesson27_draw_grid(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    Mat4 cam_vp,
    Mat4 view)
{
    Lesson27State *state = lesson27_state(demo);
    Lesson27GridVertUniforms vertex_uniforms;
    Lesson27GridFragUniforms fragment_uniforms;
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
    fragment_uniforms.grid_spacing = LESSON27_GRID_SPACING;
    fragment_uniforms.line_width = LESSON27_GRID_LINE_WIDTH;
    fragment_uniforms.fade_distance = LESSON27_GRID_FADE_DISTANCE;
    fragment_uniforms.ambient = LESSON27_MATERIAL_AMBIENT;
    fragment_uniforms.light_intensity = LESSON27_LIGHT_INTENSITY;
    fragment_uniforms.light_dir[0] = LESSON27_LIGHT_DIR_X;
    fragment_uniforms.light_dir[1] = LESSON27_LIGHT_DIR_Y;
    fragment_uniforms.light_dir[2] = LESSON27_LIGHT_DIR_Z;
    fragment_uniforms.light_color[0] = LESSON27_LIGHT_COLOR_R;
    fragment_uniforms.light_color[1] = LESSON27_LIGHT_COLOR_G;
    fragment_uniforms.light_color[2] = LESSON27_LIGHT_COLOR_B;
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

static bool lesson27_run_shadow_pass(ForgeGpuDemo *demo, SDL_GPUCommandBuffer *command_buffer)
{
    Lesson27State *state = lesson27_state(demo);
    SDL_GPURenderPass *render_pass;

    render_pass = ForgeGpuBeginDepthOnlyPass(command_buffer, state->shadow_depth, 1.0f);
    if (!render_pass) {
        return false;
    }

    SDL_BindGPUGraphicsPipeline(render_pass, state->shadow_pipeline);
    ForgeGpuDrawShadowedBoxScene(
        command_buffer,
        render_pass,
        &state->models[LESSON27_MODEL_TRUCK],
        &state->models[LESSON27_MODEL_BOX],
        state->box_placements,
        LESSON27_BOX_COUNT,
        state->light_vp);
    SDL_EndGPURenderPass(render_pass);
    return true;
}

static bool lesson27_run_geometry_pass(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    Mat4 cam_vp,
    Mat4 view)
{
    Lesson27State *state = lesson27_state(demo);
    ForgeGpuColorTargetAttachment color_targets[2];
    ForgeGpuDeferredSceneDrawInfo draw_info;
    SDL_GPURenderPass *render_pass;

    SDL_zeroa(color_targets);
    color_targets[0].texture = state->scene_color;
    color_targets[0].clear_color = { 0.008f, 0.008f, 0.026f, 1.0f };
    color_targets[1].texture = state->view_normals;
    color_targets[1].clear_color = { 0.0f, 0.0f, 0.0f, 0.0f };

    render_pass = ForgeGpuBeginColorDepthPass(command_buffer, color_targets, SDL_arraysize(color_targets), state->scene_depth, 1.0f);
    if (!render_pass) {
        return false;
    }

    SDL_BindGPUGraphicsPipeline(render_pass, state->grid_pipeline);
    lesson27_draw_grid(demo, command_buffer, render_pass, cam_vp, view);

    SDL_BindGPUGraphicsPipeline(render_pass, state->scene_pipeline);
    SDL_zero(draw_info);
    draw_info.cam_vp = cam_vp;
    draw_info.view = view;
    draw_info.light_vp = state->light_vp;
    draw_info.shadow_depth = state->shadow_depth;
    draw_info.material_sampler = demo->lesson.samplers[0];
    draw_info.shadow_sampler = demo->lesson.samplers[1];
    draw_info.lighting.light_dir = { LESSON27_LIGHT_DIR_X, LESSON27_LIGHT_DIR_Y, LESSON27_LIGHT_DIR_Z };
    draw_info.lighting.light_color = { LESSON27_LIGHT_COLOR_R, LESSON27_LIGHT_COLOR_G, LESSON27_LIGHT_COLOR_B };
    draw_info.lighting.light_intensity = LESSON27_LIGHT_INTENSITY;
    draw_info.lighting.ambient = LESSON27_MATERIAL_AMBIENT;
    draw_info.lighting.shininess = LESSON27_MATERIAL_SHININESS;
    draw_info.lighting.specular_strength = LESSON27_MATERIAL_SPECULAR_STRENGTH;
    ForgeGpuDrawDeferredBoxScene(
        demo,
        command_buffer,
        render_pass,
        &state->models[LESSON27_MODEL_TRUCK],
        &state->models[LESSON27_MODEL_BOX],
        state->box_placements,
        LESSON27_BOX_COUNT,
        &draw_info);

    SDL_EndGPURenderPass(render_pass);
    return true;
}

static bool lesson27_run_ssao_pass(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    Uint32 width,
    Uint32 height,
    Mat4 projection,
    Mat4 inv_projection)
{
    Lesson27State *state = lesson27_state(demo);
    SDL_GPUTextureSamplerBinding bindings[3];
    Lesson27SSAOUniforms uniforms;

    SDL_zeroa(bindings);
    bindings[0].texture = state->view_normals;
    bindings[0].sampler = demo->lesson.samplers[1];
    bindings[1].texture = state->scene_depth;
    bindings[1].sampler = demo->lesson.samplers[1];
    bindings[2].texture = state->noise_texture;
    bindings[2].sampler = demo->lesson.samplers[2];

    SDL_zero(uniforms);
    SDL_memcpy(uniforms.samples, state->ssao_kernel, sizeof(state->ssao_kernel));
    uniforms.projection = projection;
    uniforms.inv_projection = inv_projection;
    uniforms.noise_scale[0] = (float)width / (float)LESSON27_NOISE_TEX_SIZE;
    uniforms.noise_scale[1] = (float)height / (float)LESSON27_NOISE_TEX_SIZE;
    uniforms.radius = LESSON27_SSAO_RADIUS;
    uniforms.bias = LESSON27_SSAO_BIAS;
    uniforms.use_ign_jitter = state->use_ign_jitter ? 1 : 0;

    return ForgeGpuRunFullscreenPostprocessPass(
        command_buffer,
        state->ssao_raw,
        SDL_GPU_LOADOP_CLEAR,
        { 1.0f, 1.0f, 1.0f, 1.0f },
        state->ssao_pipeline,
        bindings,
        SDL_arraysize(bindings),
        &uniforms,
        sizeof(uniforms),
        LESSON27_FULLSCREEN_VERTICES);
}

static bool lesson27_run_blur_pass(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    Uint32 width,
    Uint32 height)
{
    Lesson27State *state = lesson27_state(demo);
    SDL_GPUTextureSamplerBinding binding;
    Lesson27BlurUniforms uniforms;

    SDL_zero(binding);
    binding.texture = state->ssao_raw;
    binding.sampler = demo->lesson.samplers[1];
    SDL_zero(uniforms);
    uniforms.texel_size[0] = 1.0f / (float)width;
    uniforms.texel_size[1] = 1.0f / (float)height;

    return ForgeGpuRunFullscreenPostprocessPass(
        command_buffer,
        state->ssao_blurred,
        SDL_GPU_LOADOP_CLEAR,
        { 0.0f, 0.0f, 0.0f, 1.0f },
        state->blur_pipeline,
        &binding,
        1,
        &uniforms,
        sizeof(uniforms),
        LESSON27_FULLSCREEN_VERTICES);
}

static bool lesson27_run_composite_pass(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPUTexture *swapchain_texture)
{
    Lesson27State *state = lesson27_state(demo);
    SDL_GPUTextureSamplerBinding bindings[2];
    Lesson27CompositeUniforms uniforms;

    SDL_zeroa(bindings);
    bindings[0].texture = state->scene_color;
    bindings[0].sampler = demo->lesson.samplers[3];
    bindings[1].texture = state->ssao_blurred;
    bindings[1].sampler = demo->lesson.samplers[3];

    SDL_zero(uniforms);
    uniforms.display_mode = state->display_mode;
    uniforms.use_dither = state->use_dither ? 1 : 0;

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
        LESSON27_FULLSCREEN_VERTICES);
}

bool ForgeGpuCreateLesson27(ForgeGpuDemo *demo)
{
    Lesson27State *state;

    if (!SDL_GPUTextureSupportsFormat(
            demo->device,
            LESSON27_GBUFFER_COLOR_FORMAT,
            SDL_GPU_TEXTURETYPE_2D,
            SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER)) {
        SDL_SetError("lesson 27 requires sampled R8G8B8A8_UNORM color targets");
        return false;
    }
    if (!SDL_GPUTextureSupportsFormat(
            demo->device,
            LESSON27_GBUFFER_NORMAL_FORMAT,
            SDL_GPU_TEXTURETYPE_2D,
            SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER)) {
        SDL_SetError("lesson 27 requires sampled R16G16B16A16_FLOAT color targets");
        return false;
    }
    if (!SDL_GPUTextureSupportsFormat(
            demo->device,
            LESSON27_GBUFFER_DEPTH_FORMAT,
            SDL_GPU_TEXTURETYPE_2D,
            SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER)) {
        SDL_SetError("lesson 27 requires sampled D32_FLOAT depth targets");
        return false;
    }
    if (!SDL_GPUTextureSupportsFormat(
            demo->device,
            LESSON27_SSAO_FORMAT,
            SDL_GPU_TEXTURETYPE_2D,
            SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER)) {
        SDL_SetError("lesson 27 requires sampled R8_UNORM color targets");
        return false;
    }
    if (!SDL_GPUTextureSupportsFormat(
            demo->device,
            LESSON27_NOISE_FORMAT,
            SDL_GPU_TEXTURETYPE_2D,
            SDL_GPU_TEXTUREUSAGE_SAMPLER)) {
        SDL_SetError("lesson 27 requires sampled R32G32B32A32_FLOAT textures");
        return false;
    }

    state = (Lesson27State *)SDL_calloc(1, sizeof(*state));
    if (!state) {
        SDL_OutOfMemory();
        return false;
    }
    demo->lesson.private_state = state;
    /* Integrated demo default: show the scene with AO first; 1 still exposes the source AO-only view. */
    state->display_mode = LESSON27_MODE_WITH_AO;
    state->use_ign_jitter = true;
    state->use_dither = true;

    demo->lesson.white_texture = ForgeGpuCreateWhiteTexture(demo->device);
    if (!demo->lesson.white_texture ||
        !lesson27_create_samplers(demo) ||
        !ForgeGpuCreateGridBuffers(demo) ||
        !ForgeGpuLoadSceneModel(demo, &state->models[LESSON27_MODEL_TRUCK], "models/CesiumMilkTruck/CesiumMilkTruck.gltf") ||
        !ForgeGpuLoadSceneModel(demo, &state->models[LESSON27_MODEL_BOX], "models/BoxTextured/BoxTextured.gltf") ||
        !lesson27_create_pipelines(demo)) {
        return false;
    }
    state->shadow_depth = ForgeGpuCreateSampledDepthTexture(demo, LESSON27_SHADOW_MAP_SIZE, LESSON27_SHADOW_MAP_SIZE, LESSON27_SHADOW_FORMAT);
    if (!state->shadow_depth) {
        return false;
    }

    state->noise_texture = lesson27_create_noise_texture(demo);
    if (!state->noise_texture) {
        return false;
    }

    lesson27_generate_ssao_kernel(state->ssao_kernel);
    lesson27_init_box_placements(state);
    lesson27_init_light(state);
    lesson27_init_camera(demo);
    return true;
}

bool ForgeGpuRenderLesson27(
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

    if (!lesson27_ensure_targets(demo, width, height)) {
        return false;
    }

    ForgeGpuUpdateCameraFromInput(demo);
    ForgeGpuCameraViewProjection(demo, width, height, 100.0f, &view, &projection);
    cam_vp = mat4_multiply(projection, view);
    inv_projection = mat4_inverse(projection);

    return lesson27_run_shadow_pass(demo, command_buffer) &&
           lesson27_run_geometry_pass(demo, command_buffer, cam_vp, view) &&
           lesson27_run_ssao_pass(demo, command_buffer, width, height, projection, inv_projection) &&
           lesson27_run_blur_pass(demo, command_buffer, width, height) &&
           lesson27_run_composite_pass(demo, command_buffer, swapchain_texture);
}

void ForgeGpuDebugLesson27(ForgeGpuDemo *demo)
{
    Lesson27State *state = lesson27_state(demo);

    if (!state) {
        return;
    }
    ImGui::Text("Mode: %s", lesson27_mode_name(state->display_mode));
    ImGui::Text("IGN jitter: %s", state->use_ign_jitter ? "on" : "off");
    ImGui::Text("Dither: %s", state->use_dither ? "on" : "off");
    ImGui::Text("Targets: %ux%u", state->target_width, state->target_height);
    ImGui::Text("G-buffer: R8G8B8A8 color, R16G16B16A16 normals, D32 depth");
    ImGui::Text("SSAO: R8 raw + blurred, 64 samples");
}

void ForgeGpuControlsLesson27(ForgeGpuDemo *demo)
{
    (void)demo;
    ImGui::Text("1 AO only, 2 scene with AO, 3 scene without AO");
    ImGui::Text("N toggles SSAO jitter and composite dithering");
}

bool ForgeGpuHandleLesson27Event(ForgeGpuDemo *demo, const SDL_Event *event)
{
    Lesson27State *state = lesson27_state(demo);

    if (!state || event->type != SDL_EVENT_KEY_DOWN || event->key.repeat) {
        return false;
    }
    switch (event->key.key) {
    case SDLK_1:
        state->display_mode = LESSON27_MODE_AO_ONLY;
        return true;
    case SDLK_2:
        state->display_mode = LESSON27_MODE_WITH_AO;
        return true;
    case SDLK_3:
        state->display_mode = LESSON27_MODE_NO_AO;
        return true;
    case SDLK_N:
        state->use_dither = !state->use_dither;
        state->use_ign_jitter = !state->use_ign_jitter;
        return true;
    default:
        break;
    }
    return false;
}

void ForgeGpuExportLesson27Metrics(ForgeGpuDemo *demo)
{
    Lesson27State *state = lesson27_state(demo);

    if (!state) {
        return;
    }
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuSsao", 1.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuSsaoDisplayMode", (double)state->display_mode);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuSsaoIgnJitter", state->use_ign_jitter ? 1.0 : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuSsaoDither", state->use_dither ? 1.0 : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuSsaoTargetsReady", state->scene_color && state->view_normals && state->scene_depth && state->ssao_raw && state->ssao_blurred ? 1.0 : 0.0);
}

void ForgeGpuDestroyLesson27(ForgeGpuDemo *demo)
{
    Lesson27State *state = lesson27_state(demo);

    if (!state) {
        return;
    }

    for (int i = 0; i < LESSON27_MODEL_COUNT; i += 1) {
        ForgeGpuFreeSceneData(demo, &state->models[i]);
    }
    if (state->noise_texture) {
        SDL_ReleaseGPUTexture(demo->device, state->noise_texture);
    }
    if (state->shadow_depth) {
        SDL_ReleaseGPUTexture(demo->device, state->shadow_depth);
    }
    if (state->ssao_blurred) {
        SDL_ReleaseGPUTexture(demo->device, state->ssao_blurred);
    }
    if (state->ssao_raw) {
        SDL_ReleaseGPUTexture(demo->device, state->ssao_raw);
    }
    if (state->scene_depth) {
        SDL_ReleaseGPUTexture(demo->device, state->scene_depth);
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
    if (state->blur_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, state->blur_pipeline);
    }
    if (state->ssao_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, state->ssao_pipeline);
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
