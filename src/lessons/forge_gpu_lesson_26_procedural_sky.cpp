#include "forge_gpu_lessons.h"

#include "forge_gpu_browser_status.h"
#include "forge_gpu_gpu_helpers.h"
#include "forge_gpu_lesson_common.h"
#include "forge_gpu_math.h"
#include "forge_gpu_shader_layouts.h"
#include "shaders/generated/forge_gpu_lesson_26_shaders.h"
#include "imgui.h"

#define LESSON26_FULLSCREEN_VERTICES 6
#define LESSON26_CAMERA_SPEED 0.2f
#define LESSON26_FOV_DEGREES 60.0f
#define LESSON26_CAMERA_START_X 0.0f
#define LESSON26_CAMERA_START_Y 6360.001f
#define LESSON26_CAMERA_START_Z 0.0f
#define LESSON26_CAMERA_START_YAW 1.63f
#define LESSON26_MOUSE_SENSITIVITY 0.003f
#define LESSON26_PITCH_CLAMP 1.5f
#define LESSON26_SUN_ELEVATION_DEFAULT 0.5f
#define LESSON26_SUN_AZIMUTH_DEFAULT 0.0f
#define LESSON26_SUN_ELEVATION_SPEED 0.5f
#define LESSON26_SUN_AZIMUTH_SPEED 0.5f
#define LESSON26_SUN_AUTO_SPEED 0.01f
#define LESSON26_SUN_ORBIT_TILT 1.2f
#define LESSON26_SUN_INTENSITY 20.0f
#define LESSON26_SUN_ORBIT_START 3.08f
#define LESSON26_CAPTURE_SUN_ELEVATION 0.04f
#define LESSON26_NUM_VIEW_STEPS 32
#define LESSON26_TRANSMITTANCE_LUT_WIDTH 256u
#define LESSON26_TRANSMITTANCE_LUT_HEIGHT 64u
#define LESSON26_MULTISCATTER_LUT_WIDTH 32u
#define LESSON26_MULTISCATTER_LUT_HEIGHT 32u
#define LESSON26_HDR_FORMAT SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT
#define LESSON26_DEFAULT_EXPOSURE 1.0f
#define LESSON26_EXPOSURE_STEP 0.1f
#define LESSON26_MIN_EXPOSURE 0.1f
#define LESSON26_MAX_EXPOSURE 20.0f
#define LESSON26_DEFAULT_BLOOM_INTENSITY 0.04f
#define LESSON26_DEFAULT_BLOOM_THRESHOLD 1.0f
#define LESSON26_TONEMAP_CLAMP 0u
#define LESSON26_TONEMAP_REINHARD 1u
#define LESSON26_TONEMAP_ACES 2u

struct Lesson26SkyVertUniforms
{
    Mat4 ray_matrix;
};

struct Lesson26SkyFragUniforms
{
    float cam_pos_km[3];
    float sun_intensity;
    float sun_dir[3];
    Sint32 num_steps;
    float resolution[2];
    float pad[2];
};

struct Lesson26TonemapUniforms
{
    float exposure;
    Uint32 tonemap_mode;
    float bloom_intensity;
    float pad;
};

struct Lesson26State
{
    SDL_GPUGraphicsPipeline *sky_pipeline;
    SDL_GPUGraphicsPipeline *downsample_pipeline;
    SDL_GPUGraphicsPipeline *upsample_pipeline;
    SDL_GPUGraphicsPipeline *tonemap_pipeline;
    SDL_GPUComputePipeline *transmittance_compute_pipeline;
    SDL_GPUComputePipeline *multiscatter_compute_pipeline;
    SDL_GPUTexture *transmittance_lut;
    SDL_GPUTexture *multiscatter_lut;
    SDL_GPUTexture *hdr_target;
    SDL_GPUSampler *lut_sampler;
    SDL_GPUSampler *hdr_sampler;
    SDL_GPUSampler *bloom_sampler;
    ForgeGpuBloomChain bloom;
    Uint32 hdr_width;
    Uint32 hdr_height;
    float sun_elevation;
    float sun_azimuth;
    float sun_orbit_angle;
    float exposure;
    float bloom_intensity;
    float bloom_threshold;
    Uint32 tonemap_mode;
    Uint64 last_ticks;
    bool sun_auto;
    bool bloom_enabled;
};

