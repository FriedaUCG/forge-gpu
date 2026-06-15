#include "forge_gpu_lessons.h"

#include "forge_gpu_camera.h"
#include "forge_gpu_gpu_helpers.h"
#include "forge_gpu_lesson_common.h"
#include "forge_gpu_math.h"
#include "shaders/generated/forge_gpu_lesson_19_shaders.h"

#include <stddef.h>

#define LESSON19_MAX_DEBUG_VERTICES 65536u
#define LESSON19_DEPTH_FORMAT SDL_GPU_TEXTUREFORMAT_D32_FLOAT
#define LESSON19_GRID_HALF_SIZE 20
#define LESSON19_CIRCLE_SEGMENTS 32

struct Lesson19DebugVertex
{
    Vec3 position;
    Vec4 color;
};

struct Lesson19DebugUniforms
{
    Mat4 view_projection;
};

struct Lesson19State
{
    SDL_GPUGraphicsPipeline *line_pipeline;
    SDL_GPUGraphicsPipeline *overlay_pipeline;
    SDL_GPUBuffer *vertex_buffer;
    SDL_GPUTransferBuffer *transfer_buffer;
    Lesson19DebugVertex *vertices;
    Uint32 world_count;
    Uint32 overlay_count;
};

static Lesson19State *lesson19_state(ForgeGpuDemo *demo)
{
    return (Lesson19State *)demo->lesson.private_state;
}

static bool lesson19_add_vertex(Lesson19State *state, Vec3 position, Vec4 color, bool overlay)
{
    const Uint32 total = state->world_count + state->overlay_count;

    if (total >= LESSON19_MAX_DEBUG_VERTICES) {
        return false;
    }
    if (overlay) {
        const Uint32 index = LESSON19_MAX_DEBUG_VERTICES - 1u - state->overlay_count;
        state->vertices[index].position = position;
        state->vertices[index].color = color;
        state->overlay_count += 1;
    } else {
        state->vertices[state->world_count].position = position;
        state->vertices[state->world_count].color = color;
        state->world_count += 1;
    }
    return true;
}

static void lesson19_debug_line(Lesson19State *state, Vec3 start, Vec3 end, Vec4 color, bool overlay)
{
    const Uint32 total = state->world_count + state->overlay_count;

    if (total + 2u > LESSON19_MAX_DEBUG_VERTICES) {
        return;
    }
    lesson19_add_vertex(state, start, color, overlay);
    lesson19_add_vertex(state, end, color, overlay);
}

static void lesson19_debug_grid(Lesson19State *state)
{
    const float extent = (float)LESSON19_GRID_HALF_SIZE;
    const Vec4 color = vec4_create(0.3f, 0.3f, 0.3f, 1.0f);

    for (int i = -LESSON19_GRID_HALF_SIZE; i <= LESSON19_GRID_HALF_SIZE; i += 1) {
        const float p = (float)i;
        lesson19_debug_line(state, { p, 0.0f, -extent }, { p, 0.0f, extent }, color, false);
        lesson19_debug_line(state, { -extent, 0.0f, p }, { extent, 0.0f, p }, color, false);
    }
}

static void lesson19_debug_axes(Lesson19State *state, Vec3 origin, float size, bool overlay)
{
    lesson19_debug_line(state, origin, vec3_add(origin, { size, 0.0f, 0.0f }), vec4_create(1.0f, 0.0f, 0.0f, 1.0f), overlay);
    lesson19_debug_line(state, origin, vec3_add(origin, { 0.0f, size, 0.0f }), vec4_create(0.0f, 1.0f, 0.0f, 1.0f), overlay);
    lesson19_debug_line(state, origin, vec3_add(origin, { 0.0f, 0.0f, size }), vec4_create(0.0f, 0.4f, 1.0f, 1.0f), overlay);
}

static void lesson19_debug_circle(
    Lesson19State *state,
    Vec3 center,
    float radius,
    Vec3 normal,
    Vec4 color,
    bool overlay)
{
    Vec3 n = vec3_normalize(normal);
    Vec3 reference = SDL_fabsf(n.y) > 0.9f ? Vec3{ 1.0f, 0.0f, 0.0f } : Vec3{ 0.0f, 1.0f, 0.0f };
    Vec3 tangent1 = vec3_normalize(vec3_cross(n, reference));
    Vec3 tangent2 = vec3_cross(n, tangent1);
    const float angle_step = (2.0f * FORGE_GPU_PI) / (float)LESSON19_CIRCLE_SEGMENTS;

    for (int i = 0; i < LESSON19_CIRCLE_SEGMENTS; i += 1) {
        const float a0 = (float)i * angle_step;
        const float a1 = (float)((i + 1) % LESSON19_CIRCLE_SEGMENTS) * angle_step;
        Vec3 p0 = vec3_add(
            center,
            vec3_add(
                vec3_scale(tangent1, radius * SDL_cosf(a0)),
                vec3_scale(tangent2, radius * SDL_sinf(a0))));
        Vec3 p1 = vec3_add(
            center,
            vec3_add(
                vec3_scale(tangent1, radius * SDL_cosf(a1)),
                vec3_scale(tangent2, radius * SDL_sinf(a1))));

        lesson19_debug_line(state, p0, p1, color, overlay);
    }
}

