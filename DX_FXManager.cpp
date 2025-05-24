// FXManager.cpp
//#include "Constants.h"
#include "Includes.h"
#include "DX_FXManager.h"
#include "Debug.h"
#include "MathPrecalculation.h"
#include "ThreadManager.h"
#include "ThreadLockHelper.h"

#if defined(__USE_DIRECTX_11__)
#include "DX11Renderer.h"
#include "RendererMacros.h"
#include <d3d11.h>
#include <d3dcompiler.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")
#endif

extern Debug debug;
extern ThreadManager threadManager;

FXManager::FXManager() : originalBlendState(nullptr), fadeBlendState(nullptr), originalRenderTarget(nullptr),
fullscreenQuadVertexBuffer(nullptr), inputLayout(nullptr), vertexShader(nullptr), pixelShader(nullptr), bHasCleanedUp(false) {
}

FXManager::~FXManager() {
    CleanUp();
}

void FXManager::CleanUp()
{
    if (bHasCleanedUp) return;

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
    if (&renderer == nullptr) return;

    WithDX11Renderer([this](std::shared_ptr<DX11Renderer> dx11) {
        // Setup fullscreen quad rendering resources
        struct Vertex {
            XMFLOAT3 position;
            XMFLOAT2 texcoord;
        };

        Vertex quadVertices[] = {
            { XMFLOAT3(-1.0f, 1.0f, 0.0f), XMFLOAT2(0.0f, 0.0f) },
            { XMFLOAT3(1.0f, 1.0f, 0.0f), XMFLOAT2(1.0f, 0.0f) },
            { XMFLOAT3(-1.0f, -1.0f, 0.0f), XMFLOAT2(0.0f, 1.0f) },
            { XMFLOAT3(1.0f, -1.0f, 0.0f), XMFLOAT2(1.0f, 1.0f) }
        };

        // Create blend state for fade effects
        D3D11_BLEND_DESC blendDesc = {};
        blendDesc.RenderTarget[0].BlendEnable = TRUE;
        blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
        blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
        blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        HRESULT hr = dx11->m_d3dDevice->CreateBlendState(&blendDesc, &fadeBlendState);
        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"FXManager: Failed to create the Faders Blend State.");
            return;
        }

        D3D11_BUFFER_DESC vertexBufferDesc = {};
        vertexBufferDesc.Usage = D3D11_USAGE_DEFAULT;
        vertexBufferDesc.ByteWidth = sizeof(quadVertices);
        vertexBufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

        D3D11_SUBRESOURCE_DATA vertexData = {};
        vertexData.pSysMem = quadVertices;
        dx11->m_d3dDevice->CreateBuffer(&vertexBufferDesc, &vertexData, &fullscreenQuadVertexBuffer);

        // Load shaders
        LoadFadeShaders();

        // Create constant buffer
        D3D11_BUFFER_DESC cbDesc = {};
        cbDesc.Usage = D3D11_USAGE_DYNAMIC;
        cbDesc.ByteWidth = 64;
//        cbDesc.ByteWidth = sizeof(XMFLOAT4);
        cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        dx11->m_d3dDevice->CreateBuffer(&cbDesc, nullptr, &constantBuffer);
    });
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
    debug.logLevelMessage(LogLevel::LOG_INFO, L"[FXManager] Stopping all FX effects for resize operation");
