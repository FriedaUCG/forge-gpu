#include "forge_gpu_lessons.h"

#include "forge_gpu_camera.h"
#include "forge_gpu_gpu_helpers.h"
#include "forge_gpu_lesson_common.h"
#include "forge_gpu_math.h"
#include "shaders/generated/forge_gpu_lesson_17_shaders.h"
#include "imgui.h"

#include <stddef.h>

#define LESSON17_SCENE_Y_OFFSET 1.2f
#define LESSON17_NORMAL_MODE_FLAT 0.0f
#define LESSON17_NORMAL_MODE_VERTEX 1.0f
#define LESSON17_NORMAL_MODE_MAPPED 2.0f

struct Lesson17Vertex
{
    float position[3];
    float normal[3];
    float uv[2];
    float tangent[4];
};

struct Lesson17FragUniforms
{
    float base_color[4];
    float light_dir[4];
    float eye_pos[4];
    float has_texture;
    float has_normal_map;
    float shininess;
    float ambient;
    float specular_str;
    float normal_mode;
    float pad0;
    float pad1;
};

struct Lesson17State
{
    SDL_GPUGraphicsPipeline *grid_pipeline;
    SDL_GPUGraphicsPipeline *scene_pipeline;
    SDL_GPUBuffer **tangent_vertex_buffers;
    int tangent_vertex_buffer_count;
    float normal_mode;
};

static Lesson17State *lesson17_state(ForgeGpuDemo *demo)
{
    return (Lesson17State *)demo->lesson.private_state;
}

static Vec3 lesson17_light_dir(void)
{
    return vec3_normalize({ 0.3f, 0.8f, 0.5f });
}

static const char *lesson17_normal_mode_name(float mode)
{
    if (mode < 0.5f) {
        return "Flat";
    }
    if (mode < 1.5f) {
        return "Per-vertex";
    }
    return "Normal mapped";
}

static Mat4 lesson17_scene_model(const ForgeGpuSceneNode *node)
{
    const Mat4 lift = mat4_translate({ 0.0f, LESSON17_SCENE_Y_OFFSET, 0.0f });
    return mat4_multiply(lift, mat4_from_forge(node->world_transform));
}

static void lesson17_fill_grid_fragment_uniforms(GridFragUniforms *uniforms, const Vec3 *light_dir, const Vec3 *eye_pos)
{
    uniforms->line_color[0] = 0.068f;
    uniforms->line_color[1] = 0.534f;
    uniforms->line_color[2] = 0.932f;
    uniforms->line_color[3] = 1.0f;
    uniforms->bg_color[0] = 0.014f;
    uniforms->bg_color[1] = 0.014f;
    uniforms->bg_color[2] = 0.045f;
    uniforms->bg_color[3] = 1.0f;
    uniforms->light_dir[0] = light_dir->x;
    uniforms->light_dir[1] = light_dir->y;
    uniforms->light_dir[2] = light_dir->z;
    uniforms->light_dir[3] = 0.0f;
    uniforms->eye_pos[0] = eye_pos->x;
    uniforms->eye_pos[1] = eye_pos->y;
    uniforms->eye_pos[2] = eye_pos->z;
    uniforms->eye_pos[3] = 0.0f;
    uniforms->grid_spacing = 1.0f;
    uniforms->line_width = 0.02f;
    uniforms->fade_distance = 40.0f;
    uniforms->ambient = 0.15f;
    uniforms->shininess = 32.0f;
    uniforms->specular_str = 0.4f;
    uniforms->pad0 = 0.0f;
    uniforms->pad1 = 0.0f;
}

static void lesson17_draw_grid(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    SDL_GPUGraphicsPipeline *pipeline,
    Mat4 vp,
    const Vec3 *light_dir)
{
    SDL_GPUBufferBinding vertex_binding;
    SDL_GPUBufferBinding index_binding;
    GridFragUniforms fragment_uniforms;
    UniformMvp vertex_uniforms;

    vertex_uniforms.mvp = vp;
    SDL_PushGPUVertexUniformData(command_buffer, 0, &vertex_uniforms, sizeof(vertex_uniforms));
    lesson17_fill_grid_fragment_uniforms(&fragment_uniforms, light_dir, &demo->lesson.camera_position);
    SDL_PushGPUFragmentUniformData(command_buffer, 0, &fragment_uniforms, sizeof(fragment_uniforms));

    SDL_zero(vertex_binding);
    vertex_binding.buffer = demo->lesson.vertex_buffer;
    SDL_zero(index_binding);
    index_binding.buffer = demo->lesson.index_buffer;

    SDL_BindGPUGraphicsPipeline(render_pass, pipeline);
    SDL_BindGPUVertexBuffers(render_pass, 0, &vertex_binding, 1);
    SDL_BindGPUIndexBuffer(render_pass, &index_binding, SDL_GPU_INDEXELEMENTSIZE_16BIT);
    SDL_DrawGPUIndexedPrimitives(render_pass, 6, 1, 0, 0, 0);
}

