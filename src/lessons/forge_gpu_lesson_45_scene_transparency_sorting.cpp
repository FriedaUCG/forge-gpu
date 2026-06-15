#include "forge_gpu_lessons.h"

#include "forge_gpu_browser_status.h"
#include "forge_gpu_camera.h"
#include "forge_gpu_processed_scene_renderer.h"
#include "imgui.h"

#define LESSON45_FAR_PLANE 200.0f
#define LESSON45_MOVE_SPEED 5.0f
#define LESSON45_MOUSE_SENSITIVITY 0.003f
#define LESSON45_PITCH_CLAMP 1.5f
#define LESSON45_CAM_START_X 0.0f
#define LESSON45_CAM_START_Y 4.0f
#define LESSON45_CAM_START_Z 12.0f
#define LESSON45_CAM_START_YAW 0.0f
#define LESSON45_CAM_START_PITCH (-0.2f)
#define LESSON45_TRUCK_A_X (-1.5f)
#define LESSON45_TRUCK_B_X 1.5f
#define LESSON45_MODEL_Y 0.0f
#define LESSON45_GLASS_ALPHA 0.3f
#define LESSON45_ALPHA_EPSILON 0.0001f

typedef struct Lesson45State
{
    ForgeGpuProcessedSceneRenderer renderer;
    ForgeGpuProcessedSceneModel truck;
    float original_glass_alpha;
    float final_glass_alpha;
    Uint32 glass_material_count;
    Uint32 blend_material_count;
    Uint32 mask_material_count;
    Uint32 double_sided_material_count;
    bool transparency_sorting;
    bool glass_override_applied;
} Lesson45State;

static Lesson45State *lesson45_state(ForgeGpuDemo *demo)
{
    return (Lesson45State *)demo->lesson.private_state;
}

static bool lesson45_alpha_near(float a, float b)
{
    return SDL_fabsf(a - b) <= LESSON45_ALPHA_EPSILON;
}

static bool lesson45_apply_material_overrides(Lesson45State *state)
{
    if (state->truck.materials.material_count != 4) {
        SDL_SetError(
            "lesson 45 expected four CesiumMilkTruck materials, got %u",
            (unsigned)state->truck.materials.material_count);
        return false;
    }

    state->glass_material_count = 0;
    state->blend_material_count = 0;
    state->mask_material_count = 0;
    state->double_sided_material_count = 0;
    state->glass_override_applied = false;

    for (Uint32 i = 0; i < state->truck.materials.material_count; i += 1) {
        ForgeGpuProcessedMaterial *material = &state->truck.materials.materials[i];

        if (SDL_strcmp(material->name, "glass") == 0) {
            const bool already_matches_lesson =
                material->alpha_mode == FORGE_GPU_PROCESSED_ALPHA_BLEND &&
                lesson45_alpha_near(material->base_color_factor[3], LESSON45_GLASS_ALPHA);

            state->glass_material_count += 1;
            state->original_glass_alpha = material->base_color_factor[3];
            if (!already_matches_lesson) {
                if (material->alpha_mode != FORGE_GPU_PROCESSED_ALPHA_OPAQUE ||
                    !lesson45_alpha_near(material->base_color_factor[3], 1.0f)) {
                    SDL_SetError("lesson 45 glass material did not match the expected opaque source fixture");
                    return false;
                }
                material->alpha_mode = FORGE_GPU_PROCESSED_ALPHA_BLEND;
                material->base_color_factor[3] = LESSON45_GLASS_ALPHA;
            }
            state->final_glass_alpha = material->base_color_factor[3];
            state->glass_override_applied =
                material->alpha_mode == FORGE_GPU_PROCESSED_ALPHA_BLEND &&
                lesson45_alpha_near(material->base_color_factor[3], LESSON45_GLASS_ALPHA);
        }

        material->double_sided = true;
        if (material->double_sided) {
            state->double_sided_material_count += 1;
        }
        if (material->alpha_mode == FORGE_GPU_PROCESSED_ALPHA_BLEND) {
            state->blend_material_count += 1;
        } else if (material->alpha_mode == FORGE_GPU_PROCESSED_ALPHA_MASK) {
            state->mask_material_count += 1;
        }
    }

    if (state->glass_material_count != 1 || !state->glass_override_applied ||
        state->blend_material_count != 1 || state->mask_material_count != 0) {
        SDL_SetError("lesson 45 CesiumMilkTruck glass BLEND facts were not established");
        return false;
    }

    return true;
}

