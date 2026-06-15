#include "forge_gpu_lesson_28_ui.h"

#include "third_party/forge_gpu/common/ui/forge_ui.h"
#include "third_party/forge_gpu/common/ui/forge_ui_ctx.h"
#include "third_party/forge_gpu/common/ui/forge_ui_window.h"

#include <stddef.h>

#define LESSON28_ATLAS_PIXEL_HEIGHT 16.0f
#define LESSON28_ATLAS_PADDING 1
#define LESSON28_ASCII_START 32
#define LESSON28_ASCII_END 126
#define LESSON28_ASCII_COUNT (LESSON28_ASCII_END - LESSON28_ASCII_START + 1)
#define LESSON28_CONTROLS_WIN_X 20.0f
#define LESSON28_CONTROLS_WIN_Y 20.0f
#define LESSON28_CONTROLS_WIN_W 300.0f
#define LESSON28_CONTROLS_WIN_H 680.0f
#define LESSON28_INSPECTOR_WIN_X 340.0f
#define LESSON28_INSPECTOR_WIN_Y 20.0f
#define LESSON28_INSPECTOR_WIN_W 340.0f
#define LESSON28_INSPECTOR_WIN_H 680.0f
#define LESSON28_PERF_WIN_X 700.0f
#define LESSON28_PERF_WIN_Y 20.0f
#define LESSON28_PERF_WIN_W 280.0f
#define LESSON28_PERF_WIN_H 420.0f
#define LESSON28_GRAPH_WIN_X 700.0f
#define LESSON28_GRAPH_WIN_Y 460.0f
#define LESSON28_GRAPH_WIN_W 560.0f
#define LESSON28_GRAPH_WIN_H 240.0f
#define LESSON28_BIG_SPARKLINE_SAMPLES 120
#define LESSON28_BIG_SPARKLINE_HEIGHT 180.0f
#define LESSON28_SIGNAL_PHASE_STEP 0.08f
#define LESSON28_SIGNAL_SEED_STEP 0.15f
#define LESSON28_LABEL_HEIGHT 26.0f
#define LESSON28_BUTTON_HEIGHT 36.0f
#define LESSON28_CHECKBOX_HEIGHT 30.0f
#define LESSON28_SLIDER_HEIGHT 30.0f
#define LESSON28_TEXT_INPUT_HEIGHT 32.0f
#define LESSON28_SEPARATOR_HEIGHT 12.0f
#define LESSON28_PROGRESS_HEIGHT 22.0f
#define LESSON28_RADIO_HEIGHT 26.0f
#define LESSON28_DRAG_HEIGHT 26.0f
#define LESSON28_LISTBOX_HEIGHT 100.0f
#define LESSON28_DROPDOWN_HEIGHT 26.0f
#define LESSON28_SPARKLINE_HEIGHT 50.0f
#define LESSON28_PICKER_HEIGHT 160.0f
#define LESSON28_TEXT_INPUT_BUF_SIZE 128
#define LESSON28_CURSOR_BLINK_INTERVAL_MS 530u
#define LESSON28_LABEL_BUF_SIZE 64
#define LESSON28_SPARKLINE_SAMPLES 60

SDL_COMPILE_TIME_ASSERT(lesson28_vertex_size, sizeof(ForgeGpuLesson28Vertex) == sizeof(ForgeUiVertex));
SDL_COMPILE_TIME_ASSERT(lesson28_vertex_pos_x, offsetof(ForgeGpuLesson28Vertex, pos_x) == offsetof(ForgeUiVertex, pos_x));
SDL_COMPILE_TIME_ASSERT(lesson28_vertex_uv_u, offsetof(ForgeGpuLesson28Vertex, uv_u) == offsetof(ForgeUiVertex, uv_u));
SDL_COMPILE_TIME_ASSERT(lesson28_vertex_r, offsetof(ForgeGpuLesson28Vertex, r) == offsetof(ForgeUiVertex, r));
SDL_COMPILE_TIME_ASSERT(lesson28_vertex_b, offsetof(ForgeGpuLesson28Vertex, b) == offsetof(ForgeUiVertex, b));

