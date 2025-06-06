#pragma once
/* -------------------------------------------------------------------------------------
   ShaderManager.h - Multi-platform shader compilation and management system

   Supports HLSL 5.0+ (DirectX 11/12) and GLSL (OpenGL/Vulkan) across all platforms.
   Provides centralized shader loading, compilation, caching, and resource management.

   Thread-safe operations using ThreadManager system.
   Integrates with existing Renderer, Models, Lights, and SceneManager classes.
   ------------------------------------------------------------------------------------- */

#include "Includes.h"
#include "ThreadManager.h"
#include "ThreadLockHelper.h"
#include "Debug.h"
#include "Renderer.h"
#include "Lights.h"
#include "SceneManager.h"

   // Forward declarations for integration with existing systems
class Model;
class LightsManager;
class SceneManager;

// Maximum number of shaders that can be loaded simultaneously
const int MAX_SHADERS = 512;

// Shader type enumeration for multi-platform support
enum class ShaderType : int {
    VERTEX_SHADER = 0,                                                  // Vertex processing stage
    PIXEL_SHADER,                                                       // Fragment/Pixel processing stage  
    GEOMETRY_SHADER,                                                    // Geometry processing stage
    HULL_SHADER,                                                        // Tessellation hull stage (DirectX)
    DOMAIN_SHADER,                                                      // Tessellation domain stage (DirectX)
    COMPUTE_SHADER,                                                     // Compute processing stage
    TESSELLATION_CONTROL_SHADER,                                        // Tessellation control stage (OpenGL)
    TESSELLATION_EVALUATION_SHADER,                                     // Tessellation evaluation stage (OpenGL)
    UNKNOWN_SHADER                                                      // Invalid or unrecognized shader type
};

// Shader compilation target platform
enum class ShaderPlatform : int {
    PLATFORM_DIRECTX11 = 0,                                             // DirectX 11 HLSL compilation
    PLATFORM_DIRECTX12,                                                 // DirectX 12 HLSL compilation
    PLATFORM_OPENGL,                                                    // OpenGL GLSL compilation
    PLATFORM_VULKAN,                                                    // Vulkan SPIR-V compilation
    PLATFORM_AUTO_DETECT                                                // Automatically detect based on active renderer
};

// Shader compilation profile information
struct ShaderProfile {
    std::string entryPoint;                                             // Main function name (e.g., "main", "VSMain")
    std::string profileVersion;                                         // Version string (e.g., "vs_5_0", "330 core")
    std::vector<std::string> defines;                                   // Preprocessor definitions
    bool optimized;                                                     // Enable optimization during compilation
    bool debugInfo;                                                     // Include debug information in compiled shader

    // Default constructor with sensible defaults
    ShaderProfile() :
        entryPoint("main"),
        profileVersion(""),
        optimized(true),
        debugInfo(false) {
    }
};

// Cross-platform shader resource container
struct ShaderResource {
    std::string name;                                                   // Unique identifier for shader lookup
    std::wstring filePath;                                              // Source file path for shader code
    ShaderType type;                                                    // Type of shader (vertex, pixel, etc.)
    ShaderPlatform platform;                                            // Target compilation platform
    ShaderProfile profile;                                              // Compilation profile and settings

    // Platform-specific compiled shader objects
#if defined(__USE_DIRECTX_11__) || defined(__USE_DIRECTX_12__)
    Microsoft::WRL::ComPtr<ID3D11VertexShader> d3d11VertexShader;       // DirectX 11 vertex shader
    Microsoft::WRL::ComPtr<ID3D11PixelShader> d3d11PixelShader;         // DirectX 11 pixel shader
    Microsoft::WRL::ComPtr<ID3D11GeometryShader> d3d11GeometryShader;   // DirectX 11 geometry shader
    Microsoft::WRL::ComPtr<ID3D11HullShader> d3d11HullShader;           // DirectX 11 hull shader
    Microsoft::WRL::ComPtr<ID3D11DomainShader> d3d11DomainShader;       // DirectX 11 domain shader
    Microsoft::WRL::ComPtr<ID3D11ComputeShader> d3d11ComputeShader;     // DirectX 11 compute shader
    Microsoft::WRL::ComPtr<ID3DBlob> shaderBlob;                        // Compiled shader bytecode
    Microsoft::WRL::ComPtr<ID3D11InputLayout> inputLayout;              // Vertex input layout (vertex shaders only)
#endif

#if defined(__USE_OPENGL__)
    GLuint openglShaderID;                                              // OpenGL shader object identifier
    GLuint openglProgramID;                                             // OpenGL shader program identifier
#endif

#if defined(__USE_VULKAN__)
    VkShaderModule vulkanShaderModule;                                  // Vulkan shader module handle
    std::vector<uint32_t> spirvBytecode;                                // SPIR-V compiled bytecode
#endif