static_assert(sizeof(Lesson26SkyVertUniforms) == 64, "lesson 26 sky vertex uniform size must match HLSL layout");
static_assert(sizeof(Lesson26SkyFragUniforms) == 48, "lesson 26 sky fragment uniform size must match HLSL layout");
static_assert(sizeof(Lesson26TonemapUniforms) == 16, "lesson 26 tonemap uniform size must match HLSL layout");

static Lesson26State *lesson26_state(ForgeGpuDemo *demo)
{
    return (Lesson26State *)demo->lesson.private_state;
}

static float lesson26_clamp(float value, float min_value, float max_value)
{
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static const char *lesson26_tonemap_name(Uint32 mode)
{
    if (mode == LESSON26_TONEMAP_REINHARD) {
        return "Reinhard";
    }
    if (mode == LESSON26_TONEMAP_ACES) {
        return "ACES";
    }
    return "Clamp";
}

static Vec3 lesson26_sun_direction(float elevation, float azimuth)
{
    const float cos_elevation = SDL_cosf(elevation);
    return {
        cos_elevation * SDL_cosf(azimuth),
        SDL_sinf(elevation),
        cos_elevation * SDL_sinf(azimuth)
    };
}

static float lesson26_delta_seconds(ForgeGpuDemo *demo, Lesson26State *state)
{
    const Uint64 now = SDL_GetTicks();
    float delta = state->last_ticks != 0 ? (float)(now - state->last_ticks) / 1000.0f : 0.0f;

    state->last_ticks = now;
    if (delta > FORGE_GPU_MAX_DELTA_TIME) {
        delta = FORGE_GPU_MAX_DELTA_TIME;
    }
    return demo->validation_mode ? 0.0f : delta;
}

static bool lesson26_create_lut_texture(ForgeGpuDemo *demo, Uint32 width, Uint32 height, SDL_GPUTexture **texture)
{
    SDL_GPUTextureCreateInfo texture_info;

    SDL_zero(texture_info);
    texture_info.type = SDL_GPU_TEXTURETYPE_2D;
    texture_info.format = LESSON26_HDR_FORMAT;
    texture_info.usage = SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    texture_info.width = width;
    texture_info.height = height;
    texture_info.layer_count_or_depth = 1;
    texture_info.num_levels = 1;
    texture_info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    *texture = SDL_CreateGPUTexture(demo->device, &texture_info);
    return *texture != nullptr;
}

static bool lesson26_ensure_hdr_target(ForgeGpuDemo *demo, Uint32 width, Uint32 height)
{
    Lesson26State *state = lesson26_state(demo);

    if (!state) {
        SDL_SetError("lesson 26 internal state is missing");
        return false;
    }
    return ForgeGpuEnsureSampledColorTarget(
        demo,
        &state->hdr_target,
        &state->hdr_width,
        &state->hdr_height,
        width,
        height,
        LESSON26_HDR_FORMAT);
}

static bool lesson26_create_samplers(ForgeGpuDemo *demo)
{
    Lesson26State *state = lesson26_state(demo);

    state->lut_sampler = ForgeGpuCreateSamplerWithAddress(
        demo->device,
        SDL_GPU_FILTER_LINEAR,
        SDL_GPU_FILTER_LINEAR,
        SDL_GPU_SAMPLERMIPMAPMODE_NEAREST,
        SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        0.0f);
    state->hdr_sampler = ForgeGpuCreateSamplerWithAddress(
        demo->device,
        SDL_GPU_FILTER_LINEAR,
        SDL_GPU_FILTER_LINEAR,
        SDL_GPU_SAMPLERMIPMAPMODE_NEAREST,
        SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        0.0f);
    state->bloom_sampler = ForgeGpuCreateSamplerWithAddress(
        demo->device,
        SDL_GPU_FILTER_LINEAR,
        SDL_GPU_FILTER_LINEAR,
        SDL_GPU_SAMPLERMIPMAPMODE_NEAREST,
        SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        0.0f);
    return state->lut_sampler && state->hdr_sampler && state->bloom_sampler;
}

static bool lesson26_create_compute_pipelines(ForgeGpuDemo *demo)
{
    Lesson26State *state = lesson26_state(demo);

    state->transmittance_compute_pipeline = ForgeGpuCreateComputePipelineWithResourceLayout(
        demo->device,
        lesson26_transmittance_lut_comp_wgsl,
        lesson26_transmittance_lut_comp_wgsl_size,
        lesson26_transmittance_lut_comp_msl,
        lesson26_transmittance_lut_comp_msl_size,
        ForgeGpuComputePipelineLayout_lesson26_transmittance_lut_comp(),
        FORGE_GPU_COMPUTE_WORKGROUP_SIZE,
        FORGE_GPU_COMPUTE_WORKGROUP_SIZE,
        1);
    state->multiscatter_compute_pipeline = ForgeGpuCreateComputePipelineWithResourceLayout(
        demo->device,
        lesson26_multiscatter_lut_comp_wgsl,
        lesson26_multiscatter_lut_comp_wgsl_size,
        lesson26_multiscatter_lut_comp_msl,
        lesson26_multiscatter_lut_comp_msl_size,
        ForgeGpuComputePipelineLayout_lesson26_multiscatter_lut_comp(),
        FORGE_GPU_COMPUTE_WORKGROUP_SIZE,
        FORGE_GPU_COMPUTE_WORKGROUP_SIZE,
        1);
    return state->transmittance_compute_pipeline && state->multiscatter_compute_pipeline;
}

static bool lesson26_generate_luts(ForgeGpuDemo *demo)
{
    Lesson26State *state = lesson26_state(demo);
    SDL_GPUCommandBuffer *command_buffer;
    SDL_GPUStorageTextureReadWriteBinding storage_binding;
    SDL_GPUComputePass *compute_pass;
    SDL_GPUTextureSamplerBinding sampler_binding;

    command_buffer = SDL_AcquireGPUCommandBuffer(demo->device);
    if (!command_buffer) {
        return false;
    }

    SDL_zero(storage_binding);
    storage_binding.texture = state->transmittance_lut;
    storage_binding.mip_level = 0;
    storage_binding.layer = 0;
    storage_binding.cycle = false;
    compute_pass = SDL_BeginGPUComputePass(command_buffer, &storage_binding, 1, nullptr, 0);
    if (!compute_pass) {
        SDL_CancelGPUCommandBuffer(command_buffer);
        return false;
    }
    SDL_BindGPUComputePipeline(compute_pass, state->transmittance_compute_pipeline);
    SDL_DispatchGPUCompute(
        compute_pass,
        (LESSON26_TRANSMITTANCE_LUT_WIDTH + FORGE_GPU_COMPUTE_WORKGROUP_SIZE - 1u) / FORGE_GPU_COMPUTE_WORKGROUP_SIZE,
        (LESSON26_TRANSMITTANCE_LUT_HEIGHT + FORGE_GPU_COMPUTE_WORKGROUP_SIZE - 1u) / FORGE_GPU_COMPUTE_WORKGROUP_SIZE,
        1);
    SDL_EndGPUComputePass(compute_pass);

    SDL_zero(storage_binding);
    storage_binding.texture = state->multiscatter_lut;
    storage_binding.mip_level = 0;
    storage_binding.layer = 0;
    storage_binding.cycle = false;
    compute_pass = SDL_BeginGPUComputePass(command_buffer, &storage_binding, 1, nullptr, 0);
    if (!compute_pass) {
        SDL_CancelGPUCommandBuffer(command_buffer);
        return false;
    }
    SDL_zero(sampler_binding);
    sampler_binding.texture = state->transmittance_lut;
    sampler_binding.sampler = state->lut_sampler;
    SDL_BindGPUComputePipeline(compute_pass, state->multiscatter_compute_pipeline);
    SDL_BindGPUComputeSamplers(compute_pass, 0, &sampler_binding, 1);
    SDL_DispatchGPUCompute(
        compute_pass,
        (LESSON26_MULTISCATTER_LUT_WIDTH + FORGE_GPU_COMPUTE_WORKGROUP_SIZE - 1u) / FORGE_GPU_COMPUTE_WORKGROUP_SIZE,
        (LESSON26_MULTISCATTER_LUT_HEIGHT + FORGE_GPU_COMPUTE_WORKGROUP_SIZE - 1u) / FORGE_GPU_COMPUTE_WORKGROUP_SIZE,
        1);
    SDL_EndGPUComputePass(compute_pass);

    if (!SDL_SubmitGPUCommandBuffer(command_buffer)) {
        return false;
    }
    return SDL_WaitForGPUIdle(demo->device);
}

static bool lesson26_create_graphics_pipelines(ForgeGpuDemo *demo)
{
    Lesson26State *state = lesson26_state(demo);
    SDL_GPUShader *sky_vertex_shader = nullptr;
    SDL_GPUShader *sky_fragment_shader = nullptr;
    SDL_GPUShader *fullscreen_vertex_shader = nullptr;
    SDL_GPUShader *downsample_fragment_shader = nullptr;
    SDL_GPUShader *upsample_fragment_shader = nullptr;
    SDL_GPUShader *tonemap_fragment_shader = nullptr;
    bool ok = false;

    sky_vertex_shader = ForgeGpuCreateShader(
        demo->device,
        SDL_GPU_SHADERSTAGE_VERTEX,
        lesson26_sky_vert_wgsl,
        lesson26_sky_vert_wgsl_size,
        lesson26_sky_vert_msl,
        lesson26_sky_vert_msl_size,
        0,
        0,
        0,
        1);
    sky_fragment_shader = ForgeGpuCreateShader(
        demo->device,
        SDL_GPU_SHADERSTAGE_FRAGMENT,
        lesson26_sky_frag_wgsl,
        lesson26_sky_frag_wgsl_size,
        lesson26_sky_frag_msl,
        lesson26_sky_frag_msl_size,
        2,
        0,
        0,
        1);
    fullscreen_vertex_shader = ForgeGpuCreateShader(
        demo->device,
        SDL_GPU_SHADERSTAGE_VERTEX,
        lesson26_fullscreen_vert_wgsl,
        lesson26_fullscreen_vert_wgsl_size,
        lesson26_fullscreen_vert_msl,
        lesson26_fullscreen_vert_msl_size,
        0,
        0,
        0,
        0);
    downsample_fragment_shader = ForgeGpuCreateShader(
        demo->device,
        SDL_GPU_SHADERSTAGE_FRAGMENT,
        lesson26_bloom_downsample_frag_wgsl,
        lesson26_bloom_downsample_frag_wgsl_size,
        lesson26_bloom_downsample_frag_msl,
        lesson26_bloom_downsample_frag_msl_size,
        1,
        0,
        0,
        1);
    upsample_fragment_shader = ForgeGpuCreateShader(
        demo->device,
        SDL_GPU_SHADERSTAGE_FRAGMENT,
        lesson26_bloom_upsample_frag_wgsl,
        lesson26_bloom_upsample_frag_wgsl_size,
        lesson26_bloom_upsample_frag_msl,
        lesson26_bloom_upsample_frag_msl_size,
        1,
        0,
        0,
        1);
    tonemap_fragment_shader = ForgeGpuCreateShader(
        demo->device,
        SDL_GPU_SHADERSTAGE_FRAGMENT,
        lesson26_tonemap_frag_wgsl,
        lesson26_tonemap_frag_wgsl_size,
        lesson26_tonemap_frag_msl,
        lesson26_tonemap_frag_msl_size,
        2,
        0,
        0,
        1);

    if (sky_vertex_shader && sky_fragment_shader && fullscreen_vertex_shader &&
        downsample_fragment_shader && upsample_fragment_shader && tonemap_fragment_shader) {
        state->sky_pipeline = ForgeGpuCreateFullscreenPostprocessPipeline(
            demo,
            sky_vertex_shader,
            sky_fragment_shader,
            LESSON26_HDR_FORMAT,
            false);
        state->downsample_pipeline = ForgeGpuCreateFullscreenPostprocessPipeline(
            demo,
            fullscreen_vertex_shader,
            downsample_fragment_shader,
            LESSON26_HDR_FORMAT,
            false);
        state->upsample_pipeline = ForgeGpuCreateFullscreenPostprocessPipeline(
            demo,
            fullscreen_vertex_shader,
            upsample_fragment_shader,
            LESSON26_HDR_FORMAT,
            true);
        state->tonemap_pipeline = ForgeGpuCreateFullscreenPostprocessPipeline(
            demo,
            fullscreen_vertex_shader,
            tonemap_fragment_shader,
            demo->color_format,
            false);
        ok = state->sky_pipeline && state->downsample_pipeline && state->upsample_pipeline && state->tonemap_pipeline;
    }

    if (sky_vertex_shader) {
        SDL_ReleaseGPUShader(demo->device, sky_vertex_shader);
    }
    if (sky_fragment_shader) {
        SDL_ReleaseGPUShader(demo->device, sky_fragment_shader);
    }
    if (fullscreen_vertex_shader) {
        SDL_ReleaseGPUShader(demo->device, fullscreen_vertex_shader);
    }
    if (downsample_fragment_shader) {
        SDL_ReleaseGPUShader(demo->device, downsample_fragment_shader);
    }
    if (upsample_fragment_shader) {
        SDL_ReleaseGPUShader(demo->device, upsample_fragment_shader);
    }
    if (tonemap_fragment_shader) {
        SDL_ReleaseGPUShader(demo->device, tonemap_fragment_shader);
    }
    return ok;
}

static void lesson26_update_camera_and_sun(ForgeGpuDemo *demo, Lesson26State *state)
{
    LessonState *lesson = &demo->lesson;
    const bool *keys;
    const float dt = lesson26_delta_seconds(demo, state);
    Quat orientation;
    Vec3 forward;
    Vec3 right;
    float speed;

    if (dt <= 0.0f) {
        return;
    }

    keys = SDL_GetKeyboardState(nullptr);
    orientation = quat_from_euler(lesson->camera_yaw, lesson->camera_pitch, 0.0f);
    forward = quat_forward(orientation);
    right = quat_right(orientation);
    /* Deliberate combined-demo UX divergence: the source lesson supports
     * Space/C vertical fly and Shift speed boost, but the SDL lesson shell
     * keeps camera movement to WASD so camera and navigation controls stay
     * consistent across all 52 lessons. */
    speed = LESSON26_CAMERA_SPEED * dt;

    if (keys[SDL_SCANCODE_W]) {
        lesson->camera_position = vec3_add(lesson->camera_position, vec3_scale(forward, speed));
    }
    if (keys[SDL_SCANCODE_S]) {
        lesson->camera_position = vec3_add(lesson->camera_position, vec3_scale(forward, -speed));
    }
    if (keys[SDL_SCANCODE_D]) {
        lesson->camera_position = vec3_add(lesson->camera_position, vec3_scale(right, speed));
    }
    if (keys[SDL_SCANCODE_A]) {
        lesson->camera_position = vec3_add(lesson->camera_position, vec3_scale(right, -speed));
    }

    if (keys[SDL_SCANCODE_UP]) {
        state->sun_elevation += LESSON26_SUN_ELEVATION_SPEED * dt;
    }
    if (keys[SDL_SCANCODE_DOWN]) {
        state->sun_elevation -= LESSON26_SUN_ELEVATION_SPEED * dt;
    }
    if (keys[SDL_SCANCODE_RIGHT]) {
        state->sun_azimuth += LESSON26_SUN_AZIMUTH_SPEED * dt;
    }
    if (keys[SDL_SCANCODE_LEFT]) {
        state->sun_azimuth -= LESSON26_SUN_AZIMUTH_SPEED * dt;
    }
    state->sun_elevation = lesson26_clamp(state->sun_elevation, -FORGE_GPU_PI * 0.5f, FORGE_GPU_PI * 0.5f);

    if (state->sun_auto) {
        state->sun_orbit_angle += LESSON26_SUN_AUTO_SPEED * dt;
        state->sun_elevation = SDL_sinf(state->sun_orbit_angle) * LESSON26_SUN_ORBIT_TILT;
        state->sun_azimuth = state->sun_orbit_angle;
    }
}

static Mat4 lesson26_build_ray_matrix(ForgeGpuDemo *demo, Uint32 width, Uint32 height)
{
    const float aspect = height > 0 ? (float)width / (float)height : 1.0f;
    const Quat orientation = quat_from_euler(demo->lesson.camera_yaw, demo->lesson.camera_pitch, 0.0f);
    const Vec3 camera_right = quat_right(orientation);
    const Vec3 camera_up = quat_up(orientation);
    const Vec3 camera_forward = quat_forward(orientation);
    const float half_fov_tan = SDL_tanf(LESSON26_FOV_DEGREES * FORGE_GPU_DEG2RAD * 0.5f);
    const float sx = aspect * half_fov_tan;
    const float sy = half_fov_tan;
    Mat4 ray_matrix;

    SDL_zero(ray_matrix);
    ray_matrix.m[0] = sx * camera_right.x;
    ray_matrix.m[1] = sx * camera_right.y;
    ray_matrix.m[2] = sx * camera_right.z;
    ray_matrix.m[4] = sy * camera_up.x;
    ray_matrix.m[5] = sy * camera_up.y;
    ray_matrix.m[6] = sy * camera_up.z;
    ray_matrix.m[12] = camera_forward.x;
    ray_matrix.m[13] = camera_forward.y;
    ray_matrix.m[14] = camera_forward.z;
    ray_matrix.m[15] = 1.0f;
    return ray_matrix;
}

static bool lesson26_render_sky(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    Uint32 width,
    Uint32 height)
{
    Lesson26State *state = lesson26_state(demo);
    SDL_GPUColorTargetInfo color_target;
    SDL_GPURenderPass *render_pass;
    SDL_GPUTextureSamplerBinding lut_bindings[2];
    Lesson26SkyVertUniforms vertex_uniforms;
    Lesson26SkyFragUniforms fragment_uniforms;
    Vec3 sun_direction;

    SDL_zero(color_target);
    color_target.texture = state->hdr_target;
    color_target.load_op = SDL_GPU_LOADOP_DONT_CARE;
    color_target.store_op = SDL_GPU_STOREOP_STORE;

    render_pass = SDL_BeginGPURenderPass(command_buffer, &color_target, 1, nullptr);
    if (!render_pass) {
        return false;
    }

    SDL_BindGPUGraphicsPipeline(render_pass, state->sky_pipeline);

    SDL_zeroa(lut_bindings);
    lut_bindings[0].texture = state->transmittance_lut;
    lut_bindings[0].sampler = state->lut_sampler;
    lut_bindings[1].texture = state->multiscatter_lut;
    lut_bindings[1].sampler = state->lut_sampler;
    SDL_BindGPUFragmentSamplers(render_pass, 0, lut_bindings, SDL_arraysize(lut_bindings));

    vertex_uniforms.ray_matrix = lesson26_build_ray_matrix(demo, width, height);
    SDL_PushGPUVertexUniformData(command_buffer, 0, &vertex_uniforms, sizeof(vertex_uniforms));

    sun_direction = lesson26_sun_direction(state->sun_elevation, state->sun_azimuth);
    SDL_zero(fragment_uniforms);
    fragment_uniforms.cam_pos_km[0] = demo->lesson.camera_position.x;
    fragment_uniforms.cam_pos_km[1] = demo->lesson.camera_position.y;
    fragment_uniforms.cam_pos_km[2] = demo->lesson.camera_position.z;
    fragment_uniforms.sun_intensity = LESSON26_SUN_INTENSITY;
    fragment_uniforms.sun_dir[0] = sun_direction.x;
    fragment_uniforms.sun_dir[1] = sun_direction.y;
    fragment_uniforms.sun_dir[2] = sun_direction.z;
    fragment_uniforms.num_steps = LESSON26_NUM_VIEW_STEPS;
    fragment_uniforms.resolution[0] = (float)width;
    fragment_uniforms.resolution[1] = (float)height;
    SDL_PushGPUFragmentUniformData(command_buffer, 0, &fragment_uniforms, sizeof(fragment_uniforms));

    SDL_DrawGPUPrimitives(render_pass, LESSON26_FULLSCREEN_VERTICES, 1, 0, 0);
    SDL_EndGPURenderPass(render_pass);
    return true;
}

bool ForgeGpuCreateLesson26(ForgeGpuDemo *demo)
{
    Lesson26State *state;

    if (!SDL_GPUTextureSupportsFormat(
            demo->device,
            LESSON26_HDR_FORMAT,
            SDL_GPU_TEXTURETYPE_2D,
            SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER)) {
        SDL_SetError("lesson 26 requires sampled R16G16B16A16_FLOAT color targets");
        return false;
    }
    if (!SDL_GPUTextureSupportsFormat(
            demo->device,
            LESSON26_HDR_FORMAT,
            SDL_GPU_TEXTURETYPE_2D,
            SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE | SDL_GPU_TEXTUREUSAGE_SAMPLER)) {
        SDL_SetError("lesson 26 requires sampled R16G16B16A16_FLOAT storage texture writes");
        return false;
    }

    state = (Lesson26State *)SDL_calloc(1, sizeof(*state));
    if (!state) {
        SDL_OutOfMemory();
        return false;
    }
    demo->lesson.private_state = state;

    if (!lesson26_create_lut_texture(demo, LESSON26_TRANSMITTANCE_LUT_WIDTH, LESSON26_TRANSMITTANCE_LUT_HEIGHT, &state->transmittance_lut) ||
        !lesson26_create_lut_texture(demo, LESSON26_MULTISCATTER_LUT_WIDTH, LESSON26_MULTISCATTER_LUT_HEIGHT, &state->multiscatter_lut) ||
        !lesson26_create_samplers(demo) ||
        !lesson26_create_compute_pipelines(demo) ||
        !lesson26_create_graphics_pipelines(demo) ||
        !lesson26_generate_luts(demo)) {
        return false;
    }

    demo->lesson.camera_position = { LESSON26_CAMERA_START_X, LESSON26_CAMERA_START_Y, LESSON26_CAMERA_START_Z };
    demo->lesson.camera_yaw = LESSON26_CAMERA_START_YAW;
    demo->lesson.camera_pitch = 0.0f;
    demo->lesson.pitch_clamp = LESSON26_PITCH_CLAMP;
    demo->lesson.mouse_sensitivity = LESSON26_MOUSE_SENSITIVITY;
    demo->lesson.move_speed = LESSON26_CAMERA_SPEED;
    demo->lesson.last_ticks = SDL_GetTicks();

    state->sun_elevation = LESSON26_SUN_ELEVATION_DEFAULT;
    state->sun_azimuth = LESSON26_SUN_AZIMUTH_DEFAULT;
    state->sun_auto = true;
    state->sun_orbit_angle = LESSON26_SUN_ORBIT_START;
    state->exposure = LESSON26_DEFAULT_EXPOSURE;
    state->tonemap_mode = LESSON26_TONEMAP_ACES;
    state->bloom_enabled = true;
    state->bloom_intensity = LESSON26_DEFAULT_BLOOM_INTENSITY;
    state->bloom_threshold = LESSON26_DEFAULT_BLOOM_THRESHOLD;
    state->last_ticks = SDL_GetTicks();

    if (demo->validation_mode) {
        state->sun_elevation = LESSON26_CAPTURE_SUN_ELEVATION;
        state->sun_azimuth = LESSON26_SUN_ORBIT_START;
        state->sun_auto = false;
    }
    return true;
}

bool ForgeGpuRenderLesson26(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPUTexture *swapchain_texture,
    Uint32 width,
    Uint32 height)
{
    Lesson26State *state = lesson26_state(demo);
    Lesson26TonemapUniforms tonemap_uniforms;

    if (!state) {
        SDL_SetError("lesson 26 internal state is missing");
        return false;
    }

    lesson26_update_camera_and_sun(demo, state);

    if (!lesson26_ensure_hdr_target(demo, width, height) ||
        !ForgeGpuEnsureBloomChain(demo, &state->bloom, state->hdr_target, state->hdr_width, state->hdr_height, LESSON26_HDR_FORMAT) ||
        !lesson26_render_sky(demo, command_buffer, state->hdr_width, state->hdr_height)) {
        return false;
    }

    if (state->bloom_enabled &&
        !ForgeGpuRunBloomChain(
            command_buffer,
            &state->bloom,
            state->hdr_target,
            state->hdr_width,
            state->hdr_height,
            state->downsample_pipeline,
            state->upsample_pipeline,
            state->bloom_sampler,
            state->bloom_threshold,
            LESSON26_FULLSCREEN_VERTICES)) {
        return false;
    }

    SDL_zero(tonemap_uniforms);
    tonemap_uniforms.exposure = state->exposure;
    tonemap_uniforms.tonemap_mode = state->tonemap_mode;
    tonemap_uniforms.bloom_intensity = state->bloom_enabled ? state->bloom_intensity : 0.0f;
    return ForgeGpuRunHdrBloomTonemapPass(
        command_buffer,
        swapchain_texture,
        state->hdr_target,
        state->hdr_sampler,
        &state->bloom,
        state->bloom_sampler,
        state->tonemap_pipeline,
        &tonemap_uniforms,
        sizeof(tonemap_uniforms),
        LESSON26_FULLSCREEN_VERTICES);
}

void ForgeGpuDebugLesson26(ForgeGpuDemo *demo)
{
    Lesson26State *state = lesson26_state(demo);

    if (!state) {
        return;
    }
    ImGui::Text("Tonemap: %s", lesson26_tonemap_name(state->tonemap_mode));
    ImGui::Text("Exposure: %.1f", state->exposure);
    ImGui::Text("Bloom: %s (%.3f)", state->bloom_enabled ? "on" : "off", state->bloom_intensity);
    ImGui::Text("Sun auto: %s", state->sun_auto ? "on" : "off");
    ImGui::Text("Sun elevation/azimuth: %.2f / %.2f", state->sun_elevation, state->sun_azimuth);
    ImGui::Text("Camera km: %.3f, %.3f, %.3f",
        demo->lesson.camera_position.x,
        demo->lesson.camera_position.y,
        demo->lesson.camera_position.z);
    ImGui::Text("LUTs: %ux%u + %ux%u",
        LESSON26_TRANSMITTANCE_LUT_WIDTH,
        LESSON26_TRANSMITTANCE_LUT_HEIGHT,
        LESSON26_MULTISCATTER_LUT_WIDTH,
        LESSON26_MULTISCATTER_LUT_HEIGHT);
}

void ForgeGpuControlsLesson26(ForgeGpuDemo *demo)
{
    (void)demo;
    ImGui::Text("Arrows move sun; T toggles auto sun");
    ImGui::Text("1/2/3 tonemap; +/- exposure; B bloom");
}

static void lesson26_export_metrics(Lesson26State *state)
{
    if (!state) {
        return;
    }
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuProceduralSky", 1.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuProceduralSkyTonemapMode", (double)state->tonemap_mode);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuProceduralSkyBloomEnabled", state->bloom_enabled ? 1.0 : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuProceduralSkySunAuto", state->sun_auto ? 1.0 : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuProceduralSkyExposure", (double)state->exposure);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuProceduralSkyExposureTenths", (double)((int)(state->exposure * 10.0f + 0.5f)));
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuProceduralSkyLutsReady", state->transmittance_lut && state->multiscatter_lut ? 1.0 : 0.0);
}

