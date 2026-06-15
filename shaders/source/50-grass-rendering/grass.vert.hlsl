/*
 * Grass vertex shader for lesson 50.
 * SPDX-License-Identifier: Zlib
 */

cbuffer GrassVertUniforms : register(b0, space1)
{
    column_major float4x4 vp;
    column_major float4x4 light_vp;
    float time;
    float wind_strength;
    float wind_speed;
    float _pad0;
    float2 wind_dir;
    float2 _pad1;
};

struct VSInput
{
    float3 position : TEXCOORD0;
    float height_t : TEXCOORD1;
    float3 inst_pos : TEXCOORD2;
    float inst_rot : TEXCOORD3;
    float2 inst_scale : TEXCOORD4;
    float3 inst_color : TEXCOORD5;
};

struct VSOutput
{
    float4 clip_pos : SV_Position;
    float3 world_pos : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float4 light_clip : TEXCOORD2;
    float3 color : TEXCOORD3;
    float height_t : TEXCOORD4;
};

VSOutput main(VSInput input)
{
    VSOutput output;

    float s = sin(input.inst_rot);
    float c = cos(input.inst_rot);
    float3 rotated = float3(
        input.position.x * c - input.position.z * s,
        input.position.y,
        input.position.x * s + input.position.z * c);

    rotated.x *= input.inst_scale.x;
    rotated.z *= input.inst_scale.x;
    rotated.y *= input.inst_scale.y;

    float wind_weight = input.height_t * input.height_t;
    float phase = time * wind_speed + input.inst_pos.x * 0.7 + input.inst_pos.z * 0.3;
    float wind_offset = sin(phase) * wind_strength * wind_weight;
    float flutter = sin(phase * 3.7 + input.inst_pos.x * 2.3) * 0.15 * wind_weight;

    rotated.x += wind_dir.x * wind_offset + wind_dir.y * flutter;
    rotated.z += wind_dir.y * wind_offset - wind_dir.x * flutter;

    float3 world = rotated + input.inst_pos;
    float3 face_normal = normalize(float3(s, 0.2, c));
    float4 world4 = float4(world, 1.0);

    output.clip_pos = mul(vp, world4);
    output.world_pos = world;
    output.normal = face_normal;
    output.light_clip = mul(light_vp, world4);
    output.color = input.inst_color;
    output.height_t = input.height_t;
    return output;
}
