#include "forge_gpu_pipelines.h"

#include "forge_gpu_gpu_helpers.h"
#include "shaders/generated/forge_gpu_lesson_06_shaders.h"
#include "shaders/generated/forge_gpu_lesson_07_shaders.h"
#include "shaders/generated/forge_gpu_lesson_11_shaders.h"
#include "shaders/generated/forge_gpu_lesson_12_shaders.h"

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
    Uint32 vertex_uniform_count)
{
    SDL_GPUShader *vertex_shader;
    SDL_GPUShader *fragment_shader;
    SDL_GPUVertexBufferDescription vertex_buffer_desc;
    SDL_GPUVertexAttribute vertex_attributes[2];

    vertex_shader = ForgeGpuCreateShaderFromSources(
        demo->device, SDL_GPU_SHADERSTAGE_VERTEX,
        vertex_wgsl, vertex_wgsl_size,
        vertex_msl, vertex_msl_size,
        vertex_spirv, vertex_spirv_size,
        vertex_dxil, vertex_dxil_size,
        0, 0, 0, vertex_uniform_count);
    if (!vertex_shader) {
        return false;
    }
    fragment_shader = ForgeGpuCreateShaderFromSources(
        demo->device, SDL_GPU_SHADERSTAGE_FRAGMENT,
        fragment_wgsl, fragment_wgsl_size,
        fragment_msl, fragment_msl_size,
        fragment_spirv, fragment_spirv_size,
        fragment_dxil, fragment_dxil_size,
        0, 0, 0, 0);
    if (!fragment_shader) {
        SDL_ReleaseGPUShader(demo->device, vertex_shader);
        return false;
    }

    SDL_zero(vertex_buffer_desc);
    vertex_buffer_desc.slot = 0;
    vertex_buffer_desc.pitch = sizeof(LessonVertex2Color);
    vertex_buffer_desc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

    SDL_zeroa(vertex_attributes);
    vertex_attributes[0].location = 0;
    vertex_attributes[0].buffer_slot = 0;
    vertex_attributes[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    vertex_attributes[0].offset = offsetof(LessonVertex2Color, position);
    vertex_attributes[1].location = 1;
    vertex_attributes[1].buffer_slot = 0;
    vertex_attributes[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    vertex_attributes[1].offset = offsetof(LessonVertex2Color, color);

    demo->lesson.pipeline = ForgeGpuCreatePipeline(
        demo, vertex_shader, fragment_shader,
        &vertex_buffer_desc, vertex_attributes, SDL_arraysize(vertex_attributes), false);

    SDL_ReleaseGPUShader(demo->device, vertex_shader);
    SDL_ReleaseGPUShader(demo->device, fragment_shader);
    return demo->lesson.pipeline != nullptr;
}

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
    unsigned int fragment_dxil_size)
{
    SDL_GPUShader *vertex_shader;
    SDL_GPUShader *fragment_shader;
    SDL_GPUVertexBufferDescription vertex_buffer_desc;
    SDL_GPUVertexAttribute vertex_attributes[2];

    vertex_shader = ForgeGpuCreateShaderFromSources(
        demo->device, SDL_GPU_SHADERSTAGE_VERTEX,
        vertex_wgsl, vertex_wgsl_size,
        vertex_msl, vertex_msl_size,
        vertex_spirv, vertex_spirv_size,
        vertex_dxil, vertex_dxil_size,
        0, 0, 0, 1);
    if (!vertex_shader) {
        return false;
    }
    fragment_shader = ForgeGpuCreateShaderFromSources(
        demo->device, SDL_GPU_SHADERSTAGE_FRAGMENT,
        fragment_wgsl, fragment_wgsl_size,
        fragment_msl, fragment_msl_size,
        fragment_spirv, fragment_spirv_size,
        fragment_dxil, fragment_dxil_size,
        1, 0, 0, 0);
    if (!fragment_shader) {
        SDL_ReleaseGPUShader(demo->device, vertex_shader);
        return false;
    }

    SDL_zero(vertex_buffer_desc);
    vertex_buffer_desc.slot = 0;
    vertex_buffer_desc.pitch = sizeof(LessonVertex2Uv);
    vertex_buffer_desc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

    SDL_zeroa(vertex_attributes);
    vertex_attributes[0].location = 0;
    vertex_attributes[0].buffer_slot = 0;
    vertex_attributes[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    vertex_attributes[0].offset = offsetof(LessonVertex2Uv, position);
    vertex_attributes[1].location = 1;
    vertex_attributes[1].buffer_slot = 0;
    vertex_attributes[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    vertex_attributes[1].offset = offsetof(LessonVertex2Uv, uv);

    demo->lesson.pipeline = ForgeGpuCreatePipeline(
        demo, vertex_shader, fragment_shader,
        &vertex_buffer_desc, vertex_attributes, SDL_arraysize(vertex_attributes), false);

    SDL_ReleaseGPUShader(demo->device, vertex_shader);
    SDL_ReleaseGPUShader(demo->device, fragment_shader);
    return demo->lesson.pipeline != nullptr;
}

bool ForgeGpuCreateCubePipeline(ForgeGpuDemo *demo)
{
    SDL_GPUShader *vertex_shader;
    SDL_GPUShader *fragment_shader;
    SDL_GPUVertexBufferDescription vertex_buffer_desc;
    SDL_GPUVertexAttribute vertex_attributes[2];

    vertex_shader = ForgeGpuCreateShader(
        demo->device, SDL_GPU_SHADERSTAGE_VERTEX,
        lesson06_cube_vert_wgsl, lesson06_cube_vert_wgsl_size,
        lesson06_cube_vert_msl, lesson06_cube_vert_msl_size,
        0, 0, 0, 1);
    if (!vertex_shader) {
        return false;
    }
    fragment_shader = ForgeGpuCreateShader(
        demo->device, SDL_GPU_SHADERSTAGE_FRAGMENT,
        lesson06_cube_frag_wgsl, lesson06_cube_frag_wgsl_size,
        lesson06_cube_frag_msl, lesson06_cube_frag_msl_size,
        0, 0, 0, 0);
    if (!fragment_shader) {
        SDL_ReleaseGPUShader(demo->device, vertex_shader);
        return false;
    }

    SDL_zero(vertex_buffer_desc);
    vertex_buffer_desc.slot = 0;
    vertex_buffer_desc.pitch = sizeof(LessonVertex3Color);
    vertex_buffer_desc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

    SDL_zeroa(vertex_attributes);
    vertex_attributes[0].location = 0;
    vertex_attributes[0].buffer_slot = 0;
    vertex_attributes[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    vertex_attributes[0].offset = offsetof(LessonVertex3Color, position);
    vertex_attributes[1].location = 1;
    vertex_attributes[1].buffer_slot = 0;
    vertex_attributes[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    vertex_attributes[1].offset = offsetof(LessonVertex3Color, color);

    demo->lesson.pipeline = ForgeGpuCreatePipeline(
        demo, vertex_shader, fragment_shader,
        &vertex_buffer_desc, vertex_attributes, SDL_arraysize(vertex_attributes), true);

    SDL_ReleaseGPUShader(demo->device, vertex_shader);
    SDL_ReleaseGPUShader(demo->device, fragment_shader);
    return demo->lesson.pipeline != nullptr;
}

bool ForgeGpuCreateLesson07Pipeline(ForgeGpuDemo *demo)
{
    SDL_GPUShader *vertex_shader;
    SDL_GPUShader *fragment_shader;
    SDL_GPUVertexBufferDescription vertex_buffer_desc;
    SDL_GPUVertexAttribute vertex_attributes[2];

    vertex_shader = ForgeGpuCreateShader(
        demo->device, SDL_GPU_SHADERSTAGE_VERTEX,
        lesson07_scene_vert_wgsl, lesson07_scene_vert_wgsl_size,
        lesson07_scene_vert_msl, lesson07_scene_vert_msl_size,
        0, 0, 0, 1);
    if (!vertex_shader) {
        return false;
    }
    fragment_shader = ForgeGpuCreateShader(
        demo->device, SDL_GPU_SHADERSTAGE_FRAGMENT,
        lesson07_scene_frag_wgsl, lesson07_scene_frag_wgsl_size,
        lesson07_scene_frag_msl, lesson07_scene_frag_msl_size,
        0, 0, 0, 0);
    if (!fragment_shader) {
        SDL_ReleaseGPUShader(demo->device, vertex_shader);
        return false;
    }

    SDL_zero(vertex_buffer_desc);
    vertex_buffer_desc.slot = 0;
    vertex_buffer_desc.pitch = sizeof(LessonVertex3Color);
    vertex_buffer_desc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

    SDL_zeroa(vertex_attributes);
    vertex_attributes[0].location = 0;
    vertex_attributes[0].buffer_slot = 0;
    vertex_attributes[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    vertex_attributes[0].offset = offsetof(LessonVertex3Color, position);
    vertex_attributes[1].location = 1;
    vertex_attributes[1].buffer_slot = 0;
    vertex_attributes[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    vertex_attributes[1].offset = offsetof(LessonVertex3Color, color);

    demo->lesson.pipeline = ForgeGpuCreatePipeline(
        demo, vertex_shader, fragment_shader,
        &vertex_buffer_desc, vertex_attributes, SDL_arraysize(vertex_attributes), true);

    SDL_ReleaseGPUShader(demo->device, vertex_shader);
    SDL_ReleaseGPUShader(demo->device, fragment_shader);
    return demo->lesson.pipeline != nullptr;
}

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
    SDL_GPUGraphicsPipeline **out_pipeline)
{
    SDL_GPUShader *vertex_shader;
    SDL_GPUShader *fragment_shader;
    SDL_GPUVertexBufferDescription vertex_buffer_desc;
    SDL_GPUVertexAttribute vertex_attributes[3];

    vertex_shader = ForgeGpuCreateShaderFromSources(
        demo->device, SDL_GPU_SHADERSTAGE_VERTEX,
        vertex_wgsl, vertex_wgsl_size,
        vertex_msl, vertex_msl_size,
        vertex_spirv, vertex_spirv_size,
        vertex_dxil, vertex_dxil_size,
        0, 0, 0, vertex_uniform_count);
    if (!vertex_shader) {
        return false;
    }
    fragment_shader = ForgeGpuCreateShaderFromSources(
        demo->device, SDL_GPU_SHADERSTAGE_FRAGMENT,
        fragment_wgsl, fragment_wgsl_size,
        fragment_msl, fragment_msl_size,
        fragment_spirv, fragment_spirv_size,
        fragment_dxil, fragment_dxil_size,
        1, 0, 0, fragment_uniform_count);
    if (!fragment_shader) {
        SDL_ReleaseGPUShader(demo->device, vertex_shader);
        return false;
    }

    SDL_zero(vertex_buffer_desc);
    vertex_buffer_desc.slot = 0;
    vertex_buffer_desc.pitch = sizeof(ForgeGpuMeshVertex);
    vertex_buffer_desc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

    SDL_zeroa(vertex_attributes);
    vertex_attributes[0].location = 0;
    vertex_attributes[0].buffer_slot = 0;
    vertex_attributes[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    vertex_attributes[0].offset = offsetof(ForgeGpuMeshVertex, position);
    vertex_attributes[1].location = 1;
    vertex_attributes[1].buffer_slot = 0;
    vertex_attributes[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    vertex_attributes[1].offset = offsetof(ForgeGpuMeshVertex, normal);
    vertex_attributes[2].location = 2;
    vertex_attributes[2].buffer_slot = 0;
    vertex_attributes[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    vertex_attributes[2].offset = offsetof(ForgeGpuMeshVertex, uv);

    *out_pipeline = ForgeGpuCreatePipeline(
        demo, vertex_shader, fragment_shader,
        &vertex_buffer_desc, vertex_attributes, SDL_arraysize(vertex_attributes), true);

    SDL_ReleaseGPUShader(demo->device, vertex_shader);
    SDL_ReleaseGPUShader(demo->device, fragment_shader);
    return *out_pipeline != nullptr;
}

bool ForgeGpuCreateFullscreenPipeline(ForgeGpuDemo *demo)
{
    SDL_GPUShader *vertex_shader;
    SDL_GPUShader *fragment_shader;

    vertex_shader = ForgeGpuCreateShader(
        demo->device, SDL_GPU_SHADERSTAGE_VERTEX,
        lesson11_fullscreen_vert_wgsl, lesson11_fullscreen_vert_wgsl_size,
        lesson11_fullscreen_vert_msl, lesson11_fullscreen_vert_msl_size,
        0, 0, 0, 0);
    if (!vertex_shader) {
        return false;
    }
    fragment_shader = ForgeGpuCreateShader(
        demo->device, SDL_GPU_SHADERSTAGE_FRAGMENT,
        lesson11_fullscreen_frag_wgsl, lesson11_fullscreen_frag_wgsl_size,
        lesson11_fullscreen_frag_msl, lesson11_fullscreen_frag_msl_size,
        1, 0, 0, 0);
    if (!fragment_shader) {
        SDL_ReleaseGPUShader(demo->device, vertex_shader);
        return false;
    }

    demo->lesson.pipeline = ForgeGpuCreatePipeline(demo, vertex_shader, fragment_shader, nullptr, nullptr, 0, false);

    SDL_ReleaseGPUShader(demo->device, vertex_shader);
    SDL_ReleaseGPUShader(demo->device, fragment_shader);
    return demo->lesson.pipeline != nullptr;
}

bool ForgeGpuCreateGridPipeline(ForgeGpuDemo *demo)
{
    SDL_GPUShader *vertex_shader;
    SDL_GPUShader *fragment_shader;
    SDL_GPUVertexBufferDescription vertex_buffer_desc;
    SDL_GPUVertexAttribute vertex_attribute;

    vertex_shader = ForgeGpuCreateShader(
        demo->device, SDL_GPU_SHADERSTAGE_VERTEX,
        lesson12_grid_vert_wgsl, lesson12_grid_vert_wgsl_size,
        lesson12_grid_vert_msl, lesson12_grid_vert_msl_size,
        0, 0, 0, 1);
    if (!vertex_shader) {
        return false;
    }
    fragment_shader = ForgeGpuCreateShader(
        demo->device, SDL_GPU_SHADERSTAGE_FRAGMENT,
        lesson12_grid_frag_wgsl, lesson12_grid_frag_wgsl_size,
        lesson12_grid_frag_msl, lesson12_grid_frag_msl_size,
        0, 0, 0, 1);
    if (!fragment_shader) {
        SDL_ReleaseGPUShader(demo->device, vertex_shader);
        return false;
    }

    SDL_zero(vertex_buffer_desc);
    vertex_buffer_desc.slot = 0;
    vertex_buffer_desc.pitch = sizeof(GridVertex);
    vertex_buffer_desc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

    SDL_zero(vertex_attribute);
    vertex_attribute.location = 0;
    vertex_attribute.buffer_slot = 0;
    vertex_attribute.format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    vertex_attribute.offset = offsetof(GridVertex, position);

    demo->lesson.pipeline = ForgeGpuCreatePipelineWithCull(
        demo,
        vertex_shader,
        fragment_shader,
        &vertex_buffer_desc,
        &vertex_attribute,
        1,
        true,
        SDL_GPU_CULLMODE_NONE);

    SDL_ReleaseGPUShader(demo->device, vertex_shader);
    SDL_ReleaseGPUShader(demo->device, fragment_shader);
    return demo->lesson.pipeline != nullptr;
}
