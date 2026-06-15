#include "forge_gpu_gpu_helpers.h"

#include "forge_gpu_math.h"

float ForgeGpuFrameTimeSeconds(ForgeGpuDemo *demo)
{
    if (demo->validation_mode) {
        return 1.25f;
    }
    return (float)(SDL_GetTicks() - demo->start_ticks) / 1000.0f;
}

static int SDLCALL compare_frame_time_ms(const void *a, const void *b)
{
    const float left = *(const float *)a;
    const float right = *(const float *)b;
    return (left > right) - (left < right);
}

static float fps_from_frame_ms(float frame_ms)
{
    return frame_ms > 0.0f ? 1000.0f / frame_ms : 0.0f;
}

static void refresh_frame_stats(FrameStats *stats)
{
    float sorted[FORGE_GPU_FRAME_TIME_WINDOW];
    Uint32 one_percent_count;
    Uint32 point_one_percent_count;
    Uint32 first_one_percent_frame;
    Uint32 first_point_one_percent_frame;
    double one_percent_total_ms = 0.0;
    double point_one_percent_total_ms = 0.0;
    Uint32 i;

    if (stats->frame_time_count == 0) {
        stats->one_percent_low_fps = 0.0f;
        stats->point_one_percent_low_fps = 0.0f;
        return;
    }

    SDL_memcpy(sorted, stats->frame_times_ms, stats->frame_time_count * sizeof(sorted[0]));
    SDL_qsort(sorted, stats->frame_time_count, sizeof(sorted[0]), compare_frame_time_ms);

    one_percent_count = SDL_max(1u, (stats->frame_time_count + 99u) / 100u);
    point_one_percent_count = SDL_max(1u, (stats->frame_time_count + 999u) / 1000u);
    first_one_percent_frame = stats->frame_time_count - one_percent_count;
    first_point_one_percent_frame = stats->frame_time_count - point_one_percent_count;

    for (i = first_one_percent_frame; i < stats->frame_time_count; i += 1) {
        one_percent_total_ms += sorted[i];
    }
    for (i = first_point_one_percent_frame; i < stats->frame_time_count; i += 1) {
        point_one_percent_total_ms += sorted[i];
    }

    stats->one_percent_low_fps = fps_from_frame_ms((float)(one_percent_total_ms / (double)one_percent_count));
    stats->point_one_percent_low_fps = fps_from_frame_ms((float)(point_one_percent_total_ms / (double)point_one_percent_count));
}

void ForgeGpuResetFrameStats(ForgeGpuDemo *demo)
{
    SDL_zero(demo->frame_stats);
    demo->frame_stats.frequency = (double)SDL_GetPerformanceFrequency();
    demo->frame_stats.last_counter = SDL_GetPerformanceCounter();
    demo->frame_stats.initialized = true;
}

void ForgeGpuUpdateFrameStats(ForgeGpuDemo *demo)
{
    FrameStats *stats = &demo->frame_stats;
    const Uint64 now = SDL_GetPerformanceCounter();
    double delta_seconds;
    float frame_ms;

    if (!stats->initialized || stats->frequency <= 0.0) {
        ForgeGpuResetFrameStats(demo);
        return;
    }

    delta_seconds = (double)(now - stats->last_counter) / stats->frequency;
    stats->last_counter = now;
    if (delta_seconds <= 0.0) {
        return;
    }

    stats->sample_count += 1;
    stats->total_seconds += delta_seconds;
    stats->average_fps = stats->total_seconds > 0.0 ? (float)((double)stats->sample_count / stats->total_seconds) : 0.0f;

    frame_ms = (float)(delta_seconds * 1000.0);
    stats->frame_times_ms[stats->frame_time_next] = frame_ms;
    stats->frame_time_next = (stats->frame_time_next + 1) % FORGE_GPU_FRAME_TIME_WINDOW;
    if (stats->frame_time_count < FORGE_GPU_FRAME_TIME_WINDOW) {
        stats->frame_time_count += 1;
    }

    stats->frames_since_refresh += 1;
    if (stats->frames_since_refresh >= FORGE_GPU_FRAME_STATS_REFRESH_INTERVAL || stats->one_percent_low_fps == 0.0f) {
        stats->frames_since_refresh = 0;
        refresh_frame_stats(stats);
    }
}

bool ForgeGpuJoinAssetPath(ForgeGpuDemo *demo, const char *relative_path, char *path, size_t path_size)
{
    const char *root = demo->asset_root ? demo->asset_root : "";
    const size_t root_len = SDL_strlen(root);

    if (root_len > 0 && root[root_len - 1] == '/') {
        return SDL_snprintf(path, path_size, "%s%s", root, relative_path) < (int)path_size;
    }
    return SDL_snprintf(path, path_size, "%s/%s", root, relative_path) < (int)path_size;
}

static bool wait_for_upload(SDL_GPUDevice *device, SDL_GPUCommandBuffer *command_buffer)
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

