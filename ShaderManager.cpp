/* -------------------------------------------------------------------------------------
   ShaderManager.cpp - Multi-platform shader compilation and management system

   Implementation of centralized shader loading, compilation, caching, and resource
   management for HLSL 5.0+ (DirectX 11/12) and GLSL (OpenGL/Vulkan) across all platforms.

   Provides thread-safe operations using ThreadManager system and integrates with
   existing Renderer, Models, Lights, and SceneManager classes.
   ------------------------------------------------------------------------------------- */

#include "Includes.h"
#include "ExceptionHandler.h"
#include "Debug.h"
#include "ShaderManager.h"
#include "ThreadManager.h"
#include "ThreadLockHelper.h"
#include "Renderer.h"
#include "Models.h"
#include "Lights.h"
#include "SceneManager.h"

   // Platform-specific includes for shader compilation
#if defined(__USE_DIRECTX_11__) || defined(__USE_DIRECTX_12__)
    #include <d3dcompiler.h>                                                        // DirectX shader compiler
    #pragma comment(lib, "d3dcompiler.lib")                                         // Link DirectX compiler library
#endif

#if defined(__USE_OPENGL__)
    #include <GL/glew.h>                                                            // OpenGL extension wrangler
#endif

#if defined(__USE_VULKAN__)
    #include <vulkan/vulkan.h>                                                      // Vulkan API headers
    #include <shaderc/shaderc.hpp>                                                  // SPIR-V compiler
#endif

// External references to existing engine systems
extern ThreadManager threadManager;
extern Debug debug;
extern ExceptionHandler exceptionHandler;
extern std::shared_ptr<Renderer> renderer;

//==============================================================================
// Constructor - Initialize shader management system
//==============================================================================
ShaderManager::ShaderManager() :
    m_isInitialized(false),                                                    // Manager not yet initialized
    m_isDestroyed(false),                                                      // Not destroyed
    m_hotReloadingEnabled(false),                                              // Hot-reloading disabled by default
    m_renderer(nullptr),                                                       // No renderer reference yet
    m_lockName("ShaderManager_MainLock"),                                      // Unique lock name for thread safety
    m_currentProgramName(""),                                                  // No shader program currently bound
    m_currentPlatform(ShaderPlatform::PLATFORM_AUTO_DETECT)                    // Auto-detect platform on initialization
{
#if defined(_DEBUG_SHADERMANAGER_)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"[ShaderManager] Constructor called - initializing shader management system.");
#endif

    // Initialize statistics structure with default values
    m_stats = ShaderManagerStats();                                            // Reset all statistics to zero
    m_stats.lastActivity = std::chrono::system_clock::now();                   // Set initial activity timestamp

    // Clear shader and program containers
    m_shaders.clear();                                                          // Ensure shader map is empty
    m_programs.clear();                                                         // Ensure program map is empty

#if defined(_DEBUG_SHADERMANAGER_)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"[ShaderManager] Constructor completed successfully.");
#endif
}

//==============================================================================
// Destructor - Clean up all shader resources
//==============================================================================
ShaderManager::~ShaderManager() {
    if (m_isDestroyed) return;                                                  // Prevent double destruction

#if defined(_DEBUG_SHADERMANAGER_)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"[ShaderManager] Destructor called - beginning cleanup process.");
#endif

    CleanUp();                                                                  // Release all shader resources
    m_isDestroyed = true;                                                       // Mark as destroyed

#if defined(_DEBUG_SHADERMANAGER_)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"[ShaderManager] Destructor completed successfully.");
#endif
}

//==============================================================================
// Initialize - Setup shader manager with active renderer
//==============================================================================
//==============================================================================
// Initialize - Setup shader manager with active renderer
//==============================================================================
bool ShaderManager::Initialize(std::shared_ptr<Renderer> rendererPtr) {
    #if defined(_DEBUG_SHADERMANAGER_)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"[ShaderManager] Initialize() called - setting up shader management system.");
    #endif

    // Validate input parameters
    if (!rendererPtr) {
        #if defined(_DEBUG_SHADERMANAGER_)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[ShaderManager] Initialize() failed - null renderer provided.");
        #endif
        return false;
    }

    // Prevent double initialization
    if (m_isInitialized) {
        #if defined(_DEBUG_SHADERMANAGER_)
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"[ShaderManager] Initialize() called but already initialized.");
        #endif
        return true;
    }

    // Acquire thread lock for safe initialization with proper RAII cleanup
    ThreadLockHelper lock(threadManager, m_lockName, 5000);                     // 5 second timeout for initialization
    if (!lock.IsLocked()) {
        #if defined(_DEBUG_SHADERMANAGER_)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[ShaderManager] Initialize() failed - could not acquire thread lock.");
        #endif
        return false;
    }

    // Store renderer reference for platform detection and device access
    m_renderer = rendererPtr;

    // Detect current rendering platform from active renderer
    if (!DetectPlatformFromRenderer()) {
        #if defined(_DEBUG_SHADERMANAGER_)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[ShaderManager] Initialize() failed - could not detect rendering platform.");
        #endif
        m_renderer = nullptr;                                                   // Clear renderer reference on failure
        return false;                                                           // ThreadLockHelper destructor will release lock
    }

    // Mark as successfully initialized
    m_isInitialized = true;

    // Update activity timestamp
    m_stats.lastActivity = std::chrono::system_clock::now();

    #if defined(_DEBUG_SHADERMANAGER_)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"[ShaderManager] Initialize() platform detection completed successfully. Platform: %hs",
            ShaderPlatformToString(m_currentPlatform).c_str());
    #endif

    // ThreadLockHelper destructor will automatically release the lock here

    // Load default engine shaders required for basic rendering (outside of lock)
    if (!LoadDefaultShaders()) {
        #if defined(_DEBUG_SHADERMANAGER_)
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"[ShaderManager] Initialize() completed with warnings - some default shaders failed to load.");
        #endif
    }

    #if defined(_DEBUG_SHADERMANAGER_)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"[ShaderManager] Initialize() completed successfully. Platform: %hs, Shaders Loaded: %d",
            ShaderPlatformToString(m_currentPlatform).c_str(), m_stats.totalShadersLoaded);
    #endif

    return true;
}

//==============================================================================
// CleanUp - Release all shader resources and reset state
//==============================================================================
void ShaderManager::CleanUp() {
    #if defined(_DEBUG_SHADERMANAGER_)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"[ShaderManager] CleanUp() called - releasing all shader resources.");
    #endif

    // Unbind any currently active shader program
    UnbindShaderProgram();

    // Clean up all shader programs first (they may reference individual shaders)
    for (auto& programPair : m_programs) {
        if (programPair.second) {
            CleanupShaderProgram(*(programPair.second));                        // Release program-specific resources
        }
    }
    m_programs.clear();                                                         // Clear program container

    // Clean up individual shader resources
    for (auto& shaderPair : m_shaders) {
        if (shaderPair.second) {
            CleanupShaderResource(*(shaderPair.second));                        // Release shader-specific resources
        }
    }
    m_shaders.clear();                                                          // Clear shader container

    // Reset manager state
    m_isInitialized = false;                                                    // Mark as uninitialized
    m_hotReloadingEnabled = false;                                              // Disable hot-reloading
    m_currentProgramName.clear();                                               // Clear current program name
    m_renderer = nullptr;                                                       // Clear renderer reference
    m_currentPlatform = ShaderPlatform::PLATFORM_AUTO_DETECT;                   // Reset platform detection

    // Reset statistics
    m_stats = ShaderManagerStats();                                             // Reset all statistics to default values

    #if defined(_DEBUG_SHADERMANAGER_)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"[ShaderManager] CleanUp() completed successfully.");
    #endif
}

//==============================================================================
// LoadShader - Load and compile shader from file
//==============================================================================
bool ShaderManager::LoadShader(const std::string& name, const std::wstring& filePath,
    ShaderType type, const ShaderProfile& profile) {
    #if defined(_DEBUG_SHADERMANAGER_)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"[ShaderManager] LoadShader() called - Name: %hs, File: %ls, Type: %hs",
            name.c_str(), filePath.c_str(), ShaderTypeToString(type).c_str());
    #endif

    // Validate input parameters
    if (name.empty()) {
        #if defined(_DEBUG_SHADERMANAGER_)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[ShaderManager] LoadShader() failed - empty shader name provided.");
        #endif
        return false;
    }

    if (filePath.empty()) {
        #if defined(_DEBUG_SHADERMANAGER_)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[ShaderManager] LoadShader() failed - empty file path provided.");
        #endif
        return false;
    }

    if (type == ShaderType::UNKNOWN_SHADER) {
        #if defined(_DEBUG_SHADERMANAGER_)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[ShaderManager] LoadShader() failed - unknown shader type specified.");
        #endif
        return false;
    }

    // Ensure manager is initialized
    if (!m_isInitialized) {
        #if defined(_DEBUG_SHADERMANAGER_)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[ShaderManager] LoadShader() failed - shader manager not initialized.");
        #endif
        return false;
    }

/*    // Acquire thread lock for safe shader loading
    ThreadLockHelper lock(threadManager, m_lockName, LOCK_TIMEOUT);
    if (!lock.IsLocked()) {
        #if defined(_DEBUG_SHADERMANAGER_)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[ShaderManager] LoadShader() failed - could not acquire thread lock.");
        #endif
        return false;
    }
*/

    // Check if shader with same name already exists
    if (m_shaders.find(name) != m_shaders.end()) {
        #if defined(_DEBUG_SHADERMANAGER_)
            debug.logDebugMessage(LogLevel::LOG_WARNING, L"[ShaderManager] LoadShader() - shader '%hs' already exists, unloading previous version.",
                name.c_str());
        #endif
        UnloadShader(name);                                                     // Remove existing shader before reloading
    }

    // Verify shader file exists
    if (!std::filesystem::exists(filePath)) {
        #if defined(_DEBUG_SHADERMANAGER_)
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"[ShaderManager] LoadShader() failed - shader file not found: %ls", filePath.c_str());
        #endif
        IncrementCompilationFailure();                                          // Record failure in statistics
        return false;
    }

    // Read shader source code from file
    std::string shaderCode;
    if (!ReadShaderFile(filePath, shaderCode)) {
        #if defined(_DEBUG_SHADERMANAGER_)
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"[ShaderManager] LoadShader() failed - could not read shader file: %ls", filePath.c_str());
        #endif
        IncrementCompilationFailure();                                          // Record failure in statistics
        return false;
    }

    // Create new shader resource
    auto shaderResource = std::make_unique<ShaderResource>();
    shaderResource->name = name;                                                // Set shader identifier
    shaderResource->filePath = filePath;                                        // Store file path for hot-reloading
    shaderResource->type = type;                                                // Set shader type
    shaderResource->platform = (profile.profileVersion.empty()) ? m_currentPlatform : ShaderPlatform::PLATFORM_AUTO_DETECT; // Use current platform or auto-detect
    shaderResource->profile = profile;                                          // Store compilation profile
    shaderResource->referenceCount = 0;                                         // Initialize reference counting
    UpdateShaderFileTimestamp(*shaderResource);                                 // Store file modification time

    // Parse additional profile information from shader source if not provided
    if (profile.profileVersion.empty()) {
        ParseShaderProfile(shaderCode, shaderResource->profile);                // Extract profile from shader comments/pragmas
    }

    // Compile shader based on current platform
    bool compilationSuccess = false;
    switch (m_currentPlatform) {
        case ShaderPlatform::PLATFORM_DIRECTX11:
        case ShaderPlatform::PLATFORM_DIRECTX12:
            compilationSuccess = CompileHLSL(*shaderResource);                  // Compile HLSL for DirectX
            break;

        case ShaderPlatform::PLATFORM_OPENGL:
            compilationSuccess = CompileGLSL(*shaderResource);                  // Compile GLSL for OpenGL
            break;

        case ShaderPlatform::PLATFORM_VULKAN:
            compilationSuccess = CompileSPIRV(*shaderResource);                 // Compile SPIR-V for Vulkan
            break;

        default:
            #if defined(_DEBUG_SHADERMANAGER_)
                debug.logLevelMessage(LogLevel::LOG_ERROR, L"[ShaderManager] LoadShader() failed - unsupported platform for compilation.");
            #endif
            IncrementCompilationFailure();                                      // Record failure in statistics
            return false;
    }

    // Handle compilation results
    if (!compilationSuccess) {
        #if defined(_DEBUG_SHADERMANAGER_)
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"[ShaderManager] LoadShader() failed - compilation error for '%hs': %hs",
                  name.c_str(), shaderResource->compilationErrors.c_str());
        #endif
        IncrementCompilationFailure();                                          // Record failure in statistics
        return false;
    }

    // Store compiled shader in manager
    m_shaders[name] = std::move(shaderResource);                                // Transfer ownership to manager
    m_stats.totalShadersLoaded++;                                               // Update statistics
    m_stats.lastActivity = std::chrono::system_clock::now();                    // Update activity timestamp
    UpdateStatistics();                                                         // Recalculate memory usage and other metrics

    #if defined(_DEBUG_SHADERMANAGER_)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"[ShaderManager] LoadShader() completed successfully - '%hs' loaded and compiled.", name.c_str());
    #endif

    return true;
}

//==============================================================================
// LoadShaderFromString - Compile shader from source string
//==============================================================================
bool ShaderManager::LoadShaderFromString(const std::string& name, const std::string& shaderCode,
    ShaderType type, const ShaderProfile& profile) {
    #if defined(_DEBUG_SHADERMANAGER_)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"[ShaderManager] LoadShaderFromString() called - Name: %hs, Type: %hs, Code Length: %d",
            name.c_str(), ShaderTypeToString(type).c_str(), (int)shaderCode.length());
    #endif

    // Validate input parameters
    if (name.empty()) {
        #if defined(_DEBUG_SHADERMANAGER_)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[ShaderManager] LoadShaderFromString() failed - empty shader name provided.");
        #endif
        return false;
    }

    if (shaderCode.empty()) {
        #if defined(_DEBUG_SHADERMANAGER_)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[ShaderManager] LoadShaderFromString() failed - empty shader code provided.");
        #endif
        return false;
    }

    if (type == ShaderType::UNKNOWN_SHADER) {
        #if defined(_DEBUG_SHADERMANAGER_)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[ShaderManager] LoadShaderFromString() failed - unknown shader type specified.");
        #endif
        return false;
    }

    // Ensure manager is initialized
    if (!m_isInitialized) {
        #if defined(_DEBUG_SHADERMANAGER_)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[ShaderManager] LoadShaderFromString() failed - shader manager not initialized.");
        #endif
        return false;
    }

    // Acquire thread lock for safe shader loading
    ThreadLockHelper lock(threadManager, m_lockName, 5000);                     // 5 second timeout for loading
    if (!lock.IsLocked()) {
        #if defined(_DEBUG_SHADERMANAGER_)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[ShaderManager] LoadShaderFromString() failed - could not acquire thread lock.");
        #endif
        return false;
    }

    // Check if shader with same name already exists
    if (m_shaders.find(name) != m_shaders.end()) {
        #if defined(_DEBUG_SHADERMANAGER_)
            debug.logDebugMessage(LogLevel::LOG_WARNING, L"[ShaderManager] LoadShaderFromString() - shader '%hs' already exists, unloading previous version.",
                name.c_str());
        #endif
        UnloadShader(name);                                                     // Remove existing shader before reloading
    }

    // Create new shader resource
    auto shaderResource = std::make_unique<ShaderResource>();
    shaderResource->name = name;                                                // Set shader identifier
    shaderResource->filePath = L"<inline>";                                     // Mark as inline shader (no file)
    shaderResource->type = type;                                                // Set shader type
    shaderResource->platform = (profile.profileVersion.empty()) ? m_currentPlatform : ShaderPlatform::PLATFORM_AUTO_DETECT; // Use current platform or auto-detect
    shaderResource->profile = profile;                                          // Store compilation profile
    shaderResource->referenceCount = 0;                                         // Initialize reference counting

    // Parse additional profile information from shader source if not provided
    if (profile.profileVersion.empty()) {
        ParseShaderProfile(shaderCode, shaderResource->profile);                // Extract profile from shader comments/pragmas
    }

    // Compile shader based on current platform
    bool compilationSuccess = false;
    switch (m_currentPlatform) {
        case ShaderPlatform::PLATFORM_DIRECTX11:
        case ShaderPlatform::PLATFORM_DIRECTX12:
            compilationSuccess = CompileHLSL(*shaderResource);                  // Compile HLSL for DirectX
            break;

        case ShaderPlatform::PLATFORM_OPENGL:
            compilationSuccess = CompileGLSL(*shaderResource);                  // Compile GLSL for OpenGL
            break;

        case ShaderPlatform::PLATFORM_VULKAN:
            compilationSuccess = CompileSPIRV(*shaderResource);                 // Compile SPIR-V for Vulkan
            break;

        default:
            #if defined(_DEBUG_SHADERMANAGER_)
                debug.logLevelMessage(LogLevel::LOG_ERROR, L"[ShaderManager] LoadShaderFromString() failed - unsupported platform for compilation.");
            #endif
            IncrementCompilationFailure();                                      // Record failure in statistics
            return false;
    }

    // Handle compilation results
    if (!compilationSuccess) {
        #if defined(_DEBUG_SHADERMANAGER_)
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"[ShaderManager] LoadShaderFromString() failed - compilation error for '%hs': %hs",
                  name.c_str(), shaderResource->compilationErrors.c_str());
        #endif
        IncrementCompilationFailure();                                          // Record failure in statistics
        return false;
    }

    // Store compiled shader in manager
    m_shaders[name] = std::move(shaderResource);                                // Transfer ownership to manager
    m_stats.totalShadersLoaded++;                                               // Update statistics
    m_stats.lastActivity = std::chrono::system_clock::now();                    // Update activity timestamp
    UpdateStatistics();                                                         // Recalculate memory usage and other metrics

    #if defined(_DEBUG_SHADERMANAGER_)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"[ShaderManager] LoadShaderFromString() completed successfully - '%hs' loaded and compiled.", name.c_str());
    #endif

    return true;
}

