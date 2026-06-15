#include "forge_gpu_lessons.h"

#include "forge_gpu_camera.h"
#include "forge_gpu_gpu_helpers.h"
#include "forge_gpu_lesson_common.h"
#include "forge_gpu_math.h"
#include "shaders/generated/forge_gpu_lesson_16_shaders.h"

#include <stddef.h>

#define LESSON16_MAX_BLEND_DRAWS 128
#define LESSON16_SCENE_Y_OFFSET 0.9f

struct Lesson16FragUniforms
{
    float base_color[4];
    float light_dir[4];
    float eye_pos[4];
    float alpha_cutoff;
    float has_texture;
    float shininess;
    float ambient;
    float specular_str;
    float pad0;
    float pad1;
    float pad2;
};

struct Lesson16BlendDraw
{
    int node_index;
    int primitive_index;
    float distance_to_camera;
};

struct Lesson16State
{
    SDL_GPUGraphicsPipeline *grid_pipeline;
    SDL_GPUGraphicsPipeline *opaque_pipeline;
    SDL_GPUGraphicsPipeline *alpha_test_pipeline;
    SDL_GPUGraphicsPipeline *blend_pipeline;
};

static Lesson16State *lesson16_state(ForgeGpuDemo *demo)
{
    return (Lesson16State *)demo->lesson.private_state;
}

