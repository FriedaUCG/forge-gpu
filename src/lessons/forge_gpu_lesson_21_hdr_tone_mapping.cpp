#include "forge_gpu_lessons.h"

#include "forge_gpu_browser_status.h"
#include "forge_gpu_camera.h"
#include "forge_gpu_gpu_helpers.h"
#include "forge_gpu_lesson_common.h"
#include "forge_gpu_math.h"
#include "forge_gpu_scene.h"
#include "forge_gpu_shader_layouts.h"
#include "shaders/generated/forge_gpu_lesson_21_shaders.h"
#include "imgui.h"

#include <stddef.h>

#define LESSON21_MODEL_TRUCK 0
#define LESSON21_MODEL_BOX 1
#define LESSON21_MODEL_COUNT 2
#define LESSON21_BOX_GROUND_COUNT 8
#define LESSON21_BOX_STACK_COUNT 4
#define LESSON21_BOX_TOTAL_COUNT (LESSON21_BOX_GROUND_COUNT + LESSON21_BOX_STACK_COUNT)
#define LESSON21_HDR_FORMAT SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT
#define LESSON21_TONEMAP_NONE 0u
#define LESSON21_TONEMAP_REINHARD 1u
#define LESSON21_TONEMAP_ACES 2u
#define LESSON21_EXPOSURE_STEP 0.1f
#define LESSON21_EXPOSURE_MIN 0.1f
#define LESSON21_EXPOSURE_MAX 10.0f
#define LESSON21_MAX_ANISOTROPY 4.0f

struct Lesson21ShadowMatrices
{
    Mat4 light_vp[FORGE_GPU_SHADOW_CASCADE_COUNT];
};

struct Lesson21SceneFragUniforms
{
    float base_color[4];
    float light_dir[4];
    float eye_pos[4];
    float cascade_splits[4];
    Uint32 has_texture;
    float shininess;
    float ambient;
    float specular_str;
    float light_intensity;
    float shadow_texel_size;
    float shadow_bias;
    float pad0;
};

struct Lesson21GridVertUniforms
{
    Mat4 vp;
};

struct Lesson21GridFragUniforms
{
    float line_color[4];
    float bg_color[4];
    float light_dir[4];
    float eye_pos[4];
    float cascade_splits[4];
    float grid_spacing;
    float line_width;
    float fade_distance;
    float ambient;
    float shininess;
    float specular_str;
    float light_intensity;
    float shadow_texel_size;
    float shadow_bias;
    float pad[3];
};

struct Lesson21TonemapFragUniforms
{
    float exposure;
    Uint32 tonemap_mode;
    float pad[2];
};

struct Lesson21BoxPlacement
{
    Vec3 position;
    float y_rotation;
};

struct Lesson21State
{
    GpuSceneData models[LESSON21_MODEL_COUNT];
    SDL_GPUGraphicsPipeline *tonemap_pipeline;
    SDL_GPUTexture *shadow_maps[FORGE_GPU_SHADOW_CASCADE_COUNT];
    SDL_GPUTexture *hdr_target;
    Uint32 hdr_width;
    Uint32 hdr_height;
    Lesson21BoxPlacement box_placements[LESSON21_BOX_TOTAL_COUNT];
    int box_count;
    float exposure;
    Uint32 tonemap_mode;
};

static Lesson21State *lesson21_state(ForgeGpuDemo *demo)
{
    return (Lesson21State *)demo->lesson.private_state;
}

static Vec3 lesson21_light_dir(void)
{
    return vec3_normalize({ 1.0f, 1.0f, 0.5f });
}

static const char *lesson21_tonemap_name(Uint32 mode)
{
    if (mode == LESSON21_TONEMAP_REINHARD) {
        return "Reinhard";
    }
    if (mode == LESSON21_TONEMAP_ACES) {
        return "ACES";
    }
    return "Clamp";
}

static float lesson21_clamp_exposure(float exposure)
{
    if (exposure < LESSON21_EXPOSURE_MIN) {
        return LESSON21_EXPOSURE_MIN;
    }
    if (exposure > LESSON21_EXPOSURE_MAX) {
        return LESSON21_EXPOSURE_MAX;
    }
    return exposure;
}

static SDL_GPUTexture *lesson21_create_shadow_map(ForgeGpuDemo *demo)
{
    SDL_GPUTextureCreateInfo texture_info;

    SDL_zero(texture_info);
    texture_info.type = SDL_GPU_TEXTURETYPE_2D;
    texture_info.format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    texture_info.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    texture_info.width = FORGE_GPU_SHADOW_MAP_SIZE;
    texture_info.height = FORGE_GPU_SHADOW_MAP_SIZE;
    texture_info.layer_count_or_depth = 1;
    texture_info.num_levels = 1;
    texture_info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    return SDL_CreateGPUTexture(demo->device, &texture_info);
}

