/* -------------------------------------------------------------- */
// Main Tasking Thread for our DX12 renderer
//
// This function is the main (optional thread) function that will
// be used to render the scene. It will be responsible for rendering
// the scene, updating the scene, and handling any other rendering
// tasks.
//
// Pipeline order (DX12):
// 1) Initialize render, safety guards, acquire exclusive lock
// 2) Viewport calculation from client rect
// 3) Camera update + delta time  ← BEFORE fence wait (CPU-GPU overlap, mirrors Vulkan)
// 2) Wait for previous frame, reset command allocator + list
// 3.5) SCENE_GAMETITLE only: D2D background pre-pass — transition + clear is
//      executed, the background image, logo, starfield, and fireworks are
//      blitted via D2D, then the command list is re-opened so the 3D models
//      render ON TOP of all of these elements.  Composition order:
//      BACKGROUND → LOGO → STARFIELD → FIREWORKS → 3D MODELS → TSOO
// 4) Set root signature & heaps, viewport, scissor
// 5) TransitionResource PRESENT → RENDER_TARGET
// 6) Clear RT (skipped when the pre-pass drew the background) + DSV
// 7) Update + bind constant buffers
// 8) Scene-specific 3D rendering (RenderGamePlay)
// 9) Close + execute 3D command list (back buffer left in RENDER_TARGET)
// 10) Single D2D pass: loading images, FX, overlays, text, FPS, cursor
//     (bLoaderTaskFinished cached once as bLoaderDone; fireworks already drawn
//      in step 3.5 so only TSOO blit remains for SCENE_GAMETITLE post-3D)
// 11) Release wrapped resources + dx11Context::Flush() (transition to PRESENT)
// 12) Present frame via PresentFrame() + MoveToNextFrame() — no software cap
//     (VSync=on: DXGI paces to refresh rate; VSync=off: uncapped, matches Vulkan)

/* ----------------------------------------------------------------
   DO NOT INCLUDE THIS FILE!!! THE PROJECT ITSELF SCOPES THIS FILE!
/* ---------------------------------------------------------------- */
#include "Includes.h"

#if defined(__USE_DIRECTX_12__)
#include "DX12Renderer.h"
#include "DX12RenderFrame.h"
#include "BuildInfo.h"
#include "Debug.h"
#include "ExceptionHandler.h"
#include "WinSystem.h"
#include "Configuration.h"
#include "DX12FXManager.h"
#include "GUIManager.h"
#include "ConsoleWindow.h"
#include "Models.h"
#include "Lights.h"
#include "SceneManager.h"
#include "ShaderManager.h"
#include "MoviePlayer.h"
#include "ScreenRecorder.h"

extern HWND hwnd;
extern HINSTANCE hInst;
extern GUIManager guiManager;
extern ConsoleWindow consoleWindow;
extern Debug debug;
extern ExceptionHandler exceptionHandler;
extern SystemUtils sysUtils;
extern SceneManager scene;
extern ThreadManager threadManager;
extern ShaderManager shadeManager;
extern FXManager fxManager;
extern Vector2 myMouseCoords;
extern Model models[MAX_MODELS];
extern LightsManager lightsManager;
extern MoviePlayer moviePlayer;
extern WindowMetrics winMetrics;
extern ScreenRecorder screenRecorder;
extern bool bResizing;
extern std::atomic<bool> bResizeInProgress;
extern std::atomic<bool> bFullScreenTransition;

#ifdef __USE_SCRIPT_MANAGER__
#include "ScriptManager.h"
extern ScriptManager scriptManager;
#endif

#pragma warning(push)
#pragma warning(disable: 4101)

