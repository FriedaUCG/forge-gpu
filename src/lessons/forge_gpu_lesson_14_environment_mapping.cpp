#include "forge_gpu_lessons.h"

#include "forge_gpu_camera.h"
#include "forge_gpu_gpu_helpers.h"
#include "forge_gpu_lesson_common.h"
#include "forge_gpu_math.h"
#include "forge_gpu_scene.h"
#include "forge_gpu_shader_layouts.h"
#include "shaders/generated/forge_gpu_lesson_14_shaders.h"

#include <stddef.h>

struct EnvFragUniforms
{
    float base_color[4];
    float light_dir[4];
    float eye_pos[4];
    Uint32 has_texture;
    float shininess;
    float ambient;
    float specular_str;
    float reflectivity_pad[4];
};

static_assert(sizeof(EnvFragUniforms) == 80, "lesson 14 fragment uniform size must match strict generated shader layout");

struct Lesson14State
{
    SDL_GPUBuffer *shuttle_vertex_buffer;
    Uint32 shuttle_vertex_count;
    SDL_GPUTexture *cube_texture;
};

static Lesson14State *lesson14_state(ForgeGpuDemo *demo)
{
    return (Lesson14State *)demo->lesson.private_state;
}

static bool create_lesson14_skybox_geometry(ForgeGpuDemo *demo)
{
    static const float vertices[] = {
        -1.0f, -1.0f, -1.0f,
         1.0f, -1.0f, -1.0f,
         1.0f,  1.0f, -1.0f,
        -1.0f,  1.0f, -1.0f,
        -1.0f, -1.0f,  1.0f,
         1.0f, -1.0f,  1.0f,
         1.0f,  1.0f,  1.0f,
        -1.0f,  1.0f,  1.0f
    };
    static const Uint16 indices[] = {
        0, 2, 1, 0, 3, 2,
        4, 5, 6, 4, 6, 7,
        0, 4, 7, 0, 7, 3,
        1, 2, 6, 1, 6, 5,
        0, 1, 5, 0, 5, 4,
        3, 7, 6, 3, 6, 2
    };

    demo->lesson.vertex_buffer = ForgeGpuCreateBufferWithData(
        demo->device, SDL_GPU_BUFFERUSAGE_VERTEX, vertices, sizeof(vertices));
    demo->lesson.index_buffer = ForgeGpuCreateBufferWithData(
        demo->device, SDL_GPU_BUFFERUSAGE_INDEX, indices, sizeof(indices));
    return demo->lesson.vertex_buffer && demo->lesson.index_buffer;
}

