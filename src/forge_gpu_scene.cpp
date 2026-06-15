#include "forge_gpu_scene.h"

#include "forge_gpu_gpu_helpers.h"

static void free_scene_gpu_resources(ForgeGpuDemo *demo, GpuSceneData *scene)
{
    if (scene->primitives) {
        for (int i = 0; i < scene->primitive_count; i += 1) {
            if (scene->primitives[i].index_buffer) {
                SDL_ReleaseGPUBuffer(demo->device, scene->primitives[i].index_buffer);
            }
            if (scene->primitives[i].vertex_buffer) {
                SDL_ReleaseGPUBuffer(demo->device, scene->primitives[i].vertex_buffer);
            }
        }
    }
    if (scene->textures) {
        for (int i = 0; i < scene->texture_count; i += 1) {
            if (scene->textures[i].texture) {
                SDL_ReleaseGPUTexture(demo->device, scene->textures[i].texture);
            }
        }
    }
    if (scene->instance_buffer) {
        SDL_ReleaseGPUBuffer(demo->device, scene->instance_buffer);
    }

    SDL_free(scene->primitives);
    SDL_free(scene->materials);
    SDL_free(scene->textures);
    scene->primitives = nullptr;
    scene->primitive_count = 0;
    scene->materials = nullptr;
    scene->material_count = 0;
    scene->textures = nullptr;
    scene->texture_count = 0;
    scene->texture_capacity = 0;
    scene->instance_buffer = nullptr;
    scene->instance_count = 0;
}

void ForgeGpuFreeSceneData(ForgeGpuDemo *demo, GpuSceneData *scene)
{
    free_scene_gpu_resources(demo, scene);
    ForgeGpuFreeLoadedScene(&scene->loaded);
}

void ForgeGpuFreeGpuScene(ForgeGpuDemo *demo)
{
    LessonState *lesson = &demo->lesson;
    GpuSceneData scene;

    SDL_zero(scene);
    scene.loaded = lesson->scene;
    scene.primitives = lesson->gpu_primitives;
    scene.primitive_count = lesson->gpu_primitive_count;
    scene.materials = lesson->gpu_materials;
    scene.material_count = lesson->gpu_material_count;
    scene.textures = lesson->gpu_scene_textures;
    scene.texture_count = lesson->gpu_scene_texture_count;
    scene.texture_capacity = lesson->gpu_scene_texture_count;
    ForgeGpuFreeSceneData(demo, &scene);
    lesson->scene = scene.loaded;
    lesson->gpu_primitives = scene.primitives;
    lesson->gpu_primitive_count = scene.primitive_count;
    lesson->gpu_materials = scene.materials;
    lesson->gpu_material_count = scene.material_count;
    lesson->gpu_scene_textures = scene.textures;
    lesson->gpu_scene_texture_count = scene.texture_count;
}

static SDL_GPUTexture *find_or_load_scene_texture(
    ForgeGpuDemo *demo,
    GpuSceneData *scene,
    const char *path,
    bool generate_mips,
    SDL_GPUTextureFormat format)
{
    GpuSceneTexture *entry;
    SDL_GPUTexture *texture;

    if (!path || path[0] == '\0') {
        SDL_SetError("scene material texture path is empty");
        return nullptr;
    }

    for (int i = 0; i < scene->texture_count; i += 1) {
        entry = &scene->textures[i];
        if (entry->format == format &&
            entry->generate_mips == generate_mips &&
            SDL_strcmp(entry->path, path) == 0) {
            return entry->texture;
        }
    }

    if (scene->texture_count == scene->texture_capacity) {
        const int new_capacity = scene->texture_capacity > 0 ? scene->texture_capacity * 2 : 4;
        GpuSceneTexture *new_textures = (GpuSceneTexture *)SDL_realloc(
            scene->textures,
            (size_t)new_capacity * sizeof(*scene->textures));
        if (!new_textures) {
            SDL_OutOfMemory();
            return nullptr;
        }
        scene->textures = new_textures;
        scene->texture_capacity = new_capacity;
    }

    texture = ForgeGpuLoadRgbaTexturePathWithFormat(demo, path, generate_mips, format);
    if (!texture) {
        return nullptr;
    }

    entry = &scene->textures[scene->texture_count];
    SDL_zero(*entry);
    entry->texture = texture;
    entry->format = format;
    entry->generate_mips = generate_mips;
    SDL_strlcpy(entry->path, path, sizeof(entry->path));
    scene->texture_count += 1;
    return texture;
}

