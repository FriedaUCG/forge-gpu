#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <stdarg.h>

#if defined(SDL_PLATFORM_EMSCRIPTEN)
#include <emscripten.h>
#endif

#include "forge_gpu_browser_status.h"
#include "forge_gpu_camera.h"
#include "forge_gpu_gpu_helpers.h"
#include "forge_gpu_imgui.h"
#include "forge_gpu_internal.h"
#include "forge_gpu_lesson_common.h"
#include "forge_gpu_lessons.h"
#include "forge_gpu_scene.h"

static const int kDefaultLessonIndex = FORGE_GPU_DEFAULT_LESSON_INDEX;
static const Uint64 kAdjustmentRepeatInitialDelayMs = 500;
static const Uint64 kAdjustmentRepeatIntervalMs = 250;

static void set_status(ForgeGpuDemo *demo, const char *format, ...)
{
    va_list args;

    va_start(args, format);
    SDL_vsnprintf(demo->status, sizeof(demo->status), format, args);
    va_end(args);

    SDL_Log("%s", demo->status);

#if defined(SDL_PLATFORM_EMSCRIPTEN)
    ForgeGpuBrowserSetStatus(demo->status, (int)demo->frame_index);
#endif
}

static bool keyboard_event_is_plus(const SDL_KeyboardEvent *key)
{
    if (key->key == SDLK_PLUS || key->key == SDLK_KP_PLUS) {
        return true;
    }
    return (key->key == SDLK_EQUALS || key->scancode == SDL_SCANCODE_EQUALS) &&
           (key->mod & SDL_KMOD_SHIFT) != 0;
}

static bool keyboard_event_is_minus(const SDL_KeyboardEvent *key)
{
    return key->key == SDLK_MINUS || key->key == SDLK_KP_MINUS ||
           key->scancode == SDL_SCANCODE_MINUS || key->scancode == SDL_SCANCODE_KP_MINUS;
}

static bool keyboard_event_is_repeatable_adjustment(const SDL_KeyboardEvent *key)
{
    if (keyboard_event_is_plus(key) || keyboard_event_is_minus(key)) {
        return true;
    }

    switch (key->key) {
    case SDLK_UP:
    case SDLK_DOWN:
    case SDLK_LEFT:
    case SDLK_RIGHT:
        return true;
    default:
        return false;
    }
}

static AdjustmentKeyRepeatSlot *adjustment_repeat_slot_for_scancode(ForgeGpuDemo *demo, SDL_Scancode scancode)
{
    for (size_t i = 0; i < SDL_arraysize(demo->adjustment_key_repeat.slots); i += 1) {
        AdjustmentKeyRepeatSlot *slot = &demo->adjustment_key_repeat.slots[i];
        if (slot->active && slot->scancode == scancode) {
            return slot;
        }
    }
    return nullptr;
}

static AdjustmentKeyRepeatSlot *adjustment_repeat_free_slot(ForgeGpuDemo *demo)
{
    for (size_t i = 0; i < SDL_arraysize(demo->adjustment_key_repeat.slots); i += 1) {
        AdjustmentKeyRepeatSlot *slot = &demo->adjustment_key_repeat.slots[i];
        if (!slot->active) {
            return slot;
        }
    }
    return &demo->adjustment_key_repeat.slots[0];
}

static void reset_adjustment_key_repeats(ForgeGpuDemo *demo)
{
    SDL_zero(demo->adjustment_key_repeat);
}

static void start_adjustment_key_repeat(ForgeGpuDemo *demo, const SDL_KeyboardEvent *key)
{
    AdjustmentKeyRepeatSlot *slot = adjustment_repeat_slot_for_scancode(demo, key->scancode);

    if (!slot) {
        slot = adjustment_repeat_free_slot(demo);
    }

    slot->key = key->key;
    slot->scancode = key->scancode;
    slot->mod = key->mod;
    slot->next_repeat_ticks = SDL_GetTicks() + kAdjustmentRepeatInitialDelayMs;
    slot->active = true;
    slot->requires_shift = key->scancode == SDL_SCANCODE_EQUALS &&
                           (key->mod & SDL_KMOD_SHIFT) != 0;
}

static void stop_adjustment_key_repeat(ForgeGpuDemo *demo, SDL_Scancode scancode)
{
    AdjustmentKeyRepeatSlot *slot = adjustment_repeat_slot_for_scancode(demo, scancode);

    if (slot) {
        SDL_zero(*slot);
    }
}