SDL_GPUBuffer *ForgeGpuCreateBufferWithData(
    SDL_GPUDevice *device,
    SDL_GPUBufferUsageFlags usage,
    const void *data,
    Uint32 size)
{
    SDL_GPUBufferCreateInfo buffer_info;
    SDL_GPUBuffer *buffer;
    SDL_GPUTransferBufferCreateInfo transfer_info;
    SDL_GPUTransferBuffer *transfer;
    void *mapped;
    SDL_GPUCommandBuffer *command_buffer;
    SDL_GPUCopyPass *copy_pass;
    SDL_GPUTransferBufferLocation source;
    SDL_GPUBufferRegion destination;

    SDL_zero(buffer_info);
    buffer_info.usage = usage;
    buffer_info.size = size;
    buffer = SDL_CreateGPUBuffer(device, &buffer_info);
    if (!buffer) {
        return nullptr;
    }

    SDL_zero(transfer_info);
    transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transfer_info.size = size;
    transfer = SDL_CreateGPUTransferBuffer(device, &transfer_info);
    if (!transfer) {
        SDL_ReleaseGPUBuffer(device, buffer);
        return nullptr;
    }

    mapped = SDL_MapGPUTransferBuffer(device, transfer, false);
    if (!mapped) {
        SDL_ReleaseGPUTransferBuffer(device, transfer);
        SDL_ReleaseGPUBuffer(device, buffer);
        return nullptr;
    }
    SDL_memcpy(mapped, data, size);
    SDL_UnmapGPUTransferBuffer(device, transfer);

    command_buffer = SDL_AcquireGPUCommandBuffer(device);
    if (!command_buffer) {
        SDL_ReleaseGPUTransferBuffer(device, transfer);
        SDL_ReleaseGPUBuffer(device, buffer);
        return nullptr;
    }

    copy_pass = SDL_BeginGPUCopyPass(command_buffer);
    if (!copy_pass) {
        SDL_CancelGPUCommandBuffer(command_buffer);
        SDL_ReleaseGPUTransferBuffer(device, transfer);
        SDL_ReleaseGPUBuffer(device, buffer);
        return nullptr;
    }

    SDL_zero(source);
    source.transfer_buffer = transfer;
    SDL_zero(destination);
    destination.buffer = buffer;
    destination.size = size;
    SDL_UploadToGPUBuffer(copy_pass, &source, &destination, false);
    SDL_EndGPUCopyPass(copy_pass);

    if (!wait_for_upload(device, command_buffer)) {
        SDL_ReleaseGPUTransferBuffer(device, transfer);
        SDL_ReleaseGPUBuffer(device, buffer);
        return nullptr;
    }

    SDL_ReleaseGPUTransferBuffer(device, transfer);
    return buffer;
}

static bool upload_texture_pixels(
    SDL_GPUDevice *device,
    SDL_GPUTexture *texture,
    const void *pixels,
    Uint32 width,
    Uint32 height,
    bool generate_mips)
{
    const Uint32 row_bytes = width * 4u;
    const Uint32 total_bytes = row_bytes * height;
    SDL_GPUTransferBufferCreateInfo transfer_info;
    SDL_GPUTransferBuffer *transfer;
    void *mapped;
    SDL_GPUCommandBuffer *command_buffer;
    SDL_GPUCopyPass *copy_pass;
    SDL_GPUTextureTransferInfo source;
    SDL_GPUTextureRegion destination;

    SDL_zero(transfer_info);
    transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transfer_info.size = total_bytes;
    transfer = SDL_CreateGPUTransferBuffer(device, &transfer_info);
    if (!transfer) {
        return false;
    }

    mapped = SDL_MapGPUTransferBuffer(device, transfer, false);
    if (!mapped) {
        SDL_ReleaseGPUTransferBuffer(device, transfer);
        return false;
    }
    SDL_memcpy(mapped, pixels, total_bytes);
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

    SDL_zero(source);
    source.transfer_buffer = transfer;
    source.pixels_per_row = width;
    source.rows_per_layer = height;
    SDL_zero(destination);
    destination.texture = texture;
    destination.w = width;
    destination.h = height;
    destination.d = 1;
    SDL_UploadToGPUTexture(copy_pass, &source, &destination, false);
    SDL_EndGPUCopyPass(copy_pass);

    if (generate_mips) {
        SDL_GenerateMipmapsForGPUTexture(command_buffer, texture);
    }

    if (!wait_for_upload(device, command_buffer)) {
        SDL_ReleaseGPUTransferBuffer(device, transfer);
        return false;
    }

    SDL_ReleaseGPUTransferBuffer(device, transfer);
    return true;
}

static bool upload_texture_pixels_layer(
    SDL_GPUDevice *device,
    SDL_GPUTexture *texture,
    const void *pixels,
    Uint32 width,
    Uint32 height,
    Uint32 layer)
{
    const Uint32 row_bytes = width * 4u;
    const Uint32 total_bytes = row_bytes * height;
    SDL_GPUTransferBufferCreateInfo transfer_info;
    SDL_GPUTransferBuffer *transfer;
    void *mapped;
    SDL_GPUCommandBuffer *command_buffer;
    SDL_GPUCopyPass *copy_pass;
    SDL_GPUTextureTransferInfo source;
    SDL_GPUTextureRegion destination;

    SDL_zero(transfer_info);
    transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transfer_info.size = total_bytes;
    transfer = SDL_CreateGPUTransferBuffer(device, &transfer_info);
    if (!transfer) {
        return false;
    }

    mapped = SDL_MapGPUTransferBuffer(device, transfer, false);
    if (!mapped) {
        SDL_ReleaseGPUTransferBuffer(device, transfer);
        return false;
    }
    SDL_memcpy(mapped, pixels, total_bytes);
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

    SDL_zero(source);
    source.transfer_buffer = transfer;
    source.pixels_per_row = width;
    source.rows_per_layer = height;
    SDL_zero(destination);
    destination.texture = texture;
    destination.layer = layer;
    destination.w = width;
    destination.h = height;
    destination.d = 1;
    SDL_UploadToGPUTexture(copy_pass, &source, &destination, false);
    SDL_EndGPUCopyPass(copy_pass);

    if (!wait_for_upload(device, command_buffer)) {
        SDL_ReleaseGPUTransferBuffer(device, transfer);
        return false;
    }

    SDL_ReleaseGPUTransferBuffer(device, transfer);
    return true;
}

