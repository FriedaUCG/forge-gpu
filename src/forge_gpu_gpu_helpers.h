#ifndef SDLGPU_FORGE_GPU_GPU_HELPERS_H
#define SDLGPU_FORGE_GPU_GPU_HELPERS_H

#include "forge_gpu_internal.h"

float ForgeGpuFrameTimeSeconds(ForgeGpuDemo *demo);
void ForgeGpuResetFrameStats(ForgeGpuDemo *demo);
void ForgeGpuUpdateFrameStats(ForgeGpuDemo *demo);
bool ForgeGpuJoinAssetPath(ForgeGpuDemo *demo, const char *relative_path, char *path, size_t path_size);
SDL_GPUBuffer *ForgeGpuCreateBufferWithData(
    SDL_GPUDevice *device,
    SDL_GPUBufferUsageFlags usage,
    const void *data,
    Uint32 size);
SDL_GPUTexture *ForgeGpuLoadRgbaTexturePath(ForgeGpuDemo *demo, const char *path, bool generate_mips);
SDL_GPUTexture *ForgeGpuLoadRgbaTexturePathWithFormat(
    ForgeGpuDemo *demo,
    const char *path,
    bool generate_mips,
    SDL_GPUTextureFormat format);
SDL_GPUTexture *ForgeGpuLoadRgbaTexturePathWithFormatAndSize(
    ForgeGpuDemo *demo,
    const char *path,
    bool generate_mips,
    SDL_GPUTextureFormat format,
    Uint32 expected_width,
    Uint32 expected_height);
SDL_GPUTexture *ForgeGpuLoadRgbaTexture(ForgeGpuDemo *demo, const char *relative_path);
SDL_GPUTexture *ForgeGpuLoadCubeTexture(ForgeGpuDemo *demo, const char *relative_dir);
SDL_GPUTexture *ForgeGpuCreateWhiteTexture(SDL_GPUDevice *device);
SDL_GPUTexture *ForgeGpuCreateCheckerTexture(SDL_GPUDevice *device);
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
    float range_b);
SDL_GPUTexture *ForgeGpuCreateRgba8TextureFromPixels(
    SDL_GPUDevice *device,
    Uint32 width,
    Uint32 height,
    const void *pixels,
    bool generate_mips);
SDL_GPUTexture *ForgeGpuCreateR32FloatTextureFromPixels(
    SDL_GPUDevice *device,
    Uint32 width,
    Uint32 height,
    const float *pixels);
SDL_GPUSampler *ForgeGpuCreateSampler(
    SDL_GPUDevice *device,
    SDL_GPUFilter min_filter,
    SDL_GPUFilter mag_filter,
    SDL_GPUSamplerMipmapMode mipmap_mode,
    float max_lod);
SDL_GPUSampler *ForgeGpuCreateSamplerWithAddress(
    SDL_GPUDevice *device,
    SDL_GPUFilter min_filter,
    SDL_GPUFilter mag_filter,
    SDL_GPUSamplerMipmapMode mipmap_mode,
    SDL_GPUSamplerAddressMode address_mode,
    float max_lod);
SDL_GPUSampler *ForgeGpuCreateSamplerWithAddressAndAnisotropy(
    SDL_GPUDevice *device,
    SDL_GPUFilter min_filter,
    SDL_GPUFilter mag_filter,
    SDL_GPUSamplerMipmapMode mipmap_mode,
    SDL_GPUSamplerAddressMode address_mode,
    float max_lod,
    float max_anisotropy);
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
    Uint32 num_uniform_buffers);
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
    const SDL_GPUShaderResourceLayoutCreateInfo *layout_createinfo);
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
    Uint32 threadcount_z);
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
    Uint32 threadcount_z);

