#include "forge_gpu_lessons.h"

#include "forge_gpu_camera.h"
#include "forge_gpu_gpu_helpers.h"
#include "forge_gpu_lesson_common.h"
#include "forge_gpu_math.h"
#include "forge_gpu_scene.h"
#include "forge_gpu_shader_layouts.h"
#include "shaders/generated/forge_gpu_lesson_24_shaders.h"
#include "imgui.h"

#include <stddef.h>

#define LESSON24_MODEL_TRUCK 0
#define LESSON24_MODEL_BOX 1
#define LESSON24_MODEL_SEARCHLIGHT 2
#define LESSON24_MODEL_COUNT 3
#define LESSON24_BOX_COUNT 5
#define LESSON24_HDR_FORMAT SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT
#define LESSON24_FULLSCREEN_VERTICES 6
#define LESSON24_SHADOW_MAP_SIZE 1024u
#define LESSON24_SHADOW_FORMAT SDL_GPU_TEXTUREFORMAT_D32_FLOAT
#define LESSON24_GOBO_FORMAT SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM
#define LESSON24_GRID_INDEX_COUNT 6
#define LESSON24_MAX_ANISOTROPY 4.0f
#define LESSON24_MATERIAL_AMBIENT 0.05f
#define LESSON24_MATERIAL_SHININESS 64.0f
#define LESSON24_MATERIAL_SPECULAR_STRENGTH 0.5f
#define LESSON24_FILL_INTENSITY 0.05f
#define LESSON24_SPOT_INNER_DEG 20.0f
#define LESSON24_SPOT_OUTER_DEG 30.0f
#define LESSON24_SPOT_INTENSITY 5.0f
#define LESSON24_SPOT_NEAR 0.5f
#define LESSON24_SPOT_FAR 30.0f
#define LESSON24_GLASS_MATERIAL_INDEX 1
#define LESSON24_GLASS_HDR_BRIGHTNESS 35.0f
#define LESSON24_SEARCHLIGHT_SCALE 0.003f
#define LESSON24_SEARCHLIGHT_ROTATION_DEG 225.0f
#define LESSON24_DEFAULT_EXPOSURE 1.0f
#define LESSON24_DEFAULT_TONEMAP 2u
#define LESSON24_DEFAULT_BLOOM_THRESHOLD 1.0f
#define LESSON24_DEFAULT_BLOOM_INTENSITY 0.5f
#define LESSON24_GRID_SPACING 1.0f
#define LESSON24_GRID_LINE_WIDTH 0.02f
#define LESSON24_GRID_FADE_DISTANCE 40.0f

struct Lesson24SceneFragUniforms
{
    float base_color[4];
    float eye_pos[3];
    float has_texture;
    float ambient;
    float fill_intensity;
    float shininess;
    float specular_str;
    float fill_dir[4];
    float spot_pos[3];
    float spot_intensity;
    float spot_dir[3];
    float cos_inner;
    float spot_color[3];
    float cos_outer;
    Mat4 light_vp;
};

struct Lesson24GridVertUniforms
{
    Mat4 vp;
};

struct Lesson24GridFragUniforms
{
    float line_color[4];
    float bg_color[4];
    float eye_pos[3];
    float grid_spacing;
    float line_width;
    float fade_distance;
    float ambient;
    float fill_intensity;
    float fill_dir[4];
    float spot_pos[3];
    float spot_intensity;
    float spot_dir[3];
    float cos_inner;
    float spot_color[3];
    float cos_outer;
    Mat4 light_vp;
};

struct Lesson24ShadowVertUniforms
{
    Mat4 light_mvp;
};

struct Lesson24TonemapFragUniforms
{
    float exposure;
    Uint32 tonemap_mode;
    float bloom_intensity;
    float pad;
};

struct Lesson24BoxPlacement
{
    Vec3 position;
    float y_rotation;
};

struct Lesson24State
{
    GpuSceneData models[LESSON24_MODEL_COUNT];
    SDL_GPUGraphicsPipeline *scene_pipeline;
    SDL_GPUGraphicsPipeline *grid_pipeline;
    SDL_GPUGraphicsPipeline *shadow_pipeline;
    SDL_GPUGraphicsPipeline *downsample_pipeline;
    SDL_GPUGraphicsPipeline *upsample_pipeline;
    SDL_GPUGraphicsPipeline *tonemap_pipeline;
    SDL_GPUTexture *hdr_target;
    ForgeGpuBloomChain bloom;
    SDL_GPUTexture *shadow_depth;
    SDL_GPUTexture *gobo_texture;
    SDL_GPUSampler *gobo_sampler;
    Uint32 hdr_width;
    Uint32 hdr_height;
    Lesson24BoxPlacement box_placements[LESSON24_BOX_COUNT];
    Mat4 light_vp;
    Mat4 searchlight_placement;
    Vec3 spot_dir;
    float exposure;
    float bloom_threshold;
    float bloom_intensity;
    Uint32 tonemap_mode;
};

