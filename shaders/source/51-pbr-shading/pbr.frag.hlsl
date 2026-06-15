/*
 * pbr.frag.hlsl — Cook-Torrance microfacet BRDF fragment shader
 *
 * Physically-based rendering with the metallic-roughness workflow.
 * Uses identical texture bindings and uniform buffer layout as
 * scene_model.frag.hlsl so the same vertex shader and model data
 * work with both shading models.
 *
 * The Cook-Torrance reflectance equation:
 *
 *   Lo = (kd * albedo/pi + DGF / (4 * NdotV * NdotL)) * NdotL * Li
 *
 * where:
 *   D = GGX/Trowbridge-Reitz normal distribution function
 *   G = Schlick-GGX geometry function (Smith method)
 *   F = Schlick Fresnel approximation
 *   kd = (1 - F) * (1 - metallic)  — energy conservation
 *
 * Texture/sampler bindings (space2):
 *   slot 0 -> base color (sRGB)
 *   slot 1 -> normal map (linear)
 *   slot 2 -> metallic-roughness (linear, G=roughness B=metallic)
 *   slot 3 -> occlusion (linear, R channel)
 *   slot 4 -> emissive (sRGB)
 *   slot 5 -> shadow map (depth, comparison sampler)
 *
 * Uniform buffer:
 *   register(b0, space3) -> material + lighting parameters (96 bytes)
 *
 * SPDX-License-Identifier: Zlib
 */

#define PI 3.14159265358979323846
#define SHADOW_BIAS 0.005

/* Dielectric base reflectivity — 4% is typical for non-metals (glass,
 * plastic, water).  Metals use the albedo color as F0 instead. */
#define DIELECTRIC_F0 0.04

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
    float  shininess;           /* unused in PBR (kept for struct compat)    */
    float  specular_str;        /* unused in PBR (kept for struct compat)    */
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

/* ── PCF 3x3 shadow (identical to scene_model.frag.hlsl) ───────────── */

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

/* ── GGX/Trowbridge-Reitz Normal Distribution Function ─────────────── */
/* Probability that a microfacet is aligned with the half-vector H.
 * Produces sharp, bright highlights at low roughness and broad, dim
 * highlights at high roughness.
 *
 *   D(h) = alpha^2 / (pi * ((n.h)^2 * (alpha^2 - 1) + 1)^2)
 */
float distribution_ggx(float NdotH, float alpha2)
{
    float denom = NdotH * NdotH * (alpha2 - 1.0) + 1.0;
    return alpha2 / (PI * denom * denom);
}

/* ── Schlick-GGX Geometry Function (single direction) ──────────────── */
/* Fraction of microfacets visible from direction x (not self-shadowed).
 * k depends on whether this is direct lighting or IBL:
 *   Direct: k = (roughness + 1)^2 / 8
 *   IBL:    k = roughness^2 / 2
 *
 *   G1(x) = (n.x) / ((n.x)(1 - k) + k)
 */
float geometry_schlick_ggx(float NdotX, float k)
{
    return NdotX / (NdotX * (1.0 - k) + k);
}

/* ── Smith Geometry Function ───────────────────────────────────────── */
/* Combined geometry term for both view and light directions.
 *   G(n,v,l) = G1(n,v) * G1(n,l)
 */
float geometry_smith(float NdotV, float NdotL, float k)
{
    return geometry_schlick_ggx(NdotV, k) * geometry_schlick_ggx(NdotL, k);
}

/* ── Schlick Fresnel Approximation ─────────────────────────────────── */
/* Reflectance increases at grazing angles.  F0 is the reflectance at
 * normal incidence — 0.04 for dielectrics, albedo color for metals.
 *
 *   F(h,v) = F0 + (1 - F0) * (1 - v.h)^5
 */
float3 fresnel_schlick(float VdotH, float3 F0)
{
    return F0 + (1.0 - F0) * pow(saturate(1.0 - VdotH), 5.0);
}

/* ══════════════════════════════════════════════════════════════════════ */

