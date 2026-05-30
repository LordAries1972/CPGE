// ---------------------------------------------------------------------------------------------------------------
// VULKAN_RenderFrame.cpp  —  Vulkan Render Loop & Scene Rendering
// ---------------------------------------------------------------------------------------------------------------
// Implements VulkanRenderer::RenderFrame() and the scene-specific rendering functions
// RenderGamePlay() and RenderIntroMovie().
//
// Pipeline order (mirrors DXRenderFrame.cpp):
//   1) Safety guards, acquire exclusive lock
//   2) Acquire swap chain image, begin command buffer
//   3) Begin render pass, set viewport/scissor
//   4) Upload D2D overlay to Vulkan texture (Windows)
//   5) 3D scene rendering per scene type (RenderGamePlay)
//   6) 2D overlay composite (full-screen quad)
//   7) FX rendering
//   8) GUI rendering
//   9) End render pass, submit, present
//
// Platform guards:
//   #if defined(PLATFORM_WINDOWS)  /  #elif defined(PLATFORM_LINUX)  /  #elif defined(PLATFORM_ANDROID)
// ---------------------------------------------------------------------------------------------------------------
#include "Includes.h"

#if defined(__USE_VULKAN__)

#include "VULKAN_Renderer.h"
#include "Debug.h"
#include "ExceptionHandler.h"
#include "Configuration.h"
#include "VULKAN_FXManager.h"
#include "GUIManager.h"
#include "Models.h"
#include "Lights.h"
#include "SceneManager.h"
#include "ShaderManager.h"
#include "MoviePlayer.h"
#include "ThreadManager.h"

#if defined(PLATFORM_WINDOWS)
    #include "WinSystem.h"
    #include "ScreenRecorder.h"
    #include "ConsoleWindow.h"
#endif

// ---------------------------------------------------------------------------------------------------------------
// Externals (same external references as DXRenderFrame.cpp)
// ---------------------------------------------------------------------------------------------------------------
#if defined(PLATFORM_WINDOWS)
extern HWND                  hwnd;
extern HINSTANCE             hInst;
extern SystemUtils           sysUtils;
extern WindowMetrics         winMetrics;
extern ScreenRecorder        screenRecorder;
extern ConsoleWindow         consoleWindow;
#endif
extern GUIManager            guiManager;
extern Debug                 debug;
extern ExceptionHandler      exceptionHandler;
extern SceneManager          scene;
extern ThreadManager         threadManager;
extern VKFXManager           fxManager;
extern Vector2               myMouseCoords;
extern Model                 models[MAX_MODELS];
extern LightsManager         lightsManager;
extern MoviePlayer           moviePlayer;
extern Configuration         config;
extern std::atomic<bool>     bResizeInProgress;
extern std::atomic<bool>     bFullScreenTransition;
extern bool                  bResizing;

// ---------------------------------------------------------------------------------------------------------------
// UBO layout matching the 3D vertex shader
// ---------------------------------------------------------------------------------------------------------------
struct VKCameraUBO
{
    float model[16];
    float view[16];
    float proj[16];
};