static bool lesson17_create_tangent_vertex_buffers(ForgeGpuDemo *demo, Lesson17State *state)
{
    const LessonState *lesson = &demo->lesson;

    state->tangent_vertex_buffer_count = lesson->scene.primitive_count;
    if (state->tangent_vertex_buffer_count <= 0) {
        SDL_SetError("lesson 17 scene has no primitives");
        return false;
    }

    state->tangent_vertex_buffers = (SDL_GPUBuffer **)SDL_calloc(
        (size_t)state->tangent_vertex_buffer_count,
        sizeof(*state->tangent_vertex_buffers));
    if (!state->tangent_vertex_buffers) {
        SDL_OutOfMemory();
        return false;
    }

    for (int i = 0; i < state->tangent_vertex_buffer_count; i += 1) {
        const ForgeGpuScenePrimitive *src = &lesson->scene.primitives[i];
        Lesson17Vertex *vertices;
        const Uint32 vertex_bytes = src->vertex_count * (Uint32)sizeof(*vertices);

        if (!src->has_tangents || !src->tangents) {
            SDL_SetError("lesson 17 primitive %d is missing supplied tangents", i);
            return false;
        }

        vertices = (Lesson17Vertex *)SDL_calloc((size_t)src->vertex_count, sizeof(*vertices));
        if (!vertices) {
            SDL_OutOfMemory();
            return false;
        }

        for (Uint32 v = 0; v < src->vertex_count; v += 1) {
            SDL_memcpy(vertices[v].position, src->vertices[v].position, sizeof(vertices[v].position));
            SDL_memcpy(vertices[v].normal, src->vertices[v].normal, sizeof(vertices[v].normal));
            SDL_memcpy(vertices[v].uv, src->vertices[v].uv, sizeof(vertices[v].uv));
            SDL_memcpy(vertices[v].tangent, src->tangents + (size_t)v * 4u, sizeof(vertices[v].tangent));
        }

        state->tangent_vertex_buffers[i] = ForgeGpuCreateBufferWithData(
            demo->device,
            SDL_GPU_BUFFERUSAGE_VERTEX,
            vertices,
            vertex_bytes);
        SDL_free(vertices);
        if (!state->tangent_vertex_buffers[i]) {
            return false;
        }
    }

    return true;
}