bool ForgeGpuRunSceneTextureCacheSelfTest(ForgeGpuDemo *demo, const char *texture_path)
{
    GpuSceneData scene;
    bool ok = false;

    if (!texture_path || texture_path[0] == '\0') {
        SDL_SetError("scene texture cache self-test texture path is empty");
        return false;
    }

    SDL_zero(scene);
    scene.loaded.material_count = 4;
    scene.loaded.materials = (ForgeGpuSceneMaterial *)SDL_calloc(
        (size_t)scene.loaded.material_count,
        sizeof(*scene.loaded.materials));
    if (!scene.loaded.materials) {
        SDL_OutOfMemory();
        return false;
    }

    for (int i = 0; i < 2; i += 1) {
        scene.loaded.materials[i].has_texture = true;
        SDL_strlcpy(scene.loaded.materials[i].texture_path, texture_path, sizeof(scene.loaded.materials[i].texture_path));
    }
    for (int i = 2; i < 4; i += 1) {
        scene.loaded.materials[i].has_normal_map = true;
        SDL_strlcpy(scene.loaded.materials[i].normal_map_path, texture_path, sizeof(scene.loaded.materials[i].normal_map_path));
    }

    if (!ForgeGpuUploadSceneDataToGpu(demo, &scene)) {
        goto done;
    }

    if (scene.texture_count != 2) {
        SDL_SetError("scene texture cache self-test expected 2 texture objects, got %d", scene.texture_count);
        goto done;
    }
    if (!scene.materials[0].texture || scene.materials[0].texture != scene.materials[1].texture) {
        SDL_SetError("scene texture cache self-test did not share sRGB material textures");
        goto done;
    }
    if (!scene.materials[2].normal_texture || scene.materials[2].normal_texture != scene.materials[3].normal_texture) {
        SDL_SetError("scene texture cache self-test did not share UNORM material textures");
        goto done;
    }
    if (scene.materials[0].texture == scene.materials[2].normal_texture) {
        SDL_SetError("scene texture cache self-test reused one texture across distinct texture formats");
        goto done;
    }

    ok = true;

done:
    ForgeGpuFreeSceneData(demo, &scene);
    return ok;
}

bool ForgeGpuRunNormalMapSceneSelfTest(ForgeGpuDemo *demo, const char *scene_path)
{
    GpuSceneData scene;
    ForgeGpuSceneLoadRequirements requirements;
    bool ok = false;

    if (!scene_path || scene_path[0] == '\0') {
        SDL_SetError("normal-map scene self-test path is empty");
        return false;
    }

    SDL_zero(scene);
    SDL_zero(requirements);
    requirements.required_features = FORGE_GPU_SCENE_FEATURE_TANGENTS |
                                     FORGE_GPU_SCENE_FEATURE_NORMAL_MAPS |
                                     FORGE_GPU_SCENE_FEATURE_PBR_MATERIALS;
    requirements.required_all_primitive_features = FORGE_GPU_SCENE_FEATURE_TANGENTS |
                                                   FORGE_GPU_SCENE_FEATURE_PRIMITIVE_BOUNDS;
    requirements.required_all_material_features = FORGE_GPU_SCENE_FEATURE_NORMAL_MAPS;

    if (!ForgeGpuLoadGltfSceneWithRequirements(scene_path, &requirements, &scene.loaded)) {
        goto done;
    }
    if (!ForgeGpuUploadSceneDataToGpu(demo, &scene)) {
        goto done;
    }
    if (scene.primitive_count != 1 || scene.material_count != 1) {
        SDL_SetError("normal-map scene self-test expected 1 primitive and 1 material");
        goto done;
    }
    if (!scene.materials[0].has_texture || !scene.materials[0].texture) {
        SDL_SetError("normal-map scene self-test missing base-color texture upload");
        goto done;
    }
    if (!scene.materials[0].has_normal_map || !scene.materials[0].normal_texture) {
        SDL_SetError("normal-map scene self-test missing normal texture upload");
        goto done;
    }
    if (scene.materials[0].texture == scene.materials[0].normal_texture) {
        SDL_SetError("normal-map scene self-test reused color texture for normal map");
        goto done;
    }
    if (scene.texture_count != 2) {
        SDL_SetError("normal-map scene self-test expected 2 uploaded textures, got %d", scene.texture_count);
        goto done;
    }
    {
        bool base_color_is_srgb = false;
        bool normal_map_is_unorm = false;

        for (int i = 0; i < scene.texture_count; i += 1) {
            if (scene.textures[i].texture == scene.materials[0].texture &&
                scene.textures[i].format == SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB) {
                base_color_is_srgb = true;
            }
            if (scene.textures[i].texture == scene.materials[0].normal_texture &&
                scene.textures[i].format == SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM) {
                normal_map_is_unorm = true;
            }
        }
        if (!base_color_is_srgb) {
            SDL_SetError("normal-map scene self-test did not upload base color as sRGB");
            goto done;
        }
        if (!normal_map_is_unorm) {
            SDL_SetError("normal-map scene self-test did not upload normal map as UNORM");
            goto done;
        }
    }

    ok = true;

done:
    ForgeGpuFreeSceneData(demo, &scene);
    return ok;
}

