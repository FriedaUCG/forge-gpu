#include "forge_gpu_lessons.h"

#include "forge_gpu_browser_status.h"
#include "forge_gpu_camera.h"
#include "forge_gpu_processed_scene_renderer.h"
#include "imgui.h"

#define LESSON43_FAR_PLANE 200.0f
#define LESSON43_MOVE_SPEED 5.0f
#define LESSON43_MOUSE_SENSITIVITY 0.003f
#define LESSON43_PITCH_CLAMP 1.5f
#define LESSON43_CAM_START_X 0.0f
#define LESSON43_CAM_START_Y 3.0f
#define LESSON43_CAM_START_Z 8.0f
#define LESSON43_CAM_START_YAW 0.0f
#define LESSON43_CAM_START_PITCH (-0.2f)
#define LESSON43_CESIUMMAN_X (-3.0f)
#define LESSON43_BRAINSTEM_X 0.0f
#define LESSON43_ANIMATED_CUBE_X 3.5f
#define LESSON43_MODEL_Y 0.0f
#define LESSON43_MODEL_Z 0.0f
#define LESSON43_CESIUMMAN_SCALE 2.0f
#define LESSON43_BRAINSTEM_SCALE 3.0f
#define LESSON43_CUBE_SCALE 1.0f
#define LESSON43_CUBE_Y_OFFSET 1.5f
#define LESSON43_DEFAULT_ANIM_SPEED 1.0f
#define LESSON43_SPEED_MIN 0.0f
#define LESSON43_SPEED_MAX 3.0f

typedef struct Lesson43State
{
    ForgeGpuProcessedSceneRenderer renderer;
    ForgeGpuProcessedSceneSkinnedModel cesium_man;
    ForgeGpuProcessedSceneSkinnedModel brain_stem;
    ForgeGpuProcessedSceneModel animated_cube;
    ForgeGpuProcessedAnimation cube_animation;
    Uint64 last_animation_counter;
    float cube_anim_time;
    float cesium_speed;
    float brain_speed;
    float cube_speed;
    bool cube_animation_active;
} Lesson43State;

static Lesson43State *lesson43_state(ForgeGpuDemo *demo)
{
    return (Lesson43State *)demo->lesson.private_state;
}

static float lesson43_delta_seconds(ForgeGpuDemo *demo, Lesson43State *state)
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

static void lesson43_reset_draw_counts(Lesson43State *state)
{
    ForgeGpuProcessedSceneResetSkinnedModelDrawCounts(&state->cesium_man);
    ForgeGpuProcessedSceneResetSkinnedModelDrawCounts(&state->brain_stem);
    ForgeGpuProcessedSceneResetModelDrawCounts(&state->animated_cube);
}

static bool lesson43_update_cube_animation(Lesson43State *state, float dt)
{
    ForgeGpuProcessedAnimationClip *clip;

    state->cube_animation_active = false;
    if (state->cube_animation.clip_count == 0) {
        return true;
    }

    clip = &state->cube_animation.clips[0];
    state->cube_anim_time += dt * state->cube_speed;
    if (clip->duration > 0.0f) {
        state->cube_anim_time = SDL_fmodf(state->cube_anim_time, clip->duration);
        if (state->cube_anim_time < 0.0f) {
            state->cube_anim_time += clip->duration;
        }
    }

    if (!ForgeGpuApplyProcessedSceneAnimation(&state->animated_cube.scene, clip, state->cube_anim_time, true) ||
        !ForgeGpuRecomputeProcessedSceneWorldTransforms(&state->animated_cube.scene)) {
        return false;
    }
    state->cube_animation_active = true;
    return true;
}

static bool lesson43_upload_skinned_joints(SDL_GPUCommandBuffer *command_buffer, Lesson43State *state)
{
    SDL_GPUCopyPass *copy_pass;

    if (state->cesium_man.pending_joint_upload_size == 0 &&
        state->brain_stem.pending_joint_upload_size == 0) {
        return true;
    }

    copy_pass = SDL_BeginGPUCopyPass(command_buffer);
    if (!copy_pass) {
        return false;
    }
    if (!ForgeGpuProcessedSceneUploadSkinnedJoints(copy_pass, &state->cesium_man) ||
        !ForgeGpuProcessedSceneUploadSkinnedJoints(copy_pass, &state->brain_stem)) {
        SDL_EndGPUCopyPass(copy_pass);
        return false;
    }
    SDL_EndGPUCopyPass(copy_pass);
    return true;
}

