#include "forge_gpu_forward_scene.h"

#include "forge_gpu_deferred_scene.h"
#include "forge_gpu_gpu_helpers.h"
#include "forge_gpu_lesson_common.h"
#include "forge_gpu_math.h"
#include "forge_gpu_scene.h"

static_assert(sizeof(ForgeGpuForwardSceneVertUniforms) == 192, "shared forward scene vertex uniform size must match HLSL layout");
static_assert(sizeof(ForgeGpuForwardSceneFragUniforms) == 80, "shared forward scene fragment uniform size must match HLSL layout");
static_assert(sizeof(ForgeGpuForwardSkyboxVertUniforms) == 64, "shared forward skybox vertex uniform size must match HLSL layout");
static_assert(sizeof(ForgeGpuShadowedGridVertUniforms) == 128, "shared shadowed grid vertex uniform size must match HLSL layout");
static_assert(sizeof(ForgeGpuShadowedGridFragUniforms) == 80, "shared shadowed grid fragment uniform size must match HLSL layout");

bool ForgeGpuCreateShadowedGridBuffers(
    SDL_GPUDevice *device,
    float half_size,
    float y,
    SDL_GPUBuffer **vertex_buffer,
    SDL_GPUBuffer **index_buffer)
{
    const GridVertex vertices[4] = {
        { { -half_size, y, -half_size } },
        { {  half_size, y, -half_size } },
        { {  half_size, y,  half_size } },
        { { -half_size, y,  half_size } },
    };
    const Uint16 indices[6] = { 0, 1, 2, 0, 2, 3 };

    if (!vertex_buffer || !index_buffer) {
        SDL_SetError("shadowed grid buffer storage is missing");
        return false;
    }

    *vertex_buffer = ForgeGpuCreateBufferWithData(device, SDL_GPU_BUFFERUSAGE_VERTEX, vertices, sizeof(vertices));
    *index_buffer = ForgeGpuCreateBufferWithData(device, SDL_GPU_BUFFERUSAGE_INDEX, indices, sizeof(indices));
    return *vertex_buffer && *index_buffer;
}

static void ForgeGpuBindForwardPrimitive(SDL_GPURenderPass *render_pass, const GpuPrimitive *primitive)
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

static void ForgeGpuDrawBoundForwardPrimitive(SDL_GPURenderPass *render_pass, const GpuPrimitive *primitive)
{
    if (primitive->index_buffer && primitive->index_count > 0) {
        SDL_DrawGPUIndexedPrimitives(render_pass, primitive->index_count, 1, 0, 0, 0);
    } else {
        SDL_DrawGPUPrimitives(render_pass, primitive->vertex_count, 1, 0, 0);
    }
}

static void ForgeGpuPushForwardFragmentUniforms(
    SDL_GPUCommandBuffer *command_buffer,
    const GpuMaterial *material,
    const ForgeGpuForwardSceneDrawInfo *draw_info)
{
    ForgeGpuForwardSceneFragUniforms uniforms;

    SDL_zero(uniforms);
    SDL_memcpy(uniforms.base_color, material->base_color, sizeof(uniforms.base_color));
    uniforms.eye_pos[0] = draw_info->eye_pos.x;
    uniforms.eye_pos[1] = draw_info->eye_pos.y;
    uniforms.eye_pos[2] = draw_info->eye_pos.z;
    uniforms.has_texture = material->has_texture && material->texture ? 1.0f : 0.0f;
    uniforms.ambient = draw_info->lighting.ambient;
    uniforms.shininess = draw_info->lighting.shininess;
    uniforms.specular_str = draw_info->lighting.specular_strength;
    uniforms.light_dir[0] = draw_info->lighting.light_dir.x;
    uniforms.light_dir[1] = draw_info->lighting.light_dir.y;
    uniforms.light_dir[2] = draw_info->lighting.light_dir.z;
    uniforms.light_color[0] = draw_info->lighting.light_color.x;
    uniforms.light_color[1] = draw_info->lighting.light_color.y;
    uniforms.light_color[2] = draw_info->lighting.light_color.z;
    uniforms.light_intensity = draw_info->lighting.light_intensity;
    SDL_PushGPUFragmentUniformData(command_buffer, 0, &uniforms, sizeof(uniforms));
}