//==============================================================================
// ReloadShader - Reload shader from file (hot-reloading support)
//==============================================================================
bool ShaderManager::ReloadShader(const std::string& name) {
    #if defined(_DEBUG_SHADERMANAGER_)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"[ShaderManager] ReloadShader() called - Name: %hs", name.c_str());
    #endif

    // Validate input parameters
    if (name.empty()) {
        #if defined(_DEBUG_SHADERMANAGER_)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[ShaderManager] ReloadShader() failed - empty shader name provided.");
        #endif
        return false;
    }

    // Ensure manager is initialized
    if (!m_isInitialized) {
        #if defined(_DEBUG_SHADERMANAGER_)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[ShaderManager] ReloadShader() failed - shader manager not initialized.");
        #endif
        return false;
    }

    // Acquire thread lock for safe shader reloading
    ThreadLockHelper lock(threadManager, m_lockName, 5000);                     // 5 second timeout for reloading
    if (!lock.IsLocked()) {
        #if defined(_DEBUG_SHADERMANAGER_)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[ShaderManager] ReloadShader() failed - could not acquire thread lock.");
        #endif
        return false;
    }

    // Find existing shader
    auto shaderIt = m_shaders.find(name);
    if (shaderIt == m_shaders.end()) {
        #if defined(_DEBUG_SHADERMANAGER_)
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"[ShaderManager] ReloadShader() failed - shader '%hs' not found.", name.c_str());
        #endif
        return false;
    }

    ShaderResource* shader = shaderIt->second.get();
    if (!shader) {
        #if defined(_DEBUG_SHADERMANAGER_)
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"[ShaderManager] ReloadShader() failed - shader '%hs' resource is null.", name.c_str());
        #endif
        return false;
    }

    // Check if shader was loaded from file (inline shaders cannot be reloaded)
    if (shader->filePath == L"<inline>") {
        #if defined(_DEBUG_SHADERMANAGER_)
            debug.logDebugMessage(LogLevel::LOG_WARNING, L"[ShaderManager] ReloadShader() failed - cannot reload inline shader '%hs'.", name.c_str());
        #endif
        return false;
    }

    // Store original shader properties for reloading
    std::wstring originalFilePath = shader->filePath;                           // Preserve original file path
    ShaderType originalType = shader->type;                                     // Preserve shader type
    ShaderProfile originalProfile = shader->profile;                           // Preserve compilation profile

    // Check if file still exists
    if (!std::filesystem::exists(originalFilePath)) {
        #if defined(_DEBUG_SHADERMANAGER_)
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"[ShaderManager] ReloadShader() failed - shader file no longer exists: %ls", originalFilePath.c_str());
        #endif
        return false;
    }

    // Check if file has been modified since last load
    auto currentModTime = GetFileModificationTime(originalFilePath);
    if (currentModTime <= shader->lastModified) {
        #if defined(_DEBUG_SHADERMANAGER_)
            debug.logDebugMessage(LogLevel::LOG_INFO, L"[ShaderManager] ReloadShader() - shader '%hs' file has not been modified, skipping reload.", name.c_str());
        #endif
        return true;                                                            // File unchanged, consider success
    }

    // Unload existing shader
    UnloadShader(name);

    // Reload shader from file
    bool reloadSuccess = LoadShader(name, originalFilePath, originalType, originalProfile);

    if (reloadSuccess) {
        #if defined(_DEBUG_SHADERMANAGER_)
            debug.logDebugMessage(LogLevel::LOG_INFO, L"[ShaderManager] ReloadShader() completed successfully - '%hs' reloaded from file.", name.c_str());
        #endif
    }
    else {
        #if defined(_DEBUG_SHADERMANAGER_)
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"[ShaderManager] ReloadShader() failed - could not reload shader '%hs' from file.", name.c_str());
        #endif
    }

    return reloadSuccess;
}

//==============================================================================
// UnloadShader - Remove shader from memory and GPU
//==============================================================================
bool ShaderManager::UnloadShader(const std::string& name) {
    #if defined(_DEBUG_SHADERMANAGER_)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"[ShaderManager] UnloadShader() called - Name: %hs", name.c_str());
    #endif

    // Validate input parameters
    if (name.empty()) {
        #if defined(_DEBUG_SHADERMANAGER_)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[ShaderManager] UnloadShader() failed - empty shader name provided.");
        #endif
        return false;
    }

    // Ensure manager is initialized
    if (!m_isInitialized) {
        #if defined(_DEBUG_SHADERMANAGER_)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[ShaderManager] UnloadShader() failed - shader manager not initialized.");
        #endif
        return false;
    }

    // Acquire thread lock for safe shader unloading
    ThreadLockHelper lock(threadManager, m_lockName, 5000);                     // 5 second timeout for unloading
    if (!lock.IsLocked()) {
        #if defined(_DEBUG_SHADERMANAGER_)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[ShaderManager] UnloadShader() failed - could not acquire thread lock.");
        #endif
        return false;
    }

    // Find shader to unload
    auto shaderIt = m_shaders.find(name);
    if (shaderIt == m_shaders.end()) {
        #if defined(_DEBUG_SHADERMANAGER_)
            debug.logDebugMessage(LogLevel::LOG_WARNING, L"[ShaderManager] UnloadShader() - shader '%hs' not found.", name.c_str());
        #endif
        return false;
    }

    ShaderResource* shader = shaderIt->second.get();
    if (!shader) {
        #if defined(_DEBUG_SHADERMANAGER_)
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"[ShaderManager] UnloadShader() failed - shader '%hs' resource is null.", name.c_str());
        #endif
        return false;
    }

    // Check if shader is currently in use
    if (shader->isInUse || shader->referenceCount > 0) {
        #if defined(_DEBUG_SHADERMANAGER_)
            debug.logDebugMessage(LogLevel::LOG_WARNING, L"[ShaderManager] UnloadShader() - shader '%hs' is currently in use (refs: %d), forcing unload.",
                  name.c_str(), shader->referenceCount);
        #endif
    }

    // Check if shader is part of any linked programs
    for (const auto& programPair : m_programs) {
        const ShaderProgram* program = programPair.second.get();
        if (program && (program->vertexShaderName == name ||
            program->pixelShaderName == name ||
            program->geometryShaderName == name ||
            program->hullShaderName == name ||
            program->domainShaderName == name ||
            program->computeShaderName == name)) {
            #if defined(_DEBUG_SHADERMANAGER_)
                debug.logDebugMessage(LogLevel::LOG_WARNING, L"[ShaderManager] UnloadShader() - shader '%hs' is referenced by program '%hs'.",
                    name.c_str(), programPair.first.c_str());
            #endif
        }
    }

    // Clean up shader-specific resources
    CleanupShaderResource(*shader);

    // Remove shader from manager
    m_shaders.erase(shaderIt);                                                  // Remove from container
    m_stats.totalShadersLoaded--;                                               // Update statistics
    m_stats.lastActivity = std::chrono::system_clock::now();                    // Update activity timestamp
    UpdateStatistics();                                                         // Recalculate memory usage and other metrics

    #if defined(_DEBUG_SHADERMANAGER_)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"[ShaderManager] UnloadShader() completed successfully - '%hs' unloaded.", name.c_str());
    #endif

    return true;
}

//==============================================================================
// CreateShaderProgram - Create and link shader program
//==============================================================================
bool ShaderManager::CreateShaderProgram(const std::string& programName,
    const std::string& vertexShaderName,
    const std::string& pixelShaderName,
    const std::string& geometryShaderName,
    const std::string& hullShaderName,
    const std::string& domainShaderName) {
    #if defined(_DEBUG_SHADERMANAGER_)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"[ShaderManager] CreateShaderProgram() called - Program: %hs, VS: %hs, PS: %hs",
            programName.c_str(), vertexShaderName.c_str(), pixelShaderName.c_str());
    #endif

        // Validate input parameters
        if (programName.empty()) {
            #if defined(_DEBUG_SHADERMANAGER_)
                debug.logLevelMessage(LogLevel::LOG_ERROR, L"[ShaderManager] CreateShaderProgram() failed - empty program name provided.");
            #endif
            return false;
        }

        if (vertexShaderName.empty() || pixelShaderName.empty()) {
            #if defined(_DEBUG_SHADERMANAGER_)
                debug.logLevelMessage(LogLevel::LOG_ERROR, L"[ShaderManager] CreateShaderProgram() failed - vertex and pixel shaders are required.");
            #endif
            return false;
        }

        // Ensure manager is initialized
        if (!m_isInitialized) {
            #if defined(_DEBUG_SHADERMANAGER_)
                debug.logLevelMessage(LogLevel::LOG_ERROR, L"[ShaderManager] CreateShaderProgram() failed - shader manager not initialized.");
            #endif
            return false;
        }

        // Check if program with same name already exists
        if (m_programs.find(programName) != m_programs.end()) {
            #if defined(_DEBUG_SHADERMANAGER_)
                debug.logDebugMessage(LogLevel::LOG_WARNING, L"[ShaderManager] CreateShaderProgram() - program '%hs' already exists, replacing.",
                    programName.c_str());
            #endif

            // Clean up existing program
            auto existingIt = m_programs.find(programName);
            if (existingIt != m_programs.end() && existingIt->second) {
                CleanupShaderProgram(*(existingIt->second));                        // Release existing program resources
                m_programs.erase(existingIt);                                       // Remove from container
                m_stats.totalProgramsLinked--;                                      // Update statistics
            }
        }

        // Verify required shaders exist
        if (m_shaders.find(vertexShaderName) == m_shaders.end()) {
            #if defined(_DEBUG_SHADERMANAGER_)
                debug.logDebugMessage(LogLevel::LOG_ERROR, L"[ShaderManager] CreateShaderProgram() failed - vertex shader '%hs' not found.",
                    vertexShaderName.c_str());
            #endif
            IncrementLinkingFailure();                                              // Record failure in statistics
            return false;
        }

        if (m_shaders.find(pixelShaderName) == m_shaders.end()) {
            #if defined(_DEBUG_SHADERMANAGER_)
                debug.logDebugMessage(LogLevel::LOG_ERROR, L"[ShaderManager] CreateShaderProgram() failed - pixel shader '%hs' not found.",
                    pixelShaderName.c_str());
            #endif
            IncrementLinkingFailure();                                              // Record failure in statistics
            return false;
        }

        // Verify optional shaders exist if specified
        if (!geometryShaderName.empty() && m_shaders.find(geometryShaderName) == m_shaders.end()) {
            #if defined(_DEBUG_SHADERMANAGER_)
                debug.logDebugMessage(LogLevel::LOG_ERROR, L"[ShaderManager] CreateShaderProgram() failed - geometry shader '%hs' not found.",
                    geometryShaderName.c_str());
            #endif
            IncrementLinkingFailure();                                              // Record failure in statistics
            return false;
        }

        if (!hullShaderName.empty() && m_shaders.find(hullShaderName) == m_shaders.end()) {
            #if defined(_DEBUG_SHADERMANAGER_)
                debug.logDebugMessage(LogLevel::LOG_ERROR, L"[ShaderManager] CreateShaderProgram() failed - hull shader '%hs' not found.",
                    hullShaderName.c_str());
            #endif
            IncrementLinkingFailure();                                              // Record failure in statistics
            return false;
        }

        if (!domainShaderName.empty() && m_shaders.find(domainShaderName) == m_shaders.end()) {
            #if defined(_DEBUG_SHADERMANAGER_)
                debug.logDebugMessage(LogLevel::LOG_ERROR, L"[ShaderManager] CreateShaderProgram() failed - domain shader '%hs' not found.",
                    domainShaderName.c_str());
            #endif
            IncrementLinkingFailure();                                              // Record failure in statistics
            return false;
        }

        // Create new shader program
        auto shaderProgram = std::make_unique<ShaderProgram>();
        shaderProgram->programName = programName;                                   // Set program identifier
        shaderProgram->vertexShaderName = vertexShaderName;                        // Store vertex shader reference
        shaderProgram->pixelShaderName = pixelShaderName;                          // Store pixel shader reference
        shaderProgram->geometryShaderName = geometryShaderName;                    // Store geometry shader reference (optional)
        shaderProgram->hullShaderName = hullShaderName;                            // Store hull shader reference (optional)
        shaderProgram->domainShaderName = domainShaderName;                        // Store domain shader reference (optional)
        shaderProgram->isLinked = false;                                            // Initialize as not linked

        // Link shader program based on current platform
        bool linkingSuccess = false;
        switch (m_currentPlatform) {
        case ShaderPlatform::PLATFORM_DIRECTX11:
        case ShaderPlatform::PLATFORM_DIRECTX12:
            // DirectX doesn't use linked programs like OpenGL, shaders are bound individually
            shaderProgram->isLinked = true;                                     // Mark as "linked" for DirectX
            linkingSuccess = true;
            #if defined(_DEBUG_SHADERMANAGER_)
                debug.logDebugMessage(LogLevel::LOG_INFO, L"[ShaderManager] DirectX program '%hs' created (no linking required).", programName.c_str());
            #endif
            break;

        case ShaderPlatform::PLATFORM_OPENGL:
            #if defined(__USE_OPENGL__)
                linkingSuccess = LinkOpenGLProgram(*shaderProgram);             // Link OpenGL shader program
            #else
                #if defined(_DEBUG_SHADERMANAGER_)
                    debug.logLevelMessage(LogLevel::LOG_ERROR, L"[ShaderManager] OpenGL not available for program linking.");
                #endif
            linkingSuccess = false;
            #endif
            break;

        case ShaderPlatform::PLATFORM_VULKAN:
            // Vulkan uses pipeline objects instead of linked programs
            shaderProgram->isLinked = true;                                     // Mark as "linked" for Vulkan
            linkingSuccess = true;
            #if defined(_DEBUG_SHADERMANAGER_)
                debug.logDebugMessage(LogLevel::LOG_INFO, L"[ShaderManager] Vulkan program '%hs' created (pipeline will be created at render time).", programName.c_str());
            #endif
            break;

        default:
            #if defined(_DEBUG_SHADERMANAGER_)
                debug.logLevelMessage(LogLevel::LOG_ERROR, L"[ShaderManager] CreateShaderProgram() failed - unsupported platform for program linking.");
            #endif
            IncrementLinkingFailure();                                          // Record failure in statistics
            return false;
        }

        // Handle linking results
        if (!linkingSuccess) {
            #if defined(_DEBUG_SHADERMANAGER_)
                debug.logDebugMessage(LogLevel::LOG_ERROR, L"[ShaderManager] CreateShaderProgram() failed - linking error for '%hs': %hs",
                    programName.c_str(), shaderProgram->linkingErrors.c_str());
            #endif
            IncrementLinkingFailure();                                              // Record failure in statistics
            return false;
        }

        // Increment reference counts for used shaders
        m_shaders[vertexShaderName]->referenceCount++;                             // Increment vertex shader reference count
        m_shaders[pixelShaderName]->referenceCount++;                              // Increment pixel shader reference count

        if (!geometryShaderName.empty()) {
            m_shaders[geometryShaderName]->referenceCount++;                       // Increment geometry shader reference count
        }

        if (!hullShaderName.empty()) {
            m_shaders[hullShaderName]->referenceCount++;                           // Increment hull shader reference count
        }

        if (!domainShaderName.empty()) {
            m_shaders[domainShaderName]->referenceCount++;                         // Increment domain shader reference count
        }

        // Store linked program in manager
        m_programs[programName] = std::move(shaderProgram);                         // Transfer ownership to manager
        m_stats.totalProgramsLinked++;                                              // Update statistics
        m_stats.lastActivity = std::chrono::system_clock::now();                    // Update activity timestamp
        UpdateStatistics();                                                         // Recalculate memory usage and other metrics

        #if defined(_DEBUG_SHADERMANAGER_)
            debug.logDebugMessage(LogLevel::LOG_INFO, L"[ShaderManager] CreateShaderProgram() completed successfully - '%hs' created and linked.", programName.c_str());
        #endif

        return true;
}

//==============================================================================
// DiagnoseShaderLinkageErrors - Analyze and report shader linkage mismatches
//==============================================================================
void ShaderManager::DiagnoseShaderLinkageErrors(const std::string& programName) {
    #if defined(_DEBUG_SHADERMANAGER_)
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"[ShaderManager] DiagnoseShaderLinkageErrors() called for program: %hs", programName.c_str());
    #endif

    // Find the shader program
    auto programIt = m_programs.find(programName);
    if (programIt == m_programs.end()) {
        #if defined(_DEBUG_SHADERMANAGER_)
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"[ShaderManager] Program '%hs' not found for diagnosis.", programName.c_str());
        #endif
        return;
    }

    ShaderProgram* program = programIt->second.get();
    if (!program) {
        #if defined(_DEBUG_SHADERMANAGER_)
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"[ShaderManager] Program '%hs' is null.", programName.c_str());
        #endif
        return;
    }

    // Get vertex shader resource
    ShaderResource* vertexShader = GetShader(program->vertexShaderName);
    if (!vertexShader || !vertexShader->shaderBlob) {
        #if defined(_DEBUG_SHADERMANAGER_)
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"[ShaderManager] Vertex shader '%hs' not found or has no blob.", program->vertexShaderName.c_str());
        #endif
        return;
    }

#if defined(__USE_DIRECTX_11__) || defined(__USE_DIRECTX_12__)
    // Use D3D reflection to analyze vertex shader input signature
    Microsoft::WRL::ComPtr<ID3D11ShaderReflection> reflection;
    HRESULT hr = D3DReflect(
        vertexShader->shaderBlob->GetBufferPointer(),
        vertexShader->shaderBlob->GetBufferSize(),
        IID_ID3D11ShaderReflection,
        reinterpret_cast<void**>(reflection.GetAddressOf())
    );

    if (SUCCEEDED(hr)) {
        D3D11_SHADER_DESC shaderDesc;
        reflection->GetDesc(&shaderDesc);

        #if defined(_DEBUG_SHADERMANAGER_)
            debug.logDebugMessage(LogLevel::LOG_INFO, L"[ShaderManager] Vertex shader '%hs' expects %d input parameters:",
                program->vertexShaderName.c_str(), shaderDesc.InputParameters);
        #endif

        // Enumerate expected input parameters
        for (UINT i = 0; i < shaderDesc.InputParameters; ++i) {
            D3D11_SIGNATURE_PARAMETER_DESC paramDesc;
            hr = reflection->GetInputParameterDesc(i, &paramDesc);

            if (SUCCEEDED(hr)) {
                #if defined(_DEBUG_SHADERMANAGER_)
                    debug.logDebugMessage(LogLevel::LOG_INFO, L"[ShaderManager]   Input[%d]: Semantic='%hs', Index=%d, Register=%d, Mask=0x%X",
                        i, paramDesc.SemanticName, paramDesc.SemanticIndex, paramDesc.Register, paramDesc.Mask);
                #endif
            }
        }
    }
    else {
        #if defined(_DEBUG_SHADERMANAGER_)
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"[ShaderManager] Failed to create shader reflection for diagnosis (HRESULT: 0x%08X).", hr);
        #endif
    }
#else
    #if defined(_DEBUG_SHADERMANAGER_)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[ShaderManager] DiagnoseShaderLinkageErrors() - DirectX not available for reflection.");
    #endif
