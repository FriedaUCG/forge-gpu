#include "forge_gpu_lessons.h"

#include "forge_gpu_browser_status.h"
#include "forge_gpu_camera.h"
#include "forge_gpu_forward_scene.h"
#include "forge_gpu_gpu_helpers.h"
#include "forge_gpu_lesson_common.h"
#include "forge_gpu_math.h"
#include "forge_gpu_shader_layouts.h"
#include "shaders/generated/forge_gpu_lesson_32_shaders.h"
#include "imgui.h"

#include <stddef.h>

#define LESSON32_DEPTH_FORMAT SDL_GPU_TEXTUREFORMAT_D32_FLOAT
#define LESSON32_SHADOW_MAP_SIZE 2048u
#define LESSON32_FAR_PLANE 100.0f
#define LESSON32_CAMERA_SPEED 3.0f
#define LESSON32_MOUSE_SENSITIVITY 0.003f
#define LESSON32_PITCH_CLAMP 1.5f
#define LESSON32_CAMERA_START_X 1.5f
#define LESSON32_CAMERA_START_Y 1.0f
#define LESSON32_CAMERA_START_Z -2.5f
#define LESSON32_CAMERA_START_YAW_DEG 180.0f
#define LESSON32_CAMERA_START_PITCH_DEG -5.0f
#define LESSON32_LIGHT_DIR_X 0.4f
#define LESSON32_LIGHT_DIR_Y -0.8f
#define LESSON32_LIGHT_DIR_Z 0.4f
#define LESSON32_LIGHT_INTENSITY 0.9f
#define LESSON32_LIGHT_COLOR_R 1.0f
#define LESSON32_LIGHT_COLOR_G 0.95f
#define LESSON32_LIGHT_COLOR_B 0.85f
#define LESSON32_MATERIAL_AMBIENT 0.25f
#define LESSON32_MATERIAL_SHININESS 32.0f
#define LESSON32_MATERIAL_SPECULAR_STRENGTH 0.4f
#define LESSON32_WALK_RADIUS 1.5f
#define LESSON32_WALK_SPEED 0.8f
#define LESSON32_SHADOW_ORTHO_SIZE 5.0f
#define LESSON32_SHADOW_NEAR 0.1f
#define LESSON32_SHADOW_FAR 20.0f
#define LESSON32_LIGHT_DISTANCE 8.0f
#define LESSON32_LIGHT_TARGET_Y 0.8f
#define LESSON32_SHADOW_DEPTH_BIAS 2.0f
#define LESSON32_SHADOW_SLOPE_BIAS 2.0f
#define LESSON32_PARALLEL_THRESHOLD 0.99f
#define LESSON32_GRID_HALF_SIZE 20.0f
#define LESSON32_GRID_Y 0.0f
#define LESSON32_GRID_SPACING 1.0f
#define LESSON32_GRID_LINE_WIDTH 0.02f
#define LESSON32_GRID_FADE_DISTANCE 30.0f
#define LESSON32_GRID_AMBIENT 0.3f
#define LESSON32_GRID_LINE_GRAY 0.4f
#define LESSON32_GRID_BG_GRAY 0.25f
#define LESSON32_CLEAR_R 0.6f
#define LESSON32_CLEAR_G 0.7f
#define LESSON32_CLEAR_B 0.8f
#define LESSON32_ANIMATION_SPEED 1.0f
#define LESSON32_MAX_JOINTS 19
#define LESSON32_MAX_ANISOTROPY 8.0f
#define LESSON32_MAX_LOD_UNLIMITED 1000.0f

struct Lesson32SkinVertex
{
    float position[3];
    float normal[3];
    float uv[2];
    Uint16 joints[4];
    float weights[4];
};

struct Lesson32SkinVertUniforms
{
    Mat4 mvp;
    Mat4 model;
    Mat4 light_vp;
};

struct Lesson32JointUniforms
{
    ForgeGpuMat4 joints[LESSON32_MAX_JOINTS];
};

struct Lesson32ShadowVertUniforms
{
    Mat4 light_vp;
};

struct Lesson32SceneFragUniforms
{
    float base_color[4];
    float eye_pos[3];
    float has_texture;
    float ambient;
    float shininess;
    float specular_str;
    float pad0;
    float light_dir[4];
    float light_color[3];
    float light_intensity;
};

struct Lesson32State
{
    ForgeGpuLoadedScene scene;
    GpuPrimitive *primitives;
    int primitive_count;
    GpuMaterial *materials;
    int material_count;
    SDL_GPUGraphicsPipeline *skin_pipeline;
    SDL_GPUGraphicsPipeline *shadow_pipeline;
    SDL_GPUGraphicsPipeline *grid_pipeline;
    SDL_GPUTexture *shadow_depth;
    SDL_GPUTexture *main_depth;
    SDL_GPUBuffer *grid_vertex_buffer;
    SDL_GPUBuffer *grid_index_buffer;
    Uint32 main_depth_width;
    Uint32 main_depth_height;
    Mat4 light_vp;
    Mat4 mesh_world;
    Lesson32JointUniforms joint_uniforms;
    int mesh_node_index;
    int joint_matrix_count;
    float anim_time;
    float walk_angle;
    Uint64 last_animation_counter;
    bool skin_data_ready;
    bool animation_applied;
    bool shadow_pass_rendered;
    bool main_pass_rendered;
};

