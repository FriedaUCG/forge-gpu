#include "forge_gpu_lesson_common.h"

#include "forge_gpu_camera.h"
#include "forge_gpu_browser_status.h"
#include "forge_gpu_gpu_helpers.h"
#include "forge_gpu_imgui.h"
#include "forge_gpu_math.h"
#include "forge_gpu_scene.h"

#define FORGE_GPU_MAX_SHARED_COLOR_TARGETS 4

const Uint16 kForgeGpuQuadIndices[6] = {
    0, 1, 2,
    2, 3, 0
};

const LessonVertex3Color kForgeGpuCubeVertices[24] = {
    { { -0.5f, -0.5f,  0.5f }, { 1.0f, 0.0f, 0.0f } },
    { {  0.5f, -0.5f,  0.5f }, { 1.0f, 0.0f, 0.0f } },
    { {  0.5f,  0.5f,  0.5f }, { 1.0f, 0.0f, 0.0f } },
    { { -0.5f,  0.5f,  0.5f }, { 1.0f, 0.0f, 0.0f } },
    { {  0.5f, -0.5f, -0.5f }, { 0.0f, 1.0f, 1.0f } },
    { { -0.5f, -0.5f, -0.5f }, { 0.0f, 1.0f, 1.0f } },
    { { -0.5f,  0.5f, -0.5f }, { 0.0f, 1.0f, 1.0f } },
    { {  0.5f,  0.5f, -0.5f }, { 0.0f, 1.0f, 1.0f } },
    { {  0.5f, -0.5f,  0.5f }, { 0.0f, 1.0f, 0.0f } },
    { {  0.5f, -0.5f, -0.5f }, { 0.0f, 1.0f, 0.0f } },
    { {  0.5f,  0.5f, -0.5f }, { 0.0f, 1.0f, 0.0f } },
    { {  0.5f,  0.5f,  0.5f }, { 0.0f, 1.0f, 0.0f } },
    { { -0.5f, -0.5f, -0.5f }, { 1.0f, 0.0f, 1.0f } },
    { { -0.5f, -0.5f,  0.5f }, { 1.0f, 0.0f, 1.0f } },
    { { -0.5f,  0.5f,  0.5f }, { 1.0f, 0.0f, 1.0f } },
    { { -0.5f,  0.5f, -0.5f }, { 1.0f, 0.0f, 1.0f } },
    { { -0.5f,  0.5f,  0.5f }, { 0.0f, 0.0f, 1.0f } },
    { {  0.5f,  0.5f,  0.5f }, { 0.0f, 0.0f, 1.0f } },
    { {  0.5f,  0.5f, -0.5f }, { 0.0f, 0.0f, 1.0f } },
    { { -0.5f,  0.5f, -0.5f }, { 0.0f, 0.0f, 1.0f } },
    { { -0.5f, -0.5f, -0.5f }, { 1.0f, 1.0f, 0.0f } },
    { {  0.5f, -0.5f, -0.5f }, { 1.0f, 1.0f, 0.0f } },
    { {  0.5f, -0.5f,  0.5f }, { 1.0f, 1.0f, 0.0f } },
    { { -0.5f, -0.5f,  0.5f }, { 1.0f, 1.0f, 0.0f } }
};

const Uint16 kForgeGpuCubeIndices[36] = {
     0,  1,  2,   2,  3,  0,
     4,  5,  6,   6,  7,  4,
     8,  9, 10,  10, 11,  8,
    12, 13, 14,  14, 15, 12,
    16, 17, 18,  18, 19, 16,
    20, 21, 22,  22, 23, 20
};

const GridVertex kForgeGpuGridVertices[4] = {
    { { -50.0f, 0.0f, -50.0f } },
    { {  50.0f, 0.0f, -50.0f } },
    { {  50.0f, 0.0f,  50.0f } },
    { { -50.0f, 0.0f,  50.0f } }
};

const Uint16 kForgeGpuGridIndices[6] = {
    0, 1, 2,
    2, 3, 0
};

static void clamp_window_mouse_position(ForgeGpuDemo *demo, float *x, float *y)
{
    int width = 0;
    int height = 0;

    SDL_GetWindowSize(demo->window, &width, &height);
    if (width > 0) {
        *x = SDL_min(SDL_max(*x, 0.0f), (float)(width - 1));
    }
    if (height > 0) {
        *y = SDL_min(SDL_max(*y, 0.0f), (float)(height - 1));
    }
}

static void clear_mouse_capture_state(LessonState *lesson)
{
    lesson->mouse_captured = false;
    lesson->mouse_capture_origin_valid = false;
    lesson->mouse_capture_origin_x = 0.0f;
    lesson->mouse_capture_origin_y = 0.0f;
    lesson->mouse_capture_started_ticks = 0;
    lesson->browser_pointer_lock_seen = false;
}

bool ForgeGpuAcquireCameraMouse(ForgeGpuDemo *demo, float x, float y)
{
    LessonState *lesson = &demo->lesson;

    if (!SDL_SetWindowRelativeMouseMode(demo->window, true)) {
        return false;
    }

    lesson->mouse_captured = true;
    lesson->mouse_capture_origin_x = x;
    lesson->mouse_capture_origin_y = y;
    lesson->mouse_capture_origin_valid = true;
    lesson->mouse_capture_started_ticks = SDL_GetTicks();
    lesson->browser_pointer_lock_seen = false;
    ForgeGpuClearImGuiMouse(demo);
    return true;
}

bool ForgeGpuReleaseCameraMouse(ForgeGpuDemo *demo)
{
    LessonState *lesson = &demo->lesson;

    if (lesson->mouse_capture_origin_valid) {
        float x = lesson->mouse_capture_origin_x;
        float y = lesson->mouse_capture_origin_y;

        clamp_window_mouse_position(demo, &x, &y);
        SDL_WarpMouseInWindow(demo->window, x, y);
    }

    if (!SDL_SetWindowRelativeMouseMode(demo->window, false)) {
        return false;
    }

    clear_mouse_capture_state(lesson);
    ForgeGpuClearImGuiMouse(demo);
    return true;
}

void ForgeGpuSyncCameraMouseCapture(ForgeGpuDemo *demo)
{
#if defined(SDL_PLATFORM_EMSCRIPTEN)
    LessonState *lesson = &demo->lesson;

    if (!lesson->mouse_captured) {
        return;
    }

    if (ForgeGpuBrowserHasPointerLock()) {
        lesson->browser_pointer_lock_seen = true;
        return;
    }

    if (lesson->browser_pointer_lock_seen ||
        SDL_GetTicks() - lesson->mouse_capture_started_ticks > 1000) {
        if (!ForgeGpuReleaseCameraMouse(demo)) {
            SDL_Log("SDL_SetWindowRelativeMouseMode failed after browser pointer-lock release: %s", SDL_GetError());
        }
    }
#else
    (void)demo;
#endif
}

bool ForgeGpuEventIsPlusKey(const SDL_Event *event)
{
    if (!event || event->type != SDL_EVENT_KEY_DOWN) {
        return false;
    }
    if (event->key.key == SDLK_PLUS || event->key.key == SDLK_KP_PLUS) {
        return true;
    }
    if ((event->key.key == SDLK_EQUALS || event->key.scancode == SDL_SCANCODE_EQUALS) &&
        (event->key.mod & SDL_KMOD_SHIFT)) {
        return true;
    }
    return false;
}

