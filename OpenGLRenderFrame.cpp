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
#include "BuildInfo.h"
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
// Push uniforms using pre-cached locations (fast path, no glGetUniformLocation calls).
static void UploadModelUniformsCached(
    const OpenGLRenderer::CachedUniforms3D& u,
    const glm::mat4& view, const glm::mat4& proj,
    const glm::vec3& camPos, const Matrix4x4& worldMat4,
    const std::vector<LightStruct>& allLights, int lightCount,
    const ModelInfo& mi)
{
    // Matrices
    if (u.uModel      >= 0) glUniformMatrix4fv(u.uModel,      1, GL_FALSE, &worldMat4.m[0][0]);
    if (u.uView       >= 0) glUniformMatrix4fv(u.uView,       1, GL_FALSE, glm::value_ptr(view));
    if (u.uProjection >= 0) glUniformMatrix4fv(u.uProjection, 1, GL_FALSE, glm::value_ptr(proj));
    if (u.uViewPos    >= 0) glUniform3f(u.uViewPos, camPos.x, camPos.y, camPos.z);
    if (u.uAlpha      >= 0) glUniform1f(u.uAlpha, 1.0f);

    // Lights
    if (u.uLightCount >= 0) glUniform1i(u.uLightCount, lightCount);
    for (int li = 0; li < lightCount && li < 8; ++li) {
        const LightStruct& lt = allLights[li];
        const auto& ll = u.lights[li];
        if (ll.position  >= 0) glUniform3f(ll.position,  lt.position.x,  lt.position.y,  lt.position.z);
        if (ll.direction >= 0) glUniform3f(ll.direction, lt.direction.x, lt.direction.y, lt.direction.z);
        if (ll.color     >= 0) glUniform3f(ll.color,     lt.color.x,     lt.color.y,     lt.color.z);
        if (ll.ambient   >= 0) glUniform3f(ll.ambient,   lt.ambient.x,   lt.ambient.y,   lt.ambient.z);
        if (ll.intensity >= 0) glUniform1f(ll.intensity, lt.intensity);
        if (ll.range     >= 0) glUniform1f(ll.range,     lt.range);
        if (ll.innerCone >= 0) glUniform1f(ll.innerCone, lt.innerCone);
        if (ll.outerCone >= 0) glUniform1f(ll.outerCone, lt.outerCone);
        if (ll.type      >= 0) glUniform1i(ll.type,      lt.type);
        if (ll.active    >= 0) glUniform1i(ll.active,    lt.active);
    }
    if (!allLights.empty()) {
        const LightStruct& sun = allLights[0];
        if (u.uLightDir   >= 0) glUniform3f(u.uLightDir,   sun.direction.x, sun.direction.y, sun.direction.z);
        if (u.uLightColor >= 0) glUniform3f(u.uLightColor, sun.color.x,     sun.color.y,     sun.color.z);
        if (u.uAmbient    >= 0) glUniform3f(u.uAmbient,    sun.ambient.x,   sun.ambient.y,   sun.ambient.z);
    }

    // Material
    if (u.uKd  >= 0) glUniform3f(u.uKd,  1.0f, 1.0f, 1.0f);
    if (u.uKa  >= 0) glUniform3f(u.uKa,  0.35f, 0.35f, 0.35f);
    if (u.uKs  >= 0) glUniform3f(u.uKs,  0.5f, 0.5f, 0.5f);
    if (u.uNs  >= 0) glUniform1f(u.uNs,  32.0f);
    if (u.uMetallic    >= 0) glUniform1f(u.uMetallic,    mi.metallic);
    if (u.uRoughness   >= 0) glUniform1f(u.uRoughness,   mi.roughness);
    if (u.uNormalScale >= 0) glUniform1f(u.uNormalScale, 1.0f);
    if (u.uEmissiveFactor >= 0) glUniform3f(u.uEmissiveFactor, 0.0f, 0.0f, 0.0f);
    if (u.uEmissiveStr    >= 0) glUniform1f(u.uEmissiveStr,    1.0f);

    bool hasDiffuse = !mi.textureIDs.empty()   && mi.textureIDs[0]   != 0;
    bool hasNormal  = !mi.normalMapIDs.empty() && mi.normalMapIDs[0] != 0;
    if (u.uHasDiffuse   >= 0) glUniform1i(u.uHasDiffuse,   hasDiffuse ? 1 : 0);
    if (u.uHasNormalMap >= 0) glUniform1i(u.uHasNormalMap, hasNormal  ? 1 : 0);

    if (u.uDiffuse   >= 0) glUniform1i(u.uDiffuse,   0);
    if (u.uNormalMap >= 0) glUniform1i(u.uNormalMap, 1);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, hasDiffuse ? mi.textureIDs[0]   : 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, hasNormal  ? mi.normalMapIDs[0] : 0);
}