static_assert(sizeof(Lesson32SkinVertex) == 56, "lesson 32 skinned vertex size must match HLSL layout");
static_assert(sizeof(Lesson32SkinVertUniforms) == 192, "lesson 32 skinned vertex uniform size must match HLSL layout");
static_assert(sizeof(Lesson32JointUniforms) == 1216, "lesson 32 joint uniform size must match HLSL layout");
static_assert(sizeof(Lesson32ShadowVertUniforms) == 64, "lesson 32 shadow vertex uniform size must match HLSL layout");
static_assert(sizeof(Lesson32SceneFragUniforms) == 80, "lesson 32 scene fragment uniform size must match HLSL layout");

static Lesson32State *lesson32_state(ForgeGpuDemo *demo)
{
    return (Lesson32State *)demo->lesson.private_state;
}

static void lesson32_init_camera(ForgeGpuDemo *demo)
{
    demo->lesson.camera_position = {
        LESSON32_CAMERA_START_X,
        LESSON32_CAMERA_START_Y,
        LESSON32_CAMERA_START_Z
    };
    demo->lesson.camera_yaw = LESSON32_CAMERA_START_YAW_DEG * FORGE_GPU_DEG2RAD;
    demo->lesson.camera_pitch = LESSON32_CAMERA_START_PITCH_DEG * FORGE_GPU_DEG2RAD;
    demo->lesson.pitch_clamp = LESSON32_PITCH_CLAMP;
    demo->lesson.mouse_sensitivity = LESSON32_MOUSE_SENSITIVITY;
    demo->lesson.move_speed = LESSON32_CAMERA_SPEED;
    demo->lesson.last_ticks = SDL_GetTicks();
}

static float lesson32_delta_seconds(ForgeGpuDemo *demo, Lesson32State *state)
{
    const Uint64 now = SDL_GetPerformanceCounter();
    float dt;

    if (demo->validation_mode) {
        return 1.0f / 60.0f;
    }
    if (state->last_animation_counter == 0) {
        state->last_animation_counter = now;
        return 0.0f;
    }

    dt = (float)((double)(now - state->last_animation_counter) / (double)SDL_GetPerformanceFrequency());
    state->last_animation_counter = now;
    return SDL_min(dt, FORGE_GPU_MAX_DELTA_TIME);
}

static Mat4 lesson32_light_view_projection(void)
{
    const Vec3 target = { 0.0f, LESSON32_LIGHT_TARGET_Y, 0.0f };
    const Vec3 light_dir = { LESSON32_LIGHT_DIR_X, LESSON32_LIGHT_DIR_Y, LESSON32_LIGHT_DIR_Z };

    return ForgeGpuComputeTargetedDirectionalLightViewProjection(
        light_dir,
        LESSON32_LIGHT_DISTANCE,
        target,
        LESSON32_SHADOW_ORTHO_SIZE,
        LESSON32_SHADOW_NEAR,
        LESSON32_SHADOW_FAR,
        LESSON32_PARALLEL_THRESHOLD);
}

static int lesson32_find_skin_mesh_node(const ForgeGpuLoadedScene *scene)
{
    for (int i = 0; i < scene->node_count; i += 1) {
        if (scene->nodes[i].skin_index == 0 && scene->nodes[i].mesh_index >= 0) {
            return i;
        }
    }
    for (int i = 0; i < scene->node_count; i += 1) {
        if (scene->nodes[i].skin_index >= 0 && scene->nodes[i].mesh_index >= 0) {
            return i;
        }
    }
    return -1;
}

static bool lesson32_create_samplers(ForgeGpuDemo *demo)
{
    demo->lesson.samplers[0] = ForgeGpuCreateSamplerWithAddressAndAnisotropy(
        demo->device,
        SDL_GPU_FILTER_LINEAR,
        SDL_GPU_FILTER_LINEAR,
        SDL_GPU_SAMPLERMIPMAPMODE_LINEAR,
        SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
        LESSON32_MAX_LOD_UNLIMITED,
        LESSON32_MAX_ANISOTROPY);
    demo->lesson.samplers[1] = ForgeGpuCreateSamplerWithAddress(
        demo->device,
        SDL_GPU_FILTER_NEAREST,
        SDL_GPU_FILTER_NEAREST,
        SDL_GPU_SAMPLERMIPMAPMODE_NEAREST,
        SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        0.0f);
    return demo->lesson.samplers[0] && demo->lesson.samplers[1];
}

