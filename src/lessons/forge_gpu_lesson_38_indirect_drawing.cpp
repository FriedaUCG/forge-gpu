#include "forge_gpu_lessons.h"

#include "forge_gpu_browser_status.h"
#include "forge_gpu_camera.h"
#include "forge_gpu_gpu_helpers.h"
#include "forge_gpu_lesson_common.h"
#include "forge_gpu_math.h"
#include "forge_gpu_scene.h"
#include "forge_gpu_shader_layouts.h"
#include "shaders/generated/forge_gpu_lesson_38_shaders.h"
#include "imgui.h"

#include <stddef.h>

#define LESSON38_NUM_BOXES 200u
#define LESSON38_SHADOW_MAP_SIZE 2048u
#define LESSON38_DEPTH_FORMAT SDL_GPU_TEXTUREFORMAT_D32_FLOAT
#define LESSON38_FOV_DEGREES 60.0f
#define LESSON38_DEBUG_FOV_DEGREES 70.0f
#define LESSON38_NEAR_PLANE 0.1f
#define LESSON38_FAR_PLANE 200.0f
#define LESSON38_MOVE_SPEED 8.0f
#define LESSON38_MOUSE_SENSITIVITY 0.003f
#define LESSON38_PITCH_CLAMP 1.5f
#define LESSON38_CAM_START_X 15.0f
#define LESSON38_CAM_START_Y 8.0f
#define LESSON38_CAM_START_Z 15.0f
#define LESSON38_CAM_START_YAW_DEG 45.0f
#define LESSON38_CAM_START_PITCH_DEG -20.0f
#define LESSON38_DEBUG_CAM_HEIGHT 45.0f
#define LESSON38_DEBUG_CAM_BACK 35.0f
#define LESSON38_LIGHT_DIR_X 0.6f
#define LESSON38_LIGHT_DIR_Y 1.0f
#define LESSON38_LIGHT_DIR_Z 0.4f
#define LESSON38_SCENE_AMBIENT 0.12f
#define LESSON38_SCENE_SHININESS 64.0f
#define LESSON38_SCENE_SPECULAR_STRENGTH 0.4f
#define LESSON38_GRID_HALF_SIZE 50.0f
#define LESSON38_GRID_SPACING 1.0f
#define LESSON38_GRID_LINE_WIDTH 0.02f
#define LESSON38_GRID_FADE_DIST 40.0f
#define LESSON38_GRID_AMBIENT 0.15f
#define LESSON38_GRID_SHININESS 32.0f
#define LESSON38_GRID_SPECULAR_STRENGTH 0.2f
#define LESSON38_GRID_LINE_R 0.068f
#define LESSON38_GRID_LINE_G 0.534f
#define LESSON38_GRID_LINE_B 0.932f
#define LESSON38_GRID_BG_R 0.014f
#define LESSON38_GRID_BG_G 0.014f
#define LESSON38_GRID_BG_B 0.045f
#define LESSON38_CLEAR_R 0.02f
#define LESSON38_CLEAR_G 0.02f
#define LESSON38_CLEAR_B 0.03f
#define LESSON38_SHADOW_ORTHO_HALF 30.0f
#define LESSON38_SHADOW_LIGHT_DIST 40.0f
#define LESSON38_WORKGROUP_SIZE 64u
#define LESSON38_BOX_GRID_SPACING 2.8f
#define LESSON38_BOX_GROUND_Y 0.5f
#define LESSON38_BOX_ROTATION_STEP 0.4f
#define LESSON38_PLANE_NORM_EPS 0.0001f
#define LESSON38_FRUSTUM_LINE_VERTS 24u
#define LESSON38_FRUSTUM_PLANES 6
#define LESSON38_FRUSTUM_CORNERS 8
#define LESSON38_CUBE_BOUNDING_SPHERE_SCALE 0.866f
#define LESSON38_BOX_GRID_COLS 15
#define LESSON38_BOX_MIN_CLEARANCE 3.0f
#define LESSON38_BOX_PUSH_DIST 4.0f
#define LESSON38_BOX_STACK_MOD 7
#define LESSON38_BOX_STACK_Y 1.5f
#define LESSON38_BOX_BASE_SCALE 0.8f
#define LESSON38_BOX_SCALE_VAR 0.2f
#define LESSON38_MAX_TRUCK_INSTANCES 64

struct Lesson38ObjectGpuData
{
    Mat4 model;
    float color[4];
    float bounding_sphere[4];
    Uint32 num_indices;
    Uint32 first_index;
    Sint32 vertex_offset;
    Uint32 pad;
};

struct Lesson38TruckInstanceData
{
    Mat4 model;
};

struct Lesson38LineVertex
{
    float position[3];
    float color[4];
};

struct Lesson38VertUniforms
{
    Mat4 vp;
    Mat4 light_vp;
};

struct Lesson38ShadowUniforms
{
    Mat4 light_vp;
};

struct Lesson38BoxFragUniforms
{
    float light_dir[4];
    float eye_pos[4];
    float shadow_texel;
    float shininess;
    float ambient;
    float specular_str;
};

struct Lesson38TruckFragUniforms
{
    float base_color[4];
    float light_dir[4];
    float eye_pos[4];
    float shadow_texel;
    float shininess;
    float ambient;
    float specular_str;
    Uint32 has_texture;
    float pad[7];
};

struct Lesson38GridVertUniforms
{
    Mat4 vp;
};

struct Lesson38GridFragUniforms
{
    float line_color[4];
    float bg_color[4];
    float light_dir[4];
    float eye_pos[4];
    Mat4 light_vp;
    float grid_spacing;
    float line_width;
    float fade_distance;
    float ambient;
    float shininess;
    float specular_str;
    float shadow_texel;
    float pad;
};

struct Lesson38CullUniforms
{
    float frustum_planes[LESSON38_FRUSTUM_PLANES][4];
    Uint32 num_objects;
    Uint32 enable_culling;
    float pad[2];
};

struct Lesson38DebugVertUniforms
{
    Mat4 vp;
};

struct Lesson38LineVertUniforms
{
    Mat4 vp;
};

struct Lesson38State
{
    SDL_GPUComputePipeline *cull_pipeline;
    SDL_GPUGraphicsPipeline *indirect_box_pipeline;
    SDL_GPUGraphicsPipeline *indirect_shadow_pipeline;
    SDL_GPUGraphicsPipeline *debug_box_pipeline;
    SDL_GPUGraphicsPipeline *frustum_line_pipeline;
    SDL_GPUGraphicsPipeline *grid_pipeline;
    SDL_GPUGraphicsPipeline *truck_pipeline;
    SDL_GPUGraphicsPipeline *truck_shadow_pipeline;
    SDL_GPUTexture *main_depth;
    SDL_GPUTexture *shadow_depth;
    SDL_GPUSampler *shadow_sampler;
    GpuSceneData box_model;
    GpuSceneData truck_model;
    SDL_GPUBuffer *object_data_buf;
    SDL_GPUBuffer *indirect_buf;
    SDL_GPUBuffer *visibility_buf;
    SDL_GPUBuffer *instance_id_buf;
    SDL_GPUBuffer *frustum_line_buf;
    SDL_GPUTransferBuffer *frustum_line_upload;
    SDL_GPUTransferBuffer *visibility_readback;
    SDL_GPUBuffer *truck_instance_buf;
    SDL_GPUBuffer *grid_vertex_buf;
    SDL_GPUBuffer *grid_index_buf;
    Lesson38ObjectGpuData objects[LESSON38_NUM_BOXES];
    int truck_node_to_inst[LESSON38_MAX_TRUCK_INSTANCES];
    int truck_instance_count;
    int box_primitive_index;
    Uint32 main_depth_width;
    Uint32 main_depth_height;
    Uint32 visible_count;
    bool culling_enabled;
    bool debug_view_enabled;
    bool compute_pass_ran;
    bool shadow_pass_rendered;
    bool main_pass_rendered;
    bool debug_pass_rendered;
    bool indirect_draws_issued;
    bool validation_readback_pending;
    bool validation_readback_completed;
    bool validation_readback_scheduled;
};

static_assert(sizeof(Lesson38ObjectGpuData) == 112, "lesson 38 object data size must match HLSL layout");
static_assert(offsetof(Lesson38ObjectGpuData, model) == 0, "lesson 38 object model offset must match vertex attributes");
static_assert(offsetof(Lesson38ObjectGpuData, color) == 64, "lesson 38 object color offset must match vertex attributes");
static_assert(offsetof(Lesson38ObjectGpuData, bounding_sphere) == 80, "lesson 38 object bounds offset must match compute shader");
static_assert(sizeof(Lesson38TruckInstanceData) == 64, "lesson 38 truck instance size must match HLSL layout");
static_assert(sizeof(Lesson38LineVertex) == 28, "lesson 38 line vertex size must match HLSL layout");
static_assert(sizeof(Lesson38VertUniforms) == 128, "lesson 38 vertex uniform size must match HLSL layout");
static_assert(sizeof(Lesson38ShadowUniforms) == 64, "lesson 38 shadow uniform size must match HLSL layout");
static_assert(sizeof(Lesson38BoxFragUniforms) == 48, "lesson 38 box fragment uniform size must match HLSL layout");
static_assert(sizeof(Lesson38TruckFragUniforms) == 96, "lesson 38 truck fragment uniform size must match HLSL layout");
static_assert(sizeof(Lesson38GridVertUniforms) == 64, "lesson 38 grid vertex uniform size must match HLSL layout");
static_assert(sizeof(Lesson38GridFragUniforms) == 160, "lesson 38 grid fragment uniform size must match HLSL layout");
static_assert(sizeof(Lesson38CullUniforms) == 112, "lesson 38 compute uniform size must match HLSL layout");
static_assert(sizeof(Lesson38DebugVertUniforms) == 64, "lesson 38 debug vertex uniform size must match HLSL layout");
static_assert(sizeof(Lesson38LineVertUniforms) == 64, "lesson 38 line vertex uniform size must match HLSL layout");

