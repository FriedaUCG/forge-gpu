#include "forge_gpu_lessons.h"

#include "forge_gpu_browser_status.h"
#include "forge_gpu_camera.h"
#include "forge_gpu_gpu_helpers.h"
#include "forge_gpu_processed_scene_renderer.h"
#include "forge_gpu_shader_layouts.h"
#include "shaders/generated/forge_gpu_lesson_52_shaders.h"
#include "shaders/generated/forge_gpu_shared_scene_shaders.h"

#include "imgui.h"

#define LESSON52_FAR_PLANE 200.0f
#define LESSON52_MOVE_SPEED 5.0f
#define LESSON52_MOUSE_SENSITIVITY 0.003f
#define LESSON52_PITCH_CLAMP 1.5f
#define LESSON52_CAM_START_X 0.0f
#define LESSON52_CAM_START_Y 3.0f
#define LESSON52_CAM_START_Z 10.0f
#define LESSON52_CAM_START_YAW 0.0f
#define LESSON52_CAM_START_PITCH (-0.15f)
#define LESSON52_GRID_COL_SPACING 5.0f
#define LESSON52_GRID_ROW_SPACING 5.0f
#define LESSON52_GRID_OFFSET_X (-2.5f)
#define LESSON52_GRID_OFFSET_Z (-5.0f)
#define LESSON52_EMISSIVE_R 0.2f
#define LESSON52_EMISSIVE_G 0.6f
#define LESSON52_EMISSIVE_B 1.0f
#define LESSON52_EMISSIVE_TEXTURE_SIZE 64u
#define LESSON52_EMISSIVE_GRID_SPACING 8u

typedef struct Lesson52MaterialInfo
{
    const char *dir;
    const char *label;
    bool metallic;
    bool occlusion;
} Lesson52MaterialInfo;

typedef struct Lesson52State
{
    ForgeGpuProcessedSceneRenderer renderer;
    ForgeGpuProcessedSceneModel shaderball;
    ForgeGpuProcessedMaterialSet material_sets[6];
    ForgeGpuProcessedSceneMaterialTextures material_textures[6];
    SDL_GPUGraphicsPipeline *pbr_pipeline;
    SDL_GPUTexture *emissive_texture;
    Uint32 pbr_draw_calls[6];
    Uint32 total_pbr_draw_calls;
    Uint32 separate_mr_materials;
    Uint32 metallic_materials;
    Uint32 occlusion_materials;
} Lesson52State;

static const Lesson52MaterialInfo kLesson52Materials[] = {
    { "Metal046B", "Metal 046B", true, false },
    { "Metal048A", "Metal 048A", true, false },
    { "Metal061B", "Metal 061B", true, false },
    { "ChristmasTreeOrnament021", "Ornament", true, false },
    { "Rock026", "Rock", false, true },
    { "WoodFloor051", "Wood Floor", false, true }
};

static const float kLesson52EmissiveFactor[3] = { 1.0f, 1.0f, 1.0f };

static Lesson52State *lesson52_state(ForgeGpuDemo *demo)
{
    return (Lesson52State *)demo->lesson.private_state;
}

static void lesson52_release_shader(SDL_GPUDevice *device, SDL_GPUShader **shader)
{
    if (*shader) {
        SDL_ReleaseGPUShader(device, *shader);
        *shader = nullptr;
    }
}

static bool lesson52_create_pbr_pipeline(ForgeGpuDemo *demo, Lesson52State *state)
{
    SDL_GPUShader *model_vs = nullptr;
    SDL_GPUShader *pbr_fs = nullptr;
    bool ok = false;

    model_vs = ForgeGpuCreateShader(demo->device, SDL_GPU_SHADERSTAGE_VERTEX,
        forge_scene_model_vert_wgsl, forge_scene_model_vert_wgsl_size,
        forge_scene_model_vert_msl, forge_scene_model_vert_msl_size,
        0, 0, 0, 1);
    pbr_fs = ForgeGpuCreateShaderWithResourceLayout(demo->device,
        lesson52_pbr_textures_frag_wgsl, lesson52_pbr_textures_frag_wgsl_size,
        lesson52_pbr_textures_frag_msl, lesson52_pbr_textures_frag_msl_size,
        ForgeGpuShaderLayout_lesson52_pbr_textures_frag());

    if (!model_vs || !pbr_fs) {
        goto done;
    }

    state->pbr_pipeline = ForgeGpuProcessedSceneCreateModelPipeline(demo, model_vs, pbr_fs);
    ok = state->pbr_pipeline != nullptr;

done:
    lesson52_release_shader(demo->device, &model_vs);
    lesson52_release_shader(demo->device, &pbr_fs);
    return ok;
}