bool ForgeGpuCreateLesson43(ForgeGpuDemo *demo)
{
    Lesson43State *state = (Lesson43State *)SDL_calloc(1, sizeof(*state));
    char path[FORGE_GPU_MAX_PATH];

    if (!state) {
        SDL_OutOfMemory();
        return false;
    }
    demo->lesson.private_state = state;
    state->cesium_speed = LESSON43_DEFAULT_ANIM_SPEED;
    state->brain_speed = LESSON43_DEFAULT_ANIM_SPEED;
    state->cube_speed = LESSON43_DEFAULT_ANIM_SPEED;

    if (!ForgeGpuProcessedSceneRendererCreate(demo, &state->renderer) ||
        !ForgeGpuProcessedSceneLoadSkinnedModel(
            demo,
            &state->renderer,
            &state->cesium_man,
            "processed/43-pipeline-skinned-animations/CesiumMan",
            "CesiumMan",
            ForgeGpuProcessedSceneLoadRgbaMaterialTexture,
            nullptr) ||
        !ForgeGpuProcessedSceneLoadSkinnedModel(
            demo,
            &state->renderer,
            &state->brain_stem,
            "processed/43-pipeline-skinned-animations/BrainStem",
            "BrainStem",
            ForgeGpuProcessedSceneLoadRgbaMaterialTexture,
            nullptr) ||
        !ForgeGpuProcessedSceneLoadModel(
            demo,
            &state->renderer,
            &state->animated_cube,
            "processed/43-pipeline-skinned-animations/AnimatedCube",
            "AnimatedCube",
            ForgeGpuProcessedSceneLoadRgbaMaterialTexture,
            nullptr)) {
        return false;
    }

    if (!ForgeGpuProcessedSceneJoinModelPath(
            demo,
            "processed/43-pipeline-skinned-animations/AnimatedCube",
            "AnimatedCube.fanim",
            path,
            sizeof(path)) ||
        !ForgeGpuLoadProcessedAnimation(path, &state->cube_animation) ||
        !ForgeGpuValidateProcessedSceneAnimationReferences(&state->animated_cube.scene, &state->cube_animation, "AnimatedCube")) {
        return false;
    }

    demo->lesson.camera_position = { LESSON43_CAM_START_X, LESSON43_CAM_START_Y, LESSON43_CAM_START_Z };
    demo->lesson.camera_yaw = LESSON43_CAM_START_YAW;
    demo->lesson.camera_pitch = LESSON43_CAM_START_PITCH;
    demo->lesson.move_speed = LESSON43_MOVE_SPEED;
    demo->lesson.mouse_sensitivity = LESSON43_MOUSE_SENSITIVITY;
    demo->lesson.pitch_clamp = LESSON43_PITCH_CLAMP;
    demo->lesson.last_ticks = SDL_GetTicks();
    return true;
}

