cbuffer ConstantBuffer : register(b0) {
    matrix worldMatrix;
    matrix viewMatrix;
    matrix projectionMatrix;
};

struct VS_INPUT {
    float3 position : POSITION;
    float2 texCoord : TEXCOORD0;
};

struct PS_INPUT {
    float4 position : SV_POSITION;
    float2 texCoord : TEXCOORD0;
};

PS_INPUT main(VS_INPUT input) {
    PS_INPUT output;
    
    float4 worldPos = mul(float4(input.position, 1.0f), worldMatrix);
    float4 viewPos = mul(worldPos, viewMatrix);
    output.position = mul(viewPos, projectionMatrix);
    output.texCoord = input.texCoord;
    
    return output;
}