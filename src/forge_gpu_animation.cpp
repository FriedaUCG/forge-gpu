#include "forge_gpu_assets.h"

#include "forge_gpu_math.h"

#define FORGE_GPU_ANIM_EPSILON 1e-7f

static Vec3 vec3_from_array(const float v[3])
{
    Vec3 result = { v[0], v[1], v[2] };
    return result;
}

static Quat quat_from_node_rotation(const float q[4])
{
    return quat_create(q[0], q[1], q[2], q[3]);
}

static Quat quat_from_gltf_rotation(const float q[4])
{
    return quat_create(q[3], q[0], q[1], q[2]);
}

static ForgeGpuMat4 forge_mat4_from_demo(Mat4 matrix)
{
    ForgeGpuMat4 result;
    SDL_memcpy(result.m, matrix.m, sizeof(result.m));
    return result;
}

static bool matrix_has_delta(ForgeGpuMat4 a, ForgeGpuMat4 b, float epsilon)
{
    for (int i = 0; i < 16; i += 1) {
        if (SDL_fabsf(a.m[i] - b.m[i]) > epsilon) {
            return true;
        }
    }
    return false;
}

static bool matrix_matches(ForgeGpuMat4 a, ForgeGpuMat4 b, float epsilon)
{
    return !matrix_has_delta(a, b, epsilon);
}

