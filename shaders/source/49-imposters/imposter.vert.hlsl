/*
 * Imposter billboard - vertex shader.
 *
 * Constructs a cylindrical billboard, locked to the Y axis, for each imposter
 * instance. Per-instance data provides the world transform and selected atlas
 * cell.
 */

cbuffer ImposterVertUniforms : register(b0, space1)
{
    column_major float4x4 vp;
    float3 cam_pos;
    float _pad;
};

struct VSInput
{
    float3 position : TEXCOORD0;
    float3 normal   : TEXCOORD1;
    float4 inst_c0  : TEXCOORD2;
    float4 inst_c1  : TEXCOORD3;
    float4 inst_c2  : TEXCOORD4;
    float4 inst_c3  : TEXCOORD5;
    float4 atlas_uv : TEXCOORD6;
    float4 alpha_color : TEXCOORD7;
};

struct VSOutput
{
    float4 clip_pos : SV_Position;
    float2 uv       : TEXCOORD0;
    float alpha     : TEXCOORD1;
    float3 color    : TEXCOORD2;
};

VSOutput main(VSInput input)
{
    VSOutput output;

    float3 pivot = input.inst_c3.xyz;
    float scale_xz = length(input.inst_c0.xyz);
    float scale_y = length(input.inst_c1.xyz);

    float3 to_cam = cam_pos - pivot;
    to_cam.y = 0.0;

    float len = length(to_cam);
    float3 forward = (len > 0.001) ? (to_cam / len) : float3(0.0, 0.0, 1.0);
    float3 up = float3(0.0, 1.0, 0.0);
    float3 right = cross(up, forward);

    right *= scale_xz;
    up *= scale_y;

    float3 world_pos = pivot
        + right * input.position.x
        + up * input.position.y;

    output.clip_pos = mul(vp, float4(world_pos, 1.0));

    float2 base_uv = float2(input.position.x + 0.5, 1.0 - input.position.y);
    output.uv = input.atlas_uv.xy + base_uv * input.atlas_uv.zw;
    output.alpha = input.alpha_color.x;
    output.color = input.alpha_color.yzw;
    return output;
}
