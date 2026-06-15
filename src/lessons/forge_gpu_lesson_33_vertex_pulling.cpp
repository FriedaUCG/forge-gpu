#include "forge_gpu_lessons.h"

#include "forge_gpu_browser_status.h"
#include "forge_gpu_camera.h"
#include "forge_gpu_forward_scene.h"
#include "forge_gpu_gpu_helpers.h"
#include "forge_gpu_lesson_common.h"
#include "forge_gpu_math.h"
#include "forge_gpu_shader_layouts.h"
#include "shaders/generated/forge_gpu_lesson_33_shaders.h"
#include "imgui.h"

#include <stddef.h>

#define LESSON33_DEPTH_FORMAT SDL_GPU_TEXTUREFORMAT_D32_FLOAT
#define LESSON33_SHADOW_MAP_SIZE 2048u
#define LESSON33_FAR_PLANE 100.0f
#define LESSON33_CAMERA_SPEED 3.0f
#define LESSON33_MOUSE_SENSITIVITY 0.003f
#define LESSON33_PITCH_CLAMP 1.5f
#define LESSON33_CAMERA_START_X 4.0f
#define LESSON33_CAMERA_START_Y 2.5f
#define LESSON33_CAMERA_START_Z -4.0f
#define LESSON33_CAMERA_START_YAW_DEG 145.0f
#define LESSON33_CAMERA_START_PITCH_DEG -15.0f
#define LESSON33_LIGHT_DIR_X -0.4f
#define LESSON33_LIGHT_DIR_Y -0.8f
#define LESSON33_LIGHT_DIR_Z -0.4f
#define LESSON33_LIGHT_INTENSITY 0.9f
#define LESSON33_LIGHT_COLOR_R 1.0f
#define LESSON33_LIGHT_COLOR_G 0.95f
#define LESSON33_LIGHT_COLOR_B 0.85f
#define LESSON33_MATERIAL_AMBIENT 0.25f
#define LESSON33_MATERIAL_SHININESS 32.0f
#define LESSON33_MATERIAL_SPECULAR_STRENGTH 0.4f
#define LESSON33_SHADOW_ORTHO_SIZE 8.0f
#define LESSON33_SHADOW_NEAR 0.1f
#define LESSON33_SHADOW_FAR 30.0f
#define LESSON33_LIGHT_DISTANCE 12.0f
#define LESSON33_LIGHT_TARGET_Y 1.0f
#define LESSON33_PARALLEL_THRESHOLD 0.99f
#define LESSON33_GRID_HALF_SIZE 20.0f
#define LESSON33_GRID_Y 0.0f
#define LESSON33_GRID_SPACING 1.0f
#define LESSON33_GRID_LINE_WIDTH 0.02f
#define LESSON33_GRID_FADE_DISTANCE 30.0f
#define LESSON33_GRID_AMBIENT 0.3f
#define LESSON33_GRID_LINE_GRAY 0.4f
#define LESSON33_GRID_BG_GRAY 0.25f
#define LESSON33_CLEAR_R 0.6f
#define LESSON33_CLEAR_G 0.7f
#define LESSON33_CLEAR_B 0.8f
#define LESSON33_BOX_COUNT 8
#define LESSON33_MAX_ANISOTROPY 8.0f
#define LESSON33_MAX_LOD_UNLIMITED 1000.0f

struct Lesson33PulledVertex
{
    float position[3];
    /* Match WGSL/std430-style storage-buffer vec3 alignment across producers. */
    float _pad0;
    float normal[3];
    float _pad1;
    float uv[2];
    float _pad2[2];
};

struct Lesson33PulledPrimitive
{
    SDL_GPUBuffer *storage_buffer;
    SDL_GPUBuffer *index_buffer;
    Uint32 vertex_count;
    Uint32 index_count;
    SDL_GPUIndexElementSize index_type;
    int material_index;
    bool has_uvs;
};

struct Lesson33Object
{
    Lesson33PulledPrimitive *primitives;
    int primitive_count;
    GpuMaterial *materials;
    int material_count;
    Mat4 model_matrix;
    bool owns_resources;
};

struct Lesson33SceneVertUniforms
{
    Mat4 mvp;
    Mat4 model;
    Mat4 light_vp;
};

struct Lesson33ShadowVertUniforms
{
    Mat4 light_vp;
};

struct Lesson33SceneFragUniforms
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