#endif

    // Clear the saved state structure
    SecureZeroMemory(&savedFXState, sizeof(ActiveFXState));

    try {
        // Check and stop starfield effect
        if (starfieldID > 0) {
            savedFXState.starfieldActive = true;                                // Remember starfield was active
            savedFXState.starfieldID = starfieldID;                             // Save the starfield ID
            StopStarfield();                                                    // Stop the starfield effect
#if defined(_DEBUG_FXMANAGER_)
            debug.logLevelMessage(LogLevel::LOG_INFO, L"[FXManager] Starfield effect stopped for resize");
#endif
        }

        // Stop all text scroller effects
        // Note: This assumes we have a way to enumerate active text scrollers
        // You may need to add this functionality to FXManager if not already present
        for (int i = 1; i <= 10; ++i) {                                         // Check common text scroller IDs
            try {
                StopTextScroller(i);                                            // Stop text scroller if active
                savedFXState.textScrollerIDs.push_back(i);                      // Add to saved list
#if defined(_DEBUG_FXMANAGER_)
                debug.logLevelMessage(LogLevel::LOG_DEBUG, L"[FXManager] Text scroller ID " + std::to_wstring(i) + L" stopped");
#endif
            }
            catch (...) {
                // Text scroller with this ID wasn't active, continue
                continue;
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

        // Stop scroll effects for common textures
        BlitObj2DIndexType scrollTextures[] = {
            BlitObj2DIndexType::IMG_SCROLLBG1,
            BlitObj2DIndexType::IMG_SCROLLBG2,
            BlitObj2DIndexType::IMG_SCROLLBG3
        };

        for (auto textureIndex : scrollTextures) {
            try {
                StopScrollEffect(textureIndex);                                 // Stop scroll effect for this texture
                savedFXState.activeScrollTextures.push_back(textureIndex);      // Save texture index
#if defined(_DEBUG_FXMANAGER_)
                debug.logLevelMessage(LogLevel::LOG_DEBUG, L"[FXManager] Scroll effect stopped for texture " +
                    std::to_wstring(int(textureIndex)));
#endif
            }
            catch (...) {
                // Scroll effect wasn't active for this texture, continue
                continue;
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

        // Check for active fade effects
        if (IsFadeActive()) {
            savedFXState.fadeEffectActive = true;                               // Mark that fade was active
#if defined(_DEBUG_FXMANAGER_)
            debug.logLevelMessage(LogLevel::LOG_INFO, L"[FXManager] Fade effect was active during resize");
#endif
        }

#if defined(_DEBUG_FXMANAGER_)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"[FXManager] All FX effects successfully stopped for resize");
#endif
    }
    catch (const std::exception& e) {
#if defined(_DEBUG_FXMANAGER_)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[FXManager] Exception stopping FX effects for resize: " +
            std::wstring(e.what(), e.what() + strlen(e.what())));
#endif
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

void FXManager::LoadFadeShaders() 
{
    WithDX11Renderer([this](std::shared_ptr<DX11Renderer> dx11) 
    {
            const char* vsSource = R"(
        struct VS_INPUT {
            float3 position : POSITION;
            float2 texcoord : TEXCOORD;
        };
        struct VS_OUTPUT {
            float4 position : SV_POSITION;
            float2 texcoord : TEXCOORD;
        };
        VS_OUTPUT main(VS_INPUT input) {
            VS_OUTPUT output;
            output.position = float4(input.position, 1.0f);
            output.texcoord = input.texcoord;
            return output;
        })";

            const char* psSource = R"(
        cbuffer FadeColorBuffer : register(b0) {
            float4 fadeColor;
        };
        float4 main(float4 position : SV_POSITION, float2 texcoord : TEXCOORD) : SV_TARGET {
            return fadeColor;
        })";

        HRESULT hr = S_OK;
        ID3DBlob * vsBlob = nullptr;
        ID3DBlob * psBlob = nullptr;
        ID3DBlob * errorBlob = nullptr;

        hr = D3DCompile(vsSource, strlen(vsSource), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, &vsBlob, &errorBlob);
        if (FAILED(hr)) {
            if (errorBlob) {
                debug.logLevelMessage(LogLevel::LOG_ERROR, std::wstring(L"FXManager: Vertex Shader Compilation Failed: ") + std::wstring((wchar_t*)errorBlob->GetBufferPointer()));
                errorBlob->Release();
            }

            return;
        }

        hr = dx11->m_d3dDevice->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &vertexShader);
        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"FXManager: Failed to create vertex shader.");
            vsBlob->Release();
            return;
        }

        D3D11_INPUT_ELEMENT_DESC layout[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,   D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 12,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };

        hr = dx11->m_d3dDevice->CreateInputLayout(layout, ARRAYSIZE(layout), vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &inputLayout);
        vsBlob->Release();
        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"FXManager: Failed to create input layout.");
            return;
        }

        hr = D3DCompile(psSource, strlen(psSource), nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0, &psBlob, &errorBlob);
        if (FAILED(hr)) {
            if (errorBlob) {
                debug.logLevelMessage(LogLevel::LOG_ERROR, std::wstring(L"FXManager: Pixel Shader Compilation Failed: ") + std::wstring((wchar_t*)errorBlob->GetBufferPointer()));
                errorBlob->Release();
            }
            return;
        }

        hr = dx11->m_d3dDevice->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &pixelShader);
        psBlob->Release();
        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"FXManager Failed to create pixel shader.");
            return;
        }
        });

    debug.logLevelMessage(LogLevel::LOG_INFO, L"FXManager: Successfully compiled and loaded fade shaders.");
}

