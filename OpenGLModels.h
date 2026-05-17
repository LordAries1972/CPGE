#pragma once
// ============================================================================
// OpenGLModels.h — OpenGL-specific model resource types and GPU helpers
//
// Included by Models.h when __USE_OPENGL__ is defined.
// Contains OpenGL-specific texture loading, VAO/VBO management, and
// shader program binding for the Model / ModelInfo pipeline.
//
// Windows SDK OpenGL (opengl32.lib + glu32.lib) is used via Includes.h.
// GLEW provides core/extension entry points when available.
// ============================================================================

#if !defined(__USE_OPENGL__)
#error "OpenGLModels.h must only be included when __USE_OPENGL__ is defined."
#endif

#include "Includes.h"   // Provides GL/glew.h or GL/gl.h, Vector2/3/4, etc.
#include "Vectors.h"
#include "Color.h"
#include "Debug.h"

#include <string>
#include <vector>
#include <memory>
#include <cstdint>

// ============================================================================
// OpenGL texture utility helpers used by the OpenGL Model pipeline.
// These are free functions rather than class members to avoid coupling the
// generic Texture class to a specific renderer context.
// ============================================================================
namespace OpenGLModelUtils
{
    // Upload raw RGBA pixel data to the GPU and return a new GL texture handle.
    // Returns 0 on failure.
    GLuint CreateGLTexture(uint32_t width, uint32_t height,
                           const uint8_t* rgbaPixels, bool generateMipmaps = true);

    // Load a texture from a file path (PNG / JPEG / BMP / TGA).
    // Uses stb_image internally; returns 0 if the file cannot be loaded.
    GLuint LoadGLTextureFromFile(const std::wstring& path);

    // Load a texture from an in-memory image buffer (e.g. GLB embedded images).
    GLuint LoadGLTextureFromMemory(const uint8_t* data, size_t size);

    // Create a 1x1 solid-colour texture.  Useful as a fallback / default.
    GLuint CreateSolidColourTexture(const Vector4& colour);

    // Delete a GL texture and zero out the handle.
    void   DeleteGLTexture(GLuint& textureID);

    // Build and link an OpenGL shader program from GLSL source strings.
    // Returns 0 on compile/link failure and writes errors to the debug log.
    GLuint CreateShaderProgram(const std::string& vertSrc, const std::string& fragSrc);

    // Load and compile a GLSL shader source file.  shaderType must be
    // GL_VERTEX_SHADER or GL_FRAGMENT_SHADER.
    GLuint CompileShaderFromFile(const std::wstring& filePath, GLenum shaderType);
}

// ============================================================================
// OpenGLModelBuffers — all per-model OpenGL GPU resources in one place.
// Stored inside ModelInfo::VAO / VBO / EBO etc. for each model instance.
// ============================================================================
struct OpenGLModelBuffers
{
    GLuint VAO          = 0;    // Vertex Array Object — captures vertex attrib state
    GLuint VBO          = 0;    // Vertex Buffer Object — vertex data
    GLuint EBO          = 0;    // Element Buffer Object — index data
    GLuint shaderProgram= 0;    // Linked GLSL program (vert + frag)

    // Texture handles indexed to match the material list
    std::vector<GLuint> diffuseTextures;    // t0 — albedo / base colour
    std::vector<GLuint> normalMaps;         // t1 — tangent-space normal map
    GLuint metallicTex  = 0;               // t2 — metallic (R channel)
    GLuint roughnessTex = 0;               // t3 — roughness (R channel)
    GLuint aoTex        = 0;               // t4 — ambient occlusion
    GLuint envTex       = 0;               // t5 — environment / reflection cubemap

    // UBO handles for uniform blocks that mirror the HLSL cbuffer layout
    GLuint uboTransform = 0;    // block 0: world/view/proj + camera + scale
    GLuint uboLights    = 0;    // block 1: LightBuffer (matches LightBuffer in Lights.h)
    GLuint uboMaterial  = 0;    // block 4: material PBR properties
    GLuint uboEnv       = 0;    // block 5: environment settings

    // Upload vertex + index data to the GPU.
    // vertices must be tightly packed { float pos[3], float norm[3],
    //                                    float uv[2], float tan[3] }.
    bool Upload(const void* vertexData, size_t vertexBytes,
                const uint32_t* indices, size_t indexCount);

    // Release all GPU resources and zero all handles.
    void Destroy();

    // Bind this model's VAO for rendering.
    void Bind()   const { glBindVertexArray(VAO); }
    void Unbind() const { glBindVertexArray(0);   }
};

// ============================================================================
// OpenGLMaterialUniforms
// Mirrors ConstantBuffer / MaterialGPU for use with GL uniform locations.
// Call after UseProgram() to push material data into the active shader.
// ============================================================================
struct OpenGLMaterialUniforms
{
    // Upload the Material struct fields into the bound shader program.
    static void Apply(GLuint program, const struct Material& mat);

    // Upload transform matrices (world, view, projection, camera, scale).
    static void ApplyTransform(GLuint program,
                                const Matrix4x4& world,
                                const Matrix4x4& view,
                                const Matrix4x4& proj,
                                const Vector3&   cameraPos,
                                const Vector3&   modelScale);
};