static bool upload_surface_to_texture(SDL_GPUDevice *device, SDL_GPUTexture *texture, SDL_Surface *surface, bool generate_mips)
{
    const Uint32 row_bytes = (Uint32)surface->w * 4u;
    const Uint32 packed_size = row_bytes * (Uint32)surface->h;

    if ((Uint32)surface->pitch == row_bytes) {
        return upload_texture_pixels(device, texture, surface->pixels, (Uint32)surface->w, (Uint32)surface->h, generate_mips);
    }

    Uint8 *packed = (Uint8 *)SDL_malloc(packed_size);
    if (!packed) {
        SDL_OutOfMemory();
        return false;
    }

    for (int y = 0; y < surface->h; y += 1) {
        SDL_memcpy(
            packed + (size_t)y * row_bytes,
            (const Uint8 *)surface->pixels + (size_t)y * surface->pitch,
            row_bytes);
    }

    const bool ok = upload_texture_pixels(device, texture, packed, (Uint32)surface->w, (Uint32)surface->h, generate_mips);
    SDL_free(packed);
    return ok;
}

static Uint32 mip_count_for_size(Uint32 width, Uint32 height)
{
    Uint32 levels = 1;
    Uint32 size = SDL_max(width, height);

    while (size > 1) {
        size >>= 1;
        levels += 1;
    }
    return levels;
}

SDL_GPUTexture *ForgeGpuCreateRgba8TextureFromPixels(
    SDL_GPUDevice *device,
    Uint32 width,
    Uint32 height,
    const void *pixels,
    bool generate_mips)
{
    SDL_GPUTextureCreateInfo texture_info;
    SDL_GPUTexture *texture;

    if (!device || !pixels || width == 0 || height == 0) {
        SDL_InvalidParamError("ForgeGpuCreateRgba8TextureFromPixels");
        return nullptr;
    }

    SDL_zero(texture_info);
    texture_info.type = SDL_GPU_TEXTURETYPE_2D;
    texture_info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    texture_info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    if (generate_mips) {
        texture_info.usage |= SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    }
    texture_info.width = width;
    texture_info.height = height;
    texture_info.layer_count_or_depth = 1;
    texture_info.num_levels = generate_mips ? mip_count_for_size(width, height) : 1u;
    texture_info.sample_count = SDL_GPU_SAMPLECOUNT_1;

    texture = SDL_CreateGPUTexture(device, &texture_info);
    if (!texture) {
        return nullptr;
    }

    if (!upload_texture_pixels(device, texture, pixels, width, height, generate_mips)) {
        SDL_ReleaseGPUTexture(device, texture);
        return nullptr;
    }
    return texture;
}

SDL_GPUTexture *ForgeGpuCreateR32FloatTextureFromPixels(
    SDL_GPUDevice *device,
    Uint32 width,
    Uint32 height,
    const float *pixels)
{
    SDL_GPUTextureCreateInfo texture_info;
    SDL_GPUTexture *texture;

    if (!device || !pixels || width == 0 || height == 0) {
        SDL_InvalidParamError("ForgeGpuCreateR32FloatTextureFromPixels");
        return nullptr;
    }

    SDL_zero(texture_info);
    texture_info.type = SDL_GPU_TEXTURETYPE_2D;
    texture_info.format = SDL_GPU_TEXTUREFORMAT_R32_FLOAT;
    texture_info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    texture_info.width = width;
    texture_info.height = height;
    texture_info.layer_count_or_depth = 1;
    texture_info.num_levels = 1;
    texture_info.sample_count = SDL_GPU_SAMPLECOUNT_1;

    texture = SDL_CreateGPUTexture(device, &texture_info);
    if (!texture) {
        return nullptr;
    }

    if (!upload_texture_pixels(device, texture, pixels, width, height, false)) {
        SDL_ReleaseGPUTexture(device, texture);
        return nullptr;
    }
    return texture;
}

SDL_GPUTexture *ForgeGpuLoadRgbaTexturePathWithFormatAndSize(
    ForgeGpuDemo *demo,
    const char *path,
    bool generate_mips,
    SDL_GPUTextureFormat format,
    Uint32 expected_width,
    Uint32 expected_height)
{
    SDL_Surface *loaded;
    SDL_Surface *converted;
    SDL_GPUTextureCreateInfo texture_info;
    SDL_GPUTexture *texture;

    loaded = SDL_LoadSurface(path);
    if (!loaded) {
        return nullptr;
    }
    if ((expected_width != 0 && (Uint32)loaded->w != expected_width) ||
        (expected_height != 0 && (Uint32)loaded->h != expected_height)) {
        SDL_SetError("texture '%s' size is %dx%d, expected %ux%u",
            path, loaded->w, loaded->h, expected_width, expected_height);
        SDL_DestroySurface(loaded);
        return nullptr;
    }

    converted = SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_ABGR8888);
    SDL_DestroySurface(loaded);
    if (!converted) {
        return nullptr;
    }

    SDL_zero(texture_info);
    texture_info.type = SDL_GPU_TEXTURETYPE_2D;
    texture_info.format = format;
    texture_info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    if (generate_mips) {
        texture_info.usage |= SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    }
    texture_info.width = (Uint32)converted->w;
    texture_info.height = (Uint32)converted->h;
    texture_info.layer_count_or_depth = 1;
    texture_info.num_levels = generate_mips ? mip_count_for_size((Uint32)converted->w, (Uint32)converted->h) : 1u;
    texture_info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    texture = SDL_CreateGPUTexture(demo->device, &texture_info);
    if (!texture) {
        SDL_DestroySurface(converted);
        return nullptr;
    }

    if (!upload_surface_to_texture(demo->device, texture, converted, generate_mips)) {
        SDL_ReleaseGPUTexture(demo->device, texture);
        SDL_DestroySurface(converted);
        return nullptr;
    }

    SDL_DestroySurface(converted);
    return texture;
}