#endif
}
//==============================================================================
// UseShaderProgram - Bind shader program to rendering pipeline
//==============================================================================
bool ShaderManager::UseShaderProgram(const std::string& programName) {
    // Validate input parameters
    if (programName.empty()) {
        #if defined(_DEBUG_SHADERMANAGER_)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[ShaderManager] UseShaderProgram() failed - empty program name provided.");
        #endif
        return false;
    }

    // Ensure manager is initialized
    if (!m_isInitialized) {
        #if defined(_DEBUG_SHADERMANAGER_)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[ShaderManager] UseShaderProgram() failed - shader manager not initialized.");
        #endif
        return false;
    }

    // Check if renderer is available
    if (!m_renderer) {
        #if defined(_DEBUG_SHADERMANAGER_)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[ShaderManager] UseShaderProgram() failed - no renderer available.");
        #endif
        return false;
    }

    // Find shader program
    auto programIt = m_programs.find(programName);
    if (programIt == m_programs.end()) {
        #if defined(_DEBUG_SHADERMANAGER_)
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"[ShaderManager] UseShaderProgram() failed - program '%hs' not found.", programName.c_str());
        #endif
        return false;
    }

    ShaderProgram* program = programIt->second.get();
    if (!program || !program->isLinked) {
        #if defined(_DEBUG_SHADERMANAGER_)
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"[ShaderManager] UseShaderProgram() failed - program '%hs' is not linked.", programName.c_str());
        #endif
        return false;
    }

    // Bind shader program based on current platform
    bool bindingSuccess = false;
    switch (m_currentPlatform) {
        case ShaderPlatform::PLATFORM_DIRECTX11:
        case ShaderPlatform::PLATFORM_DIRECTX12: 
        {
            #if defined(__USE_DIRECTX_11__) || defined(__USE_DIRECTX_12__)
            // Get device context from renderer
            void* deviceContext = m_renderer->GetDeviceContext();
            if (!deviceContext) {
                #if defined(_DEBUG_SHADERMANAGER_)
                    debug.logLevelMessage(LogLevel::LOG_ERROR, L"[ShaderManager] UseShaderProgram() failed - no DirectX device context available.");
                #endif
                return false;
            }

            ID3D11DeviceContext* d3dContext = static_cast<ID3D11DeviceContext*>(deviceContext);

            // Bind vertex shader
            ShaderResource* vertexShader = m_shaders[program->vertexShaderName].get();
            if (vertexShader && vertexShader->d3d11VertexShader) {
                d3dContext->VSSetShader(vertexShader->d3d11VertexShader.Get(), nullptr, 0); // Bind vertex shader to pipeline
                if (vertexShader->inputLayout) {
                    d3dContext->IASetInputLayout(vertexShader->inputLayout.Get()); // Set input layout for vertex shader
                }
                else {
                    #if defined(_DEBUG_SHADERMANAGER_)
                        debug.logDebugMessage(LogLevel::LOG_WARNING, L"[ShaderManager] No input layout available for vertex shader '%hs' - this may cause linkage errors.", program->vertexShaderName.c_str());
                    #endif
                    // Diagnose the shader linkage issue
                    DiagnoseShaderLinkageErrors(programName);
                }
            }
            else {
                #if defined(_DEBUG_SHADERMANAGER_)
                    debug.logDebugMessage(LogLevel::LOG_ERROR, L"[ShaderManager] Vertex shader '%hs' not available for binding.", program->vertexShaderName.c_str());
                #endif
                return false;
            }

            // Bind pixel shader
            ShaderResource* pixelShader = m_shaders[program->pixelShaderName].get();
            if (pixelShader && pixelShader->d3d11PixelShader) {
                d3dContext->PSSetShader(pixelShader->d3d11PixelShader.Get(), nullptr, 0); // Bind pixel shader to pipeline
            }
            else {
                #if defined(_DEBUG_SHADERMANAGER_)
                    debug.logDebugMessage(LogLevel::LOG_ERROR, L"[ShaderManager] Pixel shader '%hs' not available for binding.", program->pixelShaderName.c_str());
                #endif
                return false;
            }

            // Bind optional geometry shader
            if (!program->geometryShaderName.empty()) {
                ShaderResource* geometryShader = m_shaders[program->geometryShaderName].get();
                if (geometryShader && geometryShader->d3d11GeometryShader) {
                    d3dContext->GSSetShader(geometryShader->d3d11GeometryShader.Get(), nullptr, 0); // Bind geometry shader to pipeline
                }
            }
            else {
                d3dContext->GSSetShader(nullptr, nullptr, 0);               // Unbind geometry shader if not used
            }

            // Bind optional hull shader
            if (!program->hullShaderName.empty()) {
                ShaderResource* hullShader = m_shaders[program->hullShaderName].get();
                if (hullShader && hullShader->d3d11HullShader) {
                    d3dContext->HSSetShader(hullShader->d3d11HullShader.Get(), nullptr, 0); // Bind hull shader to pipeline
                }
            }
            else {
                d3dContext->HSSetShader(nullptr, nullptr, 0);               // Unbind hull shader if not used
            }

            // Bind optional domain shader
            if (!program->domainShaderName.empty()) {
                ShaderResource* domainShader = m_shaders[program->domainShaderName].get();
                if (domainShader && domainShader->d3d11DomainShader) {
                    d3dContext->DSSetShader(domainShader->d3d11DomainShader.Get(), nullptr, 0); // Bind domain shader to pipeline
                }
            }
            else {
                d3dContext->DSSetShader(nullptr, nullptr, 0);               // Unbind domain shader if not used
            }

            bindingSuccess = true;
        #else
            #if defined(_DEBUG_SHADERMANAGER_)
                debug.logLevelMessage(LogLevel::LOG_ERROR, L"[ShaderManager] DirectX not available for shader binding.");
            #endif
            bindingSuccess = false;
        #endif
        break;
    }

    case ShaderPlatform::PLATFORM_OPENGL: {
#if defined(__USE_OPENGL__)
        if (program->openglProgramID > 0) {
            glUseProgram(program->openglProgramID);                     // Bind OpenGL shader program

            // Check for OpenGL errors
            GLenum error = glGetError();
            if (error != GL_NO_ERROR) {
#if defined(_DEBUG_SHADERMANAGER_)
                debug.logDebugMessage(LogLevel::LOG_ERROR, L"[ShaderManager] OpenGL error binding program '%hs': %d",
                    programName.c_str(), error);
#endif
                bindingSuccess = false;
            }
            else {
                bindingSuccess = true;
#if defined(_DEBUG_SHADERMANAGER_)
                debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[ShaderManager] OpenGL program '%hs' bound to pipeline.", programName.c_str());
#endif
            }
        }
        else {
#if defined(_DEBUG_SHADERMANAGER_)
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"[ShaderManager] OpenGL program '%hs' has invalid program ID.", programName.c_str());
#endif
            bindingSuccess = false;
        }
#else
#if defined(_DEBUG_SHADERMANAGER_)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[ShaderManager] OpenGL not available for shader binding.");
#endif
        bindingSuccess = false;
#endif
        break;
    }

    case ShaderPlatform::PLATFORM_VULKAN: {
#if defined(__USE_VULKAN__)
        // Vulkan shader binding happens during pipeline creation and command buffer recording
        // This is a placeholder for future Vulkan implementation
        bindingSuccess = true;
#if defined(_DEBUG_SHADERMANAGER_)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[ShaderManager] Vulkan program '%hs' marked for pipeline binding.", programName.c_str());
#endif
#else
#if defined(_DEBUG_SHADERMANAGER_)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[ShaderManager] Vulkan not available for shader binding.");
#endif
        bindingSuccess = false;
#endif
        break;
    }

    default:
#if defined(_DEBUG_SHADERMANAGER_)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[ShaderManager] UseShaderProgram() failed - unsupported platform for shader binding.");
#endif
        bindingSuccess = false;
        break;
    }

    // Update current program tracking
    if (bindingSuccess) {
        // Mark previous program shaders as not in use
        if (!m_currentProgramName.empty() && m_currentProgramName != programName) {
            auto prevProgramIt = m_programs.find(m_currentProgramName);
            if (prevProgramIt != m_programs.end() && prevProgramIt->second) {
                ShaderProgram* prevProgram = prevProgramIt->second.get();

                // Mark all shaders in previous program as not in use
                if (m_shaders.find(prevProgram->vertexShaderName) != m_shaders.end()) {
                    m_shaders[prevProgram->vertexShaderName]->isInUse = false;
                }
                if (m_shaders.find(prevProgram->pixelShaderName) != m_shaders.end()) {
                    m_shaders[prevProgram->pixelShaderName]->isInUse = false;
                }
                if (!prevProgram->geometryShaderName.empty() && m_shaders.find(prevProgram->geometryShaderName) != m_shaders.end()) {
                    m_shaders[prevProgram->geometryShaderName]->isInUse = false;
                }
                if (!prevProgram->hullShaderName.empty() && m_shaders.find(prevProgram->hullShaderName) != m_shaders.end()) {
                    m_shaders[prevProgram->hullShaderName]->isInUse = false;
                }
                if (!prevProgram->domainShaderName.empty() && m_shaders.find(prevProgram->domainShaderName) != m_shaders.end()) {
                    m_shaders[prevProgram->domainShaderName]->isInUse = false;
                }
            }
        }

        // Mark current program shaders as in use
        m_shaders[program->vertexShaderName]->isInUse = true;                   // Mark vertex shader as in use
        m_shaders[program->pixelShaderName]->isInUse = true;                    // Mark pixel shader as in use

        if (!program->geometryShaderName.empty()) {
            m_shaders[program->geometryShaderName]->isInUse = true;             // Mark geometry shader as in use
        }

        if (!program->hullShaderName.empty()) {
            m_shaders[program->hullShaderName]->isInUse = true;                 // Mark hull shader as in use
        }

        if (!program->domainShaderName.empty()) {
            m_shaders[program->domainShaderName]->isInUse = true;               // Mark domain shader as in use
        }

        m_currentProgramName = programName;                                     // Update current program tracking
        m_stats.lastActivity = std::chrono::system_clock::now();                // Update activity timestamp
    }

    return bindingSuccess;
}

//==============================================================================
// UnbindShaderProgram - Unbind current shader program
//==============================================================================
void ShaderManager::UnbindShaderProgram() {
#if defined(_DEBUG_SHADERMANAGER_)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[ShaderManager] UnbindShaderProgram() called.");
#endif

    // Check if any program is currently bound
    if (m_currentProgramName.empty()) {
#if defined(_DEBUG_SHADERMANAGER_)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[ShaderManager] UnbindShaderProgram() - no program currently bound.");
#endif
        return;
    }

    // Ensure manager is initialized
    if (!m_isInitialized) {
#if defined(_DEBUG_SHADERMANAGER_)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[ShaderManager] UnbindShaderProgram() failed - shader manager not initialized.");
#endif
        return;
    }

    // Check if renderer is available
    if (!m_renderer) {
#if defined(_DEBUG_SHADERMANAGER_)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[ShaderManager] UnbindShaderProgram() failed - no renderer available.");
#endif
        return;
    }

    // Unbind shader program based on current platform
    switch (m_currentPlatform) {
    case ShaderPlatform::PLATFORM_DIRECTX11:
    case ShaderPlatform::PLATFORM_DIRECTX12: {
#if defined(__USE_DIRECTX_11__) || defined(__USE_DIRECTX_12__)
        // Get device context from renderer
        void* deviceContext = m_renderer->GetDeviceContext();
        if (deviceContext) {
            ID3D11DeviceContext* d3dContext = static_cast<ID3D11DeviceContext*>(deviceContext);

            // Unbind all shader stages
            d3dContext->VSSetShader(nullptr, nullptr, 0);               // Unbind vertex shader
            d3dContext->PSSetShader(nullptr, nullptr, 0);               // Unbind pixel shader
            d3dContext->GSSetShader(nullptr, nullptr, 0);               // Unbind geometry shader
            d3dContext->HSSetShader(nullptr, nullptr, 0);               // Unbind hull shader
            d3dContext->DSSetShader(nullptr, nullptr, 0);               // Unbind domain shader
            d3dContext->CSSetShader(nullptr, nullptr, 0);               // Unbind compute shader

#if defined(_DEBUG_SHADERMANAGER_)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[ShaderManager] DirectX shaders unbound from pipeline.");
#endif
        }
#endif
        break;
    }

    case ShaderPlatform::PLATFORM_OPENGL: {
#if defined(__USE_OPENGL__)
        glUseProgram(0);                                                // Unbind OpenGL shader program

#if defined(_DEBUG_SHADERMANAGER_)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[ShaderManager] OpenGL program unbound from pipeline.");
#endif
#endif
        break;
    }

    case ShaderPlatform::PLATFORM_VULKAN: {
        #if defined(__USE_VULKAN__)
        // Vulkan shader unbinding happens during pipeline state changes
        // This is a placeholder for future Vulkan implementation
            #if defined(_DEBUG_SHADERMANAGER_)
                debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[ShaderManager] Vulkan program marked as unbound.");
            #endif
        #endif
        break;
    }

    default:
        #if defined(_DEBUG_SHADERMANAGER_)
            debug.logDebugMessage(LogLevel::LOG_WARNING, L"[ShaderManager] UnbindShaderProgram() - unsupported platform for shader unbinding.");
        #endif
        break;
    }

    // Mark current program shaders as not in use
    auto currentProgramIt = m_programs.find(m_currentProgramName);
    if (currentProgramIt != m_programs.end() && currentProgramIt->second) {
        ShaderProgram* currentProgram = currentProgramIt->second.get();

        // Mark all shaders in current program as not in use
        if (m_shaders.find(currentProgram->vertexShaderName) != m_shaders.end()) {
            m_shaders[currentProgram->vertexShaderName]->isInUse = false;
        }
        if (m_shaders.find(currentProgram->pixelShaderName) != m_shaders.end()) {
            m_shaders[currentProgram->pixelShaderName]->isInUse = false;
        }
        if (!currentProgram->geometryShaderName.empty() && m_shaders.find(currentProgram->geometryShaderName) != m_shaders.end()) {
            m_shaders[currentProgram->geometryShaderName]->isInUse = false;
        }
        if (!currentProgram->hullShaderName.empty() && m_shaders.find(currentProgram->hullShaderName) != m_shaders.end()) {
            m_shaders[currentProgram->hullShaderName]->isInUse = false;
        }
        if (!currentProgram->domainShaderName.empty() && m_shaders.find(currentProgram->domainShaderName) != m_shaders.end()) {
            m_shaders[currentProgram->domainShaderName]->isInUse = false;
        }
    }

    // Clear current program tracking
    m_currentProgramName.clear();                                               // Clear current program name
    m_stats.lastActivity = std::chrono::system_clock::now();                    // Update activity timestamp

    #if defined(_DEBUG_SHADERMANAGER_)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[ShaderManager] UnbindShaderProgram() completed successfully.");
    #endif
}

//==============================================================================
// GetShader - Retrieve shader resource by name
//==============================================================================
ShaderResource* ShaderManager::GetShader(const std::string& name) {
    #if defined(_DEBUG_SHADERMANAGER_)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[ShaderManager] GetShader() called - Name: %hs", name.c_str());
    #endif

    // Validate input parameters
    if (name.empty()) {
        #if defined(_DEBUG_SHADERMANAGER_)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[ShaderManager] GetShader() failed - empty shader name provided.");
        #endif
        return nullptr;
    }

    // Ensure manager is initialized
    if (!m_isInitialized) {
        #if defined(_DEBUG_SHADERMANAGER_)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[ShaderManager] GetShader() failed - shader manager not initialized.");
        #endif
        return nullptr;
    }

    // Find shader in container
    auto shaderIt = m_shaders.find(name);
    if (shaderIt == m_shaders.end()) {
        #if defined(_DEBUG_SHADERMANAGER_)
            debug.logDebugMessage(LogLevel::LOG_WARNING, L"[ShaderManager] GetShader() - shader '%hs' not found.", name.c_str());
        #endif
        return nullptr;
    }

    return shaderIt->second.get();                                              // Return pointer to shader resource
}

//==============================================================================
// GetShaderProgram - Retrieve shader program by name
//==============================================================================
ShaderProgram* ShaderManager::GetShaderProgram(const std::string& programName) {
    #if defined(_DEBUG_SHADERMANAGER_)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[ShaderManager] GetShaderProgram() called - Name: %hs", programName.c_str());
    #endif

    // Validate input parameters
    if (programName.empty()) {
        #if defined(_DEBUG_SHADERMANAGER_)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[ShaderManager] GetShaderProgram() failed - empty program name provided.");
        #endif
        return nullptr;
    }

    // Ensure manager is initialized
    if (!m_isInitialized) {
        #if defined(_DEBUG_SHADERMANAGER_)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[ShaderManager] GetShaderProgram() failed - shader manager not initialized.");
        #endif
        return nullptr;
    }

    // Find program in container
    auto programIt = m_programs.find(programName);
    if (programIt == m_programs.end()) {
    #if defined(_DEBUG_SHADERMANAGER_)
        debug.logDebugMessage(LogLevel::LOG_WARNING, L"[ShaderManager] GetShaderProgram() - program '%hs' not found.", programName.c_str());
    #endif
        return nullptr;
    }

    return programIt->second.get();                                             // Return pointer to shader program
}

//==============================================================================
// DoesShaderExist - Check if shader exists in manager
//==============================================================================
bool ShaderManager::DoesShaderExist(const std::string& name) const {
    // Validate input parameters
    if (name.empty()) {
        return false;                                                           // Empty name never exists
    }

    // Check if shader exists in container
    return m_shaders.find(name) != m_shaders.end();                             // Return true if found
}

//==============================================================================
// DoesProgramExist - Check if shader program exists
//==============================================================================
bool ShaderManager::DoesProgramExist(const std::string& programName) const {
    // Validate input parameters
    if (programName.empty()) {
        return false;                                                           // Empty name never exists
    }

    // Check if program exists in container
    return m_programs.find(programName) != m_programs.end();                    // Return true if found
}

//==============================================================================
// GetLoadedShaderNames - Get list of all loaded shader names
//==============================================================================
std::vector<std::string> ShaderManager::GetLoadedShaderNames() const {
    #if defined(_DEBUG_SHADERMANAGER_)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[ShaderManager] GetLoadedShaderNames() called - returning %d shader names.", (int)m_shaders.size());
    #endif

    std::vector<std::string> shaderNames;                                       // Container for shader names
    shaderNames.reserve(m_shaders.size());                                      // Reserve space for efficiency

    // Extract all shader names from container
    for (const auto& shaderPair : m_shaders) {
        shaderNames.push_back(shaderPair.first);                                // Add shader name to list
    }

    return shaderNames;                                                         // Return list of shader names
}

//==============================================================================
// GetLoadedProgramNames - Get list of all linked program names
//==============================================================================
std::vector<std::string> ShaderManager::GetLoadedProgramNames() const {
    #if defined(_DEBUG_SHADERMANAGER_)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[ShaderManager] GetLoadedProgramNames() called - returning %d program names.", (int)m_programs.size());
    #endif

    std::vector<std::string> programNames;                                      // Container for program names
    programNames.reserve(m_programs.size());                                    // Reserve space for efficiency

    // Extract all program names from container
    for (const auto& programPair : m_programs) {
        programNames.push_back(programPair.first);                              // Add program name to list
    }

    return programNames;                                                        // Return list of program names
}

//==============================================================================
// EnableHotReloading - Enable/disable automatic shader reloading
//==============================================================================
void ShaderManager::EnableHotReloading(bool enable) {
    #if defined(_DEBUG_SHADERMANAGER_)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"[ShaderManager] EnableHotReloading() called - %s hot-reloading.",
            enable ? L"enabling" : L"disabling");
    #endif

    // Acquire thread lock for safe hot-reloading state change
    ThreadLockHelper lock(threadManager, m_lockName, 1000);                     // 1 second timeout for state change
    if (!lock.IsLocked()) {
        #if defined(_DEBUG_SHADERMANAGER_)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[ShaderManager] EnableHotReloading() failed - could not acquire thread lock.");
        #endif
        return;
    }

    m_hotReloadingEnabled = enable;                                             // Set hot-reloading state

    if (enable) {
        // Update file timestamps for all loaded shaders
        for (auto& shaderPair : m_shaders) {
            ShaderResource* shader = shaderPair.second.get();
            if (shader && shader->filePath != L"<inline>") {
                UpdateShaderFileTimestamp(*shader);                             // Update cached file timestamp
            }
        }
    }

    #if defined(_DEBUG_SHADERMANAGER_)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"[ShaderManager] Hot-reloading %s.", enable ? L"enabled" : L"disabled");
    #endif
}

