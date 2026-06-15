#ifndef SDLGPU_FORGE_GPU_INTERNAL_H
#define SDLGPU_FORGE_GPU_INTERNAL_H


#include <SDL3/SDL.h>

#include <stddef.h>

#include "forge_gpu_assets.h"

struct ImGuiContext;

#ifndef SDLGPU_FORGE_GPU_DEFAULT_ASSET_ROOT
#define SDLGPU_FORGE_GPU_DEFAULT_ASSET_ROOT "assets"
#endif

#define FORGE_GPU_WINDOW_WIDTH 1280
#define FORGE_GPU_WINDOW_HEIGHT 720
#define FORGE_GPU_VALIDATION_COMPLETE_FRAME 36
#define FORGE_GPU_DEFAULT_LESSON_INDEX 0
#define FORGE_GPU_FRAME_TIME_WINDOW 2048
#define FORGE_GPU_FRAME_STATS_REFRESH_INTERVAL 30
#define FORGE_GPU_MAX_STATUS 192
#define FORGE_GPU_MAX_PATH 1024
#define FORGE_GPU_PI 3.14159265358979323846f
#define FORGE_GPU_DEG2RAD (FORGE_GPU_PI / 180.0f)
#define FORGE_GPU_MAX_DELTA_TIME 0.1f
#define FORGE_GPU_PLASMA_SIZE 512u
#define FORGE_GPU_COMPUTE_WORKGROUP_SIZE 8u
#define FORGE_GPU_MAX_SAMPLERS 4
#define FORGE_GPU_SHADOW_CASCADE_COUNT 3
#define FORGE_GPU_SHADOW_MAP_SIZE 2048u

struct Vec3
{
    float x;
    float y;
    float z;
};

struct Mat4
{
    float m[16];
};

struct Quat
{
    float w;
    float x;
    float y;
    float z;
};

struct Vec4
{
    float x;
    float y;
    float z;
    float w;
};

struct LessonVertex2Color
{
    float position[2];
    float color[3];
};

struct LessonVertex2Uv
{
    float position[2];
    float uv[2];
};

struct LessonVertex3Color
{
    float position[3];
    float color[3];
};

struct GridVertex
{
    float position[3];
};

struct CubeInstance
{
    Vec3 position;
    float rotation_speed;
    float scale;
};

struct UniformTimeAspect
{
    float time;
    float aspect;
};

struct UniformMipmap
{
    float time;
    float aspect;
    float uv_scale;
    float pad;
};

struct UniformMvp
{
    Mat4 mvp;
};

struct UniformMvpModel
{
    Mat4 mvp;
    Mat4 model;
};

struct FragMaterialUniforms
{
    float base_color[4];
    Uint32 has_texture;
    Uint32 pad0;
    Uint32 pad1;
    Uint32 pad2;
};

struct FragLightingUniforms
{
    float base_color[4];
    float light_dir[4];
    float eye_pos[4];
    Uint32 has_texture;
    float shininess;
    float ambient;
    float specular_str;
};

struct ComputeUniforms
{
    float time;
    float width;
    float height;
    float pad;
};

struct GridFragUniforms
{
    float line_color[4];
    float bg_color[4];
    float light_dir[4];
    float eye_pos[4];
    float grid_spacing;
    float line_width;
    float fade_distance;
    float ambient;
    float shininess;
    float specular_str;
    float pad0;
    float pad1;
};

struct InstanceData
{
    Mat4 model;
};

struct GpuPrimitive
{
    SDL_GPUBuffer *vertex_buffer;
    SDL_GPUBuffer *index_buffer;
    Uint32 vertex_count;
    Uint32 index_count;
    SDL_GPUIndexElementSize index_type;
    int material_index;
    bool has_bounds;
    Vec3 aabb_min;
    Vec3 aabb_max;
};

struct GpuMaterial
{
    float base_color[4];
    float emissive_factor[3];
    float normal_scale;
    float metallic_factor;
    float roughness_factor;
    float occlusion_strength;
    float alpha_cutoff;
    SDL_GPUTexture *texture;
    SDL_GPUTexture *normal_texture;
    ForgeGpuSceneAlphaMode alpha_mode;
    bool has_texture;
    bool has_normal_map;
    bool has_metallic_roughness;
    bool has_occlusion;
    bool has_emissive;
    bool double_sided;
};

struct GpuSceneTexture
{
    SDL_GPUTexture *texture;
    SDL_GPUTextureFormat format;
    bool generate_mips;
    char path[FORGE_GPU_SCENE_PATH_SIZE];
};

struct GpuSceneData
{
    ForgeGpuLoadedScene loaded;
    GpuPrimitive *primitives;
    int primitive_count;
    GpuMaterial *materials;
    int material_count;
    GpuSceneTexture *textures;
    int texture_count;
    int texture_capacity;
    SDL_GPUBuffer *instance_buffer;
    Uint32 instance_count;
};

struct LessonState
{
    void *private_state;
    SDL_GPUGraphicsPipeline *pipeline;
    SDL_GPUGraphicsPipeline *secondary_pipeline;
    SDL_GPUGraphicsPipeline *tertiary_pipeline;
    SDL_GPUGraphicsPipeline *debug_pipeline;
    SDL_GPUComputePipeline *compute_pipeline;
    SDL_GPUBuffer *vertex_buffer;
    SDL_GPUBuffer *index_buffer;
    SDL_GPUTexture *texture;
    SDL_GPUTexture *white_texture;
    SDL_GPUSampler *samplers[FORGE_GPU_MAX_SAMPLERS];
    SDL_GPUTexture *depth_texture;
    Uint32 depth_width;
    Uint32 depth_height;
    SDL_GPUTextureFormat depth_format;
    ForgeGpuLoadedScene scene;
    GpuPrimitive *gpu_primitives;
    int gpu_primitive_count;
    GpuMaterial *gpu_materials;
    int gpu_material_count;
    GpuSceneTexture *gpu_scene_textures;
    int gpu_scene_texture_count;
    Vec3 camera_position;
    float camera_yaw;
    float camera_pitch;
    float pitch_clamp;
    float mouse_sensitivity;
    float move_speed;
    Uint64 last_ticks;
    Uint64 mouse_capture_started_ticks;
    float mouse_capture_origin_x;
    float mouse_capture_origin_y;
    bool mouse_captured;
    bool mouse_capture_origin_valid;
    bool browser_pointer_lock_seen;
};

