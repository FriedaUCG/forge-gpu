#include "forge_gpu_lessons.h"

#include "forge_gpu_browser_status.h"
#include "forge_gpu_camera.h"
#include "forge_gpu_forward_scene.h"
#include "forge_gpu_gpu_helpers.h"
#include "forge_gpu_lesson_common.h"
#include "forge_gpu_math.h"
#include "forge_gpu_shader_layouts.h"
#include "forge_gpu_shapes.h"
#include "shaders/generated/forge_gpu_lesson_40_shaders.h"
#include "shaders/generated/forge_gpu_shared_scene_shaders.h"
#include "imgui.h"

#include <stddef.h>

#define LESSON40_SPHERE_SLICES 48
#define LESSON40_SPHERE_STACKS 24
#define LESSON40_TORUS_SLICES 32
#define LESSON40_TORUS_STACKS 16
#define LESSON40_TORUS_RING_R 1.0f
#define LESSON40_TORUS_TUBE_R 0.35f
#define LESSON40_SPHERE_SPACING 2.0f
#define LESSON40_SPHERE_HEIGHT 1.0f
#define LESSON40_TORUS_HEIGHT 2.5f
#define LESSON40_TORUS_ROT_SPEED 0.5f
#define LESSON40_CAM_START_Y 2.0f
#define LESSON40_CAM_START_Z 5.0f
#define LESSON40_CAM_START_PITCH -0.3f
#define LESSON40_DEPTH_FORMAT SDL_GPU_TEXTUREFORMAT_D32_FLOAT
#define LESSON40_FOV_DEGREES 60.0f
#define LESSON40_NEAR_PLANE 0.1f
#define LESSON40_FAR_PLANE 200.0f
#define LESSON40_MOVE_SPEED 5.0f
#define LESSON40_MOUSE_SENSITIVITY 0.003f
#define LESSON40_PITCH_CLAMP 1.5f
#define LESSON40_SHADOW_MAP_SIZE 2048u
#define LESSON40_SHADOW_ORTHO_SIZE 15.0f
#define LESSON40_SHADOW_HEIGHT 20.0f
#define LESSON40_SHADOW_NEAR 0.1f
#define LESSON40_SHADOW_FAR 50.0f
#define LESSON40_SHADOW_BIAS_CONST 2.0f
#define LESSON40_SHADOW_BIAS_SLOPE 2.0f
#define LESSON40_GRID_HALF_SIZE 20.0f
#define LESSON40_GRID_SPACING 1.0f
#define LESSON40_GRID_LINE_WIDTH 0.02f
#define LESSON40_GRID_FADE_DISTANCE 30.0f
#define LESSON40_AMBIENT 0.15f
#define LESSON40_SHININESS 32.0f
#define LESSON40_SPECULAR_STRENGTH 0.5f
#define LESSON40_LIGHT_INTENSITY 1.2f

typedef struct Lesson40SceneVertex
{
    float position[3];
    float normal[3];
} Lesson40SceneVertex;

typedef struct Lesson40SceneVertUniforms
{
    Mat4 mvp;
    Mat4 model;
    Mat4 light_vp;
} Lesson40SceneVertUniforms;

typedef struct Lesson40SceneFragUniforms
{
    float base_color[4];
    float eye_pos[3];
    float ambient;
    float light_dir[4];
    float light_color[3];
    float light_intensity;
    float shininess;
    float specular_strength;
    float pad0[2];
} Lesson40SceneFragUniforms;

typedef struct Lesson40ShadowUniforms
{
    Mat4 light_vp;
} Lesson40ShadowUniforms;

typedef struct Lesson40State
{
    SDL_GPUGraphicsPipeline *scene_pipeline;
    SDL_GPUGraphicsPipeline *shadow_pipeline;
    SDL_GPUGraphicsPipeline *grid_pipeline;
    SDL_GPUGraphicsPipeline *sky_pipeline;
    SDL_GPUTexture *shadow_depth;
    SDL_GPUTexture *main_depth;
    Uint32 main_depth_width;
    Uint32 main_depth_height;
    SDL_GPUSampler *shadow_sampler;
    SDL_GPUBuffer *sphere_vertex_buffer;
    SDL_GPUBuffer *sphere_index_buffer;
    SDL_GPUBuffer *torus_vertex_buffer;
    SDL_GPUBuffer *torus_index_buffer;
    SDL_GPUBuffer *grid_vertex_buffer;
    SDL_GPUBuffer *grid_index_buffer;
    Uint32 sphere_index_count;
    Uint32 torus_index_count;
    Mat4 light_vp;
    bool shadow_pass_rendered;
    bool main_pass_rendered;
} Lesson40State;

