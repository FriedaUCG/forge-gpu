/*
 * pbr_textures.frag.hlsl — PBR fragment shader with separate texture support
 *
 * Extends the Cook-Torrance BRDF from Lesson 51 with support for both
 * packed and separate metallic-roughness texture workflows:
 *
 *   Packed (glTF convention):   one texture, G=roughness B=metallic
 *   Separate (ambientCG, etc.): individual roughness and metallic textures
 *
 * The `shininess` uniform field (unused by PBR) is repurposed as a
 * `use_separate_mr` flag.  When > 0.5, the shader reads roughness from
 * slot 6 and metallic from slot 7 instead of the packed MR in slot 2.
 *
 * Texture/sampler bindings (space2):
 *   slot 0 -> base color (sRGB)
 *   slot 1 -> normal map (linear)
 *   slot 2 -> metallic-roughness packed (linear, G=roughness B=metallic)
 *   slot 3 -> occlusion (linear, R channel)
 *   slot 4 -> emissive (sRGB)
 *   slot 5 -> shadow map (depth, comparison sampler)
 *   slot 6 -> separate roughness (linear, R channel)
 *   slot 7 -> separate metallic (linear, R channel)
 *
 * SPDX-License-Identifier: Zlib
 */

#define PI 3.14159265358979323846
#define SHADOW_BIAS 0.005
#define DIELECTRIC_F0 0.04

/* ── Texture bindings ──────────────────────────────────────────────── */

Texture2D    base_color_tex : register(t0, space2);
SamplerState base_color_smp : register(s0, space2);

Texture2D    normal_tex     : register(t1, space2);
SamplerState normal_smp     : register(s1, space2);

Texture2D    mr_tex         : register(t2, space2);
SamplerState mr_smp         : register(s2, space2);

Texture2D    occlusion_tex  : register(t3, space2);
SamplerState occlusion_smp  : register(s3, space2);

Texture2D    emissive_tex   : register(t4, space2);
SamplerState emissive_smp   : register(s4, space2);

Texture2D              shadow_map     : register(t5, space2);
SamplerComparisonState shadow_sampler : register(s5, space2);

/* Separate roughness and metallic for non-packed workflows */
Texture2D    roughness_tex  : register(t6, space2);
SamplerState roughness_smp  : register(s6, space2);

Texture2D    metallic_tex   : register(t7, space2);
SamplerState metallic_smp   : register(s7, space2);

/* ── Uniform buffer (96 bytes, matches ForgeSceneModelFragUniforms) ── */

cbuffer FragUniforms : register(b0, space3) {
    float4 light_dir;           /* xyz = direction toward light              */
    float4 eye_pos;             /* xyz = camera position                     */
    float4 base_color_factor;   /* RGBA multiplier from material             */
    float3 emissive_factor;     /* RGB emission multiplier                   */
    float  shadow_texel;        /* 1.0 / shadow_map_resolution              */
    float  metallic_factor;     /* 0 = dielectric, 1 = metal                */
    float  roughness_factor;    /* 0 = mirror, 1 = rough                    */
    float  normal_scale;        /* normal map XY intensity                   */
    float  occlusion_strength;  /* AO blend: 0 = none, 1 = full             */
    float  use_separate_mr;     /* >0.5 = read slots 6+7; repurposed field  */
    float  _pad0;               /* struct compat (was specular_str)          */
    float  alpha_cutoff;        /* MASK mode threshold                       */
    float  ambient;             /* ambient light intensity [0..1]            */
};

struct PSInput {
    float4 clip_pos      : SV_Position;
    float3 world_pos     : TEXCOORD0;
    float3 world_normal  : TEXCOORD1;
    float2 uv            : TEXCOORD2;
    float3 world_tangent : TEXCOORD3;
    float3 world_bitan   : TEXCOORD4;
    float4 shadow_pos    : TEXCOORD5;
};

/* ── PCF 3x3 shadow ───────────────────────────────────────────────── */

float compute_shadow(float4 light_clip) {
    float3 ndc = light_clip.xyz / light_clip.w;
    float2 shadow_uv = ndc.xy * 0.5 + 0.5;
    shadow_uv.y = 1.0 - shadow_uv.y;
    float depth = ndc.z;

    if (shadow_uv.x < 0.0 || shadow_uv.x > 1.0 ||
        shadow_uv.y < 0.0 || shadow_uv.y > 1.0 ||
        depth < 0.0 || depth > 1.0)
        return 1.0;

    float shadow = 0.0;
    [unroll]
    for (int y = -1; y <= 1; y++) {
        [unroll]
        for (int x = -1; x <= 1; x++) {
            shadow += shadow_map.SampleCmpLevelZero(
                shadow_sampler, shadow_uv + float2(x, y) * shadow_texel,
                depth - SHADOW_BIAS);
        }
    }
    return shadow / 9.0;
}

/* ── GGX Normal Distribution Function ──────────────────────────────── */

float distribution_ggx(float NdotH, float alpha2)
{
    float denom = NdotH * NdotH * (alpha2 - 1.0) + 1.0;
    return alpha2 / (PI * denom * denom);
}

