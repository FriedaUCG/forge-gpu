#include "forge_gpu_camera.h"

#include "forge_gpu_math.h"

static float lesson_delta_seconds(LessonState *lesson)
{
    const Uint64 now = SDL_GetTicks();
    float delta = lesson->last_ticks != 0 ? (float)(now - lesson->last_ticks) / 1000.0f : 0.0f;

    lesson->last_ticks = now;
    if (delta > FORGE_GPU_MAX_DELTA_TIME) {
        delta = FORGE_GPU_MAX_DELTA_TIME;
    }
    return delta;
}

bool ForgeGpuLessonUsesCameraInput(int lesson_index)
{
    if (lesson_index < 0 || lesson_index >= gForgeGpuLessonCount) {
        return false;
    }
    return gForgeGpuLessons[lesson_index].uses_camera_input;
}

static Quat current_camera_orientation(const LessonState *lesson)
{
    return quat_from_euler(lesson->camera_yaw, lesson->camera_pitch, 0.0f);
}

void ForgeGpuUpdateCameraFromInput(ForgeGpuDemo *demo)
{
    LessonState *lesson = &demo->lesson;
    const bool *keys;
    float dt;
    Quat orientation;
    Vec3 forward;
    Vec3 right;

    if (!ForgeGpuLessonUsesCameraInput(demo->active_lesson)) {
        return;
    }

    dt = lesson_delta_seconds(lesson);
    if (demo->validation_mode) {
        return;
    }

    keys = SDL_GetKeyboardState(nullptr);
    orientation = current_camera_orientation(lesson);
    forward = quat_forward(orientation);
    right = quat_right(orientation);

    if (keys[SDL_SCANCODE_W]) {
        lesson->camera_position = vec3_add(lesson->camera_position, vec3_scale(forward, lesson->move_speed * dt));
    }
    if (keys[SDL_SCANCODE_S]) {
        lesson->camera_position = vec3_add(lesson->camera_position, vec3_scale(forward, -lesson->move_speed * dt));
    }
    if (keys[SDL_SCANCODE_D]) {
        lesson->camera_position = vec3_add(lesson->camera_position, vec3_scale(right, lesson->move_speed * dt));
    }
    if (keys[SDL_SCANCODE_A]) {
        lesson->camera_position = vec3_add(lesson->camera_position, vec3_scale(right, -lesson->move_speed * dt));
    }
}

void ForgeGpuCameraViewProjection(ForgeGpuDemo *demo, Uint32 width, Uint32 height, float far_plane, Mat4 *out_view, Mat4 *out_projection)
{
    const float aspect = height > 0 ? (float)width / (float)height : 1.0f;
    const Quat orientation = current_camera_orientation(&demo->lesson);

    *out_view = mat4_view_from_quat(demo->lesson.camera_position, orientation);
    *out_projection = mat4_perspective(60.0f * FORGE_GPU_DEG2RAD, aspect, 0.1f, far_plane);
}