static bool adjustment_key_repeat_is_active_for_event(ForgeGpuDemo *demo, const SDL_KeyboardEvent *key)
{
    return adjustment_repeat_slot_for_scancode(demo, key->scancode) != nullptr;
}

static bool adjustment_key_repeat_slot_is_held(const AdjustmentKeyRepeatSlot *slot)
{
    int key_count = 0;
    const bool *keys = SDL_GetKeyboardState(&key_count);

    if (!keys || slot->scancode < 0 || slot->scancode >= key_count || !keys[slot->scancode]) {
        return false;
    }
    if (slot->requires_shift) {
        const bool left_shift = SDL_SCANCODE_LSHIFT >= 0 && SDL_SCANCODE_LSHIFT < key_count && keys[SDL_SCANCODE_LSHIFT];
        const bool right_shift = SDL_SCANCODE_RSHIFT >= 0 && SDL_SCANCODE_RSHIFT < key_count && keys[SDL_SCANCODE_RSHIFT];
        return left_shift || right_shift;
    }
    return true;
}

static void dispatch_adjustment_key_repeats(ForgeGpuDemo *demo)
{
    const LessonDesc *lesson_desc;
    const Uint64 now = SDL_GetTicks();

    if (demo->validation_mode || demo->pending_lesson != demo->active_lesson) {
        return;
    }

    lesson_desc = &gForgeGpuLessons[demo->active_lesson];
    if (!lesson_desc->handle_event) {
        reset_adjustment_key_repeats(demo);
        return;
    }

    for (size_t i = 0; i < SDL_arraysize(demo->adjustment_key_repeat.slots); i += 1) {
        AdjustmentKeyRepeatSlot *slot = &demo->adjustment_key_repeat.slots[i];
        SDL_Event repeat_event;

        if (!slot->active) {
            continue;
        }
        if (!adjustment_key_repeat_slot_is_held(slot)) {
            SDL_zero(*slot);
            continue;
        }
        if (now < slot->next_repeat_ticks) {
            continue;
        }

        SDL_zero(repeat_event);
        repeat_event.type = SDL_EVENT_KEY_DOWN;
        repeat_event.key.type = SDL_EVENT_KEY_DOWN;
        repeat_event.key.timestamp = SDL_GetTicksNS();
        repeat_event.key.windowID = SDL_GetWindowID(demo->window);
        repeat_event.key.scancode = slot->scancode;
        repeat_event.key.key = slot->key;
        repeat_event.key.mod = slot->mod;
        repeat_event.key.down = true;
        repeat_event.key.repeat = true;
        lesson_desc->handle_event(demo, &repeat_event);

        do {
            slot->next_repeat_ticks += kAdjustmentRepeatIntervalMs;
        } while (slot->next_repeat_ticks <= now);
    }
}