SDL_GPUTexture *ForgeGpuLoadRgbaTexturePathWithFormat(
    ForgeGpuDemo *demo,
    const char *path,
    bool generate_mips,
    SDL_GPUTextureFormat format)
{
    return ForgeGpuLoadRgbaTexturePathWithFormatAndSize(
        demo,
        path,
        generate_mips,
        format,
        0,
        0);
}

SDL_GPUTexture *ForgeGpuLoadRgbaTexturePath(ForgeGpuDemo *demo, const char *path, bool generate_mips)
{
    return ForgeGpuLoadRgbaTexturePathWithFormat(
        demo,
        path,
        generate_mips,
        SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB);
}

SDL_GPUTexture *ForgeGpuLoadRgbaTexture(ForgeGpuDemo *demo, const char *relative_path)
{
    char path[FORGE_GPU_MAX_PATH];

    if (!ForgeGpuJoinAssetPath(demo, relative_path, path, sizeof(path))) {
        SDL_SetError("asset path too long");
        return nullptr;
    }
    return ForgeGpuLoadRgbaTexturePath(demo, path, false);
}

SDL_GPUTexture *ForgeGpuLoadCubeTexture(ForgeGpuDemo *demo, const char *relative_dir)
{
    static const char *face_names[6] = { "px.png", "nx.png", "py.png", "ny.png", "pz.png", "nz.png" };
    SDL_Surface *faces[6];
    SDL_GPUTextureCreateInfo texture_info;
    SDL_GPUTexture *texture;
    char path[FORGE_GPU_MAX_PATH];
    char relative_path[FORGE_GPU_MAX_PATH];
    Uint32 face_width = 0;
    Uint32 face_height = 0;

    SDL_zeroa(faces);

    for (Uint32 face = 0; face < 6; face += 1) {
        SDL_Surface *loaded;
        SDL_Surface *converted;

        if (SDL_snprintf(relative_path, sizeof(relative_path), "%s/%s", relative_dir, face_names[face]) >= (int)sizeof(relative_path)) {
            SDL_SetError("cube texture asset path too long");
            goto fail;
        }
        if (!ForgeGpuJoinAssetPath(demo, relative_path, path, sizeof(path))) {
            SDL_SetError("cube texture asset path too long");
            goto fail;
        }

        loaded = SDL_LoadSurface(path);
        if (!loaded) {
            goto fail;
        }
        converted = SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_ABGR8888);
        SDL_DestroySurface(loaded);
        if (!converted) {
            goto fail;
        }

        if (face == 0) {
            if (converted->w <= 0 || converted->h <= 0 || converted->w != converted->h) {
                SDL_SetError("cube texture face must be square");
                SDL_DestroySurface(converted);
                goto fail;
            }
            face_width = (Uint32)converted->w;
            face_height = (Uint32)converted->h;
        } else if ((Uint32)converted->w != face_width || (Uint32)converted->h != face_height) {
            SDL_SetError("cube texture faces must have matching dimensions");
            SDL_DestroySurface(converted);
            goto fail;
        }
        faces[face] = converted;
    }

    SDL_zero(texture_info);
    texture_info.type = SDL_GPU_TEXTURETYPE_CUBE;
    texture_info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB;
    texture_info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    texture_info.width = face_width;
    texture_info.height = face_height;
    texture_info.layer_count_or_depth = 6;
    texture_info.num_levels = 1;
    texture_info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    texture = SDL_CreateGPUTexture(demo->device, &texture_info);
    if (!texture) {
        goto fail;
    }

    for (Uint32 face = 0; face < 6; face += 1) {
        const Uint32 row_bytes = face_width * 4u;
        const Uint32 packed_size = row_bytes * face_height;
        const SDL_Surface *surface = faces[face];

        if ((Uint32)surface->pitch == row_bytes) {
            if (!upload_texture_pixels_layer(demo->device, texture, surface->pixels, face_width, face_height, face)) {
                SDL_ReleaseGPUTexture(demo->device, texture);
                goto fail;
            }
        } else {
            Uint8 *packed = (Uint8 *)SDL_malloc(packed_size);
            if (!packed) {
                SDL_OutOfMemory();
                SDL_ReleaseGPUTexture(demo->device, texture);
                goto fail;
            }
            for (Uint32 y = 0; y < face_height; y += 1) {
                SDL_memcpy(
                    packed + (size_t)y * row_bytes,
                    (const Uint8 *)surface->pixels + (size_t)y * surface->pitch,
                    row_bytes);
            }
            if (!upload_texture_pixels_layer(demo->device, texture, packed, face_width, face_height, face)) {
                SDL_free(packed);
                SDL_ReleaseGPUTexture(demo->device, texture);
                goto fail;
            }
            SDL_free(packed);
        }
    }

    for (Uint32 face = 0; face < 6; face += 1) {
        SDL_DestroySurface(faces[face]);
    }
    return texture;

