#include "forge_gpu_imgui.h"

#include "forge_gpu_camera.h"

#include "imgui.h"
#include "backends/imgui_impl_sdl3.h"
#include "backends/imgui_impl_sdlgpu3.h"

#include <float.h>

static void set_current_imgui_context(ForgeGpuDemo *demo)
{
    ImGui::SetCurrentContext(demo ? demo->imgui_context : nullptr);
}

static bool event_is_mouse_input(const SDL_Event *event)
{
    return event->type == SDL_EVENT_MOUSE_MOTION ||
           event->type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
           event->type == SDL_EVENT_MOUSE_BUTTON_UP ||
           event->type == SDL_EVENT_MOUSE_WHEEL;
}

static bool event_affects_mouse_ui(const SDL_Event *event)
{
    return event_is_mouse_input(event) ||
           event->type == SDL_EVENT_WINDOW_MOUSE_ENTER ||
           event->type == SDL_EVENT_WINDOW_MOUSE_LEAVE;
}

static bool event_is_keyboard_input(const SDL_Event *event)
{
    return event->type == SDL_EVENT_KEY_DOWN ||
           event->type == SDL_EVENT_KEY_UP ||
           event->type == SDL_EVENT_TEXT_EDITING ||
           event->type == SDL_EVENT_TEXT_INPUT ||
           event->type == SDL_EVENT_TEXT_EDITING_CANDIDATES;
}

static float overlay_width(float work_width, float fraction, float min_width, float max_width)
{
    const float available_width = SDL_max(1.0f, work_width - 24.0f);
    float width = SDL_min(max_width, work_width * fraction);
    width = SDL_max(min_width, width);
    return SDL_min(width, available_width);
}

bool ForgeGpuInitImGui(ForgeGpuDemo *demo)
{
    ImGui_ImplSDLGPU3_InitInfo init_info;
    ImGuiIO *io;

    demo->imgui_context = ImGui::CreateContext();
    if (!demo->imgui_context) {
        SDL_OutOfMemory();
        return false;
    }

    set_current_imgui_context(demo);
    io = &ImGui::GetIO();
    io->IniFilename = nullptr;
    io->ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();

    if (!ImGui_ImplSDL3_InitForSDLGPU(demo->window)) {
        SDL_SetError("ImGui SDL3 platform init failed");
        return false;
    }
    demo->imgui_platform_initialized = true;

    init_info = ImGui_ImplSDLGPU3_InitInfo();
    init_info.Device = demo->device;
    init_info.ColorTargetFormat = demo->color_format;
    init_info.MSAASamples = SDL_GPU_SAMPLECOUNT_1;
    init_info.SwapchainComposition = SDL_GPU_SWAPCHAINCOMPOSITION_SDR;
    init_info.PresentMode = SDL_GPU_PRESENTMODE_VSYNC;
    if (!ImGui_ImplSDLGPU3_Init(&init_info)) {
        SDL_SetError("ImGui SDL_GPU renderer init failed");
        return false;
    }
    demo->imgui_renderer_initialized = true;
    return true;
}

void ForgeGpuShutdownImGui(ForgeGpuDemo *demo)
{
    if (!demo->imgui_context) {
        return;
    }

    set_current_imgui_context(demo);
    if (demo->imgui_renderer_initialized) {
        ImGui_ImplSDLGPU3_Shutdown();
    }
    if (demo->imgui_platform_initialized) {
        ImGui_ImplSDL3_Shutdown();
    }
    ImGui::DestroyContext(demo->imgui_context);
    demo->imgui_context = nullptr;
    demo->imgui_platform_initialized = false;
    demo->imgui_renderer_initialized = false;
}

void ForgeGpuClearImGuiMouse(ForgeGpuDemo *demo)
{
    ImGuiIO *io;

    if (!demo->imgui_context) {
        return;
    }

    set_current_imgui_context(demo);
    io = &ImGui::GetIO();
    io->ClearInputMouse();
    io->AddMousePosEvent(-FLT_MAX, -FLT_MAX);
}

bool ForgeGpuProcessImGuiEvent(ForgeGpuDemo *demo, const SDL_Event *event)
{
    ImGuiIO *io;

    if (!demo->imgui_context) {
        return false;
    }

    set_current_imgui_context(demo);
    if (demo->lesson.mouse_captured && event_affects_mouse_ui(event)) {
        ForgeGpuClearImGuiMouse(demo);
        return false;
    }

    ImGui_ImplSDL3_ProcessEvent(event);
    io = &ImGui::GetIO();

    if (event_is_mouse_input(event)) {
        return io->WantCaptureMouse;
    }
    if (event_is_keyboard_input(event)) {
        return false;
    }
    return false;
}