struct ForgeGpuLesson28Ui {
    ForgeUiFont font;
    ForgeUiFontAtlas atlas;
    ForgeUiContext ui_ctx;
    ForgeUiWindowContext ui_wctx;

    ForgeUiWindowState controls_window;
    float slider_value;
    bool checkbox_value;
    ForgeUiTextInputState text_input;
    char text_buf[LESSON28_TEXT_INPUT_BUF_SIZE];
    int click_count;
    float health;
    float mana;
    int radio_value;
    int listbox_sel;
    int dropdown_sel;
    bool dropdown_open;

    ForgeUiWindowState inspector_window;
    bool transform_open;
    bool material_open;
    bool physics_open;
    float position[3];
    float rotation[3];
    float scale_val;
    float roughness;
    int layer;
    float picker_h;
    float picker_s;
    float picker_v;

    ForgeUiWindowState perf_window;
    float frame_times[LESSON28_SPARKLINE_SAMPLES];
    int frame_time_index;
    Uint64 last_tick;

    ForgeUiWindowState graph_window;
    float big_spark[LESSON28_BIG_SPARKLINE_SAMPLES];
    float big_spark_phase;

    int cached_vertex_count;
    int cached_index_count;

    char frame_text_buf[64];
    bool frame_key_backspace;
    bool frame_key_delete;
    bool frame_key_left;
    bool frame_key_right;
    bool frame_key_home;
    bool frame_key_end;
    bool frame_key_escape;
    float frame_scroll_delta;

    bool context_initialized;
    bool window_context_initialized;
};

static bool lesson28_text_input_focused(const ForgeGpuLesson28Ui *ui)
{
    return ui && ui->ui_ctx.focused != FORGE_UI_ID_NONE;
}

static void lesson28_reset_frame_input(ForgeGpuLesson28Ui *ui)
{
    ui->frame_text_buf[0] = '\0';
    ui->frame_key_backspace = false;
    ui->frame_key_delete = false;
    ui->frame_key_left = false;
    ui->frame_key_right = false;
    ui->frame_key_home = false;
    ui->frame_key_end = false;
    ui->frame_key_escape = false;
    ui->frame_scroll_delta = 0.0f;
}

static void lesson28_seed_signal(ForgeGpuLesson28Ui *ui)
{
    int i;

    for (i = 0; i < LESSON28_BIG_SPARKLINE_SAMPLES; i += 1) {
        const float t = (float)i * LESSON28_SIGNAL_SEED_STEP;
        ui->big_spark[i] =
            SDL_sinf(t * 1.0f) * 0.4f +
            SDL_sinf(t * 2.7f) * 0.2f +
            SDL_sinf(t * 5.3f) * 0.15f +
            0.5f;
    }
}