    // Compilation status and error information
    bool isCompiled;                                                    // Successfully compiled flag
    bool isLoaded;                                                      // Successfully loaded into GPU memory flag
    std::string compilationErrors;                                      // Error messages from compilation process
    std::chrono::system_clock::time_point lastModified;                 // File modification timestamp for hot-reloading

    // Resource management flags
    bool isInUse;                                                       // Currently bound to rendering pipeline flag
    int referenceCount;                                                 // Number of objects using this shader

    // Default constructor
    ShaderResource() :
        type(ShaderType::UNKNOWN_SHADER),
        platform(ShaderPlatform::PLATFORM_AUTO_DETECT),
        isCompiled(false),
        isLoaded(false),
        isInUse(false),
        referenceCount(0) {
#if defined(__USE_OPENGL__)
        openglShaderID = 0;
        openglProgramID = 0;
#endif
#if defined(__USE_VULKAN__)
        vulkanShaderModule = VK_NULL_HANDLE;
#endif
    }
};

// Shader combination for multi-stage rendering passes
struct ShaderProgram {
    std::string programName;                                            // Unique program identifier
    std::string vertexShaderName;                                       // Name of vertex shader in manager
    std::string pixelShaderName;                                        // Name of pixel/fragment shader in manager
    std::string geometryShaderName;                                     // Name of geometry shader (optional)
    std::string hullShaderName;                                         // Name of hull/tessellation control shader (optional)
    std::string domainShaderName;                                       // Name of domain/tessellation evaluation shader (optional)
    std::string computeShaderName;                                      // Name of compute shader (optional)

    bool isLinked;                                                      // Successfully linked program flag
    std::string linkingErrors;                                          // Error messages from linking process

#if defined(__USE_OPENGL__)
    GLuint openglProgramID;                                             // OpenGL linked program identifier
#endif

    // Default constructor
    ShaderProgram() : isLinked(false) {
#if defined(__USE_OPENGL__)
        openglProgramID = 0;
#endif
    }
};

// Statistics and performance monitoring
struct ShaderManagerStats {
    int totalShadersLoaded;                                             // Total number of shaders currently loaded
    int totalProgramsLinked;                                            // Total number of shader programs linked
    int compilationFailures;                                            // Number of compilation failures encountered
    int linkingFailures;                                                // Number of linking failures encountered
    std::chrono::system_clock::time_point lastActivity;                 // Timestamp of last shader operation
    size_t memoryUsage;                                                 // Estimated GPU memory usage in bytes

    // Default constructor
    ShaderManagerStats() :
        totalShadersLoaded(0),
        totalProgramsLinked(0),
        compilationFailures(0),
        linkingFailures(0),
        memoryUsage(0) {
    }
};

//==============================================================================
// ShaderManager Class Declaration
//==============================================================================
class ShaderManager {
public:
    // Constructor and destructor
    ShaderManager();                                                                    // Initialize shader management system
    ~ShaderManager();                                                                   // Clean up all shader resources

    // Initialization and cleanup
    bool Initialize(std::shared_ptr<Renderer> renderer);                                // Initialize with active renderer
    void CleanUp();                                                                     // Release all shader resources and reset state

    // Core shader loading and compilation
    bool LoadShader(const std::string& name, const std::wstring& filePath,
        ShaderType type, const ShaderProfile& profile = ShaderProfile());               // Load and compile shader from file
    bool LoadShaderFromString(const std::string& name, const std::string& shaderCode,
        ShaderType type, const ShaderProfile& profile = ShaderProfile());               // Compile shader from source string
    bool ReloadShader(const std::string& name);                                         // Reload shader from file (hot-reloading support)
    bool UnloadShader(const std::string& name);                                         // Remove shader from memory and GPU

