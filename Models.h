#pragma once

#include "Includes.h"
#include "ConstantBuffer.h"
#include "DX11Renderer.h"
#include "Lights.h"

//==============================================================================
// Constant Declarations
//==============================================================================
const int MAX_MODELS = 2048;                                                        // Maximum number of unique models in the scene    
const int MAX_MODEL_LIGHTS = MAX_LIGHTS;                                            // Maximum number of lights per model

//==============================================================================
// namespaces
//==============================================================================
using namespace DirectX;

//==============================================================================
// Forward Declarations
//==============================================================================
class SceneManager;
class LightManager;

//==============================================================================
// Macros
//==============================================================================

//==============================================================================
// Vertex Structure Declaration
//==============================================================================
// Holds position, normal, and texture coordinate information.
#if defined(__USE_DIRECTX_11__) || defined(__USE_DIRECTX_12__)
struct Vertex {
    XMFLOAT3 position = { 0.0f, 0.0f, 0.0f };
    XMFLOAT3 normal = { 0.0f, 0.0f, 0.0f };
    XMFLOAT2 texCoord = { 0.0f, 0.0f };
    XMFLOAT3 tangent = { 1.0f, 0.0f, 0.0f };                                        // Initialized for safety
};

#elif defined(__USE_OPENGL__) || defined(__USE_VULKAN__)
struct Vertex {
	float position[3];
	float normal[3];
	float texCoord[2];
    float tangent[3] = {1.0f, 0.0f, 0.0f}; // Initialized for safety
};
#endif

enum ModelID
{
    MODEL_NONE = 0,
    MODEL_CUBE1 = 1,
    MODEL_FLOOR1 = 2,
};

//==============================================================================
// Animation Data Structures for GLTF/GLB Animation Support
//==============================================================================

// Animation interpolation types supported by GLTF specification
enum class AnimationInterpolation : int
{
    LINEAR = 0,                                                                      // Linear interpolation between keyframes
    STEP = 1,                                                                        // Step interpolation (no smoothing)
    CUBICSPLINE = 2                                                                  // Cubic spline interpolation
};

// Animation target property types that can be animated
enum class AnimationTargetPath : int
{
    TRANSLATION = 0,                                                                 // Position animation (3 floats)
    ROTATION = 1,                                                                    // Rotation animation (4 floats - quaternion)
    SCALE = 2,                                                                       // Scale animation (3 floats)
    WEIGHTS = 3                                                                      // Morph target weights animation
};

// Single keyframe data for animation sampling
struct AnimationKeyframe
{
    float time;                                                                      // Time in seconds for this keyframe
    std::vector<float> values;                                                       // Value data (3 for translation/scale, 4 for rotation)

    // Constructor for easy initialization
    AnimationKeyframe() : time(0.0f) {}
    AnimationKeyframe(float t, const std::vector<float>& v) : time(t), values(v) {}
};

// Animation sampler defines how keyframes are interpolated
struct AnimationSampler
{
    std::vector<AnimationKeyframe> keyframes;                                        // All keyframes for this sampler
    AnimationInterpolation interpolation;                                            // Interpolation method to use
    float minTime;                                                                   // Minimum time value in keyframes
    float maxTime;                                                                   // Maximum time value in keyframes

    // Constructor with default values
    AnimationSampler() : interpolation(AnimationInterpolation::LINEAR), minTime(0.0f), maxTime(0.0f) {}
};

// Animation channel connects a sampler to a specific node and property
struct AnimationChannel
{
    int samplerIndex;                                                                // Index into animation's samplers array
    int targetNodeIndex;                                                             // Index of the node to animate
    AnimationTargetPath targetPath;                                                 // Which property to animate

    // Constructor with default values
    AnimationChannel() : samplerIndex(-1), targetNodeIndex(-1), targetPath(AnimationTargetPath::TRANSLATION) {}
};

// Complete animation definition containing all samplers and channels
struct GLTFAnimation
{
    std::wstring name;                                                               // Name of this animation
    std::vector<AnimationSampler> samplers;                                          // All samplers for this animation
    std::vector<AnimationChannel> channels;                                          // All channels for this animation
    float duration;                                                                  // Total duration of animation in seconds

