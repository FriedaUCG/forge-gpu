#include "forge_gpu_assets.h"

#include <stddef.h>

#include "forge_gpu_processed_assets.h"
#include "obj/forge_obj.h"
#include "gltf/forge_gltf.h"

static bool checked_mul_size(size_t a, size_t b, size_t *out)
{
    if (b != 0 && a > ((size_t)-1) / b) {
        return false;
    }
    *out = a * b;
    return true;
}

static void copy_vertex(ForgeGpuMeshVertex *dst, const ForgeObjVertex *src)
{
    dst->position[0] = src->position.x;
    dst->position[1] = src->position.y;
    dst->position[2] = src->position.z;
    dst->normal[0] = src->normal.x;
    dst->normal[1] = src->normal.y;
    dst->normal[2] = src->normal.z;
    dst->uv[0] = src->uv.x;
    dst->uv[1] = src->uv.y;
}

static void copy_vertex(ForgeGpuMeshVertex *dst, const ForgeGltfVertex *src)
{
    dst->position[0] = src->position.x;
    dst->position[1] = src->position.y;
    dst->position[2] = src->position.z;
    dst->normal[0] = src->normal.x;
    dst->normal[1] = src->normal.y;
    dst->normal[2] = src->normal.z;
    dst->uv[0] = src->uv.x;
    dst->uv[1] = src->uv.y;
}

static ForgeGpuSceneAlphaMode copy_alpha_mode(ForgeGltfAlphaMode mode)
{
    switch (mode) {
    case FORGE_GLTF_ALPHA_MASK:
        return FORGE_GPU_SCENE_ALPHA_MASK;
    case FORGE_GLTF_ALPHA_BLEND:
        return FORGE_GPU_SCENE_ALPHA_BLEND;
    case FORGE_GLTF_ALPHA_OPAQUE:
    default:
        return FORGE_GPU_SCENE_ALPHA_OPAQUE;
    }
}

static ForgeGpuSceneAnimationPath copy_animation_path(ForgeGltfAnimPath path)
{
    switch (path) {
    case FORGE_GLTF_ANIM_TRANSLATION:
        return FORGE_GPU_SCENE_ANIM_TRANSLATION;
    case FORGE_GLTF_ANIM_ROTATION:
        return FORGE_GPU_SCENE_ANIM_ROTATION;
    case FORGE_GLTF_ANIM_SCALE:
        return FORGE_GPU_SCENE_ANIM_SCALE;
    case FORGE_GLTF_ANIM_MORPH_WEIGHTS:
    default:
        return FORGE_GPU_SCENE_ANIM_MORPH_WEIGHTS;
    }
}

static ForgeGpuSceneAnimationInterpolation copy_animation_interpolation(ForgeGltfInterpolation interpolation)
{
    switch (interpolation) {
    case FORGE_GLTF_INTERP_STEP:
        return FORGE_GPU_SCENE_INTERP_STEP;
    case FORGE_GLTF_INTERP_LINEAR:
    default:
        return FORGE_GPU_SCENE_INTERP_LINEAR;
    }
}

static void append_feature_name(char *buffer, size_t buffer_size, const char *name, bool *first)
{
    if (!*first) {
        SDL_strlcat(buffer, ", ", buffer_size);
    }
    SDL_strlcat(buffer, name, buffer_size);
    *first = false;
}

static void describe_features(ForgeGpuSceneFeatureFlags features, char *buffer, size_t buffer_size)
{
    bool first = true;

    if (buffer_size == 0) {
        return;
    }
    buffer[0] = '\0';
    if (features & FORGE_GPU_SCENE_FEATURE_TANGENTS) {
        append_feature_name(buffer, buffer_size, "tangents", &first);
    }
    if (features & FORGE_GPU_SCENE_FEATURE_ALPHA_MATERIALS) {
        append_feature_name(buffer, buffer_size, "alpha materials", &first);
    }
    if (features & FORGE_GPU_SCENE_FEATURE_NORMAL_MAPS) {
        append_feature_name(buffer, buffer_size, "normal maps", &first);
    }
    if (features & FORGE_GPU_SCENE_FEATURE_PBR_MATERIALS) {
        append_feature_name(buffer, buffer_size, "PBR material data", &first);
    }
    if (features & FORGE_GPU_SCENE_FEATURE_NODE_HIERARCHY) {
        append_feature_name(buffer, buffer_size, "node hierarchy/TRS", &first);
    }
    if (features & FORGE_GPU_SCENE_FEATURE_SKINS) {
        append_feature_name(buffer, buffer_size, "skins", &first);
    }
    if (features & FORGE_GPU_SCENE_FEATURE_ANIMATIONS) {
        append_feature_name(buffer, buffer_size, "animations", &first);
    }
    if (features & FORGE_GPU_SCENE_FEATURE_MORPHS) {
        append_feature_name(buffer, buffer_size, "morph targets", &first);
    }
    if (features & FORGE_GPU_SCENE_FEATURE_PRIMITIVE_BOUNDS) {
        append_feature_name(buffer, buffer_size, "primitive bounds", &first);
    }
    if (first) {
        SDL_strlcpy(buffer, "none", buffer_size);
    }
}

static bool material_has_pbr_facts(const ForgeGltfMaterial *material)
{
    return material->has_metallic_roughness ||
           material->has_occlusion ||
           material->has_emissive ||
           material->metallic_factor != 1.0f ||
           material->roughness_factor != 1.0f ||
           material->emissive_factor[0] != 0.0f ||
           material->emissive_factor[1] != 0.0f ||
           material->emissive_factor[2] != 0.0f;
}

static bool scene_material_has_pbr_facts(const ForgeGpuSceneMaterial *material)
{
    return material->has_metallic_roughness ||
           material->has_occlusion ||
           material->has_emissive ||
           material->metallic_factor != 1.0f ||
           material->roughness_factor != 1.0f ||
           material->emissive_factor[0] != 0.0f ||
           material->emissive_factor[1] != 0.0f ||
           material->emissive_factor[2] != 0.0f;
}

static ForgeGpuSceneFeatureFlags collect_available_features(const ForgeGltfScene *scene)
{
    ForgeGpuSceneFeatureFlags features = 0;

    for (int i = 0; i < scene->primitive_count; i += 1) {
        const ForgeGltfPrimitive *primitive = &scene->primitives[i];

        if (primitive->has_tangents && primitive->tangents) {
            features |= FORGE_GPU_SCENE_FEATURE_TANGENTS;
        }
        if (primitive->has_skin_data) {
            features |= FORGE_GPU_SCENE_FEATURE_SKINS;
        }
        if (primitive->morph_target_count > 0) {
            features |= FORGE_GPU_SCENE_FEATURE_MORPHS;
        }
    }

    for (int i = 0; i < scene->mesh_count; i += 1) {
        if (scene->meshes[i].default_weight_count > 0) {
            features |= FORGE_GPU_SCENE_FEATURE_MORPHS;
        }
    }

    for (int i = 0; i < scene->material_count; i += 1) {
        const ForgeGltfMaterial *material = &scene->materials[i];

        if (material->alpha_mode != FORGE_GLTF_ALPHA_OPAQUE ||
            material->alpha_cutoff != FORGE_GLTF_DEFAULT_ALPHA_CUTOFF ||
            material->double_sided) {
            features |= FORGE_GPU_SCENE_FEATURE_ALPHA_MATERIALS;
        }
        if (material->has_normal_map) {
            features |= FORGE_GPU_SCENE_FEATURE_NORMAL_MAPS;
        }
        if (material_has_pbr_facts(material)) {
            features |= FORGE_GPU_SCENE_FEATURE_PBR_MATERIALS;
        }
    }

    if (scene->root_node_count > 0) {
        features |= FORGE_GPU_SCENE_FEATURE_NODE_HIERARCHY;
    }
    for (int i = 0; i < scene->node_count; i += 1) {
        const ForgeGltfNode *node = &scene->nodes[i];

        if (node->parent >= 0 ||
            node->child_count > 0 ||
            node->has_trs ||
            node->skin_index >= 0) {
            features |= FORGE_GPU_SCENE_FEATURE_NODE_HIERARCHY;
        }
    }

    if (scene->skin_count > 0) {
        features |= FORGE_GPU_SCENE_FEATURE_SKINS;
    }
    if (scene->animation_count > 0) {
        features |= FORGE_GPU_SCENE_FEATURE_ANIMATIONS;
    }

    return features;
}

