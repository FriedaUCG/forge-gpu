/*
 * Terrain fragment shader — slope/height texture splatting with directional
 * light and shadow map sampling.
 *
 * Part of forge_scene.h — height map terrain ground mode.
 *
 * Normals are computed per-fragment from the heightmap using central
 * differences.  This avoids storing normals in the vertex buffer and
 * produces smooth results at any mesh resolution.
 *
 * Texture splatting uses three layers blended by height and slope:
 *   - Grass: flat areas at low/mid altitude
 *   - Rock:  steep slopes (slope > threshold)
 *   - Snow:  high altitude (above snow line)
 *
 * The colors are procedural (no texture files needed) — they use the
 * heightmap value and computed normal to derive natural-looking tones.
 *
 * Fragment samplers (space2):
 *   slot 0 -> heightmap texture + linear-clamp sampler (shared with vertex)
 *   slot 1 -> shadow depth texture + nearest-clamp sampler
 *
 * Uniform buffers:
 *   register(b0, space3) -> slot 0: terrain fragment uniforms (64 bytes)
 *
 * SPDX-License-Identifier: Zlib
 */

/* Shadow bias — prevents self-shadowing (shadow acne). */
#define SHADOW_BIAS 0.005

/* Shadow map resolution — must match shadow_map_size in ForgeSceneConfig. */
#define SHADOW_MAP_RES 2048.0

/* Number of PCF filter samples for soft shadow edges. */
#define PCF_SAMPLES 4

/* Heightmap for normal computation via central differences */
Texture2D    height_tex : register(t0, space2);
SamplerState height_smp : register(s0, space2);

/* Shadow depth map (slot 1) */
Texture2D    shadow_tex : register(t1, space2);
SamplerState shadow_smp : register(s1, space2);

cbuffer TerrainFragUniforms : register(b0, space3)
{
    float3 light_dir;        /* world-space directional light direction     */
    float  light_intensity;  /* light brightness multiplier                */
    float3 eye_pos;          /* world-space camera position                */
    float  ambient;          /* ambient light intensity [0..1]             */
    float  height_scale;     /* max Y displacement (for normal scaling)    */
    float  terrain_size;     /* world-space half-extent                    */
    float  texture_repeat;   /* UV tiling for detail patterns              */
    float  snow_line;        /* normalized height where snow begins [0..1] */
    float  slope_threshold;  /* slope steepness where rock begins [0..1]   */
    float  _pad0;            /* scalar pad avoids trailing vec3 growth     */
    float  _pad1;            /* scalar pad avoids trailing vec3 growth     */
    float  _pad2;            /* scalar pad avoids trailing vec3 growth     */
};

/* ── Shadow sampling with 2x2 PCF ──────────────────────────────────── */

float sample_shadow(float4 light_clip)
{
    float3 light_ndc = light_clip.xyz / light_clip.w;

    float2 shadow_uv = light_ndc.xy * 0.5 + 0.5;
    shadow_uv.y = 1.0 - shadow_uv.y;

    if (shadow_uv.x < 0.0 || shadow_uv.x > 1.0 ||
        shadow_uv.y < 0.0 || shadow_uv.y > 1.0)
        return 1.0;

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
        float stored = shadow_tex.SampleLevel(shadow_smp,
            shadow_uv + offsets[i] * texel_size, 0.0).r;
        shadow += (current_depth - SHADOW_BIAS <= stored) ? 1.0 : 0.0;
    }

    return shadow / (float)PCF_SAMPLES;
}

/* ── Normal from heightmap central differences ─────────────────────── */