    // Constructor with default values
    GLTFAnimation() : name(L"Unnamed Animation"), duration(0.0f) {}
};

// Animation instance for playback - tracks current state of a playing animation
struct AnimationInstance
{
    int animationIndex;                                                              // Index into GLTFAnimations array
    float currentTime;                                                               // Current playback time in seconds
    float playbackSpeed;                                                             // Speed multiplier (1.0 = normal speed)
    bool isPlaying;                                                                  // Whether animation is currently playing
    bool isLooping;                                                                  // Whether animation should loop
    int parentModelID;                                                               // Parent model ID this animation applies to

    // Constructor with default values
    AnimationInstance() : animationIndex(-1), currentTime(0.0f), playbackSpeed(1.0f),
        isPlaying(false), isLooping(true), parentModelID(-1) {
    }
};

//==============================================================================
// Texture Class Declaration
//==============================================================================
// Encapsulates GPU texture resource loading and access for DX11.
class Texture {
public:
    Texture();                                                                      // Default constructor
    Texture(const std::wstring& path);                                              // Load immediately
    ~Texture();                                                                     // Destructor

    bool LoadFromFile(const std::wstring& path);                                    // Load texture from file
    ID3D11ShaderResourceView* GetSRV() const { return textureSRV; }

    const std::wstring& GetPath() const { return texturePath; }
    bool CreateSolidColorTexture(uint32_t width, uint32_t height, const XMFLOAT4& color);

private:
    std::wstring texturePath;                                                       // File path of the texture
	bool bTextureDestroyed = false;                                                 // Flag to prevent double deletion
    #if defined(__USE_DIRECTX_11__)
        ID3D11ShaderResourceView* textureSRV = nullptr;                             // Shader Resource View
        ID3D11Resource* textureResource = nullptr;                                  // Original texture resource
        bool IsValid() const { return textureSRV != nullptr; }
    #endif

    // Prevent copy (not safe with raw COM pointers)
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;
};

struct Material {
    std::string name;                                                               // Material name
    std::string diffuseMapPath;                                                     // Texture filename
    std::string normalMapPath;
    std::wstring ambientMapPath;
    std::wstring specularMapPath;
    std::string metallicMapPath;                                                    // Path to metallic map
    std::string roughnessMapPath;                                                   // Path to roughness map
    std::string aoMapPath;                                                          // Path to ambient occlusion map
    std::shared_ptr<Texture> diffuseTexture = nullptr;
    std::shared_ptr<Texture> normalMap = nullptr;
    std::shared_ptr<Texture> ambientTexture = nullptr;
    std::shared_ptr<Texture> specularTexture = nullptr;
    std::shared_ptr<Texture> metallicMap = nullptr;
    std::shared_ptr<Texture> roughnessMap = nullptr;
    std::shared_ptr<Texture> aoMap = nullptr;

    float dissolve = 1.0f;                                                          // from 'd'
    int illumModel = 2;                                                             // from 'illum'

    XMFLOAT3 Kd = { 1.0f, 1.0f, 1.0f };                                             // Diffuse reflection
    XMFLOAT3 Ka = { 0.1f, 0.1f, 0.1f };                                             // Ambient reflection
    XMFLOAT3 Ks = { 0.5f, 0.5f, 0.5f };                                             // Specular reflection
    float Ns = 32.0f;                                                               // Specular exponent (shininess)
	float Shiningness = 0.0f;                                                       // Shiningness factor (0.0 = no shine, 1.0 = full shine)
    float Reflection = 0.0f;                                                        // Reflection coefficient
	float Metallic = 0.0f;                                                          // Metalness factor (0.0 = non-metal, 1.0 = pure metal)
	float Roughness = 0.5f;                                                         // Roughness factor (0.0 = smooth, 1.0 = rough)
	float Transmission = 0.0f;                                                      // Transmission coefficient (for transparent materials)
	float AlphaCutoff = 0.5f;                                                       // Alpha cutoff value for transparency
};

