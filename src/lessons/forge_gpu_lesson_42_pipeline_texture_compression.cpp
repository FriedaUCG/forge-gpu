#include "forge_gpu_lessons.h"

#include "forge_gpu_browser_status.h"
#include "forge_gpu_camera.h"
#include "forge_gpu_processed_scene_renderer.h"
#include "imgui.h"

#define LESSON42_FAR_PLANE 200.0f
#define LESSON42_MOVE_SPEED 5.0f
#define LESSON42_MOUSE_SENSITIVITY 0.003f
#define LESSON42_PITCH_CLAMP 1.5f
#define LESSON42_CAM_START_X -8.0f
#define LESSON42_CAM_START_Y 7.0f
#define LESSON42_CAM_START_Z 12.0f
#define LESSON42_CAM_START_YAW -0.65f
#define LESSON42_CAM_START_PITCH -0.55f
#define LESSON42_MODEL_SCALE 20.0f
#define LESSON42_BYTES_PER_MB (1024.0 * 1024.0)
#define LESSON42_VRAM_EPSILON 0.001
#define LESSON42_MAX_FTEX_MIPS 32u

typedef struct Lesson42State
{
    ForgeGpuProcessedSceneRenderer renderer;
    ForgeGpuProcessedSceneModel chess;
    float load_time_ms;
    Uint32 bc7_srgb_count;
    Uint32 bc7_unorm_count;
    Uint32 bc5_count;
} Lesson42State;

static Lesson42State *lesson42_state(ForgeGpuDemo *demo)
{
    return (Lesson42State *)demo->lesson.private_state;
}

static const char *lesson42_format_name(SDL_GPUTextureFormat format)
{
    switch (format) {
    case SDL_GPU_TEXTUREFORMAT_BC7_RGBA_UNORM_SRGB:
        return "BC7_RGBA_UNORM_SRGB";
    case SDL_GPU_TEXTUREFORMAT_BC7_RGBA_UNORM:
        return "BC7_RGBA_UNORM";
    case SDL_GPU_TEXTUREFORMAT_BC5_RG_UNORM:
        return "BC5_RG_UNORM";
    default:
        return "unknown";
    }
}

static bool lesson42_ftex_format_to_sdl(Uint32 ftex_format, SDL_GPUTextureFormat *out_format)
{
    switch (ftex_format) {
    case FORGE_GPU_PROCESSED_FTEX_BC7_SRGB:
        *out_format = SDL_GPU_TEXTUREFORMAT_BC7_RGBA_UNORM_SRGB;
        return true;
    case FORGE_GPU_PROCESSED_FTEX_BC7_UNORM:
        *out_format = SDL_GPU_TEXTUREFORMAT_BC7_RGBA_UNORM;
        return true;
    case FORGE_GPU_PROCESSED_FTEX_BC5_UNORM:
        *out_format = SDL_GPU_TEXTUREFORMAT_BC5_RG_UNORM;
        return true;
    default:
        SDL_SetError("lesson 42 unsupported .ftex format %u", (unsigned)ftex_format);
        return false;
    }
}

static bool lesson42_validate_ftex_slot(
    const char *source_path,
    Uint32 ftex_format,
    bool srgb,
    bool normal_map)
{
    if (normal_map) {
        if (ftex_format != FORGE_GPU_PROCESSED_FTEX_BC5_UNORM) {
            SDL_SetError("lesson 42 normal map '%s' did not use BC5", source_path);
            return false;
        }
        return true;
    }
    if (srgb) {
        if (ftex_format != FORGE_GPU_PROCESSED_FTEX_BC7_SRGB) {
            SDL_SetError("lesson 42 sRGB material texture '%s' did not use BC7 sRGB", source_path);
            return false;
        }
        return true;
    }
    if (ftex_format != FORGE_GPU_PROCESSED_FTEX_BC7_UNORM) {
        SDL_SetError("lesson 42 linear material texture '%s' did not use BC7 UNORM", source_path);
        return false;
    }
    return true;
}