// Fallback path for custom per-model shaders: uses glGetUniformLocation (slower, but only
// called for models that have their own GLSL program distinct from the built-in 3D shader).
static void UploadModelUniformsDynamic(GLuint prog,
    const glm::mat4& view, const glm::mat4& proj,
    const glm::vec3& camPos, const Matrix4x4& worldMat4,
    const std::vector<LightStruct>& allLights, int lightCount,
    const ModelInfo& mi)
{
    auto loc = [prog](const char* n) { return glGetUniformLocation(prog, n); };

    GLint lM = loc("uModel"), lV = loc("uView"), lP = loc("uProjection"), lC = loc("uViewPos"), lA = loc("uAlpha");
    if (lM >= 0) glUniformMatrix4fv(lM, 1, GL_FALSE, &worldMat4.m[0][0]);
    if (lV >= 0) glUniformMatrix4fv(lV, 1, GL_FALSE, glm::value_ptr(view));
    if (lP >= 0) glUniformMatrix4fv(lP, 1, GL_FALSE, glm::value_ptr(proj));
    if (lC >= 0) glUniform3f(lC, camPos.x, camPos.y, camPos.z);
    if (lA >= 0) glUniform1f(lA, 1.0f);

    GLint lLC = loc("uLightCount");
    if (lLC >= 0) glUniform1i(lLC, lightCount);
    for (int li = 0; li < lightCount && li < 8; ++li) {
        const LightStruct& lt = allLights[li];
        std::string b = "uLights[" + std::to_string(li) + "].";
        auto l = [&](const char* n) { return glGetUniformLocation(prog, (b + n).c_str()); };
        GLint lPos = l("position"), lDir = l("direction"), lCol = l("color"), lAmb = l("ambient");
        GLint lInt = l("intensity"), lRng = l("range"), lIn = l("innerCone"), lOut = l("outerCone");
        GLint lTyp = l("type"), lAct = l("active");
        if (lPos >= 0) glUniform3f(lPos, lt.position.x,  lt.position.y,  lt.position.z);
        if (lDir >= 0) glUniform3f(lDir, lt.direction.x, lt.direction.y, lt.direction.z);
        if (lCol >= 0) glUniform3f(lCol, lt.color.x,     lt.color.y,     lt.color.z);
        if (lAmb >= 0) glUniform3f(lAmb, lt.ambient.x,   lt.ambient.y,   lt.ambient.z);
        if (lInt >= 0) glUniform1f(lInt, lt.intensity);
        if (lRng >= 0) glUniform1f(lRng, lt.range);
        if (lIn  >= 0) glUniform1f(lIn,  lt.innerCone);
        if (lOut >= 0) glUniform1f(lOut, lt.outerCone);
        if (lTyp >= 0) glUniform1i(lTyp, lt.type);
        if (lAct >= 0) glUniform1i(lAct, lt.active);
    }
    if (!allLights.empty()) {
        const LightStruct& sun = allLights[0];
        GLint lLD = loc("uLightDir"), lLCol = loc("uLightColor"), lLA = loc("uAmbient");
        if (lLD   >= 0) glUniform3f(lLD,   sun.direction.x, sun.direction.y, sun.direction.z);
        if (lLCol >= 0) glUniform3f(lLCol, sun.color.x,     sun.color.y,     sun.color.z);
        if (lLA   >= 0) glUniform3f(lLA,   sun.ambient.x,   sun.ambient.y,   sun.ambient.z);
    }
    bool hasDiffuse = !mi.textureIDs.empty()   && mi.textureIDs[0]   != 0;
    bool hasNormal  = !mi.normalMapIDs.empty() && mi.normalMapIDs[0] != 0;
    GLint lHD = loc("uHasDiffuse"), lHN = loc("uHasNormalMap"), lD = loc("uDiffuse"), lN = loc("uNormalMap");
    if (lHD >= 0) glUniform1i(lHD, hasDiffuse ? 1 : 0);
    if (lHN >= 0) glUniform1i(lHN, hasNormal  ? 1 : 0);
    if (lD  >= 0) glUniform1i(lD, 0);
    if (lN  >= 0) glUniform1i(lN, 1);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, hasDiffuse ? mi.textureIDs[0]   : 0);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, hasNormal  ? mi.normalMapIDs[0] : 0);
}