// ---------------------------------------------------------------------------------------------------------------
// 3D scene rendering helper (inlined, called from RenderFrame)
// ---------------------------------------------------------------------------------------------------------------
inline void VulkanRenderer::RenderGamePlay(float deltaTime)
{
    if (!bIsInitialized.load()) return;
    if (m_3dPipeline == VK_NULL_HANDLE || m_3dPipelineLayout == VK_NULL_HANDLE) return;

    VkCommandBuffer cmd = m_frames[m_currentFrame].commandBuffer;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_3dPipeline);

    // Camera matrices — VulkanCamera returns GLM types on all platforms.
    glm::vec3 camPos = myCamera.GetPosition();
    glm::mat4 view   = myCamera.GetViewMatrix();
    glm::mat4 proj   = glm::perspective<float>(
        glm::radians<float>(config.myConfig.fov > 0.0f ? config.myConfig.fov : 60.0f),
        static_cast<float>(m_renderTargetWidth) / static_cast<float>(m_renderTargetHeight),
        0.1f, 5000.0f);
    proj[1][1] *= -1.0f; // Vulkan Y-flip (NDC Y-down)

    // Render each scene model
    if (!threadManager.threadVars.bLoaderTaskFinished.load()) return;

    for (int i = 0; i < MAX_MODELS; ++i)
    {
        Model& m = models[i];
        if (!m.IsActive() || !m.HasGeometry()) continue;

        // Build world matrix and fill UBO
        VKCameraUBO ubo{};
#if defined(PLATFORM_WINDOWS)
        // World: DirectXMath XMMATRIX (row-major) → store transposed into the float[16] UBO field
        XMMATRIX world = m.GetWorldMatrix();
        XMFLOAT4X4 worldF;
        XMStoreFloat4x4(&worldF, XMMatrixTranspose(world));
        std::memcpy(ubo.model, &worldF, sizeof(ubo.model));
#else
        std::memcpy(ubo.model, glm::value_ptr(glm::transpose(m.GetWorldMatrixGLM())), sizeof(ubo.model));
#endif
        // View and proj always come from VulkanCamera (GLM, column-major) → transpose for UBO
        std::memcpy(ubo.view, glm::value_ptr(glm::transpose(view)), sizeof(ubo.view));
        std::memcpy(ubo.proj, glm::value_ptr(glm::transpose(proj)), sizeof(ubo.proj));

        // Light push constant
        auto lights = lightsManager.GetAllLights();
        struct VKLightPC {
            float lightDir[3];
            float intensity;
            float lightColor[3];
            float ambientStrength;
            float viewPos[3];
            float _pad;
        } lpc{};

        if (!lights.empty()) {
            const LightStruct& sun = lights[0];
#if defined(PLATFORM_WINDOWS)
            lpc.lightDir[0]       = sun.direction.x;
            lpc.lightDir[1]       = sun.direction.y;
            lpc.lightDir[2]       = sun.direction.z;
            lpc.intensity         = sun.intensity;
            lpc.lightColor[0]     = sun.color.x;
            lpc.lightColor[1]     = sun.color.y;
            lpc.lightColor[2]     = sun.color.z;
            lpc.ambientStrength   = sun.ambient.x;
            lpc.viewPos[0]        = camPos.x;
            lpc.viewPos[1]        = camPos.y;
            lpc.viewPos[2]        = camPos.z;
#elif defined(PLATFORM_LINUX) || defined(PLATFORM_ANDROID)
            lpc.lightDir[0]       = sun.direction.x;
            lpc.lightDir[1]       = sun.direction.y;
            lpc.lightDir[2]       = sun.direction.z;
            lpc.intensity         = sun.intensity;
            lpc.lightColor[0]     = sun.color.x;
            lpc.lightColor[1]     = sun.color.y;
            lpc.lightColor[2]     = sun.color.z;
            lpc.ambientStrength   = sun.ambient.x;
            lpc.viewPos[0]        = camPos.x;
            lpc.viewPos[1]        = camPos.y;
            lpc.viewPos[2]        = camPos.z;
#endif
        }
        vkCmdPushConstants(cmd, m_3dPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(lpc), &lpc);

        // Write the updated UBO (model/view/proj) into the model's host-visible buffer.
        if (m.m_modelInfo.uniformBufferMapped) {
            std::memcpy(m.m_modelInfo.uniformBufferMapped, &ubo, sizeof(ubo));
        }

        // Bind texture (set=0): use the model's first texture if available, else the
        // 1×1 white fallback so the fragment shader always has a valid sampler.
        VkDescriptorSet texSet = m_defaultTextureDescSet;
        if (texSet != VK_NULL_HANDLE)
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    m_3dPipelineLayout, 0, 1, &texSet, 0, nullptr);

        // Bind UBO (set=1)
        if (m.m_modelInfo.descriptorSet != VK_NULL_HANDLE)
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    m_3dPipelineLayout, 1, 1,
                                    &m.m_modelInfo.descriptorSet, 0, nullptr);

        // Draw the model's indexed geometry
        if (m.m_modelInfo.vertexBuffer != VK_NULL_HANDLE &&
            m.m_modelInfo.indexBuffer  != VK_NULL_HANDLE &&
            !m.m_modelInfo.indices.empty())
        {
            VkBuffer     vbufs[]   = { m.m_modelInfo.vertexBuffer };
            VkDeviceSize offsets[] = { 0 };
            vkCmdBindVertexBuffers(cmd, 0, 1, vbufs, offsets);
            vkCmdBindIndexBuffer(cmd, m.m_modelInfo.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(cmd, static_cast<uint32_t>(m.m_modelInfo.indices.size()),
                             1, 0, 0, 0);
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Intro movie rendering helper
// ---------------------------------------------------------------------------------------------------------------
inline void VulkanRenderer::RenderIntroMovie()
{
#if defined(PLATFORM_WINDOWS)
    if (!moviePlayer.IsPlaying() || !m_d2dRenderTarget) return;

    // Advance the decoder and audio pipeline
    moviePlayer.UpdateFrame();

    // Retrieve the most recently decoded BGRA frame from the CPU buffer
    uint32_t fw = 0, fh = 0;
    const uint8_t* frameData = moviePlayer.GetCurrentFrameRGBA(fw, fh);
    if (!frameData || fw == 0 || fh == 0) return;

    // Create or reuse the D2D bitmap for the video frame.
    // Use BGRA + pre-multiplied alpha — matches the MF ARGB32/BGRA output format.
    const D2D1_PIXEL_FORMAT pixFmt =
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED);
    const UINT stride = fw * 4;

    if (!m_videoBitmap || m_videoBitmapWidth != fw || m_videoBitmapHeight != fh)
    {
        // Dimensions changed or first frame — (re)create the bitmap
        m_videoBitmap.Reset();
        D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(pixFmt);
        HRESULT hr = m_d2dRenderTarget->CreateBitmap(
            D2D1::SizeU(fw, fh), frameData, stride, props, &m_videoBitmap);
        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_WARNING,
                L"[VulkanRenderer] RenderIntroMovie: CreateBitmap failed");
            return;
        }
        m_videoBitmapWidth  = fw;
        m_videoBitmapHeight = fh;
    }
    else
    {
        // Update existing bitmap in-place (avoids re-allocation every frame)
        D2D1_RECT_U updateRect = D2D1::RectU(0, 0, fw, fh);
        if (FAILED(m_videoBitmap->CopyFromMemory(&updateRect, frameData, stride))) return;
    }

    // Draw the video frame fullscreen
    D2D1_RECT_F dest = D2D1::RectF(0.0f, 0.0f,
                                    static_cast<float>(iOrigWidth),
                                    static_cast<float>(iOrigHeight));
    m_d2dRenderTarget->DrawBitmap(m_videoBitmap.Get(), dest, 1.0f,
                                   D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);