static bool render_frame(ForgeGpuDemo *demo)
{
    SDL_GPUCommandBuffer *command_buffer;
    SDL_GPUTexture *swapchain_texture = nullptr;
    Uint32 width = 0;
    Uint32 height = 0;
    int window_width = 0;
    int window_height = 0;
    bool lesson_switched = false;
    const LessonDesc *lesson_desc;

    if (demo->pending_lesson != demo->active_lesson) {
        const int next_lesson = demo->pending_lesson;

        if (!SDL_WaitForGPUIdle(demo->device)) {
            set_status(demo, "SDL_WaitForGPUIdle failed: %s", SDL_GetError());
            return false;
        }
        demo->start_ticks = SDL_GetTicks();
        if (!ForgeGpuCreateLesson(demo, next_lesson)) {
            set_status(demo, "Forge GPU lesson %s create failed: %s", gForgeGpuLessons[next_lesson].id, SDL_GetError());
            return false;
        }
        reset_adjustment_key_repeats(demo);
        set_status(demo, "SDL_GPU Forge GPU lesson %s ready", gForgeGpuLessons[demo->active_lesson].id);
        ForgeGpuResetFrameStats(demo);
        lesson_switched = true;
    }

    if (!lesson_switched) {
        ForgeGpuUpdateFrameStats(demo);
    }
    ForgeGpuSyncCameraMouseCapture(demo);
    dispatch_adjustment_key_repeats(demo);

    lesson_desc = &gForgeGpuLessons[demo->active_lesson];

    command_buffer = SDL_AcquireGPUCommandBuffer(demo->device);
    if (!command_buffer) {
        set_status(demo, "SDL_AcquireGPUCommandBuffer failed: %s", SDL_GetError());
        return false;
    }

    SDL_GetWindowSizeInPixels(demo->window, &window_width, &window_height);
    if (!ForgeGpuPrepareImGui(
            demo,
            command_buffer,
            window_width > 0 ? (Uint32)window_width : 1u,
            window_height > 0 ? (Uint32)window_height : 1u)) {
        set_status(demo, "Forge GPU ImGui prepare failed: %s", SDL_GetError());
        SDL_CancelGPUCommandBuffer(command_buffer);
        return false;
    }

    if (demo->validation_mode) {
        if (!SDL_WaitAndAcquireGPUSwapchainTexture(command_buffer, demo->window, &swapchain_texture, &width, &height)) {
            set_status(demo, "SDL_WaitAndAcquireGPUSwapchainTexture failed: %s", SDL_GetError());
            SDL_CancelGPUCommandBuffer(command_buffer);
            return false;
        }
    } else if (!SDL_AcquireGPUSwapchainTexture(command_buffer, demo->window, &swapchain_texture, &width, &height)) {
        set_status(demo, "SDL_AcquireGPUSwapchainTexture failed: %s", SDL_GetError());
        SDL_CancelGPUCommandBuffer(command_buffer);
        return false;
    }

    if (!swapchain_texture) {
        if (demo->validation_mode) {
            SDL_CancelGPUCommandBuffer(command_buffer);
        } else {
            SDL_SubmitGPUCommandBuffer(command_buffer);
        }
        return true;
    }

    if (lesson_desc->render_frame) {
        if (!lesson_desc->render_frame(demo, command_buffer, swapchain_texture, width, height)) {
            set_status(demo, "Forge GPU lesson %s render failed: %s", lesson_desc->id, SDL_GetError());
            SDL_CancelGPUCommandBuffer(command_buffer);
            return false;
        }
    } else if (!ForgeGpuRenderDefaultLessonPass(demo, command_buffer, swapchain_texture, width, height, lesson_desc->render_pass)) {
        set_status(demo, "Forge GPU lesson %s render pass failed: %s", lesson_desc->id, SDL_GetError());
        SDL_CancelGPUCommandBuffer(command_buffer);
        return false;
    }

    if (!ForgeGpuRenderImGui(demo, command_buffer, swapchain_texture)) {
        set_status(demo, "Forge GPU ImGui render failed: %s", SDL_GetError());
        SDL_CancelGPUCommandBuffer(command_buffer);
        return false;
    }

    if (!SDL_SubmitGPUCommandBuffer(command_buffer)) {
        set_status(demo, "SDL_SubmitGPUCommandBuffer failed: %s", SDL_GetError());
        return false;
    }

    demo->frame_index += 1;

#if defined(SDL_PLATFORM_EMSCRIPTEN)
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuLessonIndex", (double)SDL_atoi(lesson_desc->id));
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuFrame", (double)demo->frame_index);
    ForgeGpuBrowserSetNumberMetric("sdlGpuForgeGpuShadercrossWgsl", (double)((demo->shader_formats & SDL_GPU_SHADERFORMAT_WGSL) != 0));
    if (lesson_desc->export_metrics) {
        lesson_desc->export_metrics(demo);
    }
#endif

    if (demo->validation_mode && demo->frame_index >= FORGE_GPU_VALIDATION_COMPLETE_FRAME) {
        set_status(demo, "SDL_GPU Forge GPU lesson %s ok", gForgeGpuLessons[demo->active_lesson].id);
        demo->complete = true;
    }

    return true;
}