static bool create_lesson14_pipelines(ForgeGpuDemo *demo)
{
    SDL_GPUShader *sky_vertex_shader;
    SDL_GPUShader *sky_fragment_shader;
    SDL_GPUShader *shuttle_vertex_shader;
    SDL_GPUShader *shuttle_fragment_shader;
    SDL_GPUVertexBufferDescription sky_vb_desc;
    SDL_GPUVertexAttribute sky_attr;
    SDL_GPUVertexBufferDescription shuttle_vb_desc;
    SDL_GPUVertexAttribute shuttle_attrs[3];

    sky_vertex_shader = ForgeGpuCreateShader(
        demo->device, SDL_GPU_SHADERSTAGE_VERTEX,
        lesson14_skybox_vert_wgsl, lesson14_skybox_vert_wgsl_size,
        lesson14_skybox_vert_msl, lesson14_skybox_vert_msl_size,
        0, 0, 0, 1);
    if (!sky_vertex_shader) {
        return false;
    }
    sky_fragment_shader = ForgeGpuCreateShaderWithResourceLayout(
        demo->device,
        lesson14_skybox_frag_wgsl, lesson14_skybox_frag_wgsl_size,
        lesson14_skybox_frag_msl, lesson14_skybox_frag_msl_size,
        ForgeGpuShaderLayout_lesson14_skybox_frag());
    if (!sky_fragment_shader) {
        SDL_ReleaseGPUShader(demo->device, sky_vertex_shader);
        return false;
    }

    SDL_zero(sky_vb_desc);
    sky_vb_desc.slot = 0;
    sky_vb_desc.pitch = sizeof(float) * 3u;
    sky_vb_desc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    SDL_zero(sky_attr);
    sky_attr.location = 0;
    sky_attr.buffer_slot = 0;
    sky_attr.format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    sky_attr.offset = 0;

    demo->lesson.pipeline = ForgeGpuCreateLessonGraphicsPipeline(
        demo, sky_vertex_shader, sky_fragment_shader,
        &sky_vb_desc, 1, &sky_attr, 1,
        1, true, SDL_GPU_TEXTUREFORMAT_D16_UNORM,
        true, false, SDL_GPU_CULLMODE_FRONT, 0.0f, 0.0f);

    SDL_ReleaseGPUShader(demo->device, sky_vertex_shader);
    SDL_ReleaseGPUShader(demo->device, sky_fragment_shader);
    if (!demo->lesson.pipeline) {
        return false;
    }

    shuttle_vertex_shader = ForgeGpuCreateShader(
        demo->device, SDL_GPU_SHADERSTAGE_VERTEX,
        lesson14_shuttle_vert_wgsl, lesson14_shuttle_vert_wgsl_size,
        lesson14_shuttle_vert_msl, lesson14_shuttle_vert_msl_size,
        0, 0, 0, 1);
    if (!shuttle_vertex_shader) {
        return false;
    }
    shuttle_fragment_shader = ForgeGpuCreateShaderWithResourceLayout(
        demo->device,
        lesson14_shuttle_frag_wgsl, lesson14_shuttle_frag_wgsl_size,
        lesson14_shuttle_frag_msl, lesson14_shuttle_frag_msl_size,
        ForgeGpuShaderLayout_lesson14_shuttle_frag());
    if (!shuttle_fragment_shader) {
        SDL_ReleaseGPUShader(demo->device, shuttle_vertex_shader);
        return false;
    }

    SDL_zero(shuttle_vb_desc);
    shuttle_vb_desc.slot = 0;
    shuttle_vb_desc.pitch = sizeof(ForgeGpuMeshVertex);
    shuttle_vb_desc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    SDL_zeroa(shuttle_attrs);
    shuttle_attrs[0].location = 0;
    shuttle_attrs[0].buffer_slot = 0;
    shuttle_attrs[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    shuttle_attrs[0].offset = offsetof(ForgeGpuMeshVertex, position);
    shuttle_attrs[1].location = 1;
    shuttle_attrs[1].buffer_slot = 0;
    shuttle_attrs[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    shuttle_attrs[1].offset = offsetof(ForgeGpuMeshVertex, normal);
    shuttle_attrs[2].location = 2;
    shuttle_attrs[2].buffer_slot = 0;
    shuttle_attrs[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    shuttle_attrs[2].offset = offsetof(ForgeGpuMeshVertex, uv);

    demo->lesson.secondary_pipeline = ForgeGpuCreateLessonGraphicsPipeline(
        demo, shuttle_vertex_shader, shuttle_fragment_shader,
        &shuttle_vb_desc, 1, shuttle_attrs, SDL_arraysize(shuttle_attrs),
        1, true, SDL_GPU_TEXTUREFORMAT_D16_UNORM,
        true, true, SDL_GPU_CULLMODE_BACK, 0.0f, 0.0f);

    SDL_ReleaseGPUShader(demo->device, shuttle_vertex_shader);
    SDL_ReleaseGPUShader(demo->device, shuttle_fragment_shader);
    return demo->lesson.secondary_pipeline != nullptr;
}

bool ForgeGpuCreateLesson14(ForgeGpuDemo *demo)
{
    ForgeGpuLoadedMesh mesh;
    char model_path[FORGE_GPU_MAX_PATH];
    Lesson14State *state;

    SDL_zero(mesh);
    if (!ForgeGpuJoinAssetPath(demo, "models/space-shuttle/space-shuttle.obj", model_path, sizeof(model_path))) {
        SDL_SetError("lesson 14 asset path too long");
        return false;
    }
    state = (Lesson14State *)SDL_calloc(1, sizeof(*state));
    if (!state) {
        SDL_OutOfMemory();
        return false;
    }
    demo->lesson.private_state = state;
    if (!ForgeGpuLoadObjMesh(model_path, &mesh)) {
        return false;
    }
    state->shuttle_vertex_buffer = ForgeGpuCreateBufferWithData(
        demo->device,
        SDL_GPU_BUFFERUSAGE_VERTEX,
        mesh.vertices,
        mesh.vertex_count * (Uint32)sizeof(*mesh.vertices));
    state->shuttle_vertex_count = mesh.vertex_count;
    ForgeGpuFreeLoadedMesh(&mesh);

    demo->lesson.texture = ForgeGpuLoadRgbaTexture(demo, "models/space-shuttle/ShuttleDiffuseMap.png");
    state->cube_texture = ForgeGpuLoadCubeTexture(demo, "skyboxes/milkyway");
    demo->lesson.white_texture = ForgeGpuCreateWhiteTexture(demo->device);
    demo->lesson.samplers[0] = ForgeGpuCreateSampler(
        demo->device,
        SDL_GPU_FILTER_LINEAR,
        SDL_GPU_FILTER_LINEAR,
        SDL_GPU_SAMPLERMIPMAPMODE_LINEAR,
        1000.0f);
    demo->lesson.samplers[1] = ForgeGpuCreateSamplerWithAddress(
        demo->device,
        SDL_GPU_FILTER_LINEAR,
        SDL_GPU_FILTER_LINEAR,
        SDL_GPU_SAMPLERMIPMAPMODE_LINEAR,
        SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        1000.0f);

    demo->lesson.camera_position = { -35.0f, 21.0f, 28.0f };
    demo->lesson.camera_yaw = -51.0f * FORGE_GPU_DEG2RAD;
    demo->lesson.camera_pitch = -25.0f * FORGE_GPU_DEG2RAD;
    demo->lesson.move_speed = 3.0f;
    demo->lesson.last_ticks = SDL_GetTicks();

    return state->shuttle_vertex_buffer &&
           demo->lesson.texture &&
           state->cube_texture &&
           demo->lesson.white_texture &&
           demo->lesson.samplers[0] &&
           demo->lesson.samplers[1] &&
           create_lesson14_skybox_geometry(demo) &&
           create_lesson14_pipelines(demo);
}

void ForgeGpuRenderLesson14(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    Uint32 width,
    Uint32 height)
{
    Mat4 view;
    Mat4 projection;
    Mat4 view_rotation;
    Mat4 vp;
    Mat4 sky_vp;
    SDL_GPUBufferBinding vertex_binding;
    SDL_GPUBufferBinding index_binding;
    SDL_GPUTextureSamplerBinding sampler_bindings[2];
    UniformMvp sky_uniforms;
    UniformMvpModel shuttle_vertex_uniforms;
    EnvFragUniforms shuttle_fragment_uniforms;
    Vec3 light_dir = vec3_normalize({ 1.0f, 0.3f, 1.0f });
    Lesson14State *state = lesson14_state(demo);

    if (!state) {
        return;
    }

    ForgeGpuUpdateCameraFromInput(demo);
    ForgeGpuCameraViewProjection(demo, width, height, 100.0f, &view, &projection);
    vp = mat4_multiply(projection, view);
    view_rotation = view;
    view_rotation.m[12] = 0.0f;
    view_rotation.m[13] = 0.0f;
    view_rotation.m[14] = 0.0f;
    sky_vp = mat4_multiply(projection, view_rotation);

    SDL_BindGPUGraphicsPipeline(render_pass, demo->lesson.pipeline);
    sky_uniforms.mvp = sky_vp;
    SDL_PushGPUVertexUniformData(command_buffer, 0, &sky_uniforms, sizeof(sky_uniforms));
    SDL_zero(sampler_bindings[0]);
    sampler_bindings[0].texture = state->cube_texture;
    sampler_bindings[0].sampler = demo->lesson.samplers[1];
    SDL_BindGPUFragmentSamplers(render_pass, 0, sampler_bindings, 1);
    SDL_zero(vertex_binding);
    vertex_binding.buffer = demo->lesson.vertex_buffer;
    SDL_BindGPUVertexBuffers(render_pass, 0, &vertex_binding, 1);
    SDL_zero(index_binding);
    index_binding.buffer = demo->lesson.index_buffer;
    SDL_BindGPUIndexBuffer(render_pass, &index_binding, SDL_GPU_INDEXELEMENTSIZE_16BIT);
    SDL_DrawGPUIndexedPrimitives(render_pass, 36, 1, 0, 0, 0);

    SDL_BindGPUGraphicsPipeline(render_pass, demo->lesson.secondary_pipeline);
    shuttle_vertex_uniforms.model = mat4_identity();
    shuttle_vertex_uniforms.mvp = mat4_multiply(vp, shuttle_vertex_uniforms.model);
    SDL_PushGPUVertexUniformData(command_buffer, 0, &shuttle_vertex_uniforms, sizeof(shuttle_vertex_uniforms));

    SDL_zero(shuttle_fragment_uniforms);
    shuttle_fragment_uniforms.base_color[0] = 1.0f;
    shuttle_fragment_uniforms.base_color[1] = 1.0f;
    shuttle_fragment_uniforms.base_color[2] = 1.0f;
    shuttle_fragment_uniforms.base_color[3] = 1.0f;
    shuttle_fragment_uniforms.light_dir[0] = light_dir.x;
    shuttle_fragment_uniforms.light_dir[1] = light_dir.y;
    shuttle_fragment_uniforms.light_dir[2] = light_dir.z;
    shuttle_fragment_uniforms.eye_pos[0] = demo->lesson.camera_position.x;
    shuttle_fragment_uniforms.eye_pos[1] = demo->lesson.camera_position.y;
    shuttle_fragment_uniforms.eye_pos[2] = demo->lesson.camera_position.z;
    shuttle_fragment_uniforms.has_texture = demo->lesson.texture ? 1u : 0u;
    shuttle_fragment_uniforms.shininess = 64.0f;
    shuttle_fragment_uniforms.ambient = 0.08f;
    shuttle_fragment_uniforms.specular_str = 0.5f;
    shuttle_fragment_uniforms.reflectivity_pad[0] = 0.6f;
    SDL_PushGPUFragmentUniformData(command_buffer, 0, &shuttle_fragment_uniforms, sizeof(shuttle_fragment_uniforms));

    SDL_zeroa(sampler_bindings);
    sampler_bindings[0].texture = demo->lesson.texture ? demo->lesson.texture : demo->lesson.white_texture;
    sampler_bindings[0].sampler = demo->lesson.samplers[0];
    sampler_bindings[1].texture = state->cube_texture;
    sampler_bindings[1].sampler = demo->lesson.samplers[1];
    SDL_BindGPUFragmentSamplers(render_pass, 0, sampler_bindings, 2);

    SDL_zero(vertex_binding);
    vertex_binding.buffer = state->shuttle_vertex_buffer;
    SDL_BindGPUVertexBuffers(render_pass, 0, &vertex_binding, 1);
    SDL_DrawGPUPrimitives(render_pass, state->shuttle_vertex_count, 1, 0, 0);
}

void ForgeGpuDestroyLesson14(ForgeGpuDemo *demo)
{
    Lesson14State *state = lesson14_state(demo);

    if (!state) {
        return;
    }
    if (state->shuttle_vertex_buffer) {
        SDL_ReleaseGPUBuffer(demo->device, state->shuttle_vertex_buffer);
    }
    if (state->cube_texture) {
        SDL_ReleaseGPUTexture(demo->device, state->cube_texture);
    }
    SDL_free(state);
    demo->lesson.private_state = nullptr;
}