static void ForgeGpuBindForwardSceneSamplers(
    SDL_GPURenderPass *render_pass,
    SDL_GPUTexture *texture,
    const ForgeGpuForwardSceneDrawInfo *draw_info)
{
    SDL_GPUTextureSamplerBinding bindings[2];

    SDL_zeroa(bindings);
    bindings[0].texture = texture ? texture : draw_info->fallback_texture;
    bindings[0].sampler = draw_info->material_sampler;
    bindings[1].texture = draw_info->shadow_depth;
    bindings[1].sampler = draw_info->shadow_sampler;
    SDL_BindGPUFragmentSamplers(render_pass, 0, bindings, SDL_arraysize(bindings));
}

static void ForgeGpuPushForwardVertexUniforms(
    SDL_GPUCommandBuffer *command_buffer,
    Mat4 model_matrix,
    const ForgeGpuForwardSceneDrawInfo *draw_info)
{
    ForgeGpuForwardSceneVertUniforms uniforms;

    uniforms.mvp = mat4_multiply(draw_info->cam_vp, model_matrix);
    uniforms.model = model_matrix;
    uniforms.light_vp = draw_info->light_vp;
    SDL_PushGPUVertexUniformData(command_buffer, 0, &uniforms, sizeof(uniforms));
}

void ForgeGpuDrawForwardSceneModel(
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    const GpuSceneData *model,
    Mat4 placement,
    const ForgeGpuForwardSceneDrawInfo *draw_info)
{
    for (int node_index = 0; node_index < model->loaded.node_count; node_index += 1) {
        const ForgeGpuSceneNode *node = &model->loaded.nodes[node_index];

        if (node->mesh_index < 0 || node->mesh_index >= model->loaded.mesh_count) {
            continue;
        }
        ForgeGpuDrawForwardSceneMesh(
            command_buffer,
            render_pass,
            model,
            node->mesh_index,
            mat4_multiply(placement, mat4_from_forge(node->world_transform)),
            draw_info);
    }
}

void ForgeGpuDrawForwardSceneMesh(
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    const GpuSceneData *model,
    int mesh_index,
    Mat4 model_matrix,
    const ForgeGpuForwardSceneDrawInfo *draw_info)
{
    const ForgeGpuLoadedScene *scene = &model->loaded;
    const ForgeGpuSceneMesh *mesh;

    if (mesh_index < 0 || mesh_index >= scene->mesh_count) {
        return;
    }

    ForgeGpuPushForwardVertexUniforms(command_buffer, model_matrix, draw_info);

    mesh = &scene->meshes[mesh_index];
    for (int primitive_offset = 0; primitive_offset < mesh->primitive_count; primitive_offset += 1) {
        const int primitive_index = mesh->first_primitive + primitive_offset;
        const GpuPrimitive *primitive;
        GpuMaterial fallback_material;
        const GpuMaterial *material;
        SDL_GPUTexture *texture;

        if (primitive_index < 0 || primitive_index >= model->primitive_count) {
            continue;
        }
        primitive = &model->primitives[primitive_index];
        if (!primitive->vertex_buffer) {
            continue;
        }

        material = ForgeGpuModelMaterialOrDefault(model, primitive->material_index, &fallback_material);
        texture = material->has_texture && material->texture ? material->texture : draw_info->fallback_texture;
        ForgeGpuPushForwardFragmentUniforms(command_buffer, material, draw_info);
        ForgeGpuBindForwardSceneSamplers(render_pass, texture, draw_info);
        ForgeGpuBindForwardPrimitive(render_pass, primitive);
        ForgeGpuDrawBoundForwardPrimitive(render_pass, primitive);
    }
}

