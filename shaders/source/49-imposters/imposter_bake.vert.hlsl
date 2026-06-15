/*
 * Imposter atlas bake - vertex shader.
 *
 * Transforms the procedural tree mesh (cylinder trunk + cone crown) into
 * clip space for rendering into the atlas texture. Each atlas cell captures
 * the tree from a different azimuth angle using an orthographic projection
 * that fits the tree's bounding volume.
 */

cbuffer BakeVertUniforms : register(b0, space1)
{
    column_major float4x4 mvp;
};

struct VSInput
{
    float3 position : TEXCOORD0;
    float3 normal   : TEXCOORD1;
};

struct VSOutput
{
    float4 clip_pos : SV_Position;
    float3 normal   : TEXCOORD0;
};

VSOutput main(VSInput input)
{
    VSOutput output;
    output.clip_pos = mul(mvp, float4(input.position, 1.0));
    output.normal = input.normal;
    return output;
}