static void process_event(ForgeGpuDemo *demo, const SDL_Event *event)
{
    const LessonDesc *lesson_desc = &gForgeGpuLessons[demo->active_lesson];

    if (ForgeGpuProcessImGuiEvent(demo, event)) {
        return;
    }

    if (event->type == SDL_EVENT_QUIT) {
        demo->quit = true;
        return;
    }

    if (event->type == SDL_EVENT_KEY_UP && keyboard_event_is_repeatable_adjustment(&event->key)) {
        stop_adjustment_key_repeat(demo, event->key.scancode);
    }

    if (event->type == SDL_EVENT_KEY_DOWN &&
        event->key.repeat &&
        keyboard_event_is_repeatable_adjustment(&event->key) &&
        adjustment_key_repeat_is_active_for_event(demo, &event->key)) {
        return;
    }

    if (ForgeGpuLessonUsesCameraInput(demo->active_lesson)) {
        /* ImGui consumes UI clicks before this path, so scene clicks enter the
         * same persistent capture model forge-gpu uses after startup. */
        if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN && !demo->lesson.mouse_captured) {
            if (lesson_desc->handle_event && lesson_desc->handle_event(demo, event)) {
                return;
            }
            if (!ForgeGpuAcquireCameraMouse(demo, event->button.x, event->button.y)) {
                SDL_Log("SDL_SetWindowRelativeMouseMode failed: %s", SDL_GetError());
            }
            return;
        }
        if (event->type == SDL_EVENT_MOUSE_MOTION && demo->lesson.mouse_captured) {
            const float max_pitch = demo->lesson.pitch_clamp > 0.0f ? demo->lesson.pitch_clamp : 89.0f * FORGE_GPU_DEG2RAD;
            const float mouse_sensitivity = demo->lesson.mouse_sensitivity > 0.0f ? demo->lesson.mouse_sensitivity : 0.002f;

            demo->lesson.camera_yaw -= (float)event->motion.xrel * mouse_sensitivity;
            demo->lesson.camera_pitch -= (float)event->motion.yrel * mouse_sensitivity;
            if (demo->lesson.camera_pitch > max_pitch) {
                demo->lesson.camera_pitch = max_pitch;
            }
            if (demo->lesson.camera_pitch < -max_pitch) {
                demo->lesson.camera_pitch = -max_pitch;
            }
            return;
        }
    }

    if (event->type == SDL_EVENT_KEY_DOWN && !event->key.repeat) {
        if (event->key.key == SDLK_ESCAPE) {
            if (demo->lesson.mouse_captured) {
                if (!ForgeGpuReleaseCameraMouse(demo)) {
                    SDL_Log("SDL_SetWindowRelativeMouseMode failed: %s", SDL_GetError());
                }
            }
            return;
        }
    }

    if (lesson_desc->handle_event) {
        const bool handled = lesson_desc->handle_event(demo, event);

        if (handled &&
            event->type == SDL_EVENT_KEY_DOWN &&
            !event->key.repeat &&
            keyboard_event_is_repeatable_adjustment(&event->key)) {
            start_adjustment_key_repeat(demo, &event->key);
        }
        if (handled) {
            return;
        }
    }

    if (event->type == SDL_EVENT_KEY_DOWN && !event->key.repeat) {
        if (event->key.key == SDLK_K) {
            demo->pending_lesson = (demo->active_lesson + 1) % gForgeGpuLessonCount;
            return;
        } else if (event->key.key == SDLK_J) {
            demo->pending_lesson = (demo->active_lesson + gForgeGpuLessonCount - 1) % gForgeGpuLessonCount;
            return;
        }
    }
}

static int lesson_index_from_number(int value)
{
    if (value < 1) {
        return -1;
    }

    for (int i = 0; i < gForgeGpuLessonCount; i += 1) {
        if (SDL_atoi(gForgeGpuLessons[i].id) == value) {
            return i;
        }
    }
    return -1;
}

static int lesson_index_from_id(const char *id)
{
    if (!id || !*id) {
        return kDefaultLessonIndex;
    }

    for (int i = 0; i < gForgeGpuLessonCount; i += 1) {
        if (SDL_strcmp(id, gForgeGpuLessons[i].id) == 0) {
            return i;
        }
    }

    const int value = SDL_atoi(id);
    return lesson_index_from_number(value);
}

