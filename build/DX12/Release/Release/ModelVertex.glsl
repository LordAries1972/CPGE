// ModelVertex.glsl — OpenGL Vertex Shader
// Converted from ModelVertex.hlsl (HLSL 5.0) for OpenGL 3.3+ core profile.
// Compiler: OpenGL / Windows SDK (opengl32.lib + glew)
// Shader Version: GLSL 330 core
//
// Slot mapping (mirrors HLSL cbuffer register layout):
//   Binding 0  (std140 UBO) — ConstantBuffer: world/view/proj matrices, camera pos, scale
//
#version 330 core

// ── Vertex Attributes ────────────────────────────────────────────────────────
layout(location = 0) in vec3 aPosition;     // POSITION
layout(location = 1) in vec3 aNormal;       // NORMAL
layout(location = 2) in vec2 aTexCoord;     // TEXCOORD
layout(location = 3) in vec3 aTangent;      // TANGENT

// ── Uniform Block: ConstantBuffer (binding 0) ────────────────────────────────
layout(std140, binding = 0) uniform ConstantBuffer
{
    mat4  uWorld;           // world transformation matrix
    mat4  uView;            // view transformation matrix
    mat4  uProjection;      // projection transformation matrix
    vec3  uCameraPosition;
    float _pad0;
    vec3  uModelScale;
    float _pad1;
};

// ── Vertex Shader Outputs ────────────────────────────────────────────────────
out vec3 vWorldPosition;
out vec3 vNormal;
out vec2 vTexCoord;
out vec3 vViewDirection;
out vec3 vTangent;
out vec3 vBitangent;

// ── Helper: 3×3 matrix inverse (same algorithm as HLSL version) ─────────────
mat3 inverse3x3(mat3 m)
{
    vec3 r0 = cross(m[1], m[2]);
    vec3 r1 = cross(m[2], m[0]);
    vec3 r2 = cross(m[0], m[1]);
    float det    = dot(r2, m[2]);
    float invDet = 1.0 / max(det, 1e-6);
    return transpose(mat3(r0, r1, r2)) * invDet;
}

void main()
{
    // Scale vertex position
    vec3 scaledPos = aPosition * uModelScale;

    // Transform to world space
    vec4 worldPos = uWorld * vec4(scaledPos, 1.0);
    vWorldPosition = worldPos.xyz;

    // Shear-safe normal transform using inverse of world 3×3
    mat3 world3 = mat3(uWorld);
    mat3 normalMat = transpose(inverse3x3(world3));

    vec3 N = normalize(normalMat * aNormal);
    vec3 T = normalize(normalMat * aTangent);
    vec3 B = normalize(cross(T, N));   // bitangent corrected for GLTF handedness

    vNormal    = N;
    vTangent   = T;
    vBitangent = B;

    // View direction (vertex → camera)
    vViewDirection = normalize(uCameraPosition - worldPos.xyz);

    // Texture coordinates pass-through
    vTexCoord = aTexCoord;

    // Clip-space position
    gl_Position = uProjection * uView * worldPos;
}
