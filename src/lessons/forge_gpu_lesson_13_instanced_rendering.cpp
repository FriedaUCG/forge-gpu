#include "forge_gpu_lessons.h"

#include "forge_gpu_camera.h"
#include "forge_gpu_gpu_helpers.h"
#include "forge_gpu_lesson_common.h"
#include "forge_gpu_math.h"
#include "forge_gpu_scene.h"
#include "shaders/generated/forge_gpu_lesson_13_shaders.h"

#include <stddef.h>

struct Lesson13State
{
    GpuSceneData scene_models[2];
};

static Lesson13State *lesson13_state(ForgeGpuDemo *demo)
{
    return (Lesson13State *)demo->lesson.private_state;
}

static bool create_lesson13_grid_pipeline(ForgeGpuDemo *demo)
{
    SDL_GPUShader *vertex_shader;
    SDL_GPUShader *fragment_shader;
    SDL_GPUVertexBufferDescription vertex_buffer_desc;
    SDL_GPUVertexAttribute vertex_attribute;

    vertex_shader = ForgeGpuCreateShader(
        demo->device, SDL_GPU_SHADERSTAGE_VERTEX,
        lesson13_grid_vert_wgsl, lesson13_grid_vert_wgsl_size,
        lesson13_grid_vert_msl, lesson13_grid_vert_msl_size,
        0, 0, 0, 1);
    if (!vertex_shader) {
        return false;
    }
    fragment_shader = ForgeGpuCreateShader(
        demo->device, SDL_GPU_SHADERSTAGE_FRAGMENT,
        lesson13_grid_frag_wgsl, lesson13_grid_frag_wgsl_size,
        lesson13_grid_frag_msl, lesson13_grid_frag_msl_size,
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

    demo->lesson.pipeline = ForgeGpuCreateLessonGraphicsPipeline(
        demo,
        vertex_shader,
        fragment_shader,
        &vertex_buffer_desc,
        1,
        &vertex_attribute,
        1,
        1,
        true,
        SDL_GPU_TEXTUREFORMAT_D16_UNORM,
        true,
        true,
        SDL_GPU_CULLMODE_NONE,
        0.0f,
        0.0f);

    SDL_ReleaseGPUShader(demo->device, vertex_shader);
    SDL_ReleaseGPUShader(demo->device, fragment_shader);
    return demo->lesson.pipeline != nullptr;
}

static bool create_lesson13_instanced_pipeline(ForgeGpuDemo *demo)
{
    SDL_GPUShader *vertex_shader;
    SDL_GPUShader *fragment_shader;
    SDL_GPUVertexBufferDescription vertex_buffer_descs[2];
    SDL_GPUVertexAttribute vertex_attributes[7];

    vertex_shader = ForgeGpuCreateShader(
        demo->device, SDL_GPU_SHADERSTAGE_VERTEX,
        lesson13_instanced_vert_wgsl, lesson13_instanced_vert_wgsl_size,
        lesson13_instanced_vert_msl, lesson13_instanced_vert_msl_size,
        0, 0, 0, 1);
    if (!vertex_shader) {
        return false;
    }
    fragment_shader = ForgeGpuCreateShader(
        demo->device, SDL_GPU_SHADERSTAGE_FRAGMENT,
        lesson13_instanced_frag_wgsl, lesson13_instanced_frag_wgsl_size,
        lesson13_instanced_frag_msl, lesson13_instanced_frag_msl_size,
        1, 0, 0, 1);
    if (!fragment_shader) {
        SDL_ReleaseGPUShader(demo->device, vertex_shader);
        return false;
    }

    SDL_zeroa(vertex_buffer_descs);
    vertex_buffer_descs[0].slot = 0;
    vertex_buffer_descs[0].pitch = sizeof(ForgeGpuMeshVertex);
    vertex_buffer_descs[0].input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    vertex_buffer_descs[1].slot = 1;
    vertex_buffer_descs[1].pitch = sizeof(InstanceData);
    vertex_buffer_descs[1].input_rate = SDL_GPU_VERTEXINPUTRATE_INSTANCE;

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
    for (Uint32 i = 0; i < 4; i += 1) {
        vertex_attributes[3 + i].location = 3 + i;
        vertex_attributes[3 + i].buffer_slot = 1;
        vertex_attributes[3 + i].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
        vertex_attributes[3 + i].offset = i * 16u;
    }

    demo->lesson.secondary_pipeline = ForgeGpuCreateLessonGraphicsPipeline(
        demo,
        vertex_shader,
        fragment_shader,
        vertex_buffer_descs,
        2,
        vertex_attributes,
        SDL_arraysize(vertex_attributes),
        1,
        true,
        SDL_GPU_TEXTUREFORMAT_D16_UNORM,
        true,
        true,
        SDL_GPU_CULLMODE_BACK,
        0.0f,
        0.0f);

    SDL_ReleaseGPUShader(demo->device, vertex_shader);
    SDL_ReleaseGPUShader(demo->device, fragment_shader);
    return demo->lesson.secondary_pipeline != nullptr;
}

typedef bool (*GenerateInstancesFn)(InstanceData *out, int capacity, int *count);

static bool generate_box_instances(InstanceData *out, int capacity, int *count)
{
    const int cols = 6;
    const int rows = 6;
    const float spacing = 3.0f;
    const float offset_x = (float)(cols - 1) * spacing * 0.5f;
    const float offset_z = (float)(rows - 1) * spacing * 0.5f;
    int idx = 0;

    for (int row = 0; row < rows; row += 1) {
        for (int col = 0; col < cols; col += 1) {
            const float x = (float)col * spacing - offset_x;
            const float z = (float)row * spacing - offset_z;
            const Mat4 t = mat4_translate({ x, 0.5f, z });
            const Mat4 r = mat4_rotate_y((float)idx * 0.3f);
            if (idx >= capacity) {
                SDL_SetError("lesson 13 box instance buffer too small");
                return false;
            }
            out[idx].model = mat4_multiply(t, r);
            idx += 1;
        }
    }

    for (int i = 0; i < 11; i += 1) {
        const int base_idx = i * 3;
        const int base_row = base_idx / cols;
        const int base_col = base_idx % cols;
        const float x = (float)base_col * spacing - offset_x;
        const float z = (float)base_row * spacing - offset_z;
        const Mat4 t = mat4_translate({ x, 1.5f, z });
        const Mat4 r = mat4_rotate_y((float)idx * 0.5f);
        if (idx >= capacity) {
            SDL_SetError("lesson 13 box instance buffer too small");
            return false;
        }
        out[idx].model = mat4_multiply(t, r);
        idx += 1;
    }

    *count = idx;
    return true;
}

static bool generate_duck_instances(InstanceData *out, int capacity, int *count)
{
    const int cols = 16;
    const int rows = 16;
    const float spacing = 2.0f;
    const float offset_x = (float)(cols - 1) * spacing * 0.5f;
    const float offset_z = (float)(rows - 1) * spacing * 0.5f;
    int idx = 0;

    for (int row = 0; row < rows; row += 1) {
        for (int col = 0; col < cols; col += 1) {
            const float x = (float)col * spacing - offset_x;
            const float z = (float)row * spacing - offset_z;
            const Mat4 t = mat4_translate({ x, 0.0f, z });
            const Mat4 r = mat4_rotate_y((float)idx * 2.3998f);
            const Mat4 s = mat4_scale(0.5f);
            if (idx >= capacity) {
                SDL_SetError("lesson 13 duck instance buffer too small");
                return false;
            }
            out[idx].model = mat4_multiply(t, mat4_multiply(r, s));
            idx += 1;
        }
    }

    *count = idx;
    return true;
}

static bool upload_instances(ForgeGpuDemo *demo, GpuSceneData *model, GenerateInstancesFn generate)
{
    const int instance_capacity = 256;
    InstanceData *instances;
    Mat4 base_transform = mat4_identity();
    int instance_count = 0;
    bool ok = false;

    for (int i = 0; i < model->loaded.node_count; i += 1) {
        if (model->loaded.nodes[i].mesh_index >= 0) {
            base_transform = mat4_from_forge(model->loaded.nodes[i].world_transform);
            break;
        }
    }

    instances = (InstanceData *)SDL_calloc(instance_capacity, sizeof(*instances));
    if (!instances) {
        return false;
    }

    if (!generate(instances, instance_capacity, &instance_count)) {
        SDL_free(instances);
        return false;
    }
    for (int i = 0; i < instance_count; i += 1) {
        instances[i].model = mat4_multiply(instances[i].model, base_transform);
    }

    model->instance_buffer = ForgeGpuCreateBufferWithData(
        demo->device,
        SDL_GPU_BUFFERUSAGE_VERTEX,
        instances,
        (Uint32)(instance_count * (int)sizeof(*instances)));
    if (model->instance_buffer) {
        model->instance_count = (Uint32)instance_count;
        ok = true;
    }
    SDL_free(instances);
    return ok;
}

static void render_instanced_model(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    const GpuSceneData *model,
    const Vec3 *light_dir)
{
    for (int node_index = 0; node_index < model->loaded.node_count; node_index += 1) {
        const ForgeGpuSceneNode *node = &model->loaded.nodes[node_index];
        const ForgeGpuSceneMesh *mesh;

        if (node->mesh_index < 0 || node->mesh_index >= model->loaded.mesh_count) {
            continue;
        }
        mesh = &model->loaded.meshes[node->mesh_index];

        for (int primitive_offset = 0; primitive_offset < mesh->primitive_count; primitive_offset += 1) {
            const int primitive_index = mesh->first_primitive + primitive_offset;
            GpuMaterial fallback_material;
            const GpuPrimitive *primitive;
            const GpuMaterial *material;
            SDL_GPUBufferBinding vertex_bindings[2];
            SDL_GPUTextureSamplerBinding sampler_binding;
            FragLightingUniforms fragment_uniforms;

            if (primitive_index < 0 || primitive_index >= model->primitive_count) {
                continue;
            }
            primitive = &model->primitives[primitive_index];
            material = ForgeGpuModelMaterialOrDefault(model, primitive->material_index, &fallback_material);

            SDL_memcpy(fragment_uniforms.base_color, material->base_color, sizeof(fragment_uniforms.base_color));
            fragment_uniforms.light_dir[0] = light_dir->x;
            fragment_uniforms.light_dir[1] = light_dir->y;
            fragment_uniforms.light_dir[2] = light_dir->z;
            fragment_uniforms.light_dir[3] = 0.0f;
            fragment_uniforms.eye_pos[0] = demo->lesson.camera_position.x;
            fragment_uniforms.eye_pos[1] = demo->lesson.camera_position.y;
            fragment_uniforms.eye_pos[2] = demo->lesson.camera_position.z;
            fragment_uniforms.eye_pos[3] = 0.0f;
            fragment_uniforms.has_texture = material->has_texture ? 1u : 0u;
            fragment_uniforms.shininess = 64.0f;
            fragment_uniforms.ambient = 0.15f;
            fragment_uniforms.specular_str = 0.5f;
            SDL_PushGPUFragmentUniformData(command_buffer, 0, &fragment_uniforms, sizeof(fragment_uniforms));

            SDL_zero(sampler_binding);
            sampler_binding.texture = material->has_texture ? material->texture : demo->lesson.white_texture;
            sampler_binding.sampler = demo->lesson.samplers[0];
            SDL_BindGPUFragmentSamplers(render_pass, 0, &sampler_binding, 1);

            SDL_zeroa(vertex_bindings);
            vertex_bindings[0].buffer = primitive->vertex_buffer;
            vertex_bindings[1].buffer = model->instance_buffer;
            SDL_BindGPUVertexBuffers(render_pass, 0, vertex_bindings, 2);

            if (primitive->index_buffer && primitive->index_count > 0) {
                SDL_GPUBufferBinding index_binding;

                SDL_zero(index_binding);
                index_binding.buffer = primitive->index_buffer;
                SDL_BindGPUIndexBuffer(render_pass, &index_binding, primitive->index_type);
                SDL_DrawGPUIndexedPrimitives(render_pass, primitive->index_count, model->instance_count, 0, 0, 0);
            } else {
                SDL_DrawGPUPrimitives(render_pass, primitive->vertex_count, model->instance_count, 0, 0);
            }
        }
    }
}

bool ForgeGpuCreateLesson13(ForgeGpuDemo *demo)
{
    LessonState *lesson = &demo->lesson;
    Lesson13State *state;

    state = (Lesson13State *)SDL_calloc(1, sizeof(*state));
    if (!state) {
        SDL_OutOfMemory();
        return false;
    }
    lesson->private_state = state;

    if (!ForgeGpuCreateGridBuffers(demo)) {
        return false;
    }
    if (!ForgeGpuLoadSceneModel(demo, &state->scene_models[0], "models/BoxTextured/BoxTextured.gltf") ||
        !ForgeGpuLoadSceneModel(demo, &state->scene_models[1], "models/Duck/Duck.gltf")) {
        return false;
    }
    if (!upload_instances(demo, &state->scene_models[0], generate_box_instances) ||
        !upload_instances(demo, &state->scene_models[1], generate_duck_instances)) {
        return false;
    }

    lesson->camera_position = { 12.0f, 8.0f, 12.0f };
    lesson->camera_yaw = 45.0f * FORGE_GPU_DEG2RAD;
    lesson->camera_pitch = -25.0f * FORGE_GPU_DEG2RAD;
    lesson->move_speed = 5.0f;
    lesson->last_ticks = SDL_GetTicks();
    return create_lesson13_grid_pipeline(demo) &&
           create_lesson13_instanced_pipeline(demo);
}

void ForgeGpuRenderLesson13(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    Uint32 width,
    Uint32 height)
{
    Mat4 view;
    Mat4 projection;
    Mat4 vp;
    UniformMvp instanced_uniforms;
    Vec3 light_dir = vec3_normalize({ 1.0f, 1.0f, 1.0f });
    Lesson13State *state = lesson13_state(demo);

    if (!state) {
        return;
    }

    ForgeGpuUpdateCameraFromInput(demo);
    ForgeGpuCameraViewProjection(demo, width, height, 200.0f, &view, &projection);
    vp = mat4_multiply(projection, view);

    ForgeGpuDrawBasicGrid(demo, command_buffer, render_pass, demo->lesson.pipeline, vp, &light_dir);

    SDL_BindGPUGraphicsPipeline(render_pass, demo->lesson.secondary_pipeline);
    instanced_uniforms.mvp = vp;
    SDL_PushGPUVertexUniformData(command_buffer, 0, &instanced_uniforms, sizeof(instanced_uniforms));
    render_instanced_model(demo, command_buffer, render_pass, &state->scene_models[0], &light_dir);
    render_instanced_model(demo, command_buffer, render_pass, &state->scene_models[1], &light_dir);
}

void ForgeGpuDestroyLesson13(ForgeGpuDemo *demo)
{
    Lesson13State *state = lesson13_state(demo);

    if (!state) {
        return;
    }
    for (int i = 0; i < 2; i += 1) {
        ForgeGpuFreeSceneData(demo, &state->scene_models[i]);
    }
    SDL_free(state);
    demo->lesson.private_state = nullptr;
}