bool ForgeGpuEventIsMinusKey(const SDL_Event *event)
{
    if (!event || event->type != SDL_EVENT_KEY_DOWN) {
        return false;
    }
    return event->key.key == SDLK_MINUS || event->key.key == SDLK_KP_MINUS ||
           event->key.scancode == SDL_SCANCODE_MINUS || event->key.scancode == SDL_SCANCODE_KP_MINUS;
}

void ForgeGpuDestroySharedLessonResources(ForgeGpuDemo *demo)
{
    LessonState *lesson = &demo->lesson;

    if (lesson->mouse_captured) {
        if (!ForgeGpuReleaseCameraMouse(demo)) {
            SDL_Log("SDL_SetWindowRelativeMouseMode failed during lesson cleanup: %s", SDL_GetError());
        }
    }
    ForgeGpuFreeGpuScene(demo);
    ForgeGpuFreeLoadedScene(&lesson->scene);
    if (lesson->depth_texture) {
        SDL_ReleaseGPUTexture(demo->device, lesson->depth_texture);
    }
    for (int i = 0; i < FORGE_GPU_MAX_SAMPLERS; i += 1) {
        if (lesson->samplers[i]) {
            SDL_ReleaseGPUSampler(demo->device, lesson->samplers[i]);
        }
    }
    if (lesson->white_texture) {
        SDL_ReleaseGPUTexture(demo->device, lesson->white_texture);
    }
    if (lesson->texture) {
        SDL_ReleaseGPUTexture(demo->device, lesson->texture);
    }
    if (lesson->index_buffer) {
        SDL_ReleaseGPUBuffer(demo->device, lesson->index_buffer);
    }
    if (lesson->vertex_buffer) {
        SDL_ReleaseGPUBuffer(demo->device, lesson->vertex_buffer);
    }
    if (lesson->compute_pipeline) {
        SDL_ReleaseGPUComputePipeline(demo->device, lesson->compute_pipeline);
    }
    if (lesson->secondary_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, lesson->secondary_pipeline);
    }
    if (lesson->tertiary_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, lesson->tertiary_pipeline);
    }
    if (lesson->debug_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, lesson->debug_pipeline);
    }
    if (lesson->pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, lesson->pipeline);
    }

    SDL_zero(*lesson);
}

bool ForgeGpuRenderDefaultLessonPass(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPUTexture *swapchain_texture,
    Uint32 width,
    Uint32 height,
    ForgeGpuLessonRenderPassFn draw)
{
    const LessonDesc *lesson_desc = &gForgeGpuLessons[demo->active_lesson];
    SDL_GPUColorTargetInfo color_target;
    SDL_GPUDepthStencilTargetInfo depth_target;
    SDL_GPURenderPass *render_pass;

    if (lesson_desc->needs_depth && !ForgeGpuCreateDepthTextureWithFormat(demo, width, height, lesson_desc->depth_format)) {
        return false;
    }

    SDL_zero(color_target);
    color_target.texture = swapchain_texture;
    color_target.load_op = SDL_GPU_LOADOP_CLEAR;
    color_target.store_op = SDL_GPU_STOREOP_STORE;
    color_target.clear_color = lesson_desc->clear_color;

    if (lesson_desc->needs_depth) {
        SDL_zero(depth_target);
        depth_target.texture = demo->lesson.depth_texture;
        depth_target.clear_depth = 1.0f;
        depth_target.load_op = SDL_GPU_LOADOP_CLEAR;
        depth_target.store_op = SDL_GPU_STOREOP_STORE;
        depth_target.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
        depth_target.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
        render_pass = SDL_BeginGPURenderPass(command_buffer, &color_target, 1, &depth_target);
    } else {
        render_pass = SDL_BeginGPURenderPass(command_buffer, &color_target, 1, nullptr);
    }

    if (!render_pass) {
        return false;
    }
    if (draw) {
        draw(demo, command_buffer, render_pass, width, height);
    }
    SDL_EndGPURenderPass(render_pass);
    return true;
}

bool ForgeGpuEnsureSampledColorTarget(
    ForgeGpuDemo *demo,
    SDL_GPUTexture **texture,
    Uint32 *texture_width,
    Uint32 *texture_height,
    Uint32 width,
    Uint32 height,
    SDL_GPUTextureFormat format)
{
    SDL_GPUTextureCreateInfo texture_info;
    SDL_GPUTexture *new_texture;

    if (!texture || !texture_width || !texture_height) {
        SDL_SetError("sampled color target storage is missing");
        return false;
    }
    if (*texture && *texture_width == width && *texture_height == height) {
        return true;
    }

    SDL_zero(texture_info);
    texture_info.type = SDL_GPU_TEXTURETYPE_2D;
    texture_info.format = format;
    texture_info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    texture_info.width = width;
    texture_info.height = height;
    texture_info.layer_count_or_depth = 1;
    texture_info.num_levels = 1;
    texture_info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    new_texture = SDL_CreateGPUTexture(demo->device, &texture_info);
    if (!new_texture) {
        return false;
    }
    if (*texture) {
        SDL_ReleaseGPUTexture(demo->device, *texture);
    }
    *texture = new_texture;
    *texture_width = width;
    *texture_height = height;
    return true;
}

bool ForgeGpuEnsureSampledDepthTarget(
    ForgeGpuDemo *demo,
    SDL_GPUTexture **texture,
    Uint32 *texture_width,
    Uint32 *texture_height,
    Uint32 width,
    Uint32 height,
    SDL_GPUTextureFormat format)
{
    SDL_GPUTextureCreateInfo texture_info;
    SDL_GPUTexture *new_texture;

    if (!texture || !texture_width || !texture_height) {
        SDL_SetError("sampled depth target storage is missing");
        return false;
    }
    if (*texture && *texture_width == width && *texture_height == height) {
        return true;
    }

    SDL_zero(texture_info);
    texture_info.type = SDL_GPU_TEXTURETYPE_2D;
    texture_info.format = format;
    texture_info.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    texture_info.width = width;
    texture_info.height = height;
    texture_info.layer_count_or_depth = 1;
    texture_info.num_levels = 1;
    texture_info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    new_texture = SDL_CreateGPUTexture(demo->device, &texture_info);
    if (!new_texture) {
        return false;
    }
    if (*texture) {
        SDL_ReleaseGPUTexture(demo->device, *texture);
    }
    *texture = new_texture;
    *texture_width = width;
    *texture_height = height;
    return true;
}

bool ForgeGpuEnsureSampledColorTargetSlots(
    ForgeGpuDemo *demo,
    const ForgeGpuSampledColorTargetSlot *slots,
    Uint32 num_slots,
    Uint32 width,
    Uint32 height)
{
    if (!slots && num_slots > 0) {
        SDL_SetError("sampled color target slots are missing");
        return false;
    }

    for (Uint32 i = 0; i < num_slots; i += 1) {
        const ForgeGpuSampledColorTargetSlot *slot = &slots[i];

        if (!ForgeGpuEnsureSampledColorTarget(
                demo,
                slot->texture,
                slot->width,
                slot->height,
                width,
                height,
                slot->format)) {
            return false;
        }
    }
    return true;
}

