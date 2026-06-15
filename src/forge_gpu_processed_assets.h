#ifndef SDLGPU_FORGE_GPU_PROCESSED_ASSETS_H
#define SDLGPU_FORGE_GPU_PROCESSED_ASSETS_H

#include <SDL3/SDL.h>

#define FORGE_GPU_PROCESSED_VERTEX_STRIDE_NO_TANGENTS 32u
#define FORGE_GPU_PROCESSED_VERTEX_STRIDE_TANGENTS 48u
#define FORGE_GPU_PROCESSED_VERTEX_STRIDE_SKINNED 56u
#define FORGE_GPU_PROCESSED_VERTEX_STRIDE_SKINNED_TANGENTS 72u
#define FORGE_GPU_PROCESSED_MESH_FLAG_TANGENTS (1u << 0)
#define FORGE_GPU_PROCESSED_MESH_FLAG_SKINNED (1u << 1)
#define FORGE_GPU_PROCESSED_MESH_FLAG_MORPHS (1u << 2)
#define FORGE_GPU_PROCESSED_MORPH_NAME_SIZE 64
#define FORGE_GPU_PROCESSED_MORPH_MAX_TARGETS 8u
#define FORGE_GPU_PROCESSED_MORPH_ATTR_POSITION (1u << 0)
#define FORGE_GPU_PROCESSED_MORPH_ATTR_NORMAL (1u << 1)
#define FORGE_GPU_PROCESSED_MORPH_ATTR_TANGENT (1u << 2)
#define FORGE_GPU_PROCESSED_MATERIAL_PATH_SIZE 512
#define FORGE_GPU_PROCESSED_ALPHA_OPAQUE 0
#define FORGE_GPU_PROCESSED_ALPHA_MASK 1
#define FORGE_GPU_PROCESSED_ALPHA_BLEND 2
#define FORGE_GPU_PROCESSED_FTEX_BC7_SRGB 1
#define FORGE_GPU_PROCESSED_FTEX_BC7_UNORM 2
#define FORGE_GPU_PROCESSED_FTEX_BC5_UNORM 3
#define FORGE_GPU_PROCESSED_ANIM_PATH_TRANSLATION 0u
#define FORGE_GPU_PROCESSED_ANIM_PATH_ROTATION 1u
#define FORGE_GPU_PROCESSED_ANIM_PATH_SCALE 2u
#define FORGE_GPU_PROCESSED_ANIM_PATH_MORPH_WEIGHTS 3u
#define FORGE_GPU_PROCESSED_ANIM_INTERPOLATION_LINEAR 0u
#define FORGE_GPU_PROCESSED_ANIM_INTERPOLATION_STEP 1u

typedef struct ForgeGpuProcessedLod
{
    Uint32 index_count;
    Uint32 index_offset;
    float target_error;
} ForgeGpuProcessedLod;

typedef struct ForgeGpuProcessedSubmesh
{
    Uint32 index_count;
    Uint32 index_offset;
    Sint32 material_index;
} ForgeGpuProcessedSubmesh;

typedef struct ForgeGpuProcessedMorphTarget
{
    char name[FORGE_GPU_PROCESSED_MORPH_NAME_SIZE];
    float default_weight;
    float *position_deltas;
    float *normal_deltas;
    float *tangent_deltas;
} ForgeGpuProcessedMorphTarget;

typedef struct ForgeGpuProcessedMesh
{
    void *vertices;
    Uint32 *indices;
    Uint32 vertex_count;
    Uint32 vertex_stride;
    ForgeGpuProcessedLod *lods;
    Uint32 lod_count;
    Uint32 flags;
    ForgeGpuProcessedSubmesh *submeshes;
    Uint32 submesh_count;
    Uint32 total_index_count;
    ForgeGpuProcessedMorphTarget *morph_targets;
    Uint32 morph_target_count;
    Uint32 morph_attribute_flags;
} ForgeGpuProcessedMesh;

