#if defined(__USE_DIRECTX_11__)
#if defined(_WIN32) || defined(_WIN64)

#include "Includes.h"
#include "DX_FXManager.h"
#include "DX11Renderer.h"
#include "Debug.h"
#include "MathPrecalculation.h"
#include "ThreadManager.h"
#include "ThreadLockHelper.h"

extern Debug debug;
extern ThreadManager threadManager;

#pragma warning(push)
#pragma warning(disable: 4101)
#pragma warning(disable: 4267)                  // Suppress size_t conversion warnings

// UPDATED: FXManager constructor with bIsRendering initialization
FXManager::FXManager() : originalBlendState(nullptr), fadeBlendState(nullptr), originalRenderTarget(nullptr),
fullscreenQuadVertexBuffer(nullptr), inputLayout(nullptr), vertexShader(nullptr), pixelShader(nullptr),
bHasCleanedUp(false), bIsRendering(false) {
    // Constructor body remains the same
}

FXManager::~FXManager() {
    CleanUp();
}

// UPDATED: CleanUp function with bIsRendering reset
void FXManager::CleanUp()
{
    if (bHasCleanedUp) return;

    // Reset rendering flag to prevent any pending render operations
    bIsRendering.store(false);                                          // Ensure rendering flag is cleared

    if (fadeBlendState) { fadeBlendState->Release(); fadeBlendState = nullptr; }
    if (fullscreenQuadVertexBuffer) { fullscreenQuadVertexBuffer->Release(); fullscreenQuadVertexBuffer = nullptr; }
    if (inputLayout) { inputLayout->Release(); inputLayout = nullptr; }
    if (vertexShader) { vertexShader->Release(); vertexShader = nullptr; }
    if (pixelShader) { pixelShader->Release(); pixelShader = nullptr; }
    if (constantBuffer) { constantBuffer->Release(); constantBuffer = nullptr; }

    // Optional: release stored state
    if (originalBlendState) { originalBlendState->Release(); originalBlendState = nullptr; }
    if (originalRenderTarget) { originalRenderTarget->Release(); originalRenderTarget = nullptr; }

    // Clear out any queued FX
    effects.clear();
    pendingCallbacks.clear();

    bHasCleanedUp = true;
}

bool FXManager::IsFadeActive() const {
    for (const auto& effect : effects) {
        if (effect.type == FXType::ColorFader && effect.progress < 1.0f) {
            return true;
        }
    }
    return false;
}

void FXManager::Initialize() {
    // Early validation to prevent crashes during initialization
    if (bHasCleanedUp) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[FXManager] Cannot initialize - already cleaned up");
        return;
    }

    // Validate renderer pointer before proceeding
    if (!(&renderer) || !renderer) {
        debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"[FXManager] Cannot initialize - renderer is null");
        return;
    }

    // Additional validation for renderer state
    if (!renderer->bIsInitialized.load()) {
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"[FXManager] Renderer not fully initialized yet - deferring FXManager initialization");
        return;
    }

    try {
        // Validate DirectX 11 renderer and its components
        if (!renderer) {
            debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"[FXManager] DirectX 11 renderer cast failed");
            return;
        }

        // Retrieve our Device and Context from Renderer
        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        device = static_cast<ID3D11Device*>(renderer->GetDevice());
        context = static_cast<ID3D11DeviceContext*>(renderer->GetDeviceContext());
        // Ensure these are valid        
        if (!device || !context) {
            debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"[FXManager] DirectX device or context is null");
            return;
        }

        // Setup fullscreen quad rendering resources with error checking
        struct Vertex {
            XMFLOAT3 position;                                  // 3D position of vertex
            XMFLOAT2 texcoord;                                  // Texture coordinates
        };

        // Define fullscreen quad vertices (triangle strip)
        Vertex quadVertices[] = {
            { XMFLOAT3(-1.0f, 1.0f, 0.0f), XMFLOAT2(0.0f, 0.0f) },   // Top-left vertex
            { XMFLOAT3(1.0f, 1.0f, 0.0f), XMFLOAT2(1.0f, 0.0f) },    // Top-right vertex
            { XMFLOAT3(-1.0f, -1.0f, 0.0f), XMFLOAT2(0.0f, 1.0f) },  // Bottom-left vertex
            { XMFLOAT3(1.0f, -1.0f, 0.0f), XMFLOAT2(1.0f, 1.0f) }    // Bottom-right vertex
        };

        // Create blend state for fade effects with comprehensive error checking
        D3D11_BLEND_DESC blendDesc = {};
        blendDesc.RenderTarget[0].BlendEnable = TRUE;                   // Enable blending for fade effects
        blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;     // Source blend factor
        blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA; // Destination blend factor
        blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;         // Blend operation
        blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;      // Source alpha blend factor
        blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;    // Destination alpha blend factor
        blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;    // Alpha blend operation
        blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL; // Enable all color channels

        HRESULT hr = device->CreateBlendState(&blendDesc, &fadeBlendState);
        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"[FXManager] Failed to create fade blend state - HRESULT: 0x" +
                std::to_wstring(hr));
            return;
        }

        // Create vertex buffer for fullscreen quad with validation
        D3D11_BUFFER_DESC vertexBufferDesc = {};
        vertexBufferDesc.Usage = D3D11_USAGE_DEFAULT;                   // Default usage for GPU access
        vertexBufferDesc.ByteWidth = sizeof(quadVertices);              // Size of vertex data
        vertexBufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;          // Bind as vertex buffer

        D3D11_SUBRESOURCE_DATA vertexData = {};
        vertexData.pSysMem = quadVertices;                              // Pointer to vertex data

        hr = device->CreateBuffer(&vertexBufferDesc, &vertexData, &fullscreenQuadVertexBuffer);
        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"[FXManager] Failed to create fullscreen quad vertex buffer - HRESULT: 0x" +
                std::to_wstring(hr));
            return;
        }

        // Load shaders with error checking
        if (!LoadFadeShaders()) {
            debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"[FXManager] Failed to load fade shaders");
            return;
        }

        // Create constant buffer for shader parameters with validation
        D3D11_BUFFER_DESC cbDesc = {};
        cbDesc.Usage = D3D11_USAGE_DYNAMIC;                             // Dynamic usage for frequent updates
        cbDesc.ByteWidth = 64;                                          // Size aligned to 16-byte boundary
        cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;                  // Bind as constant buffer
        cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;                 // Allow CPU write access

        hr = device->CreateBuffer(&cbDesc, nullptr, &constantBuffer);
        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"[FXManager] Failed to create constant buffer - HRESULT: 0x" +
                std::to_wstring(hr));
            return;
        }

        debug.logLevelMessage(LogLevel::LOG_INFO, L"[FXManager] Successfully initialized with DirectX 11 renderer");
    }
    catch (const std::exception& e) {
        debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"[FXManager] Exception during initialization: " +
            std::wstring(e.what(), e.what() + strlen(e.what())));
    }
    catch (...) {
        debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"[FXManager] Unknown exception during initialization");
    }
}

void FXManager::AddEffect(const FXItem& fxItem) {
    FXItem newEffect = fxItem;
    newEffect.startTime = std::chrono::steady_clock::now();
    newEffect.lastUpdate = newEffect.startTime;
    effects.push_back(newEffect);
}

// -------------------------------------------------------------------------------------------------------------
// StopAllFXEffectsForResize - Stops all active FX effects before resize operation
// This function safely stops all running effects to prevent crashes during DirectX resource recreation
// Called from WM_SIZE message handler before resize begins
// Parameters: None
// Returns: None
// -------------------------------------------------------------------------------------------------------------
void FXManager::StopAllFXForResize()
{
#if defined(_DEBUG_FXMANAGER_)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"[FXManager] StopAllFXForResize() invoked");
#endif

    // FIXED: Use ThreadLockHelper for safe locking with timeout
    ThreadLockHelper lock(threadManager, "fxmanager_stop_all_resize_lock", 5000);
    if (!lock.IsLocked()) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[FXManager] Failed to acquire lock for StopAllFXForResize");
        return;
    }

    try {
        // FIXED: Clear and properly initialize the saved state structure
        savedFXState = ActiveFXState{};                                         // Use aggregate initialization to ensure all members are zeroed

        // FIXED: Reserve capacity for vectors to prevent reallocation during push_back operations
        savedFXState.textScrollerIDs.reserve(20);                               // Reserve space for text scroller IDs
        savedFXState.activeScrollTextures.reserve(10);                          // Reserve space for scroll texture indices

        // Check and stop starfield effect with proper validation
        if (starfieldID > 0) {
            try {
                savedFXState.starfieldActive = true;                           // Remember starfield was active
                savedFXState.starfieldID = starfieldID;                         // Save the starfield ID
                StopStarfield();                                                // Stop the starfield effect
#if defined(_DEBUG_FXMANAGER_)
                debug.logLevelMessage(LogLevel::LOG_INFO, L"[FXManager] Starfield effect stopped for resize");
#endif
            }
            catch (const std::exception& e) {
                debug.logLevelMessage(LogLevel::LOG_ERROR, L"[FXManager] Exception stopping starfield: " +
                    std::wstring(e.what(), e.what() + strlen(e.what())));
            }
        }

        // Save and stop WarpDotTunnel if active
        if (tunnelID > 0) {
            try {
                savedFXState.tunnelActive = true;
                savedFXState.tunnelID     = tunnelID;
                StopWarpDotTunnel();
#if defined(_DEBUG_FXMANAGER_)
                debug.logLevelMessage(LogLevel::LOG_INFO, L"[FXManager] WarpDotTunnel stopped for resize");
#endif
            }
            catch (const std::exception& e) {
                debug.logLevelMessage(LogLevel::LOG_ERROR, L"[FXManager] Exception stopping WarpDotTunnel: " +
                    std::wstring(e.what(), e.what() + strlen(e.what())));
            }
        }

        // FIXED: Stop text scroller effects with proper error handling and bounds checking
        // Instead of assuming IDs 1-10, iterate through actual active effects
        std::vector<int> activeTextScrollerIDs;                                 // Collect active text scroller IDs first
        activeTextScrollerIDs.reserve(10);                                      // Reserve space to prevent reallocations

        // Pass 1: Identify active text scrollers without modifying anything
        for (const auto& fx : effects) {
            if (fx.type == FXType::TextScroller) {
                activeTextScrollerIDs.push_back(fx.fxID);                       // Collect active text scroller IDs
            }
        }

        // Pass 2: Stop the identified text scrollers
        for (int textScrollerID : activeTextScrollerIDs) {
            try {
                StopTextScroller(textScrollerID);                               // Stop the specific text scroller
                savedFXState.textScrollerIDs.push_back(textScrollerID);         // SAFE: Vector has reserved capacity
                #if defined(_DEBUG_FXMANAGER_)
                    debug.logLevelMessage(LogLevel::LOG_DEBUG, L"[FXManager] Text scroller ID " + std::to_wstring(textScrollerID) + L" stopped");
                #endif
            }
            catch (const std::exception& e) {
                debug.logLevelMessage(LogLevel::LOG_ERROR, L"[FXManager] Exception stopping text scroller ID " +
                    std::to_wstring(textScrollerID) + L": " + std::wstring(e.what(), e.what() + strlen(e.what())));
            }
        }

        // Check if we had active text scrollers
        if (!savedFXState.textScrollerIDs.empty()) {
            savedFXState.textScrollerActive = true;                             // Mark that text scrollers were active
            #if defined(_DEBUG_FXMANAGER_)
                debug.logLevelMessage(LogLevel::LOG_INFO, L"[FXManager] " +
                    std::to_wstring(savedFXState.textScrollerIDs.size()) + L" text scroller effects stopped");
            #endif
        }

        // FIXED: Stop scroll effects using iterator-safe approach
        // Define scroll textures to check
        const std::vector<BlitObj2DIndexType> scrollTexturesToCheck = {
//            BlitObj2DIndexType::IMG_SCROLLBG1,
//            BlitObj2DIndexType::IMG_SCROLLBG2,
//            BlitObj2DIndexType::IMG_SCROLLBG3
        };

        // Collect active scroll effects first, then stop them
        std::vector<BlitObj2DIndexType> activeScrollTextures;
        activeScrollTextures.reserve(scrollTexturesToCheck.size());

        // Pass 1: Identify active scroll effects
        for (BlitObj2DIndexType textureIndex : scrollTexturesToCheck) {
            // Check if this texture has an active scroll effect
            bool hasActiveScrollEffect = false;
            for (const auto& fx : effects) {
                if (fx.type == FXType::Scroller && fx.textureIndex == textureIndex) {
                    hasActiveScrollEffect = true;
                    break;
                }
            }

            if (hasActiveScrollEffect) {
                activeScrollTextures.push_back(textureIndex);                   // Collect active scroll texture
            }
        }

        // Pass 2: Stop the identified scroll effects
        for (BlitObj2DIndexType textureIndex : activeScrollTextures) {
            try {
                StopScrollEffect(textureIndex);                                 // Stop scroll effect for this texture
                savedFXState.activeScrollTextures.push_back(textureIndex);      // SAFE: Vector has reserved capacity
                #if defined(_DEBUG_FXMANAGER_)
                    debug.logLevelMessage(LogLevel::LOG_DEBUG, L"[FXManager] Scroll effect stopped for texture " +
                        std::to_wstring(static_cast<int>(textureIndex)));
                #endif
            }
            catch (const std::exception& e) {
                debug.logLevelMessage(LogLevel::LOG_ERROR, L"[FXManager] Exception stopping scroll effect for texture " +
                    std::to_wstring(static_cast<int>(textureIndex)) + L": " +
                    std::wstring(e.what(), e.what() + strlen(e.what())));
            }
        }

        // Check if we had active scroll effects
        if (!savedFXState.activeScrollTextures.empty()) {
            savedFXState.scrollEffectsActive = true;                            // Mark that scroll effects were active
            #if defined(_DEBUG_FXMANAGER_)
                debug.logLevelMessage(LogLevel::LOG_INFO, L"[FXManager] " +
                    std::to_wstring(savedFXState.activeScrollTextures.size()) + L" scroll effects stopped");
            #endif
        }

        // Check for active fade effects without modifying the effects vector
        bool fadeActive = false;
        for (const auto& fx : effects) {
            if (fx.type == FXType::ColorFader && fx.progress < 1.0f) {
                fadeActive = true;
                break;
            }
        }

        if (fadeActive) {
            savedFXState.fadeEffectActive = true;                               // Mark that fade was active
            #if defined(_DEBUG_FXMANAGER_)
                debug.logLevelMessage(LogLevel::LOG_INFO, L"[FXManager] Fade effect was active during resize");
            #endif
        }

        // FIXED: Clear all effects using safe approach that doesn't invalidate iterators
        std::vector<FXItem> tempEffects;                                        // Create temporary vector
        tempEffects.swap(effects);                                              // Swap contents instead of clearing
        // tempEffects destructor will clean up the old effects safely

        #if defined(_DEBUG_FXMANAGER_)
            debug.logLevelMessage(LogLevel::LOG_INFO, L"[FXManager] All FX effects successfully stopped for resize");
        #endif
    }
    catch (const std::exception& e) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[FXManager] Exception stopping FX effects for resize: " +
            std::wstring(e.what(), e.what() + strlen(e.what())));
    }
    catch (...) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[FXManager] Unknown exception stopping FX effects for resize");
    }
}

// -------------------------------------------------------------------------------------------------------------
// RestartFXEffectsAfterResize - Restarts FX effects that were active before resize
// This function restores all effects that were running before the resize operation began
// Called from WM_SIZE message handler after resize completes
// Parameters: None
// Returns: None
// -------------------------------------------------------------------------------------------------------------
void FXManager::RestartFXAfterResize()
{
    #if defined(_DEBUG_FXMANAGER_)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"[FXManager] Restarting FX effects after resize operation");
    #endif

    try {
        // Wait a brief moment to ensure DirectX resources are fully recreated
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // Your FX Restart code goes here. (My Restarting happens when the Loader resumes 
        // its thread see IOStreamThread.h/cpp) as you may want to take a different approach, 
        // so this option is here for your effects if you chose to go this route.

        
        #if defined(_DEBUG_FXManager_)
            debug.logLevelMessage(LogLevel::LOG_INFO, L"[FXManager] All FX effects successfully restarted after resize");
        #endif

        // Clear the saved state since we're done with it
        SecureZeroMemory(&savedFXState, sizeof(ActiveFXState));
    }
    catch (const std::exception& e) {
        #if defined(_DEBUG_FXManager_)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[FXManager] Exception restarting FX effects after resize: " + std::wstring(e.what(), e.what() + strlen(e.what())));
        #endif
    }
}

