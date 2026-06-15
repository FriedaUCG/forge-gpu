#include "forge_gpu_shapes.h"

#define FORGE_SHAPES_IMPLEMENTATION
#include "shapes/forge_shapes.h"

static void ForgeGpuZeroShapeMesh(ForgeGpuShapeMesh *shape)
{
    SDL_zero(*shape);
}

void ForgeGpuFreeShapeMesh(ForgeGpuShapeMesh *shape)
{
    if (!shape) {
        return;
    }
    SDL_free(shape->positions);
    SDL_free(shape->normals);
    SDL_free(shape->uvs);
    SDL_free(shape->indices);
    ForgeGpuZeroShapeMesh(shape);
}

static bool ForgeGpuCopyForgeShape(const ForgeShape *source, ForgeGpuShapeMesh *out_shape)
{
    if (!source || !out_shape || !source->positions || !source->normals ||
        !source->indices || source->vertex_count <= 0 || source->index_count <= 0) {
        SDL_SetError("Forge shape generation returned an empty shape");
        return false;
    }

    ForgeGpuZeroShapeMesh(out_shape);
    out_shape->positions = (float *)SDL_calloc((size_t)source->vertex_count * 3u, sizeof(float));
    out_shape->normals = (float *)SDL_calloc((size_t)source->vertex_count * 3u, sizeof(float));
    out_shape->uvs = (float *)SDL_calloc((size_t)source->vertex_count * 2u, sizeof(float));
    out_shape->indices = (Uint32 *)SDL_calloc((size_t)source->index_count, sizeof(Uint32));
    if (!out_shape->positions || !out_shape->normals || !out_shape->uvs || !out_shape->indices) {
        ForgeGpuFreeShapeMesh(out_shape);
        SDL_OutOfMemory();
        return false;
    }

    for (int i = 0; i < source->vertex_count; i += 1) {
        out_shape->positions[i * 3 + 0] = source->positions[i].x;
        out_shape->positions[i * 3 + 1] = source->positions[i].y;
        out_shape->positions[i * 3 + 2] = source->positions[i].z;
        out_shape->normals[i * 3 + 0] = source->normals[i].x;
        out_shape->normals[i * 3 + 1] = source->normals[i].y;
        out_shape->normals[i * 3 + 2] = source->normals[i].z;
        if (source->uvs) {
            out_shape->uvs[i * 2 + 0] = source->uvs[i].x;
            out_shape->uvs[i * 2 + 1] = source->uvs[i].y;
        }
    }
    SDL_memcpy(out_shape->indices, source->indices, (size_t)source->index_count * sizeof(Uint32));
    out_shape->vertex_count = source->vertex_count;
    out_shape->index_count = source->index_count;
    return true;
}

bool ForgeGpuCreateSphereShapeMesh(int slices, int stacks, ForgeGpuShapeMesh *out_shape)
{
    ForgeShape source;
    bool ok;

    if (!out_shape) {
        SDL_SetError("sphere shape output is missing");
        return false;
    }

    source = forge_shapes_sphere(slices, stacks);
    ok = ForgeGpuCopyForgeShape(&source, out_shape);
    forge_shapes_free(&source);
    return ok;
}

bool ForgeGpuCreateTorusShapeMesh(int slices, int stacks, float major_radius, float tube_radius, ForgeGpuShapeMesh *out_shape)
{
    ForgeShape source;
    bool ok;

    if (!out_shape) {
        SDL_SetError("torus shape output is missing");
        return false;
    }

    source = forge_shapes_torus(slices, stacks, major_radius, tube_radius);
    ok = ForgeGpuCopyForgeShape(&source, out_shape);
    forge_shapes_free(&source);
    return ok;
}

bool ForgeGpuCreateCubeShapeMesh(int slices, int stacks, ForgeGpuShapeMesh *out_shape)
{
    ForgeShape source;
    bool ok;

    if (!out_shape) {
        SDL_SetError("cube shape output is missing");
        return false;
    }

    source = forge_shapes_cube(slices, stacks);
    ok = ForgeGpuCopyForgeShape(&source, out_shape);
    forge_shapes_free(&source);
    return ok;
}

bool ForgeGpuCreatePlaneShapeMesh(int slices, int stacks, ForgeGpuShapeMesh *out_shape)
{
    ForgeShape source;
    bool ok;

    if (!out_shape) {
        SDL_SetError("plane shape output is missing");
        return false;
    }

    source = forge_shapes_plane(slices, stacks);
    ok = ForgeGpuCopyForgeShape(&source, out_shape);
    forge_shapes_free(&source);
    return ok;
}

bool ForgeGpuCreateCylinderShapeMesh(int slices, int stacks, ForgeGpuShapeMesh *out_shape)
{
    ForgeShape source;
    bool ok;

    if (!out_shape) {
        SDL_SetError("cylinder shape output is missing");
        return false;
    }

    source = forge_shapes_cylinder(slices, stacks);
    ok = ForgeGpuCopyForgeShape(&source, out_shape);
    forge_shapes_free(&source);
    return ok;
}

bool ForgeGpuCreateConeShapeMesh(int slices, int stacks, ForgeGpuShapeMesh *out_shape)
{
    ForgeShape source;
    bool ok;

    if (!out_shape) {
        SDL_SetError("cone shape output is missing");
        return false;
    }

    source = forge_shapes_cone(slices, stacks);
    ok = ForgeGpuCopyForgeShape(&source, out_shape);
    forge_shapes_free(&source);
    return ok;
}
