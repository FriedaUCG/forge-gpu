#ifndef SDLGPU_FORGE_GPU_ASSETS_H
#define SDLGPU_FORGE_GPU_ASSETS_H

#include <SDL3/SDL.h>

#define FORGE_GPU_SCENE_NAME_SIZE 64
#define FORGE_GPU_SCENE_PATH_SIZE 512
#define FORGE_GPU_SCENE_JOINTS_PER_VERTEX 4

struct ForgeGpuMat4
{
    float m[16];
};

struct ForgeGpuMeshVertex
{
    float position[3];
    float normal[3];
    float uv[2];
};

typedef Uint64 ForgeGpuSceneFeatureFlags;

#define FORGE_GPU_SCENE_FEATURE_TANGENTS          SDL_UINT64_C(0x00000001)
#define FORGE_GPU_SCENE_FEATURE_ALPHA_MATERIALS   SDL_UINT64_C(0x00000002)
#define FORGE_GPU_SCENE_FEATURE_NORMAL_MAPS       SDL_UINT64_C(0x00000004)
#define FORGE_GPU_SCENE_FEATURE_PBR_MATERIALS     SDL_UINT64_C(0x00000008)
#define FORGE_GPU_SCENE_FEATURE_NODE_HIERARCHY    SDL_UINT64_C(0x00000010)
#define FORGE_GPU_SCENE_FEATURE_SKINS             SDL_UINT64_C(0x00000020)
#define FORGE_GPU_SCENE_FEATURE_ANIMATIONS        SDL_UINT64_C(0x00000040)
#define FORGE_GPU_SCENE_FEATURE_MORPHS            SDL_UINT64_C(0x00000080)
#define FORGE_GPU_SCENE_FEATURE_PRIMITIVE_BOUNDS  SDL_UINT64_C(0x00000100)

struct ForgeGpuSceneLoadRequirements
{
    ForgeGpuSceneFeatureFlags required_features;
    ForgeGpuSceneFeatureFlags required_all_primitive_features;
    ForgeGpuSceneFeatureFlags required_all_material_features;
};

struct ForgeGpuLoadedMesh
{
    ForgeGpuMeshVertex *vertices;
    Uint32 vertex_count;
};

enum ForgeGpuSceneAlphaMode
{
    FORGE_GPU_SCENE_ALPHA_OPAQUE = 0,
    FORGE_GPU_SCENE_ALPHA_MASK = 1,
    FORGE_GPU_SCENE_ALPHA_BLEND = 2
};

struct ForgeGpuScenePrimitive
{
    ForgeGpuMeshVertex *vertices;
    float *tangents; /* vertex_count * 4 floats; NULL when absent */
    Uint32 vertex_count;
    void *indices;
    Uint32 index_count;
    Uint32 index_stride;
    int material_index;
    Uint16 *joint_indices; /* vertex_count * FORGE_GPU_SCENE_JOINTS_PER_VERTEX */
    float *weights;        /* vertex_count * FORGE_GPU_SCENE_JOINTS_PER_VERTEX */
    bool has_uvs;
    bool has_tangents;
    bool has_skin_data;
    bool has_bounds;
    float aabb_min[3];
    float aabb_max[3];
};

struct ForgeGpuSceneMesh
{
    int first_primitive;
    int primitive_count;
};

struct ForgeGpuSceneMaterial
{
    float base_color[4];
    char texture_path[FORGE_GPU_SCENE_PATH_SIZE];
    char name[FORGE_GPU_SCENE_NAME_SIZE];
    char normal_map_path[FORGE_GPU_SCENE_PATH_SIZE];
    char metallic_roughness_path[FORGE_GPU_SCENE_PATH_SIZE];
    char occlusion_path[FORGE_GPU_SCENE_PATH_SIZE];
    char emissive_path[FORGE_GPU_SCENE_PATH_SIZE];
    float emissive_factor[3];
    float normal_scale;
    float metallic_factor;
    float roughness_factor;
    float occlusion_strength;
    float alpha_cutoff;
    ForgeGpuSceneAlphaMode alpha_mode;
    bool has_texture;
    bool has_normal_map;
    bool has_metallic_roughness;
    bool has_occlusion;
    bool has_emissive;
    bool double_sided;
};