void FXManager::ApplyColorFader(FXItem& fxItem) {
    auto now = std::chrono::steady_clock::now();

    if (fxItem.lastUpdate.time_since_epoch().count() == 0)
        fxItem.lastUpdate = fxItem.startTime;

    float elapsedSinceLastUpdate = std::chrono::duration<float>(now - fxItem.lastUpdate).count();
    bool shouldUpdate = (elapsedSinceLastUpdate >= fxItem.delay);

    if (shouldUpdate) {
        fxItem.lastUpdate = now;
        fxItem.progress += fxItem.delay / fxItem.duration;
        if (fxItem.progress >= 1.0f) fxItem.progress = 1.0f;
    }

    float effectiveProgress = fxItem.progress;
    if (fxItem.subtype == FXSubType::FadeToBackground) {
        effectiveProgress = 1.0f - fxItem.progress;
    }

    XMFLOAT4 fadeColor = fxItem.targetColor;
    fadeColor.w = effectiveProgress;

    WithDX11Renderer([this](std::shared_ptr<DX11Renderer> dx11)
    {
       dx11->m_d3dContext->OMSetBlendState(fadeBlendState, nullptr, 0xffffffff);
       dx11->m_d3dContext->IASetInputLayout(inputLayout);
    });

    RenderFullScreenQuad(fadeColor);
}

void FXManager::SaveRenderState() {
    WithDX11Renderer([this](std::shared_ptr<DX11Renderer> dx11)
    {
        // Save blend state
        dx11->m_d3dContext->OMGetBlendState(&originalBlendState, nullptr, nullptr);

        // Save render target + depth-stencil view
        dx11->m_d3dContext->OMGetRenderTargets(1, &originalRenderTarget, &originalDepthStencilView);

        // Save viewport
        numViewports = 1;
        dx11->m_d3dContext->RSGetViewports(&numViewports, &originalViewport);

        // Save rasterizer state
        dx11->m_d3dContext->RSGetState(&originalRasterState);

        // Save depth-stencil state and ref
        dx11->m_d3dContext->OMGetDepthStencilState(&originalDepthStencilState, &originalStencilRef);
    });
}

void FXManager::RestoreRenderState() {
    WithDX11Renderer([this](std::shared_ptr<DX11Renderer> dx11)
    {
        // Restore blend state
        if (originalBlendState) {
            dx11->m_d3dContext->OMSetBlendState(originalBlendState, nullptr, 0xffffffff);
            originalBlendState->Release();
            originalBlendState = nullptr;
        }

        // Restore render targets
        if (originalRenderTarget || originalDepthStencilView) {
            dx11->m_d3dContext->OMSetRenderTargets(1, &originalRenderTarget, originalDepthStencilView);

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
            dx11->m_d3dContext->RSSetViewports(numViewports, &originalViewport);
            numViewports = 0;
        }

        // Restore rasterizer state
        if (originalRasterState) {
            dx11->m_d3dContext->RSSetState(originalRasterState);
            originalRasterState->Release();
            originalRasterState = nullptr;
        }

        // Restore depth-stencil state
        if (originalDepthStencilState) {
            dx11->m_d3dContext->OMSetDepthStencilState(originalDepthStencilState, originalStencilRef);
            originalDepthStencilState->Release();
            originalDepthStencilState = nullptr;
        }
    });
}

void FXManager::RemoveCompletedEffects() {
    auto now = std::chrono::steady_clock::now();                   // Get current time
    effects.erase(std::remove_if(effects.begin(), effects.end(), [now](const FXItem& fx) {
        // Check for timeout-based completion
        bool timedOut = std::chrono::duration<float>(now - fx.startTime).count() >= fx.timeout;

        // Check for progress-based completion
        bool progressCompleted = fx.progress >= 1.0f;

        // Special handling for text scrollers that should loop (consistent type)
        if (fx.type == FXType::TextScroller && fx.subtype == FXSubType::TXT_SCROLL_CONSISTANT) {
            // Only remove if duration is not infinite and timed out
            return fx.duration != FLT_MAX && timedOut;
        }

        // For all other effects, remove if timed out or progress completed
        return timedOut || progressCompleted;
        }), effects.end());
}