typedef struct ForgeGpuProcessedMaterial
{
    char name[64];
    float base_color_factor[4];
    char base_color_texture[FORGE_GPU_PROCESSED_MATERIAL_PATH_SIZE];
    float metallic_factor;
    float roughness_factor;
    char metallic_roughness_texture[FORGE_GPU_PROCESSED_MATERIAL_PATH_SIZE];
    char roughness_texture[FORGE_GPU_PROCESSED_MATERIAL_PATH_SIZE];
    char metallic_texture[FORGE_GPU_PROCESSED_MATERIAL_PATH_SIZE];
    char normal_texture[FORGE_GPU_PROCESSED_MATERIAL_PATH_SIZE];
    float normal_scale;
    char occlusion_texture[FORGE_GPU_PROCESSED_MATERIAL_PATH_SIZE];
    float occlusion_strength;
    float emissive_factor[3];
    char emissive_texture[FORGE_GPU_PROCESSED_MATERIAL_PATH_SIZE];
    int alpha_mode;
    float alpha_cutoff;
    bool double_sided;
} ForgeGpuProcessedMaterial;

typedef struct ForgeGpuProcessedMaterialSet
{
    /* Zero-material sets are valid and leave materials NULL. */
    ForgeGpuProcessedMaterial *materials;
    Uint32 material_count;
} ForgeGpuProcessedMaterialSet;

typedef struct ForgeGpuProcessedSceneMesh
{
    Uint32 first_submesh;
    Uint32 submesh_count;
} ForgeGpuProcessedSceneMesh;

typedef struct ForgeGpuProcessedSceneNode
{
    char name[64];
    Sint32 parent;
    Sint32 mesh_index;
    Sint32 skin_index;
    Uint32 first_child;
    Uint32 child_count;
    bool has_trs;
    float translation[3];
    float rotation[4];
    float scale[3];
    float local_transform[16];
    float world_transform[16];
} ForgeGpuProcessedSceneNode;

typedef struct ForgeGpuProcessedScene
{
    ForgeGpuProcessedSceneNode *nodes;
    Uint32 node_count;
    ForgeGpuProcessedSceneMesh *meshes;
    Uint32 mesh_count;
    Uint32 *roots;
    Uint32 root_count;
    Uint32 *children;
    Uint32 child_count;
} ForgeGpuProcessedScene;

typedef struct ForgeGpuProcessedSkin
{
    char name[64];
    Sint32 *joints;
    float *inverse_bind_matrices;
    Uint32 joint_count;
    Sint32 skeleton;
} ForgeGpuProcessedSkin;

typedef struct ForgeGpuProcessedSkinSet
{
    ForgeGpuProcessedSkin *skins;
    Uint32 skin_count;
} ForgeGpuProcessedSkinSet;

typedef struct ForgeGpuProcessedAnimationSampler
{
    float *times;
    float *values;
    Uint32 keyframe_count;
    Uint32 value_components;
    Uint32 interpolation;
} ForgeGpuProcessedAnimationSampler;

typedef struct ForgeGpuProcessedAnimationChannel
{
    Sint32 target_node;
    Uint32 target_path;
    Uint32 sampler_index;
} ForgeGpuProcessedAnimationChannel;

typedef struct ForgeGpuProcessedAnimationClip
{
    char name[64];
    ForgeGpuProcessedAnimationSampler *samplers;
    ForgeGpuProcessedAnimationChannel *channels;
    float duration;
    Uint32 sampler_count;
    Uint32 channel_count;
} ForgeGpuProcessedAnimationClip;

typedef struct ForgeGpuProcessedAnimation
{
    ForgeGpuProcessedAnimationClip *clips;
    Uint32 clip_count;
} ForgeGpuProcessedAnimation;

typedef struct ForgeGpuProcessedCompressedMip
{
    const Uint8 *data;
    Uint32 data_size;
    Uint32 width;
    Uint32 height;
} ForgeGpuProcessedCompressedMip;

typedef struct ForgeGpuProcessedCompressedTexture
{
    Uint8 *file_data;
    ForgeGpuProcessedCompressedMip *mips;
    Uint32 mip_count;
    Uint32 width;
    Uint32 height;
    Uint32 format;
} ForgeGpuProcessedCompressedTexture;

typedef struct ForgeGpuProcessedTextureCompressionInfo
{
    char ftex_file[FORGE_GPU_PROCESSED_MATERIAL_PATH_SIZE];
    Uint64 ftex_bytes;
    Uint32 ftex_format;
    Uint32 output_width;
    Uint32 output_height;
    Uint32 mip_count;
    bool normal_map;
} ForgeGpuProcessedTextureCompressionInfo;

