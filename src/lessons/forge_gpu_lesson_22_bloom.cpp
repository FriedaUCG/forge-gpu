#include "forge_gpu_lessons.h"

#include "forge_gpu_browser_status.h"
#include "forge_gpu_camera.h"
#include "forge_gpu_gpu_helpers.h"
#include "forge_gpu_lesson_common.h"
#include "forge_gpu_math.h"
#include "forge_gpu_scene.h"
#include "shaders/generated/forge_gpu_lesson_22_shaders.h"
#include "imgui.h"

#include <stddef.h>

#define LESSON22_MODEL_TRUCK 0
#define LESSON22_MODEL_BOX 1
#define LESSON22_MODEL_COUNT 2
#define LESSON22_BOX_GROUND_COUNT 8
#define LESSON22_BOX_STACK_COUNT 4
#define LESSON22_BOX_TOTAL_COUNT (LESSON22_BOX_GROUND_COUNT + LESSON22_BOX_STACK_COUNT)
#define LESSON22_HDR_FORMAT SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT
#define LESSON22_SPHERE_RADIUS 0.3f
#define LESSON22_SPHERE_STACKS 16
#define LESSON22_SPHERE_SLICES 32
#define LESSON22_LIGHT_ORBIT_RADIUS 4.0f
#define LESSON22_LIGHT_ORBIT_HEIGHT 2.0f
#define LESSON22_LIGHT_ORBIT_SPEED 0.6283f
#define LESSON22_LIGHT_INTENSITY 5.0f
#define LESSON22_EMISSION_R 50.0f
#define LESSON22_EMISSION_G 45.0f
#define LESSON22_EMISSION_B 40.0f
#define LESSON22_MAX_ANISOTROPY 4.0f
#define LESSON22_TONEMAP_NONE 0u
#define LESSON22_TONEMAP_REINHARD 1u
#define LESSON22_TONEMAP_ACES 2u
#define LESSON22_EXPOSURE_STEP 0.1f
#define LESSON22_EXPOSURE_MIN 0.1f
#define LESSON22_EXPOSURE_MAX 10.0f
#define LESSON22_BLOOM_INTENSITY_DEFAULT 0.04f
#define LESSON22_BLOOM_INTENSITY_STEP 0.005f
#define LESSON22_BLOOM_INTENSITY_MIN 0.0f
#define LESSON22_BLOOM_INTENSITY_MAX 0.5f
#define LESSON22_BLOOM_THRESHOLD_DEFAULT 1.0f
#define LESSON22_BLOOM_THRESHOLD_STEP 0.1f
#define LESSON22_BLOOM_THRESHOLD_MIN 0.0f
#define LESSON22_BLOOM_THRESHOLD_MAX 10.0f
#define LESSON22_FULLSCREEN_VERTICES 6

struct Lesson22SceneFragUniforms
{
    float base_color[4];
    float light_pos[3];
    float light_intensity;
    float eye_pos[3];
    float has_texture;
    float shininess;
    float ambient;
    float specular_str;
    float pad0;
};

struct Lesson22EmissiveFragUniforms
{
    float emission_color[3];
    float pad0;
};

struct Lesson22GridVertUniforms
{
    Mat4 vp;
};

struct Lesson22GridFragUniforms
{
    float line_color[4];
    float bg_color[4];
    float light_pos[3];
    float light_intensity;
    float eye_pos[3];
    float grid_spacing;
    float line_width;
    float fade_distance;
    float ambient;
    float shininess;
    float specular_str;
    float pad[3];
};

struct Lesson22TonemapFragUniforms
{
    float exposure;
    Uint32 tonemap_mode;
    float bloom_intensity;
    float pad0;
};

struct Lesson22BoxPlacement
{
    Vec3 position;
    float y_rotation;
};

struct Lesson22State
{
    GpuSceneData models[LESSON22_MODEL_COUNT];
    SDL_GPUGraphicsPipeline *scene_pipeline;
    SDL_GPUGraphicsPipeline *grid_pipeline;
    SDL_GPUGraphicsPipeline *emissive_pipeline;
    SDL_GPUGraphicsPipeline *downsample_pipeline;
    SDL_GPUGraphicsPipeline *upsample_pipeline;
    SDL_GPUGraphicsPipeline *tonemap_pipeline;
    SDL_GPUTexture *hdr_target;
    ForgeGpuBloomChain bloom;
    SDL_GPUBuffer *sphere_vertex_buffer;
    SDL_GPUBuffer *sphere_index_buffer;
    Uint32 sphere_index_count;
    Uint32 hdr_width;
    Uint32 hdr_height;
    Lesson22BoxPlacement box_placements[LESSON22_BOX_TOTAL_COUNT];
    int box_count;
    float exposure;
    float bloom_intensity;
    float bloom_threshold;
    Uint32 tonemap_mode;
    bool bloom_enabled;
};

