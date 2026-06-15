/*
 * Terrain shadow pass vertex shader — renders terrain depth from the light's
 * perspective for self-shadowing (hills casting shadows on valleys).
 *
 * Part of forge_scene.h — height map terrain ground mode.
 *
 * Samples the same heightmap as the main terrain vertex shader to produce
 * identical geometry in the shadow map.
 *
 * Vertex attributes:
 *   TEXCOORD0 -> float3 position  (location 0)
 *   TEXCOORD1 -> float2 uv        (location 1)
 *
 * Uniform buffers:
 *   register(b0, space1) -> slot 0: light VP + terrain params (80 bytes)
 *
 * Textures (space0 — vertex sampler space):
 *   slot 0 -> heightmap (R32_FLOAT) + linear-clamp sampler
 *
 * SPDX-License-Identifier: Zlib
 */

cbuffer TerrainShadowUniforms : register(b0, space1)
{
    column_major float4x4 light_vp;  /* light view-projection */
    float  terrain_size;             /* world-space half-extent */
    float  height_scale;             /* maximum Y displacement  */
    float2 _pad;
};

/* Heightmap texture for vertex displacement (space0 = vertex samplers) */
Texture2D    height_tex : register(t0, space0);
SamplerState height_smp : register(s0, space0);

struct VSInput
{
    float3 position : TEXCOORD0;
    float2 uv       : TEXCOORD1;
};

float4 main(VSInput input) : SV_Position
{
    float h = height_tex.SampleLevel(height_smp, input.uv, 0).r;

    float3 world = float3(
        input.position.x * terrain_size,
        h * height_scale,
        input.position.z * terrain_size
    );

    return mul(light_vp, float4(world, 1.0));
}