static bool lesson42_wait_for_upload(SDL_GPUDevice *device, SDL_GPUCommandBuffer *command_buffer)
{
    SDL_GPUFence *fence = SDL_SubmitGPUCommandBufferAndAcquireFence(command_buffer);
    bool ok = false;

    if (!fence) {
        return false;
    }

    ok = SDL_WaitForGPUFences(device, true, &fence, 1);
    SDL_ReleaseGPUFence(device, fence);
    return ok;
}

static bool lesson42_upload_compressed_texture(
    SDL_GPUDevice *device,
    SDL_GPUTexture *texture,
    SDL_GPUTextureFormat format,
    const ForgeGpuProcessedCompressedTexture *compressed)
{
    SDL_GPUTransferBufferCreateInfo transfer_info;
    SDL_GPUTransferBuffer *transfer;
    SDL_GPUCommandBuffer *command_buffer;
    SDL_GPUCopyPass *copy_pass;
    Uint32 offsets[LESSON42_MAX_FTEX_MIPS];
    Uint64 total_size64 = 0;
    Uint8 *mapped;

    if (compressed->mip_count > SDL_arraysize(offsets)) {
        SDL_SetError("lesson 42 .ftex mip count exceeds uploader capacity");
        return false;
    }

    for (Uint32 i = 0; i < compressed->mip_count; i += 1) {
        const ForgeGpuProcessedCompressedMip *mip = &compressed->mips[i];
        Uint32 expected_size = SDL_CalculateGPUTextureFormatSize(format, mip->width, mip->height, 1);

        if (expected_size == 0 || mip->data_size != expected_size) {
            SDL_SetError(
                "lesson 42 .ftex mip %u has %u bytes, expected %u",
                (unsigned)i,
                (unsigned)mip->data_size,
                (unsigned)expected_size);
            return false;
        }
        offsets[i] = (Uint32)total_size64;
        total_size64 += mip->data_size;
        if (total_size64 > SDL_MAX_UINT32) {
            SDL_SetError("lesson 42 compressed texture upload exceeds transfer-buffer size");
            return false;
        }
    }

    SDL_zero(transfer_info);
    transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transfer_info.size = (Uint32)total_size64;
    transfer = SDL_CreateGPUTransferBuffer(device, &transfer_info);
    if (!transfer) {
        return false;
    }

    mapped = (Uint8 *)SDL_MapGPUTransferBuffer(device, transfer, false);
    if (!mapped) {
        SDL_ReleaseGPUTransferBuffer(device, transfer);
        return false;
    }
    for (Uint32 i = 0; i < compressed->mip_count; i += 1) {
        SDL_memcpy(mapped + offsets[i], compressed->mips[i].data, compressed->mips[i].data_size);
    }
    SDL_UnmapGPUTransferBuffer(device, transfer);

    command_buffer = SDL_AcquireGPUCommandBuffer(device);
    if (!command_buffer) {
        SDL_ReleaseGPUTransferBuffer(device, transfer);
        return false;
    }

    copy_pass = SDL_BeginGPUCopyPass(command_buffer);
    if (!copy_pass) {
        SDL_CancelGPUCommandBuffer(command_buffer);
        SDL_ReleaseGPUTransferBuffer(device, transfer);
        return false;
    }

    for (Uint32 i = 0; i < compressed->mip_count; i += 1) {
        const ForgeGpuProcessedCompressedMip *mip = &compressed->mips[i];
        SDL_GPUTextureTransferInfo source;
        SDL_GPUTextureRegion destination;

        SDL_zero(source);
        source.transfer_buffer = transfer;
        source.offset = offsets[i];
        source.pixels_per_row = mip->width;
        source.rows_per_layer = mip->height;

        SDL_zero(destination);
        destination.texture = texture;
        destination.mip_level = i;
        destination.w = mip->width;
        destination.h = mip->height;
        destination.d = 1;

        SDL_UploadToGPUTexture(copy_pass, &source, &destination, false);
    }
    SDL_EndGPUCopyPass(copy_pass);

    if (!lesson42_wait_for_upload(device, command_buffer)) {
        SDL_ReleaseGPUTransferBuffer(device, transfer);
        return false;
    }

    SDL_ReleaseGPUTransferBuffer(device, transfer);
    return true;
}

