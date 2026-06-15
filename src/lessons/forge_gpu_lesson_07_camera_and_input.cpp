#include "forge_gpu_lessons.h"

#include "forge_gpu_camera.h"
#include "forge_gpu_gpu_helpers.h"
#include "forge_gpu_lesson_common.h"
#include "forge_gpu_math.h"
#include "forge_gpu_pipelines.h"

static const CubeInstance kCubes[] = {
    { {  0.0f,  0.5f,  0.0f },  0.5f,  1.0f },
    { { -3.0f,  0.3f, -1.0f }, -0.8f,  0.6f },
    { { -2.0f,  0.7f,  1.5f },  1.2f,  0.4f },
    { {  3.0f,  0.4f,  0.0f },  0.7f,  0.8f },
    { {  2.5f,  1.0f, -2.0f }, -1.0f,  0.5f },
    { {  0.0f,  0.3f, -4.0f },  0.9f,  0.7f },
    { {  1.5f,  0.2f, -6.0f }, -0.6f,  0.4f },
    { {  0.0f, -0.5f, -1.0f },  0.0f, 20.0f }
};

bool ForgeGpuCreateLesson07(ForgeGpuDemo *demo)
{
    demo->lesson.vertex_buffer = ForgeGpuCreateBufferWithData(
        demo->device, SDL_GPU_BUFFERUSAGE_VERTEX,
        kForgeGpuCubeVertices, sizeof(kForgeGpuCubeVertices));
    demo->lesson.index_buffer = ForgeGpuCreateBufferWithData(
        demo->device, SDL_GPU_BUFFERUSAGE_INDEX,
        kForgeGpuCubeIndices, sizeof(kForgeGpuCubeIndices));
    demo->lesson.camera_position = { 0.0f, 1.6f, 6.0f };
    demo->lesson.camera_yaw = 0.0f;
    demo->lesson.camera_pitch = 0.0f;
    demo->lesson.move_speed = 3.0f;
    demo->lesson.last_ticks = SDL_GetTicks();
    return demo->lesson.vertex_buffer && demo->lesson.index_buffer && ForgeGpuCreateLesson07Pipeline(demo);
}

void ForgeGpuRenderLesson07(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    Uint32 width,
    Uint32 height)
{
    SDL_GPUBufferBinding vertex_binding;
    SDL_GPUBufferBinding index_binding;
    Mat4 view;
    Mat4 projection;
    const float t = ForgeGpuFrameTimeSeconds(demo);

    ForgeGpuUpdateCameraFromInput(demo);
    ForgeGpuCameraViewProjection(demo, width, height, 100.0f, &view, &projection);

    SDL_zero(vertex_binding);
    vertex_binding.buffer = demo->lesson.vertex_buffer;
    SDL_zero(index_binding);
    index_binding.buffer = demo->lesson.index_buffer;

    SDL_BindGPUGraphicsPipeline(render_pass, demo->lesson.pipeline);
    SDL_BindGPUVertexBuffers(render_pass, 0, &vertex_binding, 1);
    SDL_BindGPUIndexBuffer(render_pass, &index_binding, SDL_GPU_INDEXELEMENTSIZE_16BIT);

    for (int i = 0; i < (int)SDL_arraysize(kCubes); i += 1) {
        const CubeInstance *cube = &kCubes[i];
        const Mat4 translation = mat4_translate(cube->position);
        const Mat4 rotation = mat4_rotate_y(t * cube->rotation_speed);
        const Mat4 scale = mat4_scale(cube->scale);
        const Mat4 model = mat4_multiply(translation, mat4_multiply(rotation, scale));
        UniformMvp uniforms;

        uniforms.mvp = mat4_multiply(projection, mat4_multiply(view, model));
        SDL_PushGPUVertexUniformData(command_buffer, 0, &uniforms, sizeof(uniforms));
        SDL_DrawGPUIndexedPrimitives(render_pass, 36, 1, 0, 0, 0);
    }
}
