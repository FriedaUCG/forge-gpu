#include "forge_gpu_lessons.h"

#include "forge_gpu_camera.h"
#include "forge_gpu_gpu_helpers.h"
#include "forge_gpu_math.h"
#include "forge_gpu_pipelines.h"
#include "forge_gpu_scene.h"
#include "shaders/generated/forge_gpu_lesson_08_shaders.h"

struct Lesson08State
{
    Uint32 vertex_count;
};

static Lesson08State *lesson08_state(ForgeGpuDemo *demo)
{
    return (Lesson08State *)demo->lesson.private_state;
}

bool ForgeGpuCreateLesson08(ForgeGpuDemo *demo)
{
    char model_path[FORGE_GPU_MAX_PATH];
    char texture_path[FORGE_GPU_MAX_PATH];
    ForgeGpuLoadedMesh mesh;
    Lesson08State *state;
    bool ok;

    SDL_zero(mesh);
    ok = ForgeGpuJoinAssetPath(demo, "models/space-shuttle/space-shuttle.obj", model_path, sizeof(model_path)) &&
         ForgeGpuJoinAssetPath(demo, "models/space-shuttle/ShuttleDiffuseMap.png", texture_path, sizeof(texture_path));
    if (!ok) {
        SDL_SetError("lesson 08 asset path too long");
        return false;
    }
    if (!ForgeGpuLoadObjMesh(model_path, &mesh)) {
        return false;
    }
    state = (Lesson08State *)SDL_calloc(1, sizeof(*state));
    if (!state) {
        ForgeGpuFreeLoadedMesh(&mesh);
        SDL_OutOfMemory();
        return false;
    }
    demo->lesson.private_state = state;
    demo->lesson.vertex_buffer = ForgeGpuCreateBufferWithData(
        demo->device,
        SDL_GPU_BUFFERUSAGE_VERTEX,
        mesh.vertices,
        mesh.vertex_count * (Uint32)sizeof(*mesh.vertices));
    state->vertex_count = mesh.vertex_count;
    ForgeGpuFreeLoadedMesh(&mesh);
    demo->lesson.texture = ForgeGpuLoadRgbaTexturePath(demo, texture_path, true);
    demo->lesson.samplers[0] = ForgeGpuCreateSampler(
        demo->device,
        SDL_GPU_FILTER_LINEAR, SDL_GPU_FILTER_LINEAR,
        SDL_GPU_SAMPLERMIPMAPMODE_LINEAR,
        1000.0f);
    demo->lesson.camera_position = { 0.0f, 12.0f, 50.0f };
    demo->lesson.camera_yaw = 0.0f;
    demo->lesson.camera_pitch = 0.0f;
    demo->lesson.move_speed = 8.0f;
    demo->lesson.last_ticks = SDL_GetTicks();
    return demo->lesson.vertex_buffer && demo->lesson.texture && demo->lesson.samplers[0] &&
           ForgeGpuCreateMeshPipeline(
               demo,
               lesson08_mesh_vert_wgsl, lesson08_mesh_vert_wgsl_size,
               lesson08_mesh_vert_msl, lesson08_mesh_vert_msl_size,
               lesson08_mesh_frag_wgsl, lesson08_mesh_frag_wgsl_size,
               lesson08_mesh_frag_msl, lesson08_mesh_frag_msl_size,
               1, 0, &demo->lesson.pipeline);
}

void ForgeGpuRenderLesson08(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    Uint32 width,
    Uint32 height)
{
    SDL_GPUBufferBinding vertex_binding;
    SDL_GPUTextureSamplerBinding sampler_binding;
    Mat4 view;
    Mat4 projection;
    UniformMvp uniforms;
    const float t = ForgeGpuFrameTimeSeconds(demo);
    const Mat4 model = mat4_rotate_y((FORGE_GPU_PI * 1.15f) + t * 0.3f);
    Lesson08State *state = lesson08_state(demo);

    if (!state) {
        return;
    }

    ForgeGpuUpdateCameraFromInput(demo);
    ForgeGpuCameraViewProjection(demo, width, height, 500.0f, &view, &projection);

    uniforms.mvp = mat4_multiply(projection, mat4_multiply(view, model));
    SDL_PushGPUVertexUniformData(command_buffer, 0, &uniforms, sizeof(uniforms));

    SDL_zero(vertex_binding);
    vertex_binding.buffer = demo->lesson.vertex_buffer;
    SDL_zero(sampler_binding);
    sampler_binding.texture = demo->lesson.texture;
    sampler_binding.sampler = demo->lesson.samplers[0];

    SDL_BindGPUGraphicsPipeline(render_pass, demo->lesson.pipeline);
    SDL_BindGPUVertexBuffers(render_pass, 0, &vertex_binding, 1);
    SDL_BindGPUFragmentSamplers(render_pass, 0, &sampler_binding, 1);
    SDL_DrawGPUPrimitives(render_pass, state->vertex_count, 1, 0, 0);
}

void ForgeGpuDestroyLesson08(ForgeGpuDemo *demo)
{
    Lesson08State *state = lesson08_state(demo);

    if (!state) {
        return;
    }
    SDL_free(state);
    demo->lesson.private_state = nullptr;
}
