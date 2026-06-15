#include "forge_gpu_lessons.h"

#include "forge_gpu_camera.h"
#include "forge_gpu_gpu_helpers.h"
#include "forge_gpu_lesson_common.h"
#include "forge_gpu_math.h"
#include "shaders/generated/forge_gpu_lesson_18_shaders.h"

#include <stddef.h>

#define LESSON18_OBJECT_COUNT 5
#define LESSON18_OBJECT_SPACING 3.5f
#define LESSON18_SCENE_Y_OFFSET 1.3f

struct Lesson18Material
{
    float ambient[4];
    float diffuse[4];
    float specular[4];
};

struct Lesson18Object
{
    const Lesson18Material *material;
    Vec3 position;
};

struct Lesson18FragUniforms
{
    float mat_ambient[4];
    float mat_diffuse[4];
    float mat_specular[4];
    float light_dir[4];
    float eye_pos[4];
    Uint32 has_texture;
    float pad[3];
};

static const Lesson18Material kLesson18Gold = {
    { 0.24725f, 0.1995f, 0.0745f, 0.0f },
    { 0.75164f, 0.60648f, 0.22648f, 0.0f },
    { 0.628281f, 0.555802f, 0.366065f, 51.2f }
};

static const Lesson18Material kLesson18RedPlastic = {
    { 0.0f, 0.0f, 0.0f, 0.0f },
    { 0.5f, 0.0f, 0.0f, 0.0f },
    { 0.7f, 0.6f, 0.6f, 32.0f }
};

static const Lesson18Material kLesson18Jade = {
    { 0.135f, 0.2225f, 0.1575f, 0.0f },
    { 0.54f, 0.89f, 0.63f, 0.0f },
    { 0.316228f, 0.316228f, 0.316228f, 12.8f }
};

static const Lesson18Material kLesson18Pearl = {
    { 0.25f, 0.20725f, 0.20725f, 0.0f },
    { 1.0f, 0.829f, 0.829f, 0.0f },
    { 0.296648f, 0.296648f, 0.296648f, 11.264f }
};

static const Lesson18Material kLesson18Chrome = {
    { 0.25f, 0.25f, 0.25f, 0.0f },
    { 0.4f, 0.4f, 0.4f, 0.0f },
    { 0.774597f, 0.774597f, 0.774597f, 76.8f }
};

static const Lesson18Object kLesson18Objects[LESSON18_OBJECT_COUNT] = {
    { &kLesson18Gold, { -2.0f * LESSON18_OBJECT_SPACING, LESSON18_SCENE_Y_OFFSET, 0.0f } },
    { &kLesson18RedPlastic, { -1.0f * LESSON18_OBJECT_SPACING, LESSON18_SCENE_Y_OFFSET, 0.0f } },
    { &kLesson18Jade, { 0.0f, LESSON18_SCENE_Y_OFFSET, 0.0f } },
    { &kLesson18Pearl, { 1.0f * LESSON18_OBJECT_SPACING, LESSON18_SCENE_Y_OFFSET, 0.0f } },
    { &kLesson18Chrome, { 2.0f * LESSON18_OBJECT_SPACING, LESSON18_SCENE_Y_OFFSET, 0.0f } }
};

static Vec3 lesson18_light_dir(void)
{
    return vec3_normalize({ 0.5f, 1.0f, 0.5f });
}

static void lesson18_fill_grid_fragment_uniforms(GridFragUniforms *uniforms, const Vec3 *light_dir, const Vec3 *eye_pos)
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
    uniforms->specular_str = 0.3f;
    uniforms->pad0 = 0.0f;
    uniforms->pad1 = 0.0f;
}