ForgeGpuLesson28Ui *ForgeGpuLesson28UiCreate(const char *font_path)
{
    ForgeGpuLesson28Ui *ui;
    Uint32 codepoints[LESSON28_ASCII_COUNT];
    int i;

    if (!font_path) {
        SDL_InvalidParamError("font_path");
        return NULL;
    }

    ui = (ForgeGpuLesson28Ui *)SDL_calloc(1, sizeof(*ui));
    if (!ui) {
        SDL_OutOfMemory();
        return NULL;
    }

    if (!forge_ui_ttf_load(font_path, &ui->font)) {
        SDL_SetError("forge_ui_ttf_load failed for '%s'", font_path);
        ForgeGpuLesson28UiDestroy(ui);
        return NULL;
    }

    for (i = 0; i < LESSON28_ASCII_COUNT; i += 1) {
        codepoints[i] = (Uint32)(LESSON28_ASCII_START + i);
    }
    if (!forge_ui_atlas_build(&ui->font, LESSON28_ATLAS_PIXEL_HEIGHT, codepoints, LESSON28_ASCII_COUNT, LESSON28_ATLAS_PADDING, &ui->atlas)) {
        SDL_SetError("forge_ui_atlas_build failed");
        ForgeGpuLesson28UiDestroy(ui);
        return NULL;
    }
    if (!forge_ui_ctx_init(&ui->ui_ctx, &ui->atlas)) {
        SDL_SetError("forge_ui_ctx_init failed");
        ForgeGpuLesson28UiDestroy(ui);
        return NULL;
    }
    ui->context_initialized = true;
    ui->ui_ctx.scale = 1.0f;
    ui->ui_ctx.base_pixel_height = LESSON28_ATLAS_PIXEL_HEIGHT;
    ui->ui_ctx.scaled_pixel_height = LESSON28_ATLAS_PIXEL_HEIGHT;

    if (!forge_ui_wctx_init(&ui->ui_wctx, &ui->ui_ctx)) {
        SDL_SetError("forge_ui_wctx_init failed");
        ForgeGpuLesson28UiDestroy(ui);
        return NULL;
    }
    ui->window_context_initialized = true;

    ui->controls_window = forge_ui_window_state_default(
        LESSON28_CONTROLS_WIN_X, LESSON28_CONTROLS_WIN_Y,
        LESSON28_CONTROLS_WIN_W, LESSON28_CONTROLS_WIN_H);
    ui->inspector_window = forge_ui_window_state_default(
        LESSON28_INSPECTOR_WIN_X, LESSON28_INSPECTOR_WIN_Y,
        LESSON28_INSPECTOR_WIN_W, LESSON28_INSPECTOR_WIN_H);
    ui->perf_window = forge_ui_window_state_default(
        LESSON28_PERF_WIN_X, LESSON28_PERF_WIN_Y,
        LESSON28_PERF_WIN_W, LESSON28_PERF_WIN_H);
    ui->graph_window = forge_ui_window_state_default(
        LESSON28_GRAPH_WIN_X, LESSON28_GRAPH_WIN_Y,
        LESSON28_GRAPH_WIN_W, LESSON28_GRAPH_WIN_H);
    ui->inspector_window.z_order = 1;
    ui->perf_window.z_order = 2;
    ui->graph_window.z_order = 3;

    ui->slider_value = 0.5f;
    ui->health = 72.0f;
    ui->mana = 45.0f;
    ui->radio_value = 1;
    ui->transform_open = true;
    ui->material_open = true;
    ui->position[0] = 12.5f;
    ui->position[2] = -3.2f;
    ui->rotation[1] = 45.0f;
    ui->scale_val = 1.0f;
    ui->roughness = 0.7f;
    ui->picker_h = 0.6f;
    ui->picker_s = 0.8f;
    ui->picker_v = 0.9f;
    ui->text_input.buffer = ui->text_buf;
    ui->text_input.capacity = LESSON28_TEXT_INPUT_BUF_SIZE;

    for (i = 0; i < LESSON28_SPARKLINE_SAMPLES; i += 1) {
        ui->frame_times[i] = 16.0f;
    }
    ui->last_tick = SDL_GetTicks();
    lesson28_seed_signal(ui);
    return ui;
}

void ForgeGpuLesson28UiDestroy(ForgeGpuLesson28Ui *ui)
{
    if (!ui) {
        return;
    }
    if (ui->window_context_initialized) {
        forge_ui_wctx_free(&ui->ui_wctx);
    }
    if (ui->context_initialized) {
        forge_ui_ctx_free(&ui->ui_ctx);
    }
    forge_ui_atlas_free(&ui->atlas);
    forge_ui_ttf_free(&ui->font);
    SDL_free(ui);
}