bool ForgeGpuRenderLesson43(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPUTexture *swapchain_texture,
    Uint32 width,
    Uint32 height)
{
    Lesson43State *state = lesson43_state(demo);
    Mat4 view;
    Mat4 projection;
    Mat4 camera_vp;
    Mat4 light_vp;
    Mat4 cesium_placement;
    Mat4 brain_placement;
    Mat4 cube_placement;
    SDL_GPURenderPass *render_pass;
    float dt;

    if (!state) {
        SDL_SetError("lesson 43 internal state is missing");
        return false;
    }
    if (!ForgeGpuProcessedSceneRendererEnsureMainDepth(demo, &state->renderer, width, height)) {
        return false;
    }

    ForgeGpuUpdateCameraFromInput(demo);
    dt = lesson43_delta_seconds(demo, state);
    state->cesium_man.anim_speed = state->cesium_speed;
    state->brain_stem.anim_speed = state->brain_speed;
    if (!ForgeGpuProcessedSceneUpdateSkinnedAnimation(demo, &state->cesium_man, dt) ||
        !ForgeGpuProcessedSceneUpdateSkinnedAnimation(demo, &state->brain_stem, dt) ||
        !lesson43_update_cube_animation(state, dt) ||
        !lesson43_upload_skinned_joints(command_buffer, state)) {
        return false;
    }

    ForgeGpuCameraViewProjection(demo, width, height, LESSON43_FAR_PLANE, &view, &projection);
    camera_vp = mat4_multiply(projection, view);
    light_vp = ForgeGpuProcessedSceneLightViewProjection();
    cesium_placement = mat4_multiply(
        mat4_translate({ LESSON43_CESIUMMAN_X, LESSON43_MODEL_Y, LESSON43_MODEL_Z }),
        mat4_scale(LESSON43_CESIUMMAN_SCALE));
    brain_placement = mat4_multiply(
        mat4_translate({ LESSON43_BRAINSTEM_X, LESSON43_MODEL_Y, LESSON43_MODEL_Z }),
        mat4_scale(LESSON43_BRAINSTEM_SCALE));
    cube_placement = mat4_multiply(
        mat4_translate({ LESSON43_ANIMATED_CUBE_X, LESSON43_CUBE_Y_OFFSET, LESSON43_MODEL_Z }),
        mat4_scale(LESSON43_CUBE_SCALE));

    ForgeGpuProcessedSceneRendererBeginFrame(&state->renderer);
    lesson43_reset_draw_counts(state);

    render_pass = ForgeGpuProcessedSceneBeginShadowPass(command_buffer, &state->renderer);
    if (!render_pass) {
        return false;
    }
    if (!ForgeGpuProcessedSceneDrawSkinnedModel(demo, command_buffer, render_pass, &state->renderer, &state->cesium_man, cesium_placement, camera_vp, light_vp, true) ||
        !ForgeGpuProcessedSceneDrawSkinnedModel(demo, command_buffer, render_pass, &state->renderer, &state->brain_stem, brain_placement, camera_vp, light_vp, true) ||
        !ForgeGpuProcessedSceneDrawModel(demo, command_buffer, render_pass, &state->renderer, &state->animated_cube, cube_placement, camera_vp, light_vp, true)) {
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
    if (!ForgeGpuProcessedSceneDrawSkinnedModel(demo, command_buffer, render_pass, &state->renderer, &state->cesium_man, cesium_placement, camera_vp, light_vp, false) ||
        !ForgeGpuProcessedSceneDrawSkinnedModel(demo, command_buffer, render_pass, &state->renderer, &state->brain_stem, brain_placement, camera_vp, light_vp, false) ||
        !ForgeGpuProcessedSceneDrawModel(demo, command_buffer, render_pass, &state->renderer, &state->animated_cube, cube_placement, camera_vp, light_vp, false)) {
        SDL_EndGPURenderPass(render_pass);
        return false;
    }
    SDL_EndGPURenderPass(render_pass);
    state->renderer.main_pass_rendered = true;
    return true;
}

void ForgeGpuDebugLesson43(ForgeGpuDemo *demo)
{
    Lesson43State *state = lesson43_state(demo);
    Uint32 total_draws;

    if (!state) {
        return;
    }

    total_draws = state->cesium_man.model.draw_calls +
        state->brain_stem.model.draw_calls +
        state->animated_cube.draw_calls;

    ImGui::Text("CesiumMan: %u joints, %.2fs, %u draws",
        state->cesium_man.active_joint_count,
        state->cesium_man.anim_time,
        state->cesium_man.model.draw_calls);
    ImGui::Text("BrainStem: %u joints, %.2fs, %u draws",
        state->brain_stem.active_joint_count,
        state->brain_stem.anim_time,
        state->brain_stem.model.draw_calls);
    ImGui::Text("AnimatedCube: %.2fs, animation %s",
        state->cube_anim_time,
        state->cube_animation_active ? "active" : "pending");
    ImGui::Text("Total draw calls: %u", total_draws);
    ImGui::Text("Passes: shadow %s, main %s",
        state->renderer.shadow_pass_rendered ? "yes" : "no",
        state->renderer.main_pass_rendered ? "yes" : "no");
}

void ForgeGpuControlsLesson43(ForgeGpuDemo *demo)
{
    Lesson43State *state = lesson43_state(demo);

    if (!state) {
        return;
    }

    ImGui::SliderFloat("CesiumMan speed", &state->cesium_speed, LESSON43_SPEED_MIN, LESSON43_SPEED_MAX, "%.1fx");
    ImGui::SliderFloat("BrainStem speed", &state->brain_speed, LESSON43_SPEED_MIN, LESSON43_SPEED_MAX, "%.1fx");
    ImGui::SliderFloat("AnimatedCube speed", &state->cube_speed, LESSON43_SPEED_MIN, LESSON43_SPEED_MAX, "%.1fx");
}

void ForgeGpuExportLesson43Metrics(ForgeGpuDemo *demo)
{
    Lesson43State *state = lesson43_state(demo);

    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson43SkinnedAnimations", state ? 1.0 : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson43ShadowPass", state && state->renderer.shadow_pass_rendered ? 1.0 : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson43MainPass", state && state->renderer.main_pass_rendered ? 1.0 : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson43CesiumJoints", state ? (double)state->cesium_man.active_joint_count : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson43BrainStemJoints", state ? (double)state->brain_stem.active_joint_count : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson43CesiumDrawCalls", state ? (double)state->cesium_man.model.draw_calls : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson43BrainStemDrawCalls", state ? (double)state->brain_stem.model.draw_calls : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson43CubeDrawCalls", state ? (double)state->animated_cube.draw_calls : 0.0);
    ForgeGpuBrowserSetNumberMetric(
        "sdlGpuForgeGpuLesson43DrawCalls",
        state ? (double)(state->cesium_man.model.draw_calls + state->brain_stem.model.draw_calls + state->animated_cube.draw_calls) : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson43CubeAnimation", state && state->cube_animation_active ? 1.0 : 0.0);
}

void ForgeGpuDestroyLesson43(ForgeGpuDemo *demo)
{
    Lesson43State *state = lesson43_state(demo);

    if (!state) {
        return;
    }

    ForgeGpuFreeProcessedAnimation(&state->cube_animation);
    ForgeGpuProcessedSceneDestroyModel(demo->device, &state->animated_cube);
    ForgeGpuProcessedSceneDestroySkinnedModel(demo->device, &state->brain_stem);
    ForgeGpuProcessedSceneDestroySkinnedModel(demo->device, &state->cesium_man);
    ForgeGpuProcessedSceneRendererDestroy(demo, &state->renderer);
    SDL_free(state);
    demo->lesson.private_state = nullptr;
}