float4 main(PSInput input) : SV_Target {
    /* ── Base color from texture x factor ────────────────────────── */
    float4 base = base_color_tex.Sample(base_color_smp, input.uv);
    base *= base_color_factor;

    /* ── Reconstruct TBN basis (re-normalize after interpolation) ─ */
    float3 N = normalize(input.world_normal);
    float3 T = normalize(input.world_tangent);
    float3 B = normalize(input.world_bitan);

    /* ── Sample and decode the normal map ────────────────────────── */
    float2 n_rg = normal_tex.Sample(normal_smp, input.uv).rg * 2.0 - 1.0;
    n_rg *= normal_scale;
    float3 map_normal = float3(n_rg, sqrt(saturate(1.0 - dot(n_rg, n_rg))));
    map_normal = normalize(map_normal);

    float3x3 TBN = float3x3(T, B, N);
    N = normalize(mul(map_normal, TBN));

    /* ── Sample PBR textures ────────────────────────────────────── */
    float2 mr = mr_tex.Sample(mr_smp, input.uv).bg;
    float metallic  = mr.x * metallic_factor;
    float roughness = mr.y * roughness_factor;
    roughness = max(roughness, 0.04); /* prevent near-zero roughness aliasing */

    float ao = occlusion_tex.Sample(occlusion_smp, input.uv).r;
    ao = 1.0 + occlusion_strength * (ao - 1.0);

    float3 emissive = emissive_tex.Sample(emissive_smp, input.uv).rgb;
    emissive *= emissive_factor;

    /* ── Alpha test (MASK mode uses alpha_cutoff > 0) ───────────── */
    /* Keep implicit-derivative samples above discard for WGSL validation. */
    if (alpha_cutoff > 0.0 && base.a < alpha_cutoff)
        discard;

    /* ── Lighting vectors ────────────────────────────────────────── */
    float3 L = normalize(light_dir.xyz);
    float3 V = normalize(eye_pos.xyz - input.world_pos);
    float3 H = normalize(L + V);

    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.001);  /* clamp above zero to avoid divide-by-zero */
    float NdotH = max(dot(N, H), 0.0);
    float VdotH = max(dot(V, H), 0.0);

    /* ── Cook-Torrance BRDF ──────────────────────────────────────── */

    float3 albedo = base.rgb;

    /* F0: base reflectivity at normal incidence.
     * Dielectrics reflect ~4% of light.  Metals use their albedo
     * color as F0 (they have no diffuse — all reflection is specular). */
    float3 F0 = lerp(float3(DIELECTRIC_F0, DIELECTRIC_F0, DIELECTRIC_F0),
                     albedo, metallic);

    /* Roughness squared — GGX uses alpha = roughness^2 */
    float alpha  = roughness * roughness;
    float alpha2 = alpha * alpha;

    /* k for direct lighting geometry term */
    float r1 = roughness + 1.0;
    float k  = (r1 * r1) / 8.0;

    /* D: microfacet alignment probability */
    float D = distribution_ggx(NdotH, alpha2);

    /* G: microfacet self-shadowing */
    float G = geometry_smith(NdotV, NdotL, k);

    /* F: Fresnel reflectance at the half-vector angle */
    float3 F = fresnel_schlick(VdotH, F0);

    /* Specular BRDF: D * G * F / (4 * NdotV * NdotL)
     * The denominator normalizes the microfacet distribution. */
    float3 numerator   = D * G * F;
    float  denominator = 4.0 * NdotV * NdotL + 0.0001;
    float3 specular    = numerator / denominator;

    /* ── Energy conservation ─────────────────────────────────────── */
    /* The fraction of light NOT reflected (1 - F) is available for
     * diffuse scattering.  Metals have no diffuse — they absorb
     * all non-reflected light, so kd is zero when metallic = 1. */
    float3 kd = (1.0 - F) * (1.0 - metallic);

    /* Lambertian diffuse: albedo / pi.  The division by pi ensures
     * energy conservation — without it, a white surface would reflect
     * more light than it receives. */
    float3 diffuse = kd * albedo / PI;

    /* ── Shadow ──────────────────────────────────────────────────── */
    float shadow = compute_shadow(input.shadow_pos);

    /* ── Final composition ───────────────────────────────────────── */
    /* Combine diffuse and specular, modulated by NdotL and shadow.
     * Ambient uses the diffuse color scaled by AO. */
    float3 Lo = (diffuse + specular) * NdotL * shadow;
    float3 ambient_term = ambient * albedo * ao;

    float3 final_color = ambient_term + Lo + emissive;

    return float4(final_color, base.a);
}