//==============================================================================
// CheckForShaderFileChanges - Manually check for modified shader files
//==============================================================================
void ShaderManager::CheckForShaderFileChanges() {
    #if defined(_DEBUG_SHADERMANAGER_)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[ShaderManager] CheckForShaderFileChanges() called.");
    #endif

    // Only check if hot-reloading is enabled
    if (!m_hotReloadingEnabled) {
        #if defined(_DEBUG_SHADERMANAGER_)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[ShaderManager] Hot-reloading disabled, skipping file change check.");
        #endif
        return;
    }

    // Ensure manager is initialized
    if (!m_isInitialized) {
        #if defined(_DEBUG_SHADERMANAGER_)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[ShaderManager] CheckForShaderFileChanges() failed - shader manager not initialized.");
        #endif
        return;
    }

    // Acquire thread lock for safe file checking
    ThreadLockHelper lock(threadManager, m_lockName, 2000);                     // 2 second timeout for file checking
    if (!lock.IsLocked()) {
        #if defined(_DEBUG_SHADERMANAGER_)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[ShaderManager] CheckForShaderFileChanges() failed - could not acquire thread lock.");
        #endif
        return;
    }

    int reloadedCount = 0;                                                      // Counter for reloaded shaders

    // Check each loaded shader for file modifications
    for (auto& shaderPair : m_shaders) {
        ShaderResource* shader = shaderPair.second.get();
        if (!shader || shader->filePath == L"<inline>") {
            continue;                                                           // Skip inline shaders (no file to check)
        }

        // Check if file exists
        if (!std::filesystem::exists(shader->filePath)) {
            #if defined(_DEBUG_SHADERMANAGER_)
                debug.logDebugMessage(LogLevel::LOG_WARNING, L"[ShaderManager] Shader file no longer exists: %ls", shader->filePath.c_str());
            #endif
            continue;
        }

        // Get current file modification time
        auto currentModTime = GetFileModificationTime(shader->filePath);

        // Check if file has been modified
        if (currentModTime > shader->lastModified) {
            #if defined(_DEBUG_SHADERMANAGER_)
                debug.logDebugMessage(LogLevel::LOG_INFO, L"[ShaderManager] Detected file change for shader '%hs', reloading.", shaderPair.first.c_str());
            #endif

            // Attempt to reload the shader
            if (ReloadShader(shaderPair.first)) {
                reloadedCount++;                                                // Increment successful reload counter
            }
        }
    }

    #if defined(_DEBUG_SHADERMANAGER_)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"[ShaderManager] CheckForShaderFileChanges() completed - %d shaders reloaded.", reloadedCount);
    #endif
}

//==============================================================================
// CompileHLSL - Compile HLSL shader for DirectX
//==============================================================================
bool ShaderManager::CompileHLSL(ShaderResource& shader) {
    #if defined(_DEBUG_SHADERMANAGER_)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"[ShaderManager] CompileHLSL() called for shader '%hs'.", shader.name.c_str());
    #endif

#if defined(__USE_DIRECTX_11__) || defined(__USE_DIRECTX_12__)
    // Read shader source code from file
    std::string shaderCode;
    if (!ReadShaderFile(shader.filePath, shaderCode)) {
        shader.compilationErrors = "Failed to read shader file: " + std::string(shader.filePath.begin(), shader.filePath.end());
        HandleCompilationError(shader, shader.compilationErrors);
        return false;
    }

    // Determine target profile based on shader type
    std::string targetProfile;
    switch (shader.type) {
        case ShaderType::VERTEX_SHADER:
            targetProfile = shader.profile.profileVersion.empty() ? "vs_5_0" : shader.profile.profileVersion;
            break;
        case ShaderType::PIXEL_SHADER:
            targetProfile = shader.profile.profileVersion.empty() ? "ps_5_0" : shader.profile.profileVersion;
            break;
        case ShaderType::GEOMETRY_SHADER:
            targetProfile = shader.profile.profileVersion.empty() ? "gs_5_0" : shader.profile.profileVersion;
            break;
        case ShaderType::HULL_SHADER:
            targetProfile = shader.profile.profileVersion.empty() ? "hs_5_0" : shader.profile.profileVersion;
            break;
        case ShaderType::DOMAIN_SHADER:
            targetProfile = shader.profile.profileVersion.empty() ? "ds_5_0" : shader.profile.profileVersion;
            break;
        case ShaderType::COMPUTE_SHADER:
            targetProfile = shader.profile.profileVersion.empty() ? "cs_5_0" : shader.profile.profileVersion;
            break;
        default:
            shader.compilationErrors = "Unsupported shader type for HLSL compilation";
            HandleCompilationError(shader, shader.compilationErrors);
            return false;
    }

    // Setup compilation flags
    DWORD compileFlags = D3DCOMPILE_ENABLE_STRICTNESS;                          // Enable strict compilation

    if (shader.profile.debugInfo) {
        compileFlags |= D3DCOMPILE_DEBUG;                                       // Include debug information
        compileFlags |= D3DCOMPILE_SKIP_OPTIMIZATION;                          // Skip optimization for debugging
    }

    if (shader.profile.optimized && !shader.profile.debugInfo) {
        compileFlags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;                        // Maximum optimization level
    }

    // Generate preprocessor defines string
    std::string definesString = GenerateShaderDefines(shader.profile.defines);

    // Convert defines to D3D_SHADER_MACRO array
    std::vector<D3D_SHADER_MACRO> macros;
    for (const std::string& define : shader.profile.defines) {
        D3D_SHADER_MACRO macro;
        size_t equalPos = define.find('=');
        if (equalPos != std::string::npos) {
            std::string name = define.substr(0, equalPos);
            std::string value = define.substr(equalPos + 1);
            macro.Name = name.c_str();
            macro.Definition = value.c_str();
        }
        else {
            macro.Name = define.c_str();
            macro.Definition = "1";
        }
        macros.push_back(macro);
    }

    // Add null terminator for macro array
    D3D_SHADER_MACRO nullMacro = { nullptr, nullptr };
    macros.push_back(nullMacro);

    // Compile shader
    Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
    HRESULT hr = D3DCompile(
        shaderCode.c_str(),                                                     // Shader source code
        shaderCode.length(),                                                    // Source code length
        shader.filePath != L"<inline>" ? std::string(shader.filePath.begin(), shader.filePath.end()).c_str() : nullptr, // Source name
        macros.empty() ? nullptr : macros.data(),                              // Preprocessor macros
        D3D_COMPILE_STANDARD_FILE_INCLUDE,                                     // Include interface
        shader.profile.entryPoint.c_str(),                                     // Entry point function
        targetProfile.c_str(),                                                  // Target profile
        compileFlags,                                                           // Compile flags
        0,                                                                      // Effect flags (not used)
        &shader.shaderBlob,                                                     // Compiled shader output
        &errorBlob                                                              // Compilation errors output
    );

    // Handle compilation errors
    if (FAILED(hr)) {
        if (errorBlob) {
            shader.compilationErrors = std::string(static_cast<const char*>(errorBlob->GetBufferPointer()), errorBlob->GetBufferSize());
        }
        else {
            shader.compilationErrors = "Unknown HLSL compilation error (HRESULT: " + std::to_string(hr) + ")";
        }
        HandleCompilationError(shader, shader.compilationErrors);
        return false;
    }

    // Create platform-specific shader object
    void* device = m_renderer->GetDevice();
    if (!device) {
        shader.compilationErrors = "No DirectX device available for shader creation";
        HandleCompilationError(shader, shader.compilationErrors);
        return false;
    }

    ID3D11Device* d3dDevice = static_cast<ID3D11Device*>(device);

    // Create appropriate shader type
    bool shaderCreationSuccess = false;
    switch (shader.type) {
        case ShaderType::VERTEX_SHADER:
            shaderCreationSuccess = CompileD3D11VertexShader(shader);
            break;
        case ShaderType::PIXEL_SHADER:
            shaderCreationSuccess = CompileD3D11PixelShader(shader);
            break;
        case ShaderType::GEOMETRY_SHADER:
            shaderCreationSuccess = CompileD3D11GeometryShader(shader);
            break;
        case ShaderType::HULL_SHADER:
            shaderCreationSuccess = CompileD3D11HullShader(shader);
            break;
        case ShaderType::DOMAIN_SHADER:
            shaderCreationSuccess = CompileD3D11DomainShader(shader);
            break;
        case ShaderType::COMPUTE_SHADER:
            shaderCreationSuccess = CompileD3D11ComputeShader(shader);
            break;
        default:
            shader.compilationErrors = "Unsupported shader type for DirectX shader creation";
            HandleCompilationError(shader, shader.compilationErrors);
            return false;
    }

    if (!shaderCreationSuccess) {
        return false;                                                           // Error already handled in specific compilation method
    }

    // Mark shader as compiled and loaded
    shader.isCompiled = true;
    shader.isLoaded = true;

    #if defined(_DEBUG_SHADERMANAGER_)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"[ShaderManager] HLSL shader '%hs' compiled successfully.", shader.name.c_str());
    #endif

    return true;

#else
    // DirectX not available
    shader.compilationErrors = "DirectX not available for HLSL compilation";
    HandleCompilationError(shader, shader.compilationErrors);
    return false;
#endif
}

//==============================================================================
// CompileGLSL - Compile GLSL shader for OpenGL
//==============================================================================
bool ShaderManager::CompileGLSL(ShaderResource& shader) {
    #if defined(_DEBUG_SHADERMANAGER_)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"[ShaderManager] CompileGLSL() called for shader '%hs'.", shader.name.c_str());
    #endif

#if defined(__USE_OPENGL__)
    // Read shader source code from file
    std::string shaderCode;
    if (!ReadShaderFile(shader.filePath, shaderCode)) {
        shader.compilationErrors = "Failed to read shader file: " + std::string(shader.filePath.begin(), shader.filePath.end());
        HandleCompilationError(shader, shader.compilationErrors);
        return false;
    }

    // Prepend version directive and defines
    std::string versionString = shader.profile.profileVersion.empty() ? "#version 330 core\n" : "#version " + shader.profile.profileVersion + "\n";
    std::string definesString = GenerateShaderDefines(shader.profile.defines);
    std::string finalShaderCode = versionString + definesString + shaderCode;

    // Get OpenGL shader type
    GLenum glShaderType = GetOpenGLShaderType(shader.type);
    if (glShaderType == 0) {
        shader.compilationErrors = "Unsupported shader type for OpenGL compilation";
        HandleCompilationError(shader, shader.compilationErrors);
        return false;
    }

    // Create OpenGL shader object
    shader.openglShaderID = glCreateShader(glShaderType);
    if (shader.openglShaderID == 0) {
        shader.compilationErrors = "Failed to create OpenGL shader object";
        HandleCompilationError(shader, shader.compilationErrors);
        return false;
    }

    // Set shader source code
    const char* sourcePtr = finalShaderCode.c_str();
    GLint sourceLength = static_cast<GLint>(finalShaderCode.length());
    glShaderSource(shader.openglShaderID, 1, &sourcePtr, &sourceLength);

    // Compile shader
    glCompileShader(shader.openglShaderID);

    // Check compilation status
    GLint compileStatus;
    glGetShaderiv(shader.openglShaderID, GL_COMPILE_STATUS, &compileStatus);

    if (compileStatus == GL_FALSE) {
        // Get compilation error log
        GLint logLength;
        glGetShaderiv(shader.openglShaderID, GL_INFO_LOG_LENGTH, &logLength);

        if (logLength > 0) {
            std::vector<char> errorLog(logLength);
            glGetShaderInfoLog(shader.openglShaderID, logLength, nullptr, errorLog.data());
            shader.compilationErrors = std::string(errorLog.data());
        }
        else {
            shader.compilationErrors = "Unknown OpenGL shader compilation error";
        }

        // Clean up failed shader
        glDeleteShader(shader.openglShaderID);
        shader.openglShaderID = 0;

        HandleCompilationError(shader, shader.compilationErrors);
        return false;
    }

    // Mark shader as compiled and loaded
    shader.isCompiled = true;
    shader.isLoaded = true;

    #if defined(_DEBUG_SHADERMANAGER_)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"[ShaderManager] GLSL shader '%hs' compiled successfully.", shader.name.c_str());
    #endif

    return true;

#else
    // OpenGL not available
    shader.compilationErrors = "OpenGL not available for GLSL compilation";
    HandleCompilationError(shader, shader.compilationErrors);
    return false;
#endif
}

//==============================================================================
// CompileSPIRV - Compile SPIR-V shader for Vulkan
//==============================================================================
bool ShaderManager::CompileSPIRV(ShaderResource& shader) {
    #if defined(_DEBUG_SHADERMANAGER_)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"[ShaderManager] CompileSPIRV() called for shader '%hs'.", shader.name.c_str());
    #endif

#if defined(__USE_VULKAN__)
    // Read shader source code from file
    std::string shaderCode;
    if (!ReadShaderFile(shader.filePath, shaderCode)) {
        shader.compilationErrors = "Failed to read shader file: " + std::string(shader.filePath.begin(), shader.filePath.end());
        HandleCompilationError(shader, shader.compilationErrors);
        return false;
    }

    // Initialize shaderc compiler
    shaderc::Compiler compiler;
    shaderc::CompileOptions options;

    // Set compilation options
    if (shader.profile.optimized) {
        options.SetOptimizationLevel(shaderc_optimization_level_performance);   // Optimize for performance
    }
    else {
        options.SetOptimizationLevel(shaderc_optimization_level_zero);          // No optimization
    }

    if (shader.profile.debugInfo) {
        options.SetGenerateDebugInfo();                                         // Generate debug information
    }

    // Add preprocessor defines
    for (const std::string& define : shader.profile.defines) {
        size_t equalPos = define.find('=');
        if (equalPos != std::string::npos) {
            std::string name = define.substr(0, equalPos);
            std::string value = define.substr(equalPos + 1);
            options.AddMacroDefinition(name, value);
        }
        else {
            options.AddMacroDefinition(define, "1");
        }
    }

    // Determine shader kind
    shaderc_shader_kind shaderKind;
    switch (shader.type) {
        case ShaderType::VERTEX_SHADER:
            shaderKind = shaderc_glsl_vertex_shader;
            break;
        case ShaderType::PIXEL_SHADER:
            shaderKind = shaderc_glsl_fragment_shader;
            break;
        case ShaderType::GEOMETRY_SHADER:
            shaderKind = shaderc_glsl_geometry_shader;
            break;
        case ShaderType::TESSELLATION_CONTROL_SHADER:
            shaderKind = shaderc_glsl_tess_control_shader;
            break;
        case ShaderType::TESSELLATION_EVALUATION_SHADER:
            shaderKind = shaderc_glsl_tess_evaluation_shader;
            break;
        case ShaderType::COMPUTE_SHADER:
            shaderKind = shaderc_glsl_compute_shader;
            break;
        default:
            shader.compilationErrors = "Unsupported shader type for SPIR-V compilation";
            HandleCompilationError(shader, shader.compilationErrors);
            return false;
    }

    // Compile shader to SPIR-V
    std::string filename = shader.filePath != L"<inline>" ? std::string(shader.filePath.begin(), shader.filePath.end()) : shader.name;
    shaderc::SpvCompilationResult result = compiler.CompileGlslToSpv(
        shaderCode,                                                             // Source code
        shaderKind,                                                             // Shader kind
        filename.c_str(),                                                       // Source filename
        shader.profile.entryPoint.c_str(),                                     // Entry point
        options                                                                 // Compilation options
    );

    // Check compilation status
    if (result.GetCompilationStatus() != shaderc_compilation_status_success) {
        shader.compilationErrors = result.GetErrorMessage();
        HandleCompilationError(shader, shader.compilationErrors);
        return false;
    }

    // Store SPIR-V bytecode
    shader.spirvBytecode = std::vector<uint32_t>(result.cbegin(), result.cend());

    // Create Vulkan shader module
    if (!CreateVulkanShaderModule(shader)) {
        return false;                                                           // Error already handled in CreateVulkanShaderModule
    }

    // Mark shader as compiled and loaded
    shader.isCompiled = true;
    shader.isLoaded = true;

    #if defined(_DEBUG_SHADERMANAGER_)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"[ShaderManager] SPIR-V shader '%hs' compiled successfully.", shader.name.c_str());
    #endif

    return true;

#else
    // Vulkan not available
    shader.compilationErrors = "Vulkan not available for SPIR-V compilation";
    HandleCompilationError(shader, shader.compilationErrors);
    return false;
#endif
}

//==============================================================================
// BindShaderToModel - Associate shader program with model
//==============================================================================
bool ShaderManager::BindShaderToModel(const std::string& shaderProgramName, Model* model) {
#if defined(_DEBUG_SHADERMANAGER_)
    debug.logDebugMessage(LogLevel::LOG_INFO, L"[ShaderManager] BindShaderToModel() called - Program: %hs, Model: %p",
        shaderProgramName.c_str(), model);
#endif

    // Validate input parameters
    if (shaderProgramName.empty()) {
#if defined(_DEBUG_SHADERMANAGER_)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[ShaderManager] BindShaderToModel() failed - empty shader program name provided.");
#endif
        return false;
    }

    if (!model) {
#if defined(_DEBUG_SHADERMANAGER_)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[ShaderManager] BindShaderToModel() failed - null model pointer provided.");
#endif
        return false;
    }

    // Ensure manager is initialized
    if (!m_isInitialized) {
#if defined(_DEBUG_SHADERMANAGER_)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[ShaderManager] BindShaderToModel() failed - shader manager not initialized.");
#endif
        return false;
    }

    // Find shader program
    ShaderProgram* program = GetShaderProgram(shaderProgramName);
    if (!program || !program->isLinked) {
#if defined(_DEBUG_SHADERMANAGER_)
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"[ShaderManager] BindShaderToModel() failed - program '%hs' not found or not linked.",
            shaderProgramName.c_str());
#endif
        return false;
    }

    // Setup model-shader bindings
    if (!SetupModelShaderBindings(model, program)) {
#if defined(_DEBUG_SHADERMANAGER_)
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"[ShaderManager] BindShaderToModel() failed - could not setup shader bindings for model.");
#endif
        return false;
    }

#if defined(_DEBUG_SHADERMANAGER_)
    debug.logDebugMessage(LogLevel::LOG_INFO, L"[ShaderManager] BindShaderToModel() completed successfully - program '%hs' bound to model.",
        shaderProgramName.c_str());
#endif

    return true;
}

