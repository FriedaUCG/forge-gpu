#include "forge_gpu_lessons.h"

#include "forge_gpu_browser_status.h"
#include "forge_gpu_camera.h"
#include "forge_gpu_gpu_helpers.h"
#include "forge_gpu_lesson_common.h"
#include "forge_gpu_math.h"
#include "forge_gpu_shader_layouts.h"
#include "shaders/generated/forge_gpu_lesson_16_shaders.h"
#include "shaders/generated/forge_gpu_lesson_46_shaders.h"

#include "imgui.h"

#include <stddef.h>

#define LESSON46_MAX_PARTICLES 4096u
#define LESSON46_PARTICLE_STRIDE 64u
#define LESSON46_WORKGROUP_SIZE 256u
#define LESSON46_ATLAS_CELLS 4
#define LESSON46_ATLAS_CELL_SIZE 64
#define LESSON46_ATLAS_SIZE (LESSON46_ATLAS_CELLS * LESSON46_ATLAS_CELL_SIZE)
#define LESSON46_BURST_COUNT 1000
#define LESSON46_VALIDATION_READBACK_FRAME 8u
#define LESSON46_FAR_PLANE 100.0f

enum Lesson46EmitterType
{
    LESSON46_EMITTER_FOUNTAIN = 0,
    LESSON46_EMITTER_FIRE = 1,
    LESSON46_EMITTER_SMOKE = 2,
    LESSON46_EMITTER_COUNT = 3
};

struct Lesson46SimUniforms
{
    float dt;
    float gravity;
    float drag;
    Uint32 frame_counter;
    float emitter_pos[4];
    float emitter_params[4];
    float extra_params[4];
};

struct Lesson46BillboardUniforms
{
    Mat4 view_proj;
    float cam_right[4];
    float cam_up[4];
};

struct Lesson46State
{
    SDL_GPUBuffer *particle_buffer;
    SDL_GPUBuffer *counter_buffer;
    SDL_GPUTransferBuffer *counter_upload;
    SDL_GPUTransferBuffer *counter_readback;
    float spawn_rate;
    float gravity;
    float drag;
    float fire_spread;
    float smoke_rise_speed;
    float smoke_spread;
    float smoke_opacity;
    float spawn_accum;
    Uint64 last_ticks;
    Uint32 frame_counter;
    int emitter_type;
    int prev_emitter_type;
    int last_spawn_budget;
    int last_counter_value;
    bool burst_requested;
    bool compute_pass_ran;
    bool draw_pass_ran;
    bool atlas_uploaded;
    bool counter_upload_ok;
    bool readback_pending;
    bool readback_scheduled;
    bool readback_valid;
    char readback_error[256];
};

static_assert(sizeof(Lesson46SimUniforms) == 64, "lesson 46 sim uniforms must match HLSL layout");
static_assert(sizeof(Lesson46BillboardUniforms) == 96, "lesson 46 billboard uniforms must match HLSL layout");

static const char *const kLesson46EmitterNames[LESSON46_EMITTER_COUNT] = {
    "Fountain",
    "Fire",
    "Smoke",
};

static Lesson46State *lesson46_state(ForgeGpuDemo *demo)
{
    return (Lesson46State *)demo->lesson.private_state;
}

static float lesson46_frame_delta(ForgeGpuDemo *demo, Lesson46State *state)
{
    const Uint64 now = SDL_GetTicks();
    float dt;

    if (demo->validation_mode) {
        state->last_ticks = now;
        return 1.0f / 60.0f;
    }

    dt = state->last_ticks != 0 ? (float)(now - state->last_ticks) / 1000.0f : 1.0f / 60.0f;
    state->last_ticks = now;
    if (dt > FORGE_GPU_MAX_DELTA_TIME) {
        dt = FORGE_GPU_MAX_DELTA_TIME;
    }
    return dt;
}