void FXManager::RenderFullScreenQuad(const XMFLOAT4& color) {
    WithDX11Renderer([this, color](std::shared_ptr<DX11Renderer> dx11)
    {
        D3D11_MAPPED_SUBRESOURCE mappedResource;
        dx11->m_d3dContext->Map(constantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
        *(XMFLOAT4*)mappedResource.pData = color;
        dx11->m_d3dContext->Unmap(constantBuffer, 0);

        UINT stride = sizeof(XMFLOAT3) + sizeof(XMFLOAT2);
        UINT offset = 0;
        dx11->m_d3dContext->IASetInputLayout(inputLayout);
        dx11->m_d3dContext->PSSetConstantBuffers(0, 1, &constantBuffer);
        dx11->m_d3dContext->IASetVertexBuffers(0, 1, &fullscreenQuadVertexBuffer, &stride, &offset);
        dx11->m_d3dContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

        dx11->m_d3dContext->VSSetShader(vertexShader, nullptr, 0);
        dx11->m_d3dContext->PSSetShader(pixelShader, nullptr, 0);

        dx11->m_d3dContext->Draw(4, 0);
    });
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

void FXManager::FadeOutThenCallback(XMFLOAT4 color, float duration, float delay, std::function<void()> callback) {
    FXItem fadeEffect;
    fadeEffect.type = FXType::ColorFader;
    fadeEffect.subtype = FXSubType::FadeToTargetColor;
    fadeEffect.duration = duration;
    fadeEffect.delay = delay;
    fadeEffect.timeout = duration + 1.0f;
    fadeEffect.progress = 0.0f;
    fadeEffect.targetColor = color;

    AddEffect(fadeEffect);
    pendingCallbacks.push_back({ fadeEffect, callback });
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

    WithDX11Renderer([this, fxItem, now, elapsed](std::shared_ptr<DX11Renderer> dx11)
    {
        if (fxItem.isPaused) 
        {
            // Still render to keep visual intact
            dx11->Blit2DWrappedObjectAtOffset(
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
        dx11->Blit2DWrappedObjectAtOffset(
            fxItem.textureIndex,
            0, 0,
            fxItem.currentXOffset,
            fxItem.currentYOffset,
            fxItem.tileWidth,
            fxItem.tileHeight
        );
    });

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
        WithDX11Renderer([&](std::shared_ptr<DX11Renderer> dx11) {
            dx11->Blit2DColoredPixel(static_cast<int>(p.x), static_cast<int>(p.y), 2.0f, finalColor);
            });

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
void FXManager::Render() {
    if ((&renderer == nullptr) || effects.empty() || threadManager.threadVars.bIsShuttingDown) return;

    bIsRendering = true;
    SaveRenderState();

    static auto lastRenderTime = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    float deltaTime = std::chrono::duration<float>(now - lastRenderTime).count();
    lastRenderTime = now;

    // Update our starfield effect (if any are active)
    for (auto& fx : effects) {
        if (fx.type == FXType::Starfield) {
            UpdateStarfield(deltaTime);
        }
    }

    for (auto& fx : effects)
    {
        switch (fx.type)
        {
        case FXType::ColorFader:
            ApplyColorFader(fx);
            break;

        default:
            break;
        }
    }

    for (auto it = pendingCallbacks.begin(); it != pendingCallbacks.end();)
    {
        if (it->first.progress >= 1.0f)
        {
            it->second();
            it = pendingCallbacks.erase(it);
        }
        else
        {
            ++it;
        }
    }

    RemoveCompletedEffects();
    RestoreRenderState();
    bIsRendering = false;
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
            // Update and render the starfield
            UpdateStarfield(deltaTime);
            RenderStarfield(fx, context, worldMatrix);
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
void FXManager::CreateStarfield(int numStars, float circularRadius, float resetDepthPos)
{
#if defined(_DEBUG_FXMANAGER_)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"[FXManager] CreateStarfield() invoked with " +
        std::to_wstring(numStars) + L" stars, radius " + std::to_wstring(circularRadius));
#endif

    std::lock_guard<std::mutex> lock(m_effectsMutex); // Add lock for thread safety

    // Create a new starfield FXItem
    FXItem newFX;
    newFX.type = FXType::Starfield;
    newFX.fxID = static_cast<int>(effects.size()) + 1;
    starfieldID = newFX.fxID;
    newFX.duration = FLT_MAX;  // Run indefinitely until stopped
    newFX.timeout = FLT_MAX;
    newFX.progress = 0.0f;

    // Store the parameters
    newFX.depthMultiplier = resetDepthPos; // Reuse this field to store the reset depth

    // Generate random stars
    for (int i = 0; i < numStars; ++i)
    {
        Particle p;

        // Generate a random position in 3D space
        // Use a cylindrical distribution for better visual effect
        float angle = static_cast<float>(rand()) / RAND_MAX * XM_2PI;
        float dist = (0.1f + (static_cast<float>(rand()) / RAND_MAX) * 0.9f) * circularRadius;

        // Set initial position
        p.x = cosf(angle) * dist;  // x position
        p.y = sinf(angle) * dist;  // y position
        p.angle = resetDepthPos * (0.1f + 0.9f * static_cast<float>(rand()) / RAND_MAX); // Use angle to store z position

        // Set star properties
        p.speed = 20.0f + static_cast<float>(rand()) / RAND_MAX * 40.0f; // Speed factor
        p.radius = 1.0f + static_cast<float>(rand()) / RAND_MAX * 2.0f;  // Star size
        p.maxRadius = resetDepthPos; // Store reset depth for reference

        // Set color (mostly white with slight variations)
        float brightness = 0.7f + static_cast<float>(rand()) / RAND_MAX * 0.3f;
        p.r = brightness;
        p.g = brightness * (0.85f + static_cast<float>(rand()) / RAND_MAX * 0.15f);
        p.b = brightness * (0.9f + static_cast<float>(rand()) / RAND_MAX * 0.1f);
        p.a = 1.0f;

        p.completed = false;
        p.hasLoggedCompletion = false;

        // Store deltas for smoother movement
        p.delayCount = 0;
        p.delayBase = static_cast<int>(p.angle); // Store the original z position

        newFX.particles.push_back(p);
    }

    // Set start time and last update time
    newFX.startTime = std::chrono::steady_clock::now();
    newFX.lastUpdate = newFX.startTime;

    effects.push_back(newFX);

#if defined(_DEBUG_FXMANAGER_)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[FXManager] Starfield created: Stars=%d, Radius=%.2f, ResetDepth=%.2f, FXID=%d",
        numStars, circularRadius, resetDepthPos, newFX.fxID);
#endif
}

void FXManager::UpdateStarfield(float deltaTime)
{
    // Starfield update - no need for mutex here as this is called from within a locked context

    for (auto& fx : effects)
    {
        if (fx.type != FXType::Starfield)
            continue;

        float resetDepth = fx.depthMultiplier; // This holds our reset depth value

        for (auto& p : fx.particles)
        {
            if (p.completed)
                continue;

            // Calculate stable movement based on deltaTime
            // Clamp deltaTime to avoid huge jumps if frame rate drops
            float clampedDelta = std::min(deltaTime, 0.1f);

            // Update z position (stored in angle field)
            float zPos = p.angle;
            zPos -= p.speed * clampedDelta; // Move toward camera

            // Adjust alpha based on distance from camera
            float distRatio = zPos / resetDepth;
            p.a = std::max(0.0f, std::min(1.0f, distRatio * 1.2f)); // Fade out as approaches

            // Check if star needs to be reset
            if (zPos <= 5.0f) // Reset when very close to camera
            {
                // Generate new random position
                float angle = static_cast<float>(rand()) / RAND_MAX * XM_2PI;
                float dist = (0.1f + (static_cast<float>(rand()) / RAND_MAX) * 0.9f) *
                    (resetDepth * 0.1f); // Smaller radius at distance

                float outCos, outSin;
                FAST_MATH.FastSinCos(angle, outSin, outCos);
                p.x = outCos * dist;
                p.y = outSin * dist;
                p.angle = resetDepth * (0.9f + 0.1f * static_cast<float>(rand()) / RAND_MAX);

                // Randomize properties slightly
                p.speed = 20.0f + static_cast<float>(rand()) / RAND_MAX * 40.0f;
                p.radius = 1.0f + static_cast<float>(rand()) / RAND_MAX * 1.2f;
                p.a = 1.0f;
            }
            else
            {
                // Update position
                p.angle = zPos;
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
    WithDX11Renderer([&](std::shared_ptr<DX11Renderer> dx11) {
        // Calculate view projection matrix
        XMMATRIX viewProj = dx11->myCamera.GetViewMatrix() * dx11->myCamera.GetProjectionMatrix();

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
                float screenX = (XMVectorGetX(projPos) + 1.0f) * 0.5f * dx11->iOrigWidth;
                float screenY = (1.0f - XMVectorGetY(projPos)) * 0.5f * dx11->iOrigHeight;

                // Calculate size based on z-position
                // Stars get larger as they get closer
                float sizeScale = 1.0f + (fxItem.depthMultiplier - p.angle) / fxItem.depthMultiplier * 3.0f;
                float displaySize = p.radius * sizeScale;

                // Draw the star
                XMFLOAT4 starColor(p.r, p.g, p.b, p.a);
                dx11->Blit2DColoredPixel(
                    static_cast<int>(screenX),
                    static_cast<int>(screenY),
                    displaySize,
                    starColor
                );
            }
        }
    });
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
        // Left to Right text scroller logic (keep existing implementation)
        float centerX = fxItem.textScrollData.regionX + (fxItem.textScrollData.regionWidth / 2.0f);
        float textWidth = 0.0f;                                 // Calculate text width using renderer

        // Get text width from renderer for proper centering
        WithDX11Renderer([&fxItem, &textWidth](std::shared_ptr<DX11Renderer> dx11) {
            textWidth = dx11->CalculateTextWidth(fxItem.textScrollData.text,
                fxItem.textScrollData.fontSize, fxItem.textScrollData.regionWidth);
            });

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
        // Right to Left text scroller logic (keep existing implementation)
        float centerX = fxItem.textScrollData.regionX + (fxItem.textScrollData.regionWidth / 2.0f);
        float textWidth = 0.0f;                                 // Calculate text width using renderer

        // Get text width from renderer for proper centering
        WithDX11Renderer([&fxItem, &textWidth](std::shared_ptr<DX11Renderer> dx11) {
            textWidth = dx11->CalculateTextWidth(fxItem.textScrollData.text,
                fxItem.textScrollData.fontSize, fxItem.textScrollData.regionWidth);
            });

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
        // CORRECTED: Consistent text scroller logic - continuous right to left movement
        fxItem.textScrollData.currentXPosition -= fxItem.textScrollData.scrollSpeed * deltaTime;

        // Calculate total text width to determine wrapping point
        float totalTextWidth = 0.0f;
        WithDX11Renderer([&fxItem, &totalTextWidth](std::shared_ptr<DX11Renderer> dx11) {
            totalTextWidth = dx11->CalculateTextWidth(fxItem.textScrollData.text,
                fxItem.textScrollData.fontSize, 9999.0f);       // Use large container to get actual text width
            });

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
        WithDX11Renderer([&fxItem, color](std::shared_ptr<DX11Renderer> dx11) {
            Vector2 position(fxItem.textScrollData.currentXPosition, fxItem.textScrollData.currentYPosition);
            dx11->DrawMyText(fxItem.textScrollData.text, position, color, fxItem.textScrollData.fontSize);
            });
        break;
    }

    case FXSubType::TXT_SCROLL_CONSISTANT:
    {
        // CORRECTED: Render consistent scroller with proper character-by-character transparency and spacing
        WithDX11Renderer([this, &fxItem](std::shared_ptr<DX11Renderer> dx11) {
            float currentCharX = fxItem.textScrollData.currentXPosition;        // Start position for rendering
            const float fadeDistance = 100.0f;                                 // Distance for fade in/out effects

            // Pre-calculate total text width for proper wrapping including spacing
            float totalTextWidth = CalculateTextWidthWithSpacing(fxItem.textScrollData.text,
                fxItem.textScrollData.fontName, fxItem.textScrollData.fontSize,
                fxItem.textScrollData.characterSpacing, fxItem.textScrollData.wordSpacing);

            // CORRECTED: Render each character individually with proper transparency calculation and spacing
            for (size_t i = 0; i < fxItem.textScrollData.text.length(); ++i) {
                wchar_t character = fxItem.textScrollData.text[i];

                // Calculate character width for proper positioning using specified font
                float charWidth = dx11->GetCharacterWidth(character, fxItem.textScrollData.fontSize, fxItem.textScrollData.fontName);

                // Apply character spacing
                charWidth += fxItem.textScrollData.characterSpacing;

                // Apply additional word spacing for space characters
                if (character == L' ') {
                    charWidth += fxItem.textScrollData.wordSpacing;             // Add extra spacing for word separation
                }

                // FIXED: Calculate transparency based on character center position
                float charCenterX = currentCharX + (charWidth / 2.0f);         // Use character center for transparency calc

                // Calculate transparency using the corrected function
                float transparency = CalculateCharacterTransparency(charCenterX,
                    fxItem.textScrollData.regionX,                             // Left boundary of visible region
                    fxItem.textScrollData.regionX + fxItem.textScrollData.regionWidth, // Right boundary of visible region
                    fadeDistance);                                             // Fade distance for smooth transitions

                // Only render if character has some visibility and is within reasonable bounds
                if (transparency > 0.01f &&                                   // Minimum visibility threshold
                    currentCharX > fxItem.textScrollData.regionX - fadeDistance - 50.0f &&  // Extended left boundary
                    currentCharX < fxItem.textScrollData.regionX + fxItem.textScrollData.regionWidth + fadeDistance + 50.0f) { // Extended right boundary

                    // CORRECTED: Convert float RGBA to uint8_t for MyColor constructor
                    XMFLOAT4 renderColor = fxItem.textScrollData.textColor;
                    renderColor.w *= transparency;                             // Multiply alpha by calculated transparency

                    // Convert float (0.0-1.0) to uint8_t (0-255) for MyColor
                    uint8_t colorR = static_cast<uint8_t>(renderColor.x * 255.0f);
                    uint8_t colorG = static_cast<uint8_t>(renderColor.y * 255.0f);
                    uint8_t colorB = static_cast<uint8_t>(renderColor.z * 255.0f);
                    uint8_t colorA = static_cast<uint8_t>(renderColor.w * 255.0f);

                    // Create render position and color objects with correct values
                    Vector2 position(currentCharX, fxItem.textScrollData.currentYPosition);
                    MyColor color(colorR, colorG, colorB, colorA);              // Use uint8_t values

                    // ENHANCED: Render character with specified font instead of default
                    dx11->DrawMyTextWithFont(std::wstring(1, character), position, color,
                        fxItem.textScrollData.fontSize, fxItem.textScrollData.fontName);

#if defined(_DEBUG_FXMANAGER_) && defined(_DEBUG)
                    // Debug output for character transparency (only for first few characters to avoid spam)
                    if (i < 5) {
                        debug.logDebugMessage(LogLevel::LOG_DEBUG,
                            L"[FXID=%d] Char='%c' Pos=%.1f CenterX=%.1f Trans=%.3f Width=%.1f",
                            fxItem.fxID, character, currentCharX, charCenterX, transparency, charWidth);
                    }
#endif
                }

                // FIXED: Advance to next character position with proper spacing
                currentCharX += charWidth;
            }
            });
        break;
    }

    case FXSubType::TXT_SCROLL_MOVIE:
    {
        // Render movie credits style scroller line by line (keep existing implementation)
        float lineY = fxItem.textScrollData.currentYPosition;

        WithDX11Renderer([this, &fxItem, lineY](std::shared_ptr<DX11Renderer> dx11) {
            for (size_t i = 0; i < fxItem.textScrollData.textLines.size(); ++i) {
                float currentLineY = lineY + (i * fxItem.textScrollData.lineSpacing);

                // Calculate transparency based on line position
                float transparency = CalculateTextTransparency(currentLineY,
                    fxItem.textScrollData.regionY,
                    fxItem.textScrollData.regionY + fxItem.textScrollData.regionHeight,
                    50.0f);                                     // Fade distance of 50 pixels

                // Only render if line is visible (transparency > 0)
                if (transparency > 0.0f) {
                    XMFLOAT4 renderColor = fxItem.textScrollData.textColor;
                    renderColor.w *= transparency;              // Apply transparency

                    // Convert float (0.0-1.0) to uint8_t (0-255) for MyColor
                    uint8_t colorR = static_cast<uint8_t>(renderColor.x * 255.0f);
                    uint8_t colorG = static_cast<uint8_t>(renderColor.y * 255.0f);
                    uint8_t colorB = static_cast<uint8_t>(renderColor.z * 255.0f);
                    uint8_t colorA = static_cast<uint8_t>(renderColor.w * 255.0f);
                    MyColor color(colorR, colorG, colorB, colorA);

                    // Center the text horizontally
                    float textWidth = dx11->CalculateTextWidth(fxItem.textScrollData.textLines[i],
                        fxItem.textScrollData.fontSize, fxItem.textScrollData.regionWidth);
                    float centeredX = fxItem.textScrollData.regionX +
                        (fxItem.textScrollData.regionWidth - textWidth) / 2.0f;

                    Vector2 position(centeredX, currentLineY);
                    dx11->DrawMyText(fxItem.textScrollData.textLines[i], position, color, fxItem.textScrollData.fontSize);
                }
            }
            });
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
        float distanceFromEnd = charPosition - regionEnd;           // Distance beyond right edge
        float transparency = 1.0f - (distanceFromEnd / fadeDistance); // Fade in as distance decreases
        return std::max(0.0f, std::min(1.0f, transparency));        // Clamp to valid range
    }

    // CORRECTED: Fade out at left side (character leaving the region to left)
    if (charPosition < regionStart) {
        float distanceFromStart = regionStart - charPosition;       // Distance beyond left edge
        float transparency = 1.0f - (distanceFromStart / fadeDistance); // Fade out as distance increases
        return std::max(0.0f, std::min(1.0f, transparency));        // Clamp to valid range
    }

    // Character is within the main visible region
    float regionWidth = regionEnd - regionStart;                    // Calculate total region width
    float positionInRegion = (charPosition - regionStart) / regionWidth; // Normalize position (0.0 to 1.0)

    // IMPROVED: Apply smooth edge fading for better visual effect
    const float edgeFadePercent = 0.15f;                            // 15% fade zone on each edge

    if (positionInRegion < edgeFadePercent) {
        // Fade in from left edge of visible region
        float edgeTransparency = positionInRegion / edgeFadePercent; // 0.0 at edge, 1.0 at fade boundary
        return std::max(0.0f, std::min(1.0f, edgeTransparency));    // Clamp to valid range
    }

    if (positionInRegion > (1.0f - edgeFadePercent)) {
        // Fade out at right edge of visible region
        float distanceFromRightEdge = 1.0f - positionInRegion;      // Distance from right edge
        float edgeTransparency = distanceFromRightEdge / edgeFadePercent; // 1.0 at fade boundary, 0.0 at edge
        return std::max(0.0f, std::min(1.0f, edgeTransparency));    // Clamp to valid range
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
    float totalWidth = 0.0f;                                                    // Initialize total width accumulator

    // Calculate width character by character including spacing
    WithDX11Renderer([&](std::shared_ptr<DX11Renderer> dx11) {
        for (size_t i = 0; i < text.length(); ++i) {
            wchar_t character = text[i];                                        // Get current character

            // Get base character width using specified font
            float charWidth = dx11->GetCharacterWidth(character, fontSize, fontName);

            // Add character spacing
            charWidth += characterSpacing;

            // Add additional word spacing for space characters
            if (character == L' ') {
                charWidth += wordSpacing;                                       // Add extra spacing for word separation
            }

            // Accumulate total width
            totalWidth += charWidth;
        }
        });

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
    lines.clear();                                                  // Clear existing lines

    std::wstringstream ss(text);                                    // Create string stream for processing
    std::wstring word;
    std::wstring currentLine;

    // Process text word by word
    while (std::getline(ss, word, L' ')) {                         // Split by spaces
        std::wstring testLine = currentLine.empty() ? word : currentLine + L" " + word;

        // Check if adding this word exceeds the maximum width
        float lineWidth = 0.0f;
        WithDX11Renderer([&testLine, fontSize, &lineWidth](std::shared_ptr<DX11Renderer> dx11) {
            lineWidth = dx11->CalculateTextWidth(testLine, fontSize, 1000.0f);
            });

        if (lineWidth > maxWidth && !currentLine.empty()) {
            // Adding this word would exceed max width, so finalize current line
            lines.push_back(currentLine);
            currentLine = word;                                     // Start new line with current word
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