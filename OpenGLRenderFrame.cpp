// ---------------------------------------------------------------------------------------------------------------
// OpenGLRenderFrame.cpp  —  OpenGL Render Loop & Scene Rendering
// ---------------------------------------------------------------------------------------------------------------
// Implements OpenGLRenderer::RenderFrame() and the scene-specific rendering helpers
// RenderGamePlay() and RenderIntroMovie().
//
// Pipeline order (mirrors DXRenderFrame.cpp / VULKAN_RenderFrame.cpp):
//   1) Safety guards, acquire exclusive lock
//   2) Clear colour + depth buffers, calculate delta time
//   3) Update camera animation
//   4) Scene-specific 3D rendering (RenderGamePlay / RenderIntroMovie)
//   5) 2D overlay composite (FX scrollers, particles, text scrollers)
//   6) FX rendering (fades, starfield, tunnel)
//   7) GUI rendering
//   8) SwapBuffers (present)
// ---------------------------------------------------------------------------------------------------------------
/* ----------------------------------------------------------------
   DO NOT INCLUDE THIS FILE!!! THE PROJECT ITSELF SCOPES THIS FILE!
/* ---------------------------------------------------------------- */
#include "Includes.h"

#if defined(__USE_OPENGL__)

#include "OpenGLRenderer.h"
#include "Debug.h"
#include "ExceptionHandler.h"
#include "WinSystem.h"
#include "Configuration.h"
#include "OpenGLFXManager.h"
#include "GUIManager.h"
#include "ConsoleWindow.h"
#include "Models.h"
#include "Lights.h"
#include "SceneManager.h"
#include "ShaderManager.h"
#include "MoviePlayer.h"
#include "ScreenRecorder.h"
#include "ThreadManager.h"

extern HWND                  hwnd;
extern HINSTANCE             hInst;
extern GUIManager            guiManager;
extern ConsoleWindow         consoleWindow;
extern Debug                 debug;
extern ExceptionHandler      exceptionHandler;
extern SystemUtils           sysUtils;
extern SceneManager          scene;
extern ThreadManager         threadManager;
extern GLFXManager           fxManager;
extern Vector2               myMouseCoords;
extern Model                 models[MAX_MODELS];
extern LightsManager         lightsManager;
extern MoviePlayer           moviePlayer;
extern WindowMetrics         winMetrics;
extern ScreenRecorder        screenRecorder;
extern Configuration         config;
extern std::atomic<bool>     bResizeInProgress;
extern std::atomic<bool>     bFullScreenTransition;
extern bool                  bResizing;

#ifdef __USE_SCRIPT_MANAGER__
#include "ScriptManager.h"
extern ScriptManager scriptManager;
#endif

#pragma warning(push)
#pragma warning(disable: 4101)