static void lesson45_reset_draw_counts(Lesson45State *state)
{
    ForgeGpuProcessedSceneResetModelDrawCounts(&state->truck);
}

bool ForgeGpuCreateLesson45(ForgeGpuDemo *demo)
{
    Lesson45State *state = (Lesson45State *)SDL_calloc(1, sizeof(*state));

    if (!state) {
        SDL_OutOfMemory();
        return false;
    }
    demo->lesson.private_state = state;
    state->transparency_sorting = true;

    if (!ForgeGpuProcessedSceneRendererCreate(demo, &state->renderer) ||
        !ForgeGpuProcessedSceneLoadModel(
            demo,
            &state->renderer,
            &state->truck,
            "processed/41-scene-model-loading/CesiumMilkTruck",
            "CesiumMilkTruck",
            ForgeGpuProcessedSceneLoadRgbaMaterialTexture,
            nullptr) ||
        !lesson45_apply_material_overrides(state)) {
        return false;
    }

    demo->lesson.camera_position = { LESSON45_CAM_START_X, LESSON45_CAM_START_Y, LESSON45_CAM_START_Z };
    demo->lesson.camera_yaw = LESSON45_CAM_START_YAW;
    demo->lesson.camera_pitch = LESSON45_CAM_START_PITCH;
    demo->lesson.move_speed = LESSON45_MOVE_SPEED;
    demo->lesson.mouse_sensitivity = LESSON45_MOUSE_SENSITIVITY;
    demo->lesson.pitch_clamp = LESSON45_PITCH_CLAMP;
    demo->lesson.last_ticks = SDL_GetTicks();
    return true;
}

bool ForgeGpuRenderLesson45(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPUTexture *swapchain_texture,
    Uint32 width,
    Uint32 height)
{
    Lesson45State *state = lesson45_state(demo);
    Mat4 view;
    Mat4 projection;
    Mat4 camera_vp;
    Mat4 light_vp;
    Mat4 truck_a;
    Mat4 truck_b;
    SDL_GPURenderPass *render_pass;

    if (!state) {
        SDL_SetError("lesson 45 internal state is missing");
        return false;
    }
    if (!ForgeGpuProcessedSceneRendererEnsureMainDepth(demo, &state->renderer, width, height)) {
        return false;
    }

    ForgeGpuUpdateCameraFromInput(demo);
    ForgeGpuCameraViewProjection(demo, width, height, LESSON45_FAR_PLANE, &view, &projection);
    camera_vp = mat4_multiply(projection, view);
    light_vp = ForgeGpuProcessedSceneLightViewProjection();
    truck_a = mat4_translate({ LESSON45_TRUCK_A_X, LESSON45_MODEL_Y, 0.0f });
    truck_b = mat4_translate({ LESSON45_TRUCK_B_X, LESSON45_MODEL_Y, 0.0f });

    state->renderer.transparency_sorting = state->transparency_sorting;
    ForgeGpuProcessedSceneRendererBeginFrame(&state->renderer);
    lesson45_reset_draw_counts(state);

    render_pass = ForgeGpuProcessedSceneBeginShadowPass(command_buffer, &state->renderer);
    if (!render_pass) {
        return false;
    }
    if (!ForgeGpuProcessedSceneDrawModel(demo, command_buffer, render_pass, &state->renderer, &state->truck, truck_a, camera_vp, light_vp, true) ||
        !ForgeGpuProcessedSceneDrawModel(demo, command_buffer, render_pass, &state->renderer, &state->truck, truck_b, camera_vp, light_vp, true)) {
        SDL_EndGPURenderPass(render_pass);
        return false;
    }
    SDL_EndGPURenderPass(render_pass);
    state->renderer.shadow_pass_rendered = true;

    render_pass = ForgeGpuProcessedSceneBeginMainPass(command_buffer, &state->renderer, swapchain_texture);
    if (!render_pass) {
        return false;
    }
    if (!ForgeGpuProcessedSceneDrawModel(demo, command_buffer, render_pass, &state->renderer, &state->truck, truck_a, camera_vp, light_vp, false) ||
        !ForgeGpuProcessedSceneDrawModel(demo, command_buffer, render_pass, &state->renderer, &state->truck, truck_b, camera_vp, light_vp, false)) {
        SDL_EndGPURenderPass(render_pass);
        return false;
    }
    ForgeGpuProcessedSceneDrawGrid(demo, command_buffer, render_pass, &state->renderer, camera_vp, light_vp);
    SDL_EndGPURenderPass(render_pass);
    state->renderer.main_pass_rendered = true;
    return true;
}