bool ForgeGpuLesson28UiHandleEvent(ForgeGpuLesson28Ui *ui, const SDL_Event *event)
{
    if (!ui || !event) {
        return false;
    }

    switch (event->type) {
    case SDL_EVENT_KEY_DOWN:
        if (event->key.repeat) {
            return lesson28_text_input_focused(ui);
        }
        switch (event->key.scancode) {
        case SDL_SCANCODE_BACKSPACE:
            ui->frame_key_backspace = true;
            return lesson28_text_input_focused(ui);
        case SDL_SCANCODE_DELETE:
            ui->frame_key_delete = true;
            return lesson28_text_input_focused(ui);
        case SDL_SCANCODE_LEFT:
            ui->frame_key_left = true;
            return lesson28_text_input_focused(ui);
        case SDL_SCANCODE_RIGHT:
            ui->frame_key_right = true;
            return lesson28_text_input_focused(ui);
        case SDL_SCANCODE_HOME:
            ui->frame_key_home = true;
            return lesson28_text_input_focused(ui);
        case SDL_SCANCODE_END:
            ui->frame_key_end = true;
            return lesson28_text_input_focused(ui);
        case SDL_SCANCODE_ESCAPE:
            ui->frame_key_escape = true;
            return lesson28_text_input_focused(ui);
        default:
            return lesson28_text_input_focused(ui);
        }

    case SDL_EVENT_TEXT_INPUT: {
        const size_t cur = SDL_strlen(ui->frame_text_buf);
        const size_t add = SDL_strlen(event->text.text);
        if (cur + add < sizeof(ui->frame_text_buf)) {
            SDL_memcpy(ui->frame_text_buf + cur, event->text.text, add + 1);
        }
        return lesson28_text_input_focused(ui);
    }

    case SDL_EVENT_MOUSE_WHEEL:
        ui->frame_scroll_delta += event->wheel.y;
        return false;

    default:
        return false;
    }
}

static void lesson28_update_sparklines(ForgeGpuLesson28Ui *ui, bool validation_mode)
{
    float dt_ms;
    int i;

    if (validation_mode) {
        dt_ms = 16.0f;
    } else {
        const Uint64 now = SDL_GetTicks();
        dt_ms = (float)(now - ui->last_tick);
        ui->last_tick = now;
    }

    ui->frame_times[ui->frame_time_index] = dt_ms;
    ui->frame_time_index = (ui->frame_time_index + 1) % LESSON28_SPARKLINE_SAMPLES;

    for (i = 0; i < LESSON28_BIG_SPARKLINE_SAMPLES - 1; i += 1) {
        ui->big_spark[i] = ui->big_spark[i + 1];
    }
    ui->big_spark_phase += LESSON28_SIGNAL_PHASE_STEP;
    {
        const float p = ui->big_spark_phase;
        ui->big_spark[LESSON28_BIG_SPARKLINE_SAMPLES - 1] =
            SDL_sinf(p) * 0.35f +
            SDL_sinf(p * 2.3f) * 0.2f +
            SDL_sinf(p * 5.7f) * 0.15f +
            SDL_sinf(p * 11.1f) * 0.08f +
            0.5f;
    }
}