SDL_GPUTexture *ForgeGpuCreateSampledDepthTexture(
    ForgeGpuDemo *demo,
    Uint32 width,
    Uint32 height,
    SDL_GPUTextureFormat format)
{
    SDL_GPUTextureCreateInfo texture_info;

    SDL_zero(texture_info);
    texture_info.type = SDL_GPU_TEXTURETYPE_2D;
    texture_info.format = format;
    texture_info.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    texture_info.width = width;
    texture_info.height = height;
    texture_info.layer_count_or_depth = 1;
    texture_info.num_levels = 1;
    texture_info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    return SDL_CreateGPUTexture(demo->device, &texture_info);
}

SDL_GPURenderPass *ForgeGpuBeginDepthOnlyPass(
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPUTexture *depth_texture,
    float clear_depth)
{
    SDL_GPUDepthStencilTargetInfo depth_target;

    SDL_zero(depth_target);
    depth_target.texture = depth_texture;
    depth_target.load_op = SDL_GPU_LOADOP_CLEAR;
    depth_target.store_op = SDL_GPU_STOREOP_STORE;
    depth_target.clear_depth = clear_depth;
    return SDL_BeginGPURenderPass(command_buffer, nullptr, 0, &depth_target);
}

SDL_GPURenderPass *ForgeGpuBeginColorDepthPass(
    SDL_GPUCommandBuffer *command_buffer,
    const ForgeGpuColorTargetAttachment *color_targets,
    Uint32 num_color_targets,
    SDL_GPUTexture *depth_texture,
    float clear_depth)
{
    SDL_GPUColorTargetInfo color_target_infos[FORGE_GPU_MAX_SHARED_COLOR_TARGETS];
    SDL_GPUDepthStencilTargetInfo depth_target;

    if (num_color_targets > FORGE_GPU_MAX_SHARED_COLOR_TARGETS) {
        SDL_SetError("too many shared color targets");
        return nullptr;
    }
    if (!color_targets && num_color_targets > 0) {
        SDL_SetError("shared color targets are missing");
        return nullptr;
    }

    SDL_zeroa(color_target_infos);
    for (Uint32 i = 0; i < num_color_targets; i += 1) {
        color_target_infos[i].texture = color_targets[i].texture;
        color_target_infos[i].load_op = SDL_GPU_LOADOP_CLEAR;
        color_target_infos[i].store_op = SDL_GPU_STOREOP_STORE;
        color_target_infos[i].clear_color = color_targets[i].clear_color;
    }

    SDL_zero(depth_target);
    depth_target.texture = depth_texture;
    depth_target.load_op = SDL_GPU_LOADOP_CLEAR;
    depth_target.store_op = SDL_GPU_STOREOP_STORE;
    depth_target.clear_depth = clear_depth;

    return SDL_BeginGPURenderPass(command_buffer, color_target_infos, num_color_targets, &depth_target);
}

SDL_GPUGraphicsPipeline *ForgeGpuCreateLessonGraphicsPipeline(
    ForgeGpuDemo *demo,
    SDL_GPUShader *vertex_shader,
    SDL_GPUShader *fragment_shader,
    const SDL_GPUVertexBufferDescription *vertex_buffers,
    Uint32 num_vertex_buffers,
    const SDL_GPUVertexAttribute *vertex_attributes,
    Uint32 num_vertex_attributes,
    Uint32 num_color_targets,
    bool has_depth_target,
    SDL_GPUTextureFormat depth_format,
    bool depth_test,
    bool depth_write,
    SDL_GPUCullMode cull_mode,
    float depth_bias_constant,
    float depth_bias_slope)
{
    return ForgeGpuCreateLessonGraphicsPipelineWithPrimitive(
        demo,
        vertex_shader,
        fragment_shader,
        SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        vertex_buffers,
        num_vertex_buffers,
        vertex_attributes,
        num_vertex_attributes,
        num_color_targets,
        has_depth_target,
        depth_format,
        depth_test,
        depth_write,
        cull_mode,
        depth_bias_constant,
        depth_bias_slope);
}

SDL_GPUGraphicsPipeline *ForgeGpuCreateLessonGraphicsPipelineWithPrimitive(
    ForgeGpuDemo *demo,
    SDL_GPUShader *vertex_shader,
    SDL_GPUShader *fragment_shader,
    SDL_GPUPrimitiveType primitive_type,
    const SDL_GPUVertexBufferDescription *vertex_buffers,
    Uint32 num_vertex_buffers,
    const SDL_GPUVertexAttribute *vertex_attributes,
    Uint32 num_vertex_attributes,
    Uint32 num_color_targets,
    bool has_depth_target,
    SDL_GPUTextureFormat depth_format,
    bool depth_test,
    bool depth_write,
    SDL_GPUCullMode cull_mode,
    float depth_bias_constant,
    float depth_bias_slope)
{
    return ForgeGpuCreateLessonGraphicsPipelineWithColorFormat(
        demo,
        vertex_shader,
        fragment_shader,
        primitive_type,
        demo->color_format,
        vertex_buffers,
        num_vertex_buffers,
        vertex_attributes,
        num_vertex_attributes,
        num_color_targets,
        has_depth_target,
        depth_format,
        depth_test,
        depth_write,
        cull_mode,
        depth_bias_constant,
        depth_bias_slope);
}

SDL_GPUGraphicsPipeline *ForgeGpuCreateLessonGraphicsPipelineWithColorFormat(
    ForgeGpuDemo *demo,
    SDL_GPUShader *vertex_shader,
    SDL_GPUShader *fragment_shader,
    SDL_GPUPrimitiveType primitive_type,
    SDL_GPUTextureFormat color_format,
    const SDL_GPUVertexBufferDescription *vertex_buffers,
    Uint32 num_vertex_buffers,
    const SDL_GPUVertexAttribute *vertex_attributes,
    Uint32 num_vertex_attributes,
    Uint32 num_color_targets,
    bool has_depth_target,
    SDL_GPUTextureFormat depth_format,
    bool depth_test,
    bool depth_write,
    SDL_GPUCullMode cull_mode,
    float depth_bias_constant,
    float depth_bias_slope)
{
    SDL_GPUColorTargetDescription *color_target_descriptions = nullptr;
    SDL_GPUGraphicsPipeline *pipeline;

    if (num_color_targets > 0) {
        color_target_descriptions = (SDL_GPUColorTargetDescription *)SDL_calloc(num_color_targets, sizeof(*color_target_descriptions));
        if (!color_target_descriptions) {
            SDL_OutOfMemory();
            return nullptr;
        }
        for (Uint32 i = 0; i < num_color_targets; i += 1) {
            color_target_descriptions[i].format = color_format;
        }
    }

    pipeline = ForgeGpuCreateLessonGraphicsPipelineWithColorTargets(
        demo,
        vertex_shader,
        fragment_shader,
        primitive_type,
        color_target_descriptions,
        num_color_targets,
        vertex_buffers,
        num_vertex_buffers,
        vertex_attributes,
        num_vertex_attributes,
        has_depth_target,
        depth_format,
        depth_test,
        depth_write,
        cull_mode,
        depth_bias_constant,
        depth_bias_slope);

    SDL_free(color_target_descriptions);
    return pipeline;
}