inline void OpenGLRenderer::RenderGamePlay(float deltaTime)
{
    if (!bIsInitialized.load()) return;
    if (!threadManager.threadVars.bLoaderTaskFinished.load()) return;

    // Restore 3D pipeline states
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    // LH system: use clockwise front faces to match DirectX convention
    if (config.myConfig.BackCulling) {
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glFrontFace(GL_CW);
    } else {
        glDisable(GL_CULL_FACE);
    }

    glm::vec3 camPos = myCamera.GetPosition();
    glm::mat4 view   = myCamera.GetViewMatrix();
    glm::mat4 proj   = myCamera.GetProjectionMatrix();

    auto allLights = lightsManager.GetAllLights();
    int  lightCount = static_cast<int>(allLights.size());
    if (lightCount > MAX_GLOBAL_LIGHTS) lightCount = MAX_GLOBAL_LIGHTS;

    // Build view/proj as Matrix4x4 for model info (GLM col-major → row-major)
    Matrix4x4 viewMat4, projMat4;
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) {
            viewMat4.m[r][c] = view[c][r];
            projMat4.m[r][c] = proj[c][r];
        }
    XMFLOAT3 camPosF3 = { camPos.x, camPos.y, camPos.z };

#if defined(_DEBUG_RENDER_WIREFRAME_)
    glPolygonMode(GL_FRONT_AND_BACK, bWireframeMode ? GL_LINE : GL_FILL);
#endif

    for (int i = 0; i < MAX_SCENE_MODELS; ++i)
    {
        if (!scene.scene_models[i].m_isLoaded) continue;
        if (scene.scene_models[i].m_modelInfo.bIsTransformProxy) continue;
        if (scene.scene_models[i].m_modelInfo.vertices.empty()) continue;

        ModelInfo& mi = scene.scene_models[i].m_modelInfo;
        mi.fxActive        = false;
        mi.viewMatrix      = viewMat4;
        mi.projectionMatrix = projMat4;
        mi.cameraPosition  = camPosF3;

        // Choose shader: prefer per-model, fall back to renderer's built-in 3D shader
        const bool useBuiltin = (mi.shaderProgram == 0 || mi.shaderProgram == m_3dShaderProgram.programID);
        GLuint prog = useBuiltin ? m_3dShaderProgram.programID : mi.shaderProgram;
        if (prog == 0) {
            scene.scene_models[i].Render(deltaTime);
            continue;
        }

        glUseProgram(prog);
        // Upload per-frame uniforms — use cached locations for built-in shader (fast path)
        if (useBuiltin && m_uniforms3D.populated)
            UploadModelUniformsCached(m_uniforms3D, view, proj, camPos, mi.worldMatrix, allLights, lightCount, mi);
        else
            UploadModelUniformsDynamic(prog, view, proj, camPos, mi.worldMatrix, allLights, lightCount, mi);

        // Lazy VAO creation: Upload() leaves VAO=0 so that the VAO is built here
        // on the render thread (render context). VAOs are NOT shared between GL
        // contexts; creating them on the loader context causes an access violation
        // in nvoglv64.dll when the render thread calls glBindVertexArray.
        // VBO/EBO are buffer objects and ARE shared — safe to use here directly.
        if (mi.VAO == 0 && mi.VBO != 0 && !mi.indices.empty()) {
            constexpr GLsizei kStride = (3 + 3 + 2 + 3) * sizeof(float); // 44 bytes
            glGenVertexArrays(1, &mi.VAO);
            glBindVertexArray(mi.VAO);
            glBindBuffer(GL_ARRAY_BUFFER,         mi.VBO);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mi.EBO);
            glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, kStride, (void*)0);
            glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, kStride, (void*)(3 * sizeof(float)));
            glEnableVertexAttribArray(2); glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, kStride, (void*)(6 * sizeof(float)));
            glEnableVertexAttribArray(3); glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, kStride, (void*)(8 * sizeof(float)));
            glBindVertexArray(0);
        }

        // Bind model's geometry buffers and issue draw call
        if (mi.VAO != 0 && !mi.indices.empty()) {
            glBindVertexArray(mi.VAO);
            glDrawElements(GL_TRIANGLES,
                           static_cast<GLsizei>(mi.indices.size()),
                           GL_UNSIGNED_INT, nullptr);
            glBindVertexArray(0);
        }

        glUseProgram(0);
        glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, 0);

        scene.scene_models[i].Render(deltaTime);
    }