static SDL_GPUTexture *lesson52_create_emissive_texture(SDL_GPUDevice *device)
{
    const Uint32 size = LESSON52_EMISSIVE_TEXTURE_SIZE;
    const size_t byte_count = (size_t)size * (size_t)size * 4u;
    Uint8 *pixels = (Uint8 *)SDL_calloc(1, byte_count);
    SDL_GPUTexture *texture;
    const Uint8 er = (Uint8)(LESSON52_EMISSIVE_R * 255.0f);
    const Uint8 eg = (Uint8)(LESSON52_EMISSIVE_G * 255.0f);
    const Uint8 eb = (Uint8)(LESSON52_EMISSIVE_B * 255.0f);

    if (!pixels) {
        SDL_OutOfMemory();
        return nullptr;
    }

    for (Uint32 y = 0; y < size; y += 1) {
        for (Uint32 x = 0; x < size; x += 1) {
            const bool line = (x % LESSON52_EMISSIVE_GRID_SPACING) == 0 ||
                (y % LESSON52_EMISSIVE_GRID_SPACING) == 0;
            Uint8 *pixel = pixels + ((size_t)y * size + x) * 4u;
            pixel[0] = line ? er : 0;
            pixel[1] = line ? eg : 0;
            pixel[2] = line ? eb : 0;
            pixel[3] = 255;
        }
    }

    texture = ForgeGpuCreateRgba8TextureFromPixels(device, size, size, pixels, false);
    SDL_free(pixels);
    return texture;
}

static bool lesson52_validate_shaderball_fixture(const Lesson52State *state)
{
    const ForgeGpuProcessedMaterial *material;

    if (state->shaderball.scene.node_count != 8 ||
        state->shaderball.mesh.submesh_count != 2 ||
        state->shaderball.materials.material_count != 1) {
        SDL_SetError("lesson 52 Shaderball fixture shape changed");
        return false;
    }
    if ((state->shaderball.mesh.flags & FORGE_GPU_PROCESSED_MESH_FLAG_TANGENTS) == 0) {
        SDL_SetError("lesson 52 Shaderball fixture requires tangent-bearing mesh data");
        return false;
    }

    material = &state->shaderball.materials.materials[0];
    if (material->alpha_mode != FORGE_GPU_PROCESSED_ALPHA_OPAQUE || material->double_sided) {
        SDL_SetError("lesson 52 Shaderball fixture must stay opaque and single-sided for the PBR texture pipeline");
        return false;
    }
    return true;
}

static bool lesson52_validate_material(
    const Lesson52MaterialInfo *info,
    const ForgeGpuProcessedMaterialSet *set,
    const ForgeGpuProcessedSceneMaterialTextures *textures)
{
    const ForgeGpuProcessedMaterial *material;

    if (set->material_count != 1) {
        SDL_SetError("lesson 52 material %s expected exactly one material, got %u",
            info->dir, (unsigned)set->material_count);
        return false;
    }
    material = &set->materials[0];
    if (SDL_strcmp(material->name, info->dir) != 0 ||
        material->alpha_mode != FORGE_GPU_PROCESSED_ALPHA_OPAQUE ||
        material->double_sided ||
        material->metallic_roughness_texture[0] != '\0' ||
        material->base_color_texture[0] == '\0' ||
        material->normal_texture[0] == '\0' ||
        material->roughness_texture[0] == '\0' ||
        material->emissive_texture[0] != '\0') {
        SDL_SetError("lesson 52 material %s sidecar shape changed", info->dir);
        return false;
    }
    if (info->metallic != (material->metallic_texture[0] != '\0') ||
        info->occlusion != (material->occlusion_texture[0] != '\0') ||
        info->metallic != (material->metallic_factor > 0.5f) ||
        !textures->uses_separate_metallic_roughness) {
        SDL_SetError("lesson 52 material %s PBR texture facts changed", info->dir);
        return false;
    }
    return true;
}

static bool lesson52_load_material(ForgeGpuDemo *demo, Lesson52State *state, Uint32 material_index)
{
    const Lesson52MaterialInfo *info = &kLesson52Materials[material_index];
    char base_relative[128];
    char fmat_file[64];
    char fmat_path[FORGE_GPU_MAX_PATH];
    ForgeGpuProcessedMaterialSet *set = &state->material_sets[material_index];
    ForgeGpuProcessedSceneMaterialTextures *textures = &state->material_textures[material_index];

    if (SDL_snprintf(base_relative, sizeof(base_relative),
            "processed/52-pbr-textures/materials/%s", info->dir) <= 0 ||
        SDL_snprintf(fmat_file, sizeof(fmat_file), "%s.fmat", info->dir) <= 0 ||
        !ForgeGpuProcessedSceneJoinModelPath(demo, base_relative, fmat_file, fmat_path, sizeof(fmat_path)) ||
        !ForgeGpuLoadProcessedMaterials(fmat_path, set)) {
        return false;
    }
    if (set->material_count != 1) {
        SDL_SetError("lesson 52 material %s expected exactly one material, got %u",
            info->dir, (unsigned)set->material_count);
        return false;
    }
    if (!ForgeGpuProcessedSceneLoadPbrMaterialTextures(
            demo, &state->renderer, &state->shaderball, base_relative,
            &set->materials[0], textures) ||
        !lesson52_validate_material(info, set, textures)) {
        return false;
    }
    if (info->metallic) {
        state->metallic_materials += 1;
    }
    if (info->occlusion) {
        state->occlusion_materials += 1;
    }
    if (textures->uses_separate_metallic_roughness) {
        state->separate_mr_materials += 1;
    }
    return true;
}

