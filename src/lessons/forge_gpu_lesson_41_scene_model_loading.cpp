#include "forge_gpu_lessons.h"

#include "forge_gpu_browser_status.h"
#include "forge_gpu_camera.h"
#include "forge_gpu_gpu_helpers.h"
#include "forge_gpu_processed_scene_renderer.h"
#include "imgui.h"

#define LESSON41_FAR_PLANE 200.0f
#define LESSON41_MOVE_SPEED 5.0f
#define LESSON41_MOUSE_SENSITIVITY 0.003f
#define LESSON41_PITCH_CLAMP 1.5f
#define LESSON41_CAM_START_Y 2.5f
#define LESSON41_CAM_START_Z 8.0f
#define LESSON41_CAM_START_PITCH -0.2f
#define LESSON41_TRUCK_X -3.0f
#define LESSON41_SUZANNE_X 0.0f
#define LESSON41_DUCK_X 3.5f
#define LESSON41_MODEL_Y 0.0f
#define LESSON41_SUZANNE_SCALE 0.8f
#define LESSON41_SUZANNE_Y 1.0f
#define LESSON41_DUCK_SCALE 1.0f

typedef struct Lesson41State
{
    ForgeGpuProcessedSceneRenderer renderer;
    ForgeGpuProcessedSceneModel truck;
    ForgeGpuProcessedSceneModel suzanne;
    ForgeGpuProcessedSceneModel duck;
} Lesson41State;

static Lesson41State *lesson41_state(ForgeGpuDemo *demo)
{
    return (Lesson41State *)demo->lesson.private_state;
}

static void lesson41_reset_draw_counts(Lesson41State *state)
{
    ForgeGpuProcessedSceneResetModelDrawCounts(&state->truck);
    ForgeGpuProcessedSceneResetModelDrawCounts(&state->suzanne);
    ForgeGpuProcessedSceneResetModelDrawCounts(&state->duck);
}

bool ForgeGpuCreateLesson41(ForgeGpuDemo *demo)
{
    Lesson41State *state = (Lesson41State *)SDL_calloc(1, sizeof(*state));

    if (!state) {
        SDL_OutOfMemory();
        return false;
    }
    demo->lesson.private_state = state;

    if (!ForgeGpuProcessedSceneRendererCreate(demo, &state->renderer) ||
        !ForgeGpuProcessedSceneLoadModel(
            demo,
            &state->renderer,
            &state->truck,
            "processed/41-scene-model-loading/CesiumMilkTruck",
            "CesiumMilkTruck",
            ForgeGpuProcessedSceneLoadRgbaMaterialTexture,
            nullptr) ||
        !ForgeGpuProcessedSceneLoadModel(
            demo,
            &state->renderer,
            &state->suzanne,
            "processed/41-scene-model-loading/Suzanne",
            "Suzanne",
            ForgeGpuProcessedSceneLoadRgbaMaterialTexture,
            nullptr) ||
        !ForgeGpuProcessedSceneLoadModel(
            demo,
            &state->renderer,
            &state->duck,
            "processed/41-scene-model-loading/Duck",
            "Duck",
            ForgeGpuProcessedSceneLoadRgbaMaterialTexture,
            nullptr)) {
        return false;
    }

    demo->lesson.camera_position = { 0.0f, LESSON41_CAM_START_Y, LESSON41_CAM_START_Z };
    demo->lesson.camera_yaw = 0.0f;
    demo->lesson.camera_pitch = LESSON41_CAM_START_PITCH;
    demo->lesson.move_speed = LESSON41_MOVE_SPEED;
    demo->lesson.mouse_sensitivity = LESSON41_MOUSE_SENSITIVITY;
    demo->lesson.pitch_clamp = LESSON41_PITCH_CLAMP;
    demo->lesson.last_ticks = SDL_GetTicks();
    return true;
}

