// ModelVertex.vert — Vulkan Vertex Shader (GLSL 450 / SPIR-V source)
// Compiled to SPIR-V via glslangValidator or shaderc.
// Mirrors ModelVertex.glsl (OpenGL) but uses Vulkan clip-space conventions:
//   Y-axis is flipped (+Y down) and Z is [0,1] rather than [-1,1].
//
// Compile command (shaderc):
//   glslangValidator -V ModelVertex.vert -o ModelVertex.vert.spv
//
// Uniform set/binding layout mirrors HLSL register(bN):
//   set 0, binding 0 — ConstantBuffer (world/view/proj, camera, scale)
//
#version 450

// ── Vertex Attributes ────────────────────────────────────────────────────────
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;
layout(location = 3) in vec3 aTangent;

// ── Uniform Buffer: ConstantBuffer ───────────────────────────────────────────
layout(set = 0, binding = 0) uniform ConstantBuffer
{
    mat4  uWorld;
    mat4  uView;
    mat4  uProjection;
    vec3  uCameraPosition;
    float _pad0;
    vec3  uModelScale;
    float _pad1;
} cb;

// ── Outputs ──────────────────────────────────────────────────────────────────
layout(location = 0) out vec3 vWorldPosition;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec2 vTexCoord;
layout(location = 3) out vec3 vViewDirection;
layout(location = 4) out vec3 vTangent;
layout(location = 5) out vec3 vBitangent;

// ── Helper: shear-safe normal matrix ─────────────────────────────────────────
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
    vec3 scaledPos = aPosition * cb.uModelScale;
    vec4 worldPos  = cb.uWorld * vec4(scaledPos, 1.0);
    vWorldPosition = worldPos.xyz;

    mat3 world3   = mat3(cb.uWorld);
    mat3 normalMat= transpose(inverse3x3(world3));

    vec3 N = normalize(normalMat * aNormal);
    vec3 T = normalize(normalMat * aTangent);
    vec3 B = normalize(cross(T, N));

    vNormal      = N;
    vTangent     = T;
    vBitangent   = B;
    vViewDirection = normalize(cb.uCameraPosition - worldPos.xyz);
    vTexCoord    = aTexCoord;

    // Vulkan clip-space: flip Y, remap Z from [-1,1] to [0,1]
    vec4 clipPos  = cb.uProjection * cb.uView * worldPos;
    clipPos.y    = -clipPos.y;
    clipPos.z    = (clipPos.z + clipPos.w) * 0.5;
    gl_Position  = clipPos;
}