static bool lesson46_create_particle_atlas(ForgeGpuDemo *demo)
{
    const size_t pixel_count = (size_t)LESSON46_ATLAS_SIZE * (size_t)LESSON46_ATLAS_SIZE;
    Uint8 *pixels = (Uint8 *)SDL_malloc(pixel_count * 4u);

    if (!pixels) {
        SDL_OutOfMemory();
        return false;
    }

    for (int cell_row = 0; cell_row < LESSON46_ATLAS_CELLS; cell_row += 1) {
        for (int cell_col = 0; cell_col < LESSON46_ATLAS_CELLS; cell_col += 1) {
            const int frame = cell_row * LESSON46_ATLAS_CELLS + cell_col;
            const float t = (float)frame / (float)(LESSON46_ATLAS_CELLS * LESSON46_ATLAS_CELLS - 1);
            const float radius = 0.15f + t * 0.33f;
            const float intensity = 1.0f - t * 0.7f;
            const int base_x = cell_col * LESSON46_ATLAS_CELL_SIZE;
            const int base_y = cell_row * LESSON46_ATLAS_CELL_SIZE;

            for (int py = 0; py < LESSON46_ATLAS_CELL_SIZE; py += 1) {
                for (int px = 0; px < LESSON46_ATLAS_CELL_SIZE; px += 1) {
                    const float nx = ((float)px + 0.5f) / (float)LESSON46_ATLAS_CELL_SIZE - 0.5f;
                    const float ny = ((float)py + 0.5f) / (float)LESSON46_ATLAS_CELL_SIZE - 0.5f;
                    const float dist = SDL_sqrtf(nx * nx + ny * ny);
                    const float sigma = radius * 0.5f;
                    float alpha = intensity * SDL_expf(-(dist * dist) / (2.0f * sigma * sigma));
                    const size_t index = ((size_t)(base_y + py) * (size_t)LESSON46_ATLAS_SIZE + (size_t)(base_x + px)) * 4u;

                    if (alpha > 1.0f) {
                        alpha = 1.0f;
                    }
                    pixels[index + 0] = 255u;
                    pixels[index + 1] = 255u;
                    pixels[index + 2] = 255u;
                    pixels[index + 3] = (Uint8)(alpha * 255.0f);
                }
            }
        }
    }

    demo->lesson.texture = ForgeGpuCreateRgba8TextureFromPixels(
        demo->device,
        LESSON46_ATLAS_SIZE,
        LESSON46_ATLAS_SIZE,
        pixels,
        false);
    SDL_free(pixels);
    if (!demo->lesson.texture) {
        return false;
    }

    demo->lesson.samplers[0] = ForgeGpuCreateSamplerWithAddress(
        demo->device,
        SDL_GPU_FILTER_LINEAR,
        SDL_GPU_FILTER_LINEAR,
        SDL_GPU_SAMPLERMIPMAPMODE_LINEAR,
        SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        0.0f);
    return demo->lesson.samplers[0] != nullptr;
}

static SDL_GPUGraphicsPipeline *lesson46_create_particle_pipeline(ForgeGpuDemo *demo, bool additive)
{
    SDL_GPUShader *vertex_shader = nullptr;
    SDL_GPUShader *fragment_shader = nullptr;
    SDL_GPUColorTargetDescription color_target;
    SDL_GPUGraphicsPipeline *pipeline = nullptr;

    vertex_shader = ForgeGpuCreateShaderWithResourceLayout(
        demo->device,
        lesson46_particle_vert_wgsl, lesson46_particle_vert_wgsl_size,
        lesson46_particle_vert_msl, lesson46_particle_vert_msl_size,
        ForgeGpuShaderLayout_lesson46_particle_vert());
    fragment_shader = ForgeGpuCreateShaderWithResourceLayout(
        demo->device,
        lesson46_particle_frag_wgsl, lesson46_particle_frag_wgsl_size,
        lesson46_particle_frag_msl, lesson46_particle_frag_msl_size,
        ForgeGpuShaderLayout_lesson46_particle_frag());
    if (!vertex_shader || !fragment_shader) {
        goto done;
    }

    SDL_zero(color_target);
    color_target.format = demo->color_format;
    color_target.blend_state.enable_blend = true;
    color_target.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    color_target.blend_state.dst_color_blendfactor = additive ? SDL_GPU_BLENDFACTOR_ONE : SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    color_target.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
    color_target.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    color_target.blend_state.dst_alpha_blendfactor = additive ? SDL_GPU_BLENDFACTOR_ONE : SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    color_target.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;

    pipeline = ForgeGpuCreateLessonGraphicsPipelineWithColorTargetsAndDepthCompare(
        demo,
        vertex_shader,
        fragment_shader,
        SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        &color_target,
        1,
        nullptr,
        0,
        nullptr,
        0,
        true,
        SDL_GPU_TEXTUREFORMAT_D32_FLOAT,
        true,
        false,
        SDL_GPU_COMPAREOP_LESS_OR_EQUAL,
        SDL_GPU_CULLMODE_NONE,
        0.0f,
        0.0f);

done:
    if (fragment_shader) {
        SDL_ReleaseGPUShader(demo->device, fragment_shader);
    }
    if (vertex_shader) {
        SDL_ReleaseGPUShader(demo->device, vertex_shader);
    }
    return pipeline;
}