static float lesson16_clampf(float value, float min_value, float max_value)
{
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static Mat4 lesson16_scene_model(const ForgeGpuSceneNode *node)
{
    const Mat4 lift = mat4_translate({ 0.0f, LESSON16_SCENE_Y_OFFSET, 0.0f });
    return mat4_multiply(lift, mat4_from_forge(node->world_transform));
}

static Vec3 lesson16_light_dir(void)
{
    return vec3_normalize({ 0.3f, 0.8f, 0.5f });
}

static void lesson16_transform_aabb(Mat4 matrix, Vec3 local_min, Vec3 local_max, Vec3 *world_min, Vec3 *world_max)
{
    float lo[3] = { local_min.x, local_min.y, local_min.z };
    float hi[3] = { local_max.x, local_max.y, local_max.z };
    float out_min[3];
    float out_max[3];

    for (int row = 0; row < 3; row += 1) {
        out_min[row] = matrix.m[12 + row];
        out_max[row] = matrix.m[12 + row];
        for (int col = 0; col < 3; col += 1) {
            const float e = matrix.m[col * 4 + row] * lo[col];
            const float f = matrix.m[col * 4 + row] * hi[col];

            if (e < f) {
                out_min[row] += e;
                out_max[row] += f;
            } else {
                out_min[row] += f;
                out_max[row] += e;
            }
        }
    }

    *world_min = { out_min[0], out_min[1], out_min[2] };
    *world_max = { out_max[0], out_max[1], out_max[2] };
}

static float lesson16_nearest_aabb_distance(Vec3 point, Vec3 world_min, Vec3 world_max)
{
    const Vec3 nearest = {
        lesson16_clampf(point.x, world_min.x, world_max.x),
        lesson16_clampf(point.y, world_min.y, world_max.y),
        lesson16_clampf(point.z, world_min.z, world_max.z)
    };
    const Vec3 delta = vec3_sub(nearest, point);
    return SDL_sqrtf(vec3_dot(delta, delta));
}

static int SDLCALL lesson16_compare_blend_draws(const void *a, const void *b)
{
    const Lesson16BlendDraw *left = (const Lesson16BlendDraw *)a;
    const Lesson16BlendDraw *right = (const Lesson16BlendDraw *)b;

    if (left->distance_to_camera > right->distance_to_camera) {
        return -1;
    }
    if (left->distance_to_camera < right->distance_to_camera) {
        return 1;
    }
    return 0;
}

static SDL_GPUGraphicsPipeline *lesson16_create_scene_pipeline(
    ForgeGpuDemo *demo,
    SDL_GPUShader *vertex_shader,
    SDL_GPUShader *fragment_shader,
    const SDL_GPUVertexBufferDescription *vertex_buffer_desc,
    const SDL_GPUVertexAttribute *vertex_attributes,
    bool enable_blend,
    bool depth_write)
{
    SDL_GPUColorTargetDescription color_target_description;
    SDL_GPUGraphicsPipelineCreateInfo pipeline_info;

    SDL_zero(color_target_description);
    color_target_description.format = demo->color_format;
    if (enable_blend) {
        color_target_description.blend_state.enable_blend = true;
        color_target_description.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
        color_target_description.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        color_target_description.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
        color_target_description.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        color_target_description.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        color_target_description.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    }

    SDL_zero(pipeline_info);
    pipeline_info.vertex_shader = vertex_shader;
    pipeline_info.fragment_shader = fragment_shader;
    pipeline_info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pipeline_info.vertex_input_state.vertex_buffer_descriptions = vertex_buffer_desc;
    pipeline_info.vertex_input_state.num_vertex_buffers = 1;
    pipeline_info.vertex_input_state.vertex_attributes = vertex_attributes;
    pipeline_info.vertex_input_state.num_vertex_attributes = 3;
    pipeline_info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    pipeline_info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    pipeline_info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    pipeline_info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    pipeline_info.depth_stencil_state.enable_depth_test = true;
    pipeline_info.depth_stencil_state.enable_depth_write = depth_write;
    pipeline_info.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
    pipeline_info.target_info.color_target_descriptions = &color_target_description;
    pipeline_info.target_info.num_color_targets = 1;
    pipeline_info.target_info.has_depth_stencil_target = true;
    pipeline_info.target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;

    return SDL_CreateGPUGraphicsPipeline(demo->device, &pipeline_info);
}

static SDL_GPUGraphicsPipeline *lesson16_create_grid_pipeline(
    ForgeGpuDemo *demo,
    SDL_GPUShader *vertex_shader,
    SDL_GPUShader *fragment_shader)
{
    SDL_GPUVertexBufferDescription vertex_buffer_desc;
    SDL_GPUVertexAttribute vertex_attribute;

    SDL_zero(vertex_buffer_desc);
    vertex_buffer_desc.slot = 0;
    vertex_buffer_desc.pitch = sizeof(GridVertex);
    vertex_buffer_desc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    SDL_zero(vertex_attribute);
    vertex_attribute.location = 0;
    vertex_attribute.buffer_slot = 0;
    vertex_attribute.format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    vertex_attribute.offset = offsetof(GridVertex, position);

    return ForgeGpuCreateLessonGraphicsPipeline(
        demo,
        vertex_shader,
        fragment_shader,
        &vertex_buffer_desc,
        1,
        &vertex_attribute,
        1,
        1,
        true,
        SDL_GPU_TEXTUREFORMAT_D32_FLOAT,
        true,
        true,
        SDL_GPU_CULLMODE_NONE,
        0.0f,
        0.0f);
}

static bool lesson16_create_pipelines(ForgeGpuDemo *demo, Lesson16State *state)
{
    SDL_GPUShader *scene_vertex_shader = nullptr;
    SDL_GPUShader *scene_fragment_shader = nullptr;
    SDL_GPUShader *alpha_test_fragment_shader = nullptr;
    SDL_GPUShader *grid_vertex_shader = nullptr;
    SDL_GPUShader *grid_fragment_shader = nullptr;
    SDL_GPUVertexBufferDescription scene_vertex_buffer_desc;
    SDL_GPUVertexAttribute scene_vertex_attributes[3];
    bool ok = false;

    scene_vertex_shader = ForgeGpuCreateShader(
        demo->device, SDL_GPU_SHADERSTAGE_VERTEX,
        lesson16_scene_vert_wgsl, lesson16_scene_vert_wgsl_size,
        lesson16_scene_vert_msl, lesson16_scene_vert_msl_size,
        0, 0, 0, 1);
    scene_fragment_shader = ForgeGpuCreateShader(
        demo->device, SDL_GPU_SHADERSTAGE_FRAGMENT,
        lesson16_scene_frag_wgsl, lesson16_scene_frag_wgsl_size,
        lesson16_scene_frag_msl, lesson16_scene_frag_msl_size,
        1, 0, 0, 1);
    alpha_test_fragment_shader = ForgeGpuCreateShader(
        demo->device, SDL_GPU_SHADERSTAGE_FRAGMENT,
        lesson16_alpha_test_frag_wgsl, lesson16_alpha_test_frag_wgsl_size,
        lesson16_alpha_test_frag_msl, lesson16_alpha_test_frag_msl_size,
        1, 0, 0, 1);
    grid_vertex_shader = ForgeGpuCreateShader(
        demo->device, SDL_GPU_SHADERSTAGE_VERTEX,
        lesson16_grid_vert_wgsl, lesson16_grid_vert_wgsl_size,
        lesson16_grid_vert_msl, lesson16_grid_vert_msl_size,
        0, 0, 0, 1);
    grid_fragment_shader = ForgeGpuCreateShader(
        demo->device, SDL_GPU_SHADERSTAGE_FRAGMENT,
        lesson16_grid_frag_wgsl, lesson16_grid_frag_wgsl_size,
        lesson16_grid_frag_msl, lesson16_grid_frag_msl_size,
        0, 0, 0, 1);
    if (!scene_vertex_shader || !scene_fragment_shader || !alpha_test_fragment_shader ||
        !grid_vertex_shader || !grid_fragment_shader) {
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

    state->opaque_pipeline = lesson16_create_scene_pipeline(
        demo, scene_vertex_shader, scene_fragment_shader,
        &scene_vertex_buffer_desc, scene_vertex_attributes,
        false, true);
    state->alpha_test_pipeline = lesson16_create_scene_pipeline(
        demo, scene_vertex_shader, alpha_test_fragment_shader,
        &scene_vertex_buffer_desc, scene_vertex_attributes,
        false, true);
    state->blend_pipeline = lesson16_create_scene_pipeline(
        demo, scene_vertex_shader, scene_fragment_shader,
        &scene_vertex_buffer_desc, scene_vertex_attributes,
        true, false);
    state->grid_pipeline = lesson16_create_grid_pipeline(demo, grid_vertex_shader, grid_fragment_shader);

    ok = state->opaque_pipeline &&
         state->alpha_test_pipeline &&
         state->blend_pipeline &&
         state->grid_pipeline;

done:
    if (grid_fragment_shader) {
        SDL_ReleaseGPUShader(demo->device, grid_fragment_shader);
    }
    if (grid_vertex_shader) {
        SDL_ReleaseGPUShader(demo->device, grid_vertex_shader);
    }
    if (alpha_test_fragment_shader) {
        SDL_ReleaseGPUShader(demo->device, alpha_test_fragment_shader);
    }
    if (scene_fragment_shader) {
        SDL_ReleaseGPUShader(demo->device, scene_fragment_shader);
    }
    if (scene_vertex_shader) {
        SDL_ReleaseGPUShader(demo->device, scene_vertex_shader);
    }
    return ok;
}

static void lesson16_fill_grid_fragment_uniforms(GridFragUniforms *uniforms, const Vec3 *light_dir, const Vec3 *eye_pos)
{
    uniforms->line_color[0] = 0.068f;
    uniforms->line_color[1] = 0.534f;
    uniforms->line_color[2] = 0.932f;
    uniforms->line_color[3] = 1.0f;
    uniforms->bg_color[0] = 0.014f;
    uniforms->bg_color[1] = 0.014f;
    uniforms->bg_color[2] = 0.045f;
    uniforms->bg_color[3] = 1.0f;
    uniforms->light_dir[0] = light_dir->x;
    uniforms->light_dir[1] = light_dir->y;
    uniforms->light_dir[2] = light_dir->z;
    uniforms->light_dir[3] = 0.0f;
    uniforms->eye_pos[0] = eye_pos->x;
    uniforms->eye_pos[1] = eye_pos->y;
    uniforms->eye_pos[2] = eye_pos->z;
    uniforms->eye_pos[3] = 0.0f;
    uniforms->grid_spacing = 1.0f;
    uniforms->line_width = 0.02f;
    uniforms->fade_distance = 40.0f;
    uniforms->ambient = 0.15f;
    uniforms->shininess = 32.0f;
    uniforms->specular_str = 0.4f;
    uniforms->pad0 = 0.0f;
    uniforms->pad1 = 0.0f;
}

static void lesson16_draw_grid(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    SDL_GPUGraphicsPipeline *pipeline,
    Mat4 vp,
    const Vec3 *light_dir)
{
    SDL_GPUBufferBinding vertex_binding;
    SDL_GPUBufferBinding index_binding;
    GridFragUniforms fragment_uniforms;
    UniformMvp vertex_uniforms;

    vertex_uniforms.mvp = vp;
    SDL_PushGPUVertexUniformData(command_buffer, 0, &vertex_uniforms, sizeof(vertex_uniforms));
    lesson16_fill_grid_fragment_uniforms(&fragment_uniforms, light_dir, &demo->lesson.camera_position);
    SDL_PushGPUFragmentUniformData(command_buffer, 0, &fragment_uniforms, sizeof(fragment_uniforms));

    SDL_zero(vertex_binding);
    vertex_binding.buffer = demo->lesson.vertex_buffer;
    SDL_zero(index_binding);
    index_binding.buffer = demo->lesson.index_buffer;

    SDL_BindGPUGraphicsPipeline(render_pass, pipeline);
    SDL_BindGPUVertexBuffers(render_pass, 0, &vertex_binding, 1);
    SDL_BindGPUIndexBuffer(render_pass, &index_binding, SDL_GPU_INDEXELEMENTSIZE_16BIT);
    SDL_DrawGPUIndexedPrimitives(render_pass, 6, 1, 0, 0, 0);
}

static ForgeGpuSceneAlphaMode lesson16_primitive_alpha_mode(const LessonState *lesson, int primitive_index)
{
    GpuMaterial fallback;
    const GpuPrimitive *primitive = &lesson->gpu_primitives[primitive_index];
    const GpuMaterial *material = ForgeGpuSceneMaterialOrDefault(lesson, primitive->material_index, &fallback);
    return material->alpha_mode;
}

static void lesson16_draw_primitive(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    Mat4 view_projection,
    int node_index,
    int primitive_index,
    const Vec3 *light_dir)
{
    LessonState *lesson = &demo->lesson;
    const ForgeGpuSceneNode *node = &lesson->scene.nodes[node_index];
    const GpuPrimitive *primitive = &lesson->gpu_primitives[primitive_index];
    GpuMaterial fallback_material;
    const GpuMaterial *material = ForgeGpuSceneMaterialOrDefault(lesson, primitive->material_index, &fallback_material);
    SDL_GPUBufferBinding vertex_binding;
    SDL_GPUTextureSamplerBinding sampler_binding;
    UniformMvpModel vertex_uniforms;
    Lesson16FragUniforms fragment_uniforms;
    Mat4 model = lesson16_scene_model(node);

    vertex_uniforms.model = model;
    vertex_uniforms.mvp = mat4_multiply(view_projection, model);
    SDL_PushGPUVertexUniformData(command_buffer, 0, &vertex_uniforms, sizeof(vertex_uniforms));

    SDL_memcpy(fragment_uniforms.base_color, material->base_color, sizeof(fragment_uniforms.base_color));
    fragment_uniforms.light_dir[0] = light_dir->x;
    fragment_uniforms.light_dir[1] = light_dir->y;
    fragment_uniforms.light_dir[2] = light_dir->z;
    fragment_uniforms.light_dir[3] = 0.0f;
    fragment_uniforms.eye_pos[0] = lesson->camera_position.x;
    fragment_uniforms.eye_pos[1] = lesson->camera_position.y;
    fragment_uniforms.eye_pos[2] = lesson->camera_position.z;
    fragment_uniforms.eye_pos[3] = 0.0f;
    fragment_uniforms.alpha_cutoff = material->alpha_cutoff;
    fragment_uniforms.has_texture = (material->has_texture && material->texture) ? 1.0f : 0.0f;
    fragment_uniforms.shininess = 32.0f;
    fragment_uniforms.ambient = 0.15f;
    fragment_uniforms.specular_str = 0.4f;
    fragment_uniforms.pad0 = 0.0f;
    fragment_uniforms.pad1 = 0.0f;
    fragment_uniforms.pad2 = 0.0f;
    SDL_PushGPUFragmentUniformData(command_buffer, 0, &fragment_uniforms, sizeof(fragment_uniforms));

    SDL_zero(vertex_binding);
    vertex_binding.buffer = primitive->vertex_buffer;
    SDL_BindGPUVertexBuffers(render_pass, 0, &vertex_binding, 1);

    SDL_zero(sampler_binding);
    sampler_binding.texture = fragment_uniforms.has_texture > 0.5f ? material->texture : lesson->white_texture;
    sampler_binding.sampler = lesson->samplers[0];
    SDL_BindGPUFragmentSamplers(render_pass, 0, &sampler_binding, 1);

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

static void lesson16_draw_alpha_mode(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    SDL_GPUGraphicsPipeline *pipeline,
    Mat4 view_projection,
    ForgeGpuSceneAlphaMode alpha_mode,
    const Vec3 *light_dir)
{
    LessonState *lesson = &demo->lesson;

    SDL_BindGPUGraphicsPipeline(render_pass, pipeline);
    for (int node_index = 0; node_index < lesson->scene.node_count; node_index += 1) {
        const ForgeGpuSceneNode *node = &lesson->scene.nodes[node_index];
        const ForgeGpuSceneMesh *mesh;

        if (node->mesh_index < 0 || node->mesh_index >= lesson->scene.mesh_count) {
            continue;
        }
        mesh = &lesson->scene.meshes[node->mesh_index];

        for (int primitive_offset = 0; primitive_offset < mesh->primitive_count; primitive_offset += 1) {
            const int primitive_index = mesh->first_primitive + primitive_offset;

            if (primitive_index < 0 || primitive_index >= lesson->gpu_primitive_count) {
                continue;
            }
            if (lesson16_primitive_alpha_mode(lesson, primitive_index) == alpha_mode) {
                lesson16_draw_primitive(demo, command_buffer, render_pass, view_projection, node_index, primitive_index, light_dir);
            }
        }
    }
}

static int lesson16_collect_blend_draws(ForgeGpuDemo *demo, Lesson16BlendDraw *draws, int max_draws)
{
    LessonState *lesson = &demo->lesson;
    int draw_count = 0;
    bool limit_hit = false;

    for (int node_index = 0; node_index < lesson->scene.node_count && !limit_hit; node_index += 1) {
        const ForgeGpuSceneNode *node = &lesson->scene.nodes[node_index];
        const ForgeGpuSceneMesh *mesh;
        Mat4 model;

        if (node->mesh_index < 0 || node->mesh_index >= lesson->scene.mesh_count) {
            continue;
        }
        mesh = &lesson->scene.meshes[node->mesh_index];
        model = lesson16_scene_model(node);

        for (int primitive_offset = 0; primitive_offset < mesh->primitive_count; primitive_offset += 1) {
            const int primitive_index = mesh->first_primitive + primitive_offset;
            const GpuPrimitive *primitive;
            Vec3 world_min;
            Vec3 world_max;

            if (primitive_index < 0 || primitive_index >= lesson->gpu_primitive_count ||
                lesson16_primitive_alpha_mode(lesson, primitive_index) != FORGE_GPU_SCENE_ALPHA_BLEND) {
                continue;
            }
            if (draw_count >= max_draws) {
                SDL_Log("forge-gpu lesson 16 transparent draw limit reached (%d)", max_draws);
                limit_hit = true;
                break;
            }

            primitive = &lesson->gpu_primitives[primitive_index];
            lesson16_transform_aabb(model, primitive->aabb_min, primitive->aabb_max, &world_min, &world_max);
            draws[draw_count].node_index = node_index;
            draws[draw_count].primitive_index = primitive_index;
            draws[draw_count].distance_to_camera = lesson16_nearest_aabb_distance(
                lesson->camera_position,
                world_min,
                world_max);
            draw_count += 1;
        }
    }

    if (draw_count > 1) {
        SDL_qsort(draws, (size_t)draw_count, sizeof(*draws), lesson16_compare_blend_draws);
    }
    return draw_count;
}

bool ForgeGpuCreateLesson16(ForgeGpuDemo *demo)
{
    Lesson16State *state;
    ForgeGpuSceneLoadRequirements requirements;

    state = (Lesson16State *)SDL_calloc(1, sizeof(*state));
    if (!state) {
        SDL_OutOfMemory();
        return false;
    }
    demo->lesson.private_state = state;

    if (!ForgeGpuCreateGridBuffers(demo)) {
        return false;
    }

    SDL_zero(requirements);
    requirements.required_features = FORGE_GPU_SCENE_FEATURE_ALPHA_MATERIALS;
    requirements.required_all_primitive_features = FORGE_GPU_SCENE_FEATURE_PRIMITIVE_BOUNDS;
    if (!ForgeGpuLoadLessonSceneWithRequirements(
            demo,
            "models/TransmissionOrderTest/TransmissionOrderTest.gltf",
            &requirements)) {
        return false;
    }

    demo->lesson.camera_position = { 0.0f, 2.1f, 5.5f };
    demo->lesson.camera_yaw = 0.0f;
    demo->lesson.camera_pitch = 0.0f;
    demo->lesson.move_speed = 5.0f;
    demo->lesson.last_ticks = SDL_GetTicks();

    return lesson16_create_pipelines(demo, state);
}

void ForgeGpuRenderLesson16(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    Uint32 width,
    Uint32 height)
{
    Lesson16State *state = lesson16_state(demo);
    Lesson16BlendDraw blend_draws[LESSON16_MAX_BLEND_DRAWS];
    Mat4 view;
    Mat4 projection;
    Mat4 view_projection;
    Vec3 light_dir = lesson16_light_dir();
    int blend_draw_count;

    if (!state) {
        return;
    }

    ForgeGpuUpdateCameraFromInput(demo);
    ForgeGpuCameraViewProjection(demo, width, height, 100.0f, &view, &projection);
    view_projection = mat4_multiply(projection, view);

    lesson16_draw_grid(demo, command_buffer, render_pass, state->grid_pipeline, view_projection, &light_dir);
    lesson16_draw_alpha_mode(
        demo, command_buffer, render_pass, state->opaque_pipeline,
        view_projection, FORGE_GPU_SCENE_ALPHA_OPAQUE, &light_dir);
    lesson16_draw_alpha_mode(
        demo, command_buffer, render_pass, state->alpha_test_pipeline,
        view_projection, FORGE_GPU_SCENE_ALPHA_MASK, &light_dir);

    blend_draw_count = lesson16_collect_blend_draws(demo, blend_draws, SDL_arraysize(blend_draws));
    SDL_BindGPUGraphicsPipeline(render_pass, state->blend_pipeline);
    for (int i = 0; i < blend_draw_count; i += 1) {
        lesson16_draw_primitive(
            demo,
            command_buffer,
            render_pass,
            view_projection,
            blend_draws[i].node_index,
            blend_draws[i].primitive_index,
            &light_dir);
    }
}

void ForgeGpuDestroyLesson16(ForgeGpuDemo *demo)
{
    Lesson16State *state = lesson16_state(demo);

    if (!state) {
        return;
    }
    if (state->blend_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, state->blend_pipeline);
    }
    if (state->alpha_test_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, state->alpha_test_pipeline);
    }
    if (state->opaque_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, state->opaque_pipeline);
    }
    if (state->grid_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, state->grid_pipeline);
    }
    SDL_free(state);
    demo->lesson.private_state = nullptr;
}