bool ForgeGpuHandleLesson26Event(ForgeGpuDemo *demo, const SDL_Event *event)
{
    Lesson26State *state = lesson26_state(demo);

    if (!state || event->type != SDL_EVENT_KEY_DOWN) {
        return false;
    }

    switch (event->key.key) {
    case SDLK_LEFT:
    case SDLK_RIGHT:
    case SDLK_UP:
    case SDLK_DOWN:
        return true;
    default:
        break;
    }

    if (ForgeGpuEventIsPlusKey(event)) {
        state->exposure = lesson26_clamp(state->exposure + LESSON26_EXPOSURE_STEP, LESSON26_MIN_EXPOSURE, LESSON26_MAX_EXPOSURE);
        return true;
    }
    if (ForgeGpuEventIsMinusKey(event)) {
        state->exposure = lesson26_clamp(state->exposure - LESSON26_EXPOSURE_STEP, LESSON26_MIN_EXPOSURE, LESSON26_MAX_EXPOSURE);
        return true;
    }

    if (event->key.repeat) {
        return false;
    }

    switch (event->key.key) {
    case SDLK_1:
        state->tonemap_mode = LESSON26_TONEMAP_CLAMP;
        return true;
    case SDLK_2:
        state->tonemap_mode = LESSON26_TONEMAP_REINHARD;
        return true;
    case SDLK_3:
        state->tonemap_mode = LESSON26_TONEMAP_ACES;
        return true;
    case SDLK_B:
        state->bloom_enabled = !state->bloom_enabled;
        return true;
    case SDLK_T:
        state->sun_auto = !state->sun_auto;
        return true;
    default:
        break;
    }
    return false;
}

