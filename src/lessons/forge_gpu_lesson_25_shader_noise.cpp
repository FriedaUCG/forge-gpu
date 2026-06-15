#include "forge_gpu_lessons.h"

#include "forge_gpu_browser_status.h"
#include "forge_gpu_gpu_helpers.h"
#include "forge_gpu_lesson_common.h"
#include "shaders/generated/forge_gpu_lesson_25_shaders.h"
#include "imgui.h"

#define LESSON25_FULLSCREEN_VERTICES 6
#define LESSON25_NUM_NOISE_MODES 6
#define LESSON25_DEFAULT_SCALE 8.0f
#define LESSON25_MIN_SCALE 1.0f
#define LESSON25_MAX_SCALE 64.0f
#define LESSON25_SCALE_STEP 1.0f
#define LESSON25_MODE_WHITE_NOISE 0
#define LESSON25_MODE_VALUE_NOISE 1
#define LESSON25_MODE_PERLIN 2
#define LESSON25_MODE_FBM 3
#define LESSON25_MODE_DOMAIN_WARP 4
#define LESSON25_MODE_TERRAIN 5

struct Lesson25NoiseUniforms
{
    float time;
    Sint32 mode;
    Sint32 dither_enabled;
    float scale;
    float resolution[2];
    float pad[2];
};

struct Lesson25State
{
    float time;
    Sint32 noise_mode;
    Sint32 dither_enabled;
    float scale;
    Uint64 last_ticks;
    bool paused;
};

static_assert(sizeof(Lesson25NoiseUniforms) == 32, "lesson 25 fragment uniform size must match HLSL layout");

static const char *const kLesson25ModeNames[LESSON25_NUM_NOISE_MODES] = {
    "White noise",
    "Value noise",
    "Gradient noise",
    "fBm",
    "Domain warping",
    "Procedural terrain"
};

static Lesson25State *lesson25_state(ForgeGpuDemo *demo)
{
    return (Lesson25State *)demo->lesson.private_state;
}

static void lesson25_set_mode(Lesson25State *state, Sint32 mode)
{
    if (mode < 0) {
        mode = 0;
    }
    if (mode >= LESSON25_NUM_NOISE_MODES) {
        mode = LESSON25_NUM_NOISE_MODES - 1;
    }
    state->noise_mode = mode;
}

static void lesson25_adjust_scale(Lesson25State *state, float delta)
{
    state->scale += delta;
    if (state->scale < LESSON25_MIN_SCALE) {
        state->scale = LESSON25_MIN_SCALE;
    }
    if (state->scale > LESSON25_MAX_SCALE) {
        state->scale = LESSON25_MAX_SCALE;
    }
}

bool ForgeGpuCreateLesson25(ForgeGpuDemo *demo)
{
    Lesson25State *state;
    SDL_GPUShader *vertex_shader;
    SDL_GPUShader *fragment_shader;

    state = (Lesson25State *)SDL_calloc(1, sizeof(*state));
    if (!state) {
        SDL_OutOfMemory();
        return false;
    }
    state->noise_mode = LESSON25_MODE_FBM;
    state->scale = LESSON25_DEFAULT_SCALE;
    state->last_ticks = SDL_GetTicks();
    demo->lesson.private_state = state;

    vertex_shader = ForgeGpuCreateShader(
        demo->device,
        SDL_GPU_SHADERSTAGE_VERTEX,
        lesson25_noise_vert_wgsl,
        lesson25_noise_vert_wgsl_size,
        lesson25_noise_vert_msl,
        lesson25_noise_vert_msl_size,
        0,
        0,
        0,
        0);
    fragment_shader = ForgeGpuCreateShader(
        demo->device,
        SDL_GPU_SHADERSTAGE_FRAGMENT,
        lesson25_noise_frag_wgsl,
        lesson25_noise_frag_wgsl_size,
        lesson25_noise_frag_msl,
        lesson25_noise_frag_msl_size,
        0,
        0,
        0,
        1);
    if (!vertex_shader || !fragment_shader) {
        if (vertex_shader) {
            SDL_ReleaseGPUShader(demo->device, vertex_shader);
        }
        if (fragment_shader) {
            SDL_ReleaseGPUShader(demo->device, fragment_shader);
        }
        return false;
    }

    demo->lesson.pipeline = ForgeGpuCreateFullscreenPostprocessPipeline(
        demo,
        vertex_shader,
        fragment_shader,
        demo->color_format,
        false);
    SDL_ReleaseGPUShader(demo->device, vertex_shader);
    SDL_ReleaseGPUShader(demo->device, fragment_shader);
    return demo->lesson.pipeline != nullptr;
}