static SDL_GPUTexture *lesson42_create_compressed_texture(
    ForgeGpuDemo *demo,
    const char *source_path,
    SDL_GPUTextureFormat format,
    const ForgeGpuProcessedCompressedTexture *compressed)
{
    SDL_GPUTextureCreateInfo texture_info;
    SDL_GPUTexture *texture;

    if (!SDL_GPUTextureSupportsFormat(
            demo->device,
            format,
            SDL_GPU_TEXTURETYPE_2D,
            SDL_GPU_TEXTUREUSAGE_SAMPLER)) {
        SDL_SetError("lesson 42 requires %s sampled texture support", lesson42_format_name(format));
        return nullptr;
    }

    SDL_zero(texture_info);
    texture_info.type = SDL_GPU_TEXTURETYPE_2D;
    texture_info.format = format;
    texture_info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    texture_info.width = compressed->width;
    texture_info.height = compressed->height;
    texture_info.layer_count_or_depth = 1;
    texture_info.num_levels = compressed->mip_count;
    texture_info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    texture = SDL_CreateGPUTexture(demo->device, &texture_info);
    if (!texture) {
        return nullptr;
    }

    if (!lesson42_upload_compressed_texture(demo->device, texture, format, compressed)) {
        char upload_error[256];
        SDL_strlcpy(upload_error, SDL_GetError(), sizeof(upload_error));
        SDL_ReleaseGPUTexture(demo->device, texture);
        SDL_SetError("lesson 42 failed to upload compressed texture '%s': %s", source_path, upload_error);
        return nullptr;
    }
    return texture;
}

static SDL_GPUTexture *lesson42_load_compressed_material_texture(
    ForgeGpuDemo *demo,
    ForgeGpuProcessedSceneRenderer *renderer,
    ForgeGpuProcessedSceneModel *model,
    const char *base_relative,
    const char *source_path,
    SDL_GPUTextureFormat requested_format,
    bool srgb,
    bool normal_map,
    void *userdata)
{
    char image_path[FORGE_GPU_MAX_PATH];
    char ftex_path[FORGE_GPU_MAX_PATH];
    ForgeGpuProcessedTextureCompressionInfo info;
    ForgeGpuProcessedCompressedTexture compressed;
    SDL_GPUTextureFormat format;
    SDL_GPUTexture *texture;
    Uint64 compressed_bytes = 0;

    (void)renderer;
    (void)requested_format;
    (void)userdata;

    if (!ForgeGpuProcessedSceneJoinModelPath(demo, base_relative, source_path, image_path, sizeof(image_path)) ||
        !ForgeGpuLoadProcessedTextureCompressionSidecar(image_path, &info) ||
        !lesson42_validate_ftex_slot(source_path, info.ftex_format, srgb, normal_map) ||
        !lesson42_ftex_format_to_sdl(info.ftex_format, &format)) {
        return nullptr;
    }

    texture = ForgeGpuProcessedSceneFindCachedTexture(model, source_path, format);
    if (texture) {
        return texture;
    }

    if (!ForgeGpuProcessedSceneJoinModelPath(demo, base_relative, info.ftex_file, ftex_path, sizeof(ftex_path)) ||
        !ForgeGpuLoadProcessedFtexV1(ftex_path, &compressed)) {
        return nullptr;
    }

    if (compressed.width != info.output_width ||
        compressed.height != info.output_height ||
        compressed.mip_count != info.mip_count ||
        compressed.format != info.ftex_format) {
        SDL_SetError("lesson 42 .ftex facts did not match sidecar for '%s'", source_path);
        ForgeGpuFreeProcessedCompressedTexture(&compressed);
        return nullptr;
    }
    for (Uint32 i = 0; i < compressed.mip_count; i += 1) {
        compressed_bytes += compressed.mips[i].data_size;
    }

    texture = lesson42_create_compressed_texture(demo, source_path, format, &compressed);
    ForgeGpuFreeProcessedCompressedTexture(&compressed);
    if (!texture) {
        return nullptr;
    }

    if (!ForgeGpuProcessedSceneCacheTexture(
            model,
            source_path,
            format,
            texture,
            true,
            compressed_bytes,
            ForgeGpuEstimateProcessedRgba8MipBytes(info.output_width, info.output_height))) {
        SDL_ReleaseGPUTexture(demo->device, texture);
        return nullptr;
    }
    return texture;
}