static ForgeGpuSceneFeatureFlags collect_available_all_primitive_features(const ForgeGltfScene *scene)
{
    ForgeGpuSceneFeatureFlags features = 0;

    if (scene->primitive_count <= 0) {
        return features;
    }

    features = FORGE_GPU_SCENE_FEATURE_TANGENTS |
               FORGE_GPU_SCENE_FEATURE_PRIMITIVE_BOUNDS |
               FORGE_GPU_SCENE_FEATURE_SKINS |
               FORGE_GPU_SCENE_FEATURE_MORPHS;

    for (int i = 0; i < scene->primitive_count; i += 1) {
        const ForgeGltfPrimitive *primitive = &scene->primitives[i];

        if (!primitive->has_tangents || !primitive->tangents) {
            features &= ~FORGE_GPU_SCENE_FEATURE_TANGENTS;
        }
        if (primitive->vertex_count <= 0 || !primitive->vertices) {
            features &= ~FORGE_GPU_SCENE_FEATURE_PRIMITIVE_BOUNDS;
        }
        if (!primitive->has_skin_data) {
            features &= ~FORGE_GPU_SCENE_FEATURE_SKINS;
        }
        if (primitive->morph_target_count <= 0) {
            features &= ~FORGE_GPU_SCENE_FEATURE_MORPHS;
        }
    }

    return features;
}

static ForgeGpuSceneFeatureFlags collect_available_all_material_features(const ForgeGltfScene *scene)
{
    ForgeGpuSceneFeatureFlags features = 0;

    if (scene->material_count <= 0) {
        return features;
    }

    features = FORGE_GPU_SCENE_FEATURE_ALPHA_MATERIALS |
               FORGE_GPU_SCENE_FEATURE_NORMAL_MAPS |
               FORGE_GPU_SCENE_FEATURE_PBR_MATERIALS;

    for (int i = 0; i < scene->material_count; i += 1) {
        const ForgeGltfMaterial *material = &scene->materials[i];

        if (material->alpha_mode == FORGE_GLTF_ALPHA_OPAQUE &&
            material->alpha_cutoff == FORGE_GLTF_DEFAULT_ALPHA_CUTOFF &&
            !material->double_sided) {
            features &= ~FORGE_GPU_SCENE_FEATURE_ALPHA_MATERIALS;
        }
        if (!material->has_normal_map) {
            features &= ~FORGE_GPU_SCENE_FEATURE_NORMAL_MAPS;
        }
        if (!material_has_pbr_facts(material)) {
            features &= ~FORGE_GPU_SCENE_FEATURE_PBR_MATERIALS;
        }
    }

    return features;
}

static bool scene_has_retained_skins(const ForgeGpuLoadedScene *scene)
{
    bool has_skinned_node = false;

    if (scene->skin_count <= 0 || !scene->skins) {
        return false;
    }
    for (int skin_index = 0; skin_index < scene->skin_count; skin_index += 1) {
        const ForgeGpuSceneSkin *skin = &scene->skins[skin_index];

        if (skin->joint_count <= 0 || !skin->joints || !skin->inverse_bind_matrices ||
            skin->skeleton < -1 || skin->skeleton >= scene->node_count) {
            return false;
        }
        for (int joint = 0; joint < skin->joint_count; joint += 1) {
            if (skin->joints[joint] < 0 || skin->joints[joint] >= scene->node_count) {
                return false;
            }
        }
    }
    for (int node_index = 0; node_index < scene->node_count; node_index += 1) {
        const int skin_index = scene->nodes[node_index].skin_index;

        if (skin_index >= scene->skin_count || skin_index < -1) {
            return false;
        }
        if (skin_index >= 0) {
            has_skinned_node = true;
        }
    }
    return has_skinned_node;
}

static ForgeGpuSceneFeatureFlags collect_retained_features(const ForgeGpuLoadedScene *scene)
{
    ForgeGpuSceneFeatureFlags features = 0;

    for (int i = 0; i < scene->primitive_count; i += 1) {
        if (scene->primitives[i].has_tangents && scene->primitives[i].tangents) {
            features |= FORGE_GPU_SCENE_FEATURE_TANGENTS;
        }
    }

    for (int i = 0; i < scene->material_count; i += 1) {
        const ForgeGpuSceneMaterial *material = &scene->materials[i];

        if (material->alpha_mode != FORGE_GPU_SCENE_ALPHA_OPAQUE ||
            material->alpha_cutoff != FORGE_GLTF_DEFAULT_ALPHA_CUTOFF ||
            material->double_sided) {
            features |= FORGE_GPU_SCENE_FEATURE_ALPHA_MATERIALS;
        }
        if (material->has_normal_map) {
            features |= FORGE_GPU_SCENE_FEATURE_NORMAL_MAPS;
        }
        if (scene_material_has_pbr_facts(material)) {
            features |= FORGE_GPU_SCENE_FEATURE_PBR_MATERIALS;
        }
    }

    if (scene->node_count > 0 &&
        scene->root_node_count > 0 &&
        scene->root_nodes &&
        (scene->child_index_count == 0 || scene->child_indices) &&
        scene->node_traversal_order &&
        scene->node_traversal_order_count == scene->node_count) {
        features |= FORGE_GPU_SCENE_FEATURE_NODE_HIERARCHY;
    }
    if (scene->animation_count > 0 && scene->animations) {
        features |= FORGE_GPU_SCENE_FEATURE_ANIMATIONS;
    }
    if (scene_has_retained_skins(scene)) {
        features |= FORGE_GPU_SCENE_FEATURE_SKINS;
    }

    return features;
}

static ForgeGpuSceneFeatureFlags collect_retained_all_primitive_features(const ForgeGpuLoadedScene *scene)
{
    ForgeGpuSceneFeatureFlags features = 0;

    if (scene->primitive_count <= 0) {
        return features;
    }

    features = FORGE_GPU_SCENE_FEATURE_TANGENTS |
               FORGE_GPU_SCENE_FEATURE_PRIMITIVE_BOUNDS |
               FORGE_GPU_SCENE_FEATURE_SKINS;

    for (int i = 0; i < scene->primitive_count; i += 1) {
        const ForgeGpuScenePrimitive *primitive = &scene->primitives[i];

        if (!primitive->has_tangents || !primitive->tangents) {
            features &= ~FORGE_GPU_SCENE_FEATURE_TANGENTS;
        }
        if (!primitive->has_bounds) {
            features &= ~FORGE_GPU_SCENE_FEATURE_PRIMITIVE_BOUNDS;
        }
        if (!primitive->has_skin_data || !primitive->joint_indices || !primitive->weights) {
            features &= ~FORGE_GPU_SCENE_FEATURE_SKINS;
        }
    }

    return features;
}

