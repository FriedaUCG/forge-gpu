#ifndef SDLGPU_FORGE_GPU_SHAPES_H
#define SDLGPU_FORGE_GPU_SHAPES_H

#include <SDL3/SDL.h>

typedef struct ForgeGpuShapeMesh
{
    float *positions;
    float *normals;
    float *uvs;
    Uint32 *indices;
    int vertex_count;
    int index_count;
} ForgeGpuShapeMesh;

#ifdef __cplusplus
extern "C" {
#endif

bool ForgeGpuCreateSphereShapeMesh(int slices, int stacks, ForgeGpuShapeMesh *out_shape);
bool ForgeGpuCreateTorusShapeMesh(int slices, int stacks, float major_radius, float tube_radius, ForgeGpuShapeMesh *out_shape);
bool ForgeGpuCreateCubeShapeMesh(int slices, int stacks, ForgeGpuShapeMesh *out_shape);
bool ForgeGpuCreatePlaneShapeMesh(int slices, int stacks, ForgeGpuShapeMesh *out_shape);
bool ForgeGpuCreateCylinderShapeMesh(int slices, int stacks, ForgeGpuShapeMesh *out_shape);
bool ForgeGpuCreateConeShapeMesh(int slices, int stacks, ForgeGpuShapeMesh *out_shape);
void ForgeGpuFreeShapeMesh(ForgeGpuShapeMesh *shape);

#ifdef __cplusplus
}
#endif

#endif /* SDLGPU_FORGE_GPU_SHAPES_H */