static void lesson42_refresh_format_counts(Lesson42State *state)
{
    state->bc7_srgb_count = 0;
    state->bc7_unorm_count = 0;
    state->bc5_count = 0;

    for (Uint32 i = 0; i < state->chess.texture_cache_count; i += 1) {
        switch (state->chess.texture_cache[i].format) {
        case SDL_GPU_TEXTUREFORMAT_BC7_RGBA_UNORM_SRGB:
            state->bc7_srgb_count += 1;
            break;
        case SDL_GPU_TEXTUREFORMAT_BC7_RGBA_UNORM:
            state->bc7_unorm_count += 1;
            break;
        case SDL_GPU_TEXTUREFORMAT_BC5_RG_UNORM:
            state->bc5_count += 1;
            break;
        default:
            break;
        }
    }
}

bool ForgeGpuCreateLesson42(ForgeGpuDemo *demo)
{
    Lesson42State *state = (Lesson42State *)SDL_calloc(1, sizeof(*state));
    Uint64 t0;
    Uint64 t1;

    if (!state) {
        SDL_OutOfMemory();
        return false;
    }
    demo->lesson.private_state = state;

    if (!ForgeGpuProcessedSceneRendererCreate(demo, &state->renderer)) {
        return false;
    }

    t0 = SDL_GetPerformanceCounter();
    if (!ForgeGpuProcessedSceneLoadModel(
            demo,
            &state->renderer,
            &state->chess,
            "processed/42-pipeline-texture-compression/ABeautifulGame",
            "ABeautifulGame",
            lesson42_load_compressed_material_texture,
            nullptr)) {
        return false;
    }
    t1 = SDL_GetPerformanceCounter();
    state->load_time_ms = (float)((double)(t1 - t0) / (double)SDL_GetPerformanceFrequency() * 1000.0);
    lesson42_refresh_format_counts(state);

    demo->lesson.camera_position = { LESSON42_CAM_START_X, LESSON42_CAM_START_Y, LESSON42_CAM_START_Z };
    demo->lesson.camera_yaw = LESSON42_CAM_START_YAW;
    demo->lesson.camera_pitch = LESSON42_CAM_START_PITCH;
    demo->lesson.move_speed = LESSON42_MOVE_SPEED;
    demo->lesson.mouse_sensitivity = LESSON42_MOUSE_SENSITIVITY;
    demo->lesson.pitch_clamp = LESSON42_PITCH_CLAMP;
    demo->lesson.last_ticks = SDL_GetTicks();
    return true;
}

bool ForgeGpuRenderLesson42(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPUTexture *swapchain_texture,
    Uint32 width,
    Uint32 height)
{
    Lesson42State *state = lesson42_state(demo);
    Mat4 view;
    Mat4 projection;
    Mat4 camera_vp;
    Mat4 light_vp;
    Mat4 placement;
    SDL_GPURenderPass *render_pass;

    if (!state) {
        SDL_SetError("lesson 42 internal state is missing");
        return false;
    }
    if (!ForgeGpuProcessedSceneRendererEnsureMainDepth(demo, &state->renderer, width, height)) {
        return false;
    }

    ForgeGpuUpdateCameraFromInput(demo);
    ForgeGpuCameraViewProjection(demo, width, height, LESSON42_FAR_PLANE, &view, &projection);
    camera_vp = mat4_multiply(projection, view);
    light_vp = ForgeGpuProcessedSceneLightViewProjection();
    placement = mat4_scale(LESSON42_MODEL_SCALE);

    ForgeGpuProcessedSceneRendererBeginFrame(&state->renderer);
    ForgeGpuProcessedSceneResetModelDrawCounts(&state->chess);

    render_pass = ForgeGpuProcessedSceneBeginShadowPass(command_buffer, &state->renderer);
    if (!render_pass) {
        return false;
    }
    if (!ForgeGpuProcessedSceneDrawModel(demo, command_buffer, render_pass, &state->renderer, &state->chess, placement, camera_vp, light_vp, true)) {
        SDL_EndGPURenderPass(render_pass);
        return false;
    }
    SDL_EndGPURenderPass(render_pass);
    state->renderer.shadow_pass_rendered = true;

    render_pass = ForgeGpuProcessedSceneBeginMainPass(command_buffer, &state->renderer, swapchain_texture);
    if (!render_pass) {
        return false;
    }
    ForgeGpuProcessedSceneDrawGrid(demo, command_buffer, render_pass, &state->renderer, camera_vp, light_vp);
    if (!ForgeGpuProcessedSceneDrawModel(demo, command_buffer, render_pass, &state->renderer, &state->chess, placement, camera_vp, light_vp, false)) {
        SDL_EndGPURenderPass(render_pass);
        return false;
    }
    SDL_EndGPURenderPass(render_pass);
    state->renderer.main_pass_rendered = true;
    return true;
}