static SDL_GPUGraphicsPipeline *lesson46_create_grid_pipeline(ForgeGpuDemo *demo)
{
    SDL_GPUShader *vertex_shader = nullptr;
    SDL_GPUShader *fragment_shader = nullptr;
    SDL_GPUVertexBufferDescription vertex_buffer;
    SDL_GPUVertexAttribute vertex_attribute;
    SDL_GPUGraphicsPipeline *pipeline = nullptr;

    vertex_shader = ForgeGpuCreateShader(
        demo->device, SDL_GPU_SHADERSTAGE_VERTEX,
        lesson16_grid_vert_wgsl, lesson16_grid_vert_wgsl_size,
        lesson16_grid_vert_msl, lesson16_grid_vert_msl_size,
        0, 0, 0, 1);
    fragment_shader = ForgeGpuCreateShader(
        demo->device, SDL_GPU_SHADERSTAGE_FRAGMENT,
        lesson16_grid_frag_wgsl, lesson16_grid_frag_wgsl_size,
        lesson16_grid_frag_msl, lesson16_grid_frag_msl_size,
        0, 0, 0, 1);
    if (!vertex_shader || !fragment_shader) {
        goto done;
    }

    SDL_zero(vertex_buffer);
    vertex_buffer.slot = 0;
    vertex_buffer.pitch = sizeof(GridVertex);
    vertex_buffer.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    SDL_zero(vertex_attribute);
    vertex_attribute.location = 0;
    vertex_attribute.buffer_slot = 0;
    vertex_attribute.format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    vertex_attribute.offset = offsetof(GridVertex, position);

    pipeline = ForgeGpuCreateLessonGraphicsPipeline(
        demo,
        vertex_shader,
        fragment_shader,
        &vertex_buffer,
        1,
        &vertex_attribute,
        1,
        1,
        true,
        SDL_GPU_TEXTUREFORMAT_D32_FLOAT,
        true,
        true,
        SDL_GPU_CULLMODE_NONE,
        0.0f,
        0.0f);

done:
    if (fragment_shader) {
        SDL_ReleaseGPUShader(demo->device, fragment_shader);
    }
    if (vertex_shader) {
        SDL_ReleaseGPUShader(demo->device, vertex_shader);
    }
    return pipeline;
}

static bool lesson46_create_buffers(ForgeGpuDemo *demo, Lesson46State *state)
{
    const Uint32 particle_bytes = LESSON46_MAX_PARTICLES * LESSON46_PARTICLE_STRIDE;
    void *zeros = SDL_calloc(1, particle_bytes);
    int zero_counter = 0;
    SDL_GPUTransferBufferCreateInfo transfer_info;

    if (!zeros) {
        SDL_OutOfMemory();
        return false;
    }
    state->particle_buffer = ForgeGpuCreateBufferWithData(
        demo->device,
        SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE | SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ,
        zeros,
        particle_bytes);
    SDL_free(zeros);
    if (!state->particle_buffer) {
        return false;
    }

    state->counter_buffer = ForgeGpuCreateBufferWithData(
        demo->device,
        SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE,
        &zero_counter,
        (Uint32)sizeof(zero_counter));
    if (!state->counter_buffer) {
        return false;
    }

    SDL_zero(transfer_info);
    transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transfer_info.size = (Uint32)sizeof(zero_counter);
    state->counter_upload = SDL_CreateGPUTransferBuffer(demo->device, &transfer_info);
    if (!state->counter_upload) {
        return false;
    }

    SDL_zero(transfer_info);
    transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    transfer_info.size = (Uint32)sizeof(zero_counter);
    state->counter_readback = SDL_CreateGPUTransferBuffer(demo->device, &transfer_info);
    return state->counter_readback != nullptr;
}