static bool lesson18_create_pipelines(ForgeGpuDemo *demo)
{
    SDL_GPUShader *scene_vertex_shader = nullptr;
    SDL_GPUShader *scene_fragment_shader = nullptr;
    SDL_GPUShader *grid_vertex_shader = nullptr;
    SDL_GPUShader *grid_fragment_shader = nullptr;
    SDL_GPUVertexBufferDescription scene_vertex_buffer_desc;
    SDL_GPUVertexAttribute scene_vertex_attributes[3];
    SDL_GPUVertexBufferDescription grid_vertex_buffer_desc;
    SDL_GPUVertexAttribute grid_vertex_attribute;
    bool ok = false;

    scene_vertex_shader = ForgeGpuCreateShader(
        demo->device, SDL_GPU_SHADERSTAGE_VERTEX,
        lesson18_material_vert_wgsl, lesson18_material_vert_wgsl_size,
        lesson18_material_vert_msl, lesson18_material_vert_msl_size,
        0, 0, 0, 1);
    scene_fragment_shader = ForgeGpuCreateShader(
        demo->device, SDL_GPU_SHADERSTAGE_FRAGMENT,
        lesson18_material_frag_wgsl, lesson18_material_frag_wgsl_size,
        lesson18_material_frag_msl, lesson18_material_frag_msl_size,
        1, 0, 0, 1);
    grid_vertex_shader = ForgeGpuCreateShader(
        demo->device, SDL_GPU_SHADERSTAGE_VERTEX,
        lesson18_grid_vert_wgsl, lesson18_grid_vert_wgsl_size,
        lesson18_grid_vert_msl, lesson18_grid_vert_msl_size,
        0, 0, 0, 1);
    grid_fragment_shader = ForgeGpuCreateShader(
        demo->device, SDL_GPU_SHADERSTAGE_FRAGMENT,
        lesson18_grid_frag_wgsl, lesson18_grid_frag_wgsl_size,
        lesson18_grid_frag_msl, lesson18_grid_frag_msl_size,
        0, 0, 0, 1);
    if (!scene_vertex_shader || !scene_fragment_shader || !grid_vertex_shader || !grid_fragment_shader) {
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

    SDL_zero(grid_vertex_buffer_desc);
    grid_vertex_buffer_desc.slot = 0;
    grid_vertex_buffer_desc.pitch = sizeof(GridVertex);
    grid_vertex_buffer_desc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    SDL_zero(grid_vertex_attribute);
    grid_vertex_attribute.location = 0;
    grid_vertex_attribute.buffer_slot = 0;
    grid_vertex_attribute.format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    grid_vertex_attribute.offset = offsetof(GridVertex, position);

    demo->lesson.pipeline = ForgeGpuCreateLessonGraphicsPipeline(
        demo,
        scene_vertex_shader,
        scene_fragment_shader,
        &scene_vertex_buffer_desc,
        1,
        scene_vertex_attributes,
        SDL_arraysize(scene_vertex_attributes),
        1,
        true,
        SDL_GPU_TEXTUREFORMAT_D32_FLOAT,
        true,
        true,
        SDL_GPU_CULLMODE_BACK,
        0.0f,
        0.0f);
    demo->lesson.secondary_pipeline = ForgeGpuCreateLessonGraphicsPipeline(
        demo,
        grid_vertex_shader,
        grid_fragment_shader,
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
    ok = demo->lesson.pipeline && demo->lesson.secondary_pipeline;

done:
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

static void lesson18_draw_grid(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    Mat4 vp,
    const Vec3 *light_dir)
{
    SDL_GPUBufferBinding vertex_binding;
    SDL_GPUBufferBinding index_binding;
    GridFragUniforms fragment_uniforms;
    UniformMvp vertex_uniforms;

    vertex_uniforms.mvp = vp;
    SDL_PushGPUVertexUniformData(command_buffer, 0, &vertex_uniforms, sizeof(vertex_uniforms));
    lesson18_fill_grid_fragment_uniforms(&fragment_uniforms, light_dir, &demo->lesson.camera_position);
    SDL_PushGPUFragmentUniformData(command_buffer, 0, &fragment_uniforms, sizeof(fragment_uniforms));

    SDL_zero(vertex_binding);
    vertex_binding.buffer = demo->lesson.vertex_buffer;
    SDL_zero(index_binding);
    index_binding.buffer = demo->lesson.index_buffer;

    SDL_BindGPUGraphicsPipeline(render_pass, demo->lesson.secondary_pipeline);
    SDL_BindGPUVertexBuffers(render_pass, 0, &vertex_binding, 1);
    SDL_BindGPUIndexBuffer(render_pass, &index_binding, SDL_GPU_INDEXELEMENTSIZE_16BIT);
    SDL_DrawGPUIndexedPrimitives(render_pass, 6, 1, 0, 0, 0);
}

static void lesson18_draw_scene_object(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    Mat4 view_projection,
    const Lesson18Object *object,
    const Vec3 *light_dir)
{
    LessonState *lesson = &demo->lesson;
    SDL_GPUTextureSamplerBinding sampler_binding;

    SDL_zero(sampler_binding);
    sampler_binding.texture = lesson->white_texture;
    sampler_binding.sampler = lesson->samplers[0];
    SDL_BindGPUFragmentSamplers(render_pass, 0, &sampler_binding, 1);

    for (int node_index = 0; node_index < lesson->scene.node_count; node_index += 1) {
        const ForgeGpuSceneNode *node = &lesson->scene.nodes[node_index];
        const ForgeGpuSceneMesh *mesh;
        Mat4 translation;
        Mat4 model;
        UniformMvpModel vertex_uniforms;
        Lesson18FragUniforms fragment_uniforms;

        if (node->mesh_index < 0 || node->mesh_index >= lesson->scene.mesh_count) {
            continue;
        }

        translation = mat4_translate(object->position);
        model = mat4_multiply(translation, mat4_from_forge(node->world_transform));
        vertex_uniforms.mvp = mat4_multiply(view_projection, model);
        vertex_uniforms.model = model;
        SDL_PushGPUVertexUniformData(command_buffer, 0, &vertex_uniforms, sizeof(vertex_uniforms));

        SDL_memcpy(fragment_uniforms.mat_ambient, object->material->ambient, sizeof(fragment_uniforms.mat_ambient));
        SDL_memcpy(fragment_uniforms.mat_diffuse, object->material->diffuse, sizeof(fragment_uniforms.mat_diffuse));
        SDL_memcpy(fragment_uniforms.mat_specular, object->material->specular, sizeof(fragment_uniforms.mat_specular));
        fragment_uniforms.light_dir[0] = light_dir->x;
        fragment_uniforms.light_dir[1] = light_dir->y;
        fragment_uniforms.light_dir[2] = light_dir->z;
        fragment_uniforms.light_dir[3] = 0.0f;
        fragment_uniforms.eye_pos[0] = lesson->camera_position.x;
        fragment_uniforms.eye_pos[1] = lesson->camera_position.y;
        fragment_uniforms.eye_pos[2] = lesson->camera_position.z;
        fragment_uniforms.eye_pos[3] = 0.0f;
        fragment_uniforms.has_texture = 0;
        fragment_uniforms.pad[0] = 0.0f;
        fragment_uniforms.pad[1] = 0.0f;
        fragment_uniforms.pad[2] = 0.0f;
        SDL_PushGPUFragmentUniformData(command_buffer, 0, &fragment_uniforms, sizeof(fragment_uniforms));

        mesh = &lesson->scene.meshes[node->mesh_index];
        for (int primitive_offset = 0; primitive_offset < mesh->primitive_count; primitive_offset += 1) {
            const int primitive_index = mesh->first_primitive + primitive_offset;
            const GpuPrimitive *primitive;
            SDL_GPUBufferBinding vertex_binding;

            if (primitive_index < 0 || primitive_index >= lesson->gpu_primitive_count) {
                continue;
            }
            primitive = &lesson->gpu_primitives[primitive_index];
            if (!primitive->vertex_buffer) {
                continue;
            }

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

bool ForgeGpuCreateLesson18(ForgeGpuDemo *demo)
{
    demo->lesson.camera_position = { 0.0f, 2.0f, 12.0f };
    demo->lesson.camera_yaw = 0.0f;
    demo->lesson.camera_pitch = 0.0f;
    demo->lesson.move_speed = 5.0f;
    demo->lesson.last_ticks = SDL_GetTicks();

    demo->lesson.white_texture = ForgeGpuCreateWhiteTexture(demo->device);
    demo->lesson.samplers[0] = ForgeGpuCreateSampler(
        demo->device,
        SDL_GPU_FILTER_LINEAR,
        SDL_GPU_FILTER_LINEAR,
        SDL_GPU_SAMPLERMIPMAPMODE_LINEAR,
        1000.0f);

    return demo->lesson.white_texture &&
           demo->lesson.samplers[0] &&
           ForgeGpuCreateGridBuffers(demo) &&
           ForgeGpuLoadLessonScene(demo, "models/Suzanne/Suzanne.gltf") &&
           lesson18_create_pipelines(demo);
}

void ForgeGpuRenderLesson18(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    Uint32 width,
    Uint32 height)
{
    Mat4 view;
    Mat4 projection;
    Mat4 view_projection;
    Vec3 light_dir = lesson18_light_dir();

    ForgeGpuUpdateCameraFromInput(demo);
    ForgeGpuCameraViewProjection(demo, width, height, 100.0f, &view, &projection);
    view_projection = mat4_multiply(projection, view);

    lesson18_draw_grid(demo, command_buffer, render_pass, view_projection, &light_dir);

    SDL_BindGPUGraphicsPipeline(render_pass, demo->lesson.pipeline);
    for (int i = 0; i < LESSON18_OBJECT_COUNT; i += 1) {
        lesson18_draw_scene_object(
            demo,
            command_buffer,
            render_pass,
            view_projection,
            &kLesson18Objects[i],
            &light_dir);
    }
}