// ===========================================================================================
// RenderFrame — main per-frame entry point for the DX12 rendering pipeline.
// ===========================================================================================
void DX12Renderer::RenderFrame()
{
    // ENHANCED SAFE-GUARDS — Check all critical conditions before proceeding
    if (bHasCleanedUp || !m_d3d12Device || !m_commandQueue || !m_constantBuffer)
    {
        #if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_DEBUG, L"[DX12 RENDERFRAME] Early exit - missing critical resources");
        #endif
        return;
    }

    // Check for shutdown, minimised, resizing, or uninitialised states
    if (threadManager.threadVars.bIsShuttingDown.load() || bIsMinimized.load() ||
        threadManager.threadVars.bIsResizing.load()     || !bIsInitialized.load())
    {
        #if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_DEBUG, L"[DX12 RENDERFRAME] Early exit - system state prevents rendering");
        #endif
        return;
    }

    // CRITICAL: Prevent multiple render operations; acquire exclusive DX12 access
    ThreadLockHelper exclusiveRenderLock(threadManager, "exclusive_render_operation", 50);
    if (!exclusiveRenderLock.IsLocked())
    {
        #if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_DEBUG, L"[DX12 RENDERFRAME] Could not acquire exclusive render lock - skipping frame");
        #endif
        return;
    }

    // Double-check rendering state after acquiring lock
    if (threadManager.threadVars.bIsRendering.load())
    {
        #if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"[DX12 RENDERFRAME] Another render operation already active - aborting");
        #endif
        return;
    }

    try
    {
        exceptionHandler.RecordFunctionCall("DX12Renderer::RenderFrame");

        // Set rendering state atomically to indicate we are now rendering
        threadManager.threadVars.bIsRendering.store(true);

        #if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_DEBUG, L"[DX12 RENDERFRAME] Beginning render operation");
        #endif

        HWND hWnd = hwnd;

        // Clear colour: very dark grey in debug (aids model visibility), pure black in release
        #if defined(_DEBUG)
            const float clearColor[4] = { 0.01f, 0.01f, 0.01f, 1.0f };
        #else
            const float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
        #endif

        D3D12_VIEWPORT viewport  = {};
        D3D12_RECT     scissorRect = {};
        RECT           rc;

        ThreadStatus status = threadManager.GetThreadStatus(THREAD_RENDERER);
        while (((status == ThreadStatus::Running) || (status == ThreadStatus::Paused)) &&
                (!threadManager.threadVars.bIsShuttingDown.load()))
        {
            status = threadManager.GetThreadStatus(THREAD_RENDERER);
            if (status == ThreadStatus::Paused)
            {
                threadManager.threadVars.bIsRendering.store(false);
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }

            if (threadManager.threadVars.bIsResizing.load() || bIsMinimized.load())
            {
                threadManager.threadVars.bIsRendering.store(false);
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }

            threadManager.threadVars.bIsRendering.store(true);

            // CRITICAL RESOURCE CHECK — recover from lost swap chain or command queue
            if (!m_swapChain || !m_commandQueue || !m_fence)
            {
                if (!threadManager.threadVars.bIsResizing.load() && !sysUtils.IsWindowMinimized())
                {
                    #if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
                        debug.logLevelMessage(LogLevel::LOG_WARNING, L"[DX12 RENDERFRAME] Critical resources invalid. Attempting recovery.");
                    #endif
                    threadManager.threadVars.bIsResizing.store(true);
                    try {
                        Resize(iOrigWidth, iOrigHeight);
                        ResumeLoader();
                    }
                    catch (const std::exception& e) {
                        #if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
                            debug.logDebugMessage(LogLevel::LOG_ERROR, L"[DX12 RENDERFRAME] Recovery failed: %hs", e.what());
                        #endif
                    }
                    threadManager.threadVars.bIsResizing.store(false);
                    threadManager.threadVars.bIsRendering.store(false);
                    return;
                }
                else
                {
                    threadManager.threadVars.bIsRendering.store(false);
                    return;
                }
            }

            // STEP 1: Calculate viewport from fullscreen / windowed mode
            if (!winMetrics.isFullScreen)
                GetClientRect(hWnd, &rc);
            else
                rc = winMetrics.monitorFullArea;

            float width  = float(rc.right  - rc.left);
            float height = float(rc.bottom - rc.top);

            viewport.Width    = width;
            viewport.Height   = height;
            viewport.MinDepth = 0.0f;
            viewport.MaxDepth = 1.0f;
            viewport.TopLeftX = 0;
            viewport.TopLeftY = 0;

            scissorRect = { 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };

            // STEP 3: Camera update + delta time — computed BEFORE the GPU fence wait
            // so the CPU does useful work while WaitForPreviousFrame() may stall.
            // Mirrors the Vulkan render loop where delta/camera are computed before
            // vkWaitForFences to maximise CPU-GPU overlap.
            myCamera.UpdateViewMatrix();
            myCamera.UpdateJumpAnimation();

            auto now         = std::chrono::steady_clock::now();
            float rawDelta   = std::chrono::duration<float>(now - lastFrameTime).count();
            float deltaTime  = deltaTimeSmoothing.ProcessDelta(rawDelta, 60.0f);
            deltaTime        = std::clamp(deltaTime, 0.001f, 0.1f);
            lastFrameTime    = now;

            #ifdef __USE_SCRIPT_MANAGER__
                scriptManager.Update(deltaTime);
            #endif

            #if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
                static int frameCounter = 0;
                frameCounter++;
                if (frameCounter >= 60)
                {
                    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[DX12 RENDERFRAME] Delta time - Raw: %.6f, Smoothed: %.6f", rawDelta, deltaTime);
                    frameCounter = 0;
                }
            #endif

            // STEP 2: Wait for the previous frame, then reset the command list
            WaitForPreviousFrame();
            ResetCommandList();

            // STEP 3.5: D2D background pre-pass — SCENE_GAMETITLE only.
            //
            // DX11 parity: DXRenderFrame.cpp draws the title background via
            // RenderBackgroundImage() BEFORE RenderGamePlay() so the 3D ship is
            // composited on top of the background image.  The single post-3D D2D
            // pass below previously blitted IMG_GAMEINTRO1 full-screen AFTER the
            // 3D models, painting over them and making them invisible.
            //
            // Sequence:
            //   1) Record + execute PRESENT → RENDER_TARGET transition and clears.
            //   2) D2D pass blits the background and company logo; releasing the
            //      wrapped resource returns the back buffer to PRESENT state.
            //   3) Re-open the command list on the SAME allocator (the pre-pass
            //      list is still in flight on the GPU — the allocator must NOT be
            //      reset).  STEP 5's PRESENT → RENDER_TARGET transition therefore
            //      stays valid and STEP 6 skips the RTV clear so the background
            //      is preserved underneath the 3D models.
            bool bBackgroundPrePassDone = false;
            const bool bUseD2DOffscreen = (m_compositePSO && m_compositeRS &&
                                            m_d2dOffscreenTex[m_frameIndex]     &&
                                            m_d2dWrappedOffscreen[m_frameIndex] &&
                                            m_d2dOffscreenBitmap[m_frameIndex]  &&
                                            m_frameContexts[m_frameIndex].compositeAllocator
                                            );
            bool bD2DPrePassDone        = false;
            if (scene.stSceneType == SceneType::SCENE_GAMETITLE              &&
                threadManager.threadVars.bLoaderTaskFinished.load()          &&
                (!threadManager.threadVars.bIsShuttingDown.load())           &&
                (!bIsMinimized.load())                                       &&
                (!threadManager.threadVars.bIsResizing.load())               &&
                bIsInitialized.load()                                        &&
                m_d2dContext && m_dx11Dx12Compat.dx11On12Device              &&
                (bUseD2DOffscreen || (m_wrappedBackBuffers[m_frameIndex] && m_d2dRenderTargets[m_frameIndex])) &&
                m_d2dTextures[int(BlitObj2DIndexType::IMG_GAMEINTRO1)])
            {
                #if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
                    debug.logLevelMessage(LogLevel::LOG_DEBUG, L"[DX12 RENDERFRAME] STEP 3.5: background pre-pass (SCENE_GAMETITLE)");
                #endif

                if (!bUseD2DOffscreen)
                {
                    // 1) Legacy path: transition + clear the back buffer so D2D can acquire it.
                    //    The back buffer's wrapped resource has InState=RENDER_TARGET, so the
                    //    back buffer must be in that state before AcquireWrappedResources.
                    TransitionResource(m_frameContexts[m_frameIndex].renderTarget.Get(),
                        D3D12_RESOURCE_STATE_PRESENT,
                        D3D12_RESOURCE_STATE_RENDER_TARGET);

                    CD3DX12_CPU_DESCRIPTOR_HANDLE bgRtvHandle(m_frameContexts[m_frameIndex].rtvHandle);
                    m_commandList->ClearRenderTargetView(bgRtvHandle, clearColor, 0, nullptr);
                    // DSV is NOT cleared here — STEP 6 clears it unconditionally and no depth
                    // writes occur between this pre-pass and STEP 6, so a second clear is redundant.

                    CloseCommandList();
                    ExecuteCommandList();
                }

                // 2) D2D background pass.
                //    Off-screen path: the off-screen texture was transitioned to RENDER_TARGET at
                //    the end of the previous frame's composite B command list, so no DX12 barrier
                //    or command list close/execute is needed before AcquireWrappedResources.
                //    Legacy path: the transition + execute above ensures the back buffer is in RT.
                ID3D11Resource* bgWrapped = bUseD2DOffscreen
                    ? m_d2dWrappedOffscreen[m_frameIndex].Get()
                    : m_wrappedBackBuffers[m_frameIndex].Get();
                m_dx11Dx12Compat.dx11On12Device->AcquireWrappedResources(&bgWrapped, 1);
                if (bUseD2DOffscreen)
                    m_d2dContext->SetTarget(m_d2dOffscreenBitmap[m_frameIndex].Get());
                else
                    m_d2dContext->SetTarget(m_d2dRenderTargets[m_frameIndex].Get());
                m_d2dContext->BeginDraw();
                if (bUseD2DOffscreen)
                    m_d2dContext->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));

                // Full-screen title background — render zoomed version at same position if FX is active
                if (fxManager.IsImageZoomActive(int(BlitObj2DIndexType::IMG_GAMEINTRO1)))
                    fxManager.RenderZoomedImage(int(BlitObj2DIndexType::IMG_GAMEINTRO1), 0, 0, iOrigWidth, iOrigHeight);
                else
                    Blit2DObjectToSize(BlitObj2DIndexType::IMG_GAMEINTRO1, 0, 0, iOrigWidth, iOrigHeight);

                // Company logo overlay at half size, bottom-left corner
                if (m_d2dTextures[int(BlitObj2DIndexType::IMG_COMPANYLOGO)])
                {
                    D2D1_SIZE_F bgLogoSz = m_d2dTextures[int(BlitObj2DIndexType::IMG_COMPANYLOGO)]->GetSize();
                    int bgLogoW = static_cast<int>(bgLogoSz.width * 0.5f);
                    int bgLogoH = static_cast<int>(bgLogoSz.height * 0.5f);
                    if (fxManager.IsImageZoomActive(int(BlitObj2DIndexType::IMG_COMPANYLOGO)))
                        fxManager.RenderZoomedImage(int(BlitObj2DIndexType::IMG_COMPANYLOGO), 0, iOrigHeight - bgLogoH, bgLogoW, bgLogoH);
                    else
                        Blit2DObjectToSize(BlitObj2DIndexType::IMG_COMPANYLOGO, 0, iOrigHeight - bgLogoH, bgLogoW, bgLogoH);
                }

                // 3D starfield (background FX pass) — drawn AFTER the background and
                // logo but BEFORE the 3D models so the composition order is
                // BACKGROUND IMG → LOGO → 3D STARFIELD → FIREWORKS → MODELS → TSOO.
                // Previously the starfield was only rendered in the post-3D D2D pass
                // (STEP 10), which painted the stars OVER the ship model.
                try { fxManager.Render(true); }
                catch (const std::exception&) {}

                // Fireworks — rendered in the pre-pass so they appear BEHIND the 3D
                // ship model and BEFORE the TSOO blit in the post-3D D2D pass.
                try { fxManager.RenderFireworks(); }
                catch (const std::exception&) {}

                HRESULT bgHr = m_d2dContext->EndDraw();
                if (FAILED(bgHr))
                    debug.logDebugMessage(LogLevel::LOG_ERROR,
                        L"[DX12 RENDERFRAME] Background pre-pass EndDraw failed (0x%08X)", bgHr);

                m_d2dContext->SetTarget(nullptr);
                m_dx11Dx12Compat.dx11On12Device->ReleaseWrappedResources(&bgWrapped, 1);

                if (bUseD2DOffscreen)
                {
                    // Off-screen path: Flush is deferred to STEP 5.5 (just before Composite A
                    // records the SRV read). The command list stays open for STEP 5.5/5.6 and
                    // the main 3D pass — no close/execute needed here.
                    bD2DPrePassDone = true;
                }
                else
                {
                    // Legacy path: Flush is DEFERRED to immediately before ExecuteCommandList at
                    // STEP 9, letting the CPU record 3D commands while the GPU processes the
                    // pre-pass PRESENT→RT transition and RTV clear.

                    // 3) Re-open the command list for the main 3D pass.  The allocator
                    //    keeps accumulating both lists; it is reset next frame after
                    //    WaitForPreviousFrame guarantees the GPU is done with them.
                    HRESULT bgReset = m_commandList->Reset(
                        m_frameContexts[m_frameIndex].commandAllocator.Get(), m_pipelineState.Get());
                    if (FAILED(bgReset))
                    {
                        debug.logDebugMessage(LogLevel::LOG_ERROR,
                            L"[DX12 RENDERFRAME] Command list re-open after background pre-pass failed (0x%08X)", bgReset);
                        threadManager.threadVars.bIsRendering.store(false);
                        return;
                    }
                }

                bBackgroundPrePassDone = true;
            }

            // STEP 4: Set root signature, descriptor heaps, viewport, scissor
            m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());
            ID3D12DescriptorHeap* ppHeaps[] = { m_cbvSrvUavHeap.heap.Get(), m_samplerHeap.heap.Get() };
            m_commandList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);
            m_commandList->RSSetViewports(1, &viewport);
            m_commandList->RSSetScissorRects(1, &scissorRect);

            // STEP 5: Transition back buffer PRESENT → RENDER_TARGET
            TransitionResource(m_frameContexts[m_frameIndex].renderTarget.Get(),
                D3D12_RESOURCE_STATE_PRESENT,
                D3D12_RESOURCE_STATE_RENDER_TARGET);

            CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_frameContexts[m_frameIndex].rtvHandle);
            CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(m_dsvHeap.cpuStart);

            m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

            // STEP 5.5: Composite pre-pass off-screen onto the back buffer before 3D rendering.
            // Flush() submits the DX11On12 pre-pass commands (including the off-screen RT→SR
            // barrier from ReleaseWrappedResources) to the DX12 queue before the SRV read in
            // CompositeD2DToBackBuffer.  FIFO on the shared queue guarantees correct ordering.
            if (bD2DPrePassDone && bUseD2DOffscreen)
            {
                m_dx11Dx12Compat.dx11Context->Flush();
                CompositeD2DToBackBuffer(rtvHandle, viewport, scissorRect);
                // Restore the 3D pipeline state overwritten by the composite draw.
                m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());
                ID3D12DescriptorHeap* ppHeaps3D[] = { m_cbvSrvUavHeap.heap.Get(), m_samplerHeap.heap.Get() };
                m_commandList->SetDescriptorHeaps(_countof(ppHeaps3D), ppHeaps3D);
                m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);
            }

            // STEP 5.6: Transition off-screen back to RENDER_TARGET for the post-pass
            // AcquireWrappedResources (InState=RT).  Only needed when the pre-pass ran and
            // left off-screen in SHADER_RESOURCE (via Composite A + ReleaseWrappedResources).
            if (bUseD2DOffscreen && bD2DPrePassDone && m_d2dOffscreenTex[m_frameIndex])
            {
                TransitionResource(m_d2dOffscreenTex[m_frameIndex].Get(),
                    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                    D3D12_RESOURCE_STATE_RENDER_TARGET);
            }

            // STEP 6: Clear render targets (no lock needed — render thread is the sole user).
            // When the STEP 3.5 pre-pass ran, the RTV already holds the D2D background —
            // skip the colour clear so it is not wiped; depth is re-cleared either way.
            if (!bBackgroundPrePassDone)
                m_commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
            m_commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

            // Animate lights (pulse / flicker / strobe) each frame before updating
            // the global light buffer — mirrors the DX11, OpenGL, and Vulkan render paths.
            lightsManager.AnimateLights(deltaTime);

            // STEP 7: Update + bind constant buffers via root descriptors
            UpdateConstantBuffers();

            if (m_constantBuffer)
                m_commandList->SetGraphicsRootConstantBufferView(DX12_ROOT_PARAM_CONST_BUFFER,
                    m_constantBuffer->GetGPUVirtualAddress());

            if (m_globalLightBuffer)
                m_commandList->SetGraphicsRootConstantBufferView(DX12_ROOT_PARAM_GLOBAL_LIGHT_BUFFER,
                    m_globalLightBuffer->GetGPUVirtualAddress());

            if (m_envBuffer)
                m_commandList->SetGraphicsRootConstantBufferView(DX12_ROOT_PARAM_ENVIRONMENT_BUFFER,
                    m_envBuffer->GetGPUVirtualAddress());

            // Set primitive topology
            m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

            // STEP 8: Scene-specific 3D rendering via DX12 command list
            switch (scene.stSceneType)
            {
                #if defined(_DEBUG)
                    case SceneType::SCENE_EXPERIMENT:
                    {
                        break;
                    }
                #endif

                case SceneType::SCENE_INTRO:
                    break;
                    
                case SceneType::SCENE_INTRO_MOVIE:
                    break;

                case SceneType::SCENE_GAMETITLE:
                {
                    if (threadManager.threadVars.bLoaderTaskFinished.load())
                    {
                        int iModelID = scene.FindParentModelID(SplashShipName);
                        if (scene.gltfAnimator.IsAnimationPlaying(iModelID))
                            scene.gltfAnimator.UpdateAnimations(deltaTime, scene.scene_models, MAX_MODELS);
                    }
                    RenderGamePlay(deltaTime);
                    break;
                }

                case SceneType::SCENE_GAMEPLAY:
                {
                    int iModelID = scene.FindParentModelID(ShipName1);
                    if (scene.gltfAnimator.IsAnimationPlaying(iModelID))
                        scene.gltfAnimator.UpdateAnimations(deltaTime, scene.scene_models, MAX_MODELS);
                    RenderGamePlay(deltaTime);
                    break;
                }

                default:
                    break;
            }

            // STEP 9: Close and execute 3D command list.
            // Deliberately leave the back buffer in RENDER_TARGET state so that
            // AcquireWrappedResources (inState=RENDER_TARGET) can hand it to D2D.
            CloseCommandList();

            // Deferred 11On12 flush (legacy path only): ensures the STEP 3.5 D2D pre-pass
            // commands are submitted to the shared hardware queue before the 3D draw list
            // executes.  For the off-screen path the Flush was already called in STEP 5.5
            // (before recording Composite A), so submitting it again here is unnecessary.
            if (bBackgroundPrePassDone && !bUseD2DOffscreen)
                m_dx11Dx12Compat.dx11Context->Flush();
            ExecuteCommandList();

            // STEP 10: 2D rendering via DX11On12 / D2D interop.
            //
            // Off-screen path: D2D renders into the per-frame off-screen texture (in RT state).
            // After EndDraw/Release/Flush, Composite B draws the off-screen onto the back buffer
            // and then transitions both resources to their next-frame starting states.
            // Legacy path: D2D renders directly into the back buffer (in RT state).
            // ReleaseWrappedResources with OutState=PRESENT transitions it for PresentFrame().
            if (m_d2dContext && m_dx11Dx12Compat.dx11On12Device &&
                (bUseD2DOffscreen || (m_wrappedBackBuffers[m_frameIndex] && m_d2dRenderTargets[m_frameIndex])))
            {
                // Off-screen path: render D2D into the per-frame off-screen texture
                // (already in RENDER_TARGET state — either from the previous frame's
                // Composite B command list or, for SCENE_GAMETITLE, from STEP 5.6).
                // Legacy path: render D2D directly into the back buffer.
                ID3D11Resource* wrappedRes = bUseD2DOffscreen
                    ? m_d2dWrappedOffscreen[m_frameIndex].Get()
                    : m_wrappedBackBuffers[m_frameIndex].Get();
                m_dx11Dx12Compat.dx11On12Device->AcquireWrappedResources(&wrappedRes, 1);
                if (bUseD2DOffscreen)
                    m_d2dContext->SetTarget(m_d2dOffscreenBitmap[m_frameIndex].Get());
                else
                    m_d2dContext->SetTarget(m_d2dRenderTargets[m_frameIndex].Get());

                // ── Single D2D pass: background, 3D-behind FX, overlays ──────────
                // One BeginDraw/EndDraw per frame eliminates the inter-pass flush
                // overhead of the previous two-pass design.
                if ((!threadManager.threadVars.bIsShuttingDown.load()) &&
                    (!bIsMinimized.load()) && (!threadManager.threadVars.bIsResizing.load()) &&
                    bIsInitialized.load())
                {
                    m_d2dContext->BeginDraw();
                    if (bUseD2DOffscreen)
                        m_d2dContext->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));

                    // Cache the loader state once — bLoaderTaskFinished only ever
                    // transitions false→true, so a single read per BeginDraw is safe
                    // and saves three redundant atomic loads within this block.
                    const bool bLoaderDone = threadManager.threadVars.bLoaderTaskFinished.load();

                    switch (scene.stSceneType)
                    {
                        case SceneType::SCENE_GAMETITLE:
                        {
                            if (bBackgroundPrePassDone)
                            {
                                // Fireworks moved to STEP 3.5 pre-pass — rendered before 3D models.
                                if (m_d2dTextures[int(BlitObj2DIndexType::IMG_TSOO)])
                                {
                                    int startX = (iOrigWidth - 536) / 2; // Centered horizontally
                                    int startY = (iOrigHeight - 466) / 2; // Centered vertically
                                    Blit2DObjectToSize(BlitObj2DIndexType::IMG_TSOO, startX, startY, 536, 466);
                                }
                            }
                            else if (bLoaderDone)
                            {
                                // Pre-pass unavailable this frame (e.g. background image
                                // not loaded yet) — legacy post-3D blit as a fallback.
                                if (m_d2dTextures[int(BlitObj2DIndexType::IMG_GAMEINTRO1)]) {
                                    if (fxManager.IsImageZoomActive(int(BlitObj2DIndexType::IMG_GAMEINTRO1)))
                                        fxManager.RenderZoomedImage(int(BlitObj2DIndexType::IMG_GAMEINTRO1), 0, 0, iOrigWidth, iOrigHeight);
                                    else
                                        Blit2DObjectToSize(BlitObj2DIndexType::IMG_GAMEINTRO1, 0, 0, iOrigWidth, iOrigHeight);
                                }

                                if (m_d2dTextures[int(BlitObj2DIndexType::IMG_COMPANYLOGO)]) {
                                    D2D1_SIZE_F sz = m_d2dTextures[int(BlitObj2DIndexType::IMG_COMPANYLOGO)]->GetSize();
                                    int fbW = static_cast<int>(sz.width * 0.5f);
                                    int fbH = static_cast<int>(sz.height * 0.5f);
                                    if (fxManager.IsImageZoomActive(int(BlitObj2DIndexType::IMG_COMPANYLOGO)))
                                        fxManager.RenderZoomedImage(int(BlitObj2DIndexType::IMG_COMPANYLOGO), 0, iOrigHeight - fbH, fbW, fbH);
                                    else
                                        Blit2DObjectToSize(BlitObj2DIndexType::IMG_COMPANYLOGO, 0, iOrigHeight - fbH, fbW, fbH);
                                }
                            }
                            else
                            {
                                if (m_d2dTextures[int(BlitObj2DIndexType::IMG_LOADING)])
                                {
                                    if (threadManager.threadVars.bInitiateFader.load())
                                    {
                                        threadManager.threadVars.bInitiateFader.store(false);
                                        fxManager.FadeToImage(1.0f, 0.1f);
                                    }
                                    if (fxManager.IsImageZoomActive(int(BlitObj2DIndexType::IMG_LOADING)))
                                        fxManager.RenderZoomedImage(int(BlitObj2DIndexType::IMG_LOADING), 0, 0, iOrigWidth, iOrigHeight);
                                    else
                                        Blit2DObjectToSize(BlitObj2DIndexType::IMG_LOADING, 0, 0, iOrigWidth, iOrigHeight);
                                }
                            }
                            break;
                        }

                        case SceneType::SCENE_GAMEPLAY:
                        {
                            if (!bLoaderDone &&
                                m_d2dTextures[int(BlitObj2DIndexType::IMG_LOADING)])
                            {
                                if (threadManager.threadVars.bInitiateFader.load())
                                {
                                    threadManager.threadVars.bInitiateFader.store(false);
                                    fxManager.FadeToImage(1.0f, 0.1f);
                                }
                                if (fxManager.IsImageZoomActive(int(BlitObj2DIndexType::IMG_LOADING)))
                                    fxManager.RenderZoomedImage(int(BlitObj2DIndexType::IMG_LOADING), 0, 0, iOrigWidth, iOrigHeight);
                                else
                                    Blit2DObjectToSize(BlitObj2DIndexType::IMG_LOADING, 0, 0, iOrigWidth, iOrigHeight);
                            }
                            break;
                        }

                        default:
                            break;
                    }

                    // Background FX (starfield, warp tunnel) behind 3D content.
                    // When the STEP 3.5 pre-pass ran it already rendered these BEFORE
                    // the 3D models — rendering them again here would paint the stars
                    // over the ship model.
                    if (!bBackgroundPrePassDone)
                    {
                        try { fxManager.Render(true); }
                        catch (const std::exception&) {}
                    }

                    // ── Scene-specific 2D + overlays ─────────────────────────────
                    switch (scene.stSceneType)
                    {
                            case SceneType::SCENE_INTRO:
                            {
                                if (moviePlayer.IsPlaying())
                                    RenderIntroMovie();
                                /*if (m_d2dTextures[int(BlitObj2DIndexType::IMG_SPLASH1)]) {
                                    if (fxManager.IsImageZoomActive(int(BlitObj2DIndexType::IMG_SPLASH1)))
                                        fxManager.RenderZoomedImage(int(BlitObj2DIndexType::IMG_SPLASH1), 0, 0, iOrigWidth, iOrigHeight);
                                    else
                                        Blit2DObjectToSize(BlitObj2DIndexType::IMG_SPLASH1, 0, 0, iOrigWidth, iOrigHeight);
                                } */
                                break;
                            }

                            case SceneType::SCENE_INTRO_MOVIE:
                            {
                                if (moviePlayer.IsPlaying())
                                    RenderIntroMovie();
                                break;
                            }

                            case SceneType::SCENE_GAMETITLE:
                            {
                                #if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
                                    debug.logLevelMessage(LogLevel::LOG_DEBUG, L"[DX12 RENDERFRAME] Rendering game title 2D elements");
                                #endif
                                fxManager.RenderLoadingText();
                                break;
                            }

                            case SceneType::SCENE_GAMEPLAY:
                            {
                                #if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
                                    debug.logLevelMessage(LogLevel::LOG_DEBUG, L"[DX12 RENDERFRAME] Rendering gameplay 2D elements");
                                #endif
                                if (!bLoaderDone || fxManager.HasActiveLoadingTextEffects())
                                {
                                    fxManager.RenderLoadingText();
                                }
                                break;
                            }

                            default:
                                break;
                        }

                    // FPS display + debug info overlay
                    if (USE_FPS_DISPLAY && config.myConfig.showDebugInfo)
                    {
                        // Reuse the per-frame timestamp already captured for delta-time —
                        // avoids a second steady_clock::now() call per frame.
                        static auto lastFPSTime     = now;
                        static int  fpsFrameCounter = 0;

                        float elapsedForFPS = std::chrono::duration<float>(now - lastFPSTime).count();
                        fpsFrameCounter++;

                        if (elapsedForFPS >= 1.0f)
                        {
                            fps             = static_cast<float>(fpsFrameCounter) / elapsedForFPS;
                            fpsFrameCounter = 0;
                            lastFPSTime     = now;
                        }

                        #ifdef _DEBUG
                        // Debug builds: full diagnostic overlay
                        const XMFLOAT3 Coords = myCamera.GetPosition();
                        wchar_t fpsBuf[512];
                        swprintf_s(fpsBuf, _countof(fpsBuf),
                            L"FPS: %.2f\nMOUSE: x%.0f, y%.0f"
                            L"\nClient Width: %d, Client Height:%d"
                            L"\nCamera X: %.3f, Y: %.3f, Z: %.3f, Yaw: %.3f, Pitch: %.3f"
                            L"\nGlobal Light Count: %d\n",
                            fps,
                            myMouseCoords.x, myMouseCoords.y,
                            iOrigWidth, iOrigHeight,
                            Coords.x, Coords.y, Coords.z,
                            myCamera.m_yaw, myCamera.m_pitch,
                            lightsManager.GetLightCount());
                        #else
                        // Release builds: FPS only
                        wchar_t fpsBuf[32];
                        swprintf_s(fpsBuf, _countof(fpsBuf), L"FPS: %.2f", fps);
                        #endif

                        const float dbgFontSize = std::clamp(height / 108.0f, 8.0f, 12.0f);
                        DrawMyText(fpsBuf, Vector2(0, 0), MyColor(255, 255, 255, 255), dbgFontSize);
                    }

                    // Renderer info overlay — bottom-right corner
                    if (USE_RENDERER_INFO && !scene.bSceneSwitching && m_d2dContext && m_dwriteFactory)
                    {
                        bool riShow = (scene.stSceneType == SceneType::SCENE_GAMETITLE ||
                                       scene.stSceneType == SceneType::SCENE_GAMEPLAY  ||
                                       scene.stSceneType == SceneType::SCENE_INTRO     ||
                                       scene.stSceneType == SceneType::SCENE_INTRO_MOVIE ||
                                       scene.stSceneType == SceneType::SCENE_GAMEOVER);
                        #if defined(_DEBUG)
                            riShow = riShow || (scene.stSceneType == SceneType::SCENE_EXPERIMENT);
                        #endif

                        if (riShow)
                        {
                            const float riFontSize = std::clamp(
                                static_cast<float>(iOrigHeight) / 72.0f, 10.0f, 16.0f);

                            // e.g. "Debug CPGE v0.0.1723 15-06-2026"
                            static const std::wstring buildDate = []() -> std::wstring {
                                const char* d = __DATE__; // "Mmm DD YYYY", e.g. "Jun 15 2026"
                                const char* m = "JanFebMarAprMayJunJulAugSepOctNovDec";
                                int mon = 1;
                                for (int i = 0; i < 12; i++) {
                                    if (d[0]==m[i*3] && d[1]==m[i*3+1] && d[2]==m[i*3+2]) { mon=i+1; break; }
                                }
                                int day  = (d[4]==' ') ? (d[5]-'0') : ((d[4]-'0')*10+(d[5]-'0'));
                                int year = (d[7]-'0')*1000+(d[8]-'0')*100+(d[9]-'0')*10+(d[10]-'0');
                                wchar_t buf[12];
                                swprintf_s(buf, 12, L"%02d-%02d-%04d", day, mon, year);
                                return std::wstring(buf);
                            }();
                            // Static: all components are compile-time constants or the
                            // already-static buildDate — computed once, zero heap allocs thereafter.
                            static const std::wstring riText =
                                std::wstring(BUILD_TYPE_W L" " RENDERER_NAME_W L" " GAME_NAME_W L" v") +
                                std::to_wstring(CURRENT_BUILD_VERSION)    + L"." +
                                std::to_wstring(CURRENT_BUILD_SUBVERSION) + L"." +
                                std::to_wstring(CURRENT_BUILD)            + L" " +
                                buildDate;

                            IDWriteTextFormat* riFmt = GetOrCreateTextFormat(FontName, riFontSize);
                            if (riFmt)
                            {
                                // Cache the text layout — the text and font are both static, so
                                // creating it fresh every frame is a pointless per-frame COM alloc.
                                // Invalidated when the font size changes (window resize).
                                static ComPtr<IDWriteTextLayout> s_riLayout;
                                static float s_riLayoutFontSize = 0.0f;
                                if (!s_riLayout || s_riLayoutFontSize != riFontSize)
                                {
                                    s_riLayout.Reset();
                                    if (SUCCEEDED(m_dwriteFactory->CreateTextLayout(
                                            riText.c_str(), static_cast<UINT32>(riText.size()),
                                            riFmt,
                                            static_cast<float>(iOrigWidth),
                                            riFontSize * 2.0f,
                                            &s_riLayout)))
                                        s_riLayoutFontSize = riFontSize;
                                }

                                if (s_riLayout)
                                {
                                    DWRITE_TEXT_METRICS riMetrics = {};
                                    s_riLayout->GetMetrics(&riMetrics);

                                    const float riX = static_cast<float>(iOrigWidth)  - riMetrics.width;
                                    const float riY = static_cast<float>(iOrigHeight) - riMetrics.height;

                                    if (SetGeneralBrushColor(220.0f/255.0f, 220.0f/255.0f, 220.0f/255.0f, 1.0f))
                                    {
                                        m_d2dContext->DrawTextLayout(
                                            D2D1::Point2F(riX, riY),
                                            s_riLayout.Get(),
                                            m_generalBrush.Get());
                                    }
                                }
                            }
                        }
                    }

                    // 5-second OSD notification after F2 debug toggle
                    if (bDebugOSDActive)
                    {
                        // Reuse the per-frame `now` timestamp — avoids a redundant steady_clock::now() call.
                        float osdElapsed = std::chrono::duration<float>(now - debugOSDStartTime).count();
                        if (osdElapsed < 5.0f)
                        {
                            std::wstring osdMsg = config.myConfig.showDebugInfo
                                ? L"=> Debug Info: ENABLED"
                                : L"=> Debug Info: DISABLED";
                            DrawMyText(osdMsg, Vector2(10.0f, 80.0f), MyColor(255, 220, 0, 255), 14.0f);
                        }
                        else
                            bDebugOSDActive = false;
                    }

                    // Animated loading circle while assets are loading
                    if (!bLoaderDone)
                    {
                        delay++;
                        if (delay > 3) { loadIndex++; if (loadIndex > 9) { loadIndex = 0; } delay = 0; }

                        if (m_d2dTextures[int(BlitObj2DIndexType::BG_LOADER_CIRCLE)])
                        {
                            iPosX = loadIndex << 5;
                            Blit2DObjectAtOffset(BlitObj2DIndexType::BG_LOADER_CIRCLE,
                                iOrigWidth - 34, iOrigHeight - 45, iPosX, 0, 32, 32);
                        }
                    }

                    // 2D effects overlay
                    try {
                        fxManager.Render2D();
                    }
                    catch (const std::exception& e) {
                        #if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
                            debug.logDebugMessage(LogLevel::LOG_ERROR, L"[DX12 RENDERFRAME] 2D effects rendering failed: %hs", e.what());
                        #endif
                    }

                    // GUI windows and interface elements
                    try {
                        guiManager.Render();
                    }
                    catch (const std::exception& e) {
                        #if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
                            debug.logDebugMessage(LogLevel::LOG_ERROR, L"[DX12 RENDERFRAME] GUI rendering failed: %hs", e.what());
                        #endif
                    }

                    // Mouse cursor (always on top)
                    if (m_d2dTextures[int(BlitObj2DIndexType::BLIT_ALWAYS_CURSOR)])
                        Blit2DObject(BlitObj2DIndexType::BLIT_ALWAYS_CURSOR, myMouseCoords.x, myMouseCoords.y);

                    // Blinking REC indicator when screen recorder is active
                    if (screenRecorder.IsRecording())
                    {
                        static int recBlinkCounter = 0;
                        recBlinkCounter = (recBlinkCounter + 1) % 60;
                        if (recBlinkCounter < 30)
                        {
                            DrawMyText(L"* REC",
                                Vector2(width - 75.0f, 12.0f),
                                MyColor::Red(),
                                18.0f);
                        }
                    }

                    // Full-screen pass FX (color fader, starfield, warp tunnel) — must be inside BeginDraw/EndDraw
                    try {
                        fxManager.Render();
                    }
                    catch (const std::exception& e)
                    {
                        #if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
                            debug.logDebugMessage(LogLevel::LOG_ERROR, L"[DX12 RENDERFRAME] Post-processing effects failed: %hs", e.what());
                        #endif
                    }

                    // End D2D overlay pass
                    try
                    {
                        HRESULT hr = m_d2dContext->EndDraw();
                        if (FAILED(hr))
                        {
                            #if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
                                debug.logDebugMessage(LogLevel::LOG_ERROR, L"[DX12 RENDERFRAME] Direct2D EndDraw failed (0x%08X)", hr);
                            #endif
                        }
                    }
                    catch (const std::exception& e)
                    {
                        #if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
                            debug.logDebugMessage(LogLevel::LOG_ERROR, L"[DX12 RENDERFRAME] EndDraw failed: %hs", e.what());
                        #endif
                    }

                    #if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
                        debug.logLevelMessage(LogLevel::LOG_DEBUG, L"[DX12 RENDERFRAME] 2D overlay pass completed");
                    #endif
                }

                // Return D2D context target and release the wrapped resource.
                m_d2dContext->SetTarget(nullptr);
                m_dx11Dx12Compat.dx11On12Device->ReleaseWrappedResources(&wrappedRes, 1);
                // Flush submits the DX11On12 D2D commands (including the off-screen / back-buffer
                // state transition barrier) to the shared DX12 queue.
                m_dx11Dx12Compat.dx11Context->Flush();

                if (bUseD2DOffscreen)
                {
                    // Off-screen path: composite the post-pass onto the back buffer,
                    // then transition the off-screen back to RT for the next frame's
                    // pre-pass and the back buffer to PRESENT for PresentFrame().
                    // Reset with the per-frame composite allocator so this command list
                    // does not conflict with the main 3D allocator (still in flight).
                    m_commandList->Reset(m_frameContexts[m_frameIndex].compositeAllocator.Get(), nullptr);
                    CompositeD2DToBackBuffer(rtvHandle, viewport, scissorRect);
                    // Transition off-screen back to RENDER_TARGET so the next frame's
                    // pre-pass can AcquireWrappedResources (InState=RT) without a barrier.
                    TransitionResource(m_d2dOffscreenTex[m_frameIndex].Get(),
                        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                        D3D12_RESOURCE_STATE_RENDER_TARGET);
                    // Transition back buffer to PRESENT for PresentFrame().
                    // (Legacy path: ReleaseWrappedResources with OutState=PRESENT handles this.)
                    TransitionResource(m_frameContexts[m_frameIndex].renderTarget.Get(),
                        D3D12_RESOURCE_STATE_RENDER_TARGET,
                        D3D12_RESOURCE_STATE_PRESENT);
                    CloseCommandList();
                    ExecuteCommandList();
                }
            }

            // STEP 12: Present the finished frame
            try {
                #if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
                    debug.logLevelMessage(LogLevel::LOG_DEBUG, L"[DX12 RENDERFRAME] Presenting frame to display");
                #endif

                // Capture the fully-composed back-buffer BEFORE Present.
                // ReleaseWrappedResources above transitions it to PRESENT state,
                // which is the state CaptureFrame expects to transition from.
                if (screenRecorder.IsRecording())
                    screenRecorder.CaptureFrame(m_d3d12Device.Get(), m_commandQueue.Get(),
                                                m_swapChain.Get(), m_frameIndex);

                PresentFrame();
                MoveToNextFrame();
            }
            catch (const std::exception& e) {
                #if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
                    debug.logDebugMessage(LogLevel::LOG_ERROR, L"[DX12 RENDERFRAME] Present operation failed: %hs", e.what());
                #endif
            }

            // STEP 13: Clear rendering state for next frame
            threadManager.threadVars.bIsRendering.store(false);
        } // End of while loop for threaded rendering

        #if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_DEBUG, L"[DX12 RENDERFRAME] Render operation completed successfully");
        #endif
    }
    catch (const std::exception& e)
    {
        #if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_CRITICAL, L"[DX12 RENDERFRAME] Critical exception occurred: %hs", e.what());
        #endif

        threadManager.threadVars.bIsRendering.store(false);
    }

    #if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"[DX12 RENDERFRAME] Render thread exiting normally");
    #endif

    // FINAL: Guarantee rendering state is clear
    threadManager.threadVars.bIsRendering.store(false);
    // exclusiveRenderLock releases automatically on scope exit
}

