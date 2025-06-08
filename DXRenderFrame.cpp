/* -------------------------------------------------------------- */
// Main Tasking Thread for our renderer
//
// This function is the main (optional thread) function that will 
// be used to render the scene. It will be responsible for rendering 
// the scene, updating the scene, and handling any other rendering 
// tasks.
//
// --- Begin 3D Rendering ---
// --- Begin 2D Rendering ---
// --- Post-process FX ---
// --- Present to screen ---

/* ----------------------------------------------------------------
   DO NOT INCLUDE THIS FILE!!! THE PROJECT ITSELF SCOPES THIS FILE!
/* ---------------------------------------------------------------- */
#include "Includes.h"

#if defined(__USE_DIRECTX_11__)
#include "DX11Renderer.h"
#include "Debug.h"
#include "ExceptionHandler.h"
#include "WinSystem.h"
#include "Configuration.h"
#include "DX_FXManager.h"
#include "GUIManager.h"
#include "Models.h"
#include "Lights.h"
#include "SceneManager.h"
#include "ShaderManager.h"
#include "MoviePlayer.h"

extern HWND hwnd;
extern HINSTANCE hInst;
extern GUIManager guiManager;
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
extern bool bResizing;
extern std::atomic<bool> bResizeInProgress;                    // Prevents multiple resize operations
extern std::atomic<bool> bFullScreenTransition;                // Prevents handling during fullscreen transitions

#pragma warning(push)
#pragma warning(disable: 4101)