enum ForgeGpuSceneAnimationPath
{
    FORGE_GPU_SCENE_ANIM_TRANSLATION = 0,
    FORGE_GPU_SCENE_ANIM_ROTATION = 1,
    FORGE_GPU_SCENE_ANIM_SCALE = 2,
    FORGE_GPU_SCENE_ANIM_MORPH_WEIGHTS = 3
};

enum ForgeGpuSceneAnimationInterpolation
{
    FORGE_GPU_SCENE_INTERP_LINEAR = 0,
    FORGE_GPU_SCENE_INTERP_STEP = 1
};

struct ForgeGpuSceneAnimationSampler
{
    float *timestamps;
    float *values;
    int keyframe_count;
    int value_components;
    ForgeGpuSceneAnimationInterpolation interpolation;
};

struct ForgeGpuSceneAnimationChannel
{
    int target_node;
    ForgeGpuSceneAnimationPath target_path;
    int sampler_index;
};

struct ForgeGpuSceneSkin
{
    char name[FORGE_GPU_SCENE_NAME_SIZE];
    int *joints;
    int joint_count;
    int skeleton;
    ForgeGpuMat4 *inverse_bind_matrices;
};

struct ForgeGpuSceneAnimation
{
    char name[FORGE_GPU_SCENE_NAME_SIZE];
    float duration;
    ForgeGpuSceneAnimationSampler *samplers;
    int sampler_count;
    ForgeGpuSceneAnimationChannel *channels;
    int channel_count;
};

struct ForgeGpuSceneNode
{
    int mesh_index;
    int parent;
    int first_child;
    int child_count;
    int skin_index;
    bool has_trs;
    char name[FORGE_GPU_SCENE_NAME_SIZE];
    ForgeGpuMat4 local_transform;
    ForgeGpuMat4 world_transform;
    float translation[3];
    float rotation[4]; /* w, x, y, z */
    float scale[3];
};

struct ForgeGpuLoadedScene
{
    ForgeGpuSceneNode *nodes;
    int node_count;
    int *root_nodes;
    int root_node_count;
    int *child_indices;
    int child_index_count;
    int *node_traversal_order;
    int node_traversal_order_count;
    ForgeGpuSceneMesh *meshes;
    int mesh_count;
    ForgeGpuScenePrimitive *primitives;
    int primitive_count;
    ForgeGpuSceneMaterial *materials;
    int material_count;
    ForgeGpuSceneSkin *skins;
    int skin_count;
    ForgeGpuSceneAnimation *animations;
    int animation_count;
    /* CPU-side glTF facts retained by this wrapper, not a GPU texture-slot guarantee. */
    ForgeGpuSceneFeatureFlags available_features;
    ForgeGpuSceneFeatureFlags retained_features;
    ForgeGpuSceneFeatureFlags available_all_primitive_features;
    ForgeGpuSceneFeatureFlags retained_all_primitive_features;
    ForgeGpuSceneFeatureFlags available_all_material_features;
    ForgeGpuSceneFeatureFlags retained_all_material_features;
};

bool ForgeGpuLoadObjMesh(const char *path, ForgeGpuLoadedMesh *out_mesh);
void ForgeGpuFreeLoadedMesh(ForgeGpuLoadedMesh *mesh);

bool ForgeGpuLoadGltfScene(const char *path, ForgeGpuLoadedScene *out_scene);
bool ForgeGpuLoadGltfSceneWithRequirements(
    const char *path,
    const ForgeGpuSceneLoadRequirements *requirements,
    ForgeGpuLoadedScene *out_scene);
void ForgeGpuFreeLoadedScene(ForgeGpuLoadedScene *scene);
bool ForgeGpuApplySceneAnimation(ForgeGpuLoadedScene *scene, int animation_index, float time_seconds, bool loop);
bool ForgeGpuRecomputeSceneWorldTransforms(ForgeGpuLoadedScene *scene);
bool ForgeGpuComputeSkinJointMatrices(
    const ForgeGpuLoadedScene *scene,
    int mesh_node_index,
    ForgeGpuMat4 *out_matrices,
    int matrix_capacity,
    int *out_matrix_count);

bool ForgeGpuRunAssetLoaderSelfTest(const char *asset_root);
bool ForgeGpuRunSceneAnimationSelfTest(const char *scene_path);
bool ForgeGpuRunSceneSkinningSelfTest(const char *scene_path);

#endif /* SDLGPU_FORGE_GPU_ASSETS_H */