bool ForgeGpuRenderLesson25(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPUTexture *swapchain_texture,
    Uint32 width,
    Uint32 height)
{
    Lesson25State *state = lesson25_state(demo);
    Lesson25NoiseUniforms uniforms;
    SDL_FColor clear_color;

    if (!state) {
        SDL_SetError("lesson 25 internal state is missing");
        return false;
    }

    if (demo->validation_mode) {
        state->time = ForgeGpuFrameTimeSeconds(demo);
    } else {
        const Uint64 now = SDL_GetTicks();
        float delta = state->last_ticks != 0 ? (float)(now - state->last_ticks) / 1000.0f : 0.0f;
        state->last_ticks = now;
        if (delta > FORGE_GPU_MAX_DELTA_TIME) {
            delta = FORGE_GPU_MAX_DELTA_TIME;
        }
        if (!state->paused) {
            state->time += delta;
        }
    }

    SDL_zero(uniforms);
    uniforms.time = state->time;
    uniforms.mode = state->noise_mode;
    uniforms.dither_enabled = state->dither_enabled;
    uniforms.scale = state->scale;
    uniforms.resolution[0] = (float)width;
    uniforms.resolution[1] = (float)height;

    SDL_zero(clear_color);
    return ForgeGpuRunFullscreenPostprocessPass(
        command_buffer,
        swapchain_texture,
        SDL_GPU_LOADOP_DONT_CARE,
        clear_color,
        demo->lesson.pipeline,
        nullptr,
        0,
        &uniforms,
        sizeof(uniforms),
        LESSON25_FULLSCREEN_VERTICES);
}

void ForgeGpuDebugLesson25(ForgeGpuDemo *demo)
{
    Lesson25State *state = lesson25_state(demo);

    if (!state) {
        return;
    }
    ImGui::Text("Mode: %s", kLesson25ModeNames[state->noise_mode]);
    ImGui::Text("Scale: %.0f", state->scale);
    ImGui::Text("Dithering: %s", state->dither_enabled ? "on" : "off");
    ImGui::Text("Animation: %s", state->paused ? "paused" : "running");
}

void ForgeGpuControlsLesson25(ForgeGpuDemo *demo)
{
    (void)demo;
    ImGui::Text("1-6 selects noise mode");
    ImGui::Text("D toggles dithering; +/- adjusts scale; P pauses");
}

bool ForgeGpuHandleLesson25Event(ForgeGpuDemo *demo, const SDL_Event *event)
{
    Lesson25State *state = lesson25_state(demo);

    if (!state || event->type != SDL_EVENT_KEY_DOWN) {
        return false;
    }

    if (ForgeGpuEventIsPlusKey(event)) {
        lesson25_adjust_scale(state, LESSON25_SCALE_STEP);
        return true;
    }
    if (ForgeGpuEventIsMinusKey(event)) {
        lesson25_adjust_scale(state, -LESSON25_SCALE_STEP);
        return true;
    }
    if (!event->key.repeat) {
        if (event->key.key >= SDLK_1 && event->key.key <= SDLK_6) {
            lesson25_set_mode(state, (Sint32)(event->key.key - SDLK_1));
            return true;
        }
        if (event->key.key == SDLK_D) {
            state->dither_enabled = !state->dither_enabled;
            return true;
        }
        if (event->key.key == SDLK_P) {
            state->paused = !state->paused;
            return true;
        }
    }
    return false;
}

void ForgeGpuExportLesson25Metrics(ForgeGpuDemo *demo)
{
    Lesson25State *state = lesson25_state(demo);

    if (!state) {
        return;
    }
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuNoiseMode", (double)state->noise_mode);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuNoiseScale", (double)state->scale);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuNoiseDither", (double)state->dither_enabled);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuNoisePaused", state->paused ? 1.0 : 0.0);
}

void ForgeGpuDestroyLesson25(ForgeGpuDemo *demo)
{
    Lesson25State *state = lesson25_state(demo);

    if (!state) {
        return;
    }
    SDL_free(state);
    demo->lesson.private_state = nullptr;
}
