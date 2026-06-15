/*
 * Imposter billboard - fragment shader.
 *
 * Samples the baked atlas, discards transparent background pixels, and applies
 * the per-instance color tint and cross-fade alpha.
 */

Texture2D atlas_tex : register(t0, space2);
SamplerState atlas_smp : register(s0, space2);

struct PSInput
{
    float4 clip_pos : SV_Position;
    float2 uv       : TEXCOORD0;
    float alpha     : TEXCOORD1;
    float3 color    : TEXCOORD2;
};

float4 main(PSInput input) : SV_Target0
{
    float4 tex = atlas_tex.Sample(atlas_smp, input.uv);
    if (tex.a < 0.5) {
        discard;
    }
    return float4(tex.rgb * input.color, tex.a * input.alpha);
}
