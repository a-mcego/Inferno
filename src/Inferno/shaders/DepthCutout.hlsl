#define RS "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), "\
    "RootConstants(b0, num32BitConstants = 25),"\
    "DescriptorTable(SRV(t0, numDescriptors = 1), visibility=SHADER_VISIBILITY_PIXEL), "\
    "DescriptorTable(SRV(t1, numDescriptors = 1), visibility=SHADER_VISIBILITY_PIXEL), "\
    "DescriptorTable(Sampler(s0), visibility=SHADER_VISIBILITY_PIXEL)"

Texture2D Diffuse : register(t0);
Texture2D StMask : register(t1);
SamplerState Sampler : register(s0);

cbuffer FrameConstants : register(b0) {
    // Frame constants
    float4x4 ProjectionMatrix;
    float NearClip, FarClip;
    float Time; // elapsed game time in seconds
    // Instance constants
    bool HasOverlay;
    float2 Scroll, Scroll2;
    float Threshold;
};

struct LevelVertex {
    float3 pos : POSITION;
    float4 col : COLOR0;
    float2 uv : TEXCOORD0;
    float2 uv2 : TEXCOORD1;
    float3 normal : NORMAL;
};

struct PS_INPUT {
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
    float2 uv2 : TEXCOORD1;
    float depth : COLOR0;
};

[RootSignature(RS)]
PS_INPUT vsmain(LevelVertex input) {
    PS_INPUT output;
    output.pos = mul(ProjectionMatrix, float4(input.pos, 1));
    output.depth = output.pos.z / output.pos.w;
    output.uv = input.uv + Scroll * Time * 100;
    output.uv2 = input.uv2 + Scroll2 * Time * 100;
    return output;
}

float LinearizeDepth(float n, float f, float depth) {
    return n / (f + depth * (n - f));
}

float psmain(PS_INPUT input) : SV_Target {
    float alpha = Diffuse.Sample(Sampler, input.uv).a;
    
    if (HasOverlay) {
        float mask = StMask.Sample(Sampler, input.uv2).r;
        alpha *= (1 - mask);
    }
    
    if (alpha <= 0.01) /*Threshold*/
        discard;
    
    return LinearizeDepth(NearClip, FarClip, input.pos.z);
}