bool ForgeGpuRenderLesson41(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPUTexture *swapchain_texture,
    Uint32 width,
    Uint32 height)
{
    Lesson41State *state = lesson41_state(demo);
    Mat4 view;
    Mat4 projection;
    Mat4 camera_vp;
    Mat4 light_vp;
    Mat4 truck_placement;
    Mat4 suzanne_placement;
    Mat4 duck_placement;
    SDL_GPURenderPass *render_pass;

    if (!state) {
        SDL_SetError("lesson 41 internal state is missing");
        return false;
    }
    if (!ForgeGpuProcessedSceneRendererEnsureMainDepth(demo, &state->renderer, width, height)) {
        return false;
    }

    ForgeGpuUpdateCameraFromInput(demo);
    ForgeGpuCameraViewProjection(demo, width, height, LESSON41_FAR_PLANE, &view, &projection);
    camera_vp = mat4_multiply(projection, view);
    light_vp = ForgeGpuProcessedSceneLightViewProjection();
    truck_placement = mat4_translate({ LESSON41_TRUCK_X, LESSON41_MODEL_Y, 0.0f });
    suzanne_placement = mat4_multiply(
        mat4_translate({ LESSON41_SUZANNE_X, LESSON41_MODEL_Y + LESSON41_SUZANNE_Y, 0.0f }),
        mat4_scale(LESSON41_SUZANNE_SCALE));
    duck_placement = mat4_multiply(
        mat4_translate({ LESSON41_DUCK_X, LESSON41_MODEL_Y, 0.0f }),
        mat4_scale(LESSON41_DUCK_SCALE));

    ForgeGpuProcessedSceneRendererBeginFrame(&state->renderer);
    lesson41_reset_draw_counts(state);

    render_pass = ForgeGpuProcessedSceneBeginShadowPass(command_buffer, &state->renderer);
    if (!render_pass) {
        return false;
    }
    if (!ForgeGpuProcessedSceneDrawModel(demo, command_buffer, render_pass, &state->renderer, &state->truck, truck_placement, camera_vp, light_vp, true) ||
        !ForgeGpuProcessedSceneDrawModel(demo, command_buffer, render_pass, &state->renderer, &state->suzanne, suzanne_placement, camera_vp, light_vp, true) ||
        !ForgeGpuProcessedSceneDrawModel(demo, command_buffer, render_pass, &state->renderer, &state->duck, duck_placement, camera_vp, light_vp, true)) {
        SDL_EndGPURenderPass(render_pass);
        return false;
    }
    SDL_EndGPURenderPass(render_pass);
    state->renderer.shadow_pass_rendered = true;

    render_pass = ForgeGpuProcessedSceneBeginMainPass(command_buffer, &state->renderer, swapchain_texture);
    if (!render_pass) {
        return false;
    }
    if (!ForgeGpuProcessedSceneDrawModel(demo, command_buffer, render_pass, &state->renderer, &state->truck, truck_placement, camera_vp, light_vp, false) ||
        !ForgeGpuProcessedSceneDrawModel(demo, command_buffer, render_pass, &state->renderer, &state->suzanne, suzanne_placement, camera_vp, light_vp, false) ||
        !ForgeGpuProcessedSceneDrawModel(demo, command_buffer, render_pass, &state->renderer, &state->duck, duck_placement, camera_vp, light_vp, false)) {
        SDL_EndGPURenderPass(render_pass);
        return false;
    }
    ForgeGpuProcessedSceneDrawGrid(demo, command_buffer, render_pass, &state->renderer, camera_vp, light_vp);
    SDL_EndGPURenderPass(render_pass);
    state->renderer.main_pass_rendered = true;
    return true;
}

void ForgeGpuDebugLesson41(ForgeGpuDemo *demo)
{
    Lesson41State *state = lesson41_state(demo);
    Uint32 total_draws;

    if (!state) {
        return;
    }

    total_draws = state->truck.draw_calls + state->suzanne.draw_calls + state->duck.draw_calls;
    ImGui::Text("Truck: %u nodes, %u materials, %u draws",
        state->truck.scene.node_count,
        state->truck.materials.material_count,
        state->truck.draw_calls);
    ImGui::Text("Suzanne: %u nodes, %u materials, %u draws",
        state->suzanne.scene.node_count,
        state->suzanne.materials.material_count,
        state->suzanne.draw_calls);
    ImGui::Text("Duck: %u nodes, %u materials, %u draws",
        state->duck.scene.node_count,
        state->duck.materials.material_count,
        state->duck.draw_calls);
    ImGui::Text("Total draw calls: %u", total_draws);
    ImGui::Text("Passes: shadow %s, main %s",
        state->renderer.shadow_pass_rendered ? "yes" : "no",
        state->renderer.main_pass_rendered ? "yes" : "no");
}

void ForgeGpuExportLesson41Metrics(ForgeGpuDemo *demo)
{
    Lesson41State *state = lesson41_state(demo);

    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson41SceneModelLoading", state ? 1.0 : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson41ShadowPass", state && state->renderer.shadow_pass_rendered ? 1.0 : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson41MainPass", state && state->renderer.main_pass_rendered ? 1.0 : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson41TruckNodes", state ? (double)state->truck.scene.node_count : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson41TruckMaterials", state ? (double)state->truck.materials.material_count : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson41SuzanneNodes", state ? (double)state->suzanne.scene.node_count : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson41DuckNodes", state ? (double)state->duck.scene.node_count : 0.0);
    ForgeGpuBrowserSetNumberMetric(
        "sdlGpuForgeGpuLesson41DrawCalls",
        state ? (double)(state->truck.draw_calls + state->suzanne.draw_calls + state->duck.draw_calls) : 0.0);
    ForgeGpuBrowserSetNumberMetric(
        "sdlGpuForgeGpuLesson41TransparentDrawCalls",
        state ? (double)(state->truck.transparent_draw_calls + state->suzanne.transparent_draw_calls + state->duck.transparent_draw_calls) : 0.0);
}

void ForgeGpuDestroyLesson41(ForgeGpuDemo *demo)
{
    Lesson41State *state = lesson41_state(demo);

    if (!state) {
        return;
    }

    ForgeGpuProcessedSceneDestroyModel(demo->device, &state->duck);
    ForgeGpuProcessedSceneDestroyModel(demo->device, &state->suzanne);
    ForgeGpuProcessedSceneDestroyModel(demo->device, &state->truck);
    ForgeGpuProcessedSceneRendererDestroy(demo, &state->renderer);
    SDL_free(state);
    demo->lesson.private_state = nullptr;
}