static void build_imgui_menu(ForgeGpuDemo *demo)
{
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    const ImVec2 work_pos = viewport ? viewport->WorkPos : ImVec2(0.0f, 0.0f);
    const ImVec2 work_size = viewport ? viewport->WorkSize : ImVec2(960.0f, 540.0f);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoFocusOnAppearing |
                             ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoResize;
    float menu_height = work_size.y - 24.0f;
    float list_height;

    if (demo->validation_mode) {
        return;
    }

    if (menu_height > 240.0f) {
        menu_height = 240.0f;
    }
    if (menu_height < 160.0f) {
        menu_height = 160.0f;
    }
    list_height = menu_height - 84.0f;
    if (list_height < 64.0f) {
        list_height = 64.0f;
    }

    ImGui::SetNextWindowPos(ImVec2(work_pos.x + 12.0f, work_pos.y + 12.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(300.0f, menu_height), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.82f);
    if (ImGui::Begin("Forge GPU Lessons", nullptr, flags)) {
        ImGui::TextWrapped("Active: %s  %s", gForgeGpuLessons[demo->active_lesson].id, gForgeGpuLessons[demo->active_lesson].title);
        ImGui::BeginChild("LessonList", ImVec2(0.0f, list_height), true);
        for (int i = 0; i < gForgeGpuLessonCount; i += 1) {
            char label[80];
            SDL_snprintf(label, sizeof(label), "%s  %s", gForgeGpuLessons[i].id, gForgeGpuLessons[i].title);
            if (ImGui::Selectable(label, demo->active_lesson == i)) {
                demo->pending_lesson = i;
            }
        }
        ImGui::EndChild();
    }
    ImGui::End();
}

static void build_fps_overlay(ForgeGpuDemo *demo)
{
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    const ImVec2 work_pos = viewport ? viewport->WorkPos : ImVec2(0.0f, 0.0f);
    const ImVec2 work_size = viewport ? viewport->WorkSize : ImVec2(960.0f, 540.0f);
    ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize |
                             ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoFocusOnAppearing |
                             ImGuiWindowFlags_NoNav |
                             ImGuiWindowFlags_NoMove;
    FrameStats *stats = &demo->frame_stats;

    if (demo->validation_mode) {
        return;
    }

    ImGui::SetNextWindowPos(
        ImVec2(work_pos.x + work_size.x - 12.0f, work_pos.y + 12.0f),
        ImGuiCond_Always,
        ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowBgAlpha(0.76f);
    if (ImGui::Begin("FPS", nullptr, flags)) {
        ImGui::Text("Average FPS: %.1f", stats->average_fps);
        ImGui::Text("1%% low: %.1f", stats->one_percent_low_fps);
        ImGui::Text("0.1%% low: %.1f", stats->point_one_percent_low_fps);
    }
    ImGui::End();
}

static void build_debug_overlay(ForgeGpuDemo *demo)
{
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    const ImVec2 work_pos = viewport ? viewport->WorkPos : ImVec2(0.0f, 0.0f);
    const ImVec2 work_size = viewport ? viewport->WorkSize : ImVec2(960.0f, 540.0f);
    const float debug_width = overlay_width(work_size.x, 0.38f, 240.0f, 340.0f);
    ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize |
                             ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoFocusOnAppearing |
                             ImGuiWindowFlags_NoNav |
                             ImGuiWindowFlags_NoMove;

    if (demo->validation_mode) {
        return;
    }

    ImGui::SetNextWindowPos(
        ImVec2(work_pos.x + work_size.x - 12.0f, work_pos.y + work_size.y - 12.0f),
        ImGuiCond_Always,
        ImVec2(1.0f, 1.0f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(debug_width, 0.0f), ImVec2(debug_width, 1000.0f));
    ImGui::SetNextWindowBgAlpha(0.76f);
    if (ImGui::Begin("Debug", nullptr, flags)) {
        const LessonDesc *lesson_desc = &gForgeGpuLessons[demo->active_lesson];
        ImGui::PushTextWrapPos(0.0f);
        ImGui::Text("Frame: %u", demo->frame_index);
        if (lesson_desc->debug_ui) {
            ImGui::Separator();
            lesson_desc->debug_ui(demo);
        }
        if (lesson_desc->uses_camera_input && !lesson_desc->custom_camera_debug) {
            ImGui::Separator();
            ImGui::Text("Camera: %.1f %.1f %.1f",
                demo->lesson.camera_position.x,
                demo->lesson.camera_position.y,
                demo->lesson.camera_position.z);
            ImGui::Text("Look: %s", demo->lesson.mouse_captured ? "captured" : "click scene");
        }
        ImGui::PopTextWrapPos();
    }
    ImGui::End();
}

static void build_controls_overlay(ForgeGpuDemo *demo)
{
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    const ImVec2 work_pos = viewport ? viewport->WorkPos : ImVec2(0.0f, 0.0f);
    const ImVec2 work_size = viewport ? viewport->WorkSize : ImVec2(960.0f, 540.0f);
    const float controls_width = overlay_width(work_size.x, 0.46f, 260.0f, 420.0f);
    ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize |
                             ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoFocusOnAppearing |
                             ImGuiWindowFlags_NoNav |
                             ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoTitleBar;

    if (demo->validation_mode) {
        return;
    }

    ImGui::SetNextWindowPos(
        ImVec2(work_pos.x + 12.0f, work_pos.y + work_size.y - 12.0f),
        ImGuiCond_Always,
        ImVec2(0.0f, 1.0f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(controls_width, 0.0f), ImVec2(controls_width, 1000.0f));
    ImGui::SetNextWindowBgAlpha(0.74f);
    if (ImGui::Begin("Controls", nullptr, flags)) {
        const LessonDesc *lesson_desc = &gForgeGpuLessons[demo->active_lesson];
        const float controls_item_width = SDL_max(96.0f, controls_width - 170.0f);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted("Lessons: J previous, K next");
        if (lesson_desc->controls_ui) {
            ImGui::Separator();
            ImGui::PushItemWidth(controls_item_width);
            lesson_desc->controls_ui(demo);
            ImGui::PopItemWidth();
        }
        if (lesson_desc->uses_camera_input) {
            const char *camera_controls_hint = lesson_desc->camera_controls_hint ?
                lesson_desc->camera_controls_hint :
                "Camera: click scene to capture, Esc release, WASD move";

            ImGui::Separator();
            ImGui::TextWrapped("%s", camera_controls_hint);
        }
        ImGui::PopTextWrapPos();
    }
    ImGui::End();
}

bool ForgeGpuPrepareImGui(ForgeGpuDemo *demo, SDL_GPUCommandBuffer *command_buffer, Uint32 width, Uint32 height)
{
    ImDrawData *draw_data;

    (void)width;
    (void)height;

    if (!demo->imgui_context) {
        return true;
    }

    set_current_imgui_context(demo);
    if (demo->lesson.mouse_captured) {
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouse;
        ForgeGpuClearImGuiMouse(demo);
    } else {
        ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
    }
    ImGui_ImplSDLGPU3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    if (demo->lesson.mouse_captured) {
        ForgeGpuClearImGuiMouse(demo);
    }
    ImGui::NewFrame();
    build_imgui_menu(demo);
    build_fps_overlay(demo);
    build_debug_overlay(demo);
    build_controls_overlay(demo);
    ImGui::Render();

    demo->imgui_draw_prepared = true;
    draw_data = ImGui::GetDrawData();
    if (draw_data && draw_data->DisplaySize.x > 0.0f && draw_data->DisplaySize.y > 0.0f) {
        ImGui_ImplSDLGPU3_PrepareDrawData(draw_data, command_buffer);
    }
    return true;
}

bool ForgeGpuRenderImGui(ForgeGpuDemo *demo, SDL_GPUCommandBuffer *command_buffer, SDL_GPUTexture *swapchain_texture)
{
    SDL_GPUColorTargetInfo color_target;
    SDL_GPURenderPass *render_pass;
    ImDrawData *draw_data;

    if (!demo->imgui_context || !demo->imgui_draw_prepared) {
        return true;
    }
    demo->imgui_draw_prepared = false;

    set_current_imgui_context(demo);
    draw_data = ImGui::GetDrawData();
    if (!draw_data || draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f) {
        return true;
    }

    SDL_zero(color_target);
    color_target.texture = swapchain_texture;
    color_target.load_op = SDL_GPU_LOADOP_LOAD;
    color_target.store_op = SDL_GPU_STOREOP_STORE;
    render_pass = SDL_BeginGPURenderPass(command_buffer, &color_target, 1, nullptr);
    if (!render_pass) {
        return false;
    }

    ImGui_ImplSDLGPU3_RenderDrawData(draw_data, command_buffer, render_pass);
    SDL_EndGPURenderPass(render_pass);
    return true;
}