static bool lesson21_create_hdr_target(ForgeGpuDemo *demo, Uint32 width, Uint32 height)
{
    Lesson21State *state = lesson21_state(demo);

    if (!state) {
        SDL_SetError("lesson 21 internal state is missing");
        return false;
    }
    return ForgeGpuEnsureSampledColorTarget(
        demo,
        &state->hdr_target,
        &state->hdr_width,
        &state->hdr_height,
        width,
        height,
        LESSON21_HDR_FORMAT);
}

static void lesson21_generate_box_placements(Lesson21State *state)
{
    int index = 0;

    for (int i = 0; i < LESSON21_BOX_GROUND_COUNT; i += 1) {
        const float angle = (float)i * (2.0f * FORGE_GPU_PI / (float)LESSON21_BOX_GROUND_COUNT);

        state->box_placements[index].position = {
            SDL_cosf(angle) * 5.0f,
            0.5f,
            SDL_sinf(angle) * 5.0f
        };
        state->box_placements[index].y_rotation = angle;
        index += 1;
    }

    for (int i = 0; i < LESSON21_BOX_STACK_COUNT; i += 1) {
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

static bool lesson21_create_pipelines(ForgeGpuDemo *demo)
{
    Lesson21State *state = lesson21_state(demo);
    SDL_GPUShader *shadow_vertex_shader = nullptr;
    SDL_GPUShader *shadow_fragment_shader = nullptr;
    SDL_GPUShader *scene_vertex_shader = nullptr;
    SDL_GPUShader *scene_fragment_shader = nullptr;
    SDL_GPUShader *grid_vertex_shader = nullptr;
    SDL_GPUShader *grid_fragment_shader = nullptr;
    SDL_GPUShader *tonemap_vertex_shader = nullptr;
    SDL_GPUShader *tonemap_fragment_shader = nullptr;
    SDL_GPUVertexBufferDescription mesh_vertex_buffer_desc;
    SDL_GPUVertexAttribute mesh_vertex_attributes[3];
    SDL_GPUVertexBufferDescription grid_vertex_buffer_desc;
    SDL_GPUVertexAttribute grid_vertex_attribute;
    bool ok = false;

    SDL_zero(mesh_vertex_buffer_desc);
    mesh_vertex_buffer_desc.slot = 0;
    mesh_vertex_buffer_desc.pitch = sizeof(ForgeGpuMeshVertex);
    mesh_vertex_buffer_desc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    SDL_zeroa(mesh_vertex_attributes);
    mesh_vertex_attributes[0].location = 0;
    mesh_vertex_attributes[0].buffer_slot = 0;
    mesh_vertex_attributes[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    mesh_vertex_attributes[0].offset = offsetof(ForgeGpuMeshVertex, position);
    mesh_vertex_attributes[1].location = 1;
    mesh_vertex_attributes[1].buffer_slot = 0;
    mesh_vertex_attributes[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    mesh_vertex_attributes[1].offset = offsetof(ForgeGpuMeshVertex, normal);
    mesh_vertex_attributes[2].location = 2;
    mesh_vertex_attributes[2].buffer_slot = 0;
    mesh_vertex_attributes[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    mesh_vertex_attributes[2].offset = offsetof(ForgeGpuMeshVertex, uv);

    SDL_zero(grid_vertex_buffer_desc);
    grid_vertex_buffer_desc.slot = 0;
    grid_vertex_buffer_desc.pitch = sizeof(GridVertex);
    grid_vertex_buffer_desc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    SDL_zero(grid_vertex_attribute);
    grid_vertex_attribute.location = 0;
    grid_vertex_attribute.buffer_slot = 0;
    grid_vertex_attribute.format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    grid_vertex_attribute.offset = offsetof(GridVertex, position);

    shadow_vertex_shader = ForgeGpuCreateShader(
        demo->device, SDL_GPU_SHADERSTAGE_VERTEX,
        lesson21_shadow_vert_wgsl, lesson21_shadow_vert_wgsl_size,
        lesson21_shadow_vert_msl, lesson21_shadow_vert_msl_size,
        0, 0, 0, 1);
    shadow_fragment_shader = ForgeGpuCreateShader(
        demo->device, SDL_GPU_SHADERSTAGE_FRAGMENT,
        lesson21_shadow_frag_wgsl, lesson21_shadow_frag_wgsl_size,
        lesson21_shadow_frag_msl, lesson21_shadow_frag_msl_size,
        0, 0, 0, 0);
    scene_vertex_shader = ForgeGpuCreateShader(
        demo->device, SDL_GPU_SHADERSTAGE_VERTEX,
        lesson21_scene_vert_wgsl, lesson21_scene_vert_wgsl_size,
        lesson21_scene_vert_msl, lesson21_scene_vert_msl_size,
        0, 0, 0, 2);
    scene_fragment_shader = ForgeGpuCreateShaderWithResourceLayout(
        demo->device,
        lesson21_scene_frag_wgsl, lesson21_scene_frag_wgsl_size,
        lesson21_scene_frag_msl, lesson21_scene_frag_msl_size,
        ForgeGpuShaderLayout_lesson21_scene_frag());
    grid_vertex_shader = ForgeGpuCreateShader(
        demo->device, SDL_GPU_SHADERSTAGE_VERTEX,
        lesson21_grid_vert_wgsl, lesson21_grid_vert_wgsl_size,
        lesson21_grid_vert_msl, lesson21_grid_vert_msl_size,
        0, 0, 0, 2);
    grid_fragment_shader = ForgeGpuCreateShaderWithResourceLayout(
        demo->device,
        lesson21_grid_frag_wgsl, lesson21_grid_frag_wgsl_size,
        lesson21_grid_frag_msl, lesson21_grid_frag_msl_size,
        ForgeGpuShaderLayout_lesson21_grid_frag());
    tonemap_vertex_shader = ForgeGpuCreateShader(
        demo->device, SDL_GPU_SHADERSTAGE_VERTEX,
        lesson21_tonemap_vert_wgsl, lesson21_tonemap_vert_wgsl_size,
        lesson21_tonemap_vert_msl, lesson21_tonemap_vert_msl_size,
        0, 0, 0, 0);
    tonemap_fragment_shader = ForgeGpuCreateShader(
        demo->device, SDL_GPU_SHADERSTAGE_FRAGMENT,
        lesson21_tonemap_frag_wgsl, lesson21_tonemap_frag_wgsl_size,
        lesson21_tonemap_frag_msl, lesson21_tonemap_frag_msl_size,
        1, 0, 0, 1);
    if (!shadow_vertex_shader || !shadow_fragment_shader ||
        !scene_vertex_shader || !scene_fragment_shader ||
        !grid_vertex_shader || !grid_fragment_shader ||
        !tonemap_vertex_shader || !tonemap_fragment_shader) {
        goto done;
    }

    demo->lesson.pipeline = ForgeGpuCreateLessonGraphicsPipeline(
        demo,
        shadow_vertex_shader,
        shadow_fragment_shader,
        &mesh_vertex_buffer_desc,
        1,
        mesh_vertex_attributes,
        SDL_arraysize(mesh_vertex_attributes),
        0,
        true,
        SDL_GPU_TEXTUREFORMAT_D32_FLOAT,
        true,
        true,
        SDL_GPU_CULLMODE_BACK,
        20.5f,
        20.5f);
    demo->lesson.secondary_pipeline = ForgeGpuCreateLessonGraphicsPipelineWithColorFormat(
        demo,
        scene_vertex_shader,
        scene_fragment_shader,
        SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        LESSON21_HDR_FORMAT,
        &mesh_vertex_buffer_desc,
        1,
        mesh_vertex_attributes,
        SDL_arraysize(mesh_vertex_attributes),
        1,
        true,
        SDL_GPU_TEXTUREFORMAT_D32_FLOAT,
        true,
        true,
        SDL_GPU_CULLMODE_BACK,
        0.0f,
        0.0f);
    demo->lesson.tertiary_pipeline = ForgeGpuCreateLessonGraphicsPipelineWithColorFormat(
        demo,
        grid_vertex_shader,
        grid_fragment_shader,
        SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        LESSON21_HDR_FORMAT,
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
    if (!state) {
        SDL_SetError("lesson 21 internal state is missing");
        goto done;
    }

    state->tonemap_pipeline = ForgeGpuCreateFullscreenPostprocessPipeline(
        demo,
        tonemap_vertex_shader,
        tonemap_fragment_shader,
        demo->color_format,
        false);
    ok = demo->lesson.pipeline &&
         demo->lesson.secondary_pipeline &&
         demo->lesson.tertiary_pipeline &&
         state->tonemap_pipeline;

done:
    if (tonemap_fragment_shader) {
        SDL_ReleaseGPUShader(demo->device, tonemap_fragment_shader);
    }
    if (tonemap_vertex_shader) {
        SDL_ReleaseGPUShader(demo->device, tonemap_vertex_shader);
    }
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
    if (shadow_fragment_shader) {
        SDL_ReleaseGPUShader(demo->device, shadow_fragment_shader);
    }
    if (shadow_vertex_shader) {
        SDL_ReleaseGPUShader(demo->device, shadow_vertex_shader);
    }
    return ok;
}

bool ForgeGpuCreateLesson21(ForgeGpuDemo *demo)
{
    LessonState *lesson = &demo->lesson;
    Lesson21State *state;

    if (!SDL_GPUTextureSupportsFormat(
            demo->device,
            LESSON21_HDR_FORMAT,
            SDL_GPU_TEXTURETYPE_2D,
            SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER)) {
        SDL_SetError("lesson 21 requires sampled R16G16B16A16_FLOAT color targets");
        return false;
    }
    if (!SDL_GPUTextureSupportsFormat(
            demo->device,
            SDL_GPU_TEXTUREFORMAT_D32_FLOAT,
            SDL_GPU_TEXTURETYPE_2D,
            SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER)) {
        SDL_SetError("lesson 21 requires sampled D32_FLOAT depth textures");
        return false;
    }

    state = (Lesson21State *)SDL_calloc(1, sizeof(*state));
    if (!state) {
        SDL_OutOfMemory();
        return false;
    }
    lesson->private_state = state;

    if (!ForgeGpuCreateGridBuffers(demo)) {
        return false;
    }

    lesson->white_texture = ForgeGpuCreateWhiteTexture(demo->device);
    lesson->samplers[0] = ForgeGpuCreateSamplerWithAddressAndAnisotropy(
        demo->device,
        SDL_GPU_FILTER_LINEAR,
        SDL_GPU_FILTER_LINEAR,
        SDL_GPU_SAMPLERMIPMAPMODE_LINEAR,
        SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
        1000.0f,
        LESSON21_MAX_ANISOTROPY);
    lesson->samplers[1] = ForgeGpuCreateSamplerWithAddress(
        demo->device,
        SDL_GPU_FILTER_NEAREST,
        SDL_GPU_FILTER_NEAREST,
        SDL_GPU_SAMPLERMIPMAPMODE_NEAREST,
        SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        0.0f);
    lesson->samplers[2] = ForgeGpuCreateSamplerWithAddress(
        demo->device,
        SDL_GPU_FILTER_NEAREST,
        SDL_GPU_FILTER_NEAREST,
        SDL_GPU_SAMPLERMIPMAPMODE_NEAREST,
        SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        0.0f);
    if (!lesson->white_texture || !lesson->samplers[0] || !lesson->samplers[1] || !lesson->samplers[2]) {
        return false;
    }

    if (!ForgeGpuLoadSceneModel(demo, &state->models[LESSON21_MODEL_TRUCK], "models/CesiumMilkTruck/CesiumMilkTruck.gltf") ||
        !ForgeGpuLoadSceneModel(demo, &state->models[LESSON21_MODEL_BOX], "models/BoxTextured/BoxTextured.gltf")) {
        return false;
    }
    for (int i = 0; i < FORGE_GPU_SHADOW_CASCADE_COUNT; i += 1) {
        state->shadow_maps[i] = lesson21_create_shadow_map(demo);
        if (!state->shadow_maps[i]) {
            return false;
        }
    }

    lesson21_generate_box_placements(state);
    lesson->camera_position = { -6.1f, 7.0f, 4.4f };
    lesson->camera_yaw = -50.0f * FORGE_GPU_DEG2RAD;
    lesson->camera_pitch = -50.0f * FORGE_GPU_DEG2RAD;
    lesson->move_speed = 5.0f;
    lesson->last_ticks = SDL_GetTicks();
    state->exposure = 1.0f;
    state->tonemap_mode = LESSON21_TONEMAP_ACES;

    return lesson21_create_pipelines(demo);
}

static void lesson21_draw_model_shadow(
    SDL_GPURenderPass *render_pass,
    SDL_GPUCommandBuffer *command_buffer,
    const GpuSceneData *model,
    Mat4 placement,
    Mat4 light_vp)
{
    for (int node_index = 0; node_index < model->loaded.node_count; node_index += 1) {
        const ForgeGpuSceneNode *node = &model->loaded.nodes[node_index];
        const ForgeGpuSceneMesh *mesh;
        Mat4 model_matrix;
        UniformMvp uniforms;

        if (node->mesh_index < 0 || node->mesh_index >= model->loaded.mesh_count) {
            continue;
        }
        model_matrix = mat4_multiply(placement, mat4_from_forge(node->world_transform));
        uniforms.mvp = mat4_multiply(light_vp, model_matrix);
        SDL_PushGPUVertexUniformData(command_buffer, 0, &uniforms, sizeof(uniforms));

        mesh = &model->loaded.meshes[node->mesh_index];
        for (int primitive_offset = 0; primitive_offset < mesh->primitive_count; primitive_offset += 1) {
            const int primitive_index = mesh->first_primitive + primitive_offset;
            SDL_GPUBufferBinding vertex_binding;
            const GpuPrimitive *primitive;

            if (primitive_index < 0 || primitive_index >= model->primitive_count) {
                continue;
            }
            primitive = &model->primitives[primitive_index];
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

static void lesson21_draw_model_scene(
    ForgeGpuDemo *demo,
    SDL_GPURenderPass *render_pass,
    SDL_GPUCommandBuffer *command_buffer,
    const GpuSceneData *model,
    Mat4 placement,
    Mat4 cam_vp,
    const Lesson21ShadowMatrices *shadow_mats,
    const Vec3 *light_dir,
    const float *cascade_splits,
    const Lesson21State *state)
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
        vertex_uniforms.model = model_matrix;
        vertex_uniforms.mvp = mat4_multiply(cam_vp, model_matrix);
        SDL_PushGPUVertexUniformData(command_buffer, 0, &vertex_uniforms, sizeof(vertex_uniforms));
        SDL_PushGPUVertexUniformData(command_buffer, 1, shadow_mats, sizeof(*shadow_mats));

        mesh = &model->loaded.meshes[node->mesh_index];
        for (int primitive_offset = 0; primitive_offset < mesh->primitive_count; primitive_offset += 1) {
            const int primitive_index = mesh->first_primitive + primitive_offset;
            GpuMaterial fallback_material;
            const GpuPrimitive *primitive;
            const GpuMaterial *material;
            SDL_GPUTextureSamplerBinding sampler_bindings[4];
            SDL_GPUBufferBinding vertex_binding;
            Lesson21SceneFragUniforms fragment_uniforms;

            if (primitive_index < 0 || primitive_index >= model->primitive_count) {
                continue;
            }
            primitive = &model->primitives[primitive_index];
            material = ForgeGpuModelMaterialOrDefault(model, primitive->material_index, &fallback_material);

            SDL_zero(fragment_uniforms);
            SDL_memcpy(fragment_uniforms.base_color, material->base_color, sizeof(fragment_uniforms.base_color));
            fragment_uniforms.light_dir[0] = light_dir->x;
            fragment_uniforms.light_dir[1] = light_dir->y;
            fragment_uniforms.light_dir[2] = light_dir->z;
            fragment_uniforms.eye_pos[0] = demo->lesson.camera_position.x;
            fragment_uniforms.eye_pos[1] = demo->lesson.camera_position.y;
            fragment_uniforms.eye_pos[2] = demo->lesson.camera_position.z;
            fragment_uniforms.cascade_splits[0] = cascade_splits[0];
            fragment_uniforms.cascade_splits[1] = cascade_splits[1];
            fragment_uniforms.cascade_splits[2] = cascade_splits[2];
            fragment_uniforms.has_texture = material->has_texture ? 1u : 0u;
            fragment_uniforms.shininess = 64.0f;
            fragment_uniforms.ambient = 0.1f;
            fragment_uniforms.specular_str = 1.0f;
            fragment_uniforms.light_intensity = 3.0f;
            fragment_uniforms.shadow_texel_size = 1.0f / (float)FORGE_GPU_SHADOW_MAP_SIZE;
            fragment_uniforms.shadow_bias = 0.0053f;
            SDL_PushGPUFragmentUniformData(command_buffer, 0, &fragment_uniforms, sizeof(fragment_uniforms));

            SDL_zeroa(sampler_bindings);
            sampler_bindings[0].texture = material->has_texture ? material->texture : demo->lesson.white_texture;
            sampler_bindings[0].sampler = demo->lesson.samplers[0];
            for (int i = 0; i < FORGE_GPU_SHADOW_CASCADE_COUNT; i += 1) {
                sampler_bindings[1 + i].texture = state->shadow_maps[i];
                sampler_bindings[1 + i].sampler = demo->lesson.samplers[1];
            }
            SDL_BindGPUFragmentSamplers(render_pass, 0, sampler_bindings, SDL_arraysize(sampler_bindings));

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

static void lesson21_draw_shadowed_grid(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    Mat4 cam_vp,
    const Lesson21ShadowMatrices *shadow_mats,
    const Vec3 *light_dir,
    const float *cascade_splits,
    const Lesson21State *state)
{
    SDL_GPUTextureSamplerBinding shadow_bindings[FORGE_GPU_SHADOW_CASCADE_COUNT];
    SDL_GPUBufferBinding vertex_binding;
    SDL_GPUBufferBinding index_binding;
    Lesson21GridVertUniforms vertex_uniforms;
    Lesson21GridFragUniforms fragment_uniforms;

    SDL_BindGPUGraphicsPipeline(render_pass, demo->lesson.tertiary_pipeline);
    vertex_uniforms.vp = cam_vp;
    SDL_PushGPUVertexUniformData(command_buffer, 0, &vertex_uniforms, sizeof(vertex_uniforms));
    SDL_PushGPUVertexUniformData(command_buffer, 1, shadow_mats, sizeof(*shadow_mats));

    SDL_zero(fragment_uniforms);
    fragment_uniforms.line_color[0] = 0.15f;
    fragment_uniforms.line_color[1] = 0.55f;
    fragment_uniforms.line_color[2] = 0.85f;
    fragment_uniforms.line_color[3] = 1.0f;
    fragment_uniforms.bg_color[0] = 0.04f;
    fragment_uniforms.bg_color[1] = 0.04f;
    fragment_uniforms.bg_color[2] = 0.08f;
    fragment_uniforms.bg_color[3] = 1.0f;
    fragment_uniforms.light_dir[0] = light_dir->x;
    fragment_uniforms.light_dir[1] = light_dir->y;
    fragment_uniforms.light_dir[2] = light_dir->z;
    fragment_uniforms.eye_pos[0] = demo->lesson.camera_position.x;
    fragment_uniforms.eye_pos[1] = demo->lesson.camera_position.y;
    fragment_uniforms.eye_pos[2] = demo->lesson.camera_position.z;
    fragment_uniforms.cascade_splits[0] = cascade_splits[0];
    fragment_uniforms.cascade_splits[1] = cascade_splits[1];
    fragment_uniforms.cascade_splits[2] = cascade_splits[2];
    fragment_uniforms.grid_spacing = 1.0f;
    fragment_uniforms.line_width = 0.02f;
    fragment_uniforms.fade_distance = 40.0f;
    fragment_uniforms.ambient = 0.15f;
    fragment_uniforms.shininess = 32.0f;
    fragment_uniforms.specular_str = 0.5f;
    fragment_uniforms.light_intensity = 3.0f;
    fragment_uniforms.shadow_texel_size = 1.0f / (float)FORGE_GPU_SHADOW_MAP_SIZE;
    fragment_uniforms.shadow_bias = 0.0053f;
    SDL_PushGPUFragmentUniformData(command_buffer, 0, &fragment_uniforms, sizeof(fragment_uniforms));

    SDL_zeroa(shadow_bindings);
    for (int i = 0; i < FORGE_GPU_SHADOW_CASCADE_COUNT; i += 1) {
        shadow_bindings[i].texture = state->shadow_maps[i];
        shadow_bindings[i].sampler = demo->lesson.samplers[1];
    }
    SDL_BindGPUFragmentSamplers(render_pass, 0, shadow_bindings, SDL_arraysize(shadow_bindings));

    SDL_zero(vertex_binding);
    vertex_binding.buffer = demo->lesson.vertex_buffer;
    SDL_BindGPUVertexBuffers(render_pass, 0, &vertex_binding, 1);
    SDL_zero(index_binding);
    index_binding.buffer = demo->lesson.index_buffer;
    SDL_BindGPUIndexBuffer(render_pass, &index_binding, SDL_GPU_INDEXELEMENTSIZE_16BIT);
    SDL_DrawGPUIndexedPrimitives(render_pass, SDL_arraysize(kForgeGpuGridIndices), 1, 0, 0, 0);
}

bool ForgeGpuRenderLesson21(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPUTexture *swapchain_texture,
    Uint32 width,
    Uint32 height)
{
    Mat4 view;
    Mat4 projection;
    Mat4 cam_vp;
    Mat4 inv_cam_vp;
    Lesson21ShadowMatrices shadow_mats;
    float cascade_splits[FORGE_GPU_SHADOW_CASCADE_COUNT];
    const Vec3 light_dir = lesson21_light_dir();
    const Mat4 truck_placement = mat4_identity();
    Lesson21State *state = lesson21_state(demo);

    if (!state) {
        SDL_SetError("lesson 21 internal state is missing");
        return false;
    }

    ForgeGpuUpdateCameraFromInput(demo);
    ForgeGpuCameraViewProjection(demo, width, height, 100.0f, &view, &projection);
    cam_vp = mat4_multiply(projection, view);
    inv_cam_vp = mat4_inverse(cam_vp);
    ForgeGpuComputeCascadeSplits(0.1f, 100.0f, cascade_splits);
    ForgeGpuComputeCascadeLightViewProjections(
        inv_cam_vp,
        0.1f,
        100.0f,
        light_dir,
        cascade_splits,
        shadow_mats.light_vp);

    for (int cascade = 0; cascade < FORGE_GPU_SHADOW_CASCADE_COUNT; cascade += 1) {
        SDL_GPUDepthStencilTargetInfo shadow_depth;
        SDL_GPURenderPass *shadow_pass;

        SDL_zero(shadow_depth);
        shadow_depth.texture = state->shadow_maps[cascade];
        shadow_depth.load_op = SDL_GPU_LOADOP_CLEAR;
        shadow_depth.store_op = SDL_GPU_STOREOP_STORE;
        shadow_depth.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
        shadow_depth.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
        shadow_depth.clear_depth = 1.0f;

        shadow_pass = SDL_BeginGPURenderPass(command_buffer, nullptr, 0, &shadow_depth);
        if (!shadow_pass) {
            return false;
        }

        SDL_BindGPUGraphicsPipeline(shadow_pass, demo->lesson.pipeline);
        lesson21_draw_model_shadow(
            shadow_pass,
            command_buffer,
            &state->models[LESSON21_MODEL_TRUCK],
            truck_placement,
            shadow_mats.light_vp[cascade]);
        for (int i = 0; i < state->box_count; i += 1) {
            const Mat4 t = mat4_translate(state->box_placements[i].position);
            const Mat4 r = mat4_rotate_y(state->box_placements[i].y_rotation);
            const Mat4 placement = mat4_multiply(t, r);

            lesson21_draw_model_shadow(
                shadow_pass,
                command_buffer,
                &state->models[LESSON21_MODEL_BOX],
                placement,
                shadow_mats.light_vp[cascade]);
        }
        SDL_EndGPURenderPass(shadow_pass);
    }

    if (!ForgeGpuCreateDepthTextureWithFormat(demo, width, height, SDL_GPU_TEXTUREFORMAT_D32_FLOAT) ||
        !lesson21_create_hdr_target(demo, width, height)) {
        return false;
    }

    {
        SDL_GPUColorTargetInfo color_target;
        SDL_GPUDepthStencilTargetInfo depth_target;
        SDL_GPURenderPass *render_pass;

        SDL_zero(color_target);
        color_target.texture = state->hdr_target;
        color_target.load_op = SDL_GPU_LOADOP_CLEAR;
        color_target.store_op = SDL_GPU_STOREOP_STORE;
        color_target.clear_color = { 0.008f, 0.008f, 0.026f, 1.0f };

        SDL_zero(depth_target);
        depth_target.texture = demo->lesson.depth_texture;
        depth_target.load_op = SDL_GPU_LOADOP_CLEAR;
        depth_target.store_op = SDL_GPU_STOREOP_DONT_CARE;
        depth_target.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
        depth_target.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
        depth_target.clear_depth = 1.0f;

        render_pass = SDL_BeginGPURenderPass(command_buffer, &color_target, 1, &depth_target);
        if (!render_pass) {
            return false;
        }

        lesson21_draw_shadowed_grid(demo, command_buffer, render_pass, cam_vp, &shadow_mats, &light_dir, cascade_splits, state);

        SDL_BindGPUGraphicsPipeline(render_pass, demo->lesson.secondary_pipeline);
        lesson21_draw_model_scene(
            demo,
            render_pass,
            command_buffer,
            &state->models[LESSON21_MODEL_TRUCK],
            truck_placement,
            cam_vp,
            &shadow_mats,
            &light_dir,
            cascade_splits,
            state);
        for (int i = 0; i < state->box_count; i += 1) {
            const Mat4 t = mat4_translate(state->box_placements[i].position);
            const Mat4 r = mat4_rotate_y(state->box_placements[i].y_rotation);
            const Mat4 placement = mat4_multiply(t, r);

            lesson21_draw_model_scene(
                demo,
                render_pass,
                command_buffer,
                &state->models[LESSON21_MODEL_BOX],
                placement,
                cam_vp,
                &shadow_mats,
                &light_dir,
                cascade_splits,
                state);
        }

        SDL_EndGPURenderPass(render_pass);
    }

    {
        SDL_GPUTextureSamplerBinding hdr_binding;
        Lesson21TonemapFragUniforms tonemap_uniforms;

        SDL_zero(hdr_binding);
        hdr_binding.texture = state->hdr_target;
        hdr_binding.sampler = demo->lesson.samplers[2];

        SDL_zero(tonemap_uniforms);
        tonemap_uniforms.exposure = state->exposure;
        tonemap_uniforms.tonemap_mode = state->tonemap_mode;
        return ForgeGpuRunFullscreenPostprocessPass(
            command_buffer,
            swapchain_texture,
            SDL_GPU_LOADOP_DONT_CARE,
            { 0.0f, 0.0f, 0.0f, 1.0f },
            state->tonemap_pipeline,
            &hdr_binding,
            1,
            &tonemap_uniforms,
            sizeof(tonemap_uniforms),
            6);
    }
}

void ForgeGpuDebugLesson21(ForgeGpuDemo *demo)
{
    Lesson21State *state = lesson21_state(demo);

    if (!state) {
        return;
    }
    ImGui::Text("Tone map: %s", lesson21_tonemap_name(state->tonemap_mode));
    ImGui::Text("Exposure: %.1f", (double)state->exposure);
    if (state->hdr_target) {
        ImGui::Text("HDR target: %ux%u R16G16B16A16_FLOAT", state->hdr_width, state->hdr_height);
    }
}

void ForgeGpuControlsLesson21(ForgeGpuDemo *demo)
{
    (void)demo;
    ImGui::Text("1: Clamp");
    ImGui::Text("2: Reinhard");
    ImGui::Text("3: ACES");
    ImGui::Text("+/-: Exposure up/down");
}

static void lesson21_export_metrics(Lesson21State *state)
{
    if (!state) {
        return;
    }
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson21ExposureTenths", (double)((int)(state->exposure * 10.0f + 0.5f)));
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson21TonemapMode", (double)state->tonemap_mode);
}

bool ForgeGpuHandleLesson21Event(ForgeGpuDemo *demo, const SDL_Event *event)
{
    Lesson21State *state = lesson21_state(demo);

    if (!state || event->type != SDL_EVENT_KEY_DOWN) {
        return false;
    }
    if (!event->key.repeat) {
        if (event->key.key == SDLK_1) {
            state->tonemap_mode = LESSON21_TONEMAP_NONE;
            return true;
        }
        if (event->key.key == SDLK_2) {
            state->tonemap_mode = LESSON21_TONEMAP_REINHARD;
            return true;
        }
        if (event->key.key == SDLK_3) {
            state->tonemap_mode = LESSON21_TONEMAP_ACES;
            return true;
        }
    }
    if (ForgeGpuEventIsPlusKey(event)) {
        state->exposure = lesson21_clamp_exposure(state->exposure + LESSON21_EXPOSURE_STEP);
        return true;
    }
    if (ForgeGpuEventIsMinusKey(event)) {
        state->exposure = lesson21_clamp_exposure(state->exposure - LESSON21_EXPOSURE_STEP);
        return true;
    }
    return false;
}

void ForgeGpuExportLesson21Metrics(ForgeGpuDemo *demo)
{
    lesson21_export_metrics(lesson21_state(demo));
}

void ForgeGpuDestroyLesson21(ForgeGpuDemo *demo)
{
    Lesson21State *state = lesson21_state(demo);

    if (!state) {
        return;
    }
    for (int i = 0; i < LESSON21_MODEL_COUNT; i += 1) {
        ForgeGpuFreeSceneData(demo, &state->models[i]);
    }
    for (int i = 0; i < FORGE_GPU_SHADOW_CASCADE_COUNT; i += 1) {
        if (state->shadow_maps[i]) {
            SDL_ReleaseGPUTexture(demo->device, state->shadow_maps[i]);
        }
    }
    if (state->hdr_target) {
        SDL_ReleaseGPUTexture(demo->device, state->hdr_target);
    }
    if (state->tonemap_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, state->tonemap_pipeline);
    }
    SDL_free(state);
    demo->lesson.private_state = nullptr;
}
