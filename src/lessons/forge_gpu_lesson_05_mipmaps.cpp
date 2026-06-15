#include "forge_gpu_lessons.h"

#include "forge_gpu_browser_status.h"
#include "forge_gpu_gpu_helpers.h"
#include "forge_gpu_lesson_common.h"
#include "forge_gpu_pipelines.h"
#include "shaders/generated/forge_gpu_lesson_05_shaders.h"
#include "imgui.h"

static const LessonVertex2Uv kQuadVertices[] = {
    { { -0.9f,  0.9f }, { 0.0f, 0.0f } },
    { {  0.9f,  0.9f }, { 1.0f, 0.0f } },
    { {  0.9f, -0.9f }, { 1.0f, 1.0f } },
    { { -0.9f, -0.9f }, { 0.0f, 1.0f } }
};

struct Lesson05State
{
    int current_sampler;
};

static Lesson05State *lesson05_state(ForgeGpuDemo *demo)
{
    return (Lesson05State *)demo->lesson.private_state;
}

bool ForgeGpuCreateLesson05(ForgeGpuDemo *demo)
{
    Lesson05State *state = (Lesson05State *)SDL_calloc(1, sizeof(*state));

    if (!state) {
        SDL_OutOfMemory();
        return false;
    }
    demo->lesson.private_state = state;

    demo->lesson.vertex_buffer = ForgeGpuCreateBufferWithData(
        demo->device, SDL_GPU_BUFFERUSAGE_VERTEX,
        kQuadVertices, sizeof(kQuadVertices));
    demo->lesson.index_buffer = ForgeGpuCreateBufferWithData(
        demo->device, SDL_GPU_BUFFERUSAGE_INDEX,
        kForgeGpuQuadIndices, sizeof(kForgeGpuQuadIndices));
    demo->lesson.texture = ForgeGpuCreateCheckerTexture(demo->device);
    demo->lesson.samplers[0] = ForgeGpuCreateSampler(
        demo->device,
        SDL_GPU_FILTER_LINEAR, SDL_GPU_FILTER_LINEAR,
        SDL_GPU_SAMPLERMIPMAPMODE_LINEAR,
        1000.0f);
    demo->lesson.samplers[1] = ForgeGpuCreateSampler(
        demo->device,
        SDL_GPU_FILTER_LINEAR, SDL_GPU_FILTER_LINEAR,
        SDL_GPU_SAMPLERMIPMAPMODE_NEAREST,
        1000.0f);
    demo->lesson.samplers[2] = ForgeGpuCreateSampler(
        demo->device,
        SDL_GPU_FILTER_NEAREST, SDL_GPU_FILTER_NEAREST,
        SDL_GPU_SAMPLERMIPMAPMODE_NEAREST,
        0.0f);
    state->current_sampler = 0;
    return demo->lesson.vertex_buffer && demo->lesson.index_buffer &&
           demo->lesson.texture &&
           demo->lesson.samplers[0] && demo->lesson.samplers[1] && demo->lesson.samplers[2] &&
           ForgeGpuCreateTexturedQuadPipeline(
               demo,
               lesson05_quad_vert_wgsl, lesson05_quad_vert_wgsl_size,
               lesson05_quad_vert_msl, lesson05_quad_vert_msl_size,
               lesson05_quad_frag_wgsl, lesson05_quad_frag_wgsl_size,
               lesson05_quad_frag_msl, lesson05_quad_frag_msl_size);
}

void ForgeGpuRenderLesson05(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    Uint32 width,
    Uint32 height)
{
    SDL_GPUBufferBinding vertex_binding;
    SDL_GPUBufferBinding index_binding;
    SDL_GPUTextureSamplerBinding sampler_binding;
    UniformMipmap uniforms;
    Lesson05State *state = lesson05_state(demo);

    if (!state) {
        return;
    }

    uniforms.time = ForgeGpuFrameTimeSeconds(demo);
    uniforms.aspect = height > 0 ? (float)width / (float)height : 1.0f;
    uniforms.uv_scale = 2.0f;
    uniforms.pad = 0.0f;
    SDL_PushGPUVertexUniformData(command_buffer, 0, &uniforms, sizeof(uniforms));

    SDL_zero(vertex_binding);
    vertex_binding.buffer = demo->lesson.vertex_buffer;
    SDL_zero(index_binding);
    index_binding.buffer = demo->lesson.index_buffer;
    SDL_zero(sampler_binding);
    sampler_binding.texture = demo->lesson.texture;
    sampler_binding.sampler = demo->lesson.samplers[state->current_sampler];

    SDL_BindGPUGraphicsPipeline(render_pass, demo->lesson.pipeline);
    SDL_BindGPUVertexBuffers(render_pass, 0, &vertex_binding, 1);
    SDL_BindGPUIndexBuffer(render_pass, &index_binding, SDL_GPU_INDEXELEMENTSIZE_16BIT);
    SDL_BindGPUFragmentSamplers(render_pass, 0, &sampler_binding, 1);
    SDL_DrawGPUIndexedPrimitives(render_pass, 6, 1, 0, 0, 0);
}

void ForgeGpuDebugLesson05(ForgeGpuDemo *demo)
{
    Lesson05State *state = lesson05_state(demo);

    if (state) {
        ImGui::Text("Sampler: %s", gForgeGpuSamplerNames[state->current_sampler]);
    }
}

void ForgeGpuControlsLesson05(ForgeGpuDemo *demo)
{
    (void)demo;
    ImGui::Text("Lesson action: Space cycles sampler");
}

bool ForgeGpuHandleLesson05Event(ForgeGpuDemo *demo, const SDL_Event *event)
{
    Lesson05State *state = lesson05_state(demo);

    if (event->type == SDL_EVENT_KEY_DOWN && !event->key.repeat && event->key.key == SDLK_SPACE) {
        if (state) {
            state->current_sampler = (state->current_sampler + 1) % 3;
            return true;
        }
    }
    return false;
}

void ForgeGpuExportLesson05Metrics(ForgeGpuDemo *demo)
{
    Lesson05State *state = lesson05_state(demo);

    if (state) {
        ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuSamplerIndex", (double)state->current_sampler);
    }
}

void ForgeGpuDestroyLesson05(ForgeGpuDemo *demo)
{
    Lesson05State *state = lesson05_state(demo);

    if (!state) {
        return;
    }
    SDL_free(state);
    demo->lesson.private_state = nullptr;
}