static bool create_demo(
    SDL_Window *window,
    SDL_GPUDevice *device,
    const char *asset_root,
    int lesson_index,
    bool validation_mode,
    SDL_GPUTextureFormat color_format_override,
    bool claimed_window,
    ForgeGpuDemo **out_demo)
{
    ForgeGpuDemo *demo;
    SDL_GPUTextureFormat swapchain_format = SDL_GPU_TEXTUREFORMAT_INVALID;

    if (!window || !device || !out_demo) {
        SDL_SetError("invalid forge-gpu demo create arguments");
        return false;
    }

    *out_demo = nullptr;
    demo = (ForgeGpuDemo *)SDL_calloc(1, sizeof(*demo));
    if (!demo) {
        SDL_OutOfMemory();
        return false;
    }

    if (claimed_window) {
        swapchain_format = SDL_GetGPUSwapchainTextureFormat(device, window);
    }
    demo->window = window;
    demo->device = device;
    demo->asset_root = asset_root;
    demo->swapchain_color_format = swapchain_format;
    demo->color_format = color_format_override != SDL_GPU_TEXTUREFORMAT_INVALID ? color_format_override : swapchain_format;
    demo->shader_formats = SDL_GetGPUShaderFormats(device);
    demo->validation_mode = validation_mode;
    demo->swapchain_sdr_linear_supported = claimed_window ?
        SDL_WindowSupportsGPUSwapchainComposition(device, window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR_LINEAR) :
        false;
    demo->color_format_overridden = color_format_override != SDL_GPU_TEXTUREFORMAT_INVALID && color_format_override != swapchain_format;
    demo->claimed_window = claimed_window;
    demo->active_lesson = -1;
    demo->pending_lesson = lesson_index;
    demo->start_ticks = SDL_GetTicks();

    if ((claimed_window && swapchain_format == SDL_GPU_TEXTUREFORMAT_INVALID) ||
        (!claimed_window && color_format_override == SDL_GPU_TEXTUREFORMAT_INVALID) ||
        demo->color_format == SDL_GPU_TEXTUREFORMAT_INVALID) {
        SDL_free(demo);
        SDL_SetError("invalid forge-gpu color format");
        return false;
    }

    if (!ForgeGpuInitImGui(demo)) {
        ForgeGpuShutdownImGui(demo);
        SDL_free(demo);
        return false;
    }

    if (!ForgeGpuCreateLesson(demo, lesson_index)) {
        ForgeGpuShutdownImGui(demo);
        ForgeGpuDestroyLesson(demo);
        SDL_free(demo);
        return false;
    }

    set_status(demo, "SDL_GPU Forge GPU lesson %s ready", gForgeGpuLessons[demo->active_lesson].id);
    ForgeGpuResetFrameStats(demo);
    *out_demo = demo;
    return true;
}

static void destroy_demo(ForgeGpuDemo *demo)
{
    if (!demo) {
        return;
    }

    if (demo->device) {
        SDL_WaitForGPUIdle(demo->device);
        ForgeGpuDestroyLesson(demo);
    }
    ForgeGpuShutdownImGui(demo);
    SDL_free(demo);
}


#if defined(SDL_PLATFORM_EMSCRIPTEN)


EM_JS(int, ForgeGpuBrowserLessonNumber, (), {
    const params = new URLSearchParams(window.location.search);
    const raw = params.get("scene") || params.get("lesson") || "01";
    const match = String(raw).match(/[0-9]+/);
    const value = match ? Number(match[0]) : 1;
    if (!Number.isFinite(value) || value < 1) {
        return -1;
    }
    return value;
});


static SDL_Window *g_window;
static SDL_GPUDevice *g_device;
static ForgeGpuDemo *g_demo;

static void fail_browser_startup(const char *context)
{
    char message[FORGE_GPU_MAX_STATUS];
    SDL_snprintf(message, sizeof(message), "%s: %s", context, SDL_GetError());
    SDL_Log("%s", message);
    ForgeGpuBrowserSetStatus(message, 0);
}



static void browser_main_loop(void)
{
    SDL_Event event;

    while (SDL_PollEvent(&event)) {
        process_event(g_demo, &event);
    }


    if (!render_frame(g_demo) || g_demo->quit || g_demo->complete) {
        emscripten_cancel_main_loop();
    }
}