static ForgeGpuSceneFeatureFlags collect_retained_all_material_features(const ForgeGpuLoadedScene *scene)
{
    ForgeGpuSceneFeatureFlags features = 0;

    if (scene->material_count <= 0) {
        return features;
    }

    features = FORGE_GPU_SCENE_FEATURE_ALPHA_MATERIALS |
               FORGE_GPU_SCENE_FEATURE_NORMAL_MAPS |
               FORGE_GPU_SCENE_FEATURE_PBR_MATERIALS;

    for (int i = 0; i < scene->material_count; i += 1) {
        const ForgeGpuSceneMaterial *material = &scene->materials[i];

        if (material->alpha_mode == FORGE_GPU_SCENE_ALPHA_OPAQUE &&
            material->alpha_cutoff == FORGE_GLTF_DEFAULT_ALPHA_CUTOFF &&
            !material->double_sided) {
            features &= ~FORGE_GPU_SCENE_FEATURE_ALPHA_MATERIALS;
        }
        if (!material->has_normal_map) {
            features &= ~FORGE_GPU_SCENE_FEATURE_NORMAL_MAPS;
        }
        if (!scene_material_has_pbr_facts(material)) {
            features &= ~FORGE_GPU_SCENE_FEATURE_PBR_MATERIALS;
        }
    }

    return features;
}

static bool check_scene_requirements(
    const ForgeGpuSceneLoadRequirements *requirements,
    ForgeGpuSceneFeatureFlags available_features,
    ForgeGpuSceneFeatureFlags retained_features,
    ForgeGpuSceneFeatureFlags available_all_primitive_features,
    ForgeGpuSceneFeatureFlags retained_all_primitive_features,
    ForgeGpuSceneFeatureFlags available_all_material_features,
    ForgeGpuSceneFeatureFlags retained_all_material_features)
{
    char features[256];
    ForgeGpuSceneFeatureFlags required_features;
    ForgeGpuSceneFeatureFlags missing_features;
    ForgeGpuSceneFeatureFlags dropped_features;

    if (!requirements || (requirements->required_features |
                          requirements->required_all_primitive_features |
                          requirements->required_all_material_features) == 0) {
        return true;
    }

    required_features = requirements->required_features;
    missing_features = required_features & ~available_features;
    if (missing_features != 0) {
        describe_features(missing_features, features, sizeof(features));
        SDL_SetError("required glTF data missing in asset: %s", features);
        return false;
    }

    dropped_features = required_features & ~retained_features;
    if (dropped_features != 0) {
        describe_features(dropped_features, features, sizeof(features));
        SDL_SetError("required glTF data is not retained by the Forge demo loader yet: %s", features);
        return false;
    }

    required_features = requirements->required_all_primitive_features;
    missing_features = required_features & ~available_all_primitive_features;
    if (missing_features != 0) {
        describe_features(missing_features, features, sizeof(features));
        SDL_SetError("required glTF data missing on at least one primitive: %s", features);
        return false;
    }

    dropped_features = required_features & ~retained_all_primitive_features;
    if (dropped_features != 0) {
        describe_features(dropped_features, features, sizeof(features));
        SDL_SetError("required glTF primitive data is not retained by the Forge demo loader yet: %s", features);
        return false;
    }

    required_features = requirements->required_all_material_features;
    missing_features = required_features & ~available_all_material_features;
    if (missing_features != 0) {
        describe_features(missing_features, features, sizeof(features));
        SDL_SetError("required glTF data missing on at least one material: %s", features);
        return false;
    }

    dropped_features = required_features & ~retained_all_material_features;
    if (dropped_features != 0) {
        describe_features(dropped_features, features, sizeof(features));
        SDL_SetError("required glTF material data is not retained by the Forge demo loader yet: %s", features);
        return false;
    }

    return true;
}

bool ForgeGpuLoadObjMesh(const char *path, ForgeGpuLoadedMesh *out_mesh)
{
    ForgeObjMesh mesh;
    size_t vertex_bytes;

    if (!path || !out_mesh) {
        SDL_SetError("invalid OBJ load arguments");
        return false;
    }

    SDL_zero(*out_mesh);
    if (!forge_obj_load(path, &mesh)) {
        SDL_SetError("failed to load forge OBJ mesh: %s", path);
        return false;
    }

    if (!checked_mul_size((size_t)mesh.vertex_count, sizeof(*out_mesh->vertices), &vertex_bytes)) {
        forge_obj_free(&mesh);
        SDL_SetError("OBJ mesh vertex allocation overflow");
        return false;
    }

    out_mesh->vertices = (ForgeGpuMeshVertex *)SDL_malloc(vertex_bytes);
    if (!out_mesh->vertices) {
        forge_obj_free(&mesh);
        SDL_OutOfMemory();
        return false;
    }

    for (Uint32 i = 0; i < mesh.vertex_count; i += 1) {
        copy_vertex(&out_mesh->vertices[i], &mesh.vertices[i]);
    }
    out_mesh->vertex_count = mesh.vertex_count;

    forge_obj_free(&mesh);
    return true;
}

void ForgeGpuFreeLoadedMesh(ForgeGpuLoadedMesh *mesh)
{
    if (!mesh) {
        return;
    }

    SDL_free(mesh->vertices);
    SDL_zero(*mesh);
}

static bool copy_gltf_primitives(const ForgeGltfScene *src, ForgeGpuLoadedScene *dst)
{
    size_t primitive_bytes;

    dst->primitive_count = src->primitive_count;
    if (src->primitive_count == 0) {
        return true;
    }

    if (!checked_mul_size((size_t)src->primitive_count, sizeof(*dst->primitives), &primitive_bytes)) {
        SDL_SetError("glTF primitive allocation overflow");
        return false;
    }

    dst->primitives = (ForgeGpuScenePrimitive *)SDL_calloc(1, primitive_bytes);
    if (!dst->primitives) {
        SDL_OutOfMemory();
        return false;
    }

    for (int i = 0; i < src->primitive_count; i += 1) {
        const ForgeGltfPrimitive *src_prim = &src->primitives[i];
        ForgeGpuScenePrimitive *dst_prim = &dst->primitives[i];
        size_t vertex_bytes;
        size_t index_bytes;

        dst_prim->vertex_count = src_prim->vertex_count;
        dst_prim->index_count = src_prim->index_count;
        dst_prim->index_stride = src_prim->index_stride;
        dst_prim->material_index = src_prim->material_index;
        dst_prim->has_uvs = src_prim->has_uvs;
        dst_prim->has_tangents = src_prim->has_tangents && src_prim->tangents && src_prim->vertex_count > 0;

        if (src_prim->vertex_count > 0) {
            if (!checked_mul_size((size_t)src_prim->vertex_count, sizeof(*dst_prim->vertices), &vertex_bytes)) {
                SDL_SetError("glTF vertex allocation overflow");
                return false;
            }
            dst_prim->vertices = (ForgeGpuMeshVertex *)SDL_malloc(vertex_bytes);
            if (!dst_prim->vertices) {
                SDL_OutOfMemory();
                return false;
            }
            for (Uint32 v = 0; v < src_prim->vertex_count; v += 1) {
                copy_vertex(&dst_prim->vertices[v], &src_prim->vertices[v]);
            }

            dst_prim->has_bounds = true;
            SDL_memcpy(dst_prim->aabb_min, dst_prim->vertices[0].position, sizeof(dst_prim->aabb_min));
            SDL_memcpy(dst_prim->aabb_max, dst_prim->vertices[0].position, sizeof(dst_prim->aabb_max));
            for (Uint32 v = 1; v < src_prim->vertex_count; v += 1) {
                for (int axis = 0; axis < 3; axis += 1) {
                    const float p = dst_prim->vertices[v].position[axis];
                    if (p < dst_prim->aabb_min[axis]) {
                        dst_prim->aabb_min[axis] = p;
                    }
                    if (p > dst_prim->aabb_max[axis]) {
                        dst_prim->aabb_max[axis] = p;
                    }
                }
            }
        }

        if (dst_prim->has_tangents && src_prim->vertex_count > 0) {
            size_t tangent_bytes;

            if (!checked_mul_size((size_t)src_prim->vertex_count, 4u * sizeof(float), &tangent_bytes)) {
                SDL_SetError("glTF tangent allocation overflow");
                return false;
            }
            dst_prim->tangents = (float *)SDL_malloc(tangent_bytes);
            if (!dst_prim->tangents) {
                SDL_OutOfMemory();
                return false;
            }
            for (Uint32 v = 0; v < src_prim->vertex_count; v += 1) {
                dst_prim->tangents[(size_t)v * 4u + 0] = src_prim->tangents[v].x;
                dst_prim->tangents[(size_t)v * 4u + 1] = src_prim->tangents[v].y;
                dst_prim->tangents[(size_t)v * 4u + 2] = src_prim->tangents[v].z;
                dst_prim->tangents[(size_t)v * 4u + 3] = src_prim->tangents[v].w;
            }
        }

        if (src_prim->has_skin_data) {
            size_t element_count;
            size_t joint_bytes;
            size_t weight_bytes;

            if (src_prim->vertex_count == 0 || !src_prim->joint_indices || !src_prim->weights) {
                SDL_SetError("glTF skin primitive data is incomplete");
                return false;
            }
            if (!checked_mul_size((size_t)src_prim->vertex_count, FORGE_GPU_SCENE_JOINTS_PER_VERTEX, &element_count) ||
                !checked_mul_size(element_count, sizeof(*dst_prim->joint_indices), &joint_bytes) ||
                !checked_mul_size(element_count, sizeof(*dst_prim->weights), &weight_bytes)) {
                SDL_SetError("glTF skin primitive allocation overflow");
                return false;
            }
            dst_prim->joint_indices = (Uint16 *)SDL_malloc(joint_bytes);
            dst_prim->weights = (float *)SDL_malloc(weight_bytes);
            if (!dst_prim->joint_indices || !dst_prim->weights) {
                SDL_OutOfMemory();
                return false;
            }
            SDL_memcpy(dst_prim->joint_indices, src_prim->joint_indices, joint_bytes);
            SDL_memcpy(dst_prim->weights, src_prim->weights, weight_bytes);
            dst_prim->has_skin_data = true;
        }

        if (src_prim->indices && src_prim->index_count > 0) {
            if (src_prim->index_stride != 2 && src_prim->index_stride != 4) {
                SDL_SetError("unsupported glTF index stride");
                return false;
            }
            if (!checked_mul_size((size_t)src_prim->index_count, (size_t)src_prim->index_stride, &index_bytes)) {
                SDL_SetError("glTF index allocation overflow");
                return false;
            }
            dst_prim->indices = SDL_malloc(index_bytes);
            if (!dst_prim->indices) {
                SDL_OutOfMemory();
                return false;
            }
            SDL_memcpy(dst_prim->indices, src_prim->indices, index_bytes);
        }
    }

    return true;
}

