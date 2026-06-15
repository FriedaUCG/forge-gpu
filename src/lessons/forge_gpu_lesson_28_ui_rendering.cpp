#include "forge_gpu_lessons.h"

#include "forge_gpu_browser_status.h"
#include "forge_gpu_gpu_helpers.h"
#include "forge_gpu_lesson_28_ui.h"
#include "forge_gpu_lesson_common.h"
#include "shaders/generated/forge_gpu_lesson_28_shaders.h"
#include "imgui.h"

#include <stddef.h>

#define LESSON28_INITIAL_VERTEX_BUFFER_SIZE (16384u * (Uint32)sizeof(ForgeGpuLesson28Vertex))
#define LESSON28_INITIAL_INDEX_BUFFER_SIZE (24576u * (Uint32)sizeof(Uint32))

struct Lesson28Uniforms
{
    Mat4 projection;
};

struct Lesson28State
{
    ForgeGpuLesson28Ui *ui;
    Uint32 vertex_buffer_size;
    Uint32 index_buffer_size;
    ForgeGpuLesson28Frame last_frame;
    bool text_input_started;
    bool rendered_frame;
};

static_assert(sizeof(ForgeGpuLesson28Vertex) == 32, "lesson 28 vertex size must match ForgeUiVertex");
static_assert(sizeof(Lesson28Uniforms) == 64, "lesson 28 vertex uniform size must match HLSL layout");

static Lesson28State *lesson28_state(ForgeGpuDemo *demo)
{
    return (Lesson28State *)demo->lesson.private_state;
}

static Uint32 lesson28_next_power_of_two(Uint32 value)
{
    if (value == 0) {
        return 1;
    }
    value -= 1;
    value |= value >> 1;
    value |= value >> 2;
    value |= value >> 4;
    value |= value >> 8;
    value |= value >> 16;
    return value + 1;
}

static Mat4 lesson28_ui_projection(float width, float height)
{
    Mat4 projection = {};
    if (width <= 0.0f) {
        width = 1.0f;
    }
    if (height <= 0.0f) {
        height = 1.0f;
    }
    projection.m[0] = 2.0f / width;
    projection.m[5] = -2.0f / height;
    projection.m[10] = 1.0f;
    projection.m[12] = -1.0f;
    projection.m[13] = 1.0f;
    projection.m[15] = 1.0f;
    return projection;
}

static bool lesson28_wait_for_upload(SDL_GPUDevice *device, SDL_GPUCommandBuffer *command_buffer)
{
    SDL_GPUFence *fence = SDL_SubmitGPUCommandBufferAndAcquireFence(command_buffer);
    bool ok;

    if (!fence) {
        return false;
    }
    ok = SDL_WaitForGPUFences(device, true, &fence, 1);
    SDL_ReleaseGPUFence(device, fence);
    return ok;
}