    // Shader program management (multi-stage combinations)
    bool CreateShaderProgram(const std::string& programName,
        const std::string& vertexShaderName,
        const std::string& pixelShaderName,
        const std::string& geometryShaderName = "",
        const std::string& hullShaderName = "",
        const std::string& domainShaderName = "");                                      // Create and link shader program
    bool UseShaderProgram(const std::string& programName);                              // Bind shader program to rendering pipeline
    void UnbindShaderProgram();                                                         // Unbind current shader program

    // Shader resource access and querying
    ShaderResource* GetShader(const std::string& name);                                 // Retrieve shader resource by name
    ShaderProgram* GetShaderProgram(const std::string& programName);                    // Retrieve shader program by name
    bool DoesShaderExist(const std::string& name) const;                                // Check if shader exists in manager
    bool DoesProgramExist(const std::string& programName) const;                        // Check if shader program exists
    std::vector<std::string> GetLoadedShaderNames() const;                              // Get list of all loaded shader names
    std::vector<std::string> GetLoadedProgramNames() const;                             // Get list of all linked program names

    // Hot-reloading and file monitoring
    void EnableHotReloading(bool enable);                                               // Enable/disable automatic shader reloading
    void CheckForShaderFileChanges();                                                   // Manually check for modified shader files

    // Platform-specific compilation
    bool CompileHLSL(ShaderResource& shader);                                           // Compile HLSL shader for DirectX
    bool CompileGLSL(ShaderResource& shader);                                           // Compile GLSL shader for OpenGL
    bool CompileSPIRV(ShaderResource& shader);                                          // Compile SPIR-V shader for Vulkan

    // Integration with existing engine systems
    bool BindShaderToModel(const std::string& shaderProgramName, Model* model);         // Associate shader program with model
    bool SetupLightingShaders(LightsManager* lightManager);                             // Configure shaders for lighting system
    bool LoadSceneShaders(SceneManager* sceneManager);                                  // Load shaders required by scene

    // Statistics and debugging
    ShaderManagerStats GetStatistics() const;                                           // Get performance and usage statistics
    void PrintDebugInfo() const;                                                        // Output debug information to console
    bool ValidateAllShaders();                                                          // Verify all loaded shaders are valid

    // Utility functions
    static std::string ShaderTypeToString(ShaderType type);                             // Convert shader type enum to string
    static std::string ShaderPlatformToString(ShaderPlatform platform);                 // Convert platform enum to string
    static ShaderPlatform DetectCurrentPlatform();                                      // Auto-detect current rendering platform
    static ShaderType GetShaderTypeFromName(const std::string& shaderName);             // Determine shader type from filename/name

private:
    // Core member variables
    bool m_isInitialized;                                                               // Manager initialization status flag
    bool m_isDestroyed;                                                                 // Destruction status flag to prevent double cleanup
    bool m_hotReloadingEnabled;                                                         // Hot-reloading feature enable flag
    const int LOCK_TIMEOUT = 2000;                                                      // Number of Milliseconds for Thread Lock timeout.
    std::shared_ptr<Renderer> m_renderer;                                               // Reference to active renderer system

    // Thread safety using ThreadManager system
    std::string m_lockName;                                                             // Unique lock name for thread safety

    // Shader storage containers
    std::unordered_map<std::string, std::unique_ptr<ShaderResource>> m_shaders;         // Map of loaded shaders by name
    std::unordered_map<std::string, std::unique_ptr<ShaderProgram>> m_programs;         // Map of linked programs by name
    std::string m_currentProgramName;                                                   // Name of currently bound shader program

    // Statistics and monitoring
    mutable ShaderManagerStats m_stats;                                                 // Performance statistics (mutable for const methods)

    // Platform detection and compilation helpers
    ShaderPlatform m_currentPlatform;                                                   // Currently detected rendering platform
    bool DetectPlatformFromRenderer();                                                  // Determine platform from active renderer

    // File I/O and parsing
    bool ReadShaderFile(const std::wstring& filePath, std::string& outContent);         // Read shader source from file
    bool ParseShaderProfile(const std::string& shaderCode, ShaderProfile& profile);     // Extract compilation settings from source
    std::string GenerateShaderDefines(const std::vector<std::string>& defines);         // Convert defines to preprocessor string

