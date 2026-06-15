cbuffer VertUniforms : register(b0, space1) {
    column_major float4x4 mvp;
    column_major float4x4 model;
    column_major float4x4 light_vp;
};

StructuredBuffer<float4x4> joint_mats : register(t0, space0);

struct VSInput {
    float3 position : TEXCOORD0;
    float3 normal   : TEXCOORD1;
    float2 uv       : TEXCOORD2;
    float4 tangent  : TEXCOORD3;
    uint4  joints   : TEXCOORD4;
    float4 weights  : TEXCOORD5;
};

struct VSOutput {
    float4 clip_pos      : SV_Position;
    float3 world_pos     : TEXCOORD0;
    float3 world_normal  : TEXCOORD1;
    float2 uv            : TEXCOORD2;
    float3 world_tangent : TEXCOORD3;
    float3 world_bitan   : TEXCOORD4;
    float4 shadow_pos    : TEXCOORD5;
};

VSOutput main(VSInput input) {
    VSOutput output;

    float4x4 skin_mat =
        input.weights.x * joint_mats[input.joints.x] +
        input.weights.y * joint_mats[input.joints.y] +
        input.weights.z * joint_mats[input.joints.z] +
        input.weights.w * joint_mats[input.joints.w];

    float4 skinned_pos = mul(skin_mat, float4(input.position, 1.0));
    float3 skinned_nrm = normalize(mul((float3x3)skin_mat, input.normal));
    float3 skinned_tan = normalize(mul((float3x3)skin_mat, input.tangent.xyz));

    output.clip_pos = mul(mvp, skinned_pos);
    float4 wp = mul(model, skinned_pos);
    output.world_pos = wp.xyz;
    output.shadow_pos = mul(light_vp, skinned_pos);

    float3x3 m = (float3x3)model;
    float3x3 adj_t;
    adj_t[0] = cross(m[1], m[2]);
    adj_t[1] = cross(m[2], m[0]);
    adj_t[2] = cross(m[0], m[1]);
    float3 N = normalize(mul(adj_t, skinned_nrm));
    float3 T = normalize(mul(m, skinned_tan));

    T = normalize(T - N * dot(N, T));
    float3 B = cross(N, T) * input.tangent.w;

    output.world_normal  = N;
    output.world_tangent = T;
    output.world_bitan   = B;
    output.uv            = input.uv;

    return output;
}