static Lesson38State *lesson38_state(ForgeGpuDemo *demo)
{
    return (Lesson38State *)demo->lesson.private_state;
}

static void lesson38_init_camera(ForgeGpuDemo *demo)
{
    demo->lesson.camera_position = {
        LESSON38_CAM_START_X,
        LESSON38_CAM_START_Y,
        LESSON38_CAM_START_Z
    };
    demo->lesson.camera_yaw = LESSON38_CAM_START_YAW_DEG * FORGE_GPU_DEG2RAD;
    demo->lesson.camera_pitch = LESSON38_CAM_START_PITCH_DEG * FORGE_GPU_DEG2RAD;
    demo->lesson.pitch_clamp = LESSON38_PITCH_CLAMP;
    demo->lesson.mouse_sensitivity = LESSON38_MOUSE_SENSITIVITY;
    demo->lesson.move_speed = LESSON38_MOVE_SPEED;
    demo->lesson.last_ticks = SDL_GetTicks();
}

static Vec3 lesson38_light_dir(void)
{
    return vec3_normalize({ LESSON38_LIGHT_DIR_X, LESSON38_LIGHT_DIR_Y, LESSON38_LIGHT_DIR_Z });
}

static Mat4 lesson38_light_view_projection(void)
{
    const Vec3 light_dir = lesson38_light_dir();
    const Mat4 light_view = mat4_look_at(
        vec3_scale(light_dir, LESSON38_SHADOW_LIGHT_DIST),
        { 0.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f });
    const Mat4 light_proj = mat4_orthographic(
        -LESSON38_SHADOW_ORTHO_HALF,
        LESSON38_SHADOW_ORTHO_HALF,
        -LESSON38_SHADOW_ORTHO_HALF,
        LESSON38_SHADOW_ORTHO_HALF,
        0.1f,
        100.0f);
    return mat4_multiply(light_proj, light_view);
}

static void lesson38_extract_frustum_planes(Mat4 vp, float planes[LESSON38_FRUSTUM_PLANES][4])
{
    float m[16];

    SDL_memcpy(m, vp.m, sizeof(m));

    planes[0][0] = m[3] + m[0];
    planes[0][1] = m[7] + m[4];
    planes[0][2] = m[11] + m[8];
    planes[0][3] = m[15] + m[12];
    planes[1][0] = m[3] - m[0];
    planes[1][1] = m[7] - m[4];
    planes[1][2] = m[11] - m[8];
    planes[1][3] = m[15] - m[12];
    planes[2][0] = m[3] + m[1];
    planes[2][1] = m[7] + m[5];
    planes[2][2] = m[11] + m[9];
    planes[2][3] = m[15] + m[13];
    planes[3][0] = m[3] - m[1];
    planes[3][1] = m[7] - m[5];
    planes[3][2] = m[11] - m[9];
    planes[3][3] = m[15] - m[13];
    planes[4][0] = m[2];
    planes[4][1] = m[6];
    planes[4][2] = m[10];
    planes[4][3] = m[14];
    planes[5][0] = m[3] - m[2];
    planes[5][1] = m[7] - m[6];
    planes[5][2] = m[11] - m[10];
    planes[5][3] = m[15] - m[14];

    for (int i = 0; i < LESSON38_FRUSTUM_PLANES; i += 1) {
        const float len = SDL_sqrtf(
            planes[i][0] * planes[i][0] +
            planes[i][1] * planes[i][1] +
            planes[i][2] * planes[i][2]);
        if (len > LESSON38_PLANE_NORM_EPS) {
            planes[i][0] /= len;
            planes[i][1] /= len;
            planes[i][2] /= len;
            planes[i][3] /= len;
        }
    }
}

static void lesson38_compute_frustum_corners(Mat4 vp, Vec3 out_corners[LESSON38_FRUSTUM_CORNERS])
{
    static const float ndc[LESSON38_FRUSTUM_CORNERS][4] = {
        { -1.0f, -1.0f, 0.0f, 1.0f },
        {  1.0f, -1.0f, 0.0f, 1.0f },
        {  1.0f,  1.0f, 0.0f, 1.0f },
        { -1.0f,  1.0f, 0.0f, 1.0f },
        { -1.0f, -1.0f, 1.0f, 1.0f },
        {  1.0f, -1.0f, 1.0f, 1.0f },
        {  1.0f,  1.0f, 1.0f, 1.0f },
        { -1.0f,  1.0f, 1.0f, 1.0f },
    };
    const Mat4 inv_vp = mat4_inverse(vp);

    for (int i = 0; i < LESSON38_FRUSTUM_CORNERS; i += 1) {
        const Vec4 clip = vec4_create(ndc[i][0], ndc[i][1], ndc[i][2], ndc[i][3]);
        const Vec4 world = mat4_multiply_vec4(inv_vp, clip);
        const float inv_w = 1.0f / world.w;
        out_corners[i] = { world.x * inv_w, world.y * inv_w, world.z * inv_w };
    }
}

static void lesson38_build_frustum_line_vertices(
    const Vec3 corners[LESSON38_FRUSTUM_CORNERS],
    Lesson38LineVertex out_vertices[LESSON38_FRUSTUM_LINE_VERTS])
{
    static const int edges[12][2] = {
        { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 },
        { 4, 5 }, { 5, 6 }, { 6, 7 }, { 7, 4 },
        { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 },
    };

    for (int i = 0; i < 12; i += 1) {
        Lesson38LineVertex *a = &out_vertices[i * 2 + 0];
        Lesson38LineVertex *b = &out_vertices[i * 2 + 1];

        a->position[0] = corners[edges[i][0]].x;
        a->position[1] = corners[edges[i][0]].y;
        a->position[2] = corners[edges[i][0]].z;
        b->position[0] = corners[edges[i][1]].x;
        b->position[1] = corners[edges[i][1]].y;
        b->position[2] = corners[edges[i][1]].z;
        a->color[0] = b->color[0] = 1.0f;
        a->color[1] = b->color[1] = 0.9f;
        a->color[2] = b->color[2] = 0.0f;
        a->color[3] = b->color[3] = 1.0f;
    }
}

static bool lesson38_upload_frustum_lines(
    ForgeGpuDemo *demo,
    Lesson38State *state,
    SDL_GPUCommandBuffer *command_buffer,
    const Lesson38LineVertex vertices[LESSON38_FRUSTUM_LINE_VERTS])
{
    void *mapped;
    SDL_GPUCopyPass *copy_pass;
    SDL_GPUTransferBufferLocation source;
    SDL_GPUBufferRegion destination;

    mapped = SDL_MapGPUTransferBuffer(demo->device, state->frustum_line_upload, true);
    if (!mapped) {
        return false;
    }
    SDL_memcpy(mapped, vertices, LESSON38_FRUSTUM_LINE_VERTS * sizeof(*vertices));
    SDL_UnmapGPUTransferBuffer(demo->device, state->frustum_line_upload);

    copy_pass = SDL_BeginGPUCopyPass(command_buffer);
    if (!copy_pass) {
        return false;
    }

    SDL_zero(source);
    source.transfer_buffer = state->frustum_line_upload;
    SDL_zero(destination);
    destination.buffer = state->frustum_line_buf;
    destination.size = LESSON38_FRUSTUM_LINE_VERTS * (Uint32)sizeof(*vertices);
    SDL_UploadToGPUBuffer(copy_pass, &source, &destination, false);
    SDL_EndGPUCopyPass(copy_pass);
    return true;
}

static void lesson38_decode_pending_readback(ForgeGpuDemo *demo, Lesson38State *state)
{
    const Uint32 *visibility;
    Uint32 count = 0;

    if (!state->validation_readback_pending) {
        return;
    }
    if (!SDL_WaitForGPUIdle(demo->device)) {
        SDL_Log("lesson 38 visibility readback wait failed: %s", SDL_GetError());
        state->validation_readback_pending = false;
        state->validation_readback_scheduled = false;
        return;
    }

    visibility = (const Uint32 *)SDL_MapGPUTransferBuffer(demo->device, state->visibility_readback, false);
    if (!visibility) {
        SDL_Log("lesson 38 visibility readback map failed: %s", SDL_GetError());
        state->validation_readback_pending = false;
        state->validation_readback_scheduled = false;
        return;
    }
    for (Uint32 i = 0; i < LESSON38_NUM_BOXES; i += 1) {
        count += visibility[i] != 0u ? 1u : 0u;
    }
    SDL_UnmapGPUTransferBuffer(demo->device, state->visibility_readback);
    state->visible_count = count;
    state->validation_readback_completed = true;
    state->validation_readback_pending = false;
}

static void lesson38_schedule_visibility_readback(
    Lesson38State *state,
    SDL_GPUCommandBuffer *command_buffer)
{
    SDL_GPUCopyPass *copy_pass;
    SDL_GPUBufferRegion source;
    SDL_GPUTransferBufferLocation destination;

    if (state->validation_readback_pending || state->validation_readback_scheduled || !state->visibility_readback) {
        return;
    }

    copy_pass = SDL_BeginGPUCopyPass(command_buffer);
    if (!copy_pass) {
        return;
    }
    SDL_zero(source);
    source.buffer = state->visibility_buf;
    source.size = LESSON38_NUM_BOXES * (Uint32)sizeof(Uint32);
    SDL_zero(destination);
    destination.transfer_buffer = state->visibility_readback;
    SDL_DownloadFromGPUBuffer(copy_pass, &source, &destination);
    SDL_EndGPUCopyPass(copy_pass);
    state->validation_readback_pending = true;
    state->validation_readback_scheduled = true;
}