bool FXManager::LoadFadeShaders()
{
    // Early validation checks
    if (bHasCleanedUp || threadManager.threadVars.bIsShuttingDown.load()) {
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"[FXManager] LoadFadeShaders called after cleanup - aborting");
        return false;
    }

    bool success = false;                                           // Track overall success status

    try {
        // Validate DirectX 11 renderer and device
        if (!renderer) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[FXManager] Invalid Renderer in LoadFadeShaders");
            success = false;
            return success;
        }

        // Retrieve our Device and Context from Renderer
        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        device = static_cast<ID3D11Device*>(renderer->GetDevice());
        context = static_cast<ID3D11DeviceContext*>(renderer->GetDeviceContext());
        if (!device || !context) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[FXManager] Invalid Device or Context in LoadFadeShaders");
            success = false;
            return success;
        }

        // Define vertex shader source code for fullscreen quad rendering
        const char* vsSource = R"(
        struct VS_INPUT {
            float3 position : POSITION;                     // Input vertex position
            float2 texcoord : TEXCOORD;                     // Input texture coordinates
        };
        struct VS_OUTPUT {
            float4 position : SV_POSITION;                  // Output clip-space position
            float2 texcoord : TEXCOORD;                     // Output texture coordinates
        };
        VS_OUTPUT main(VS_INPUT input) {
            VS_OUTPUT output;
            output.position = float4(input.position, 1.0f); // Transform to clip space
            output.texcoord = input.texcoord;               // Pass through texture coordinates
            return output;
        })";

        // Define pixel shader source code for fade color rendering
        const char* psSource = R"(
        cbuffer FadeColorBuffer : register(b0) {
            float4 fadeColor;                               // Fade color from constant buffer
        };
        float4 main(float4 position : SV_POSITION, float2 texcoord : TEXCOORD) : SV_TARGET {
            return fadeColor;                               // Output the fade color
        })";

        HRESULT hr = S_OK;
        ID3DBlob* vsBlob = nullptr;                             // Vertex shader blob
        ID3DBlob* psBlob = nullptr;                             // Pixel shader blob
        ID3DBlob* errorBlob = nullptr;                          // Error message blob

        // Compile vertex shader with error checking
        hr = D3DCompile(vsSource, strlen(vsSource), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, &vsBlob, &errorBlob);
        if (FAILED(hr)) {
            if (errorBlob) {
                // Convert error message to wide string for logging
                std::string errorStr(static_cast<const char*>(errorBlob->GetBufferPointer()), errorBlob->GetBufferSize());
                debug.logLevelMessage(LogLevel::LOG_ERROR, L"[FXManager] Vertex Shader Compilation Failed: " +
                    std::wstring(errorStr.begin(), errorStr.end()));
                errorBlob->Release();
            }
            else {
                debug.logLevelMessage(LogLevel::LOG_ERROR, L"[FXManager] Vertex Shader Compilation Failed: Unknown error");
            }
            success = false;
            return success;
        }

        // Create vertex shader object with validation
        hr = device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &vertexShader);
        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[FXManager] Failed to create vertex shader - HRESULT: 0x" +
                std::to_wstring(hr));
            vsBlob->Release();
            success = false;
            return success;
        }

        // Define input layout for vertex buffer
        D3D11_INPUT_ELEMENT_DESC layout[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,   D3D11_INPUT_PER_VERTEX_DATA, 0 },  // Position element
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 12,  D3D11_INPUT_PER_VERTEX_DATA, 0 },  // Texture coordinate element
        };

        // Create input layout with error checking
        hr = device->CreateInputLayout(layout, ARRAYSIZE(layout), vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &inputLayout);
        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[FXManager] Failed to create input layout - HRESULT: 0x" +
                std::to_wstring(hr));
            vsBlob->Release();
            success = false;
            return success;
        }

        // Release vertex shader blob after input layout creation
        vsBlob->Release();

        // Compile pixel shader with error checking
        hr = D3DCompile(psSource, strlen(psSource), nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0, &psBlob, &errorBlob);
        if (FAILED(hr)) {
            if (errorBlob) {
                // Convert error message to wide string for logging
                std::string errorStr(static_cast<const char*>(errorBlob->GetBufferPointer()), errorBlob->GetBufferSize());
                debug.logLevelMessage(LogLevel::LOG_ERROR, L"[FXManager] Pixel Shader Compilation Failed: " +
                    std::wstring(errorStr.begin(), errorStr.end()));
                errorBlob->Release();
            }
            else {
                debug.logLevelMessage(LogLevel::LOG_ERROR, L"[FXManager] Pixel Shader Compilation Failed: Unknown error");
            }
            success = false;
            return success;
        }

        // Create pixel shader object with validation
        hr = device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &pixelShader);
        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[FXManager] Failed to create pixel shader - HRESULT: 0x" +
                std::to_wstring(hr));
            psBlob->Release();
            success = false;
            return success;
        }

        // Release pixel shader blob after shader creation
        psBlob->Release();

        // Mark successful completion
        success = true;
        debug.logLevelMessage(LogLevel::LOG_INFO, L"[FXManager] Successfully compiled and loaded fade shaders");
    }
    catch (const std::exception& e) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[FXManager] Exception in LoadFadeShaders: " +
            std::wstring(e.what(), e.what() + strlen(e.what())));
        success = false;
    }
    catch (...) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[FXManager] Unknown exception in LoadFadeShaders");
        success = false;
    }

    return success;                                                 // Return success status
}

void FXManager::ApplyColorFader(FXItem& fxItem) {
    // Early validation checks to prevent crashes
    if (bHasCleanedUp || threadManager.threadVars.bIsShuttingDown.load()) {
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"[FXManager] ApplyColorFader called after cleanup - aborting");
        return;
    }

    // Validate the FX item parameters
    if (fxItem.duration <= 0.0f) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[FXManager] Invalid duration in ApplyColorFader - aborting");
        fxItem.progress = 1.0f;                                     // Mark as completed to remove it
        return;
    }

    // Validate color values to prevent DirectX crashes
    if (fxItem.targetColor.x < 0.0f || fxItem.targetColor.x > 1.0f ||
        fxItem.targetColor.y < 0.0f || fxItem.targetColor.y > 1.0f ||
        fxItem.targetColor.z < 0.0f || fxItem.targetColor.z > 1.0f ||
        fxItem.targetColor.w < 0.0f || fxItem.targetColor.w > 1.0f) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[FXManager] Invalid color values in ApplyColorFader - clamping");
        // Clamp invalid color values to valid range
        fxItem.targetColor.x = std::max(0.0f, std::min(1.0f, fxItem.targetColor.x));
        fxItem.targetColor.y = std::max(0.0f, std::min(1.0f, fxItem.targetColor.y));
        fxItem.targetColor.z = std::max(0.0f, std::min(1.0f, fxItem.targetColor.z));
        fxItem.targetColor.w = std::max(0.0f, std::min(1.0f, fxItem.targetColor.w));
    }

    auto now = std::chrono::steady_clock::now();

    // Initialize lastUpdate if not set to prevent invalid time calculations
    if (fxItem.lastUpdate.time_since_epoch().count() == 0)
        fxItem.lastUpdate = fxItem.startTime;

    // Calculate elapsed time since effect started
    float totalElapsed = std::chrono::duration<float>(now - fxItem.startTime).count();
    float elapsedSinceLastUpdate = std::chrono::duration<float>(now - fxItem.lastUpdate).count();

    // Validate time calculations to prevent infinite or negative values
    if (totalElapsed < 0.0f || elapsedSinceLastUpdate < 0.0f) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[FXManager] Invalid time calculation in ApplyColorFader - resetting");
        fxItem.startTime = now;                                     // Reset start time
        fxItem.lastUpdate = now;                                    // Reset last update time
        fxItem.progress = 0.0f;                                     // Reset progress
        return;
    }

    // FIXED: Ensure progress calculation accounts for total elapsed time, not just updates
    bool shouldUpdate = (elapsedSinceLastUpdate >= fxItem.delay) || (totalElapsed >= fxItem.duration);

    if (shouldUpdate) {
        fxItem.lastUpdate = now;

        // FIXED: Calculate progress based on total time, ensuring it reaches 1.0f
        if (totalElapsed >= fxItem.duration) {
            fxItem.progress = 1.0f;                                 // Ensure completion
        }
        else {
            fxItem.progress = totalElapsed / fxItem.duration;       // Calculate based on total elapsed time
        }

        // Clamp progress to valid range to prevent shader errors
        fxItem.progress = std::max(0.0f, std::min(1.0f, fxItem.progress));
    }

    // Calculate effective progress based on fade direction
    float effectiveProgress = fxItem.progress;
    if (fxItem.subtype == FXSubType::FadeToBackground) {
        effectiveProgress = 1.0f - fxItem.progress;
    }

    // Create final fade color with validated alpha component
    XMFLOAT4 fadeColor = fxItem.targetColor;
    fadeColor.w = std::max(0.0f, std::min(1.0f, effectiveProgress)); // Ensure valid alpha range

    // Validate renderer before attempting DirectX operations
    if (!(&renderer) || !renderer) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[FXManager] Renderer is null in ApplyColorFader - aborting");
        return;
    }

    // Safe DirectX operations with error checking
    try {
        // Retrieve our Device and Context from Renderer
        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        device = static_cast<ID3D11Device*>(renderer->GetDevice());
        context = static_cast<ID3D11DeviceContext*>(renderer->GetDeviceContext());
        // Additional validation for DirectX 11 renderer
        if (!renderer || !context || !fadeBlendState || !inputLayout) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[FXManager] Invalid DirectX resources in ApplyColorFader");
            return;
        }

        // Set blend state and input layout with error checking
        context->OMSetBlendState(fadeBlendState, nullptr, 0xffffffff);
        context->IASetInputLayout(inputLayout);

        #if defined(_DEBUG_FXMANAGER_)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[FXManager] Applying fade color: R=%.2f G=%.2f B=%.2f A=%.2f",
                fadeColor.x, fadeColor.y, fadeColor.z, fadeColor.w);
        #endif

        // Render the fullscreen quad with validated color
        RenderFullScreenQuad(fadeColor);
    }
    catch (const std::exception& e) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[FXManager] Exception in ApplyColorFader: " +
            std::wstring(e.what(), e.what() + strlen(e.what())));

        // Mark effect as completed to remove it from processing
        fxItem.progress = 1.0f;
    }
}

void FXManager::SaveRenderState() {
    // Retrieve our Device and Context from Renderer
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    device = static_cast<ID3D11Device*>(renderer->GetDevice());
    context = static_cast<ID3D11DeviceContext*>(renderer->GetDeviceContext());
    // Save blend state
    context->OMGetBlendState(&originalBlendState, nullptr, nullptr);

    // Save render target + depth-stencil view
    context->OMGetRenderTargets(1, &originalRenderTarget, &originalDepthStencilView);

    // Save viewport
    numViewports = 1;
    context->RSGetViewports(&numViewports, &originalViewport);

    // Save rasterizer state
    context->RSGetState(&originalRasterState);

    // Save depth-stencil state and ref
    context->OMGetDepthStencilState(&originalDepthStencilState, &originalStencilRef);
}

void FXManager::RestoreRenderState() {
    // Retrieve our Device and Context from Renderer
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    device = static_cast<ID3D11Device*>(renderer->GetDevice());
    context = static_cast<ID3D11DeviceContext*>(renderer->GetDeviceContext());
    // Restore blend state
    if (originalBlendState) {
        context->OMSetBlendState(originalBlendState, nullptr, 0xffffffff);
        originalBlendState->Release();
        originalBlendState = nullptr;
    }

    // Restore render targets
    if (originalRenderTarget || originalDepthStencilView) {
        context->OMSetRenderTargets(1, &originalRenderTarget, originalDepthStencilView);

        if (originalRenderTarget) {
            originalRenderTarget->Release();
            originalRenderTarget = nullptr;
        }

        if (originalDepthStencilView) {
            originalDepthStencilView->Release();
            originalDepthStencilView = nullptr;
        }
    }

    // Restore viewport
    if (numViewports > 0) {
        context->RSSetViewports(numViewports, &originalViewport);
        numViewports = 0;
    }

    // Restore rasterizer state
    if (originalRasterState) {
        context->RSSetState(originalRasterState);
        originalRasterState->Release();
        originalRasterState = nullptr;
    }

    // Restore depth-stencil state
    if (originalDepthStencilState) {
        context->OMSetDepthStencilState(originalDepthStencilState, originalStencilRef);
        originalDepthStencilState->Release();
        originalDepthStencilState = nullptr;
    }
}

// -------------------------------------------------------------------------------------------------------------
// RemoveCompletedEffects - Safely removes completed FX effects using two-pass approach
//
// This function uses a completely safe two-pass method to avoid any iterator invalidation issues
// Pass 1: Identify completed effects by index
// Pass 2: Remove effects in reverse order by index
// Parameters: None
// Returns: None
// -------------------------------------------------------------------------------------------------------------
void FXManager::RemoveCompletedEffects() {
    // Use ThreadLockHelper for safe locking to prevent crashes during vector operations
    ThreadLockHelper lock(threadManager, "fxmanager_remove_effects_lock", 1000);
    if (!lock.IsLocked()) {
        // If we can't acquire the lock, skip this frame to prevent crashes
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"[FXManager] Failed to acquire lock for RemoveCompletedEffects - skipping frame");
        return;
    }

    // Early exit if vector is empty to prevent unnecessary processing
    if (effects.empty()) {
        return;
    }

    auto now = std::chrono::steady_clock::now();                   // Get current time

    // FIXED: Use two-pass approach to completely avoid iterator invalidation
    // Pass 1: Collect indices of effects to remove (scan forward, no modification)
    std::vector<size_t> indicesToRemove;                          // Store indices of effects to remove
    indicesToRemove.reserve(effects.size());                       // Reserve space to avoid reallocations

    for (size_t i = 0; i < effects.size(); ++i) {
        const FXItem& fx = effects[i];                             // Get reference to current effect

        // Check for timeout-based completion
        bool timedOut = std::chrono::duration<float>(now - fx.startTime).count() >= fx.timeout;

        // Check for progress-based completion
        bool progressCompleted = fx.progress >= 1.0f;

        // Special handling for text scrollers that should loop (consistent type)
        if (fx.type == FXType::TextScroller && fx.subtype == FXSubType::TXT_SCROLL_CONSISTANT) {
            // Only remove if duration is not infinite and timed out
            if (fx.duration != FLT_MAX && timedOut) {
                indicesToRemove.push_back(i);                      // Add index to removal list
#if defined(_DEBUG_FXMANAGER_)
                debug.logLevelMessage(LogLevel::LOG_DEBUG, L"[FXManager] Marked consistent text scroller at index %zu for removal", i);
#endif
            }
        }
        // For all other effects, remove if timed out or progress completed
        else if (timedOut || progressCompleted) {
            indicesToRemove.push_back(i);                          // Add index to removal list
#if defined(_DEBUG_FXMANAGER_)
            debug.logLevelMessage(LogLevel::LOG_DEBUG, L"[FXManager] Marked effect at index %zu for removal - Type: %d, Progress: %.2f",
                i, static_cast<int>(fx.type), fx.progress);
#endif
        }
    }

    // Pass 2: Remove effects in reverse order to maintain index validity
    // Process removal list in reverse order so that removing higher indices doesn't affect lower indices
    for (auto it = indicesToRemove.rbegin(); it != indicesToRemove.rend(); ++it) {
        size_t indexToRemove = *it;                                // Get index to remove

        // Verify index is still valid (safety check)
        if (indexToRemove < effects.size()) {
            effects.erase(effects.begin() + indexToRemove);        // Remove effect at specified index
#if defined(_DEBUG_FXMANAGER_)
            debug.logLevelMessage(LogLevel::LOG_DEBUG, L"[FXManager] Removed effect at index %zu", indexToRemove);
#endif
        }
    }

    // Log summary if any effects were removed
    if (!indicesToRemove.empty()) {
#if defined(_DEBUG_FXMANAGER_)
        debug.logLevelMessage(LogLevel::LOG_DEBUG, L"[FXManager] Successfully removed %zu completed effects", indicesToRemove.size());
#endif
    }
}