SDL_GPUGraphicsPipeline *ForgeGpuCreateLessonGraphicsPipelineWithColorTargets(
    ForgeGpuDemo *demo,
    SDL_GPUShader *vertex_shader,
    SDL_GPUShader *fragment_shader,
    SDL_GPUPrimitiveType primitive_type,
    const SDL_GPUColorTargetDescription *color_target_descriptions,
    Uint32 num_color_targets,
    const SDL_GPUVertexBufferDescription *vertex_buffers,
    Uint32 num_vertex_buffers,
    const SDL_GPUVertexAttribute *vertex_attributes,
    Uint32 num_vertex_attributes,
    bool has_depth_target,
    SDL_GPUTextureFormat depth_format,
    bool depth_test,
    bool depth_write,
    SDL_GPUCullMode cull_mode,
    float depth_bias_constant,
    float depth_bias_slope)
{
    return ForgeGpuCreateLessonGraphicsPipelineWithColorTargetsAndDepthCompare(
        demo,
        vertex_shader,
        fragment_shader,
        primitive_type,
        color_target_descriptions,
        num_color_targets,
        vertex_buffers,
        num_vertex_buffers,
        vertex_attributes,
        num_vertex_attributes,
        has_depth_target,
        depth_format,
        depth_test,
        depth_write,
        SDL_GPU_COMPAREOP_LESS_OR_EQUAL,
        cull_mode,
        depth_bias_constant,
        depth_bias_slope);
}

SDL_GPUGraphicsPipeline *ForgeGpuCreateLessonGraphicsPipelineWithColorTargetsAndDepthCompare(
    ForgeGpuDemo *demo,
    SDL_GPUShader *vertex_shader,
    SDL_GPUShader *fragment_shader,
    SDL_GPUPrimitiveType primitive_type,
    const SDL_GPUColorTargetDescription *color_target_descriptions,
    Uint32 num_color_targets,
    const SDL_GPUVertexBufferDescription *vertex_buffers,
    Uint32 num_vertex_buffers,
    const SDL_GPUVertexAttribute *vertex_attributes,
    Uint32 num_vertex_attributes,
    bool has_depth_target,
    SDL_GPUTextureFormat depth_format,
    bool depth_test,
    bool depth_write,
    SDL_GPUCompareOp depth_compare_op,
    SDL_GPUCullMode cull_mode,
    float depth_bias_constant,
    float depth_bias_slope)
{
    SDL_GPUGraphicsPipelineCreateInfo pipeline_info;

    if (num_color_targets > 0 && !color_target_descriptions) {
        SDL_SetError("color target descriptions are required");
        return nullptr;
    }

    SDL_zero(pipeline_info);
    pipeline_info.vertex_shader = vertex_shader;
    pipeline_info.fragment_shader = fragment_shader;
    pipeline_info.primitive_type = primitive_type;
    pipeline_info.vertex_input_state.vertex_buffer_descriptions = vertex_buffers;
    pipeline_info.vertex_input_state.num_vertex_buffers = num_vertex_buffers;
    pipeline_info.vertex_input_state.vertex_attributes = vertex_attributes;
    pipeline_info.vertex_input_state.num_vertex_attributes = num_vertex_attributes;
    pipeline_info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    pipeline_info.rasterizer_state.cull_mode = cull_mode;
    pipeline_info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    pipeline_info.rasterizer_state.depth_bias_constant_factor = depth_bias_constant;
    pipeline_info.rasterizer_state.depth_bias_slope_factor = depth_bias_slope;
    pipeline_info.rasterizer_state.enable_depth_bias = depth_bias_constant != 0.0f || depth_bias_slope != 0.0f;
    pipeline_info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    pipeline_info.depth_stencil_state.compare_op = depth_compare_op;
    pipeline_info.depth_stencil_state.enable_depth_test = depth_test;
    pipeline_info.depth_stencil_state.enable_depth_write = depth_write;
    pipeline_info.target_info.color_target_descriptions = color_target_descriptions;
    pipeline_info.target_info.num_color_targets = num_color_targets;
    pipeline_info.target_info.has_depth_stencil_target = has_depth_target;
    pipeline_info.target_info.depth_stencil_format = depth_format;

    return SDL_CreateGPUGraphicsPipeline(demo->device, &pipeline_info);
}

SDL_GPUGraphicsPipeline *ForgeGpuCreateFullscreenPostprocessPipeline(
    ForgeGpuDemo *demo,
    SDL_GPUShader *vertex_shader,
    SDL_GPUShader *fragment_shader,
    SDL_GPUTextureFormat color_format,
    bool additive_blend)
{
    SDL_GPUColorTargetDescription color_target_description;
    SDL_GPUGraphicsPipelineCreateInfo pipeline_info;

    SDL_zero(color_target_description);
    color_target_description.format = color_format;
    if (additive_blend) {
        color_target_description.blend_state.enable_blend = true;
        color_target_description.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        color_target_description.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        color_target_description.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
        color_target_description.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        color_target_description.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        color_target_description.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    }

    SDL_zero(pipeline_info);
    pipeline_info.vertex_shader = vertex_shader;
    pipeline_info.fragment_shader = fragment_shader;
    pipeline_info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pipeline_info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    pipeline_info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    pipeline_info.target_info.color_target_descriptions = &color_target_description;
    pipeline_info.target_info.num_color_targets = 1;
    return SDL_CreateGPUGraphicsPipeline(demo->device, &pipeline_info);
}

bool ForgeGpuRunFullscreenPostprocessPass(
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPUTexture *target_texture,
    SDL_GPULoadOp load_op,
    SDL_FColor clear_color,
    SDL_GPUGraphicsPipeline *pipeline,
    const SDL_GPUTextureSamplerBinding *fragment_samplers,
    Uint32 num_fragment_samplers,
    const void *fragment_uniform_data,
    Uint32 fragment_uniform_size,
    Uint32 vertex_count)
{
    SDL_GPUColorTargetInfo color_target;
    SDL_GPURenderPass *render_pass;

    if (!target_texture || !pipeline) {
        SDL_SetError("fullscreen postprocess pass is missing target or pipeline");
        return false;
    }

    SDL_zero(color_target);
    color_target.texture = target_texture;
    color_target.load_op = load_op;
    color_target.store_op = SDL_GPU_STOREOP_STORE;
    color_target.clear_color = clear_color;

    render_pass = SDL_BeginGPURenderPass(command_buffer, &color_target, 1, nullptr);
    if (!render_pass) {
        return false;
    }

    SDL_BindGPUGraphicsPipeline(render_pass, pipeline);
    if (fragment_samplers && num_fragment_samplers > 0) {
        SDL_BindGPUFragmentSamplers(render_pass, 0, fragment_samplers, num_fragment_samplers);
    }
    if (fragment_uniform_data && fragment_uniform_size > 0) {
        SDL_PushGPUFragmentUniformData(command_buffer, 0, fragment_uniform_data, fragment_uniform_size);
    }
    SDL_DrawGPUPrimitives(render_pass, vertex_count, 1, 0, 0);
    SDL_EndGPURenderPass(render_pass);
    return true;
}

typedef struct ForgeGpuBloomDownsampleUniforms
{
    float texel_size[2];
    float threshold;
    float use_karis;
} ForgeGpuBloomDownsampleUniforms;

typedef struct ForgeGpuBloomUpsampleUniforms
{
    float texel_size[2];
    float pad[2];
} ForgeGpuBloomUpsampleUniforms;