static bool lesson46_upload_spawn_counter(
    ForgeGpuDemo *demo,
    Lesson46State *state,
    SDL_GPUCommandBuffer *command_buffer,
    int spawn_count)
{
    void *mapped;
    SDL_GPUCopyPass *copy_pass;
    SDL_GPUTransferBufferLocation source;
    SDL_GPUBufferRegion destination;

    mapped = SDL_MapGPUTransferBuffer(demo->device, state->counter_upload, true);
    if (!mapped) {
        return false;
    }
    SDL_memcpy(mapped, &spawn_count, sizeof(spawn_count));
    SDL_UnmapGPUTransferBuffer(demo->device, state->counter_upload);

    copy_pass = SDL_BeginGPUCopyPass(command_buffer);
    if (!copy_pass) {
        return false;
    }
    SDL_zero(source);
    source.transfer_buffer = state->counter_upload;
    SDL_zero(destination);
    destination.buffer = state->counter_buffer;
    destination.size = (Uint32)sizeof(spawn_count);
    SDL_UploadToGPUBuffer(copy_pass, &source, &destination, false);
    SDL_EndGPUCopyPass(copy_pass);
    return true;
}

static void lesson46_decode_pending_readback(ForgeGpuDemo *demo, Lesson46State *state)
{
    const int *value;

    if (!state->readback_pending) {
        return;
    }
    if (!SDL_WaitForGPUIdle(demo->device)) {
        const char *error = SDL_GetError();
        SDL_Log("lesson 46 counter readback wait failed: %s", error);
        SDL_strlcpy(state->readback_error, error && error[0] ? error : "GPU idle wait failed",
            sizeof(state->readback_error));
        state->readback_pending = false;
        return;
    }

    value = (const int *)SDL_MapGPUTransferBuffer(demo->device, state->counter_readback, false);
    if (!value) {
        const char *error = SDL_GetError();
        SDL_Log("lesson 46 counter readback map failed: %s", error);
        SDL_strlcpy(state->readback_error, error && error[0] ? error : "counter readback map failed",
            sizeof(state->readback_error));
        state->readback_pending = false;
        return;
    }
    state->last_counter_value = *value;
    SDL_UnmapGPUTransferBuffer(demo->device, state->counter_readback);
    state->readback_valid = true;
    state->readback_pending = false;
    state->readback_error[0] = '\0';
}

static void lesson46_schedule_counter_readback(
    ForgeGpuDemo *demo,
    Lesson46State *state,
    SDL_GPUCommandBuffer *command_buffer)
{
    SDL_GPUCopyPass *copy_pass;
    SDL_GPUBufferRegion source;
    SDL_GPUTransferBufferLocation destination;

    if (!demo->validation_mode ||
        state->readback_scheduled ||
        state->readback_pending ||
        state->frame_counter < LESSON46_VALIDATION_READBACK_FRAME) {
        return;
    }

    copy_pass = SDL_BeginGPUCopyPass(command_buffer);
    if (!copy_pass) {
        const char *error = SDL_GetError();
        SDL_strlcpy(state->readback_error, error && error[0] ? error : "counter readback copy pass failed",
            sizeof(state->readback_error));
        return;
    }
    SDL_zero(source);
    source.buffer = state->counter_buffer;
    source.size = (Uint32)sizeof(state->last_counter_value);
    SDL_zero(destination);
    destination.transfer_buffer = state->counter_readback;
    SDL_DownloadFromGPUBuffer(copy_pass, &source, &destination);
    SDL_EndGPUCopyPass(copy_pass);
    state->readback_pending = true;
    state->readback_scheduled = true;
    state->readback_error[0] = '\0';
}