static const float kLesson40Red[4] = { 0.9f, 0.2f, 0.2f, 1.0f };
static const float kLesson40Green[4] = { 0.2f, 0.8f, 0.3f, 1.0f };
static const float kLesson40Blue[4] = { 0.2f, 0.3f, 0.9f, 1.0f };
static const float kLesson40Gold[4] = { 0.9f, 0.7f, 0.2f, 1.0f };
static const float kLesson40GridLineColor[4] = { 0.4f, 0.4f, 0.5f, 1.0f };
static const float kLesson40GridBgColor[4] = { 0.08f, 0.08f, 0.1f, 1.0f };

static_assert(sizeof(Lesson40SceneVertex) == 24, "lesson 40 scene vertex size must match ForgeSceneVertex");
static_assert(sizeof(Lesson40SceneVertUniforms) == 192, "lesson 40 scene vertex uniform size must match HLSL layout");
static_assert(sizeof(Lesson40SceneFragUniforms) == 80, "lesson 40 scene fragment uniform size must match HLSL layout");
static_assert(sizeof(Lesson40ShadowUniforms) == 64, "lesson 40 shadow uniform size must match HLSL layout");

static Lesson40State *lesson40_state(ForgeGpuDemo *demo)
{
    return (Lesson40State *)demo->lesson.private_state;
}

static Vec3 lesson40_light_dir(void)
{
    return vec3_normalize({ 0.4f, 0.8f, 0.6f });
}

static Mat4 lesson40_light_view_projection(void)
{
    const Vec3 light_dir = lesson40_light_dir();
    const Vec3 light_pos = vec3_scale(light_dir, LESSON40_SHADOW_HEIGHT);
    Vec3 up = { 0.0f, 1.0f, 0.0f };

    if (SDL_fabsf(vec3_dot(light_dir, up)) > 0.999f) {
        up = { 1.0f, 0.0f, 0.0f };
    }
    return mat4_multiply(
        mat4_orthographic(
            -LESSON40_SHADOW_ORTHO_SIZE,
            LESSON40_SHADOW_ORTHO_SIZE,
            -LESSON40_SHADOW_ORTHO_SIZE,
            LESSON40_SHADOW_ORTHO_SIZE,
            LESSON40_SHADOW_NEAR,
            LESSON40_SHADOW_FAR),
        mat4_look_at(light_pos, { 0.0f, 0.0f, 0.0f }, up));
}

static void lesson40_release_shader(SDL_GPUDevice *device, SDL_GPUShader **shader)
{
    if (*shader) {
        SDL_ReleaseGPUShader(device, *shader);
        *shader = nullptr;
    }
}