// ===========================================================================================
// RenderGamePlay — 3D rendering pipeline for gameplay and title scenes.
// ===========================================================================================
inline void DX12Renderer::RenderGamePlay(float deltaTime)
{
    if (!m_commandList || !m_constantBuffer)
        return;

    #if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_DEBUG, L"[DX12 RENDERFRAME] Rendering gameplay scene - 3D pipeline");
    #endif

    // Debug pixel shader mode switching (debug builds only)
    #if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG) && defined(_DEBUG_PIXSHADER_)
        if (GetAsyncKeyState('1') & 0x8000) SetDebugMode(0); // Production view mode
        if (GetAsyncKeyState('2') & 0x8000) SetDebugMode(1); // Normals only mode
        if (GetAsyncKeyState('3') & 0x8000) SetDebugMode(2); // Texture only mode
        if (GetAsyncKeyState('4') & 0x8000) SetDebugMode(3); // Lighting only mode
        if (GetAsyncKeyState('5') & 0x8000) SetDebugMode(4); // Specular only mode
        if (GetAsyncKeyState('6') & 0x8000) SetDebugMode(5); // Attenuation/normals mode
        if (GetAsyncKeyState('7') & 0x8000) SetDebugMode(6); // Shadows only mode
        if (GetAsyncKeyState('8') & 0x8000) SetDebugMode(7); // Reflection only mode
        if (GetAsyncKeyState('9') & 0x8000) SetDebugMode(8); // Metallic only mode
    #endif

    // Render all loaded scene models when loading is complete
    if (threadManager.threadVars.bLoaderTaskFinished.load())
    {
        #if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_DEBUG, L"[DX12 RENDERFRAME] Rendering 3D models");
        #endif

        // Set the DX12 PSO once for all model draws this frame
        if (m_pipelineState)
            m_commandList->SetPipelineState(m_pipelineState.Get());

        // Hoist camera reads once per frame — calling the getters inside the loop
        // would invoke them once per loaded model each frame.
        const auto viewMat = myCamera.GetViewMatrix();
        const auto projMat = myCamera.GetProjectionMatrix();
        const auto camPos  = myCamera.GetPosition();

        for (int i = 0; i < MAX_SCENE_MODELS; ++i)
        {
            if (scene.scene_models[i].m_isLoaded && !scene.scene_models[i].m_modelInfo.bIsTransformProxy)
            {
                scene.scene_models[i].m_modelInfo.fxActive        = false;
                // MatrixCopy4x4F: 4 SSE MOVUPS loads+stores (64 bytes each) — avoids
                // XMMATRIX operator= overhead in the per-model hot loop.
                MatrixCopy4x4F(&viewMat, &scene.scene_models[i].m_modelInfo.viewMatrix);
                MatrixCopy4x4F(&projMat, &scene.scene_models[i].m_modelInfo.projectionMatrix);
                scene.scene_models[i].m_modelInfo.cameraPosition   = camPos;

                // Draw via the DX12 native command-list path.
                // RenderDX12() binds the upload-heap VB/IB/CB directly on the
                // command list, bypassing the D3D11-shader guard in Render().
                scene.scene_models[i].RenderDX12(m_commandList.Get(), this, deltaTime);
            }
        }
    }
}

