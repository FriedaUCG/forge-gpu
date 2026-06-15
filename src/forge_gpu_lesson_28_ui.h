#ifndef SDLGPU_FORGE_GPU_LESSON_28_UI_H
#define SDLGPU_FORGE_GPU_LESSON_28_UI_H

#include <SDL3/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ForgeGpuLesson28Ui ForgeGpuLesson28Ui;

typedef struct ForgeGpuLesson28Vertex {
    float pos_x;
    float pos_y;
    float uv_u;
    float uv_v;
    float r;
    float g;
    float b;
    float a;
} ForgeGpuLesson28Vertex;

typedef struct ForgeGpuLesson28Frame {
    const ForgeGpuLesson28Vertex *vertices;
    const Uint32 *indices;
    Uint32 vertex_count;
    Uint32 index_count;
    Uint32 cached_vertex_count;
    Uint32 cached_index_count;
    Uint32 atlas_width;
    Uint32 atlas_height;
} ForgeGpuLesson28Frame;

ForgeGpuLesson28Ui *ForgeGpuLesson28UiCreate(const char *font_path);
void ForgeGpuLesson28UiDestroy(ForgeGpuLesson28Ui *ui);
bool ForgeGpuLesson28UiHandleEvent(ForgeGpuLesson28Ui *ui, const SDL_Event *event);
bool ForgeGpuLesson28UiBuildFrame(
    ForgeGpuLesson28Ui *ui,
    float mouse_x,
    float mouse_y,
    bool mouse_down,
    bool validation_mode,
    ForgeGpuLesson28Frame *frame);
const Uint8 *ForgeGpuLesson28UiAtlasPixels(const ForgeGpuLesson28Ui *ui);
Uint32 ForgeGpuLesson28UiAtlasWidth(const ForgeGpuLesson28Ui *ui);
Uint32 ForgeGpuLesson28UiAtlasHeight(const ForgeGpuLesson28Ui *ui);

#ifdef __cplusplus
}
#endif

#endif /* SDLGPU_FORGE_GPU_LESSON_28_UI_H */