static_assert(sizeof(Lesson24SceneFragUniforms) == lesson24_scene_frag_uniform_buffer_0_size, "lesson 24 scene fragment uniform size must match generated shader layout");
static_assert(sizeof(Lesson24GridFragUniforms) == lesson24_grid_frag_uniform_buffer_0_size, "lesson 24 grid fragment uniform size must match generated shader layout");

static Lesson24State *lesson24_state(ForgeGpuDemo *demo)
{
    return (Lesson24State *)demo->lesson.private_state;
}

static bool lesson24_create_hdr_target(ForgeGpuDemo *demo, Uint32 width, Uint32 height)
{
    Lesson24State *state = lesson24_state(demo);

    if (!state) {
        SDL_SetError("lesson 24 internal state is missing");
        return false;
    }
    return ForgeGpuEnsureSampledColorTarget(
        demo,
        &state->hdr_target,
        &state->hdr_width,
        &state->hdr_height,
        width,
        height,
        LESSON24_HDR_FORMAT);
}

static bool lesson24_create_shadow_depth(ForgeGpuDemo *demo)
{
    Lesson24State *state = lesson24_state(demo);
    SDL_GPUTextureCreateInfo texture_info;

    if (!state) {
        SDL_SetError("lesson 24 internal state is missing");
        return false;
    }

    SDL_zero(texture_info);
    texture_info.type = SDL_GPU_TEXTURETYPE_2D;
    texture_info.format = LESSON24_SHADOW_FORMAT;
    texture_info.width = LESSON24_SHADOW_MAP_SIZE;
    texture_info.height = LESSON24_SHADOW_MAP_SIZE;
    texture_info.layer_count_or_depth = 1;
    texture_info.num_levels = 1;
    texture_info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    texture_info.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    state->shadow_depth = SDL_CreateGPUTexture(demo->device, &texture_info);
    return state->shadow_depth != nullptr;
}

static bool lesson24_load_gobo_texture(ForgeGpuDemo *demo)
{
    Lesson24State *state = lesson24_state(demo);
    char path[FORGE_GPU_MAX_PATH];

    if (!state) {
        SDL_SetError("lesson 24 internal state is missing");
        return false;
    }
    if (!ForgeGpuJoinAssetPath(demo, "textures/24-gobo-spotlight/gobo_window.png", path, sizeof(path))) {
        SDL_SetError("lesson 24 gobo texture path is too long");
        return false;
    }

    state->gobo_texture = ForgeGpuLoadRgbaTexturePathWithFormat(
        demo,
        path,
        false,
        LESSON24_GOBO_FORMAT);
    return state->gobo_texture != nullptr;
}

static void lesson24_generate_box_placements(Lesson24State *state)
{
    static const Lesson24BoxPlacement placements[LESSON24_BOX_COUNT] = {
        { { -3.5f, 0.5f,  2.0f }, 0.3f },
        { { -2.5f, 0.5f,  0.5f }, 1.1f },
        { {  3.0f, 0.5f, -2.0f }, 0.7f },
        { { -1.0f, 0.5f, -3.0f }, 2.0f },
        { { -3.5f, 1.5f,  2.0f }, 0.9f }
    };

    SDL_memcpy(state->box_placements, placements, sizeof(placements));
}