bool ForgeGpuUploadSceneDataToGpu(ForgeGpuDemo *demo, GpuSceneData *scene)
{
    LessonState *lesson = &demo->lesson;

    if (!lesson->white_texture) {
        lesson->white_texture = ForgeGpuCreateWhiteTexture(demo->device);
    }
    if (!lesson->samplers[0]) {
        lesson->samplers[0] = ForgeGpuCreateSampler(
            demo->device,
            SDL_GPU_FILTER_LINEAR, SDL_GPU_FILTER_LINEAR,
            SDL_GPU_SAMPLERMIPMAPMODE_LINEAR,
            1000.0f);
    }
    if (!lesson->white_texture || !lesson->samplers[0]) {
        goto fail;
    }

    scene->primitive_count = scene->loaded.primitive_count;
    if (scene->primitive_count > 0) {
        scene->primitives = (GpuPrimitive *)SDL_calloc((size_t)scene->primitive_count, sizeof(*scene->primitives));
        if (!scene->primitives) {
            SDL_OutOfMemory();
            goto fail;
        }
    }

    for (int i = 0; i < scene->loaded.primitive_count; i += 1) {
        const ForgeGpuScenePrimitive *src = &scene->loaded.primitives[i];
        GpuPrimitive *dst = &scene->primitives[i];
        const Uint32 vertex_bytes = src->vertex_count * (Uint32)sizeof(*src->vertices);

        if (src->vertex_count == 0 || !src->vertices) {
            SDL_SetError("glTF primitive has no vertices");
            goto fail;
        }

        dst->vertex_buffer = ForgeGpuCreateBufferWithData(
            demo->device,
            SDL_GPU_BUFFERUSAGE_VERTEX,
            src->vertices,
            vertex_bytes);
        if (!dst->vertex_buffer) {
            goto fail;
        }
        dst->vertex_count = src->vertex_count;
        dst->index_count = src->index_count;
        dst->material_index = src->material_index;
        dst->index_type = src->index_stride == 4 ? SDL_GPU_INDEXELEMENTSIZE_32BIT : SDL_GPU_INDEXELEMENTSIZE_16BIT;
        dst->has_bounds = src->has_bounds;
        if (src->has_bounds) {
            dst->aabb_min.x = src->aabb_min[0];
            dst->aabb_min.y = src->aabb_min[1];
            dst->aabb_min.z = src->aabb_min[2];
            dst->aabb_max.x = src->aabb_max[0];
            dst->aabb_max.y = src->aabb_max[1];
            dst->aabb_max.z = src->aabb_max[2];
        }

        if (src->indices && src->index_count > 0) {
            dst->index_buffer = ForgeGpuCreateBufferWithData(
                demo->device,
                SDL_GPU_BUFFERUSAGE_INDEX,
                src->indices,
                src->index_count * src->index_stride);
            if (!dst->index_buffer) {
                goto fail;
            }
        }
    }

    scene->material_count = scene->loaded.material_count;
    if (scene->material_count > 0) {
        scene->materials = (GpuMaterial *)SDL_calloc((size_t)scene->material_count, sizeof(*scene->materials));
        if (!scene->materials) {
            SDL_OutOfMemory();
            goto fail;
        }
    }

    for (int i = 0; i < scene->loaded.material_count; i += 1) {
        const ForgeGpuSceneMaterial *src = &scene->loaded.materials[i];
        GpuMaterial *dst = &scene->materials[i];

        SDL_memcpy(dst->base_color, src->base_color, sizeof(dst->base_color));
        SDL_memcpy(dst->emissive_factor, src->emissive_factor, sizeof(dst->emissive_factor));
        dst->normal_scale = src->normal_scale;
        dst->metallic_factor = src->metallic_factor;
        dst->roughness_factor = src->roughness_factor;
        dst->occlusion_strength = src->occlusion_strength;
        dst->alpha_cutoff = src->alpha_cutoff;
        dst->alpha_mode = src->alpha_mode;
        dst->has_texture = src->has_texture;
        dst->has_normal_map = src->has_normal_map;
        dst->has_metallic_roughness = src->has_metallic_roughness;
        dst->has_occlusion = src->has_occlusion;
        dst->has_emissive = src->has_emissive;
        dst->double_sided = src->double_sided;
        if (src->has_texture) {
            dst->texture = find_or_load_scene_texture(
                demo,
                scene,
                src->texture_path,
                true,
                SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB);
            if (!dst->texture) {
                goto fail;
            }
        }
        if (src->has_normal_map) {
            dst->normal_texture = find_or_load_scene_texture(
                demo,
                scene,
                src->normal_map_path,
                true,
                SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM);
            if (!dst->normal_texture) {
                goto fail;
            }
        }
    }

    return true;

fail:
    free_scene_gpu_resources(demo, scene);
    return false;
}

bool ForgeGpuUploadLoadedSceneToGpu(ForgeGpuDemo *demo)
{
    LessonState *lesson = &demo->lesson;
    GpuSceneData scene;
    bool ok;

    SDL_zero(scene);
    scene.loaded = lesson->scene;
    ok = ForgeGpuUploadSceneDataToGpu(demo, &scene);
    lesson->scene = scene.loaded;
    lesson->gpu_primitives = scene.primitives;
    lesson->gpu_primitive_count = scene.primitive_count;
    lesson->gpu_materials = scene.materials;
    lesson->gpu_material_count = scene.material_count;
    lesson->gpu_scene_textures = scene.textures;
    lesson->gpu_scene_texture_count = scene.texture_count;
    return ok;
}