bool ForgeGpuLoadProcessedMesh(const char *path, ForgeGpuProcessedMesh *mesh);
void ForgeGpuFreeProcessedMesh(ForgeGpuProcessedMesh *mesh);
bool ForgeGpuProcessedMeshHasTangents(const ForgeGpuProcessedMesh *mesh);
bool ForgeGpuProcessedMeshIsSkinned(const ForgeGpuProcessedMesh *mesh);
bool ForgeGpuProcessedMeshHasMorphs(const ForgeGpuProcessedMesh *mesh);
Uint32 ForgeGpuProcessedMeshLodIndexCount(const ForgeGpuProcessedMesh *mesh, Uint32 lod);
Uint32 ForgeGpuProcessedMeshLodFirstIndex(const ForgeGpuProcessedMesh *mesh, Uint32 lod);

bool ForgeGpuLoadProcessedMaterials(const char *path, ForgeGpuProcessedMaterialSet *set);
void ForgeGpuFreeProcessedMaterials(ForgeGpuProcessedMaterialSet *set);
bool ForgeGpuLoadProcessedSceneV1(const char *path, ForgeGpuProcessedScene *scene);
void ForgeGpuFreeProcessedScene(ForgeGpuProcessedScene *scene);
bool ForgeGpuRecomputeProcessedSceneWorldTransforms(ForgeGpuProcessedScene *scene);
bool ForgeGpuValidateProcessedSceneModelReferences(
    const ForgeGpuProcessedScene *scene,
    const ForgeGpuProcessedMesh *mesh,
    const ForgeGpuProcessedMaterialSet *materials,
    const char *label);
bool ForgeGpuLoadProcessedSkins(const char *path, ForgeGpuProcessedSkinSet *set);
void ForgeGpuFreeProcessedSkins(ForgeGpuProcessedSkinSet *set);
bool ForgeGpuValidateProcessedSceneSkinReferences(
    const ForgeGpuProcessedScene *scene,
    const ForgeGpuProcessedMesh *mesh,
    const ForgeGpuProcessedSkinSet *skins,
    const char *label);
bool ForgeGpuLoadProcessedAnimation(const char *path, ForgeGpuProcessedAnimation *animation);
void ForgeGpuFreeProcessedAnimation(ForgeGpuProcessedAnimation *animation);
bool ForgeGpuApplyProcessedSceneAnimation(
    ForgeGpuProcessedScene *scene,
    const ForgeGpuProcessedAnimationClip *clip,
    float time_seconds,
    bool loop);
bool ForgeGpuEvaluateProcessedMorphWeights(
    const ForgeGpuProcessedAnimationClip *clip,
    Sint32 target_node,
    float time_seconds,
    bool loop,
    float *weights,
    Uint32 weight_count);
bool ForgeGpuComputeProcessedSkinJointMatrices(
    const ForgeGpuProcessedScene *scene,
    const ForgeGpuProcessedSkin *skin,
    Sint32 mesh_node_index,
    float *out_matrices,
    Uint32 matrix_capacity,
    Uint32 *out_matrix_count);
bool ForgeGpuValidateProcessedSceneAnimationReferences(
    const ForgeGpuProcessedScene *scene,
    const ForgeGpuProcessedAnimation *animation,
    const char *label);
bool ForgeGpuLoadProcessedFtexV1(const char *path, ForgeGpuProcessedCompressedTexture *texture);
void ForgeGpuFreeProcessedCompressedTexture(ForgeGpuProcessedCompressedTexture *texture);
const char *ForgeGpuProcessedBasename(const char *path);
bool ForgeGpuValidateProcessedTextureSidecar(const char *image_path, Uint32 *out_width, Uint32 *out_height);
bool ForgeGpuLoadProcessedTextureCompressionSidecar(
    const char *image_path,
    ForgeGpuProcessedTextureCompressionInfo *info);
Uint64 ForgeGpuEstimateProcessedRgba8MipBytes(Uint32 width, Uint32 height);
bool ForgeGpuRunProcessedAssetSelfTest(const char *asset_root);

#endif /* SDLGPU_FORGE_GPU_PROCESSED_ASSETS_H */