fail:
    for (Uint32 face = 0; face < 6; face += 1) {
        if (faces[face]) {
            SDL_DestroySurface(faces[face]);
        }
    }
    return nullptr;
}

SDL_GPUTexture *ForgeGpuCreateWhiteTexture(SDL_GPUDevice *device)
{
    const Uint8 pixel[4] = { 255, 255, 255, 255 };
    SDL_GPUTextureCreateInfo texture_info;
    SDL_GPUTexture *texture;

    SDL_zero(texture_info);
    texture_info.type = SDL_GPU_TEXTURETYPE_2D;
    texture_info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB;
    texture_info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    texture_info.width = 1;
    texture_info.height = 1;
    texture_info.layer_count_or_depth = 1;
    texture_info.num_levels = 1;
    texture_info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    texture = SDL_CreateGPUTexture(device, &texture_info);
    if (!texture) {
        return nullptr;
    }

    if (!upload_texture_pixels(device, texture, pixel, 1, 1, false)) {
        SDL_ReleaseGPUTexture(device, texture);
        return nullptr;
    }
    return texture;
}

SDL_GPUTexture *ForgeGpuCreateCheckerTexture(SDL_GPUDevice *device)
{
    const Uint32 texture_size = 256;
    const Uint32 tile_count = 8;
    const Uint32 tile_size = texture_size / tile_count;
    const Uint32 mip_levels = 9;
    const Uint32 total_bytes = texture_size * texture_size * 4u;
    Uint8 *pixels;
    SDL_GPUTextureCreateInfo texture_info;
    SDL_GPUTexture *texture;

    pixels = (Uint8 *)SDL_malloc(total_bytes);
    if (!pixels) {
        SDL_OutOfMemory();
        return nullptr;
    }

    for (Uint32 y = 0; y < texture_size; y += 1) {
        for (Uint32 x = 0; x < texture_size; x += 1) {
            const Uint32 tile_x = x / tile_size;
            const Uint32 tile_y = y / tile_size;
            const Uint8 color = ((tile_x + tile_y) % 2u) == 0u ? 255u : 0u;
            const Uint32 index = (y * texture_size + x) * 4u;
            pixels[index + 0] = color;
            pixels[index + 1] = color;
            pixels[index + 2] = color;
            pixels[index + 3] = 255u;
        }
    }

    SDL_zero(texture_info);
    texture_info.type = SDL_GPU_TEXTURETYPE_2D;
    texture_info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB;
    texture_info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    texture_info.width = texture_size;
    texture_info.height = texture_size;
    texture_info.layer_count_or_depth = 1;
    texture_info.num_levels = mip_levels;
    texture_info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    texture = SDL_CreateGPUTexture(device, &texture_info);
    if (!texture) {
        SDL_free(pixels);
        return nullptr;
    }

    if (!upload_texture_pixels(device, texture, pixels, texture_size, texture_size, true)) {
        SDL_ReleaseGPUTexture(device, texture);
        SDL_free(pixels);
        return nullptr;
    }

    SDL_free(pixels);
    return texture;
}

static Uint8 forge_gpu_color_byte(float value)
{
    const float clamped = SDL_clamp(value, 0.0f, 1.0f);
    return (Uint8)(clamped * 255.0f);
}

SDL_GPUTexture *ForgeGpuCreateNoiseDirtTexture(
    SDL_GPUDevice *device,
    Uint32 size,
    float noise_scale,
    Uint32 seed,
    int octaves,
    float lacunarity,
    float persistence,
    float base_r,
    float range_r,
    float base_g,
    float range_g,
    float base_b,
    float range_b)
{
    SDL_GPUTextureCreateInfo texture_info;
    SDL_GPUTexture *texture;
    Uint8 *pixels;
    size_t pixel_count;
    size_t total_bytes_size;
    Uint32 total_bytes;
    Uint32 mip_levels;

    if (size == 0) {
        SDL_SetError("noise dirt texture size must be non-zero");
        return nullptr;
    }
    if (!SDL_size_mul_check_overflow((size_t)size, (size_t)size, &pixel_count) ||
        !SDL_size_mul_check_overflow(pixel_count, 4u, &total_bytes_size) ||
        total_bytes_size > SDL_MAX_UINT32) {
        SDL_SetError("noise dirt texture size overflow");
        return nullptr;
    }
    total_bytes = (Uint32)total_bytes_size;
    mip_levels = mip_count_for_size(size, size);

    pixels = (Uint8 *)SDL_malloc(total_bytes);
    if (!pixels) {
        SDL_OutOfMemory();
        return nullptr;
    }

    for (Uint32 y = 0; y < size; y += 1) {
        for (Uint32 x = 0; x < size; x += 1) {
            const float nx = (float)x / (float)size * noise_scale;
            const float ny = (float)y / (float)size * noise_scale;
            float noise = forge_gpu_noise_fbm2d(nx, ny, seed, octaves, lacunarity, persistence);
            const size_t index = (((size_t)y * (size_t)size) + (size_t)x) * 4u;

            noise = noise * 0.5f + 0.5f;
            pixels[index + 0] = forge_gpu_color_byte(base_r + range_r * noise);
            pixels[index + 1] = forge_gpu_color_byte(base_g + range_g * noise);
            pixels[index + 2] = forge_gpu_color_byte(base_b + range_b * noise);
            pixels[index + 3] = 255u;
        }
    }

    SDL_zero(texture_info);
    texture_info.type = SDL_GPU_TEXTURETYPE_2D;
    texture_info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB;
    texture_info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    texture_info.width = size;
    texture_info.height = size;
    texture_info.layer_count_or_depth = 1;
    texture_info.num_levels = mip_levels;
    texture_info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    texture = SDL_CreateGPUTexture(device, &texture_info);
    if (!texture) {
        SDL_free(pixels);
        return nullptr;
    }

    if (!upload_texture_pixels(device, texture, pixels, size, size, true)) {
        SDL_ReleaseGPUTexture(device, texture);
        SDL_free(pixels);
        return nullptr;
    }

    SDL_free(pixels);
    return texture;
}