void ForgeGpuDrawForwardSceneBuffer(
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    SDL_GPUBuffer *vertex_buffer,
    SDL_GPUBuffer *index_buffer,
    SDL_GPUIndexElementSize index_type,
    Uint32 index_count,
    Mat4 model_matrix,
    const GpuMaterial *material,
    const ForgeGpuForwardSceneDrawInfo *draw_info)
{
    SDL_GPUBufferBinding vertex_binding;
    SDL_GPUBufferBinding index_binding;
    SDL_GPUTexture *texture = material->has_texture && material->texture ? material->texture : draw_info->fallback_texture;

    ForgeGpuPushForwardVertexUniforms(command_buffer, model_matrix, draw_info);
    ForgeGpuPushForwardFragmentUniforms(command_buffer, material, draw_info);
    ForgeGpuBindForwardSceneSamplers(render_pass, texture, draw_info);

    SDL_zero(vertex_binding);
    vertex_binding.buffer = vertex_buffer;
    SDL_BindGPUVertexBuffers(render_pass, 0, &vertex_binding, 1);
    SDL_zero(index_binding);
    index_binding.buffer = index_buffer;
    SDL_BindGPUIndexBuffer(render_pass, &index_binding, index_type);
    SDL_DrawGPUIndexedPrimitives(render_pass, index_count, 1, 0, 0, 0);
}

void ForgeGpuDrawForwardShadowMesh(
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    const GpuSceneData *model,
    int mesh_index,
    Mat4 model_matrix,
    Mat4 light_vp)
{
    const ForgeGpuLoadedScene *scene = &model->loaded;
    const ForgeGpuSceneMesh *mesh;
    ForgeGpuShadowVertUniforms uniforms;

    if (mesh_index < 0 || mesh_index >= scene->mesh_count) {
        return;
    }

    uniforms.light_mvp = mat4_multiply(light_vp, model_matrix);
    SDL_PushGPUVertexUniformData(command_buffer, 0, &uniforms, sizeof(uniforms));

    mesh = &scene->meshes[mesh_index];
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
        ForgeGpuBindForwardPrimitive(render_pass, primitive);
        ForgeGpuDrawBoundForwardPrimitive(render_pass, primitive);
    }
}

void ForgeGpuDrawForwardShadowBuffer(
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    SDL_GPUBuffer *vertex_buffer,
    SDL_GPUBuffer *index_buffer,
    SDL_GPUIndexElementSize index_type,
    Uint32 index_count,
    Mat4 light_mvp)
{
    ForgeGpuShadowVertUniforms uniforms;
    SDL_GPUBufferBinding vertex_binding;
    SDL_GPUBufferBinding index_binding;

    uniforms.light_mvp = light_mvp;
    SDL_PushGPUVertexUniformData(command_buffer, 0, &uniforms, sizeof(uniforms));

    SDL_zero(vertex_binding);
    vertex_binding.buffer = vertex_buffer;
    SDL_BindGPUVertexBuffers(render_pass, 0, &vertex_binding, 1);
    SDL_zero(index_binding);
    index_binding.buffer = index_buffer;
    SDL_BindGPUIndexBuffer(render_pass, &index_binding, index_type);
    SDL_DrawGPUIndexedPrimitives(render_pass, index_count, 1, 0, 0, 0);
}