void ForgeGpuReleaseBloomChain(ForgeGpuDemo *demo, ForgeGpuBloomChain *chain)
{
    if (!demo || !chain) {
        return;
    }
    for (int i = 0; i < FORGE_GPU_BLOOM_MIP_COUNT; i += 1) {
        if (chain->mips[i]) {
            SDL_ReleaseGPUTexture(demo->device, chain->mips[i]);
            chain->mips[i] = nullptr;
        }
        chain->widths[i] = 0;
        chain->heights[i] = 0;
    }
}

bool ForgeGpuEnsureBloomChain(
    ForgeGpuDemo *demo,
    ForgeGpuBloomChain *chain,
    SDL_GPUTexture *hdr_target,
    Uint32 hdr_width,
    Uint32 hdr_height,
    SDL_GPUTextureFormat format)
{
    SDL_GPUTexture *new_mips[FORGE_GPU_BLOOM_MIP_COUNT];
    Uint32 new_widths[FORGE_GPU_BLOOM_MIP_COUNT];
    Uint32 new_heights[FORGE_GPU_BLOOM_MIP_COUNT];
    Uint32 width;
    Uint32 height;

    if (!chain || !hdr_target) {
        SDL_SetError("bloom chain is missing its HDR source");
        return false;
    }
    if (chain->mips[0] &&
        chain->widths[0] == SDL_max(1u, hdr_width / 2u) &&
        chain->heights[0] == SDL_max(1u, hdr_height / 2u)) {
        return true;
    }

    SDL_zeroa(new_mips);
    SDL_zeroa(new_widths);
    SDL_zeroa(new_heights);
    width = hdr_width / 2u;
    height = hdr_height / 2u;

    for (int i = 0; i < FORGE_GPU_BLOOM_MIP_COUNT; i += 1) {
        width = SDL_max(1u, width);
        height = SDL_max(1u, height);

        if (!ForgeGpuEnsureSampledColorTarget(
                demo,
                &new_mips[i],
                &new_widths[i],
                &new_heights[i],
                width,
                height,
                format)) {
            for (int j = 0; j < FORGE_GPU_BLOOM_MIP_COUNT; j += 1) {
                if (new_mips[j]) {
                    SDL_ReleaseGPUTexture(demo->device, new_mips[j]);
                }
            }
            return false;
        }
        width /= 2u;
        height /= 2u;
    }

    ForgeGpuReleaseBloomChain(demo, chain);
    for (int i = 0; i < FORGE_GPU_BLOOM_MIP_COUNT; i += 1) {
        chain->mips[i] = new_mips[i];
        chain->widths[i] = new_widths[i];
        chain->heights[i] = new_heights[i];
    }
    return true;
}

bool ForgeGpuRunBloomChain(
    SDL_GPUCommandBuffer *command_buffer,
    ForgeGpuBloomChain *chain,
    SDL_GPUTexture *hdr_target,
    Uint32 hdr_width,
    Uint32 hdr_height,
    SDL_GPUGraphicsPipeline *downsample_pipeline,
    SDL_GPUGraphicsPipeline *upsample_pipeline,
    SDL_GPUSampler *bloom_sampler,
    float threshold,
    Uint32 vertex_count)
{
    if (!chain || !hdr_target || !downsample_pipeline || !upsample_pipeline || !bloom_sampler) {
        SDL_SetError("bloom pass is missing required resources");
        return false;
    }

    for (int i = 0; i < FORGE_GPU_BLOOM_MIP_COUNT; i += 1) {
        SDL_GPUTextureSamplerBinding src_binding;
        ForgeGpuBloomDownsampleUniforms uniforms;

        SDL_zero(src_binding);
        src_binding.texture = (i == 0) ? hdr_target : chain->mips[i - 1];
        src_binding.sampler = bloom_sampler;

        SDL_zero(uniforms);
        if (i == 0) {
            uniforms.texel_size[0] = 1.0f / (float)hdr_width;
            uniforms.texel_size[1] = 1.0f / (float)hdr_height;
        } else {
            uniforms.texel_size[0] = 1.0f / (float)chain->widths[i - 1];
            uniforms.texel_size[1] = 1.0f / (float)chain->heights[i - 1];
        }
        uniforms.threshold = threshold;
        uniforms.use_karis = (i == 0) ? 1.0f : 0.0f;
        if (!ForgeGpuRunFullscreenPostprocessPass(
                command_buffer,
                chain->mips[i],
                SDL_GPU_LOADOP_CLEAR,
                { 0.0f, 0.0f, 0.0f, 1.0f },
                downsample_pipeline,
                &src_binding,
                1,
                &uniforms,
                sizeof(uniforms),
                vertex_count)) {
            return false;
        }
    }

    for (int i = FORGE_GPU_BLOOM_MIP_COUNT - 2; i >= 0; i -= 1) {
        SDL_GPUTextureSamplerBinding src_binding;
        ForgeGpuBloomUpsampleUniforms uniforms;

        SDL_zero(src_binding);
        src_binding.texture = chain->mips[i + 1];
        src_binding.sampler = bloom_sampler;

        SDL_zero(uniforms);
        uniforms.texel_size[0] = 1.0f / (float)chain->widths[i + 1];
        uniforms.texel_size[1] = 1.0f / (float)chain->heights[i + 1];
        if (!ForgeGpuRunFullscreenPostprocessPass(
                command_buffer,
                chain->mips[i],
                SDL_GPU_LOADOP_LOAD,
                { 0.0f, 0.0f, 0.0f, 1.0f },
                upsample_pipeline,
                &src_binding,
                1,
                &uniforms,
                sizeof(uniforms),
                vertex_count)) {
            return false;
        }
    }
    return true;
}

bool ForgeGpuRunHdrBloomTonemapPass(
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPUTexture *swapchain_texture,
    SDL_GPUTexture *hdr_target,
    SDL_GPUSampler *hdr_sampler,
    const ForgeGpuBloomChain *chain,
    SDL_GPUSampler *bloom_sampler,
    SDL_GPUGraphicsPipeline *tonemap_pipeline,
    const void *fragment_uniform_data,
    Uint32 fragment_uniform_size,
    Uint32 vertex_count)
{
    SDL_GPUTextureSamplerBinding texture_bindings[2];

    if (!chain || !chain->mips[0] || !hdr_target || !hdr_sampler || !bloom_sampler) {
        SDL_SetError("HDR bloom tonemap pass is missing required resources");
        return false;
    }

    SDL_zeroa(texture_bindings);
    texture_bindings[0].texture = hdr_target;
    texture_bindings[0].sampler = hdr_sampler;
    texture_bindings[1].texture = chain->mips[0];
    texture_bindings[1].sampler = bloom_sampler;

    return ForgeGpuRunFullscreenPostprocessPass(
        command_buffer,
        swapchain_texture,
        SDL_GPU_LOADOP_DONT_CARE,
        { 0.0f, 0.0f, 0.0f, 1.0f },
        tonemap_pipeline,
        texture_bindings,
        SDL_arraysize(texture_bindings),
        fragment_uniform_data,
        fragment_uniform_size,
        vertex_count);
}