SDL_GPUSampler *ForgeGpuCreateSampler(
    SDL_GPUDevice *device,
    SDL_GPUFilter min_filter,
    SDL_GPUFilter mag_filter,
    SDL_GPUSamplerMipmapMode mipmap_mode,
    float max_lod)
{
    return ForgeGpuCreateSamplerWithAddress(
        device,
        min_filter,
        mag_filter,
        mipmap_mode,
        SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
        max_lod);
}

SDL_GPUSampler *ForgeGpuCreateSamplerWithAddress(
    SDL_GPUDevice *device,
    SDL_GPUFilter min_filter,
    SDL_GPUFilter mag_filter,
    SDL_GPUSamplerMipmapMode mipmap_mode,
    SDL_GPUSamplerAddressMode address_mode,
    float max_lod)
{
    return ForgeGpuCreateSamplerWithAddressAndAnisotropy(
        device,
        min_filter,
        mag_filter,
        mipmap_mode,
        address_mode,
        max_lod,
        1.0f);
}

SDL_GPUSampler *ForgeGpuCreateSamplerWithAddressAndAnisotropy(
    SDL_GPUDevice *device,
    SDL_GPUFilter min_filter,
    SDL_GPUFilter mag_filter,
    SDL_GPUSamplerMipmapMode mipmap_mode,
    SDL_GPUSamplerAddressMode address_mode,
    float max_lod,
    float max_anisotropy)
{
    SDL_GPUSamplerCreateInfo sampler_info;
    SDL_zero(sampler_info);
    sampler_info.min_filter = min_filter;
    sampler_info.mag_filter = mag_filter;
    sampler_info.mipmap_mode = mipmap_mode;
    sampler_info.address_mode_u = address_mode;
    sampler_info.address_mode_v = address_mode;
    sampler_info.address_mode_w = address_mode;
    sampler_info.min_lod = 0.0f;
    sampler_info.max_lod = max_lod;
    sampler_info.enable_anisotropy = max_anisotropy > 1.0f;
    sampler_info.max_anisotropy = max_anisotropy;
    return SDL_CreateGPUSampler(device, &sampler_info);
}

static bool ForgeGpuSelectShaderSource(
    SDL_GPUDevice *device,
    const char *wgsl,
    unsigned int wgsl_size,
    const char *msl,
    unsigned int msl_size,
    const Uint8 *spirv,
    unsigned int spirv_size,
    const Uint8 *dxil,
    unsigned int dxil_size,
    SDL_GPUShaderFormat *format,
    const char **entrypoint,
    const Uint8 **code,
    size_t *code_size)
{
    const SDL_GPUShaderFormat formats = SDL_GetGPUShaderFormats(device);

    if ((formats & SDL_GPU_SHADERFORMAT_WGSL) != 0 && wgsl && wgsl_size > 0) {
        *format = SDL_GPU_SHADERFORMAT_WGSL;
        *entrypoint = "main";
        *code = (const Uint8 *)wgsl;
        *code_size = wgsl_size;
        return true;
    }

    if ((formats & SDL_GPU_SHADERFORMAT_SPIRV) != 0 && spirv && spirv_size > 0) {
        *format = SDL_GPU_SHADERFORMAT_SPIRV;
        *entrypoint = "main";
        *code = spirv;
        *code_size = spirv_size;
        return true;
    }

    if ((formats & SDL_GPU_SHADERFORMAT_DXIL) != 0 && dxil && dxil_size > 0) {
        *format = SDL_GPU_SHADERFORMAT_DXIL;
        *entrypoint = "main";
        *code = dxil;
        *code_size = dxil_size;
        return true;
    }

    if ((formats & SDL_GPU_SHADERFORMAT_MSL) != 0 && msl && msl_size > 0) {
        *format = SDL_GPU_SHADERFORMAT_MSL;
        *entrypoint = "main0";
        *code = (const Uint8 *)msl;
        *code_size = msl_size;
        return true;
    }

    SDL_SetError("forge-gpu demo requires WGSL, SPIR-V, DXIL, or MSL shader support");
    return false;
}

SDL_GPUShader *ForgeGpuCreateShaderFromSources(
    SDL_GPUDevice *device,
    SDL_GPUShaderStage stage,
    const char *wgsl,
    unsigned int wgsl_size,
    const char *msl,
    unsigned int msl_size,
    const Uint8 *spirv,
    unsigned int spirv_size,
    const Uint8 *dxil,
    unsigned int dxil_size,
    Uint32 num_samplers,
    Uint32 num_storage_textures,
    Uint32 num_storage_buffers,
    Uint32 num_uniform_buffers)
{
    SDL_GPUShaderCreateInfo shader_info;
    SDL_GPUShaderFormat format;
    const char *entrypoint;
    const Uint8 *code;
    size_t code_size;

    if (!ForgeGpuSelectShaderSource(device, wgsl, wgsl_size, msl, msl_size, spirv, spirv_size, dxil, dxil_size, &format, &entrypoint, &code, &code_size)) {
        return nullptr;
    }

    SDL_zero(shader_info);
    shader_info.stage = stage;
    shader_info.num_samplers = num_samplers;
    shader_info.num_storage_textures = num_storage_textures;
    shader_info.num_storage_buffers = num_storage_buffers;
    shader_info.num_uniform_buffers = num_uniform_buffers;
    shader_info.format = format;
    shader_info.entrypoint = entrypoint;
    shader_info.code = code;
    shader_info.code_size = code_size;
    return SDL_CreateGPUShader(device, &shader_info);
}