static void lesson28_controls_window(ForgeGpuLesson28Ui *ui, bool validation_mode)
{
    char label[LESSON28_LABEL_BUF_SIZE];
    ForgeUiColor health_color = { 0.2f, 0.8f, 0.3f, 1.0f };
    ForgeUiColor mana_color = { 0.3f, 0.4f, 0.9f, 1.0f };
    static const char *modes[] = { "Low", "Medium", "High" };

    if (!forge_ui_wctx_window_begin(&ui->ui_wctx, "Controls", &ui->controls_window)) {
        return;
    }

    forge_ui_ctx_label_colored_layout(&ui->ui_ctx, "Hello, GPU UI!", LESSON28_LABEL_HEIGHT, ui->ui_ctx.theme.accent.r, ui->ui_ctx.theme.accent.g, ui->ui_ctx.theme.accent.b, ui->ui_ctx.theme.accent.a);
    if (forge_ui_ctx_button_layout(&ui->ui_ctx, "Click me", LESSON28_BUTTON_HEIGHT)) {
        ui->click_count += 1;
    }
    SDL_snprintf(label, sizeof(label), "Clicks: %d", ui->click_count);
    forge_ui_ctx_label_colored_layout(&ui->ui_ctx, label, LESSON28_LABEL_HEIGHT, ui->ui_ctx.theme.text_dim.r, ui->ui_ctx.theme.text_dim.g, ui->ui_ctx.theme.text_dim.b, ui->ui_ctx.theme.text_dim.a);

    forge_ui_ctx_separator_layout(&ui->ui_ctx, LESSON28_SEPARATOR_HEIGHT);
    forge_ui_ctx_checkbox_layout(&ui->ui_ctx, "Toggle option", &ui->checkbox_value, LESSON28_CHECKBOX_HEIGHT);
    forge_ui_ctx_slider_layout(&ui->ui_ctx, "##slider", &ui->slider_value, 0.0f, 1.0f, LESSON28_SLIDER_HEIGHT);
    SDL_snprintf(label, sizeof(label), "Value: %.2f", (double)ui->slider_value);
    forge_ui_ctx_label_colored_layout(&ui->ui_ctx, label, LESSON28_LABEL_HEIGHT, ui->ui_ctx.theme.text_dim.r, ui->ui_ctx.theme.text_dim.g, ui->ui_ctx.theme.text_dim.b, ui->ui_ctx.theme.text_dim.a);

    forge_ui_ctx_separator_layout(&ui->ui_ctx, LESSON28_SEPARATOR_HEIGHT);
    forge_ui_ctx_label_layout(&ui->ui_ctx, "Health", LESSON28_LABEL_HEIGHT);
    forge_ui_ctx_progress_bar_layout(&ui->ui_ctx, ui->health, 100.0f, health_color, LESSON28_PROGRESS_HEIGHT);
    forge_ui_ctx_label_layout(&ui->ui_ctx, "Mana", LESSON28_LABEL_HEIGHT);
    forge_ui_ctx_progress_bar_layout(&ui->ui_ctx, ui->mana, 80.0f, mana_color, LESSON28_PROGRESS_HEIGHT);

    forge_ui_ctx_separator_layout(&ui->ui_ctx, LESSON28_SEPARATOR_HEIGHT);
    {
        ForgeUiRect rect = forge_ui_ctx_layout_next(&ui->ui_ctx, LESSON28_TEXT_INPUT_HEIGHT);
        const Uint64 ticks = validation_mode ? 0u : SDL_GetTicks();
        const bool cursor_visible = validation_mode || ((ticks / LESSON28_CURSOR_BLINK_INTERVAL_MS) % 2u) == 0u;
        forge_ui_ctx_text_input(&ui->ui_ctx, "##text_input", &ui->text_input, rect, cursor_visible);
    }

    forge_ui_ctx_separator_layout(&ui->ui_ctx, LESSON28_SEPARATOR_HEIGHT);
    forge_ui_ctx_label_layout(&ui->ui_ctx, "Quality:", LESSON28_LABEL_HEIGHT);
    forge_ui_ctx_radio_layout(&ui->ui_ctx, modes[0], &ui->radio_value, 0, LESSON28_RADIO_HEIGHT);
    forge_ui_ctx_radio_layout(&ui->ui_ctx, modes[1], &ui->radio_value, 1, LESSON28_RADIO_HEIGHT);
    forge_ui_ctx_radio_layout(&ui->ui_ctx, modes[2], &ui->radio_value, 2, LESSON28_RADIO_HEIGHT);

    forge_ui_wctx_window_end(&ui->ui_wctx);
}