static int find_keyframe(const float *timestamps, int count, float time_seconds)
{
    int lo = 0;
    int hi = count - 1;

    while (lo + 1 < hi) {
        const int mid = (lo + hi) / 2;

        if (timestamps[mid] <= time_seconds) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    return lo;
}

static Vec3 eval_vec3_sampler(const ForgeGpuSceneAnimationSampler *sampler, float time_seconds)
{
    const float *timestamps = sampler->timestamps;
    const float *values = sampler->values;
    const int count = sampler->keyframe_count;
    int lo;
    float alpha;
    const float *a;
    const float *b;

    if (count <= 0) {
        Vec3 zero = { 0.0f, 0.0f, 0.0f };
        return zero;
    }
    if (time_seconds <= timestamps[0]) {
        Vec3 result = { values[0], values[1], values[2] };
        return result;
    }
    if (time_seconds >= timestamps[count - 1]) {
        const float *v = values + (size_t)(count - 1) * 3u;
        Vec3 result = { v[0], v[1], v[2] };
        return result;
    }

    lo = find_keyframe(timestamps, count, time_seconds);
    a = values + (size_t)lo * 3u;
    if (sampler->interpolation == FORGE_GPU_SCENE_INTERP_STEP) {
        Vec3 result = { a[0], a[1], a[2] };
        return result;
    }

    b = values + (size_t)(lo + 1) * 3u;
    alpha = timestamps[lo + 1] - timestamps[lo];
    alpha = alpha > FORGE_GPU_ANIM_EPSILON ? (time_seconds - timestamps[lo]) / alpha : 0.0f;
    return vec3_lerp(vec3_from_array(a), vec3_from_array(b), alpha);
}

static Quat eval_quat_sampler(const ForgeGpuSceneAnimationSampler *sampler, float time_seconds)
{
    const float *timestamps = sampler->timestamps;
    const float *values = sampler->values;
    const int count = sampler->keyframe_count;
    int lo;
    float alpha;
    const float *a;
    const float *b;

    if (count <= 0) {
        return quat_create(1.0f, 0.0f, 0.0f, 0.0f);
    }
    if (time_seconds <= timestamps[0]) {
        return quat_from_gltf_rotation(values);
    }
    if (time_seconds >= timestamps[count - 1]) {
        return quat_from_gltf_rotation(values + (size_t)(count - 1) * 4u);
    }

    lo = find_keyframe(timestamps, count, time_seconds);
    a = values + (size_t)lo * 4u;
    if (sampler->interpolation == FORGE_GPU_SCENE_INTERP_STEP) {
        return quat_from_gltf_rotation(a);
    }

    b = values + (size_t)(lo + 1) * 4u;
    alpha = timestamps[lo + 1] - timestamps[lo];
    alpha = alpha > FORGE_GPU_ANIM_EPSILON ? (time_seconds - timestamps[lo]) / alpha : 0.0f;
    return quat_slerp(quat_from_gltf_rotation(a), quat_from_gltf_rotation(b), alpha);
}

static ForgeGpuMat4 compose_node_local_transform(const ForgeGpuSceneNode *node)
{
    const Mat4 translation = mat4_translate(vec3_from_array(node->translation));
    const Mat4 rotation = quat_to_mat4(quat_from_node_rotation(node->rotation));
    const Mat4 scale = mat4_scale_vec3(vec3_from_array(node->scale));
    return forge_mat4_from_demo(mat4_multiply(translation, mat4_multiply(rotation, scale)));
}

static void write_vec3(float out[3], Vec3 value)
{
    out[0] = value.x;
    out[1] = value.y;
    out[2] = value.z;
}

static void write_quat(float out[4], Quat value)
{
    out[0] = value.w;
    out[1] = value.x;
    out[2] = value.y;
    out[3] = value.z;
}

static float normalize_animation_time(const ForgeGpuSceneAnimation *animation, float time_seconds, bool loop)
{
    if (animation->duration <= FORGE_GPU_ANIM_EPSILON) {
        return 0.0f;
    }
    if (loop) {
        time_seconds = SDL_fmodf(time_seconds, animation->duration);
        if (time_seconds < 0.0f) {
            time_seconds += animation->duration;
        }
    } else {
        if (time_seconds < 0.0f) {
            time_seconds = 0.0f;
        }
        if (time_seconds > animation->duration) {
            time_seconds = animation->duration;
        }
    }
    return time_seconds;
}

bool ForgeGpuRecomputeSceneWorldTransforms(ForgeGpuLoadedScene *scene)
{
    if (!scene) {
        SDL_SetError("invalid scene transform recompute arguments");
        return false;
    }
    if (scene->node_count <= 0) {
        return true;
    }
    if (!scene->nodes ||
        !scene->node_traversal_order ||
        scene->node_traversal_order_count != scene->node_count) {
        SDL_SetError("scene node hierarchy is not retained");
        return false;
    }

    for (int order_index = 0; order_index < scene->node_traversal_order_count; order_index += 1) {
        const int node_index = scene->node_traversal_order[order_index];
        ForgeGpuSceneNode *node;
        Mat4 local;

        if (node_index < 0 || node_index >= scene->node_count) {
            SDL_SetError("scene node traversal order contains an out-of-range node");
            return false;
        }

        node = &scene->nodes[node_index];
        local = mat4_from_forge(node->local_transform);
        if (node->parent >= 0) {
            if (node->parent >= scene->node_count) {
                SDL_SetError("scene node parent index is out of range");
                return false;
            }
            node->world_transform = forge_mat4_from_demo(
                mat4_multiply(mat4_from_forge(scene->nodes[node->parent].world_transform), local));
        } else {
            node->world_transform = node->local_transform;
        }
    }

    return true;
}

bool ForgeGpuApplySceneAnimation(ForgeGpuLoadedScene *scene, int animation_index, float time_seconds, bool loop)
{
    const ForgeGpuSceneAnimation *animation;
    Uint8 *modified;

    if (!scene || !scene->nodes || animation_index < 0 || animation_index >= scene->animation_count) {
        SDL_SetError("invalid scene animation arguments");
        return false;
    }
    animation = &scene->animations[animation_index];
    time_seconds = normalize_animation_time(animation, time_seconds, loop);

    modified = (Uint8 *)SDL_calloc((size_t)scene->node_count, sizeof(*modified));
    if (!modified) {
        SDL_OutOfMemory();
        return false;
    }

    for (int channel_index = 0; channel_index < animation->channel_count; channel_index += 1) {
        const ForgeGpuSceneAnimationChannel *channel = &animation->channels[channel_index];
        const ForgeGpuSceneAnimationSampler *sampler;
        ForgeGpuSceneNode *node;

        if (channel->target_node < 0 || channel->target_node >= scene->node_count ||
            channel->sampler_index < 0 || channel->sampler_index >= animation->sampler_count) {
            continue;
        }
        sampler = &animation->samplers[channel->sampler_index];
        if (!sampler->timestamps || !sampler->values || sampler->keyframe_count <= 0) {
            continue;
        }

        node = &scene->nodes[channel->target_node];
        if (!node->has_trs) {
            SDL_free(modified);
            SDL_SetError("scene animation targets a matrix-only node");
            return false;
        }

        switch (channel->target_path) {
        case FORGE_GPU_SCENE_ANIM_TRANSLATION:
            if (sampler->value_components != 3) {
                SDL_free(modified);
                SDL_SetError("scene translation animation sampler has wrong component count");
                return false;
            }
            write_vec3(node->translation, eval_vec3_sampler(sampler, time_seconds));
            break;
        case FORGE_GPU_SCENE_ANIM_ROTATION:
            if (sampler->value_components != 4) {
                SDL_free(modified);
                SDL_SetError("scene rotation animation sampler has wrong component count");
                return false;
            }
            write_quat(node->rotation, eval_quat_sampler(sampler, time_seconds));
            break;
        case FORGE_GPU_SCENE_ANIM_SCALE:
            if (sampler->value_components != 3) {
                SDL_free(modified);
                SDL_SetError("scene scale animation sampler has wrong component count");
                return false;
            }
            write_vec3(node->scale, eval_vec3_sampler(sampler, time_seconds));
            break;
        case FORGE_GPU_SCENE_ANIM_MORPH_WEIGHTS:
        default:
            SDL_free(modified);
            SDL_SetError("scene morph animation is not retained");
            return false;
        }

        modified[channel->target_node] = 1;
    }

    for (int node_index = 0; node_index < scene->node_count; node_index += 1) {
        if (modified[node_index]) {
            scene->nodes[node_index].local_transform = compose_node_local_transform(&scene->nodes[node_index]);
        }
    }

    SDL_free(modified);
    return true;
}

bool ForgeGpuComputeSkinJointMatrices(
    const ForgeGpuLoadedScene *scene,
    int mesh_node_index,
    ForgeGpuMat4 *out_matrices,
    int matrix_capacity,
    int *out_matrix_count)
{
    const ForgeGpuSceneNode *mesh_node;
    const ForgeGpuSceneSkin *skin;
    Mat4 inverse_mesh_world;

    if (!scene || !scene->nodes || !scene->skins || !out_matrices || !out_matrix_count ||
        mesh_node_index < 0 || mesh_node_index >= scene->node_count) {
        SDL_SetError("invalid scene skinning arguments");
        return false;
    }

    mesh_node = &scene->nodes[mesh_node_index];
    if (mesh_node->skin_index < 0 || mesh_node->skin_index >= scene->skin_count) {
        SDL_SetError("scene node does not reference a retained skin");
        return false;
    }

    skin = &scene->skins[mesh_node->skin_index];
    if (skin->joint_count < 0 || matrix_capacity < skin->joint_count ||
        (skin->joint_count > 0 && (!skin->joints || !skin->inverse_bind_matrices))) {
        SDL_SetError("scene skin joint palette is not retained");
        return false;
    }

    inverse_mesh_world = mat4_inverse(mat4_from_forge(mesh_node->world_transform));
    for (int joint = 0; joint < skin->joint_count; joint += 1) {
        const int node_index = skin->joints[joint];
        Mat4 joint_world;
        Mat4 inverse_bind;

        if (node_index < 0 || node_index >= scene->node_count) {
            SDL_SetError("scene skin joint index is out of range");
            return false;
        }

        joint_world = mat4_from_forge(scene->nodes[node_index].world_transform);
        inverse_bind = mat4_from_forge(skin->inverse_bind_matrices[joint]);
        out_matrices[joint] = forge_mat4_from_demo(
            mat4_multiply(inverse_mesh_world, mat4_multiply(joint_world, inverse_bind)));
    }
    *out_matrix_count = skin->joint_count;
    return true;
}

bool ForgeGpuRunSceneAnimationSelfTest(const char *scene_path)
{
    ForgeGpuLoadedScene scene;
    ForgeGpuSceneLoadRequirements requirements;
    ForgeGpuMat4 *load_worlds = nullptr;
    bool changed = false;
    bool checked_trs = false;
    bool ok = false;

    if (!scene_path || scene_path[0] == '\0') {
        SDL_SetError("scene animation self-test path is empty");
        return false;
    }

    SDL_zero(scene);
    SDL_zero(requirements);
    requirements.required_features = FORGE_GPU_SCENE_FEATURE_NODE_HIERARCHY |
                                     FORGE_GPU_SCENE_FEATURE_ANIMATIONS;
    if (!ForgeGpuLoadGltfSceneWithRequirements(scene_path, &requirements, &scene)) {
        return false;
    }

    load_worlds = (ForgeGpuMat4 *)SDL_malloc((size_t)scene.node_count * sizeof(*load_worlds));
    if (!load_worlds) {
        SDL_OutOfMemory();
        goto done;
    }
    for (int i = 0; i < scene.node_count; i += 1) {
        load_worlds[i] = scene.nodes[i].world_transform;
    }
    for (int i = 0; i < scene.node_count; i += 1) {
        if (scene.nodes[i].has_trs) {
            checked_trs = true;
            if (!matrix_matches(scene.nodes[i].local_transform, compose_node_local_transform(&scene.nodes[i]), 0.0001f)) {
                SDL_SetError("scene TRS composition diverged from loaded local transform");
                goto done;
            }
        }
    }
    if (!checked_trs) {
        SDL_SetError("scene animation self-test did not find a retained TRS node");
        goto done;
    }

    if (!ForgeGpuRecomputeSceneWorldTransforms(&scene)) {
        goto done;
    }
    for (int i = 0; i < scene.node_count; i += 1) {
        if (!matrix_matches(load_worlds[i], scene.nodes[i].world_transform, 0.0001f)) {
            SDL_SetError("scene hierarchy recompute diverged from loaded world transforms");
            goto done;
        }
    }

    if (!ForgeGpuApplySceneAnimation(&scene, 0, scene.animations[0].duration * 0.5f, true) ||
        !ForgeGpuRecomputeSceneWorldTransforms(&scene)) {
        goto done;
    }

    for (int channel_index = 0; channel_index < scene.animations[0].channel_count; channel_index += 1) {
        const int target = scene.animations[0].channels[channel_index].target_node;

        if (target >= 0 && target < scene.node_count &&
            matrix_has_delta(load_worlds[target], scene.nodes[target].world_transform, 0.0001f)) {
            changed = true;
            break;
        }
    }
    if (!changed) {
        SDL_SetError("scene animation did not change any targeted node world transform");
        goto done;
    }

    ok = true;

done:
    SDL_free(load_worlds);
    ForgeGpuFreeLoadedScene(&scene);
    return ok;
}

bool ForgeGpuRunSceneSkinningSelfTest(const char *scene_path)
{
    ForgeGpuLoadedScene scene;
    ForgeGpuSceneLoadRequirements requirements;
    ForgeGpuMat4 *bind_matrices = nullptr;
    ForgeGpuMat4 *animated_matrices = nullptr;
    int bind_matrix_count = 0;
    int animated_matrix_count = 0;
    int mesh_node_index = -1;
    int skin_index;
    int joint_count;
    bool changed = false;
    bool ok = false;

    if (!scene_path || scene_path[0] == '\0') {
        SDL_SetError("scene skinning self-test path is empty");
        return false;
    }

    SDL_zero(scene);
    SDL_zero(requirements);
    requirements.required_features = FORGE_GPU_SCENE_FEATURE_NODE_HIERARCHY |
                                     FORGE_GPU_SCENE_FEATURE_SKINS |
                                     FORGE_GPU_SCENE_FEATURE_ANIMATIONS;
    requirements.required_all_primitive_features = FORGE_GPU_SCENE_FEATURE_SKINS;
    if (!ForgeGpuLoadGltfSceneWithRequirements(scene_path, &requirements, &scene)) {
        return false;
    }

    for (int node_index = 0; node_index < scene.node_count; node_index += 1) {
        if (scene.nodes[node_index].mesh_index >= 0 && scene.nodes[node_index].skin_index >= 0) {
            mesh_node_index = node_index;
            break;
        }
    }
    if (mesh_node_index < 0) {
        SDL_SetError("scene skinning self-test did not find a skinned mesh node");
        goto done;
    }
    skin_index = scene.nodes[mesh_node_index].skin_index;
    joint_count = scene.skins[skin_index].joint_count;
    if (joint_count <= 0) {
        SDL_SetError("scene skinning self-test found an empty joint palette");
        goto done;
    }

    bind_matrices = (ForgeGpuMat4 *)SDL_malloc((size_t)joint_count * sizeof(*bind_matrices));
    animated_matrices = (ForgeGpuMat4 *)SDL_malloc((size_t)joint_count * sizeof(*animated_matrices));
    if (!bind_matrices || !animated_matrices) {
        SDL_OutOfMemory();
        goto done;
    }

    if (!ForgeGpuComputeSkinJointMatrices(
            &scene,
            mesh_node_index,
            bind_matrices,
            joint_count,
            &bind_matrix_count)) {
        goto done;
    }
    if (bind_matrix_count <= 0) {
        SDL_SetError("scene skinning self-test did not compute a joint palette");
        goto done;
    }

    if (!ForgeGpuApplySceneAnimation(&scene, 0, scene.animations[0].duration * 0.5f, true) ||
        !ForgeGpuRecomputeSceneWorldTransforms(&scene) ||
        !ForgeGpuComputeSkinJointMatrices(
            &scene,
            mesh_node_index,
            animated_matrices,
            joint_count,
            &animated_matrix_count)) {
        goto done;
    }
    if (animated_matrix_count != bind_matrix_count) {
        SDL_SetError("scene skinning self-test changed joint palette size");
        goto done;
    }

    for (int joint = 0; joint < bind_matrix_count; joint += 1) {
        if (matrix_has_delta(bind_matrices[joint], animated_matrices[joint], 0.0001f)) {
            changed = true;
            break;
        }
    }
    if (!changed) {
        SDL_SetError("scene animation did not change any joint matrix");
        goto done;
    }

    ok = true;

done:
    SDL_free(bind_matrices);
    SDL_free(animated_matrices);
    ForgeGpuFreeLoadedScene(&scene);
    return ok;
}
