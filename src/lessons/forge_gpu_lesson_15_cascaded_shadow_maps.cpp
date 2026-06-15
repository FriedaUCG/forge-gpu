#include "forge_gpu_lessons.h"

#include "forge_gpu_camera.h"
#include "forge_gpu_gpu_helpers.h"
#include "forge_gpu_lesson_common.h"
#include "forge_gpu_math.h"
#include "forge_gpu_scene.h"
#include "forge_gpu_shader_layouts.h"
#include "shaders/generated/forge_gpu_lesson_15_shaders.h"
#include "imgui.h"

#include <stddef.h>

struct ShadowMatrices
{
    Mat4 light_vp[FORGE_GPU_SHADOW_CASCADE_COUNT];
};

struct SceneShadowFragUniforms
{
    float base_color[4];
    float light_dir[4];
    float eye_pos[4];
    float cascade_splits[4];
    Uint32 has_texture;
    float shininess;
    float ambient;
    float specular_str;
    float shadow_texel_size;
    float shadow_bias;
    float pad0;
    float pad1;
};

struct GridShadowFragUniforms
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
    float shadow_texel_size;
    float shadow_bias;
};

struct DebugVertUniforms
{
    float quad_bounds[4];
};

struct BoxPlacement
{
    Vec3 position;
    float y_rotation;
};

struct Lesson15State
{
    GpuSceneData scene_models[2];
    SDL_GPUTexture *shadow_maps[FORGE_GPU_SHADOW_CASCADE_COUNT];
    BoxPlacement box_placements[16];
    int box_count;
    bool show_shadow_map;
};

static Lesson15State *lesson15_state(ForgeGpuDemo *demo)
{
    return (Lesson15State *)demo->lesson.private_state;
}

static SDL_GPUTexture *create_shadow_map(ForgeGpuDemo *demo)
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

static void generate_shadow_box_placements(Lesson15State *state)
{
    int idx = 0;

    for (int i = 0; i < 8; i += 1) {
        const float angle = (float)i * (2.0f * FORGE_GPU_PI / 8.0f);
        state->box_placements[idx].position = {
            SDL_cosf(angle) * 5.0f,
            0.5f,
            SDL_sinf(angle) * 5.0f
        };
        state->box_placements[idx].y_rotation = angle + 0.3f * (float)i;
        idx += 1;
    }

    for (int i = 0; i < 4; i += 1) {
        const int base = i * 2;
        state->box_placements[idx].position = {
            state->box_placements[base].position.x,
            1.5f,
            state->box_placements[base].position.z
        };
        state->box_placements[idx].y_rotation = state->box_placements[base].y_rotation + 0.5f;
        idx += 1;
    }

    state->box_count = idx;
}