void ForgeGpuExportLesson26Metrics(ForgeGpuDemo *demo)
{
    Lesson26State *state = lesson26_state(demo);

    lesson26_export_metrics(state);
}

void ForgeGpuDestroyLesson26(ForgeGpuDemo *demo)
{
    Lesson26State *state = lesson26_state(demo);

    if (!state) {
        return;
    }

    ForgeGpuReleaseBloomChain(demo, &state->bloom);
    if (state->bloom_sampler) {
        SDL_ReleaseGPUSampler(demo->device, state->bloom_sampler);
    }
    if (state->hdr_sampler) {
        SDL_ReleaseGPUSampler(demo->device, state->hdr_sampler);
    }
    if (state->lut_sampler) {
        SDL_ReleaseGPUSampler(demo->device, state->lut_sampler);
    }
    if (state->hdr_target) {
        SDL_ReleaseGPUTexture(demo->device, state->hdr_target);
    }
    if (state->multiscatter_lut) {
        SDL_ReleaseGPUTexture(demo->device, state->multiscatter_lut);
    }
    if (state->transmittance_lut) {
        SDL_ReleaseGPUTexture(demo->device, state->transmittance_lut);
    }
    if (state->tonemap_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, state->tonemap_pipeline);
    }
    if (state->upsample_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, state->upsample_pipeline);
    }
    if (state->downsample_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, state->downsample_pipeline);
    }
    if (state->sky_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, state->sky_pipeline);
    }
    if (state->multiscatter_compute_pipeline) {
        SDL_ReleaseGPUComputePipeline(demo->device, state->multiscatter_compute_pipeline);
    }
    if (state->transmittance_compute_pipeline) {
        SDL_ReleaseGPUComputePipeline(demo->device, state->transmittance_compute_pipeline);
    }

    SDL_free(state);
    demo->lesson.private_state = nullptr;
}