static void lesson24_setup_static_transforms(Lesson24State *state)
{
    const Vec3 spot_pos = { 6.0f, 5.0f, 4.0f };
    const Vec3 spot_target = { 0.0f, 0.0f, 0.0f };
    const Mat4 light_view = mat4_look_at(spot_pos, spot_target, { 0.0f, 1.0f, 0.0f });
    const Mat4 light_proj = mat4_perspective(
        2.0f * LESSON24_SPOT_OUTER_DEG * FORGE_GPU_DEG2RAD,
        1.0f,
        LESSON24_SPOT_NEAR,
        LESSON24_SPOT_FAR);
    const Mat4 searchlight_scale = mat4_scale(LESSON24_SEARCHLIGHT_SCALE);
    const Mat4 searchlight_rotate = mat4_rotate_y(LESSON24_SEARCHLIGHT_ROTATION_DEG * FORGE_GPU_DEG2RAD);
    const Mat4 searchlight_translate = mat4_translate({ 6.0f, 1.0f, 4.0f });

    state->light_vp = mat4_multiply(light_proj, light_view);
    state->spot_dir = vec3_normalize(vec3_sub(spot_target, spot_pos));
    state->searchlight_placement = mat4_multiply(searchlight_translate, mat4_multiply(searchlight_rotate, searchlight_scale));
}

static void lesson24_fill_spotlight_scene_uniforms(
    ForgeGpuDemo *demo,
    const GpuMaterial *material,
    Lesson24SceneFragUniforms *uniforms)
{
    Lesson24State *state = lesson24_state(demo);

    SDL_zero(*uniforms);
    SDL_memcpy(uniforms->base_color, material->base_color, sizeof(uniforms->base_color));
    uniforms->eye_pos[0] = demo->lesson.camera_position.x;
    uniforms->eye_pos[1] = demo->lesson.camera_position.y;
    uniforms->eye_pos[2] = demo->lesson.camera_position.z;
    uniforms->has_texture = material->has_texture ? 1.0f : 0.0f;
    uniforms->ambient = LESSON24_MATERIAL_AMBIENT;
    uniforms->fill_intensity = LESSON24_FILL_INTENSITY;
    uniforms->shininess = LESSON24_MATERIAL_SHININESS;
    uniforms->specular_str = LESSON24_MATERIAL_SPECULAR_STRENGTH;
    uniforms->fill_dir[0] = 0.3f;
    uniforms->fill_dir[1] = -0.8f;
    uniforms->fill_dir[2] = 0.2f;
    uniforms->spot_pos[0] = 6.0f;
    uniforms->spot_pos[1] = 5.0f;
    uniforms->spot_pos[2] = 4.0f;
    uniforms->spot_intensity = LESSON24_SPOT_INTENSITY;
    uniforms->spot_dir[0] = state->spot_dir.x;
    uniforms->spot_dir[1] = state->spot_dir.y;
    uniforms->spot_dir[2] = state->spot_dir.z;
    uniforms->cos_inner = SDL_cosf(LESSON24_SPOT_INNER_DEG * FORGE_GPU_DEG2RAD);
    uniforms->spot_color[0] = 1.0f;
    uniforms->spot_color[1] = 0.95f;
    uniforms->spot_color[2] = 0.8f;
    uniforms->cos_outer = SDL_cosf(LESSON24_SPOT_OUTER_DEG * FORGE_GPU_DEG2RAD);
    uniforms->light_vp = state->light_vp;
}

static void lesson24_fill_grid_uniforms(
    ForgeGpuDemo *demo,
    Mat4 cam_vp,
    Lesson24GridVertUniforms *vertex_uniforms,
    Lesson24GridFragUniforms *fragment_uniforms)
{
    Lesson24State *state = lesson24_state(demo);

    SDL_zero(*vertex_uniforms);
    vertex_uniforms->vp = cam_vp;

    SDL_zero(*fragment_uniforms);
    fragment_uniforms->line_color[0] = 0.15f;
    fragment_uniforms->line_color[1] = 0.55f;
    fragment_uniforms->line_color[2] = 0.85f;
    fragment_uniforms->line_color[3] = 1.0f;
    fragment_uniforms->bg_color[0] = 0.04f;
    fragment_uniforms->bg_color[1] = 0.04f;
    fragment_uniforms->bg_color[2] = 0.08f;
    fragment_uniforms->bg_color[3] = 1.0f;
    fragment_uniforms->eye_pos[0] = demo->lesson.camera_position.x;
    fragment_uniforms->eye_pos[1] = demo->lesson.camera_position.y;
    fragment_uniforms->eye_pos[2] = demo->lesson.camera_position.z;
    fragment_uniforms->grid_spacing = LESSON24_GRID_SPACING;
    fragment_uniforms->line_width = LESSON24_GRID_LINE_WIDTH;
    fragment_uniforms->fade_distance = LESSON24_GRID_FADE_DISTANCE;
    fragment_uniforms->ambient = LESSON24_MATERIAL_AMBIENT;
    fragment_uniforms->fill_intensity = LESSON24_FILL_INTENSITY;
    fragment_uniforms->fill_dir[0] = 0.3f;
    fragment_uniforms->fill_dir[1] = -0.8f;
    fragment_uniforms->fill_dir[2] = 0.2f;
    fragment_uniforms->spot_pos[0] = 6.0f;
    fragment_uniforms->spot_pos[1] = 5.0f;
    fragment_uniforms->spot_pos[2] = 4.0f;
    fragment_uniforms->spot_intensity = LESSON24_SPOT_INTENSITY;
    fragment_uniforms->spot_dir[0] = state->spot_dir.x;
    fragment_uniforms->spot_dir[1] = state->spot_dir.y;
    fragment_uniforms->spot_dir[2] = state->spot_dir.z;
    fragment_uniforms->cos_inner = SDL_cosf(LESSON24_SPOT_INNER_DEG * FORGE_GPU_DEG2RAD);
    fragment_uniforms->spot_color[0] = 1.0f;
    fragment_uniforms->spot_color[1] = 0.95f;
    fragment_uniforms->spot_color[2] = 0.8f;
    fragment_uniforms->cos_outer = SDL_cosf(LESSON24_SPOT_OUTER_DEG * FORGE_GPU_DEG2RAD);
    fragment_uniforms->light_vp = state->light_vp;
}

