#include "forge_gpu_lessons.h"

#include "forge_gpu_browser_status.h"
#include "forge_gpu_camera.h"
#include "forge_gpu_processed_scene_renderer.h"
#include "imgui.h"

#define LESSON44_FAR_PLANE 200.0f
#define LESSON44_MOVE_SPEED 5.0f
#define LESSON44_MOUSE_SENSITIVITY 0.003f
#define LESSON44_PITCH_CLAMP 1.5f
#define LESSON44_CAM_START_X 3.0f
#define LESSON44_CAM_START_Y 2.5f
#define LESSON44_CAM_START_Z 10.0f
#define LESSON44_CAM_START_YAW 0.0f
#define LESSON44_CAM_START_PITCH (-0.1f)
#define LESSON44_MORPH_CUBE_X 1.0f
#define LESSON44_SIMPLE_MORPH_X 5.0f
#define LESSON44_MODEL_Z 0.0f
#define LESSON44_MORPH_CUBE_SCALE 1.0f
#define LESSON44_MORPH_CUBE_Y_OFFSET 1.5f
#define LESSON44_SIMPLE_MORPH_SCALE 2.5f
#define LESSON44_SIMPLE_MORPH_Y_OFFSET 1.5f
#define LESSON44_DEFAULT_ANIM_SPEED 1.0f
#define LESSON44_SPEED_MIN 0.0f
#define LESSON44_SPEED_MAX 3.0f
#define LESSON44_WEIGHT_MIN 0.0f
#define LESSON44_WEIGHT_MAX 1.0f

typedef struct Lesson44State
{
    ForgeGpuProcessedSceneRenderer renderer;
    ForgeGpuProcessedSceneMorphModel morph_cube;
    ForgeGpuProcessedSceneMorphModel simple_morph;
    Uint64 last_animation_counter;
    float cube_speed;
    float simple_speed;
    float cube_w0;
    float cube_w1;
    float simple_w0;
    float simple_w1;
} Lesson44State;

static Lesson44State *lesson44_state(ForgeGpuDemo *demo)
{
    return (Lesson44State *)demo->lesson.private_state;
}

static float lesson44_delta_seconds(ForgeGpuDemo *demo, Lesson44State *state)
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

static void lesson44_reset_draw_counts(Lesson44State *state)
{
    ForgeGpuProcessedSceneResetMorphModelDrawCounts(&state->morph_cube);
    ForgeGpuProcessedSceneResetMorphModelDrawCounts(&state->simple_morph);
}

static bool lesson44_upload_morph_deltas(SDL_GPUCommandBuffer *command_buffer, Lesson44State *state)
{
    SDL_GPUCopyPass *copy_pass;

    if (state->morph_cube.pending_morph_delta_upload_size == 0 &&
        state->simple_morph.pending_morph_delta_upload_size == 0) {
        return true;
    }

    copy_pass = SDL_BeginGPUCopyPass(command_buffer);
    if (!copy_pass) {
        return false;
    }
    if (!ForgeGpuProcessedSceneUploadMorphDeltas(copy_pass, &state->morph_cube) ||
        !ForgeGpuProcessedSceneUploadMorphDeltas(copy_pass, &state->simple_morph)) {
        SDL_EndGPUCopyPass(copy_pass);
        return false;
    }
    SDL_EndGPUCopyPass(copy_pass);
    return true;
}

static Uint32 lesson44_total_draw_calls(const Lesson44State *state)
{
    return state->morph_cube.model.draw_calls + state->simple_morph.model.draw_calls;
}