int main(int argc, char **argv)
{
    int lesson_index;
    const bool validation_mode = ForgeGpuBrowserGetValidationMode() != 0;
    SDL_GPUTextureFormat color_format_override = SDL_GPU_TEXTUREFORMAT_INVALID;

    (void)argc;
    (void)argv;

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fail_browser_startup("SDL_Init failed");
        return 1;
    }

    lesson_index = lesson_index_from_number(ForgeGpuBrowserLessonNumber());
    if (lesson_index < 0) {
        SDL_SetError("invalid forge-gpu lesson query");
        fail_browser_startup("Forge GPU startup failed");
        return 1;
    }

    g_window = SDL_CreateWindow("SDL_GPU Forge GPU", 960, 540, SDL_WINDOW_RESIZABLE);
    if (!g_window) {
        fail_browser_startup("SDL_CreateWindow failed");
        return 1;
    }

    g_device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_WGSL, false, "webgpu");
    if (!g_device) {
        fail_browser_startup("SDL_CreateGPUDevice failed");
        return 1;
    }

    if (!SDL_ClaimWindowForGPUDevice(g_device, g_window)) {
        fail_browser_startup("SDL_ClaimWindowForGPUDevice failed");
        return 1;
    }

    if (!ForgeGpuConfigureSwapchain(g_device, g_window)) {
        fail_browser_startup("SDL_SetGPUSwapchainParameters failed");
        return 1;
    }


    if (!create_demo(g_window, g_device, "/assets", lesson_index, validation_mode, color_format_override, true, &g_demo)) {
        fail_browser_startup("Forge GPU demo create failed");
        return 1;
    }


    emscripten_set_main_loop(browser_main_loop, 0, 1);
    return 0;
}

#else

static void print_usage(const char *argv0)
{
    char scene_ids[256];

    SDL_strlcpy(scene_ids, gForgeGpuLessons[0].id, sizeof(scene_ids));
    for (int i = 1; i < gForgeGpuLessonCount; i += 1) {
        SDL_strlcat(scene_ids, ", ", sizeof(scene_ids));
        SDL_strlcat(scene_ids, gForgeGpuLessons[i].id, sizeof(scene_ids));
    }

    SDL_Log(
        "Usage: %s [--gpu DRIVER] [--asset-root PATH] [--scene LESSON_ID] [--validation] [--debug] [--hidden] [--max-iterations N] [--asset-self-test] [--scene-texture-cache-self-test]"
        ,
        argv0);
    SDL_Log(
        "Available scene IDs: %s",
        scene_ids);
}