static bool lesson32_upload_skinned_model(ForgeGpuDemo *demo, Lesson32State *state)
{
    const ForgeGpuLoadedScene *scene = &state->scene;

    state->primitive_count = scene->primitive_count;
    if (state->primitive_count > 0) {
        state->primitives = (GpuPrimitive *)SDL_calloc((size_t)state->primitive_count, sizeof(*state->primitives));
        if (!state->primitives) {
            SDL_OutOfMemory();
            return false;
        }
    }

    for (int i = 0; i < scene->primitive_count; i += 1) {
        const ForgeGpuScenePrimitive *src = &scene->primitives[i];
        GpuPrimitive *dst = &state->primitives[i];
        Lesson32SkinVertex *vertices;
        size_t vertex_bytes_size;
        Uint32 vertex_bytes;

        if (src->vertex_count == 0 || !src->vertices || !src->has_skin_data || !src->joint_indices || !src->weights) {
            SDL_SetError("lesson 32 requires skinned primitive vertex data");
            return false;
        }
        if (!SDL_size_mul_check_overflow((size_t)src->vertex_count, sizeof(*vertices), &vertex_bytes_size) ||
            vertex_bytes_size > SDL_MAX_UINT32) {
            SDL_SetError("lesson 32 vertex allocation overflow");
            return false;
        }

        vertices = (Lesson32SkinVertex *)SDL_calloc((size_t)src->vertex_count, sizeof(*vertices));
        if (!vertices) {
            SDL_OutOfMemory();
            return false;
        }
        for (Uint32 vertex = 0; vertex < src->vertex_count; vertex += 1) {
            SDL_memcpy(vertices[vertex].position, src->vertices[vertex].position, sizeof(vertices[vertex].position));
            SDL_memcpy(vertices[vertex].normal, src->vertices[vertex].normal, sizeof(vertices[vertex].normal));
            SDL_memcpy(vertices[vertex].uv, src->vertices[vertex].uv, sizeof(vertices[vertex].uv));
            SDL_memcpy(
                vertices[vertex].joints,
                src->joint_indices + (size_t)vertex * FORGE_GPU_SCENE_JOINTS_PER_VERTEX,
                sizeof(vertices[vertex].joints));
            SDL_memcpy(
                vertices[vertex].weights,
                src->weights + (size_t)vertex * FORGE_GPU_SCENE_JOINTS_PER_VERTEX,
                sizeof(vertices[vertex].weights));
        }

        vertex_bytes = (Uint32)vertex_bytes_size;
        dst->vertex_buffer = ForgeGpuCreateBufferWithData(demo->device, SDL_GPU_BUFFERUSAGE_VERTEX, vertices, vertex_bytes);
        SDL_free(vertices);
        if (!dst->vertex_buffer) {
            return false;
        }

        dst->vertex_count = src->vertex_count;
        dst->index_count = src->index_count;
        dst->material_index = src->material_index;
        dst->index_type = src->index_stride == 4 ? SDL_GPU_INDEXELEMENTSIZE_32BIT : SDL_GPU_INDEXELEMENTSIZE_16BIT;
        if (src->indices && src->index_count > 0) {
            size_t index_bytes_size;

            if (src->index_stride != 2 && src->index_stride != 4) {
                SDL_SetError("lesson 32 unsupported index stride");
                return false;
            }
            if (!SDL_size_mul_check_overflow((size_t)src->index_count, (size_t)src->index_stride, &index_bytes_size) ||
                index_bytes_size > SDL_MAX_UINT32) {
                SDL_SetError("lesson 32 index allocation overflow");
                return false;
            }
            dst->index_buffer = ForgeGpuCreateBufferWithData(
                demo->device,
                SDL_GPU_BUFFERUSAGE_INDEX,
                src->indices,
                (Uint32)index_bytes_size);
            if (!dst->index_buffer) {
                return false;
            }
        }
    }

    state->material_count = scene->material_count;
    if (state->material_count > 0) {
        state->materials = (GpuMaterial *)SDL_calloc((size_t)state->material_count, sizeof(*state->materials));
        if (!state->materials) {
            SDL_OutOfMemory();
            return false;
        }
    }

    for (int i = 0; i < scene->material_count; i += 1) {
        const ForgeGpuSceneMaterial *src = &scene->materials[i];
        GpuMaterial *dst = &state->materials[i];

        SDL_memcpy(dst->base_color, src->base_color, sizeof(dst->base_color));
        dst->has_texture = src->has_texture;
        if (src->has_texture) {
            dst->texture = ForgeGpuLoadRgbaTexturePath(demo, src->texture_path, true);
            if (!dst->texture) {
                return false;
            }
        }
    }

    return true;
}

static bool lesson32_create_grid(ForgeGpuDemo *demo)
{
    Lesson32State *state = lesson32_state(demo);

    return ForgeGpuCreateShadowedGridBuffers(
        demo->device,
        LESSON32_GRID_HALF_SIZE,
        LESSON32_GRID_Y,
        &state->grid_vertex_buffer,
        &state->grid_index_buffer);
}