static void lesson28_inspector_window(ForgeGpuLesson28Ui *ui)
{
    static const char *render_modes[] = { "Shaded", "Wireframe", "Normals", "Depth" };
    static const char *shaders[] = { "Blinn-Phong", "PBR", "Toon", "Unlit" };

    if (!forge_ui_wctx_window_begin(&ui->ui_wctx, "Inspector", &ui->inspector_window)) {
        return;
    }

    if (forge_ui_ctx_tree_push_layout(&ui->ui_ctx, "Transform", &ui->transform_open, LESSON28_LABEL_HEIGHT)) {
        forge_ui_ctx_label_colored_layout(&ui->ui_ctx, "Position", LESSON28_LABEL_HEIGHT, ui->ui_ctx.theme.text_dim.r, ui->ui_ctx.theme.text_dim.g, ui->ui_ctx.theme.text_dim.b, ui->ui_ctx.theme.text_dim.a);
        forge_ui_ctx_drag_float_n_layout(&ui->ui_ctx, "##pos", ui->position, 3, 0.1f, -100.0f, 100.0f, LESSON28_DRAG_HEIGHT);
        forge_ui_ctx_label_colored_layout(&ui->ui_ctx, "Rotation", LESSON28_LABEL_HEIGHT, ui->ui_ctx.theme.text_dim.r, ui->ui_ctx.theme.text_dim.g, ui->ui_ctx.theme.text_dim.b, ui->ui_ctx.theme.text_dim.a);
        forge_ui_ctx_drag_float_n_layout(&ui->ui_ctx, "##rot", ui->rotation, 3, 1.0f, -360.0f, 360.0f, LESSON28_DRAG_HEIGHT);
        forge_ui_ctx_label_colored_layout(&ui->ui_ctx, "Scale", LESSON28_LABEL_HEIGHT, ui->ui_ctx.theme.text_dim.r, ui->ui_ctx.theme.text_dim.g, ui->ui_ctx.theme.text_dim.b, ui->ui_ctx.theme.text_dim.a);
        forge_ui_ctx_drag_float_layout(&ui->ui_ctx, "##scale", &ui->scale_val, 0.01f, 0.01f, 10.0f, LESSON28_DRAG_HEIGHT);
        forge_ui_ctx_separator_layout(&ui->ui_ctx, LESSON28_SEPARATOR_HEIGHT);
    }
    forge_ui_ctx_tree_pop(&ui->ui_ctx);

    if (forge_ui_ctx_tree_push_layout(&ui->ui_ctx, "Material", &ui->material_open, LESSON28_LABEL_HEIGHT)) {
        forge_ui_ctx_label_colored_layout(&ui->ui_ctx, "Roughness", LESSON28_LABEL_HEIGHT, ui->ui_ctx.theme.text_dim.r, ui->ui_ctx.theme.text_dim.g, ui->ui_ctx.theme.text_dim.b, ui->ui_ctx.theme.text_dim.a);
        forge_ui_ctx_drag_float_layout(&ui->ui_ctx, "##rough", &ui->roughness, 0.01f, 0.0f, 1.0f, LESSON28_DRAG_HEIGHT);
        forge_ui_ctx_label_colored_layout(&ui->ui_ctx, "Layer", LESSON28_LABEL_HEIGHT, ui->ui_ctx.theme.text_dim.r, ui->ui_ctx.theme.text_dim.g, ui->ui_ctx.theme.text_dim.b, ui->ui_ctx.theme.text_dim.a);
        forge_ui_ctx_drag_int_layout(&ui->ui_ctx, "##layer", &ui->layer, 1, 0, 31, LESSON28_DRAG_HEIGHT);
        forge_ui_ctx_separator_layout(&ui->ui_ctx, LESSON28_SEPARATOR_HEIGHT);
        forge_ui_ctx_label_layout(&ui->ui_ctx, "Color", LESSON28_LABEL_HEIGHT);
        forge_ui_ctx_color_picker_layout(&ui->ui_ctx, "##picker", &ui->picker_h, &ui->picker_s, &ui->picker_v, LESSON28_PICKER_HEIGHT);
        forge_ui_ctx_separator_layout(&ui->ui_ctx, LESSON28_SEPARATOR_HEIGHT);
    }
    forge_ui_ctx_tree_pop(&ui->ui_ctx);

    forge_ui_ctx_tree_push_layout(&ui->ui_ctx, "Physics", &ui->physics_open, LESSON28_LABEL_HEIGHT);
    forge_ui_ctx_tree_pop(&ui->ui_ctx);
    forge_ui_ctx_separator_layout(&ui->ui_ctx, LESSON28_SEPARATOR_HEIGHT);
    forge_ui_ctx_label_layout(&ui->ui_ctx, "Render Mode:", LESSON28_LABEL_HEIGHT);
    forge_ui_ctx_dropdown_layout(&ui->ui_ctx, "##mode", &ui->dropdown_sel, &ui->dropdown_open, render_modes, 4, LESSON28_DROPDOWN_HEIGHT);
    forge_ui_ctx_separator_layout(&ui->ui_ctx, LESSON28_SEPARATOR_HEIGHT);
    forge_ui_ctx_label_layout(&ui->ui_ctx, "Shader:", LESSON28_LABEL_HEIGHT);
    forge_ui_ctx_listbox_layout(&ui->ui_ctx, "##shaders", &ui->listbox_sel, shaders, 4, LESSON28_LISTBOX_HEIGHT);

    forge_ui_wctx_window_end(&ui->ui_wctx);
}

