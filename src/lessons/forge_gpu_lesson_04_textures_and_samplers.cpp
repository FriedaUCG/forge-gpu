#include "forge_gpu_lessons.h"

#include "forge_gpu_gpu_helpers.h"
#include "forge_gpu_lesson_common.h"
#include "forge_gpu_pipelines.h"
#include "shaders/generated/forge_gpu_lesson_04_shaders.h"

static const LessonVertex2Uv kQuadVertices[] = {
    { { -0.6f,  0.6f }, { 0.0f, 0.0f } },
    { {  0.6f,  0.6f }, { 1.0f, 0.0f } },
    { {  0.6f, -0.6f }, { 1.0f, 1.0f } },
    { { -0.6f, -0.6f }, { 0.0f, 1.0f } }
};

bool ForgeGpuCreateLesson04(ForgeGpuDemo *demo)
{
    demo->lesson.vertex_buffer = ForgeGpuCreateBufferWithData(
        demo->device, SDL_GPU_BUFFERUSAGE_VERTEX,
        kQuadVertices, sizeof(kQuadVertices));
    demo->lesson.index_buffer = ForgeGpuCreateBufferWithData(
        demo->device, SDL_GPU_BUFFERUSAGE_INDEX,
        kForgeGpuQuadIndices, sizeof(kForgeGpuQuadIndices));
    demo->lesson.texture = ForgeGpuLoadRgbaTexture(
        demo, "textures/04-textures-and-samplers/brick_wall.png");
    demo->lesson.samplers[0] = ForgeGpuCreateSampler(
        demo->device,
        SDL_GPU_FILTER_LINEAR, SDL_GPU_FILTER_LINEAR,
        SDL_GPU_SAMPLERMIPMAPMODE_LINEAR,
        1000.0f);
    return demo->lesson.vertex_buffer && demo->lesson.index_buffer &&
           demo->lesson.texture && demo->lesson.samplers[0] &&
           ForgeGpuCreateTexturedQuadPipeline(
               demo,
               lesson04_quad_vert_wgsl, lesson04_quad_vert_wgsl_size,
               lesson04_quad_vert_msl, lesson04_quad_vert_msl_size,
               lesson04_quad_frag_wgsl, lesson04_quad_frag_wgsl_size,
               lesson04_quad_frag_msl, lesson04_quad_frag_msl_size);
}

void ForgeGpuRenderLesson04(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    Uint32 width,
    Uint32 height)
{
    SDL_GPUBufferBinding vertex_binding;
    SDL_GPUBufferBinding index_binding;
    SDL_GPUTextureSamplerBinding sampler_binding;
    UniformTimeAspect uniforms;

    uniforms.time = ForgeGpuFrameTimeSeconds(demo);
    uniforms.aspect = height > 0 ? (float)width / (float)height : 1.0f;
    SDL_PushGPUVertexUniformData(command_buffer, 0, &uniforms, sizeof(uniforms));

    SDL_zero(vertex_binding);
    vertex_binding.buffer = demo->lesson.vertex_buffer;
    SDL_zero(index_binding);
    index_binding.buffer = demo->lesson.index_buffer;
    SDL_zero(sampler_binding);
    sampler_binding.texture = demo->lesson.texture;
    sampler_binding.sampler = demo->lesson.samplers[0];

    SDL_BindGPUGraphicsPipeline(render_pass, demo->lesson.pipeline);
    SDL_BindGPUVertexBuffers(render_pass, 0, &vertex_binding, 1);
    SDL_BindGPUIndexBuffer(render_pass, &index_binding, SDL_GPU_INDEXELEMENTSIZE_16BIT);
    SDL_BindGPUFragmentSamplers(render_pass, 0, &sampler_binding, 1);
    SDL_DrawGPUIndexedPrimitives(render_pass, 6, 1, 0, 0, 0);
}
