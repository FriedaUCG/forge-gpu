#include "forge_gpu_lessons.h"

#include "forge_gpu_lesson_common.h"

const char *const gForgeGpuSamplerNames[] = {
    "Trilinear",
    "Bilinear + nearest mip",
    "No mipmaps"
};

const LessonDesc gForgeGpuLessons[] = {
    {
        "01", "Hello window",
        false, false, SDL_GPU_TEXTUREFORMAT_INVALID, { 0.02f, 0.02f, 0.03f, 1.0f },
        ForgeGpuCreateLesson01, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
        nullptr
    },
    {
        "02", "First triangle",
        false, false, SDL_GPU_TEXTUREFORMAT_INVALID, { 0.02f, 0.02f, 0.03f, 1.0f },
        ForgeGpuCreateLesson02, ForgeGpuRenderLesson02, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
        nullptr
    },
    {
        "03", "Uniforms and motion",
        false, false, SDL_GPU_TEXTUREFORMAT_INVALID, { 0.02f, 0.02f, 0.03f, 1.0f },
        ForgeGpuCreateLesson03, ForgeGpuRenderLesson03, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
        nullptr
    },
    {
        "04", "Textures and samplers",
        false, false, SDL_GPU_TEXTUREFORMAT_INVALID, { 0.02f, 0.02f, 0.03f, 1.0f },
        ForgeGpuCreateLesson04, ForgeGpuRenderLesson04, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
        nullptr
    },
    {
        "05", "Mipmaps",
        false, false, SDL_GPU_TEXTUREFORMAT_INVALID, { 0.02f, 0.02f, 0.03f, 1.0f },
        ForgeGpuCreateLesson05, ForgeGpuRenderLesson05, nullptr,
        ForgeGpuDebugLesson05, ForgeGpuControlsLesson05,
        ForgeGpuHandleLesson05Event, ForgeGpuExportLesson05Metrics,
        ForgeGpuDestroyLesson05, nullptr
    },
    {
        "06", "Depth and 3D",
        false, true, SDL_GPU_TEXTUREFORMAT_D16_UNORM, { 0.02f, 0.02f, 0.04f, 1.0f },
        ForgeGpuCreateLesson06, ForgeGpuRenderLesson06, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
        nullptr
    },
    {
        "07", "Camera and input",
        true, true, SDL_GPU_TEXTUREFORMAT_D16_UNORM, { 0.02f, 0.02f, 0.04f, 1.0f },
        ForgeGpuCreateLesson07, ForgeGpuRenderLesson07, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
        nullptr
    },
    {
        "08", "Mesh loading",
        true, true, SDL_GPU_TEXTUREFORMAT_D16_UNORM, { 0.02f, 0.02f, 0.04f, 1.0f },
        ForgeGpuCreateLesson08, ForgeGpuRenderLesson08, nullptr, nullptr, nullptr, nullptr, nullptr,
        ForgeGpuDestroyLesson08, nullptr
    },
    {
        "09", "Scene loading",
        true, true, SDL_GPU_TEXTUREFORMAT_D16_UNORM, { 0.02f, 0.02f, 0.03f, 1.0f },
        ForgeGpuCreateLesson09, ForgeGpuRenderLesson09, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
        nullptr
    },
    {
        "10", "Basic lighting",
        true, true, SDL_GPU_TEXTUREFORMAT_D16_UNORM, { 0.02f, 0.02f, 0.03f, 1.0f },
        ForgeGpuCreateLesson10, ForgeGpuRenderLesson10, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
        nullptr
    },
    {
        "11", "Compute shaders",
        false, false, SDL_GPU_TEXTUREFORMAT_INVALID, { 0.02f, 0.02f, 0.03f, 1.0f },
        ForgeGpuCreateLesson11, nullptr, ForgeGpuRenderLesson11,
        ForgeGpuDebugLesson11, nullptr,
        nullptr, ForgeGpuExportLesson11Metrics, nullptr, nullptr
    },
    {
        "12", "Shader grid",
        true, true, SDL_GPU_TEXTUREFORMAT_D16_UNORM, { 0.0099f, 0.0099f, 0.0267f, 1.0f },
        ForgeGpuCreateLesson12, ForgeGpuRenderLesson12, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
        nullptr
    },
    {
        "13", "Instanced rendering",
        true, true, SDL_GPU_TEXTUREFORMAT_D16_UNORM, { 0.0099f, 0.0099f, 0.0267f, 1.0f },
        ForgeGpuCreateLesson13, ForgeGpuRenderLesson13, nullptr, nullptr, nullptr, nullptr, nullptr,
        ForgeGpuDestroyLesson13, nullptr
    },
    {
        "14", "Environment mapping",
        true, true, SDL_GPU_TEXTUREFORMAT_D16_UNORM, { 0.02f, 0.02f, 0.03f, 1.0f },
        ForgeGpuCreateLesson14, ForgeGpuRenderLesson14, nullptr, nullptr, nullptr, nullptr, nullptr,
        ForgeGpuDestroyLesson14, nullptr
    },
    {
        "15", "Cascaded shadow maps",
        true, false, SDL_GPU_TEXTUREFORMAT_INVALID, { 0.0099f, 0.0099f, 0.0267f, 1.0f },
        ForgeGpuCreateLesson15, nullptr, ForgeGpuRenderLesson15,
        ForgeGpuDebugLesson15, ForgeGpuControlsLesson15, nullptr, nullptr,
        ForgeGpuDestroyLesson15, nullptr
    },
    {
        "16", "Blending",
        true, true, SDL_GPU_TEXTUREFORMAT_D32_FLOAT, { 0.0099f, 0.0099f, 0.0267f, 1.0f },
        ForgeGpuCreateLesson16, ForgeGpuRenderLesson16, nullptr, nullptr, nullptr, nullptr, nullptr,
        ForgeGpuDestroyLesson16, nullptr
    },
    {
        "17", "Normal maps",
        true, true, SDL_GPU_TEXTUREFORMAT_D32_FLOAT, { 0.0099f, 0.0099f, 0.0267f, 1.0f },
        ForgeGpuCreateLesson17, ForgeGpuRenderLesson17, nullptr,
        ForgeGpuDebugLesson17, ForgeGpuControlsLesson17,
        ForgeGpuHandleLesson17Event, nullptr, ForgeGpuDestroyLesson17, nullptr
    },
    {
        "18", "Blinn-Phong materials",
        true, true, SDL_GPU_TEXTUREFORMAT_D32_FLOAT, { 0.0099f, 0.0099f, 0.0267f, 1.0f },
        ForgeGpuCreateLesson18, ForgeGpuRenderLesson18, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
        nullptr
    },
    {
        "19", "Debug lines",
        true, false, SDL_GPU_TEXTUREFORMAT_INVALID, { 0.05f, 0.05f, 0.07f, 1.0f },
        ForgeGpuCreateLesson19, nullptr, ForgeGpuRenderLesson19, nullptr, nullptr, nullptr, nullptr,
        ForgeGpuDestroyLesson19, nullptr
    },
    {
        "20", "Linear fog",
        true, true, SDL_GPU_TEXTUREFORMAT_D32_FLOAT, { 0.5f, 0.5f, 0.5f, 1.0f },
        ForgeGpuCreateLesson20, ForgeGpuRenderLesson20, nullptr,
        ForgeGpuDebugLesson20, ForgeGpuControlsLesson20,
        ForgeGpuHandleLesson20Event, nullptr, ForgeGpuDestroyLesson20, nullptr
    },
    {
        "21", "HDR tone mapping",
        true, false, SDL_GPU_TEXTUREFORMAT_INVALID, { 0.008f, 0.008f, 0.026f, 1.0f },
        ForgeGpuCreateLesson21, nullptr, ForgeGpuRenderLesson21,
        ForgeGpuDebugLesson21, ForgeGpuControlsLesson21,
        ForgeGpuHandleLesson21Event, ForgeGpuExportLesson21Metrics, ForgeGpuDestroyLesson21, nullptr
    },
    {
        "22", "Bloom",
        true, false, SDL_GPU_TEXTUREFORMAT_INVALID, { 0.008f, 0.008f, 0.026f, 1.0f },
        ForgeGpuCreateLesson22, nullptr, ForgeGpuRenderLesson22,
        ForgeGpuDebugLesson22, ForgeGpuControlsLesson22,
        ForgeGpuHandleLesson22Event, ForgeGpuExportLesson22Metrics, ForgeGpuDestroyLesson22, nullptr
    },
    {
        "23", "Point light shadows",
        true, false, SDL_GPU_TEXTUREFORMAT_INVALID, { 0.008f, 0.008f, 0.026f, 1.0f },
        ForgeGpuCreateLesson23, nullptr, ForgeGpuRenderLesson23,
        ForgeGpuDebugLesson23, ForgeGpuControlsLesson23,
        ForgeGpuHandleLesson23Event, ForgeGpuExportLesson23Metrics, ForgeGpuDestroyLesson23, nullptr
    },
    {
        "24", "Gobo spotlight",
        true, false, SDL_GPU_TEXTUREFORMAT_INVALID, { 0.008f, 0.008f, 0.026f, 1.0f },
        ForgeGpuCreateLesson24, nullptr, ForgeGpuRenderLesson24,
        ForgeGpuDebugLesson24, nullptr, nullptr, nullptr, ForgeGpuDestroyLesson24, nullptr
    },
    {
        "25", "Shader noise",
        false, false, SDL_GPU_TEXTUREFORMAT_INVALID, { 0.02f, 0.02f, 0.03f, 1.0f },
        ForgeGpuCreateLesson25, nullptr, ForgeGpuRenderLesson25,
        ForgeGpuDebugLesson25, ForgeGpuControlsLesson25,
        ForgeGpuHandleLesson25Event, ForgeGpuExportLesson25Metrics,
        ForgeGpuDestroyLesson25, nullptr
    },
    {
        "26", "Procedural sky",
        true, false, SDL_GPU_TEXTUREFORMAT_INVALID, { 0.0f, 0.0f, 0.0f, 1.0f },
        ForgeGpuCreateLesson26, nullptr, ForgeGpuRenderLesson26,
        ForgeGpuDebugLesson26, ForgeGpuControlsLesson26,
        ForgeGpuHandleLesson26Event, ForgeGpuExportLesson26Metrics,
        ForgeGpuDestroyLesson26,
        nullptr,
        true
    },
    {
        "27", "SSAO",
        true, false, SDL_GPU_TEXTUREFORMAT_INVALID, { 0.008f, 0.008f, 0.026f, 1.0f },
        ForgeGpuCreateLesson27, nullptr, ForgeGpuRenderLesson27,
        ForgeGpuDebugLesson27, ForgeGpuControlsLesson27,
        ForgeGpuHandleLesson27Event, ForgeGpuExportLesson27Metrics,
        ForgeGpuDestroyLesson27,
        nullptr
    },
    {
        "28", "UI rendering",
        false, false, SDL_GPU_TEXTUREFORMAT_INVALID, { 0.02f, 0.02f, 0.03f, 1.0f },
        ForgeGpuCreateLesson28, nullptr, ForgeGpuRenderLesson28,
        ForgeGpuDebugLesson28, ForgeGpuControlsLesson28,
        ForgeGpuHandleLesson28Event, ForgeGpuExportLesson28Metrics,
        ForgeGpuDestroyLesson28,
        nullptr
    },
    {
        "29", "Screen-space reflections",
        true, false, SDL_GPU_TEXTUREFORMAT_INVALID, { 0.008f, 0.008f, 0.026f, 1.0f },
        ForgeGpuCreateLesson29, nullptr, ForgeGpuRenderLesson29,
        ForgeGpuDebugLesson29, ForgeGpuControlsLesson29,
        ForgeGpuHandleLesson29Event, ForgeGpuExportLesson29Metrics,
        ForgeGpuDestroyLesson29,
        nullptr
    },
    {
        "30", "Planar reflections",
        true, false, SDL_GPU_TEXTUREFORMAT_INVALID, { 0.5f, 0.7f, 0.9f, 1.0f },
        ForgeGpuCreateLesson30, nullptr, ForgeGpuRenderLesson30,
        ForgeGpuDebugLesson30, nullptr, nullptr, ForgeGpuExportLesson30Metrics,
        ForgeGpuDestroyLesson30,
        nullptr
    },
    {
        "31", "Transform animations",
        true, false, SDL_GPU_TEXTUREFORMAT_INVALID, { 0.5f, 0.7f, 0.9f, 1.0f },
        ForgeGpuCreateLesson31, nullptr, ForgeGpuRenderLesson31,
        ForgeGpuDebugLesson31, nullptr, nullptr, ForgeGpuExportLesson31Metrics,
        ForgeGpuDestroyLesson31,
        nullptr
    },
    {
        "32", "Skinning animations",
        true, false, SDL_GPU_TEXTUREFORMAT_INVALID, { 0.6f, 0.7f, 0.8f, 1.0f },
        ForgeGpuCreateLesson32, nullptr, ForgeGpuRenderLesson32,
        ForgeGpuDebugLesson32, nullptr, nullptr, ForgeGpuExportLesson32Metrics,
        ForgeGpuDestroyLesson32,
        nullptr
    },
    {
        "33", "Vertex pulling",
        true, false, SDL_GPU_TEXTUREFORMAT_INVALID, { 0.6f, 0.7f, 0.8f, 1.0f },
        ForgeGpuCreateLesson33, nullptr, ForgeGpuRenderLesson33,
        ForgeGpuDebugLesson33, nullptr, nullptr, ForgeGpuExportLesson33Metrics,
        ForgeGpuDestroyLesson33,
        nullptr
    },
    {
        "34", "Portals and outlines",
        true, false, SDL_GPU_TEXTUREFORMAT_INVALID, { 0.05f, 0.05f, 0.08f, 1.0f },
        ForgeGpuCreateLesson34, nullptr, ForgeGpuRenderLesson34,
        ForgeGpuDebugLesson34, ForgeGpuControlsLesson34, ForgeGpuHandleLesson34Event, ForgeGpuExportLesson34Metrics,
        ForgeGpuDestroyLesson34,
        nullptr
    },
    {
        "35", "Decals",
        true, false, SDL_GPU_TEXTUREFORMAT_INVALID, { 0.05f, 0.05f, 0.08f, 1.0f },
        ForgeGpuCreateLesson35, nullptr, ForgeGpuRenderLesson35,
        ForgeGpuDebugLesson35, nullptr, nullptr, ForgeGpuExportLesson35Metrics,
        ForgeGpuDestroyLesson35,
        nullptr
    },
    {
        "36", "Edge detection",
        true, false, SDL_GPU_TEXTUREFORMAT_INVALID, { 0.05f, 0.05f, 0.08f, 1.0f },
        ForgeGpuCreateLesson36, nullptr, ForgeGpuRenderLesson36,
        ForgeGpuDebugLesson36, ForgeGpuControlsLesson36, ForgeGpuHandleLesson36Event, ForgeGpuExportLesson36Metrics,
        ForgeGpuDestroyLesson36,
        nullptr
    },
    {
        "37", "3D picking",
        true, false, SDL_GPU_TEXTUREFORMAT_INVALID, { 0.05f, 0.05f, 0.08f, 1.0f },
        ForgeGpuCreateLesson37, nullptr, ForgeGpuRenderLesson37,
        ForgeGpuDebugLesson37, ForgeGpuControlsLesson37, ForgeGpuHandleLesson37Event, ForgeGpuExportLesson37Metrics,
        ForgeGpuDestroyLesson37,
        nullptr
    },
    {
        "38", "Indirect drawing",
        true, false, SDL_GPU_TEXTUREFORMAT_INVALID, { 0.02f, 0.02f, 0.03f, 1.0f },
        ForgeGpuCreateLesson38, nullptr, ForgeGpuRenderLesson38,
        ForgeGpuDebugLesson38, ForgeGpuControlsLesson38, ForgeGpuHandleLesson38Event, ForgeGpuExportLesson38Metrics,
        ForgeGpuDestroyLesson38,
        nullptr
    },
    {
        "39", "Pipeline-processed assets",
        true, false, SDL_GPU_TEXTUREFORMAT_INVALID, { 0.02f, 0.02f, 0.03f, 1.0f },
        ForgeGpuCreateLesson39, nullptr, ForgeGpuRenderLesson39,
        ForgeGpuDebugLesson39, ForgeGpuControlsLesson39, ForgeGpuHandleLesson39Event, ForgeGpuExportLesson39Metrics,
        ForgeGpuDestroyLesson39,
        nullptr
    },
    {
        "40", "Scene renderer",
        true, false, SDL_GPU_TEXTUREFORMAT_INVALID, { 0.15f, 0.15f, 0.20f, 1.0f },
        ForgeGpuCreateLesson40, nullptr, ForgeGpuRenderLesson40,
        ForgeGpuDebugLesson40, nullptr, nullptr, ForgeGpuExportLesson40Metrics,
        ForgeGpuDestroyLesson40,
        nullptr
    },
    {
        "41", "Scene model loading",
        true, false, SDL_GPU_TEXTUREFORMAT_INVALID, { 0.15f, 0.15f, 0.20f, 1.0f },
        ForgeGpuCreateLesson41, nullptr, ForgeGpuRenderLesson41,
        ForgeGpuDebugLesson41, nullptr, nullptr, ForgeGpuExportLesson41Metrics,
        ForgeGpuDestroyLesson41,
        nullptr
    },
    {
        "42", "Texture compression",
        true, false, SDL_GPU_TEXTUREFORMAT_INVALID, { 0.15f, 0.15f, 0.20f, 1.0f },
        ForgeGpuCreateLesson42, nullptr, ForgeGpuRenderLesson42,
        ForgeGpuDebugLesson42, nullptr, nullptr, ForgeGpuExportLesson42Metrics,
        ForgeGpuDestroyLesson42,
        nullptr
    },
    {
        "43", "Skinned animations",
        true, false, SDL_GPU_TEXTUREFORMAT_INVALID, { 0.15f, 0.15f, 0.20f, 1.0f },
        ForgeGpuCreateLesson43, nullptr, ForgeGpuRenderLesson43,
        ForgeGpuDebugLesson43, ForgeGpuControlsLesson43, nullptr, ForgeGpuExportLesson43Metrics,
        ForgeGpuDestroyLesson43,
        nullptr
    },
    {
        "44", "Morph animations",
        true, false, SDL_GPU_TEXTUREFORMAT_INVALID, { 0.15f, 0.15f, 0.20f, 1.0f },
        ForgeGpuCreateLesson44, nullptr, ForgeGpuRenderLesson44,
        ForgeGpuDebugLesson44, ForgeGpuControlsLesson44, nullptr, ForgeGpuExportLesson44Metrics,
        ForgeGpuDestroyLesson44,
        nullptr
    },
    {
        "45", "Transparency sorting",
        true, false, SDL_GPU_TEXTUREFORMAT_INVALID, { 0.15f, 0.15f, 0.20f, 1.0f },
        ForgeGpuCreateLesson45, nullptr, ForgeGpuRenderLesson45,
        ForgeGpuDebugLesson45, ForgeGpuControlsLesson45, ForgeGpuHandleLesson45Event, ForgeGpuExportLesson45Metrics,
        ForgeGpuDestroyLesson45,
        nullptr
    },
    {
        "46", "Particle animations",
        true, false, SDL_GPU_TEXTUREFORMAT_INVALID, { 0.02f, 0.02f, 0.03f, 1.0f },
        ForgeGpuCreateLesson46, nullptr, ForgeGpuRenderLesson46,
        ForgeGpuDebugLesson46, ForgeGpuControlsLesson46, ForgeGpuHandleLesson46Event, ForgeGpuExportLesson46Metrics,
        ForgeGpuDestroyLesson46,
        nullptr
    },
    {
        "47", "Texture atlas rendering",
        true, false, SDL_GPU_TEXTUREFORMAT_INVALID, { 0.15f, 0.15f, 0.20f, 1.0f },
        ForgeGpuCreateLesson47, nullptr, ForgeGpuRenderLesson47,
        ForgeGpuDebugLesson47, ForgeGpuControlsLesson47, ForgeGpuHandleLesson47Event, ForgeGpuExportLesson47Metrics,
        ForgeGpuDestroyLesson47,
        nullptr
    },
    {
        "48", "Height map terrain",
        true, false, SDL_GPU_TEXTUREFORMAT_INVALID, { 0.15f, 0.15f, 0.20f, 1.0f },
        ForgeGpuCreateLesson48, nullptr, ForgeGpuRenderLesson48,
        ForgeGpuDebugLesson48, ForgeGpuControlsLesson48, ForgeGpuHandleLesson48Event, ForgeGpuExportLesson48Metrics,
        ForgeGpuDestroyLesson48,
        nullptr
    },
    {
        "49", "Imposters",
        true, false, SDL_GPU_TEXTUREFORMAT_INVALID, { 0.15f, 0.15f, 0.20f, 1.0f },
        ForgeGpuCreateLesson49, nullptr, ForgeGpuRenderLesson49,
        ForgeGpuDebugLesson49, ForgeGpuControlsLesson49, ForgeGpuHandleLesson49Event, ForgeGpuExportLesson49Metrics,
        ForgeGpuDestroyLesson49,
        nullptr
    },
    {
        "50", "Grass rendering",
        true, false, SDL_GPU_TEXTUREFORMAT_INVALID, { 0.15f, 0.15f, 0.20f, 1.0f },
        ForgeGpuCreateLesson50, nullptr, ForgeGpuRenderLesson50,
        ForgeGpuDebugLesson50, ForgeGpuControlsLesson50, ForgeGpuHandleLesson50Event, ForgeGpuExportLesson50Metrics,
        ForgeGpuDestroyLesson50,
        nullptr
    },
    {
        "51", "PBR shading model",
        true, false, SDL_GPU_TEXTUREFORMAT_INVALID, { 0.15f, 0.15f, 0.20f, 1.0f },
        ForgeGpuCreateLesson51, nullptr, ForgeGpuRenderLesson51,
        ForgeGpuDebugLesson51, ForgeGpuControlsLesson51, nullptr, ForgeGpuExportLesson51Metrics,
        ForgeGpuDestroyLesson51,
        nullptr
    },
    {
        "52", "PBR textures",
        true, false, SDL_GPU_TEXTUREFORMAT_INVALID, { 0.15f, 0.15f, 0.20f, 1.0f },
        ForgeGpuCreateLesson52, nullptr, ForgeGpuRenderLesson52,
        ForgeGpuDebugLesson52, nullptr, nullptr, ForgeGpuExportLesson52Metrics,
        ForgeGpuDestroyLesson52,
        nullptr
    }
};

const int gForgeGpuLessonCount = (int)SDL_arraysize(gForgeGpuLessons);

bool ForgeGpuCreateLesson(ForgeGpuDemo *demo, int lesson_index)
{
    if (lesson_index < 0 || lesson_index >= gForgeGpuLessonCount) {
        SDL_SetError("invalid forge-gpu lesson index");
        return false;
    }
    if (!gForgeGpuLessons[lesson_index].create) {
        SDL_SetError("forge-gpu lesson has no create hook");
        return false;
    }
    ForgeGpuDestroyLesson(demo);
    demo->active_lesson = lesson_index;
    if (!gForgeGpuLessons[lesson_index].create(demo)) {
        ForgeGpuDestroyLesson(demo);
        return false;
    }
    return true;
}

void ForgeGpuDestroyLesson(ForgeGpuDemo *demo)
{
    if (!demo) {
        return;
    }
    if (demo->active_lesson >= 0 && demo->active_lesson < gForgeGpuLessonCount) {
        const LessonDesc *lesson_desc = &gForgeGpuLessons[demo->active_lesson];

        if (lesson_desc->destroy) {
            lesson_desc->destroy(demo);
        }
    }
    ForgeGpuDestroySharedLessonResources(demo);
    demo->active_lesson = -1;
}