static void lesson28_performance_window(ForgeGpuLesson28Ui *ui)
{
    char label[LESSON28_LABEL_BUF_SIZE];
    ForgeUiColor spark_color = { 0.3f, 0.8f, 0.5f, 1.0f };
    ForgeUiColor mem_color = { 0.9f, 0.6f, 0.2f, 1.0f };
    const float last_dt = ui->frame_times[(ui->frame_time_index + LESSON28_SPARKLINE_SAMPLES - 1) % LESSON28_SPARKLINE_SAMPLES];
    const float fps = last_dt > 0.001f ? 1000.0f / last_dt : 0.0f;

    if (!forge_ui_wctx_window_begin(&ui->ui_wctx, "Performance", &ui->perf_window)) {
        return;
    }

    SDL_snprintf(label, sizeof(label), "FPS: %.0f", (double)fps);
    forge_ui_ctx_label_colored_layout(&ui->ui_ctx, label, LESSON28_LABEL_HEIGHT, ui->ui_ctx.theme.accent.r, ui->ui_ctx.theme.accent.g, ui->ui_ctx.theme.accent.b, ui->ui_ctx.theme.accent.a);
    forge_ui_ctx_label_colored_layout(&ui->ui_ctx, "Frame Time (ms)", LESSON28_LABEL_HEIGHT, ui->ui_ctx.theme.text_dim.r, ui->ui_ctx.theme.text_dim.g, ui->ui_ctx.theme.text_dim.b, ui->ui_ctx.theme.text_dim.a);
    forge_ui_ctx_sparkline_layout(&ui->ui_ctx, ui->frame_times, LESSON28_SPARKLINE_SAMPLES, 0.0f, 33.0f, spark_color, LESSON28_SPARKLINE_HEIGHT);
    forge_ui_ctx_separator_layout(&ui->ui_ctx, LESSON28_SEPARATOR_HEIGHT);
    forge_ui_ctx_label_colored_layout(&ui->ui_ctx, "Draw calls: 1", LESSON28_LABEL_HEIGHT, 0.5f, 0.9f, 0.5f, 1.0f);
    SDL_snprintf(label, sizeof(label), "Triangles: %d", ui->cached_index_count / 3);
    forge_ui_ctx_label_colored_layout(&ui->ui_ctx, label, LESSON28_LABEL_HEIGHT, 0.5f, 0.9f, 0.5f, 1.0f);
    SDL_snprintf(label, sizeof(label), "Vertices: %d", ui->cached_vertex_count);
    forge_ui_ctx_label_colored_layout(&ui->ui_ctx, label, LESSON28_LABEL_HEIGHT, 0.5f, 0.9f, 0.5f, 1.0f);
    forge_ui_ctx_separator_layout(&ui->ui_ctx, LESSON28_SEPARATOR_HEIGHT);
    forge_ui_ctx_label_layout(&ui->ui_ctx, "GPU Memory", LESSON28_LABEL_HEIGHT);
    forge_ui_ctx_progress_bar_layout(&ui->ui_ctx, 256.0f, 1024.0f, mem_color, LESSON28_PROGRESS_HEIGHT);

    forge_ui_wctx_window_end(&ui->ui_wctx);
}