float3 compute_normal(float2 uv)
{
    /* Heightmap dimensions for texel offset */
    float tw, th;
    height_tex.GetDimensions(tw, th);
    float2 texel = float2(1.0 / tw, 1.0 / th);

    /* Central differences — sample 4 neighbors */
    float hL = height_tex.Sample(height_smp, uv + float2(-texel.x, 0.0)).r;
    float hR = height_tex.Sample(height_smp, uv + float2( texel.x, 0.0)).r;
    float hD = height_tex.Sample(height_smp, uv + float2(0.0, -texel.y)).r;
    float hU = height_tex.Sample(height_smp, uv + float2(0.0,  texel.y)).r;

    /* Gradient scaled by height_scale and terrain_size.
     * The derivative dh/dx in world space is:
     *   (hR - hL) * height_scale / (2 * texel_world_size)
     * where texel_world_size = (2 * terrain_size) / texture_width */
    float dx = (hR - hL) * height_scale * tw / (4.0 * terrain_size);
    float dz = (hU - hD) * height_scale * th / (4.0 * terrain_size);

    return normalize(float3(-dx, 1.0, -dz));
}

struct PSInput
{
    float4 clip_pos   : SV_Position;
    float3 world_pos  : TEXCOORD0;
    float2 uv         : TEXCOORD1;
    float4 light_clip : TEXCOORD2;
};

float4 main(PSInput input) : SV_Target0
{
    /* ── Compute normal from heightmap ──────────────────────────────── */

    float3 N = compute_normal(input.uv);
    float slope = 1.0 - N.y;  /* 0 = flat, 1 = vertical */

    /* ── Sample height for altitude-based blending ─────────────────── */

    float h = height_tex.Sample(height_smp, input.uv).r;

    /* ── Procedural texture splatting ──────────────────────────────── */

    /* Detail UV for tiling patterns */
    float2 detail_uv = input.world_pos.xz * texture_repeat * 0.1;

    /* Base material colors */
    float3 grass_color = float3(0.22, 0.38, 0.15);  /* dark green    */
    float3 rock_color  = float3(0.40, 0.35, 0.30);  /* grey-brown    */
    float3 snow_color  = float3(0.90, 0.92, 0.95);  /* near-white    */
    float3 dirt_color  = float3(0.35, 0.25, 0.18);  /* brown         */

#if !defined(FORGE_TERRAIN_DISABLE_VARIATION)
    /* Add subtle variation using world-space position */
    float variation = frac(sin(dot(detail_uv, float2(12.9898, 78.233))) * 43758.5453);
    grass_color += (variation - 0.5) * 0.06;
    rock_color  += (variation - 0.5) * 0.08;
#endif

    /* Slope blending: grass → rock as slope increases */
    float rock_blend = smoothstep(slope_threshold, slope_threshold + 0.15, slope);
    float3 surface = lerp(grass_color, rock_color, rock_blend);

    /* Mix in dirt at low altitudes and steep mid-slopes */
    float dirt_blend = smoothstep(0.0, 0.15, slope) * (1.0 - smoothstep(0.3, 0.5, slope));
    dirt_blend *= (1.0 - smoothstep(0.3, 0.5, h));
    surface = lerp(surface, dirt_color, dirt_blend * 0.5);

    /* Snow above the snow line, with slope falloff (snow doesn't stick to cliffs) */
    float snow_blend = smoothstep(snow_line, snow_line + 0.1, h) * (1.0 - rock_blend);
    surface = lerp(surface, snow_color, snow_blend);

    /* ── Directional lighting with shadow ──────────────────────────── */

    float3 L = normalize(light_dir);
    float NdotL = max(dot(N, L), 0.0);

    float shadow = sample_shadow(input.light_clip);

    float3 lit = surface * (ambient + NdotL * light_intensity * shadow);

    /* Specular highlight (subtle, for wet rock) */
    float3 V = normalize(eye_pos - input.world_pos);
    float3 H = normalize(L + V);
    float spec = pow(max(dot(N, H), 0.0), 32.0) * 0.2 * rock_blend * shadow;
    lit += spec * float3(1.0, 1.0, 1.0);

    /* ── Distance fog (blend toward sky color at far distances) ────── */
    float cam_dist = length(input.world_pos - eye_pos);
    float fog = 1.0 - exp(-cam_dist * 0.008);
    float3 fog_color = float3(0.55, 0.62, 0.75);
    lit = lerp(lit, fog_color, fog * 0.6);

    return float4(lit, 1.0);
}