#elif defined(PLATFORM_LINUX) || defined(PLATFORM_ANDROID)
    if (moviePlayer.IsPlaying()) {
        uint32_t fw = 0, fh = 0;
        const uint8_t* frameData = moviePlayer.GetCurrentFrameRGBA(fw, fh);
        if (frameData && fw == m_overlayWidth && fh == m_overlayHeight) {
            void* mapped = nullptr;
            VkDeviceSize sz = static_cast<VkDeviceSize>(fw) * fh * 4;
            vkMapMemory(m_device, m_overlayStagingMemory, 0, sz, 0, &mapped);
            std::memcpy(mapped, frameData, static_cast<size_t>(sz));
            vkUnmapMemory(m_device, m_overlayStagingMemory);
            m_overlayDirty = true;
        }
    }
#endif
}

// ---------------------------------------------------------------------------------------------------------------
// RenderFrame  —  main render loop (mirrors DXRenderFrame.cpp structure)
// ---------------------------------------------------------------------------------------------------------------
void VulkanRenderer::RenderFrame()
{
    // ---- Safety guards ----
    if (m_bHasCleanedUp || m_device == VK_NULL_HANDLE || m_swapchain == VK_NULL_HANDLE)
        return;

    if (threadManager.threadVars.bIsShuttingDown.load() ||
        bIsMinimized.load()                             ||
        threadManager.threadVars.bIsResizing.load()     ||
        !bIsInitialized.load())
        return;

    ThreadLockHelper exclusiveLock(threadManager, m_renderFrameLockName, 50);
    if (!exclusiveLock.IsLocked()) return;

    if (threadManager.threadVars.bIsRendering.load()) return;

    try
    {
        exceptionHandler.RecordFunctionCall("VulkanRenderer::RenderFrame");
        threadManager.threadVars.bIsRendering.store(true);

#ifdef RENDERER_IS_THREAD
        ThreadStatus status = threadManager.GetThreadStatus(THREAD_RENDERER);
        while (((status == ThreadStatus::Running) || (status == ThreadStatus::Paused)) &&
               (!threadManager.threadVars.bIsShuttingDown.load()))
        {
            status = threadManager.GetThreadStatus(THREAD_RENDERER);
            if (status == ThreadStatus::Paused) {
                threadManager.threadVars.bIsRendering.store(false);
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
            if (threadManager.threadVars.bIsResizing.load() || bIsMinimized.load()) {
                threadManager.threadVars.bIsRendering.store(false);
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            threadManager.threadVars.bIsRendering.store(true);
#endif

            // ---- Delta time ----
            auto   now            = std::chrono::steady_clock::now();
            float  rawDelta       = std::chrono::duration<float>(now - lastFrameTime).count();
            float  deltaTime      = m_deltaTimeSmoothing.ProcessDelta(rawDelta, 60.0f);
            deltaTime             = std::clamp(deltaTime, 0.001f, 0.1f);
            lastFrameTime         = now;

            myCamera.UpdateJumpAnimation();

            // ---- Acquire swap chain image ----
            auto& fd = m_frames[m_currentFrame];
            {
                VkResult waitResult = vkWaitForFences(m_device, 1, &fd.inFlightFence, VK_TRUE, UINT64_MAX);
                if (waitResult == VK_ERROR_DEVICE_LOST) {
                    debug.logLevelMessage(LogLevel::LOG_ERROR,
                        L"[VulkanRenderer] VK_ERROR_DEVICE_LOST in vkWaitForFences — stopping render loop");
                    threadManager.threadVars.bIsRendering.store(false);
#ifdef RENDERER_IS_THREAD
                    break;
#else
                    return;
#endif
                }
            }

            // Free descriptor sets deferred from the previous use of this frame slot
            if (!fd.pendingFreeSets.empty()) {
                vkFreeDescriptorSets(m_device, m_descriptorPool,
                    static_cast<uint32_t>(fd.pendingFreeSets.size()),
                    fd.pendingFreeSets.data());
                fd.pendingFreeSets.clear();
            }

            uint32_t imageIndex = 0;
            VkResult acquireResult = vkAcquireNextImageKHR(
                m_device, m_swapchain, UINT64_MAX,
                fd.imageAvailable, VK_NULL_HANDLE, &imageIndex);

            if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR ||
                acquireResult == VK_ERROR_SURFACE_LOST_KHR) {
                RecreateSwapChain(static_cast<uint32_t>(m_renderTargetWidth),
                                  static_cast<uint32_t>(m_renderTargetHeight));
                threadManager.threadVars.bIsRendering.store(false);
#ifdef RENDERER_IS_THREAD
                continue;
#else
                return;
#endif
            }
            if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR) {
                threadManager.threadVars.bIsRendering.store(false);
#ifdef RENDERER_IS_THREAD
                continue;
#else
                return;
#endif
            }
            m_currentImageIndex = imageIndex;
            vkResetFences(m_device, 1, &fd.inFlightFence);

            // ---- Begin command buffer ----
            VkCommandBuffer cmd = fd.commandBuffer;
            vkResetCommandBuffer(cmd, 0);

            VkCommandBufferBeginInfo bi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
            vkBeginCommandBuffer(cmd, &bi);

            // ---- Upload 2D overlay (Windows: D2D, Linux/Android: CPU buffer) ----
#if defined(PLATFORM_WINDOWS)
            // Begin D2D drawing for this frame
            if (m_d2dRenderTarget) {
                m_d2dRenderTarget->BeginDraw();
                m_d2dRenderTarget->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f)); // transparent clear
            }
#endif
            // Render background image to the D2D overlay (or CPU buffer on Linux/Android)
            RenderBackgroundImage();

            // Scene-specific D2D overlay content
#if defined(PLATFORM_WINDOWS)
            if (m_d2dRenderTarget) {
                switch (scene.stSceneType) {
                    case SceneType::SCENE_INTRO:
                        if (m_d2dTextures[int(BlitObj2DIndexType::IMG_SPLASH1)])
                            Blit2DObjectToSize(BlitObj2DIndexType::IMG_SPLASH1, 0, 0, iOrigWidth, iOrigHeight);
                        break;
                    case SceneType::SCENE_INTRO_MOVIE:
                        RenderIntroMovie();
                        break;
                    case SceneType::SCENE_GAMETITLE:
                        if (threadManager.threadVars.bLoaderTaskFinished.load())
                            myCamera.SetYawPitch(0.240f, -0.28f);
                        break;
                    case SceneType::SCENE_GAMEPLAY:
                        break;
                    default: break;
                }

                // FPS / debug overlay
                if (USE_FPS_DISPLAY && config.myConfig.showDebugInfo) {
                    static auto lastFPSTime   = std::chrono::steady_clock::now();
                    static int  fpsCounter    = 0;
                    auto        currentTime   = std::chrono::steady_clock::now();
                    float       elapsedForFPS = std::chrono::duration<float>(currentTime - lastFPSTime).count();
                    fpsCounter++;
                    if (elapsedForFPS >= 1.0f) {
                        m_fps        = static_cast<float>(fpsCounter) / elapsedForFPS;
                        fpsCounter   = 0;
                        lastFPSTime  = currentTime;
                    }
                    glm::vec3 coords = myCamera.GetPosition();
                    std::wstring fpsText =
                        L"FPS: " + std::to_wstring(m_fps) +
                        L"\nMOUSE: x" + std::to_wstring(myMouseCoords.x) + L", y" + std::to_wstring(myMouseCoords.y) +
                        L"\nCamera X: " + std::to_wstring(coords.x) + L", Y: " + std::to_wstring(coords.y) +
                        L", Z: " + std::to_wstring(coords.z) +
                        L"\nGlobal Lights: " + std::to_wstring(lightsManager.GetLightCount());
                    DrawMyText(fpsText, Vector2(0.0f, 0.0f), MyColor(255, 255, 255, 255), 10.0f);
                }

                // Debug OSD toggle notification
                if (bDebugOSDActive) {
                    float osdElapsed = std::chrono::duration<float>(
                        std::chrono::steady_clock::now() - debugOSDStartTime).count();
                    if (osdElapsed < 5.0f) {
                        std::wstring osdMsg = config.myConfig.showDebugInfo
                            ? L"► Debug Info: ENABLED" : L"► Debug Info: DISABLED";
                        DrawMyText(osdMsg, Vector2(10.0f, 80.0f), MyColor(255, 220, 0, 255), 14.0f);
                    } else { bDebugOSDActive = false; }
                }

                // ── Renderer info overlay (bottom-right) ─────────────────────────────
                // Mirrors DXRenderFrame.cpp: "CPGE Windows Vulkan v0.0.XXXX"
                if (USE_RENDERER_INFO && !scene.bSceneSwitching && m_d2dRenderTarget && m_dwriteFactory)
                {
                    bool riShow = (scene.stSceneType == SceneType::SCENE_GAMETITLE ||
                                   scene.stSceneType == SceneType::SCENE_GAMEPLAY  ||
                                   scene.stSceneType == SceneType::SCENE_INTRO     ||
                                   scene.stSceneType == SceneType::SCENE_GAMEOVER);
                    #if defined(_DEBUG)
                        riShow = riShow || (scene.stSceneType == SceneType::SCENE_EXPERIMENT);
                    #endif

                    if (riShow)
                    {
                        const float riFontSize = std::clamp(
                            static_cast<float>(iOrigHeight) / 72.0f, 10.0f, 16.0f);

                        const std::wstring riText =
                            std::wstring(GAME_NAME_W L" " PLATFORM_NAME_W L" " RENDERER_NAME_W L" v") +
                            std::to_wstring(CURRENT_BUILD_VERSION)    + L"." +
                            std::to_wstring(CURRENT_BUILD_SUBVERSION) + L"." +
                            std::to_wstring(CURRENT_BUILD);

                        IDWriteTextFormat* riFmt = GetOrCreateTextFormat(FontName, riFontSize);
                        if (riFmt)
                        {
                            ComPtr<IDWriteTextLayout> riLayout;
                            HRESULT riHr = m_dwriteFactory->CreateTextLayout(
                                riText.c_str(), static_cast<UINT32>(riText.size()),
                                riFmt,
                                static_cast<float>(iOrigWidth),
                                riFontSize * 2.0f,
                                &riLayout);

                            if (SUCCEEDED(riHr) && riLayout)
                            {
                                DWRITE_TEXT_METRICS riMetrics = {};
                                riLayout->GetMetrics(&riMetrics);

                                const float riX = static_cast<float>(iOrigWidth)  - riMetrics.width;
                                const float riY = static_cast<float>(iOrigHeight) - riMetrics.height;

                                ComPtr<ID2D1SolidColorBrush> ribrush;
                                m_d2dRenderTarget->CreateSolidColorBrush(
                                    D2D1::ColorF(220.0f/255.0f, 220.0f/255.0f, 220.0f/255.0f, 1.0f),
                                    &ribrush);
                                if (ribrush)
                                    m_d2dRenderTarget->DrawTextLayout(
                                        D2D1::Point2F(riX, riY),
                                        riLayout.Get(),
                                        ribrush.Get());
                            }
                        }
                    }
                }

                // Loading ring animation (only while loading)
                if (!threadManager.threadVars.bLoaderTaskFinished.load()) {
                    m_delay++;
                    if (m_delay > 3) { m_loadIndex = (m_loadIndex + 1) % 10; m_delay = 0; }
                    if (m_d2dTextures[int(BlitObj2DIndexType::BG_LOADER_CIRCLE)]) {
                        m_iPosX = m_loadIndex << 5;
                        Blit2DObjectAtOffset(BlitObj2DIndexType::BG_LOADER_CIRCLE,
                                             iOrigWidth - 34, iOrigHeight - 34,
                                             m_iPosX, 0, 32, 32);
                    }
                }
                // Loading text: continue rendering until graceful fade-outs complete
                if (!threadManager.threadVars.bLoaderTaskFinished.load() ||
                    fxManager.HasActiveLoadingTextEffects())
                {
                    fxManager.RenderLoadingText();
                }

                // FX 2D effects (scrollers, particles)
                try { fxManager.Render2D(); } catch (...) {}

                // 3D starfield (projects to 2D overlay via Blit2DColoredPixel — mirrors DXRenderFrame)
                if (fxManager.starfieldID > 0) {
                    try { fxManager.RenderFX(fxManager.starfieldID, cmd, myCamera.GetViewMatrix()); } catch (...) {}
                }

                // 3D warp dot tunnel (projects to 2D overlay via Blit2DColoredPixel)
                if (fxManager.tunnelID > 0) {
                    try { fxManager.RenderFX(fxManager.tunnelID, cmd, myCamera.GetViewMatrix()); } catch (...) {}
                }

                // GUI windows
                try { guiManager.Render(); } catch (...) {}

                // F8 debug console — only in GAMETITLE/GAMEPLAY and not during scene transitions
                // (mirrors DXRenderFrame.cpp behaviour exactly)
#if defined(PLATFORM_WINDOWS)
                if (!scene.bSceneSwitching &&
                    (scene.stSceneType == SceneType::SCENE_GAMETITLE ||
                     scene.stSceneType == SceneType::SCENE_GAMEPLAY))
                {
                    try { consoleWindow.Render(this, iOrigWidth, iOrigHeight); } catch (...) {}
                }
#endif

                // Cursor
                if (m_d2dTextures[int(BlitObj2DIndexType::BLIT_ALWAYS_CURSOR)])
                    Blit2DObject(BlitObj2DIndexType::BLIT_ALWAYS_CURSOR,
                                 static_cast<int>(myMouseCoords.x), static_cast<int>(myMouseCoords.y));

                // REC indicator
                if (screenRecorder.IsRecording()) {
                    static int recBlink = 0;
                    recBlink = (recBlink + 1) % 60;
                    if (recBlink < 30)
                        DrawMyText(L"● REC",
                                   Vector2(static_cast<float>(m_renderTargetWidth) - 75.0f, 12.0f),
                                   MyColor::Red(), 18.0f);
                }

                // End D2D drawing
                m_d2dRenderTarget->EndDraw();
                m_overlayDirty = true;
            } // m_d2dRenderTarget
#elif defined(PLATFORM_LINUX) || defined(PLATFORM_ANDROID)
            // Linux/Android: scene 2D and GUI rendered via CPU rasterizer (stub)
            try { fxManager.Render2D(); } catch (...) {}
            try { guiManager.Render();  } catch (...) {}
#endif

            // Upload overlay texture to GPU (if dirty)
#if defined(PLATFORM_WINDOWS)
            UploadOverlayToVulkan(cmd);
#endif

            // ---- Begin render pass ----
            VkClearValue clearValues[2];
#if defined(_DEBUG)
            clearValues[0].color = {{ 0.01f, 0.01f, 0.01f, 1.0f }};
#else
            clearValues[0].color = {{ 0.0f, 0.0f, 0.0f, 1.0f }};
#endif
            clearValues[1].depthStencil = { 1.0f, 0 };

            VkRenderPassBeginInfo rpBegin{};
            rpBegin.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            rpBegin.renderPass        = m_renderPass;
            rpBegin.framebuffer       = m_framebuffers[imageIndex];
            rpBegin.renderArea.offset = { 0, 0 };
            rpBegin.renderArea.extent = m_swapchainExtent;
            rpBegin.clearValueCount   = 2;
            rpBegin.pClearValues      = clearValues;

            vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);

            // Dynamic viewport and scissor
            VkViewport viewport{};
            viewport.x        = 0.0f;
            viewport.y        = 0.0f;
            viewport.width    = static_cast<float>(m_swapchainExtent.width);
            viewport.height   = static_cast<float>(m_swapchainExtent.height);
            viewport.minDepth = 0.0f;
            viewport.maxDepth = 1.0f;
            vkCmdSetViewport(cmd, 0, 1, &viewport);

            VkRect2D scissor{ { 0, 0 }, m_swapchainExtent };
            vkCmdSetScissor(cmd, 0, 1, &scissor);

            // ---- 3D scene rendering (RenderGamePlay) ----
            switch (scene.stSceneType) {
                case SceneType::SCENE_GAMETITLE:
                    if (threadManager.threadVars.bLoaderTaskFinished.load()) {
                        int modelID = scene.FindParentModelID(SplashShipName);
                        if (scene.gltfAnimator.IsAnimationPlaying(modelID))
                            scene.gltfAnimator.UpdateAnimations(deltaTime, scene.scene_models, MAX_MODELS);
                    }
                    RenderGamePlay(deltaTime);
                    break;
                case SceneType::SCENE_GAMEPLAY: {
                    int modelID = scene.FindParentModelID(ShipName1);
                    if (scene.gltfAnimator.IsAnimationPlaying(modelID))
                        scene.gltfAnimator.UpdateAnimations(deltaTime, scene.scene_models, MAX_MODELS);
                    RenderGamePlay(deltaTime);
                    break;
                }
                default: break;
            }

            // ---- FX 3D effects (starfield, fade quads) ----
            try { fxManager.Render(); } catch (...) {}

            // ---- 2D overlay composite (textured fullscreen quad alpha-blended on top) ----
            if (m_2dPipeline != VK_NULL_HANDLE && m_overlayTexture.isValid) {
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_2dPipeline);

                // Descriptor set for overlay texture
                VkDescriptorSetAllocateInfo dsai{};
                dsai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
                dsai.descriptorPool     = m_descriptorPool;
                dsai.descriptorSetCount = 1;
                dsai.pSetLayouts        = &m_textureDescSetLayout;
                VkDescriptorSet ds = VK_NULL_HANDLE;
                if (vkAllocateDescriptorSets(m_device, &dsai, &ds) == VK_SUCCESS) {
                    VkDescriptorImageInfo imgInfo{};
                    imgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    imgInfo.imageView   = m_overlayTexture.view;
                    imgInfo.sampler     = m_overlayTexture.sampler;
                    VkWriteDescriptorSet write{};
                    write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    write.dstSet          = ds;
                    write.dstBinding      = 0;
                    write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                    write.descriptorCount = 1;
                    write.pImageInfo      = &imgInfo;
                    vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
                    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                            m_2dPipelineLayout, 0, 1, &ds, 0, nullptr);

                    // Push: screenSize, position (0,0), size (full-screen), opacity 1.0
                    float pc[7] = {
                        static_cast<float>(m_renderTargetWidth),
                        static_cast<float>(m_renderTargetHeight),
                        0.0f, 0.0f,
                        static_cast<float>(m_renderTargetWidth),
                        static_cast<float>(m_renderTargetHeight),
                        1.0f
                    };
                    vkCmdPushConstants(cmd, m_2dPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT,
                                       0, sizeof(pc), pc);

                    VkBuffer vbufs[] = { m_quadVertexBuffer };
                    VkDeviceSize offsets[] = { 0 };
                    vkCmdBindVertexBuffers(cmd, 0, 1, vbufs, offsets);
                    vkCmdDraw(cmd, 4, 1, 0, 0);

                    // Defer free until the fence for this frame signals on the next iteration
                    fd.pendingFreeSets.push_back(ds);
                }
            }

            vkCmdEndRenderPass(cmd);
            vkEndCommandBuffer(cmd);

            // ---- Submit ----
            VkSemaphore          waitSems[]   = { fd.imageAvailable };
            VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
            // renderFinished is indexed by swapchain imageIndex (not by currentFrame) to avoid
            // VUID-vkQueueSubmit-pSignalSemaphores-00067 when the presentation engine is still
            // waiting on the semaphore from a previous frame that used the same image.
            VkSemaphore          signalSems[] = { m_renderFinishedSemaphores[imageIndex] };

            VkSubmitInfo submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
            submit.waitSemaphoreCount   = 1;
            submit.pWaitSemaphores      = waitSems;
            submit.pWaitDstStageMask    = waitStages;
            submit.commandBufferCount   = 1;
            submit.pCommandBuffers      = &cmd;
            submit.signalSemaphoreCount = 1;
            submit.pSignalSemaphores    = signalSems;
            // Submit and present must share the queue mutex: on most GPUs the present
            // queue is the same VkQueue handle as the graphics queue, so vkQueuePresentKHR
            // races with loader-thread vkQueueSubmit calls if not serialised together.
            VkResult presentResult = VK_SUCCESS;
            {
                std::lock_guard<std::mutex> qlock(m_queueMutex);
                vkQueueSubmit(m_graphicsQueue, 1, &submit, fd.inFlightFence);

                VkPresentInfoKHR present{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
                present.waitSemaphoreCount = 1;
                present.pWaitSemaphores    = signalSems;
                present.swapchainCount     = 1;
                present.pSwapchains        = &m_swapchain;
                present.pImageIndices      = &imageIndex;
                presentResult = vkQueuePresentKHR(m_presentQueue, &present);
            }

            if (presentResult == VK_ERROR_OUT_OF_DATE_KHR ||
                presentResult == VK_SUBOPTIMAL_KHR          ||
                presentResult == VK_ERROR_SURFACE_LOST_KHR) {
                RecreateSwapChain(static_cast<uint32_t>(m_renderTargetWidth),
                                  static_cast<uint32_t>(m_renderTargetHeight));
            }

            // Advance frame index
            m_currentFrame = (m_currentFrame + 1) % VK_MAX_FRAMES_IN_FLIGHT;
            threadManager.threadVars.bIsRendering.store(false);

#ifdef RENDERER_IS_THREAD
        } // while loop
#endif
    }
    catch (const std::exception& e)
    {
        debug.logLevelMessage(LogLevel::LOG_ERROR,
            L"[VulkanRenderer::RenderFrame] Exception: " +
            std::wstring(e.what(), e.what() + std::strlen(e.what())));
        threadManager.threadVars.bIsRendering.store(false);
    }

    threadManager.threadVars.bIsRendering.store(false);
}

#endif // __USE_VULKAN__