void FXManager::RenderFullScreenQuad(const XMFLOAT4& color) {
    // Early validation checks to prevent crashes
    if (bHasCleanedUp || threadManager.threadVars.bIsShuttingDown.load()) {
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"[FXManager] RenderFullScreenQuad called after cleanup - aborting");
        return;
    }

    // Validate color parameters to prevent DirectX crashes
    if (std::isnan(color.x) || std::isnan(color.y) || std::isnan(color.z) || std::isnan(color.w) ||
        std::isinf(color.x) || std::isinf(color.y) || std::isinf(color.z) || std::isinf(color.w)) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[FXManager] Invalid color values (NaN/Inf) in RenderFullScreenQuad - aborting");
        return;
    }

    // Clamp color values to valid DirectX range
    XMFLOAT4 validatedColor = {
        std::max(0.0f, std::min(1.0f, color.x)),                   // Red component
        std::max(0.0f, std::min(1.0f, color.y)),                   // Green component
        std::max(0.0f, std::min(1.0f, color.z)),                   // Blue component
        std::max(0.0f, std::min(1.0f, color.w))                    // Alpha component
    };

    // Validate renderer before attempting DirectX operations
    if (!(&renderer) || !renderer) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[FXManager] Renderer is null in RenderFullScreenQuad - aborting");
        return;
    }

    // Safe DirectX operations with comprehensive error checking
    try 
    {
        // Retrieve our Device and Context from Renderer
        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        device = static_cast<ID3D11Device*>(renderer->GetDevice());
        context = static_cast<ID3D11DeviceContext*>(renderer->GetDeviceContext());
        // Validate all required DirectX resources before use
        if (!renderer || !context) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[FXManager] Invalid DX11 renderer or context in RenderFullScreenQuad");
            return;
        }

        if (!constantBuffer) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[FXManager] Constant buffer is null in RenderFullScreenQuad");
            return;
        }

        if (!fullscreenQuadVertexBuffer) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[FXManager] Vertex buffer is null in RenderFullScreenQuad");
            return;
        }

        if (!inputLayout || !vertexShader || !pixelShader) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[FXManager] Required shaders or input layout not initialized in RenderFullScreenQuad");
            return;
        }

        // Map constant buffer and update color data with error checking
        D3D11_MAPPED_SUBRESOURCE mappedResource;
        HRESULT hr = context->Map(constantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[FXManager] Failed to map constant buffer in RenderFullScreenQuad");
            return;
        }

        // Safely copy validated color data to constant buffer
        if (mappedResource.pData) {
            memcpy(mappedResource.pData, &validatedColor, sizeof(XMFLOAT4));
        }
        else {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[FXManager] Mapped constant buffer data is null");
            context->Unmap(constantBuffer, 0);
            return;
        }

        // Unmap the constant buffer
        context->Unmap(constantBuffer, 0);

        // Set up rendering pipeline with validated parameters
        UINT stride = sizeof(XMFLOAT3) + sizeof(XMFLOAT2);      // Vertex stride calculation
        UINT offset = 0;                                        // Starting offset

        // Set input layout with validation
        context->IASetInputLayout(inputLayout);

        // Set constant buffer to pixel shader with validation
        context->PSSetConstantBuffers(0, 1, &constantBuffer);

        // Set vertex buffer with validation
        context->IASetVertexBuffers(0, 1, &fullscreenQuadVertexBuffer, &stride, &offset);

        // Set primitive topology for triangle strip
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

        // Set shaders with validation
        context->VSSetShader(vertexShader, nullptr, 0);
        context->PSSetShader(pixelShader, nullptr, 0);

        // Draw the fullscreen quad (4 vertices for triangle strip)
        context->Draw(4, 0);

        #if defined(_DEBUG_FXMANAGER_)
            debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[FXManager] Successfully rendered fullscreen quad with color: R=%.2f G=%.2f B=%.2f A=%.2f",
                validatedColor.x, validatedColor.y, validatedColor.z, validatedColor.w);
        #endif
    }
    catch (const std::exception& e) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[FXManager] Exception in RenderFullScreenQuad: " +
            std::wstring(e.what(), e.what() + strlen(e.what())));
    }
    catch (...) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[FXManager] Unknown exception in RenderFullScreenQuad");
    }
}

void FXManager::FadeToColor(XMFLOAT4 color, float duration, float delay) {
    FXItem fadeEffect;
    fadeEffect.type = FXType::ColorFader;
    fadeEffect.subtype = FXSubType::FadeToTargetColor;
    fadeEffect.duration = duration;
    fadeEffect.delay = delay;
    fadeEffect.timeout = duration + 1.0f;
    fadeEffect.progress = 0.0f;
    fadeEffect.targetColor = color;
    AddEffect(fadeEffect);
}

void FXManager::FadeToBlack(float duration, float delay) {
    FadeToColor(XMFLOAT4(0, 0, 0, 1), duration, delay);
}

void FXManager::FadeToWhite(float duration, float delay) {
    FadeToColor(XMFLOAT4(1, 1, 1, 1), duration, delay);
}

// -------------------------------------------------------------------------------------------------------------
// FadeOutThenCallback - Creates a fade effect that executes a callback when complete
// FIXED: Proper callback identification using unique FX ID instead of object reference comparison
// FIXED: Added thread safety with mutex protection
// FIXED: Added proper error handling and null callback checking
// Parameters:
//   color - Target fade color (RGBA)
//   duration - Duration of the fade effect in seconds
//   delay - Delay before starting the fade effect
//   callback - Function to execute when fade completes
// -------------------------------------------------------------------------------------------------------------
void FXManager::FadeOutThenCallback(XMFLOAT4 color, float duration, float delay, std::function<void()> callback) {
#if defined(_DEBUG_FXMANAGER_)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"[FXManager] FadeOutThenCallback() invoked.");
#endif

    // Validate callback parameter to prevent null function crashes
    if (!callback) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[FXManager] FadeOutThenCallback: Null callback provided - operation aborted");
        return;
    }

    // Validate color parameters to prevent DirectX crashes
    if (std::isnan(color.x) || std::isnan(color.y) || std::isnan(color.z) || std::isnan(color.w) ||
        std::isinf(color.x) || std::isinf(color.y) || std::isinf(color.z) || std::isinf(color.w)) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[FXManager] FadeOutThenCallback: Invalid color values (NaN/Inf) - operation aborted");
        return;
    }

    // Validate timing parameters
    if (duration <= 0.0f || delay < 0.0f) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[FXManager] FadeOutThenCallback: Invalid timing parameters - operation aborted");
        return;
    }

    // Use ThreadLockHelper for safe locking
    ThreadLockHelper lock(threadManager, "fxmanager_callback_lock", 1000);
    if (!lock.IsLocked()) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[FXManager] Failed to acquire lock for FadeOutThenCallback");
        return;
    }

    try {
        // Create fade effect with proper initialization and validation
        FXItem fadeEffect;
        fadeEffect.type = FXType::ColorFader;                           // Set effect type to ColorFader
        fadeEffect.subtype = FXSubType::FadeToTargetColor;              // Set subtype to target color fade
        fadeEffect.fxID = static_cast<int>(effects.size()) + 1000;     // Generate unique FX ID (offset to avoid conflicts)
        fadeEffect.duration = duration;                                 // Set fade duration
        fadeEffect.delay = delay;                                       // Set delay before fade starts
        fadeEffect.timeout = duration + delay + 2.0f;                  // Set timeout longer than total effect time
        fadeEffect.progress = 0.0f;                                     // Initialize progress to zero
        fadeEffect.targetColor = color;                                 // Set target fade color
        fadeEffect.startTime = std::chrono::steady_clock::now();        // Record start time
        fadeEffect.lastUpdate = fadeEffect.startTime;                   // Initialize last update time

        // Validate that the FX ID is unique
        bool idExists = false;
        for (const auto& existingFx : effects) {
            if (existingFx.fxID == fadeEffect.fxID) {
                idExists = true;
                break;
            }
        }

        // If ID exists, generate a new one
        if (idExists) {
            fadeEffect.fxID = static_cast<int>(effects.size()) + static_cast<int>(pendingCallbacks.size()) + 2000;
#if defined(_DEBUG_FXMANAGER_)
            debug.logLevelMessage(LogLevel::LOG_DEBUG, L"[FXManager] Generated alternate FX ID: " + std::to_wstring(fadeEffect.fxID));
#endif
        }

        // Add effect to effects vector
        AddEffect(fadeEffect);

        // ENHANCED: Create callback entry using the new constructor for better safety
        CallbackEntry callbackEntry(fadeEffect.fxID, callback);        // Use explicit constructor

        // Additional validation for the callback entry
        if (callbackEntry.fxID != fadeEffect.fxID) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[FXManager] Callback entry FX ID mismatch - operation aborted");
            return;
        }

        // Add callback to pending callbacks vector
        pendingCallbacks.push_back(std::move(callbackEntry));          // Use move semantics for efficiency

#if defined(_DEBUG_FXMANAGER_)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[FXManager] FadeOutThenCallback created: FXID=%d, Duration=%.2f, Delay=%.2f, CallbackCount=%zu",
            fadeEffect.fxID, duration, delay, pendingCallbacks.size());
#endif
    }
    catch (const std::exception& e) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[FXManager] Exception in FadeOutThenCallback: " +
            std::wstring(e.what(), e.what() + strlen(e.what())));
    }
    catch (...) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[FXManager] Unknown exception in FadeOutThenCallback");
    }
}

void FXManager::FadeOutInSequence(XMFLOAT4 fadeOutColor, XMFLOAT4 fadeInColor, float duration, float delay, std::function<void()> midpointCallback) {
    FadeOutThenCallback(fadeOutColor, duration, delay, [=]() {
        if (midpointCallback) midpointCallback();
        FadeToColor(fadeInColor, duration, delay);
        });
}

void FXManager::FadeToImage(float duration, float delay) {
    FXItem fadeEffect;
    fadeEffect.type = FXType::ColorFader;
    fadeEffect.subtype = FXSubType::FadeToBackground;
    fadeEffect.duration = duration;
    fadeEffect.delay = delay;
    fadeEffect.timeout = duration + 1.0f;
    fadeEffect.progress = 0.0f; // Start fully black and fade out
    fadeEffect.targetColor = XMFLOAT4(0, 0, 0, 1);
    AddEffect(fadeEffect);
}

void FXManager::UpdateTweens(float deltaTime) {
    for (auto& tween : activeTweens) {
        if (!tween.active) continue;
        tween.elapsed += deltaTime;
        float t = std::min(tween.elapsed / tween.duration, 1.0f);
        int newSpeed = static_cast<int>(tween.from + (tween.to - tween.from) * t);
        UpdateScrollSpeed(tween.textureIndex, newSpeed);
        if (t >= 1.0f) tween.active = false;
    }

    // Remove finished tweens
    activeTweens.erase(std::remove_if(activeTweens.begin(), activeTweens.end(),
        [](const ScrollTween& t) { return !t.active; }), activeTweens.end());
}

void FXManager::StartParallaxLayer(
    BlitObj2DIndexType textureIndex,
    FXSubType direction,
    int baseSpeed,
    float depthMultiplier,
    int tileWidth,
    int tileHeight,
    float delay,
    bool cameraLinked)
{
    FXItem fx;
    fx.type = FXType::Scroller;
    fx.subtype = direction;
    fx.scrollSpeed = baseSpeed;
    fx.textureIndex = textureIndex;
    fx.tileWidth = tileWidth;
    fx.tileHeight = tileHeight;
    fx.delay = delay;
    fx.progress = 0.0f;
    fx.timeout = FLT_MAX;
    fx.depthMultiplier = depthMultiplier;
    fx.cameraLinked = cameraLinked;
    fx.startTime = std::chrono::steady_clock::now();
    fx.lastUpdate = fx.startTime;

    AddEffect(fx);
}

void FXManager::SetScrollDirection(BlitObj2DIndexType textureIndex, FXSubType newDirection) {
    for (auto& fx : effects) {
        if (fx.type == FXType::Scroller && fx.textureIndex == textureIndex) {
            fx.subtype = newDirection;
            debug.logLevelMessage(LogLevel::LOG_INFO, L"FXManager: Updated scroll direction for texture " + std::to_wstring(int(textureIndex)));
        }
    }
}

void FXManager::FadeScrollSpeed(BlitObj2DIndexType textureIndex, int fromSpeed, int toSpeed, float duration) {
    // Set initial speed
    UpdateScrollSpeed(textureIndex, fromSpeed);

    // Add tween
    ScrollTween tween{ textureIndex, fromSpeed, toSpeed, duration };
    activeTweens.push_back(tween);

    debug.logLevelMessage(LogLevel::LOG_INFO,
        L"FXManager: Tween scroll speed from " + std::to_wstring(fromSpeed) +
        L" to " + std::to_wstring(toSpeed) + L" over " + std::to_wstring(duration) + L"s");
}

void FXManager::PauseScroll(BlitObj2DIndexType textureIndex) {
    for (auto& fx : effects) {
        if (fx.type == FXType::Scroller && fx.textureIndex == textureIndex) {
            fx.isPaused = true;
            debug.logLevelMessage(LogLevel::LOG_INFO, L"FXManager: Paused scroll for texture " + std::to_wstring(int(textureIndex)));
        }
    }
}

void FXManager::ResumeScroll(BlitObj2DIndexType textureIndex) {
    for (auto& fx : effects) {
        if (fx.type == FXType::Scroller && fx.textureIndex == textureIndex) {
            fx.isPaused = false;
            fx.lastUpdate = std::chrono::steady_clock::now(); // avoid jump
            debug.logLevelMessage(LogLevel::LOG_INFO, L"FXManager: Resumed scroll for texture " + std::to_wstring(int(textureIndex)));
        }
    }
}

void FXManager::UpdateScrollSpeed(BlitObj2DIndexType textureIndex, int newSpeed) {
    for (auto& fx : effects) {
        if (fx.type == FXType::Scroller && fx.textureIndex == textureIndex) {
            fx.scrollSpeed = newSpeed;

            debug.logLevelMessage(LogLevel::LOG_INFO,
                L"FXManager: Scroll speed updated for texture " + std::to_wstring(int(textureIndex)) +
                L" -> new speed: " + std::to_wstring(newSpeed));
        }
    }
}

void FXManager::ApplyScroller(FXItem& fxItem) {
    auto now = std::chrono::steady_clock::now();
    float elapsed = std::chrono::duration<float>(now - fxItem.lastUpdate).count();

    if (fxItem.isPaused) 
    {
        // Still render to keep visual intact
        renderer->Blit2DWrappedObjectAtOffset(
            fxItem.textureIndex,
            0, 0,
            fxItem.currentXOffset,
            fxItem.currentYOffset,
            fxItem.tileWidth,
            fxItem.tileHeight
        );
        return;
    }

    // Always render every frame
    renderer->Blit2DWrappedObjectAtOffset(
        fxItem.textureIndex,
        0, 0,
        fxItem.currentXOffset,
        fxItem.currentYOffset,
        fxItem.tileWidth,
        fxItem.tileHeight
    );

    // Only update the offset if the delay has passed
    if (elapsed >= fxItem.delay) {
        fxItem.lastUpdate = now;
        int effectiveSpeed = static_cast<int>(fxItem.scrollSpeed * fxItem.depthMultiplier);

        switch (fxItem.subtype) {
        case FXSubType::ScrollRight:
            fxItem.currentXOffset += effectiveSpeed;
            break;
        case FXSubType::ScrollLeft:
            fxItem.currentXOffset -= effectiveSpeed;
            break;
        case FXSubType::ScrollUp:
            fxItem.currentYOffset -= effectiveSpeed;
            break;
        case FXSubType::ScrollDown:
            fxItem.currentYOffset += effectiveSpeed;
            break;
        case FXSubType::ScrollUpAndLeft:
            fxItem.currentXOffset -= effectiveSpeed;
            fxItem.currentYOffset -= effectiveSpeed;
            break;
        case FXSubType::ScrollUpAndRight:
            fxItem.currentXOffset += effectiveSpeed;
            fxItem.currentYOffset -= effectiveSpeed;
            break;
        case FXSubType::ScrollDownAndLeft:
            fxItem.currentXOffset -= effectiveSpeed;
            fxItem.currentYOffset += effectiveSpeed;
            break;
        case FXSubType::ScrollDownAndRight:
            fxItem.currentXOffset += effectiveSpeed;
            fxItem.currentYOffset += effectiveSpeed;
            break;
        default:
            break;
        }

        fxItem.currentXOffset = ((fxItem.currentXOffset % fxItem.tileWidth) + fxItem.tileWidth) % fxItem.tileWidth;
        fxItem.currentYOffset = ((fxItem.currentYOffset % fxItem.tileHeight) + fxItem.tileHeight) % fxItem.tileHeight;
    }
}

void FXManager::StopScrollEffect(BlitObj2DIndexType textureIndex) {
    effects.erase(
        std::remove_if(effects.begin(), effects.end(), [=](const FXItem& fx) {
            return fx.type == FXType::Scroller && fx.textureIndex == textureIndex;
            }),
        effects.end()
    );

    debug.logLevelMessage(LogLevel::LOG_INFO, L"FXManager: Scroll effect manually stopped.");
}

