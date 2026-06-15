#include "forge_gpu_lessons.h"

#include "forge_gpu_gpu_helpers.h"
#include "forge_gpu_pipelines.h"
#include "shaders/generated/forge_gpu_lesson_02_shaders.h"

static const LessonVertex2Color kTriangleVertices[] = {
    { {  0.0f,  0.5f }, { 1.0f, 0.0f, 0.0f } },
    { { -0.5f, -0.5f }, { 0.0f, 1.0f, 0.0f } },
    { {  0.5f, -0.5f }, { 0.0f, 0.0f, 1.0f } }
};

bool ForgeGpuCreateLesson02(ForgeGpuDemo *demo)
{
    demo->lesson.vertex_buffer = ForgeGpuCreateBufferWithData(
        demo->device, SDL_GPU_BUFFERUSAGE_VERTEX,
        kTriangleVertices, sizeof(kTriangleVertices));
    return demo->lesson.vertex_buffer != nullptr &&
           ForgeGpuCreateColorTrianglePipeline(
               demo,
               lesson02_triangle_vert_wgsl, lesson02_triangle_vert_wgsl_size,
               lesson02_triangle_vert_msl, lesson02_triangle_vert_msl_size,
               lesson02_triangle_frag_wgsl, lesson02_triangle_frag_wgsl_size,
               lesson02_triangle_frag_msl, lesson02_triangle_frag_msl_size,
               0);
}

void ForgeGpuRenderLesson02(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    Uint32 width,
    Uint32 height)
{
    SDL_GPUBufferBinding vertex_binding;

    (void)command_buffer;
    (void)width;
    (void)height;

    SDL_zero(vertex_binding);
    vertex_binding.buffer = demo->lesson.vertex_buffer;
    SDL_BindGPUGraphicsPipeline(render_pass, demo->lesson.pipeline);
    SDL_BindGPUVertexBuffers(render_pass, 0, &vertex_binding, 1);
    SDL_DrawGPUPrimitives(render_pass, 3, 1, 0, 0);
}