//==============================================================================
// SetupLightingShaders - Configure shaders for lighting system
//==============================================================================
bool ShaderManager::SetupLightingShaders(LightsManager* lightManager) {
#if defined(_DEBUG_SHADERMANAGER_)
    debug.logDebugMessage(LogLevel::LOG_INFO, L"[ShaderManager] SetupLightingShaders() called with LightsManager: %p", lightManager);
#endif

    // Validate input parameters
    if (!lightManager) {
#if defined(_DEBUG_SHADERMANAGER_)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[ShaderManager] SetupLightingShaders() failed - null light manager pointer provided.");
#endif
        return false;
    }

    // Ensure manager is initialized
    if (!m_isInitialized) {
#if defined(_DEBUG_SHADERMANAGER_)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[ShaderManager] SetupLightingShaders() failed - shader manager not initialized.");
#endif
        return false;
    }

    // Configure lighting uniforms for all loaded shader programs
    bool overallSuccess = true;
    for (auto& programPair : m_programs) {
        ShaderProgram* program = programPair.second.get();
        if (program && program->isLinked) {
            if (!ConfigureLightingUniforms(program, lightManager)) {
#if defined(_DEBUG_SHADERMANAGER_)
                debug.logDebugMessage(LogLevel::LOG_WARNING, L"[ShaderManager] Failed to configure lighting for program '%hs'.",
                    programPair.first.c_str());
#endif
                overallSuccess = false;                                         // Mark as partial failure but continue
            }
        }
    }

#if defined(_DEBUG_SHADERMANAGER_)
    debug.logDebugMessage(LogLevel::LOG_INFO, L"[ShaderManager] SetupLightingShaders() completed - %s",
        overallSuccess ? L"all programs configured successfully" : L"some programs failed to configure");
#endif

    return overallSuccess;
}

//==============================================================================
// LoadSceneShaders - Load shaders required by scene
//==============================================================================
bool ShaderManager::LoadSceneShaders(SceneManager* sceneManager) {
#if defined(_DEBUG_SHADERMANAGER_)
    debug.logDebugMessage(LogLevel::LOG_INFO, L"[ShaderManager] LoadSceneShaders() called with SceneManager: %p", sceneManager);
#endif

    // Validate input parameters
    if (!sceneManager) {
#if defined(_DEBUG_SHADERMANAGER_)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[ShaderManager] LoadSceneShaders() failed - null scene manager pointer provided.");
#endif
        return false;
    }

    // Ensure manager is initialized
    if (!m_isInitialized) {
#if defined(_DEBUG_SHADERMANAGER_)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[ShaderManager] LoadSceneShaders() failed - shader manager not initialized.");
#endif
        return false;
    }

    // Load scene-specific shaders based on detected exporter and platform
    bool loadSuccess = true;
    std::wstring detectedExporter = sceneManager->GetLastDetectedExporter();

    // Load shaders based on scene requirements
    if (detectedExporter == L"Sketchfab") {
        // Load Sketchfab-optimized shaders with PBR support
        if (!LoadShader("SketchfabVertex", L"./Assets/Shaders/SketchfabVertex.hlsl", ShaderType::VERTEX_SHADER)) {
#if defined(_DEBUG_SHADERMANAGER_)
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"[ShaderManager] Failed to load Sketchfab vertex shader, using default.");
#endif
        }

        if (!LoadShader("SketchfabPixel", L"./Assets/Shaders/SketchfabPixel.hlsl", ShaderType::PIXEL_SHADER)) {
#if defined(_DEBUG_SHADERMANAGER_)
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"[ShaderManager] Failed to load Sketchfab pixel shader, using default.");
#endif
        }
    }
    else if (detectedExporter == L"Blender") {
        // Load Blender-optimized shaders
        if (!LoadShader("BlenderVertex", L"./Assets/Shaders/BlenderVertex.hlsl", ShaderType::VERTEX_SHADER)) {
#if defined(_DEBUG_SHADERMANAGER_)
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"[ShaderManager] Failed to load Blender vertex shader, using default.");
#endif
        }

        if (!LoadShader("BlenderPixel", L"./Assets/Shaders/BlenderPixel.hlsl", ShaderType::PIXEL_SHADER)) {
#if defined(_DEBUG_SHADERMANAGER_)
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"[ShaderManager] Failed to load Blender pixel shader, using default.");
#endif
        }
    }

    // Load universal scene shaders
    if (!LoadShader("SceneVertex", L"./Assets/Shaders/SceneVertex.hlsl", ShaderType::VERTEX_SHADER)) {
#if defined(_DEBUG_SHADERMANAGER_)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[ShaderManager] Failed to load scene vertex shader.");
#endif
        loadSuccess = false;
    }

    if (!LoadShader("ScenePixel", L"./Assets/Shaders/ScenePixel.hlsl", ShaderType::PIXEL_SHADER)) {
#if defined(_DEBUG_SHADERMANAGER_)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[ShaderManager] Failed to load scene pixel shader.");
#endif
        loadSuccess = false;
    }

    // Create scene shader program
    if (loadSuccess) {
        if (!CreateShaderProgram("SceneProgram", "SceneVertex", "ScenePixel")) {
#if defined(_DEBUG_SHADERMANAGER_)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[ShaderManager] Failed to create scene shader program.");
#endif
            loadSuccess = false;
        }
    }

#if defined(_DEBUG_SHADERMANAGER_)
    debug.logDebugMessage(LogLevel::LOG_INFO, L"[ShaderManager] LoadSceneShaders() completed - %s",
        loadSuccess ? L"success" : L"with errors");
#endif

    return loadSuccess;
}

//==============================================================================
// GetStatistics - Get performance and usage statistics
//==============================================================================
ShaderManagerStats ShaderManager::GetStatistics() const {
#if defined(_DEBUG_SHADERMANAGER_)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[ShaderManager] GetStatistics() called.");
#endif

    // Update statistics before returning
    const_cast<ShaderManager*>(this)->UpdateStatistics();                      // Cast away const to update statistics

    return m_stats;                                                             // Return current statistics
}

//==============================================================================
// PrintDebugInfo - Output debug information to console
//==============================================================================
void ShaderManager::PrintDebugInfo() const {
#if defined(_DEBUG_SHADERMANAGER_)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"[ShaderManager] === SHADER MANAGER DEBUG INFO ===");
    debug.logDebugMessage(LogLevel::LOG_INFO, L"[ShaderManager] Initialized: %s", m_isInitialized ? L"Yes" : L"No");
    debug.logDebugMessage(LogLevel::LOG_INFO, L"[ShaderManager] Platform: %hs", ShaderPlatformToString(m_currentPlatform).c_str());
    debug.logDebugMessage(LogLevel::LOG_INFO, L"[ShaderManager] Hot-reloading: %s", m_hotReloadingEnabled ? L"Enabled" : L"Disabled");
    debug.logDebugMessage(LogLevel::LOG_INFO, L"[ShaderManager] Current Program: %hs", m_currentProgramName.empty() ? "None" : m_currentProgramName.c_str());

    debug.logDebugMessage(LogLevel::LOG_INFO, L"[ShaderManager] Statistics:");
    debug.logDebugMessage(LogLevel::LOG_INFO, L"  - Total Shaders Loaded: %d", m_stats.totalShadersLoaded);
    debug.logDebugMessage(LogLevel::LOG_INFO, L"  - Total Programs Linked: %d", m_stats.totalProgramsLinked);
    debug.logDebugMessage(LogLevel::LOG_INFO, L"  - Compilation Failures: %d", m_stats.compilationFailures);
    debug.logDebugMessage(LogLevel::LOG_INFO, L"  - Linking Failures: %d", m_stats.linkingFailures);
    debug.logDebugMessage(LogLevel::LOG_INFO, L"  - Memory Usage (est.): %llu bytes", m_stats.memoryUsage);

    debug.logLevelMessage(LogLevel::LOG_INFO, L"[ShaderManager] Loaded Shaders:");
    for (const auto& shaderPair : m_shaders) {
        const ShaderResource* shader = shaderPair.second.get();
        if (shader) {
            debug.logDebugMessage(LogLevel::LOG_INFO, L"  - %hs: Type=%hs, Compiled=%s, InUse=%s, RefCount=%d",
                shaderPair.first.c_str(),
                ShaderTypeToString(shader->type).c_str(),
                shader->isCompiled ? L"Yes" : L"No",
                shader->isInUse ? L"Yes" : L"No",
                shader->referenceCount);
        }
    }

    debug.logLevelMessage(LogLevel::LOG_INFO, L"[ShaderManager] Linked Programs:");
    for (const auto& programPair : m_programs) {
        const ShaderProgram* program = programPair.second.get();
        if (program) {
            debug.logDebugMessage(LogLevel::LOG_INFO, L"  - %hs: VS=%hs, PS=%hs, Linked=%s",
                programPair.first.c_str(),
                program->vertexShaderName.c_str(),
                program->pixelShaderName.c_str(),
                program->isLinked ? L"Yes" : L"No");
        }
    }

    debug.logLevelMessage(LogLevel::LOG_INFO, L"[ShaderManager] === END DEBUG INFO ===");
#endif
}

//==============================================================================
// ValidateAllShaders - Verify all loaded shaders are valid
//==============================================================================
bool ShaderManager::ValidateAllShaders() {
    #if defined(_DEBUG_SHADERMANAGER_)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"[ShaderManager] ValidateAllShaders() called - validating %d shaders.", (int)m_shaders.size());
    #endif

    // Ensure manager is initialized
    if (!m_isInitialized) {
        #if defined(_DEBUG_SHADERMANAGER_)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[ShaderManager] ValidateAllShaders() failed - shader manager not initialized.");
        #endif
        return false;
    }

    bool allValid = true;                                                       // Track overall validation status
    int validatedCount = 0;                                                     // Count successfully validated shaders

    // Validate each loaded shader
    for (const auto& shaderPair : m_shaders) {
        const ShaderResource* shader = shaderPair.second.get();
        if (!shader) {
            #if defined(_DEBUG_SHADERMANAGER_)
                debug.logDebugMessage(LogLevel::LOG_ERROR, L"[ShaderManager] Shader '%hs' has null resource.", shaderPair.first.c_str());
            #endif
            allValid = false;
            continue;
        }

        // Validate shader resource integrity
        if (!ValidateShaderResource(*shader)) {
            #if defined(_DEBUG_SHADERMANAGER_)
                debug.logDebugMessage(LogLevel::LOG_ERROR, L"[ShaderManager] Shader '%hs' failed validation.", shaderPair.first.c_str());
            #endif
            allValid = false;
        }
        else {
            validatedCount++;                                                   // Increment successful validation counter
        }
    }

    // Validate each linked program
    for (const auto& programPair : m_programs) {
        const ShaderProgram* program = programPair.second.get();
        if (!program) {
            #if defined(_DEBUG_SHADERMANAGER_)
                debug.logDebugMessage(LogLevel::LOG_ERROR, L"[ShaderManager] Program '%hs' has null resource.", programPair.first.c_str());
            #endif
            allValid = false;
            continue;
        }

        // Validate shader program integrity
        if (!ValidateShaderProgram(*program)) {
            #if defined(_DEBUG_SHADERMANAGER_)
                debug.logDebugMessage(LogLevel::LOG_ERROR, L"[ShaderManager] Program '%hs' failed validation.", programPair.first.c_str());
            #endif
            allValid = false;
        }
    }

    #if defined(_DEBUG_SHADERMANAGER_)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"[ShaderManager] ValidateAllShaders() completed - %d/%d shaders valid, overall result: %s",
            validatedCount, (int)m_shaders.size(), allValid ? L"PASS" : L"FAIL");
    #endif

    return allValid;
}

//==============================================================================
// ShaderTypeToString - Convert shader type enum to string
//==============================================================================
std::string ShaderManager::ShaderTypeToString(ShaderType type) {
    switch (type) {
        case ShaderType::VERTEX_SHADER:                 return "Vertex";
        case ShaderType::PIXEL_SHADER:                  return "Pixel";
        case ShaderType::GEOMETRY_SHADER:               return "Geometry";
        case ShaderType::HULL_SHADER:                   return "Hull";
        case ShaderType::DOMAIN_SHADER:                 return "Domain";
        case ShaderType::COMPUTE_SHADER:                return "Compute";
        case ShaderType::TESSELLATION_CONTROL_SHADER:  return "TessellationControl";
        case ShaderType::TESSELLATION_EVALUATION_SHADER: return "TessellationEvaluation";
        case ShaderType::UNKNOWN_SHADER:                return "Unknown";
        default:                                         return "Invalid";
    }
}

//==============================================================================
// ShaderPlatformToString - Convert platform enum to string
//==============================================================================
std::string ShaderManager::ShaderPlatformToString(ShaderPlatform platform) {
    switch (platform) {
        case ShaderPlatform::PLATFORM_DIRECTX11:    return "DirectX11";
        case ShaderPlatform::PLATFORM_DIRECTX12:    return "DirectX12";
        case ShaderPlatform::PLATFORM_OPENGL:       return "OpenGL";
        case ShaderPlatform::PLATFORM_VULKAN:       return "Vulkan";
        case ShaderPlatform::PLATFORM_AUTO_DETECT:  return "AutoDetect";
        default:                                     return "Unknown";
    }
}

//==============================================================================
// GetShaderTypeFromName - Determine shader type from filename/name
//==============================================================================
ShaderType ShaderManager::GetShaderTypeFromName(const std::string& shaderName) {
    #if defined(_DEBUG_SHADERMANAGER_)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[ShaderManager] GetShaderTypeFromName() called for: %hs", shaderName.c_str());
    #endif

    // Convert to lowercase for case-insensitive comparison
    std::string lowerName = shaderName;
    std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);

    // Check for vertex shader patterns
    if (lowerName.find("vertex") != std::string::npos ||
        lowerName.find("vert") != std::string::npos ||
        lowerName.find("vs") != std::string::npos ||
        lowerName.find("vshader") != std::string::npos ||
        lowerName.find("v_") == 0 ||
        lowerName.find("_v") != std::string::npos) {
        #if defined(_DEBUG_SHADERMANAGER_)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[ShaderManager] Detected vertex shader type for: %hs", shaderName.c_str());
        #endif
        return ShaderType::VERTEX_SHADER;
    }

    // Check for pixel/fragment shader patterns
    if (lowerName.find("pixel") != std::string::npos ||
        lowerName.find("fragment") != std::string::npos ||
        lowerName.find("frag") != std::string::npos ||
        lowerName.find("ps") != std::string::npos ||
        lowerName.find("pshader") != std::string::npos ||
        lowerName.find("f_") == 0 ||
        lowerName.find("_f") != std::string::npos ||
        lowerName.find("_p") != std::string::npos) {
        #if defined(_DEBUG_SHADERMANAGER_)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[ShaderManager] Detected pixel/fragment shader type for: %hs", shaderName.c_str());
        #endif
        return ShaderType::PIXEL_SHADER;
    }

    // Check for geometry shader patterns
    if (lowerName.find("geometry") != std::string::npos ||
        lowerName.find("geom") != std::string::npos ||
        lowerName.find("gs") != std::string::npos ||
        lowerName.find("gshader") != std::string::npos ||
        lowerName.find("g_") == 0 ||
        lowerName.find("_g") != std::string::npos) {
        #if defined(_DEBUG_SHADERMANAGER_)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[ShaderManager] Detected geometry shader type for: %hs", shaderName.c_str());
        #endif
        return ShaderType::GEOMETRY_SHADER;
    }

    // Check for hull/tessellation control shader patterns
    if (lowerName.find("hull") != std::string::npos ||
        lowerName.find("tesscontrol") != std::string::npos ||
        lowerName.find("tess_control") != std::string::npos ||
        lowerName.find("tc") != std::string::npos ||
        lowerName.find("hs") != std::string::npos ||
        lowerName.find("hshader") != std::string::npos ||
        lowerName.find("h_") == 0 ||
        lowerName.find("_h") != std::string::npos) {
        #if defined(_DEBUG_SHADERMANAGER_)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[ShaderManager] Detected hull/tessellation control shader type for: %hs", shaderName.c_str());
        #endif
        return ShaderType::HULL_SHADER;
    }

    // Check for domain/tessellation evaluation shader patterns
    if (lowerName.find("domain") != std::string::npos ||
        lowerName.find("tesseval") != std::string::npos ||
        lowerName.find("tess_eval") != std::string::npos ||
        lowerName.find("te") != std::string::npos ||
        lowerName.find("ds") != std::string::npos ||
        lowerName.find("dshader") != std::string::npos ||
        lowerName.find("d_") == 0 ||
        lowerName.find("_d") != std::string::npos) {
        #if defined(_DEBUG_SHADERMANAGER_)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[ShaderManager] Detected domain/tessellation evaluation shader type for: %hs", shaderName.c_str());
        #endif
        return ShaderType::DOMAIN_SHADER;
    }

    // Check for compute shader patterns
    if (lowerName.find("compute") != std::string::npos ||
        lowerName.find("comp") != std::string::npos ||
        lowerName.find("cs") != std::string::npos ||
        lowerName.find("cshader") != std::string::npos ||
        lowerName.find("c_") == 0 ||
        lowerName.find("_c") != std::string::npos) {
        #if defined(_DEBUG_SHADERMANAGER_)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[ShaderManager] Detected compute shader type for: %hs", shaderName.c_str());
        #endif
        return ShaderType::COMPUTE_SHADER;
    }

    // Check for tessellation control shader patterns (OpenGL specific)
    if (lowerName.find("tesscontrol") != std::string::npos ||
        lowerName.find("tesc") != std::string::npos) {
        #if defined(_DEBUG_SHADERMANAGER_)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[ShaderManager] Detected tessellation control shader type for: %hs", shaderName.c_str());
        #endif
        return ShaderType::TESSELLATION_CONTROL_SHADER;
    }

    // Check for tessellation evaluation shader patterns (OpenGL specific)
    if (lowerName.find("tessevaluation") != std::string::npos ||
        lowerName.find("tese") != std::string::npos) {
        #if defined(_DEBUG_SHADERMANAGER_)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[ShaderManager] Detected tessellation evaluation shader type for: %hs", shaderName.c_str());
        #endif
        return ShaderType::TESSELLATION_EVALUATION_SHADER;
    }

    // Check file extension patterns as fallback
    if (lowerName.find(".vert") != std::string::npos) {
        return ShaderType::VERTEX_SHADER;
    }
    if (lowerName.find(".frag") != std::string::npos) {
        return ShaderType::PIXEL_SHADER;
    }
    if (lowerName.find(".geom") != std::string::npos) {
        return ShaderType::GEOMETRY_SHADER;
    }
    if (lowerName.find(".tesc") != std::string::npos) {
        return ShaderType::TESSELLATION_CONTROL_SHADER;
    }
    if (lowerName.find(".tese") != std::string::npos) {
        return ShaderType::TESSELLATION_EVALUATION_SHADER;
    }
    if (lowerName.find(".comp") != std::string::npos) {
        return ShaderType::COMPUTE_SHADER;
    }

    // Default fallback - assume vertex shader if no pattern matches
#if defined(_DEBUG_SHADERMANAGER_)
    debug.logDebugMessage(LogLevel::LOG_WARNING, L"[ShaderManager] Could not determine shader type from name '%hs', defaulting to VERTEX_SHADER", shaderName.c_str());
#endif

    return ShaderType::VERTEX_SHADER;
}

//==============================================================================
// DetectCurrentPlatform - Auto-detect current rendering platform
//==============================================================================
ShaderPlatform ShaderManager::DetectCurrentPlatform() {
#if defined(_DEBUG_SHADERMANAGER_)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[ShaderManager] DetectCurrentPlatform() called.");
#endif

    // Detect platform based on compile-time defines
#if defined(__USE_DIRECTX_11__)
    return ShaderPlatform::PLATFORM_DIRECTX11;
#elif defined(__USE_DIRECTX_12__)
    return ShaderPlatform::PLATFORM_DIRECTX12;
#elif defined(__USE_OPENGL__)
    return ShaderPlatform::PLATFORM_OPENGL;
#elif defined(__USE_VULKAN__)
    return ShaderPlatform::PLATFORM_VULKAN;
#else
    return ShaderPlatform::PLATFORM_AUTO_DETECT;                               // Could not determine platform
#endif
}