void DX11Renderer::RenderFrame()
{
    // SAFE-GUARDS
    if (bHasCleanedUp || !m_d3dDevice || !m_d3dContext || !m_cameraConstantBuffer) return;
    if (threadManager.threadVars.bIsShuttingDown.load() || bIsMinimized.load() || threadManager.threadVars.bIsResizing.load() ||
        threadManager.threadVars.bIsRendering.load() || !bIsInitialized.load()) return;

    try
    {
        exceptionHandler.RecordFunctionCall("RenderFrame");
        // Try to acquire the render frame lock with a 10ms timeout
        if (!threadManager.TryLock(renderFrameLockName, 10)) {
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"Could not acquire render frame lock - timeout reached");
            return;
        }

        // Clear buffers & Initialize
        HRESULT hr = S_OK;
        HWND hWnd = hwnd;

        // Save the current Direct3D state
        ComPtr<ID3D11RenderTargetView> previousRenderTargetView;
        ComPtr<ID3D11DepthStencilView> previousDepthStencilView;
        FLOAT clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
        D3D11_VIEWPORT previousViewport;
        UINT numViewports = 1;
        D3D11_VIEWPORT viewport = {};
        RECT rc;

#ifdef RENDERER_IS_THREAD
        ThreadStatus status = threadManager.GetThreadStatus(THREAD_RENDERER);
        while (((status == ThreadStatus::Running) || (status == ThreadStatus::Paused)) && (!threadManager.threadVars.bIsShuttingDown.load()))
        {
            status = threadManager.GetThreadStatus(THREAD_RENDERER);
            if (status == ThreadStatus::Paused)
            {
                threadManager.threadVars.bIsRendering.store(false);
                Sleep(1);
                continue;
            }

            threadManager.threadVars.bIsRendering.store(true);
#endif

            m_d3dContext->OMGetRenderTargets(1, previousRenderTargetView.GetAddressOf(), previousDepthStencilView.GetAddressOf());
            m_d3dContext->RSGetViewports(&numViewports, &previousViewport);

            if (!winMetrics.isFullScreen)
            {
                GetClientRect(hWnd, &rc);
            }
            else
            {
                rc = winMetrics.monitorFullArea;
            }

            float width = float(rc.right - rc.left);
            float height = float(rc.bottom - rc.top);
            viewport.Width = width;
            viewport.Height = height;
            viewport.MinDepth = 0.0f;
            viewport.MaxDepth = 1.0f;
            viewport.TopLeftX = 0;
            viewport.TopLeftY = 0;
            m_d3dContext->RSSetViewports(1, &viewport);

            static Microsoft::WRL::ComPtr<ID3D11RasterizerState> wireframeRS;

#if defined(_DEBUG_RENDER_WIREFRAME_)
            if (bWireframeMode && m_wireframeState)
            {
                // Set rasterizer state
                m_d3dContext->RSSetState(m_wireframeState.Get());
            }
            else if (m_rasterizerState)
            {
                // Set rasterizer state
                m_d3dContext->RSSetState(m_rasterizerState.Get());
            }
#else
            // DEFAULT: Set rasterizer state
            m_d3dContext->RSSetState(m_rasterizerState.Get());
#endif

            // Check the status of the rendering thread
            if (m_d3dDevice)
            {
                HRESULT hr = m_d3dDevice->GetDeviceRemovedReason();

                // Only attempt reset if:
                // 1. Device is reporting failure
                // 2. Thread is NOT actively resizing
                // 3. Window is not minimized or hidden
                if (FAILED(hr) && (!threadManager.threadVars.bIsResizing.load()))
                {
                    if (!sysUtils.IsWindowMinimized()) // <- Confirm window state first!
                    {
                        debug.logLevelMessage(LogLevel::LOG_WARNING, L"[DX11Renderer] Device removed detected. Attempting Reset.");
                        threadManager.threadVars.bIsResizing.store(true);
                        Resize(iOrigWidth, iOrigHeight);
                        ResumeLoader();
                        threadManager.threadVars.bIsResizing.store(false);
                        return;
                    }
                    else
                    {
                        debug.logLevelMessage(LogLevel::LOG_WARNING, L"[DX11Renderer] Device removed but window minimized. Skipping reset.");
                    }
                }
            }

            // State that we are now rendering
            threadManager.threadVars.bIsRendering.store(true);

            // Clear the render target and depth stencil view before we start rendering
            try
            {
                if (m_d3dContext && threadManager.TryLock(D2DLockName, 100))
                {
                    m_d3dContext->ClearRenderTargetView(m_renderTargetView.Get(), clearColor);
                    m_d3dContext->ClearDepthStencilView(m_depthStencilView.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
                    threadManager.RemoveLock(D2DLockName);
                }
            }
            catch (const std::exception& e)
            {
                debug.logLevelMessage(LogLevel::LOG_ERROR, L"[RENDER] - RenderFrame Clear Failed: " + std::wstring(e.what(), e.what() + strlen(e.what())));
                #ifdef RENDERER_IS_THREAD
                       continue;
                #else
                       return;
                #endif
            }

            // Update the camera's jump animation
            myCamera.UpdateJumpAnimation();
                // Update the cameras viewmatrix
//                myCamera.UpdateViewMatrix();

            // Update effects
            static auto myLastTime = std::chrono::high_resolution_clock::now();
            auto myCurrentTime = std::chrono::high_resolution_clock::now();
            myLastTime = myCurrentTime;

            // Get current frame start time
            auto now = std::chrono::steady_clock::now();

            // Compute delta time in seconds as a float
            float deltaTime = std::chrono::duration<float>(now - lastFrameTime).count();

            // Update the timer for the next frame
            lastFrameTime = now;

            //            GetCursorPos(&cursorPos);
            //            ScreenToClient(hWnd, &cursorPos);
            //            myMouseCoords.x = cursorPos.x;
            //            myMouseCoords.y = cursorPos.y;
                        // Scale the mouse coordinates
            //            auto [scaledX, scaledY] = sysUtils.ScaleMouseCoordinates(cursorPos.x, cursorPos.x, iOrigWidth, iOrigHeight, width, height);
            //            float x = float(scaledX);
            //			float y = float(scaledY);

                        // We now need to determine which Scene we are to render for?
            m_d3dContext->OMSetRenderTargets(1, m_renderTargetView.GetAddressOf(), m_depthStencilView.Get());
            switch (scene.stSceneType)
            {
            case SceneType::SCENE_SPLASH:
            {
                // Try to acquire the D2D render lock with a 100ms timeout
                if (threadManager.TryLock(D2DLockName, 100))                        // Signal that Direct2D is now busy with rendering operations.
                {
                    // Inform Direct2D we are to begin drawing calls.
                    m_d2dRenderTarget->BeginDraw();

                    // Present Splash Screen
                    if ((m_d2dTextures[int(BlitObj2DIndexType::IMG_SPLASH1)]))
                        Blit2DObjectToSize(BlitObj2DIndexType::IMG_SPLASH1, 0, 0, iOrigWidth, iOrigHeight);

                    m_d2dRenderTarget->EndDraw();
                    // Release the D2D render lock
                    threadManager.RemoveLock(D2DLockName);
                }
                else
                {
                    debug.logLevelMessage(LogLevel::LOG_WARNING, L"Could not acquire D2D render lock - skipping D2D operations");
                }

                break;
            }

            case SceneType::SCENE_INTRO_MOVIE:
            {
                // Try to acquire the D2D render lock with a 100ms timeout
                if (threadManager.TryLock(D2DLockName, 100))                        // Signal that Direct2D is now busy with rendering operations.
                {
                    // Inform Direct2D we are to begin drawing calls.
                    m_d2dRenderTarget->BeginDraw();
                    // Is the movie playing
                    if (moviePlayer.IsPlaying()) {
                        // Update the movie frame
                        moviePlayer.UpdateFrame();

                        // Render the movie to fill the screen
                        moviePlayer.Render(Vector2(0, 0), Vector2(iOrigWidth, iOrigHeight));

                        // Draw Logo image.
                        Blit2DObject(BlitObj2DIndexType::IMG_COMPANYLOGO, 0, iOrigHeight - 47);

                        // Check to see if the user has pressed the space bar
                        if (GetAsyncKeyState(' ') & 0x8000)
                        {
                            // Stop Playback as this will switch the scene!
                            moviePlayer.Stop();
                            scene.bSceneSwitching = true;
                            fxManager.FadeToBlack(1.0f, 0.06f);
                        }
                    }

                    m_d2dRenderTarget->EndDraw();
                    // Release the D2D render lock
                    threadManager.RemoveLock(D2DLockName);
                }
                else
                {
                    debug.logLevelMessage(LogLevel::LOG_WARNING, L"Could not acquire D2D render lock - skipping D2D operations");
                }

                break;
            }

            case SceneType::SCENE_GAMEPLAY:
            {
                // Clear buffers
                if (m_d3dContext)
                {
                    // 3D Rendering

                    // Populate the constant buffer data
                    ConstantBuffer cb;
                    cb.viewMatrix = myCamera.GetViewMatrix();
                    cb.projectionMatrix = myCamera.GetProjectionMatrix();
                    cb.cameraPosition = myCamera.GetPosition();

                    // Update the constant buffer
                    D3D11_MAPPED_SUBRESOURCE mappedResource;
                    HRESULT hr = m_d3dContext->Map(m_cameraConstantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
                    if (FAILED(hr)) {
                        debug.logLevelMessage(LogLevel::LOG_ERROR, L"UpdateConstantBuffer: Failed to map constant buffer.");
                        return;
                    }

                    // Copy the data to the constant buffer
                    memcpy(mappedResource.pData, &cb, sizeof(ConstantBuffer));
                    m_d3dContext->Unmap(m_cameraConstantBuffer.Get(), 0);
                    // Bind the constant buffer to the vertex shader
                    m_d3dContext->VSSetConstantBuffers(SLOT_CONST_BUFFER, 1, m_cameraConstantBuffer.GetAddressOf());

                    // Debug constant buffer for debugging pixel shader.
                    #if defined(_DEBUG_RENDERER_) && defined(_DEBUG) && defined(_DEBUG_PIXSHADER_)
                        if (GetAsyncKeyState('1') & 0x8000) SetDebugMode(0); // Run as Normally would! Production view!
                        if (GetAsyncKeyState('2') & 0x8000) SetDebugMode(1); // Normals Only
                        if (GetAsyncKeyState('3') & 0x8000) SetDebugMode(2); // Texture Only
                        if (GetAsyncKeyState('4') & 0x8000) SetDebugMode(3); // Lighting Only
                        if (GetAsyncKeyState('5') & 0x8000) SetDebugMode(4); // Specular Only
                        if (GetAsyncKeyState('6') & 0x8000) SetDebugMode(5); // Attenuation/Normals
                        if (GetAsyncKeyState('7') & 0x8000) SetDebugMode(6); // Shadows Only
                        if (GetAsyncKeyState('8') & 0x8000) SetDebugMode(7); // Reflection Only
                        if (GetAsyncKeyState('9') & 0x8000) SetDebugMode(8); // Metallic Only
                    #endif

                    // Ensure all loading is complete before rendering models and lighting.
                    if (threadManager.threadVars.bLoaderTaskFinished.load())
                    {
                        // (Add 3D object rendering here)
                        #if defined(_RENDERER_WIREFRAME_)
                            m_d3dContext->RSSetState(wireframeRS.Get());
                        #endif

                        // This block is used if you need to test your Pipeline.
                        #if defined(_DEBUG_RENDERER_) && defined(_SIMPLE_TRIANGLE_) && defined(_DEBUG)
                            TestDrawTriangle();                                                                                     // Test triangle for rendering (debug purposes)
                        #endif

                        for (int i = 0; i < MAX_MODELS; ++i)
                        {
                            if (scene.scene_models[i].m_isLoaded)
                            {
                                scene.scene_models[i].m_modelInfo.fxActive = false;                                                 // Force this off before render call for now.
                                scene.scene_models[i].m_modelInfo.viewMatrix = myCamera.GetViewMatrix();
                                scene.scene_models[i].m_modelInfo.projectionMatrix = myCamera.GetProjectionMatrix();
                                scene.scene_models[i].m_modelInfo.cameraPosition = myCamera.GetPosition();
                                scene.scene_models[i].UpdateAnimation(deltaTime);                                                   // Update animation for this model
                                scene.scene_models[i].Render(m_d3dContext.Get(), deltaTime);
                            }
                        }
                    }

                    // Animate Lights Per Frame
    //                    lightsManager.AnimateLights(deltaTime); // TODO: Replace with real deltaTime

                    // Update the General Lighting, not object lighting as
                    // this is handle elsewhere (ie. Model::Render())
                    std::vector<LightStruct> globalLights = lightsManager.GetAllLights();

                    GlobalLightBuffer glb = {};
                    glb.numLights = static_cast<int>(globalLights.size());
                    if (glb.numLights > MAX_GLOBAL_LIGHTS)
                        glb.numLights = MAX_GLOBAL_LIGHTS;

                    for (int i = 0; i < glb.numLights; ++i)
                    {
                        //                    globalLights[i].intensity = 1.0f;
                        //                    globalLights[i].baseIntensity = 1.2f;
                        memcpy(&glb.lights[i], &globalLights[i], sizeof(LightStruct)); // Copy the light data
#if defined(_DEBUG_RENDERER_) && defined(_DEBUG_LIGHTING_)
                        debug.logDebugMessage(LogLevel::LOG_INFO, L"Global Lights: %d", glb.numLights);
                        debug.logDebugMessage(LogLevel::LOG_INFO,
                            L"Global Light[%zu] active=%d intensity=%.2f color=(%.2f %.2f %.2f) range=%.2f type=%d position=(%.2f, %.2f, %.2f)",
                            i, glb.lights[i].active, glb.lights[i].intensity,
                            glb.lights[i].color.x, glb.lights[i].color.y, glb.lights[i].color.z,
                            glb.lights[i].range, glb.lights[i].type,
                            glb.lights[i].position.x, glb.lights[i].position.y, glb.lights[i].position.z);
#endif
                    }

                    // Upload to GPU
                    D3D11_MAPPED_SUBRESOURCE mapped;
                    hr = m_d3dContext->Map(m_globalLightBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
                    if (SUCCEEDED(hr)) {
                        memcpy(mapped.pData, &glb, sizeof(GlobalLightBuffer));
                        m_d3dContext->Unmap(m_globalLightBuffer.Get(), 0);
                    }

                    // Bind to PS slot b3
                    m_d3dContext->PSSetConstantBuffers(SLOT_GLOBAL_LIGHT_BUFFER, 1, m_globalLightBuffer.GetAddressOf());

                } // End of 3D Rendering

                break;
            }
            }

            // 2D Rendering
            if (m_d2dRenderTarget)
            {
                // Check status of Direct2D to see if available for use.
                if (threadManager.TryLock(D2DLockName, 100))                        // Signal that Direct2D is now busy with rendering operations.
                {
                    // Inform Direct2D we are to begin drawing calls.
                    m_d2dRenderTarget->BeginDraw();

                    // (Add 2D UI/text rendering here)
/*
                    for (int iX = 0; iX < MAX_2D_IMG_QUEUE_OBJS; iX++)
                    {
                        for (int iPhase = int(BlitPhaseLevel::PHASE_LEVEL_1); iPhase <= int(BlitPhaseLevel::PHASE_LEVEL_5); iPhase++)
                        {
                            if (My2DBlitQueue[iX].bInUse && int(My2DBlitQueue[iX].BlitPhase) == iPhase)
                            {
                                Blit2DObject(My2DBlitQueue[iX].BlitObjDetails.iBlitID, My2DBlitQueue[iX].BlitObjDetails.iBlitX, My2DBlitQueue[iX].BlitObjDetails.iBlitY);
                            }
                        }
                    }
*/
                    if ((!threadManager.threadVars.bIsShuttingDown.load()) &&
                        (!bIsMinimized.load()) &&
                        (!threadManager.threadVars.bIsResizing.load()) &&
                        (bIsInitialized.load()))
                    {
                        switch (scene.stSceneType)
                        {
                            case SceneType::SCENE_INTRO:
                            {
                                // Ensure all loading is complete before rendering models and lighting.
                                if (threadManager.threadVars.bLoaderTaskFinished.load())
                                {
                                    myCamera.SetYawPitch(0.285f, -0.22f);
                                    // Draw background image first.
                                    Blit2DObjectToSize(BlitObj2DIndexType::IMG_GAMEINTRO1, 0, 0, iOrigWidth, iOrigHeight);
                                    // Draw Logo image.
                                    Blit2DObject(BlitObj2DIndexType::IMG_COMPANYLOGO, 0, iOrigHeight - 47);
                                    // Render our 3D starfield.
                                    fxManager.RenderFX(fxManager.starfieldID, m_d3dContext.Get(), myCamera.GetViewMatrix());
                                }
                                break;
                            }
                        }
                    }

                    // Are we to display the FPS so we can see 
                    // our render frame rate (Handy for debugging/optimization requirements)
                    if (USE_FPS_DISPLAY)
                    {
                        static auto lastFrameTime = std::chrono::steady_clock::now();
                        static auto lastFPSTime = lastFrameTime;
                        static int frameCounter = 0;

                        auto currentTime = std::chrono::steady_clock::now();
                        float deltaTime = std::chrono::duration<float>(currentTime - lastFrameTime).count();  // Frame delta time
                        float elapsedForFPS = std::chrono::duration<float>(currentTime - lastFPSTime).count();

                        lastFrameTime = currentTime;
                        frameCounter++;

                        if (elapsedForFPS >= 1.0f)
                        {
                            fps = static_cast<float>(frameCounter) / elapsedForFPS;
                            frameCounter = 0;
                            lastFPSTime = currentTime;
                        }

                        XMFLOAT3 Coords;
                        Coords = myCamera.GetPosition();
                        std::wstring fpsText = L"FPS: " + std::to_wstring(fps) + L"\nMOUSE: x" + std::to_wstring(myMouseCoords.x) + L", y" + std::to_wstring(myMouseCoords.y);
                        fpsText = fpsText + L"\nCamera X: " + std::to_wstring(Coords.x) + L", Y: " + std::to_wstring(Coords.y) + L", Z: " + std::to_wstring(Coords.z) +
                            L", Yaw: " + std::to_wstring(myCamera.m_yaw) + L", Pitch: " + std::to_wstring(myCamera.m_pitch) + L"\n";
                        fpsText = fpsText + L"Global Light Count: " + std::to_wstring(lightsManager.GetLightCount()) + L"\n";

                        DrawMyText(fpsText, Vector2(0, 0), MyColor(255, 255, 255, 255), 10.0f);
                    }

                    if (!threadManager.threadVars.bLoaderTaskFinished.load())
                    {
                        delay++;
                        if (delay > 5)
                        {
                            loadIndex++;
                            if (loadIndex > 9) { loadIndex = 0; }
                            delay = 0;
                        }

                        if ((m_d2dTextures[int(BlitObj2DIndexType::BG_LOADER_CIRCLE)]))
                        {
                            iPosX = loadIndex << 5;
                            Blit2DObjectAtOffset(BlitObj2DIndexType::BG_LOADER_CIRCLE, width - 32, height - 32, iPosX, 0, 32, 32);
                        }
                    }

                    // Apply 2D Effects.
                    fxManager.Render2D();
                    // Now render any windows that are to be displayed.
                    guiManager.Render();

                    // Check X & Y Mouse Cursor Boundaries
//                    if (myMouseCoords.x >= iOrigWidth) { myMouseCoords.x = iOrigWidth - 3; }
//                    if (cursorPos.y >= iOrigHeight) { cursorPos.y = iOrigHeight - 3; }
                    if ((m_d2dTextures[int(BlitObj2DIndexType::BLIT_ALWAYS_CURSOR)]))
                        Blit2DObject(BlitObj2DIndexType::BLIT_ALWAYS_CURSOR, myMouseCoords.x, myMouseCoords.y);

                    // State we are NOW finished With Direct2D Drawing.
                    try
                    {
                        HRESULT hr = m_d2dRenderTarget->EndDraw();
                        if (FAILED(hr))
                        {
                            debug.logLevelMessage(LogLevel::LOG_ERROR, L"Direct2D EndDraw failed.");
                        }

                        // Render effects (Fader) (should be after normal rendering but before present)
                        fxManager.Render();
                    }
                    catch (const std::exception& e)
                    {
                    }

                    // Release the D2D render lock
                    threadManager.RemoveLock(D2DLockName);
                }
                else
                {
                    debug.logLevelMessage(LogLevel::LOG_WARNING, L"Could not acquire D2D render lock - skipping D2D operations");
                }
            }

            // Present frame
//			WaitForGPUToFinish();
#if defined(_DEBUG_RENDERER_)
            debug.logLevelMessage(LogLevel::LOG_INFO, L"Presenting frame...");
#endif
            if (m_swapChain) { m_swapChain->Present(config.myConfig.enableVSync ? 1 : 0, 0); }

            // State that we are no longer rendering    
            threadManager.threadVars.bIsRendering.store(false);
#ifdef RENDERER_IS_THREAD
        }
#endif

        // Make sure to remove the lock
        threadManager.RemoveLock(renderFrameLockName);
    }
    catch (const std::exception& e)
    {
        // Make sure to remove the lock even if an exception occurs
        threadManager.RemoveLock(renderFrameLockName);
        debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"[RENDER] - " + std::wstring(e.what(), e.what() + strlen(e.what())));
    }

#ifdef RENDERER_IS_THREAD
    debug.logLevelMessage(LogLevel::LOG_WARNING, L"Render Thread Exiting.");
#endif
}

#pragma warning(pop)

#endif