static void lesson19_debug_box(Lesson19State *state, Vec3 min_pt, Vec3 max_pt, Vec4 color, bool overlay)
{
    const Vec3 c[8] = {
        { min_pt.x, min_pt.y, max_pt.z },
        { max_pt.x, min_pt.y, max_pt.z },
        { min_pt.x, min_pt.y, min_pt.z },
        { max_pt.x, min_pt.y, min_pt.z },
        { min_pt.x, max_pt.y, max_pt.z },
        { max_pt.x, max_pt.y, max_pt.z },
        { min_pt.x, max_pt.y, min_pt.z },
        { max_pt.x, max_pt.y, min_pt.z }
    };

    lesson19_debug_line(state, c[0], c[1], color, overlay);
    lesson19_debug_line(state, c[1], c[3], color, overlay);
    lesson19_debug_line(state, c[3], c[2], color, overlay);
    lesson19_debug_line(state, c[2], c[0], color, overlay);
    lesson19_debug_line(state, c[4], c[5], color, overlay);
    lesson19_debug_line(state, c[5], c[7], color, overlay);
    lesson19_debug_line(state, c[7], c[6], color, overlay);
    lesson19_debug_line(state, c[6], c[4], color, overlay);
    lesson19_debug_line(state, c[0], c[4], color, overlay);
    lesson19_debug_line(state, c[1], c[5], color, overlay);
    lesson19_debug_line(state, c[2], c[6], color, overlay);
    lesson19_debug_line(state, c[3], c[7], color, overlay);
}

static void lesson19_build_debug_scene(Lesson19State *state, float time)
{
    const float angle = time;
    const Vec3 animated_normal = { SDL_sinf(angle), 0.5f, SDL_cosf(angle) };

    state->world_count = 0;
    state->overlay_count = 0;

    lesson19_debug_grid(state);
    lesson19_debug_axes(state, { 0.0f, 0.0f, 0.0f }, 2.0f, true);

    lesson19_debug_box(state, { -6.0f, 0.0f, -3.0f }, { -4.0f, 2.0f, -1.0f }, vec4_create(1.0f, 0.6f, 0.0f, 1.0f), false);
    lesson19_debug_box(state, { 4.0f, 0.0f, -4.0f }, { 7.0f, 3.0f, -1.0f }, vec4_create(0.2f, 0.8f, 1.0f, 1.0f), false);
    lesson19_debug_box(state, { -2.0f, 0.0f, -8.0f }, { 2.0f, 4.0f, -5.0f }, vec4_create(1.0f, 0.3f, 0.5f, 1.0f), false);
    lesson19_debug_box(state, { -0.5f, 0.0f, -0.5f }, { 0.5f, 1.0f, 0.5f }, vec4_create(1.0f, 1.0f, 0.0f, 1.0f), true);

    lesson19_debug_circle(state, { 6.0f, 0.5f, 4.0f }, 1.5f, { 0.0f, 1.0f, 0.0f }, vec4_create(0.0f, 1.0f, 0.5f, 1.0f), false);
    lesson19_debug_circle(state, { -6.0f, 2.0f, 4.0f }, 1.5f, { 0.0f, 0.0f, 1.0f }, vec4_create(0.8f, 0.2f, 1.0f, 1.0f), false);
    lesson19_debug_circle(state, { 0.0f, 3.0f, -3.0f }, 2.0f, animated_normal, vec4_create(1.0f, 0.8f, 0.2f, 1.0f), false);

    lesson19_debug_axes(state, { -5.0f, 1.0f, -2.0f }, 1.0f, false);
    lesson19_debug_axes(state, { 5.5f, 1.5f, -2.5f }, 1.0f, false);
}

