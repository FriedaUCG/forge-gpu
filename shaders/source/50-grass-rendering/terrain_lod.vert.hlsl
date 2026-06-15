/*
 * Terrain LOD vertex shader for lesson 50.
 * SPDX-License-Identifier: Zlib
 */

cbuffer TerrainLodVertUniforms : register(b0, space1)
{
    column_major float4x4 vp;
    column_major float4x4 light_vp;
    float terrain_size;
    float height_scale;
    float2 tile_offset;
    float tile_scale;
    float morph_factor;
    float coarse_cell_size;
    float _pad;
};

Texture2D height_tex : register(t0, space0);
SamplerState height_smp : register(s0, space0);

struct VSInput
{
    float3 position : TEXCOORD0;
    float2 uv : TEXCOORD1;
};

struct VSOutput
{
    float4 clip_pos : SV_Position;
    float3 world_pos : TEXCOORD0;
    float2 uv : TEXCOORD1;
    float4 light_clip : TEXCOORD2;
};

VSOutput main(VSInput input)
{
    VSOutput output;

    float half_tile = tile_scale * 0.5;
    float world_x = input.position.x * half_tile + tile_offset.x;
    float world_z = input.position.z * half_tile + tile_offset.y;
    float2 global_uv = float2(
        (world_x + terrain_size) / (2.0 * terrain_size),
        (world_z + terrain_size) / (2.0 * terrain_size));

    float2 snapped_uv = round(global_uv / coarse_cell_size) * coarse_cell_size;
    float2 morphed_uv = lerp(global_uv, snapped_uv, morph_factor);
    float morphed_x = lerp(world_x, snapped_uv.x * 2.0 * terrain_size - terrain_size, morph_factor);
    float morphed_z = lerp(world_z, snapped_uv.y * 2.0 * terrain_size - terrain_size, morph_factor);
    float h = height_tex.SampleLevel(height_smp, morphed_uv, 0).r;
    float3 world = float3(morphed_x, h * height_scale, morphed_z);
    float4 world4 = float4(world, 1.0);

    output.clip_pos = mul(vp, world4);
    output.world_pos = world;
    output.uv = morphed_uv;
    output.light_clip = mul(light_vp, world4);
    return output;
}
