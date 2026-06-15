/*
 * Grass fragment shader for lesson 50.
 * SPDX-License-Identifier: Zlib
 */

#define SHADOW_BIAS 0.005
#define SHADOW_MAP_RES 2048.0
#define PCF_SAMPLES 4

Texture2D shadow_tex : register(t0, space2);
SamplerState shadow_smp : register(s0, space2);

cbuffer GrassFragUniforms : register(b0, space3)
{
    float3 light_dir;
    float light_intensity;
    float3 eye_pos;
    float ambient;
};

float sample_shadow(float4 light_clip)
{
    float3 light_ndc = light_clip.xyz / light_clip.w;
    float2 shadow_uv = light_ndc.xy * 0.5 + 0.5;
    shadow_uv.y = 1.0 - shadow_uv.y;

    if (shadow_uv.x < 0.0 || shadow_uv.x > 1.0 ||
        shadow_uv.y < 0.0 || shadow_uv.y > 1.0) {
        return 1.0;
    }

    float current_depth = light_ndc.z;
    float2 texel_size = float2(1.0 / SHADOW_MAP_RES, 1.0 / SHADOW_MAP_RES);
    float shadow = 0.0;
    float2 offsets[PCF_SAMPLES] = {
        float2(-0.5, -0.5),
        float2( 0.5, -0.5),
        float2(-0.5,  0.5),
        float2( 0.5,  0.5)
    };

    for (int i = 0; i < PCF_SAMPLES; i++)
    {
        float stored = shadow_tex.SampleLevel(shadow_smp, shadow_uv + offsets[i] * texel_size, 0.0).r;
        shadow += (current_depth - SHADOW_BIAS <= stored) ? 1.0 : 0.0;
    }

    return shadow / (float)PCF_SAMPLES;
}

struct PSInput
{
    float4 clip_pos : SV_Position;
    float3 world_pos : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float4 light_clip : TEXCOORD2;
    float3 color : TEXCOORD3;
    float height_t : TEXCOORD4;
};

float4 main(PSInput input) : SV_Target0
{
    float3 N = normalize(input.normal);
    float3 L = normalize(light_dir);
    float NdotL = dot(N, L);
    float shadow = sample_shadow(input.light_clip);
    float diffuse = max(NdotL, 0.0) * light_intensity * shadow;
    float subsurface = 0.0;

    if (NdotL < 0.0)
    {
        subsurface = 0.2 * (-NdotL) * (0.5 + 0.5 * input.height_t) * shadow;
    }

    float3 lit = input.color * (ambient + diffuse + subsurface);
    float cam_dist = length(input.world_pos - eye_pos);
    float fog = 1.0 - exp(-cam_dist * 0.008);
    float3 fog_color = float3(0.55, 0.62, 0.75);
    lit = lerp(lit, fog_color, fog * 0.6);

    return float4(lit, 1.0);
}