static void lesson28_graph_window(ForgeGpuLesson28Ui *ui)
{
    ForgeUiColor blue_line = { 0.3f, 0.55f, 1.0f, 1.0f };

    if (!forge_ui_wctx_window_begin(&ui->ui_wctx, "Signal Monitor", &ui->graph_window)) {
        return;
    }
    forge_ui_ctx_sparkline_layout(&ui->ui_ctx, ui->big_spark, LESSON28_BIG_SPARKLINE_SAMPLES, 0.0f, 1.0f, blue_line, LESSON28_BIG_SPARKLINE_HEIGHT);
    forge_ui_wctx_window_end(&ui->ui_wctx);
}

bool ForgeGpuLesson28UiBuildFrame(
    ForgeGpuLesson28Ui *ui,
    float mouse_x,
    float mouse_y,
    bool mouse_down,
    bool validation_mode,
    ForgeGpuLesson28Frame *frame)
{
    if (!ui || !frame) {
        SDL_InvalidParamError("ForgeGpuLesson28UiBuildFrame");
        return false;
    }

    if (validation_mode) {
        mouse_x = -1.0f;
        mouse_y = -1.0f;
        mouse_down = false;
        lesson28_reset_frame_input(ui);
    }

    forge_ui_ctx_begin(&ui->ui_ctx, mouse_x, mouse_y, mouse_down);
    ui->ui_ctx.scroll_delta = ui->frame_scroll_delta;
    forge_ui_ctx_set_keyboard(
        &ui->ui_ctx,
        ui->frame_text_buf[0] ? ui->frame_text_buf : NULL,
        ui->frame_key_backspace,
        ui->frame_key_delete,
        ui->frame_key_left,
        ui->frame_key_right,
        ui->frame_key_home,
        ui->frame_key_end,
        ui->frame_key_escape);

    forge_ui_wctx_begin(&ui->ui_wctx);
    lesson28_update_sparklines(ui, validation_mode);
    lesson28_controls_window(ui, validation_mode);
    lesson28_inspector_window(ui);
    lesson28_performance_window(ui);
    lesson28_graph_window(ui);
    forge_ui_wctx_end(&ui->ui_wctx);
    forge_ui_ctx_end(&ui->ui_ctx);

    ui->cached_vertex_count = ui->ui_ctx.vertex_count;
    ui->cached_index_count = ui->ui_ctx.index_count;
    lesson28_reset_frame_input(ui);

    SDL_zero(*frame);
    frame->vertices = (const ForgeGpuLesson28Vertex *)ui->ui_ctx.vertices;
    frame->indices = ui->ui_ctx.indices;
    frame->vertex_count = (Uint32)ui->ui_ctx.vertex_count;
    frame->index_count = (Uint32)ui->ui_ctx.index_count;
    frame->cached_vertex_count = (Uint32)ui->cached_vertex_count;
    frame->cached_index_count = (Uint32)ui->cached_index_count;
    frame->atlas_width = (Uint32)ui->atlas.width;
    frame->atlas_height = (Uint32)ui->atlas.height;
    return true;
}

const Uint8 *ForgeGpuLesson28UiAtlasPixels(const ForgeGpuLesson28Ui *ui)
{
    return ui ? ui->atlas.pixels : NULL;
}

Uint32 ForgeGpuLesson28UiAtlasWidth(const ForgeGpuLesson28Ui *ui)
{
    return ui ? (Uint32)ui->atlas.width : 0u;
}

Uint32 ForgeGpuLesson28UiAtlasHeight(const ForgeGpuLesson28Ui *ui)
{
    return ui ? (Uint32)ui->atlas.height : 0u;
}