#if defined(__EMSCRIPTEN__)
#define FORGE_GPU_SPIRV_ARGS(wgsl_symbol) nullptr, 0u
#define FORGE_GPU_DXIL_ARGS(wgsl_symbol) nullptr, 0u
#else
#define FORGE_GPU_SPIRV_ARGS(wgsl_symbol) (const Uint8 *)(wgsl_symbol##_spirv), (wgsl_symbol##_spirv_size)
#define FORGE_GPU_DXIL_ARGS(wgsl_symbol) (const Uint8 *)(wgsl_symbol##_dxil), (wgsl_symbol##_dxil_size)
#endif

#define ForgeGpuCreateShader(device, stage, wgsl, wgsl_size, msl, msl_size, num_samplers, num_storage_textures, num_storage_buffers, num_uniform_buffers) \
    ForgeGpuCreateShaderFromSources( \
        (device), (stage), (wgsl), (wgsl_size), (msl), (msl_size), \
        FORGE_GPU_SPIRV_ARGS(wgsl), \
        FORGE_GPU_DXIL_ARGS(wgsl), \
        (num_samplers), (num_storage_textures), (num_storage_buffers), (num_uniform_buffers))

#define ForgeGpuCreateShaderWithResourceLayout(device, wgsl, wgsl_size, msl, msl_size, layout_createinfo) \
    ForgeGpuCreateShaderWithResourceLayoutFromSources( \
        (device), (wgsl), (wgsl_size), (msl), (msl_size), \
        FORGE_GPU_SPIRV_ARGS(wgsl), \
        FORGE_GPU_DXIL_ARGS(wgsl), \
        (layout_createinfo))

#define ForgeGpuCreateComputePipeline(device, wgsl, wgsl_size, msl, msl_size, num_samplers, num_readwrite_storage_textures, num_uniform_buffers, threadcount_x, threadcount_y, threadcount_z) \
    ForgeGpuCreateComputePipelineFromSources( \
        (device), (wgsl), (wgsl_size), (msl), (msl_size), \
        FORGE_GPU_SPIRV_ARGS(wgsl), \
        FORGE_GPU_DXIL_ARGS(wgsl), \
        (num_samplers), (num_readwrite_storage_textures), (num_uniform_buffers), \
        (threadcount_x), (threadcount_y), (threadcount_z))

#define ForgeGpuCreateComputePipelineWithResourceLayout(device, wgsl, wgsl_size, msl, msl_size, layout_createinfo, threadcount_x, threadcount_y, threadcount_z) \
    ForgeGpuCreateComputePipelineWithResourceLayoutFromSources( \
        (device), (wgsl), (wgsl_size), (msl), (msl_size), \
        FORGE_GPU_SPIRV_ARGS(wgsl), \
        FORGE_GPU_DXIL_ARGS(wgsl), \
        (layout_createinfo), (threadcount_x), (threadcount_y), (threadcount_z))
SDL_GPUGraphicsPipeline *ForgeGpuCreatePipelineWithCull(
    ForgeGpuDemo *demo,
    SDL_GPUShader *vertex_shader,
    SDL_GPUShader *fragment_shader,
    const SDL_GPUVertexBufferDescription *vertex_buffer_desc,
    const SDL_GPUVertexAttribute *vertex_attributes,
    Uint32 num_vertex_attributes,
    bool depth_enabled,
    SDL_GPUCullMode cull_mode);
SDL_GPUGraphicsPipeline *ForgeGpuCreatePipeline(
    ForgeGpuDemo *demo,
    SDL_GPUShader *vertex_shader,
    SDL_GPUShader *fragment_shader,
    const SDL_GPUVertexBufferDescription *vertex_buffer_desc,
    const SDL_GPUVertexAttribute *vertex_attributes,
    Uint32 num_vertex_attributes,
    bool depth_enabled);
bool ForgeGpuCreateDepthTexture(ForgeGpuDemo *demo, Uint32 width, Uint32 height);
bool ForgeGpuCreateDepthTextureWithFormat(
    ForgeGpuDemo *demo,
    Uint32 width,
    Uint32 height,
    SDL_GPUTextureFormat format);
bool ForgeGpuConfigureSwapchain(SDL_GPUDevice *device, SDL_Window *window);

#endif /* SDLGPU_FORGE_GPU_GPU_HELPERS_H */