static void lesson46_draw_grid(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    Mat4 camera_vp)
{
    UniformMvp vertex_uniforms;
    GridFragUniforms fragment_uniforms;
    SDL_GPUBufferBinding vertex_binding;
    SDL_GPUBufferBinding index_binding;

    SDL_BindGPUGraphicsPipeline(render_pass, demo->lesson.tertiary_pipeline);
    vertex_uniforms.mvp = camera_vp;
    SDL_PushGPUVertexUniformData(command_buffer, 0, &vertex_uniforms, sizeof(vertex_uniforms));

    SDL_zero(fragment_uniforms);
    fragment_uniforms.line_color[0] = 0.55f;
    fragment_uniforms.line_color[1] = 0.62f;
    fragment_uniforms.line_color[2] = 0.68f;
    fragment_uniforms.line_color[3] = 1.0f;
    fragment_uniforms.bg_color[0] = 0.08f;
    fragment_uniforms.bg_color[1] = 0.09f;
    fragment_uniforms.bg_color[2] = 0.11f;
    fragment_uniforms.bg_color[3] = 1.0f;
    fragment_uniforms.light_dir[0] = 0.3f;
    fragment_uniforms.light_dir[1] = 0.8f;
    fragment_uniforms.light_dir[2] = 0.5f;
    fragment_uniforms.light_dir[3] = 0.0f;
    fragment_uniforms.eye_pos[0] = demo->lesson.camera_position.x;
    fragment_uniforms.eye_pos[1] = demo->lesson.camera_position.y;
    fragment_uniforms.eye_pos[2] = demo->lesson.camera_position.z;
    fragment_uniforms.eye_pos[3] = 1.0f;
    fragment_uniforms.grid_spacing = 1.0f;
    fragment_uniforms.line_width = 0.035f;
    fragment_uniforms.fade_distance = 50.0f;
    fragment_uniforms.ambient = 0.35f;
    fragment_uniforms.shininess = 32.0f;
    fragment_uniforms.specular_str = 0.05f;
    SDL_PushGPUFragmentUniformData(command_buffer, 0, &fragment_uniforms, sizeof(fragment_uniforms));

    SDL_zero(vertex_binding);
    vertex_binding.buffer = demo->lesson.vertex_buffer;
    SDL_zero(index_binding);
    index_binding.buffer = demo->lesson.index_buffer;
    SDL_BindGPUVertexBuffers(render_pass, 0, &vertex_binding, 1);
    SDL_BindGPUIndexBuffer(render_pass, &index_binding, SDL_GPU_INDEXELEMENTSIZE_16BIT);
    SDL_DrawGPUIndexedPrimitives(render_pass, 6, 1, 0, 0, 0);
}

static bool lesson46_run_compute_pass(
    ForgeGpuDemo *demo,
    Lesson46State *state,
    SDL_GPUCommandBuffer *command_buffer,
    float dt)
{
    Lesson46SimUniforms uniforms;
    SDL_GPUStorageBufferReadWriteBinding bindings[2];
    SDL_GPUComputePass *compute_pass;
    const Uint32 groups = (LESSON46_MAX_PARTICLES + LESSON46_WORKGROUP_SIZE - 1u) / LESSON46_WORKGROUP_SIZE;

    SDL_zero(uniforms);
    uniforms.dt = dt;
    uniforms.gravity = state->gravity;
    uniforms.drag = state->drag;
    uniforms.frame_counter = state->frame_counter;
    uniforms.emitter_pos[1] = 0.5f;
    uniforms.emitter_params[0] = (float)state->emitter_type;
    uniforms.emitter_params[1] = 6.0f;
    uniforms.emitter_params[2] = 0.08f;
    uniforms.emitter_params[3] = 0.3f;
    if (state->emitter_type == LESSON46_EMITTER_FIRE) {
        uniforms.extra_params[0] = state->fire_spread;
    } else if (state->emitter_type == LESSON46_EMITTER_SMOKE) {
        uniforms.extra_params[0] = state->smoke_rise_speed;
        uniforms.extra_params[1] = state->smoke_spread;
        uniforms.extra_params[2] = state->smoke_opacity;
    }
    SDL_PushGPUComputeUniformData(command_buffer, 0, &uniforms, sizeof(uniforms));

    SDL_zeroa(bindings);
    bindings[0].buffer = state->particle_buffer;
    bindings[1].buffer = state->counter_buffer;
    compute_pass = SDL_BeginGPUComputePass(command_buffer, nullptr, 0, bindings, 2);
    if (!compute_pass) {
        return false;
    }
    SDL_BindGPUComputePipeline(compute_pass, demo->lesson.compute_pipeline);
    SDL_DispatchGPUCompute(compute_pass, groups, 1, 1);
    SDL_EndGPUComputePass(compute_pass);
    state->compute_pass_ran = true;
    return true;
}