//==============================================================================
// DetectPlatformFromRenderer - Determine platform from active renderer
//==============================================================================
bool ShaderManager::DetectPlatformFromRenderer() {
#if defined(_DEBUG_SHADERMANAGER_)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[ShaderManager] DetectPlatformFromRenderer() called.");
#endif

    // Validate renderer is available
    if (!m_renderer) {
#if defined(_DEBUG_SHADERMANAGER_)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[ShaderManager] DetectPlatformFromRenderer() failed - no renderer available.");
#endif
        return false;
    }

    // Get renderer type from active renderer
    RendererType rendererType = m_renderer->RenderType;

    // Map renderer type to shader platform
    switch (rendererType) {
    case RendererType::RT_DirectX11:
        m_currentPlatform = ShaderPlatform::PLATFORM_DIRECTX11;
        break;
    case RendererType::RT_DirectX12:
        m_currentPlatform = ShaderPlatform::PLATFORM_DIRECTX12;
        break;
    case RendererType::RT_OpenGL:
        m_currentPlatform = ShaderPlatform::PLATFORM_OPENGL;
        break;
    case RendererType::RT_Vulkan:
        m_currentPlatform = ShaderPlatform::PLATFORM_VULKAN;
        break;
    default:
#if defined(_DEBUG_SHADERMANAGER_)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[ShaderManager] DetectPlatformFromRenderer() failed - unknown renderer type.");
#endif
        return false;
    }

#if defined(_DEBUG_SHADERMANAGER_)
    debug.logDebugMessage(LogLevel::LOG_INFO, L"[ShaderManager] Platform detected from renderer: %hs",
        ShaderPlatformToString(m_currentPlatform).c_str());
#endif

    return true;
}

//==============================================================================
// ReadShaderFile - Read shader source from file
//==============================================================================
bool ShaderManager::ReadShaderFile(const std::wstring& filePath, std::string& outContent) {
#if defined(_DEBUG_SHADERMANAGER_)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[ShaderManager] ReadShaderFile() called - File: %ls", filePath.c_str());
#endif

    // Validate file path
    if (filePath.empty() || filePath == L"<inline>") {
#if defined(_DEBUG_SHADERMANAGER_)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[ShaderManager] ReadShaderFile() failed - invalid file path.");
#endif
        return false;
    }

    // Check if file exists
    if (!std::filesystem::exists(filePath)) {
#if defined(_DEBUG_SHADERMANAGER_)
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"[ShaderManager] ReadShaderFile() failed - file does not exist: %ls", filePath.c_str());
#endif
        return false;
    }

    // Open file for reading
    std::ifstream file(filePath, std::ios::in | std::ios::binary);
    if (!file.is_open()) {
#if defined(_DEBUG_SHADERMANAGER_)
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"[ShaderManager] ReadShaderFile() failed - could not open file: %ls", filePath.c_str());
#endif
        return false;
    }

    // Read file contents
    try {
        // Get file size
        file.seekg(0, std::ios::end);
        size_t fileSize = static_cast<size_t>(file.tellg());
        file.seekg(0, std::ios::beg);

        // Read entire file into string
        outContent.resize(fileSize);
        file.read(&outContent[0], fileSize);
        file.close();

#if defined(_DEBUG_SHADERMANAGER_)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[ShaderManager] ReadShaderFile() success - read %llu bytes from file.", fileSize);
#endif

        return true;
    }
    catch (const std::exception& e) {
#if defined(_DEBUG_SHADERMANAGER_)
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"[ShaderManager] ReadShaderFile() exception: %hs", e.what());
#endif
        file.close();
        return false;
    }
}

//==============================================================================
// ParseShaderProfile - Extract compilation settings from source
//==============================================================================
bool ShaderManager::ParseShaderProfile(const std::string& shaderCode, ShaderProfile& profile) {
#if defined(_DEBUG_SHADERMANAGER_)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[ShaderManager] ParseShaderProfile() called - parsing %d bytes of shader code.", (int)shaderCode.length());
#endif

    // Look for profile pragmas in shader code
    std::istringstream stream(shaderCode);
    std::string line;
    bool foundProfile = false;

    while (std::getline(stream, line)) {
        // Trim whitespace
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);

        // Look for pragma directives
        if (line.find("#pragma") == 0) {
            // Parse entry point pragma
            if (line.find("#pragma entry_point") != std::string::npos) {
                size_t start = line.find_first_of('"');
                size_t end = line.find_last_of('"');
                if (start != std::string::npos && end != std::string::npos && start < end) {
                    profile.entryPoint = line.substr(start + 1, end - start - 1);
                    foundProfile = true;
#if defined(_DEBUG_SHADERMANAGER_)
                    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[ShaderManager] Found entry point pragma: %hs", profile.entryPoint.c_str());
#endif
                }
            }
            // Parse profile version pragma
            else if (line.find("#pragma profile") != std::string::npos) {
                size_t start = line.find_first_of('"');
                size_t end = line.find_last_of('"');
                if (start != std::string::npos && end != std::string::npos && start < end) {
                    profile.profileVersion = line.substr(start + 1, end - start - 1);
                    foundProfile = true;
#if defined(_DEBUG_SHADERMANAGER_)
                    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[ShaderManager] Found profile pragma: %hs", profile.profileVersion.c_str());
#endif
                }
            }
            // Parse optimization pragma
            else if (line.find("#pragma optimize") != std::string::npos) {
                if (line.find("off") != std::string::npos || line.find("false") != std::string::npos) {
                    profile.optimized = false;
                }
                else if (line.find("on") != std::string::npos || line.find("true") != std::string::npos) {
                    profile.optimized = true;
                }
                foundProfile = true;
#if defined(_DEBUG_SHADERMANAGER_)
                debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[ShaderManager] Found optimization pragma: %s", profile.optimized ? L"enabled" : L"disabled");
#endif
            }
            // Parse debug pragma
            else if (line.find("#pragma debug") != std::string::npos) {
                if (line.find("on") != std::string::npos || line.find("true") != std::string::npos) {
                    profile.debugInfo = true;
                }
                else if (line.find("off") != std::string::npos || line.find("false") != std::string::npos) {
                    profile.debugInfo = false;
                }
                foundProfile = true;
#if defined(_DEBUG_SHADERMANAGER_)
                debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[ShaderManager] Found debug pragma: %s", profile.debugInfo ? L"enabled" : L"disabled");
#endif
            }
        }
        // Look for define directives
        else if (line.find("#define") == 0) {
            size_t defineStart = line.find("#define") + 7;
            std::string defineContent = line.substr(defineStart);

            // Trim whitespace
            defineContent.erase(0, defineContent.find_first_not_of(" \t"));

            if (!defineContent.empty()) {
                profile.defines.push_back(defineContent);
                foundProfile = true;
#if defined(_DEBUG_SHADERMANAGER_)
                debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[ShaderManager] Found define: %hs", defineContent.c_str());
#endif
            }
        }
    }

#if defined(_DEBUG_SHADERMANAGER_)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[ShaderManager] ParseShaderProfile() completed - %s", foundProfile ? L"profile data found" : L"no profile data found");
#endif

    return foundProfile;
}

//==============================================================================
// GenerateShaderDefines - Convert defines to preprocessor string
//==============================================================================
std::string ShaderManager::GenerateShaderDefines(const std::vector<std::string>& defines) {
#if defined(_DEBUG_SHADERMANAGER_)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[ShaderManager] GenerateShaderDefines() called - processing %d defines.", (int)defines.size());
#endif

    std::string definesString;                                                  // String to accumulate all defines

    // Process each define
    for (const std::string& define : defines) {
        if (!define.empty()) {
            definesString += "#define " + define + "\n";                        // Add define directive
        }
    }

#if defined(_DEBUG_SHADERMANAGER_)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[ShaderManager] GenerateShaderDefines() completed - generated %d bytes of defines.", (int)definesString.length());
#endif

    return definesString;
}

//==============================================================================
// DirectX-specific compilation methods
//==============================================================================
#if defined(__USE_DIRECTX_11__) || defined(__USE_DIRECTX_12__)

//==============================================================================
// CompileD3D11PixelShader - Compile DirectX 11 pixel shader
//==============================================================================
bool ShaderManager::CompileD3D11PixelShader(ShaderResource& shader) {
    #if defined(_DEBUG_SHADERMANAGER_)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[ShaderManager] CompileD3D11PixelShader() called for shader '%hs'.", shader.name.c_str());
    #endif

    // Validate shader blob exists
    if (!shader.shaderBlob) {
        shader.compilationErrors = "No compiled shader blob available for pixel shader creation";
        HandleCompilationError(shader, shader.compilationErrors);
        return false;
    }

    // Get DirectX device
    void* device = m_renderer->GetDevice();
    if (!device) {
        shader.compilationErrors = "No DirectX device available for pixel shader creation";
        HandleCompilationError(shader, shader.compilationErrors);
        return false;
    }

    ID3D11Device* d3dDevice = static_cast<ID3D11Device*>(device);

    // Create pixel shader
    HRESULT hr = d3dDevice->CreatePixelShader(
        shader.shaderBlob->GetBufferPointer(),                                  // Compiled shader bytecode
        shader.shaderBlob->GetBufferSize(),                                     // Bytecode size
        nullptr,                                                                // Class linkage (not used)
        &shader.d3d11PixelShader                                                // Output pixel shader
    );

    if (FAILED(hr)) {
        shader.compilationErrors = "Failed to create DirectX 11 pixel shader (HRESULT: " + std::to_string(hr) + ")";
        HandleCompilationError(shader, shader.compilationErrors);
        return false;
    }

#if defined(_DEBUG_SHADERMANAGER_)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[ShaderManager] CompileD3D11PixelShader() completed successfully for shader '%hs'.", shader.name.c_str());
#endif

    return true;
}

//==============================================================================
// CompileD3D11GeometryShader - Compile DirectX 11 geometry shader
//==============================================================================
bool ShaderManager::CompileD3D11GeometryShader(ShaderResource& shader) {
#if defined(_DEBUG_SHADERMANAGER_)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[ShaderManager] CompileD3D11GeometryShader() called for shader '%hs'.", shader.name.c_str());
#endif

    // Validate shader blob exists
    if (!shader.shaderBlob) {
        shader.compilationErrors = "No compiled shader blob available for geometry shader creation";
        HandleCompilationError(shader, shader.compilationErrors);
        return false;
    }

    // Get DirectX device
    void* device = m_renderer->GetDevice();
    if (!device) {
        shader.compilationErrors = "No DirectX device available for geometry shader creation";
        HandleCompilationError(shader, shader.compilationErrors);
        return false;
    }

    ID3D11Device* d3dDevice = static_cast<ID3D11Device*>(device);

    // Create geometry shader
    HRESULT hr = d3dDevice->CreateGeometryShader(
        shader.shaderBlob->GetBufferPointer(),                                  // Compiled shader bytecode
        shader.shaderBlob->GetBufferSize(),                                     // Bytecode size
        nullptr,                                                                // Class linkage (not used)
        &shader.d3d11GeometryShader                                             // Output geometry shader
    );

    if (FAILED(hr)) {
        shader.compilationErrors = "Failed to create DirectX 11 geometry shader (HRESULT: " + std::to_string(hr) + ")";
        HandleCompilationError(shader, shader.compilationErrors);
        return false;
    }

#if defined(_DEBUG_SHADERMANAGER_)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[ShaderManager] CompileD3D11GeometryShader() completed successfully for shader '%hs'.", shader.name.c_str());
#endif

    return true;
}

//==============================================================================
// CompileD3D11HullShader - Compile DirectX 11 hull shader
//==============================================================================
bool ShaderManager::CompileD3D11HullShader(ShaderResource& shader) {
#if defined(_DEBUG_SHADERMANAGER_)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[ShaderManager] CompileD3D11HullShader() called for shader '%hs'.", shader.name.c_str());
#endif

    // Validate shader blob exists
    if (!shader.shaderBlob) {
        shader.compilationErrors = "No compiled shader blob available for hull shader creation";
        HandleCompilationError(shader, shader.compilationErrors);
        return false;
    }

    // Get DirectX device
    void* device = m_renderer->GetDevice();
    if (!device) {
        shader.compilationErrors = "No DirectX device available for hull shader creation";
        HandleCompilationError(shader, shader.compilationErrors);
        return false;
    }

    ID3D11Device* d3dDevice = static_cast<ID3D11Device*>(device);

    // Create hull shader
    HRESULT hr = d3dDevice->CreateHullShader(
        shader.shaderBlob->GetBufferPointer(),                                  // Compiled shader bytecode
        shader.shaderBlob->GetBufferSize(),                                     // Bytecode size
        nullptr,                                                                // Class linkage (not used)
        &shader.d3d11HullShader                                                  // Output hull shader
    );

    if (FAILED(hr)) {
        shader.compilationErrors = "Failed to create DirectX 11 hull shader (HRESULT: " + std::to_string(hr) + ")";
        HandleCompilationError(shader, shader.compilationErrors);
        return false;
    }

#if defined(_DEBUG_SHADERMANAGER_)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[ShaderManager] CompileD3D11HullShader() completed successfully for shader '%hs'.", shader.name.c_str());
#endif

    return true;
}

//==============================================================================
// CompileD3D11DomainShader - Compile DirectX 11 domain shader
//==============================================================================
bool ShaderManager::CompileD3D11DomainShader(ShaderResource& shader) {
#if defined(_DEBUG_SHADERMANAGER_)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[ShaderManager] CompileD3D11DomainShader() called for shader '%hs'.", shader.name.c_str());
#endif

    // Validate shader blob exists
    if (!shader.shaderBlob) {
        shader.compilationErrors = "No compiled shader blob available for domain shader creation";
        HandleCompilationError(shader, shader.compilationErrors);
        return false;
    }

    // Get DirectX device
    void* device = m_renderer->GetDevice();
    if (!device) {
        shader.compilationErrors = "No DirectX device available for domain shader creation";
        HandleCompilationError(shader, shader.compilationErrors);
        return false;
    }

    ID3D11Device* d3dDevice = static_cast<ID3D11Device*>(device);

    // Create domain shader
    HRESULT hr = d3dDevice->CreateDomainShader(
        shader.shaderBlob->GetBufferPointer(),                                  // Compiled shader bytecode
        shader.shaderBlob->GetBufferSize(),                                     // Bytecode size
        nullptr,                                                                // Class linkage (not used)
        &shader.d3d11DomainShader                                               // Output domain shader
    );

    if (FAILED(hr)) {
        shader.compilationErrors = "Failed to create DirectX 11 domain shader (HRESULT: " + std::to_string(hr) + ")";
        HandleCompilationError(shader, shader.compilationErrors);
        return false;
    }

#if defined(_DEBUG_SHADERMANAGER_)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[ShaderManager] CompileD3D11DomainShader() completed successfully for shader '%hs'.", shader.name.c_str());
#endif

    return true;
}

//==============================================================================
// CompileD3D11ComputeShader - Compile DirectX 11 compute shader
//==============================================================================
bool ShaderManager::CompileD3D11ComputeShader(ShaderResource& shader) {
#if defined(_DEBUG_SHADERMANAGER_)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[ShaderManager] CompileD3D11ComputeShader() called for shader '%hs'.", shader.name.c_str());
#endif

    // Validate shader blob exists
    if (!shader.shaderBlob) {
        shader.compilationErrors = "No compiled shader blob available for compute shader creation";
        HandleCompilationError(shader, shader.compilationErrors);
        return false;
    }

    // Get DirectX device
    void* device = m_renderer->GetDevice();
    if (!device) {
        shader.compilationErrors = "No DirectX device available for compute shader creation";
        HandleCompilationError(shader, shader.compilationErrors);
        return false;
    }

    ID3D11Device* d3dDevice = static_cast<ID3D11Device*>(device);

    // Create compute shader
    HRESULT hr = d3dDevice->CreateComputeShader(
        shader.shaderBlob->GetBufferPointer(),                                  // Compiled shader bytecode
        shader.shaderBlob->GetBufferSize(),                                     // Bytecode size
        nullptr,                                                                // Class linkage (not used)
        &shader.d3d11ComputeShader                                              // Output compute shader
    );

    if (FAILED(hr)) {
        shader.compilationErrors = "Failed to create DirectX 11 compute shader (HRESULT: " + std::to_string(hr) + ")";
        HandleCompilationError(shader, shader.compilationErrors);
        return false;
    }

#if defined(_DEBUG_SHADERMANAGER_)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[ShaderManager] CompileD3D11ComputeShader() completed successfully for shader '%hs'.", shader.name.c_str());
#endif

    return true;
}

//==============================================================================
// CreateD3D11InputLayout - Create vertex input layout
//==============================================================================
//==============================================================================
// CreateInputLayoutForShader - Create input layout for vertex shader
//==============================================================================
bool ShaderManager::CreateInputLayoutForShader(ShaderResource& shader, const std::vector<D3D11_INPUT_ELEMENT_DESC>& layout) {
#if defined(_DEBUG_SHADERMANAGER_)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[ShaderManager] CreateInputLayoutForShader() called for shader '%hs' with %d elements.",
        shader.name.c_str(), (int)layout.size());
#endif

    // Validate shader blob exists (required for input layout creation)
    if (!shader.shaderBlob) {
        shader.compilationErrors = "No compiled shader blob available for input layout creation";
        HandleCompilationError(shader, shader.compilationErrors);
        return false;
    }

    // Validate layout elements
    if (layout.empty()) {
        shader.compilationErrors = "Empty input layout provided for vertex shader";
        HandleCompilationError(shader, shader.compilationErrors);
        return false;
    }

    // Get DirectX device
    void* device = m_renderer->GetDevice();
    if (!device) {
        shader.compilationErrors = "No DirectX device available for input layout creation";
        HandleCompilationError(shader, shader.compilationErrors);
        return false;
    }

    ID3D11Device* d3dDevice = static_cast<ID3D11Device*>(device);

    // Create input layout
    HRESULT hr = d3dDevice->CreateInputLayout(
        layout.data(),                                                          // Input element descriptions
        static_cast<UINT>(layout.size()),                                       // Number of elements
        shader.shaderBlob->GetBufferPointer(),                                  // Compiled vertex shader bytecode
        shader.shaderBlob->GetBufferSize(),                                     // Bytecode size
        &shader.inputLayout                                                     // Output input layout
    );

    if (FAILED(hr)) {
        shader.compilationErrors = "Failed to create DirectX 11 input layout (HRESULT: " + std::to_string(hr) + ")";
        HandleCompilationError(shader, shader.compilationErrors);
        return false;
    }

#if defined(_DEBUG_SHADERMANAGER_)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[ShaderManager] CreateInputLayoutForShader() completed successfully for shader '%hs'.", shader.name.c_str());
#endif

    return true;
}