void FXManager::StartScrollEffect(
    BlitObj2DIndexType textureIndex,
    FXSubType direction,
    int speed,
    int tileWidth,
    int tileHeight,
    float delay
) {
    FXItem fx;
    fx.type = FXType::Scroller;
    fx.subtype = direction;
    fx.scrollSpeed = speed;
    fx.textureIndex = textureIndex;
    fx.tileWidth = tileWidth;
    fx.tileHeight = tileHeight;
    fx.delay = delay;
    fx.progress = 0.0f;
    fx.timeout = FLT_MAX;                                   // Run forever unless manually removed
    fx.startTime = std::chrono::steady_clock::now();
    fx.lastUpdate = fx.startTime;

    debug.logLevelMessage(LogLevel::LOG_INFO, L"FXManager: Started scroll effect.");
    AddEffect(fx);
}

void FXManager::CreateParticleExplosion(int startX, int startY, int maxParticles, int maxRadius)
{
#if defined(_DEBUG_FXMANAGER_)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"[FXManager] CreateParticleExplosion() invoked.");
#endif

    std::lock_guard<std::mutex> lock(m_effectsMutex); // Add lock for thread safety

    FXItem newFX;
    newFX.type = FXType::ParticleExplosion;
    newFX.fxID = effects.size() + 1;
    newFX.originX = startX;
    newFX.originY = startY;
    newFX.duration = 3.0f;  // Set a reasonable duration
    newFX.timeout = 5.0f;   // Set a timeout longer than duration

    const float PI = 3.14159265f;
    float angleStep = 2.0f * PI / static_cast<float>(maxParticles);

    // Define a static color palette
    const float colors[15][3] =
    {
        {1.0f, 0.0f, 0.0f}, {1.0f, 0.5f, 0.0f}, {1.0f, 1.0f, 0.0f},
        {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 1.0f},
        {0.5f, 0.0f, 1.0f}, {1.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 0.5f},
        {0.7f, 0.7f, 0.7f}, {1.0f, 0.8f, 0.2f}, {0.3f, 1.0f, 0.3f},
        {0.9f, 0.2f, 0.9f}, {0.6f, 0.6f, 1.0f}, {0.8f, 0.4f, 0.2f}
    };

    for (int i = 0; i < maxParticles; ++i)
    {
        Particle p;

        // Create proper angle distribution with a slight random variance
        p.angle = angleStep * i + (static_cast<float>(rand()) / RAND_MAX * 0.2f - 0.1f);

        // Set delay variables with better randomization
        p.delayCount = rand() % 3;  // Some particles start with a small initial delay
        p.delayBase = (rand() % 3) + 2;  // Random delay between 2-5 frames

        // Randomize speed slightly for more natural effect
        p.speed = 2.0f + static_cast<float>(rand()) / RAND_MAX * 3.0f; // 2-5 speed

        // Initialize radius and max radius
        p.radius = 0.0f;
        p.maxRadius = static_cast<float>(maxRadius);

        // Pick a random color from the palette
        int colorIndex = rand() % 15; // 0 to 14 inclusive
        p.r = colors[colorIndex][0];
        p.g = colors[colorIndex][1];
        p.b = colors[colorIndex][2];
        p.a = 1.0f;

        // Set initial position to the origin point
        p.x = static_cast<float>(startX);
        p.y = static_cast<float>(startY);
        p.completed = false;
        p.hasLoggedCompletion = false;

#if defined(_DEBUG_PARTICLEFX_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG,
            L"[Particle] FXID=%d Angle=%.2f Speed=%.2f DelayBase=%d",
            newFX.fxID, p.angle, p.speed, p.delayBase);
#endif

        newFX.particles.push_back(p);
    }

    // Set start time and last update time
    newFX.startTime = std::chrono::steady_clock::now();
    newFX.lastUpdate = newFX.startTime;

    effects.push_back(newFX);

#if defined(_DEBUG_FXMANAGER_)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[FXManager] ParticleExplosion created: Pixels=%d, MaxRadius=%d, FXID=%d, Origin=(%d,%d)",
        maxParticles, maxRadius, newFX.fxID, startX, startY);
#endif
}

// -------------------------------------------------------------------------------------------------------------
// Renders all active ParticleExplosion effects.
// Each particle increments delayCount every frame.
// Only when delayCount >= delayBase is the particle's position updated (outward dispersion).
// The particle is always rendered at current position and fade level.
// Once all particles have reached maxRadius, the effect is marked completed.
// -------------------------------------------------------------------------------------------------------------
void FXManager::RenderParticles(FXItem& fxItem)
{
    std::lock_guard<std::mutex> lock(m_effectsMutex); // Ensure thread safety

    if (fxItem.type != FXType::ParticleExplosion)
        return;

    bool allCompleted = true;
    auto now = std::chrono::steady_clock::now();
    float elapsedSecs = std::chrono::duration<float>(now - fxItem.startTime).count();
    float lifeFactor = 1.0f;

    // Add overall FX life fading - particles fade out collectively at the end of the effect
    if (fxItem.duration > 0.0f && elapsedSecs > fxItem.duration * 0.7f) {
        lifeFactor = 1.0f - ((elapsedSecs - fxItem.duration * 0.7f) / (fxItem.duration * 0.3f));
        lifeFactor = std::max(0.0f, std::min(1.0f, lifeFactor));
    }

    for (size_t i = 0; i < fxItem.particles.size(); ++i)
    {
        Particle& p = fxItem.particles[i]; // Reference to the actual vector element
        if (!p.completed)
        {
            // Increment delay counter
            p.delayCount += 1;

            // Update position if threshold met
            if (p.delayCount >= p.delayBase)
            {
                p.delayCount = 0;  // reset delay counter

                // Update radius - move the particle outward
                p.radius += p.speed;

                // Check if reached max radius
                if (p.radius >= p.maxRadius)
                {
                    p.radius = p.maxRadius;
                    p.completed = true;

#if defined(_DEBUG_PARTICLEFX_) && defined(_DEBUG)
                    if (!p.hasLoggedCompletion)
                    {
                        debug.logDebugMessage(LogLevel::LOG_DEBUG,
                            L"[Particle] FXID=%d completed at Radius=%.2f",
                            fxItem.fxID, p.radius);
                        p.hasLoggedCompletion = true;
                    }
#endif
                    continue;
                }
            }

            allCompleted = false;
        }

        // Compute position using proper angle as a float
        // This is key: using the float angle with trig functions for correct circular dispersion
        float sinVal, cosVal;
        FAST_MATH.FastSinCos(p.angle, sinVal, cosVal);
        float xPos = fxItem.originX + cosVal * p.radius;
        float yPos = fxItem.originY + sinVal * p.radius;

        // Update the particle's stored position
        p.x = xPos;
        p.y = yPos;

        // Improved fade calculation - smoother fade out as particles approach max radius
        // Using a non-linear curve for more visually appealing fade
        float distanceRatio = p.radius / p.maxRadius;
        float fadeFactor = 1.0f - (distanceRatio * distanceRatio); // Quadratic fade for better visual

        // Apply the overall effect lifetime factor as well
        fadeFactor *= lifeFactor;

        // Clamp alpha to valid range
        float alpha = p.a * fadeFactor;
        alpha = std::max(0.0f, std::min(1.0f, alpha));

        XMFLOAT4 finalColor(p.r, p.g, p.b, alpha);

        // Render the pixel
        renderer->Blit2DColoredPixel(static_cast<int>(p.x), static_cast<int>(p.y), 2.0f, finalColor);

#if defined(_DEBUG_PARTICLEFX_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG,
            L"[FXID=%d] Particle Pos=(%.1f, %.1f) Radius=%.2f Delay=%d/%d Alpha=%.2f",
            fxItem.fxID, p.x, p.y, p.radius, p.delayCount, p.delayBase, alpha);
#endif
    }

    if (allCompleted && !fxItem.restartOnExpire)
    {
#if defined(_DEBUG_PARTICLEFX_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_INFO,
            L"[ParticleExplosion] FXID=%d COMPLETED.", fxItem.fxID);
#endif
        fxItem.progress = 1.0f;
        fxItem.timeout = 0.0f;
    }
}

// This is the Renderer that is used for the 3D Rendering operations.
// COMPLETE: Enhanced FXManager::Render() function with comprehensive safety and error handling
// File: DX_FXManager.cpp
// Replace the entire Render() function (around lines 1240-1400)

void FXManager::Render() {
    // CRITICAL: Early validation checks to prevent crashes during rendering
    if (bHasCleanedUp || threadManager.threadVars.bIsShuttingDown.load()) {
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"[FXManager] Render called after cleanup or during shutdown - aborting");
        return;
    }

    // Validate renderer before proceeding
    if (!(&renderer) || !renderer) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[FXManager] Renderer is null in Render() - aborting");
        return;
    }

    // Check if effects vector is empty to avoid unnecessary processing
    if (effects.empty() && pendingCallbacks.empty()) {
        return;                                                         // Nothing to render, exit early
    }

    // Prevent recursive rendering calls
    if (bIsRendering.load()) {
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"[FXManager] Recursive render call detected - skipping");
        return;
    }

    // Set rendering flag to prevent recursive calls
    bIsRendering.store(true);

    try {
        // Save render state before making any changes
        SaveRenderState();

        // Get timing information for delta time calculations
        static auto lastRenderTime = std::chrono::steady_clock::now();
        auto now = std::chrono::steady_clock::now();
        float deltaTime = std::chrono::duration<float>(now - lastRenderTime).count();

        // Clamp delta time to prevent huge jumps if frame rate drops
        deltaTime = std::min(deltaTime, 0.1f);                         // Maximum 100ms delta time
        lastRenderTime = now;

        // Update per-frame animated effects
        for (auto& fx : effects) {
            if (fx.type == FXType::Starfield)
                UpdateStarfield(deltaTime);
            else if (fx.type == FXType::WarpDotTunnel)
                UpdateWarpDotTunnel(fx, deltaTime);
        }

        // Process all active effects
        for (auto& fx : effects) {
            // Skip processing if effect is invalid or system is shutting down
            if (threadManager.threadVars.bIsShuttingDown.load()) {
                break;                                                  // Exit loop if shutting down
            }

            switch (fx.type) {
            case FXType::ColorFader:
                ApplyColorFader(fx);
                break;

            default:
                // Log unknown effect types for debugging
#if defined(_DEBUG_FXMANAGER_)
                debug.logLevelMessage(LogLevel::LOG_WARNING, L"[FXManager] Unknown effect type: " + std::to_wstring(static_cast<int>(fx.type)));
#endif
                break;
            }
        }

        // FIXED: Process pending callbacks with safe iterator handling and thread safety
        if (!pendingCallbacks.empty()) {
            // Use ThreadLockHelper for safe callback processing
            ThreadLockHelper callbackLock(threadManager, "fxmanager_callback_process_lock", 500);
            if (callbackLock.IsLocked()) {
                auto currentTime = std::chrono::steady_clock::now();    // Get current time for timeout checking

                // FIXED: Use safe two-pass approach to avoid iterator invalidation
                // Pass 1: Identify callbacks to execute and mark them
                std::vector<size_t> callbacksToExecute;                 // Store indices of callbacks to execute
                std::vector<size_t> callbacksToRemove;                  // Store indices of callbacks to remove

                callbacksToExecute.reserve(pendingCallbacks.size());    // Reserve space to avoid reallocations
                callbacksToRemove.reserve(pendingCallbacks.size());     // Reserve space to avoid reallocations

                // Scan all callbacks to determine which ones need processing
                for (size_t i = 0; i < pendingCallbacks.size(); ++i) {
                    CallbackEntry& entry = pendingCallbacks[i];         // Get reference to callback entry

                    // Check for timeout (callbacks older than 30 seconds are removed to prevent memory leaks)
                    auto age = std::chrono::duration<float>(currentTime - entry.creationTime).count();
                    if (age > 30.0f) {
                        debug.logLevelMessage(LogLevel::LOG_WARNING, L"[FXManager] Callback timeout - removing stale callback for FXID: " + std::to_wstring(entry.fxID));
                        callbacksToRemove.push_back(i);                 // Mark for removal
                        continue;
                    }

                    // Skip if already executed to prevent double execution
                    if (entry.isExecuted) {
                        callbacksToRemove.push_back(i);                 // Mark for removal
                        continue;
                    }

                    // Check if the corresponding effect has completed
                    bool effectCompleted = false;
                    for (const auto& fx : effects) {
                        if (fx.fxID == entry.fxID && fx.progress >= 1.0f) {
                            effectCompleted = true;
                            break;
                        }
                    }

                    // Mark callback for execution if effect is completed
                    if (effectCompleted) {
                        callbacksToExecute.push_back(i);                // Mark for execution
                    }
                }

                // Pass 2: Execute marked callbacks safely
                for (size_t index : callbacksToExecute) {
                    // Validate index is still valid (safety check)
                    if (index < pendingCallbacks.size()) {
                        CallbackEntry& entry = pendingCallbacks[index]; // Get reference to callback entry

                        // Double-check the callback hasn't been executed already
                        if (!entry.isExecuted && entry.callback) {
                            try {
                                entry.callback();                       // Execute the callback function
                                entry.isExecuted = true;                // Mark as executed
                                callbacksToRemove.push_back(index);     // Mark for removal
#if defined(_DEBUG_FXMANAGER_)
                                debug.logLevelMessage(LogLevel::LOG_INFO, L"[FXManager] Callback executed successfully for FXID: " + std::to_wstring(entry.fxID));
#endif
                            }
                            catch (const std::exception& e) {
                                // Handle callback execution errors gracefully
                                debug.logLevelMessage(LogLevel::LOG_ERROR, L"[FXManager] Callback execution failed for FXID: " + std::to_wstring(entry.fxID) +
                                    L" - Error: " + std::wstring(e.what(), e.what() + strlen(e.what())));
                                entry.isExecuted = true;                // Mark as executed to prevent retry
                                callbacksToRemove.push_back(index);     // Mark for removal
                            }
                            catch (...) {
                                // Handle unknown exceptions
                                debug.logLevelMessage(LogLevel::LOG_ERROR, L"[FXManager] Unknown exception in callback execution for FXID: " + std::to_wstring(entry.fxID));
                                entry.isExecuted = true;                // Mark as executed to prevent retry
                                callbacksToRemove.push_back(index);     // Mark for removal
                            }
                        }
                    }
                }

                // Pass 3: Remove processed callbacks in reverse order to maintain index validity
                if (!callbacksToRemove.empty()) {
                    // Sort indices in descending order to remove from back to front
                    std::sort(callbacksToRemove.begin(), callbacksToRemove.end(), std::greater<size_t>());

                    // Remove duplicates that might have been added multiple times
                    callbacksToRemove.erase(std::unique(callbacksToRemove.begin(), callbacksToRemove.end()), callbacksToRemove.end());

                    // Remove callbacks in reverse order to preserve index validity
                    for (size_t index : callbacksToRemove) {
                        // Validate index is still valid before removal
                        if (index < pendingCallbacks.size()) {
                            pendingCallbacks.erase(pendingCallbacks.begin() + index);   // Remove callback at specified index
#if defined(_DEBUG_FXMANAGER_)
                            debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[FXManager] Removed callback at index %zu", index);
#endif
                        }
                    }

#if defined(_DEBUG_FXMANAGER_)
                    debug.logLevelMessage(LogLevel::LOG_DEBUG, L"[FXManager] Processed " + std::to_wstring(callbacksToExecute.size()) +
                        L" callbacks, removed " + std::to_wstring(callbacksToRemove.size()) + L" entries");
#endif
                }
            }
            else {
#if defined(_DEBUG_FXMANAGER_)
                debug.logLevelMessage(LogLevel::LOG_WARNING, L"[FXManager] Could not acquire callback processing lock - skipping frame");
#endif
            }
        }

        // Remove completed effects to prevent memory leaks
        RemoveCompletedEffects();

        // Restore render state after processing
        RestoreRenderState();
    }
    catch (const std::exception& e) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[FXManager] Exception in Render(): " +
            std::wstring(e.what(), e.what() + strlen(e.what())));

        // Attempt to restore render state even after exception
        try {
            RestoreRenderState();
        }
        catch (...) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[FXManager] Failed to restore render state after exception");
        }
    }
    catch (...) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[FXManager] Unknown exception in Render()");

        // Attempt to restore render state even after exception
        try {
            RestoreRenderState();
        }
        catch (...) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[FXManager] Failed to restore render state after unknown exception");
        }
    }

    // Clear rendering flag
    bIsRendering.store(false);
}

