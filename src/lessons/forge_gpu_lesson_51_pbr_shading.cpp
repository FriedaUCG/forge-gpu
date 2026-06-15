#include "forge_gpu_lessons.h"

#include "forge_gpu_browser_status.h"
#include "forge_gpu_camera.h"
#include "forge_gpu_gpu_helpers.h"
#include "forge_gpu_processed_scene_renderer.h"
#include "forge_gpu_shader_layouts.h"
#include "shaders/generated/forge_gpu_lesson_51_shaders.h"
#include "shaders/generated/forge_gpu_shared_scene_shaders.h"

#include "imgui.h"

#define LESSON51_FAR_PLANE 200.0f
#define LESSON51_MOVE_SPEED 5.0f
#define LESSON51_MOUSE_SENSITIVITY 0.003f
#define LESSON51_PITCH_CLAMP 1.5f
#define LESSON51_MODEL_LEFT_X (-2.0f)
#define LESSON51_MODEL_RIGHT_X 2.0f
#define LESSON51_MODEL_Y 0.0f
#define LESSON51_CAM_START_X (-1.5f)
#define LESSON51_CAM_START_Y 2.0f
#define LESSON51_CAM_START_Z 7.0f
#define LESSON51_CAM_START_YAW 0.0f
#define LESSON51_CAM_START_PITCH (-0.2f)
#define LESSON51_DEFAULT_METALLIC 0.0f
#define LESSON51_DEFAULT_ROUGHNESS 0.5f
#define LESSON51_DEFAULT_BASE_R 0.8f
#define LESSON51_DEFAULT_BASE_G 0.8f
#define LESSON51_DEFAULT_BASE_B 0.8f

typedef struct Lesson51State
{
    ForgeGpuProcessedSceneRenderer renderer;
    ForgeGpuProcessedSceneModel shaderball;
    SDL_GPUGraphicsPipeline *pbr_pipeline;
    float metallic;
    float roughness;
    float base_color[3];
    Uint32 blinn_draw_calls;
    Uint32 pbr_draw_calls;
} Lesson51State;

static Lesson51State *lesson51_state(ForgeGpuDemo *demo)
{
    return (Lesson51State *)demo->lesson.private_state;
}

static void lesson51_release_shader(SDL_GPUDevice *device, SDL_GPUShader **shader)
{
    if (*shader) {
        SDL_ReleaseGPUShader(device, *shader);
        *shader = nullptr;
    }
}

static bool lesson51_create_pbr_pipeline(ForgeGpuDemo *demo, Lesson51State *state)
{
    SDL_GPUShader *model_vs = nullptr;
    SDL_GPUShader *pbr_fs = nullptr;
    bool ok = false;

    model_vs = ForgeGpuCreateShader(demo->device, SDL_GPU_SHADERSTAGE_VERTEX,
        forge_scene_model_vert_wgsl, forge_scene_model_vert_wgsl_size,
        forge_scene_model_vert_msl, forge_scene_model_vert_msl_size,
        0, 0, 0, 1);
    pbr_fs = ForgeGpuCreateShaderWithResourceLayout(demo->device,
        lesson51_pbr_frag_wgsl, lesson51_pbr_frag_wgsl_size,
        lesson51_pbr_frag_msl, lesson51_pbr_frag_msl_size,
        ForgeGpuShaderLayout_lesson51_pbr_frag());

    if (!model_vs || !pbr_fs) {
        goto done;
    }

    state->pbr_pipeline = ForgeGpuProcessedSceneCreateModelPipeline(demo, model_vs, pbr_fs);
    ok = state->pbr_pipeline != nullptr;

done:
    lesson51_release_shader(demo->device, &model_vs);
    lesson51_release_shader(demo->device, &pbr_fs);
    return ok;
}

static void lesson51_apply_material_controls(Lesson51State *state)
{
    for (Uint32 i = 0; i < state->shaderball.materials.material_count; i += 1) {
        ForgeGpuProcessedMaterial *material = &state->shaderball.materials.materials[i];

        material->base_color_factor[0] = state->base_color[0];
        material->base_color_factor[1] = state->base_color[1];
        material->base_color_factor[2] = state->base_color[2];
        material->base_color_factor[3] = 1.0f;
        material->metallic_factor = state->metallic;
        material->roughness_factor = state->roughness;
    }
}