//==============================================================================
// ModelInfo Structure Declaration
//==============================================================================
// Aggregates all CPU‑side information for a model.
// In Models.h, update the ModelInfo structure:
//==============================================================================
struct ModelInfo {
    int ID = 0;                                                                     // This Model ID number.    
    int iParentModelID = -1;                                                        // Parent model ID (if any) -> -1 = Is Parent Model.
    std::wstring name;                                                              // Model name

#if defined(__USE_DIRECTX_11__) || defined(__USE_DIRECTX_12__)
	XMFLOAT3 position;                                                              // Model position in the world space.
    XMMATRIX worldMatrix;                                                           // World transformation matrix.
    XMMATRIX viewMatrix;                                                            // View transformation matrix.
	XMMATRIX projectionMatrix;												        // Projection transformation matrix.
	XMFLOAT3 cameraPosition;                                                        // Camera position in world space.
	XMFLOAT3 scale = { 0.01f, 0.01f, 0.01f };                                       // Default to no scaling.
    XMFLOAT3 rotation = {0.0f, 0.0f, 0.0f};                                         // Default Model Rotation.

#elif defined(__USE_OPENGL__) || defined(__USE_VULKAN__)
    Matrix4x4 worldMatrix;                                                          // World transformation matrix.
    Matrix4x4 viewMatrix;                                                           // View transformation matrix.
    Matrix4x4 projectionMatrix;                                                     // Projection transformation matrix.
#endif

    std::vector<Vertex> vertices;                                                   // Geometry vertices.
    std::vector<uint32_t> indices;                                                  // Geometry indices.
    std::vector<Vertex> animationVertices;                                          // Vertices for animation updates.
    std::vector<std::shared_ptr<Texture>> textures;                                 // Our list of used textures
    std::vector<LightStruct> localLights;                                           // Lights attached to this model

    // Temporary binary GLTF buffer — optional reference to .bin file contents if needed for re-processing
    std::vector<uint8_t> gltfBinaryBuffer;

    bool fxActive = false;                                                          // Whether an FX is currently active for this model
	int iAnimationIndex;                                                            // Index of the animation vertices to be played (if any)
    int fxID = -1;                                                                  // ID of the FX to be triggered (used by FXManager)

#if defined(__USE_DIRECTX_11__) || defined(__USE_DIRECTX_12__)
    // Buffers
    ComPtr<ID3D11Buffer> vertexBuffer;                                              // Vertex buffer for GPU.
    ComPtr<ID3D11Buffer> indexBuffer;                                               // Index buffer for GPU.
    ComPtr<ID3D11Buffer> constantBuffer;                                            // Constant buffer for GPU.
    ComPtr<ID3D11Buffer> materialBuffer;                                            // Model Material Constant buffer for GPU.
    ComPtr<ID3D11Buffer> debugConstantBuffer;                                       // Model Debug Constant buffer for GPU Debugging.
    ComPtr<ID3D11Buffer> lightConstantBuffer;                                       // Light constant buffer (register b1)

    // Shaders
    ComPtr<ID3D11VertexShader> vertexShader;                                        // Vertex shader.
    ComPtr<ID3D11PixelShader> pixelShader;                                          // Pixel shader.

    // Shader Blobs
    ComPtr<ID3DBlob> vertexShaderBlob;                                              // Vertex shader blob.
    ComPtr<ID3DBlob> pixelShaderBlob;                                               // Pixel shader blob.

    // Input Layout
    ComPtr<ID3D11InputLayout> inputLayout;                                          // Input layout for vertex shader.

    // Texture Resources
    std::vector<ComPtr<ID3D11ShaderResourceView>> textureSRVs;                      // Texture SRVs.
	std::vector<ComPtr<ID3D11ShaderResourceView>> normalMapSRVs;                    // Normal map SRVs.
    ComPtr<ID3D11SamplerState> samplerState;                                        // Sampler state for textures.

    // Additional fields for OBJ parsing
    std::vector<XMFLOAT3> tempPositions;                                            // Temporary storage for vertex positions.
    std::vector<XMFLOAT3> tempNormals;                                              // Temporary storage for vertex normals.
    std::vector<XMFLOAT2> tempTexCoords;                                            // Temporary storage for texture coordinates.
    std::vector<std::string> materials;                                             // Material names from the OBJ / GLTF 2.0 ? GLB file.

    // PBR Material Properties
    float metallic = 0.0f;                                                          // Base metallic value [0-1]
    float roughness = 0.5f;                                                         // Base roughness value [0-1]
    float reflectionStrength = 1.0f;                                                // Reflection strength multiplier

