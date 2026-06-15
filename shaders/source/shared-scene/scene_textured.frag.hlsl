/*
 * Scene textured fragment shader: Blinn-Phong lighting with texture
 * sampling, atlas UV remapping, and shadow map.
 *
 * Part of forge_scene.h. The uv_transform uniform remaps UVs for atlas
 * rendering:
 *   atlas_uv = original_uv * uv_transform.zw + uv_transform.xy
 * For non-atlas textures, pass (0, 0, 1, 1) as identity.
 *
 * Fragment samplers (space2):
 *   slot 0 -> shadow depth texture + nearest-clamp sampler
 *   slot 1 -> material texture + linear sampler
 *
 * Uniform buffers:
 *   register(b0, space3) -> slot 0: material, lighting, and UV transform
 *
 * SPDX-License-Identifier: Zlib
 */

#define SHADOW_BIAS 0.005
#define SHADOW_MAP_RES 2048.0
#define PCF_SAMPLES 4

Texture2D    shadow_tex : register(t0, space2);
SamplerState shadow_smp : register(s0, space2);

Texture2D    material_tex : register(t1, space2);
SamplerState material_smp : register(s1, space2);

cbuffer SceneTexturedFragUniforms : register(b0, space3)
{
    float4 uv_transform;
    float3 eye_pos;
    float  ambient;
    float4 light_dir;
    float3 light_color;
    float  light_intensity;
    float  shininess;
    float  specular_str;
    float2 _pad0;
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
        float stored = shadow_tex.SampleLevel(
            shadow_smp, shadow_uv + offsets[i] * texel_size, 0.0).r;
        shadow += (current_depth - SHADOW_BIAS <= stored) ? 1.0 : 0.0;
    }

    return shadow / (float)PCF_SAMPLES;
}

struct PSInput
{
    float4 clip_pos   : SV_Position;
    float3 world_pos  : TEXCOORD0;
    float3 world_nrm  : TEXCOORD1;
    float4 light_clip : TEXCOORD2;
    float2 uv         : TEXCOORD3;
};

float4 main(PSInput input) : SV_Target0
{
    float2 clamped_uv = saturate(input.uv);
    float2 atlas_uv = clamped_uv * uv_transform.zw + uv_transform.xy;
    float4 albedo = material_tex.Sample(material_smp, atlas_uv);

    float3 N = normalize(input.world_nrm);
    float3 V = normalize(eye_pos - input.world_pos);
    float3 total_light = albedo.rgb * ambient;

    {
        float3 L = normalize(light_dir.xyz);
        float NdotL = max(dot(N, L), 0.0);
        float3 H = normalize(L + V);
        float NdotH = max(dot(N, H), 0.0);
        float spec = specular_str * pow(NdotH, shininess);

        float shadow = sample_shadow(input.light_clip);
        total_light += (albedo.rgb * NdotL + spec) * light_intensity *
                       light_color * shadow;
    }

    return float4(total_light, albedo.a);
}