static bool lesson19_upload_vertices(ForgeGpuDemo *demo, SDL_GPUCommandBuffer *command_buffer, Lesson19State *state)
{
    const Uint32 total_vertices = state->world_count + state->overlay_count;
    void *mapped;
    SDL_GPUCopyPass *copy_pass;
    SDL_GPUTransferBufferLocation source;
    SDL_GPUBufferRegion destination;

    if (total_vertices == 0) {
        return true;
    }
    if (state->overlay_count > 0) {
        const Uint32 overlay_start = LESSON19_MAX_DEBUG_VERTICES - state->overlay_count;

        for (Uint32 i = 0; i < state->overlay_count; i += 1) {
            state->vertices[state->world_count + i] = state->vertices[overlay_start + i];
        }
    }

    mapped = SDL_MapGPUTransferBuffer(demo->device, state->transfer_buffer, true);
    if (!mapped) {
        return false;
    }
    SDL_memcpy(mapped, state->vertices, total_vertices * sizeof(*state->vertices));
    SDL_UnmapGPUTransferBuffer(demo->device, state->transfer_buffer);

    copy_pass = SDL_BeginGPUCopyPass(command_buffer);
    if (!copy_pass) {
        return false;
    }

    SDL_zero(source);
    source.transfer_buffer = state->transfer_buffer;
    SDL_zero(destination);
    destination.buffer = state->vertex_buffer;
    destination.size = total_vertices * (Uint32)sizeof(*state->vertices);
    SDL_UploadToGPUBuffer(copy_pass, &source, &destination, false);
    SDL_EndGPUCopyPass(copy_pass);
    return true;
}