bool ForgeGpuCreateLesson52(ForgeGpuDemo *demo)
{
    Lesson52State *state = (Lesson52State *)SDL_calloc(1, sizeof(*state));

    if (!state) {
        SDL_OutOfMemory();
        return false;
    }
    demo->lesson.private_state = state;

    if (!ForgeGpuProcessedSceneRendererCreate(demo, &state->renderer) ||
        !ForgeGpuProcessedSceneLoadModel(
            demo,
            &state->renderer,
            &state->shaderball,
            "processed/51-pbr-shading/Shaderball",
            "Shaderball",
            ForgeGpuProcessedSceneLoadRgbaMaterialTexture,
            nullptr) ||
        !lesson52_validate_shaderball_fixture(state)) {
        return false;
    }

    for (Uint32 i = 0; i < SDL_arraysize(kLesson52Materials); i += 1) {
        if (!lesson52_load_material(demo, state, i)) {
            return false;
        }
    }

    state->emissive_texture = lesson52_create_emissive_texture(demo->device);
    if (!state->emissive_texture) {
        return false;
    }
    state->material_textures[0].emissive = state->emissive_texture;

    if (!lesson52_create_pbr_pipeline(demo, state)) {
        return false;
    }

    demo->lesson.camera_position = {
        LESSON52_CAM_START_X,
        LESSON52_CAM_START_Y,
        LESSON52_CAM_START_Z
    };
    demo->lesson.camera_yaw = LESSON52_CAM_START_YAW;
    demo->lesson.camera_pitch = LESSON52_CAM_START_PITCH;
    demo->lesson.move_speed = LESSON52_MOVE_SPEED;
    demo->lesson.mouse_sensitivity = LESSON52_MOUSE_SENSITIVITY;
    demo->lesson.pitch_clamp = LESSON52_PITCH_CLAMP;
    demo->lesson.last_ticks = SDL_GetTicks();
    return true;
}

bool ForgeGpuRenderLesson52(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPUTexture *swapchain_texture,
    Uint32 width,
    Uint32 height)
{
    Lesson52State *state = lesson52_state(demo);
    Mat4 view;
    Mat4 projection;
    Mat4 camera_vp;
    Mat4 light_vp;
    SDL_GPURenderPass *render_pass;

    if (!state) {
        SDL_SetError("lesson 52 internal state is missing");
        return false;
    }
    if (!ForgeGpuProcessedSceneRendererEnsureMainDepth(demo, &state->renderer, width, height)) {
        return false;
    }

    ForgeGpuUpdateCameraFromInput(demo);
    ForgeGpuCameraViewProjection(demo, width, height, LESSON52_FAR_PLANE, &view, &projection);
    camera_vp = mat4_multiply(projection, view);
    light_vp = ForgeGpuProcessedSceneLightViewProjection();

    ForgeGpuProcessedSceneRendererBeginFrame(&state->renderer);
    ForgeGpuProcessedSceneResetModelDrawCounts(&state->shaderball);
    SDL_zeroa(state->pbr_draw_calls);
    state->total_pbr_draw_calls = 0;

    render_pass = ForgeGpuProcessedSceneBeginShadowPass(command_buffer, &state->renderer);
    if (!render_pass) {
        return false;
    }
    for (Uint32 i = 0; i < SDL_arraysize(kLesson52Materials); i += 1) {
        const Uint32 col = i % 2u;
        const Uint32 row = i / 2u;
        const float x = LESSON52_GRID_OFFSET_X + (float)col * LESSON52_GRID_COL_SPACING;
        const float z = LESSON52_GRID_OFFSET_Z + (float)row * LESSON52_GRID_ROW_SPACING;
        const Mat4 placement = mat4_translate({ x, 0.0f, z });

        if (!ForgeGpuProcessedSceneDrawModel(
                demo, command_buffer, render_pass, &state->renderer, &state->shaderball,
                placement, camera_vp, light_vp, true)) {
            SDL_EndGPURenderPass(render_pass);
            return false;
        }
    }
    SDL_EndGPURenderPass(render_pass);
    state->renderer.shadow_pass_rendered = true;

    render_pass = ForgeGpuProcessedSceneBeginMainPass(command_buffer, &state->renderer, swapchain_texture);
    if (!render_pass) {
        return false;
    }
    for (Uint32 i = 0; i < SDL_arraysize(kLesson52Materials); i += 1) {
        const Uint32 col = i % 2u;
        const Uint32 row = i / 2u;
        const float x = LESSON52_GRID_OFFSET_X + (float)col * LESSON52_GRID_COL_SPACING;
        const float z = LESSON52_GRID_OFFSET_Z + (float)row * LESSON52_GRID_ROW_SPACING;
        const Mat4 placement = mat4_translate({ x, 0.0f, z });
        const Uint32 before_draws = state->shaderball.draw_calls;

        if (!ForgeGpuProcessedSceneDrawModelWithPbrMaterial(
                demo, command_buffer, render_pass, &state->renderer, &state->shaderball,
                state->pbr_pipeline, placement, camera_vp, light_vp,
                &state->material_sets[i].materials[0],
                &state->material_textures[i],
                i == 0 ? kLesson52EmissiveFactor : nullptr)) {
            SDL_EndGPURenderPass(render_pass);
            return false;
        }
        state->pbr_draw_calls[i] = state->shaderball.draw_calls - before_draws;
        state->total_pbr_draw_calls += state->pbr_draw_calls[i];
    }

    ForgeGpuProcessedSceneDrawGrid(demo, command_buffer, render_pass, &state->renderer, camera_vp, light_vp);
    SDL_EndGPURenderPass(render_pass);
    state->renderer.main_pass_rendered = true;
    return true;
}