static bool copy_gltf_skins(const ForgeGltfScene *src, ForgeGpuLoadedScene *dst)
{
    size_t skin_bytes;

    dst->skin_count = src->skin_count;
    if (src->skin_count == 0) {
        return true;
    }

    if (!checked_mul_size((size_t)src->skin_count, sizeof(*dst->skins), &skin_bytes)) {
        SDL_SetError("glTF skin allocation overflow");
        return false;
    }
    dst->skins = (ForgeGpuSceneSkin *)SDL_calloc(1, skin_bytes);
    if (!dst->skins) {
        SDL_OutOfMemory();
        return false;
    }

    for (int skin_index = 0; skin_index < src->skin_count; skin_index += 1) {
        const ForgeGltfSkin *src_skin = &src->skins[skin_index];
        ForgeGpuSceneSkin *dst_skin = &dst->skins[skin_index];

        if (src_skin->joint_count < 0 ||
            src_skin->skeleton < -1 ||
            src_skin->skeleton >= src->node_count ||
            (src_skin->joint_count > 0 && (!src_skin->joints || !src_skin->inverse_bind_matrices))) {
            SDL_SetError("glTF skin data is incomplete");
            return false;
        }

        SDL_strlcpy(dst_skin->name, src_skin->name, sizeof(dst_skin->name));
        dst_skin->joint_count = src_skin->joint_count;
        dst_skin->skeleton = src_skin->skeleton;

        if (src_skin->joint_count > 0) {
            size_t joint_bytes;
            size_t matrix_bytes;

            if (!checked_mul_size((size_t)src_skin->joint_count, sizeof(*dst_skin->joints), &joint_bytes) ||
                !checked_mul_size((size_t)src_skin->joint_count, sizeof(*dst_skin->inverse_bind_matrices), &matrix_bytes)) {
                SDL_SetError("glTF skin joint allocation overflow");
                return false;
            }
            dst_skin->joints = (int *)SDL_malloc(joint_bytes);
            dst_skin->inverse_bind_matrices = (ForgeGpuMat4 *)SDL_malloc(matrix_bytes);
            if (!dst_skin->joints || !dst_skin->inverse_bind_matrices) {
                SDL_OutOfMemory();
                return false;
            }
            for (int joint = 0; joint < src_skin->joint_count; joint += 1) {
                if (src_skin->joints[joint] < 0 || src_skin->joints[joint] >= src->node_count) {
                    SDL_SetError("glTF skin joint index out of range");
                    return false;
                }
                dst_skin->joints[joint] = src_skin->joints[joint];
                SDL_memcpy(
                    dst_skin->inverse_bind_matrices[joint].m,
                    src_skin->inverse_bind_matrices[joint].m,
                    sizeof(dst_skin->inverse_bind_matrices[joint].m));
            }
        }
    }

    return true;
}

static bool copy_gltf_meshes(const ForgeGltfScene *src, ForgeGpuLoadedScene *dst)
{
    size_t mesh_bytes;

    dst->mesh_count = src->mesh_count;
    if (src->mesh_count == 0) {
        return true;
    }

    if (!checked_mul_size((size_t)src->mesh_count, sizeof(*dst->meshes), &mesh_bytes)) {
        SDL_SetError("glTF mesh allocation overflow");
        return false;
    }

    dst->meshes = (ForgeGpuSceneMesh *)SDL_calloc(1, mesh_bytes);
    if (!dst->meshes) {
        SDL_OutOfMemory();
        return false;
    }

    for (int i = 0; i < src->mesh_count; i += 1) {
        dst->meshes[i].first_primitive = src->meshes[i].first_primitive;
        dst->meshes[i].primitive_count = src->meshes[i].primitive_count;
    }
    return true;
}

static bool append_node_traversal_order(ForgeGpuLoadedScene *scene, int node_index, Uint8 *visited, Uint8 *visiting)
{
    const ForgeGpuSceneNode *node;

    if (node_index < 0 || node_index >= scene->node_count) {
        SDL_SetError("glTF hierarchy contains out-of-range node index");
        return false;
    }
    if (visiting[node_index]) {
        SDL_SetError("glTF hierarchy contains a cycle");
        return false;
    }
    if (visited[node_index]) {
        SDL_SetError("glTF hierarchy contains a duplicate node reference");
        return false;
    }

    visiting[node_index] = 1;
    visited[node_index] = 1;
    scene->node_traversal_order[scene->node_traversal_order_count] = node_index;
    scene->node_traversal_order_count += 1;

    node = &scene->nodes[node_index];
    for (int i = 0; i < node->child_count; i += 1) {
        const int child = scene->child_indices[node->first_child + i];
        if (scene->nodes[child].parent != node_index) {
            SDL_SetError("glTF hierarchy child/parent links are inconsistent");
            return false;
        }
        if (!append_node_traversal_order(scene, child, visited, visiting)) {
            return false;
        }
    }

    visiting[node_index] = 0;
    return true;
}