static bool lesson19_create_pipelines(ForgeGpuDemo *demo, Lesson19State *state)
{
    SDL_GPUShader *vertex_shader = nullptr;
    SDL_GPUShader *fragment_shader = nullptr;
    SDL_GPUVertexBufferDescription vertex_buffer_desc;
    SDL_GPUVertexAttribute vertex_attributes[2];
    bool ok = false;

    vertex_shader = ForgeGpuCreateShader(
        demo->device, SDL_GPU_SHADERSTAGE_VERTEX,
        lesson19_debug_vert_wgsl, lesson19_debug_vert_wgsl_size,
        lesson19_debug_vert_msl, lesson19_debug_vert_msl_size,
        0, 0, 0, 1);
    fragment_shader = ForgeGpuCreateShader(
        demo->device, SDL_GPU_SHADERSTAGE_FRAGMENT,
        lesson19_debug_frag_wgsl, lesson19_debug_frag_wgsl_size,
        lesson19_debug_frag_msl, lesson19_debug_frag_msl_size,
        0, 0, 0, 0);
    if (!vertex_shader || !fragment_shader) {
        goto done;
    }

    SDL_zero(vertex_buffer_desc);
    vertex_buffer_desc.slot = 0;
    vertex_buffer_desc.pitch = sizeof(Lesson19DebugVertex);
    vertex_buffer_desc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    SDL_zeroa(vertex_attributes);
    vertex_attributes[0].location = 0;
    vertex_attributes[0].buffer_slot = 0;
    vertex_attributes[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    vertex_attributes[0].offset = offsetof(Lesson19DebugVertex, position);
    vertex_attributes[1].location = 1;
    vertex_attributes[1].buffer_slot = 0;
    vertex_attributes[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
    vertex_attributes[1].offset = offsetof(Lesson19DebugVertex, color);

    state->line_pipeline = ForgeGpuCreateLessonGraphicsPipelineWithPrimitive(
        demo,
        vertex_shader,
        fragment_shader,
        SDL_GPU_PRIMITIVETYPE_LINELIST,
        &vertex_buffer_desc,
        1,
        vertex_attributes,
        SDL_arraysize(vertex_attributes),
        1,
        true,
        LESSON19_DEPTH_FORMAT,
        true,
        true,
        SDL_GPU_CULLMODE_NONE,
        0.0f,
        0.0f);
    state->overlay_pipeline = ForgeGpuCreateLessonGraphicsPipelineWithPrimitive(
        demo,
        vertex_shader,
        fragment_shader,
        SDL_GPU_PRIMITIVETYPE_LINELIST,
        &vertex_buffer_desc,
        1,
        vertex_attributes,
        SDL_arraysize(vertex_attributes),
        1,
        true,
        LESSON19_DEPTH_FORMAT,
        false,
        false,
        SDL_GPU_CULLMODE_NONE,
        0.0f,
        0.0f);
    ok = state->line_pipeline && state->overlay_pipeline;

done:
    if (fragment_shader) {
        SDL_ReleaseGPUShader(demo->device, fragment_shader);
    }
    if (vertex_shader) {
        SDL_ReleaseGPUShader(demo->device, vertex_shader);
    }
    return ok;
}

bool ForgeGpuCreateLesson19(ForgeGpuDemo *demo)
{
    Lesson19State *state;
    SDL_GPUBufferCreateInfo buffer_info;
    SDL_GPUTransferBufferCreateInfo transfer_info;
    const Uint32 buffer_size = LESSON19_MAX_DEBUG_VERTICES * (Uint32)sizeof(Lesson19DebugVertex);

    state = (Lesson19State *)SDL_calloc(1, sizeof(*state));
    if (!state) {
        SDL_OutOfMemory();
        return false;
    }
    demo->lesson.private_state = state;

    SDL_zero(buffer_info);
    buffer_info.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
    buffer_info.size = buffer_size;
    state->vertex_buffer = SDL_CreateGPUBuffer(demo->device, &buffer_info);

    SDL_zero(transfer_info);
    transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transfer_info.size = buffer_size;
    state->transfer_buffer = SDL_CreateGPUTransferBuffer(demo->device, &transfer_info);

    state->vertices = (Lesson19DebugVertex *)SDL_calloc(LESSON19_MAX_DEBUG_VERTICES, sizeof(*state->vertices));
    if (!state->vertex_buffer || !state->transfer_buffer || !state->vertices) {
        return false;
    }

    demo->lesson.camera_position = { 0.0f, 4.0f, 12.0f };
    demo->lesson.camera_yaw = 0.0f;
    demo->lesson.camera_pitch = 0.0f;
    demo->lesson.move_speed = 5.0f;
    demo->lesson.last_ticks = SDL_GetTicks();

    return lesson19_create_pipelines(demo, state);
}

bool ForgeGpuRenderLesson19(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPUTexture *swapchain_texture,
    Uint32 width,
    Uint32 height)
{
    Lesson19State *state = lesson19_state(demo);
    SDL_GPUColorTargetInfo color_target;
    SDL_GPUDepthStencilTargetInfo depth_target;
    SDL_GPURenderPass *render_pass;
    Mat4 view;
    Mat4 projection;
    Lesson19DebugUniforms uniforms;

    if (!state) {
        return true;
    }

    ForgeGpuUpdateCameraFromInput(demo);
    lesson19_build_debug_scene(state, ForgeGpuFrameTimeSeconds(demo));
    if (!lesson19_upload_vertices(demo, command_buffer, state)) {
        return false;
    }

    if (!ForgeGpuCreateDepthTextureWithFormat(demo, width, height, LESSON19_DEPTH_FORMAT)) {
        return false;
    }

    ForgeGpuCameraViewProjection(demo, width, height, 200.0f, &view, &projection);
    uniforms.view_projection = mat4_multiply(projection, view);

    SDL_zero(color_target);
    color_target.texture = swapchain_texture;
    color_target.load_op = SDL_GPU_LOADOP_CLEAR;
    color_target.store_op = SDL_GPU_STOREOP_STORE;
    color_target.clear_color = { 0.05f, 0.05f, 0.07f, 1.0f };

    SDL_zero(depth_target);
    depth_target.texture = demo->lesson.depth_texture;
    depth_target.clear_depth = 1.0f;
    depth_target.load_op = SDL_GPU_LOADOP_CLEAR;
    depth_target.store_op = SDL_GPU_STOREOP_DONT_CARE;
    depth_target.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
    depth_target.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;

    render_pass = SDL_BeginGPURenderPass(command_buffer, &color_target, 1, &depth_target);
    if (!render_pass) {
        return false;
    }

    if (state->world_count > 0) {
        SDL_GPUBufferBinding vertex_binding;

        SDL_zero(vertex_binding);
        vertex_binding.buffer = state->vertex_buffer;
        SDL_BindGPUGraphicsPipeline(render_pass, state->line_pipeline);
        SDL_PushGPUVertexUniformData(command_buffer, 0, &uniforms, sizeof(uniforms));
        SDL_BindGPUVertexBuffers(render_pass, 0, &vertex_binding, 1);
        SDL_DrawGPUPrimitives(render_pass, state->world_count, 1, 0, 0);
    }

    if (state->overlay_count > 0) {
        SDL_GPUBufferBinding vertex_binding;

        SDL_zero(vertex_binding);
        vertex_binding.buffer = state->vertex_buffer;
        SDL_BindGPUGraphicsPipeline(render_pass, state->overlay_pipeline);
        SDL_PushGPUVertexUniformData(command_buffer, 0, &uniforms, sizeof(uniforms));
        SDL_BindGPUVertexBuffers(render_pass, 0, &vertex_binding, 1);
        SDL_DrawGPUPrimitives(render_pass, state->overlay_count, 1, state->world_count, 0);
    }

    SDL_EndGPURenderPass(render_pass);
    return true;
}

void ForgeGpuDestroyLesson19(ForgeGpuDemo *demo)
{
    Lesson19State *state = lesson19_state(demo);

    if (!state) {
        return;
    }
    if (state->overlay_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, state->overlay_pipeline);
    }
    if (state->line_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, state->line_pipeline);
    }
    if (state->transfer_buffer) {
        SDL_ReleaseGPUTransferBuffer(demo->device, state->transfer_buffer);
    }
    if (state->vertex_buffer) {
        SDL_ReleaseGPUBuffer(demo->device, state->vertex_buffer);
    }
    SDL_free(state->vertices);
    SDL_free(state);
    demo->lesson.private_state = nullptr;
}