static void lesson46_draw_particles(
    ForgeGpuDemo *demo,
    Lesson46State *state,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    Mat4 camera_vp)
{
    Lesson46BillboardUniforms uniforms;
    SDL_GPUBuffer *particle_buffer = state->particle_buffer;
    SDL_GPUTextureSamplerBinding atlas_binding;
    const Quat camera_orientation = quat_from_euler(
        demo->lesson.camera_yaw,
        demo->lesson.camera_pitch,
        0.0f);
    const Vec3 right = quat_right(camera_orientation);
    const Vec3 up = quat_up(camera_orientation);

    SDL_BindGPUGraphicsPipeline(
        render_pass,
        state->emitter_type == LESSON46_EMITTER_SMOKE ? demo->lesson.secondary_pipeline : demo->lesson.pipeline);
    SDL_BindGPUVertexStorageBuffers(render_pass, 0, &particle_buffer, 1);

    SDL_zero(atlas_binding);
    atlas_binding.texture = demo->lesson.texture;
    atlas_binding.sampler = demo->lesson.samplers[0];
    SDL_BindGPUFragmentSamplers(render_pass, 0, &atlas_binding, 1);

    uniforms.view_proj = camera_vp;
    uniforms.cam_right[0] = right.x;
    uniforms.cam_right[1] = right.y;
    uniforms.cam_right[2] = right.z;
    uniforms.cam_right[3] = 0.0f;
    uniforms.cam_up[0] = up.x;
    uniforms.cam_up[1] = up.y;
    uniforms.cam_up[2] = up.z;
    uniforms.cam_up[3] = 0.0f;
    SDL_PushGPUVertexUniformData(command_buffer, 0, &uniforms, sizeof(uniforms));

    SDL_DrawGPUPrimitives(render_pass, LESSON46_MAX_PARTICLES * 6u, 1, 0, 0);
    state->draw_pass_ran = true;
}

bool ForgeGpuCreateLesson46(ForgeGpuDemo *demo)
{
    Lesson46State *state = (Lesson46State *)SDL_calloc(1, sizeof(*state));

    if (!state) {
        SDL_OutOfMemory();
        return false;
    }
    demo->lesson.private_state = state;

    state->spawn_rate = 150.0f;
    state->gravity = -9.8f;
    state->drag = 1.0f;
    state->fire_spread = 1.0f;
    state->smoke_rise_speed = 1.0f;
    state->smoke_spread = 1.0f;
    state->smoke_opacity = 0.5f;
    state->emitter_type = LESSON46_EMITTER_FIRE;
    state->prev_emitter_type = LESSON46_EMITTER_FIRE;
    state->last_ticks = SDL_GetTicks();

    demo->lesson.camera_position = { 0.0f, 3.0f, 4.0f };
    demo->lesson.camera_yaw = 0.0f;
    demo->lesson.camera_pitch = -0.35f;
    demo->lesson.move_speed = 5.0f;
    demo->lesson.mouse_sensitivity = 0.0025f;
    demo->lesson.pitch_clamp = 1.45f;
    demo->lesson.last_ticks = SDL_GetTicks();

    demo->lesson.compute_pipeline = ForgeGpuCreateComputePipelineWithResourceLayout(
        demo->device,
        lesson46_particle_sim_comp_wgsl, lesson46_particle_sim_comp_wgsl_size,
        lesson46_particle_sim_comp_msl, lesson46_particle_sim_comp_msl_size,
        ForgeGpuComputePipelineLayout_lesson46_particle_sim_comp(),
        LESSON46_WORKGROUP_SIZE, 1, 1);
    demo->lesson.pipeline = lesson46_create_particle_pipeline(demo, true);
    demo->lesson.secondary_pipeline = lesson46_create_particle_pipeline(demo, false);
    demo->lesson.tertiary_pipeline = lesson46_create_grid_pipeline(demo);

    if (!demo->lesson.compute_pipeline ||
        !demo->lesson.pipeline ||
        !demo->lesson.secondary_pipeline ||
        !demo->lesson.tertiary_pipeline ||
        !lesson46_create_particle_atlas(demo)) {
        return false;
    }
    state->atlas_uploaded = true;

    return lesson46_create_buffers(demo, state) &&
           ForgeGpuCreateGridBuffers(demo);
}

