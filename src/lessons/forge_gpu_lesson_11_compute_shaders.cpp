#include "forge_gpu_lessons.h"

#include "forge_gpu_browser_status.h"
#include "forge_gpu_gpu_helpers.h"
#include "forge_gpu_lesson_common.h"
#include "forge_gpu_pipelines.h"
#include "forge_gpu_shader_layouts.h"
#include "shaders/generated/forge_gpu_lesson_11_shaders.h"
#include "imgui.h"

static bool run_compute_plasma_lesson(ForgeGpuDemo *demo, SDL_GPUCommandBuffer *command_buffer)
{
    SDL_GPUStorageTextureReadWriteBinding storage_binding;
    SDL_GPUComputePass *compute_pass;
    ComputeUniforms uniforms;

    SDL_zero(storage_binding);
    storage_binding.texture = demo->lesson.texture;
    storage_binding.mip_level = 0;
    storage_binding.layer = 0;
    storage_binding.cycle = true;

    compute_pass = SDL_BeginGPUComputePass(command_buffer, &storage_binding, 1, nullptr, 0);
    if (!compute_pass) {
        return false;
    }

    uniforms.time = ForgeGpuFrameTimeSeconds(demo);
    uniforms.width = (float)FORGE_GPU_PLASMA_SIZE;
    uniforms.height = (float)FORGE_GPU_PLASMA_SIZE;
    uniforms.pad = 0.0f;
    SDL_BindGPUComputePipeline(compute_pass, demo->lesson.compute_pipeline);
    SDL_PushGPUComputeUniformData(command_buffer, 0, &uniforms, sizeof(uniforms));
    SDL_DispatchGPUCompute(
        compute_pass,
        FORGE_GPU_PLASMA_SIZE / FORGE_GPU_COMPUTE_WORKGROUP_SIZE,
        FORGE_GPU_PLASMA_SIZE / FORGE_GPU_COMPUTE_WORKGROUP_SIZE,
        1);
    SDL_EndGPUComputePass(compute_pass);
    return true;
}

static void render_compute_fullscreen_lesson(ForgeGpuDemo *demo, SDL_GPURenderPass *render_pass)
{
    SDL_GPUTextureSamplerBinding sampler_binding;

    SDL_zero(sampler_binding);
    sampler_binding.texture = demo->lesson.texture;
    sampler_binding.sampler = demo->lesson.samplers[0];

    SDL_BindGPUGraphicsPipeline(render_pass, demo->lesson.pipeline);
    SDL_BindGPUFragmentSamplers(render_pass, 0, &sampler_binding, 1);
    SDL_DrawGPUPrimitives(render_pass, 3, 1, 0, 0);
}

static void render_compute_fullscreen_draw(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    Uint32 width,
    Uint32 height)
{
    (void)command_buffer;
    (void)width;
    (void)height;
    render_compute_fullscreen_lesson(demo, render_pass);
}

bool ForgeGpuCreateLesson11(ForgeGpuDemo *demo)
{
    SDL_GPUTextureCreateInfo texture_info;

    if (!SDL_GPUTextureSupportsFormat(
            demo->device,
            SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
            SDL_GPU_TEXTURETYPE_2D,
            SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE)) {
        SDL_SetError("lesson 11 requires sampled RGBA8 storage texture writes");
        return false;
    }

    SDL_zero(texture_info);
    texture_info.type = SDL_GPU_TEXTURETYPE_2D;
    texture_info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    texture_info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE;
    texture_info.width = FORGE_GPU_PLASMA_SIZE;
    texture_info.height = FORGE_GPU_PLASMA_SIZE;
    texture_info.layer_count_or_depth = 1;
    texture_info.num_levels = 1;
    texture_info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    demo->lesson.texture = SDL_CreateGPUTexture(demo->device, &texture_info);
    demo->lesson.samplers[0] = ForgeGpuCreateSampler(
        demo->device,
        SDL_GPU_FILTER_LINEAR, SDL_GPU_FILTER_LINEAR,
        SDL_GPU_SAMPLERMIPMAPMODE_NEAREST,
        0.0f);
    demo->lesson.compute_pipeline = ForgeGpuCreateComputePipelineWithResourceLayout(
        demo->device,
        lesson11_plasma_comp_wgsl, lesson11_plasma_comp_wgsl_size,
        lesson11_plasma_comp_msl, lesson11_plasma_comp_msl_size,
        ForgeGpuComputePipelineLayout_lesson11_plasma_comp(),
        FORGE_GPU_COMPUTE_WORKGROUP_SIZE,
        FORGE_GPU_COMPUTE_WORKGROUP_SIZE,
        1);
    demo->lesson.last_ticks = SDL_GetTicks();
    return demo->lesson.texture && demo->lesson.samplers[0] && demo->lesson.compute_pipeline &&
           ForgeGpuCreateFullscreenPipeline(demo);
}

bool ForgeGpuRenderLesson11(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPUTexture *swapchain_texture,
    Uint32 width,
    Uint32 height)
{
    if (!run_compute_plasma_lesson(demo, command_buffer)) {
        return false;
    }
    return ForgeGpuRenderDefaultLessonPass(demo, command_buffer, swapchain_texture, width, height, render_compute_fullscreen_draw);
}

void ForgeGpuDebugLesson11(ForgeGpuDemo *demo)
{
    (void)demo;
    ImGui::Text("Storage texture: %ux%u", FORGE_GPU_PLASMA_SIZE, FORGE_GPU_PLASMA_SIZE);
}

void ForgeGpuExportLesson11Metrics(ForgeGpuDemo *demo)
{
    (void)demo;
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuComputeStorageTexture", 1.0);
}