static bool build_node_traversal_order(ForgeGpuLoadedScene *scene)
{
    Uint8 *visited;
    Uint8 *visiting;
    bool ok = false;

    if (scene->node_count <= 0) {
        return true;
    }
    if (scene->root_node_count <= 0 || !scene->root_nodes) {
        return true;
    }

    scene->node_traversal_order = (int *)SDL_calloc((size_t)scene->node_count, sizeof(*scene->node_traversal_order));
    visited = (Uint8 *)SDL_calloc((size_t)scene->node_count, sizeof(*visited));
    visiting = (Uint8 *)SDL_calloc((size_t)scene->node_count, sizeof(*visiting));
    if (!scene->node_traversal_order || !visited || !visiting) {
        SDL_OutOfMemory();
        goto done;
    }

    for (int i = 0; i < scene->root_node_count; i += 1) {
        const int root = scene->root_nodes[i];

        if (root < 0 || root >= scene->node_count || scene->nodes[root].parent >= 0) {
            SDL_SetError("glTF root-node list is inconsistent");
            goto done;
        }
        if (!append_node_traversal_order(scene, scene->root_nodes[i], visited, visiting)) {
            goto done;
        }
    }

    ok = true;

done:
    SDL_free(visited);
    SDL_free(visiting);
    return ok;
}

static bool copy_gltf_nodes(const ForgeGltfScene *src, ForgeGpuLoadedScene *dst)
{
    size_t node_bytes;
    int child_offset = 0;

    dst->node_count = src->node_count;
    if (src->node_count == 0) {
        return true;
    }

    if (!checked_mul_size((size_t)src->node_count, sizeof(*dst->nodes), &node_bytes)) {
        SDL_SetError("glTF node allocation overflow");
        return false;
    }

    dst->nodes = (ForgeGpuSceneNode *)SDL_calloc(1, node_bytes);
    if (!dst->nodes) {
        SDL_OutOfMemory();
        return false;
    }

    for (int i = 0; i < src->node_count; i += 1) {
        if (src->nodes[i].child_count > 0) {
            if (src->nodes[i].child_count > SDL_MAX_SINT32 - dst->child_index_count) {
                SDL_SetError("glTF child-index allocation overflow");
                return false;
            }
            dst->child_index_count += src->nodes[i].child_count;
        }
    }
    if (dst->child_index_count > 0) {
        size_t child_bytes;

        if (!checked_mul_size((size_t)dst->child_index_count, sizeof(*dst->child_indices), &child_bytes)) {
            SDL_SetError("glTF child-index allocation overflow");
            return false;
        }
        dst->child_indices = (int *)SDL_malloc(child_bytes);
        if (!dst->child_indices) {
            SDL_OutOfMemory();
            return false;
        }
    }

    for (int i = 0; i < src->node_count; i += 1) {
        if (src->nodes[i].skin_index < -1 || src->nodes[i].skin_index >= src->skin_count) {
            SDL_SetError("glTF node skin index out of range");
            return false;
        }
        dst->nodes[i].mesh_index = src->nodes[i].mesh_index;
        dst->nodes[i].parent = src->nodes[i].parent;
        dst->nodes[i].first_child = -1;
        dst->nodes[i].child_count = src->nodes[i].child_count;
        dst->nodes[i].skin_index = src->nodes[i].skin_index;
        dst->nodes[i].has_trs = src->nodes[i].has_trs;
        SDL_strlcpy(dst->nodes[i].name, src->nodes[i].name, sizeof(dst->nodes[i].name));
        SDL_memcpy(dst->nodes[i].local_transform.m, src->nodes[i].local_transform.m, sizeof(dst->nodes[i].local_transform.m));
        SDL_memcpy(dst->nodes[i].world_transform.m, src->nodes[i].world_transform.m, sizeof(dst->nodes[i].world_transform.m));
        dst->nodes[i].translation[0] = src->nodes[i].translation.x;
        dst->nodes[i].translation[1] = src->nodes[i].translation.y;
        dst->nodes[i].translation[2] = src->nodes[i].translation.z;
        dst->nodes[i].rotation[0] = src->nodes[i].rotation.w;
        dst->nodes[i].rotation[1] = src->nodes[i].rotation.x;
        dst->nodes[i].rotation[2] = src->nodes[i].rotation.y;
        dst->nodes[i].rotation[3] = src->nodes[i].rotation.z;
        dst->nodes[i].scale[0] = src->nodes[i].scale_xyz.x;
        dst->nodes[i].scale[1] = src->nodes[i].scale_xyz.y;
        dst->nodes[i].scale[2] = src->nodes[i].scale_xyz.z;
        if (src->nodes[i].child_count > 0) {
            if (!src->nodes[i].children) {
                SDL_SetError("glTF node child array is missing");
                return false;
            }
            dst->nodes[i].first_child = child_offset;
            for (int child = 0; child < src->nodes[i].child_count; child += 1) {
                const int child_index = src->nodes[i].children[child];

                if (child_index < 0 || child_index >= src->node_count) {
                    SDL_SetError("glTF node child index out of range");
                    return false;
                }
                dst->child_indices[child_offset + child] = child_index;
            }
            child_offset += src->nodes[i].child_count;
        }
    }

    dst->root_node_count = src->root_node_count;
    if (src->root_node_count > 0) {
        size_t root_bytes;

        if (!checked_mul_size((size_t)src->root_node_count, sizeof(*dst->root_nodes), &root_bytes)) {
            SDL_SetError("glTF root-node allocation overflow");
            return false;
        }
        dst->root_nodes = (int *)SDL_malloc(root_bytes);
        if (!dst->root_nodes) {
            SDL_OutOfMemory();
            return false;
        }
        for (int i = 0; i < src->root_node_count; i += 1) {
            const int root = src->root_nodes[i];

            if (root < 0 || root >= src->node_count) {
                SDL_SetError("glTF root node index out of range");
                return false;
            }
            dst->root_nodes[i] = root;
        }
        if (!build_node_traversal_order(dst)) {
            return false;
        }
    }

    return true;
}

static bool scene_has_unsupported_animation_channels(const ForgeGltfScene *src)
{
    for (int anim_index = 0; anim_index < src->animation_count; anim_index += 1) {
        const ForgeGltfAnimation *anim = &src->animations[anim_index];

        for (int channel_index = 0; channel_index < anim->channel_count; channel_index += 1) {
            if (anim->channels[channel_index].target_path == FORGE_GLTF_ANIM_MORPH_WEIGHTS) {
                return true;
            }
        }
    }
    return false;
}