void ForgeGpuDrawForwardSkybox(
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    SDL_GPUTexture *cubemap_texture,
    SDL_GPUSampler *sampler,
    SDL_GPUBuffer *vertex_buffer,
    SDL_GPUBuffer *index_buffer,
    SDL_GPUIndexElementSize index_type,
    Uint32 index_count,
    Mat4 view,
    Mat4 projection)
{
    ForgeGpuForwardSkyboxVertUniforms uniforms;
    SDL_GPUTextureSamplerBinding sampler_binding;
    SDL_GPUBufferBinding vertex_binding;
    SDL_GPUBufferBinding index_binding;

    view.m[12] = 0.0f;
    view.m[13] = 0.0f;
    view.m[14] = 0.0f;
    uniforms.vp_no_translation = mat4_multiply(projection, view);
    SDL_PushGPUVertexUniformData(command_buffer, 0, &uniforms, sizeof(uniforms));

    SDL_zero(sampler_binding);
    sampler_binding.texture = cubemap_texture;
    sampler_binding.sampler = sampler;
    SDL_BindGPUFragmentSamplers(render_pass, 0, &sampler_binding, 1);

    SDL_zero(vertex_binding);
    vertex_binding.buffer = vertex_buffer;
    SDL_BindGPUVertexBuffers(render_pass, 0, &vertex_binding, 1);
    SDL_zero(index_binding);
    index_binding.buffer = index_buffer;
    SDL_BindGPUIndexBuffer(render_pass, &index_binding, index_type);
    SDL_DrawGPUIndexedPrimitives(render_pass, index_count, 1, 0, 0, 0);
}

void ForgeGpuDrawShadowedGrid(
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    SDL_GPUGraphicsPipeline *pipeline,
    SDL_GPUBuffer *vertex_buffer,
    SDL_GPUBuffer *index_buffer,
    const ForgeGpuShadowedGridDrawInfo *draw_info)
{
    ForgeGpuShadowedGridVertUniforms vertex_uniforms;
    ForgeGpuShadowedGridFragUniforms fragment_uniforms;
    SDL_GPUTextureSamplerBinding shadow_binding;
    SDL_GPUBufferBinding vertex_binding;
    SDL_GPUBufferBinding index_binding;

    SDL_BindGPUGraphicsPipeline(render_pass, pipeline);

    vertex_uniforms.vp = draw_info->vp;
    vertex_uniforms.light_vp = draw_info->light_vp;
    SDL_PushGPUVertexUniformData(command_buffer, 0, &vertex_uniforms, sizeof(vertex_uniforms));

    SDL_zero(fragment_uniforms);
    SDL_memcpy(fragment_uniforms.line_color, draw_info->line_color, sizeof(fragment_uniforms.line_color));
    SDL_memcpy(fragment_uniforms.bg_color, draw_info->bg_color, sizeof(fragment_uniforms.bg_color));
    fragment_uniforms.light_dir[0] = draw_info->light_dir.x;
    fragment_uniforms.light_dir[1] = draw_info->light_dir.y;
    fragment_uniforms.light_dir[2] = draw_info->light_dir.z;
    fragment_uniforms.light_intensity = draw_info->light_intensity;
    fragment_uniforms.eye_pos[0] = draw_info->eye_pos.x;
    fragment_uniforms.eye_pos[1] = draw_info->eye_pos.y;
    fragment_uniforms.eye_pos[2] = draw_info->eye_pos.z;
    fragment_uniforms.grid_spacing = draw_info->grid_spacing;
    fragment_uniforms.line_width = draw_info->line_width;
    fragment_uniforms.fade_distance = draw_info->fade_distance;
    fragment_uniforms.ambient = draw_info->ambient;
    SDL_PushGPUFragmentUniformData(command_buffer, 0, &fragment_uniforms, sizeof(fragment_uniforms));

    SDL_zero(shadow_binding);
    shadow_binding.texture = draw_info->shadow_depth;
    shadow_binding.sampler = draw_info->shadow_sampler;
    SDL_BindGPUFragmentSamplers(render_pass, 0, &shadow_binding, 1);

    SDL_zero(vertex_binding);
    vertex_binding.buffer = vertex_buffer;
    SDL_BindGPUVertexBuffers(render_pass, 0, &vertex_binding, 1);
    SDL_zero(index_binding);
    index_binding.buffer = index_buffer;
    SDL_BindGPUIndexBuffer(render_pass, &index_binding, SDL_GPU_INDEXELEMENTSIZE_16BIT);
    SDL_DrawGPUIndexedPrimitives(render_pass, 6, 1, 0, 0, 0);
}
