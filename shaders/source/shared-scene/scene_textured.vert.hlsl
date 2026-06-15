/*
 * Scene textured vertex shader: position + normal + UV.
 *
 * Part of forge_scene.h. Extends the base scene pipeline with texture
 * coordinate support for textured meshes and atlas rendering.
 *
 * Vertex attributes:
 *   TEXCOORD0 -> float3 position (location 0)
 *   TEXCOORD1 -> float3 normal   (location 1)
 *   TEXCOORD2 -> float2 uv       (location 2)
 *
 * Uniform buffers:
 *   register(b0, space1) -> slot 0: scene transforms (192 bytes, 3 mat4s)
 *
 * SPDX-License-Identifier: Zlib
 */

cbuffer SceneVertUniforms : register(b0, space1)
{
    column_major float4x4 mvp;
    column_major float4x4 model;
    column_major float4x4 light_vp;
};

struct VSInput
{
    float3 position : TEXCOORD0;
    float3 normal   : TEXCOORD1;
    float2 uv       : TEXCOORD2;
};

struct VSOutput
{
    float4 clip_pos   : SV_Position;
    float3 world_pos  : TEXCOORD0;
    float3 world_nrm  : TEXCOORD1;
    float4 light_clip : TEXCOORD2;
    float2 uv         : TEXCOORD3;
};

VSOutput main(VSInput input)
{
    VSOutput output;

    float4 world = mul(model, float4(input.position, 1.0));
    output.world_pos = world.xyz;
    output.clip_pos = mul(mvp, float4(input.position, 1.0));
    output.world_nrm = normalize(mul((float3x3)model, input.normal));
    output.light_clip = mul(light_vp, float4(input.position, 1.0));
    output.uv = input.uv;

    return output;
}