bool ForgeGpuRenderLesson46(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPUTexture *swapchain_texture,
    Uint32 width,
    Uint32 height)
{
    Lesson46State *state = lesson46_state(demo);
    Mat4 view;
    Mat4 projection;
    Mat4 camera_vp;
    SDL_GPUColorTargetInfo color_target;
    SDL_GPUDepthStencilTargetInfo depth_target;
    SDL_GPURenderPass *render_pass;
    float dt;
    int spawn_count;

    if (!state) {
        SDL_SetError("lesson 46 internal state is missing");
        return false;
    }
    if (!ForgeGpuCreateDepthTextureWithFormat(demo, width, height, SDL_GPU_TEXTUREFORMAT_D32_FLOAT)) {
        return false;
    }

    lesson46_decode_pending_readback(demo, state);
    if (demo->validation_mode) {
        if (state->readback_error[0] != '\0') {
            SDL_SetError("lesson 46 counter readback failed: %s", state->readback_error);
            return false;
        }
        if (state->readback_valid && state->last_counter_value > 0) {
            SDL_SetError("lesson 46 counter readback stayed positive after atomic spawn dispatch");
            return false;
        }
        if (!state->readback_valid &&
            state->frame_counter > LESSON46_VALIDATION_READBACK_FRAME + 4u) {
            SDL_SetError("lesson 46 counter readback did not complete");
            return false;
        }
    }
    ForgeGpuUpdateCameraFromInput(demo);
    ForgeGpuCameraViewProjection(demo, width, height, LESSON46_FAR_PLANE, &view, &projection);
    camera_vp = mat4_multiply(projection, view);
    dt = lesson46_frame_delta(demo, state);

    state->compute_pass_ran = false;
    state->draw_pass_ran = false;
    state->counter_upload_ok = false;

    if (state->emitter_type != state->prev_emitter_type) {
        state->frame_counter = 0;
        state->prev_emitter_type = state->emitter_type;
    }

    state->spawn_accum += state->spawn_rate * dt;
    spawn_count = (int)state->spawn_accum;
    state->spawn_accum -= (float)spawn_count;
    if (state->burst_requested) {
        spawn_count += LESSON46_BURST_COUNT;
        state->burst_requested = false;
    }
    if (spawn_count > (int)LESSON46_MAX_PARTICLES) {
        spawn_count = (int)LESSON46_MAX_PARTICLES;
    }
    state->last_spawn_budget = spawn_count;

    state->counter_upload_ok = lesson46_upload_spawn_counter(demo, state, command_buffer, spawn_count);
    if (state->counter_upload_ok) {
        if (!lesson46_run_compute_pass(demo, state, command_buffer, dt)) {
            return false;
        }
        lesson46_schedule_counter_readback(demo, state, command_buffer);
    }
    state->frame_counter += 1u;

    SDL_zero(color_target);
    color_target.texture = swapchain_texture;
    color_target.clear_color = gForgeGpuLessons[demo->active_lesson].clear_color;
    color_target.load_op = SDL_GPU_LOADOP_CLEAR;
    color_target.store_op = SDL_GPU_STOREOP_STORE;

    SDL_zero(depth_target);
    depth_target.texture = demo->lesson.depth_texture;
    depth_target.clear_depth = 1.0f;
    depth_target.load_op = SDL_GPU_LOADOP_CLEAR;
    depth_target.store_op = SDL_GPU_STOREOP_STORE;
    depth_target.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
    depth_target.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;

    render_pass = SDL_BeginGPURenderPass(command_buffer, &color_target, 1, &depth_target);
    if (!render_pass) {
        return false;
    }
    lesson46_draw_grid(demo, command_buffer, render_pass, camera_vp);
    lesson46_draw_particles(demo, state, command_buffer, render_pass, camera_vp);
    SDL_EndGPURenderPass(render_pass);
    return true;
}

void ForgeGpuDebugLesson46(ForgeGpuDemo *demo)
{
    Lesson46State *state = lesson46_state(demo);

    if (!state) {
        return;
    }

    ImGui::Text("Emitter: %s", kLesson46EmitterNames[state->emitter_type]);
    ImGui::Text("Particles: %u max", LESSON46_MAX_PARTICLES);
    ImGui::Text("Compute: %s", state->compute_pass_ran ? "yes" : "no");
    ImGui::Text("Draw: %s", state->draw_pass_ran ? "yes" : "no");
    if (state->readback_valid) {
        ImGui::Text("Counter readback: %d", state->last_counter_value);
    } else {
        ImGui::Text("Counter readback: pending");
    }
}