struct Lesson33State
{
    ForgeGpuLoadedScene truck_scene;
    ForgeGpuLoadedScene box_scene;
    Lesson33Object truck;
    Lesson33Object boxes[LESSON33_BOX_COUNT];
    SDL_GPUGraphicsPipeline *pulled_pipeline;
    SDL_GPUGraphicsPipeline *shadow_pipeline;
    SDL_GPUGraphicsPipeline *grid_pipeline;
    SDL_GPUTexture *shadow_depth;
    SDL_GPUTexture *main_depth;
    SDL_GPUBuffer *grid_vertex_buffer;
    SDL_GPUBuffer *grid_index_buffer;
    Uint32 main_depth_width;
    Uint32 main_depth_height;
    Mat4 light_vp;
    bool shadow_pass_rendered;
    bool main_pass_rendered;
};

static_assert(sizeof(Lesson33PulledVertex) == 48, "lesson 33 pulled vertex size must match HLSL layout");
static_assert(sizeof(Lesson33SceneVertUniforms) == 192, "lesson 33 scene vertex uniform size must match HLSL layout");
static_assert(sizeof(Lesson33ShadowVertUniforms) == 64, "lesson 33 shadow vertex uniform size must match HLSL layout");
static_assert(sizeof(Lesson33SceneFragUniforms) == 80, "lesson 33 scene fragment uniform size must match HLSL layout");

static Lesson33State *lesson33_state(ForgeGpuDemo *demo)
{
    return (Lesson33State *)demo->lesson.private_state;
}

static void lesson33_init_camera(ForgeGpuDemo *demo)
{
    demo->lesson.camera_position = {
        LESSON33_CAMERA_START_X,
        LESSON33_CAMERA_START_Y,
        LESSON33_CAMERA_START_Z
    };
    demo->lesson.camera_yaw = LESSON33_CAMERA_START_YAW_DEG * FORGE_GPU_DEG2RAD;
    demo->lesson.camera_pitch = LESSON33_CAMERA_START_PITCH_DEG * FORGE_GPU_DEG2RAD;
    demo->lesson.pitch_clamp = LESSON33_PITCH_CLAMP;
    demo->lesson.mouse_sensitivity = LESSON33_MOUSE_SENSITIVITY;
    demo->lesson.move_speed = LESSON33_CAMERA_SPEED;
    demo->lesson.last_ticks = SDL_GetTicks();
}

static Mat4 lesson33_light_view_projection(void)
{
    const Vec3 target = { 0.0f, LESSON33_LIGHT_TARGET_Y, 0.0f };
    const Vec3 light_dir = { LESSON33_LIGHT_DIR_X, LESSON33_LIGHT_DIR_Y, LESSON33_LIGHT_DIR_Z };

    return ForgeGpuComputeTargetedDirectionalLightViewProjection(
        light_dir,
        LESSON33_LIGHT_DISTANCE,
        target,
        LESSON33_SHADOW_ORTHO_SIZE,
        LESSON33_SHADOW_NEAR,
        LESSON33_SHADOW_FAR,
        LESSON33_PARALLEL_THRESHOLD);
}

static bool lesson33_load_scene(ForgeGpuDemo *demo, const char *relative_path, ForgeGpuLoadedScene *scene)
{
    char path[FORGE_GPU_MAX_PATH];

    if (!ForgeGpuJoinAssetPath(demo, relative_path, path, sizeof(path))) {
        return false;
    }
    return ForgeGpuLoadGltfScene(path, scene);
}

static void lesson33_release_object_resources(ForgeGpuDemo *demo, Lesson33Object *object)
{
    if (!object || !object->owns_resources) {
        return;
    }

    for (int i = 0; i < object->primitive_count; i += 1) {
        if (object->primitives[i].storage_buffer) {
            SDL_ReleaseGPUBuffer(demo->device, object->primitives[i].storage_buffer);
        }
        if (object->primitives[i].index_buffer) {
            SDL_ReleaseGPUBuffer(demo->device, object->primitives[i].index_buffer);
        }
    }
    for (int i = 0; i < object->material_count; i += 1) {
        if (object->materials[i].texture) {
            SDL_ReleaseGPUTexture(demo->device, object->materials[i].texture);
        }
    }

    SDL_free(object->primitives);
    SDL_free(object->materials);
    SDL_zero(*object);
}