    // DirectX-specific compilation methods
#if defined(__USE_DIRECTX_11__) || defined(__USE_DIRECTX_12__)
    bool CompileD3D11VertexShader(ShaderResource& shader);                              // Compile DirectX 11 vertex shader
    bool CompileD3D11PixelShader(ShaderResource& shader);                               // Compile DirectX 11 pixel shader
    bool CompileD3D11GeometryShader(ShaderResource& shader);                            // Compile DirectX 11 geometry shader
    bool CompileD3D11HullShader(ShaderResource& shader);                                // Compile DirectX 11 hull shader
    bool CompileD3D11DomainShader(ShaderResource& shader);                              // Compile DirectX 11 domain shader
    bool CompileD3D11ComputeShader(ShaderResource& shader);                             // Compile DirectX 11 compute shader

    // DirectX-specific input layout creation methods
    bool CreateInputLayoutForShader(ShaderResource& shader, const std::vector<D3D11_INPUT_ELEMENT_DESC>& layout);    // Create vertex input layout
    bool CreateD3D11InputLayout(ShaderResource& shader, const std::vector<D3D11_INPUT_ELEMENT_DESC>& layout); 

#endif

    // OpenGL-specific compilation methods
#if defined(__USE_OPENGL__)
    bool CompileOpenGLShader(ShaderResource& shader); // Compile OpenGL shader
    bool LinkOpenGLProgram(ShaderProgram& program); // Link OpenGL shader program
    GLenum GetOpenGLShaderType(ShaderType type); // Convert ShaderType to OpenGL enum
#endif

    // Vulkan-specific compilation methods
#if defined(__USE_VULKAN__)
    bool CompileVulkanShader(ShaderResource& shader); // Compile Vulkan SPIR-V shader
    bool CreateVulkanShaderModule(ShaderResource& shader); // Create Vulkan shader module
#endif

    // Error handling and validation
    void HandleCompilationError(ShaderResource& shader, const std::string& error); // Process compilation error
    void HandleLinkingError(ShaderProgram& program, const std::string& error); // Process linking error
    bool ValidateShaderResource(const ShaderResource& shader) const; // Verify shader resource integrity
    bool ValidateShaderProgram(const ShaderProgram& program) const; // Verify shader program integrity
    // Diagnostic methods for shader linkage debugging
    void DiagnoseShaderLinkageErrors(const std::string& programName);

    // Hot-reloading support
    std::chrono::system_clock::time_point GetFileModificationTime(const std::wstring& filePath); // Get file timestamp
    void UpdateShaderFileTimestamp(ShaderResource& shader); // Update cached file timestamp

    // Resource cleanup helpers
    void CleanupShaderResource(ShaderResource& shader); // Release individual shader resources
    void CleanupShaderProgram(ShaderProgram& program); // Release individual program resources
    void CleanupAllResources(); // Release all managed resources

    // Statistics updating
    void UpdateStatistics(); // Recalculate performance statistics
    void IncrementCompilationFailure(); // Record compilation failure
    void IncrementLinkingFailure(); // Record linking failure

    // Integration helpers for existing engine systems
    bool SetupModelShaderBindings(Model* model, ShaderProgram* program); // Configure model-shader bindings
    bool ConfigureLightingUniforms(ShaderProgram* program, LightsManager* lightManager); // Setup lighting parameters
    bool LoadDefaultShaders(); // Load standard engine shaders

    // Thread safety enforcement (using ThreadManager system)
    bool AcquireShaderLock(int timeoutMs = 100);                        // Acquire thread lock for shader operations
    void ReleaseShaderLock();                                           // Release thread lock

    // Prevent copying and assignment
    ShaderManager(const ShaderManager&) = delete;                       // Delete copy constructor
    ShaderManager& operator=(const ShaderManager&) = delete;            // Delete assignment operator

    // Default resource creation methods
    bool CreateDefaultSamplers();

    // Default sampler states
    #if defined(PLATFORM_WINDOWS)
        Microsoft::WRL::ComPtr<ID3D11SamplerState> m_defaultSampler;               // Default sampler for slot 0
        Microsoft::WRL::ComPtr<ID3D11SamplerState> m_environmentSampler;           // Environment sampler for slot 1};
    #endif
};

// Global shader manager instance declaration
extern ShaderManager shaderManager;

// External references to existing engine systems
extern ThreadManager threadManager;
extern Debug debug;
extern std::shared_ptr<Renderer> renderer;