static bool copy_gltf_animations(const ForgeGltfScene *src, ForgeGpuLoadedScene *dst)
{
    size_t animation_bytes;

    if (src->animation_count <= 0) {
        return true;
    }

    /* Keep animation retention all-or-nothing until morph targets are retained. */
    if (scene_has_unsupported_animation_channels(src)) {
        return true;
    }

    dst->animation_count = src->animation_count;
    if (!checked_mul_size((size_t)src->animation_count, sizeof(*dst->animations), &animation_bytes)) {
        SDL_SetError("glTF animation allocation overflow");
        return false;
    }
    dst->animations = (ForgeGpuSceneAnimation *)SDL_calloc(1, animation_bytes);
    if (!dst->animations) {
        SDL_OutOfMemory();
        return false;
    }

    for (int anim_index = 0; anim_index < src->animation_count; anim_index += 1) {
        const ForgeGltfAnimation *src_anim = &src->animations[anim_index];
        ForgeGpuSceneAnimation *dst_anim = &dst->animations[anim_index];

        SDL_strlcpy(dst_anim->name, src_anim->name, sizeof(dst_anim->name));
        dst_anim->duration = src_anim->duration;
        dst_anim->sampler_count = src_anim->sampler_count;
        dst_anim->channel_count = src_anim->channel_count;

        if (src_anim->sampler_count > 0) {
            size_t sampler_bytes;

            if (!checked_mul_size((size_t)src_anim->sampler_count, sizeof(*dst_anim->samplers), &sampler_bytes)) {
                SDL_SetError("glTF animation sampler allocation overflow");
                return false;
            }
            dst_anim->samplers = (ForgeGpuSceneAnimationSampler *)SDL_calloc(1, sampler_bytes);
            if (!dst_anim->samplers) {
                SDL_OutOfMemory();
                return false;
            }
            for (int sampler_index = 0; sampler_index < src_anim->sampler_count; sampler_index += 1) {
                const ForgeGltfAnimSampler *src_sampler = &src_anim->samplers[sampler_index];
                ForgeGpuSceneAnimationSampler *dst_sampler = &dst_anim->samplers[sampler_index];
                size_t timestamp_bytes;
                size_t value_count;
                size_t value_bytes;

                dst_sampler->keyframe_count = src_sampler->keyframe_count;
                dst_sampler->value_components = src_sampler->value_components;
                dst_sampler->interpolation = copy_animation_interpolation(src_sampler->interpolation);

                if (src_sampler->keyframe_count <= 0) {
                    continue;
                }
                if (!src_sampler->timestamps || !src_sampler->values || src_sampler->value_components <= 0) {
                    SDL_SetError("glTF animation sampler has incomplete keyframe data");
                    return false;
                }
                if (!checked_mul_size((size_t)src_sampler->keyframe_count, sizeof(*dst_sampler->timestamps), &timestamp_bytes) ||
                    !checked_mul_size((size_t)src_sampler->keyframe_count, (size_t)src_sampler->value_components, &value_count) ||
                    !checked_mul_size(value_count, sizeof(*dst_sampler->values), &value_bytes)) {
                    SDL_SetError("glTF animation keyframe allocation overflow");
                    return false;
                }
                dst_sampler->timestamps = (float *)SDL_malloc(timestamp_bytes);
                dst_sampler->values = (float *)SDL_malloc(value_bytes);
                if (!dst_sampler->timestamps || !dst_sampler->values) {
                    SDL_OutOfMemory();
                    return false;
                }
                SDL_memcpy(dst_sampler->timestamps, src_sampler->timestamps, timestamp_bytes);
                SDL_memcpy(dst_sampler->values, src_sampler->values, value_bytes);
            }
        }

        if (src_anim->channel_count > 0) {
            size_t channel_bytes;

            if (!checked_mul_size((size_t)src_anim->channel_count, sizeof(*dst_anim->channels), &channel_bytes)) {
                SDL_SetError("glTF animation channel allocation overflow");
                return false;
            }
            dst_anim->channels = (ForgeGpuSceneAnimationChannel *)SDL_calloc(1, channel_bytes);
            if (!dst_anim->channels) {
                SDL_OutOfMemory();
                return false;
            }
            for (int channel_index = 0; channel_index < src_anim->channel_count; channel_index += 1) {
                const ForgeGltfAnimChannel *src_channel = &src_anim->channels[channel_index];
                ForgeGpuSceneAnimationChannel *dst_channel = &dst_anim->channels[channel_index];

                dst_channel->target_node = src_channel->target_node;
                dst_channel->target_path = copy_animation_path(src_channel->target_path);
                dst_channel->sampler_index = src_channel->sampler_index;
            }
        }
    }

    return true;
}

static bool copy_gltf_materials(const ForgeGltfScene *src, ForgeGpuLoadedScene *dst)
{
    size_t material_bytes;

    dst->material_count = src->material_count;
    if (src->material_count == 0) {
        return true;
    }

    if (!checked_mul_size((size_t)src->material_count, sizeof(*dst->materials), &material_bytes)) {
        SDL_SetError("glTF material allocation overflow");
        return false;
    }

    dst->materials = (ForgeGpuSceneMaterial *)SDL_calloc(1, material_bytes);
    if (!dst->materials) {
        SDL_OutOfMemory();
        return false;
    }

    for (int i = 0; i < src->material_count; i += 1) {
        SDL_memcpy(dst->materials[i].base_color, src->materials[i].base_color, sizeof(dst->materials[i].base_color));
        SDL_strlcpy(dst->materials[i].texture_path, src->materials[i].texture_path, sizeof(dst->materials[i].texture_path));
        SDL_strlcpy(dst->materials[i].name, src->materials[i].name, sizeof(dst->materials[i].name));
        SDL_strlcpy(dst->materials[i].normal_map_path, src->materials[i].normal_map_path, sizeof(dst->materials[i].normal_map_path));
        SDL_strlcpy(dst->materials[i].metallic_roughness_path, src->materials[i].metallic_roughness_path, sizeof(dst->materials[i].metallic_roughness_path));
        SDL_strlcpy(dst->materials[i].occlusion_path, src->materials[i].occlusion_path, sizeof(dst->materials[i].occlusion_path));
        SDL_strlcpy(dst->materials[i].emissive_path, src->materials[i].emissive_path, sizeof(dst->materials[i].emissive_path));
        SDL_memcpy(dst->materials[i].emissive_factor, src->materials[i].emissive_factor, sizeof(dst->materials[i].emissive_factor));
        dst->materials[i].normal_scale = src->materials[i].normal_scale;
        dst->materials[i].metallic_factor = src->materials[i].metallic_factor;
        dst->materials[i].roughness_factor = src->materials[i].roughness_factor;
        dst->materials[i].occlusion_strength = src->materials[i].occlusion_strength;
        dst->materials[i].alpha_cutoff = src->materials[i].alpha_cutoff;
        dst->materials[i].alpha_mode = copy_alpha_mode(src->materials[i].alpha_mode);
        dst->materials[i].has_texture = src->materials[i].has_texture;
        dst->materials[i].has_normal_map = src->materials[i].has_normal_map;
        dst->materials[i].has_metallic_roughness = src->materials[i].has_metallic_roughness;
        dst->materials[i].has_occlusion = src->materials[i].has_occlusion;
        dst->materials[i].has_emissive = src->materials[i].has_emissive;
        dst->materials[i].double_sided = src->materials[i].double_sided;
    }
    return true;
}

bool ForgeGpuLoadGltfScene(const char *path, ForgeGpuLoadedScene *out_scene)
{
    return ForgeGpuLoadGltfSceneWithRequirements(path, nullptr, out_scene);
}

bool ForgeGpuLoadGltfSceneWithRequirements(
    const char *path,
    const ForgeGpuSceneLoadRequirements *requirements,
    ForgeGpuLoadedScene *out_scene)
{
    ForgeArena arena;
    ForgeGltfScene scene;
    bool ok;

    if (!path || !out_scene) {
        SDL_SetError("invalid glTF load arguments");
        return false;
    }

    SDL_zero(*out_scene);
    arena = forge_arena_create(0);
    if (!arena.first) {
        SDL_OutOfMemory();
        return false;
    }

    SDL_zero(scene);
    ok = forge_gltf_load(path, &scene, &arena);
    if (!ok) {
        forge_arena_destroy(&arena);
        SDL_SetError("failed to load forge glTF scene: %s", path);
        return false;
    }

    out_scene->available_features = collect_available_features(&scene);
    out_scene->available_all_primitive_features = collect_available_all_primitive_features(&scene);
    out_scene->available_all_material_features = collect_available_all_material_features(&scene);
    ok = copy_gltf_primitives(&scene, out_scene) &&
         copy_gltf_meshes(&scene, out_scene) &&
         copy_gltf_nodes(&scene, out_scene) &&
         copy_gltf_skins(&scene, out_scene) &&
         copy_gltf_materials(&scene, out_scene) &&
         copy_gltf_animations(&scene, out_scene);
    if (ok) {
        out_scene->retained_features = collect_retained_features(out_scene);
        out_scene->retained_all_primitive_features = collect_retained_all_primitive_features(out_scene);
        out_scene->retained_all_material_features = collect_retained_all_material_features(out_scene);
        ok = check_scene_requirements(
            requirements,
            out_scene->available_features,
            out_scene->retained_features,
            out_scene->available_all_primitive_features,
            out_scene->retained_all_primitive_features,
            out_scene->available_all_material_features,
            out_scene->retained_all_material_features);
    }

    forge_arena_destroy(&arena);
    if (!ok) {
        ForgeGpuFreeLoadedScene(out_scene);
        return false;
    }

    return true;
}