#if defined(_DEBUG_RENDER_WIREFRAME_)
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
#endif
}

// ---------------------------------------------------------------------------------------------------------------
// Intro movie rendering helper  (mirrors DX11Renderer::RenderIntroMovie)
// ---------------------------------------------------------------------------------------------------------------
inline void OpenGLRenderer::RenderIntroMovie()
{
    if (!moviePlayer.IsPlaying()) return;

    // Advance the movie to the current frame (mirrors DX11 UpdateFrame call)
    moviePlayer.UpdateFrame();

#if defined(_WIN32) || defined(_WIN64)
    // Retrieve current RGBA frame from MoviePlayer and upload / update a GL texture
    uint32_t fw = 0, fh = 0;
    const uint8_t* frameData = moviePlayer.GetCurrentFrameRGBA(fw, fh);
    if (frameData && fw > 0 && fh > 0) {
        static GLuint movieTexID = 0;
        static uint32_t lastW = 0, lastH = 0;

        if (!movieTexID) glGenTextures(1, &movieTexID);
        glBindTexture(GL_TEXTURE_2D, movieTexID);
        // CPU buffer is RGBA (MoviePlayer::UpdateVideoTextureCPU swaps BGRA→RGBA for OpenGL)
        if (fw != lastW || fh != lastH) {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, fw, fh, 0, GL_RGBA, GL_UNSIGNED_BYTE, frameData);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            lastW = fw; lastH = fh;
        } else {
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, fw, fh, GL_RGBA, GL_UNSIGNED_BYTE, frameData);
        }
        glBindTexture(GL_TEXTURE_2D, 0);

        // Blit movie to fill entire screen
        DrawVideoFrame(
            Vector2(0.0f, 0.0f),
            Vector2(static_cast<float>(iOrigWidth), static_cast<float>(iOrigHeight)),
            MyColor(255, 255, 255, 255),
            movieTexID);
    }

    // Company logo overlay: half size, bottom-left corner (mirrors DX11 behaviour)
    {
        int logoIdx = int(BlitObj2DIndexType::IMG_COMPANYLOGO);
        if (logoIdx >= 0 && logoIdx < MAX_TEXTURE_BUFFERS && m_2dTextures[logoIdx].isLoaded) {
            int halfW = m_2dTextures[logoIdx].width  / 2;
            int halfH = m_2dTextures[logoIdx].height / 2;
            Blit2DObjectToSize(BlitObj2DIndexType::IMG_COMPANYLOGO,
                               0, iOrigHeight - halfH, halfW, halfH);
        }
    }

    // Spacebar to skip movie — mirrors DX11 skip check
    if (GetAsyncKeyState(' ') & 0x8000)
    {
        moviePlayer.Stop();
        scene.bSceneSwitching = true;
        fxManager.FadeToBlack(1.0f, 0.06f);
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

            // ---- Starfield background pass (before 3D models so geometry renders on top) ----
            fxManager.RenderBackground();

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

            // ---- 2D scene overlays (rendered after 3D so they appear in front) ----
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
                if (delay > 3) { loadIndex = (loadIndex + 1) % 9; delay = 0; }
                // Animated circle texture (sprite sheet: 9 valid frames of 32x32,
                // frame 9 skipped — one frame in the sheet is corrupt/invalid).
                if (m_2dTextures[int(BlitObj2DIndexType::BG_LOADER_CIRCLE)].isLoaded) {
                    iPosX = loadIndex << 5;
                    Blit2DObjectAtOffset(BlitObj2DIndexType::BG_LOADER_CIRCLE,
                        iOrigWidth - 34, iOrigHeight - 49, iPosX, 0, 32, 32);
                }
            }

            // ---- Renderer info overlay (bottom-right corner) ----
            // Format mirrors DX11: "CPGE Windows OpenGL v0.0.1370"
