#include "forge_gpu_lessons.h"

#include "forge_gpu_browser_status.h"
#include "forge_gpu_camera.h"
#include "forge_gpu_gpu_helpers.h"
#include "forge_gpu_lesson_common.h"
#include "forge_gpu_math.h"
#include "forge_gpu_scene.h"
#include "forge_gpu_shader_layouts.h"
#include "shaders/generated/forge_gpu_lesson_23_shaders.h"
#include "imgui.h"

#include <stddef.h>

#define LESSON23_MODEL_TRUCK 0
#define LESSON23_MODEL_BOX 1
#define LESSON23_MODEL_COUNT 2
#define LESSON23_MAX_POINT_LIGHTS 4
#define LESSON23_BOX_GROUND_COUNT 8
#define LESSON23_BOX_STACK_COUNT 4
#define LESSON23_BOX_TOTAL_COUNT (LESSON23_BOX_GROUND_COUNT + LESSON23_BOX_STACK_COUNT)
#define LESSON23_HDR_FORMAT SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT
#define LESSON23_SPHERE_RADIUS 0.2f
#define LESSON23_SPHERE_STACKS 12
#define LESSON23_SPHERE_SLICES 24
#define LESSON23_EMISSION_SCALE 30.0f
#define LESSON23_MAX_ANISOTROPY 4.0f
#define LESSON23_EXPOSURE_STEP 0.1f
#define LESSON23_EXPOSURE_MIN 0.1f
#define LESSON23_EXPOSURE_MAX 10.0f
#define LESSON23_BLOOM_INTENSITY_DEFAULT 0.04f
#define LESSON23_BLOOM_INTENSITY_STEP 0.005f
#define LESSON23_BLOOM_INTENSITY_MIN 0.0f
#define LESSON23_BLOOM_INTENSITY_MAX 0.5f
#define LESSON23_BLOOM_THRESHOLD_DEFAULT 1.0f
#define LESSON23_BLOOM_THRESHOLD_STEP 0.1f
#define LESSON23_BLOOM_THRESHOLD_MIN 0.0f
#define LESSON23_BLOOM_THRESHOLD_MAX 10.0f
#define LESSON23_FULLSCREEN_VERTICES 3
#define LESSON23_SHADOW_MAP_SIZE 512u
#define LESSON23_SHADOW_MAP_FORMAT SDL_GPU_TEXTUREFORMAT_R32_FLOAT
#define LESSON23_SHADOW_DEPTH_FORMAT SDL_GPU_TEXTUREFORMAT_D32_FLOAT
#define LESSON23_SHADOW_NEAR_PLANE 0.1f
#define LESSON23_SHADOW_FAR_PLANE 25.0f
#define LESSON23_CUBE_FACE_COUNT 6
#define LESSON23_GRID_SPACING 1.0f
#define LESSON23_GRID_LINE_WIDTH 0.02f
#define LESSON23_GRID_FADE_DISTANCE 40.0f
#define LESSON23_GRID_AMBIENT 0.02f
#define LESSON23_GRID_SHININESS 32.0f
#define LESSON23_GRID_SPECULAR_STRENGTH 0.5f
#define LESSON23_MATERIAL_SHININESS 64.0f
#define LESSON23_MATERIAL_AMBIENT 0.02f
#define LESSON23_MATERIAL_SPECULAR_STRENGTH 1.0f

struct Lesson23PointLight
{
    float position[3];
    float intensity;
    float color[3];
    float pad0;
};

struct Lesson23SceneFragUniforms
{
    float base_color[4];
    float eye_pos[3];
    float has_texture;
    float shininess;
    float ambient;
    float specular_str;
    float shadow_far_plane;
    Lesson23PointLight lights[LESSON23_MAX_POINT_LIGHTS];
};

struct Lesson23EmissiveFragUniforms
{
    float emission_color[3];
    float pad0;
};

struct Lesson23GridVertUniforms
{
    Mat4 vp;
};

struct Lesson23GridFragUniforms
{
    float line_color[4];
    float bg_color[4];
    float eye_pos[3];
    float grid_spacing;
    float line_width;
    float fade_distance;
    float ambient;
    float shininess;
    float specular_str;
    float shadow_far_plane;
    float pad1;
    float pad2;
    Lesson23PointLight lights[LESSON23_MAX_POINT_LIGHTS];
};

struct Lesson23TonemapFragUniforms
{
    float exposure;
    float bloom_intensity;
    float pad0;
    float pad1;
};

struct Lesson23ShadowVertUniforms
{
    Mat4 light_mvp;
    Mat4 model;
};

struct Lesson23ShadowFragUniforms
{
    float light_pos[3];
    float far_plane;
};

struct Lesson23BoxPlacement
{
    Vec3 position;
    float y_rotation;
};