void ForgeGpuFreeLoadedScene(ForgeGpuLoadedScene *scene)
{
    if (!scene) {
        return;
    }

    if (scene->primitives) {
        for (int i = 0; i < scene->primitive_count; i += 1) {
            SDL_free(scene->primitives[i].vertices);
            SDL_free(scene->primitives[i].tangents);
            SDL_free(scene->primitives[i].joint_indices);
            SDL_free(scene->primitives[i].weights);
            SDL_free(scene->primitives[i].indices);
        }
    }
    if (scene->skins) {
        for (int i = 0; i < scene->skin_count; i += 1) {
            SDL_free(scene->skins[i].joints);
            SDL_free(scene->skins[i].inverse_bind_matrices);
        }
    }
    if (scene->animations) {
        for (int i = 0; i < scene->animation_count; i += 1) {
            if (scene->animations[i].samplers) {
                for (int sampler = 0; sampler < scene->animations[i].sampler_count; sampler += 1) {
                    SDL_free(scene->animations[i].samplers[sampler].timestamps);
                    SDL_free(scene->animations[i].samplers[sampler].values);
                }
            }
            SDL_free(scene->animations[i].samplers);
            SDL_free(scene->animations[i].channels);
        }
    }
    SDL_free(scene->primitives);
    SDL_free(scene->meshes);
    SDL_free(scene->nodes);
    SDL_free(scene->root_nodes);
    SDL_free(scene->child_indices);
    SDL_free(scene->node_traversal_order);
    SDL_free(scene->materials);
    SDL_free(scene->skins);
    SDL_free(scene->animations);
    SDL_zero(*scene);
}

static bool join_asset_path(const char *asset_root, const char *relative_path, char *out_path, size_t out_size)
{
    const char *root = asset_root ? asset_root : "";
    const size_t root_len = SDL_strlen(root);
    const bool needs_separator = root_len > 0 && root[root_len - 1] != '/';

    if (SDL_snprintf(out_path, out_size, "%s%s%s", root, needs_separator ? "/" : "", relative_path) >= (int)out_size) {
        SDL_SetError("asset self-test path too long");
        return false;
    }
    return true;
}

static bool path_ends_with(const char *path, const char *suffix)
{
    const size_t path_len = SDL_strlen(path);
    const size_t suffix_len = SDL_strlen(suffix);

    return path_len >= suffix_len && SDL_strcmp(path + path_len - suffix_len, suffix) == 0;
}

static bool validate_cesiumman_skin_primitive_weights(const ForgeGpuLoadedScene *scene, const ForgeGpuSceneSkin *skin)
{
    for (int primitive_index = 0; primitive_index < scene->primitive_count; primitive_index += 1) {
        const ForgeGpuScenePrimitive *primitive = &scene->primitives[primitive_index];

        if (!primitive->has_skin_data || !primitive->joint_indices || !primitive->weights) {
            SDL_SetError("skin primitive data was not retained");
            return false;
        }
        for (Uint32 vertex = 0; vertex < primitive->vertex_count; vertex += 1) {
            float weight_sum = 0.0f;

            for (int influence = 0; influence < FORGE_GPU_SCENE_JOINTS_PER_VERTEX; influence += 1) {
                const size_t element = (size_t)vertex * FORGE_GPU_SCENE_JOINTS_PER_VERTEX + (size_t)influence;

                if (primitive->joint_indices[element] >= skin->joint_count) {
                    SDL_SetError("skin primitive joint index exceeds skin palette");
                    return false;
                }
                weight_sum += primitive->weights[element];
            }
            if (SDL_fabsf(weight_sum - 1.0f) > 0.0005f) {
                SDL_SetError("skin primitive vertex weights are not normalized");
                return false;
            }
        }
    }
    return true;
}

static bool matrix_is_identity(ForgeGpuMat4 matrix, float epsilon)
{
    for (int i = 0; i < 16; i += 1) {
        const float expected = (i == 0 || i == 5 || i == 10 || i == 15) ? 1.0f : 0.0f;

        if (SDL_fabsf(matrix.m[i] - expected) > epsilon) {
            return false;
        }
    }
    return true;
}