static void lesson44_draw_weight_sliders(const char *label, ForgeGpuProcessedSceneMorphModel *model, float *w0, float *w1)
{
    ImGui::Checkbox(label, &model->manual_weights);
    if (!model->manual_weights) {
        ImGui::Text("W0: %.3f  W1: %.3f",
            model->morph_target_count > 0 ? model->morph_weights[0] : 0.0f,
            model->morph_target_count > 1 ? model->morph_weights[1] : 0.0f);
        return;
    }

    if (model->morph_target_count > 0) {
        char slider_label[64];

        SDL_snprintf(slider_label, sizeof(slider_label), "Target 0##%s0", label);
        ImGui::SliderFloat(slider_label, w0, LESSON44_WEIGHT_MIN, LESSON44_WEIGHT_MAX, "%.2f");
    }
    if (model->morph_target_count > 1) {
        char slider_label[64];

        SDL_snprintf(slider_label, sizeof(slider_label), "Target 1##%s1", label);
        ImGui::SliderFloat(slider_label, w1, LESSON44_WEIGHT_MIN, LESSON44_WEIGHT_MAX, "%.2f");
    }
}

bool ForgeGpuCreateLesson44(ForgeGpuDemo *demo)
{
    Lesson44State *state = (Lesson44State *)SDL_calloc(1, sizeof(*state));

    if (!state) {
        SDL_OutOfMemory();
        return false;
    }
    demo->lesson.private_state = state;
    state->cube_speed = LESSON44_DEFAULT_ANIM_SPEED;
    state->simple_speed = LESSON44_DEFAULT_ANIM_SPEED;

    if (!ForgeGpuProcessedSceneRendererCreate(demo, &state->renderer) ||
        !ForgeGpuProcessedSceneLoadMorphModel(
            demo,
            &state->renderer,
            &state->morph_cube,
            "processed/44-pipeline-morph-animations/AnimatedMorphCube",
            "AnimatedMorphCube",
            true,
            ForgeGpuProcessedSceneLoadRgbaMaterialTexture,
            nullptr) ||
        !ForgeGpuProcessedSceneLoadMorphModel(
            demo,
            &state->renderer,
            &state->simple_morph,
            "processed/44-pipeline-morph-animations/SimpleMorph",
            "SimpleMorph",
            false,
            ForgeGpuProcessedSceneLoadRgbaMaterialTexture,
            nullptr)) {
        return false;
    }

    demo->lesson.camera_position = { LESSON44_CAM_START_X, LESSON44_CAM_START_Y, LESSON44_CAM_START_Z };
    demo->lesson.camera_yaw = LESSON44_CAM_START_YAW;
    demo->lesson.camera_pitch = LESSON44_CAM_START_PITCH;
    demo->lesson.move_speed = LESSON44_MOVE_SPEED;
    demo->lesson.mouse_sensitivity = LESSON44_MOUSE_SENSITIVITY;
    demo->lesson.pitch_clamp = LESSON44_PITCH_CLAMP;
    demo->lesson.last_ticks = SDL_GetTicks();
    return true;
}