void ForgeGpuDebugLesson45(ForgeGpuDemo *demo)
{
    Lesson45State *state = lesson45_state(demo);

    if (!state) {
        return;
    }

    ImGui::Text("Glass sorting: %s", state->transparency_sorting ? "on" : "off");
    ImGui::Text("Glass: BLEND alpha %.2f", state->final_glass_alpha);
    ImGui::Text("Draw calls: %u", state->truck.draw_calls);
    ImGui::Text("Transparent draws: %u", state->truck.transparent_draw_calls);
    ImGui::Text("Shadow draws: %u", state->truck.shadow_draw_calls);
    ImGui::Text("Passes: shadow %s, main %s",
        state->renderer.shadow_pass_rendered ? "yes" : "no",
        state->renderer.main_pass_rendered ? "yes" : "no");
}

void ForgeGpuControlsLesson45(ForgeGpuDemo *demo)
{
    Lesson45State *state = lesson45_state(demo);

    if (!state) {
        return;
    }

    ImGui::Checkbox("Glass transparency sorting (T)", &state->transparency_sorting);
}

bool ForgeGpuHandleLesson45Event(ForgeGpuDemo *demo, const SDL_Event *event)
{
    Lesson45State *state = lesson45_state(demo);

    if (!state || event->type != SDL_EVENT_KEY_DOWN || event->key.repeat) {
        return false;
    }
    if (event->key.key == SDLK_T) {
        state->transparency_sorting = !state->transparency_sorting;
        return true;
    }
    return false;
}

void ForgeGpuExportLesson45Metrics(ForgeGpuDemo *demo)
{
    Lesson45State *state = lesson45_state(demo);

    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson45Ready", state ? 1.0 : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson45ShadowPass", state && state->renderer.shadow_pass_rendered ? 1.0 : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson45MainPass", state && state->renderer.main_pass_rendered ? 1.0 : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson45SortingEnabled", state && state->transparency_sorting ? 1.0 : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson45GlassOverride", state && state->glass_override_applied ? 1.0 : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson45OriginalGlassAlpha", state ? (double)state->original_glass_alpha : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson45FinalGlassAlpha", state ? (double)state->final_glass_alpha : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson45GlassMaterials", state ? (double)state->glass_material_count : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson45BlendMaterials", state ? (double)state->blend_material_count : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson45MaskMaterials", state ? (double)state->mask_material_count : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson45DoubleSidedMaterials", state ? (double)state->double_sided_material_count : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson45TruckMaterials", state ? (double)state->truck.materials.material_count : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson45TruckDrawCalls", state ? (double)state->truck.draw_calls : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson45TransparentDrawCalls", state ? (double)state->truck.transparent_draw_calls : 0.0);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLesson45ShadowDrawCalls", state ? (double)state->truck.shadow_draw_calls : 0.0);
}

void ForgeGpuDestroyLesson45(ForgeGpuDemo *demo)
{
    Lesson45State *state = lesson45_state(demo);

    if (!state) {
        return;
    }

    ForgeGpuProcessedSceneDestroyModel(demo->device, &state->truck);
    ForgeGpuProcessedSceneRendererDestroy(demo, &state->renderer);
    SDL_free(state);
    demo->lesson.private_state = nullptr;
}