void ForgeGpuComputeCascadeSplits(float near_plane, float far_plane, float splits[FORGE_GPU_SHADOW_CASCADE_COUNT])
{
    for (int i = 0; i < FORGE_GPU_SHADOW_CASCADE_COUNT; i += 1) {
        const float p = (float)(i + 1) / (float)FORGE_GPU_SHADOW_CASCADE_COUNT;
        const float log_split = near_plane * SDL_powf(far_plane / near_plane, p);
        const float uniform_split = near_plane + (far_plane - near_plane) * p;
        splits[i] = 0.5f * log_split + 0.5f * uniform_split;
    }
}

Mat4 ForgeGpuComputeCascadeLightViewProjection(
    Mat4 inv_cam_vp,
    float split_near,
    float split_far,
    float cam_near,
    float cam_far,
    Vec3 light_dir)
{
    const Vec4 ndc_corners[8] = {
        { -1.0f, -1.0f, 0.0f, 1.0f },
        {  1.0f, -1.0f, 0.0f, 1.0f },
        {  1.0f,  1.0f, 0.0f, 1.0f },
        { -1.0f,  1.0f, 0.0f, 1.0f },
        { -1.0f, -1.0f, 1.0f, 1.0f },
        {  1.0f, -1.0f, 1.0f, 1.0f },
        {  1.0f,  1.0f, 1.0f, 1.0f },
        { -1.0f,  1.0f, 1.0f, 1.0f }
    };
    Vec3 world_corners[8];
    Vec3 cascade_corners[8];
    Vec3 center = { 0.0f, 0.0f, 0.0f };
    const float t_near = (split_near - cam_near) / (cam_far - cam_near);
    const float t_far = (split_far - cam_near) / (cam_far - cam_near);

    for (int i = 0; i < 8; i += 1) {
        world_corners[i] = vec3_perspective_divide(mat4_multiply_vec4(inv_cam_vp, ndc_corners[i]));
    }
    for (int i = 0; i < 4; i += 1) {
        cascade_corners[i] = vec3_lerp(world_corners[i], world_corners[i + 4], t_near);
        cascade_corners[i + 4] = vec3_lerp(world_corners[i], world_corners[i + 4], t_far);
    }
    for (int i = 0; i < 8; i += 1) {
        center = vec3_add(center, cascade_corners[i]);
    }
    center = vec3_scale(center, 1.0f / 8.0f);

    {
        const Vec3 light_pos = vec3_add(center, vec3_scale(light_dir, 50.0f));
        const Mat4 light_view = mat4_look_at(light_pos, center, { 0.0f, 1.0f, 0.0f });
        float min_x = 1e30f;
        float min_y = 1e30f;
        float min_z = 1e30f;
        float max_x = -1e30f;
        float max_y = -1e30f;
        float max_z = -1e30f;

        for (int i = 0; i < 8; i += 1) {
            const Vec4 light_space = mat4_multiply_vec4(
                light_view,
                { cascade_corners[i].x, cascade_corners[i].y, cascade_corners[i].z, 1.0f });
            min_x = SDL_min(min_x, light_space.x);
            min_y = SDL_min(min_y, light_space.y);
            min_z = SDL_min(min_z, light_space.z);
            max_x = SDL_max(max_x, light_space.x);
            max_y = SDL_max(max_y, light_space.y);
            max_z = SDL_max(max_z, light_space.z);
        }
        min_z -= 50.0f;
        return mat4_multiply(mat4_orthographic(min_x, max_x, min_y, max_y, -max_z, -min_z), light_view);
    }
}

Mat4 ForgeGpuComputeTargetedDirectionalLightViewProjection(
    Vec3 light_dir,
    float light_distance,
    Vec3 target,
    float ortho_size,
    float near_plane,
    float far_plane,
    float parallel_threshold)
{
    Vec3 normalized_light_dir = vec3_normalize(light_dir);
    Vec3 light_pos = vec3_scale(normalized_light_dir, -light_distance);
    Vec3 up = { 0.0f, 1.0f, 0.0f };

    if (SDL_fabsf(vec3_dot(normalized_light_dir, up)) > parallel_threshold) {
        up = { 0.0f, 0.0f, 1.0f };
    }

    return mat4_multiply(
        mat4_orthographic(
            -ortho_size,
            ortho_size,
            -ortho_size,
            ortho_size,
            near_plane,
            far_plane),
        mat4_look_at(light_pos, target, up));
}

void ForgeGpuComputeCascadeLightViewProjections(
    Mat4 inv_cam_vp,
    float cam_near,
    float cam_far,
    Vec3 light_dir,
    float splits[FORGE_GPU_SHADOW_CASCADE_COUNT],
    Mat4 light_vp[FORGE_GPU_SHADOW_CASCADE_COUNT])
{
    float prev_split = cam_near;

    for (int i = 0; i < FORGE_GPU_SHADOW_CASCADE_COUNT; i += 1) {
        light_vp[i] = ForgeGpuComputeCascadeLightViewProjection(
            inv_cam_vp,
            prev_split,
            splits[i],
            cam_near,
            cam_far,
            light_dir);
        prev_split = splits[i];
    }
}

bool ForgeGpuCreateGridBuffers(ForgeGpuDemo *demo)
{
    demo->lesson.vertex_buffer = ForgeGpuCreateBufferWithData(
        demo->device,
        SDL_GPU_BUFFERUSAGE_VERTEX,
        kForgeGpuGridVertices,
        sizeof(kForgeGpuGridVertices));
    demo->lesson.index_buffer = ForgeGpuCreateBufferWithData(
        demo->device,
        SDL_GPU_BUFFERUSAGE_INDEX,
        kForgeGpuGridIndices,
        sizeof(kForgeGpuGridIndices));
    return demo->lesson.vertex_buffer && demo->lesson.index_buffer;
}