// Use for 2D Rendering Operations.
void FXManager::Render2D()
{
    if (bHasCleanedUp) return;
    static auto lastTweenTime = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    float deltaTime = std::chrono::duration<float>(now - lastTweenTime).count();

    UpdateTweens(deltaTime);

    for (auto& fx : effects)
    {
        // Texture Scroller
        if (fx.type == FXType::Scroller)
        {
            ApplyScroller(fx);
        }

        // Particle Explosion
        if (fx.type == FXType::ParticleExplosion)
        {
            RenderParticles(fx);
        }

        // NEW: Text Scroller
        if (fx.type == FXType::TextScroller)
        {
            UpdateTextScroller(fx, deltaTime);                      // Update text scroller position and state
            RenderTextScroller(fx);                                 // Render text scroller to screen
        }
    }

    lastTweenTime = now;  // moved here to avoid premature zeroing
}

void FXManager::RenderFX(int effectID, ID3D11DeviceContext* context, const XMMATRIX& worldMatrix)
{
    #if defined(_DEBUG_FXMANAGER_)
        debug.logLevelMessage(LogLevel::LOG_DEBUG, L"FXManager: RenderFX called with ID = " + std::to_wstring(effectID));
    #endif

    if (!context || effectID < 0)
        return;

    for (FXItem& fx : effects)
    {
        if (fx.fxID != effectID)
            continue;

        // Calculate progress using correct steady_clock (matches FXItem's type)
        auto now = std::chrono::steady_clock::now();
        float elapsed = std::chrono::duration<float>(now - fx.startTime).count();
        static auto lastTweenTime = std::chrono::steady_clock::now();
        float deltaTime = std::chrono::duration<float>(now - lastTweenTime).count();
        fx.progress = fx.duration > 0.0f ? std::clamp(elapsed / fx.duration, 0.0f, 1.0f) : 1.0f;

        // Update lastUpdate time
        fx.lastUpdate = now;

        // Apply FX logic (example: ColorFader)
        switch (fx.type)
        {
        case FXType::ColorFader:
            ApplyColorFader(fx);
            break;

        case FXType::Starfield:
            UpdateStarfield(deltaTime);
            RenderStarfield(fx, context, worldMatrix);
            break;

        case FXType::WarpDotTunnel:
            RenderWarpDotTunnel(fx, context);
            break;

        default:
            #if defined(_DEBUG_FXMANAGER_)
                debug.logLevelMessage(LogLevel::LOG_WARNING, L"FXManager: Unknown FXType for RenderFX");
            #endif
            break;
        }

        // Handle FX restart or chaining
        if (fx.progress >= 1.0f)
        {
            if (fx.restartOnExpire)
            {
                fx.startTime = std::chrono::steady_clock::now();
                fx.progress = 0.0f;
                fx.lastUpdate = fx.startTime;
                #if defined(_DEBUG_FXMANAGER_)
                    debug.logLevelMessage(LogLevel::LOG_INFO, L"FXManager: Restarting FX ID = " + std::to_wstring(fx.fxID));
                #endif
            }
            else if (fx.nextEffectID >= 0)
            {
                AddEffect(FXItem{ fx.fxID = fx.nextEffectID });
                #if defined(_DEBUG_FXMANAGER_)
                    debug.logLevelMessage(LogLevel::LOG_INFO, L"FXManager: Chaining FX ID = " + std::to_wstring(fx.fxID) + L" → " + std::to_wstring(fx.nextEffectID));
                #endif
            }
        }
    }
}

// --------------------------------------------------------------------------------------------------------
void FXManager::CreateStarfield(int numStars, float circularRadius, float resetDepthPos, XMFLOAT3 startPos, bool reverse)
{
#if defined(_DEBUG_FXMANAGER_)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"[FXManager] CreateStarfield() invoked with " +
        std::to_wstring(numStars) + L" stars, radius " + std::to_wstring(circularRadius));
#endif

    std::lock_guard<std::mutex> lock(m_effectsMutex);

    FXItem newFX;
    newFX.type = FXType::Starfield;
    newFX.fxID = static_cast<int>(effects.size()) + 1;
    starfieldID = newFX.fxID;
    newFX.duration = FLT_MAX;
    newFX.timeout = FLT_MAX;
    newFX.progress = 0.0f;
    newFX.depthMultiplier = resetDepthPos;
    newFX.starfieldOrigin = startPos;
    newFX.starfieldReverse = reverse;

    for (int i = 0; i < numStars; ++i)
    {
        Particle p;

        float angle = static_cast<float>(rand()) / RAND_MAX * XM_2PI;
        float dist  = (0.1f + (static_cast<float>(rand()) / RAND_MAX) * 0.9f) * circularRadius;
        float spreadX = cosf(angle) * dist;
        float spreadY = sinf(angle) * dist;

        if (!reverse)
        {
            // Default: stars spawn far and fly toward/past camera
            p.x     = startPos.x + spreadX;
            p.y     = startPos.y + spreadY;
            p.angle = startPos.z + resetDepthPos * (0.1f + 0.9f * static_cast<float>(rand()) / RAND_MAX);
            p.vx    = 0.0f;
            p.vy    = 0.0f;
        }
        else
        {
            // Reverse: stars start spread near camera and converge toward startPos as z increases
            float startZ  = 5.0f + static_cast<float>(rand()) / RAND_MAX * (resetDepthPos * 0.1f);
            float fraction = 1.0f - (startZ / resetDepthPos); // ~1.0 near camera
            p.vx    = spreadX;  // full spread offset from startPos at camera-near position
            p.vy    = spreadY;
            p.angle = startZ;
            p.x     = startPos.x + p.vx * fraction;
            p.y     = startPos.y + p.vy * fraction;
        }

        p.speed    = 20.0f + static_cast<float>(rand()) / RAND_MAX * 40.0f;
        p.radius   = 1.0f  + static_cast<float>(rand()) / RAND_MAX * 2.0f;
        p.maxRadius = resetDepthPos;

        float brightness = 0.7f + static_cast<float>(rand()) / RAND_MAX * 0.3f;
        p.r = brightness;
        p.g = brightness * (0.85f + static_cast<float>(rand()) / RAND_MAX * 0.15f);
        p.b = brightness * (0.9f  + static_cast<float>(rand()) / RAND_MAX * 0.1f);
        p.a = 1.0f;

        p.completed          = false;
        p.hasLoggedCompletion = false;
        p.delayCount         = 0;
        p.delayBase          = static_cast<int>(p.angle);

        newFX.particles.push_back(p);
    }

    newFX.startTime = std::chrono::steady_clock::now();
    newFX.lastUpdate = newFX.startTime;

    effects.push_back(newFX);

    #if defined(_DEBUG_FXMANAGER_)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[FXManager] Starfield created: Stars=%d, Radius=%.2f, ResetDepth=%.2f, Reverse=%s, FXID=%d",
            numStars, circularRadius, resetDepthPos, reverse ? L"true" : L"false", newFX.fxID);
    #endif
}

void FXManager::UpdateStarfield(float deltaTime)
{
    for (auto& fx : effects)
    {
        if (fx.type != FXType::Starfield)
            continue;

        float    resetDepth = fx.depthMultiplier;
        bool     reverse    = fx.starfieldReverse;
        XMFLOAT3 origin     = fx.starfieldOrigin;

        for (auto& p : fx.particles)
        {
            if (p.completed)
                continue;

            float clampedDelta = std::min(deltaTime, 0.1f);
            float zPos = p.angle;

            if (!reverse)
            {
                // Default: stars fly toward camera (z decreases)
                zPos -= p.speed * clampedDelta;

                float distRatio = zPos / resetDepth;
                p.a = std::max(0.0f, std::min(1.0f, distRatio * 1.2f));

                if (zPos <= 5.0f)
                {
                    float angle = static_cast<float>(rand()) / RAND_MAX * XM_2PI;
                    float dist  = (0.1f + (static_cast<float>(rand()) / RAND_MAX) * 0.9f) * (resetDepth * 0.1f);

                    float outCos, outSin;
                    FAST_MATH.FastSinCos(angle, outSin, outCos);
                    p.x     = origin.x + outCos * dist;
                    p.y     = origin.y + outSin * dist;
                    p.angle = origin.z + resetDepth * (0.9f + 0.1f * static_cast<float>(rand()) / RAND_MAX);
                    p.speed  = 20.0f + static_cast<float>(rand()) / RAND_MAX * 40.0f;
                    p.radius = 1.0f  + static_cast<float>(rand()) / RAND_MAX * 1.2f;
                    p.a      = 1.0f;
                }
                else
                {
                    p.angle = zPos;
                }
            }
            else
            {
                // Reverse: stars travel away from camera, converging toward starfieldOrigin
                zPos += p.speed * clampedDelta;

                if (zPos >= resetDepth)
                {
                    // Reset: spawn spread near camera again
                    float angle = static_cast<float>(rand()) / RAND_MAX * XM_2PI;
                    float dist  = (0.1f + (static_cast<float>(rand()) / RAND_MAX) * 0.9f) * p.maxRadius;
                    p.vx    = cosf(angle) * dist;
                    p.vy    = sinf(angle) * dist;
                    p.angle = 5.0f + static_cast<float>(rand()) / RAND_MAX * (resetDepth * 0.1f);

                    float fraction = 1.0f - (p.angle / resetDepth);
                    p.x     = origin.x + p.vx * fraction;
                    p.y     = origin.y + p.vy * fraction;
                    p.speed  = 20.0f + static_cast<float>(rand()) / RAND_MAX * 40.0f;
                    p.radius = 1.0f  + static_cast<float>(rand()) / RAND_MAX * 1.2f;
                    p.a      = 1.0f;
                }
                else
                {
                    // Converge x,y toward origin as z increases
                    float fraction = 1.0f - (zPos / resetDepth);
                    p.x     = origin.x + p.vx * fraction;
                    p.y     = origin.y + p.vy * fraction;
                    p.angle = zPos;

                    // Fade out as stars recede
                    p.a = std::max(0.0f, std::min(1.0f, fraction * 1.2f));
                }
            }
        }
    }
}

void FXManager::StopStarfield() {
    if (starfieldID <= 0) {
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"FXManager: Attempted to stop starfield, but no active starfield found.");
        return;
    }

    effects.erase(
        std::remove_if(effects.begin(), effects.end(), [this](const FXItem& fx) {
            return fx.type == FXType::Starfield && fx.fxID == starfieldID;
            }),
        effects.end()
    );

    debug.logLevelMessage(LogLevel::LOG_INFO, L"FXManager: Starfield effect manually stopped.");
    starfieldID = 0;
}

void FXManager::RenderStarfield(FXItem& fxItem, ID3D11DeviceContext* context, const XMMATRIX& viewMatrix)
{
    if (fxItem.type != FXType::Starfield || !context)
        return;

    // Get camera transform matrices from the context
    // Calculate view projection matrix
    XMMATRIX viewProj = renderer->myCamera.GetViewMatrix() * renderer->myCamera.GetProjectionMatrix();

    // For each star in the starfield
    for (auto& p : fxItem.particles)
    {
        if (p.completed)
            continue;

        // Create the 3D world position
        XMVECTOR worldPos = XMVectorSet(p.x, p.y, p.angle, 1.0f);

        // Transform to projection space
        XMVECTOR projPos = XMVector3TransformCoord(worldPos, viewProj);

        // If in front of camera and within normalized device coordinates
        if (XMVectorGetZ(projPos) <= 1.0f &&
            XMVectorGetX(projPos) >= -1.0f && XMVectorGetX(projPos) <= 1.0f &&
            XMVectorGetY(projPos) >= -1.0f && XMVectorGetY(projPos) <= 1.0f)
        {
            // Convert to screen coordinates
            float screenX = (XMVectorGetX(projPos) + 1.0f) * 0.5f * renderer->iOrigWidth;
            float screenY = (1.0f - XMVectorGetY(projPos)) * 0.5f * renderer->iOrigHeight;

            // Calculate size based on z-position
            // Stars get larger as they get closer
            float sizeScale = 1.0f + (fxItem.depthMultiplier - p.angle) / fxItem.depthMultiplier * 3.0f;
            float displaySize = p.radius * sizeScale;

            // Draw the star
            XMFLOAT4 starColor(p.r, p.g, p.b, p.a);
            renderer->Blit2DColoredPixel(
                static_cast<int>(screenX),
                static_cast<int>(screenY),
                displaySize,
                starColor
            );
        }
    }
}

// -------------------------------------------------------------------------------------------------------------
// NEW TEXT SCROLLER IMPLEMENTATION
// -------------------------------------------------------------------------------------------------------------
// CreateTextScrollerLTOR - Creates a Left to Right text scroller effect
// Text starts from left side with transparency, moves to center, holds, then continues to right side
// Parameters:
//   text - Text string to scroll
//   fontSize - Size of the font for rendering
//   textColor - Base color of the text (RGBA)
//   regionX, regionY - Position of the scroll region
//   regionWidth, regionHeight - Dimensions of the scroll region
//   scrollSpeed - Speed of scrolling movement
//   centerHoldTime - Time in seconds to hold text in center
//   duration - Total duration of the effect
// -------------------------------------------------------------------------------------------------------------
void FXManager::CreateTextScrollerLTOR(const std::wstring& text, const std::wstring& fontName, float fontSize, XMFLOAT4 textColor,
    float regionX, float regionY, float regionWidth, float regionHeight,
    float scrollSpeed, float centerHoldTime, float duration, float characterSpacing, float wordSpacing)
{
#if defined(_DEBUG_FXMANAGER_)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"[FXManager] CreateTextScrollerLTOR() invoked.");
#endif

    // Use ThreadLockHelper for safe locking
    ThreadLockHelper lock(threadManager, "fxmanager_textscroller_lock", 1000);
    if (!lock.IsLocked()) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[FXManager] Failed to acquire lock for CreateTextScrollerLTOR");
        return;
    }

    // Create new text scroller FX item
    FXItem newFX;
    newFX.type = FXType::TextScroller;                              // Set effect type to TextScroller
    newFX.subtype = FXSubType::TXT_SCROLL_LTOR;                     // Set subtype to Left to Right
    newFX.fxID = static_cast<int>(effects.size()) + 1;             // Generate unique FX ID
    newFX.duration = duration;                                      // Set total effect duration
    newFX.timeout = duration + 1.0f;                               // Set timeout slightly longer than duration
    newFX.progress = 0.0f;                                          // Initialize progress to zero

    // Initialize text scroll data structure
    newFX.textScrollData.text = text;                               // Store the text to scroll
    newFX.textScrollData.fontSize = fontSize;                       // Store font size
    newFX.textScrollData.textColor = textColor;                     // Store text color
    newFX.textScrollData.scrollSpeed = scrollSpeed;                 // Store scroll speed
    newFX.textScrollData.centerHoldTime = centerHoldTime;           // Store center hold time
    newFX.textScrollData.centerHoldTimer = 0.0f;                    // Initialize center hold timer
    newFX.textScrollData.regionX = regionX;                         // Store region position X
    newFX.textScrollData.regionY = regionY;                         // Store region position Y
    newFX.textScrollData.regionWidth = regionWidth;                 // Store region width
    newFX.textScrollData.regionHeight = regionHeight;               // Store region height
    newFX.textScrollData.currentXPosition = regionX - 100.0f;       // Start position (off-screen left)
    newFX.textScrollData.currentYPosition = regionY + (regionHeight / 2.0f); // Center vertically
    newFX.textScrollData.isInCenterPhase = false;                   // Not in center phase initially
    newFX.textScrollData.hasReachedCenter = false;                  // Has not reached center yet

    // Set start time and last update time
    newFX.startTime = std::chrono::steady_clock::now();             // Record start time
    newFX.lastUpdate = newFX.startTime;                             // Initialize last update time

    // Add effect to the effects vector
    effects.push_back(newFX);

#if defined(_DEBUG_FXMANAGER_)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[FXManager] TextScrollerLTOR created: Text='%s', FXID=%d, Region=(%.1f,%.1f,%.1f,%.1f)",
        text.c_str(), newFX.fxID, regionX, regionY, regionWidth, regionHeight);
#endif
}