static void lesson32_fill_skin_vertex_input(SDL_GPUVertexBufferDescription *vertex_buffer, SDL_GPUVertexAttribute attributes[5])
{
    SDL_zero(*vertex_buffer);
    vertex_buffer->slot = 0;
    vertex_buffer->pitch = sizeof(Lesson32SkinVertex);
    vertex_buffer->input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

    SDL_memset(attributes, 0, 5 * sizeof(*attributes));
    attributes[0].location = 0;
    attributes[0].buffer_slot = 0;
    attributes[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    attributes[0].offset = (Uint32)offsetof(Lesson32SkinVertex, position);
    attributes[1].location = 1;
    attributes[1].buffer_slot = 0;
    attributes[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    attributes[1].offset = (Uint32)offsetof(Lesson32SkinVertex, normal);
    attributes[2].location = 2;
    attributes[2].buffer_slot = 0;
    attributes[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    attributes[2].offset = (Uint32)offsetof(Lesson32SkinVertex, uv);
    attributes[3].location = 3;
    attributes[3].buffer_slot = 0;
    attributes[3].format = SDL_GPU_VERTEXELEMENTFORMAT_USHORT4;
    attributes[3].offset = (Uint32)offsetof(Lesson32SkinVertex, joints);
    attributes[4].location = 4;
    attributes[4].buffer_slot = 0;
    attributes[4].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
    attributes[4].offset = (Uint32)offsetof(Lesson32SkinVertex, weights);
}

static bool lesson32_create_shadow_pipeline(ForgeGpuDemo *demo)
{
    Lesson32State *state = lesson32_state(demo);
    SDL_GPUShader *vertex_shader;
    SDL_GPUShader *fragment_shader;
    SDL_GPUVertexBufferDescription vertex_buffer;
    SDL_GPUVertexAttribute attributes[5];

    vertex_shader = ForgeGpuCreateShader(
        demo->device,
        SDL_GPU_SHADERSTAGE_VERTEX,
        lesson32_shadow_skin_vert_wgsl,
        lesson32_shadow_skin_vert_wgsl_size,
        lesson32_shadow_skin_vert_msl,
        lesson32_shadow_skin_vert_msl_size,
        0, 0, 0, 2);
    if (!vertex_shader) {
        return false;
    }
    fragment_shader = ForgeGpuCreateShader(
        demo->device,
        SDL_GPU_SHADERSTAGE_FRAGMENT,
        lesson32_shadow_frag_wgsl,
        lesson32_shadow_frag_wgsl_size,
        lesson32_shadow_frag_msl,
        lesson32_shadow_frag_msl_size,
        0, 0, 0, 0);
    if (!fragment_shader) {
        SDL_ReleaseGPUShader(demo->device, vertex_shader);
        return false;
    }

    lesson32_fill_skin_vertex_input(&vertex_buffer, attributes);
    state->shadow_pipeline = ForgeGpuCreateLessonGraphicsPipelineWithColorTargetsAndDepthCompare(
        demo,
        vertex_shader,
        fragment_shader,
        SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        nullptr,
        0,
        &vertex_buffer,
        1,
        attributes,
        SDL_arraysize(attributes),
        true,
        LESSON32_DEPTH_FORMAT,
        true,
        true,
        SDL_GPU_COMPAREOP_LESS_OR_EQUAL,
        SDL_GPU_CULLMODE_BACK,
        LESSON32_SHADOW_DEPTH_BIAS,
        LESSON32_SHADOW_SLOPE_BIAS);

    SDL_ReleaseGPUShader(demo->device, vertex_shader);
    SDL_ReleaseGPUShader(demo->device, fragment_shader);
    return state->shadow_pipeline != nullptr;
}

static bool lesson32_create_skin_pipeline(ForgeGpuDemo *demo)
{
    Lesson32State *state = lesson32_state(demo);
    SDL_GPUShader *vertex_shader;
    SDL_GPUShader *fragment_shader;
    SDL_GPUVertexBufferDescription vertex_buffer;
    SDL_GPUVertexAttribute attributes[5];

    vertex_shader = ForgeGpuCreateShader(
        demo->device,
        SDL_GPU_SHADERSTAGE_VERTEX,
        lesson32_skin_vert_wgsl,
        lesson32_skin_vert_wgsl_size,
        lesson32_skin_vert_msl,
        lesson32_skin_vert_msl_size,
        0, 0, 0, 2);
    if (!vertex_shader) {
        return false;
    }
    fragment_shader = ForgeGpuCreateShaderWithResourceLayout(
        demo->device,
        lesson32_skin_frag_wgsl,
        lesson32_skin_frag_wgsl_size,
        lesson32_skin_frag_msl,
        lesson32_skin_frag_msl_size,
        ForgeGpuShaderLayout_lesson32_skin_frag());
    if (!fragment_shader) {
        SDL_ReleaseGPUShader(demo->device, vertex_shader);
        return false;
    }

    lesson32_fill_skin_vertex_input(&vertex_buffer, attributes);
    state->skin_pipeline = ForgeGpuCreateLessonGraphicsPipelineWithPrimitive(
        demo,
        vertex_shader,
        fragment_shader,
        SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        &vertex_buffer,
        1,
        attributes,
        SDL_arraysize(attributes),
        1,
        true,
        LESSON32_DEPTH_FORMAT,
        true,
        true,
        SDL_GPU_CULLMODE_BACK,
        0.0f,
        0.0f);

    SDL_ReleaseGPUShader(demo->device, vertex_shader);
    SDL_ReleaseGPUShader(demo->device, fragment_shader);
    return state->skin_pipeline != nullptr;
}

static bool lesson32_create_grid_pipeline(ForgeGpuDemo *demo)
{
    Lesson32State *state = lesson32_state(demo);
    SDL_GPUShader *vertex_shader;
    SDL_GPUShader *fragment_shader;
    SDL_GPUVertexBufferDescription vertex_buffer;
    SDL_GPUVertexAttribute attribute;

    vertex_shader = ForgeGpuCreateShader(
        demo->device,
        SDL_GPU_SHADERSTAGE_VERTEX,
        lesson32_grid_vert_wgsl,
        lesson32_grid_vert_wgsl_size,
        lesson32_grid_vert_msl,
        lesson32_grid_vert_msl_size,
        0, 0, 0, 1);
    if (!vertex_shader) {
        return false;
    }
    fragment_shader = ForgeGpuCreateShaderWithResourceLayout(
        demo->device,
        lesson32_grid_frag_wgsl,
        lesson32_grid_frag_wgsl_size,
        lesson32_grid_frag_msl,
        lesson32_grid_frag_msl_size,
        ForgeGpuShaderLayout_lesson32_grid_frag());
    if (!fragment_shader) {
        SDL_ReleaseGPUShader(demo->device, vertex_shader);
        return false;
    }

    SDL_zero(vertex_buffer);
    vertex_buffer.slot = 0;
    vertex_buffer.pitch = sizeof(GridVertex);
    vertex_buffer.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    SDL_zero(attribute);
    attribute.location = 0;
    attribute.buffer_slot = 0;
    attribute.format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    attribute.offset = 0;
    state->grid_pipeline = ForgeGpuCreateLessonGraphicsPipelineWithPrimitive(
        demo,
        vertex_shader,
        fragment_shader,
        SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        &vertex_buffer,
        1,
        &attribute,
        1,
        1,
        true,
        LESSON32_DEPTH_FORMAT,
        true,
        true,
        SDL_GPU_CULLMODE_NONE,
        0.0f,
        0.0f);

    SDL_ReleaseGPUShader(demo->device, vertex_shader);
    SDL_ReleaseGPUShader(demo->device, fragment_shader);
    return state->grid_pipeline != nullptr;
}

static bool lesson32_create_pipelines(ForgeGpuDemo *demo)
{
    return lesson32_create_shadow_pipeline(demo) &&
           lesson32_create_skin_pipeline(demo) &&
           lesson32_create_grid_pipeline(demo);
}

static bool lesson32_update_animation(Lesson32State *state, float dt)
{
    ForgeGpuLoadedScene *scene = &state->scene;
    const ForgeGpuSceneAnimation *animation;
    Mat4 path_translate;
    Mat4 path_rotate;
    Mat4 path_transform;
    Mat4 base_mesh_world;
    float px;
    float pz;
    float tx;
    float tz;
    float facing;

    if (scene->animation_count <= 0) {
        SDL_SetError("lesson 32 requires retained animation data");
        return false;
    }
    animation = &scene->animations[0];
    if (animation->duration <= 0.0f || animation->channel_count <= 0) {
        SDL_SetError("lesson 32 retained animation data is incomplete");
        return false;
    }

    state->anim_time += dt * LESSON32_ANIMATION_SPEED;
    while (state->anim_time >= animation->duration) {
        state->anim_time -= animation->duration;
    }
    if (!ForgeGpuApplySceneAnimation(scene, 0, state->anim_time, true)) {
        return false;
    }
    if (!ForgeGpuRecomputeSceneWorldTransforms(scene)) {
        return false;
    }
    state->animation_applied = true;

    if (!ForgeGpuComputeSkinJointMatrices(
            scene,
            state->mesh_node_index,
            state->joint_uniforms.joints,
            SDL_arraysize(state->joint_uniforms.joints),
            &state->joint_matrix_count)) {
        return false;
    }
    if (state->joint_matrix_count != LESSON32_MAX_JOINTS) {
        SDL_SetError("lesson 32 expected %d joint matrices, got %d", LESSON32_MAX_JOINTS, state->joint_matrix_count);
        return false;
    }

    state->walk_angle += LESSON32_WALK_SPEED * dt;
    if (state->walk_angle > 2.0f * FORGE_GPU_PI) {
        state->walk_angle -= 2.0f * FORGE_GPU_PI;
    }

    px = LESSON32_WALK_RADIUS * SDL_sinf(state->walk_angle);
    pz = LESSON32_WALK_RADIUS * SDL_cosf(state->walk_angle);
    tx = SDL_cosf(state->walk_angle);
    tz = -SDL_sinf(state->walk_angle);
    facing = SDL_atan2f(tx, tz);

    path_translate = mat4_translate({ px, 0.0f, pz });
    path_rotate = mat4_rotate_y(facing);
    path_transform = mat4_multiply(path_translate, path_rotate);
    base_mesh_world = mat4_from_forge(scene->nodes[state->mesh_node_index].world_transform);
    state->mesh_world = mat4_multiply(path_transform, base_mesh_world);
    return true;
}

static const GpuMaterial *lesson32_material_or_default(const Lesson32State *state, int material_index, GpuMaterial *fallback)
{
    if (material_index >= 0 && material_index < state->material_count) {
        return &state->materials[material_index];
    }

    SDL_zero(*fallback);
    fallback->base_color[0] = 1.0f;
    fallback->base_color[1] = 1.0f;
    fallback->base_color[2] = 1.0f;
    fallback->base_color[3] = 1.0f;
    return fallback;
}

static void lesson32_bind_primitive(SDL_GPURenderPass *render_pass, const GpuPrimitive *primitive)
{
    SDL_GPUBufferBinding vertex_binding;

    SDL_zero(vertex_binding);
    vertex_binding.buffer = primitive->vertex_buffer;
    SDL_BindGPUVertexBuffers(render_pass, 0, &vertex_binding, 1);
    if (primitive->index_buffer && primitive->index_count > 0) {
        SDL_GPUBufferBinding index_binding;

        SDL_zero(index_binding);
        index_binding.buffer = primitive->index_buffer;
        SDL_BindGPUIndexBuffer(render_pass, &index_binding, primitive->index_type);
    }
}

static void lesson32_draw_primitive(SDL_GPURenderPass *render_pass, const GpuPrimitive *primitive)
{
    if (primitive->index_buffer && primitive->index_count > 0) {
        SDL_DrawGPUIndexedPrimitives(render_pass, primitive->index_count, 1, 0, 0, 0);
    } else {
        SDL_DrawGPUPrimitives(render_pass, primitive->vertex_count, 1, 0, 0);
    }
}

static bool lesson32_run_shadow_pass(ForgeGpuDemo *demo, SDL_GPUCommandBuffer *command_buffer)
{
    Lesson32State *state = lesson32_state(demo);
    Lesson32ShadowVertUniforms uniforms;
    SDL_GPURenderPass *render_pass;

    render_pass = ForgeGpuBeginDepthOnlyPass(command_buffer, state->shadow_depth, 1.0f);
    if (!render_pass) {
        return false;
    }

    SDL_BindGPUGraphicsPipeline(render_pass, state->shadow_pipeline);
    uniforms.light_vp = mat4_multiply(state->light_vp, state->mesh_world);
    SDL_PushGPUVertexUniformData(command_buffer, 0, &uniforms, sizeof(uniforms));
    SDL_PushGPUVertexUniformData(command_buffer, 1, &state->joint_uniforms, sizeof(state->joint_uniforms));
    for (int i = 0; i < state->primitive_count; i += 1) {
        if (!state->primitives[i].vertex_buffer) {
            continue;
        }
        lesson32_bind_primitive(render_pass, &state->primitives[i]);
        lesson32_draw_primitive(render_pass, &state->primitives[i]);
    }

    SDL_EndGPURenderPass(render_pass);
    state->shadow_pass_rendered = true;
    return true;
}

static void lesson32_push_scene_fragment_uniforms(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    const GpuMaterial *material)
{
    Lesson32SceneFragUniforms uniforms;

    SDL_zero(uniforms);
    SDL_memcpy(uniforms.base_color, material->base_color, sizeof(uniforms.base_color));
    uniforms.eye_pos[0] = demo->lesson.camera_position.x;
    uniforms.eye_pos[1] = demo->lesson.camera_position.y;
    uniforms.eye_pos[2] = demo->lesson.camera_position.z;
    uniforms.has_texture = material->has_texture && material->texture ? 1.0f : 0.0f;
    uniforms.ambient = LESSON32_MATERIAL_AMBIENT;
    uniforms.shininess = LESSON32_MATERIAL_SHININESS;
    uniforms.specular_str = LESSON32_MATERIAL_SPECULAR_STRENGTH;
    uniforms.light_dir[0] = LESSON32_LIGHT_DIR_X;
    uniforms.light_dir[1] = LESSON32_LIGHT_DIR_Y;
    uniforms.light_dir[2] = LESSON32_LIGHT_DIR_Z;
    uniforms.light_color[0] = LESSON32_LIGHT_COLOR_R;
    uniforms.light_color[1] = LESSON32_LIGHT_COLOR_G;
    uniforms.light_color[2] = LESSON32_LIGHT_COLOR_B;
    uniforms.light_intensity = LESSON32_LIGHT_INTENSITY;
    SDL_PushGPUFragmentUniformData(command_buffer, 0, &uniforms, sizeof(uniforms));
}

static void lesson32_draw_skinned_model(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    Mat4 cam_vp)
{
    Lesson32State *state = lesson32_state(demo);
    Lesson32SkinVertUniforms vertex_uniforms;

    SDL_BindGPUGraphicsPipeline(render_pass, state->skin_pipeline);
    vertex_uniforms.mvp = mat4_multiply(cam_vp, state->mesh_world);
    vertex_uniforms.model = state->mesh_world;
    vertex_uniforms.light_vp = mat4_multiply(state->light_vp, state->mesh_world);
    SDL_PushGPUVertexUniformData(command_buffer, 0, &vertex_uniforms, sizeof(vertex_uniforms));
    SDL_PushGPUVertexUniformData(command_buffer, 1, &state->joint_uniforms, sizeof(state->joint_uniforms));

    for (int i = 0; i < state->primitive_count; i += 1) {
        GpuMaterial fallback;
        const GpuMaterial *material;
        SDL_GPUTextureSamplerBinding bindings[2];

        if (!state->primitives[i].vertex_buffer) {
            continue;
        }
        material = lesson32_material_or_default(state, state->primitives[i].material_index, &fallback);
        lesson32_push_scene_fragment_uniforms(demo, command_buffer, material);

        SDL_zeroa(bindings);
        bindings[0].texture = material->has_texture && material->texture ? material->texture : demo->lesson.white_texture;
        bindings[0].sampler = demo->lesson.samplers[0];
        bindings[1].texture = state->shadow_depth;
        bindings[1].sampler = demo->lesson.samplers[1];
        SDL_BindGPUFragmentSamplers(render_pass, 0, bindings, SDL_arraysize(bindings));

        lesson32_bind_primitive(render_pass, &state->primitives[i]);
        lesson32_draw_primitive(render_pass, &state->primitives[i]);
    }
}

static void lesson32_draw_grid(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    Mat4 cam_vp)
{
    Lesson32State *state = lesson32_state(demo);
    ForgeGpuShadowedGridDrawInfo draw_info;

    SDL_zero(draw_info);
    draw_info.vp = cam_vp;
    draw_info.light_vp = state->light_vp;
    draw_info.light_dir = { LESSON32_LIGHT_DIR_X, LESSON32_LIGHT_DIR_Y, LESSON32_LIGHT_DIR_Z };
    draw_info.eye_pos = demo->lesson.camera_position;
    draw_info.light_intensity = LESSON32_LIGHT_INTENSITY;
    draw_info.line_color[0] = LESSON32_GRID_LINE_GRAY;
    draw_info.line_color[1] = LESSON32_GRID_LINE_GRAY;
    draw_info.line_color[2] = LESSON32_GRID_LINE_GRAY;
    draw_info.line_color[3] = 1.0f;
    draw_info.bg_color[0] = LESSON32_GRID_BG_GRAY;
    draw_info.bg_color[1] = LESSON32_GRID_BG_GRAY;
    draw_info.bg_color[2] = LESSON32_GRID_BG_GRAY;
    draw_info.bg_color[3] = 1.0f;
    draw_info.grid_spacing = LESSON32_GRID_SPACING;
    draw_info.line_width = LESSON32_GRID_LINE_WIDTH;
    draw_info.fade_distance = LESSON32_GRID_FADE_DISTANCE;
    draw_info.ambient = LESSON32_GRID_AMBIENT;
    draw_info.shadow_depth = state->shadow_depth;
    draw_info.shadow_sampler = demo->lesson.samplers[1];
    ForgeGpuDrawShadowedGrid(
        command_buffer,
        render_pass,
        state->grid_pipeline,
        state->grid_vertex_buffer,
        state->grid_index_buffer,
        &draw_info);
}

static bool lesson32_run_main_pass(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPUTexture *swapchain_texture,
    Mat4 cam_vp)
{
    Lesson32State *state = lesson32_state(demo);
    SDL_GPUColorTargetInfo color_target;
    SDL_GPUDepthStencilTargetInfo depth_target;
    SDL_GPURenderPass *render_pass;

    SDL_zero(color_target);
    color_target.texture = swapchain_texture;
    color_target.load_op = SDL_GPU_LOADOP_CLEAR;
    color_target.store_op = SDL_GPU_STOREOP_STORE;
    color_target.clear_color = { LESSON32_CLEAR_R, LESSON32_CLEAR_G, LESSON32_CLEAR_B, 1.0f };
    SDL_zero(depth_target);
    depth_target.texture = state->main_depth;
    depth_target.load_op = SDL_GPU_LOADOP_CLEAR;
    depth_target.store_op = SDL_GPU_STOREOP_STORE;
    depth_target.clear_depth = 1.0f;

    render_pass = SDL_BeginGPURenderPass(command_buffer, &color_target, 1, &depth_target);
    if (!render_pass) {
        return false;
    }

    lesson32_draw_skinned_model(demo, command_buffer, render_pass, cam_vp);
    lesson32_draw_grid(demo, command_buffer, render_pass, cam_vp);

    SDL_EndGPURenderPass(render_pass);
    state->main_pass_rendered = true;
    return true;
}

bool ForgeGpuCreateLesson32(ForgeGpuDemo *demo)
{
    Lesson32State *state;
    ForgeGpuSceneLoadRequirements requirements;
    char path[FORGE_GPU_MAX_PATH];

    if (!SDL_GPUTextureSupportsFormat(
            demo->device,
            LESSON32_DEPTH_FORMAT,
            SDL_GPU_TEXTURETYPE_2D,
            SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER)) {
        SDL_SetError("lesson 32 requires sampled D32_FLOAT depth targets");
        return false;
    }

    state = (Lesson32State *)SDL_calloc(1, sizeof(*state));
    if (!state) {
        SDL_OutOfMemory();
        return false;
    }
    demo->lesson.private_state = state;
    state->mesh_node_index = -1;
    state->mesh_world = mat4_identity();
    state->walk_angle = FORGE_GPU_PI * 0.5f;
    state->light_vp = lesson32_light_view_projection();

    demo->lesson.white_texture = ForgeGpuCreateWhiteTexture(demo->device);
    state->shadow_depth = ForgeGpuCreateSampledDepthTexture(
        demo,
        LESSON32_SHADOW_MAP_SIZE,
        LESSON32_SHADOW_MAP_SIZE,
        LESSON32_DEPTH_FORMAT);

    SDL_zero(requirements);
    requirements.required_features = FORGE_GPU_SCENE_FEATURE_NODE_HIERARCHY |
                                     FORGE_GPU_SCENE_FEATURE_SKINS |
                                     FORGE_GPU_SCENE_FEATURE_ANIMATIONS;
    requirements.required_all_primitive_features = FORGE_GPU_SCENE_FEATURE_SKINS |
                                                   FORGE_GPU_SCENE_FEATURE_PRIMITIVE_BOUNDS;

    if (!ForgeGpuJoinAssetPath(demo, "models/CesiumMan/CesiumMan.gltf", path, sizeof(path)) ||
        !ForgeGpuLoadGltfSceneWithRequirements(path, &requirements, &state->scene)) {
        return false;
    }

    state->mesh_node_index = lesson32_find_skin_mesh_node(&state->scene);
    state->skin_data_ready =
        state->mesh_node_index >= 0 &&
        state->scene.skin_count > 0 &&
        state->scene.skins[0].joint_count == LESSON32_MAX_JOINTS;
    if (!state->skin_data_ready) {
        SDL_SetError("lesson 32 requires CesiumMan skinning data");
        return false;
    }

    if (!demo->lesson.white_texture ||
        !state->shadow_depth ||
        !lesson32_create_samplers(demo) ||
        !lesson32_upload_skinned_model(demo, state) ||
        !lesson32_create_grid(demo) ||
        !lesson32_create_pipelines(demo)) {
        return false;
    }

    lesson32_init_camera(demo);
    return true;
}

bool ForgeGpuRenderLesson32(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPUTexture *swapchain_texture,
    Uint32 width,
    Uint32 height)
{
    Lesson32State *state = lesson32_state(demo);
    float dt;
    Mat4 view;
    Mat4 projection;
    Mat4 cam_vp;

    if (!state) {
        SDL_SetError("lesson 32 internal state is missing");
        return false;
    }
    if (!ForgeGpuEnsureSampledDepthTarget(
            demo,
            &state->main_depth,
            &state->main_depth_width,
            &state->main_depth_height,
            width,
            height,
            LESSON32_DEPTH_FORMAT)) {
        return false;
    }

    ForgeGpuUpdateCameraFromInput(demo);
    dt = lesson32_delta_seconds(demo, state);
    if (!lesson32_update_animation(state, dt)) {
        return false;
    }

    ForgeGpuCameraViewProjection(demo, width, height, LESSON32_FAR_PLANE, &view, &projection);
    cam_vp = mat4_multiply(projection, view);
    state->shadow_pass_rendered = false;
    state->main_pass_rendered = false;

    return lesson32_run_shadow_pass(demo, command_buffer) &&
           lesson32_run_main_pass(demo, command_buffer, swapchain_texture, cam_vp);
}

void ForgeGpuDebugLesson32(ForgeGpuDemo *demo)
{
    Lesson32State *state = lesson32_state(demo);

    if (!state) {
        return;
    }
    ImGui::Text("Skinning: %s", state->skin_data_ready ? "ready" : "missing");
    ImGui::Text("Animation: %s", state->animation_applied ? "active" : "pending");
    ImGui::Text("Animation time: %.2fs", state->anim_time);
    ImGui::Text("Walk angle: %.2f", state->walk_angle);
    ImGui::Text("Joint matrices: %d / %d", state->joint_matrix_count, LESSON32_MAX_JOINTS);
    ImGui::Text("Shadow: D32 2048x2048");
}

void ForgeGpuExportLesson32Metrics(ForgeGpuDemo *demo)
{
    Lesson32State *state = lesson32_state(demo);

    if (!state) {
        return;
    }
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuSkinningAnimations", 1.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuSkinningDataReady", state->skin_data_ready ? 1.0 : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuSkinningAnimationApplied", state->animation_applied ? 1.0 : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuSkinningJointCount", (double)state->joint_matrix_count);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuSkinningPassesRendered", state->shadow_pass_rendered && state->main_pass_rendered ? 1.0 : 0.0);
}

void ForgeGpuDestroyLesson32(ForgeGpuDemo *demo)
{
    Lesson32State *state = lesson32_state(demo);

    if (!state) {
        return;
    }
    if (state->materials) {
        for (int i = 0; i < state->material_count; i += 1) {
            if (state->materials[i].texture) {
                SDL_ReleaseGPUTexture(demo->device, state->materials[i].texture);
            }
        }
    }
    if (state->primitives) {
        for (int i = 0; i < state->primitive_count; i += 1) {
            if (state->primitives[i].index_buffer) {
                SDL_ReleaseGPUBuffer(demo->device, state->primitives[i].index_buffer);
            }
            if (state->primitives[i].vertex_buffer) {
                SDL_ReleaseGPUBuffer(demo->device, state->primitives[i].vertex_buffer);
            }
        }
    }
    SDL_free(state->materials);
    SDL_free(state->primitives);
    ForgeGpuFreeLoadedScene(&state->scene);
    if (state->grid_index_buffer) {
        SDL_ReleaseGPUBuffer(demo->device, state->grid_index_buffer);
    }
    if (state->grid_vertex_buffer) {
        SDL_ReleaseGPUBuffer(demo->device, state->grid_vertex_buffer);
    }
    if (state->main_depth) {
        SDL_ReleaseGPUTexture(demo->device, state->main_depth);
    }
    if (state->shadow_depth) {
        SDL_ReleaseGPUTexture(demo->device, state->shadow_depth);
    }
    if (state->grid_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, state->grid_pipeline);
    }
    if (state->shadow_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, state->shadow_pipeline);
    }
    if (state->skin_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, state->skin_pipeline);
    }
    SDL_free(state);
    demo->lesson.private_state = nullptr;
}
