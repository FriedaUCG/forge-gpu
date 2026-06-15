#include "forge_gpu_lessons.h"

#include "forge_gpu_camera.h"
#include "forge_gpu_gpu_helpers.h"
#include "forge_gpu_lesson_common.h"
#include "forge_gpu_math.h"
#include "forge_gpu_scene.h"
#include "shaders/generated/forge_gpu_lesson_20_shaders.h"
#include "imgui.h"

#include <stddef.h>

#define LESSON20_MODEL_TRUCK 0
#define LESSON20_MODEL_BOX 1
#define LESSON20_MODEL_COUNT 2
#define LESSON20_BOX_GROUND_COUNT 8
#define LESSON20_BOX_STACK_COUNT 4
#define LESSON20_BOX_TOTAL_COUNT (LESSON20_BOX_GROUND_COUNT + LESSON20_BOX_STACK_COUNT)
#define LESSON20_BOX_RING_RADIUS 5.0f
#define LESSON20_FOG_MODE_LINEAR 0u
#define LESSON20_FOG_MODE_EXP 1u
#define LESSON20_FOG_MODE_EXP2 2u

struct Lesson20FragUniforms
{
    float mat_ambient[4];
    float mat_diffuse[4];
    float mat_specular[4];
    float light_dir[4];
    float eye_pos[4];
    Uint32 has_texture;
    float pad[3];
    float fog_color[4];
    float fog_start;
    float fog_end;
    float fog_density;
    Uint32 fog_mode;
};

struct Lesson20GridFragUniforms
{
    float line_color[4];
    float bg_color[4];
    float light_dir[4];
    float eye_pos[4];
    float grid_spacing;
    float line_width;
    float fade_distance;
    float ambient;
    float shininess;
    float specular_str;
    float pad0;
    float pad1;
    float fog_color[4];
    float fog_start;
    float fog_end;
    float fog_density;
    Uint32 fog_mode;
};

struct Lesson20BoxPlacement
{
    Vec3 position;
    float y_rotation;
};

struct Lesson20State
{
    GpuSceneData models[LESSON20_MODEL_COUNT];
    Lesson20BoxPlacement box_placements[LESSON20_BOX_TOTAL_COUNT];
    int box_count;
    Uint32 fog_mode;
};

static Lesson20State *lesson20_state(ForgeGpuDemo *demo)
{
    return (Lesson20State *)demo->lesson.private_state;
}

static Vec3 lesson20_light_dir(void)
{
    return vec3_normalize({ 0.5f, 1.0f, 0.5f });
}

static const char *lesson20_fog_mode_name(Uint32 mode)
{
    if (mode == LESSON20_FOG_MODE_EXP) {
        return "Exponential";
    }
    if (mode == LESSON20_FOG_MODE_EXP2) {
        return "Exp-squared";
    }
    return "Linear";
}

static float lesson20_fog_density(Uint32 mode)
{
    return mode == LESSON20_FOG_MODE_EXP2 ? 0.08f : 0.12f;
}

static void lesson20_generate_box_placements(Lesson20State *state)
{
    int index = 0;

    for (int i = 0; i < LESSON20_BOX_GROUND_COUNT; i += 1) {
        const float angle = (float)i * (2.0f * FORGE_GPU_PI / (float)LESSON20_BOX_GROUND_COUNT);

        state->box_placements[index].position = {
            SDL_cosf(angle) * LESSON20_BOX_RING_RADIUS,
            0.5f,
            SDL_sinf(angle) * LESSON20_BOX_RING_RADIUS
        };
        state->box_placements[index].y_rotation = angle + 0.3f * (float)i;
        index += 1;
    }

    for (int i = 0; i < LESSON20_BOX_STACK_COUNT; i += 1) {
        const int base = i * 2;

        state->box_placements[index].position = {
            state->box_placements[base].position.x,
            1.5f,
            state->box_placements[base].position.z
        };
        state->box_placements[index].y_rotation = state->box_placements[base].y_rotation + 0.5f;
        index += 1;
    }

    state->box_count = index;
}