/* ── Schlick-GGX Geometry Function ─────────────────────────────────── */

float geometry_schlick_ggx(float NdotX, float k)
{
    return NdotX / (NdotX * (1.0 - k) + k);
}

/* ── Smith Geometry Function ───────────────────────────────────────── */

float geometry_smith(float NdotV, float NdotL, float k)
{
    return geometry_schlick_ggx(NdotV, k) * geometry_schlick_ggx(NdotL, k);
}

/* ── Schlick Fresnel ───────────────────────────────────────────────── */

float3 fresnel_schlick(float VdotH, float3 F0)
{
    return F0 + (1.0 - F0) * pow(saturate(1.0 - VdotH), 5.0);
}

/* ══════════════════════════════════════════════════════════════════════ */

float4 main(PSInput input) : SV_Target {
    /* ── Base color ──────────────────────────────────────────────── */
    float4 base = base_color_tex.Sample(base_color_smp, input.uv);
    base *= base_color_factor;

    /* ── TBN basis ───────────────────────────────────────────────── */
    float3 N = normalize(input.world_normal);
    float3 T = normalize(input.world_tangent);
    float3 B = normalize(input.world_bitan);

    /* ── Normal map ──────────────────────────────────────────────── */
    float2 n_rg = normal_tex.Sample(normal_smp, input.uv).rg * 2.0 - 1.0;
    n_rg *= normal_scale;
    float3 map_normal = float3(n_rg, sqrt(saturate(1.0 - dot(n_rg, n_rg))));
    map_normal = normalize(map_normal);

    float3x3 TBN = float3x3(T, B, N);
    N = normalize(mul(map_normal, TBN));

    /* ── Metallic and roughness ──────────────────────────────────── */
    /* Two workflows: packed (single MR texture with G=roughness,
     * B=metallic) and separate (individual single-channel textures).
     * The use_separate_mr flag selects which path to use. */
    float2 mr = mr_tex.Sample(mr_smp, input.uv).bg;
    float packed_metallic  = mr.x * metallic_factor;
    float packed_roughness = mr.y * roughness_factor;

    /* Separate textures — each stores the value in R channel */
    float separate_roughness = roughness_tex.Sample(roughness_smp, input.uv).r * roughness_factor;
    float separate_metallic  = metallic_tex.Sample(metallic_smp, input.uv).r * metallic_factor;

    float use_separate = (use_separate_mr > 0.5) ? 1.0 : 0.0;
    float metallic  = lerp(packed_metallic, separate_metallic, use_separate);
    float roughness = lerp(packed_roughness, separate_roughness, use_separate);
    roughness = max(roughness, 0.04);

    /* ── Ambient occlusion ───────────────────────────────────────── */
    float ao = occlusion_tex.Sample(occlusion_smp, input.uv).r;
    ao = 1.0 + occlusion_strength * (ao - 1.0);

    /* ── Emissive ────────────────────────────────────────────────── */
    float3 emissive = emissive_tex.Sample(emissive_smp, input.uv).rgb;
    emissive *= emissive_factor;

    /* Keep implicit-derivative samples above discard for WGSL validation. */
    if (alpha_cutoff > 0.0 && base.a < alpha_cutoff)
        discard;

    /* ── Lighting vectors ────────────────────────────────────────── */
    float3 L = normalize(light_dir.xyz);
    float3 V = normalize(eye_pos.xyz - input.world_pos);
    float3 H = normalize(L + V);

    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.001);
    float NdotH = max(dot(N, H), 0.0);
    float VdotH = max(dot(V, H), 0.0);

    /* ── Cook-Torrance BRDF ──────────────────────────────────────── */
    float3 albedo = base.rgb;

    float3 F0 = lerp(float3(DIELECTRIC_F0, DIELECTRIC_F0, DIELECTRIC_F0),
                     albedo, metallic);

    float alpha  = roughness * roughness;
    float alpha2 = alpha * alpha;

    float r1 = roughness + 1.0;
    float k  = (r1 * r1) / 8.0;

    float  D = distribution_ggx(NdotH, alpha2);
    float  G = geometry_smith(NdotV, NdotL, k);
    float3 F = fresnel_schlick(VdotH, F0);

    float3 numerator   = D * G * F;
    float  denominator = 4.0 * NdotV * NdotL + 0.0001;
    float3 specular    = numerator / denominator;

    /* ── Energy conservation ─────────────────────────────────────── */
    float3 kd = (1.0 - F) * (1.0 - metallic);
    float3 diffuse = kd * albedo / PI;

    /* ── Shadow ──────────────────────────────────────────────────── */
    float shadow = compute_shadow(input.shadow_pos);

    /* ── Final composition ───────────────────────────────────────── */
    float3 Lo = (diffuse + specular) * NdotL * shadow;
    float3 ambient_term = ambient * albedo * ao;
    float3 final_color = ambient_term + Lo + emissive;

    return float4(final_color, base.a);
}