static bool lesson28_upload_atlas_texture(ForgeGpuDemo *demo, const Uint8 *pixels, Uint32 width, Uint32 height)
{
    SDL_GPUTransferBufferCreateInfo transfer_info;
    SDL_GPUTransferBuffer *transfer;
    SDL_GPUCommandBuffer *command_buffer;
    SDL_GPUCopyPass *copy_pass;
    SDL_GPUTextureTransferInfo source;
    SDL_GPUTextureRegion destination;
    void *mapped;
    const Uint32 byte_count = width * height;

    SDL_zero(transfer_info);
    transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transfer_info.size = byte_count;
    transfer = SDL_CreateGPUTransferBuffer(demo->device, &transfer_info);
    if (!transfer) {
        return false;
    }

    mapped = SDL_MapGPUTransferBuffer(demo->device, transfer, false);
    if (!mapped) {
        SDL_ReleaseGPUTransferBuffer(demo->device, transfer);
        return false;
    }
    SDL_memcpy(mapped, pixels, byte_count);
    SDL_UnmapGPUTransferBuffer(demo->device, transfer);

    command_buffer = SDL_AcquireGPUCommandBuffer(demo->device);
    if (!command_buffer) {
        SDL_ReleaseGPUTransferBuffer(demo->device, transfer);
        return false;
    }
    copy_pass = SDL_BeginGPUCopyPass(command_buffer);
    if (!copy_pass) {
        SDL_CancelGPUCommandBuffer(command_buffer);
        SDL_ReleaseGPUTransferBuffer(demo->device, transfer);
        return false;
    }

    SDL_zero(source);
    source.transfer_buffer = transfer;
    source.pixels_per_row = width;
    source.rows_per_layer = height;

    SDL_zero(destination);
    destination.texture = demo->lesson.texture;
    destination.w = width;
    destination.h = height;
    destination.d = 1;
    SDL_UploadToGPUTexture(copy_pass, &source, &destination, false);
    SDL_EndGPUCopyPass(copy_pass);

    if (!lesson28_wait_for_upload(demo->device, command_buffer)) {
        SDL_ReleaseGPUTransferBuffer(demo->device, transfer);
        return false;
    }

    SDL_ReleaseGPUTransferBuffer(demo->device, transfer);
    return true;
}