// -------------------------------------------------------------------------------------------------------------
// CreateTextScrollerRTOL - Creates a Right to Left text scroller effect
// Text starts from right side with transparency, moves to center, holds, then continues to left side
// Parameters: Same as CreateTextScrollerLTOR but movement direction is reversed
// -------------------------------------------------------------------------------------------------------------
void FXManager::CreateTextScrollerRTOL(const std::wstring& text, const std::wstring& fontName, float fontSize, XMFLOAT4 textColor,
    float regionX, float regionY, float regionWidth, float regionHeight,
    float scrollSpeed, float centerHoldTime, float duration, float characterSpacing, float wordSpacing)
{
#if defined(_DEBUG_FXMANAGER_)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"[FXManager] CreateTextScrollerRTOL() invoked.");
#endif

    // Use ThreadLockHelper for safe locking
    ThreadLockHelper lock(threadManager, "fxmanager_textscroller_lock", 1000);
    if (!lock.IsLocked()) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[FXManager] Failed to acquire lock for CreateTextScrollerRTOL");
        return;
    }

    // Create new text scroller FX item
    FXItem newFX;
    newFX.type = FXType::TextScroller;                              // Set effect type to TextScroller
    newFX.subtype = FXSubType::TXT_SCROLL_RTOL;                     // Set subtype to Right to Left
    newFX.fxID = static_cast<int>(effects.size()) + 1;             // Generate unique FX ID
    newFX.duration = duration;                                      // Set total effect duration
    newFX.timeout = duration + 1.0f;                               // Set timeout slightly longer than duration
    newFX.progress = 0.0f;                                          // Initialize progress to zero

    // Initialize text scroll data structure
    newFX.textScrollData.text = text;                               // Store the text to scroll
    newFX.textScrollData.fontSize = fontSize;                       // Store font size
    newFX.textScrollData.textColor = textColor;                     // Store text color
    newFX.textScrollData.scrollSpeed = scrollSpeed;                 // Store scroll speed
    newFX.textScrollData.centerHoldTime = centerHoldTime;           // Store center hold time
    newFX.textScrollData.centerHoldTimer = 0.0f;                    // Initialize center hold timer
    newFX.textScrollData.regionX = regionX;                         // Store region position X
    newFX.textScrollData.regionY = regionY;                         // Store region position Y
    newFX.textScrollData.regionWidth = regionWidth;                 // Store region width
    newFX.textScrollData.regionHeight = regionHeight;               // Store region height
    newFX.textScrollData.currentXPosition = regionX + regionWidth + 100.0f; // Start position (off-screen right)
    newFX.textScrollData.currentYPosition = regionY + (regionHeight / 2.0f); // Center vertically
    newFX.textScrollData.isInCenterPhase = false;                   // Not in center phase initially
    newFX.textScrollData.hasReachedCenter = false;                  // Has not reached center yet

    // Set start time and last update time
    newFX.startTime = std::chrono::steady_clock::now();             // Record start time
    newFX.lastUpdate = newFX.startTime;                             // Initialize last update time

    // Add effect to the effects vector
    effects.push_back(newFX);

#if defined(_DEBUG_FXMANAGER_)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[FXManager] TextScrollerRTOL created: Text='%s', FXID=%d, Region=(%.1f,%.1f,%.1f,%.1f)",
        text.c_str(), newFX.fxID, regionX, regionY, regionWidth, regionHeight);
#endif
}

// -------------------------------------------------------------------------------------------------------------
// CreateTextScrollerConsistent - Creates a consistent text scroller that moves from right to left continuously
// Each character fades in from right and fades out as it approaches left side
// Parameters:
//   text - Text string to scroll
//   fontName - Font family name to use for rendering (NEW)
//   fontSize - Size of the font for rendering
//   textColor - Base color of the text (RGBA)
//   regionX, regionY - Position of the scroll region
//   regionWidth, regionHeight - Dimensions of the scroll region
//   scrollSpeed - Speed of scrolling movement per frame
//   duration - Total duration of the effect (use FLT_MAX for infinite)
//   characterSpacing - Additional spacing between characters (NEW)
//   wordSpacing - Additional spacing between words (NEW)
// -------------------------------------------------------------------------------------------------------------
void FXManager::CreateTextScrollerConsistent(const std::wstring& text, const std::wstring& fontName, float fontSize, XMFLOAT4 textColor,
    float regionX, float regionY, float regionWidth, float regionHeight,
    float scrollSpeed, float duration, float characterSpacing, float wordSpacing)
{
#if defined(_DEBUG_FXMANAGER_)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"[FXManager] CreateTextScrollerConsistent() invoked.");
#endif

    // Use ThreadLockHelper for safe locking
    ThreadLockHelper lock(threadManager, "fxmanager_textscroller_lock", 1000);
    if (!lock.IsLocked()) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[FXManager] Failed to acquire lock for CreateTextScrollerConsistent");
        return;
    }

    // Create new text scroller FX item
    FXItem newFX;
    newFX.type = FXType::TextScroller;                              // Set effect type to TextScroller
    newFX.subtype = FXSubType::TXT_SCROLL_CONSISTANT;               // Set subtype to Consistent
    newFX.fxID = static_cast<int>(effects.size()) + 1;             // Generate unique FX ID
    newFX.duration = duration;                                      // Set total effect duration
    newFX.timeout = duration == FLT_MAX ? FLT_MAX : duration + 1.0f; // Set timeout based on duration
    newFX.progress = 0.0f;                                          // Initialize progress to zero

    // Initialize text scroll data structure
    newFX.textScrollData.text = text;                               // Store the text to scroll
    newFX.textScrollData.fontName = fontName;                       // Store font name for rendering (NEW)
    newFX.textScrollData.fontSize = fontSize;                       // Store font size
    newFX.textScrollData.textColor = textColor;                     // Store text color
    newFX.textScrollData.scrollSpeed = scrollSpeed;                 // Store scroll speed
    newFX.textScrollData.characterSpacing = characterSpacing;       // Store character spacing (NEW)
    newFX.textScrollData.wordSpacing = wordSpacing;                 // Store word spacing (NEW)
    newFX.textScrollData.regionX = regionX;                         // Store region position X
    newFX.textScrollData.regionY = regionY;                         // Store region position Y
    newFX.textScrollData.regionWidth = regionWidth;                 // Store region width
    newFX.textScrollData.regionHeight = regionHeight;               // Store region height
    newFX.textScrollData.currentXPosition = regionX + regionWidth;  // Start position (right side of region)
    newFX.textScrollData.currentYPosition = regionY + (regionHeight / 2.0f); // Center vertically

    // Set start time and last update time
    newFX.startTime = std::chrono::steady_clock::now();             // Record start time
    newFX.lastUpdate = newFX.startTime;                             // Initialize last update time

    // Add effect to the effects vector
    effects.push_back(newFX);

#if defined(_DEBUG_FXMANAGER_)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[FXManager] TextScrollerConsistent created: Text='%s', Font='%s', FXID=%d, Speed=%.2f",
        text.c_str(), fontName.c_str(), newFX.fxID, scrollSpeed);
#endif
}

// -------------------------------------------------------------------------------------------------------------
// CreateTextScrollerMovie - Creates a movie credits style text scroller
// Text lines move from bottom to top with transparency effects
// Parameters:
//   textLines - Vector of text lines to scroll
//   fontSize - Size of the font for rendering
//   textColor - Base color of the text (RGBA)
//   regionX, regionY - Position of the scroll region
//   regionWidth, regionHeight - Dimensions of the scroll region
//   scrollSpeed - Speed of scrolling movement
//   lineSpacing - Spacing between text lines
//   duration - Total duration of the effect
// -------------------------------------------------------------------------------------------------------------
void FXManager::CreateTextScrollerMovie(const std::vector<std::wstring>& textLines, const std::wstring& fontName, float fontSize, XMFLOAT4 textColor,
    float regionX, float regionY, float regionWidth, float regionHeight,
    float scrollSpeed, float lineSpacing, float duration, float characterSpacing, float wordSpacing)
{
#if defined(_DEBUG_FXMANAGER_)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"[FXManager] CreateTextScrollerMovie() invoked.");
#endif

    // Use ThreadLockHelper for safe locking
    ThreadLockHelper lock(threadManager, "fxmanager_textscroller_lock", 1000);
    if (!lock.IsLocked()) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[FXManager] Failed to acquire lock for CreateTextScrollerMovie");
        return;
    }

    // Create new text scroller FX item
    FXItem newFX;
    newFX.type = FXType::TextScroller;                              // Set effect type to TextScroller
    newFX.subtype = FXSubType::TXT_SCROLL_MOVIE;                    // Set subtype to Movie
    newFX.fxID = static_cast<int>(effects.size()) + 1;             // Generate unique FX ID
    newFX.duration = duration;                                      // Set total effect duration
    newFX.timeout = duration + 1.0f;                               // Set timeout slightly longer than duration
    newFX.progress = 0.0f;                                          // Initialize progress to zero

    // Initialize text scroll data structure
    newFX.textScrollData.textLines = textLines;                     // Store the text lines to scroll
    newFX.textScrollData.fontSize = fontSize;                       // Store font size
    newFX.textScrollData.textColor = textColor;                     // Store text color
    newFX.textScrollData.scrollSpeed = scrollSpeed;                 // Store scroll speed
    newFX.textScrollData.lineSpacing = lineSpacing;                 // Store line spacing
    newFX.textScrollData.regionX = regionX;                         // Store region position X
    newFX.textScrollData.regionY = regionY;                         // Store region position Y
    newFX.textScrollData.regionWidth = regionWidth;                 // Store region width
    newFX.textScrollData.regionHeight = regionHeight;               // Store region height
    newFX.textScrollData.currentYPosition = regionY + regionHeight; // Start position (bottom of region)
    newFX.textScrollData.currentLineIndex = 0;                      // Start with first line

    // Set start time and last update time
    newFX.startTime = std::chrono::steady_clock::now();             // Record start time
    newFX.lastUpdate = newFX.startTime;                             // Initialize last update time

    // Add effect to the effects vector
    effects.push_back(newFX);

#if defined(_DEBUG_FXMANAGER_)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[FXManager] TextScrollerMovie created: Lines=%zu, FXID=%d, LineSpacing=%.2f",
        textLines.size(), newFX.fxID, lineSpacing);
#endif
}

// -------------------------------------------------------------------------------------------------------------
// StopTextScroller - Stops a text scroller effect by its ID
// Parameters:
//   effectID - ID of the text scroller effect to stop
// -------------------------------------------------------------------------------------------------------------
void FXManager::StopTextScroller(int effectID)
{
#if defined(_DEBUG_FXMANAGER_)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"[FXManager] StopTextScroller() invoked for ID: " + std::to_wstring(effectID));
#endif

    // Use ThreadLockHelper for safe locking
    ThreadLockHelper lock(threadManager, "fxmanager_textscroller_lock", 1000);
    if (!lock.IsLocked()) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[FXManager] Failed to acquire lock for StopTextScroller");
        return;
    }

    // Find and remove the text scroller effect with the specified ID
    effects.erase(
        std::remove_if(effects.begin(), effects.end(), [effectID](const FXItem& fx) {
            return fx.type == FXType::TextScroller && fx.fxID == effectID;
            }),
        effects.end()
    );

#if defined(_DEBUG_FXMANAGER_)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"[FXManager] Text scroller effect with ID " + std::to_wstring(effectID) + L" stopped.");
#endif
}

// -------------------------------------------------------------------------------------------------------------
// PauseTextScroller - Pauses a text scroller effect by its ID
// Parameters:
//   effectID - ID of the text scroller effect to pause
// -------------------------------------------------------------------------------------------------------------
void FXManager::PauseTextScroller(int effectID)
{
#if defined(_DEBUG_FXMANAGER_)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"[FXManager] PauseTextScroller() invoked for ID: " + std::to_wstring(effectID));
#endif

    // Use ThreadLockHelper for safe locking
    ThreadLockHelper lock(threadManager, "fxmanager_textscroller_lock", 1000);
    if (!lock.IsLocked()) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[FXManager] Failed to acquire lock for PauseTextScroller");
        return;
    }

    // Find the text scroller effect and pause it
    for (auto& fx : effects) {
        if (fx.type == FXType::TextScroller && fx.fxID == effectID) {
            fx.isPaused = true;                                     // Set the paused flag
#if defined(_DEBUG_FXMANAGER_)
            debug.logLevelMessage(LogLevel::LOG_INFO, L"[FXManager] Text scroller with ID " + std::to_wstring(effectID) + L" paused.");
#endif
            return;
        }
    }

#if defined(_DEBUG_FXMANAGER_)
    debug.logLevelMessage(LogLevel::LOG_WARNING, L"[FXManager] Text scroller with ID " + std::to_wstring(effectID) + L" not found for pausing.");
#endif
}

// -------------------------------------------------------------------------------------------------------------
// ResumeTextScroller - Resumes a paused text scroller effect by its ID
// Parameters:
//   effectID - ID of the text scroller effect to resume
// -------------------------------------------------------------------------------------------------------------
void FXManager::ResumeTextScroller(int effectID)
{
#if defined(_DEBUG_FXMANAGER_)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"[FXManager] ResumeTextScroller() invoked for ID: " + std::to_wstring(effectID));
#endif

    // Use ThreadLockHelper for safe locking
    ThreadLockHelper lock(threadManager, "fxmanager_textscroller_lock", 1000);
    if (!lock.IsLocked()) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[FXManager] Failed to acquire lock for ResumeTextScroller");
        return;
    }

    // Find the text scroller effect and resume it
    for (auto& fx : effects) {
        if (fx.type == FXType::TextScroller && fx.fxID == effectID) {
            fx.isPaused = false;                                    // Clear the paused flag
            fx.lastUpdate = std::chrono::steady_clock::now();       // Reset last update to avoid time jump
#if defined(_DEBUG_FXMANAGER_)
            debug.logLevelMessage(LogLevel::LOG_INFO, L"[FXManager] Text scroller with ID " + std::to_wstring(effectID) + L" resumed.");
#endif
            return;
        }
    }

#if defined(_DEBUG_FXMANAGER_)
    debug.logLevelMessage(LogLevel::LOG_WARNING, L"[FXManager] Text scroller with ID " + std::to_wstring(effectID) + L" not found for resuming.");
#endif
}