void ForgeGpuDebugLesson42(ForgeGpuDemo *demo)
{
    Lesson42State *state = lesson42_state(demo);
    double compressed_mb;
    double uncompressed_mb;
    double savings_pct = 0.0;

    if (!state) {
        return;
    }

    compressed_mb = (double)state->chess.vram.compressed_bytes / LESSON42_BYTES_PER_MB;
    uncompressed_mb = (double)state->chess.vram.uncompressed_bytes / LESSON42_BYTES_PER_MB;
    if (uncompressed_mb > LESSON42_VRAM_EPSILON) {
        savings_pct = (1.0 - compressed_mb / uncompressed_mb) * 100.0;
    }

    ImGui::Text("Model: ABeautifulGame");
    ImGui::Text("Textures: %u total, %u compressed",
        state->chess.vram.total_texture_count,
        state->chess.vram.compressed_texture_count);
    ImGui::Text("Formats: BC7 sRGB %u, BC7 UNORM %u, BC5 %u",
        state->bc7_srgb_count,
        state->bc7_unorm_count,
        state->bc5_count);
    ImGui::Text("VRAM compressed: %.1f MB", compressed_mb);
    ImGui::Text("VRAM uncompressed: %.1f MB", uncompressed_mb);
    ImGui::Text("Savings: %.0f%%", savings_pct);
    ImGui::Text("Load time: %.0f ms", state->load_time_ms);
    ImGui::Text("Draw calls: %u (%u transparent)",
        state->chess.draw_calls,
        state->chess.transparent_draw_calls);
    ImGui::Text("Passes: shadow %s, main %s",
        state->renderer.shadow_pass_rendered ? "yes" : "no",
        state->renderer.main_pass_rendered ? "yes" : "no");
}

void ForgeGpuExportLesson42Metrics(ForgeGpuDemo *demo)
{
    Lesson42State *state = lesson42_state(demo);

    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson42TextureCompression", state ? 1.0 : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson42ShadowPass", state && state->renderer.shadow_pass_rendered ? 1.0 : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson42MainPass", state && state->renderer.main_pass_rendered ? 1.0 : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson42Nodes", state ? (double)state->chess.scene.node_count : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson42Meshes", state ? (double)state->chess.scene.mesh_count : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson42Materials", state ? (double)state->chess.materials.material_count : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson42Textures", state ? (double)state->chess.vram.total_texture_count : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson42CompressedTextures", state ? (double)state->chess.vram.compressed_texture_count : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson42Bc7SrgbTextures", state ? (double)state->bc7_srgb_count : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson42Bc7UnormTextures", state ? (double)state->bc7_unorm_count : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson42Bc5Textures", state ? (double)state->bc5_count : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson42DrawCalls", state ? (double)state->chess.draw_calls : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson42TransparentDrawCalls", state ? (double)state->chess.transparent_draw_calls : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson42CompressedBytes", state ? (double)state->chess.vram.compressed_bytes : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson42UncompressedBytes", state ? (double)state->chess.vram.uncompressed_bytes : 0.0);
}

void ForgeGpuDestroyLesson42(ForgeGpuDemo *demo)
{
    Lesson42State *state = lesson42_state(demo);

    if (!state) {
        return;
    }

    ForgeGpuProcessedSceneDestroyModel(demo->device, &state->chess);
    ForgeGpuProcessedSceneRendererDestroy(demo, &state->renderer);
    SDL_free(state);
    demo->lesson.private_state = nullptr;
}