static Lesson22State *lesson22_state(ForgeGpuDemo *demo)
{
    return (Lesson22State *)demo->lesson.private_state;
}

static const char *lesson22_tonemap_name(Uint32 mode)
{
    if (mode == LESSON22_TONEMAP_REINHARD) {
        return "Reinhard";
    }
    if (mode == LESSON22_TONEMAP_ACES) {
        return "ACES";
    }
    return "Clamp";
}

static float lesson22_clamp_float(float value, float min_value, float max_value)
{
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static Vec3 lesson22_light_position(float time_seconds)
{
    const float angle = LESSON22_LIGHT_ORBIT_SPEED * time_seconds + FORGE_GPU_PI / 3.0f;
    return {
        LESSON22_LIGHT_ORBIT_RADIUS * SDL_cosf(angle),
        LESSON22_LIGHT_ORBIT_HEIGHT,
        LESSON22_LIGHT_ORBIT_RADIUS * SDL_sinf(angle)
    };
}

static bool lesson22_create_hdr_target(ForgeGpuDemo *demo, Uint32 width, Uint32 height)
{
    Lesson22State *state = lesson22_state(demo);

    if (!state) {
        SDL_SetError("lesson 22 internal state is missing");
        return false;
    }
    return ForgeGpuEnsureSampledColorTarget(
        demo,
        &state->hdr_target,
        &state->hdr_width,
        &state->hdr_height,
        width,
        height,
        LESSON22_HDR_FORMAT);
}

static void lesson22_generate_box_placements(Lesson22State *state)
{
    int index = 0;

    for (int i = 0; i < LESSON22_BOX_GROUND_COUNT; i += 1) {
        const float angle = (float)i * (2.0f * FORGE_GPU_PI / (float)LESSON22_BOX_GROUND_COUNT);

        state->box_placements[index].position = {
            SDL_cosf(angle) * 5.0f,
            0.5f,
            SDL_sinf(angle) * 5.0f
        };
        state->box_placements[index].y_rotation = angle;
        index += 1;
    }

    for (int i = 0; i < LESSON22_BOX_STACK_COUNT; i += 1) {
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

static bool lesson22_create_sphere_geometry(ForgeGpuDemo *demo)
{
    Lesson22State *state = lesson22_state(demo);

    if (!state) {
        SDL_SetError("lesson 22 internal state is missing");
        return false;
    }

    return ForgeGpuCreateSphereMeshBuffers(
        demo,
        LESSON22_SPHERE_RADIUS,
        LESSON22_SPHERE_STACKS,
        LESSON22_SPHERE_SLICES,
        &state->sphere_vertex_buffer,
        &state->sphere_index_buffer,
        &state->sphere_index_count);
}

static bool lesson22_create_pipelines(ForgeGpuDemo *demo)
{
    Lesson22State *state = lesson22_state(demo);
    SDL_GPUShader *scene_vertex_shader = nullptr;
    SDL_GPUShader *scene_fragment_shader = nullptr;
    SDL_GPUShader *grid_vertex_shader = nullptr;
    SDL_GPUShader *grid_fragment_shader = nullptr;
    SDL_GPUShader *emissive_fragment_shader = nullptr;
    SDL_GPUShader *fullscreen_vertex_shader = nullptr;
    SDL_GPUShader *downsample_fragment_shader = nullptr;
    SDL_GPUShader *upsample_fragment_shader = nullptr;
    SDL_GPUShader *tonemap_fragment_shader = nullptr;
    SDL_GPUVertexBufferDescription mesh_vertex_buffer_desc;
    SDL_GPUVertexAttribute mesh_vertex_attributes[3];
    SDL_GPUVertexBufferDescription grid_vertex_buffer_desc;
    SDL_GPUVertexAttribute grid_vertex_attribute;
    bool ok = false;

    if (!state) {
        SDL_SetError("lesson 22 internal state is missing");
        return false;
    }

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

    scene_vertex_shader = ForgeGpuCreateShader(
        demo->device, SDL_GPU_SHADERSTAGE_VERTEX,
        lesson22_scene_vert_wgsl, lesson22_scene_vert_wgsl_size,
        lesson22_scene_vert_msl, lesson22_scene_vert_msl_size,
        0, 0, 0, 1);
    scene_fragment_shader = ForgeGpuCreateShader(
        demo->device, SDL_GPU_SHADERSTAGE_FRAGMENT,
        lesson22_scene_frag_wgsl, lesson22_scene_frag_wgsl_size,
        lesson22_scene_frag_msl, lesson22_scene_frag_msl_size,
        1, 0, 0, 1);
    grid_vertex_shader = ForgeGpuCreateShader(
        demo->device, SDL_GPU_SHADERSTAGE_VERTEX,
        lesson22_grid_vert_wgsl, lesson22_grid_vert_wgsl_size,
        lesson22_grid_vert_msl, lesson22_grid_vert_msl_size,
        0, 0, 0, 1);
    grid_fragment_shader = ForgeGpuCreateShader(
        demo->device, SDL_GPU_SHADERSTAGE_FRAGMENT,
        lesson22_grid_frag_wgsl, lesson22_grid_frag_wgsl_size,
        lesson22_grid_frag_msl, lesson22_grid_frag_msl_size,
        0, 0, 0, 1);
    emissive_fragment_shader = ForgeGpuCreateShader(
        demo->device, SDL_GPU_SHADERSTAGE_FRAGMENT,
        lesson22_emissive_frag_wgsl, lesson22_emissive_frag_wgsl_size,
        lesson22_emissive_frag_msl, lesson22_emissive_frag_msl_size,
        0, 0, 0, 1);
    fullscreen_vertex_shader = ForgeGpuCreateShader(
        demo->device, SDL_GPU_SHADERSTAGE_VERTEX,
        lesson22_fullscreen_vert_wgsl, lesson22_fullscreen_vert_wgsl_size,
        lesson22_fullscreen_vert_msl, lesson22_fullscreen_vert_msl_size,
        0, 0, 0, 0);
    downsample_fragment_shader = ForgeGpuCreateShader(
        demo->device, SDL_GPU_SHADERSTAGE_FRAGMENT,
        lesson22_bloom_downsample_frag_wgsl, lesson22_bloom_downsample_frag_wgsl_size,
        lesson22_bloom_downsample_frag_msl, lesson22_bloom_downsample_frag_msl_size,
        1, 0, 0, 1);
    upsample_fragment_shader = ForgeGpuCreateShader(
        demo->device, SDL_GPU_SHADERSTAGE_FRAGMENT,
        lesson22_bloom_upsample_frag_wgsl, lesson22_bloom_upsample_frag_wgsl_size,
        lesson22_bloom_upsample_frag_msl, lesson22_bloom_upsample_frag_msl_size,
        1, 0, 0, 1);
    tonemap_fragment_shader = ForgeGpuCreateShader(
        demo->device, SDL_GPU_SHADERSTAGE_FRAGMENT,
        lesson22_tonemap_frag_wgsl, lesson22_tonemap_frag_wgsl_size,
        lesson22_tonemap_frag_msl, lesson22_tonemap_frag_msl_size,
        2, 0, 0, 1);
    if (!scene_vertex_shader || !scene_fragment_shader ||
        !grid_vertex_shader || !grid_fragment_shader ||
        !emissive_fragment_shader || !fullscreen_vertex_shader ||
        !downsample_fragment_shader || !upsample_fragment_shader ||
        !tonemap_fragment_shader) {
        goto done;
    }

    state->scene_pipeline = ForgeGpuCreateLessonGraphicsPipelineWithColorFormat(
        demo,
        scene_vertex_shader,
        scene_fragment_shader,
        SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        LESSON22_HDR_FORMAT,
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
    state->grid_pipeline = ForgeGpuCreateLessonGraphicsPipelineWithColorFormat(
        demo,
        grid_vertex_shader,
        grid_fragment_shader,
        SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        LESSON22_HDR_FORMAT,
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
    state->emissive_pipeline = ForgeGpuCreateLessonGraphicsPipelineWithColorFormat(
        demo,
        scene_vertex_shader,
        emissive_fragment_shader,
        SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        LESSON22_HDR_FORMAT,
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
    state->downsample_pipeline = ForgeGpuCreateFullscreenPostprocessPipeline(
        demo, fullscreen_vertex_shader, downsample_fragment_shader, LESSON22_HDR_FORMAT, false);
    state->upsample_pipeline = ForgeGpuCreateFullscreenPostprocessPipeline(
        demo, fullscreen_vertex_shader, upsample_fragment_shader, LESSON22_HDR_FORMAT, true);
    state->tonemap_pipeline = ForgeGpuCreateFullscreenPostprocessPipeline(
        demo, fullscreen_vertex_shader, tonemap_fragment_shader, demo->color_format, false);

    ok = state->scene_pipeline &&
         state->grid_pipeline &&
         state->emissive_pipeline &&
         state->downsample_pipeline &&
         state->upsample_pipeline &&
         state->tonemap_pipeline;

done:
    if (tonemap_fragment_shader) {
        SDL_ReleaseGPUShader(demo->device, tonemap_fragment_shader);
    }
    if (upsample_fragment_shader) {
        SDL_ReleaseGPUShader(demo->device, upsample_fragment_shader);
    }
    if (downsample_fragment_shader) {
        SDL_ReleaseGPUShader(demo->device, downsample_fragment_shader);
    }
    if (fullscreen_vertex_shader) {
        SDL_ReleaseGPUShader(demo->device, fullscreen_vertex_shader);
    }
    if (emissive_fragment_shader) {
        SDL_ReleaseGPUShader(demo->device, emissive_fragment_shader);
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
    return ok;
}

static void lesson22_draw_model_scene(
    ForgeGpuDemo *demo,
    SDL_GPURenderPass *render_pass,
    SDL_GPUCommandBuffer *command_buffer,
    const GpuSceneData *model,
    Mat4 placement,
    Mat4 cam_vp,
    Vec3 light_pos)
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

        mesh = &model->loaded.meshes[node->mesh_index];
        for (int primitive_offset = 0; primitive_offset < mesh->primitive_count; primitive_offset += 1) {
            const int primitive_index = mesh->first_primitive + primitive_offset;
            GpuMaterial fallback_material;
            const GpuPrimitive *primitive;
            const GpuMaterial *material;
            SDL_GPUTextureSamplerBinding sampler_binding;
            SDL_GPUBufferBinding vertex_binding;
            Lesson22SceneFragUniforms fragment_uniforms;

            if (primitive_index < 0 || primitive_index >= model->primitive_count) {
                continue;
            }
            primitive = &model->primitives[primitive_index];
            material = ForgeGpuModelMaterialOrDefault(model, primitive->material_index, &fallback_material);

            SDL_zero(fragment_uniforms);
            SDL_memcpy(fragment_uniforms.base_color, material->base_color, sizeof(fragment_uniforms.base_color));
            fragment_uniforms.light_pos[0] = light_pos.x;
            fragment_uniforms.light_pos[1] = light_pos.y;
            fragment_uniforms.light_pos[2] = light_pos.z;
            fragment_uniforms.light_intensity = LESSON22_LIGHT_INTENSITY;
            fragment_uniforms.eye_pos[0] = demo->lesson.camera_position.x;
            fragment_uniforms.eye_pos[1] = demo->lesson.camera_position.y;
            fragment_uniforms.eye_pos[2] = demo->lesson.camera_position.z;
            fragment_uniforms.has_texture = material->has_texture ? 1.0f : 0.0f;
            fragment_uniforms.shininess = 64.0f;
            fragment_uniforms.ambient = 0.1f;
            fragment_uniforms.specular_str = 1.0f;
            SDL_PushGPUFragmentUniformData(command_buffer, 0, &fragment_uniforms, sizeof(fragment_uniforms));

            SDL_zero(sampler_binding);
            sampler_binding.texture = material->has_texture ? material->texture : demo->lesson.white_texture;
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

static void lesson22_draw_grid(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    Mat4 cam_vp,
    Vec3 light_pos)
{
    SDL_GPUBufferBinding vertex_binding;
    SDL_GPUBufferBinding index_binding;
    Lesson22GridVertUniforms vertex_uniforms;
    Lesson22GridFragUniforms fragment_uniforms;

    SDL_BindGPUGraphicsPipeline(render_pass, lesson22_state(demo)->grid_pipeline);
    vertex_uniforms.vp = cam_vp;
    SDL_PushGPUVertexUniformData(command_buffer, 0, &vertex_uniforms, sizeof(vertex_uniforms));

    SDL_zero(fragment_uniforms);
    fragment_uniforms.line_color[0] = 0.15f;
    fragment_uniforms.line_color[1] = 0.55f;
    fragment_uniforms.line_color[2] = 0.85f;
    fragment_uniforms.line_color[3] = 1.0f;
    fragment_uniforms.bg_color[0] = 0.04f;
    fragment_uniforms.bg_color[1] = 0.04f;
    fragment_uniforms.bg_color[2] = 0.08f;
    fragment_uniforms.bg_color[3] = 1.0f;
    fragment_uniforms.light_pos[0] = light_pos.x;
    fragment_uniforms.light_pos[1] = light_pos.y;
    fragment_uniforms.light_pos[2] = light_pos.z;
    fragment_uniforms.light_intensity = LESSON22_LIGHT_INTENSITY;
    fragment_uniforms.eye_pos[0] = demo->lesson.camera_position.x;
    fragment_uniforms.eye_pos[1] = demo->lesson.camera_position.y;
    fragment_uniforms.eye_pos[2] = demo->lesson.camera_position.z;
    fragment_uniforms.grid_spacing = 1.0f;
    fragment_uniforms.line_width = 0.02f;
    fragment_uniforms.fade_distance = 40.0f;
    fragment_uniforms.ambient = 0.15f;
    fragment_uniforms.shininess = 32.0f;
    fragment_uniforms.specular_str = 0.5f;
    SDL_PushGPUFragmentUniformData(command_buffer, 0, &fragment_uniforms, sizeof(fragment_uniforms));

    SDL_zero(vertex_binding);
    vertex_binding.buffer = demo->lesson.vertex_buffer;
    SDL_BindGPUVertexBuffers(render_pass, 0, &vertex_binding, 1);
    SDL_zero(index_binding);
    index_binding.buffer = demo->lesson.index_buffer;
    SDL_BindGPUIndexBuffer(render_pass, &index_binding, SDL_GPU_INDEXELEMENTSIZE_16BIT);
    SDL_DrawGPUIndexedPrimitives(render_pass, SDL_arraysize(kForgeGpuGridIndices), 1, 0, 0, 0);
}

static void lesson22_draw_emissive_sphere(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    Mat4 cam_vp,
    Vec3 light_pos)
{
    Lesson22State *state = lesson22_state(demo);
    SDL_GPUBufferBinding vertex_binding;
    SDL_GPUBufferBinding index_binding;
    UniformMvpModel vertex_uniforms;
    Lesson22EmissiveFragUniforms fragment_uniforms;
    const Mat4 model = mat4_translate(light_pos);

    SDL_BindGPUGraphicsPipeline(render_pass, state->emissive_pipeline);
    vertex_uniforms.model = model;
    vertex_uniforms.mvp = mat4_multiply(cam_vp, model);
    SDL_PushGPUVertexUniformData(command_buffer, 0, &vertex_uniforms, sizeof(vertex_uniforms));

    SDL_zero(fragment_uniforms);
    fragment_uniforms.emission_color[0] = LESSON22_EMISSION_R;
    fragment_uniforms.emission_color[1] = LESSON22_EMISSION_G;
    fragment_uniforms.emission_color[2] = LESSON22_EMISSION_B;
    SDL_PushGPUFragmentUniformData(command_buffer, 0, &fragment_uniforms, sizeof(fragment_uniforms));

    SDL_zero(vertex_binding);
    vertex_binding.buffer = state->sphere_vertex_buffer;
    SDL_BindGPUVertexBuffers(render_pass, 0, &vertex_binding, 1);
    SDL_zero(index_binding);
    index_binding.buffer = state->sphere_index_buffer;
    SDL_BindGPUIndexBuffer(render_pass, &index_binding, SDL_GPU_INDEXELEMENTSIZE_16BIT);
    SDL_DrawGPUIndexedPrimitives(render_pass, state->sphere_index_count, 1, 0, 0, 0);
}

static bool lesson22_render_hdr_scene(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    Mat4 cam_vp,
    Vec3 light_pos)
{
    Lesson22State *state = lesson22_state(demo);
    SDL_GPUColorTargetInfo color_target;
    SDL_GPUDepthStencilTargetInfo depth_target;
    SDL_GPURenderPass *render_pass;
    const Mat4 truck_placement = mat4_identity();

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

    lesson22_draw_grid(demo, command_buffer, render_pass, cam_vp, light_pos);

    SDL_BindGPUGraphicsPipeline(render_pass, state->scene_pipeline);
    lesson22_draw_model_scene(
        demo,
        render_pass,
        command_buffer,
        &state->models[LESSON22_MODEL_TRUCK],
        truck_placement,
        cam_vp,
        light_pos);
    for (int i = 0; i < state->box_count; i += 1) {
        const Mat4 t = mat4_translate(state->box_placements[i].position);
        const Mat4 r = mat4_rotate_y(state->box_placements[i].y_rotation);
        const Mat4 placement = mat4_multiply(t, r);

        lesson22_draw_model_scene(
            demo,
            render_pass,
            command_buffer,
            &state->models[LESSON22_MODEL_BOX],
            placement,
            cam_vp,
            light_pos);
    }

    lesson22_draw_emissive_sphere(demo, command_buffer, render_pass, cam_vp, light_pos);

    SDL_EndGPURenderPass(render_pass);
    return true;
}

static bool lesson22_render_bloom(ForgeGpuDemo *demo, SDL_GPUCommandBuffer *command_buffer)
{
    Lesson22State *state = lesson22_state(demo);

    if (!state->bloom_enabled) {
        return true;
    }

    return ForgeGpuRunBloomChain(
        command_buffer,
        &state->bloom,
        state->hdr_target,
        state->hdr_width,
        state->hdr_height,
        state->downsample_pipeline,
        state->upsample_pipeline,
        demo->lesson.samplers[2],
        state->bloom_threshold,
        LESSON22_FULLSCREEN_VERTICES);
}

static bool lesson22_render_tonemap(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPUTexture *swapchain_texture)
{
    Lesson22State *state = lesson22_state(demo);
    Lesson22TonemapFragUniforms uniforms;

    SDL_zero(uniforms);
    uniforms.exposure = state->exposure;
    uniforms.tonemap_mode = state->tonemap_mode;
    uniforms.bloom_intensity = state->bloom_enabled ? state->bloom_intensity : 0.0f;
    return ForgeGpuRunHdrBloomTonemapPass(
        command_buffer,
        swapchain_texture,
        state->hdr_target,
        demo->lesson.samplers[1],
        &state->bloom,
        demo->lesson.samplers[2],
        state->tonemap_pipeline,
        &uniforms,
        sizeof(uniforms),
        LESSON22_FULLSCREEN_VERTICES);
}

bool ForgeGpuCreateLesson22(ForgeGpuDemo *demo)
{
    LessonState *lesson = &demo->lesson;
    Lesson22State *state;

    if (!SDL_GPUTextureSupportsFormat(
            demo->device,
            LESSON22_HDR_FORMAT,
            SDL_GPU_TEXTURETYPE_2D,
            SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER)) {
        SDL_SetError("lesson 22 requires sampled R16G16B16A16_FLOAT color targets");
        return false;
    }
    if (!SDL_GPUTextureSupportsFormat(
            demo->device,
            SDL_GPU_TEXTUREFORMAT_D32_FLOAT,
            SDL_GPU_TEXTURETYPE_2D,
            SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET)) {
        SDL_SetError("lesson 22 requires D32_FLOAT depth targets");
        return false;
    }

    state = (Lesson22State *)SDL_calloc(1, sizeof(*state));
    if (!state) {
        SDL_OutOfMemory();
        return false;
    }
    lesson->private_state = state;

    lesson->white_texture = ForgeGpuCreateWhiteTexture(demo->device);
    lesson->samplers[0] = ForgeGpuCreateSamplerWithAddressAndAnisotropy(
        demo->device,
        SDL_GPU_FILTER_LINEAR,
        SDL_GPU_FILTER_LINEAR,
        SDL_GPU_SAMPLERMIPMAPMODE_LINEAR,
        SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
        1000.0f,
        LESSON22_MAX_ANISOTROPY);
    lesson->samplers[1] = ForgeGpuCreateSamplerWithAddress(
        demo->device,
        SDL_GPU_FILTER_NEAREST,
        SDL_GPU_FILTER_NEAREST,
        SDL_GPU_SAMPLERMIPMAPMODE_NEAREST,
        SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        0.0f);
    lesson->samplers[2] = ForgeGpuCreateSamplerWithAddress(
        demo->device,
        SDL_GPU_FILTER_LINEAR,
        SDL_GPU_FILTER_LINEAR,
        SDL_GPU_SAMPLERMIPMAPMODE_NEAREST,
        SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        0.0f);
    if (!lesson->white_texture || !lesson->samplers[0] || !lesson->samplers[1] || !lesson->samplers[2]) {
        return false;
    }

    if (!ForgeGpuCreateGridBuffers(demo) ||
        !lesson22_create_sphere_geometry(demo) ||
        !ForgeGpuLoadSceneModel(demo, &state->models[LESSON22_MODEL_TRUCK], "models/CesiumMilkTruck/CesiumMilkTruck.gltf") ||
        !ForgeGpuLoadSceneModel(demo, &state->models[LESSON22_MODEL_BOX], "models/BoxTextured/BoxTextured.gltf")) {
        return false;
    }

    lesson22_generate_box_placements(state);
    lesson->camera_position = { -6.1f, 7.0f, 4.4f };
    lesson->camera_yaw = -50.0f * FORGE_GPU_DEG2RAD;
    lesson->camera_pitch = -50.0f * FORGE_GPU_DEG2RAD;
    lesson->move_speed = 5.0f;
    lesson->last_ticks = SDL_GetTicks();
    state->exposure = 1.0f;
    state->tonemap_mode = LESSON22_TONEMAP_ACES;
    state->bloom_enabled = true;
    state->bloom_intensity = LESSON22_BLOOM_INTENSITY_DEFAULT;
    state->bloom_threshold = LESSON22_BLOOM_THRESHOLD_DEFAULT;

    return lesson22_create_pipelines(demo);
}

bool ForgeGpuRenderLesson22(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPUTexture *swapchain_texture,
    Uint32 width,
    Uint32 height)
{
    Lesson22State *state = lesson22_state(demo);
    Mat4 view;
    Mat4 projection;
    Mat4 cam_vp;
    Vec3 light_pos;

    if (!state) {
        SDL_SetError("lesson 22 internal state is missing");
        return false;
    }

    ForgeGpuUpdateCameraFromInput(demo);
    ForgeGpuCameraViewProjection(demo, width, height, 100.0f, &view, &projection);
    cam_vp = mat4_multiply(projection, view);
    light_pos = lesson22_light_position(ForgeGpuFrameTimeSeconds(demo));

    if (!ForgeGpuCreateDepthTextureWithFormat(demo, width, height, SDL_GPU_TEXTUREFORMAT_D32_FLOAT) ||
        !lesson22_create_hdr_target(demo, width, height) ||
        !ForgeGpuEnsureBloomChain(
            demo,
            &state->bloom,
            state->hdr_target,
            state->hdr_width,
            state->hdr_height,
            LESSON22_HDR_FORMAT)) {
        return false;
    }

    if (!lesson22_render_hdr_scene(demo, command_buffer, cam_vp, light_pos) ||
        !lesson22_render_bloom(demo, command_buffer) ||
        !lesson22_render_tonemap(demo, command_buffer, swapchain_texture)) {
        return false;
    }
    return true;
}

void ForgeGpuDebugLesson22(ForgeGpuDemo *demo)
{
    Lesson22State *state = lesson22_state(demo);

    if (!state) {
        return;
    }
    ImGui::Text("Tone map: %s", lesson22_tonemap_name(state->tonemap_mode));
    ImGui::Text("Exposure: %.1f", (double)state->exposure);
    ImGui::Text("Bloom: %s", state->bloom_enabled ? "on" : "off");
    ImGui::Text("Bloom intensity: %.3f", (double)state->bloom_intensity);
    ImGui::Text("Bloom threshold: %.1f", (double)state->bloom_threshold);
    if (state->hdr_target) {
        ImGui::Text("HDR target: %ux%u R16G16B16A16_FLOAT", state->hdr_width, state->hdr_height);
    }
    if (state->bloom.mips[0]) {
        ImGui::Text("Bloom mips: %ux%u -> %ux%u",
            state->bloom.widths[0],
            state->bloom.heights[0],
            state->bloom.widths[FORGE_GPU_BLOOM_MIP_COUNT - 1],
            state->bloom.heights[FORGE_GPU_BLOOM_MIP_COUNT - 1]);
    }
}

void ForgeGpuControlsLesson22(ForgeGpuDemo *demo)
{
    (void)demo;
    ImGui::Text("1: Clamp");
    ImGui::Text("2: Reinhard");
    ImGui::Text("3: ACES");
    ImGui::Text("+/-: Exposure up/down");
    ImGui::Text("B: Toggle bloom");
    ImGui::Text("Up/Down: Bloom intensity");
    ImGui::Text("Left/Right: Bloom threshold");
}

static void lesson22_export_metrics(Lesson22State *state)
{
    if (!state) {
        return;
    }
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson22ExposureTenths", (double)((int)(state->exposure * 10.0f + 0.5f)));
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson22TonemapMode", (double)state->tonemap_mode);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson22BloomEnabled", state->bloom_enabled ? 1.0 : 0.0);
}

bool ForgeGpuHandleLesson22Event(ForgeGpuDemo *demo, const SDL_Event *event)
{
    Lesson22State *state = lesson22_state(demo);

    if (!state || event->type != SDL_EVENT_KEY_DOWN) {
        return false;
    }
    if (!event->key.repeat) {
        if (event->key.key == SDLK_1) {
            state->tonemap_mode = LESSON22_TONEMAP_NONE;
            return true;
        }
        if (event->key.key == SDLK_2) {
            state->tonemap_mode = LESSON22_TONEMAP_REINHARD;
            return true;
        }
        if (event->key.key == SDLK_3) {
            state->tonemap_mode = LESSON22_TONEMAP_ACES;
            return true;
        }
    }
    if (ForgeGpuEventIsPlusKey(event)) {
        state->exposure = lesson22_clamp_float(
            state->exposure + LESSON22_EXPOSURE_STEP,
            LESSON22_EXPOSURE_MIN,
            LESSON22_EXPOSURE_MAX);
        return true;
    }
    if (ForgeGpuEventIsMinusKey(event)) {
        state->exposure = lesson22_clamp_float(
            state->exposure - LESSON22_EXPOSURE_STEP,
            LESSON22_EXPOSURE_MIN,
            LESSON22_EXPOSURE_MAX);
        return true;
    }
    if (!event->key.repeat && event->key.key == SDLK_B) {
        state->bloom_enabled = !state->bloom_enabled;
        return true;
    }
    if (event->key.key == SDLK_UP) {
        state->bloom_intensity = lesson22_clamp_float(
            state->bloom_intensity + LESSON22_BLOOM_INTENSITY_STEP,
            LESSON22_BLOOM_INTENSITY_MIN,
            LESSON22_BLOOM_INTENSITY_MAX);
        return true;
    }
    if (event->key.key == SDLK_DOWN) {
        state->bloom_intensity = lesson22_clamp_float(
            state->bloom_intensity - LESSON22_BLOOM_INTENSITY_STEP,
            LESSON22_BLOOM_INTENSITY_MIN,
            LESSON22_BLOOM_INTENSITY_MAX);
        return true;
    }
    if (event->key.key == SDLK_RIGHT) {
        state->bloom_threshold = lesson22_clamp_float(
            state->bloom_threshold + LESSON22_BLOOM_THRESHOLD_STEP,
            LESSON22_BLOOM_THRESHOLD_MIN,
            LESSON22_BLOOM_THRESHOLD_MAX);
        return true;
    }
    if (event->key.key == SDLK_LEFT) {
        state->bloom_threshold = lesson22_clamp_float(
            state->bloom_threshold - LESSON22_BLOOM_THRESHOLD_STEP,
            LESSON22_BLOOM_THRESHOLD_MIN,
            LESSON22_BLOOM_THRESHOLD_MAX);
        return true;
    }
    return false;
}

void ForgeGpuExportLesson22Metrics(ForgeGpuDemo *demo)
{
    lesson22_export_metrics(lesson22_state(demo));
}

void ForgeGpuDestroyLesson22(ForgeGpuDemo *demo)
{
    Lesson22State *state = lesson22_state(demo);

    if (!state) {
        return;
    }
    for (int i = 0; i < LESSON22_MODEL_COUNT; i += 1) {
        ForgeGpuFreeSceneData(demo, &state->models[i]);
    }
    if (state->sphere_vertex_buffer) {
        SDL_ReleaseGPUBuffer(demo->device, state->sphere_vertex_buffer);
    }
    if (state->sphere_index_buffer) {
        SDL_ReleaseGPUBuffer(demo->device, state->sphere_index_buffer);
    }
    if (state->hdr_target) {
        SDL_ReleaseGPUTexture(demo->device, state->hdr_target);
    }
    ForgeGpuReleaseBloomChain(demo, &state->bloom);
    if (state->tonemap_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, state->tonemap_pipeline);
    }
    if (state->upsample_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, state->upsample_pipeline);
    }
    if (state->downsample_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, state->downsample_pipeline);
    }
    if (state->emissive_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, state->emissive_pipeline);
    }
    if (state->grid_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, state->grid_pipeline);
    }
    if (state->scene_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, state->scene_pipeline);
    }
    SDL_free(state);
    demo->lesson.private_state = nullptr;
}