// ---------------------------------------------------------------------------------------------------------------
// 3D scene rendering helper
// ---------------------------------------------------------------------------------------------------------------
inline void OpenGLRenderer::RenderGamePlay(float deltaTime)
{
    if (!bIsInitialized.load()) return;
    if (m_3dShaderProgram.programID == 0) return;
    if (!threadManager.threadVars.bLoaderTaskFinished.load()) return;

    glUseProgram(m_3dShaderProgram.programID);

    // Build camera matrices using GLM (OpenGL native)
    glm::vec3 camPos = myCamera.GetPosition();
    glm::mat4 view   = myCamera.GetViewMatrix();
    float fovRad     = glm::radians(config.myConfig.fov > 0.0f ? config.myConfig.fov : 60.0f);
    float aspect     = (m_renderTargetHeight > 0)
                        ? static_cast<float>(m_renderTargetWidth) / static_cast<float>(m_renderTargetHeight)
                        : 16.0f / 9.0f;
    glm::mat4 proj   = glm::perspective(fovRad, aspect, 0.1f, 5000.0f);

    GLint locView   = glGetUniformLocation(m_3dShaderProgram.programID, "uView");
    GLint locProj   = glGetUniformLocation(m_3dShaderProgram.programID, "uProjection");
    GLint locCamPos = glGetUniformLocation(m_3dShaderProgram.programID, "uViewPos");

    if (locView  >= 0) glUniformMatrix4fv(locView,  1, GL_FALSE, glm::value_ptr(view));
    if (locProj  >= 0) glUniformMatrix4fv(locProj,  1, GL_FALSE, glm::value_ptr(proj));
    if (locCamPos >= 0) glUniform3f(locCamPos, camPos.x, camPos.y, camPos.z);

    // Upload first light
    auto lights = lightsManager.GetAllLights();
    if (!lights.empty()) {
        const LightStruct& sun = lights[0];
        GLint locLightDir   = glGetUniformLocation(m_3dShaderProgram.programID, "uLightDir");
        GLint locLightColor = glGetUniformLocation(m_3dShaderProgram.programID, "uLightColor");
        GLint locAmbient    = glGetUniformLocation(m_3dShaderProgram.programID, "uAmbient");
        if (locLightDir   >= 0) glUniform3f(locLightDir,   sun.direction.x, sun.direction.y, sun.direction.z);
        if (locLightColor >= 0) glUniform3f(locLightColor, sun.color.x,     sun.color.y,     sun.color.z);
        if (locAmbient    >= 0) glUniform3f(locAmbient,    sun.ambient.x,   sun.ambient.y,   sun.ambient.z);
    }

    // Render each active scene model
    for (int i = 0; i < MAX_MODELS; ++i)
    {
        Model& m = models[i];
        if (!m.IsActive() || !m.HasGeometry()) continue;

        // Matrix4x4 is row-major; build column-major glm::mat4 for OpenGL upload.
        Matrix4x4 world = m.GetWorldMatrix();
        glm::mat4 mWorld(
            world.m[0][0], world.m[1][0], world.m[2][0], world.m[3][0],
            world.m[0][1], world.m[1][1], world.m[2][1], world.m[3][1],
            world.m[0][2], world.m[1][2], world.m[2][2], world.m[3][2],
            world.m[0][3], world.m[1][3], world.m[2][3], world.m[3][3]);

        GLint locModel = glGetUniformLocation(m_3dShaderProgram.programID, "uModel");
        if (locModel >= 0) glUniformMatrix4fv(locModel, 1, GL_FALSE, glm::value_ptr(mWorld));

        // TODO: Call m.DrawOpenGL() once OpenGL vertex/index buffers are provisioned.
    }

    glUseProgram(0);
}

// ---------------------------------------------------------------------------------------------------------------
// Intro movie rendering helper
// ---------------------------------------------------------------------------------------------------------------
inline void OpenGLRenderer::RenderIntroMovie()
{
    if (!moviePlayer.IsPlaying()) return;
    // OpenGL path: request current RGBA frame from MoviePlayer and blit as texture
#if defined(_WIN32) || defined(_WIN64)
    uint32_t fw = 0, fh = 0;
    const uint8_t* frameData = moviePlayer.GetCurrentFrameRGBA(fw, fh);
    if (frameData && fw > 0 && fh > 0) {
        static GLuint movieTexID = 0;
        static uint32_t lastW = 0, lastH = 0;

        if (!movieTexID) glGenTextures(1, &movieTexID);
        glBindTexture(GL_TEXTURE_2D, movieTexID);
        if (fw != lastW || fh != lastH) {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, fw, fh, 0, GL_RGBA, GL_UNSIGNED_BYTE, frameData);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            lastW = fw; lastH = fh;
        } else {
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, fw, fh, GL_RGBA, GL_UNSIGNED_BYTE, frameData);
        }
        glBindTexture(GL_TEXTURE_2D, 0);

        DrawVideoFrame(
            Vector2(0.0f, 0.0f),
            Vector2(static_cast<float>(iOrigWidth), static_cast<float>(iOrigHeight)),
            MyColor(255, 255, 255, 255),
            movieTexID);
    }
#endif
}