    // Environment settings
    float envIntensity = 1.0f;                                                      // Environment map intensity
    XMFLOAT3 envTint = { 1.0f, 1.0f, 1.0f };                                        // Environment map tint color
    float mipLODBias = 0.0f;                                                        // Mip level bias for environment sampling
    float fresnel0 = 0.04f;                                                         // Base fresnel reflectance at normal incidence

    // PBR Texture Maps
    std::shared_ptr<Texture> metallicMap;
    std::shared_ptr<Texture> roughnessMap;
    std::shared_ptr<Texture> aoMap;

    // PBR Texture Resource Views
    ComPtr<ID3D11ShaderResourceView> metallicMapSRV;
    ComPtr<ID3D11ShaderResourceView> roughnessMapSRV;
    ComPtr<ID3D11ShaderResourceView> aoMapSRV;
    ComPtr<ID3D11ShaderResourceView> environmentMapSRV;

    // PBR Constant Buffers
    ComPtr<ID3D11Buffer> environmentBuffer;                                         // Model Environment buffer for GPU.

    // PBR Sampler State
    ComPtr<ID3D11SamplerState> environmentSamplerState;

    // PBR Flags
    bool useMetallicMap = false;
    bool useRoughnessMap = false;
    bool useAOMap = false;
    bool useEnvironmentMap = false;

#elif defined(__USE_OPENGL__) || defined(__USE_VULKAN__)
    // Placeholder for OpenGL or Vulkan
    // Add OpenGL/Vulkan-specific fields here (e.g., VAO, VBO, shader programs, etc.).
#endif
};

//==============================================================================
// Model Class Declaration
//==============================================================================
// Encapsulates loading, processing, animating, rendering, and resource management.
class Model {
public:
    Model();
    ~Model();

    bool m_isLoaded;                                                                // Flag indicating if the model is loaded.
	bool bInitialized = false;                                                      // Flag indicating if the model is initialized.
    bool bIsDestroyed = false;
    float m_animationTime;                                                          // Internal animation timer.
    LightsManager lighting;
    ModelInfo m_modelInfo;                                                          // CPU‑side model information.

    // Loads a model from a file (.obj formats).
    bool LoadModel(const std::wstring& filename, int ID);
    bool LoadMTL(const std::wstring& mtlPath);

    // Frees all model resources.
    void DestroyModel();

    // Loads a model from a Wavefront OBJ file.
    bool LoadOBJ(const std::string& path);
    // Updates the constant buffer with the current world matrix.
    void UpdateConstantBuffer();
    // Load Shaders and Compiler
    HRESULT CompileShaderFromFile(const std::wstring& filePath, const std::string& entryPoint, const std::string& shaderModel, ID3DBlob** blobOut);
    // Positioning.
    void SetPosition(XMFLOAT3 position);
    // Our Render routine
    void Render(ID3D11DeviceContext* deviceContext, float deltaTime);
    void TriggerEffect(int effectID);
	// Model Rendering Preparations
    bool SetupModelForRendering();
    bool SetupModelForRendering(int ID);
    void UpdateModelLighting();
    void ApplyDefaultLightingFromManager(LightsManager& myLightsManager);

	// Utility functions
    void CopyFrom(const Model& other);
    ModelInfo GetModelInfo() const { return m_modelInfo; }                          // Get model information

    // Debug functions
    void DebugInfoForModel() const;

    // PBR Extension Methods
    bool SetupPBRResources();
    bool LoadEnvironmentMap(const std::wstring& filePath);
    bool LoadMetallicMap(const std::wstring& filePath);
    bool LoadRoughnessMap(const std::wstring& filePath);
    bool LoadAOMap(const std::wstring& filePath);
    void UpdateEnvironmentBuffer();
    void SetPBRProperties(float metallic, float roughness, float reflectionStrength);
    void SetEnvironmentProperties(float intensity, XMFLOAT3 tint, float mipBias, float fresnel0);

    std::unordered_map<std::string, Material> m_materials;
    std::mutex m_ModelMutex;                                                        // Mutex for thread safety.
    std::atomic<bool> bIsSettingUpModel{ false };

private:
    // File Parsing Fallback funtions.
    void LoadFallbackTexture();
    void LoadFallbackNormalMap();

};