static void lesson20_fill_fog_fields(float fog_color[4], float *fog_start, float *fog_end, float *fog_density, Uint32 *fog_mode, Uint32 mode)
{
    fog_color[0] = 0.5f;
    fog_color[1] = 0.5f;
    fog_color[2] = 0.5f;
    fog_color[3] = 1.0f;
    *fog_start = 2.0f;
    *fog_end = 18.0f;
    *fog_density = lesson20_fog_density(mode);
    *fog_mode = mode;
}

static bool lesson20_create_pipelines(ForgeGpuDemo *demo)
{
    SDL_GPUShader *scene_vertex_shader = nullptr;
    SDL_GPUShader *scene_fragment_shader = nullptr;
    SDL_GPUShader *grid_vertex_shader = nullptr;
    SDL_GPUShader *grid_fragment_shader = nullptr;
    SDL_GPUVertexBufferDescription scene_vertex_buffer_desc;
    SDL_GPUVertexAttribute scene_vertex_attributes[3];
    SDL_GPUVertexBufferDescription grid_vertex_buffer_desc;
    SDL_GPUVertexAttribute grid_vertex_attribute;
    bool ok = false;

    scene_vertex_shader = ForgeGpuCreateShader(
        demo->device, SDL_GPU_SHADERSTAGE_VERTEX,
        lesson20_fog_vert_wgsl, lesson20_fog_vert_wgsl_size,
        lesson20_fog_vert_msl, lesson20_fog_vert_msl_size,
        0, 0, 0, 1);
    scene_fragment_shader = ForgeGpuCreateShader(
        demo->device, SDL_GPU_SHADERSTAGE_FRAGMENT,
        lesson20_fog_frag_wgsl, lesson20_fog_frag_wgsl_size,
        lesson20_fog_frag_msl, lesson20_fog_frag_msl_size,
        1, 0, 0, 1);
    grid_vertex_shader = ForgeGpuCreateShader(
        demo->device, SDL_GPU_SHADERSTAGE_VERTEX,
        lesson20_grid_fog_vert_wgsl, lesson20_grid_fog_vert_wgsl_size,
        lesson20_grid_fog_vert_msl, lesson20_grid_fog_vert_msl_size,
        0, 0, 0, 1);
    grid_fragment_shader = ForgeGpuCreateShader(
        demo->device, SDL_GPU_SHADERSTAGE_FRAGMENT,
        lesson20_grid_fog_frag_wgsl, lesson20_grid_fog_frag_wgsl_size,
        lesson20_grid_fog_frag_msl, lesson20_grid_fog_frag_msl_size,
        0, 0, 0, 1);
    if (!scene_vertex_shader || !scene_fragment_shader || !grid_vertex_shader || !grid_fragment_shader) {
        goto done;
    }

    SDL_zero(scene_vertex_buffer_desc);
    scene_vertex_buffer_desc.slot = 0;
    scene_vertex_buffer_desc.pitch = sizeof(ForgeGpuMeshVertex);
    scene_vertex_buffer_desc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    SDL_zeroa(scene_vertex_attributes);
    scene_vertex_attributes[0].location = 0;
    scene_vertex_attributes[0].buffer_slot = 0;
    scene_vertex_attributes[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    scene_vertex_attributes[0].offset = offsetof(ForgeGpuMeshVertex, position);
    scene_vertex_attributes[1].location = 1;
    scene_vertex_attributes[1].buffer_slot = 0;
    scene_vertex_attributes[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    scene_vertex_attributes[1].offset = offsetof(ForgeGpuMeshVertex, normal);
    scene_vertex_attributes[2].location = 2;
    scene_vertex_attributes[2].buffer_slot = 0;
    scene_vertex_attributes[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    scene_vertex_attributes[2].offset = offsetof(ForgeGpuMeshVertex, uv);

    SDL_zero(grid_vertex_buffer_desc);
    grid_vertex_buffer_desc.slot = 0;
    grid_vertex_buffer_desc.pitch = sizeof(GridVertex);
    grid_vertex_buffer_desc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    SDL_zero(grid_vertex_attribute);
    grid_vertex_attribute.location = 0;
    grid_vertex_attribute.buffer_slot = 0;
    grid_vertex_attribute.format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    grid_vertex_attribute.offset = offsetof(GridVertex, position);

    demo->lesson.pipeline = ForgeGpuCreateLessonGraphicsPipeline(
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
        SDL_GPU_CULLMODE_BACK,
        0.0f,
        0.0f);
    demo->lesson.secondary_pipeline = ForgeGpuCreateLessonGraphicsPipeline(
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
    ok = demo->lesson.pipeline && demo->lesson.secondary_pipeline;

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

static void lesson20_draw_grid(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    Mat4 vp,
    const Vec3 *light_dir,
    Uint32 fog_mode)
{
    SDL_GPUBufferBinding vertex_binding;
    SDL_GPUBufferBinding index_binding;
    UniformMvp vertex_uniforms;
    Lesson20GridFragUniforms fragment_uniforms;

    vertex_uniforms.mvp = vp;
    SDL_PushGPUVertexUniformData(command_buffer, 0, &vertex_uniforms, sizeof(vertex_uniforms));

    fragment_uniforms.line_color[0] = 0.35f;
    fragment_uniforms.line_color[1] = 0.35f;
    fragment_uniforms.line_color[2] = 0.35f;
    fragment_uniforms.line_color[3] = 1.0f;
    fragment_uniforms.bg_color[0] = 0.2f;
    fragment_uniforms.bg_color[1] = 0.2f;
    fragment_uniforms.bg_color[2] = 0.2f;
    fragment_uniforms.bg_color[3] = 1.0f;
    fragment_uniforms.light_dir[0] = light_dir->x;
    fragment_uniforms.light_dir[1] = light_dir->y;
    fragment_uniforms.light_dir[2] = light_dir->z;
    fragment_uniforms.light_dir[3] = 0.0f;
    fragment_uniforms.eye_pos[0] = demo->lesson.camera_position.x;
    fragment_uniforms.eye_pos[1] = demo->lesson.camera_position.y;
    fragment_uniforms.eye_pos[2] = demo->lesson.camera_position.z;
    fragment_uniforms.eye_pos[3] = 0.0f;
    fragment_uniforms.grid_spacing = 1.0f;
    fragment_uniforms.line_width = 0.02f;
    fragment_uniforms.fade_distance = 40.0f;
    fragment_uniforms.ambient = 0.15f;
    fragment_uniforms.shininess = 32.0f;
    fragment_uniforms.specular_str = 0.3f;
    fragment_uniforms.pad0 = 0.0f;
    fragment_uniforms.pad1 = 0.0f;
    lesson20_fill_fog_fields(
        fragment_uniforms.fog_color,
        &fragment_uniforms.fog_start,
        &fragment_uniforms.fog_end,
        &fragment_uniforms.fog_density,
        &fragment_uniforms.fog_mode,
        fog_mode);
    SDL_PushGPUFragmentUniformData(command_buffer, 0, &fragment_uniforms, sizeof(fragment_uniforms));

    SDL_zero(vertex_binding);
    vertex_binding.buffer = demo->lesson.vertex_buffer;
    SDL_zero(index_binding);
    index_binding.buffer = demo->lesson.index_buffer;

    SDL_BindGPUGraphicsPipeline(render_pass, demo->lesson.secondary_pipeline);
    SDL_BindGPUVertexBuffers(render_pass, 0, &vertex_binding, 1);
    SDL_BindGPUIndexBuffer(render_pass, &index_binding, SDL_GPU_INDEXELEMENTSIZE_16BIT);
    SDL_DrawGPUIndexedPrimitives(render_pass, 6, 1, 0, 0, 0);
}

static void lesson20_draw_model(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    const GpuSceneData *model,
    Mat4 placement,
    Mat4 view_projection,
    const Vec3 *light_dir,
    Uint32 fog_mode)
{
    for (int node_index = 0; node_index < model->loaded.node_count; node_index += 1) {
        const ForgeGpuSceneNode *node = &model->loaded.nodes[node_index];
        const ForgeGpuSceneMesh *mesh;
        Mat4 model_matrix;
        UniformMvpModel vertex_uniforms;

        if (node->mesh_index < 0 || node->mesh_index >= model->loaded.mesh_count) {
            continue;
        }

        model_matrix = mat4_multiply(placement, mat4_from_forge(node->world_transform));
        vertex_uniforms.mvp = mat4_multiply(view_projection, model_matrix);
        vertex_uniforms.model = model_matrix;
        SDL_PushGPUVertexUniformData(command_buffer, 0, &vertex_uniforms, sizeof(vertex_uniforms));

        mesh = &model->loaded.meshes[node->mesh_index];
        for (int primitive_offset = 0; primitive_offset < mesh->primitive_count; primitive_offset += 1) {
            const int primitive_index = mesh->first_primitive + primitive_offset;
            const GpuPrimitive *primitive;
            GpuMaterial fallback_material;
            const GpuMaterial *material;
            SDL_GPUTextureSamplerBinding sampler_binding;
            SDL_GPUBufferBinding vertex_binding;
            Lesson20FragUniforms fragment_uniforms;

            if (primitive_index < 0 || primitive_index >= model->primitive_count) {
                continue;
            }

            primitive = &model->primitives[primitive_index];
            material = ForgeGpuModelMaterialOrDefault(model, primitive->material_index, &fallback_material);
            if (!primitive->vertex_buffer) {
                continue;
            }

            fragment_uniforms.mat_ambient[0] = material->base_color[0] * 0.2f;
            fragment_uniforms.mat_ambient[1] = material->base_color[1] * 0.2f;
            fragment_uniforms.mat_ambient[2] = material->base_color[2] * 0.2f;
            fragment_uniforms.mat_ambient[3] = 0.0f;
            fragment_uniforms.mat_diffuse[0] = material->base_color[0];
            fragment_uniforms.mat_diffuse[1] = material->base_color[1];
            fragment_uniforms.mat_diffuse[2] = material->base_color[2];
            fragment_uniforms.mat_diffuse[3] = 0.0f;
            fragment_uniforms.mat_specular[0] = 0.3f;
            fragment_uniforms.mat_specular[1] = 0.3f;
            fragment_uniforms.mat_specular[2] = 0.3f;
            fragment_uniforms.mat_specular[3] = 32.0f;
            fragment_uniforms.light_dir[0] = light_dir->x;
            fragment_uniforms.light_dir[1] = light_dir->y;
            fragment_uniforms.light_dir[2] = light_dir->z;
            fragment_uniforms.light_dir[3] = 0.0f;
            fragment_uniforms.eye_pos[0] = demo->lesson.camera_position.x;
            fragment_uniforms.eye_pos[1] = demo->lesson.camera_position.y;
            fragment_uniforms.eye_pos[2] = demo->lesson.camera_position.z;
            fragment_uniforms.eye_pos[3] = 0.0f;
            fragment_uniforms.has_texture = (material->has_texture && material->texture) ? 1u : 0u;
            fragment_uniforms.pad[0] = 0.0f;
            fragment_uniforms.pad[1] = 0.0f;
            fragment_uniforms.pad[2] = 0.0f;
            lesson20_fill_fog_fields(
                fragment_uniforms.fog_color,
                &fragment_uniforms.fog_start,
                &fragment_uniforms.fog_end,
                &fragment_uniforms.fog_density,
                &fragment_uniforms.fog_mode,
                fog_mode);
            SDL_PushGPUFragmentUniformData(command_buffer, 0, &fragment_uniforms, sizeof(fragment_uniforms));

            SDL_zero(sampler_binding);
            sampler_binding.texture = fragment_uniforms.has_texture ? material->texture : demo->lesson.white_texture;
            sampler_binding.sampler = demo->lesson.samplers[0];
            SDL_BindGPUFragmentSamplers(render_pass, 0, &sampler_binding, 1);

            SDL_zero(vertex_binding);
            vertex_binding.buffer = primitive->vertex_buffer;
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
    }
}

bool ForgeGpuCreateLesson20(ForgeGpuDemo *demo)
{
    Lesson20State *state;

    state = (Lesson20State *)SDL_calloc(1, sizeof(*state));
    if (!state) {
        SDL_OutOfMemory();
        return false;
    }
    demo->lesson.private_state = state;
    state->fog_mode = LESSON20_FOG_MODE_LINEAR;

    demo->lesson.white_texture = ForgeGpuCreateWhiteTexture(demo->device);
    demo->lesson.samplers[0] = ForgeGpuCreateSampler(
        demo->device,
        SDL_GPU_FILTER_LINEAR,
        SDL_GPU_FILTER_LINEAR,
        SDL_GPU_SAMPLERMIPMAPMODE_LINEAR,
        1000.0f);

    lesson20_generate_box_placements(state);

    demo->lesson.camera_position = { -6.0f, 5.0f, 6.0f };
    demo->lesson.camera_yaw = -40.0f * FORGE_GPU_DEG2RAD;
    demo->lesson.camera_pitch = -25.0f * FORGE_GPU_DEG2RAD;
    demo->lesson.move_speed = 5.0f;
    demo->lesson.last_ticks = SDL_GetTicks();

    return demo->lesson.white_texture &&
           demo->lesson.samplers[0] &&
           ForgeGpuCreateGridBuffers(demo) &&
           ForgeGpuLoadSceneModel(demo, &state->models[LESSON20_MODEL_TRUCK], "models/CesiumMilkTruck/CesiumMilkTruck.gltf") &&
           ForgeGpuLoadSceneModel(demo, &state->models[LESSON20_MODEL_BOX], "models/BoxTextured/BoxTextured.gltf") &&
           lesson20_create_pipelines(demo);
}

void ForgeGpuRenderLesson20(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    Uint32 width,
    Uint32 height)
{
    Lesson20State *state = lesson20_state(demo);
    Mat4 view;
    Mat4 projection;
    Mat4 view_projection;
    Vec3 light_dir = lesson20_light_dir();

    if (!state) {
        return;
    }

    ForgeGpuUpdateCameraFromInput(demo);
    ForgeGpuCameraViewProjection(demo, width, height, 100.0f, &view, &projection);
    view_projection = mat4_multiply(projection, view);

    lesson20_draw_grid(demo, command_buffer, render_pass, view_projection, &light_dir, state->fog_mode);

    SDL_BindGPUGraphicsPipeline(render_pass, demo->lesson.pipeline);
    lesson20_draw_model(
        demo,
        command_buffer,
        render_pass,
        &state->models[LESSON20_MODEL_TRUCK],
        mat4_identity(),
        view_projection,
        &light_dir,
        state->fog_mode);

    for (int i = 0; i < state->box_count; i += 1) {
        const Mat4 translation = mat4_translate(state->box_placements[i].position);
        const Mat4 rotation = mat4_rotate_y(state->box_placements[i].y_rotation);

        lesson20_draw_model(
            demo,
            command_buffer,
            render_pass,
            &state->models[LESSON20_MODEL_BOX],
            mat4_multiply(translation, rotation),
            view_projection,
            &light_dir,
            state->fog_mode);
    }
}

void ForgeGpuDebugLesson20(ForgeGpuDemo *demo)
{
    Lesson20State *state = lesson20_state(demo);

    if (!state) {
        return;
    }
    ImGui::Text("Fog mode: %s", lesson20_fog_mode_name(state->fog_mode));
    ImGui::Text("Box instances: %d", state->box_count);
}

void ForgeGpuControlsLesson20(ForgeGpuDemo *demo)
{
    (void)demo;
    ImGui::Text("Fog mode: 1 linear, 2 exp, 3 exp-squared");
}

bool ForgeGpuHandleLesson20Event(ForgeGpuDemo *demo, const SDL_Event *event)
{
    Lesson20State *state = lesson20_state(demo);

    if (!state || event->type != SDL_EVENT_KEY_DOWN || event->key.repeat) {
        return false;
    }
    if (event->key.key == SDLK_1) {
        state->fog_mode = LESSON20_FOG_MODE_LINEAR;
        return true;
    }
    if (event->key.key == SDLK_2) {
        state->fog_mode = LESSON20_FOG_MODE_EXP;
        return true;
    }
    if (event->key.key == SDLK_3) {
        state->fog_mode = LESSON20_FOG_MODE_EXP2;
        return true;
    }
    return false;
}

void ForgeGpuDestroyLesson20(ForgeGpuDemo *demo)
{
    Lesson20State *state = lesson20_state(demo);

    if (!state) {
        return;
    }
    for (int i = 0; i < LESSON20_MODEL_COUNT; i += 1) {
        ForgeGpuFreeSceneData(demo, &state->models[i]);
    }
    SDL_free(state);
    demo->lesson.private_state = nullptr;
}