struct Lesson23State
{
    GpuSceneData models[LESSON23_MODEL_COUNT];
    SDL_GPUGraphicsPipeline *scene_pipeline;
    SDL_GPUGraphicsPipeline *grid_pipeline;
    SDL_GPUGraphicsPipeline *emissive_pipeline;
    SDL_GPUGraphicsPipeline *shadow_pipeline;
    SDL_GPUGraphicsPipeline *downsample_pipeline;
    SDL_GPUGraphicsPipeline *upsample_pipeline;
    SDL_GPUGraphicsPipeline *tonemap_pipeline;
    SDL_GPUTexture *hdr_target;
    ForgeGpuBloomChain bloom;
    SDL_GPUTexture *shadow_cubes[LESSON23_MAX_POINT_LIGHTS];
    SDL_GPUTexture *shadow_depth;
    SDL_GPUBuffer *sphere_vertex_buffer;
    SDL_GPUBuffer *sphere_index_buffer;
    Uint32 sphere_index_count;
    Uint32 hdr_width;
    Uint32 hdr_height;
    Lesson23BoxPlacement box_placements[LESSON23_BOX_TOTAL_COUNT];
    int box_count;
    float exposure;
    float bloom_intensity;
    float bloom_threshold;
    bool bloom_enabled;
    bool light_enabled[LESSON23_MAX_POINT_LIGHTS];
};

static Lesson23State *lesson23_state(ForgeGpuDemo *demo)
{
    return (Lesson23State *)demo->lesson.private_state;
}

