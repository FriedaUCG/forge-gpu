#include "forge_gpu_lessons.h"

#include "forge_gpu_gpu_helpers.h"
#include "forge_gpu_lesson_common.h"
#include "forge_gpu_math.h"
#include "forge_gpu_pipelines.h"

bool ForgeGpuCreateLesson06(ForgeGpuDemo *demo)
{
    demo->lesson.vertex_buffer = ForgeGpuCreateBufferWithData(
        demo->device, SDL_GPU_BUFFERUSAGE_VERTEX,
        kForgeGpuCubeVertices, sizeof(kForgeGpuCubeVertices));
    demo->lesson.index_buffer = ForgeGpuCreateBufferWithData(
        demo->device, SDL_GPU_BUFFERUSAGE_INDEX,
        kForgeGpuCubeIndices, sizeof(kForgeGpuCubeIndices));
    return demo->lesson.vertex_buffer && demo->lesson.index_buffer && ForgeGpuCreateCubePipeline(demo);
}

void ForgeGpuRenderLesson06(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    Uint32 width,
    Uint32 height)
{
    SDL_GPUBufferBinding vertex_binding;
    SDL_GPUBufferBinding index_binding;
    const float t = ForgeGpuFrameTimeSeconds(demo);
    const Mat4 rot_y = mat4_rotate_y(t * 1.0f);
    const Mat4 rot_x = mat4_rotate_x(t * 0.7f);
    const Mat4 model = mat4_multiply(rot_y, rot_x);
    const Vec3 eye = { 0.0f, 1.5f, 3.0f };
    const Vec3 target = { 0.0f, 0.0f, 0.0f };
    const Vec3 up = { 0.0f, 1.0f, 0.0f };
    const Mat4 view = mat4_look_at(eye, target, up);
    const float aspect = height > 0 ? (float)width / (float)height : 1.0f;
    const Mat4 projection = mat4_perspective(60.0f * FORGE_GPU_DEG2RAD, aspect, 0.1f, 100.0f);
    UniformMvp uniforms;

    uniforms.mvp = mat4_multiply(projection, mat4_multiply(view, model));
    SDL_PushGPUVertexUniformData(command_buffer, 0, &uniforms, sizeof(uniforms));

    SDL_zero(vertex_binding);
    vertex_binding.buffer = demo->lesson.vertex_buffer;
    SDL_zero(index_binding);
    index_binding.buffer = demo->lesson.index_buffer;
    SDL_BindGPUGraphicsPipeline(render_pass, demo->lesson.pipeline);
    SDL_BindGPUVertexBuffers(render_pass, 0, &vertex_binding, 1);
    SDL_BindGPUIndexBuffer(render_pass, &index_binding, SDL_GPU_INDEXELEMENTSIZE_16BIT);
    SDL_DrawGPUIndexedPrimitives(render_pass, 36, 1, 0, 0, 0);
}