SDL_GPUShader *ForgeGpuCreateShaderWithResourceLayoutFromSources(
    SDL_GPUDevice *device,
    const char *wgsl,
    unsigned int wgsl_size,
    const char *msl,
    unsigned int msl_size,
    const Uint8 *spirv,
    unsigned int spirv_size,
    const Uint8 *dxil,
    unsigned int dxil_size,
    const SDL_GPUShaderResourceLayoutCreateInfo *layout_createinfo)
{
    SDL_GPUShaderWithResourceLayoutCreateInfo shader_info;
    SDL_GPUShaderResourceLayout *resource_layout;
    SDL_GPUShader *shader;
    SDL_GPUShaderFormat format;
    const char *entrypoint;
    const Uint8 *code;
    size_t code_size;

    if (!layout_createinfo) {
        SDL_SetError("forge-gpu shader resource layout create info must not be NULL");
        return nullptr;
    }

    if (!ForgeGpuSelectShaderSource(device, wgsl, wgsl_size, msl, msl_size, spirv, spirv_size, dxil, dxil_size, &format, &entrypoint, &code, &code_size)) {
        return nullptr;
    }

    resource_layout = SDL_CreateGPUShaderResourceLayout(device, layout_createinfo);
    if (!resource_layout) {
        return nullptr;
    }

    SDL_INIT_INTERFACE(&shader_info);
    shader_info.stage = layout_createinfo->stage;
    shader_info.format = format;
    shader_info.entrypoint = entrypoint;
    shader_info.code = code;
    shader_info.code_size = code_size;
    shader_info.resource_layout = resource_layout;

    shader = SDL_CreateGPUShaderWithResourceLayout(device, &shader_info);
    SDL_ReleaseGPUShaderResourceLayout(device, resource_layout);
    return shader;
}

SDL_GPUComputePipeline *ForgeGpuCreateComputePipelineFromSources(
    SDL_GPUDevice *device,
    const char *wgsl,
    unsigned int wgsl_size,
    const char *msl,
    unsigned int msl_size,
    const Uint8 *spirv,
    unsigned int spirv_size,
    const Uint8 *dxil,
    unsigned int dxil_size,
    Uint32 num_samplers,
    Uint32 num_readwrite_storage_textures,
    Uint32 num_uniform_buffers,
    Uint32 threadcount_x,
    Uint32 threadcount_y,
    Uint32 threadcount_z)
{
    SDL_GPUComputePipelineCreateInfo pipeline_info;
    SDL_GPUShaderFormat format;
    const char *entrypoint;
    const Uint8 *code;
    size_t code_size;

    if (!ForgeGpuSelectShaderSource(device, wgsl, wgsl_size, msl, msl_size, spirv, spirv_size, dxil, dxil_size, &format, &entrypoint, &code, &code_size)) {
        return nullptr;
    }

    SDL_zero(pipeline_info);
    pipeline_info.num_samplers = num_samplers;
    pipeline_info.num_readwrite_storage_textures = num_readwrite_storage_textures;
    pipeline_info.num_uniform_buffers = num_uniform_buffers;
    pipeline_info.threadcount_x = threadcount_x;
    pipeline_info.threadcount_y = threadcount_y;
    pipeline_info.threadcount_z = threadcount_z;
    pipeline_info.format = format;
    pipeline_info.entrypoint = entrypoint;
    pipeline_info.code = code;
    pipeline_info.code_size = code_size;

    return SDL_CreateGPUComputePipeline(device, &pipeline_info);
}

SDL_GPUComputePipeline *ForgeGpuCreateComputePipelineWithResourceLayoutFromSources(
    SDL_GPUDevice *device,
    const char *wgsl,
    unsigned int wgsl_size,
    const char *msl,
    unsigned int msl_size,
    const Uint8 *spirv,
    unsigned int spirv_size,
    const Uint8 *dxil,
    unsigned int dxil_size,
    const SDL_GPUComputePipelineResourceLayoutCreateInfo *layout_createinfo,
    Uint32 threadcount_x,
    Uint32 threadcount_y,
    Uint32 threadcount_z)
{
    SDL_GPUComputePipelineWithResourceLayoutCreateInfo pipeline_info;
    SDL_GPUShaderResourceLayout *resource_layout;
    SDL_GPUComputePipeline *pipeline;
    SDL_GPUShaderFormat format;
    const char *entrypoint;
    const Uint8 *code;
    size_t code_size;

    if (!layout_createinfo) {
        SDL_SetError("forge-gpu compute pipeline resource layout create info must not be NULL");
        return nullptr;
    }

    if (!ForgeGpuSelectShaderSource(device, wgsl, wgsl_size, msl, msl_size, spirv, spirv_size, dxil, dxil_size, &format, &entrypoint, &code, &code_size)) {
        return nullptr;
    }

    resource_layout = SDL_CreateGPUComputePipelineResourceLayout(device, layout_createinfo);
    if (!resource_layout) {
        return nullptr;
    }

    SDL_INIT_INTERFACE(&pipeline_info);
    pipeline_info.resource_layout = resource_layout;
    pipeline_info.threadcount_x = threadcount_x;
    pipeline_info.threadcount_y = threadcount_y;
    pipeline_info.threadcount_z = threadcount_z;
    pipeline_info.format = format;
    pipeline_info.entrypoint = entrypoint;
    pipeline_info.code = code;
    pipeline_info.code_size = code_size;

    pipeline = SDL_CreateGPUComputePipelineWithResourceLayout(device, &pipeline_info);
    SDL_ReleaseGPUShaderResourceLayout(device, resource_layout);
    return pipeline;
}