//==============================================================================
// CompileD3D11VertexShader - Compile DirectX 11 vertex shader
//==============================================================================
bool ShaderManager::CompileD3D11VertexShader(ShaderResource& shader) {
#if defined(_DEBUG_SHADERMANAGER_)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[ShaderManager] CompileD3D11VertexShader() called for shader '%hs'.", shader.name.c_str());
#endif

    // Validate shader blob exists
    if (!shader.shaderBlob) {
        shader.compilationErrors = "No compiled shader blob available for vertex shader creation";
        HandleCompilationError(shader, shader.compilationErrors);
        return false;
    }

    // Get DirectX device
    void* device = m_renderer->GetDevice();
    if (!device) {
        shader.compilationErrors = "No DirectX device available for vertex shader creation";
        HandleCompilationError(shader, shader.compilationErrors);
        return false;
    }

    ID3D11Device* d3dDevice = static_cast<ID3D11Device*>(device);

    // Create vertex shader
    HRESULT hr = d3dDevice->CreateVertexShader(
        shader.shaderBlob->GetBufferPointer(),                                  // Compiled shader bytecode
        shader.shaderBlob->GetBufferSize(),                                     // Bytecode size
        nullptr,                                                                // Class linkage (not used)
        &shader.d3d11VertexShader                                               // Output vertex shader
    );

    if (FAILED(hr)) {
        shader.compilationErrors = "Failed to create DirectX 11 vertex shader (HRESULT: " + std::to_string(hr) + ")";
        HandleCompilationError(shader, shader.compilationErrors);
        return false;
    }

    // Create input layout for this vertex shader based on shader name
    std::vector<D3D11_INPUT_ELEMENT_DESC> inputLayout;

    // Define the vertex input layout that matches ModelVertex.hlsl VS_INPUT
    if (shader.name == "ModelVertex" || shader.name == "ModelVShader") {
        inputLayout = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TANGENT",  0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0 }
        };
    }
    else if (shader.name == "DefaultVertex") {
        // Default simpler vertex layout
        inputLayout = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 }
        };
    }
    else {
        // Generic vertex layout fallback
        inputLayout = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TANGENT",  0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0 }
        };
    }

    // Create the input layout for this vertex shader
    if (!CreateInputLayoutForShader(shader, inputLayout)) {
        return false;                                                           // Error already handled in CreateInputLayoutForShader
    }

    #if defined(_DEBUG_SHADERMANAGER_)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[ShaderManager] CompileD3D11VertexShader() completed successfully for shader '%hs' with input layout.", shader.name.c_str());
    #endif

    return true;
}

#endif // __USE_DIRECTX_11__ || __USE_DIRECTX_12__

//==============================================================================
// OpenGL-specific compilation methods
//==============================================================================
#if defined(__USE_OPENGL__)

//==============================================================================
// CompileOpenGLShader - Compile OpenGL shader
//==============================================================================
bool ShaderManager::CompileOpenGLShader(ShaderResource& shader) {
#if defined(_DEBUG_SHADERMANAGER_)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[ShaderManager] CompileOpenGLShader() called for shader '%hs'.", shader.name.c_str());
#endif

    // This method is called from CompileGLSL and assumes OpenGL context is available
    // All compilation work is already done in CompileGLSL

#if defined(_DEBUG_SHADERMANAGER_)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[ShaderManager] CompileOpenGLShader() completed - compilation handled by CompileGLSL().");
#endif

    return true;
}

//==============================================================================
// LinkOpenGLProgram - Link OpenGL shader program
//==============================================================================
bool ShaderManager::LinkOpenGLProgram(ShaderProgram& program) {
#if defined(_DEBUG_SHADERMANAGER_)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[ShaderManager] LinkOpenGLProgram() called for program '%hs'.", program.programName.c_str());
#endif

    // Create OpenGL program object
    program.openglProgramID = glCreateProgram();
    if (program.openglProgramID == 0) {
        program.linkingErrors = "Failed to create OpenGL program object";
        HandleLinkingError(program, program.linkingErrors);
        return false;
    }

    // Attach vertex shader
    ShaderResource* vertexShader = GetShader(program.vertexShaderName);
    if (!vertexShader || vertexShader->openglShaderID == 0) {
        program.linkingErrors = "Vertex shader not available for linking: " + program.vertexShaderName;
        HandleLinkingError(program, program.linkingErrors);
        glDeleteProgram(program.openglProgramID);
        program.openglProgramID = 0;
        return false;
    }
    glAttachShader(program.openglProgramID, vertexShader->openglShaderID);

    // Attach pixel/fragment shader
    ShaderResource* pixelShader = GetShader(program.pixelShaderName);
    if (!pixelShader || pixelShader->openglShaderID == 0) {
        program.linkingErrors = "Fragment shader not available for linking: " + program.pixelShaderName;
        HandleLinkingError(program, program.linkingErrors);
        glDeleteProgram(program.openglProgramID);
        program.openglProgramID = 0;
        return false;
    }
    glAttachShader(program.openglProgramID, pixelShader->openglShaderID);

    // Attach optional geometry shader
    if (!program.geometryShaderName.empty()) {
        ShaderResource* geometryShader = GetShader(program.geometryShaderName);
        if (geometryShader && geometryShader->openglShaderID != 0) {
            glAttachShader(program.openglProgramID, geometryShader->openglShaderID);
        }
        else {
#if defined(_DEBUG_SHADERMANAGER_)
            debug.logDebugMessage(LogLevel::LOG_WARNING, L"[ShaderManager] Geometry shader '%hs' not available for linking.",
                program.geometryShaderName.c_str());
#endif
        }
    }

    // Attach optional tessellation control shader
    if (!program.hullShaderName.empty()) {
        ShaderResource* tessControlShader = GetShader(program.hullShaderName);
        if (tessControlShader && tessControlShader->openglShaderID != 0) {
            glAttachShader(program.openglProgramID, tessControlShader->openglShaderID);
        }
        else {
#if defined(_DEBUG_SHADERMANAGER_)
            debug.logDebugMessage(LogLevel::LOG_WARNING, L"[ShaderManager] Tessellation control shader '%hs' not available for linking.",
                program.hullShaderName.c_str());
#endif
        }
    }

    // Attach optional tessellation evaluation shader
    if (!program.domainShaderName.empty()) {
        ShaderResource* tessEvalShader = GetShader(program.domainShaderName);
        if (tessEvalShader && tessEvalShader->openglShaderID != 0) {
            glAttachShader(program.openglProgramID, tessEvalShader->openglShaderID);
        }
        else {
#if defined(_DEBUG_SHADERMANAGER_)
            debug.logDebugMessage(LogLevel::LOG_WARNING, L"[ShaderManager] Tessellation evaluation shader '%hs' not available for linking.",
                program.domainShaderName.c_str());
#endif
        }
    }

    // Link the program
    glLinkProgram(program.openglProgramID);

    // Check linking status
    GLint linkStatus;
    glGetProgramiv(program.openglProgramID, GL_LINK_STATUS, &linkStatus);

    if (linkStatus == GL_FALSE) {
        // Get linking error log
        GLint logLength;
        glGetProgramiv(program.openglProgramID, GL_INFO_LOG_LENGTH, &logLength);

        if (logLength > 0) {
            std::vector<char> errorLog(logLength);
            glGetProgramInfoLog(program.openglProgramID, logLength, nullptr, errorLog.data());
            program.linkingErrors = std::string(errorLog.data());
        }
        else {
            program.linkingErrors = "Unknown OpenGL program linking error";
        }

        // Clean up failed program
        glDeleteProgram(program.openglProgramID);
        program.openglProgramID = 0;

        HandleLinkingError(program, program.linkingErrors);
        return false;
    }

    // Mark program as linked
    program.isLinked = true;

#if defined(_DEBUG_SHADERMANAGER_)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[ShaderManager] LinkOpenGLProgram() completed successfully for program '%hs'.", program.programName.c_str());
#endif

    return true;
}

//==============================================================================
// GetOpenGLShaderType - Convert ShaderType to OpenGL enum
//==============================================================================
GLenum ShaderManager::GetOpenGLShaderType(ShaderType type) {
    switch (type) {
    case ShaderType::VERTEX_SHADER:                 return GL_VERTEX_SHADER;
    case ShaderType::PIXEL_SHADER:                  return GL_FRAGMENT_SHADER;
    case ShaderType::GEOMETRY_SHADER:               return GL_GEOMETRY_SHADER;
    case ShaderType::TESSELLATION_CONTROL_SHADER:  return GL_TESS_CONTROL_SHADER;
    case ShaderType::TESSELLATION_EVALUATION_SHADER: return GL_TESS_EVALUATION_SHADER;
    case ShaderType::COMPUTE_SHADER:                return GL_COMPUTE_SHADER;
    default:                                         return 0; // Invalid/unsupported type
    }
}

#endif // __USE_OPENGL__

//==============================================================================
// Vulkan-specific compilation methods
//==============================================================================
#if defined(__USE_VULKAN__)

//==============================================================================
// CompileVulkanShader - Compile Vulkan SPIR-V shader
//==============================================================================
bool ShaderManager::CompileVulkanShader(ShaderResource& shader) {
#if defined(_DEBUG_SHADERMANAGER_)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[ShaderManager] CompileVulkanShader() called for shader '%hs'.", shader.name.c_str());
#endif

    // This method is called from CompileSPIRV and assumes SPIR-V bytecode is available
    // All compilation work is already done in CompileSPIRV

#if defined(_DEBUG_SHADERMANAGER_)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[ShaderManager] CompileVulkanShader() completed - compilation handled by CompileSPIRV().");
#endif

    return true;
}

//==============================================================================
// CreateVulkanShaderModule - Create Vulkan shader module
//==============================================================================
bool ShaderManager::CreateVulkanShaderModule(ShaderResource& shader) {
#if defined(_DEBUG_SHADERMANAGER_)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[ShaderManager] CreateVulkanShaderModule() called for shader '%hs'.", shader.name.c_str());
#endif

    // Validate SPIR-V bytecode exists
    if (shader.spirvBytecode.empty()) {
        shader.compilationErrors = "No SPIR-V bytecode available for Vulkan shader module creation";
        HandleCompilationError(shader, shader.compilationErrors);
        return false;
    }

    // TODO: Implement Vulkan shader module creation when Vulkan renderer is available
    // This would require access to VkDevice and creating VkShaderModule
    // For now, mark as compiled since SPIR-V bytecode generation succeeded

#if defined(_DEBUG_SHADERMANAGER_)
    debug.logDebugMessage(LogLevel::LOG_INFO, L"[ShaderManager] Vulkan shader module creation deferred - SPIR-V bytecode ready (%d bytes).",
        (int)(shader.spirvBytecode.size() * sizeof(uint32_t)));
#endif

    return true;
}

#endif // __USE_VULKAN__

//==============================================================================
// Thread safety enforcement methods (using ThreadManager system)
//==============================================================================

//==============================================================================
// AcquireShaderLock - Acquire thread lock for shader operations
//==============================================================================
bool ShaderManager::AcquireShaderLock(int timeoutMs) {
    #if defined(_DEBUG_SHADERMANAGER_)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[ShaderManager] AcquireShaderLock() called with timeout: %d ms", timeoutMs);
    #endif

    // Validate timeout parameter
    if (timeoutMs < 0) {
        #if defined(_DEBUG_SHADERMANAGER_)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[ShaderManager] AcquireShaderLock() failed - invalid timeout value.");
        #endif
        return false;
    }

    // Use ThreadManager to acquire lock
    bool lockAcquired = threadManager.TryLock(m_lockName, timeoutMs);

    if (lockAcquired) {
        #if defined(_DEBUG_SHADERMANAGER_)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[ShaderManager] Thread lock acquired successfully.");
        #endif
    }
    else {
        #if defined(_DEBUG_SHADERMANAGER_)
            debug.logDebugMessage(LogLevel::LOG_WARNING, L"[ShaderManager] Failed to acquire thread lock within %d ms timeout.", timeoutMs);
        #endif
    }

    return lockAcquired;
}

//==============================================================================
// ReleaseShaderLock - Release thread lock
//==============================================================================
void ShaderManager::ReleaseShaderLock() {
    #if defined(_DEBUG_SHADERMANAGER_)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[ShaderManager] ReleaseShaderLock() called.");
    #endif

    // Use ThreadManager to release lock
    bool lockReleased = threadManager.RemoveLock(m_lockName);

    if (lockReleased) {
        #if defined(_DEBUG_SHADERMANAGER_)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[ShaderManager] Thread lock released successfully.");
        #endif
    }
    else {
        #if defined(_DEBUG_SHADERMANAGER_)
            debug.logDebugMessage(LogLevel::LOG_WARNING, L"[ShaderManager] Failed to release thread lock - lock may not exist or not owned by this thread.");
        #endif
    }
}

//==============================================================================
// Error handling and validation methods
//==============================================================================

//==============================================================================
// HandleCompilationError - Process compilation error
//==============================================================================
void ShaderManager::HandleCompilationError(ShaderResource& shader, const std::string& error) {
#if defined(_DEBUG_SHADERMANAGER_)
    debug.logDebugMessage(LogLevel::LOG_ERROR, L"[ShaderManager] Compilation error for shader '%hs': %hs",
        shader.name.c_str(), error.c_str());
#endif

    // Store error message in shader resource
    shader.compilationErrors = error;
    shader.isCompiled = false;                                                  // Mark as not compiled
    shader.isLoaded = false;                                                    // Mark as not loaded

    // Update statistics
    IncrementCompilationFailure();
    m_stats.lastActivity = std::chrono::system_clock::now();
}

//==============================================================================
// HandleLinkingError - Process linking error
//==============================================================================
void ShaderManager::HandleLinkingError(ShaderProgram& program, const std::string& error) {
#if defined(_DEBUG_SHADERMANAGER_)
    debug.logDebugMessage(LogLevel::LOG_ERROR, L"[ShaderManager] Linking error for program '%hs': %hs",
        program.programName.c_str(), error.c_str());
#endif

    // Store error message in program resource
    program.linkingErrors = error;
    program.isLinked = false;                                                   // Mark as not linked

    // Update statistics
    IncrementLinkingFailure();
    m_stats.lastActivity = std::chrono::system_clock::now();
}

//==============================================================================
// ValidateShaderResource - Verify shader resource integrity
//==============================================================================
bool ShaderManager::ValidateShaderResource(const ShaderResource& shader) const {
#if defined(_DEBUG_SHADERMANAGER_)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[ShaderManager] ValidateShaderResource() called for shader '%hs'.", shader.name.c_str());
#endif

    // Check basic properties
    if (shader.name.empty()) {
#if defined(_DEBUG_SHADERMANAGER_)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[ShaderManager] Validation failed - shader has empty name.");
#endif
        return false;
    }

    if (shader.type == ShaderType::UNKNOWN_SHADER) {
#if defined(_DEBUG_SHADERMANAGER_)
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"[ShaderManager] Validation failed - shader '%hs' has unknown type.", shader.name.c_str());
#endif
        return false;
    }

    // Check compilation status
    if (!shader.isCompiled) {
#if defined(_DEBUG_SHADERMANAGER_)
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"[ShaderManager] Validation failed - shader '%hs' is not compiled.", shader.name.c_str());
#endif
        return false;
    }

    // Validate platform-specific resources
    switch (m_currentPlatform) {
    case ShaderPlatform::PLATFORM_DIRECTX11:
    case ShaderPlatform::PLATFORM_DIRECTX12: {
#if defined(__USE_DIRECTX_11__) || defined(__USE_DIRECTX_12__)
        // Check for appropriate DirectX shader object
        bool hasValidShader = false;
        switch (shader.type) {
        case ShaderType::VERTEX_SHADER:
            hasValidShader = (shader.d3d11VertexShader.Get() != nullptr);
            break;
        case ShaderType::PIXEL_SHADER:
            hasValidShader = (shader.d3d11PixelShader.Get() != nullptr);
            break;
        case ShaderType::GEOMETRY_SHADER:
            hasValidShader = (shader.d3d11GeometryShader.Get() != nullptr);
            break;
        case ShaderType::HULL_SHADER:
            hasValidShader = (shader.d3d11HullShader.Get() != nullptr);
            break;
        case ShaderType::DOMAIN_SHADER:
            hasValidShader = (shader.d3d11DomainShader.Get() != nullptr);
            break;
        case ShaderType::COMPUTE_SHADER:
            hasValidShader = (shader.d3d11ComputeShader.Get() != nullptr);
            break;
        default:
            hasValidShader = false;
            break;
        }

        if (!hasValidShader) {
#if defined(_DEBUG_SHADERMANAGER_)
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"[ShaderManager] Validation failed - DirectX shader '%hs' has no valid shader object.", shader.name.c_str());
#endif
            return false;
        }

        // Check for shader blob
        if (!shader.shaderBlob) {
#if defined(_DEBUG_SHADERMANAGER_)
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"[ShaderManager] Validation failed - DirectX shader '%hs' has no shader blob.", shader.name.c_str());
#endif
            return false;
        }
#endif
        break;
    }

    case ShaderPlatform::PLATFORM_OPENGL: {
#if defined(__USE_OPENGL__)
        if (shader.openglShaderID == 0) {
#if defined(_DEBUG_SHADERMANAGER_)
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"[ShaderManager] Validation failed - OpenGL shader '%hs' has invalid shader ID.", shader.name.c_str());
#endif
            return false;
        }
#endif
        break;
    }

    case ShaderPlatform::PLATFORM_VULKAN: {
#if defined(__USE_VULKAN__)
        if (shader.spirvBytecode.empty()) {
#if defined(_DEBUG_SHADERMANAGER_)
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"[ShaderManager] Validation failed - Vulkan shader '%hs' has no SPIR-V bytecode.", shader.name.c_str());
#endif
            return false;
        }
#endif
        break;
    }

    default:
#if defined(_DEBUG_SHADERMANAGER_)
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"[ShaderManager] Validation failed - unsupported platform for shader '%hs'.", shader.name.c_str());
#endif
        return false;
    }

#if defined(_DEBUG_SHADERMANAGER_)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[ShaderManager] ValidateShaderResource() completed successfully for shader '%hs'.", shader.name.c_str());
#endif

    return true;
}

