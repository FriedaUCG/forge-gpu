/*
 * Terrain vertex shader — displaces a flat XZ grid using a heightmap texture.
 *
 * Part of forge_scene.h — height map terrain ground mode.
 *
 * The input mesh is a tessellated XZ plane (from forge_shapes_plane) with
 * positions in [-1, +1] and UVs in [0, 1].  The vertex shader:
 *   1. Scales XZ by terrain_size to set the world footprint
 *   2. Samples the heightmap at the vertex UV
 *   3. Displaces Y by height_scale * sampled_value
 *
 * Vertex attributes:
 *   TEXCOORD0 -> float3 position  (location 0)
 *   TEXCOORD1 -> float2 uv        (location 1)
 *
 * Uniform buffers:
 *   register(b0, space1) -> slot 0: terrain vertex uniforms (144 bytes)
 *
 * Textures (space0 — vertex sampler space):
 *   slot 0 -> heightmap (R32_FLOAT) + linear-clamp sampler
 *
 * SPDX-License-Identifier: Zlib
 */

cbuffer TerrainVertUniforms : register(b0, space1)
{
    column_major float4x4 vp;        /* camera view-projection           */
    column_major float4x4 light_vp;  /* light view-projection for shadow */
    float  terrain_size;             /* world-space half-extent           */
    float  height_scale;             /* maximum Y displacement            */
    float2 _pad_vert;               /* 16-byte alignment                 */
};

/* Heightmap texture for vertex displacement (space0 = vertex samplers) */
Texture2D    height_tex : register(t0, space0);
SamplerState height_smp : register(s0, space0);

struct VSInput
{
    float3 position : TEXCOORD0;  /* XZ plane vertex [-1, +1] */
    float2 uv       : TEXCOORD1;  /* texture coordinate [0, 1] */
};

struct VSOutput
{
    float4 clip_pos   : SV_Position;
    float3 world_pos  : TEXCOORD0;
    float2 uv         : TEXCOORD1;
    float4 light_clip : TEXCOORD2;
};

VSOutput main(VSInput input)
{
    VSOutput output;

    /* Sample heightmap at vertex UV to get displacement */
    float h = height_tex.SampleLevel(height_smp, input.uv, 0).r;

    /* Build world-space position: scale XZ by terrain size, displace Y */
    float3 world = float3(
        input.position.x * terrain_size,
        h * height_scale,
        input.position.z * terrain_size
    );

    float4 world4 = float4(world, 1.0);
    output.clip_pos   = mul(vp, world4);
    output.world_pos  = world;
    output.uv         = input.uv;
    output.light_clip = mul(light_vp, world4);

    return output;
}