// ---------------------------------------------------------------------------------------------------------------
// RenderFrame  —  main render loop (mirrors DXRenderFrame.cpp / VULKAN_RenderFrame.cpp)
// ---------------------------------------------------------------------------------------------------------------
void OpenGLRenderer::RenderFrame()
{
    // ---- Safety guards ----
    if (bHasCleanedUp || m_glContext.renderingContext == nullptr)
        return;

#if defined(RENDERER_IS_THREAD) && (defined(_WIN32) || defined(_WIN64))
    // Make the OpenGL context current in THIS render thread.
    // Initialize() released it from the main thread so we own it here.
    if (!wglMakeCurrent(m_glContext.deviceContext, m_glContext.renderingContext)) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[RENDERFRAME] wglMakeCurrent failed — render thread cannot acquire GL context");
        return;
    }
#endif

    if (threadManager.threadVars.bIsShuttingDown.load() ||
        bIsMinimized.load()                             ||
        threadManager.threadVars.bIsResizing.load()     ||
        !bIsInitialized.load())
        return;

    ThreadLockHelper exclusiveLock(threadManager, renderFrameLockName, 50);
    if (!exclusiveLock.IsLocked()) return;

    if (threadManager.threadVars.bIsRendering.load()) return;

    try
    {
        exceptionHandler.RecordFunctionCall("OpenGLRenderer::RenderFrame");
        threadManager.threadVars.bIsRendering.store(true);

#if defined(_DEBUG) && defined(_DEBUG_RENDERER_)
        FLOAT clearR = 0.01f, clearG = 0.01f, clearB = 0.01f;
#else
        FLOAT clearR = 0.0f, clearG = 0.0f, clearB = 0.0f;
#endif

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
            auto  now      = std::chrono::steady_clock::now();
            float rawDelta = std::chrono::duration<float>(now - lastFrameTime).count();

            // Simple 8-frame weighted smoothing
            static float history[8] = {};
            static int   histIdx    = 0;
            history[histIdx] = std::clamp(rawDelta, 1.0f / 120.0f, 1.0f / 10.0f);
            histIdx = (histIdx + 1) % 8;
            float weightedSum = 0.0f, totalWeight = 0.0f;
            for (int i = 0; i < 8; ++i) {
                float w = static_cast<float>(i + 1) / 8.0f;
                weightedSum += history[(histIdx - 1 - i + 8) % 8] * w;
                totalWeight += w;
            }
            float deltaTime = std::clamp(weightedSum / totalWeight, 0.001f, 0.1f);
            lastFrameTime   = now;

#ifdef __USE_SCRIPT_MANAGER__
            scriptManager.Update(deltaTime);
#endif

            myCamera.UpdateJumpAnimation();

            // ---- Determine render dimensions ----
            int renderW = iOrigWidth, renderH = iOrigHeight;
#if defined(_WIN32) || defined(_WIN64)
            if (winMetrics.isFullScreen) {
                renderW = winMetrics.monitorFullArea.right  - winMetrics.monitorFullArea.left;
                renderH = winMetrics.monitorFullArea.bottom - winMetrics.monitorFullArea.top;
            } else {
                renderW = winMetrics.clientWidth;
                renderH = winMetrics.clientHeight;
            }
#endif
            glViewport(0, 0, renderW, renderH);
            m_renderTargetWidth  = renderW;
            m_renderTargetHeight = renderH;

            // ---- Clear buffers ----
            glClearColor(clearR, clearG, clearB, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

            // ---- Background images (scene-aware, before 3D) ----
            RenderBackgroundImage();

            // ---- Scene-specific 3D rendering ----
            switch (scene.stSceneType)
            {
#if defined(_DEBUG)
                case SceneType::SCENE_EXPERIMENT:
                    break;
#endif
                case SceneType::SCENE_INTRO:
                    break;
                case SceneType::SCENE_GAMETITLE:
                    if (threadManager.threadVars.bLoaderTaskFinished.load()) {
                        int iModelID = scene.FindParentModelID(SplashShipName);
                        if (scene.gltfAnimator.IsAnimationPlaying(iModelID))
                            scene.gltfAnimator.UpdateAnimations(deltaTime, scene.scene_models, MAX_MODELS);
                        RenderGamePlay(deltaTime);
                    }
                    break;
                case SceneType::SCENE_GAMEPLAY:
                {
                    int iModelID = scene.FindParentModelID(ShipName1);
                    if (scene.gltfAnimator.IsAnimationPlaying(iModelID))
                        scene.gltfAnimator.UpdateAnimations(deltaTime, scene.scene_models, MAX_MODELS);
                    RenderGamePlay(deltaTime);
                    break;
                }
                default: break;
            }

            // ---- 2D scene overlays ----
            switch (scene.stSceneType)
            {
                case SceneType::SCENE_INTRO:
                    if (m_2dTextures[int(BlitObj2DIndexType::IMG_SPLASH1)].isLoaded)
                        Blit2DObjectToSize(BlitObj2DIndexType::IMG_SPLASH1, 0, 0, iOrigWidth, iOrigHeight);
                    break;
                case SceneType::SCENE_INTRO_MOVIE:
                    RenderIntroMovie();
                    break;
                case SceneType::SCENE_GAMETITLE:
                    if (threadManager.threadVars.bLoaderTaskFinished.load())
                        myCamera.SetYawPitch(0.240f, -0.28f);
                    fxManager.RenderLoadingText();
                    break;
                case SceneType::SCENE_GAMEPLAY:
                    if (!threadManager.threadVars.bLoaderTaskFinished.load() ||
                        fxManager.HasActiveLoadingTextEffects())
                        fxManager.RenderLoadingText();
                    break;
                default: break;
            }

            // ---- FX: 2D overlay effects (scrollers, particles, text) ----
            fxManager.Render2D();

            // ---- FX: 3D effects (fades, starfield, tunnel) ----
            fxManager.Render();

            // ---- FPS / debug info display ----
            if (USE_FPS_DISPLAY && config.myConfig.showDebugInfo)
            {
                static auto lastFPSTime  = std::chrono::steady_clock::now();
                static int  frameCounter = 0;
                auto        curTime      = std::chrono::steady_clock::now();
                float       elapsed      = std::chrono::duration<float>(curTime - lastFPSTime).count();
                ++frameCounter;
                if (elapsed >= 1.0f) {
                    fps          = static_cast<float>(frameCounter) / elapsed;
                    frameCounter = 0;
                    lastFPSTime  = curTime;
                }
#if defined(_WIN32) || defined(_WIN64)
                glm::vec3 coords = myCamera.GetPosition();
                const float dbgFontSize = std::clamp(static_cast<float>(renderH) / 108.0f, 8.0f, 12.0f);
                std::wstring dbgText =
                    L"FPS: " + std::to_wstring(fps) +
                    L"\nMOUSE: x" + std::to_wstring(myMouseCoords.x) +
                    L", y" + std::to_wstring(myMouseCoords.y) +
                    L"\nCamera X: " + std::to_wstring(coords.x) +
                    L", Y: "  + std::to_wstring(coords.y) +
                    L", Z: "  + std::to_wstring(coords.z) +
                    L", Yaw: " + std::to_wstring(myCamera.m_yaw) +
                    L", Pitch: " + std::to_wstring(myCamera.m_pitch) + L"\n" +
                    L"Global Light Count: " + std::to_wstring(lightsManager.GetLightCount()) + L"\n";
                DrawMyText(dbgText, Vector2(0.0f, 0.0f), MyColor(255, 255, 255, 255), dbgFontSize);
#endif
            }

            // ---- Debug OSD (F2 toggle notification) ----
            if (bDebugOSDActive)
            {
                float osdElapsed = std::chrono::duration<float>(
                    std::chrono::steady_clock::now() - debugOSDStartTime).count();
                if (osdElapsed < 5.0f) {
                    std::wstring osdMsg = config.myConfig.showDebugInfo
                        ? L"=> Debug Info: ENABLED"
                        : L"=> Debug Info: DISABLED";
                    DrawMyText(osdMsg, Vector2(10.0f, 80.0f), MyColor(255, 220, 0, 255), 14.0f);
                } else {
                    bDebugOSDActive = false;
                }
            }

            // ---- Loading spinner (fallback when loading text effects are inactive) ----
            if (!threadManager.threadVars.bLoaderTaskFinished.load())
            {
                delay++;
                if (delay > 3) {
                    loadIndex = (loadIndex + 1) % 4;
                    delay = 0;
                }
                // Animated circle texture (matches DX11 loader circle)
                if (m_2dTextures[int(BlitObj2DIndexType::BG_LOADER_CIRCLE)].isLoaded) {
                    iPosX = loadIndex << 5;
                    Blit2DObjectAtOffset(BlitObj2DIndexType::BG_LOADER_CIRCLE,
                        iOrigWidth - 34, iOrigHeight - 45, iPosX, 0, 32, 32);
                }
            }

            // ---- Renderer info overlay (bottom-right corner) ----
#if defined(_WIN32) || defined(_WIN64)
            if (USE_RENDERER_INFO && !scene.bSceneSwitching)
            {
                bool riShow = (scene.stSceneType == SceneType::SCENE_GAMETITLE ||
                               scene.stSceneType == SceneType::SCENE_GAMEPLAY  ||
                               scene.stSceneType == SceneType::SCENE_INTRO     ||
                               scene.stSceneType == SceneType::SCENE_GAMEOVER);
#if defined(_DEBUG)
                riShow = riShow || (scene.stSceneType == SceneType::SCENE_EXPERIMENT);
#endif
                if (riShow) {
                    const float riFontSize = std::clamp(
                        static_cast<float>(iOrigHeight) / 72.0f, 10.0f, 16.0f);
                    const std::wstring riText =
                        std::wstring(GAME_NAME_W L" " PLATFORM_NAME_W L" " RENDERER_NAME_W);
                    int tw = 0, th = 0;
                    GLuint riTex = RenderTextToTexture(riText, L"Arial", riFontSize,
                        MyColor(220, 220, 220, 255), tw, th);
                    if (riTex) {
                        Render2DQuad(riTex,
                            iOrigWidth - tw - 4, iOrigHeight - th - 4,
                            tw, th, 0, 0, tw, th, MyColor(255,255,255,255), false);
                        glDeleteTextures(1, &riTex);
                    }
                }
            }
#endif

            // ---- GUI rendering ----
            guiManager.Render();

            // ---- Console window (F8 toggle; GAMETITLE / GAMEPLAY only) ----
            if (!scene.bSceneSwitching &&
                (scene.stSceneType == SceneType::SCENE_GAMETITLE ||
                 scene.stSceneType == SceneType::SCENE_GAMEPLAY))
            {
                consoleWindow.Render(this, renderW, renderH);
            }

            // ---- Custom cursor ----
            if (m_2dTextures[int(BlitObj2DIndexType::BLIT_ALWAYS_CURSOR)].isLoaded)
                Blit2DObject(BlitObj2DIndexType::BLIT_ALWAYS_CURSOR,
                    static_cast<int>(myMouseCoords.x),
                    static_cast<int>(myMouseCoords.y));

            // ---- REC indicator (blinking) ----
            if (screenRecorder.IsRecording())
            {
                static int recBlinkCounter = 0;
                recBlinkCounter = (recBlinkCounter + 1) % 60;
                if (recBlinkCounter < 30) {
                    DrawMyText(L"● REC",
                        Vector2(static_cast<float>(renderW) - 75.0f, 12.0f),
                        MyColor::Red(), 18.0f);
                }
            }

            // ---- Present frame ----
#if defined(_WIN32) || defined(_WIN64)
            SwapBuffers(m_glContext.deviceContext);
#elif defined(__linux__)
            glXSwapBuffers(m_glContext.display, m_glContext.window);
#elif defined(__ANDROID__)
            eglSwapBuffers(m_glContext.eglDisplay, m_glContext.eglSurface);
#endif

            // ---- Frame counter ----
            ++frameCount;

#ifdef RENDERER_IS_THREAD
        } // end while
#endif

        threadManager.threadVars.bIsRendering.store(false);
    }
    catch (const std::exception& e)
    {
        threadManager.threadVars.bIsRendering.store(false);
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[RENDERFRAME] Exception: " +
            std::wstring(e.what(), e.what() + strlen(e.what())));
        exceptionHandler.LogException(e, "OpenGLRenderer::RenderFrame");
    }
}

#pragma warning(pop)

#endif // __USE_OPENGL__
