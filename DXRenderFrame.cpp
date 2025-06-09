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
    // ENHANCED SAFE-GUARDS - Check all critical conditions before proceeding
    if (bHasCleanedUp || !m_d3dDevice || !m_d3dContext || !m_cameraConstantBuffer) {
        #if defined(_DEBUG_RENDERER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_DEBUG, L"[RENDERFRAME] Early exit - missing critical resources");
        #endif
        return;
    }
    
    // Check for shutdown, minimized, resizing, or already rendering states
    if (threadManager.threadVars.bIsShuttingDown.load() || bIsMinimized.load() || 
        threadManager.threadVars.bIsResizing.load() || !bIsInitialized.load()) {
        #if defined(_DEBUG_RENDERER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_DEBUG, L"[RENDERFRAME] Early exit - system state prevents rendering");
        #endif
        return;
    }

    // CRITICAL: Prevent multiple render operations and ensure exclusive DirectX access
    ThreadLockHelper exclusiveRenderLock(threadManager, "exclusive_render_operation", 50);
    if (!exclusiveRenderLock.IsLocked()) {
        #if defined(_DEBUG_RENDERER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_DEBUG, L"[RENDERFRAME] Could not acquire exclusive render lock - skipping frame");
        #endif
        return;
    }

    // Double-check rendering state after acquiring lock
    if (threadManager.threadVars.bIsRendering.load()) {
        #if defined(_DEBUG_RENDERER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"[RENDERFRAME] Another render operation already active - aborting");
        #endif
        return;
    }

    try
    {
        exceptionHandler.RecordFunctionCall("RenderFrame");             // Record function call for crash analysis
        
        // Set rendering state atomically to indicate we are now rendering
        threadManager.threadVars.bIsRendering.store(true);

        #if defined(_DEBUG_RENDERER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_DEBUG, L"[RENDERFRAME] Beginning render operation");
        #endif

        // Initialize local variables for rendering
        HRESULT hr = S_OK;                                              // DirectX operation result
        HWND hWnd = hwnd;                                               // Window handle for client area calculations

        // Save the current Direct3D state for restoration
        ComPtr<ID3D11RenderTargetView> previousRenderTargetView;        // Previous render target backup
        ComPtr<ID3D11DepthStencilView> previousDepthStencilView;        // Previous depth stencil backup
        FLOAT clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };             // Black clear color with full alpha
        D3D11_VIEWPORT previousViewport;                                // Previous viewport backup
        UINT numViewports = 1;                                          // Number of viewports to retrieve
        D3D11_VIEWPORT viewport = {};                                   // Current viewport configuration
        RECT rc;                                                        // Client rectangle for viewport calculation

