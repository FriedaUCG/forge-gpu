#ifndef SDLGPU_FORGE_GPU_PIPELINES_H
#define SDLGPU_FORGE_GPU_PIPELINES_H

#include "forge_gpu_gpu_helpers.h"

bool ForgeGpuCreateColorTrianglePipelineFromSources(
    ForgeGpuDemo *demo,
    const char *vertex_wgsl,
    unsigned int vertex_wgsl_size,
    const char *vertex_msl,
    unsigned int vertex_msl_size,
    const Uint8 *vertex_spirv,
    unsigned int vertex_spirv_size,
    const Uint8 *vertex_dxil,
    unsigned int vertex_dxil_size,
    const char *fragment_wgsl,
    unsigned int fragment_wgsl_size,
    const char *fragment_msl,
    unsigned int fragment_msl_size,
    const Uint8 *fragment_spirv,
    unsigned int fragment_spirv_size,
    const Uint8 *fragment_dxil,
    unsigned int fragment_dxil_size,
    Uint32 vertex_uniform_count);
bool ForgeGpuCreateTexturedQuadPipelineFromSources(
    ForgeGpuDemo *demo,
    const char *vertex_wgsl,
    unsigned int vertex_wgsl_size,
    const char *vertex_msl,
    unsigned int vertex_msl_size,
    const Uint8 *vertex_spirv,
    unsigned int vertex_spirv_size,
    const Uint8 *vertex_dxil,
    unsigned int vertex_dxil_size,
    const char *fragment_wgsl,
    unsigned int fragment_wgsl_size,
    const char *fragment_msl,
    unsigned int fragment_msl_size,
    const Uint8 *fragment_spirv,
    unsigned int fragment_spirv_size,
    const Uint8 *fragment_dxil,
    unsigned int fragment_dxil_size);
bool ForgeGpuCreateCubePipeline(ForgeGpuDemo *demo);
bool ForgeGpuCreateLesson07Pipeline(ForgeGpuDemo *demo);
bool ForgeGpuCreateMeshPipelineFromSources(
    ForgeGpuDemo *demo,
    const char *vertex_wgsl,
    unsigned int vertex_wgsl_size,
    const char *vertex_msl,
    unsigned int vertex_msl_size,
    const Uint8 *vertex_spirv,
    unsigned int vertex_spirv_size,
    const Uint8 *vertex_dxil,
    unsigned int vertex_dxil_size,
    const char *fragment_wgsl,
    unsigned int fragment_wgsl_size,
    const char *fragment_msl,
    unsigned int fragment_msl_size,
    const Uint8 *fragment_spirv,
    unsigned int fragment_spirv_size,
    const Uint8 *fragment_dxil,
    unsigned int fragment_dxil_size,
    Uint32 vertex_uniform_count,
    Uint32 fragment_uniform_count,
    SDL_GPUGraphicsPipeline **out_pipeline);

#define ForgeGpuCreateColorTrianglePipeline(demo, vertex_wgsl, vertex_wgsl_size, vertex_msl, vertex_msl_size, fragment_wgsl, fragment_wgsl_size, fragment_msl, fragment_msl_size, vertex_uniform_count) \
    ForgeGpuCreateColorTrianglePipelineFromSources( \
        (demo), (vertex_wgsl), (vertex_wgsl_size), (vertex_msl), (vertex_msl_size), \
        FORGE_GPU_SPIRV_ARGS(vertex_wgsl), \
        FORGE_GPU_DXIL_ARGS(vertex_wgsl), \
        (fragment_wgsl), (fragment_wgsl_size), (fragment_msl), (fragment_msl_size), \
        FORGE_GPU_SPIRV_ARGS(fragment_wgsl), \
        FORGE_GPU_DXIL_ARGS(fragment_wgsl), \
        (vertex_uniform_count))

#define ForgeGpuCreateTexturedQuadPipeline(demo, vertex_wgsl, vertex_wgsl_size, vertex_msl, vertex_msl_size, fragment_wgsl, fragment_wgsl_size, fragment_msl, fragment_msl_size) \
    ForgeGpuCreateTexturedQuadPipelineFromSources( \
        (demo), (vertex_wgsl), (vertex_wgsl_size), (vertex_msl), (vertex_msl_size), \
        FORGE_GPU_SPIRV_ARGS(vertex_wgsl), \
        FORGE_GPU_DXIL_ARGS(vertex_wgsl), \
        (fragment_wgsl), (fragment_wgsl_size), (fragment_msl), (fragment_msl_size), \
        FORGE_GPU_SPIRV_ARGS(fragment_wgsl), \
        FORGE_GPU_DXIL_ARGS(fragment_wgsl))

#define ForgeGpuCreateMeshPipeline(demo, vertex_wgsl, vertex_wgsl_size, vertex_msl, vertex_msl_size, fragment_wgsl, fragment_wgsl_size, fragment_msl, fragment_msl_size, vertex_uniform_count, fragment_uniform_count, out_pipeline) \
    ForgeGpuCreateMeshPipelineFromSources( \
        (demo), (vertex_wgsl), (vertex_wgsl_size), (vertex_msl), (vertex_msl_size), \
        FORGE_GPU_SPIRV_ARGS(vertex_wgsl), \
        FORGE_GPU_DXIL_ARGS(vertex_wgsl), \
        (fragment_wgsl), (fragment_wgsl_size), (fragment_msl), (fragment_msl_size), \
        FORGE_GPU_SPIRV_ARGS(fragment_wgsl), \
        FORGE_GPU_DXIL_ARGS(fragment_wgsl), \
        (vertex_uniform_count), (fragment_uniform_count), (out_pipeline))

bool ForgeGpuCreateFullscreenPipeline(ForgeGpuDemo *demo);
bool ForgeGpuCreateGridPipeline(ForgeGpuDemo *demo);

#endif /* SDLGPU_FORGE_GPU_PIPELINES_H */
