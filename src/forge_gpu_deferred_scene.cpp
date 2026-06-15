#include "forge_gpu_deferred_scene.h"

#include "forge_gpu_lesson_common.h"
#include "forge_gpu_math.h"
#include "forge_gpu_scene.h"

static_assert(sizeof(ForgeGpuShadowVertUniforms) == 64, "shared shadow vertex uniform size must match HLSL layout");
static_assert(sizeof(ForgeGpuDeferredSceneVertUniforms) == 256, "shared deferred scene vertex uniform size must match HLSL layout");
static_assert(sizeof(ForgeGpuDeferredSceneFragUniforms) == 80, "shared deferred scene fragment uniform size must match HLSL layout");

Mat4 ForgeGpuComputeDirectionalLightViewProjection(
    Vec3 light_dir,
    float light_distance,
    float ortho_size,
    float near_plane,
    float far_plane,
    float parallel_threshold)
{
    const Vec3 target = { 0.0f, 0.0f, 0.0f };

    return ForgeGpuComputeTargetedDirectionalLightViewProjection(
        light_dir,
        light_distance,
        target,
        ortho_size,
        near_plane,
        far_plane,
        parallel_threshold);
}

Mat4 ForgeGpuBoxPlacementMatrix(const ForgeGpuBoxPlacement *placement)
{
    return mat4_multiply(mat4_translate(placement->position), mat4_rotate_y(placement->y_rotation));
}

static void ForgeGpuBindPrimitive(SDL_GPURenderPass *render_pass, const GpuPrimitive *primitive)
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

static void ForgeGpuDrawBoundPrimitive(SDL_GPURenderPass *render_pass, const GpuPrimitive *primitive)
{
    if (primitive->index_buffer && primitive->index_count > 0) {
        SDL_DrawGPUIndexedPrimitives(render_pass, primitive->index_count, 1, 0, 0, 0);
    } else {
        SDL_DrawGPUPrimitives(render_pass, primitive->vertex_count, 1, 0, 0);
    }
}

void ForgeGpuDrawModelShadow(
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    const GpuSceneData *model,
    Mat4 placement,
    Mat4 light_vp)
{
    for (int node_index = 0; node_index < model->loaded.node_count; node_index += 1) {
        const ForgeGpuSceneNode *node = &model->loaded.nodes[node_index];
        const ForgeGpuSceneMesh *mesh;
        Mat4 model_matrix;
        ForgeGpuShadowVertUniforms uniforms;

        if (node->mesh_index < 0 || node->mesh_index >= model->loaded.mesh_count) {
            continue;
        }
        mesh = &model->loaded.meshes[node->mesh_index];
        model_matrix = mat4_multiply(placement, mat4_from_forge(node->world_transform));
        uniforms.light_mvp = mat4_multiply(light_vp, model_matrix);
        SDL_PushGPUVertexUniformData(command_buffer, 0, &uniforms, sizeof(uniforms));

        for (int primitive_offset = 0; primitive_offset < mesh->primitive_count; primitive_offset += 1) {
            const int primitive_index = mesh->first_primitive + primitive_offset;
            const GpuPrimitive *primitive;

            if (primitive_index < 0 || primitive_index >= model->primitive_count) {
                continue;
            }
            primitive = &model->primitives[primitive_index];
            if (!primitive->vertex_buffer) {
                continue;
            }
            ForgeGpuBindPrimitive(render_pass, primitive);
            ForgeGpuDrawBoundPrimitive(render_pass, primitive);
        }
    }
}