static void lesson40_fill_scene_vertex_input(
    SDL_GPUVertexBufferDescription *vertex_buffer,
    SDL_GPUVertexAttribute attributes[2])
{
    SDL_zero(*vertex_buffer);
    vertex_buffer->slot = 0;
    vertex_buffer->pitch = sizeof(Lesson40SceneVertex);
    vertex_buffer->input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

    SDL_memset(attributes, 0, 2 * sizeof(*attributes));
    attributes[0].location = 0;
    attributes[0].buffer_slot = 0;
    attributes[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    attributes[0].offset = offsetof(Lesson40SceneVertex, position);
    attributes[1].location = 1;
    attributes[1].buffer_slot = 0;
    attributes[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    attributes[1].offset = offsetof(Lesson40SceneVertex, normal);
}

static void lesson40_fill_grid_vertex_input(
    SDL_GPUVertexBufferDescription *vertex_buffer,
    SDL_GPUVertexAttribute *attribute)
{
    SDL_zero(*vertex_buffer);
    vertex_buffer->slot = 0;
    vertex_buffer->pitch = sizeof(GridVertex);
    vertex_buffer->input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

    SDL_zero(*attribute);
    attribute->location = 0;
    attribute->buffer_slot = 0;
    attribute->format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    attribute->offset = 0;
}

static bool lesson40_upload_shape(
    ForgeGpuDemo *demo,
    const ForgeGpuShapeMesh *shape,
    SDL_GPUBuffer **vertex_buffer,
    SDL_GPUBuffer **index_buffer,
    Uint32 *index_count)
{
    Lesson40SceneVertex *vertices = nullptr;
    bool ok = false;

    if (!shape || !shape->positions || !shape->normals || !shape->indices ||
        shape->vertex_count <= 0 || shape->index_count <= 0) {
        SDL_SetError("lesson 40 shape is empty");
        return false;
    }

    vertices = (Lesson40SceneVertex *)SDL_calloc((size_t)shape->vertex_count, sizeof(*vertices));
    if (!vertices) {
        SDL_OutOfMemory();
        return false;
    }

    for (int i = 0; i < shape->vertex_count; i += 1) {
        vertices[i].position[0] = shape->positions[i * 3 + 0];
        vertices[i].position[1] = shape->positions[i * 3 + 1];
        vertices[i].position[2] = shape->positions[i * 3 + 2];
        vertices[i].normal[0] = shape->normals[i * 3 + 0];
        vertices[i].normal[1] = shape->normals[i * 3 + 1];
        vertices[i].normal[2] = shape->normals[i * 3 + 2];
    }

    *vertex_buffer = ForgeGpuCreateBufferWithData(
        demo->device,
        SDL_GPU_BUFFERUSAGE_VERTEX,
        vertices,
        (Uint32)((size_t)shape->vertex_count * sizeof(*vertices)));
    *index_buffer = ForgeGpuCreateBufferWithData(
        demo->device,
        SDL_GPU_BUFFERUSAGE_INDEX,
        shape->indices,
        (Uint32)((size_t)shape->index_count * sizeof(*shape->indices)));

    if (*vertex_buffer && *index_buffer) {
        *index_count = (Uint32)shape->index_count;
        ok = true;
    } else {
        if (*vertex_buffer) {
            SDL_ReleaseGPUBuffer(demo->device, *vertex_buffer);
            *vertex_buffer = nullptr;
        }
        if (*index_buffer) {
            SDL_ReleaseGPUBuffer(demo->device, *index_buffer);
            *index_buffer = nullptr;
        }
        *index_count = 0;
    }

    SDL_free(vertices);
    return ok;
}

static bool lesson40_create_geometry(ForgeGpuDemo *demo)
{
    Lesson40State *state = lesson40_state(demo);
    ForgeGpuShapeMesh sphere;
    ForgeGpuShapeMesh torus;
    bool ok = false;

    SDL_zero(sphere);
    SDL_zero(torus);

    if (!ForgeGpuCreateSphereShapeMesh(LESSON40_SPHERE_SLICES, LESSON40_SPHERE_STACKS, &sphere)) {
        goto done;
    }
    if (!lesson40_upload_shape(
            demo,
            &sphere,
            &state->sphere_vertex_buffer,
            &state->sphere_index_buffer,
            &state->sphere_index_count)) {
        goto done;
    }

    if (!ForgeGpuCreateTorusShapeMesh(
        LESSON40_TORUS_SLICES,
        LESSON40_TORUS_STACKS,
        LESSON40_TORUS_RING_R,
        LESSON40_TORUS_TUBE_R,
        &torus)) {
        goto done;
    }
    if (!lesson40_upload_shape(
            demo,
            &torus,
            &state->torus_vertex_buffer,
            &state->torus_index_buffer,
            &state->torus_index_count)) {
        goto done;
    }

    if (!ForgeGpuCreateShadowedGridBuffers(
            demo->device,
            LESSON40_GRID_HALF_SIZE,
            0.0f,
            &state->grid_vertex_buffer,
            &state->grid_index_buffer)) {
        goto done;
    }

    ok = true;

done:
    ForgeGpuFreeShapeMesh(&torus);
    ForgeGpuFreeShapeMesh(&sphere);
    return ok;
}

static bool lesson40_create_pipelines(ForgeGpuDemo *demo)
{
    Lesson40State *state = lesson40_state(demo);
    SDL_GPUShader *scene_vs = nullptr;
    SDL_GPUShader *scene_fs = nullptr;
    SDL_GPUShader *shadow_vs = nullptr;
    SDL_GPUShader *shadow_fs = nullptr;
    SDL_GPUShader *grid_vs = nullptr;
    SDL_GPUShader *grid_fs = nullptr;
    SDL_GPUShader *sky_vs = nullptr;
    SDL_GPUShader *sky_fs = nullptr;
    SDL_GPUColorTargetDescription color_target;
    SDL_GPUVertexBufferDescription scene_vertex_buffer;
    SDL_GPUVertexAttribute scene_attributes[2];
    SDL_GPUVertexBufferDescription grid_vertex_buffer;
    SDL_GPUVertexAttribute grid_attribute;
    bool ok = false;

    scene_vs = ForgeGpuCreateShader(demo->device, SDL_GPU_SHADERSTAGE_VERTEX,
        lesson40_scene_vert_wgsl, lesson40_scene_vert_wgsl_size,
        lesson40_scene_vert_msl, lesson40_scene_vert_msl_size,
        0, 0, 0, 1);
    scene_fs = ForgeGpuCreateShaderWithResourceLayout(demo->device,
        lesson40_scene_frag_wgsl, lesson40_scene_frag_wgsl_size,
        lesson40_scene_frag_msl, lesson40_scene_frag_msl_size,
        ForgeGpuShaderLayout_lesson40_scene_frag());
    shadow_vs = ForgeGpuCreateShader(demo->device, SDL_GPU_SHADERSTAGE_VERTEX,
        forge_scene_shadow_vert_wgsl, forge_scene_shadow_vert_wgsl_size,
        forge_scene_shadow_vert_msl, forge_scene_shadow_vert_msl_size,
        0, 0, 0, 1);
    shadow_fs = ForgeGpuCreateShader(demo->device, SDL_GPU_SHADERSTAGE_FRAGMENT,
        forge_scene_shadow_frag_wgsl, forge_scene_shadow_frag_wgsl_size,
        forge_scene_shadow_frag_msl, forge_scene_shadow_frag_msl_size,
        0, 0, 0, 0);
    grid_vs = ForgeGpuCreateShader(demo->device, SDL_GPU_SHADERSTAGE_VERTEX,
        forge_scene_grid_vert_wgsl, forge_scene_grid_vert_wgsl_size,
        forge_scene_grid_vert_msl, forge_scene_grid_vert_msl_size,
        0, 0, 0, 1);
    grid_fs = ForgeGpuCreateShaderWithResourceLayout(demo->device,
        forge_scene_grid_frag_wgsl, forge_scene_grid_frag_wgsl_size,
        forge_scene_grid_frag_msl, forge_scene_grid_frag_msl_size,
        ForgeGpuShaderLayout_forge_scene_grid_frag());
    sky_vs = ForgeGpuCreateShader(demo->device, SDL_GPU_SHADERSTAGE_VERTEX,
        forge_scene_sky_vert_wgsl, forge_scene_sky_vert_wgsl_size,
        forge_scene_sky_vert_msl, forge_scene_sky_vert_msl_size,
        0, 0, 0, 0);
    sky_fs = ForgeGpuCreateShader(demo->device, SDL_GPU_SHADERSTAGE_FRAGMENT,
        forge_scene_sky_frag_wgsl, forge_scene_sky_frag_wgsl_size,
        forge_scene_sky_frag_msl, forge_scene_sky_frag_msl_size,
        0, 0, 0, 0);
    if (!scene_vs || !scene_fs || !shadow_vs || !shadow_fs ||
        !grid_vs || !grid_fs || !sky_vs || !sky_fs) {
        goto done;
    }

    SDL_zero(color_target);
    color_target.format = demo->color_format;

    lesson40_fill_scene_vertex_input(&scene_vertex_buffer, scene_attributes);

    state->scene_pipeline = ForgeGpuCreateLessonGraphicsPipelineWithColorTargetsAndDepthCompare(
        demo, scene_vs, scene_fs, SDL_GPU_PRIMITIVETYPE_TRIANGLELIST, &color_target, 1,
        &scene_vertex_buffer, 1, scene_attributes, 2,
        true, LESSON40_DEPTH_FORMAT, true, true, SDL_GPU_COMPAREOP_LESS,
        SDL_GPU_CULLMODE_BACK, 0.0f, 0.0f);
    state->shadow_pipeline = ForgeGpuCreateLessonGraphicsPipelineWithColorTargetsAndDepthCompare(
        demo, shadow_vs, shadow_fs, SDL_GPU_PRIMITIVETYPE_TRIANGLELIST, nullptr, 0,
        &scene_vertex_buffer, 1, scene_attributes, 1,
        true, LESSON40_DEPTH_FORMAT, true, true, SDL_GPU_COMPAREOP_LESS,
        SDL_GPU_CULLMODE_NONE, LESSON40_SHADOW_BIAS_CONST, LESSON40_SHADOW_BIAS_SLOPE);

    lesson40_fill_grid_vertex_input(&grid_vertex_buffer, &grid_attribute);
    color_target.blend_state.enable_blend = true;
    color_target.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    color_target.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    color_target.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
    color_target.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    color_target.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    color_target.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    state->grid_pipeline = ForgeGpuCreateLessonGraphicsPipelineWithColorTargetsAndDepthCompare(
        demo, grid_vs, grid_fs, SDL_GPU_PRIMITIVETYPE_TRIANGLELIST, &color_target, 1,
        &grid_vertex_buffer, 1, &grid_attribute, 1,
        true, LESSON40_DEPTH_FORMAT, true, true, SDL_GPU_COMPAREOP_LESS_OR_EQUAL,
        SDL_GPU_CULLMODE_NONE, 0.0f, 0.0f);

    SDL_zero(color_target);
    color_target.format = demo->color_format;
    state->sky_pipeline = ForgeGpuCreateLessonGraphicsPipelineWithColorTargetsAndDepthCompare(
        demo, sky_vs, sky_fs, SDL_GPU_PRIMITIVETYPE_TRIANGLELIST, &color_target, 1,
        nullptr, 0, nullptr, 0,
        true, LESSON40_DEPTH_FORMAT, true, false, SDL_GPU_COMPAREOP_LESS_OR_EQUAL,
        SDL_GPU_CULLMODE_NONE, 0.0f, 0.0f);

    ok = state->scene_pipeline && state->shadow_pipeline && state->grid_pipeline && state->sky_pipeline;

done:
    lesson40_release_shader(demo->device, &sky_fs);
    lesson40_release_shader(demo->device, &sky_vs);
    lesson40_release_shader(demo->device, &grid_fs);
    lesson40_release_shader(demo->device, &grid_vs);
    lesson40_release_shader(demo->device, &shadow_fs);
    lesson40_release_shader(demo->device, &shadow_vs);
    lesson40_release_shader(demo->device, &scene_fs);
    lesson40_release_shader(demo->device, &scene_vs);
    return ok;
}

static void lesson40_bind_indexed_mesh(
    SDL_GPURenderPass *render_pass,
    SDL_GPUBuffer *vertex_buffer,
    SDL_GPUBuffer *index_buffer)
{
    SDL_GPUBufferBinding vertex_binding;
    SDL_GPUBufferBinding index_binding;

    SDL_zero(vertex_binding);
    vertex_binding.buffer = vertex_buffer;
    SDL_BindGPUVertexBuffers(render_pass, 0, &vertex_binding, 1);

    SDL_zero(index_binding);
    index_binding.buffer = index_buffer;
    SDL_BindGPUIndexBuffer(render_pass, &index_binding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
}

static void lesson40_draw_shadow_mesh(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    SDL_GPUBuffer *vertex_buffer,
    SDL_GPUBuffer *index_buffer,
    Uint32 index_count,
    Mat4 model)
{
    Lesson40State *state = lesson40_state(demo);
    Lesson40ShadowUniforms uniforms;

    uniforms.light_vp = mat4_multiply(state->light_vp, model);
    SDL_BindGPUGraphicsPipeline(render_pass, state->shadow_pipeline);
    SDL_PushGPUVertexUniformData(command_buffer, 0, &uniforms, sizeof(uniforms));
    lesson40_bind_indexed_mesh(render_pass, vertex_buffer, index_buffer);
    SDL_DrawGPUIndexedPrimitives(render_pass, index_count, 1, 0, 0, 0);
}

static void lesson40_draw_scene_mesh(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    SDL_GPUBuffer *vertex_buffer,
    SDL_GPUBuffer *index_buffer,
    Uint32 index_count,
    Mat4 camera_vp,
    Mat4 model,
    const float color[4])
{
    Lesson40State *state = lesson40_state(demo);
    const Vec3 light_dir = lesson40_light_dir();
    Lesson40SceneVertUniforms vertex_uniforms;
    Lesson40SceneFragUniforms fragment_uniforms;
    SDL_GPUTextureSamplerBinding shadow_binding;

    vertex_uniforms.mvp = mat4_multiply(camera_vp, model);
    vertex_uniforms.model = model;
    vertex_uniforms.light_vp = mat4_multiply(state->light_vp, model);
    SDL_PushGPUVertexUniformData(command_buffer, 0, &vertex_uniforms, sizeof(vertex_uniforms));

    SDL_zero(fragment_uniforms);
    SDL_memcpy(fragment_uniforms.base_color, color, sizeof(fragment_uniforms.base_color));
    fragment_uniforms.eye_pos[0] = demo->lesson.camera_position.x;
    fragment_uniforms.eye_pos[1] = demo->lesson.camera_position.y;
    fragment_uniforms.eye_pos[2] = demo->lesson.camera_position.z;
    fragment_uniforms.ambient = LESSON40_AMBIENT;
    fragment_uniforms.light_dir[0] = light_dir.x;
    fragment_uniforms.light_dir[1] = light_dir.y;
    fragment_uniforms.light_dir[2] = light_dir.z;
    fragment_uniforms.light_color[0] = 1.0f;
    fragment_uniforms.light_color[1] = 0.95f;
    fragment_uniforms.light_color[2] = 0.9f;
    fragment_uniforms.light_intensity = LESSON40_LIGHT_INTENSITY;
    fragment_uniforms.shininess = LESSON40_SHININESS;
    fragment_uniforms.specular_strength = LESSON40_SPECULAR_STRENGTH;
    SDL_PushGPUFragmentUniformData(command_buffer, 0, &fragment_uniforms, sizeof(fragment_uniforms));

    SDL_zero(shadow_binding);
    shadow_binding.texture = state->shadow_depth;
    shadow_binding.sampler = state->shadow_sampler;
    SDL_BindGPUFragmentSamplers(render_pass, 0, &shadow_binding, 1);

    SDL_BindGPUGraphicsPipeline(render_pass, state->scene_pipeline);
    lesson40_bind_indexed_mesh(render_pass, vertex_buffer, index_buffer);
    SDL_DrawGPUIndexedPrimitives(render_pass, index_count, 1, 0, 0, 0);
}

static void lesson40_draw_scene(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    Mat4 camera_vp,
    const Mat4 models[4])
{
    Lesson40State *state = lesson40_state(demo);
    ForgeGpuShadowedGridDrawInfo grid_info;

    SDL_BindGPUGraphicsPipeline(render_pass, state->sky_pipeline);
    SDL_DrawGPUPrimitives(render_pass, 3, 1, 0, 0);

    lesson40_draw_scene_mesh(
        demo, command_buffer, render_pass,
        state->sphere_vertex_buffer, state->sphere_index_buffer, state->sphere_index_count,
        camera_vp, models[0], kLesson40Red);
    lesson40_draw_scene_mesh(
        demo, command_buffer, render_pass,
        state->sphere_vertex_buffer, state->sphere_index_buffer, state->sphere_index_count,
        camera_vp, models[1], kLesson40Green);
    lesson40_draw_scene_mesh(
        demo, command_buffer, render_pass,
        state->sphere_vertex_buffer, state->sphere_index_buffer, state->sphere_index_count,
        camera_vp, models[2], kLesson40Blue);
    lesson40_draw_scene_mesh(
        demo, command_buffer, render_pass,
        state->torus_vertex_buffer, state->torus_index_buffer, state->torus_index_count,
        camera_vp, models[3], kLesson40Gold);

    SDL_zero(grid_info);
    grid_info.vp = camera_vp;
    grid_info.light_vp = state->light_vp;
    grid_info.light_dir = lesson40_light_dir();
    grid_info.eye_pos = demo->lesson.camera_position;
    grid_info.light_intensity = LESSON40_LIGHT_INTENSITY;
    SDL_memcpy(grid_info.line_color, kLesson40GridLineColor, sizeof(grid_info.line_color));
    SDL_memcpy(grid_info.bg_color, kLesson40GridBgColor, sizeof(grid_info.bg_color));
    grid_info.grid_spacing = LESSON40_GRID_SPACING;
    grid_info.line_width = LESSON40_GRID_LINE_WIDTH;
    grid_info.fade_distance = LESSON40_GRID_FADE_DISTANCE;
    grid_info.ambient = LESSON40_AMBIENT;
    grid_info.shadow_depth = state->shadow_depth;
    grid_info.shadow_sampler = state->shadow_sampler;
    ForgeGpuDrawShadowedGrid(
        command_buffer,
        render_pass,
        state->grid_pipeline,
        state->grid_vertex_buffer,
        state->grid_index_buffer,
        &grid_info);
}

bool ForgeGpuCreateLesson40(ForgeGpuDemo *demo)
{
    Lesson40State *state;

    if (!SDL_GPUTextureSupportsFormat(
            demo->device,
            LESSON40_DEPTH_FORMAT,
            SDL_GPU_TEXTURETYPE_2D,
            SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER)) {
        SDL_SetError("lesson 40 requires sampled D32_FLOAT depth textures");
        return false;
    }

    state = (Lesson40State *)SDL_calloc(1, sizeof(*state));
    if (!state) {
        SDL_OutOfMemory();
        return false;
    }
    demo->lesson.private_state = state;

    state->light_vp = lesson40_light_view_projection();
    state->shadow_depth = ForgeGpuCreateSampledDepthTexture(
        demo,
        LESSON40_SHADOW_MAP_SIZE,
        LESSON40_SHADOW_MAP_SIZE,
        LESSON40_DEPTH_FORMAT);
    state->shadow_sampler = ForgeGpuCreateSamplerWithAddress(
        demo->device,
        SDL_GPU_FILTER_NEAREST,
        SDL_GPU_FILTER_NEAREST,
        SDL_GPU_SAMPLERMIPMAPMODE_NEAREST,
        SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        0.0f);
    if (!state->shadow_depth || !state->shadow_sampler) {
        return false;
    }
    if (!lesson40_create_geometry(demo)) {
        return false;
    }
    if (!lesson40_create_pipelines(demo)) {
        return false;
    }

    demo->lesson.camera_position = { 0.0f, LESSON40_CAM_START_Y, LESSON40_CAM_START_Z };
    demo->lesson.camera_yaw = 0.0f;
    demo->lesson.camera_pitch = LESSON40_CAM_START_PITCH;
    demo->lesson.move_speed = LESSON40_MOVE_SPEED;
    demo->lesson.mouse_sensitivity = LESSON40_MOUSE_SENSITIVITY;
    demo->lesson.pitch_clamp = LESSON40_PITCH_CLAMP;
    demo->lesson.last_ticks = SDL_GetTicks();

    return true;
}

bool ForgeGpuRenderLesson40(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPUTexture *swapchain_texture,
    Uint32 width,
    Uint32 height)
{
    Lesson40State *state = lesson40_state(demo);
    Mat4 view;
    Mat4 projection;
    Mat4 camera_vp;
    Mat4 models[4];
    SDL_GPURenderPass *render_pass;
    const ForgeGpuColorTargetAttachment color_target = {
        swapchain_texture,
        { 0.15f, 0.15f, 0.20f, 1.0f }
    };

    if (!state) {
        SDL_SetError("lesson 40 internal state is missing");
        return false;
    }

    if (!ForgeGpuEnsureSampledDepthTarget(
            demo,
            &state->main_depth,
            &state->main_depth_width,
            &state->main_depth_height,
            width,
            height,
            LESSON40_DEPTH_FORMAT)) {
        return false;
    }

    ForgeGpuUpdateCameraFromInput(demo);
    ForgeGpuCameraViewProjection(demo, width, height, LESSON40_FAR_PLANE, &view, &projection);
    camera_vp = mat4_multiply(projection, view);

    models[0] = mat4_translate({ -LESSON40_SPHERE_SPACING, LESSON40_SPHERE_HEIGHT, 0.0f });
    models[1] = mat4_translate({ 0.0f, LESSON40_SPHERE_HEIGHT, 0.0f });
    models[2] = mat4_translate({ LESSON40_SPHERE_SPACING, LESSON40_SPHERE_HEIGHT, 0.0f });
    models[3] = mat4_multiply(
        mat4_translate({ 0.0f, LESSON40_TORUS_HEIGHT, 0.0f }),
        mat4_rotate_y(ForgeGpuFrameTimeSeconds(demo) * LESSON40_TORUS_ROT_SPEED));

    state->shadow_pass_rendered = false;
    state->main_pass_rendered = false;

    render_pass = ForgeGpuBeginDepthOnlyPass(command_buffer, state->shadow_depth, 1.0f);
    if (!render_pass) {
        return false;
    }
    for (int i = 0; i < 3; i += 1) {
        lesson40_draw_shadow_mesh(
            demo,
            command_buffer,
            render_pass,
            state->sphere_vertex_buffer,
            state->sphere_index_buffer,
            state->sphere_index_count,
            models[i]);
    }
    lesson40_draw_shadow_mesh(
        demo,
        command_buffer,
        render_pass,
        state->torus_vertex_buffer,
        state->torus_index_buffer,
        state->torus_index_count,
        models[3]);
    SDL_EndGPURenderPass(render_pass);
    state->shadow_pass_rendered = true;

    render_pass = ForgeGpuBeginColorDepthPass(command_buffer, &color_target, 1, state->main_depth, 1.0f);
    if (!render_pass) {
        return false;
    }
    lesson40_draw_scene(demo, command_buffer, render_pass, camera_vp, models);
    SDL_EndGPURenderPass(render_pass);
    state->main_pass_rendered = true;

    return true;
}

void ForgeGpuDebugLesson40(ForgeGpuDemo *demo)
{
    Lesson40State *state = lesson40_state(demo);

    if (!state) {
        return;
    }

    ImGui::Text("Scene renderer: sky, 3 spheres, torus, grid");
    ImGui::Text("Shadow map: %ux%u D32", LESSON40_SHADOW_MAP_SIZE, LESSON40_SHADOW_MAP_SIZE);
    ImGui::Text("Sphere indices: %u", state->sphere_index_count);
    ImGui::Text("Torus indices: %u", state->torus_index_count);
    ImGui::Text("Passes: shadow %s, main %s",
        state->shadow_pass_rendered ? "yes" : "no",
        state->main_pass_rendered ? "yes" : "no");
}

void ForgeGpuExportLesson40Metrics(ForgeGpuDemo *demo)
{
    Lesson40State *state = lesson40_state(demo);

    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson40SceneRenderer", state ? 1.0 : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson40ShadowPass", state && state->shadow_pass_rendered ? 1.0 : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson40MainPass", state && state->main_pass_rendered ? 1.0 : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson40SphereIndices", state ? (double)state->sphere_index_count : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson40TorusIndices", state ? (double)state->torus_index_count : 0.0);
}

void ForgeGpuDestroyLesson40(ForgeGpuDemo *demo)
{
    Lesson40State *state = lesson40_state(demo);

    if (!state) {
        return;
    }

    if (state->scene_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, state->scene_pipeline);
    }
    if (state->shadow_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, state->shadow_pipeline);
    }
    if (state->grid_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, state->grid_pipeline);
    }
    if (state->sky_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, state->sky_pipeline);
    }
    if (state->shadow_depth) {
        SDL_ReleaseGPUTexture(demo->device, state->shadow_depth);
    }
    if (state->main_depth) {
        SDL_ReleaseGPUTexture(demo->device, state->main_depth);
    }
    if (state->shadow_sampler) {
        SDL_ReleaseGPUSampler(demo->device, state->shadow_sampler);
    }
    if (state->sphere_vertex_buffer) {
        SDL_ReleaseGPUBuffer(demo->device, state->sphere_vertex_buffer);
    }
    if (state->sphere_index_buffer) {
        SDL_ReleaseGPUBuffer(demo->device, state->sphere_index_buffer);
    }
    if (state->torus_vertex_buffer) {
        SDL_ReleaseGPUBuffer(demo->device, state->torus_vertex_buffer);
    }
    if (state->torus_index_buffer) {
        SDL_ReleaseGPUBuffer(demo->device, state->torus_index_buffer);
    }
    if (state->grid_vertex_buffer) {
        SDL_ReleaseGPUBuffer(demo->device, state->grid_vertex_buffer);
    }
    if (state->grid_index_buffer) {
        SDL_ReleaseGPUBuffer(demo->device, state->grid_index_buffer);
    }
    SDL_free(state);
    demo->lesson.private_state = nullptr;
}
