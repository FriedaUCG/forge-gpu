#include "forge_gpu_lessons.h"

#include "forge_gpu_camera.h"
#include "forge_gpu_gpu_helpers.h"
#include "forge_gpu_lesson_common.h"
#include "forge_gpu_pipelines.h"
#include "shaders/generated/forge_gpu_lesson_09_shaders.h"

bool ForgeGpuCreateLesson09(ForgeGpuDemo *demo)
{
    demo->lesson.camera_position = { 6.0f, 3.0f, 6.0f };
    demo->lesson.camera_yaw = 45.0f * FORGE_GPU_DEG2RAD;
    demo->lesson.camera_pitch = -13.0f * FORGE_GPU_DEG2RAD;
    demo->lesson.move_speed = 5.0f;
    demo->lesson.last_ticks = SDL_GetTicks();
    return ForgeGpuLoadLessonScene(demo, "models/CesiumMilkTruck/CesiumMilkTruck.gltf") &&
           ForgeGpuCreateMeshPipeline(
               demo,
               lesson09_scene_vert_wgsl, lesson09_scene_vert_wgsl_size,
               lesson09_scene_vert_msl, lesson09_scene_vert_msl_size,
               lesson09_scene_frag_wgsl, lesson09_scene_frag_wgsl_size,
               lesson09_scene_frag_msl, lesson09_scene_frag_msl_size,
               1, 1, &demo->lesson.pipeline);
}

void ForgeGpuRenderLesson09(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    Uint32 width,
    Uint32 height)
{
    ForgeGpuUpdateCameraFromInput(demo);
    ForgeGpuRenderLoadedScene(demo, command_buffer, render_pass, width, height, demo->lesson.pipeline, false);
}