static bool lesson28_create_pipeline(ForgeGpuDemo *demo)
{
    SDL_GPUShader *vertex_shader;
    SDL_GPUShader *fragment_shader;
    SDL_GPUVertexBufferDescription vertex_buffer_desc;
    SDL_GPUVertexAttribute vertex_attributes[4];
    SDL_GPUColorTargetDescription color_target;
    SDL_GPUGraphicsPipelineCreateInfo pipeline_info;

    vertex_shader = ForgeGpuCreateShader(
        demo->device,
        SDL_GPU_SHADERSTAGE_VERTEX,
        lesson28_ui_vert_wgsl,
        lesson28_ui_vert_wgsl_size,
        lesson28_ui_vert_msl,
        lesson28_ui_vert_msl_size,
        0,
        0,
        0,
        1);
    fragment_shader = ForgeGpuCreateShader(
        demo->device,
        SDL_GPU_SHADERSTAGE_FRAGMENT,
        lesson28_ui_frag_wgsl,
        lesson28_ui_frag_wgsl_size,
        lesson28_ui_frag_msl,
        lesson28_ui_frag_msl_size,
        1,
        0,
        0,
        0);
    if (!vertex_shader || !fragment_shader) {
        if (vertex_shader) {
            SDL_ReleaseGPUShader(demo->device, vertex_shader);
        }
        if (fragment_shader) {
            SDL_ReleaseGPUShader(demo->device, fragment_shader);
        }
        return false;
    }

    SDL_zero(vertex_buffer_desc);
    vertex_buffer_desc.slot = 0;
    vertex_buffer_desc.pitch = sizeof(ForgeGpuLesson28Vertex);
    vertex_buffer_desc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

    SDL_zeroa(vertex_attributes);
    vertex_attributes[0].location = 0;
    vertex_attributes[0].buffer_slot = 0;
    vertex_attributes[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    vertex_attributes[0].offset = offsetof(ForgeGpuLesson28Vertex, pos_x);
    vertex_attributes[1].location = 1;
    vertex_attributes[1].buffer_slot = 0;
    vertex_attributes[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    vertex_attributes[1].offset = offsetof(ForgeGpuLesson28Vertex, uv_u);
    vertex_attributes[2].location = 2;
    vertex_attributes[2].buffer_slot = 0;
    vertex_attributes[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    vertex_attributes[2].offset = offsetof(ForgeGpuLesson28Vertex, r);
    vertex_attributes[3].location = 3;
    vertex_attributes[3].buffer_slot = 0;
    vertex_attributes[3].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    vertex_attributes[3].offset = offsetof(ForgeGpuLesson28Vertex, b);

    SDL_zero(color_target);
    color_target.format = demo->color_format;
    color_target.blend_state.enable_blend = true;
    color_target.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    color_target.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    color_target.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
    color_target.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    color_target.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    color_target.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    color_target.blend_state.color_write_mask =
        SDL_GPU_COLORCOMPONENT_R | SDL_GPU_COLORCOMPONENT_G |
        SDL_GPU_COLORCOMPONENT_B | SDL_GPU_COLORCOMPONENT_A;

    SDL_zero(pipeline_info);
    pipeline_info.vertex_shader = vertex_shader;
    pipeline_info.fragment_shader = fragment_shader;
    pipeline_info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pipeline_info.vertex_input_state.vertex_buffer_descriptions = &vertex_buffer_desc;
    pipeline_info.vertex_input_state.num_vertex_buffers = 1;
    pipeline_info.vertex_input_state.vertex_attributes = vertex_attributes;
    pipeline_info.vertex_input_state.num_vertex_attributes = SDL_arraysize(vertex_attributes);
    pipeline_info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    pipeline_info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    pipeline_info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    pipeline_info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    pipeline_info.depth_stencil_state.enable_depth_test = false;
    pipeline_info.depth_stencil_state.enable_depth_write = false;
    pipeline_info.target_info.color_target_descriptions = &color_target;
    pipeline_info.target_info.num_color_targets = 1;
    pipeline_info.target_info.has_depth_stencil_target = false;

    demo->lesson.pipeline = SDL_CreateGPUGraphicsPipeline(demo->device, &pipeline_info);
    SDL_ReleaseGPUShader(demo->device, vertex_shader);
    SDL_ReleaseGPUShader(demo->device, fragment_shader);
    return demo->lesson.pipeline != nullptr;
}

static SDL_GPUBuffer *lesson28_create_buffer(SDL_GPUDevice *device, SDL_GPUBufferUsageFlags usage, Uint32 size)
{
    SDL_GPUBufferCreateInfo info;

    SDL_zero(info);
    info.usage = usage;
    info.size = size;
    return SDL_CreateGPUBuffer(device, &info);
}

static bool lesson28_ensure_frame_buffers(ForgeGpuDemo *demo, Lesson28State *state, Uint32 vertex_bytes, Uint32 index_bytes)
{
    if (vertex_bytes > state->vertex_buffer_size) {
        const Uint32 new_size = lesson28_next_power_of_two(vertex_bytes);
        SDL_GPUBuffer *new_buffer = lesson28_create_buffer(demo->device, SDL_GPU_BUFFERUSAGE_VERTEX, new_size);
        if (!new_buffer) {
            return false;
        }
        if (demo->lesson.vertex_buffer) {
            SDL_ReleaseGPUBuffer(demo->device, demo->lesson.vertex_buffer);
        }
        demo->lesson.vertex_buffer = new_buffer;
        state->vertex_buffer_size = new_size;
    }

    if (index_bytes > state->index_buffer_size) {
        const Uint32 new_size = lesson28_next_power_of_two(index_bytes);
        SDL_GPUBuffer *new_buffer = lesson28_create_buffer(demo->device, SDL_GPU_BUFFERUSAGE_INDEX, new_size);
        if (!new_buffer) {
            return false;
        }
        if (demo->lesson.index_buffer) {
            SDL_ReleaseGPUBuffer(demo->device, demo->lesson.index_buffer);
        }
        demo->lesson.index_buffer = new_buffer;
        state->index_buffer_size = new_size;
    }
    return true;
}

static bool lesson28_upload_frame_geometry(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    const ForgeGpuLesson28Frame *frame)
{
    const Uint32 vertex_bytes = frame->vertex_count * (Uint32)sizeof(ForgeGpuLesson28Vertex);
    const Uint32 index_bytes = frame->index_count * (Uint32)sizeof(Uint32);
    const Uint32 total_bytes = vertex_bytes + index_bytes;
    SDL_GPUTransferBufferCreateInfo transfer_info;
    SDL_GPUTransferBuffer *transfer;
    SDL_GPUCopyPass *copy_pass;
    SDL_GPUTransferBufferLocation source;
    SDL_GPUBufferRegion destination;
    void *mapped;

    if (total_bytes == 0) {
        return true;
    }

    SDL_zero(transfer_info);
    transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transfer_info.size = total_bytes;
    transfer = SDL_CreateGPUTransferBuffer(demo->device, &transfer_info);
    if (!transfer) {
        return false;
    }

    mapped = SDL_MapGPUTransferBuffer(demo->device, transfer, false);
    if (!mapped) {
        SDL_ReleaseGPUTransferBuffer(demo->device, transfer);
        return false;
    }
    SDL_memcpy(mapped, frame->vertices, vertex_bytes);
    SDL_memcpy((Uint8 *)mapped + vertex_bytes, frame->indices, index_bytes);
    SDL_UnmapGPUTransferBuffer(demo->device, transfer);

    copy_pass = SDL_BeginGPUCopyPass(command_buffer);
    if (!copy_pass) {
        SDL_ReleaseGPUTransferBuffer(demo->device, transfer);
        return false;
    }

    SDL_zero(source);
    source.transfer_buffer = transfer;
    source.offset = 0;
    SDL_zero(destination);
    destination.buffer = demo->lesson.vertex_buffer;
    destination.offset = 0;
    destination.size = vertex_bytes;
    SDL_UploadToGPUBuffer(copy_pass, &source, &destination, false);

    SDL_zero(source);
    source.transfer_buffer = transfer;
    source.offset = vertex_bytes;
    SDL_zero(destination);
    destination.buffer = demo->lesson.index_buffer;
    destination.offset = 0;
    destination.size = index_bytes;
    SDL_UploadToGPUBuffer(copy_pass, &source, &destination, false);

    SDL_EndGPUCopyPass(copy_pass);
    SDL_ReleaseGPUTransferBuffer(demo->device, transfer);
    return true;
}

bool ForgeGpuCreateLesson28(ForgeGpuDemo *demo)
{
    Lesson28State *state;
    char font_path[FORGE_GPU_MAX_PATH];
    SDL_GPUTextureCreateInfo texture_info;
    SDL_GPUSamplerCreateInfo sampler_info;

    if (!SDL_GPUTextureSupportsFormat(
            demo->device,
            SDL_GPU_TEXTUREFORMAT_R8_UNORM,
            SDL_GPU_TEXTURETYPE_2D,
            SDL_GPU_TEXTUREUSAGE_SAMPLER)) {
        SDL_SetError("lesson 28 requires sampled R8_UNORM font atlas textures");
        return false;
    }

    state = (Lesson28State *)SDL_calloc(1, sizeof(*state));
    if (!state) {
        SDL_OutOfMemory();
        return false;
    }
    demo->lesson.private_state = state;

    if (!ForgeGpuJoinAssetPath(demo, "fonts/liberation_mono/LiberationMono-Regular.ttf", font_path, sizeof(font_path))) {
        SDL_SetError("lesson 28 font path is too long");
        return false;
    }
    state->ui = ForgeGpuLesson28UiCreate(font_path);
    if (!state->ui) {
        return false;
    }

    SDL_zero(texture_info);
    texture_info.type = SDL_GPU_TEXTURETYPE_2D;
    texture_info.format = SDL_GPU_TEXTUREFORMAT_R8_UNORM;
    texture_info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    texture_info.width = ForgeGpuLesson28UiAtlasWidth(state->ui);
    texture_info.height = ForgeGpuLesson28UiAtlasHeight(state->ui);
    texture_info.layer_count_or_depth = 1;
    texture_info.num_levels = 1;
    texture_info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    if (texture_info.width == 0u || texture_info.height == 0u) {
        SDL_SetError("lesson 28 font atlas is empty");
        return false;
    }
    demo->lesson.texture = SDL_CreateGPUTexture(demo->device, &texture_info);
    if (!demo->lesson.texture) {
        return false;
    }
    if (!lesson28_upload_atlas_texture(demo, ForgeGpuLesson28UiAtlasPixels(state->ui), texture_info.width, texture_info.height)) {
        return false;
    }

    SDL_zero(sampler_info);
    sampler_info.min_filter = SDL_GPU_FILTER_LINEAR;
    sampler_info.mag_filter = SDL_GPU_FILTER_LINEAR;
    sampler_info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    sampler_info.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler_info.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler_info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    demo->lesson.samplers[0] = SDL_CreateGPUSampler(demo->device, &sampler_info);
    if (!demo->lesson.samplers[0]) {
        return false;
    }

    if (!lesson28_create_pipeline(demo)) {
        return false;
    }

    demo->lesson.vertex_buffer = lesson28_create_buffer(demo->device, SDL_GPU_BUFFERUSAGE_VERTEX, LESSON28_INITIAL_VERTEX_BUFFER_SIZE);
    demo->lesson.index_buffer = lesson28_create_buffer(demo->device, SDL_GPU_BUFFERUSAGE_INDEX, LESSON28_INITIAL_INDEX_BUFFER_SIZE);
    if (!demo->lesson.vertex_buffer || !demo->lesson.index_buffer) {
        return false;
    }
    state->vertex_buffer_size = LESSON28_INITIAL_VERTEX_BUFFER_SIZE;
    state->index_buffer_size = LESSON28_INITIAL_INDEX_BUFFER_SIZE;

    if (!SDL_StartTextInput(demo->window)) {
        return false;
    }
    state->text_input_started = true;
    return true;
}

bool ForgeGpuRenderLesson28(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPUTexture *swapchain_texture,
    Uint32 width,
    Uint32 height)
{
    Lesson28State *state = lesson28_state(demo);
    ForgeGpuLesson28Frame frame;
    float mouse_x = 0.0f;
    float mouse_y = 0.0f;
    Uint32 mouse_buttons;
    SDL_GPUColorTargetInfo color_target;
    SDL_GPURenderPass *render_pass;

    if (!state || !state->ui) {
        SDL_SetError("lesson 28 internal state is missing");
        return false;
    }

    mouse_buttons = SDL_GetMouseState(&mouse_x, &mouse_y);
    if (!ForgeGpuLesson28UiBuildFrame(
            state->ui,
            mouse_x,
            mouse_y,
            (mouse_buttons & SDL_BUTTON_LMASK) != 0,
            demo->validation_mode,
            &frame)) {
        return false;
    }
    state->last_frame = frame;

    if (!lesson28_ensure_frame_buffers(
            demo,
            state,
            frame.vertex_count * (Uint32)sizeof(ForgeGpuLesson28Vertex),
            frame.index_count * (Uint32)sizeof(Uint32))) {
        return false;
    }
    if (!lesson28_upload_frame_geometry(demo, command_buffer, &frame)) {
        return false;
    }

    SDL_zero(color_target);
    color_target.texture = swapchain_texture;
    color_target.load_op = SDL_GPU_LOADOP_CLEAR;
    color_target.store_op = SDL_GPU_STOREOP_STORE;
    color_target.clear_color.r = 0.02f;
    color_target.clear_color.g = 0.02f;
    color_target.clear_color.b = 0.03f;
    color_target.clear_color.a = 1.0f;
    render_pass = SDL_BeginGPURenderPass(command_buffer, &color_target, 1, nullptr);
    if (!render_pass) {
        return false;
    }

    if (frame.vertex_count > 0 && frame.index_count > 0) {
        SDL_GPUBufferBinding vertex_binding;
        SDL_GPUBufferBinding index_binding;
        SDL_GPUTextureSamplerBinding sampler_binding;
        Lesson28Uniforms uniforms;

        SDL_BindGPUGraphicsPipeline(render_pass, demo->lesson.pipeline);

        SDL_zero(vertex_binding);
        vertex_binding.buffer = demo->lesson.vertex_buffer;
        SDL_BindGPUVertexBuffers(render_pass, 0, &vertex_binding, 1);

        SDL_zero(index_binding);
        index_binding.buffer = demo->lesson.index_buffer;
        SDL_BindGPUIndexBuffer(render_pass, &index_binding, SDL_GPU_INDEXELEMENTSIZE_32BIT);

        SDL_zero(sampler_binding);
        sampler_binding.texture = demo->lesson.texture;
        sampler_binding.sampler = demo->lesson.samplers[0];
        SDL_BindGPUFragmentSamplers(render_pass, 0, &sampler_binding, 1);

        uniforms.projection = lesson28_ui_projection((float)width, (float)height);
        SDL_PushGPUVertexUniformData(command_buffer, 0, &uniforms, sizeof(uniforms));
        SDL_DrawGPUIndexedPrimitives(render_pass, frame.index_count, 1, 0, 0, 0);
    }

    SDL_EndGPURenderPass(render_pass);
    state->rendered_frame = frame.index_count > 0;
    return true;
}

void ForgeGpuDebugLesson28(ForgeGpuDemo *demo)
{
    Lesson28State *state = lesson28_state(demo);

    if (!state) {
        return;
    }

    ImGui::Text("Forge UI vertices: %u", state->last_frame.cached_vertex_count);
    ImGui::Text("Forge UI indices: %u", state->last_frame.cached_index_count);
    ImGui::Text("Forge UI triangles: %u", state->last_frame.cached_index_count / 3u);
    ImGui::Text("Font atlas: %ux%u R8_UNORM", state->last_frame.atlas_width, state->last_frame.atlas_height);
    ImGui::Text("Lesson draw calls: %d", state->rendered_frame ? 1 : 0);
}

void ForgeGpuControlsLesson28(ForgeGpuDemo *demo)
{
    (void)demo;
    ImGui::TextUnformatted("Forge UI");
    ImGui::BulletText("Mouse: interact with the rendered Forge UI windows");
    ImGui::BulletText("Wheel: scroll Forge UI windows");
    ImGui::BulletText("Keyboard: type into the Forge UI text field");
    ImGui::TextWrapped("The shell ImGui overlay intentionally remains on top so lesson 28 exposes the current UI overlap behavior.");
}

bool ForgeGpuHandleLesson28Event(ForgeGpuDemo *demo, const SDL_Event *event)
{
    Lesson28State *state = lesson28_state(demo);

    if (!state || !state->ui) {
        return false;
    }
    return ForgeGpuLesson28UiHandleEvent(state->ui, event);
}

void ForgeGpuExportLesson28Metrics(ForgeGpuDemo *demo)
{
    Lesson28State *state = lesson28_state(demo);

    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson28Ui", state ? 1.0 : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson28Rendered", state && state->rendered_frame ? 1.0 : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson28AtlasWidth", state ? (double)state->last_frame.atlas_width : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson28AtlasHeight", state ? (double)state->last_frame.atlas_height : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson28VertexCount", state ? (double)state->last_frame.cached_vertex_count : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson28IndexCount", state ? (double)state->last_frame.cached_index_count : 0.0);
}

void ForgeGpuDestroyLesson28(ForgeGpuDemo *demo)
{
    Lesson28State *state = lesson28_state(demo);

    if (!state) {
        return;
    }
    if (state->text_input_started && demo->window) {
        SDL_StopTextInput(demo->window);
    }
    ForgeGpuLesson28UiDestroy(state->ui);
    SDL_free(state);
    demo->lesson.private_state = nullptr;
}