static bool lesson17_create_pipelines(ForgeGpuDemo *demo, Lesson17State *state)
{
    SDL_GPUShader *scene_vertex_shader = nullptr;
    SDL_GPUShader *scene_fragment_shader = nullptr;
    SDL_GPUShader *grid_vertex_shader = nullptr;
    SDL_GPUShader *grid_fragment_shader = nullptr;
    SDL_GPUVertexBufferDescription scene_vertex_buffer_desc;
    SDL_GPUVertexAttribute scene_vertex_attributes[4];
    SDL_GPUVertexBufferDescription grid_vertex_buffer_desc;
    SDL_GPUVertexAttribute grid_vertex_attribute;
    bool ok = false;

    scene_vertex_shader = ForgeGpuCreateShader(
        demo->device, SDL_GPU_SHADERSTAGE_VERTEX,
        lesson17_scene_vert_wgsl, lesson17_scene_vert_wgsl_size,
        lesson17_scene_vert_msl, lesson17_scene_vert_msl_size,
        0, 0, 0, 1);
    scene_fragment_shader = ForgeGpuCreateShader(
        demo->device, SDL_GPU_SHADERSTAGE_FRAGMENT,
        lesson17_scene_frag_wgsl, lesson17_scene_frag_wgsl_size,
        lesson17_scene_frag_msl, lesson17_scene_frag_msl_size,
        2, 0, 0, 1);
    grid_vertex_shader = ForgeGpuCreateShader(
        demo->device, SDL_GPU_SHADERSTAGE_VERTEX,
        lesson17_grid_vert_wgsl, lesson17_grid_vert_wgsl_size,
        lesson17_grid_vert_msl, lesson17_grid_vert_msl_size,
        0, 0, 0, 1);
    grid_fragment_shader = ForgeGpuCreateShader(
        demo->device, SDL_GPU_SHADERSTAGE_FRAGMENT,
        lesson17_grid_frag_wgsl, lesson17_grid_frag_wgsl_size,
        lesson17_grid_frag_msl, lesson17_grid_frag_msl_size,
        0, 0, 0, 1);
    if (!scene_vertex_shader || !scene_fragment_shader || !grid_vertex_shader || !grid_fragment_shader) {
        goto done;
    }

    SDL_zero(scene_vertex_buffer_desc);
    scene_vertex_buffer_desc.slot = 0;
    scene_vertex_buffer_desc.pitch = sizeof(Lesson17Vertex);
    scene_vertex_buffer_desc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    SDL_zeroa(scene_vertex_attributes);
    scene_vertex_attributes[0].location = 0;
    scene_vertex_attributes[0].buffer_slot = 0;
    scene_vertex_attributes[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    scene_vertex_attributes[0].offset = offsetof(Lesson17Vertex, position);
    scene_vertex_attributes[1].location = 1;
    scene_vertex_attributes[1].buffer_slot = 0;
    scene_vertex_attributes[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    scene_vertex_attributes[1].offset = offsetof(Lesson17Vertex, normal);
    scene_vertex_attributes[2].location = 2;
    scene_vertex_attributes[2].buffer_slot = 0;
    scene_vertex_attributes[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    scene_vertex_attributes[2].offset = offsetof(Lesson17Vertex, uv);
    scene_vertex_attributes[3].location = 3;
    scene_vertex_attributes[3].buffer_slot = 0;
    scene_vertex_attributes[3].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
    scene_vertex_attributes[3].offset = offsetof(Lesson17Vertex, tangent);

    SDL_zero(grid_vertex_buffer_desc);
    grid_vertex_buffer_desc.slot = 0;
    grid_vertex_buffer_desc.pitch = sizeof(GridVertex);
    grid_vertex_buffer_desc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    SDL_zero(grid_vertex_attribute);
    grid_vertex_attribute.location = 0;
    grid_vertex_attribute.buffer_slot = 0;
    grid_vertex_attribute.format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    grid_vertex_attribute.offset = offsetof(GridVertex, position);

    state->scene_pipeline = ForgeGpuCreateLessonGraphicsPipeline(
        demo,
        scene_vertex_shader,
        scene_fragment_shader,
        &scene_vertex_buffer_desc,
        1,
        scene_vertex_attributes,
        SDL_arraysize(scene_vertex_attributes),
        1,
        true,
        SDL_GPU_TEXTUREFORMAT_D32_FLOAT,
        true,
        true,
        SDL_GPU_CULLMODE_NONE,
        0.0f,
        0.0f);
    state->grid_pipeline = ForgeGpuCreateLessonGraphicsPipeline(
        demo,
        grid_vertex_shader,
        grid_fragment_shader,
        &grid_vertex_buffer_desc,
        1,
        &grid_vertex_attribute,
        1,
        1,
        true,
        SDL_GPU_TEXTUREFORMAT_D32_FLOAT,
        true,
        true,
        SDL_GPU_CULLMODE_NONE,
        0.0f,
        0.0f);

    ok = state->scene_pipeline && state->grid_pipeline;

done:
    if (grid_fragment_shader) {
        SDL_ReleaseGPUShader(demo->device, grid_fragment_shader);
    }
    if (grid_vertex_shader) {
        SDL_ReleaseGPUShader(demo->device, grid_vertex_shader);
    }
    if (scene_fragment_shader) {
        SDL_ReleaseGPUShader(demo->device, scene_fragment_shader);
    }
    if (scene_vertex_shader) {
        SDL_ReleaseGPUShader(demo->device, scene_vertex_shader);
    }
    return ok;
}

static void lesson17_draw_primitive(
    ForgeGpuDemo *demo,
    Lesson17State *state,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    Mat4 view_projection,
    int node_index,
    int primitive_index,
    const Vec3 *light_dir)
{
    LessonState *lesson = &demo->lesson;
    const ForgeGpuSceneNode *node;
    const GpuPrimitive *primitive;
    GpuMaterial fallback_material;
    const GpuMaterial *material;
    SDL_GPUBufferBinding vertex_binding;
    SDL_GPUTextureSamplerBinding sampler_bindings[2];
    UniformMvpModel vertex_uniforms;
    Lesson17FragUniforms fragment_uniforms;
    Mat4 model;

    if (primitive_index < 0 ||
        primitive_index >= state->tangent_vertex_buffer_count ||
        !state->tangent_vertex_buffers[primitive_index]) {
        return;
    }

    node = &lesson->scene.nodes[node_index];
    primitive = &lesson->gpu_primitives[primitive_index];
    material = ForgeGpuSceneMaterialOrDefault(lesson, primitive->material_index, &fallback_material);
    model = lesson17_scene_model(node);

    vertex_uniforms.model = model;
    vertex_uniforms.mvp = mat4_multiply(view_projection, model);
    SDL_PushGPUVertexUniformData(command_buffer, 0, &vertex_uniforms, sizeof(vertex_uniforms));

    SDL_memcpy(fragment_uniforms.base_color, material->base_color, sizeof(fragment_uniforms.base_color));
    fragment_uniforms.light_dir[0] = light_dir->x;
    fragment_uniforms.light_dir[1] = light_dir->y;
    fragment_uniforms.light_dir[2] = light_dir->z;
    fragment_uniforms.light_dir[3] = 0.0f;
    fragment_uniforms.eye_pos[0] = lesson->camera_position.x;
    fragment_uniforms.eye_pos[1] = lesson->camera_position.y;
    fragment_uniforms.eye_pos[2] = lesson->camera_position.z;
    fragment_uniforms.eye_pos[3] = 0.0f;
    fragment_uniforms.has_texture = (material->has_texture && material->texture) ? 1.0f : 0.0f;
    fragment_uniforms.has_normal_map = (material->has_normal_map && material->normal_texture) ? 1.0f : 0.0f;
    fragment_uniforms.shininess = 32.0f;
    fragment_uniforms.ambient = 0.15f;
    fragment_uniforms.specular_str = 0.4f;
    fragment_uniforms.normal_mode = state->normal_mode;
    fragment_uniforms.pad0 = 0.0f;
    fragment_uniforms.pad1 = 0.0f;
    SDL_PushGPUFragmentUniformData(command_buffer, 0, &fragment_uniforms, sizeof(fragment_uniforms));

    SDL_zeroa(sampler_bindings);
    sampler_bindings[0].texture = fragment_uniforms.has_texture > 0.5f ? material->texture : lesson->white_texture;
    sampler_bindings[0].sampler = lesson->samplers[0];
    sampler_bindings[1].texture = fragment_uniforms.has_normal_map > 0.5f ? material->normal_texture : lesson->white_texture;
    sampler_bindings[1].sampler = lesson->samplers[0];
    SDL_BindGPUFragmentSamplers(render_pass, 0, sampler_bindings, SDL_arraysize(sampler_bindings));

    SDL_zero(vertex_binding);
    vertex_binding.buffer = state->tangent_vertex_buffers[primitive_index];
    SDL_BindGPUVertexBuffers(render_pass, 0, &vertex_binding, 1);

    if (primitive->index_buffer && primitive->index_count > 0) {
        SDL_GPUBufferBinding index_binding;

        SDL_zero(index_binding);
        index_binding.buffer = primitive->index_buffer;
        SDL_BindGPUIndexBuffer(render_pass, &index_binding, primitive->index_type);
        SDL_DrawGPUIndexedPrimitives(render_pass, primitive->index_count, 1, 0, 0, 0);
    } else {
        SDL_DrawGPUPrimitives(render_pass, primitive->vertex_count, 1, 0, 0);
    }
}

bool ForgeGpuCreateLesson17(ForgeGpuDemo *demo)
{
    Lesson17State *state;
    ForgeGpuSceneLoadRequirements requirements;

    state = (Lesson17State *)SDL_calloc(1, sizeof(*state));
    if (!state) {
        SDL_OutOfMemory();
        return false;
    }
    demo->lesson.private_state = state;
    state->normal_mode = LESSON17_NORMAL_MODE_MAPPED;

    if (!ForgeGpuCreateGridBuffers(demo)) {
        return false;
    }

    SDL_zero(requirements);
    requirements.required_features = FORGE_GPU_SCENE_FEATURE_TANGENTS |
                                     FORGE_GPU_SCENE_FEATURE_NORMAL_MAPS;
    requirements.required_all_primitive_features = FORGE_GPU_SCENE_FEATURE_TANGENTS;
    requirements.required_all_material_features = FORGE_GPU_SCENE_FEATURE_NORMAL_MAPS;
    if (!ForgeGpuLoadLessonSceneWithRequirements(
            demo,
            "models/NormalTangentMirrorTest/NormalTangentMirrorTest.gltf",
            &requirements)) {
        return false;
    }

    if (!lesson17_create_tangent_vertex_buffers(demo, state)) {
        return false;
    }

    demo->lesson.camera_position = { 0.0f, 1.5f, 3.5f };
    demo->lesson.camera_yaw = 0.0f;
    demo->lesson.camera_pitch = 0.0f;
    demo->lesson.move_speed = 5.0f;
    demo->lesson.last_ticks = SDL_GetTicks();

    return lesson17_create_pipelines(demo, state);
}

void ForgeGpuRenderLesson17(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    Uint32 width,
    Uint32 height)
{
    Lesson17State *state = lesson17_state(demo);
    LessonState *lesson = &demo->lesson;
    Mat4 view;
    Mat4 projection;
    Mat4 view_projection;
    Vec3 light_dir = lesson17_light_dir();

    if (!state) {
        return;
    }

    ForgeGpuUpdateCameraFromInput(demo);
    ForgeGpuCameraViewProjection(demo, width, height, 100.0f, &view, &projection);
    view_projection = mat4_multiply(projection, view);

    lesson17_draw_grid(demo, command_buffer, render_pass, state->grid_pipeline, view_projection, &light_dir);

    SDL_BindGPUGraphicsPipeline(render_pass, state->scene_pipeline);
    for (int node_index = 0; node_index < lesson->scene.node_count; node_index += 1) {
        const ForgeGpuSceneNode *node = &lesson->scene.nodes[node_index];
        const ForgeGpuSceneMesh *mesh;

        if (node->mesh_index < 0 || node->mesh_index >= lesson->scene.mesh_count) {
            continue;
        }
        mesh = &lesson->scene.meshes[node->mesh_index];

        for (int primitive_offset = 0; primitive_offset < mesh->primitive_count; primitive_offset += 1) {
            const int primitive_index = mesh->first_primitive + primitive_offset;

            if (primitive_index < 0 || primitive_index >= lesson->gpu_primitive_count) {
                continue;
            }
            lesson17_draw_primitive(
                demo,
                state,
                command_buffer,
                render_pass,
                view_projection,
                node_index,
                primitive_index,
                &light_dir);
        }
    }
}

void ForgeGpuDebugLesson17(ForgeGpuDemo *demo)
{
    Lesson17State *state = lesson17_state(demo);

    if (!state) {
        return;
    }
    ImGui::Text("Normal mode: %s", lesson17_normal_mode_name(state->normal_mode));
    ImGui::Text("Tangent primitives: %d", state->tangent_vertex_buffer_count);
}

void ForgeGpuControlsLesson17(ForgeGpuDemo *demo)
{
    (void)demo;
    ImGui::Text("Shading: 1 flat, 2 per-vertex, 3 normal mapped");
}

bool ForgeGpuHandleLesson17Event(ForgeGpuDemo *demo, const SDL_Event *event)
{
    Lesson17State *state = lesson17_state(demo);

    if (!state || event->type != SDL_EVENT_KEY_DOWN || event->key.repeat) {
        return false;
    }

    if (event->key.key == SDLK_1) {
        state->normal_mode = LESSON17_NORMAL_MODE_FLAT;
        return true;
    }
    if (event->key.key == SDLK_2) {
        state->normal_mode = LESSON17_NORMAL_MODE_VERTEX;
        return true;
    }
    if (event->key.key == SDLK_3) {
        state->normal_mode = LESSON17_NORMAL_MODE_MAPPED;
        return true;
    }
    return false;
}

void ForgeGpuDestroyLesson17(ForgeGpuDemo *demo)
{
    Lesson17State *state = lesson17_state(demo);

    if (!state) {
        return;
    }
    if (state->tangent_vertex_buffers) {
        for (int i = 0; i < state->tangent_vertex_buffer_count; i += 1) {
            if (state->tangent_vertex_buffers[i]) {
                SDL_ReleaseGPUBuffer(demo->device, state->tangent_vertex_buffers[i]);
            }
        }
    }
    if (state->scene_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, state->scene_pipeline);
    }
    if (state->grid_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, state->grid_pipeline);
    }
    SDL_free(state->tangent_vertex_buffers);
    SDL_free(state);
    demo->lesson.private_state = nullptr;
}
