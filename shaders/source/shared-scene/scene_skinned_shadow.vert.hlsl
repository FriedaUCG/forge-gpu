cbuffer ShadowUniforms : register(b0, space1) {
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

float4 main(VSInput input) : SV_Position {
    float4x4 skin_mat =
        input.weights.x * joint_mats[input.joints.x] +
        input.weights.y * joint_mats[input.joints.y] +
        input.weights.z * joint_mats[input.joints.z] +
        input.weights.w * joint_mats[input.joints.w];

    float4 skinned_pos = mul(skin_mat, float4(input.position, 1.0));
    return mul(light_vp, skinned_pos);
}