static bool lesson24_create_pipelines(ForgeGpuDemo *demo)
{
    Lesson24State *state = lesson24_state(demo);
    SDL_GPUShader *scene_vertex_shader = nullptr;
    SDL_GPUShader *scene_fragment_shader = nullptr;
    SDL_GPUShader *grid_vertex_shader = nullptr;
    SDL_GPUShader *grid_fragment_shader = nullptr;
    SDL_GPUShader *shadow_vertex_shader = nullptr;
    SDL_GPUShader *shadow_fragment_shader = nullptr;
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
        SDL_SetError("lesson 24 internal state is missing");
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
        lesson24_scene_vert_wgsl, lesson24_scene_vert_wgsl_size,
        lesson24_scene_vert_msl, lesson24_scene_vert_msl_size,
        0, 0, 0, 1);
    scene_fragment_shader = ForgeGpuCreateShaderWithResourceLayout(
        demo->device,
        lesson24_scene_frag_wgsl, lesson24_scene_frag_wgsl_size,
        lesson24_scene_frag_msl, lesson24_scene_frag_msl_size,
        ForgeGpuShaderLayout_lesson24_scene_frag());
    grid_vertex_shader = ForgeGpuCreateShader(
        demo->device, SDL_GPU_SHADERSTAGE_VERTEX,
        lesson24_grid_vert_wgsl, lesson24_grid_vert_wgsl_size,
        lesson24_grid_vert_msl, lesson24_grid_vert_msl_size,
        0, 0, 0, 1);
    grid_fragment_shader = ForgeGpuCreateShaderWithResourceLayout(
        demo->device,
        lesson24_grid_frag_wgsl, lesson24_grid_frag_wgsl_size,
        lesson24_grid_frag_msl, lesson24_grid_frag_msl_size,
        ForgeGpuShaderLayout_lesson24_grid_frag());
    shadow_vertex_shader = ForgeGpuCreateShader(
        demo->device, SDL_GPU_SHADERSTAGE_VERTEX,
        lesson24_shadow_vert_wgsl, lesson24_shadow_vert_wgsl_size,
        lesson24_shadow_vert_msl, lesson24_shadow_vert_msl_size,
        0, 0, 0, 1);
    shadow_fragment_shader = ForgeGpuCreateShader(
        demo->device, SDL_GPU_SHADERSTAGE_FRAGMENT,
        lesson24_shadow_frag_wgsl, lesson24_shadow_frag_wgsl_size,
        lesson24_shadow_frag_msl, lesson24_shadow_frag_msl_size,
        0, 0, 0, 0);
    fullscreen_vertex_shader = ForgeGpuCreateShader(
        demo->device, SDL_GPU_SHADERSTAGE_VERTEX,
        lesson24_tonemap_vert_wgsl, lesson24_tonemap_vert_wgsl_size,
        lesson24_tonemap_vert_msl, lesson24_tonemap_vert_msl_size,
        0, 0, 0, 0);
    downsample_fragment_shader = ForgeGpuCreateShader(
        demo->device, SDL_GPU_SHADERSTAGE_FRAGMENT,
        lesson24_bloom_downsample_frag_wgsl, lesson24_bloom_downsample_frag_wgsl_size,
        lesson24_bloom_downsample_frag_msl, lesson24_bloom_downsample_frag_msl_size,
        1, 0, 0, 1);
    upsample_fragment_shader = ForgeGpuCreateShader(
        demo->device, SDL_GPU_SHADERSTAGE_FRAGMENT,
        lesson24_bloom_upsample_frag_wgsl, lesson24_bloom_upsample_frag_wgsl_size,
        lesson24_bloom_upsample_frag_msl, lesson24_bloom_upsample_frag_msl_size,
        1, 0, 0, 1);
    tonemap_fragment_shader = ForgeGpuCreateShader(
        demo->device, SDL_GPU_SHADERSTAGE_FRAGMENT,
        lesson24_tonemap_frag_wgsl, lesson24_tonemap_frag_wgsl_size,
        lesson24_tonemap_frag_msl, lesson24_tonemap_frag_msl_size,
        2, 0, 0, 1);
    if (!scene_vertex_shader || !scene_fragment_shader ||
        !grid_vertex_shader || !grid_fragment_shader ||
        !shadow_vertex_shader || !shadow_fragment_shader ||
        !fullscreen_vertex_shader || !downsample_fragment_shader ||
        !upsample_fragment_shader || !tonemap_fragment_shader) {
        goto done;
    }

    state->scene_pipeline = ForgeGpuCreateLessonGraphicsPipelineWithColorFormat(
        demo,
        scene_vertex_shader,
        scene_fragment_shader,
        SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        LESSON24_HDR_FORMAT,
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
        LESSON24_HDR_FORMAT,
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
    state->shadow_pipeline = ForgeGpuCreateLessonGraphicsPipelineWithColorFormat(
        demo,
        shadow_vertex_shader,
        shadow_fragment_shader,
        SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        LESSON24_HDR_FORMAT,
        &mesh_vertex_buffer_desc,
        1,
        mesh_vertex_attributes,
        SDL_arraysize(mesh_vertex_attributes),
        0,
        true,
        LESSON24_SHADOW_FORMAT,
        true,
        true,
        SDL_GPU_CULLMODE_BACK,
        0.0f,
        0.0f);
    state->downsample_pipeline = ForgeGpuCreateFullscreenPostprocessPipeline(
        demo, fullscreen_vertex_shader, downsample_fragment_shader, LESSON24_HDR_FORMAT, false);
    state->upsample_pipeline = ForgeGpuCreateFullscreenPostprocessPipeline(
        demo, fullscreen_vertex_shader, upsample_fragment_shader, LESSON24_HDR_FORMAT, true);
    state->tonemap_pipeline = ForgeGpuCreateFullscreenPostprocessPipeline(
        demo, fullscreen_vertex_shader, tonemap_fragment_shader, demo->color_format, false);

    ok = state->scene_pipeline &&
         state->grid_pipeline &&
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

static void lesson24_draw_model_shadow(
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
        Lesson24ShadowVertUniforms vertex_uniforms;

        if (node->mesh_index < 0 || node->mesh_index >= model->loaded.mesh_count) {
            continue;
        }
        model_matrix = mat4_multiply(placement, mat4_from_forge(node->world_transform));
        vertex_uniforms.light_mvp = mat4_multiply(light_vp, model_matrix);
        SDL_PushGPUVertexUniformData(command_buffer, 0, &vertex_uniforms, sizeof(vertex_uniforms));

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

static void lesson24_draw_model_scene(
    ForgeGpuDemo *demo,
    SDL_GPURenderPass *render_pass,
    SDL_GPUCommandBuffer *command_buffer,
    const GpuSceneData *model,
    Mat4 placement,
    Mat4 cam_vp)
{
    Lesson24State *state = lesson24_state(demo);

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
            SDL_GPUTextureSamplerBinding sampler_bindings[3];
            SDL_GPUBufferBinding vertex_binding;
            Lesson24SceneFragUniforms fragment_uniforms;

            if (primitive_index < 0 || primitive_index >= model->primitive_count) {
                continue;
            }
            primitive = &model->primitives[primitive_index];
            material = ForgeGpuModelMaterialOrDefault(model, primitive->material_index, &fallback_material);

            lesson24_fill_spotlight_scene_uniforms(demo, material, &fragment_uniforms);
            SDL_PushGPUFragmentUniformData(command_buffer, 0, &fragment_uniforms, sizeof(fragment_uniforms));

            SDL_zeroa(sampler_bindings);
            sampler_bindings[0].texture = material->has_texture ? material->texture : demo->lesson.white_texture;
            sampler_bindings[0].sampler = demo->lesson.samplers[0];
            sampler_bindings[1].texture = state->shadow_depth;
            sampler_bindings[1].sampler = demo->lesson.samplers[3];
            sampler_bindings[2].texture = state->gobo_texture;
            sampler_bindings[2].sampler = state->gobo_sampler;
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

static void lesson24_draw_grid(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    Mat4 cam_vp)
{
    Lesson24State *state = lesson24_state(demo);
    SDL_GPUTextureSamplerBinding texture_bindings[2];
    SDL_GPUBufferBinding vertex_binding;
    SDL_GPUBufferBinding index_binding;
    Lesson24GridVertUniforms vertex_uniforms;
    Lesson24GridFragUniforms fragment_uniforms;

    lesson24_fill_grid_uniforms(demo, cam_vp, &vertex_uniforms, &fragment_uniforms);

    SDL_BindGPUGraphicsPipeline(render_pass, state->grid_pipeline);
    SDL_PushGPUVertexUniformData(command_buffer, 0, &vertex_uniforms, sizeof(vertex_uniforms));
    SDL_PushGPUFragmentUniformData(command_buffer, 0, &fragment_uniforms, sizeof(fragment_uniforms));

    SDL_zeroa(texture_bindings);
    texture_bindings[0].texture = state->shadow_depth;
    texture_bindings[0].sampler = demo->lesson.samplers[3];
    texture_bindings[1].texture = state->gobo_texture;
    texture_bindings[1].sampler = state->gobo_sampler;
    SDL_BindGPUFragmentSamplers(render_pass, 0, texture_bindings, SDL_arraysize(texture_bindings));

    SDL_zero(vertex_binding);
    vertex_binding.buffer = demo->lesson.vertex_buffer;
    SDL_BindGPUVertexBuffers(render_pass, 0, &vertex_binding, 1);
    SDL_zero(index_binding);
    index_binding.buffer = demo->lesson.index_buffer;
    SDL_BindGPUIndexBuffer(render_pass, &index_binding, SDL_GPU_INDEXELEMENTSIZE_16BIT);
    SDL_DrawGPUIndexedPrimitives(render_pass, LESSON24_GRID_INDEX_COUNT, 1, 0, 0, 0);
}

static bool lesson24_render_shadow_map(ForgeGpuDemo *demo, SDL_GPUCommandBuffer *command_buffer)
{
    Lesson24State *state = lesson24_state(demo);
    SDL_GPUDepthStencilTargetInfo depth_target;
    SDL_GPURenderPass *render_pass;
    const Mat4 truck_placement = mat4_identity();

    SDL_zero(depth_target);
    depth_target.texture = state->shadow_depth;
    depth_target.load_op = SDL_GPU_LOADOP_CLEAR;
    depth_target.store_op = SDL_GPU_STOREOP_STORE;
    depth_target.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
    depth_target.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
    depth_target.clear_depth = 1.0f;

    render_pass = SDL_BeginGPURenderPass(command_buffer, nullptr, 0, &depth_target);
    if (!render_pass) {
        return false;
    }

    SDL_BindGPUGraphicsPipeline(render_pass, state->shadow_pipeline);
    lesson24_draw_model_shadow(
        render_pass,
        command_buffer,
        &state->models[LESSON24_MODEL_TRUCK],
        truck_placement,
        state->light_vp);
    for (int i = 0; i < LESSON24_BOX_COUNT; i += 1) {
        const Mat4 translate = mat4_translate(state->box_placements[i].position);
        const Mat4 rotate = mat4_rotate_y(state->box_placements[i].y_rotation);
        const Mat4 placement = mat4_multiply(translate, rotate);

        lesson24_draw_model_shadow(
            render_pass,
            command_buffer,
            &state->models[LESSON24_MODEL_BOX],
            placement,
            state->light_vp);
    }

    SDL_EndGPURenderPass(render_pass);
    return true;
}

static bool lesson24_render_hdr_scene(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    Mat4 cam_vp)
{
    Lesson24State *state = lesson24_state(demo);
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

    lesson24_draw_grid(demo, command_buffer, render_pass, cam_vp);

    SDL_BindGPUGraphicsPipeline(render_pass, state->scene_pipeline);
    lesson24_draw_model_scene(
        demo,
        render_pass,
        command_buffer,
        &state->models[LESSON24_MODEL_TRUCK],
        truck_placement,
        cam_vp);
    for (int i = 0; i < LESSON24_BOX_COUNT; i += 1) {
        const Mat4 translate = mat4_translate(state->box_placements[i].position);
        const Mat4 rotate = mat4_rotate_y(state->box_placements[i].y_rotation);
        const Mat4 placement = mat4_multiply(translate, rotate);

        lesson24_draw_model_scene(
            demo,
            render_pass,
            command_buffer,
            &state->models[LESSON24_MODEL_BOX],
            placement,
            cam_vp);
    }
    lesson24_draw_model_scene(
        demo,
        render_pass,
        command_buffer,
        &state->models[LESSON24_MODEL_SEARCHLIGHT],
        state->searchlight_placement,
        cam_vp);

    SDL_EndGPURenderPass(render_pass);
    return true;
}

static bool lesson24_render_bloom(ForgeGpuDemo *demo, SDL_GPUCommandBuffer *command_buffer)
{
    Lesson24State *state = lesson24_state(demo);

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
        LESSON24_FULLSCREEN_VERTICES);
}

static bool lesson24_render_tonemap(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPUTexture *swapchain_texture)
{
    Lesson24State *state = lesson24_state(demo);
    Lesson24TonemapFragUniforms uniforms;

    SDL_zero(uniforms);
    uniforms.exposure = state->exposure;
    uniforms.tonemap_mode = state->tonemap_mode;
    uniforms.bloom_intensity = state->bloom_intensity;
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
        LESSON24_FULLSCREEN_VERTICES);
}

bool ForgeGpuCreateLesson24(ForgeGpuDemo *demo)
{
    LessonState *lesson = &demo->lesson;
    Lesson24State *state;

    if (!SDL_GPUTextureSupportsFormat(
            demo->device,
            LESSON24_HDR_FORMAT,
            SDL_GPU_TEXTURETYPE_2D,
            SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER)) {
        SDL_SetError("lesson 24 requires sampled R16G16B16A16_FLOAT color targets");
        return false;
    }
    if (!SDL_GPUTextureSupportsFormat(
            demo->device,
            LESSON24_SHADOW_FORMAT,
            SDL_GPU_TEXTURETYPE_2D,
            SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER)) {
        SDL_SetError("lesson 24 requires sampled D32_FLOAT depth textures");
        return false;
    }
    if (!SDL_GPUTextureSupportsFormat(
            demo->device,
            LESSON24_GOBO_FORMAT,
            SDL_GPU_TEXTURETYPE_2D,
            SDL_GPU_TEXTUREUSAGE_SAMPLER)) {
        SDL_SetError("lesson 24 requires sampled R8G8B8A8_UNORM textures");
        return false;
    }

    state = (Lesson24State *)SDL_calloc(1, sizeof(*state));
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
        LESSON24_MAX_ANISOTROPY);
    lesson->samplers[1] = ForgeGpuCreateSamplerWithAddress(
        demo->device,
        SDL_GPU_FILTER_LINEAR,
        SDL_GPU_FILTER_LINEAR,
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
    state->gobo_sampler = ForgeGpuCreateSamplerWithAddress(
        demo->device,
        SDL_GPU_FILTER_LINEAR,
        SDL_GPU_FILTER_LINEAR,
        SDL_GPU_SAMPLERMIPMAPMODE_NEAREST,
        SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        0.0f);
    if (!lesson->white_texture || !lesson->samplers[0] || !lesson->samplers[1] ||
        !lesson->samplers[2] || !lesson->samplers[3] || !state->gobo_sampler) {
        return false;
    }

    if (!ForgeGpuCreateGridBuffers(demo) ||
        !lesson24_create_shadow_depth(demo) ||
        !lesson24_load_gobo_texture(demo) ||
        !ForgeGpuLoadSceneModel(demo, &state->models[LESSON24_MODEL_TRUCK], "models/CesiumMilkTruck/CesiumMilkTruck.gltf") ||
        !ForgeGpuLoadSceneModel(demo, &state->models[LESSON24_MODEL_BOX], "models/BoxTextured/BoxTextured.gltf") ||
        !ForgeGpuLoadSceneModel(demo, &state->models[LESSON24_MODEL_SEARCHLIGHT], "models/Searchlight/scene.gltf")) {
        return false;
    }

    if (state->models[LESSON24_MODEL_SEARCHLIGHT].material_count > LESSON24_GLASS_MATERIAL_INDEX) {
        GpuMaterial *glass = &state->models[LESSON24_MODEL_SEARCHLIGHT].materials[LESSON24_GLASS_MATERIAL_INDEX];
        glass->base_color[0] = LESSON24_GLASS_HDR_BRIGHTNESS;
        glass->base_color[1] = LESSON24_GLASS_HDR_BRIGHTNESS;
        glass->base_color[2] = LESSON24_GLASS_HDR_BRIGHTNESS;
        glass->base_color[3] = 1.0f;
    }

    lesson24_generate_box_placements(state);
    lesson24_setup_static_transforms(state);
    lesson->camera_position = { -2.7f, 2.5f, 8.4f };
    lesson->camera_yaw = -20.0f * FORGE_GPU_DEG2RAD;
    lesson->camera_pitch = -12.0f * FORGE_GPU_DEG2RAD;
    lesson->move_speed = 5.0f;
    lesson->last_ticks = SDL_GetTicks();
    state->exposure = LESSON24_DEFAULT_EXPOSURE;
    state->tonemap_mode = LESSON24_DEFAULT_TONEMAP;
    state->bloom_threshold = LESSON24_DEFAULT_BLOOM_THRESHOLD;
    state->bloom_intensity = LESSON24_DEFAULT_BLOOM_INTENSITY;

    return lesson24_create_pipelines(demo);
}

bool ForgeGpuRenderLesson24(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPUTexture *swapchain_texture,
    Uint32 width,
    Uint32 height)
{
    Lesson24State *state = lesson24_state(demo);
    Mat4 view;
    Mat4 projection;
    Mat4 cam_vp;

    if (!state) {
        SDL_SetError("lesson 24 internal state is missing");
        return false;
    }

    ForgeGpuUpdateCameraFromInput(demo);
    ForgeGpuCameraViewProjection(demo, width, height, 100.0f, &view, &projection);
    cam_vp = mat4_multiply(projection, view);

    if (!ForgeGpuCreateDepthTextureWithFormat(demo, width, height, SDL_GPU_TEXTUREFORMAT_D32_FLOAT) ||
        !lesson24_create_hdr_target(demo, width, height) ||
        !ForgeGpuEnsureBloomChain(
            demo,
            &state->bloom,
            state->hdr_target,
            state->hdr_width,
            state->hdr_height,
            LESSON24_HDR_FORMAT)) {
        return false;
    }

    if (!lesson24_render_shadow_map(demo, command_buffer) ||
        !lesson24_render_hdr_scene(demo, command_buffer, cam_vp) ||
        !lesson24_render_bloom(demo, command_buffer) ||
        !lesson24_render_tonemap(demo, command_buffer, swapchain_texture)) {
        return false;
    }
    return true;
}

void ForgeGpuDebugLesson24(ForgeGpuDemo *demo)
{
    Lesson24State *state = lesson24_state(demo);

    if (!state) {
        return;
    }
    ImGui::Text("Exposure: %.1f", (double)state->exposure);
    ImGui::Text("Bloom intensity: %.1f", (double)state->bloom_intensity);
    ImGui::Text("Bloom threshold: %.1f", (double)state->bloom_threshold);
    ImGui::Text("Shadow map: 1024x1024 D32_FLOAT");
    ImGui::Text("Gobo texture: R8G8B8A8_UNORM");
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

void ForgeGpuDestroyLesson24(ForgeGpuDemo *demo)
{
    Lesson24State *state = lesson24_state(demo);

    if (!state) {
        return;
    }
    for (int i = 0; i < LESSON24_MODEL_COUNT; i += 1) {
        ForgeGpuFreeSceneData(demo, &state->models[i]);
    }
    if (state->gobo_sampler) {
        SDL_ReleaseGPUSampler(demo->device, state->gobo_sampler);
    }
    if (state->gobo_texture) {
        SDL_ReleaseGPUTexture(demo->device, state->gobo_texture);
    }
    if (state->hdr_target) {
        SDL_ReleaseGPUTexture(demo->device, state->hdr_target);
    }
    ForgeGpuReleaseBloomChain(demo, &state->bloom);
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
    if (state->grid_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, state->grid_pipeline);
    }
    if (state->scene_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, state->scene_pipeline);
    }
    SDL_free(state);
    demo->lesson.private_state = nullptr;
}