// ===========================================================================================
// RenderIntroMovie — 2D movie playback rendering for SCENE_INTRO_MOVIE.
// ===========================================================================================
inline void DX12Renderer::RenderIntroMovie()
{
    #if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_DEBUG, L"[DX12 RENDERFRAME] Rendering movie intro 2D elements");
    #endif

    if (!moviePlayer.IsPlaying())
        return;

    moviePlayer.UpdateFrame();
    moviePlayer.Render(Vector2(0, 0), Vector2(iOrigWidth, iOrigHeight));

    // Company logo overlay at half size, bottom-left corner
    if (m_d2dTextures[int(BlitObj2DIndexType::IMG_COMPANYLOGO)])
    {
        D2D1_SIZE_F logoSz = m_d2dTextures[int(BlitObj2DIndexType::IMG_COMPANYLOGO)]->GetSize();
        int halfW = static_cast<int>(logoSz.width  * 0.5f);
        int halfH = static_cast<int>(logoSz.height * 0.5f);
        if (fxManager.IsImageZoomActive(int(BlitObj2DIndexType::IMG_COMPANYLOGO)))
            fxManager.RenderZoomedImage(int(BlitObj2DIndexType::IMG_COMPANYLOGO), 0, iOrigHeight - halfH, halfW, halfH);
        else
            Blit2DObjectToSize(BlitObj2DIndexType::IMG_COMPANYLOGO, 0, iOrigHeight - halfH, halfW, halfH);
    }

    // Spacebar to skip movie — only in SCENE_INTRO_MOVIE, not splash SCENE_INTRO
    if (scene.stSceneType == SceneType::SCENE_INTRO_MOVIE && (GetAsyncKeyState(' ') & 0x8000))
    {
        moviePlayer.Stop();
        scene.bSceneSwitching = true;
        fxManager.FadeToBlack(1.0f, 0.06f);
    }
}