bool ForgeGpuRenderLesson44(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPUTexture *swapchain_texture,
    Uint32 width,
    Uint32 height)
{
    Lesson44State *state = lesson44_state(demo);
    Mat4 view;
    Mat4 projection;
    Mat4 camera_vp;
    Mat4 light_vp;
    Mat4 cube_placement;
    Mat4 simple_placement;
    SDL_GPURenderPass *render_pass;
    float dt;

    if (!state) {
        SDL_SetError("lesson 44 internal state is missing");
        return false;
    }
    if (!ForgeGpuProcessedSceneRendererEnsureMainDepth(demo, &state->renderer, width, height)) {
        return false;
    }

    ForgeGpuUpdateCameraFromInput(demo);
    dt = lesson44_delta_seconds(demo, state);
    state->morph_cube.anim_speed = state->cube_speed;
    state->simple_morph.anim_speed = state->simple_speed;
    if (state->morph_cube.manual_weights) {
        if (state->morph_cube.morph_target_count > 0) {
            state->morph_cube.morph_weights[0] = state->cube_w0;
        }
        if (state->morph_cube.morph_target_count > 1) {
            state->morph_cube.morph_weights[1] = state->cube_w1;
        }
    }
    if (state->simple_morph.manual_weights) {
        if (state->simple_morph.morph_target_count > 0) {
            state->simple_morph.morph_weights[0] = state->simple_w0;
        }
        if (state->simple_morph.morph_target_count > 1) {
            state->simple_morph.morph_weights[1] = state->simple_w1;
        }
    }
    if (!ForgeGpuProcessedSceneUpdateMorphAnimation(demo, &state->morph_cube, dt) ||
        !ForgeGpuProcessedSceneUpdateMorphAnimation(demo, &state->simple_morph, dt) ||
        !lesson44_upload_morph_deltas(command_buffer, state)) {
        return false;
    }

    ForgeGpuCameraViewProjection(demo, width, height, LESSON44_FAR_PLANE, &view, &projection);
    camera_vp = mat4_multiply(projection, view);
    light_vp = ForgeGpuProcessedSceneLightViewProjection();
    cube_placement = mat4_multiply(
        mat4_translate({ LESSON44_MORPH_CUBE_X, LESSON44_MORPH_CUBE_Y_OFFSET, LESSON44_MODEL_Z }),
        mat4_scale(LESSON44_MORPH_CUBE_SCALE));
    simple_placement = mat4_multiply(
        mat4_translate({ LESSON44_SIMPLE_MORPH_X, LESSON44_SIMPLE_MORPH_Y_OFFSET, LESSON44_MODEL_Z }),
        mat4_scale(LESSON44_SIMPLE_MORPH_SCALE));

    ForgeGpuProcessedSceneRendererBeginFrame(&state->renderer);
    lesson44_reset_draw_counts(state);

    render_pass = ForgeGpuProcessedSceneBeginShadowPass(command_buffer, &state->renderer);
    if (!render_pass) {
        return false;
    }
    if (!ForgeGpuProcessedSceneDrawMorphModel(demo, command_buffer, render_pass, &state->renderer, &state->morph_cube, cube_placement, camera_vp, light_vp, true) ||
        !ForgeGpuProcessedSceneDrawMorphModel(demo, command_buffer, render_pass, &state->renderer, &state->simple_morph, simple_placement, camera_vp, light_vp, true)) {
        SDL_EndGPURenderPass(render_pass);
        return false;
    }
    SDL_EndGPURenderPass(render_pass);
    state->renderer.shadow_pass_rendered = true;

    render_pass = ForgeGpuProcessedSceneBeginMainPass(command_buffer, &state->renderer, swapchain_texture);
    if (!render_pass) {
        return false;
    }
    ForgeGpuProcessedSceneDrawGrid(demo, command_buffer, render_pass, &state->renderer, camera_vp, light_vp);
    if (!ForgeGpuProcessedSceneDrawMorphModel(demo, command_buffer, render_pass, &state->renderer, &state->morph_cube, cube_placement, camera_vp, light_vp, false) ||
        !ForgeGpuProcessedSceneDrawMorphModel(demo, command_buffer, render_pass, &state->renderer, &state->simple_morph, simple_placement, camera_vp, light_vp, false)) {
        SDL_EndGPURenderPass(render_pass);
        return false;
    }
    SDL_EndGPURenderPass(render_pass);
    state->renderer.main_pass_rendered = true;
    return true;
}

void ForgeGpuDebugLesson44(ForgeGpuDemo *demo)
{
    Lesson44State *state = lesson44_state(demo);

    if (!state) {
        return;
    }

    ImGui::Text("AnimatedMorphCube: %u targets, %.2fs, %u draws",
        state->morph_cube.morph_target_count,
        state->morph_cube.anim_time,
        state->morph_cube.model.draw_calls);
    ImGui::Text("SimpleMorph: %u targets, %.2fs, %u draws",
        state->simple_morph.morph_target_count,
        state->simple_morph.anim_time,
        state->simple_morph.model.draw_calls);
    ImGui::Text("Weights: cube %.3f %.3f, simple %.3f %.3f",
        state->morph_cube.morph_target_count > 0 ? state->morph_cube.morph_weights[0] : 0.0f,
        state->morph_cube.morph_target_count > 1 ? state->morph_cube.morph_weights[1] : 0.0f,
        state->simple_morph.morph_target_count > 0 ? state->simple_morph.morph_weights[0] : 0.0f,
        state->simple_morph.morph_target_count > 1 ? state->simple_morph.morph_weights[1] : 0.0f);
    ImGui::Text("Total draw calls: %u", lesson44_total_draw_calls(state));
    ImGui::Text("Passes: shadow %s, main %s",
        state->renderer.shadow_pass_rendered ? "yes" : "no",
        state->renderer.main_pass_rendered ? "yes" : "no");
}