bool ForgeGpuRunAssetLoaderSelfTest(const char *asset_root)
{
    ForgeGpuLoadedScene scene;
    ForgeGpuSceneLoadRequirements requirements;
    char path[FORGE_GPU_SCENE_PATH_SIZE];
    bool ok;

    SDL_zero(requirements);
    requirements.required_features = FORGE_GPU_SCENE_FEATURE_TANGENTS | FORGE_GPU_SCENE_FEATURE_PBR_MATERIALS;
    requirements.required_all_primitive_features = FORGE_GPU_SCENE_FEATURE_TANGENTS;
    requirements.required_all_material_features = FORGE_GPU_SCENE_FEATURE_PBR_MATERIALS;
    if (!join_asset_path(asset_root, "models/Suzanne/Suzanne.gltf", path, sizeof(path))) {
        return false;
    }
    SDL_zero(scene);
    if (!ForgeGpuLoadGltfSceneWithRequirements(path, &requirements, &scene)) {
        return false;
    }
    ForgeGpuFreeLoadedScene(&scene);

    SDL_zero(requirements);
    requirements.required_features = FORGE_GPU_SCENE_FEATURE_TANGENTS |
                                     FORGE_GPU_SCENE_FEATURE_ALPHA_MATERIALS |
                                     FORGE_GPU_SCENE_FEATURE_NORMAL_MAPS |
                                     FORGE_GPU_SCENE_FEATURE_PBR_MATERIALS;
    requirements.required_all_primitive_features = FORGE_GPU_SCENE_FEATURE_TANGENTS |
                                                   FORGE_GPU_SCENE_FEATURE_PRIMITIVE_BOUNDS;
    requirements.required_all_material_features = FORGE_GPU_SCENE_FEATURE_NORMAL_MAPS |
                                                  FORGE_GPU_SCENE_FEATURE_PBR_MATERIALS;
    if (!join_asset_path(asset_root, "models/NormalTangentMirrorTest/NormalTangentMirrorTest.gltf", path, sizeof(path))) {
        return false;
    }
    SDL_zero(scene);
    if (!ForgeGpuLoadGltfSceneWithRequirements(path, &requirements, &scene)) {
        return false;
    }
    if (scene.primitive_count != 1 || scene.material_count != 1) {
        SDL_SetError("NormalTangentMirrorTest expected 1 primitive and 1 material");
        ForgeGpuFreeLoadedScene(&scene);
        return false;
    }
    if (!scene.primitives[0].has_tangents || !scene.primitives[0].tangents) {
        SDL_SetError("NormalTangentMirrorTest tangent data was not retained");
        ForgeGpuFreeLoadedScene(&scene);
        return false;
    }
    if (!scene.materials[0].has_normal_map ||
        !path_ends_with(scene.materials[0].normal_map_path, "NormalTangentMirrorTest_Normal.png") ||
        scene.materials[0].normal_scale != 1.0f) {
        SDL_SetError("NormalTangentMirrorTest normal-map material data was not retained");
        ForgeGpuFreeLoadedScene(&scene);
        return false;
    }
    if (!scene.materials[0].has_texture ||
        !path_ends_with(scene.materials[0].texture_path, "NormalTangentMirrorTest_BaseColor.png")) {
        SDL_SetError("NormalTangentMirrorTest base-color material data was not retained");
        ForgeGpuFreeLoadedScene(&scene);
        return false;
    }
    if (!scene.materials[0].has_metallic_roughness ||
        !scene.materials[0].has_occlusion ||
        !path_ends_with(scene.materials[0].metallic_roughness_path, "NormalTangentMirrorTest_OcclusionRoughnessMetallic.png") ||
        !path_ends_with(scene.materials[0].occlusion_path, "NormalTangentMirrorTest_OcclusionRoughnessMetallic.png")) {
        SDL_SetError("NormalTangentMirrorTest PBR material texture facts were not retained");
        ForgeGpuFreeLoadedScene(&scene);
        return false;
    }
    ForgeGpuFreeLoadedScene(&scene);

    SDL_zero(requirements);
    requirements.required_features = FORGE_GPU_SCENE_FEATURE_NORMAL_MAPS;
    if (!join_asset_path(asset_root, "models/BoxTextured/BoxTextured.gltf", path, sizeof(path))) {
        return false;
    }
    SDL_zero(scene);
    ok = ForgeGpuLoadGltfSceneWithRequirements(path, &requirements, &scene);
    ForgeGpuFreeLoadedScene(&scene);
    if (ok) {
        SDL_SetError("asset loader self-test expected normal-map requirement to fail");
        return false;
    }
    SDL_ClearError();

    SDL_zero(requirements);
    requirements.required_all_primitive_features = FORGE_GPU_SCENE_FEATURE_TANGENTS;
    if (!join_asset_path(asset_root, "models/BoxTextured/BoxTextured.gltf", path, sizeof(path))) {
        return false;
    }
    SDL_zero(scene);
    ok = ForgeGpuLoadGltfSceneWithRequirements(path, &requirements, &scene);
    ForgeGpuFreeLoadedScene(&scene);
    if (ok) {
        SDL_SetError("asset loader self-test expected all-primitive tangent requirement to fail");
        return false;
    }
    SDL_ClearError();

    SDL_zero(requirements);
    requirements.required_features = FORGE_GPU_SCENE_FEATURE_ALPHA_MATERIALS;
    requirements.required_all_primitive_features = FORGE_GPU_SCENE_FEATURE_PRIMITIVE_BOUNDS;
    if (!join_asset_path(asset_root, "models/TransmissionOrderTest/TransmissionOrderTest.gltf", path, sizeof(path))) {
        return false;
    }
    SDL_zero(scene);
    if (!ForgeGpuLoadGltfSceneWithRequirements(path, &requirements, &scene)) {
        return false;
    }
    ForgeGpuFreeLoadedScene(&scene);

    SDL_zero(requirements);
    requirements.required_features = FORGE_GPU_SCENE_FEATURE_NODE_HIERARCHY |
                                     FORGE_GPU_SCENE_FEATURE_ANIMATIONS;
    if (!join_asset_path(asset_root, "models/CesiumMilkTruck/CesiumMilkTruck.gltf", path, sizeof(path))) {
        return false;
    }
    SDL_zero(scene);
    if (!ForgeGpuLoadGltfSceneWithRequirements(path, &requirements, &scene)) {
        return false;
    }
    if (scene.node_count != 6 || scene.root_node_count != 1 || scene.root_nodes[0] != 5 ||
        scene.node_traversal_order_count != scene.node_count) {
        SDL_SetError("CesiumMilkTruck hierarchy data was not retained as expected");
        ForgeGpuFreeLoadedScene(&scene);
        return false;
    }
    if (scene.nodes[0].parent != 1 || scene.nodes[2].parent != 3 || scene.nodes[4].parent != 5) {
        SDL_SetError("CesiumMilkTruck parent links were not retained");
        ForgeGpuFreeLoadedScene(&scene);
        return false;
    }
    if (scene.animation_count != 1 || scene.animations[0].channel_count != 2 ||
        scene.animations[0].sampler_count != 2) {
        SDL_SetError("CesiumMilkTruck animation data was not retained as expected");
        ForgeGpuFreeLoadedScene(&scene);
        return false;
    }
    for (int channel = 0; channel < scene.animations[0].channel_count; channel += 1) {
        const ForgeGpuSceneAnimationChannel *anim_channel = &scene.animations[0].channels[channel];
        const ForgeGpuSceneAnimationSampler *sampler;

        if (anim_channel->target_path != FORGE_GPU_SCENE_ANIM_ROTATION ||
            (anim_channel->target_node != 0 && anim_channel->target_node != 2) ||
            anim_channel->sampler_index < 0 ||
            anim_channel->sampler_index >= scene.animations[0].sampler_count) {
            SDL_SetError("CesiumMilkTruck wheel animation channel was not retained");
            ForgeGpuFreeLoadedScene(&scene);
            return false;
        }
        sampler = &scene.animations[0].samplers[anim_channel->sampler_index];
        if (sampler->value_components != 4 ||
            sampler->keyframe_count <= 0 ||
            !sampler->timestamps ||
            !sampler->values) {
            SDL_SetError("CesiumMilkTruck wheel animation sampler was not retained");
            ForgeGpuFreeLoadedScene(&scene);
            return false;
        }
    }
    if ((scene.retained_features & (FORGE_GPU_SCENE_FEATURE_SKINS | FORGE_GPU_SCENE_FEATURE_MORPHS)) != 0) {
        SDL_SetError("CesiumMilkTruck retained unsupported skin or morph data");
        ForgeGpuFreeLoadedScene(&scene);
        return false;
    }
    ForgeGpuFreeLoadedScene(&scene);
    if (!ForgeGpuRunSceneAnimationSelfTest(path)) {
        return false;
    }

    SDL_zero(requirements);
    requirements.required_features = FORGE_GPU_SCENE_FEATURE_NODE_HIERARCHY |
                                     FORGE_GPU_SCENE_FEATURE_SKINS |
                                     FORGE_GPU_SCENE_FEATURE_ANIMATIONS;
    requirements.required_all_primitive_features = FORGE_GPU_SCENE_FEATURE_SKINS |
                                                   FORGE_GPU_SCENE_FEATURE_PRIMITIVE_BOUNDS;
    if (!join_asset_path(asset_root, "models/CesiumMan/CesiumMan.gltf", path, sizeof(path))) {
        return false;
    }
    SDL_zero(scene);
    if (!ForgeGpuLoadGltfSceneWithRequirements(path, &requirements, &scene)) {
        return false;
    }
    if (scene.skin_count != 1 || scene.skins[0].joint_count != 19 || scene.skins[0].skeleton != 3) {
        SDL_SetError("CesiumMan skin data was not retained as expected");
        ForgeGpuFreeLoadedScene(&scene);
        return false;
    }
    if (scene.animation_count != 1 || scene.animations[0].channel_count != 57) {
        SDL_SetError("CesiumMan animation data was not retained as expected");
        ForgeGpuFreeLoadedScene(&scene);
        return false;
    }
    if (!scene.skins[0].inverse_bind_matrices ||
        matrix_is_identity(scene.skins[0].inverse_bind_matrices[0], 0.0001f)) {
        SDL_SetError("CesiumMan inverse bind matrices were not retained as expected");
        ForgeGpuFreeLoadedScene(&scene);
        return false;
    }
    if (scene.primitive_count != 1 || scene.primitives[0].vertex_count != 3273) {
        SDL_SetError("CesiumMan primitive data was not retained as expected");
        ForgeGpuFreeLoadedScene(&scene);
        return false;
    }
    {
        bool found_skinned_mesh_node = false;

        for (int node_index = 0; node_index < scene.node_count; node_index += 1) {
            if (scene.nodes[node_index].mesh_index >= 0 && scene.nodes[node_index].skin_index == 0) {
                found_skinned_mesh_node = true;
                break;
            }
        }
        if (!found_skinned_mesh_node) {
            SDL_SetError("CesiumMan skinned mesh node was not retained");
            ForgeGpuFreeLoadedScene(&scene);
            return false;
        }
    }
    if (!validate_cesiumman_skin_primitive_weights(&scene, &scene.skins[0])) {
        ForgeGpuFreeLoadedScene(&scene);
        return false;
    }
    ForgeGpuFreeLoadedScene(&scene);
    if (!ForgeGpuRunSceneSkinningSelfTest(path)) {
        return false;
    }

    return ForgeGpuRunProcessedAssetSelfTest(asset_root);
}