struct FrameStats
{
    Uint64 last_counter;
    double frequency;
    double total_seconds;
    Uint64 sample_count;
    float frame_times_ms[FORGE_GPU_FRAME_TIME_WINDOW];
    Uint32 frame_time_count;
    Uint32 frame_time_next;
    Uint32 frames_since_refresh;
    float average_fps;
    float one_percent_low_fps;
    float point_one_percent_low_fps;
    bool initialized;
};

struct AdjustmentKeyRepeatSlot
{
    SDL_Keycode key;
    SDL_Scancode scancode;
    SDL_Keymod mod;
    Uint64 next_repeat_ticks;
    bool active;
    bool requires_shift;
};

struct AdjustmentKeyRepeatState
{
    AdjustmentKeyRepeatSlot slots[8];
};


struct ForgeGpuDemo
{
    SDL_Window *window;
    SDL_GPUDevice *device;
    SDL_GPUTextureFormat swapchain_color_format;
    SDL_GPUTextureFormat color_format;
    SDL_GPUShaderFormat shader_formats;
    const char *asset_root;
    bool validation_mode;
    bool swapchain_sdr_linear_supported;
    bool color_format_overridden;
    bool claimed_window;
    bool quit;
    bool complete;
    Uint32 frame_index;
    Uint64 start_ticks;
    int active_lesson;
    int pending_lesson;
    char status[FORGE_GPU_MAX_STATUS];
    LessonState lesson;
    FrameStats frame_stats;
    AdjustmentKeyRepeatState adjustment_key_repeat;

    ImGuiContext *imgui_context;
    bool imgui_platform_initialized;
    bool imgui_renderer_initialized;
    bool imgui_draw_prepared;
};

typedef void (*ForgeGpuLessonRenderPassFn)(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPURenderPass *render_pass,
    Uint32 width,
    Uint32 height);
typedef bool (*ForgeGpuLessonRenderFrameFn)(
    ForgeGpuDemo *demo,
    SDL_GPUCommandBuffer *command_buffer,
    SDL_GPUTexture *swapchain_texture,
    Uint32 width,
    Uint32 height);
typedef bool (*ForgeGpuLessonCreateFn)(ForgeGpuDemo *demo);
typedef void (*ForgeGpuLessonDestroyFn)(ForgeGpuDemo *demo);
typedef void (*ForgeGpuLessonUiFn)(ForgeGpuDemo *demo);
typedef bool (*ForgeGpuLessonEventFn)(ForgeGpuDemo *demo, const SDL_Event *event);
typedef void (*ForgeGpuLessonMetricsFn)(ForgeGpuDemo *demo);

struct LessonDesc
{
    LessonDesc(
        const char *id_,
        const char *title_,
        bool uses_camera_input_,
        bool needs_depth_,
        SDL_GPUTextureFormat depth_format_,
        SDL_FColor clear_color_,
        ForgeGpuLessonCreateFn create_,
        ForgeGpuLessonRenderPassFn render_pass_,
        ForgeGpuLessonRenderFrameFn render_frame_,
        ForgeGpuLessonUiFn debug_ui_,
        ForgeGpuLessonUiFn controls_ui_,
        ForgeGpuLessonEventFn handle_event_,
        ForgeGpuLessonMetricsFn export_metrics_,
        ForgeGpuLessonDestroyFn destroy_,
        const char *camera_controls_hint_,
        bool custom_camera_debug_ = false) :
        id(id_),
        title(title_),
        uses_camera_input(uses_camera_input_),
        needs_depth(needs_depth_),
        depth_format(depth_format_),
        clear_color(clear_color_),
        create(create_),
        render_pass(render_pass_),
        render_frame(render_frame_),
        debug_ui(debug_ui_),
        controls_ui(controls_ui_),
        handle_event(handle_event_),
        export_metrics(export_metrics_),
        destroy(destroy_),
        camera_controls_hint(camera_controls_hint_),
        custom_camera_debug(custom_camera_debug_)
    {
    }

    const char *id;
    const char *title;
    bool uses_camera_input;
    bool needs_depth;
    SDL_GPUTextureFormat depth_format;
    SDL_FColor clear_color;
    ForgeGpuLessonCreateFn create;
    ForgeGpuLessonRenderPassFn render_pass;
    ForgeGpuLessonRenderFrameFn render_frame;
    ForgeGpuLessonUiFn debug_ui;
    ForgeGpuLessonUiFn controls_ui;
    ForgeGpuLessonEventFn handle_event;
    ForgeGpuLessonMetricsFn export_metrics;
    ForgeGpuLessonDestroyFn destroy;
    const char *camera_controls_hint;
    bool custom_camera_debug;
};

extern const LessonDesc gForgeGpuLessons[];
extern const int gForgeGpuLessonCount;
extern const char *const gForgeGpuSamplerNames[];

#endif /* SDLGPU_FORGE_GPU_INTERNAL_H */