// ===========================================================================================
// RenderBackgroundImage — replaced by the STEP 3.5 D2D pre-pass in RenderFrame().
// The pre-pass blits the background, logo, starfield, and fireworks directly inside a
// properly paired AcquireWrappedResources/ReleaseWrappedResources block before the 3D
// command list executes, guaranteeing correct DX11On12 interop ordering.
// This function is retained as a no-op stub to satisfy the DX12Renderer.h declaration.
// ===========================================================================================
void DX12Renderer::RenderBackgroundImage()
{
    // No-op: all background rendering is handled by the STEP 3.5 pre-pass in RenderFrame().
    return;

    // DEAD CODE BELOW — kept in compiler-excluded block to document the former implementation.
    if (false)
    {
    m_d2dContext->BeginDraw();

    switch (scene.stSceneType)
    {
        case SceneType::SCENE_GAMETITLE:
        {
            if (threadManager.threadVars.bLoaderTaskFinished.load())
            {
                // Background image — render zoomed version at same position if FX is active
                if (m_d2dTextures[int(BlitObj2DIndexType::IMG_GAMEINTRO1)]) {
                    if (fxManager.IsImageZoomActive(int(BlitObj2DIndexType::IMG_GAMEINTRO1)))
                        fxManager.RenderZoomedImage(int(BlitObj2DIndexType::IMG_GAMEINTRO1), 0, 0, iOrigWidth, iOrigHeight);
                    else
                        Blit2DObjectToSize(BlitObj2DIndexType::IMG_GAMEINTRO1, 0, 0, iOrigWidth, iOrigHeight);
                }

                // Company logo overlay at half size, bottom-left corner
                if (m_d2dTextures[int(BlitObj2DIndexType::IMG_COMPANYLOGO)])
                {
                    D2D1_SIZE_F logoSz = m_d2dTextures[int(BlitObj2DIndexType::IMG_COMPANYLOGO)]->GetSize();
                    int halfW = static_cast<int>(logoSz.width  * 0.5f);
                    int halfH = static_cast<int>(logoSz.height * 0.5f);
                    if (fxManager.IsImageZoomActive(int(BlitObj2DIndexType::IMG_COMPANYLOGO)))
                        fxManager.RenderZoomedImage(int(BlitObj2DIndexType::IMG_COMPANYLOGO), 0, iOrigHeight - halfH, halfW, halfH);
                    else
                        Blit2DObjectToSize(BlitObj2DIndexType::IMG_COMPANYLOGO, 0, iOrigHeight - halfH, halfW, halfH);
                }

                // 3D starfield and warp-dot tunnel are rendered via fxManager once DX12
                // FXManager render paths are available (currently DX11-only).
            }
            else
            {
                if (m_d2dTextures[int(BlitObj2DIndexType::IMG_LOADING)])
                {
                    if (threadManager.threadVars.bInitiateFader.load())
                    {
                        threadManager.threadVars.bInitiateFader.store(false);
                        fxManager.FadeToImage(1.0f, 0.1f);
                    }
                    if (fxManager.IsImageZoomActive(int(BlitObj2DIndexType::IMG_LOADING)))
                        fxManager.RenderZoomedImage(int(BlitObj2DIndexType::IMG_LOADING), 0, 0, iOrigWidth, iOrigHeight);
                    else
                        Blit2DObjectToSize(BlitObj2DIndexType::IMG_LOADING, 0, 0, iOrigWidth, iOrigHeight);
                }
            }
            break;
        }

        case SceneType::SCENE_GAMEPLAY:
        {
            if (!threadManager.threadVars.bLoaderTaskFinished.load())
            {
                if (m_d2dTextures[int(BlitObj2DIndexType::IMG_LOADING)])
                {
                    if (threadManager.threadVars.bInitiateFader.load())
                    {
                        threadManager.threadVars.bInitiateFader.store(false);
                        fxManager.FadeToImage(1.0f, 0.1f);
                    }
                    if (fxManager.IsImageZoomActive(int(BlitObj2DIndexType::IMG_LOADING)))
                        fxManager.RenderZoomedImage(int(BlitObj2DIndexType::IMG_LOADING), 0, 0, iOrigWidth, iOrigHeight);
                    else
                        Blit2DObjectToSize(BlitObj2DIndexType::IMG_LOADING, 0, 0, iOrigWidth, iOrigHeight);
                }
            }
            break;
        }

        #if defined(_DEBUG)
        case SceneType::SCENE_EXPERIMENT:
        {
            // Black background — 3D tunnel dots rendered via DX12 FXManager when available
            break;
        }
        #endif

        default:
            break;
    }

    try
    {
        HRESULT hr = m_d2dContext->EndDraw();
        if (FAILED(hr))
        {
            #if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
                debug.logDebugMessage(LogLevel::LOG_ERROR, L"[DX12 RenderBackgroundImage] EndDraw failed (0x%08X)", hr);
            #endif
        }
    }
    catch (const std::exception& e)
    {
        #if defined(_DEBUG_DX12RENDERER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"[DX12 RenderBackgroundImage] EndDraw exception: %hs", e.what());
        #endif
    }
    } // end if (false) — dead code block
} // End of RenderBackgroundImage()

#pragma warning(pop)

#endif // defined(__USE_DIRECTX_12__)