void ForgeGpuControlsLesson46(ForgeGpuDemo *demo)
{
    Lesson46State *state = lesson46_state(demo);

    if (!state) {
        return;
    }

    ImGui::TextUnformatted("1/2/3: emitter, B: burst");
    ImGui::TextUnformatted("Emitter");
    for (int i = 0; i < LESSON46_EMITTER_COUNT; i += 1) {
        if (ImGui::RadioButton(kLesson46EmitterNames[i], state->emitter_type == i)) {
            state->emitter_type = i;
        }
    }
    ImGui::SliderFloat("Spawn rate", &state->spawn_rate, 0.0f, 500.0f, "%.0f/s");
    if (state->emitter_type == LESSON46_EMITTER_FOUNTAIN) {
        ImGui::SliderFloat("Gravity", &state->gravity, -20.0f, 0.0f, "%.1f");
        ImGui::SliderFloat("Drag", &state->drag, 0.0f, 5.0f, "%.2f");
    } else if (state->emitter_type == LESSON46_EMITTER_FIRE) {
        ImGui::SliderFloat("Spread", &state->fire_spread, 0.2f, 5.0f, "%.1f");
    } else {
        ImGui::SliderFloat("Rise speed", &state->smoke_rise_speed, 0.2f, 3.0f, "%.1f");
        ImGui::SliderFloat("Spread", &state->smoke_spread, 0.2f, 5.0f, "%.1f");
        ImGui::SliderFloat("Opacity", &state->smoke_opacity, 0.05f, 1.0f, "%.2f");
    }
    if (ImGui::Button("Burst")) {
        state->burst_requested = true;
    }
}

bool ForgeGpuHandleLesson46Event(ForgeGpuDemo *demo, const SDL_Event *event)
{
    Lesson46State *state = lesson46_state(demo);

    if (!state || event->type != SDL_EVENT_KEY_DOWN || event->key.repeat) {
        return false;
    }
    switch (event->key.key) {
    case SDLK_1:
        state->emitter_type = LESSON46_EMITTER_FOUNTAIN;
        return true;
    case SDLK_2:
        state->emitter_type = LESSON46_EMITTER_FIRE;
        return true;
    case SDLK_3:
        state->emitter_type = LESSON46_EMITTER_SMOKE;
        return true;
    case SDLK_B:
        state->burst_requested = true;
        return true;
    default:
        return false;
    }
}

void ForgeGpuExportLesson46Metrics(ForgeGpuDemo *demo)
{
    Lesson46State *state = lesson46_state(demo);

    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson46Complete", state ? 1.0 : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson46MaxParticles", (double)LESSON46_MAX_PARTICLES);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson46EmitterType", state ? (double)state->emitter_type : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson46AtlasUploaded", state && state->atlas_uploaded ? 1.0 : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson46ComputePass", state && state->compute_pass_ran ? 1.0 : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson46DrawPass", state && state->draw_pass_ran ? 1.0 : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson46CounterUpload", state && state->counter_upload_ok ? 1.0 : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson46SpawnBudget", state ? (double)state->last_spawn_budget : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson46CounterReadbackValid", state && state->readback_valid ? 1.0 : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson46CounterReadback", state ? (double)state->last_counter_value : 0.0);
}

void ForgeGpuDestroyLesson46(ForgeGpuDemo *demo)
{
    Lesson46State *state = lesson46_state(demo);

    if (!state) {
        return;
    }
    if (state->counter_readback) {
        SDL_ReleaseGPUTransferBuffer(demo->device, state->counter_readback);
    }
    if (state->counter_upload) {
        SDL_ReleaseGPUTransferBuffer(demo->device, state->counter_upload);
    }
    if (state->counter_buffer) {
        SDL_ReleaseGPUBuffer(demo->device, state->counter_buffer);
    }
    if (state->particle_buffer) {
        SDL_ReleaseGPUBuffer(demo->device, state->particle_buffer);
    }
    SDL_free(state);
    demo->lesson.private_state = nullptr;
}
