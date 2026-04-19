Texture2D diffuseTexture : register(t0);
SamplerState samplerState : register(s0);

struct PS_INPUT {
    float4 position : SV_POSITION;
    float2 texCoord : TEXCOORD0;
};

float4 main(PS_INPUT input) : SV_Target {
    return diffuseTexture.Sample(samplerState, input.texCoord);
}