// -------------------------------------------------------------------------------------------------------------
// UpdateTextScroller - Updates the position and state of a text scroller effect
// Parameters:
//   fxItem - Reference to the FX item to update
//   deltaTime - Time elapsed since last update in seconds
// -------------------------------------------------------------------------------------------------------------
void FXManager::UpdateTextScroller(FXItem& fxItem, float deltaTime)
{
    // Early exit if paused or not a text scroller
    if (fxItem.isPaused || fxItem.type != FXType::TextScroller) {
        return;
    }

    // Update based on the text scroller subtype
    switch (fxItem.subtype) {
    case FXSubType::TXT_SCROLL_LTOR:
    {
        if (!fxItem.textScrollData.widthCached) {
            fxItem.textScrollData.cachedTextWidth = renderer->CalculateTextWidth(
                fxItem.textScrollData.text, fxItem.textScrollData.fontSize, fxItem.textScrollData.regionWidth);
            fxItem.textScrollData.widthCached = true;
        }
        float centerX = fxItem.textScrollData.regionX + (fxItem.textScrollData.regionWidth / 2.0f);
        float textWidth = fxItem.textScrollData.cachedTextWidth;
        float textCenterX = centerX - (textWidth / 2.0f);      // Calculate center position for text

        if (!fxItem.textScrollData.hasReachedCenter) {
            // Moving toward center - start slow, speed up as approaching center
            float distanceToCenter = abs(fxItem.textScrollData.currentXPosition - textCenterX);
            float maxDistance = fxItem.textScrollData.regionWidth / 2.0f;
            float speedMultiplier = 1.0f + (1.0f - (distanceToCenter / maxDistance)) * 2.0f; // Speed increases as getting closer

            fxItem.textScrollData.currentXPosition += fxItem.textScrollData.scrollSpeed * speedMultiplier * deltaTime;

            // Check if reached center
            if (fxItem.textScrollData.currentXPosition >= textCenterX) {
                fxItem.textScrollData.currentXPosition = textCenterX;
                fxItem.textScrollData.hasReachedCenter = true;
                fxItem.textScrollData.isInCenterPhase = true;
                fxItem.textScrollData.centerHoldTimer = 0.0f;
            }
        }
        else if (fxItem.textScrollData.isInCenterPhase) {
            // Hold in center phase
            fxItem.textScrollData.centerHoldTimer += deltaTime;
            if (fxItem.textScrollData.centerHoldTimer >= fxItem.textScrollData.centerHoldTime) {
                fxItem.textScrollData.isInCenterPhase = false;  // Exit center phase
            }
        }
        else {
            // Moving away from center - start slow, speed up as leaving
            float distanceFromCenter = abs(fxItem.textScrollData.currentXPosition - textCenterX);
            float maxDistance = fxItem.textScrollData.regionWidth / 2.0f;
            float speedMultiplier = 1.0f + (distanceFromCenter / maxDistance) * 2.0f; // Speed increases as getting farther

            fxItem.textScrollData.currentXPosition += fxItem.textScrollData.scrollSpeed * speedMultiplier * deltaTime;

            // Check if completely off screen
            if (fxItem.textScrollData.currentXPosition > fxItem.textScrollData.regionX + fxItem.textScrollData.regionWidth + 100.0f) {
                fxItem.progress = 1.0f;                         // Mark as completed
            }
        }
        break;
    }

    case FXSubType::TXT_SCROLL_RTOL:
    {
        if (!fxItem.textScrollData.widthCached) {
            fxItem.textScrollData.cachedTextWidth = renderer->CalculateTextWidth(
                fxItem.textScrollData.text, fxItem.textScrollData.fontSize, fxItem.textScrollData.regionWidth);
            fxItem.textScrollData.widthCached = true;
        }
        float centerX = fxItem.textScrollData.regionX + (fxItem.textScrollData.regionWidth / 2.0f);
        float textWidth = fxItem.textScrollData.cachedTextWidth;
        float textCenterX = centerX - (textWidth / 2.0f);      // Calculate center position for text

        if (!fxItem.textScrollData.hasReachedCenter) {
            // Moving toward center - start slow, speed up as approaching center
            float distanceToCenter = abs(fxItem.textScrollData.currentXPosition - textCenterX);
            float maxDistance = fxItem.textScrollData.regionWidth / 2.0f;
            float speedMultiplier = 1.0f + (1.0f - (distanceToCenter / maxDistance)) * 2.0f; // Speed increases as getting closer

            fxItem.textScrollData.currentXPosition -= fxItem.textScrollData.scrollSpeed * speedMultiplier * deltaTime;

            // Check if reached center
            if (fxItem.textScrollData.currentXPosition <= textCenterX) {
                fxItem.textScrollData.currentXPosition = textCenterX;
                fxItem.textScrollData.hasReachedCenter = true;
                fxItem.textScrollData.isInCenterPhase = true;
                fxItem.textScrollData.centerHoldTimer = 0.0f;
            }
        }
        else if (fxItem.textScrollData.isInCenterPhase) {
            // Hold in center phase
            fxItem.textScrollData.centerHoldTimer += deltaTime;
            if (fxItem.textScrollData.centerHoldTimer >= fxItem.textScrollData.centerHoldTime) {
                fxItem.textScrollData.isInCenterPhase = false;  // Exit center phase
            }
        }
        else {
            // Moving away from center - start slow, speed up as leaving
            float distanceFromCenter = abs(fxItem.textScrollData.currentXPosition - textCenterX);
            float maxDistance = fxItem.textScrollData.regionWidth / 2.0f;
            float speedMultiplier = 1.0f + (distanceFromCenter / maxDistance) * 2.0f; // Speed increases as getting farther

            fxItem.textScrollData.currentXPosition -= fxItem.textScrollData.scrollSpeed * speedMultiplier * deltaTime;

            // Check if completely off screen
            if (fxItem.textScrollData.currentXPosition < fxItem.textScrollData.regionX - 100.0f) {
                fxItem.progress = 1.0f;                         // Mark as completed
            }
        }
        break;
    }

    case FXSubType::TXT_SCROLL_CONSISTANT:
    {
        if (!fxItem.textScrollData.widthCached) {
            const auto& text = fxItem.textScrollData.text;
            const auto& fontName = fxItem.textScrollData.fontName;
            const float fSize = fxItem.textScrollData.fontSize;
            const float cSpace = fxItem.textScrollData.characterSpacing;
            const float wSpace = fxItem.textScrollData.wordSpacing;

            fxItem.textScrollData.cachedCharWidths.resize(text.length());
            fxItem.textScrollData.cachedCharOffsets.resize(text.length());

            float offset = 0.0f;
            for (size_t i = 0; i < text.length(); ++i) {
                float w = renderer->GetCharacterWidth(text[i], fSize, fontName) + cSpace;
                if (text[i] == L' ') w += wSpace;
                fxItem.textScrollData.cachedCharOffsets[i] = offset;
                fxItem.textScrollData.cachedCharWidths[i] = w;
                offset += w;
            }
            fxItem.textScrollData.cachedTotalTextWidth = offset;
            fxItem.textScrollData.widthCached = true;
        }

        // CORRECTED: Consistent text scroller logic - continuous right to left movement
        fxItem.textScrollData.currentXPosition -= fxItem.textScrollData.scrollSpeed * deltaTime;

        float totalTextWidth = fxItem.textScrollData.cachedTotalTextWidth;

        // FIXED: If text has completely scrolled off the left side, wrap to right side
        if (fxItem.textScrollData.currentXPosition + totalTextWidth < fxItem.textScrollData.regionX) {
            fxItem.textScrollData.currentXPosition = fxItem.textScrollData.regionX + fxItem.textScrollData.regionWidth;
        }

        // Check for duration completion (if not infinite)
        if (fxItem.duration != FLT_MAX) {
            auto now = std::chrono::steady_clock::now();
            float elapsed = std::chrono::duration<float>(now - fxItem.startTime).count();
            if (elapsed >= fxItem.duration) {
                fxItem.progress = 1.0f;                         // Mark as completed
            }
        }
        break;
    }

    case FXSubType::TXT_SCROLL_MOVIE:
    {
        // Movie credits style scroller logic - vertical scrolling (keep existing implementation)
        fxItem.textScrollData.currentYPosition -= fxItem.textScrollData.scrollSpeed * deltaTime;

        // Check if all lines have scrolled off the top
        float totalHeight = fxItem.textScrollData.textLines.size() * fxItem.textScrollData.lineSpacing;
        if (fxItem.textScrollData.currentYPosition + totalHeight < fxItem.textScrollData.regionY) {
            fxItem.progress = 1.0f;                             // Mark as completed
        }
        break;
    }

    default:
        break;
    }

#if defined(_DEBUG_FXMANAGER_)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[FXManager] TextScroller FXID=%d updated: Pos=(%.2f,%.2f), Progress=%.2f",
        fxItem.fxID, fxItem.textScrollData.currentXPosition, fxItem.textScrollData.currentYPosition, fxItem.progress);
#endif
}

// -------------------------------------------------------------------------------------------------------------
// RenderTextScroller - Renders a text scroller effect
// Parameters:
//   fxItem - Reference to the FX item to render
// -------------------------------------------------------------------------------------------------------------
void FXManager::RenderTextScroller(FXItem& fxItem)
{
    // Early exit if not a text scroller or if paused and shouldn't render
    if (fxItem.type != FXType::TextScroller) {
        return;
    }

    // Render based on the text scroller subtype
    switch (fxItem.subtype) {
    case FXSubType::TXT_SCROLL_LTOR:
    case FXSubType::TXT_SCROLL_RTOL:
    {
        // Calculate transparency based on position for LTOR/RTOL scrollers (keep existing implementation)
        float transparency = 1.0f;                              // Default to fully opaque

        if (!fxItem.textScrollData.isInCenterPhase) {
            // Calculate transparency based on distance from center
            float centerX = fxItem.textScrollData.regionX + (fxItem.textScrollData.regionWidth / 2.0f);
            float distanceFromCenter = abs(fxItem.textScrollData.currentXPosition - centerX);
            float fadeDistance = fxItem.textScrollData.regionWidth / 4.0f; // 25% of region width for fade

            if (distanceFromCenter > fadeDistance) {
                transparency = std::max(0.0f, 1.0f - ((distanceFromCenter - fadeDistance) / fadeDistance));
            }
        }

        // Apply transparency to text color
        XMFLOAT4 renderColor = fxItem.textScrollData.textColor;
        renderColor.w *= transparency;                          // Multiply alpha by transparency

        // Convert float (0.0-1.0) to uint8_t (0-255) for MyColor
        uint8_t colorR = static_cast<uint8_t>(renderColor.x * 255.0f);
        uint8_t colorG = static_cast<uint8_t>(renderColor.y * 255.0f);
        uint8_t colorB = static_cast<uint8_t>(renderColor.z * 255.0f);
        uint8_t colorA = static_cast<uint8_t>(renderColor.w * 255.0f);
        MyColor color(colorR, colorG, colorB, colorA);

        // Render the text using the DX11 renderer
        Vector2 position(fxItem.textScrollData.currentXPosition, fxItem.textScrollData.currentYPosition);
        renderer->DrawMyText(fxItem.textScrollData.text, position, color, fxItem.textScrollData.fontSize);
        break;
    }

    case FXSubType::TXT_SCROLL_CONSISTANT:
    {
        const float baseX = fxItem.textScrollData.currentXPosition;
        const float baseY = fxItem.textScrollData.currentYPosition;
        const float regionLeft  = fxItem.textScrollData.regionX;
        const float regionRight = regionLeft + fxItem.textScrollData.regionWidth;
        const float fadeDistance = 100.0f;
        const float cullLeft  = regionLeft  - fadeDistance - 50.0f;
        const float cullRight = regionRight + fadeDistance + 50.0f;

        const auto& text      = fxItem.textScrollData.text;
        const auto& offsets   = fxItem.textScrollData.cachedCharOffsets;
        const auto& widths    = fxItem.textScrollData.cachedCharWidths;
        const float fontSize  = fxItem.textScrollData.fontSize;
        const auto& fontName  = fxItem.textScrollData.fontName;
        const XMFLOAT4& baseColor = fxItem.textScrollData.textColor;

        for (size_t i = 0; i < text.length(); ++i) {
            float charX = baseX + offsets[i];

            // Cull characters that are fully outside the extended region
            if (charX < cullLeft || charX > cullRight) continue;

            float charWidth   = widths[i];
            float charCenterX = charX + (charWidth * 0.5f);

            float transparency = CalculateCharacterTransparency(charCenterX, regionLeft, regionRight, fadeDistance);
            if (transparency <= 0.01f) continue;

            XMFLOAT4 renderColor = baseColor;
            renderColor.w *= transparency;

            MyColor color(
                static_cast<uint8_t>(renderColor.x * 255.0f),
                static_cast<uint8_t>(renderColor.y * 255.0f),
                static_cast<uint8_t>(renderColor.z * 255.0f),
                static_cast<uint8_t>(renderColor.w * 255.0f));

            Vector2 position(charX, baseY);
            renderer->DrawMyTextWithFont(std::wstring(1, text[i]), position, color, fontSize, fontName);
        }
        break;
    }

    case FXSubType::TXT_SCROLL_MOVIE:
    {
        // Build per-line width cache once
        if (!fxItem.textScrollData.widthCached) {
            const auto& lines = fxItem.textScrollData.textLines;
            fxItem.textScrollData.cachedLineWidths.resize(lines.size());
            for (size_t i = 0; i < lines.size(); ++i) {
                fxItem.textScrollData.cachedLineWidths[i] = renderer->CalculateTextWidth(
                    lines[i], fxItem.textScrollData.fontSize, fxItem.textScrollData.regionWidth);
            }
            fxItem.textScrollData.widthCached = true;
        }

        const float lineY      = fxItem.textScrollData.currentYPosition;
        const float regionTop  = fxItem.textScrollData.regionY;
        const float regionBot  = regionTop + fxItem.textScrollData.regionHeight;
        const float regionX    = fxItem.textScrollData.regionX;
        const float regionW    = fxItem.textScrollData.regionWidth;
        const float fontSize   = fxItem.textScrollData.fontSize;
        const XMFLOAT4& baseColor = fxItem.textScrollData.textColor;

        for (size_t i = 0; i < fxItem.textScrollData.textLines.size(); ++i) {
            float currentLineY = lineY + (i * fxItem.textScrollData.lineSpacing);

            float transparency = CalculateTextTransparency(currentLineY, regionTop, regionBot, 50.0f);
            if (transparency <= 0.0f) continue;

            XMFLOAT4 renderColor = baseColor;
            renderColor.w *= transparency;

            MyColor color(
                static_cast<uint8_t>(renderColor.x * 255.0f),
                static_cast<uint8_t>(renderColor.y * 255.0f),
                static_cast<uint8_t>(renderColor.z * 255.0f),
                static_cast<uint8_t>(renderColor.w * 255.0f));

            float centeredX = regionX + (regionW - fxItem.textScrollData.cachedLineWidths[i]) * 0.5f;
            Vector2 position(centeredX, currentLineY);
            renderer->DrawMyText(fxItem.textScrollData.textLines[i], position, color, fontSize);
        }
        break;
    }

    default:
        break;
    }
}

// -------------------------------------------------------------------------------------------------------------
// CalculateTextTransparency - Helper function to calculate transparency based on position within region
// Used for fade in/out effects at region boundaries
// Parameters:
//   position - Current position of the text element
//   regionStart - Start boundary of the region
//   regionEnd - End boundary of the region
//   fadeDistance - Distance over which to apply fade effect
// Returns:
//   float - Transparency value between 0.0 and 1.0
// -------------------------------------------------------------------------------------------------------------
float FXManager::CalculateTextTransparency(float position, float regionStart, float regionEnd, float fadeDistance)
{
    // Check if position is completely outside the region
    if (position < regionStart - fadeDistance || position > regionEnd + fadeDistance) {
        return 0.0f;                                                // Completely transparent
    }

    // Calculate fade in from bottom
    if (position < regionStart) {
        float distanceFromStart = regionStart - position;
        return 1.0f - (distanceFromStart / fadeDistance);
    }

    // Calculate fade out at top
    if (position > regionEnd) {
        float distanceFromEnd = position - regionEnd;
        return 1.0f - (distanceFromEnd / fadeDistance);
    }

    // Within the main region - fully opaque
    return 1.0f;
}

// -------------------------------------------------------------------------------------------------------------
// CalculateCharacterTransparency - Helper function to calculate transparency for individual characters
// Used for consistent scroller character-by-character fade effects
// Parameters:
//   charPosition - Current position of the character
//   regionStart - Start boundary of the region
//   regionEnd - End boundary of the region
//   fadeDistance - Distance over which to apply fade effect
// Returns:
//   float - Transparency value between 0.0 and 1.0
// -------------------------------------------------------------------------------------------------------------
float FXManager::CalculateCharacterTransparency(float charPosition, float regionStart, float regionEnd, float fadeDistance)
{
    // Check if character is completely outside the visible region with fade zones
    if (charPosition < regionStart - fadeDistance || charPosition > regionEnd + fadeDistance) {
        return 0.0f;                                                // Completely transparent - outside fade zones
    }

    // CORRECTED: Fade in from right side (character entering the region from right)
    if (charPosition > regionEnd) {
        float distanceFromEnd = charPosition - regionEnd;                   // Distance beyond right edge
        float transparency = 1.0f - (distanceFromEnd / fadeDistance);       // Fade in as distance decreases
        return std::max(0.0f, std::min(1.0f, transparency));                // Clamp to valid range
    }

    // CORRECTED: Fade out at left side (character leaving the region to left)
    if (charPosition < regionStart) {
        float distanceFromStart = regionStart - charPosition;               // Distance beyond left edge
        float transparency = 1.0f - (distanceFromStart / fadeDistance);     // Fade out as distance increases
        return std::max(0.0f, std::min(1.0f, transparency));                // Clamp to valid range
    }

    // Character is within the main visible region
    float regionWidth = regionEnd - regionStart;                            // Calculate total region width
    float positionInRegion = (charPosition - regionStart) / regionWidth;    // Normalize position (0.0 to 1.0)

    // IMPROVED: Apply smooth edge fading for better visual effect
    const float edgeFadePercent = 0.25f;                                    // 25% fade zone on each edge

    if (positionInRegion < edgeFadePercent) {
        // Fade in from left edge of visible region
        float edgeTransparency = positionInRegion / edgeFadePercent;        // 0.0 at edge, 1.0 at fade boundary
        return std::max(0.0f, std::min(1.0f, edgeTransparency));            // Clamp to valid range
    }

    if (positionInRegion > (1.0f - edgeFadePercent)) {
        // Fade out at right edge of visible region
        float distanceFromRightEdge = 1.0f - positionInRegion;              // Distance from right edge
        float edgeTransparency = distanceFromRightEdge / edgeFadePercent;   // 1.0 at fade boundary, 0.0 at edge
        return std::max(0.0f, std::min(1.0f, edgeTransparency));            // Clamp to valid range
    }

    // Center region - fully opaque
    return 1.0f;
}

// -------------------------------------------------------------------------------------------------------------
// CalculateTextWidthWithSpacing - Helper function to calculate text width including character and word spacing
// Used for consistent scroller to determine total text width for wrapping calculations
// Parameters:
//   text - Text string to measure
//   fontName - Font family name to use for measurements
//   fontSize - Font size for measurements
//   characterSpacing - Additional spacing between characters
//   wordSpacing - Additional spacing between words
// Returns:
//   float - Total width of the text including all spacing
// -------------------------------------------------------------------------------------------------------------
float FXManager::CalculateTextWidthWithSpacing(const std::wstring& text, const std::wstring& fontName,
    float fontSize, float characterSpacing, float wordSpacing)
{
    float totalWidth = 0.0f;                                                // Initialize total width accumulator

    // Calculate width character by character including spacing
    for (size_t i = 0; i < text.length(); ++i) {
        wchar_t character = text[i];                                        // Get current character

        // Get base character width using specified font
        float charWidth = renderer->GetCharacterWidth(character, fontSize, fontName);

        // Add character spacing
        charWidth += characterSpacing;

        // Add additional word spacing for space characters
        if (character == L' ') {
            charWidth += wordSpacing;                                       // Add extra spacing for word separation
        }

        // Accumulate total width
        totalWidth += charWidth;
    }

    // Return the total calculated width
    return totalWidth;
}