void ForgeGpuControlsLesson44(ForgeGpuDemo *demo)
{
    Lesson44State *state = lesson44_state(demo);

    if (!state) {
        return;
    }

    ImGui::Text("AnimatedMorphCube");
    ImGui::SliderFloat("Cube speed", &state->cube_speed, LESSON44_SPEED_MIN, LESSON44_SPEED_MAX, "%.1fx");
    lesson44_draw_weight_sliders("Manual weights (Cube)", &state->morph_cube, &state->cube_w0, &state->cube_w1);

    ImGui::Separator();

    ImGui::Text("SimpleMorph");
    ImGui::SliderFloat("Simple speed", &state->simple_speed, LESSON44_SPEED_MIN, LESSON44_SPEED_MAX, "%.1fx");
    lesson44_draw_weight_sliders("Manual weights (Simple)", &state->simple_morph, &state->simple_w0, &state->simple_w1);
}

void ForgeGpuExportLesson44Metrics(ForgeGpuDemo *demo)
{
    Lesson44State *state = lesson44_state(demo);

    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson44MorphAnimations", state ? 1.0 : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson44ShadowPass", state && state->renderer.shadow_pass_rendered ? 1.0 : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson44MainPass", state && state->renderer.main_pass_rendered ? 1.0 : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson44CubeTargets", state ? (double)state->morph_cube.morph_target_count : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson44SimpleTargets", state ? (double)state->simple_morph.morph_target_count : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson44CubeDrawCalls", state ? (double)state->morph_cube.model.draw_calls : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson44SimpleDrawCalls", state ? (double)state->simple_morph.model.draw_calls : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson44DrawCalls", state ? (double)lesson44_total_draw_calls(state) : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson44CubeManualWeights", state && state->morph_cube.manual_weights ? 1.0 : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson44SimpleManualWeights", state && state->simple_morph.manual_weights ? 1.0 : 0.0);
    ForgeGpuBrowserSetNumberMetric(
        "sdlGpuForgeGpuLesson44CubeWeight0",
        state && state->morph_cube.morph_target_count > 0 ? (double)state->morph_cube.morph_weights[0] : 0.0);
    ForgeGpuBrowserSetNumberMetric(
        "sdlGpuForgeGpuLesson44CubeWeight1",
        state && state->morph_cube.morph_target_count > 1 ? (double)state->morph_cube.morph_weights[1] : 0.0);
    ForgeGpuBrowserSetNumberMetric(
        "sdlGpuForgeGpuLesson44SimpleWeight0",
        state && state->simple_morph.morph_target_count > 0 ? (double)state->simple_morph.morph_weights[0] : 0.0);
    ForgeGpuBrowserSetNumberMetric(
        "sdlGpuForgeGpuLesson44SimpleWeight1",
        state && state->simple_morph.morph_target_count > 1 ? (double)state->simple_morph.morph_weights[1] : 0.0);
}

void ForgeGpuDestroyLesson44(ForgeGpuDemo *demo)
{
    Lesson44State *state = lesson44_state(demo);

    if (!state) {
        return;
    }

    ForgeGpuProcessedSceneDestroyMorphModel(demo->device, &state->simple_morph);
    ForgeGpuProcessedSceneDestroyMorphModel(demo->device, &state->morph_cube);
    ForgeGpuProcessedSceneRendererDestroy(demo, &state->renderer);
    SDL_free(state);
    demo->lesson.private_state = nullptr;
}