void ForgeGpuDebugLesson52(ForgeGpuDemo *demo)
{
    Lesson52State *state = lesson52_state(demo);

    if (!state) {
        return;
    }

    ImGui::Text("Shaderballs: %u materials, %u cached textures",
        (unsigned)SDL_arraysize(kLesson52Materials),
        state->shaderball.texture_cache_count);
    ImGui::Text("Draw calls: PBR %u, shadow %u",
        state->total_pbr_draw_calls,
        state->shaderball.shadow_draw_calls);
    ImGui::Text("Passes: shadow %s, main %s",
        state->renderer.shadow_pass_rendered ? "yes" : "no",
        state->renderer.main_pass_rendered ? "yes" : "no");
    for (Uint32 i = 0; i < SDL_arraysize(kLesson52Materials); i += 1) {
        ImGui::Text("%s: %s Color Nrm Rough %s%s",
            kLesson52Materials[i].label,
            kLesson52Materials[i].metallic ? "Metallic" : "Dielectric",
            kLesson52Materials[i].metallic ? "Metal " : "",
            kLesson52Materials[i].occlusion ? "AO" : "");
    }
}

void ForgeGpuExportLesson52Metrics(ForgeGpuDemo *demo)
{
    Lesson52State *state = lesson52_state(demo);

    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson52Ready", state ? 1.0 : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson52ShadowPass", state && state->renderer.shadow_pass_rendered ? 1.0 : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson52MainPass", state && state->renderer.main_pass_rendered ? 1.0 : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson52Nodes", state ? (double)state->shaderball.scene.node_count : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson52Materials", state ? (double)SDL_arraysize(kLesson52Materials) : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson52Submeshes", state ? (double)state->shaderball.mesh.submesh_count : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson52PbrDrawCalls", state ? (double)state->total_pbr_draw_calls : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson52ShadowDrawCalls", state ? (double)state->shaderball.shadow_draw_calls : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson52SeparateMrMaterials", state ? (double)state->separate_mr_materials : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson52MetallicMaterials", state ? (double)state->metallic_materials : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson52OcclusionMaterials", state ? (double)state->occlusion_materials : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson52TextureCacheCount", state ? (double)state->shaderball.texture_cache_count : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson52ProceduralEmissive", state && state->emissive_texture ? 1.0 : 0.0);
}

void ForgeGpuDestroyLesson52(ForgeGpuDemo *demo)
{
    Lesson52State *state = lesson52_state(demo);

    if (!state) {
        return;
    }

    if (state->pbr_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, state->pbr_pipeline);
    }
    if (state->emissive_texture) {
        SDL_ReleaseGPUTexture(demo->device, state->emissive_texture);
    }
    for (Uint32 i = 0; i < SDL_arraysize(state->material_sets); i += 1) {
        ForgeGpuFreeProcessedMaterials(&state->material_sets[i]);
    }
    ForgeGpuProcessedSceneDestroyModel(demo->device, &state->shaderball);
    ForgeGpuProcessedSceneRendererDestroy(demo, &state->renderer);
    SDL_free(state);
    demo->lesson.private_state = nullptr;
}