static bool create_lesson15_pipelines(ForgeGpuDemo *demo)
{
    SDL_GPUVertexBufferDescription mesh_vb_desc;
    SDL_GPUVertexAttribute mesh_attrs[3];
    SDL_GPUVertexBufferDescription grid_vb_desc;
    SDL_GPUVertexAttribute grid_attr;

    SDL_zero(mesh_vb_desc);
    mesh_vb_desc.slot = 0;
    mesh_vb_desc.pitch = sizeof(ForgeGpuMeshVertex);
    mesh_vb_desc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    SDL_zeroa(mesh_attrs);
    mesh_attrs[0].location = 0;
    mesh_attrs[0].buffer_slot = 0;
    mesh_attrs[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    mesh_attrs[0].offset = offsetof(ForgeGpuMeshVertex, position);
    mesh_attrs[1].location = 1;
    mesh_attrs[1].buffer_slot = 0;
    mesh_attrs[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    mesh_attrs[1].offset = offsetof(ForgeGpuMeshVertex, normal);
    mesh_attrs[2].location = 2;
    mesh_attrs[2].buffer_slot = 0;
    mesh_attrs[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    mesh_attrs[2].offset = offsetof(ForgeGpuMeshVertex, uv);

    SDL_zero(grid_vb_desc);
    grid_vb_desc.slot = 0;
    grid_vb_desc.pitch = sizeof(GridVertex);
    grid_vb_desc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    SDL_zero(grid_attr);
    grid_attr.location = 0;
    grid_attr.buffer_slot = 0;
    grid_attr.format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    grid_attr.offset = offsetof(GridVertex, position);

    {
        SDL_GPUShader *vs = ForgeGpuCreateShader(
            demo->device, SDL_GPU_SHADERSTAGE_VERTEX,
            lesson15_shadow_vert_wgsl, lesson15_shadow_vert_wgsl_size,
            lesson15_shadow_vert_msl, lesson15_shadow_vert_msl_size,
            0, 0, 0, 1);
        SDL_GPUShader *fs = ForgeGpuCreateShader(
            demo->device, SDL_GPU_SHADERSTAGE_FRAGMENT,
            lesson15_shadow_frag_wgsl, lesson15_shadow_frag_wgsl_size,
            lesson15_shadow_frag_msl, lesson15_shadow_frag_msl_size,
            0, 0, 0, 0);
        if (!vs || !fs) {
            if (vs) {
                SDL_ReleaseGPUShader(demo->device, vs);
            }
            if (fs) {
                SDL_ReleaseGPUShader(demo->device, fs);
            }
            return false;
        }
        demo->lesson.pipeline = ForgeGpuCreateLessonGraphicsPipeline(
            demo, vs, fs, &mesh_vb_desc, 1, mesh_attrs, SDL_arraysize(mesh_attrs),
            0, true, SDL_GPU_TEXTUREFORMAT_D32_FLOAT,
            true, true, SDL_GPU_CULLMODE_FRONT, 1.0f, 1.5f);
        SDL_ReleaseGPUShader(demo->device, vs);
        SDL_ReleaseGPUShader(demo->device, fs);
        if (!demo->lesson.pipeline) {
            return false;
        }
    }

    {
        SDL_GPUShader *vs = ForgeGpuCreateShader(
            demo->device, SDL_GPU_SHADERSTAGE_VERTEX,
            lesson15_scene_vert_wgsl, lesson15_scene_vert_wgsl_size,
            lesson15_scene_vert_msl, lesson15_scene_vert_msl_size,
            0, 0, 0, 2);
        SDL_GPUShader *fs = ForgeGpuCreateShaderWithResourceLayout(
            demo->device,
            lesson15_scene_frag_wgsl, lesson15_scene_frag_wgsl_size,
            lesson15_scene_frag_msl, lesson15_scene_frag_msl_size,
            ForgeGpuShaderLayout_lesson15_scene_frag());
        if (!vs || !fs) {
            if (vs) {
                SDL_ReleaseGPUShader(demo->device, vs);
            }
            if (fs) {
                SDL_ReleaseGPUShader(demo->device, fs);
            }
            return false;
        }
        demo->lesson.secondary_pipeline = ForgeGpuCreateLessonGraphicsPipeline(
            demo, vs, fs, &mesh_vb_desc, 1, mesh_attrs, SDL_arraysize(mesh_attrs),
            1, true, SDL_GPU_TEXTUREFORMAT_D32_FLOAT,
            true, true, SDL_GPU_CULLMODE_BACK, 0.0f, 0.0f);
        SDL_ReleaseGPUShader(demo->device, vs);
        SDL_ReleaseGPUShader(demo->device, fs);
        if (!demo->lesson.secondary_pipeline) {
            return false;
        }
    }

    {
        SDL_GPUShader *vs = ForgeGpuCreateShader(
            demo->device, SDL_GPU_SHADERSTAGE_VERTEX,
            lesson15_grid_vert_wgsl, lesson15_grid_vert_wgsl_size,
            lesson15_grid_vert_msl, lesson15_grid_vert_msl_size,
            0, 0, 0, 2);
        SDL_GPUShader *fs = ForgeGpuCreateShaderWithResourceLayout(
            demo->device,
            lesson15_grid_frag_wgsl, lesson15_grid_frag_wgsl_size,
            lesson15_grid_frag_msl, lesson15_grid_frag_msl_size,
            ForgeGpuShaderLayout_lesson15_grid_frag());
        if (!vs || !fs) {
            if (vs) {
                SDL_ReleaseGPUShader(demo->device, vs);
            }
            if (fs) {
                SDL_ReleaseGPUShader(demo->device, fs);
            }
            return false;
        }
        demo->lesson.tertiary_pipeline = ForgeGpuCreateLessonGraphicsPipeline(
            demo, vs, fs, &grid_vb_desc, 1, &grid_attr, 1,
            1, true, SDL_GPU_TEXTUREFORMAT_D32_FLOAT,
            true, true, SDL_GPU_CULLMODE_NONE, 0.0f, 0.0f);
        SDL_ReleaseGPUShader(demo->device, vs);
        SDL_ReleaseGPUShader(demo->device, fs);
        if (!demo->lesson.tertiary_pipeline) {
            return false;
        }
    }

    {
        SDL_GPUShader *vs = ForgeGpuCreateShader(
            demo->device, SDL_GPU_SHADERSTAGE_VERTEX,
            lesson15_debug_quad_vert_wgsl, lesson15_debug_quad_vert_wgsl_size,
            lesson15_debug_quad_vert_msl, lesson15_debug_quad_vert_msl_size,
            0, 0, 0, 1);
        SDL_GPUShader *fs = ForgeGpuCreateShaderWithResourceLayout(
            demo->device,
            lesson15_debug_quad_frag_wgsl, lesson15_debug_quad_frag_wgsl_size,
            lesson15_debug_quad_frag_msl, lesson15_debug_quad_frag_msl_size,
            ForgeGpuShaderLayout_lesson15_debug_quad_frag());
        if (!vs || !fs) {
            if (vs) {
                SDL_ReleaseGPUShader(demo->device, vs);
            }
            if (fs) {
                SDL_ReleaseGPUShader(demo->device, fs);
            }
            return false;
        }
        demo->lesson.debug_pipeline = ForgeGpuCreateLessonGraphicsPipeline(
            demo, vs, fs, nullptr, 0, nullptr, 0,
            1, true, SDL_GPU_TEXTUREFORMAT_D32_FLOAT,
            false, false, SDL_GPU_CULLMODE_NONE, 0.0f, 0.0f);
        SDL_ReleaseGPUShader(demo->device, vs);
        SDL_ReleaseGPUShader(demo->device, fs);
        if (!demo->lesson.debug_pipeline) {
            return false;
        }
    }

    return true;
}

bool ForgeGpuCreateLesson15(ForgeGpuDemo *demo)
{
    LessonState *lesson = &demo->lesson;
    Lesson15State *state;

    if (!SDL_GPUTextureSupportsFormat(
            demo->device,
            SDL_GPU_TEXTUREFORMAT_D32_FLOAT,
            SDL_GPU_TEXTURETYPE_2D,
            SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER)) {
        SDL_SetError("lesson 15 requires sampled D32_FLOAT depth textures");
        return false;
    }

    state = (Lesson15State *)SDL_calloc(1, sizeof(*state));
    if (!state) {
        SDL_OutOfMemory();
        return false;
    }
    lesson->private_state = state;

    if (!ForgeGpuCreateGridBuffers(demo)) {
        return false;
    }
    if (!ForgeGpuLoadSceneModel(demo, &state->scene_models[0], "models/CesiumMilkTruck/CesiumMilkTruck.gltf") ||
        !ForgeGpuLoadSceneModel(demo, &state->scene_models[1], "models/BoxTextured/BoxTextured.gltf")) {
        return false;
    }
    for (int i = 0; i < FORGE_GPU_SHADOW_CASCADE_COUNT; i += 1) {
        state->shadow_maps[i] = create_shadow_map(demo);
        if (!state->shadow_maps[i]) {
            return false;
        }
    }

    if (!lesson->samplers[0]) {
        lesson->samplers[0] = ForgeGpuCreateSampler(
            demo->device,
            SDL_GPU_FILTER_LINEAR,
            SDL_GPU_FILTER_LINEAR,
            SDL_GPU_SAMPLERMIPMAPMODE_LINEAR,
            1000.0f);
    }
    lesson->samplers[1] = ForgeGpuCreateSamplerWithAddress(
        demo->device,
        SDL_GPU_FILTER_NEAREST,
        SDL_GPU_FILTER_NEAREST,
        SDL_GPU_SAMPLERMIPMAPMODE_NEAREST,
        SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        0.0f);
    if (!lesson->samplers[0] || !lesson->samplers[1]) {
        return false;
    }

    generate_shadow_box_placements(state);
    lesson->camera_position = { -6.1f, 7.0f, 4.4f };
    lesson->camera_yaw = -50.0f * FORGE_GPU_DEG2RAD;
    lesson->camera_pitch = -50.0f * FORGE_GPU_DEG2RAD;
    lesson->move_speed = 5.0f;
    lesson->last_ticks = SDL_GetTicks();
    state->show_shadow_map = false;

    return create_lesson15_pipelines(demo);
}

static void draw_model_shadow(
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

static void draw_model_scene(
    ForgeGpuDemo *demo,
    SDL_GPURenderPass *render_pass,
    SDL_GPUCommandBuffer *command_buffer,
    const GpuSceneData *model,
    Mat4 placement,
    Mat4 cam_vp,
    const ShadowMatrices *shadow_mats,
    const Vec3 *light_dir,
    const float *cascade_splits,
    const Lesson15State *state)
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
            SceneShadowFragUniforms fragment_uniforms;

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
            fragment_uniforms.cascade_splits[0] = cascade_splits[0];
            fragment_uniforms.cascade_splits[1] = cascade_splits[1];
            fragment_uniforms.cascade_splits[2] = cascade_splits[2];
            fragment_uniforms.cascade_splits[3] = 0.0f;
            fragment_uniforms.has_texture = material->has_texture ? 1u : 0u;
            fragment_uniforms.shininess = 64.0f;
            fragment_uniforms.ambient = 0.15f;
            fragment_uniforms.specular_str = 0.5f;
            fragment_uniforms.shadow_texel_size = 1.0f / (float)FORGE_GPU_SHADOW_MAP_SIZE;
            fragment_uniforms.shadow_bias = 0.0005f;
            fragment_uniforms.pad0 = 0.0f;
            fragment_uniforms.pad1 = 0.0f;
            SDL_PushGPUFragmentUniformData(command_buffer, 0, &fragment_uniforms, sizeof(fragment_uniforms));

            SDL_zeroa(sampler_bindings);
            sampler_bindings[0].texture = material->has_texture ? material->texture : demo->lesson.white_texture;
            sampler_bindings[0].sampler = demo->lesson.samplers[0];
            for (int i = 0; i < FORGE_GPU_SHADOW_CASCADE_COUNT; i += 1) {
                sampler_bindings[1 + i].texture = state->shadow_maps[i];
                sampler_bindings[1 + i].sampler = demo->lesson.samplers[1];
            }
            SDL_BindGPUFragmentSamplers(render_pass, 0, sampler_bindings, 4);

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

static void draw_shadowed_grid(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    Mat4 cam_vp,
    const ShadowMatrices *shadow_mats,
    const Vec3 *light_dir,
    const float *cascade_splits,
    const Lesson15State *state)
{
    SDL_GPUTextureSamplerBinding shadow_bindings[FORGE_GPU_SHADOW_CASCADE_COUNT];
    SDL_GPUBufferBinding vertex_binding;
    SDL_GPUBufferBinding index_binding;
    GridShadowFragUniforms fragment_uniforms;
    UniformMvp vertex_uniforms;

    SDL_BindGPUGraphicsPipeline(render_pass, demo->lesson.tertiary_pipeline);
    vertex_uniforms.mvp = cam_vp;
    SDL_PushGPUVertexUniformData(command_buffer, 0, &vertex_uniforms, sizeof(vertex_uniforms));
    SDL_PushGPUVertexUniformData(command_buffer, 1, shadow_mats, sizeof(*shadow_mats));

    fragment_uniforms.line_color[0] = 0.068f;
    fragment_uniforms.line_color[1] = 0.534f;
    fragment_uniforms.line_color[2] = 0.932f;
    fragment_uniforms.line_color[3] = 1.0f;
    fragment_uniforms.bg_color[0] = 0.014f;
    fragment_uniforms.bg_color[1] = 0.014f;
    fragment_uniforms.bg_color[2] = 0.045f;
    fragment_uniforms.bg_color[3] = 1.0f;
    fragment_uniforms.light_dir[0] = light_dir->x;
    fragment_uniforms.light_dir[1] = light_dir->y;
    fragment_uniforms.light_dir[2] = light_dir->z;
    fragment_uniforms.light_dir[3] = 0.0f;
    fragment_uniforms.eye_pos[0] = demo->lesson.camera_position.x;
    fragment_uniforms.eye_pos[1] = demo->lesson.camera_position.y;
    fragment_uniforms.eye_pos[2] = demo->lesson.camera_position.z;
    fragment_uniforms.eye_pos[3] = 0.0f;
    fragment_uniforms.cascade_splits[0] = cascade_splits[0];
    fragment_uniforms.cascade_splits[1] = cascade_splits[1];
    fragment_uniforms.cascade_splits[2] = cascade_splits[2];
    fragment_uniforms.cascade_splits[3] = 0.0f;
    fragment_uniforms.grid_spacing = 1.0f;
    fragment_uniforms.line_width = 0.02f;
    fragment_uniforms.fade_distance = 40.0f;
    fragment_uniforms.ambient = 0.3f;
    fragment_uniforms.shininess = 32.0f;
    fragment_uniforms.specular_str = 0.2f;
    fragment_uniforms.shadow_texel_size = 1.0f / (float)FORGE_GPU_SHADOW_MAP_SIZE;
    fragment_uniforms.shadow_bias = 0.0005f;
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
    SDL_DrawGPUIndexedPrimitives(render_pass, 6, 1, 0, 0, 0);
}

bool ForgeGpuRenderLesson15(
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
    ShadowMatrices shadow_mats;
    float cascade_splits[FORGE_GPU_SHADOW_CASCADE_COUNT];
    Vec3 light_dir = vec3_normalize({ 1.0f, 1.0f, 0.5f });
    Mat4 truck_placement = mat4_identity();
    Lesson15State *state = lesson15_state(demo);

    if (!state) {
        SDL_SetError("lesson 15 internal state is missing");
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
        draw_model_shadow(shadow_pass, command_buffer, &state->scene_models[0], truck_placement, shadow_mats.light_vp[cascade]);
        for (int i = 0; i < state->box_count; i += 1) {
            const Mat4 t = mat4_translate(state->box_placements[i].position);
            const Mat4 r = mat4_rotate_y(state->box_placements[i].y_rotation);
            const Mat4 placement = mat4_multiply(t, r);
            draw_model_shadow(shadow_pass, command_buffer, &state->scene_models[1], placement, shadow_mats.light_vp[cascade]);
        }
        SDL_EndGPURenderPass(shadow_pass);
    }

    if (!ForgeGpuCreateDepthTextureWithFormat(demo, width, height, SDL_GPU_TEXTUREFORMAT_D32_FLOAT)) {
        return false;
    }

    {
        SDL_GPUColorTargetInfo color_target;
        SDL_GPUDepthStencilTargetInfo depth_target;
        SDL_GPURenderPass *render_pass;

        SDL_zero(color_target);
        color_target.texture = swapchain_texture;
        color_target.load_op = SDL_GPU_LOADOP_CLEAR;
        color_target.store_op = SDL_GPU_STOREOP_STORE;
        color_target.clear_color = { 0.0099f, 0.0099f, 0.0267f, 1.0f };

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

        draw_shadowed_grid(demo, command_buffer, render_pass, cam_vp, &shadow_mats, &light_dir, cascade_splits, state);

        SDL_BindGPUGraphicsPipeline(render_pass, demo->lesson.secondary_pipeline);
        draw_model_scene(
            demo,
            render_pass,
            command_buffer,
            &state->scene_models[0],
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
            draw_model_scene(
                demo,
                render_pass,
                command_buffer,
                &state->scene_models[1],
                placement,
                cam_vp,
                &shadow_mats,
                &light_dir,
                cascade_splits,
                state);
        }

        if (state->show_shadow_map) {
            SDL_GPUTextureSamplerBinding sampler_binding;
            DebugVertUniforms debug_uniforms = { { -1.0f, -1.0f, 1.0f, 1.0f } };

            SDL_BindGPUGraphicsPipeline(render_pass, demo->lesson.debug_pipeline);
            SDL_PushGPUVertexUniformData(command_buffer, 0, &debug_uniforms, sizeof(debug_uniforms));
            SDL_zero(sampler_binding);
            sampler_binding.texture = state->shadow_maps[0];
            sampler_binding.sampler = demo->lesson.samplers[1];
            SDL_BindGPUFragmentSamplers(render_pass, 0, &sampler_binding, 1);
            SDL_DrawGPUPrimitives(render_pass, 6, 1, 0, 0);
        }

        SDL_EndGPURenderPass(render_pass);
    }

    return true;
}

void ForgeGpuDebugLesson15(ForgeGpuDemo *demo)
{
    Lesson15State *state = lesson15_state(demo);

    if (state) {
        ImGui::Text("Shadow preview: %s", state->show_shadow_map ? "shown" : "hidden");
    }
}

void ForgeGpuControlsLesson15(ForgeGpuDemo *demo)
{
    Lesson15State *state = lesson15_state(demo);

    if (state) {
        ImGui::Checkbox("Show shadow map", &state->show_shadow_map);
    }
}

void ForgeGpuDestroyLesson15(ForgeGpuDemo *demo)
{
    Lesson15State *state = lesson15_state(demo);

    if (!state) {
        return;
    }
    for (int i = 0; i < 2; i += 1) {
        ForgeGpuFreeSceneData(demo, &state->scene_models[i]);
    }
    for (int i = 0; i < FORGE_GPU_SHADOW_CASCADE_COUNT; i += 1) {
        if (state->shadow_maps[i]) {
            SDL_ReleaseGPUTexture(demo->device, state->shadow_maps[i]);
        }
    }
    SDL_free(state);
    demo->lesson.private_state = nullptr;
}