static void lesson38_fill_mesh_vertex_input(
    SDL_GPUVertexBufferDescription *vertex_buffer,
    SDL_GPUVertexAttribute attributes[3])
{
    SDL_zero(*vertex_buffer);
    vertex_buffer->slot = 0;
    vertex_buffer->pitch = sizeof(ForgeGpuMeshVertex);
    vertex_buffer->input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

    SDL_memset(attributes, 0, 3 * sizeof(*attributes));
    attributes[0].location = 0;
    attributes[0].buffer_slot = 0;
    attributes[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    attributes[0].offset = offsetof(ForgeGpuMeshVertex, position);
    attributes[1].location = 1;
    attributes[1].buffer_slot = 0;
    attributes[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    attributes[1].offset = offsetof(ForgeGpuMeshVertex, normal);
    attributes[2].location = 2;
    attributes[2].buffer_slot = 0;
    attributes[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    attributes[2].offset = offsetof(ForgeGpuMeshVertex, uv);
}

static void lesson38_fill_box_vertex_input(
    SDL_GPUVertexBufferDescription vertex_buffers[2],
    SDL_GPUVertexAttribute attributes[4])
{
    lesson38_fill_mesh_vertex_input(&vertex_buffers[0], attributes);
    SDL_zero(vertex_buffers[1]);
    vertex_buffers[1].slot = 1;
    vertex_buffers[1].pitch = sizeof(Uint32);
    vertex_buffers[1].input_rate = SDL_GPU_VERTEXINPUTRATE_INSTANCE;

    attributes[3].location = 3;
    attributes[3].buffer_slot = 1;
    attributes[3].format = SDL_GPU_VERTEXELEMENTFORMAT_UINT;
    attributes[3].offset = 0;
}

static void lesson38_fill_truck_vertex_input(
    SDL_GPUVertexBufferDescription vertex_buffers[2],
    SDL_GPUVertexAttribute attributes[7])
{
    SDL_GPUVertexAttribute mesh_attributes[3];

    lesson38_fill_mesh_vertex_input(&vertex_buffers[0], mesh_attributes);
    SDL_memcpy(attributes, mesh_attributes, sizeof(mesh_attributes));

    SDL_zero(vertex_buffers[1]);
    vertex_buffers[1].slot = 1;
    vertex_buffers[1].pitch = sizeof(Lesson38TruckInstanceData);
    vertex_buffers[1].input_rate = SDL_GPU_VERTEXINPUTRATE_INSTANCE;

    for (Uint32 i = 0; i < 4; i += 1) {
        attributes[3 + i].location = 3 + i;
        attributes[3 + i].buffer_slot = 1;
        attributes[3 + i].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
        attributes[3 + i].offset = i * 16u;
    }
}

static SDL_GPUGraphicsPipeline *lesson38_create_graphics_pipeline(
    ForgeGpuDemo *demo,
    SDL_GPUShader *vertex_shader,
    SDL_GPUShader *fragment_shader,
    SDL_GPUPrimitiveType primitive_type,
    const SDL_GPUColorTargetDescription *color_target,
    Uint32 num_color_targets,
    const SDL_GPUVertexBufferDescription *vertex_buffers,
    Uint32 num_vertex_buffers,
    const SDL_GPUVertexAttribute *attributes,
    Uint32 num_attributes,
    bool has_depth,
    SDL_GPUTextureFormat depth_format,
    bool depth_test,
    bool depth_write,
    SDL_GPUCompareOp compare_op,
    SDL_GPUCullMode cull_mode,
    float depth_bias_constant,
    float depth_bias_slope)
{
    SDL_GPUGraphicsPipelineCreateInfo pipeline_info;

    SDL_zero(pipeline_info);
    pipeline_info.vertex_shader = vertex_shader;
    pipeline_info.fragment_shader = fragment_shader;
    pipeline_info.primitive_type = primitive_type;
    pipeline_info.vertex_input_state.vertex_buffer_descriptions = vertex_buffers;
    pipeline_info.vertex_input_state.num_vertex_buffers = num_vertex_buffers;
    pipeline_info.vertex_input_state.vertex_attributes = attributes;
    pipeline_info.vertex_input_state.num_vertex_attributes = num_attributes;
    pipeline_info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    pipeline_info.rasterizer_state.cull_mode = cull_mode;
    pipeline_info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    pipeline_info.rasterizer_state.enable_depth_bias = depth_bias_constant != 0.0f || depth_bias_slope != 0.0f;
    pipeline_info.rasterizer_state.depth_bias_constant_factor = depth_bias_constant;
    pipeline_info.rasterizer_state.depth_bias_slope_factor = depth_bias_slope;
    pipeline_info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    pipeline_info.depth_stencil_state.enable_depth_test = depth_test;
    pipeline_info.depth_stencil_state.enable_depth_write = depth_write;
    pipeline_info.depth_stencil_state.compare_op = compare_op;
    pipeline_info.target_info.color_target_descriptions = color_target;
    pipeline_info.target_info.num_color_targets = num_color_targets;
    pipeline_info.target_info.has_depth_stencil_target = has_depth;
    pipeline_info.target_info.depth_stencil_format = depth_format;
    return SDL_CreateGPUGraphicsPipeline(demo->device, &pipeline_info);
}

static void lesson38_release_shader(SDL_GPUDevice *device, SDL_GPUShader **shader)
{
    if (*shader) {
        SDL_ReleaseGPUShader(device, *shader);
        *shader = nullptr;
    }
}

static bool lesson38_create_pipelines(ForgeGpuDemo *demo, Lesson38State *state)
{
    SDL_GPUShader *ibox_vert = nullptr;
    SDL_GPUShader *ibox_frag = nullptr;
    SDL_GPUShader *ishadow_vert = nullptr;
    SDL_GPUShader *ishadow_frag = nullptr;
    SDL_GPUShader *dbox_vert = nullptr;
    SDL_GPUShader *dbox_frag = nullptr;
    SDL_GPUShader *fline_vert = nullptr;
    SDL_GPUShader *fline_frag = nullptr;
    SDL_GPUShader *grid_vert = nullptr;
    SDL_GPUShader *grid_frag = nullptr;
    SDL_GPUShader *truck_vert = nullptr;
    SDL_GPUShader *truck_frag = nullptr;
    SDL_GPUColorTargetDescription color_target;
    SDL_GPUColorTargetDescription blended_color_target;
    SDL_GPUVertexBufferDescription box_vbs[2];
    SDL_GPUVertexAttribute box_attrs[4];
    SDL_GPUVertexBufferDescription truck_vbs[2];
    SDL_GPUVertexAttribute truck_attrs[7];
    SDL_GPUVertexBufferDescription line_vb;
    SDL_GPUVertexAttribute line_attrs[2];
    SDL_GPUVertexBufferDescription grid_vb;
    SDL_GPUVertexAttribute grid_attr;
    bool ok = false;

    ibox_vert = ForgeGpuCreateShader(demo->device, SDL_GPU_SHADERSTAGE_VERTEX,
        lesson38_indirect_box_vert_wgsl, lesson38_indirect_box_vert_wgsl_size,
        lesson38_indirect_box_vert_msl, lesson38_indirect_box_vert_msl_size,
        0, 0, 1, 1);
    ibox_frag = ForgeGpuCreateShaderWithResourceLayout(demo->device,
        lesson38_indirect_box_frag_wgsl, lesson38_indirect_box_frag_wgsl_size,
        lesson38_indirect_box_frag_msl, lesson38_indirect_box_frag_msl_size,
        ForgeGpuShaderLayout_lesson38_indirect_box_frag());
    ishadow_vert = ForgeGpuCreateShader(demo->device, SDL_GPU_SHADERSTAGE_VERTEX,
        lesson38_indirect_shadow_vert_wgsl, lesson38_indirect_shadow_vert_wgsl_size,
        lesson38_indirect_shadow_vert_msl, lesson38_indirect_shadow_vert_msl_size,
        0, 0, 1, 1);
    ishadow_frag = ForgeGpuCreateShader(demo->device, SDL_GPU_SHADERSTAGE_FRAGMENT,
        lesson38_indirect_shadow_frag_wgsl, lesson38_indirect_shadow_frag_wgsl_size,
        lesson38_indirect_shadow_frag_msl, lesson38_indirect_shadow_frag_msl_size,
        0, 0, 0, 0);
    dbox_vert = ForgeGpuCreateShader(demo->device, SDL_GPU_SHADERSTAGE_VERTEX,
        lesson38_debug_box_vert_wgsl, lesson38_debug_box_vert_wgsl_size,
        lesson38_debug_box_vert_msl, lesson38_debug_box_vert_msl_size,
        0, 0, 1, 1);
    dbox_frag = ForgeGpuCreateShader(demo->device, SDL_GPU_SHADERSTAGE_FRAGMENT,
        lesson38_debug_box_frag_wgsl, lesson38_debug_box_frag_wgsl_size,
        lesson38_debug_box_frag_msl, lesson38_debug_box_frag_msl_size,
        0, 0, 1, 0);
    fline_vert = ForgeGpuCreateShader(demo->device, SDL_GPU_SHADERSTAGE_VERTEX,
        lesson38_frustum_lines_vert_wgsl, lesson38_frustum_lines_vert_wgsl_size,
        lesson38_frustum_lines_vert_msl, lesson38_frustum_lines_vert_msl_size,
        0, 0, 0, 1);
    fline_frag = ForgeGpuCreateShader(demo->device, SDL_GPU_SHADERSTAGE_FRAGMENT,
        lesson38_frustum_lines_frag_wgsl, lesson38_frustum_lines_frag_wgsl_size,
        lesson38_frustum_lines_frag_msl, lesson38_frustum_lines_frag_msl_size,
        0, 0, 0, 0);
    grid_vert = ForgeGpuCreateShader(demo->device, SDL_GPU_SHADERSTAGE_VERTEX,
        lesson38_grid_vert_wgsl, lesson38_grid_vert_wgsl_size,
        lesson38_grid_vert_msl, lesson38_grid_vert_msl_size,
        0, 0, 0, 1);
    grid_frag = ForgeGpuCreateShaderWithResourceLayout(demo->device,
        lesson38_grid_frag_wgsl, lesson38_grid_frag_wgsl_size,
        lesson38_grid_frag_msl, lesson38_grid_frag_msl_size,
        ForgeGpuShaderLayout_lesson38_grid_frag());
    truck_vert = ForgeGpuCreateShader(demo->device, SDL_GPU_SHADERSTAGE_VERTEX,
        lesson38_truck_scene_vert_wgsl, lesson38_truck_scene_vert_wgsl_size,
        lesson38_truck_scene_vert_msl, lesson38_truck_scene_vert_msl_size,
        0, 0, 0, 1);
    truck_frag = ForgeGpuCreateShaderWithResourceLayout(demo->device,
        lesson38_truck_scene_frag_wgsl, lesson38_truck_scene_frag_wgsl_size,
        lesson38_truck_scene_frag_msl, lesson38_truck_scene_frag_msl_size,
        ForgeGpuShaderLayout_lesson38_truck_scene_frag());
    if (!ibox_vert || !ibox_frag || !ishadow_vert || !ishadow_frag ||
        !dbox_vert || !dbox_frag || !fline_vert || !fline_frag ||
        !grid_vert || !grid_frag || !truck_vert || !truck_frag) {
        goto done;
    }

    state->cull_pipeline = ForgeGpuCreateComputePipelineWithResourceLayout(
        demo->device,
        lesson38_frustum_cull_comp_wgsl, lesson38_frustum_cull_comp_wgsl_size,
        lesson38_frustum_cull_comp_msl, lesson38_frustum_cull_comp_msl_size,
        ForgeGpuComputePipelineLayout_lesson38_frustum_cull_comp(),
        LESSON38_WORKGROUP_SIZE, 1, 1);
    if (!state->cull_pipeline) {
        goto done;
    }

    SDL_zero(color_target);
    color_target.format = demo->color_format;
    blended_color_target = color_target;
    blended_color_target.blend_state.enable_blend = true;
    blended_color_target.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    blended_color_target.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    blended_color_target.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
    blended_color_target.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    blended_color_target.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    blended_color_target.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;

    lesson38_fill_box_vertex_input(box_vbs, box_attrs);
    state->indirect_box_pipeline = lesson38_create_graphics_pipeline(
        demo, ibox_vert, ibox_frag, SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        &color_target, 1, box_vbs, 2, box_attrs, 4,
        true, LESSON38_DEPTH_FORMAT, true, true, SDL_GPU_COMPAREOP_LESS,
        SDL_GPU_CULLMODE_BACK, 0.0f, 0.0f);
    state->indirect_shadow_pipeline = lesson38_create_graphics_pipeline(
        demo, ishadow_vert, ishadow_frag, SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        nullptr, 0, box_vbs, 2, box_attrs, 4,
        true, LESSON38_DEPTH_FORMAT, true, true, SDL_GPU_COMPAREOP_LESS,
        SDL_GPU_CULLMODE_BACK, 2.0f, 2.0f);
    state->debug_box_pipeline = lesson38_create_graphics_pipeline(
        demo, dbox_vert, dbox_frag, SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        &color_target, 1, box_vbs, 2, box_attrs, 4,
        true, LESSON38_DEPTH_FORMAT, true, true, SDL_GPU_COMPAREOP_LESS,
        SDL_GPU_CULLMODE_BACK, 0.0f, 0.0f);

    SDL_zero(line_vb);
    line_vb.slot = 0;
    line_vb.pitch = sizeof(Lesson38LineVertex);
    line_vb.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    SDL_zeroa(line_attrs);
    line_attrs[0].location = 0;
    line_attrs[0].buffer_slot = 0;
    line_attrs[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    line_attrs[0].offset = offsetof(Lesson38LineVertex, position);
    line_attrs[1].location = 1;
    line_attrs[1].buffer_slot = 0;
    line_attrs[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
    line_attrs[1].offset = offsetof(Lesson38LineVertex, color);
    state->frustum_line_pipeline = lesson38_create_graphics_pipeline(
        demo, fline_vert, fline_frag, SDL_GPU_PRIMITIVETYPE_LINELIST,
        &color_target, 1, &line_vb, 1, line_attrs, 2,
        true, LESSON38_DEPTH_FORMAT, true, false, SDL_GPU_COMPAREOP_LESS,
        SDL_GPU_CULLMODE_NONE, 0.0f, 0.0f);

    SDL_zero(grid_vb);
    grid_vb.slot = 0;
    grid_vb.pitch = 3u * sizeof(float);
    grid_vb.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    SDL_zero(grid_attr);
    grid_attr.location = 0;
    grid_attr.buffer_slot = 0;
    grid_attr.format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    grid_attr.offset = 0;
    state->grid_pipeline = lesson38_create_graphics_pipeline(
        demo, grid_vert, grid_frag, SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        &blended_color_target, 1, &grid_vb, 1, &grid_attr, 1,
        true, LESSON38_DEPTH_FORMAT, true, true, SDL_GPU_COMPAREOP_LESS_OR_EQUAL,
        SDL_GPU_CULLMODE_NONE, 0.0f, 0.0f);

    lesson38_fill_truck_vertex_input(truck_vbs, truck_attrs);
    state->truck_pipeline = lesson38_create_graphics_pipeline(
        demo, truck_vert, truck_frag, SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        &color_target, 1, truck_vbs, 2, truck_attrs, 7,
        true, LESSON38_DEPTH_FORMAT, true, true, SDL_GPU_COMPAREOP_LESS,
        SDL_GPU_CULLMODE_BACK, 0.0f, 0.0f);
    state->truck_shadow_pipeline = lesson38_create_graphics_pipeline(
        demo, truck_vert, ishadow_frag, SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        nullptr, 0, truck_vbs, 2, truck_attrs, 7,
        true, LESSON38_DEPTH_FORMAT, true, true, SDL_GPU_COMPAREOP_LESS,
        SDL_GPU_CULLMODE_BACK, 2.0f, 2.0f);

    ok = state->indirect_box_pipeline && state->indirect_shadow_pipeline &&
        state->debug_box_pipeline && state->frustum_line_pipeline &&
        state->grid_pipeline && state->truck_pipeline && state->truck_shadow_pipeline;

done:
    lesson38_release_shader(demo->device, &truck_frag);
    lesson38_release_shader(demo->device, &truck_vert);
    lesson38_release_shader(demo->device, &grid_frag);
    lesson38_release_shader(demo->device, &grid_vert);
    lesson38_release_shader(demo->device, &fline_frag);
    lesson38_release_shader(demo->device, &fline_vert);
    lesson38_release_shader(demo->device, &dbox_frag);
    lesson38_release_shader(demo->device, &dbox_vert);
    lesson38_release_shader(demo->device, &ishadow_frag);
    lesson38_release_shader(demo->device, &ishadow_vert);
    lesson38_release_shader(demo->device, &ibox_frag);
    lesson38_release_shader(demo->device, &ibox_vert);
    return ok;
}

static bool lesson38_create_shadow_sampler(ForgeGpuDemo *demo, Lesson38State *state)
{
    SDL_GPUSamplerCreateInfo sampler_info;

    SDL_zero(sampler_info);
    sampler_info.min_filter = SDL_GPU_FILTER_LINEAR;
    sampler_info.mag_filter = SDL_GPU_FILTER_LINEAR;
    sampler_info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    sampler_info.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler_info.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler_info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler_info.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
    sampler_info.enable_compare = true;
    state->shadow_sampler = SDL_CreateGPUSampler(demo->device, &sampler_info);
    return state->shadow_sampler != nullptr;
}

static bool lesson38_find_box_primitive(Lesson38State *state)
{
    const ForgeGpuLoadedScene *scene = &state->box_model.loaded;

    for (int ni = 0; ni < scene->node_count; ni += 1) {
        const ForgeGpuSceneNode *node = &scene->nodes[ni];
        if (node->mesh_index < 0 || node->mesh_index >= scene->mesh_count) {
            continue;
        }
        const ForgeGpuSceneMesh *mesh = &scene->meshes[node->mesh_index];
        if (mesh->primitive_count > 0) {
            const int primitive_index = mesh->first_primitive;
            if (primitive_index >= 0 && primitive_index < state->box_model.primitive_count) {
                state->box_primitive_index = primitive_index;
                return true;
            }
        }
    }

    SDL_SetError("lesson 38 BoxTextured model has no drawable primitive");
    return false;
}

static bool lesson38_create_truck_instances(ForgeGpuDemo *demo, Lesson38State *state)
{
    const ForgeGpuLoadedScene *scene = &state->truck_model.loaded;
    Lesson38TruckInstanceData instances[LESSON38_MAX_TRUCK_INSTANCES];
    int count = 0;

    for (int i = 0; i < LESSON38_MAX_TRUCK_INSTANCES; i += 1) {
        state->truck_node_to_inst[i] = -1;
    }

    for (int ni = 0; ni < scene->node_count && ni < LESSON38_MAX_TRUCK_INSTANCES; ni += 1) {
        if (scene->nodes[ni].mesh_index < 0) {
            continue;
        }
        if (count >= LESSON38_MAX_TRUCK_INSTANCES) {
            SDL_SetError("lesson 38 truck has too many mesh-bearing nodes");
            return false;
        }
        state->truck_node_to_inst[ni] = count;
        instances[count].model = mat4_from_forge(scene->nodes[ni].world_transform);
        count += 1;
    }

    if (count == 0) {
        SDL_SetError("lesson 38 truck has no mesh-bearing nodes");
        return false;
    }

    state->truck_instance_buf = ForgeGpuCreateBufferWithData(
        demo->device,
        SDL_GPU_BUFFERUSAGE_VERTEX,
        instances,
        (Uint32)((size_t)count * sizeof(*instances)));
    if (!state->truck_instance_buf) {
        return false;
    }
    state->truck_instance_count = count;
    return true;
}

static void lesson38_fill_object_data(Lesson38State *state)
{
    const ForgeGpuLoadedScene *scene = &state->box_model.loaded;
    const GpuPrimitive *primitive = &state->box_model.primitives[state->box_primitive_index];
    Mat4 box_mesh_base = mat4_identity();

    for (int ni = 0; ni < scene->node_count; ni += 1) {
        const ForgeGpuSceneNode *node = &scene->nodes[ni];
        if (node->mesh_index < 0 || node->mesh_index >= scene->mesh_count) {
            continue;
        }
        const ForgeGpuSceneMesh *mesh = &scene->meshes[node->mesh_index];
        if (state->box_primitive_index >= mesh->first_primitive &&
            state->box_primitive_index < mesh->first_primitive + mesh->primitive_count) {
            box_mesh_base = mat4_from_forge(node->world_transform);
            break;
        }
    }

    for (Uint32 i = 0; i < LESSON38_NUM_BOXES; i += 1) {
        const int grid_x = (int)(i % LESSON38_BOX_GRID_COLS) - LESSON38_BOX_GRID_COLS / 2;
        const int grid_z = (int)(i / LESSON38_BOX_GRID_COLS) - LESSON38_BOX_GRID_COLS / 2;
        float x = (float)grid_x * LESSON38_BOX_GRID_SPACING;
        float z = (float)grid_z * LESSON38_BOX_GRID_SPACING;
        float y = LESSON38_BOX_GROUND_Y;
        const float scale = LESSON38_BOX_BASE_SCALE + (float)(i % 3u) * LESSON38_BOX_SCALE_VAR;
        Mat4 model;

        if (SDL_fabsf(x) < LESSON38_BOX_MIN_CLEARANCE && SDL_fabsf(z) < LESSON38_BOX_MIN_CLEARANCE) {
            x += x >= 0.0f ? LESSON38_BOX_PUSH_DIST : -LESSON38_BOX_PUSH_DIST;
        }
        if (i % LESSON38_BOX_STACK_MOD == 0u && i > 0u) {
            x = state->objects[i - 1u].bounding_sphere[0];
            z = state->objects[i - 1u].bounding_sphere[2];
            y = LESSON38_BOX_STACK_Y;
        }

        model = mat4_multiply(
            mat4_translate({ x, y, z }),
            mat4_multiply(
                mat4_rotate_y((float)i * LESSON38_BOX_ROTATION_STEP),
                mat4_multiply(mat4_scale(scale), box_mesh_base)));

        state->objects[i].model = model;
        state->objects[i].color[0] = 1.0f;
        state->objects[i].color[1] = 1.0f;
        state->objects[i].color[2] = 1.0f;
        state->objects[i].color[3] = 1.0f;
        state->objects[i].bounding_sphere[0] = x;
        state->objects[i].bounding_sphere[1] = y;
        state->objects[i].bounding_sphere[2] = z;
        state->objects[i].bounding_sphere[3] = scale * LESSON38_CUBE_BOUNDING_SPHERE_SCALE;
        state->objects[i].num_indices = primitive->index_count;
        state->objects[i].first_index = 0;
        state->objects[i].vertex_offset = 0;
        state->objects[i].pad = 0;
    }
}

static bool lesson38_create_buffers(ForgeGpuDemo *demo, Lesson38State *state)
{
    Uint32 instance_ids[LESSON38_NUM_BOXES];
    const float grid_vertices[] = {
        -LESSON38_GRID_HALF_SIZE, 0.0f, -LESSON38_GRID_HALF_SIZE,
         LESSON38_GRID_HALF_SIZE, 0.0f, -LESSON38_GRID_HALF_SIZE,
         LESSON38_GRID_HALF_SIZE, 0.0f,  LESSON38_GRID_HALF_SIZE,
        -LESSON38_GRID_HALF_SIZE, 0.0f,  LESSON38_GRID_HALF_SIZE,
    };
    const Uint16 grid_indices[] = { 0, 1, 2, 0, 2, 3 };
    SDL_GPUBufferCreateInfo buffer_info;
    SDL_GPUTransferBufferCreateInfo transfer_info;

    lesson38_fill_object_data(state);
    state->object_data_buf = ForgeGpuCreateBufferWithData(
        demo->device,
        SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ | SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ,
        state->objects,
        sizeof(state->objects));
    if (!state->object_data_buf) {
        return false;
    }

    for (Uint32 i = 0; i < LESSON38_NUM_BOXES; i += 1) {
        instance_ids[i] = i;
    }
    state->instance_id_buf = ForgeGpuCreateBufferWithData(
        demo->device,
        SDL_GPU_BUFFERUSAGE_VERTEX,
        instance_ids,
        sizeof(instance_ids));
    if (!state->instance_id_buf) {
        return false;
    }

    state->grid_vertex_buf = ForgeGpuCreateBufferWithData(
        demo->device,
        SDL_GPU_BUFFERUSAGE_VERTEX,
        grid_vertices,
        sizeof(grid_vertices));
    state->grid_index_buf = ForgeGpuCreateBufferWithData(
        demo->device,
        SDL_GPU_BUFFERUSAGE_INDEX,
        grid_indices,
        sizeof(grid_indices));
    if (!state->grid_vertex_buf || !state->grid_index_buf) {
        return false;
    }

    SDL_zero(buffer_info);
    buffer_info.usage = SDL_GPU_BUFFERUSAGE_INDIRECT | SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE;
    buffer_info.size = LESSON38_NUM_BOXES * (Uint32)sizeof(SDL_GPUIndexedIndirectDrawCommand);
    state->indirect_buf = SDL_CreateGPUBuffer(demo->device, &buffer_info);
    if (!state->indirect_buf) {
        return false;
    }

    SDL_zero(buffer_info);
    buffer_info.usage = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE | SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
    buffer_info.size = LESSON38_NUM_BOXES * (Uint32)sizeof(Uint32);
    state->visibility_buf = SDL_CreateGPUBuffer(demo->device, &buffer_info);
    if (!state->visibility_buf) {
        return false;
    }

    SDL_zero(buffer_info);
    buffer_info.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
    buffer_info.size = LESSON38_FRUSTUM_LINE_VERTS * (Uint32)sizeof(Lesson38LineVertex);
    state->frustum_line_buf = SDL_CreateGPUBuffer(demo->device, &buffer_info);
    if (!state->frustum_line_buf) {
        return false;
    }

    SDL_zero(transfer_info);
    transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transfer_info.size = LESSON38_FRUSTUM_LINE_VERTS * (Uint32)sizeof(Lesson38LineVertex);
    state->frustum_line_upload = SDL_CreateGPUTransferBuffer(demo->device, &transfer_info);
    if (!state->frustum_line_upload) {
        return false;
    }

    SDL_zero(transfer_info);
    transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    transfer_info.size = LESSON38_NUM_BOXES * (Uint32)sizeof(Uint32);
    state->visibility_readback = SDL_CreateGPUTransferBuffer(demo->device, &transfer_info);
    if (!state->visibility_readback) {
        return false;
    }

    return true;
}

static bool lesson38_ensure_targets(ForgeGpuDemo *demo, Lesson38State *state, Uint32 width, Uint32 height)
{
    if (!ForgeGpuEnsureSampledDepthTarget(
            demo,
            &state->main_depth,
            &state->main_depth_width,
            &state->main_depth_height,
            width,
            height,
            LESSON38_DEPTH_FORMAT)) {
        return false;
    }
    if (!state->shadow_depth) {
        state->shadow_depth = ForgeGpuCreateSampledDepthTexture(
            demo,
            LESSON38_SHADOW_MAP_SIZE,
            LESSON38_SHADOW_MAP_SIZE,
            LESSON38_DEPTH_FORMAT);
        if (!state->shadow_depth) {
            return false;
        }
    }
    return true;
}

static void lesson38_fill_grid_fragment_uniforms(
    Lesson38GridFragUniforms *uniforms,
    Vec3 light_dir,
    Vec3 eye_pos,
    Mat4 light_vp)
{
    SDL_zero(*uniforms);
    uniforms->line_color[0] = LESSON38_GRID_LINE_R;
    uniforms->line_color[1] = LESSON38_GRID_LINE_G;
    uniforms->line_color[2] = LESSON38_GRID_LINE_B;
    uniforms->line_color[3] = 1.0f;
    uniforms->bg_color[0] = LESSON38_GRID_BG_R;
    uniforms->bg_color[1] = LESSON38_GRID_BG_G;
    uniforms->bg_color[2] = LESSON38_GRID_BG_B;
    uniforms->bg_color[3] = 1.0f;
    uniforms->light_dir[0] = light_dir.x;
    uniforms->light_dir[1] = light_dir.y;
    uniforms->light_dir[2] = light_dir.z;
    uniforms->eye_pos[0] = eye_pos.x;
    uniforms->eye_pos[1] = eye_pos.y;
    uniforms->eye_pos[2] = eye_pos.z;
    uniforms->light_vp = light_vp;
    uniforms->grid_spacing = LESSON38_GRID_SPACING;
    uniforms->line_width = LESSON38_GRID_LINE_WIDTH;
    uniforms->fade_distance = LESSON38_GRID_FADE_DIST;
    uniforms->ambient = LESSON38_GRID_AMBIENT;
    uniforms->shininess = LESSON38_GRID_SHININESS;
    uniforms->specular_str = LESSON38_GRID_SPECULAR_STRENGTH;
    uniforms->shadow_texel = 1.0f / (float)LESSON38_SHADOW_MAP_SIZE;
}

static void lesson38_draw_grid(
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    Lesson38State *state,
    Mat4 view_projection,
    Vec3 eye_pos,
    Vec3 light_dir,
    Mat4 light_vp)
{
    Lesson38GridVertUniforms vertex_uniforms;
    Lesson38GridFragUniforms fragment_uniforms;
    SDL_GPUTextureSamplerBinding shadow_binding;
    SDL_GPUBufferBinding vertex_binding;
    SDL_GPUBufferBinding index_binding;

    SDL_BindGPUGraphicsPipeline(render_pass, state->grid_pipeline);
    vertex_uniforms.vp = view_projection;
    SDL_PushGPUVertexUniformData(command_buffer, 0, &vertex_uniforms, sizeof(vertex_uniforms));
    lesson38_fill_grid_fragment_uniforms(&fragment_uniforms, light_dir, eye_pos, light_vp);
    SDL_PushGPUFragmentUniformData(command_buffer, 0, &fragment_uniforms, sizeof(fragment_uniforms));

    SDL_zero(shadow_binding);
    shadow_binding.texture = state->shadow_depth;
    shadow_binding.sampler = state->shadow_sampler;
    SDL_BindGPUFragmentSamplers(render_pass, 0, &shadow_binding, 1);

    SDL_zero(vertex_binding);
    vertex_binding.buffer = state->grid_vertex_buf;
    SDL_BindGPUVertexBuffers(render_pass, 0, &vertex_binding, 1);
    SDL_zero(index_binding);
    index_binding.buffer = state->grid_index_buf;
    SDL_BindGPUIndexBuffer(render_pass, &index_binding, SDL_GPU_INDEXELEMENTSIZE_16BIT);
    SDL_DrawGPUIndexedPrimitives(render_pass, 6, 1, 0, 0, 0);
}

static void lesson38_draw_truck(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    Lesson38State *state,
    Mat4 view_projection,
    Mat4 light_vp,
    Vec3 eye_pos,
    Vec3 light_dir,
    bool shadow_pass)
{
    const ForgeGpuLoadedScene *scene = &state->truck_model.loaded;
    Lesson38VertUniforms vertex_uniforms;

    SDL_BindGPUGraphicsPipeline(render_pass, shadow_pass ? state->truck_shadow_pipeline : state->truck_pipeline);
    vertex_uniforms.vp = shadow_pass ? light_vp : view_projection;
    vertex_uniforms.light_vp = light_vp;
    SDL_PushGPUVertexUniformData(command_buffer, 0, &vertex_uniforms, sizeof(vertex_uniforms));

    for (int ni = 0; ni < scene->node_count && ni < LESSON38_MAX_TRUCK_INSTANCES; ni += 1) {
        const ForgeGpuSceneNode *node = &scene->nodes[ni];
        const ForgeGpuSceneMesh *mesh;
        int truck_instance;

        if (node->mesh_index < 0 || node->mesh_index >= scene->mesh_count) {
            continue;
        }
        truck_instance = state->truck_node_to_inst[ni];
        if (truck_instance < 0) {
            continue;
        }
        mesh = &scene->meshes[node->mesh_index];

        for (int pi = 0; pi < mesh->primitive_count; pi += 1) {
            const int primitive_index = mesh->first_primitive + pi;
            const GpuPrimitive *primitive;
            SDL_GPUBufferBinding vertex_bindings[2];
            SDL_GPUBufferBinding index_binding;

            if (primitive_index < 0 || primitive_index >= state->truck_model.primitive_count) {
                continue;
            }
            primitive = &state->truck_model.primitives[primitive_index];
            if (!primitive->vertex_buffer || !primitive->index_buffer) {
                continue;
            }

            if (!shadow_pass) {
                GpuMaterial fallback;
                const GpuMaterial *material = ForgeGpuModelMaterialOrDefault(&state->truck_model, primitive->material_index, &fallback);
                Lesson38TruckFragUniforms fragment_uniforms;
                SDL_GPUTextureSamplerBinding fragment_bindings[2];

                SDL_zero(fragment_uniforms);
                SDL_memcpy(fragment_uniforms.base_color, material->base_color, sizeof(fragment_uniforms.base_color));
                fragment_uniforms.light_dir[0] = light_dir.x;
                fragment_uniforms.light_dir[1] = light_dir.y;
                fragment_uniforms.light_dir[2] = light_dir.z;
                fragment_uniforms.eye_pos[0] = eye_pos.x;
                fragment_uniforms.eye_pos[1] = eye_pos.y;
                fragment_uniforms.eye_pos[2] = eye_pos.z;
                fragment_uniforms.shadow_texel = 1.0f / (float)LESSON38_SHADOW_MAP_SIZE;
                fragment_uniforms.shininess = LESSON38_SCENE_SHININESS;
                fragment_uniforms.ambient = LESSON38_SCENE_AMBIENT;
                fragment_uniforms.specular_str = LESSON38_SCENE_SPECULAR_STRENGTH;
                fragment_uniforms.has_texture = material->has_texture ? 1u : 0u;
                SDL_PushGPUFragmentUniformData(command_buffer, 0, &fragment_uniforms, sizeof(fragment_uniforms));

                SDL_zeroa(fragment_bindings);
                fragment_bindings[0].texture = material->texture ? material->texture : demo->lesson.white_texture;
                fragment_bindings[0].sampler = demo->lesson.samplers[0];
                fragment_bindings[1].texture = state->shadow_depth;
                fragment_bindings[1].sampler = state->shadow_sampler;
                SDL_BindGPUFragmentSamplers(render_pass, 0, fragment_bindings, 2);
            }

            SDL_zeroa(vertex_bindings);
            vertex_bindings[0].buffer = primitive->vertex_buffer;
            vertex_bindings[1].buffer = state->truck_instance_buf;
            SDL_BindGPUVertexBuffers(render_pass, 0, vertex_bindings, 2);
            SDL_zero(index_binding);
            index_binding.buffer = primitive->index_buffer;
            SDL_BindGPUIndexBuffer(render_pass, &index_binding, primitive->index_type);
            SDL_DrawGPUIndexedPrimitives(render_pass, primitive->index_count, 1, 0, 0, (Uint32)truck_instance);
        }
    }
}

static void lesson38_bind_box_geometry(SDL_GPURenderPass *render_pass, const Lesson38State *state)
{
    const GpuPrimitive *primitive = &state->box_model.primitives[state->box_primitive_index];
    SDL_GPUBufferBinding vertex_bindings[2];
    SDL_GPUBufferBinding index_binding;

    SDL_zeroa(vertex_bindings);
    vertex_bindings[0].buffer = primitive->vertex_buffer;
    vertex_bindings[1].buffer = state->instance_id_buf;
    SDL_BindGPUVertexBuffers(render_pass, 0, vertex_bindings, 2);
    SDL_zero(index_binding);
    index_binding.buffer = primitive->index_buffer;
    SDL_BindGPUIndexBuffer(render_pass, &index_binding, primitive->index_type);
}

static void lesson38_draw_boxes_indirect(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    Lesson38State *state,
    Mat4 view_projection,
    Mat4 light_vp,
    Vec3 eye_pos,
    Vec3 light_dir,
    bool shadow_pass)
{
    SDL_GPUBuffer *object_buffer = state->object_data_buf;

    SDL_BindGPUGraphicsPipeline(render_pass, shadow_pass ? state->indirect_shadow_pipeline : state->indirect_box_pipeline);
    if (shadow_pass) {
        Lesson38ShadowUniforms shadow_uniforms;
        shadow_uniforms.light_vp = light_vp;
        SDL_PushGPUVertexUniformData(command_buffer, 0, &shadow_uniforms, sizeof(shadow_uniforms));
    } else {
        GpuMaterial fallback;
        const GpuPrimitive *primitive = &state->box_model.primitives[state->box_primitive_index];
        const GpuMaterial *material = ForgeGpuModelMaterialOrDefault(&state->box_model, primitive->material_index, &fallback);
        Lesson38VertUniforms vertex_uniforms;
        Lesson38BoxFragUniforms fragment_uniforms;
        SDL_GPUTextureSamplerBinding fragment_bindings[2];

        vertex_uniforms.vp = view_projection;
        vertex_uniforms.light_vp = light_vp;
        SDL_PushGPUVertexUniformData(command_buffer, 0, &vertex_uniforms, sizeof(vertex_uniforms));

        SDL_zero(fragment_uniforms);
        fragment_uniforms.light_dir[0] = light_dir.x;
        fragment_uniforms.light_dir[1] = light_dir.y;
        fragment_uniforms.light_dir[2] = light_dir.z;
        fragment_uniforms.eye_pos[0] = eye_pos.x;
        fragment_uniforms.eye_pos[1] = eye_pos.y;
        fragment_uniforms.eye_pos[2] = eye_pos.z;
        fragment_uniforms.shadow_texel = 1.0f / (float)LESSON38_SHADOW_MAP_SIZE;
        fragment_uniforms.shininess = LESSON38_SCENE_SHININESS;
        fragment_uniforms.ambient = LESSON38_SCENE_AMBIENT;
        fragment_uniforms.specular_str = LESSON38_SCENE_SPECULAR_STRENGTH;
        SDL_PushGPUFragmentUniformData(command_buffer, 0, &fragment_uniforms, sizeof(fragment_uniforms));

        SDL_zeroa(fragment_bindings);
        fragment_bindings[0].texture = material->texture ? material->texture : demo->lesson.white_texture;
        fragment_bindings[0].sampler = demo->lesson.samplers[0];
        fragment_bindings[1].texture = state->shadow_depth;
        fragment_bindings[1].sampler = state->shadow_sampler;
        SDL_BindGPUFragmentSamplers(render_pass, 0, fragment_bindings, 2);
    }

    SDL_BindGPUVertexStorageBuffers(render_pass, 0, &object_buffer, 1);
    lesson38_bind_box_geometry(render_pass, state);
    SDL_DrawGPUIndexedPrimitivesIndirect(render_pass, state->indirect_buf, 0, LESSON38_NUM_BOXES);
    state->indirect_draws_issued = true;
}

static void lesson38_draw_debug_boxes(
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    Lesson38State *state,
    Mat4 debug_vp)
{
    const GpuPrimitive *primitive = &state->box_model.primitives[state->box_primitive_index];
    SDL_GPUBuffer *object_buffer = state->object_data_buf;
    SDL_GPUBuffer *visibility_buffer = state->visibility_buf;
    Lesson38DebugVertUniforms vertex_uniforms;

    SDL_BindGPUGraphicsPipeline(render_pass, state->debug_box_pipeline);
    vertex_uniforms.vp = debug_vp;
    SDL_PushGPUVertexUniformData(command_buffer, 0, &vertex_uniforms, sizeof(vertex_uniforms));
    SDL_BindGPUVertexStorageBuffers(render_pass, 0, &object_buffer, 1);
    SDL_BindGPUFragmentStorageBuffers(render_pass, 0, &visibility_buffer, 1);
    lesson38_bind_box_geometry(render_pass, state);
    SDL_DrawGPUIndexedPrimitives(render_pass, primitive->index_count, LESSON38_NUM_BOXES, 0, 0, 0);
}

static void lesson38_draw_frustum_lines(
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    Lesson38State *state,
    Mat4 debug_vp)
{
    Lesson38LineVertUniforms uniforms;
    SDL_GPUBufferBinding vertex_binding;

    SDL_BindGPUGraphicsPipeline(render_pass, state->frustum_line_pipeline);
    uniforms.vp = debug_vp;
    SDL_PushGPUVertexUniformData(command_buffer, 0, &uniforms, sizeof(uniforms));
    SDL_zero(vertex_binding);
    vertex_binding.buffer = state->frustum_line_buf;
    SDL_BindGPUVertexBuffers(render_pass, 0, &vertex_binding, 1);
    SDL_DrawGPUPrimitives(render_pass, LESSON38_FRUSTUM_LINE_VERTS, 1, 0, 0);
}

static bool lesson38_run_compute_pass(
    SDL_GPUCommandBuffer *command_buffer,
    Lesson38State *state,
    Mat4 main_vp)
{
    Lesson38CullUniforms uniforms;
    SDL_GPUStorageBufferReadWriteBinding readwrite_bindings[2];
    SDL_GPUComputePass *compute_pass;
    SDL_GPUBuffer *object_buffer = state->object_data_buf;
    const Uint32 groups = (LESSON38_NUM_BOXES + LESSON38_WORKGROUP_SIZE - 1u) / LESSON38_WORKGROUP_SIZE;

    SDL_zero(uniforms);
    uniforms.num_objects = LESSON38_NUM_BOXES;
    uniforms.enable_culling = state->culling_enabled ? 1u : 0u;
    lesson38_extract_frustum_planes(main_vp, uniforms.frustum_planes);
    SDL_PushGPUComputeUniformData(command_buffer, 0, &uniforms, sizeof(uniforms));

    SDL_zeroa(readwrite_bindings);
    readwrite_bindings[0].buffer = state->indirect_buf;
    readwrite_bindings[0].cycle = true;
    readwrite_bindings[1].buffer = state->visibility_buf;
    readwrite_bindings[1].cycle = true;
    compute_pass = SDL_BeginGPUComputePass(command_buffer, nullptr, 0, readwrite_bindings, 2);
    if (!compute_pass) {
        return false;
    }

    SDL_BindGPUComputePipeline(compute_pass, state->cull_pipeline);
    SDL_BindGPUComputeStorageBuffers(compute_pass, 0, &object_buffer, 1);
    SDL_DispatchGPUCompute(compute_pass, groups, 1, 1);
    SDL_EndGPUComputePass(compute_pass);
    state->compute_pass_ran = true;
    return true;
}

static bool lesson38_run_shadow_pass(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    Lesson38State *state,
    Mat4 light_vp)
{
    SDL_GPUDepthStencilTargetInfo depth_target;
    SDL_GPURenderPass *render_pass;

    SDL_zero(depth_target);
    depth_target.texture = state->shadow_depth;
    depth_target.load_op = SDL_GPU_LOADOP_CLEAR;
    depth_target.store_op = SDL_GPU_STOREOP_STORE;
    depth_target.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
    depth_target.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
    depth_target.clear_depth = 1.0f;
    depth_target.cycle = true;
    render_pass = SDL_BeginGPURenderPass(command_buffer, nullptr, 0, &depth_target);
    if (!render_pass) {
        return false;
    }

    {
        SDL_GPUViewport viewport;
        SDL_zero(viewport);
        viewport.w = (float)LESSON38_SHADOW_MAP_SIZE;
        viewport.h = (float)LESSON38_SHADOW_MAP_SIZE;
        viewport.max_depth = 1.0f;
        SDL_SetGPUViewport(render_pass, &viewport);
    }

    lesson38_draw_truck(demo, command_buffer, render_pass, state, light_vp, light_vp, { 0.0f, 0.0f, 0.0f }, lesson38_light_dir(), true);
    lesson38_draw_boxes_indirect(demo, command_buffer, render_pass, state, light_vp, light_vp, { 0.0f, 0.0f, 0.0f }, lesson38_light_dir(), true);
    SDL_EndGPURenderPass(render_pass);
    state->shadow_pass_rendered = true;
    return true;
}

static bool lesson38_run_main_pass(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPUTexture *swapchain_texture,
    Lesson38State *state,
    Uint32 width,
    Uint32 height,
    Mat4 main_vp,
    Mat4 debug_vp,
    Mat4 light_vp,
    Vec3 light_dir,
    Vec3 debug_eye)
{
    SDL_GPUColorTargetInfo color_target;
    SDL_GPUDepthStencilTargetInfo depth_target;
    SDL_GPURenderPass *render_pass;
    const int w = (int)width;
    const int h = (int)height;
    const int half_w = w / 2;
    const int main_width = state->debug_view_enabled ? half_w : w;
    SDL_GPUViewport viewport;
    SDL_Rect scissor;

    SDL_zero(color_target);
    color_target.texture = swapchain_texture;
    color_target.load_op = SDL_GPU_LOADOP_CLEAR;
    color_target.store_op = SDL_GPU_STOREOP_STORE;
    color_target.clear_color = { LESSON38_CLEAR_R, LESSON38_CLEAR_G, LESSON38_CLEAR_B, 1.0f };

    SDL_zero(depth_target);
    depth_target.texture = state->main_depth;
    depth_target.load_op = SDL_GPU_LOADOP_CLEAR;
    depth_target.store_op = SDL_GPU_STOREOP_STORE;
    depth_target.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
    depth_target.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
    depth_target.clear_depth = 1.0f;
    depth_target.cycle = true;

    render_pass = SDL_BeginGPURenderPass(command_buffer, &color_target, 1, &depth_target);
    if (!render_pass) {
        return false;
    }

    SDL_zero(viewport);
    viewport.w = (float)main_width;
    viewport.h = (float)h;
    viewport.max_depth = 1.0f;
    SDL_SetGPUViewport(render_pass, &viewport);
    scissor.x = 0;
    scissor.y = 0;
    scissor.w = main_width;
    scissor.h = h;
    SDL_SetGPUScissor(render_pass, &scissor);

    lesson38_draw_grid(command_buffer, render_pass, state, main_vp, demo->lesson.camera_position, light_dir, light_vp);
    lesson38_draw_truck(demo, command_buffer, render_pass, state, main_vp, light_vp, demo->lesson.camera_position, light_dir, false);
    lesson38_draw_boxes_indirect(demo, command_buffer, render_pass, state, main_vp, light_vp, demo->lesson.camera_position, light_dir, false);
    state->main_pass_rendered = true;

    if (state->debug_view_enabled) {
        SDL_zero(viewport);
        viewport.x = (float)half_w;
        viewport.w = (float)(w - half_w);
        viewport.h = (float)h;
        viewport.max_depth = 1.0f;
        SDL_SetGPUViewport(render_pass, &viewport);
        scissor.x = half_w;
        scissor.y = 0;
        scissor.w = w - half_w;
        scissor.h = h;
        SDL_SetGPUScissor(render_pass, &scissor);

        lesson38_draw_grid(command_buffer, render_pass, state, debug_vp, debug_eye, light_dir, light_vp);
        lesson38_draw_truck(demo, command_buffer, render_pass, state, debug_vp, light_vp, debug_eye, light_dir, false);
        lesson38_draw_debug_boxes(command_buffer, render_pass, state, debug_vp);
        lesson38_draw_frustum_lines(command_buffer, render_pass, state, debug_vp);
        state->debug_pass_rendered = true;
    }

    SDL_EndGPURenderPass(render_pass);
    return true;
}

bool ForgeGpuCreateLesson38(ForgeGpuDemo *demo)
{
    Lesson38State *state;

    state = (Lesson38State *)SDL_calloc(1, sizeof(*state));
    if (!state) {
        SDL_OutOfMemory();
        return false;
    }
    demo->lesson.private_state = state;
    state->culling_enabled = true;
    state->debug_view_enabled = true;
    state->box_primitive_index = -1;

    lesson38_init_camera(demo);
    if (!lesson38_create_shadow_sampler(demo, state) ||
        !lesson38_create_pipelines(demo, state) ||
        !ForgeGpuLoadSceneModel(demo, &state->box_model, "models/BoxTextured/BoxTextured.gltf") ||
        !ForgeGpuLoadSceneModel(demo, &state->truck_model, "models/CesiumMilkTruck/CesiumMilkTruck.gltf") ||
        !lesson38_find_box_primitive(state) ||
        !lesson38_create_truck_instances(demo, state) ||
        !lesson38_create_buffers(demo, state)) {
        return false;
    }

    return true;
}

bool ForgeGpuRenderLesson38(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPUTexture *swapchain_texture,
    Uint32 width,
    Uint32 height)
{
    Lesson38State *state = lesson38_state(demo);
    Mat4 main_view;
    Mat4 main_projection;
    Mat4 main_vp;
    Mat4 debug_view;
    Mat4 debug_projection;
    Mat4 debug_vp;
    Mat4 light_vp;
    Vec3 light_dir;
    Vec3 debug_eye = { 0.0f, LESSON38_DEBUG_CAM_HEIGHT, -LESSON38_DEBUG_CAM_BACK };
    Vec3 frustum_corners[LESSON38_FRUSTUM_CORNERS];
    Lesson38LineVertex frustum_vertices[LESSON38_FRUSTUM_LINE_VERTS];
    Uint32 main_width;

    if (!state) {
        SDL_SetError("lesson 38 state is missing");
        return false;
    }

    lesson38_decode_pending_readback(demo, state);
    if (!lesson38_ensure_targets(demo, state, width, height)) {
        return false;
    }

    ForgeGpuUpdateCameraFromInput(demo);
    main_width = state->debug_view_enabled ? width / 2u : width;
    main_view = mat4_view_from_quat(
        demo->lesson.camera_position,
        quat_from_euler(demo->lesson.camera_yaw, demo->lesson.camera_pitch, 0.0f));
    main_projection = mat4_perspective(
        LESSON38_FOV_DEGREES * FORGE_GPU_DEG2RAD,
        height > 0u ? (float)main_width / (float)height : 1.0f,
        LESSON38_NEAR_PLANE,
        LESSON38_FAR_PLANE);
    main_vp = mat4_multiply(main_projection, main_view);

    debug_view = mat4_look_at(debug_eye, { 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f });
    debug_projection = mat4_perspective(
        LESSON38_DEBUG_FOV_DEGREES * FORGE_GPU_DEG2RAD,
        height > 0u ? ((float)width * 0.5f) / (float)height : 1.0f,
        LESSON38_NEAR_PLANE,
        LESSON38_FAR_PLANE);
    debug_vp = mat4_multiply(debug_projection, debug_view);
    light_dir = lesson38_light_dir();
    light_vp = lesson38_light_view_projection();

    lesson38_compute_frustum_corners(main_vp, frustum_corners);
    lesson38_build_frustum_line_vertices(frustum_corners, frustum_vertices);
    if (!lesson38_upload_frustum_lines(demo, state, command_buffer, frustum_vertices)) {
        return false;
    }

    state->compute_pass_ran = false;
    state->shadow_pass_rendered = false;
    state->main_pass_rendered = false;
    state->debug_pass_rendered = false;
    state->indirect_draws_issued = false;

    if (!lesson38_run_compute_pass(command_buffer, state, main_vp) ||
        !lesson38_run_shadow_pass(demo, command_buffer, state, light_vp) ||
        !lesson38_run_main_pass(
            demo,
            command_buffer,
            swapchain_texture,
            state,
            width,
            height,
            main_vp,
            debug_vp,
            light_vp,
            light_dir,
            debug_eye)) {
        return false;
    }

    if (demo->validation_mode && !state->validation_readback_completed) {
        lesson38_schedule_visibility_readback(state, command_buffer);
    }
    return true;
}

void ForgeGpuDebugLesson38(ForgeGpuDemo *demo)
{
    Lesson38State *state = lesson38_state(demo);

    if (!state) {
        return;
    }

    ImGui::Text("Culling: %s", state->culling_enabled ? "on" : "off");
    ImGui::Text("Debug view: %s", state->debug_view_enabled ? "on" : "off");
    ImGui::Text("Visible boxes: %u / %u%s",
        state->visible_count,
        LESSON38_NUM_BOXES,
        state->validation_readback_completed ? "" : " (readback pending)");
    ImGui::Text("Compute pass: %s", state->compute_pass_ran ? "yes" : "no");
    ImGui::Text("Shadow pass: %s", state->shadow_pass_rendered ? "yes" : "no");
    ImGui::Text("Main pass: %s", state->main_pass_rendered ? "yes" : "no");
    ImGui::Text("Indirect draws: %s", state->indirect_draws_issued ? "yes" : "no");
}

void ForgeGpuControlsLesson38(ForgeGpuDemo *demo)
{
    Lesson38State *state = lesson38_state(demo);
    bool culling_enabled;

    if (!state) {
        return;
    }

    culling_enabled = state->culling_enabled;
    if (ImGui::Checkbox("Frustum culling (F)", &culling_enabled)) {
        state->culling_enabled = culling_enabled;
        state->validation_readback_completed = false;
        state->validation_readback_scheduled = false;
    }
    ImGui::Checkbox("Debug split view (V)", &state->debug_view_enabled);
}

bool ForgeGpuHandleLesson38Event(ForgeGpuDemo *demo, const SDL_Event *event)
{
    Lesson38State *state = lesson38_state(demo);

    if (!state || event->type != SDL_EVENT_KEY_DOWN || event->key.repeat) {
        return false;
    }
    if (event->key.key == SDLK_F) {
        state->culling_enabled = !state->culling_enabled;
        state->validation_readback_completed = false;
        state->validation_readback_scheduled = false;
        return true;
    }
    if (event->key.key == SDLK_V) {
        state->debug_view_enabled = !state->debug_view_enabled;
        return true;
    }
    return false;
}

void ForgeGpuExportLesson38Metrics(ForgeGpuDemo *demo)
{
    Lesson38State *state = lesson38_state(demo);

    if (!state) {
        return;
    }

    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson38ComputePass", state->compute_pass_ran ? 1.0 : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson38ShadowPass", state->shadow_pass_rendered ? 1.0 : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson38MainPass", state->main_pass_rendered ? 1.0 : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson38DebugPass", state->debug_pass_rendered ? 1.0 : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson38IndirectDraws", state->indirect_draws_issued ? 1.0 : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson38CullingEnabled", state->culling_enabled ? 1.0 : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson38DebugView", state->debug_view_enabled ? 1.0 : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson38ReadbackReady", state->validation_readback_completed ? 1.0 : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson38VisibleCount", (double)state->visible_count);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson38ObjectCount", (double)LESSON38_NUM_BOXES);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson38ComparisonSamplerPath", 1.0);
}

void ForgeGpuDestroyLesson38(ForgeGpuDemo *demo)
{
    Lesson38State *state = lesson38_state(demo);

    if (!state) {
        return;
    }

    if (state->visibility_readback) {
        SDL_ReleaseGPUTransferBuffer(demo->device, state->visibility_readback);
    }
    if (state->frustum_line_upload) {
        SDL_ReleaseGPUTransferBuffer(demo->device, state->frustum_line_upload);
    }
    if (state->grid_index_buf) {
        SDL_ReleaseGPUBuffer(demo->device, state->grid_index_buf);
    }
    if (state->grid_vertex_buf) {
        SDL_ReleaseGPUBuffer(demo->device, state->grid_vertex_buf);
    }
    if (state->truck_instance_buf) {
        SDL_ReleaseGPUBuffer(demo->device, state->truck_instance_buf);
    }
    if (state->frustum_line_buf) {
        SDL_ReleaseGPUBuffer(demo->device, state->frustum_line_buf);
    }
    if (state->instance_id_buf) {
        SDL_ReleaseGPUBuffer(demo->device, state->instance_id_buf);
    }
    if (state->visibility_buf) {
        SDL_ReleaseGPUBuffer(demo->device, state->visibility_buf);
    }
    if (state->indirect_buf) {
        SDL_ReleaseGPUBuffer(demo->device, state->indirect_buf);
    }
    if (state->object_data_buf) {
        SDL_ReleaseGPUBuffer(demo->device, state->object_data_buf);
    }
    ForgeGpuFreeSceneData(demo, &state->truck_model);
    ForgeGpuFreeSceneData(demo, &state->box_model);
    if (state->shadow_sampler) {
        SDL_ReleaseGPUSampler(demo->device, state->shadow_sampler);
    }
    if (state->shadow_depth) {
        SDL_ReleaseGPUTexture(demo->device, state->shadow_depth);
    }
    if (state->main_depth) {
        SDL_ReleaseGPUTexture(demo->device, state->main_depth);
    }
    if (state->truck_shadow_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, state->truck_shadow_pipeline);
    }
    if (state->truck_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, state->truck_pipeline);
    }
    if (state->grid_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, state->grid_pipeline);
    }
    if (state->frustum_line_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, state->frustum_line_pipeline);
    }
    if (state->debug_box_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, state->debug_box_pipeline);
    }
    if (state->indirect_shadow_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, state->indirect_shadow_pipeline);
    }
    if (state->indirect_box_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, state->indirect_box_pipeline);
    }
    if (state->cull_pipeline) {
        SDL_ReleaseGPUComputePipeline(demo->device, state->cull_pipeline);
    }
    SDL_free(state);
    demo->lesson.private_state = nullptr;
}