static bool lesson33_upload_pulled_model(
    ForgeGpuDemo *demo,
    const ForgeGpuLoadedScene *scene,
    Lesson33Object *object)
{
    object->primitive_count = scene->primitive_count;
    object->owns_resources = true;
    if (object->primitive_count > 0) {
        object->primitives = (Lesson33PulledPrimitive *)SDL_calloc((size_t)object->primitive_count, sizeof(*object->primitives));
        if (!object->primitives) {
            SDL_OutOfMemory();
            return false;
        }
    }

    for (int i = 0; i < scene->primitive_count; i += 1) {
        const ForgeGpuScenePrimitive *src = &scene->primitives[i];
        Lesson33PulledPrimitive *dst = &object->primitives[i];
        Lesson33PulledVertex *vertices;
        size_t vertex_bytes_size;
        Uint32 vertex_bytes;

        if (src->vertex_count == 0 || !src->vertices) {
            SDL_SetError("lesson 33 requires primitive vertex data");
            return false;
        }
        if (!SDL_size_mul_check_overflow((size_t)src->vertex_count, sizeof(*vertices), &vertex_bytes_size) ||
            vertex_bytes_size > SDL_MAX_UINT32) {
            SDL_SetError("lesson 33 vertex allocation overflow");
            return false;
        }

        vertices = (Lesson33PulledVertex *)SDL_calloc((size_t)src->vertex_count, sizeof(*vertices));
        if (!vertices) {
            SDL_OutOfMemory();
            return false;
        }
        for (Uint32 vertex = 0; vertex < src->vertex_count; vertex += 1) {
            SDL_memcpy(vertices[vertex].position, src->vertices[vertex].position, sizeof(vertices[vertex].position));
            SDL_memcpy(vertices[vertex].normal, src->vertices[vertex].normal, sizeof(vertices[vertex].normal));
            SDL_memcpy(vertices[vertex].uv, src->vertices[vertex].uv, sizeof(vertices[vertex].uv));
        }

        vertex_bytes = (Uint32)vertex_bytes_size;
        dst->storage_buffer = ForgeGpuCreateBufferWithData(
            demo->device,
            SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ,
            vertices,
            vertex_bytes);
        SDL_free(vertices);
        if (!dst->storage_buffer) {
            return false;
        }

        dst->vertex_count = src->vertex_count;
        dst->index_count = src->index_count;
        dst->material_index = src->material_index;
        dst->has_uvs = src->has_uvs;
        dst->index_type = src->index_stride == 4 ? SDL_GPU_INDEXELEMENTSIZE_32BIT : SDL_GPU_INDEXELEMENTSIZE_16BIT;
        if (src->indices && src->index_count > 0) {
            size_t index_bytes_size;

            if (src->index_stride != 2 && src->index_stride != 4) {
                SDL_SetError("lesson 33 unsupported index stride");
                return false;
            }
            if (!SDL_size_mul_check_overflow((size_t)src->index_count, (size_t)src->index_stride, &index_bytes_size) ||
                index_bytes_size > SDL_MAX_UINT32) {
                SDL_SetError("lesson 33 index allocation overflow");
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

    object->material_count = scene->material_count;
    if (object->material_count > 0) {
        object->materials = (GpuMaterial *)SDL_calloc((size_t)object->material_count, sizeof(*object->materials));
        if (!object->materials) {
            SDL_OutOfMemory();
            return false;
        }
    }

    for (int i = 0; i < scene->material_count; i += 1) {
        const ForgeGpuSceneMaterial *src = &scene->materials[i];
        GpuMaterial *dst = &object->materials[i];

        SDL_memcpy(dst->base_color, src->base_color, sizeof(dst->base_color));
        dst->has_texture = src->has_texture && src->texture_path[0] != '\0';
        if (dst->has_texture) {
            dst->texture = ForgeGpuLoadRgbaTexturePathWithFormat(
                demo,
                src->texture_path,
                true,
                SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB);
            if (!dst->texture) {
                return false;
            }
        }
    }

    return true;
}

static bool lesson33_create_samplers(ForgeGpuDemo *demo)
{
    demo->lesson.white_texture = ForgeGpuCreateWhiteTexture(demo->device);
    demo->lesson.samplers[0] = ForgeGpuCreateSamplerWithAddressAndAnisotropy(
        demo->device,
        SDL_GPU_FILTER_LINEAR,
        SDL_GPU_FILTER_LINEAR,
        SDL_GPU_SAMPLERMIPMAPMODE_LINEAR,
        SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
        LESSON33_MAX_LOD_UNLIMITED,
        LESSON33_MAX_ANISOTROPY);
    demo->lesson.samplers[1] = ForgeGpuCreateSamplerWithAddress(
        demo->device,
        SDL_GPU_FILTER_NEAREST,
        SDL_GPU_FILTER_NEAREST,
        SDL_GPU_SAMPLERMIPMAPMODE_NEAREST,
        SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        0.0f);
    return demo->lesson.white_texture && demo->lesson.samplers[0] && demo->lesson.samplers[1];
}

static bool lesson33_create_grid(ForgeGpuDemo *demo)
{
    Lesson33State *state = lesson33_state(demo);

    return ForgeGpuCreateShadowedGridBuffers(
        demo->device,
        LESSON33_GRID_HALF_SIZE,
        LESSON33_GRID_Y,
        &state->grid_vertex_buffer,
        &state->grid_index_buffer);
}

static bool lesson33_ensure_main_depth(ForgeGpuDemo *demo, Uint32 width, Uint32 height)
{
    Lesson33State *state = lesson33_state(demo);
    SDL_GPUTextureCreateInfo texture_info;
    SDL_GPUTexture *texture;

    if (state->main_depth && state->main_depth_width == width && state->main_depth_height == height) {
        return true;
    }

    SDL_zero(texture_info);
    texture_info.type = SDL_GPU_TEXTURETYPE_2D;
    texture_info.format = LESSON33_DEPTH_FORMAT;
    texture_info.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
    texture_info.width = width;
    texture_info.height = height;
    texture_info.layer_count_or_depth = 1;
    texture_info.num_levels = 1;
    texture_info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    texture = SDL_CreateGPUTexture(demo->device, &texture_info);
    if (!texture) {
        return false;
    }
    if (state->main_depth) {
        SDL_ReleaseGPUTexture(demo->device, state->main_depth);
    }
    state->main_depth = texture;
    state->main_depth_width = width;
    state->main_depth_height = height;
    return true;
}

static bool lesson33_create_pulled_pipeline(ForgeGpuDemo *demo)
{
    Lesson33State *state = lesson33_state(demo);
    SDL_GPUShader *vertex_shader;
    SDL_GPUShader *fragment_shader;

    vertex_shader = ForgeGpuCreateShader(
        demo->device,
        SDL_GPU_SHADERSTAGE_VERTEX,
        lesson33_pulled_vert_wgsl,
        lesson33_pulled_vert_wgsl_size,
        lesson33_pulled_vert_msl,
        lesson33_pulled_vert_msl_size,
        0, 0, 1, 1);
    if (!vertex_shader) {
        return false;
    }
    fragment_shader = ForgeGpuCreateShaderWithResourceLayout(
        demo->device,
        lesson33_pulled_frag_wgsl,
        lesson33_pulled_frag_wgsl_size,
        lesson33_pulled_frag_msl,
        lesson33_pulled_frag_msl_size,
        ForgeGpuShaderLayout_lesson33_pulled_frag());
    if (!fragment_shader) {
        SDL_ReleaseGPUShader(demo->device, vertex_shader);
        return false;
    }

    state->pulled_pipeline = ForgeGpuCreateLessonGraphicsPipelineWithPrimitive(
        demo,
        vertex_shader,
        fragment_shader,
        SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        nullptr,
        0,
        nullptr,
        0,
        1,
        true,
        LESSON33_DEPTH_FORMAT,
        true,
        true,
        SDL_GPU_CULLMODE_BACK,
        0.0f,
        0.0f);

    SDL_ReleaseGPUShader(demo->device, vertex_shader);
    SDL_ReleaseGPUShader(demo->device, fragment_shader);
    return state->pulled_pipeline != nullptr;
}

static bool lesson33_create_shadow_pipeline(ForgeGpuDemo *demo)
{
    Lesson33State *state = lesson33_state(demo);
    SDL_GPUShader *vertex_shader;
    SDL_GPUShader *fragment_shader;

    vertex_shader = ForgeGpuCreateShader(
        demo->device,
        SDL_GPU_SHADERSTAGE_VERTEX,
        lesson33_shadow_pulled_vert_wgsl,
        lesson33_shadow_pulled_vert_wgsl_size,
        lesson33_shadow_pulled_vert_msl,
        lesson33_shadow_pulled_vert_msl_size,
        0, 0, 1, 1);
    if (!vertex_shader) {
        return false;
    }
    fragment_shader = ForgeGpuCreateShader(
        demo->device,
        SDL_GPU_SHADERSTAGE_FRAGMENT,
        lesson33_shadow_frag_wgsl,
        lesson33_shadow_frag_wgsl_size,
        lesson33_shadow_frag_msl,
        lesson33_shadow_frag_msl_size,
        0, 0, 0, 0);
    if (!fragment_shader) {
        SDL_ReleaseGPUShader(demo->device, vertex_shader);
        return false;
    }

    state->shadow_pipeline = ForgeGpuCreateLessonGraphicsPipelineWithColorTargetsAndDepthCompare(
        demo,
        vertex_shader,
        fragment_shader,
        SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        nullptr,
        0,
        nullptr,
        0,
        nullptr,
        0,
        true,
        LESSON33_DEPTH_FORMAT,
        true,
        true,
        SDL_GPU_COMPAREOP_LESS_OR_EQUAL,
        SDL_GPU_CULLMODE_BACK,
        0.0f,
        0.0f);

    SDL_ReleaseGPUShader(demo->device, vertex_shader);
    SDL_ReleaseGPUShader(demo->device, fragment_shader);
    return state->shadow_pipeline != nullptr;
}

static bool lesson33_create_grid_pipeline(ForgeGpuDemo *demo)
{
    Lesson33State *state = lesson33_state(demo);
    SDL_GPUShader *vertex_shader;
    SDL_GPUShader *fragment_shader;
    SDL_GPUVertexBufferDescription vertex_buffer;
    SDL_GPUVertexAttribute attribute;

    vertex_shader = ForgeGpuCreateShader(
        demo->device,
        SDL_GPU_SHADERSTAGE_VERTEX,
        lesson33_grid_vert_wgsl,
        lesson33_grid_vert_wgsl_size,
        lesson33_grid_vert_msl,
        lesson33_grid_vert_msl_size,
        0, 0, 0, 1);
    if (!vertex_shader) {
        return false;
    }
    fragment_shader = ForgeGpuCreateShaderWithResourceLayout(
        demo->device,
        lesson33_grid_frag_wgsl,
        lesson33_grid_frag_wgsl_size,
        lesson33_grid_frag_msl,
        lesson33_grid_frag_msl_size,
        ForgeGpuShaderLayout_lesson33_grid_frag());
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
        LESSON33_DEPTH_FORMAT,
        true,
        true,
        SDL_GPU_CULLMODE_NONE,
        0.0f,
        0.0f);

    SDL_ReleaseGPUShader(demo->device, vertex_shader);
    SDL_ReleaseGPUShader(demo->device, fragment_shader);
    return state->grid_pipeline != nullptr;
}

static bool lesson33_create_pipelines(ForgeGpuDemo *demo)
{
    return lesson33_create_pulled_pipeline(demo) &&
           lesson33_create_shadow_pipeline(demo) &&
           lesson33_create_grid_pipeline(demo);
}

static void lesson33_build_box_transforms(Lesson33State *state)
{
    const struct
    {
        float x;
        float z;
        float scale;
        float angle;
    } box_layout[LESSON33_BOX_COUNT] = {
        {  3.0f,  2.0f, 0.8f,  25.0f },
        { -3.0f,  1.5f, 1.0f, -15.0f },
        {  2.0f, -3.0f, 0.6f,  45.0f },
        { -2.5f, -2.5f, 0.9f, -30.0f },
        {  4.5f, -1.0f, 0.7f,  60.0f },
        { -4.0f,  0.0f, 1.1f,  10.0f },
        {  1.0f,  4.0f, 0.5f, -45.0f },
        { -1.0f, -4.5f, 0.8f,  35.0f },
    };

    for (int i = 0; i < LESSON33_BOX_COUNT; i += 1) {
        const float ground_y = box_layout[i].scale * 0.5f;
        const Mat4 translation = mat4_translate({ box_layout[i].x, ground_y, box_layout[i].z });
        const Mat4 rotation = mat4_rotate_y(box_layout[i].angle * FORGE_GPU_DEG2RAD);
        const Mat4 scale = mat4_scale(box_layout[i].scale);

        state->boxes[i].model_matrix = mat4_multiply(translation, mat4_multiply(rotation, scale));
    }
}

static void lesson33_share_box_resources(Lesson33State *state)
{
    for (int i = 1; i < LESSON33_BOX_COUNT; i += 1) {
        state->boxes[i].primitives = state->boxes[0].primitives;
        state->boxes[i].primitive_count = state->boxes[0].primitive_count;
        state->boxes[i].materials = state->boxes[0].materials;
        state->boxes[i].material_count = state->boxes[0].material_count;
        state->boxes[i].owns_resources = false;
    }
}

static const GpuMaterial *lesson33_material_or_default(const Lesson33Object *object, int material_index, GpuMaterial *fallback)
{
    if (material_index >= 0 && material_index < object->material_count) {
        return &object->materials[material_index];
    }

    SDL_zero(*fallback);
    fallback->base_color[0] = 1.0f;
    fallback->base_color[1] = 1.0f;
    fallback->base_color[2] = 1.0f;
    fallback->base_color[3] = 1.0f;
    return fallback;
}

static void lesson33_draw_pulled_primitive(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    const Lesson33Object *object,
    const Lesson33PulledPrimitive *primitive,
    Mat4 world,
    Mat4 cam_vp,
    bool shadow_pass)
{
    SDL_GPUBuffer *storage_buffers[1];
    SDL_GPUBufferBinding index_binding;

    if (!primitive->storage_buffer) {
        return;
    }

    if (shadow_pass) {
        Lesson33ShadowVertUniforms uniforms;

        uniforms.light_vp = mat4_multiply(lesson33_state(demo)->light_vp, world);
        SDL_PushGPUVertexUniformData(command_buffer, 0, &uniforms, sizeof(uniforms));
    } else {
        Lesson33SceneVertUniforms vertex_uniforms;
        Lesson33SceneFragUniforms fragment_uniforms;
        SDL_GPUTextureSamplerBinding bindings[2];
        GpuMaterial fallback_material;
        const GpuMaterial *material = lesson33_material_or_default(object, primitive->material_index, &fallback_material);
        SDL_GPUTexture *texture = demo->lesson.white_texture;

        vertex_uniforms.mvp = mat4_multiply(cam_vp, world);
        vertex_uniforms.model = world;
        vertex_uniforms.light_vp = mat4_multiply(lesson33_state(demo)->light_vp, world);
        SDL_PushGPUVertexUniformData(command_buffer, 0, &vertex_uniforms, sizeof(vertex_uniforms));

        SDL_zero(fragment_uniforms);
        SDL_memcpy(fragment_uniforms.base_color, material->base_color, sizeof(fragment_uniforms.base_color));
        fragment_uniforms.eye_pos[0] = demo->lesson.camera_position.x;
        fragment_uniforms.eye_pos[1] = demo->lesson.camera_position.y;
        fragment_uniforms.eye_pos[2] = demo->lesson.camera_position.z;
        fragment_uniforms.has_texture = material->has_texture && material->texture && primitive->has_uvs ? 1.0f : 0.0f;
        fragment_uniforms.ambient = LESSON33_MATERIAL_AMBIENT;
        fragment_uniforms.shininess = LESSON33_MATERIAL_SHININESS;
        fragment_uniforms.specular_str = LESSON33_MATERIAL_SPECULAR_STRENGTH;
        fragment_uniforms.light_dir[0] = LESSON33_LIGHT_DIR_X;
        fragment_uniforms.light_dir[1] = LESSON33_LIGHT_DIR_Y;
        fragment_uniforms.light_dir[2] = LESSON33_LIGHT_DIR_Z;
        fragment_uniforms.light_color[0] = LESSON33_LIGHT_COLOR_R;
        fragment_uniforms.light_color[1] = LESSON33_LIGHT_COLOR_G;
        fragment_uniforms.light_color[2] = LESSON33_LIGHT_COLOR_B;
        fragment_uniforms.light_intensity = LESSON33_LIGHT_INTENSITY;
        SDL_PushGPUFragmentUniformData(command_buffer, 0, &fragment_uniforms, sizeof(fragment_uniforms));

        if (fragment_uniforms.has_texture > 0.5f) {
            texture = material->texture;
        }
        SDL_zeroa(bindings);
        bindings[0].texture = texture;
        bindings[0].sampler = demo->lesson.samplers[0];
        bindings[1].texture = lesson33_state(demo)->shadow_depth;
        bindings[1].sampler = demo->lesson.samplers[1];
        SDL_BindGPUFragmentSamplers(render_pass, 0, bindings, SDL_arraysize(bindings));
    }

    storage_buffers[0] = primitive->storage_buffer;
    SDL_BindGPUVertexStorageBuffers(render_pass, 0, storage_buffers, SDL_arraysize(storage_buffers));

    if (primitive->index_buffer && primitive->index_count > 0) {
        SDL_zero(index_binding);
        index_binding.buffer = primitive->index_buffer;
        SDL_BindGPUIndexBuffer(render_pass, &index_binding, primitive->index_type);
        SDL_DrawGPUIndexedPrimitives(render_pass, primitive->index_count, 1, 0, 0, 0);
    } else {
        SDL_DrawGPUPrimitives(render_pass, primitive->vertex_count, 1, 0, 0);
    }
}

static void lesson33_draw_pulled_object(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    const Lesson33Object *object,
    const ForgeGpuLoadedScene *scene,
    Mat4 cam_vp,
    bool shadow_pass)
{
    for (int node_index = 0; node_index < scene->node_count; node_index += 1) {
        const ForgeGpuSceneNode *node = &scene->nodes[node_index];
        const ForgeGpuSceneMesh *mesh;
        Mat4 world;

        if (node->mesh_index < 0 || node->mesh_index >= scene->mesh_count) {
            continue;
        }
        mesh = &scene->meshes[node->mesh_index];
        world = mat4_multiply(object->model_matrix, mat4_from_forge(node->world_transform));
        for (int primitive_offset = 0; primitive_offset < mesh->primitive_count; primitive_offset += 1) {
            const int primitive_index = mesh->first_primitive + primitive_offset;

            if (primitive_index < 0 || primitive_index >= object->primitive_count) {
                continue;
            }
            lesson33_draw_pulled_primitive(
                demo,
                command_buffer,
                render_pass,
                object,
                &object->primitives[primitive_index],
                world,
                cam_vp,
                shadow_pass);
        }
    }
}

static bool lesson33_run_shadow_pass(ForgeGpuDemo *demo, SDL_GPUCommandBuffer *command_buffer)
{
    Lesson33State *state = lesson33_state(demo);
    SDL_GPURenderPass *render_pass = ForgeGpuBeginDepthOnlyPass(command_buffer, state->shadow_depth, 1.0f);

    if (!render_pass) {
        return false;
    }

    SDL_BindGPUGraphicsPipeline(render_pass, state->shadow_pipeline);
    lesson33_draw_pulled_object(demo, command_buffer, render_pass, &state->truck, &state->truck_scene, mat4_identity(), true);
    for (int i = 0; i < LESSON33_BOX_COUNT; i += 1) {
        lesson33_draw_pulled_object(demo, command_buffer, render_pass, &state->boxes[i], &state->box_scene, mat4_identity(), true);
    }
    SDL_EndGPURenderPass(render_pass);
    state->shadow_pass_rendered = true;
    return true;
}

static void lesson33_draw_grid(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    Mat4 cam_vp)
{
    Lesson33State *state = lesson33_state(demo);
    ForgeGpuShadowedGridDrawInfo draw_info;

    SDL_zero(draw_info);
    draw_info.vp = cam_vp;
    draw_info.light_vp = state->light_vp;
    draw_info.light_dir = { LESSON33_LIGHT_DIR_X, LESSON33_LIGHT_DIR_Y, LESSON33_LIGHT_DIR_Z };
    draw_info.eye_pos = demo->lesson.camera_position;
    draw_info.light_intensity = LESSON33_LIGHT_INTENSITY;
    draw_info.line_color[0] = LESSON33_GRID_LINE_GRAY;
    draw_info.line_color[1] = LESSON33_GRID_LINE_GRAY;
    draw_info.line_color[2] = LESSON33_GRID_LINE_GRAY;
    draw_info.line_color[3] = 1.0f;
    draw_info.bg_color[0] = LESSON33_GRID_BG_GRAY;
    draw_info.bg_color[1] = LESSON33_GRID_BG_GRAY;
    draw_info.bg_color[2] = LESSON33_GRID_BG_GRAY;
    draw_info.bg_color[3] = 1.0f;
    draw_info.grid_spacing = LESSON33_GRID_SPACING;
    draw_info.line_width = LESSON33_GRID_LINE_WIDTH;
    draw_info.fade_distance = LESSON33_GRID_FADE_DISTANCE;
    draw_info.ambient = LESSON33_GRID_AMBIENT;
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

static bool lesson33_run_scene_pass(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPUTexture *swapchain_texture,
    Mat4 cam_vp)
{
    Lesson33State *state = lesson33_state(demo);
    SDL_GPUColorTargetInfo color_target;
    SDL_GPUDepthStencilTargetInfo depth_target;
    SDL_GPURenderPass *render_pass;

    SDL_zero(color_target);
    color_target.texture = swapchain_texture;
    color_target.load_op = SDL_GPU_LOADOP_CLEAR;
    color_target.store_op = SDL_GPU_STOREOP_STORE;
    color_target.clear_color = { LESSON33_CLEAR_R, LESSON33_CLEAR_G, LESSON33_CLEAR_B, 1.0f };

    SDL_zero(depth_target);
    depth_target.texture = state->main_depth;
    depth_target.load_op = SDL_GPU_LOADOP_CLEAR;
    depth_target.store_op = SDL_GPU_STOREOP_DONT_CARE;
    depth_target.clear_depth = 1.0f;

    render_pass = SDL_BeginGPURenderPass(command_buffer, &color_target, 1, &depth_target);
    if (!render_pass) {
        return false;
    }

    SDL_BindGPUGraphicsPipeline(render_pass, state->pulled_pipeline);
    lesson33_draw_pulled_object(demo, command_buffer, render_pass, &state->truck, &state->truck_scene, cam_vp, false);
    for (int i = 0; i < LESSON33_BOX_COUNT; i += 1) {
        lesson33_draw_pulled_object(demo, command_buffer, render_pass, &state->boxes[i], &state->box_scene, cam_vp, false);
    }
    lesson33_draw_grid(demo, command_buffer, render_pass, cam_vp);

    SDL_EndGPURenderPass(render_pass);
    state->main_pass_rendered = true;
    return true;
}

bool ForgeGpuCreateLesson33(ForgeGpuDemo *demo)
{
    Lesson33State *state = (Lesson33State *)SDL_calloc(1, sizeof(*state));

    if (!state) {
        SDL_OutOfMemory();
        return false;
    }
    demo->lesson.private_state = state;
    state->truck.model_matrix = mat4_identity();
    state->boxes[0].model_matrix = mat4_identity();

    lesson33_init_camera(demo);
    state->light_vp = lesson33_light_view_projection();

    if (!lesson33_create_samplers(demo)) {
        return false;
    }
    if (!lesson33_load_scene(demo, "models/CesiumMilkTruck/CesiumMilkTruck.gltf", &state->truck_scene) ||
        !lesson33_upload_pulled_model(demo, &state->truck_scene, &state->truck)) {
        return false;
    }
    if (!lesson33_load_scene(demo, "models/BoxTextured/BoxTextured.gltf", &state->box_scene) ||
        !lesson33_upload_pulled_model(demo, &state->box_scene, &state->boxes[0])) {
        return false;
    }
    lesson33_share_box_resources(state);
    lesson33_build_box_transforms(state);

    state->shadow_depth = ForgeGpuCreateSampledDepthTexture(
        demo,
        LESSON33_SHADOW_MAP_SIZE,
        LESSON33_SHADOW_MAP_SIZE,
        LESSON33_DEPTH_FORMAT);
    if (!state->shadow_depth) {
        return false;
    }
    return lesson33_create_grid(demo) && lesson33_create_pipelines(demo);
}

bool ForgeGpuRenderLesson33(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPUTexture *swapchain_texture,
    Uint32 width,
    Uint32 height)
{
    Lesson33State *state = lesson33_state(demo);
    Mat4 view;
    Mat4 projection;
    Mat4 cam_vp;

    ForgeGpuUpdateCameraFromInput(demo);
    if (!lesson33_ensure_main_depth(demo, width, height)) {
        return false;
    }

    ForgeGpuCameraViewProjection(demo, width, height, LESSON33_FAR_PLANE, &view, &projection);
    cam_vp = mat4_multiply(projection, view);
    state->shadow_pass_rendered = false;
    state->main_pass_rendered = false;

    return lesson33_run_shadow_pass(demo, command_buffer) &&
           lesson33_run_scene_pass(demo, command_buffer, swapchain_texture, cam_vp);
}

void ForgeGpuDebugLesson33(ForgeGpuDemo *demo)
{
    Lesson33State *state = lesson33_state(demo);

    if (!state) {
        return;
    }
    ImGui::Text("Vertex pulling");
    ImGui::Text("Truck primitives: %d", state->truck.primitive_count);
    ImGui::Text("Box instances: %d", LESSON33_BOX_COUNT);
    ImGui::Text("Vertex input: storage buffer");
}

void ForgeGpuExportLesson33Metrics(ForgeGpuDemo *demo)
{
    Lesson33State *state = lesson33_state(demo);

    if (!state) {
        return;
    }
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuVertexPulling", 1.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuVertexStorageBuffers", 1.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuVertexPullingTruckPrimitives", (double)state->truck.primitive_count);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuVertexPullingBoxInstances", (double)LESSON33_BOX_COUNT);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuVertexPullingPassesRendered", state->shadow_pass_rendered && state->main_pass_rendered ? 1.0 : 0.0);
}

void ForgeGpuDestroyLesson33(ForgeGpuDemo *demo)
{
    Lesson33State *state = lesson33_state(demo);

    if (!state) {
        return;
    }

    lesson33_release_object_resources(demo, &state->truck);
    lesson33_release_object_resources(demo, &state->boxes[0]);
    ForgeGpuFreeLoadedScene(&state->truck_scene);
    ForgeGpuFreeLoadedScene(&state->box_scene);

    if (state->pulled_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, state->pulled_pipeline);
    }
    if (state->shadow_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, state->shadow_pipeline);
    }
    if (state->grid_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, state->grid_pipeline);
    }
    if (state->shadow_depth) {
        SDL_ReleaseGPUTexture(demo->device, state->shadow_depth);
    }
    if (state->main_depth) {
        SDL_ReleaseGPUTexture(demo->device, state->main_depth);
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