//==============================================================================
// ValidateShaderProgram - Verify shader program integrity
//==============================================================================
bool ShaderManager::ValidateShaderProgram(const ShaderProgram& program) const {
    #if defined(_DEBUG_SHADERMANAGER_)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[ShaderManager] ValidateShaderProgram() called for program '%hs'.", program.programName.c_str());
    #endif

    // Check basic properties
    if (program.programName.empty()) {
        #if defined(_DEBUG_SHADERMANAGER_)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[ShaderManager] Validation failed - program has empty name.");
        #endif
        return false;
    }

    if (program.vertexShaderName.empty() || program.pixelShaderName.empty()) {
        #if defined(_DEBUG_SHADERMANAGER_)
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"[ShaderManager] Validation failed - program '%hs' missing required vertex or pixel shader.", program.programName.c_str());
        #endif
        return false;
    }

    // Check linking status
    if (!program.isLinked) {
        #if defined(_DEBUG_SHADERMANAGER_)
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"[ShaderManager] Validation failed - program '%hs' is not linked.", program.programName.c_str());
        #endif
        return false;
    }

    // Verify referenced shaders exist
    if (!DoesShaderExist(program.vertexShaderName)) {
        #if defined(_DEBUG_SHADERMANAGER_)
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"[ShaderManager] Validation failed - program '%hs' references non-existent vertex shader '%hs'.",
                program.programName.c_str(), program.vertexShaderName.c_str());
        #endif
        return false;
    }

    if (!DoesShaderExist(program.pixelShaderName)) {
        #if defined(_DEBUG_SHADERMANAGER_)
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"[ShaderManager] Validation failed - program '%hs' references non-existent pixel shader '%hs'.",
                  program.programName.c_str(), program.pixelShaderName.c_str());
        #endif
        return false;
    }

    // Validate platform-specific program resources
    switch (m_currentPlatform) {
        case ShaderPlatform::PLATFORM_DIRECTX11:
        case ShaderPlatform::PLATFORM_DIRECTX12:
            // DirectX doesn't use linked program objects, individual shaders are validated separately
            break;

        case ShaderPlatform::PLATFORM_OPENGL: {
            #if defined(__USE_OPENGL__)
                if (program.openglProgramID == 0) {
                    #if defined(_DEBUG_SHADERMANAGER_)
                        debug.logDebugMessage(LogLevel::LOG_ERROR, L"[ShaderManager] Validation failed - OpenGL program '%hs' has invalid program ID.", program.programName.c_str());
                    #endif
                    return false;
                    }
            #endif
            break;
        }

        case ShaderPlatform::PLATFORM_VULKAN:
            // Vulkan uses pipeline objects, validation handled elsewhere
            break;

        default:
            #if defined(_DEBUG_SHADERMANAGER_)
                debug.logDebugMessage(LogLevel::LOG_ERROR, L"[ShaderManager] Validation failed - unsupported platform for program '%hs'.", program.programName.c_str());
            #endif
            return false;
    }

    #if defined(_DEBUG_SHADERMANAGER_)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[ShaderManager] ValidateShaderProgram() completed successfully for program '%hs'.", program.programName.c_str());
    #endif

    return true;
}

//==============================================================================
// Hot-reloading support methods
//==============================================================================

//==============================================================================
// GetFileModificationTime - Get file timestamp
//==============================================================================
std::chrono::system_clock::time_point ShaderManager::GetFileModificationTime(const std::wstring& filePath) {
#if defined(_DEBUG_SHADERMANAGER_)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[ShaderManager] GetFileModificationTime() called for file: %ls", filePath.c_str());
#endif

    try {
        // Get file modification time using filesystem library
        auto fileTime = std::filesystem::last_write_time(filePath);

        // Convert to system_clock time_point
        auto systemTime = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            fileTime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now()
        );

        return systemTime;
    }
    catch (const std::filesystem::filesystem_error& e) {
#if defined(_DEBUG_SHADERMANAGER_)
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"[ShaderManager] Failed to get file modification time for '%ls': %hs",
            filePath.c_str(), e.what());
#endif

        // Return epoch time if error occurs
        return std::chrono::system_clock::time_point{};
    }
}

//==============================================================================
// UpdateShaderFileTimestamp - Update cached file timestamp
//==============================================================================
void ShaderManager::UpdateShaderFileTimestamp(ShaderResource& shader) {
#if defined(_DEBUG_SHADERMANAGER_)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[ShaderManager] UpdateShaderFileTimestamp() called for shader '%hs'.", shader.name.c_str());
#endif

    // Only update timestamp for file-based shaders
    if (shader.filePath != L"<inline>") {
        shader.lastModified = GetFileModificationTime(shader.filePath);         // Update cached timestamp

#if defined(_DEBUG_SHADERMANAGER_)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[ShaderManager] Updated timestamp for shader '%hs'.", shader.name.c_str());
#endif
    }
}

//==============================================================================
// Resource cleanup helpers
//==============================================================================

//==============================================================================
// CleanupShaderResource - Release individual shader resources
//==============================================================================
void ShaderManager::CleanupShaderResource(ShaderResource& shader) {
#if defined(_DEBUG_SHADERMANAGER_)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[ShaderManager] CleanupShaderResource() called for shader '%hs'.", shader.name.c_str());
#endif

    // Clean up platform-specific resources
    switch (m_currentPlatform) {
    case ShaderPlatform::PLATFORM_DIRECTX11:
    case ShaderPlatform::PLATFORM_DIRECTX12: {
#if defined(__USE_DIRECTX_11__) || defined(__USE_DIRECTX_12__)
        // Release DirectX shader objects
        shader.d3d11VertexShader.Reset();                              // Release vertex shader
        shader.d3d11PixelShader.Reset();                               // Release pixel shader
        shader.d3d11GeometryShader.Reset();                            // Release geometry shader
        shader.d3d11HullShader.Reset();                                // Release hull shader
        shader.d3d11DomainShader.Reset();                              // Release domain shader
        shader.d3d11ComputeShader.Reset();                             // Release compute shader
        shader.shaderBlob.Reset();                                     // Release shader blob
        shader.inputLayout.Reset();                                    // Release input layout
#endif
        break;
    }

    case ShaderPlatform::PLATFORM_OPENGL: {
#if defined(__USE_OPENGL__)
        // Delete OpenGL shader object
        if (shader.openglShaderID != 0) {
            glDeleteShader(shader.openglShaderID);                     // Delete OpenGL shader
            shader.openglShaderID = 0;                                 // Clear shader ID
        }

        if (shader.openglProgramID != 0) {
            glDeleteProgram(shader.openglProgramID);                   // Delete OpenGL program (if applicable)
            shader.openglProgramID = 0;                                // Clear program ID
        }
#endif
        break;
    }

    case ShaderPlatform::PLATFORM_VULKAN: {
#if defined(__USE_VULKAN__)
        // Clean up Vulkan resources
        if (shader.vulkanShaderModule != VK_NULL_HANDLE) {
            // TODO: Destroy Vulkan shader module when Vulkan renderer is available
            // vkDestroyShaderModule(device, shader.vulkanShaderModule, nullptr);
            shader.vulkanShaderModule = VK_NULL_HANDLE;                // Clear shader module handle
        }

        shader.spirvBytecode.clear();                                  // Clear SPIR-V bytecode
        shader.spirvBytecode.shrink_to_fit();                          // Free memory
#endif
        break;
    }

    default:
        break;
    }

    // Reset shader state
    shader.isCompiled = false;                                                  // Mark as not compiled
    shader.isLoaded = false;                                                    // Mark as not loaded
    shader.isInUse = false;                                                     // Mark as not in use
    shader.referenceCount = 0;                                                  // Clear reference count
    shader.compilationErrors.clear();                                          // Clear error messages

#if defined(_DEBUG_SHADERMANAGER_)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[ShaderManager] CleanupShaderResource() completed for shader '%hs'.", shader.name.c_str());
#endif
}

//==============================================================================
// CleanupShaderProgram - Release individual program resources
//==============================================================================
void ShaderManager::CleanupShaderProgram(ShaderProgram& program) {
#if defined(_DEBUG_SHADERMANAGER_)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[ShaderManager] CleanupShaderProgram() called for program '%hs'.", program.programName.c_str());
#endif

    // Decrement reference counts for used shaders
    if (!program.vertexShaderName.empty() && DoesShaderExist(program.vertexShaderName)) {
        ShaderResource* shader = GetShader(program.vertexShaderName);
        if (shader && shader->referenceCount > 0) {
            shader->referenceCount--;                                          // Decrement vertex shader reference count
        }
    }

    if (!program.pixelShaderName.empty() && DoesShaderExist(program.pixelShaderName)) {
        ShaderResource* shader = GetShader(program.pixelShaderName);
        if (shader && shader->referenceCount > 0) {
            shader->referenceCount--;                                          // Decrement pixel shader reference count
        }
    }

    if (!program.geometryShaderName.empty() && DoesShaderExist(program.geometryShaderName)) {
        ShaderResource* shader = GetShader(program.geometryShaderName);
        if (shader && shader->referenceCount > 0) {
            shader->referenceCount--;                                          // Decrement geometry shader reference count
        }
    }

    if (!program.hullShaderName.empty() && DoesShaderExist(program.hullShaderName)) {
        ShaderResource* shader = GetShader(program.hullShaderName);
        if (shader && shader->referenceCount > 0) {
            shader->referenceCount--;                                          // Decrement hull shader reference count
        }
    }

    if (!program.domainShaderName.empty() && DoesShaderExist(program.domainShaderName)) {
        ShaderResource* shader = GetShader(program.domainShaderName);
        if (shader && shader->referenceCount > 0) {
            shader->referenceCount--;                                          // Decrement domain shader reference count
        }
    }

    // Clean up platform-specific program resources
    switch (m_currentPlatform) {
    case ShaderPlatform::PLATFORM_DIRECTX11:
    case ShaderPlatform::PLATFORM_DIRECTX12:
        // DirectX doesn't use linked program objects
        break;

    case ShaderPlatform::PLATFORM_OPENGL: {
#if defined(__USE_OPENGL__)
        // Delete OpenGL program object
        if (program.openglProgramID != 0) {
            glDeleteProgram(program.openglProgramID);                   // Delete OpenGL program
            program.openglProgramID = 0;                                // Clear program ID
        }
#endif
        break;
    }

    case ShaderPlatform::PLATFORM_VULKAN:
        // Vulkan uses pipeline objects, cleanup handled elsewhere
        break;

    default:
        break;
    }

    // Reset program state
    program.isLinked = false;                                                   // Mark as not linked
    program.linkingErrors.clear();                                             // Clear error messages

#if defined(_DEBUG_SHADERMANAGER_)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[ShaderManager] CleanupShaderProgram() completed for program '%hs'.", program.programName.c_str());
#endif
}

//==============================================================================
// Statistics updating methods
//==============================================================================

//==============================================================================
// UpdateStatistics - Recalculate performance statistics
//==============================================================================
void ShaderManager::UpdateStatistics() {
    #if defined(_DEBUG_SHADERMANAGER_)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[ShaderManager] UpdateStatistics() called.");
    #endif

    // Update shader and program counts
    m_stats.totalShadersLoaded = static_cast<int>(m_shaders.size());           // Update shader count
    m_stats.totalProgramsLinked = static_cast<int>(m_programs.size());         // Update program count

    // Estimate memory usage
    size_t estimatedMemory = 0;

    // Calculate memory usage for shaders
    for (const auto& shaderPair : m_shaders) {
        const ShaderResource* shader = shaderPair.second.get();
        if (shader) {
            // Add base shader object size
            estimatedMemory += sizeof(ShaderResource);

            // Add platform-specific memory estimates
            switch (m_currentPlatform) {
            case ShaderPlatform::PLATFORM_DIRECTX11:
            case ShaderPlatform::PLATFORM_DIRECTX12: {
#if defined(__USE_DIRECTX_11__) || defined(__USE_DIRECTX_12__)
                if (shader->shaderBlob) {
                    estimatedMemory += shader->shaderBlob->GetBufferSize(); // Add shader blob size
                }
#endif
                break;
            }

            case ShaderPlatform::PLATFORM_VULKAN: {
#if defined(__USE_VULKAN__)
                estimatedMemory += shader->spirvBytecode.size() * sizeof(uint32_t); // Add SPIR-V bytecode size
#endif
                break;
            }

            default:
                break;
            }
        }
    }

    // Calculate memory usage for programs
    for (const auto& programPair : m_programs) {
        const ShaderProgram* program = programPair.second.get();
        if (program) {
            estimatedMemory += sizeof(ShaderProgram);                          // Add base program object size
        }
    }

    m_stats.memoryUsage = estimatedMemory;                                      // Update memory usage estimate
    m_stats.lastActivity = std::chrono::system_clock::now();                   // Update activity timestamp

    #if defined(_DEBUG_SHADERMANAGER_)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[ShaderManager] UpdateStatistics() completed - memory usage: %llu bytes.", m_stats.memoryUsage);
    #endif
}

//==============================================================================
// IncrementCompilationFailure - Record compilation failure
//==============================================================================
void ShaderManager::IncrementCompilationFailure() {
    m_stats.compilationFailures++;                                             // Increment compilation failure counter
    m_stats.lastActivity = std::chrono::system_clock::now();                   // Update activity timestamp

#if defined(_DEBUG_SHADERMANAGER_)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[ShaderManager] Compilation failure recorded - total failures: %d", m_stats.compilationFailures);
#endif
}

//==============================================================================
// IncrementLinkingFailure - Record linking failure
//==============================================================================
void ShaderManager::IncrementLinkingFailure() {
    m_stats.linkingFailures++;                                                 // Increment linking failure counter
    m_stats.lastActivity = std::chrono::system_clock::now();                   // Update activity timestamp

    #if defined(_DEBUG_SHADERMANAGER_)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[ShaderManager] Linking failure recorded - total failures: %d", m_stats.linkingFailures);
    #endif
}

//==============================================================================
// SetupModelShaderBindings - Configure model-shader bindings
//==============================================================================
bool ShaderManager::SetupModelShaderBindings(Model* model, ShaderProgram* program) {
    #if defined(_DEBUG_SHADERMANAGER_)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[ShaderManager] SetupModelShaderBindings() called for model %p and program '%hs'.",
            model, program->programName.c_str());
    #endif

    // Validate input parameters
    if (!model || !program) {
        #if defined(_DEBUG_SHADERMANAGER_)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[ShaderManager] SetupModelShaderBindings() failed - null model or program pointer.");
        #endif
        return false;
    }

    // Store shader program reference in model for rendering
    // This integration assumes the Model class has methods to store shader references
    // The exact implementation depends on how the Model class is structured

    #if defined(_DEBUG_SHADERMANAGER_)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[ShaderManager] SetupModelShaderBindings() completed successfully.");
    #endif

    return true;
}

//==============================================================================
// ConfigureLightingUniforms - Setup lighting parameters
//==============================================================================
bool ShaderManager::ConfigureLightingUniforms(ShaderProgram* program, LightsManager* lightManager) {
#if defined(_DEBUG_SHADERMANAGER_)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[ShaderManager] ConfigureLightingUniforms() called for program '%hs'.", program->programName.c_str());
#endif

    // Validate input parameters
    if (!program || !lightManager) {
#if defined(_DEBUG_SHADERMANAGER_)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[ShaderManager] ConfigureLightingUniforms() failed - null program or light manager pointer.");
#endif
        return false;
    }

    // Configure lighting uniforms based on current platform
    switch (m_currentPlatform) {
    case ShaderPlatform::PLATFORM_DIRECTX11:
    case ShaderPlatform::PLATFORM_DIRECTX12: {
        // DirectX lighting configuration handled through constant buffers
        // This integration assumes proper constant buffer setup in the Model/Renderer classes
        break;
    }

    case ShaderPlatform::PLATFORM_OPENGL: {
#if defined(__USE_OPENGL__)
        // OpenGL lighting configuration through uniforms
        if (program->openglProgramID != 0) {
            // Get lighting data from manager
            std::vector<LightStruct> lights = lightManager->GetAllLights();
            int lightCount = std::min(static_cast<int>(lights.size()), MAX_LIGHTS);

            // Set light count uniform
            GLint lightCountLocation = glGetUniformLocation(program->openglProgramID, "u_lightCount");
            if (lightCountLocation != -1) {
                glUniform1i(lightCountLocation, lightCount);           // Set number of lights
            }

            // Set individual light parameters
            for (int i = 0; i < lightCount; ++i) {
                const LightStruct& light = lights[i];
                std::string lightPrefix = "u_lights[" + std::to_string(i) + "].";

                // Set light position
                GLint posLocation = glGetUniformLocation(program->openglProgramID, (lightPrefix + "position").c_str());
                if (posLocation != -1) {
                    glUniform3f(posLocation, light.position.x, light.position.y, light.position.z);
                }

                // Set light color
                GLint colorLocation = glGetUniformLocation(program->openglProgramID, (lightPrefix + "color").c_str());
                if (colorLocation != -1) {
                    glUniform3f(colorLocation, light.color.x, light.color.y, light.color.z);
                }

                // Set light intensity
                GLint intensityLocation = glGetUniformLocation(program->openglProgramID, (lightPrefix + "intensity").c_str());
                if (intensityLocation != -1) {
                    glUniform1f(intensityLocation, light.intensity);   // Set light intensity
                }

                // Set additional light parameters as needed
                // Direction, range, type, etc.
            }
        }
#endif
        break;
    }

    case ShaderPlatform::PLATFORM_VULKAN: {
        // Vulkan lighting configuration through descriptor sets
        // This would be implemented when Vulkan renderer is available
        break;
    }

    default:
#if defined(_DEBUG_SHADERMANAGER_)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[ShaderManager] ConfigureLightingUniforms() failed - unsupported platform.");
#endif
        return false;
    }

#if defined(_DEBUG_SHADERMANAGER_)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[ShaderManager] ConfigureLightingUniforms() completed successfully.");
#endif

    return true;
}

//==============================================================================
// LoadDefaultShaders - Load standard engine shaders
//==============================================================================
//==============================================================================
// LoadDefaultShaders - Load standard engine shaders
//==============================================================================
bool ShaderManager::LoadDefaultShaders() {
    #if defined(_DEBUG_SHADERMANAGER_)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"[ShaderManager] LoadDefaultShaders() called - loading standard engine shaders.");
    #endif

    bool allShadersLoaded = true;                                               // Track overall loading success

    // Load default vertex shader
    if (!LoadShader("DefaultVertex", L"./Assets/Shaders/DefaultVertex.hlsl", ShaderType::VERTEX_SHADER)) {
        #if defined(_DEBUG_SHADERMANAGER_)
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"[ShaderManager] Failed to load default vertex shader.");
        #endif
        allShadersLoaded = false;
    }

    // Load default pixel shader
    if (!LoadShader("DefaultPixel", L"./Assets/Shaders/DefaultPixel.hlsl", ShaderType::PIXEL_SHADER)) {
        #if defined(_DEBUG_SHADERMANAGER_)
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"[ShaderManager] Failed to load default pixel shader.");
        #endif
        allShadersLoaded = false;
    }

    // Load model vertex shader with proper file names
    if (!LoadShader("ModelVertex", L"./Assets/Shaders/ModelVertex.hlsl", ShaderType::VERTEX_SHADER)) {
        #if defined(_DEBUG_SHADERMANAGER_)
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"[ShaderManager] Failed to load model vertex shader.");
        #endif
        allShadersLoaded = false;
    }

    // Load model pixel shader with proper file names
    if (!LoadShader("ModelPixel", L"./Assets/Shaders/ModelPixel.hlsl", ShaderType::PIXEL_SHADER)) {
        #if defined(_DEBUG_SHADERMANAGER_)
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"[ShaderManager] Failed to load model pixel shader.");
        #endif
        allShadersLoaded = false;
    }

    // Create default shader programs
    if (allShadersLoaded) {
        if (!CreateShaderProgram("DefaultProgram", "DefaultVertex", "DefaultPixel")) {
            #if defined(_DEBUG_SHADERMANAGER_)
                debug.logLevelMessage(LogLevel::LOG_WARNING, L"[ShaderManager] Failed to create default shader program.");
            #endif
            allShadersLoaded = false;
        }

        if (!CreateShaderProgram("ModelProgram", "ModelVertex", "ModelPixel")) {
            #if defined(_DEBUG_SHADERMANAGER_)
                debug.logLevelMessage(LogLevel::LOG_WARNING, L"[ShaderManager] Failed to create model shader program.");
            #endif
            allShadersLoaded = false;
        }
    }

    #if defined(_DEBUG_SHADERMANAGER_)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"[ShaderManager] LoadDefaultShaders() completed - %s",
            allShadersLoaded ? L"all shaders loaded successfully" : L"some shaders failed to load");
    #endif

    return allShadersLoaded;
}
