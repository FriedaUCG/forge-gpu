#include "forge_gpu_lessons.h"

#include "forge_gpu_camera.h"
#include "forge_gpu_gpu_helpers.h"
#include "forge_gpu_lesson_common.h"
#include "forge_gpu_pipelines.h"
#include "shaders/generated/forge_gpu_lesson_10_shaders.h"

bool ForgeGpuCreateLesson10(ForgeGpuDemo *demo)
{
    demo->lesson.camera_position = { 0.0f, 0.0f, 4.0f };
    demo->lesson.camera_yaw = 0.0f;
    demo->lesson.camera_pitch = 0.0f;
    demo->lesson.move_speed = 3.0f;
    demo->lesson.last_ticks = SDL_GetTicks();
    return ForgeGpuLoadLessonScene(demo, "models/Suzanne/Suzanne.gltf") &&
           ForgeGpuCreateMeshPipeline(
               demo,
               lesson10_lighting_vert_wgsl, lesson10_lighting_vert_wgsl_size,
               lesson10_lighting_vert_msl, lesson10_lighting_vert_msl_size,
               lesson10_lighting_frag_wgsl, lesson10_lighting_frag_wgsl_size,
               lesson10_lighting_frag_msl, lesson10_lighting_frag_msl_size,
               1, 1, &demo->lesson.pipeline);
}

void ForgeGpuRenderLesson10(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    Uint32 width,
    Uint32 height)
{
    ForgeGpuUpdateCameraFromInput(demo);
    ForgeGpuRenderLoadedScene(demo, command_buffer, render_pass, width, height, demo->lesson.pipeline, true);
}