SDL_GPUGraphicsPipeline *ForgeGpuCreatePipelineWithCull(
    ForgeGpuDemo *demo,
    SDL_GPUShader *vertex_shader,
    SDL_GPUShader *fragment_shader,
    const SDL_GPUVertexBufferDescription *vertex_buffer_desc,
    const SDL_GPUVertexAttribute *vertex_attributes,
    Uint32 num_vertex_attributes,
    bool depth_enabled,
    SDL_GPUCullMode cull_mode)
{
    SDL_GPUColorTargetDescription color_target_description;
    SDL_GPUGraphicsPipelineCreateInfo pipeline_info;

    SDL_zero(color_target_description);
    color_target_description.format = demo->color_format;

    SDL_zero(pipeline_info);
    pipeline_info.vertex_shader = vertex_shader;
    pipeline_info.fragment_shader = fragment_shader;
    pipeline_info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pipeline_info.vertex_input_state.vertex_buffer_descriptions = vertex_buffer_desc;
    pipeline_info.vertex_input_state.num_vertex_buffers = vertex_buffer_desc ? 1 : 0;
    pipeline_info.vertex_input_state.vertex_attributes = vertex_attributes;
    pipeline_info.vertex_input_state.num_vertex_attributes = num_vertex_attributes;
    pipeline_info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    pipeline_info.rasterizer_state.cull_mode = cull_mode;
    pipeline_info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    pipeline_info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    pipeline_info.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
    pipeline_info.depth_stencil_state.enable_depth_test = depth_enabled;
    pipeline_info.depth_stencil_state.enable_depth_write = depth_enabled;
    pipeline_info.target_info.color_target_descriptions = &color_target_description;
    pipeline_info.target_info.num_color_targets = 1;
    pipeline_info.target_info.has_depth_stencil_target = depth_enabled;
    pipeline_info.target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D16_UNORM;

    return SDL_CreateGPUGraphicsPipeline(demo->device, &pipeline_info);
}

SDL_GPUGraphicsPipeline *ForgeGpuCreatePipeline(
    ForgeGpuDemo *demo,
    SDL_GPUShader *vertex_shader,
    SDL_GPUShader *fragment_shader,
    const SDL_GPUVertexBufferDescription *vertex_buffer_desc,
    const SDL_GPUVertexAttribute *vertex_attributes,
    Uint32 num_vertex_attributes,
    bool depth_enabled)
{
    return ForgeGpuCreatePipelineWithCull(
        demo,
        vertex_shader,
        fragment_shader,
        vertex_buffer_desc,
        vertex_attributes,
        num_vertex_attributes,
        depth_enabled,
        depth_enabled ? SDL_GPU_CULLMODE_BACK : SDL_GPU_CULLMODE_NONE);
}

bool ForgeGpuCreateDepthTextureWithFormat(
    ForgeGpuDemo *demo,
    Uint32 width,
    Uint32 height,
    SDL_GPUTextureFormat format)
{
    SDL_GPUTextureCreateInfo texture_info;

    if (width == 0 || height == 0) {
        return false;
    }

    if (demo->lesson.depth_texture &&
        demo->lesson.depth_width == width &&
        demo->lesson.depth_height == height &&
        demo->lesson.depth_format == format) {
        return true;
    }

    if (demo->lesson.depth_texture) {
        SDL_ReleaseGPUTexture(demo->device, demo->lesson.depth_texture);
        demo->lesson.depth_texture = nullptr;
    }

    SDL_zero(texture_info);
    texture_info.type = SDL_GPU_TEXTURETYPE_2D;
    texture_info.format = format;
    texture_info.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
    texture_info.width = width;
    texture_info.height = height;
    texture_info.layer_count_or_depth = 1;
    texture_info.num_levels = 1;
    texture_info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    demo->lesson.depth_texture = SDL_CreateGPUTexture(demo->device, &texture_info);
    if (!demo->lesson.depth_texture) {
        demo->lesson.depth_width = 0;
        demo->lesson.depth_height = 0;
        demo->lesson.depth_format = SDL_GPU_TEXTUREFORMAT_INVALID;
        return false;
    }

    demo->lesson.depth_width = width;
    demo->lesson.depth_height = height;
    demo->lesson.depth_format = format;
    return true;
}

bool ForgeGpuCreateDepthTexture(ForgeGpuDemo *demo, Uint32 width, Uint32 height)
{
    return ForgeGpuCreateDepthTextureWithFormat(demo, width, height, SDL_GPU_TEXTUREFORMAT_D16_UNORM);
}

bool ForgeGpuConfigureSwapchain(SDL_GPUDevice *device, SDL_Window *window)
{
    if (SDL_WindowSupportsGPUSwapchainComposition(device, window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR_LINEAR)) {
        return SDL_SetGPUSwapchainParameters(
            device,
            window,
            SDL_GPU_SWAPCHAINCOMPOSITION_SDR_LINEAR,
            SDL_GPU_PRESENTMODE_VSYNC);
    }

    return true;
}