static bool lesson51_validate_shaderball_fixture(const Lesson51State *state)
{
    const ForgeGpuProcessedMaterial *material;

    if (state->shaderball.scene.node_count != 8 ||
        state->shaderball.mesh.submesh_count != 2 ||
        state->shaderball.materials.material_count != 1) {
        SDL_SetError("lesson 51 Shaderball fixture shape changed");
        return false;
    }
    if ((state->shaderball.mesh.flags & FORGE_GPU_PROCESSED_MESH_FLAG_TANGENTS) == 0) {
        SDL_SetError("lesson 51 Shaderball fixture requires tangent-bearing mesh data");
        return false;
    }

    material = &state->shaderball.materials.materials[0];
    if (material->alpha_mode != FORGE_GPU_PROCESSED_ALPHA_OPAQUE || material->double_sided) {
        SDL_SetError("lesson 51 Shaderball fixture must stay opaque and single-sided for the comparison pipeline");
        return false;
    }
    return true;
}

bool ForgeGpuCreateLesson51(ForgeGpuDemo *demo)
{
    Lesson51State *state = (Lesson51State *)SDL_calloc(1, sizeof(*state));

    if (!state) {
        SDL_OutOfMemory();
        return false;
    }
    demo->lesson.private_state = state;
    state->metallic = LESSON51_DEFAULT_METALLIC;
    state->roughness = LESSON51_DEFAULT_ROUGHNESS;
    state->base_color[0] = LESSON51_DEFAULT_BASE_R;
    state->base_color[1] = LESSON51_DEFAULT_BASE_G;
    state->base_color[2] = LESSON51_DEFAULT_BASE_B;

    if (!ForgeGpuProcessedSceneRendererCreate(demo, &state->renderer) ||
        !ForgeGpuProcessedSceneLoadModel(
            demo,
            &state->renderer,
            &state->shaderball,
            "processed/51-pbr-shading/Shaderball",
            "Shaderball",
            ForgeGpuProcessedSceneLoadRgbaMaterialTexture,
            nullptr) ||
        !lesson51_validate_shaderball_fixture(state) ||
        !lesson51_create_pbr_pipeline(demo, state)) {
        return false;
    }

    demo->lesson.camera_position = {
        LESSON51_CAM_START_X,
        LESSON51_CAM_START_Y,
        LESSON51_CAM_START_Z
    };
    demo->lesson.camera_yaw = LESSON51_CAM_START_YAW;
    demo->lesson.camera_pitch = LESSON51_CAM_START_PITCH;
    demo->lesson.move_speed = LESSON51_MOVE_SPEED;
    demo->lesson.mouse_sensitivity = LESSON51_MOUSE_SENSITIVITY;
    demo->lesson.pitch_clamp = LESSON51_PITCH_CLAMP;
    demo->lesson.last_ticks = SDL_GetTicks();
    return true;
}

bool ForgeGpuRenderLesson51(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPUTexture *swapchain_texture,
    Uint32 width,
    Uint32 height)
{
    Lesson51State *state = lesson51_state(demo);
    Mat4 view;
    Mat4 projection;
    Mat4 camera_vp;
    Mat4 light_vp;
    Mat4 left_placement;
    Mat4 right_placement;
    SDL_GPURenderPass *render_pass;
    Uint32 before_draws;

    if (!state) {
        SDL_SetError("lesson 51 internal state is missing");
        return false;
    }
    if (!ForgeGpuProcessedSceneRendererEnsureMainDepth(demo, &state->renderer, width, height)) {
        return false;
    }

    ForgeGpuUpdateCameraFromInput(demo);
    ForgeGpuCameraViewProjection(demo, width, height, LESSON51_FAR_PLANE, &view, &projection);
    camera_vp = mat4_multiply(projection, view);
    light_vp = ForgeGpuProcessedSceneLightViewProjection();
    left_placement = mat4_translate({ LESSON51_MODEL_LEFT_X, LESSON51_MODEL_Y, 0.0f });
    right_placement = mat4_translate({ LESSON51_MODEL_RIGHT_X, LESSON51_MODEL_Y, 0.0f });

    lesson51_apply_material_controls(state);
    ForgeGpuProcessedSceneRendererBeginFrame(&state->renderer);
    ForgeGpuProcessedSceneResetModelDrawCounts(&state->shaderball);
    state->blinn_draw_calls = 0;
    state->pbr_draw_calls = 0;

    render_pass = ForgeGpuProcessedSceneBeginShadowPass(command_buffer, &state->renderer);
    if (!render_pass) {
        return false;
    }
    if (!ForgeGpuProcessedSceneDrawModel(
            demo, command_buffer, render_pass, &state->renderer, &state->shaderball,
            left_placement, camera_vp, light_vp, true) ||
        !ForgeGpuProcessedSceneDrawModel(
            demo, command_buffer, render_pass, &state->renderer, &state->shaderball,
            right_placement, camera_vp, light_vp, true)) {
        SDL_EndGPURenderPass(render_pass);
        return false;
    }
    SDL_EndGPURenderPass(render_pass);
    state->renderer.shadow_pass_rendered = true;

    render_pass = ForgeGpuProcessedSceneBeginMainPass(command_buffer, &state->renderer, swapchain_texture);
    if (!render_pass) {
        return false;
    }
    before_draws = state->shaderball.draw_calls;
    if (!ForgeGpuProcessedSceneDrawModel(
            demo, command_buffer, render_pass, &state->renderer, &state->shaderball,
            left_placement, camera_vp, light_vp, false)) {
        SDL_EndGPURenderPass(render_pass);
        return false;
    }
    state->blinn_draw_calls = state->shaderball.draw_calls - before_draws;

    before_draws = state->shaderball.draw_calls;
    if (!ForgeGpuProcessedSceneDrawModelWithPipeline(
            demo, command_buffer, render_pass, &state->renderer, &state->shaderball,
            state->pbr_pipeline, right_placement, camera_vp, light_vp)) {
        SDL_EndGPURenderPass(render_pass);
        return false;
    }
    state->pbr_draw_calls = state->shaderball.draw_calls - before_draws;

    ForgeGpuProcessedSceneDrawGrid(demo, command_buffer, render_pass, &state->renderer, camera_vp, light_vp);
    SDL_EndGPURenderPass(render_pass);
    state->renderer.main_pass_rendered = true;
    return true;
}