// -------------------------------------------------------------------------------------------------------------
// SplitTextIntoLines - Helper function to split text into lines for movie scroller
// Parameters:
//   text - Input text string to split
//   lines - Output vector to store the split lines
//   maxWidth - Maximum width allowed per line
//   fontSize - Font size for width calculations
// -------------------------------------------------------------------------------------------------------------
void FXManager::SplitTextIntoLines(const std::wstring& text, std::vector<std::wstring>& lines, float maxWidth, float fontSize)
{
    lines.clear();                                                          // Clear existing lines

    std::wstringstream ss(text);                                            // Create string stream for processing
    std::wstring word;
    std::wstring currentLine;

    // Process text word by word
    while (std::getline(ss, word, L' ')) {                                  // Split by spaces
        std::wstring testLine = currentLine.empty() ? word : currentLine + L" " + word;

        // Check if adding this word exceeds the maximum width
        float lineWidth = 0.0f;
        lineWidth = renderer->CalculateTextWidth(testLine, fontSize, 1000.0f);

        if (lineWidth > maxWidth && !currentLine.empty()) {
            // Adding this word would exceed max width, so finalize current line
            lines.push_back(currentLine);
            currentLine = word;                                             // Start new line with current word
        }
        else {
            currentLine = testLine;                                 // Add word to current line
        }
    }

    // Add the last line if it has content
    if (!currentLine.empty()) {
        lines.push_back(currentLine);
    }
}

// ============================================================================================================
// WarpDotTunnel Implementation
// ============================================================================================================

void FXManager::StopAllFX()
{
    // Drain any in-flight Render() call before touching effects — prevents use-after-free
    // on the effects vector when the render thread is mid-iteration without the mutex.
    {
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
        while (bIsRendering.load() && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    if (starfieldID > 0) StopStarfield();
    if (tunnelID    > 0) StopWarpDotTunnel();
    SafelyClearAllEffects();
    debug.logLevelMessage(LogLevel::LOG_INFO, L"[FXManager] StopAllFX: all effects cleared.");
}

void FXManager::SaveAndSuspendFXForScene()
{
    // Drain any in-flight Render() call before touching effects — same pattern as
    // StopAllFX/RestoreFXAfterScene (see history Change 5). Without this drain,
    // SafelyClearAllEffects() can swap the vector while the render thread holds
    // iterators into it, causing use-after-free / lock state.
    {
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
        while (bIsRendering.load() && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    std::lock_guard<std::mutex> lock(m_effectsMutex);

    // Guard against double-save without a restore in between
    if (!m_sceneSavedEffects.empty() || m_sceneSavedStarfieldID > 0)
    {
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"[FXManager] SaveAndSuspendFXForScene: previous save not yet restored — overwriting.");
        m_sceneSavedEffects.clear();
    }

    // Snapshot the entire effects list plus the named IDs
    m_sceneSavedEffects       = effects;
    m_sceneSavedStarfieldID   = starfieldID;
    m_sceneSavedTunnelID      = tunnelID;

    // Clear everything so the experimental scene starts with a blank slate
    SafelyClearAllEffects();
    starfieldID = 0;
    tunnelID    = 0;

    debug.logLevelMessage(LogLevel::LOG_INFO, L"[FXManager] Scene FX state saved ("
        + std::to_wstring(m_sceneSavedEffects.size()) + L" effects). Scene suspended.");
}

void FXManager::RestoreFXAfterScene()
{
    // Drain any in-flight Render() call. StopAllFX() should have already emptied effects,
    // so Render() will exit fast on the empty-check — but wait anyway before the move-assign.
    {
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
        while (bIsRendering.load() && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    std::lock_guard<std::mutex> lock(m_effectsMutex);

    if (m_sceneSavedEffects.empty() && m_sceneSavedStarfieldID == 0)
    {
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"[FXManager] RestoreFXAfterScene: nothing saved to restore.");
        return;
    }

    // Stop whatever the experimental scene started
    if (tunnelID > 0) StopWarpDotTunnel();
    SafelyClearAllEffects();

    // Restore the full snapshot
    effects     = std::move(m_sceneSavedEffects);
    starfieldID = m_sceneSavedStarfieldID;
    tunnelID    = m_sceneSavedTunnelID;

    // Clear the snapshot slots
    m_sceneSavedEffects.clear();
    m_sceneSavedStarfieldID = 0;
    m_sceneSavedTunnelID    = 0;

    debug.logLevelMessage(LogLevel::LOG_INFO, L"[FXManager] Scene FX state restored ("
        + std::to_wstring(effects.size()) + L" effects).");
}

void FXManager::DiscardSavedFXState()
{
    std::lock_guard<std::mutex> lock(m_effectsMutex);
    m_sceneSavedEffects.clear();
    m_sceneSavedStarfieldID = 0;
    m_sceneSavedTunnelID    = 0;
    debug.logLevelMessage(LogLevel::LOG_INFO, L"[FXManager] Saved FX state discarded (full scene reload will recreate effects).");
}

void FXManager::Init3DWarpDOTTunnel(float x, float y, float z,
                                    float minRadius, float maxRadius,
                                    TunnelSpinCycle spinCycle,
                                    int travelSpeed, bool reverseTravel,
                                    int dotsPerCircle, int density)
{
    std::lock_guard<std::mutex> lock(m_effectsMutex);

    if (tunnelID > 0)
        StopWarpDotTunnel();

    FXItem newFX;
    newFX.type       = FXType::WarpDotTunnel;
    newFX.fxID       = static_cast<int>(effects.size()) + 1;
    newFX.duration   = FLT_MAX;
    newFX.timeout    = FLT_MAX;
    newFX.progress   = 0.0f;
    newFX.startTime  = std::chrono::steady_clock::now();
    newFX.lastUpdate = newFX.startTime;
    tunnelID         = newFX.fxID;

    WarpTunnelData& data = newFX.warpTunnelData;
    data.startX        = x;
    data.startY        = y;
    data.startZ        = z;
    data.minRadius     = minRadius;
    data.maxRadius     = maxRadius;
    data.spinCycle     = spinCycle;
    data.travelSpeed   = std::max(1, travelSpeed);
    data.reverseTravel = reverseTravel;
    data.dotsPerCircle = std::max(3, dotsPerCircle);
    data.density       = std::clamp(density, 1, 100);
    data.totalDistance  = 800.0f;
    data.nearZ          = z;
    data.farZ           = z + data.totalDistance;
    data.spinSpeed      = static_cast<float>(travelSpeed) * 0.05f;
    data.smoothLookTarget = XMFLOAT3(x, y, data.farZ);

    int ringCount = data.density;
    data.rings.reserve(ringCount);
    for (int i = 0; i < ringCount; ++i)
    {
        TunnelRing ring{};
        float fraction = static_cast<float>(i) / static_cast<float>(ringCount);

        // Stagger rings evenly along the tunnel so density is uniform from frame 1
        ring.zPos = reverseTravel
            ? (data.nearZ + fraction * data.totalDistance)
            : (data.farZ  - fraction * data.totalDistance);

        // Stagger initial birth positions around the full circle (sin/cos) so the
        // tunnel already looks like it's weaving from the very first frame.
        ring.bornCx    = x + WarpTunnelData::kSideWaveRadius * sinf(fraction * XM_2PI);
        ring.bornCy    = y + WarpTunnelData::kSideWaveRadius * cosf(fraction * XM_2PI);
        ring.cx        = ring.bornCx;
        ring.cy        = ring.bornCy;
        ring.spinAngle = 0.0f;
        ring.alive     = true;
        ring.colorStep = i % WarpTunnelData::kGraySteps;

        data.rings.push_back(ring);
    }

    // Place camera at tunnel entrance, aimed straight down the tunnel axis.
    // This fires once during initialisation (under the fade-to-black overlay).
    if (renderer)
    {
        renderer->myCamera.SetPosition(x, y, data.nearZ);
        renderer->myCamera.SetTarget(XMFLOAT3(x, y, data.farZ));
        renderer->myCamera.SetYawPitch(0.0f, 0.0f);
    }

    effects.push_back(newFX);

    debug.logLevelMessage(LogLevel::LOG_INFO, L"[FXManager] WarpDotTunnel created: Density=" +
        std::to_wstring(density) + L", DotsPerCircle=" + std::to_wstring(dotsPerCircle) +
        L", FXID=" + std::to_wstring(newFX.fxID));
}

void FXManager::StopWarpDotTunnel()
{
    if (tunnelID <= 0) return;

    effects.erase(
        std::remove_if(effects.begin(), effects.end(), [this](const FXItem& fx) {
            return fx.type == FXType::WarpDotTunnel && fx.fxID == tunnelID;
        }),
        effects.end()
    );

    debug.logLevelMessage(LogLevel::LOG_INFO, L"[FXManager] WarpDotTunnel stopped.");
    tunnelID = 0;
}

void FXManager::UpdateWarpDotTunnel(FXItem& fx, float deltaTime)
{
    WarpTunnelData& data = fx.warpTunnelData;
    if (data.rings.empty()) return;

    const float dt        = std::min(deltaTime, 0.05f);
    const float baseSpeed = static_cast<float>(data.travelSpeed);

    data.sideWaveTime += dt;

    for (auto& ring : data.rings)
    {
        if (!ring.alive) continue;

        float pathT = std::clamp((data.farZ - ring.zPos) / data.totalDistance, 0.0f, 1.0f);

        // Minimum factor 1.0 keeps all rings — even far ones — visibly moving.
        // Quartic surge near camera gives the explosive Doctor Who warp rush.
        float t2          = pathT * pathT;
        float speedFactor = data.reverseTravel
            ? (1.0f + t2 * t2 * 6.0f)
            : (1.0f + t2 * t2 * 10.0f);

        float frameSpeed = baseSpeed * speedFactor * dt;

        if (!data.reverseTravel)
            ring.zPos -= frameSpeed;
        else
            ring.zPos += frameSpeed;

        // On wrap: assign new birth position from the circular sway wave (sin X, cos Y).
        // The ring then travels STRAIGHT from that offset — no mid-flight drift.
        if (!data.reverseTravel && ring.zPos < data.nearZ)
        {
            ring.zPos   = data.farZ;
            float phase = data.sideWaveTime * WarpTunnelData::kSideWaveSpeed;
            ring.bornCx = data.startX + WarpTunnelData::kSideWaveRadius * sinf(phase);
            ring.bornCy = data.startY + WarpTunnelData::kSideWaveRadius * cosf(phase);
        }
        else if (data.reverseTravel && ring.zPos > data.farZ)
        {
            ring.zPos   = data.nearZ;
            float phase = data.sideWaveTime * WarpTunnelData::kSideWaveSpeed;
            ring.bornCx = data.startX + WarpTunnelData::kSideWaveRadius * sinf(phase);
            ring.bornCy = data.startY + WarpTunnelData::kSideWaveRadius * cosf(phase);
        }

        // Straight-line travel: cx/cy never change after birth
        ring.cx = ring.bornCx;
        ring.cy = ring.bornCy;

        // Reverse travel inverts the perceived rotation direction; negate to keep
        // Clockwise/AntiClockwise consistent regardless of travel direction.
        const float spinDelta = (data.reverseTravel ? -data.spinSpeed : data.spinSpeed) * dt;
        switch (data.spinCycle)
        {
        case TunnelSpinCycle::Clockwise:
            ring.spinAngle += spinDelta;
            break;
        case TunnelSpinCycle::AntiClockwise:
            ring.spinAngle -= spinDelta;
            break;
        default:
            break;
        }
        ring.spinAngle = fmodf(ring.spinAngle, XM_2PI);
        if (ring.spinAngle < 0.0f) ring.spinAngle += XM_2PI;
    }

    // Camera look-ahead: sort rings nearest-first, aim at ring 20 positions ahead.
    // Target is exponentially smoothed so the camera glides to the new aim point
    // rather than snapping — alpha uses dt so the rate is framerate-independent.
    if (renderer)
    {
        const int ringCount = static_cast<int>(data.rings.size());
        const int lookIdx   = std::min(19, ringCount - 1);

        std::vector<int> order;
        order.reserve(ringCount);
        for (int ri = 0; ri < ringCount; ++ri) order.push_back(ri);
        std::sort(order.begin(), order.end(), [&data](int a, int b) {
            float ptA = (data.farZ - data.rings[a].zPos) / data.totalDistance;
            float ptB = (data.farZ - data.rings[b].zPos) / data.totalDistance;
            return ptA > ptB;
        });

        const TunnelRing& lookRing = data.rings[order[lookIdx]];

        // Exponential ease: covers ~95 % of distance in 1/kCameraSmooth seconds
        const float alpha = 1.0f - expf(-WarpTunnelData::kCameraSmooth * dt);
        data.smoothLookTarget.x += (lookRing.cx   - data.smoothLookTarget.x) * alpha;
        data.smoothLookTarget.y += (lookRing.cy   - data.smoothLookTarget.y) * alpha;
        data.smoothLookTarget.z += (lookRing.zPos - data.smoothLookTarget.z) * alpha;

        renderer->myCamera.SetTarget(data.smoothLookTarget);
    }
}

void FXManager::RenderWarpDotTunnel(FXItem& fx, ID3D11DeviceContext* context)
{
    const WarpTunnelData& data = fx.warpTunnelData;
    if (data.rings.empty() || !context) return;

    XMMATRIX viewProj = renderer->myCamera.GetViewMatrix() * renderer->myCamera.GetProjectionMatrix();

    const float angleStep   = XM_2PI / static_cast<float>(data.dotsPerCircle);
    const float halfW       = static_cast<float>(renderer->iOrigWidth)  * 0.5f;
    const float halfH       = static_cast<float>(renderer->iOrigHeight) * 0.5f;
    const float edgeFade    = 0.08f;

    // Sequential gray ramp: very dark → white, kGraySteps entries
    static constexpr float kGrayRamp[WarpTunnelData::kGraySteps] = {
        0.08f, 0.19f, 0.30f, 0.44f, 0.58f, 0.72f, 0.86f, 1.0f
    };

    for (const auto& ring : data.rings)
    {
        if (!ring.alive) continue;

        // Normalised progress: 0 = far, 1 = near camera
        float pathT = std::clamp((data.farZ - ring.zPos) / data.totalDistance, 0.0f, 1.0f);

        // Per-ring radius grows from minRadius (far) to maxRadius (near)
        float ringRadius = data.minRadius + (data.maxRadius - data.minRadius) * pathT;

        // Alpha: brief fade-in/out at the far and near edges to prevent popping
        float alpha = 1.0f;
        if (pathT < edgeFade)
            alpha = pathT / edgeFade;
        else if (pathT > 1.0f - edgeFade)
            alpha = (1.0f - pathT) / edgeFade;

        // Sequential gray from the ramp — each ring cycles independently from dark to white
        float gray = kGrayRamp[ring.colorStep % WarpTunnelData::kGraySteps];
        float r = gray;
        float g = gray;
        float b = gray;

        // Dot pixel size grows with depth proximity
        float dotSize = 1.0f + pathT * 3.0f;

        XMFLOAT4 dotColor(r, g, b, alpha);

        for (int i = 0; i < data.dotsPerCircle; ++i)
        {
            float dotAngle = angleStep * static_cast<float>(i) + ring.spinAngle;
            float sinA, cosA;
            FAST_MATH.FastSinCos(dotAngle, sinA, cosA);

            XMVECTOR worldPos = XMVectorSet(
                ring.cx + cosA * ringRadius,
                ring.cy + sinA * ringRadius,
                ring.zPos,
                1.0f
            );

            XMVECTOR proj = XMVector3TransformCoord(worldPos, viewProj);
            float ndcX = XMVectorGetX(proj);
            float ndcY = XMVectorGetY(proj);
            float ndcZ = XMVectorGetZ(proj);

            // Cull anything behind camera or outside NDC box
            if (ndcZ <= 0.0f || ndcZ > 1.0f) continue;
            if (ndcX < -1.0f || ndcX > 1.0f) continue;
            if (ndcY < -1.0f || ndcY > 1.0f) continue;

            float screenX = (ndcX + 1.0f) * halfW;
            float screenY = (1.0f - ndcY) * halfH;

            renderer->Blit2DColoredPixel(
                static_cast<int>(screenX),
                static_cast<int>(screenY),
                dotSize,
                dotColor
            );
        }
    }
}

#pragma warning(pop)

    #endif   // End of #if defined(_WIN32) || defined(_WIN64)
#endif       // End of #if defined(__USE_DIRECTX_11__)
