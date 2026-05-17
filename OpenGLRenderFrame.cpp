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

    // Build camera matrices using DirectXMath on Windows
#if defined(_WIN32) || defined(_WIN64)
    using namespace DirectX;
    XMFLOAT3 camPos = myCamera.GetPosition();
    XMMATRIX view   = myCamera.GetViewMatrix();
    float fovRad    = XMConvertToRadians(config.myConfig.fov > 0.0f ? config.myConfig.fov : 60.0f);
    float aspect    = (m_renderTargetHeight > 0)
                        ? static_cast<float>(m_renderTargetWidth) / static_cast<float>(m_renderTargetHeight)
                        : 16.0f / 9.0f;
    XMMATRIX proj   = XMMatrixPerspectiveFovLH(fovRad, aspect, 0.1f, 5000.0f);

    // Obtain uniform locations
    GLint locView = glGetUniformLocation(m_3dShaderProgram.programID, "uView");
    GLint locProj = glGetUniformLocation(m_3dShaderProgram.programID, "uProjection");
    GLint locCamPos = glGetUniformLocation(m_3dShaderProgram.programID, "uViewPos");

    // Upload matrices (column-major for OpenGL — transpose the row-major XMMATRIX)
    XMFLOAT4X4 mView, mProj;
    XMStoreFloat4x4(&mView, XMMatrixTranspose(view));
    XMStoreFloat4x4(&mProj, XMMatrixTranspose(proj));
    if (locView  >= 0) glUniformMatrix4fv(locView,  1, GL_FALSE, &mView.m[0][0]);
    if (locProj  >= 0) glUniformMatrix4fv(locProj,  1, GL_FALSE, &mProj.m[0][0]);
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

        XMMATRIX world = m.GetWorldMatrix();
        XMFLOAT4X4 mWorld;
        XMStoreFloat4x4(&mWorld, XMMatrixTranspose(world));

        GLint locModel = glGetUniformLocation(m_3dShaderProgram.programID, "uModel");
        if (locModel >= 0) glUniformMatrix4fv(locModel, 1, GL_FALSE, &mWorld.m[0][0]);

        // TODO: Call m.DrawOpenGL() once OpenGL vertex/index buffers are provisioned in ModelInfo.
        // GPU buffers for OpenGL will be added alongside the DX11/Vulkan buffer fields.
        (void)mWorld;
    }
#endif

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

        // Render full-screen using DrawTexture (index 0 reserved for movie)
        DrawTexture(0,
            Vector2(0.0f, 0.0f),
            Vector2(static_cast<float>(iOrigWidth), static_cast<float>(iOrigHeight)),
            MyColor(255, 255, 255, 255), true);
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
                    break;
                default: break;
            }

            // ---- FX: 2D overlay effects (scrollers, particles, text) ----
            fxManager.Render2D();

            // ---- FX: 3D effects (fades, starfield, tunnel) ----
            fxManager.Render();

            // ---- FPS display ----
            if (USE_FPS_DISPLAY && config.myConfig.showDebugInfo)
            {
                static auto lastFPSTime  = std::chrono::steady_clock::now();
                static int  frameCounter = 0;
                auto        curTime      = std::chrono::steady_clock::now();
                float       elapsed      = std::chrono::duration<float>(curTime - lastFPSTime).count();
                ++frameCounter;
                if (elapsed >= 1.0f) {
                    fps         = static_cast<float>(frameCounter) / elapsed;
                    frameCounter = 0;
                    lastFPSTime  = curTime;
                }
#if defined(_WIN32) || defined(_WIN64)
                XMFLOAT3 coords = myCamera.GetPosition();
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
                DrawMyText(dbgText, Vector2(0.0f, 0.0f), MyColor(255, 255, 255, 255), 10.0f);
#endif
            }

            // Loading indicator
            if (!threadManager.threadVars.bLoaderTaskFinished.load())
            {
                delay++;
                if (delay > 3) {
                    loadIndex = (loadIndex + 1) % 4;
                    delay = 0;
                }
                static const wchar_t* spinner[] = { L"|", L"/", L"-", L"\\" };
                DrawMyText(std::wstring(L"Loading... ") + spinner[loadIndex],
                    Vector2(static_cast<float>(iOrigWidth  / 2 - 40),
                            static_cast<float>(iOrigHeight / 2 - 10)),
                    MyColor(255, 255, 255, 255), 14.0f);
            }

            // ---- GUI rendering ----
            guiManager.Render();

            // ---- Present frame ----
#if defined(_WIN32) || defined(_WIN64)
            SwapBuffers(m_glContext.deviceContext);
#elif defined(__linux__)
            glXSwapBuffers(m_glContext.display, m_glContext.window);
#elif defined(__ANDROID__)
            eglSwapBuffers(m_glContext.eglDisplay, m_glContext.eglSurface);
#endif

            // ---- Frame counter / FPS tracking ----
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