void ForgeGpuDebugLesson51(ForgeGpuDemo *demo)
{
    Lesson51State *state = lesson51_state(demo);

    if (!state) {
        return;
    }

    ImGui::Text("Left: Blinn-Phong");
    ImGui::Text("Right: Cook-Torrance PBR");
    ImGui::Text("Shaderball: %u nodes, %u materials",
        state->shaderball.scene.node_count,
        state->shaderball.materials.material_count);
    ImGui::Text("Draw calls: Blinn %u, PBR %u, shadow %u",
        state->blinn_draw_calls,
        state->pbr_draw_calls,
        state->shaderball.shadow_draw_calls);
    ImGui::Text("Passes: shadow %s, main %s",
        state->renderer.shadow_pass_rendered ? "yes" : "no",
        state->renderer.main_pass_rendered ? "yes" : "no");
}

void ForgeGpuControlsLesson51(ForgeGpuDemo *demo)
{
    Lesson51State *state = lesson51_state(demo);

    if (!state) {
        return;
    }

    ImGui::SliderFloat("Metallic", &state->metallic, 0.0f, 1.0f);
    ImGui::SliderFloat("Roughness", &state->roughness, 0.0f, 1.0f);
    ImGui::ColorEdit3("Base color", state->base_color);
}

void ForgeGpuExportLesson51Metrics(ForgeGpuDemo *demo)
{
    Lesson51State *state = lesson51_state(demo);

    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson51Ready", state ? 1.0 : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson51ShadowPass", state && state->renderer.shadow_pass_rendered ? 1.0 : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson51MainPass", state && state->renderer.main_pass_rendered ? 1.0 : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson51Nodes", state ? (double)state->shaderball.scene.node_count : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson51Materials", state ? (double)state->shaderball.materials.material_count : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson51Submeshes", state ? (double)state->shaderball.mesh.submesh_count : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson51BlinnDrawCalls", state ? (double)state->blinn_draw_calls : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson51PbrDrawCalls", state ? (double)state->pbr_draw_calls : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson51ShadowDrawCalls", state ? (double)state->shaderball.shadow_draw_calls : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson51Metallic", state ? (double)state->metallic : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson51Roughness", state ? (double)state->roughness : 0.0);
}

void ForgeGpuDestroyLesson51(ForgeGpuDemo *demo)
{
    Lesson51State *state = lesson51_state(demo);

    if (!state) {
        return;
    }

    if (state->pbr_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(demo->device, state->pbr_pipeline);
    }
    ForgeGpuProcessedSceneDestroyModel(demo->device, &state->shaderball);
    ForgeGpuProcessedSceneRendererDestroy(demo, &state->renderer);
    SDL_free(state);
    demo->lesson.private_state = nullptr;
}