#ifdef RENDERER_IS_THREAD
        // Thread-based rendering loop - check thread status continuously
        ThreadStatus status = threadManager.GetThreadStatus(THREAD_RENDERER);
        while (((status == ThreadStatus::Running) || (status == ThreadStatus::Paused)) && 
               (!threadManager.threadVars.bIsShuttingDown.load()))
        {
            // Update thread status for current iteration
            status = threadManager.GetThreadStatus(THREAD_RENDERER);
            
            // Handle paused state - yield CPU and continue loop
            if (status == ThreadStatus::Paused)
            {
                threadManager.threadVars.bIsRendering.store(false);     // Clear rendering flag during pause
                std::this_thread::sleep_for(std::chrono::milliseconds(1)); // Brief yield to prevent CPU spinning
                continue;                                               // Skip to next iteration
            }

            // Verify we can still render after status check
            if (threadManager.threadVars.bIsResizing.load() || bIsMinimized.load()) {
                threadManager.threadVars.bIsRendering.store(false);     // Clear rendering flag
                std::this_thread::sleep_for(std::chrono::milliseconds(10)); // Longer yield during resize/minimize
                continue;                                               // Skip to next iteration
            }

            // Confirm rendering state is active
            threadManager.threadVars.bIsRendering.store(true);
#endif

            // CRITICAL: Check device state and handle device removal
            if (m_d3dDevice)
            {
                HRESULT deviceStatus = m_d3dDevice->GetDeviceRemovedReason(); // Check for device removal

                // Only attempt device reset if conditions are met
                if (FAILED(deviceStatus) && (!threadManager.threadVars.bIsResizing.load()))
                {
                    // Verify window is not minimized before attempting device reset
                    if (!sysUtils.IsWindowMinimized()) 
                    {
                        #if defined(_DEBUG_RENDERER_) && defined(_DEBUG)
                            debug.logDebugMessage(LogLevel::LOG_WARNING, L"[RENDERFRAME] Device removed detected (0x%08X). Attempting reset.", deviceStatus);
                        #endif
                        
                        threadManager.threadVars.bIsResizing.store(true); // Set resize flag for reset operation
                        
                        try {
                            Resize(iOrigWidth, iOrigHeight);             // Attempt device reset through resize
                            ResumeLoader();                              // Reload resources after reset
                        }
                        catch (const std::exception& e) {
                            #if defined(_DEBUG_RENDERER_) && defined(_DEBUG)
                                debug.logDebugMessage(LogLevel::LOG_ERROR, L"[RENDERFRAME] Device reset failed: %hs", e.what());
                            #endif
                        }
                        
                        threadManager.threadVars.bIsResizing.store(false); // Clear resize flag
                        threadManager.threadVars.bIsRendering.store(false); // Clear rendering flag
                        return;                                         // Exit after device reset attempt
                    }
                    else
                    {
                        #if defined(_DEBUG_RENDERER_) && defined(_DEBUG)
                            debug.logLevelMessage(LogLevel::LOG_DEBUG, L"[RENDERFRAME] Device removed but window minimized. Skipping reset.");
                        #endif
                        threadManager.threadVars.bIsRendering.store(false); // Clear rendering flag
                        return;                                         // Exit without reset
                    }
                }
            }

            // STEP 1: Save current DirectX state for proper restoration
            try {
                m_d3dContext->OMGetRenderTargets(1, previousRenderTargetView.GetAddressOf(), previousDepthStencilView.GetAddressOf());
                m_d3dContext->RSGetViewports(&numViewports, &previousViewport);
            }
            catch (const std::exception& e) {
                #if defined(_DEBUG_RENDERER_) && defined(_DEBUG)
                    debug.logDebugMessage(LogLevel::LOG_ERROR, L"[RENDERFRAME] Failed to save DirectX state: %hs", e.what());
                #endif
                threadManager.threadVars.bIsRendering.store(false);     // Clear rendering flag on error
                return;                                                 // Exit on state save failure
            }

            // STEP 2: Calculate viewport dimensions based on fullscreen/windowed mode
            if (!winMetrics.isFullScreen)
            {
                GetClientRect(hWnd, &rc);                               // Get windowed client area
            }
            else
            {
                rc = winMetrics.monitorFullArea;                        // Use fullscreen monitor area
            }

            // Configure viewport with calculated dimensions
            float width = float(rc.right - rc.left);                   // Calculate viewport width
            float height = float(rc.bottom - rc.top);                  // Calculate viewport height
            viewport.Width = width;                                     // Set viewport width
            viewport.Height = height;                                   // Set viewport height
            viewport.MinDepth = 0.0f;                                   // Set minimum depth value
            viewport.MaxDepth = 1.0f;                                   // Set maximum depth value
            viewport.TopLeftX = 0;                                      // Set viewport X origin
            viewport.TopLeftY = 0;                                      // Set viewport Y origin
            m_d3dContext->RSSetViewports(1, &viewport);                 // Apply viewport to rendering context

            // STEP 3: Configure rasterizer state based on wireframe mode
            static Microsoft::WRL::ComPtr<ID3D11RasterizerState> wireframeRS; // Static wireframe state

#if defined(_DEBUG_RENDER_WIREFRAME_)
            // Debug wireframe rendering mode
            if (bWireframeMode && m_wireframeState)
            {
                m_d3dContext->RSSetState(m_wireframeState.Get());       // Apply wireframe rasterizer state
            }
            else if (m_rasterizerState)
            {
                m_d3dContext->RSSetState(m_rasterizerState.Get());      // Apply standard rasterizer state
            }
#else
            // Production rendering mode - always use standard rasterizer
            if (m_rasterizerState) {
                m_d3dContext->RSSetState(m_rasterizerState.Get());      // Apply standard rasterizer state
            }
#endif

            // STEP 4: Clear render targets with thread-safe access
            try
            {
                // Acquire D2D lock to prevent conflicts with 2D rendering operations
                ThreadLockHelper d2dClearLock(threadManager, D2DLockName, 100);
                if (d2dClearLock.IsLocked())
                {
                    // Clear render target with black color
                    m_d3dContext->ClearRenderTargetView(m_renderTargetView.Get(), clearColor);
                    // Clear depth stencil with maximum depth and zero stencil
                    m_d3dContext->ClearDepthStencilView(m_depthStencilView.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
                    
                    #if defined(_DEBUG_RENDERER_) && defined(_DEBUG)
                        debug.logLevelMessage(LogLevel::LOG_DEBUG, L"[RENDERFRAME] Render targets cleared successfully");
                    #endif
                }
                else
                {
                    #if defined(_DEBUG_RENDERER_) && defined(_DEBUG)
                        debug.logLevelMessage(LogLevel::LOG_WARNING, L"[RENDERFRAME] Could not acquire D2D lock for clearing - skipping clear");
                    #endif
                }
            }
            catch (const std::exception& e)
            {
                #if defined(_DEBUG_RENDERER_) && defined(_DEBUG)
                    debug.logDebugMessage(LogLevel::LOG_ERROR, L"[RENDERFRAME] Clear operation failed: %hs", e.what());
                #endif
                threadManager.threadVars.bIsRendering.store(false);     // Clear rendering flag on error
#ifdef RENDERER_IS_THREAD
                continue;                                               // Continue thread loop on error
#else
                return;                                                 // Exit on error in non-threaded mode
#endif
            }

            // STEP 5: Update camera animation and timing
            myCamera.UpdateJumpAnimation();                             // Update camera jump animation state

            // Calculate frame timing for smooth animation
            auto now = std::chrono::steady_clock::now();                // Get current high-resolution time
            float deltaTime = std::chrono::duration<float>(now - lastFrameTime).count(); // Calculate delta time in seconds
            lastFrameTime = now;                                        // Update frame time for next frame

            // STEP 6: Set render targets for 3D rendering
            m_d3dContext->OMSetRenderTargets(1, m_renderTargetView.GetAddressOf(), m_depthStencilView.Get());

            // STEP 7: Scene-specific 3D rendering
            switch (scene.stSceneType)
            {
                case SceneType::SCENE_SPLASH:
                {
                    // Splash screen - minimal 3D rendering, primarily 2D
                    #if defined(_DEBUG_RENDERER_) && defined(_DEBUG)
                        debug.logLevelMessage(LogLevel::LOG_DEBUG, L"[RENDERFRAME] Rendering splash scene");
                    #endif
                    break;
                }

                case SceneType::SCENE_INTRO_MOVIE:
                {
                    // Movie introduction - minimal 3D rendering
                    #if defined(_DEBUG_RENDERER_) && defined(_DEBUG)
                        debug.logLevelMessage(LogLevel::LOG_DEBUG, L"[RENDERFRAME] Rendering movie intro scene");
                    #endif
                    break;
                }

                case SceneType::SCENE_GAMEPLAY:
                {
                    // Primary gameplay scene - full 3D rendering pipeline
                    if (m_d3dContext && m_cameraConstantBuffer)
                    {
                        #if defined(_DEBUG_RENDERER_) && defined(_DEBUG)
                            debug.logLevelMessage(LogLevel::LOG_DEBUG, L"[RENDERFRAME] Rendering gameplay scene - 3D pipeline");
                        #endif

                        // STEP 7A: Update camera constant buffer for 3D rendering
                        ConstantBuffer cb;                               // Camera constant buffer data
                        cb.viewMatrix = myCamera.GetViewMatrix();       // Current view matrix from camera
                        cb.projectionMatrix = myCamera.GetProjectionMatrix(); // Current projection matrix
                        cb.cameraPosition = myCamera.GetPosition();     // Current camera world position

                        // Map and update the constant buffer
                        D3D11_MAPPED_SUBRESOURCE mappedResource;        // Mapped resource for buffer update
                        HRESULT hr = m_d3dContext->Map(m_cameraConstantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
                        if (SUCCEEDED(hr)) {
                            memcpy(mappedResource.pData, &cb, sizeof(ConstantBuffer)); // Copy camera data to GPU
                            m_d3dContext->Unmap(m_cameraConstantBuffer.Get(), 0); // Unmap the buffer
                            // Bind the constant buffer to vertex shader slot 0
                            m_d3dContext->VSSetConstantBuffers(SLOT_CONST_BUFFER, 1, m_cameraConstantBuffer.GetAddressOf());
                        } else {
                            #if defined(_DEBUG_RENDERER_) && defined(_DEBUG)
                                debug.logDebugMessage(LogLevel::LOG_ERROR, L"[RENDERFRAME] Failed to map camera constant buffer (0x%08X)", hr);
                            #endif
                        }

                        // STEP 7B: Debug pixel shader controls (debug builds only)
                        #if defined(_DEBUG_RENDERER_) && defined(_DEBUG) && defined(_DEBUG_PIXSHADER_)
                            // Real-time pixel shader debug mode switching
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

                        // STEP 7C: Render 3D models if loading is complete
                        if (threadManager.threadVars.bLoaderTaskFinished.load())
                        {
                            #if defined(_DEBUG_RENDERER_) && defined(_DEBUG)
                                debug.logLevelMessage(LogLevel::LOG_DEBUG, L"[RENDERFRAME] Rendering 3D models");
                            #endif

                            // Optional wireframe rendering for debug purposes
                            #if defined(_RENDERER_WIREFRAME_)
                                if (wireframeRS) {
                                    m_d3dContext->RSSetState(wireframeRS.Get()); // Apply wireframe rasterizer
                                }
                            #endif

                            // Debug triangle test for pipeline verification
                            #if defined(_DEBUG_RENDERER_) && defined(_SIMPLE_TRIANGLE_) && defined(_DEBUG)
                                TestDrawTriangle();                      // Render test triangle for pipeline verification
                            #endif

                            // Render all loaded scene models
                            for (int i = 0; i < MAX_MODELS; ++i)
                            {
                                if (scene.scene_models[i].m_isLoaded)   // Only render loaded models
                                {
                                    // Configure model rendering parameters
                                    scene.scene_models[i].m_modelInfo.fxActive = false; // Disable FX for now
                                    scene.scene_models[i].m_modelInfo.viewMatrix = myCamera.GetViewMatrix();
                                    scene.scene_models[i].m_modelInfo.projectionMatrix = myCamera.GetProjectionMatrix();
                                    scene.scene_models[i].m_modelInfo.cameraPosition = myCamera.GetPosition();
                                    
                                    // Update model animation with frame delta time
                                    scene.scene_models[i].UpdateAnimation(deltaTime);
                                    // Render the model to the current context
                                    scene.scene_models[i].Render(m_d3dContext.Get(), deltaTime);
                                }
                            }
                        }

                        // STEP 7D: Update global lighting system
                        std::vector<LightStruct> globalLights = lightsManager.GetAllLights(); // Get all global lights

                        GlobalLightBuffer glb = {};                     // Global light buffer for GPU
                        glb.numLights = static_cast<int>(globalLights.size()); // Set number of lights
                        if (glb.numLights > MAX_GLOBAL_LIGHTS)          // Clamp to maximum supported lights
                            glb.numLights = MAX_GLOBAL_LIGHTS;

                        // Copy light data to GPU buffer
                        for (int i = 0; i < glb.numLights; ++i)
                        {
                            memcpy(&glb.lights[i], &globalLights[i], sizeof(LightStruct)); // Copy light structure

                            #if defined(_DEBUG_RENDERER_) && defined(_DEBUG_LIGHTING_)
                                // Debug logging for light information
                                debug.logDebugMessage(LogLevel::LOG_DEBUG,
                                    L"[RENDERFRAME] Light[%d] active=%d intensity=%.2f color=(%.2f %.2f %.2f) range=%.2f type=%d pos=(%.2f, %.2f, %.2f)",
                                    i, glb.lights[i].active, glb.lights[i].intensity,
                                    glb.lights[i].color.x, glb.lights[i].color.y, glb.lights[i].color.z,
                                    glb.lights[i].range, glb.lights[i].type,
                                    glb.lights[i].position.x, glb.lights[i].position.y, glb.lights[i].position.z);
                            #endif
                        }

                        // Upload global light buffer to GPU
                        D3D11_MAPPED_SUBRESOURCE mapped;                // Mapped resource for light buffer
                        hr = m_d3dContext->Map(m_globalLightBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
                        if (SUCCEEDED(hr)) {
                            memcpy(mapped.pData, &glb, sizeof(GlobalLightBuffer)); // Copy light data to GPU
                            m_d3dContext->Unmap(m_globalLightBuffer.Get(), 0); // Unmap the buffer
                            // Bind global light buffer to pixel shader slot 3
                            m_d3dContext->PSSetConstantBuffers(SLOT_GLOBAL_LIGHT_BUFFER, 1, m_globalLightBuffer.GetAddressOf());
                        }
                    }
                    break;
                }
            }

            // STEP 8: 2D Rendering with enhanced thread safety
            if (m_d2dRenderTarget)
            {
                // Acquire D2D rendering lock to prevent conflicts
                ThreadLockHelper d2dRenderLock(threadManager, D2DLockName, 100);
                if (d2dRenderLock.IsLocked())
                {
                    #if defined(_DEBUG_RENDERER_) && defined(_DEBUG)
                        debug.logLevelMessage(LogLevel::LOG_DEBUG, L"[RENDERFRAME] Beginning 2D rendering operations");
                    #endif

                    // Begin Direct2D drawing operations
                    try {
                        m_d2dRenderTarget->BeginDraw();                  // Start Direct2D rendering session
                    }
                    catch (const std::exception& e) {
                        #if defined(_DEBUG_RENDERER_) && defined(_DEBUG)
                            debug.logDebugMessage(LogLevel::LOG_ERROR, L"[RENDERFRAME] Failed to begin 2D draw: %hs", e.what());
                        #endif
                        threadManager.threadVars.bIsRendering.store(false); // Clear rendering flag
                        return;                                         // Exit on 2D begin failure
                    }

                    // Verify system state before 2D rendering
                    if ((!threadManager.threadVars.bIsShuttingDown.load()) &&
                        (!bIsMinimized.load()) &&
                        (!threadManager.threadVars.bIsResizing.load()) &&
                        (bIsInitialized.load()))
                    {
                        // Scene-specific 2D rendering
                        switch (scene.stSceneType)
                        {
                            case SceneType::SCENE_SPLASH:
                            {
                               // Splash screen 2D
                               #if defined(_DEBUG_RENDERER_) && defined(_DEBUG)
                                    debug.logLevelMessage(LogLevel::LOG_DEBUG, L"[RENDERFRAME] Rendering splash screen 2D elements");
                               #endif

                               // Render splash screen background image
                               if ((m_d2dTextures[int(BlitObj2DIndexType::IMG_SPLASH1)]))
                                   Blit2DObjectToSize(BlitObj2DIndexType::IMG_SPLASH1, 0, 0, iOrigWidth, iOrigHeight);
                               break;
                           }

                           case SceneType::SCENE_INTRO_MOVIE:
                           {
                               #if defined(_DEBUG_RENDERER_) && defined(_DEBUG)
                                   debug.logLevelMessage(LogLevel::LOG_DEBUG, L"[RENDERFRAME] Rendering movie intro 2D elements");
                               #endif

                               // Movie playback 2D rendering
                               if (moviePlayer.IsPlaying()) {
                                   // Update the movie frame for current time
                                   moviePlayer.UpdateFrame();

                                   // Render the movie to fill the entire screen
                                   moviePlayer.Render(Vector2(0, 0), Vector2(iOrigWidth, iOrigHeight));

                                   // Draw company logo overlay
                                   if (m_d2dTextures[int(BlitObj2DIndexType::IMG_COMPANYLOGO)]) {
                                       Blit2DObject(BlitObj2DIndexType::IMG_COMPANYLOGO, 0, iOrigHeight - 47);
                                   }

                                   // Check for spacebar input to skip movie
                                   if (GetAsyncKeyState(' ') & 0x8000)
                                   {
                                       // Stop movie playback to trigger scene transition
                                       moviePlayer.Stop();
                                       scene.bSceneSwitching = true;       // Flag scene transition
                                       fxManager.FadeToBlack(1.0f, 0.06f); // Start fade effect
                                   }
                               }
                               break;
                           }

                           case SceneType::SCENE_INTRO:
                           {
                               #if defined(_DEBUG_RENDERER_) && defined(_DEBUG)
                                   debug.logLevelMessage(LogLevel::LOG_DEBUG, L"[RENDERFRAME] Rendering game intro 2D elements");
                               #endif

                               // Game intro 2D rendering - only if loading complete
                               if (threadManager.threadVars.bLoaderTaskFinished.load())
                               {
                                   // Set camera for intro scene background
                                   myCamera.SetYawPitch(0.285f, -0.22f);
                                   
                                   // Draw background image first
                                   if (m_d2dTextures[int(BlitObj2DIndexType::IMG_GAMEINTRO1)]) {
                                       Blit2DObjectToSize(BlitObj2DIndexType::IMG_GAMEINTRO1, 0, 0, iOrigWidth, iOrigHeight);
                                   }
                                   
                                   // Draw company logo overlay
                                   if (m_d2dTextures[int(BlitObj2DIndexType::IMG_COMPANYLOGO)]) {
                                       Blit2DObject(BlitObj2DIndexType::IMG_COMPANYLOGO, 0, iOrigHeight - 47);
                                   }
                                   
                                   // Render 3D starfield effect if available
                                   if (fxManager.starfieldID > 0) {
                                       fxManager.RenderFX(fxManager.starfieldID, m_d3dContext.Get(), myCamera.GetViewMatrix());
                                   }
                               }
                               break;
                           }

                           case SceneType::SCENE_GAMEPLAY:
                           {
                               #if defined(_DEBUG_RENDERER_) && defined(_DEBUG)
                                   debug.logLevelMessage(LogLevel::LOG_DEBUG, L"[RENDERFRAME] Rendering gameplay 2D elements");
                               #endif
                               // Gameplay-specific 2D elements would go here
                               break;
                           }
                       }
                   }

                   // STEP 8A: Render FPS display and debug information (if enabled)
                   if (USE_FPS_DISPLAY)
                   {
                       // Calculate FPS using frame timing
                       static auto lastFrameTime = std::chrono::steady_clock::now(); // Last frame timestamp
                       static auto lastFPSTime = lastFrameTime;                // Last FPS calculation time
                       static int frameCounter = 0;                           // Frame counter for FPS calculation

                       auto currentTime = std::chrono::steady_clock::now();   // Current frame timestamp
                       float deltaTime = std::chrono::duration<float>(currentTime - lastFrameTime).count(); // Frame delta time
                       float elapsedForFPS = std::chrono::duration<float>(currentTime - lastFPSTime).count(); // Elapsed time for FPS

                       lastFrameTime = currentTime;                           // Update last frame time
                       frameCounter++;                                        // Increment frame counter

                       // Update FPS every second
                       if (elapsedForFPS >= 1.0f)
                       {
                           fps = static_cast<float>(frameCounter) / elapsedForFPS; // Calculate FPS
                           frameCounter = 0;                                   // Reset frame counter
                           lastFPSTime = currentTime;                          // Reset FPS timer
                       }

                       // Build comprehensive debug information string
                       XMFLOAT3 Coords = myCamera.GetPosition();              // Get current camera position
                       std::wstring fpsText = L"FPS: " + std::to_wstring(fps) + // FPS display
                           L"\nMOUSE: x" + std::to_wstring(myMouseCoords.x) + L", y" + std::to_wstring(myMouseCoords.y) + // Mouse coordinates
                           L"\nCamera X: " + std::to_wstring(Coords.x) + L", Y: " + std::to_wstring(Coords.y) + L", Z: " + std::to_wstring(Coords.z) + // Camera position
                           L", Yaw: " + std::to_wstring(myCamera.m_yaw) + L", Pitch: " + std::to_wstring(myCamera.m_pitch) + L"\n" + // Camera orientation
                           L"Global Light Count: " + std::to_wstring(lightsManager.GetLightCount()) + L"\n"; // Light count

                       // Render debug text in top-left corner
                       DrawMyText(fpsText, Vector2(0, 0), MyColor(255, 255, 255, 255), 10.0f);
                   }

                   // STEP 8B: Render loading indicator if assets are still loading
                   if (!threadManager.threadVars.bLoaderTaskFinished.load())
                   {
                       // Animate loading indicator
                       delay++;                                               // Increment animation delay counter
                       if (delay > 5)                                         // Update animation every 5 frames
                       {
                           loadIndex++;                                       // Move to next animation frame
                           if (loadIndex > 9) { loadIndex = 0; }             // Reset animation cycle
                           delay = 0;                                         // Reset delay counter
                       }

                       // Render animated loading circle
                       if ((m_d2dTextures[int(BlitObj2DIndexType::BG_LOADER_CIRCLE)]))
                       {
                           iPosX = loadIndex << 5;                            // Calculate X offset for animation frame
                           // Draw loading circle in bottom-right corner
                           Blit2DObjectAtOffset(BlitObj2DIndexType::BG_LOADER_CIRCLE, width - 32, height - 32, iPosX, 0, 32, 32);
                       }
                   }

                   // STEP 8C: Apply 2D post-processing effects
                   try {
                       fxManager.Render2D();                                  // Render 2D effects overlay
                   }
                   catch (const std::exception& e) {
                       #if defined(_DEBUG_RENDERER_) && defined(_DEBUG)
                           debug.logDebugMessage(LogLevel::LOG_ERROR, L"[RENDERFRAME] 2D effects rendering failed: %hs", e.what());
                       #endif
                   }

                   // STEP 8D: Render GUI windows and interface elements
                   try {
                       guiManager.Render();                                   // Render all GUI windows and controls
                   }
                   catch (const std::exception& e) {
                       #if defined(_DEBUG_RENDERER_) && defined(_DEBUG)
                           debug.logDebugMessage(LogLevel::LOG_ERROR, L"[RENDERFRAME] GUI rendering failed: %hs", e.what());
                       #endif
                   }

                   // STEP 8E: Render mouse cursor (always on top)
                   if ((m_d2dTextures[int(BlitObj2DIndexType::BLIT_ALWAYS_CURSOR)]))
                       Blit2DObject(BlitObj2DIndexType::BLIT_ALWAYS_CURSOR, myMouseCoords.x, myMouseCoords.y);

                   // STEP 8F: End Direct2D drawing operations
                   try
                   {
                       HRESULT hr = m_d2dRenderTarget->EndDraw();             // End Direct2D rendering session
                       if (FAILED(hr))
                       {
                           #if defined(_DEBUG_RENDERER_) && defined(_DEBUG)
                               debug.logDebugMessage(LogLevel::LOG_ERROR, L"[RENDERFRAME] Direct2D EndDraw failed (0x%08X)", hr);
                           #endif
                       }

                       // STEP 8G: Render post-processing effects (after 2D but before present)
                       fxManager.Render();                                    // Render fade effects and post-processing
                   }
                   catch (const std::exception& e)
                   {
                       #if defined(_DEBUG_RENDERER_) && defined(_DEBUG)
                           debug.logDebugMessage(LogLevel::LOG_ERROR, L"[RENDERFRAME] Post-processing effects failed: %hs", e.what());
                       #endif
                   }

                   #if defined(_DEBUG_RENDERER_) && defined(_DEBUG)
                       debug.logLevelMessage(LogLevel::LOG_DEBUG, L"[RENDERFRAME] 2D rendering operations completed");
                   #endif
               }
               else
               {
                   #if defined(_DEBUG_RENDERER_) && defined(_DEBUG)
                       debug.logLevelMessage(LogLevel::LOG_WARNING, L"[RENDERFRAME] Could not acquire D2D render lock - skipping 2D operations");
                   #endif
               }
           }

           // STEP 9: Present the final frame to the display
           try {
               #if defined(_DEBUG_RENDERER_) && defined(_DEBUG)
                   debug.logLevelMessage(LogLevel::LOG_DEBUG, L"[RENDERFRAME] Presenting frame to display");
               #endif
               
               // Present frame with VSync setting from configuration
               if (m_swapChain) { 
                   HRESULT presentResult = m_swapChain->Present(config.myConfig.enableVSync ? 1 : 0, 0);
                   
                   // Check for present failure
                   if (FAILED(presentResult)) {
                       #if defined(_DEBUG_RENDERER_) && defined(_DEBUG)
                           debug.logDebugMessage(LogLevel::LOG_ERROR, L"[RENDERFRAME] Present failed (0x%08X)", presentResult);
                       #endif
                   }
               }
           }
           catch (const std::exception& e) {
               #if defined(_DEBUG_RENDERER_) && defined(_DEBUG)
                   debug.logDebugMessage(LogLevel::LOG_ERROR, L"[RENDERFRAME] Present operation failed: %hs", e.what());
               #endif
           }

           // STEP 10: Clear rendering state for next frame
           threadManager.threadVars.bIsRendering.store(false);            // Clear rendering flag atomically

#ifdef RENDERER_IS_THREAD
       } // End of while loop for threaded rendering
#endif

       #if defined(_DEBUG_RENDERER_) && defined(_DEBUG)
           debug.logLevelMessage(LogLevel::LOG_DEBUG, L"[RENDERFRAME] Render operation completed successfully");
       #endif
   }
   catch (const std::exception& e)
   {
       // CRITICAL: Exception handling with proper cleanup
       #if defined(_DEBUG_RENDERER_) && defined(_DEBUG)
           debug.logDebugMessage(LogLevel::LOG_CRITICAL, L"[RENDERFRAME] Critical exception occurred: %hs", e.what());
       #endif
       
       // Ensure rendering flag is cleared on exception
       threadManager.threadVars.bIsRendering.store(false);
   }

   #ifdef RENDERER_IS_THREAD
      // Thread exit logging
      #if defined(_DEBUG_RENDERER_) && defined(_DEBUG)
          debug.logLevelMessage(LogLevel::LOG_INFO, L"[RENDERFRAME] Render thread exiting normally");
      #endif
   #endif

   // FINAL: Ensure rendering state is cleared
   threadManager.threadVars.bIsRendering.store(false);

   // Note: exclusiveRenderLock will be automatically released when it goes out of scope
}
#pragma warning(pop)

#endif