int main(int argc, char **argv)
{
    const char *gpu_driver = nullptr;
    const char *asset_root = SDLGPU_FORGE_GPU_DEFAULT_ASSET_ROOT;
    int lesson_index = kDefaultLessonIndex;
    bool validation_mode = false;
    bool debug_mode = false;
    bool hidden = false;
    bool asset_self_test = false;
    bool scene_texture_cache_self_test = false;
    Uint32 max_iterations = 600;
    SDL_WindowFlags window_flags = SDL_WINDOW_RESIZABLE;
    SDL_Window *window = nullptr;
    SDL_GPUDevice *device = nullptr;
    ForgeGpuDemo *demo = nullptr;
    bool claimed_window = false;
    bool ok = false;
    SDL_GPUTextureFormat color_format_override = SDL_GPU_TEXTUREFORMAT_INVALID;

    for (int i = 1; i < argc; i += 1) {
        if (SDL_strcmp(argv[i], "--gpu") == 0 && i + 1 < argc) {
            gpu_driver = argv[++i];
        } else if (SDL_strcmp(argv[i], "--asset-root") == 0 && i + 1 < argc) {
            asset_root = argv[++i];
        } else if (SDL_strcmp(argv[i], "--scene") == 0 && i + 1 < argc) {
            lesson_index = lesson_index_from_id(argv[++i]);
            if (lesson_index < 0) {
                SDL_Log("Unknown forge-gpu lesson: %s", argv[i]);
                return 2;
            }
        } else if (SDL_strcmp(argv[i], "--validation") == 0) {
            validation_mode = true;
        } else if (SDL_strcmp(argv[i], "--debug") == 0) {
            debug_mode = true;
        } else if (SDL_strcmp(argv[i], "--hidden") == 0) {
            hidden = true;
        } else if (SDL_strcmp(argv[i], "--asset-self-test") == 0) {
            asset_self_test = true;
        } else if (SDL_strcmp(argv[i], "--scene-texture-cache-self-test") == 0) {
            scene_texture_cache_self_test = true;
        } else if (SDL_strcmp(argv[i], "--max-iterations") == 0 && i + 1 < argc) {
            max_iterations = (Uint32)SDL_atoi(argv[++i]);
        } else if (SDL_strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            SDL_Log("Unknown argument: %s", argv[i]);
            print_usage(argv[0]);
            return 2;
        }
    }

    SDL_SetLogPriorities(SDL_LOG_PRIORITY_VERBOSE);


    if (asset_self_test) {
        if (!ForgeGpuRunAssetLoaderSelfTest(asset_root)) {
            SDL_Log("Forge GPU asset loader self-test failed: %s", SDL_GetError());
            return 1;
        }
        SDL_Log("Forge GPU asset loader self-test ok");
        return 0;
    }

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        goto done;
    }

    if (hidden) {
        window_flags |= SDL_WINDOW_HIDDEN;
    }
    window = SDL_CreateWindow("SDL_GPU Forge GPU", 960, 540, window_flags);
    if (!window) {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        goto done;
    }

    device = SDL_CreateGPUDevice(
        SDL_GPU_SHADERFORMAT_WGSL | SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_MSL,
        debug_mode,
        gpu_driver);
    if (!device) {
        SDL_Log("SDL_CreateGPUDevice failed: %s", SDL_GetError());
        goto done;
    }
    SDL_Log("Using SDL_GPU driver: %s", SDL_GetGPUDeviceDriver(device));

    if (!SDL_ClaimWindowForGPUDevice(device, window)) {
        SDL_Log("SDL_ClaimWindowForGPUDevice failed: %s", SDL_GetError());
        goto done;
    }
    claimed_window = true;

    if (scene_texture_cache_self_test) {
        ForgeGpuDemo scratch_demo;
        char texture_path[FORGE_GPU_MAX_PATH];
        char normal_map_scene_path[FORGE_GPU_MAX_PATH];
        bool self_test_ok;

        SDL_zero(scratch_demo);
        scratch_demo.device = device;
        scratch_demo.asset_root = asset_root;
        if (!ForgeGpuJoinAssetPath(&scratch_demo, "textures/04-textures-and-samplers/brick_wall.png", texture_path, sizeof(texture_path))) {
            SDL_SetError("scene texture cache self-test texture path too long");
            self_test_ok = false;
        } else {
            self_test_ok = ForgeGpuRunSceneTextureCacheSelfTest(&scratch_demo, texture_path);
        }
        if (self_test_ok) {
            if (!ForgeGpuJoinAssetPath(&scratch_demo, "models/NormalTangentMirrorTest/NormalTangentMirrorTest.gltf", normal_map_scene_path, sizeof(normal_map_scene_path))) {
                SDL_SetError("normal-map scene self-test asset path too long");
                self_test_ok = false;
            } else {
                self_test_ok = ForgeGpuRunNormalMapSceneSelfTest(&scratch_demo, normal_map_scene_path);
            }
        }
        if (scratch_demo.lesson.white_texture) {
            SDL_ReleaseGPUTexture(device, scratch_demo.lesson.white_texture);
        }
        for (int i = 0; i < FORGE_GPU_MAX_SAMPLERS; i += 1) {
            if (scratch_demo.lesson.samplers[i]) {
                SDL_ReleaseGPUSampler(device, scratch_demo.lesson.samplers[i]);
            }
        }
        if (!self_test_ok) {
            SDL_Log("Forge GPU scene texture cache self-test failed: %s", SDL_GetError());
            goto done;
        }
        SDL_Log("Forge GPU scene texture cache self-test ok");
        ok = true;
        goto done;
    }

    if (claimed_window) {
        if (!ForgeGpuConfigureSwapchain(device, window)) {
            SDL_Log("SDL_SetGPUSwapchainParameters failed: %s", SDL_GetError());
            goto done;
        }
    }


    if (!create_demo(window, device, asset_root, lesson_index, validation_mode, color_format_override, claimed_window, &demo)) {
        SDL_Log("Forge GPU demo create failed: %s", SDL_GetError());
        goto done;
    }


    for (Uint32 iteration = 0; !validation_mode || iteration < max_iterations; iteration += 1) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            process_event(demo, &event);
        }

        if (!render_frame(demo)) {
            goto done;
        }
        if (demo->complete) {
            ok = true;
            break;
        }
        if (demo->quit) {
            ok = !validation_mode;
            break;
        }
    }

    if (!validation_mode) {
        ok = true;
    } else if (!ok) {
        SDL_Log("Forge GPU validation timed out for lesson %s", gForgeGpuLessons[lesson_index].id);
    }

done:
    destroy_demo(demo);
    if (device && claimed_window) {
        SDL_ReleaseWindowFromGPUDevice(device, window);
    }
    if (device) {
        SDL_DestroyGPUDevice(device);
    }
    if (window) {
        SDL_DestroyWindow(window);
    }
    SDL_Quit();
    return ok ? 0 : 1;
}

#endif