void ForgeGpuDrawDeferredSceneModel(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    const GpuSceneData *model,
    Mat4 placement,
    const ForgeGpuDeferredSceneDrawInfo *draw_info)
{
    for (int node_index = 0; node_index < model->loaded.node_count; node_index += 1) {
        const ForgeGpuSceneNode *node = &model->loaded.nodes[node_index];
        const ForgeGpuSceneMesh *mesh;
        Mat4 model_matrix;
        Mat4 mvp;
        ForgeGpuDeferredSceneVertUniforms vertex_uniforms;

        if (node->mesh_index < 0 || node->mesh_index >= model->loaded.mesh_count) {
            continue;
        }
        mesh = &model->loaded.meshes[node->mesh_index];
        model_matrix = mat4_multiply(placement, mat4_from_forge(node->world_transform));
        mvp = mat4_multiply(draw_info->cam_vp, model_matrix);

        vertex_uniforms.mvp = mvp;
        vertex_uniforms.model = model_matrix;
        vertex_uniforms.view = draw_info->view;
        vertex_uniforms.light_vp = draw_info->light_vp;
        SDL_PushGPUVertexUniformData(command_buffer, 0, &vertex_uniforms, sizeof(vertex_uniforms));

        for (int primitive_offset = 0; primitive_offset < mesh->primitive_count; primitive_offset += 1) {
            const int primitive_index = mesh->first_primitive + primitive_offset;
            const GpuPrimitive *primitive;
            GpuMaterial fallback_material;
            const GpuMaterial *material;
            SDL_GPUTextureSamplerBinding sampler_bindings[2];
            SDL_GPUTexture *texture;
            ForgeGpuDeferredSceneFragUniforms fragment_uniforms;

            if (primitive_index < 0 || primitive_index >= model->primitive_count) {
                continue;
            }
            primitive = &model->primitives[primitive_index];
            if (!primitive->vertex_buffer) {
                continue;
            }
            material = ForgeGpuModelMaterialOrDefault(model, primitive->material_index, &fallback_material);
            texture = material->has_texture && material->texture ? material->texture : demo->lesson.white_texture;

            SDL_zero(fragment_uniforms);
            SDL_memcpy(fragment_uniforms.base_color, material->base_color, sizeof(fragment_uniforms.base_color));
            fragment_uniforms.eye_pos[0] = demo->lesson.camera_position.x;
            fragment_uniforms.eye_pos[1] = demo->lesson.camera_position.y;
            fragment_uniforms.eye_pos[2] = demo->lesson.camera_position.z;
            fragment_uniforms.has_texture = material->has_texture && material->texture ? 1.0f : 0.0f;
            fragment_uniforms.ambient = draw_info->lighting.ambient;
            fragment_uniforms.shininess = draw_info->lighting.shininess;
            fragment_uniforms.specular_str = draw_info->lighting.specular_strength;
            fragment_uniforms.light_dir[0] = draw_info->lighting.light_dir.x;
            fragment_uniforms.light_dir[1] = draw_info->lighting.light_dir.y;
            fragment_uniforms.light_dir[2] = draw_info->lighting.light_dir.z;
            fragment_uniforms.light_color[0] = draw_info->lighting.light_color.x;
            fragment_uniforms.light_color[1] = draw_info->lighting.light_color.y;
            fragment_uniforms.light_color[2] = draw_info->lighting.light_color.z;
            fragment_uniforms.light_intensity = draw_info->lighting.light_intensity;
            SDL_PushGPUFragmentUniformData(command_buffer, 0, &fragment_uniforms, sizeof(fragment_uniforms));

            SDL_zeroa(sampler_bindings);
            sampler_bindings[0].texture = texture;
            sampler_bindings[0].sampler = draw_info->material_sampler;
            sampler_bindings[1].texture = draw_info->shadow_depth;
            sampler_bindings[1].sampler = draw_info->shadow_sampler;
            SDL_BindGPUFragmentSamplers(render_pass, 0, sampler_bindings, SDL_arraysize(sampler_bindings));

            ForgeGpuBindPrimitive(render_pass, primitive);
            ForgeGpuDrawBoundPrimitive(render_pass, primitive);
        }
    }
}

void ForgeGpuDrawShadowedBoxScene(
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    const GpuSceneData *primary_model,
    const GpuSceneData *box_model,
    const ForgeGpuBoxPlacement *box_placements,
    int box_count,
    Mat4 light_vp)
{
    ForgeGpuDrawModelShadow(command_buffer, render_pass, primary_model, mat4_identity(), light_vp);
    for (int i = 0; i < box_count; i += 1) {
        ForgeGpuDrawModelShadow(
            command_buffer,
            render_pass,
            box_model,
            ForgeGpuBoxPlacementMatrix(&box_placements[i]),
            light_vp);
    }
}

void ForgeGpuDrawDeferredBoxScene(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    const GpuSceneData *primary_model,
    const GpuSceneData *box_model,
    const ForgeGpuBoxPlacement *box_placements,
    int box_count,
    const ForgeGpuDeferredSceneDrawInfo *draw_info)
{
    ForgeGpuDrawDeferredSceneModel(demo, command_buffer, render_pass, primary_model, mat4_identity(), draw_info);
    for (int i = 0; i < box_count; i += 1) {
        ForgeGpuDrawDeferredSceneModel(
            demo,
            command_buffer,
            render_pass,
            box_model,
            ForgeGpuBoxPlacementMatrix(&box_placements[i]),
            draw_info);
    }
}
