/*
 * Imposter atlas bake - fragment shader.
 *
 * Applies simple directional lighting to produce a shaded tree atlas. The
 * atlas background is cleared to transparent black; tree pixels write alpha 1.
 */

cbuffer BakeFragUniforms : register(b0, space3)
{
    float4 base_color;
    float3 light_dir;
    float ambient;
};

struct PSInput
{
    float4 clip_pos : SV_Position;
    float3 normal   : TEXCOORD0;
};

float4 main(PSInput input) : SV_Target0
{
    float3 N = normalize(input.normal);
    float NdotL = max(dot(N, normalize(light_dir)), 0.0);
    float shade = NdotL * (1.0 - ambient) + ambient;
    return float4(base_color.rgb * shade, base_color.a);
}