static float lesson23_clamp_float(float value, float min_value, float max_value)
{
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static void lesson23_fill_lights(
    const Lesson23State *state,
    float time_seconds,
    Lesson23PointLight lights[LESSON23_MAX_POINT_LIGHTS])
{
    const float orbit_angle = FORGE_GPU_PI / 3.0f + 0.5f * time_seconds;

    SDL_memset(lights, 0, sizeof(*lights) * LESSON23_MAX_POINT_LIGHTS);

    lights[0].position[0] = 4.0f * SDL_cosf(orbit_angle);
    lights[0].position[1] = 3.5f;
    lights[0].position[2] = 4.0f * SDL_sinf(orbit_angle);
    lights[0].intensity = state->light_enabled[0] ? 8.0f : 0.0f;
    lights[0].color[0] = 0.08f;
    lights[0].color[1] = 0.55f;
    lights[0].color[2] = 0.93f;

    lights[1].position[0] = 6.0f;
    lights[1].position[1] = 2.5f;
    lights[1].position[2] = -3.0f;
    lights[1].intensity = state->light_enabled[1] ? 6.0f : 0.0f;
    lights[1].color[0] = 1.00f;
    lights[1].color[1] = 0.16f;
    lights[1].color[2] = 0.05f;

    lights[2].position[0] = -5.0f;
    lights[2].position[1] = 4.0f;
    lights[2].position[2] = -2.0f;
    lights[2].intensity = state->light_enabled[2] ? 5.0f : 0.0f;
    lights[2].color[0] = 0.13f;
    lights[2].color[1] = 0.51f;
    lights[2].color[2] = 0.14f;

    lights[3].position[0] = 2.0f;
    lights[3].position[1] = 3.0f;
    lights[3].position[2] = 5.0f;
    lights[3].intensity = state->light_enabled[3] ? 6.0f : 0.0f;
    lights[3].color[0] = 0.42f;
    lights[3].color[1] = 0.06f;
    lights[3].color[2] = 0.51f;
}

static bool lesson23_create_hdr_target(ForgeGpuDemo *demo, Uint32 width, Uint32 height)
{
    Lesson23State *state = lesson23_state(demo);

    if (!state) {
        SDL_SetError("lesson 23 internal state is missing");
        return false;
    }
    return ForgeGpuEnsureSampledColorTarget(
        demo,
        &state->hdr_target,
        &state->hdr_width,
        &state->hdr_height,
        width,
        height,
        LESSON23_HDR_FORMAT);
}

static void lesson23_generate_box_placements(Lesson23State *state)
{
    int index = 0;

    for (int i = 0; i < LESSON23_BOX_GROUND_COUNT; i += 1) {
        const float angle = (float)i * (2.0f * FORGE_GPU_PI / (float)LESSON23_BOX_GROUND_COUNT);

        state->box_placements[index].position = {
            SDL_cosf(angle) * 5.0f,
            0.5f,
            SDL_sinf(angle) * 5.0f
        };
        state->box_placements[index].y_rotation = angle;
        index += 1;
    }

    for (int i = 0; i < LESSON23_BOX_STACK_COUNT; i += 1) {
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

static bool lesson23_create_sphere_geometry(ForgeGpuDemo *demo)
{
    Lesson23State *state = lesson23_state(demo);

    if (!state) {
        SDL_SetError("lesson 23 internal state is missing");
        return false;
    }

    return ForgeGpuCreateSphereMeshBuffers(
        demo,
        LESSON23_SPHERE_RADIUS,
        LESSON23_SPHERE_STACKS,
        LESSON23_SPHERE_SLICES,
        &state->sphere_vertex_buffer,
        &state->sphere_index_buffer,
        &state->sphere_index_count);
}

static SDL_GPUTexture *lesson23_create_shadow_cube(SDL_GPUDevice *device)
{
    SDL_GPUTextureCreateInfo texture_info;

    SDL_zero(texture_info);
    texture_info.type = SDL_GPU_TEXTURETYPE_CUBE;
    texture_info.format = LESSON23_SHADOW_MAP_FORMAT;
    texture_info.width = LESSON23_SHADOW_MAP_SIZE;
    texture_info.height = LESSON23_SHADOW_MAP_SIZE;
    texture_info.layer_count_or_depth = LESSON23_CUBE_FACE_COUNT;
    texture_info.num_levels = 1;
    texture_info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    return SDL_CreateGPUTexture(device, &texture_info);
}

static bool lesson23_create_shadow_resources(ForgeGpuDemo *demo)
{
    Lesson23State *state = lesson23_state(demo);
    SDL_GPUTextureCreateInfo depth_info;

    if (!state) {
        SDL_SetError("lesson 23 internal state is missing");
        return false;
    }

    for (int i = 0; i < LESSON23_MAX_POINT_LIGHTS; i += 1) {
        state->shadow_cubes[i] = lesson23_create_shadow_cube(demo->device);
        if (!state->shadow_cubes[i]) {
            return false;
        }
    }

    SDL_zero(depth_info);
    depth_info.type = SDL_GPU_TEXTURETYPE_2D;
    depth_info.format = LESSON23_SHADOW_DEPTH_FORMAT;
    depth_info.width = LESSON23_SHADOW_MAP_SIZE;
    depth_info.height = LESSON23_SHADOW_MAP_SIZE;
    depth_info.layer_count_or_depth = 1;
    depth_info.num_levels = 1;
    depth_info.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
    state->shadow_depth = SDL_CreateGPUTexture(demo->device, &depth_info);
    return state->shadow_depth != nullptr;
}

static void lesson23_build_cube_face_view_projections(Vec3 light_pos, Mat4 face_vps[LESSON23_CUBE_FACE_COUNT])
{
    const Vec3 look_dirs[LESSON23_CUBE_FACE_COUNT] = {
        {  1.0f,  0.0f,  0.0f },
        { -1.0f,  0.0f,  0.0f },
        {  0.0f,  1.0f,  0.0f },
        {  0.0f, -1.0f,  0.0f },
        {  0.0f,  0.0f,  1.0f },
        {  0.0f,  0.0f, -1.0f }
    };
    const Vec3 up_dirs[LESSON23_CUBE_FACE_COUNT] = {
        { 0.0f, -1.0f,  0.0f },
        { 0.0f, -1.0f,  0.0f },
        { 0.0f,  0.0f,  1.0f },
        { 0.0f,  0.0f, -1.0f },
        { 0.0f, -1.0f,  0.0f },
        { 0.0f, -1.0f,  0.0f }
    };
    Mat4 shadow_projection = mat4_perspective(
        FORGE_GPU_PI / 2.0f,
        1.0f,
        LESSON23_SHADOW_NEAR_PLANE,
        LESSON23_SHADOW_FAR_PLANE);

    shadow_projection.m[5] = -shadow_projection.m[5];
    for (int face = 0; face < LESSON23_CUBE_FACE_COUNT; face += 1) {
        const Vec3 target = vec3_add(light_pos, look_dirs[face]);
        const Mat4 view = mat4_look_at(light_pos, target, up_dirs[face]);
        face_vps[face] = mat4_multiply(shadow_projection, view);
    }
}

static bool lesson23_create_pipelines(ForgeGpuDemo *demo)
{
    Lesson23State *state = lesson23_state(demo);
    SDL_GPUShader *scene_vertex_shader = nullptr;
    SDL_GPUShader *scene_fragment_shader = nullptr;
    SDL_GPUShader *grid_vertex_shader = nullptr;
    SDL_GPUShader *grid_fragment_shader = nullptr;
    SDL_GPUShader *shadow_vertex_shader = nullptr;
    SDL_GPUShader *shadow_fragment_shader = nullptr;
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
        SDL_SetError("lesson 23 internal state is missing");
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
        lesson23_scene_vert_wgsl, lesson23_scene_vert_wgsl_size,
        lesson23_scene_vert_msl, lesson23_scene_vert_msl_size,
        0, 0, 0, 1);
    scene_fragment_shader = ForgeGpuCreateShaderWithResourceLayout(
        demo->device,
        lesson23_scene_frag_wgsl, lesson23_scene_frag_wgsl_size,
        lesson23_scene_frag_msl, lesson23_scene_frag_msl_size,
        ForgeGpuShaderLayout_lesson23_scene_frag());
    grid_vertex_shader = ForgeGpuCreateShader(
        demo->device, SDL_GPU_SHADERSTAGE_VERTEX,
        lesson23_grid_vert_wgsl, lesson23_grid_vert_wgsl_size,
        lesson23_grid_vert_msl, lesson23_grid_vert_msl_size,
        0, 0, 0, 1);
    grid_fragment_shader = ForgeGpuCreateShaderWithResourceLayout(
        demo->device,
        lesson23_grid_frag_wgsl, lesson23_grid_frag_wgsl_size,
        lesson23_grid_frag_msl, lesson23_grid_frag_msl_size,
        ForgeGpuShaderLayout_lesson23_grid_frag());
    shadow_vertex_shader = ForgeGpuCreateShader(
        demo->device, SDL_GPU_SHADERSTAGE_VERTEX,
        lesson23_shadow_vert_wgsl, lesson23_shadow_vert_wgsl_size,
        lesson23_shadow_vert_msl, lesson23_shadow_vert_msl_size,
        0, 0, 0, 1);
    shadow_fragment_shader = ForgeGpuCreateShader(
        demo->device, SDL_GPU_SHADERSTAGE_FRAGMENT,
        lesson23_shadow_frag_wgsl, lesson23_shadow_frag_wgsl_size,
        lesson23_shadow_frag_msl, lesson23_shadow_frag_msl_size,
        0, 0, 0, 1);
    emissive_fragment_shader = ForgeGpuCreateShader(
        demo->device, SDL_GPU_SHADERSTAGE_FRAGMENT,
        lesson23_emissive_frag_wgsl, lesson23_emissive_frag_wgsl_size,
        lesson23_emissive_frag_msl, lesson23_emissive_frag_msl_size,
        0, 0, 0, 1);
    fullscreen_vertex_shader = ForgeGpuCreateShader(
        demo->device, SDL_GPU_SHADERSTAGE_VERTEX,
        lesson23_fullscreen_vert_wgsl, lesson23_fullscreen_vert_wgsl_size,
        lesson23_fullscreen_vert_msl, lesson23_fullscreen_vert_msl_size,
        0, 0, 0, 0);
    downsample_fragment_shader = ForgeGpuCreateShader(
        demo->device, SDL_GPU_SHADERSTAGE_FRAGMENT,
        lesson23_bloom_downsample_frag_wgsl, lesson23_bloom_downsample_frag_wgsl_size,
        lesson23_bloom_downsample_frag_msl, lesson23_bloom_downsample_frag_msl_size,
        1, 0, 0, 1);
    upsample_fragment_shader = ForgeGpuCreateShader(
        demo->device, SDL_GPU_SHADERSTAGE_FRAGMENT,
        lesson23_bloom_upsample_frag_wgsl, lesson23_bloom_upsample_frag_wgsl_size,
        lesson23_bloom_upsample_frag_msl, lesson23_bloom_upsample_frag_msl_size,
        1, 0, 0, 1);
    tonemap_fragment_shader = ForgeGpuCreateShader(
        demo->device, SDL_GPU_SHADERSTAGE_FRAGMENT,
        lesson23_tonemap_frag_wgsl, lesson23_tonemap_frag_wgsl_size,
        lesson23_tonemap_frag_msl, lesson23_tonemap_frag_msl_size,
        2, 0, 0, 1);
    if (!scene_vertex_shader || !scene_fragment_shader ||
        !grid_vertex_shader || !grid_fragment_shader ||
        !shadow_vertex_shader || !shadow_fragment_shader ||
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
        LESSON23_HDR_FORMAT,
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
        LESSON23_HDR_FORMAT,
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
        LESSON23_HDR_FORMAT,
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
    state->shadow_pipeline = ForgeGpuCreateLessonGraphicsPipelineWithColorFormat(
        demo,
        shadow_vertex_shader,
        shadow_fragment_shader,
        SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        LESSON23_SHADOW_MAP_FORMAT,
        &mesh_vertex_buffer_desc,
        1,
        mesh_vertex_attributes,
        SDL_arraysize(mesh_vertex_attributes),
        1,
        true,
        LESSON23_SHADOW_DEPTH_FORMAT,
        true,
        true,
        SDL_GPU_CULLMODE_NONE,
        0.0f,
        0.0f);
    state->downsample_pipeline = ForgeGpuCreateFullscreenPostprocessPipeline(
        demo, fullscreen_vertex_shader, downsample_fragment_shader, LESSON23_HDR_FORMAT, false);
    state->upsample_pipeline = ForgeGpuCreateFullscreenPostprocessPipeline(
        demo, fullscreen_vertex_shader, upsample_fragment_shader, LESSON23_HDR_FORMAT, true);
    state->tonemap_pipeline = ForgeGpuCreateFullscreenPostprocessPipeline(
        demo, fullscreen_vertex_shader, tonemap_fragment_shader, demo->color_format, false);

    ok = state->scene_pipeline &&
         state->grid_pipeline &&
         state->emissive_pipeline &&
         state->shadow_pipeline &&
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
    if (shadow_fragment_shader) {
        SDL_ReleaseGPUShader(demo->device, shadow_fragment_shader);
    }
    if (shadow_vertex_shader) {
        SDL_ReleaseGPUShader(demo->device, shadow_vertex_shader);
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

static void lesson23_draw_model_shadow(
    SDL_GPURenderPass *render_pass,
    SDL_GPUCommandBuffer *command_buffer,
    const GpuSceneData *model,
    Mat4 placement,
    Mat4 face_vp,
    const Lesson23PointLight *light)
{
    for (int node_index = 0; node_index < model->loaded.node_count; node_index += 1) {
        const ForgeGpuSceneNode *node = &model->loaded.nodes[node_index];
        const ForgeGpuSceneMesh *mesh;
        Mat4 model_matrix;
        Lesson23ShadowVertUniforms vertex_uniforms;
        Lesson23ShadowFragUniforms fragment_uniforms;

        if (node->mesh_index < 0 || node->mesh_index >= model->loaded.mesh_count) {
            continue;
        }
        model_matrix = mat4_multiply(placement, mat4_from_forge(node->world_transform));
        vertex_uniforms.model = model_matrix;
        vertex_uniforms.light_mvp = mat4_multiply(face_vp, model_matrix);
        SDL_PushGPUVertexUniformData(command_buffer, 0, &vertex_uniforms, sizeof(vertex_uniforms));

        SDL_zero(fragment_uniforms);
        fragment_uniforms.light_pos[0] = light->position[0];
        fragment_uniforms.light_pos[1] = light->position[1];
        fragment_uniforms.light_pos[2] = light->position[2];
        fragment_uniforms.far_plane = LESSON23_SHADOW_FAR_PLANE;
        SDL_PushGPUFragmentUniformData(command_buffer, 0, &fragment_uniforms, sizeof(fragment_uniforms));

        mesh = &model->loaded.meshes[node->mesh_index];
        for (int primitive_offset = 0; primitive_offset < mesh->primitive_count; primitive_offset += 1) {
            const int primitive_index = mesh->first_primitive + primitive_offset;
            const GpuPrimitive *primitive;
            SDL_GPUBufferBinding vertex_binding;

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

static void lesson23_draw_model_scene(
    ForgeGpuDemo *demo,
    SDL_GPURenderPass *render_pass,
    SDL_GPUCommandBuffer *command_buffer,
    const GpuSceneData *model,
    Mat4 placement,
    Mat4 cam_vp,
    const Lesson23PointLight lights[LESSON23_MAX_POINT_LIGHTS])
{
    Lesson23State *state = lesson23_state(demo);

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
            SDL_GPUTextureSamplerBinding sampler_bindings[1 + LESSON23_MAX_POINT_LIGHTS];
            SDL_GPUBufferBinding vertex_binding;
            Lesson23SceneFragUniforms fragment_uniforms;

            if (primitive_index < 0 || primitive_index >= model->primitive_count) {
                continue;
            }
            primitive = &model->primitives[primitive_index];
            material = ForgeGpuModelMaterialOrDefault(model, primitive->material_index, &fallback_material);

            SDL_zero(fragment_uniforms);
            SDL_memcpy(fragment_uniforms.base_color, material->base_color, sizeof(fragment_uniforms.base_color));
            fragment_uniforms.eye_pos[0] = demo->lesson.camera_position.x;
            fragment_uniforms.eye_pos[1] = demo->lesson.camera_position.y;
            fragment_uniforms.eye_pos[2] = demo->lesson.camera_position.z;
            fragment_uniforms.has_texture = material->has_texture ? 1.0f : 0.0f;
            fragment_uniforms.shininess = LESSON23_MATERIAL_SHININESS;
            fragment_uniforms.ambient = LESSON23_MATERIAL_AMBIENT;
            fragment_uniforms.specular_str = LESSON23_MATERIAL_SPECULAR_STRENGTH;
            fragment_uniforms.shadow_far_plane = LESSON23_SHADOW_FAR_PLANE;
            SDL_memcpy(fragment_uniforms.lights, lights, sizeof(fragment_uniforms.lights));
            SDL_PushGPUFragmentUniformData(command_buffer, 0, &fragment_uniforms, sizeof(fragment_uniforms));

            SDL_zeroa(sampler_bindings);
            sampler_bindings[0].texture = material->has_texture ? material->texture : demo->lesson.white_texture;
            sampler_bindings[0].sampler = demo->lesson.samplers[0];
            for (int i = 0; i < LESSON23_MAX_POINT_LIGHTS; i += 1) {
                sampler_bindings[i + 1].texture = state->shadow_cubes[i];
                sampler_bindings[i + 1].sampler = demo->lesson.samplers[3];
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

static void lesson23_draw_grid(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    Mat4 cam_vp,
    const Lesson23PointLight lights[LESSON23_MAX_POINT_LIGHTS])
{
    Lesson23State *state = lesson23_state(demo);
    SDL_GPUTextureSamplerBinding shadow_bindings[LESSON23_MAX_POINT_LIGHTS];
    SDL_GPUBufferBinding vertex_binding;
    SDL_GPUBufferBinding index_binding;
    Lesson23GridVertUniforms vertex_uniforms;
    Lesson23GridFragUniforms fragment_uniforms;

    SDL_BindGPUGraphicsPipeline(render_pass, state->grid_pipeline);
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
    fragment_uniforms.eye_pos[0] = demo->lesson.camera_position.x;
    fragment_uniforms.eye_pos[1] = demo->lesson.camera_position.y;
    fragment_uniforms.eye_pos[2] = demo->lesson.camera_position.z;
    fragment_uniforms.grid_spacing = LESSON23_GRID_SPACING;
    fragment_uniforms.line_width = LESSON23_GRID_LINE_WIDTH;
    fragment_uniforms.fade_distance = LESSON23_GRID_FADE_DISTANCE;
    fragment_uniforms.ambient = LESSON23_GRID_AMBIENT;
    fragment_uniforms.shininess = LESSON23_GRID_SHININESS;
    fragment_uniforms.specular_str = LESSON23_GRID_SPECULAR_STRENGTH;
    fragment_uniforms.shadow_far_plane = LESSON23_SHADOW_FAR_PLANE;
    SDL_memcpy(fragment_uniforms.lights, lights, sizeof(fragment_uniforms.lights));
    SDL_PushGPUFragmentUniformData(command_buffer, 0, &fragment_uniforms, sizeof(fragment_uniforms));

    SDL_zeroa(shadow_bindings);
    for (int i = 0; i < LESSON23_MAX_POINT_LIGHTS; i += 1) {
        shadow_bindings[i].texture = state->shadow_cubes[i];
        shadow_bindings[i].sampler = demo->lesson.samplers[3];
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

static void lesson23_draw_emissive_spheres(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    Mat4 cam_vp,
    const Lesson23PointLight lights[LESSON23_MAX_POINT_LIGHTS])
{
    Lesson23State *state = lesson23_state(demo);
    SDL_GPUBufferBinding vertex_binding;
    SDL_GPUBufferBinding index_binding;

    SDL_BindGPUGraphicsPipeline(render_pass, state->emissive_pipeline);
    SDL_zero(vertex_binding);
    vertex_binding.buffer = state->sphere_vertex_buffer;
    SDL_BindGPUVertexBuffers(render_pass, 0, &vertex_binding, 1);
    SDL_zero(index_binding);
    index_binding.buffer = state->sphere_index_buffer;
    SDL_BindGPUIndexBuffer(render_pass, &index_binding, SDL_GPU_INDEXELEMENTSIZE_16BIT);

    for (int i = 0; i < LESSON23_MAX_POINT_LIGHTS; i += 1) {
        UniformMvpModel vertex_uniforms;
        Lesson23EmissiveFragUniforms fragment_uniforms;
        Mat4 model;

        if (lights[i].intensity <= 0.0f) {
            continue;
        }

        model = mat4_translate({ lights[i].position[0], lights[i].position[1], lights[i].position[2] });
        vertex_uniforms.model = model;
        vertex_uniforms.mvp = mat4_multiply(cam_vp, model);
        SDL_PushGPUVertexUniformData(command_buffer, 0, &vertex_uniforms, sizeof(vertex_uniforms));

        SDL_zero(fragment_uniforms);
        fragment_uniforms.emission_color[0] = lights[i].color[0] * LESSON23_EMISSION_SCALE;
        fragment_uniforms.emission_color[1] = lights[i].color[1] * LESSON23_EMISSION_SCALE;
        fragment_uniforms.emission_color[2] = lights[i].color[2] * LESSON23_EMISSION_SCALE;
        SDL_PushGPUFragmentUniformData(command_buffer, 0, &fragment_uniforms, sizeof(fragment_uniforms));

        SDL_DrawGPUIndexedPrimitives(render_pass, state->sphere_index_count, 1, 0, 0, 0);
    }
}

static bool lesson23_render_shadow_maps(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    const Lesson23PointLight lights[LESSON23_MAX_POINT_LIGHTS])
{
    Lesson23State *state = lesson23_state(demo);
    const Mat4 truck_placement = mat4_identity();

    for (int light_index = 0; light_index < LESSON23_MAX_POINT_LIGHTS; light_index += 1) {
        Mat4 face_vps[LESSON23_CUBE_FACE_COUNT];
        Vec3 light_pos;

        if (lights[light_index].intensity <= 0.0f) {
            continue;
        }

        light_pos = { lights[light_index].position[0], lights[light_index].position[1], lights[light_index].position[2] };
        lesson23_build_cube_face_view_projections(light_pos, face_vps);

        for (int face = 0; face < LESSON23_CUBE_FACE_COUNT; face += 1) {
            SDL_GPUColorTargetInfo color_target;
            SDL_GPUDepthStencilTargetInfo depth_target;
            SDL_GPURenderPass *render_pass;

            SDL_zero(color_target);
            color_target.texture = state->shadow_cubes[light_index];
            color_target.layer_or_depth_plane = (Uint32)face;
            color_target.load_op = SDL_GPU_LOADOP_CLEAR;
            color_target.store_op = SDL_GPU_STOREOP_STORE;
            color_target.clear_color = { 1.0f, 0.0f, 0.0f, 1.0f };

            SDL_zero(depth_target);
            depth_target.texture = state->shadow_depth;
            depth_target.load_op = SDL_GPU_LOADOP_CLEAR;
            depth_target.store_op = SDL_GPU_STOREOP_DONT_CARE;
            depth_target.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
            depth_target.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
            depth_target.clear_depth = 1.0f;

            render_pass = SDL_BeginGPURenderPass(command_buffer, &color_target, 1, &depth_target);
            if (!render_pass) {
                return false;
            }

            SDL_BindGPUGraphicsPipeline(render_pass, state->shadow_pipeline);
            lesson23_draw_model_shadow(
                render_pass,
                command_buffer,
                &state->models[LESSON23_MODEL_TRUCK],
                truck_placement,
                face_vps[face],
                &lights[light_index]);
            for (int box_index = 0; box_index < state->box_count; box_index += 1) {
                const Mat4 translate = mat4_translate(state->box_placements[box_index].position);
                const Mat4 rotate = mat4_rotate_y(state->box_placements[box_index].y_rotation);
                const Mat4 placement = mat4_multiply(translate, rotate);

                lesson23_draw_model_shadow(
                    render_pass,
                    command_buffer,
                    &state->models[LESSON23_MODEL_BOX],
                    placement,
                    face_vps[face],
                    &lights[light_index]);
            }

            SDL_EndGPURenderPass(render_pass);
        }
    }
    return true;
}

static bool lesson23_render_hdr_scene(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    Mat4 cam_vp,
    const Lesson23PointLight lights[LESSON23_MAX_POINT_LIGHTS])
{
    Lesson23State *state = lesson23_state(demo);
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

    lesson23_draw_grid(demo, command_buffer, render_pass, cam_vp, lights);

    SDL_BindGPUGraphicsPipeline(render_pass, state->scene_pipeline);
    lesson23_draw_model_scene(
        demo,
        render_pass,
        command_buffer,
        &state->models[LESSON23_MODEL_TRUCK],
        truck_placement,
        cam_vp,
        lights);
    for (int i = 0; i < state->box_count; i += 1) {
        const Mat4 translate = mat4_translate(state->box_placements[i].position);
        const Mat4 rotate = mat4_rotate_y(state->box_placements[i].y_rotation);
        const Mat4 placement = mat4_multiply(translate, rotate);

        lesson23_draw_model_scene(
            demo,
            render_pass,
            command_buffer,
            &state->models[LESSON23_MODEL_BOX],
            placement,
            cam_vp,
            lights);
    }

    lesson23_draw_emissive_spheres(demo, command_buffer, render_pass, cam_vp, lights);

    SDL_EndGPURenderPass(render_pass);
    return true;
}

static bool lesson23_render_bloom(ForgeGpuDemo *demo, SDL_GPUCommandBuffer *command_buffer)
{
    Lesson23State *state = lesson23_state(demo);

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
        LESSON23_FULLSCREEN_VERTICES);
}

static bool lesson23_render_tonemap(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPUTexture *swapchain_texture)
{
    Lesson23State *state = lesson23_state(demo);
    Lesson23TonemapFragUniforms uniforms;

    SDL_zero(uniforms);
    uniforms.exposure = state->exposure;
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
        LESSON23_FULLSCREEN_VERTICES);
}

bool ForgeGpuCreateLesson23(ForgeGpuDemo *demo)
{
    LessonState *lesson = &demo->lesson;
    Lesson23State *state;

    if (!SDL_GPUTextureSupportsFormat(
            demo->device,
            LESSON23_HDR_FORMAT,
            SDL_GPU_TEXTURETYPE_2D,
            SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER)) {
        SDL_SetError("lesson 23 requires sampled R16G16B16A16_FLOAT color targets");
        return false;
    }
    if (!SDL_GPUTextureSupportsFormat(
            demo->device,
            SDL_GPU_TEXTUREFORMAT_D32_FLOAT,
            SDL_GPU_TEXTURETYPE_2D,
            SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET)) {
        SDL_SetError("lesson 23 requires D32_FLOAT depth targets");
        return false;
    }
    if (!SDL_GPUTextureSupportsFormat(
            demo->device,
            LESSON23_SHADOW_MAP_FORMAT,
            SDL_GPU_TEXTURETYPE_CUBE,
            SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER)) {
        SDL_SetError("lesson 23 requires sampled R32_FLOAT cube color targets");
        return false;
    }

    state = (Lesson23State *)SDL_calloc(1, sizeof(*state));
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
        LESSON23_MAX_ANISOTROPY);
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
    lesson->samplers[3] = ForgeGpuCreateSamplerWithAddress(
        demo->device,
        SDL_GPU_FILTER_NEAREST,
        SDL_GPU_FILTER_NEAREST,
        SDL_GPU_SAMPLERMIPMAPMODE_NEAREST,
        SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        0.0f);
    if (!lesson->white_texture || !lesson->samplers[0] || !lesson->samplers[1] ||
        !lesson->samplers[2] || !lesson->samplers[3]) {
        return false;
    }

    if (!ForgeGpuCreateGridBuffers(demo) ||
        !lesson23_create_sphere_geometry(demo) ||
        !lesson23_create_shadow_resources(demo) ||
        !ForgeGpuLoadSceneModel(demo, &state->models[LESSON23_MODEL_TRUCK], "models/CesiumMilkTruck/CesiumMilkTruck.gltf") ||
        !ForgeGpuLoadSceneModel(demo, &state->models[LESSON23_MODEL_BOX], "models/BoxTextured/BoxTextured.gltf")) {
        return false;
    }

    lesson23_generate_box_placements(state);
    lesson->camera_position = { 5.0f, 4.0f, 8.0f };
    lesson->camera_yaw = 34.0f * FORGE_GPU_DEG2RAD;
    lesson->camera_pitch = -20.0f * FORGE_GPU_DEG2RAD;
    lesson->move_speed = 5.0f;
    lesson->last_ticks = SDL_GetTicks();
    state->exposure = 1.0f;
    state->bloom_enabled = true;
    state->bloom_intensity = LESSON23_BLOOM_INTENSITY_DEFAULT;
    state->bloom_threshold = LESSON23_BLOOM_THRESHOLD_DEFAULT;
    for (int i = 0; i < LESSON23_MAX_POINT_LIGHTS; i += 1) {
        state->light_enabled[i] = true;
    }

    return lesson23_create_pipelines(demo);
}

bool ForgeGpuRenderLesson23(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPUTexture *swapchain_texture,
    Uint32 width,
    Uint32 height)
{
    Lesson23State *state = lesson23_state(demo);
    Lesson23PointLight lights[LESSON23_MAX_POINT_LIGHTS];
    Mat4 view;
    Mat4 projection;
    Mat4 cam_vp;

    if (!state) {
        SDL_SetError("lesson 23 internal state is missing");
        return false;
    }

    ForgeGpuUpdateCameraFromInput(demo);
    ForgeGpuCameraViewProjection(demo, width, height, 100.0f, &view, &projection);
    cam_vp = mat4_multiply(projection, view);
    lesson23_fill_lights(state, ForgeGpuFrameTimeSeconds(demo), lights);

    if (!ForgeGpuCreateDepthTextureWithFormat(demo, width, height, SDL_GPU_TEXTUREFORMAT_D32_FLOAT) ||
        !lesson23_create_hdr_target(demo, width, height) ||
        !ForgeGpuEnsureBloomChain(
            demo,
            &state->bloom,
            state->hdr_target,
            state->hdr_width,
            state->hdr_height,
            LESSON23_HDR_FORMAT)) {
        return false;
    }

    if (!lesson23_render_shadow_maps(demo, command_buffer, lights) ||
        !lesson23_render_hdr_scene(demo, command_buffer, cam_vp, lights) ||
        !lesson23_render_bloom(demo, command_buffer) ||
        !lesson23_render_tonemap(demo, command_buffer, swapchain_texture)) {
        return false;
    }
    return true;
}

void ForgeGpuDebugLesson23(ForgeGpuDemo *demo)
{
    Lesson23State *state = lesson23_state(demo);

    if (!state) {
        return;
    }
    ImGui::Text("Exposure: %.1f", (double)state->exposure);
    ImGui::Text("Bloom: %s", state->bloom_enabled ? "on" : "off");
    ImGui::Text("Bloom intensity: %.3f", (double)state->bloom_intensity);
    ImGui::Text("Bloom threshold: %.1f", (double)state->bloom_threshold);
    ImGui::Text(
        "Lights: %s %s %s %s",
        state->light_enabled[0] ? "0:on" : "0:off",
        state->light_enabled[1] ? "1:on" : "1:off",
        state->light_enabled[2] ? "2:on" : "2:off",
        state->light_enabled[3] ? "3:on" : "3:off");
    ImGui::Text("Shadow maps: 4 x 512 cubemap R32_FLOAT");
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

void ForgeGpuControlsLesson23(ForgeGpuDemo *demo)
{
    (void)demo;
    ImGui::Text("1/2/3/4: Toggle lights");
    ImGui::Text("+/-: Exposure up/down");
    ImGui::Text("B: Toggle bloom");
    ImGui::Text("Up/Down: Bloom intensity");
    ImGui::Text("Left/Right: Bloom threshold");
}

static void lesson23_export_metrics(Lesson23State *state)
{
    if (!state) {
        return;
    }
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson23ExposureTenths", (double)((int)(state->exposure * 10.0f + 0.5f)));
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson23BloomEnabled", state->bloom_enabled ? 1.0 : 0.0);
}

bool ForgeGpuHandleLesson23Event(ForgeGpuDemo *demo, const SDL_Event *event)
{
    Lesson23State *state = lesson23_state(demo);

    if (!state || event->type != SDL_EVENT_KEY_DOWN) {
        return false;
    }
    if (!event->key.repeat && event->key.key >= SDLK_1 && event->key.key <= SDLK_4) {
        const int index = (int)(event->key.key - SDLK_1);
        state->light_enabled[index] = !state->light_enabled[index];
        return true;
    }
    if (ForgeGpuEventIsPlusKey(event)) {
        state->exposure = lesson23_clamp_float(
            state->exposure + LESSON23_EXPOSURE_STEP,
            LESSON23_EXPOSURE_MIN,
            LESSON23_EXPOSURE_MAX);
        return true;
    }
    if (ForgeGpuEventIsMinusKey(event)) {
        state->exposure = lesson23_clamp_float(
            state->exposure - LESSON23_EXPOSURE_STEP,
            LESSON23_EXPOSURE_MIN,
            LESSON23_EXPOSURE_MAX);
        return true;
    }
    if (!event->key.repeat && event->key.key == SDLK_B) {
        state->bloom_enabled = !state->bloom_enabled;
        return true;
    }
    if (event->key.key == SDLK_UP) {
        state->bloom_intensity = lesson23_clamp_float(
            state->bloom_intensity + LESSON23_BLOOM_INTENSITY_STEP,
            LESSON23_BLOOM_INTENSITY_MIN,
            LESSON23_BLOOM_INTENSITY_MAX);
        return true;
    }
    if (event->key.key == SDLK_DOWN) {
        state->bloom_intensity = lesson23_clamp_float(
            state->bloom_intensity - LESSON23_BLOOM_INTENSITY_STEP,
            LESSON23_BLOOM_INTENSITY_MIN,
            LESSON23_BLOOM_INTENSITY_MAX);
        return true;
    }
    if (event->key.key == SDLK_RIGHT) {
        state->bloom_threshold = lesson23_clamp_float(
            state->bloom_threshold + LESSON23_BLOOM_THRESHOLD_STEP,
            LESSON23_BLOOM_THRESHOLD_MIN,
            LESSON23_BLOOM_THRESHOLD_MAX);
        return true;
    }
    if (event->key.key == SDLK_LEFT) {
        state->bloom_threshold = lesson23_clamp_float(
            state->bloom_threshold - LESSON23_BLOOM_THRESHOLD_STEP,
            LESSON23_BLOOM_THRESHOLD_MIN,
            LESSON23_BLOOM_THRESHOLD_MAX);
        return true;
    }
    return false;
}

void ForgeGpuExportLesson23Metrics(ForgeGpuDemo *demo)
{
    lesson23_export_metrics(lesson23_state(demo));
}

void ForgeGpuDestroyLesson23(ForgeGpuDemo *demo)
{
    Lesson23State *state = lesson23_state(demo);

    if (!state) {
        return;
    }
    for (int i = 0; i < LESSON23_MODEL_COUNT; i += 1) {
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
    for (int i = 0; i < LESSON23_MAX_POINT_LIGHTS; i += 1) {
        if (state->shadow_cubes[i]) {
            SDL_ReleaseGPUTexture(demo->device, state->shadow_cubes[i]);
        }
    }
    if (state->shadow_depth) {
        SDL_ReleaseGPUTexture(demo->device, state->shadow_depth);
    }
    if (state->tonemap_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, state->tonemap_pipeline);
    }
    if (state->upsample_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, state->upsample_pipeline);
    }
    if (state->downsample_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, state->downsample_pipeline);
    }
    if (state->shadow_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, state->shadow_pipeline);
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