#if defined(_WIN32) || defined(_WIN64)
            if (USE_RENDERER_INFO && !scene.bSceneSwitching)
            {
                bool riShow = (scene.stSceneType == SceneType::SCENE_GAMETITLE  ||
                               scene.stSceneType == SceneType::SCENE_GAMEPLAY   ||
                               scene.stSceneType == SceneType::SCENE_INTRO      ||
                               scene.stSceneType == SceneType::SCENE_INTRO_MOVIE||
                               scene.stSceneType == SceneType::SCENE_GAMEOVER);
#if defined(_DEBUG)
                riShow = riShow || (scene.stSceneType == SceneType::SCENE_EXPERIMENT);
#endif
                if (riShow) {
                    // OpenGL version string is slightly smaller than DX11/Vulkan
                    const float riFontSize = std::clamp(
                        static_cast<float>(iOrigHeight) / 86.0f, 8.0f, 12.0f);

                    // Full version string — identical format to DXRenderFrame.cpp
                    const std::wstring riText =
                        std::wstring(GAME_NAME_W L" " PLATFORM_NAME_W L" " RENDERER_NAME_W L" v") +
                        std::to_wstring(CURRENT_BUILD_VERSION)    + L"." +
                        std::to_wstring(CURRENT_BUILD_SUBVERSION) + L"." +
                        std::to_wstring(CURRENT_BUILD);

                    int tw = 0, th = 0;
                    GLuint riTex = RenderTextToTexture(riText, L"Arial", riFontSize,
                        MyColor(220, 220, 220, 255), tw, th);
                    if (riTex) {
                        // Align to bottom-right corner, 4 px inset from each edge
                        Render2DQuad(riTex,
                            iOrigWidth  - tw - 4,
                            iOrigHeight - th - 2,
                            tw, th, 0, 0, tw, th, MyColor(255, 255, 255, 255), false);
                        glDeleteTextures(1, &riTex);
                    }
                }
            }
#endif

            // ---- GUI rendering ----
            // Rendered after 3D so windows always appear in front of scene geometry.
            guiManager.Render();

            // ---- Console window (F8 toggle; GAMETITLE / GAMEPLAY only) ----
            // Rendered after 3D and GUI — console must be topmost UI element (mirrors DX11/Vulkan).
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
                    DrawMyText(L"* REC",
                        Vector2(static_cast<float>(renderW) - 80.0f, 9.0f),
                        MyColor::Red(), 18.0f);
                }
            }

            // ---- FX: fullscreen effects (fades, starfield, tunnel) ----
            // Rendered LAST (after GUI and console) so fades overlay EVERYTHING including the
            // console window — mirrors DXRenderFrame.cpp where fxManager.Render() executes
            // after D2D EndDraw (which contains all GUI/console content).
            fxManager.Render();

            // ---- Screen recorder: capture back-buffer BEFORE Present (mirrors DX11 behaviour) ----
#if defined(_WIN32) || defined(_WIN64)
            if (screenRecorder.IsRecording())
                screenRecorder.CaptureFrame(static_cast<UINT>(renderW), static_cast<UINT>(renderH));
#endif

            // ---- Present frame ----
#if defined(_WIN32) || defined(_WIN64)
            {
                // Apply per-frame VSync setting — update swap interval if config changed
                static bool lastVSync = true;
                bool vsyncNow = config.myConfig.enableVSync;
                if (vsyncNow != lastVSync) {
                    if (WGLEW_EXT_swap_control)
                        wglSwapIntervalEXT(vsyncNow ? 1 : 0);
                    lastVSync = vsyncNow;
                }

                auto presentStart = std::chrono::steady_clock::now();
                SwapBuffers(m_glContext.deviceContext);

                // Software frame cap when VSync is disabled (~60 FPS target, mirrors DX11 behaviour)
                if (!vsyncNow) {
                    const auto targetFrame = std::chrono::milliseconds(16);
                    auto presentEnd  = std::chrono::steady_clock::now();
                    auto frameTime   = std::chrono::duration_cast<std::chrono::milliseconds>(presentEnd - presentStart);
                    if (frameTime < targetFrame)
                        std::this_thread::sleep_for(targetFrame - frameTime);
                }
            }
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