void ForgeGpuFillMeshVertexInput(
    SDL_GPUVertexBufferDescription *vertex_buffer,
    SDL_GPUVertexAttribute attributes[3])
{
    SDL_zero(*vertex_buffer);
    vertex_buffer->slot = 0;
    vertex_buffer->pitch = sizeof(ForgeGpuMeshVertex);
    vertex_buffer->input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

    SDL_memset(attributes, 0, 3 * sizeof(*attributes));
    attributes[0].location = 0;
    attributes[0].buffer_slot = 0;
    attributes[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    attributes[0].offset = offsetof(ForgeGpuMeshVertex, position);
    attributes[1].location = 1;
    attributes[1].buffer_slot = 0;
    attributes[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    attributes[1].offset = offsetof(ForgeGpuMeshVertex, normal);
    attributes[2].location = 2;
    attributes[2].buffer_slot = 0;
    attributes[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    attributes[2].offset = offsetof(ForgeGpuMeshVertex, uv);
}

bool ForgeGpuCreateSphereMeshBuffers(
    ForgeGpuDemo *demo,
    float radius,
    int stacks,
    int slices,
    SDL_GPUBuffer **vertex_buffer,
    SDL_GPUBuffer **index_buffer,
    Uint32 *index_count)
{
    const int vertex_count = (stacks + 1) * (slices + 1);
    const int total_indices = stacks * slices * 6;
    ForgeGpuMeshVertex *vertices;
    Uint16 *indices;
    int vertex_index = 0;
    int index_index = 0;
    bool ok = false;

    if (!demo || !vertex_buffer || !index_buffer || !index_count || stacks <= 0 || slices <= 0 || radius <= 0.0f) {
        SDL_SetError("invalid sphere mesh parameters");
        return false;
    }
    if (vertex_count > SDL_MAX_UINT16) {
        SDL_SetError("sphere mesh has too many vertices for 16-bit indices");
        return false;
    }

    vertices = (ForgeGpuMeshVertex *)SDL_calloc((size_t)vertex_count, sizeof(*vertices));
    indices = (Uint16 *)SDL_calloc((size_t)total_indices, sizeof(*indices));
    if (!vertices || !indices) {
        SDL_free(vertices);
        SDL_free(indices);
        SDL_OutOfMemory();
        return false;
    }

    for (int stack = 0; stack <= stacks; stack += 1) {
        const float phi = FORGE_GPU_PI * (float)stack / (float)stacks;
        const float sin_phi = SDL_sinf(phi);
        const float cos_phi = SDL_cosf(phi);

        for (int slice = 0; slice <= slices; slice += 1) {
            const float theta = 2.0f * FORGE_GPU_PI * (float)slice / (float)slices;
            const float sin_theta = SDL_sinf(theta);
            const float cos_theta = SDL_cosf(theta);
            const float nx = sin_phi * cos_theta;
            const float ny = cos_phi;
            const float nz = sin_phi * sin_theta;
            ForgeGpuMeshVertex *vertex = &vertices[vertex_index++];

            vertex->position[0] = radius * nx;
            vertex->position[1] = radius * ny;
            vertex->position[2] = radius * nz;
            vertex->normal[0] = nx;
            vertex->normal[1] = ny;
            vertex->normal[2] = nz;
            vertex->uv[0] = (float)slice / (float)slices;
            vertex->uv[1] = (float)stack / (float)stacks;
        }
    }

    for (int stack = 0; stack < stacks; stack += 1) {
        for (int slice = 0; slice < slices; slice += 1) {
            const int top_left = stack * (slices + 1) + slice;
            const int top_right = top_left + 1;
            const int bottom_left = top_left + (slices + 1);
            const int bottom_right = bottom_left + 1;

            indices[index_index++] = (Uint16)top_left;
            indices[index_index++] = (Uint16)bottom_left;
            indices[index_index++] = (Uint16)top_right;
            indices[index_index++] = (Uint16)top_right;
            indices[index_index++] = (Uint16)bottom_left;
            indices[index_index++] = (Uint16)bottom_right;
        }
    }

    *vertex_buffer = ForgeGpuCreateBufferWithData(
        demo->device,
        SDL_GPU_BUFFERUSAGE_VERTEX,
        vertices,
        (Uint32)((size_t)vertex_count * sizeof(*vertices)));
    *index_buffer = ForgeGpuCreateBufferWithData(
        demo->device,
        SDL_GPU_BUFFERUSAGE_INDEX,
        indices,
        (Uint32)((size_t)total_indices * sizeof(*indices)));
    if (*vertex_buffer && *index_buffer) {
        *index_count = (Uint32)total_indices;
        ok = true;
    } else {
        if (*vertex_buffer) {
            SDL_ReleaseGPUBuffer(demo->device, *vertex_buffer);
            *vertex_buffer = nullptr;
        }
        if (*index_buffer) {
            SDL_ReleaseGPUBuffer(demo->device, *index_buffer);
            *index_buffer = nullptr;
        }
        *index_count = 0;
    }

    SDL_free(vertices);
    SDL_free(indices);
    return ok;
}

bool ForgeGpuLoadLessonSceneWithRequirements(
    ForgeGpuDemo *demo,
    const char *relative_path,
    const ForgeGpuSceneLoadRequirements *requirements)
{
    char path[FORGE_GPU_MAX_PATH];

    if (!ForgeGpuJoinAssetPath(demo, relative_path, path, sizeof(path))) {
        SDL_SetError("forge-gpu scene asset path too long");
        return false;
    }
    if (!ForgeGpuLoadGltfSceneWithRequirements(path, requirements, &demo->lesson.scene)) {
        return false;
    }
    if (!ForgeGpuUploadLoadedSceneToGpu(demo)) {
        ForgeGpuFreeGpuScene(demo);
        return false;
    }
    return true;
}

bool ForgeGpuLoadLessonScene(ForgeGpuDemo *demo, const char *relative_path)
{
    return ForgeGpuLoadLessonSceneWithRequirements(demo, relative_path, nullptr);
}

bool ForgeGpuLoadSceneModelWithRequirements(
    ForgeGpuDemo *demo,
    GpuSceneData *model,
    const char *relative_path,
    const ForgeGpuSceneLoadRequirements *requirements)
{
    char path[FORGE_GPU_MAX_PATH];

    if (!ForgeGpuJoinAssetPath(demo, relative_path, path, sizeof(path))) {
        SDL_SetError("forge-gpu model asset path too long");
        return false;
    }
    if (!ForgeGpuLoadGltfSceneWithRequirements(path, requirements, &model->loaded)) {
        return false;
    }
    if (!ForgeGpuUploadSceneDataToGpu(demo, model)) {
        ForgeGpuFreeSceneData(demo, model);
        return false;
    }
    return true;
}

bool ForgeGpuLoadSceneModel(ForgeGpuDemo *demo, GpuSceneData *model, const char *relative_path)
{
    return ForgeGpuLoadSceneModelWithRequirements(demo, model, relative_path, nullptr);
}

const GpuMaterial *ForgeGpuModelMaterialOrDefault(const GpuSceneData *model, int material_index, GpuMaterial *fallback)
{
    if (material_index >= 0 && material_index < model->material_count) {
        return &model->materials[material_index];
    }

    SDL_zero(*fallback);
    fallback->base_color[0] = 1.0f;
    fallback->base_color[1] = 1.0f;
    fallback->base_color[2] = 1.0f;
    fallback->base_color[3] = 1.0f;
    fallback->normal_scale = 1.0f;
    fallback->metallic_factor = 1.0f;
    fallback->roughness_factor = 1.0f;
    fallback->occlusion_strength = 1.0f;
    fallback->alpha_cutoff = 0.5f;
    fallback->alpha_mode = FORGE_GPU_SCENE_ALPHA_OPAQUE;
    return fallback;
}

const GpuMaterial *ForgeGpuSceneMaterialOrDefault(const LessonState *lesson, int material_index, GpuMaterial *fallback)
{
    if (material_index >= 0 && material_index < lesson->gpu_material_count) {
        return &lesson->gpu_materials[material_index];
    }

    SDL_zero(*fallback);
    fallback->base_color[0] = 1.0f;
    fallback->base_color[1] = 1.0f;
    fallback->base_color[2] = 1.0f;
    fallback->base_color[3] = 1.0f;
    fallback->normal_scale = 1.0f;
    fallback->metallic_factor = 1.0f;
    fallback->roughness_factor = 1.0f;
    fallback->occlusion_strength = 1.0f;
    fallback->alpha_cutoff = 0.5f;
    fallback->alpha_mode = FORGE_GPU_SCENE_ALPHA_OPAQUE;
    return fallback;
}

void ForgeGpuFillGridFragmentUniforms(GridFragUniforms *uniforms, const Vec3 *light_dir, const Vec3 *eye_pos)
{
    uniforms->line_color[0] = 0.068f;
    uniforms->line_color[1] = 0.534f;
    uniforms->line_color[2] = 0.932f;
    uniforms->line_color[3] = 1.0f;
    uniforms->bg_color[0] = 0.014f;
    uniforms->bg_color[1] = 0.014f;
    uniforms->bg_color[2] = 0.045f;
    uniforms->bg_color[3] = 1.0f;
    uniforms->light_dir[0] = light_dir->x;
    uniforms->light_dir[1] = light_dir->y;
    uniforms->light_dir[2] = light_dir->z;
    uniforms->light_dir[3] = 0.0f;
    uniforms->eye_pos[0] = eye_pos->x;
    uniforms->eye_pos[1] = eye_pos->y;
    uniforms->eye_pos[2] = eye_pos->z;
    uniforms->eye_pos[3] = 0.0f;
    uniforms->grid_spacing = 1.0f;
    uniforms->line_width = 0.02f;
    uniforms->fade_distance = 40.0f;
    uniforms->ambient = 0.3f;
    uniforms->shininess = 32.0f;
    uniforms->specular_str = 0.2f;
    uniforms->pad0 = 0.0f;
    uniforms->pad1 = 0.0f;
}

void ForgeGpuDrawBasicGrid(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    SDL_GPUGraphicsPipeline *pipeline,
    Mat4 vp,
    const Vec3 *light_dir)
{
    SDL_GPUBufferBinding vertex_binding;
    SDL_GPUBufferBinding index_binding;
    GridFragUniforms fragment_uniforms;
    UniformMvp grid_uniforms;

    grid_uniforms.mvp = vp;
    SDL_PushGPUVertexUniformData(command_buffer, 0, &grid_uniforms, sizeof(grid_uniforms));
    ForgeGpuFillGridFragmentUniforms(&fragment_uniforms, light_dir, &demo->lesson.camera_position);
    SDL_PushGPUFragmentUniformData(command_buffer, 0, &fragment_uniforms, sizeof(fragment_uniforms));

    SDL_zero(vertex_binding);
    vertex_binding.buffer = demo->lesson.vertex_buffer;
    SDL_zero(index_binding);
    index_binding.buffer = demo->lesson.index_buffer;

    SDL_BindGPUGraphicsPipeline(render_pass, pipeline);
    SDL_BindGPUVertexBuffers(render_pass, 0, &vertex_binding, 1);
    SDL_BindGPUIndexBuffer(render_pass, &index_binding, SDL_GPU_INDEXELEMENTSIZE_16BIT);
    SDL_DrawGPUIndexedPrimitives(render_pass, 6, 1, 0, 0, 0);
}

void ForgeGpuRenderLoadedScene(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    Uint32 width,
    Uint32 height,
    SDL_GPUGraphicsPipeline *pipeline,
    bool lighting)
{
    LessonState *lesson = &demo->lesson;
    Mat4 view;
    Mat4 projection;
    Vec3 light_dir = vec3_normalize({ 1.0f, 1.0f, 1.0f });

    ForgeGpuCameraViewProjection(demo, width, height, 100.0f, &view, &projection);
    SDL_BindGPUGraphicsPipeline(render_pass, pipeline);

    for (int node_index = 0; node_index < lesson->scene.node_count; node_index += 1) {
        const ForgeGpuSceneNode *node = &lesson->scene.nodes[node_index];
        const ForgeGpuSceneMesh *mesh;
        Mat4 model;
        Mat4 mvp;

        if (node->mesh_index < 0 || node->mesh_index >= lesson->scene.mesh_count) {
            continue;
        }

        mesh = &lesson->scene.meshes[node->mesh_index];
        model = mat4_from_forge(node->world_transform);
        mvp = mat4_multiply(projection, mat4_multiply(view, model));

        for (int primitive_offset = 0; primitive_offset < mesh->primitive_count; primitive_offset += 1) {
            const int primitive_index = mesh->first_primitive + primitive_offset;
            GpuMaterial fallback_material;
            const GpuPrimitive *primitive;
            const GpuMaterial *material;
            SDL_GPUBufferBinding vertex_binding;
            SDL_GPUTextureSamplerBinding sampler_binding;

            if (primitive_index < 0 || primitive_index >= lesson->gpu_primitive_count) {
                continue;
            }

            primitive = &lesson->gpu_primitives[primitive_index];
            material = ForgeGpuSceneMaterialOrDefault(lesson, primitive->material_index, &fallback_material);

            SDL_zero(vertex_binding);
            vertex_binding.buffer = primitive->vertex_buffer;
            SDL_zero(sampler_binding);
            sampler_binding.texture = material->has_texture ? material->texture : lesson->white_texture;
            sampler_binding.sampler = lesson->samplers[0];

            if (lighting) {
                UniformMvpModel vertex_uniforms;
                FragLightingUniforms fragment_uniforms;

                vertex_uniforms.mvp = mvp;
                vertex_uniforms.model = model;
                SDL_PushGPUVertexUniformData(command_buffer, 0, &vertex_uniforms, sizeof(vertex_uniforms));

                SDL_memcpy(fragment_uniforms.base_color, material->base_color, sizeof(fragment_uniforms.base_color));
                fragment_uniforms.light_dir[0] = light_dir.x;
                fragment_uniforms.light_dir[1] = light_dir.y;
                fragment_uniforms.light_dir[2] = light_dir.z;
                fragment_uniforms.light_dir[3] = 0.0f;
                fragment_uniforms.eye_pos[0] = lesson->camera_position.x;
                fragment_uniforms.eye_pos[1] = lesson->camera_position.y;
                fragment_uniforms.eye_pos[2] = lesson->camera_position.z;
                fragment_uniforms.eye_pos[3] = 0.0f;
                fragment_uniforms.has_texture = material->has_texture ? 1u : 0u;
                fragment_uniforms.shininess = 64.0f;
                fragment_uniforms.ambient = 0.15f;
                fragment_uniforms.specular_str = 0.5f;
                SDL_PushGPUFragmentUniformData(command_buffer, 0, &fragment_uniforms, sizeof(fragment_uniforms));
            } else {
                UniformMvp vertex_uniforms;
                FragMaterialUniforms fragment_uniforms;

                vertex_uniforms.mvp = mvp;
                SDL_PushGPUVertexUniformData(command_buffer, 0, &vertex_uniforms, sizeof(vertex_uniforms));

                SDL_memcpy(fragment_uniforms.base_color, material->base_color, sizeof(fragment_uniforms.base_color));
                fragment_uniforms.has_texture = material->has_texture ? 1u : 0u;
                fragment_uniforms.pad0 = 0u;
                fragment_uniforms.pad1 = 0u;
                fragment_uniforms.pad2 = 0u;
                SDL_PushGPUFragmentUniformData(command_buffer, 0, &fragment_uniforms, sizeof(fragment_uniforms));
            }

            SDL_BindGPUVertexBuffers(render_pass, 0, &vertex_binding, 1);
            SDL_BindGPUFragmentSamplers(render_pass, 0, &sampler_binding, 1);
            if (primitive->index_buffer && primitive->index_count > 0) {
                SDL_GPUBufferBinding index_binding;

                SDL_zero(index_binding);
                index_binding.buffer = primitive->index_buffer;
                SDL_BindGPUIndexBuffer(render_pass, &index_binding, primitive->index_type);
                SDL_DrawGPUIndexedPrimitives(render_pass, primitive->index_count, 1, 0, 0, 0);
            } else {
                SDL_DrawGPUPrimitives(render_pass, primitive->vertex_count, 1, 0, 0);
            }
        }
    }
}
