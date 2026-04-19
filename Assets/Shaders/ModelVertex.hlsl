// ModelVShader.hlsl

cbuffer ConstantBuffer : register(b0)
{
    matrix worldMatrix;
    matrix viewMatrix;
    matrix projectionMatrix;
    float3 cameraPosition;
    float padding;
    float3 modelScale;
    float  padding2;
};

struct VS_INPUT
{
    float3 position  : POSITION;
    float3 normal    : NORMAL;
    float2 texCoord  : TEXCOORD;
    float3 tangent   : TANGENT;
};

struct VS_OUTPUT
{
    float4 position      : SV_POSITION;
    float3 worldPosition : TEXCOORD0;
    float3 normal        : TEXCOORD1;
    float2 texCoord      : TEXCOORD2;
    float3 viewDirection : TEXCOORD3;
    float3 tangent       : TEXCOORD4;
    float3 bitangent     : TEXCOORD5;
};

// HLSL 5.0-Compliant float3x3 Inverse Function
float3x3 inverse3x3(float3x3 m)
{
    float3 r0 = cross(m[1], m[2]);
    float3 r1 = cross(m[2], m[0]);
    float3 r2 = cross(m[0], m[1]);

    float det = dot(r2, m[2]);
    float invDet = 1.0 / max(det, 1e-6); // avoid divide by zero

    return transpose(float3x3(r0, r1, r2)) * invDet;
}

VS_OUTPUT main(VS_INPUT input)
{
    VS_OUTPUT output;

    // === Transform vertex position to world space (including scaling)
    float3 scaledPos = input.position * modelScale;
    float4 worldPos = mul(float4(scaledPos, 1.0f), worldMatrix);
    output.worldPosition = worldPos.xyz;

    // === Shear-safe normal transform using inverse3x3
    float3x3 worldMatrix3x3 = (float3x3) worldMatrix;
    float3x3 normalMatrix = transpose(inverse3x3(worldMatrix3x3));

    float3 N = normalize(mul(input.normal, normalMatrix));
    float3 T = normalize(mul(input.tangent, normalMatrix));
    float3 B = normalize(cross(N, T)); // Bitangent

    output.normal = N;
    output.tangent = T;
    output.bitangent = B;

    // === View direction (from vertex to camera)
    output.viewDirection = normalize(cameraPosition - worldPos.xyz);

    // === Projected clip space position
    float4 viewPos = mul(worldPos, viewMatrix);
    output.position = mul(viewPos, projectionMatrix);

    // === Pass-through texcoord
    output.texCoord = input.texCoord;

    return output;
}
