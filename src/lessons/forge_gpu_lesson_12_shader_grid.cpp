#include "forge_gpu_lessons.h"

#include "forge_gpu_camera.h"
#include "forge_gpu_gpu_helpers.h"
#include "forge_gpu_lesson_common.h"
#include "forge_gpu_math.h"
#include "forge_gpu_pipelines.h"
#include "shaders/generated/forge_gpu_lesson_12_shaders.h"

bool ForgeGpuCreateLesson12(ForgeGpuDemo *demo)
{
    demo->lesson.camera_position = { 6.0f, 3.0f, 6.0f };
    demo->lesson.camera_yaw = 45.0f * FORGE_GPU_DEG2RAD;
    demo->lesson.camera_pitch = -13.0f * FORGE_GPU_DEG2RAD;
    demo->lesson.move_speed = 3.0f;
    demo->lesson.last_ticks = SDL_GetTicks();
    return ForgeGpuCreateGridBuffers(demo) &&
           ForgeGpuLoadLessonScene(demo, "models/CesiumMilkTruck/CesiumMilkTruck.gltf") &&
           ForgeGpuCreateGridPipeline(demo) &&
           ForgeGpuCreateMeshPipeline(
               demo,
               lesson12_lighting_vert_wgsl, lesson12_lighting_vert_wgsl_size,
               lesson12_lighting_vert_msl, lesson12_lighting_vert_msl_size,
               lesson12_lighting_frag_wgsl, lesson12_lighting_frag_wgsl_size,
               lesson12_lighting_frag_msl, lesson12_lighting_frag_msl_size,
               1, 1, &demo->lesson.secondary_pipeline);
}

void ForgeGpuRenderLesson12(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    Uint32 width,
    Uint32 height)
{
    Mat4 view;
    Mat4 projection;
    Mat4 vp;
    Vec3 light_dir = vec3_normalize({ 1.0f, 1.0f, 1.0f });

    ForgeGpuUpdateCameraFromInput(demo);
    ForgeGpuCameraViewProjection(demo, width, height, 100.0f, &view, &projection);
    vp = mat4_multiply(projection, view);

    ForgeGpuDrawBasicGrid(demo, command_buffer, render_pass, demo->lesson.pipeline, vp, &light_dir);
    ForgeGpuRenderLoadedScene(demo, command_buffer, render_pass, width, height, demo->lesson.secondary_pipeline, true);
}
