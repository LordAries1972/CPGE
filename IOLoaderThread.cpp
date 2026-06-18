/* ----------------------------------------------------------------
   DO NOT INCLUDE THIS FILE!!! THE PROJECT ITSELF SCOPES THIS FILE!
/* ----------------------------------------------------------------
   IOLoaderThread.cpp -- Unified I/O Loader Thread implementation.

   Replaces the four per-pipeline source files:
       IOStreamDX11Thread.cpp  / IOStreamDX12Thread.cpp
       OpenGL_IOStreamThread.cpp / VULKAN_IOStreamThread.cpp

   All four pipelines share ONE LoaderTaskThread() body.
   Renderer-specific API calls are wrapped in inline
   #if defined(__USE_<RENDERER>__) / #if defined(PLATFORM_WINDOWS)
   guards so only the code for the active build is compiled.

   This is where you load & initialise all necessary resources
   for the given scene.
----------------------------------------------------------------- */
#include "Includes.h"

/* ----------------------------------------------------------------
   Per-renderer: renderer class and FX manager headers.
   Only the block matching the active renderer define is compiled.
---------------------------------------------------------------- */
#if defined(__USE_DIRECTX_11__)
    #include "DX11Renderer.h"
    #include "DX_FXManager.h"
#elif defined(__USE_DIRECTX_12__)
    #include "DX12Renderer.h"
    #include "DX12FXManager.h"
#elif defined(__USE_OPENGL__)
    #include "OpenGLRenderer.h"
    #include "OpenGLFXManager.h"
#elif defined(__USE_VULKAN__)
    #include "VULKAN_Renderer.h"
    #include "VULKAN_FXManager.h"
#endif

/* ----------------------------------------------------------------
   Platform-specific system headers.
   DX11/DX12 are always Windows so these are always included;
   OpenGL/Vulkan include them only when building on Windows.
---------------------------------------------------------------- */
#if defined(PLATFORM_WINDOWS)
    #include "WinSystem.h"
    #include "ShaderManager.h"
#endif

/* ----------------------------------------------------------------
   Common headers shared by all four pipelines.
---------------------------------------------------------------- */
#include "MathPrecalculation.h"
#include "ExceptionHandler.h"
#include "ThreadManager.h"
#include "Models.h"
#include "Lights.h"
#include "SoundManager.h"
#include "SceneManager.h"
#include "GUIManager.h"
#include "MoviePlayer.h"
#include "Configuration.h"
#include "GamePlayer.h"

using namespace SoundSystem;

/* ----------------------------------------------------------------
   External references -- common across all pipelines.
---------------------------------------------------------------- */
extern ExceptionHandler exceptionHandler;
extern ThreadManager    threadManager;
extern SceneManager     scene;
extern SoundManager     soundManager;
extern GUIManager       guiManager;
extern MoviePlayer moviePlayer;
extern Model            models[MAX_MODELS];
extern LightsManager    lightsManager;
extern GamePlayer       gamePlayer;
extern bool             bResizing;
extern int              textScrollerEffectID;
extern bool             Load_Music();                                               // Declared in main.cpp
extern std::wstring     baseDir;

/* fxManager type varies by renderer -- only one extern is compiled. */
#if defined(__USE_OPENGL__)
    extern GLFXManager  fxManager;
#elif defined(__USE_VULKAN__)
    extern VKFXManager  fxManager;
#else
    extern FXManager    fxManager;                                                  // DX11 and DX12
#endif

/* Windows-only system references (DX builds are inherently Windows). */
#if defined(PLATFORM_WINDOWS)
    extern SystemUtils   sysUtils;
    extern WindowMetrics winMetrics;
    extern ShaderManager shaderManager;
#endif

/* ----------------------------------------------------------------
   Static mutex definition.
     DX11   : defined here (matches original old IOStreamDX11Thread.cpp).
     DX12   : defined in DX12Renderer.cpp -- no redefinition needed.
     OpenGL : defined in OpenGLRenderer.cpp -- no redefinition needed.
     Vulkan : defined here (matches original old VULKAN_IOStreamThread.cpp).
---------------------------------------------------------------- */
#if defined(__USE_DIRECTX_11__)
    std::mutex DX11Renderer::s_loaderMutex;
#elif defined(__USE_VULKAN__)
    std::mutex VulkanRenderer::s_loaderMutex;
#endif

// Forward Declarations
PlayerInfo CreateShootEmUpPlayer(int playerID, const std::string& playerName, const Vector2& startPosition);

/* ================================================================
   UNIFIED LoaderTaskThread() -- one body covers all pipelines.

   The function signature selects the correct renderer class via
   a #if / #elif chain.  Inside the body the shared logic is
   written once; inline #if guards appear only where pipelines
   genuinely diverge (OpenGL context, glFlush, Vulkan camera path,
   platform-only system calls, etc.).
================================================================ */
#if defined(__USE_DIRECTX_11__)
    void DX11Renderer::LoaderTaskThread()
#elif defined(__USE_DIRECTX_12__)
    void DX12Renderer::LoaderTaskThread()
#elif defined(__USE_OPENGL__)
    void OpenGLRenderer::LoaderTaskThread()
#elif defined(__USE_VULKAN__)
    void VulkanRenderer::LoaderTaskThread()
#endif

{
    exceptionHandler.RecordFunctionCall("LoaderTaskThread");
    std::lock_guard<std::mutex> lock(s_loaderMutex);

    /* OpenGL only: acquire the shared loader GL context so that
       glGenTextures / glTexImage2D calls in LoadAllKnownTextures()
       have a valid context on this thread.  Both contexts share the
       same GL object namespace (textures, buffers, etc.). */
    #if defined(__USE_OPENGL__) && defined(PLATFORM_WINDOWS)
        if (m_loaderGLContext && m_glContext.deviceContext) {
            if (!wglMakeCurrent(m_glContext.deviceContext, m_loaderGLContext)) {
                debug.logLevelMessage(LogLevel::LOG_ERROR,
                    L"[LOADER] wglMakeCurrent failed -- texture uploads will silently fail and screen will be blank");
            }
        }
    #endif

    // Check the status of the Loader thread and flag that we have loading to do.
    ThreadStatus status = threadManager.GetThreadStatus(THREAD_LOADER);
    threadManager.threadVars.bLoaderTaskFinished.store(false);

    while (((status == ThreadStatus::Running) || (status == ThreadStatus::Paused)) &&
           (status != ThreadStatus::Terminated) &&
           (!threadManager.threadVars.bIsShuttingDown.load()) &&
           (status != ThreadStatus::Stopped))
    {
        /* Be careful here! ProcessMessages() will initiate your message handler queue,
           so make sure you don't do anything that could cause a crash and check all
           status flags carefully before handling your messages! */
        #if defined(PLATFORM_WINDOWS)
            sysUtils.ProcessMessages();
        #endif

        // Re-read the thread status and yield if we are paused.
        status = threadManager.GetThreadStatus(THREAD_LOADER);
        if (status == ThreadStatus::Paused)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        // --- Scene I/O dispatch ---
        switch (scene.stSceneType)
        {
            /* ------------------------------------------------------------
               DEBUG EXPERIMENT SCENE
               For rapid resource/FX iteration without loading the full
               intro sequence.  Enable with #define _DEBUG.
            -------------------------------------------------------------- */
            #if defined(_DEBUG)
                case SceneType::SCENE_EXPERIMENT:
                {
                    fxManager.StopZooming();                                        // In case we are returning from somewhere where a zooming effect maybe active.
                    fxManager.StopFireworks();                                      // In case we are returning from somewhere where fireworks maybe active.
                    threadManager.threadVars.bLoaderTaskFinished.store(false);
                    threadManager.threadVars.b2DTexturesLoaded.store(false);
                    if (LoadAllKnownTextures())
                        threadManager.threadVars.b2DTexturesLoaded.store(true);

                    // Screen is already black (button handler faded before ResumeLoader).
                    // Suspend any prior FX then start the WarpDotTunnel before fade-in.
                    fxManager.SaveAndSuspendFXForScene();
                    fxManager.Init3DWarpDOTTunnel(0.0f, 0.0f, 1000.0f, 10.0f, 200.0f,
                        TunnelSpinCycle::Clockwise, 100, false, 24, 100);

                    threadManager.PauseThread(THREAD_LOADER);
                    threadManager.threadVars.bLoaderTaskFinished.store(true);
                    fxManager.FadeToImage(1.0f, 0.08f);
                    break;
                }
            #endif

            /* ------------------------------------------------------------
               INTRO SCENE
               Load 2D textures, then pause and wait for the render thread.
            -------------------------------------------------------------- */
            case SceneType::SCENE_INTRO:
            {
                threadManager.threadVars.bLoaderTaskFinished.store(false);
                threadManager.threadVars.b2DTexturesLoaded.store(false);
                if (LoadAllKnownTextures())
                    threadManager.threadVars.b2DTexturesLoaded.store(true);

                threadManager.PauseThread(THREAD_LOADER);
                threadManager.threadVars.bLoaderTaskFinished.store(true);
                break;
            }

            /* ------------------------------------------------------------
               INTRO MOVIE SCENE -- OpenGL pipeline only.
               All 2D textures are already in VRAM from SCENE_INTRO.
               The movie was opened by the timeout in main.cpp
            -------------------------------------------------------------- */
                case SceneType::SCENE_INTRO_MOVIE:
                {
                    fxManager.StopZooming();                                        // In case we are returning from somewhere where a zooming effect maybe active.
                    fxManager.StopFireworks();                                      // In case we are returning from somewhere where fireworks maybe active.
                    threadManager.threadVars.bLoaderTaskFinished.store(false);
                    debug.logLevelMessage(LogLevel::LOG_INFO, L"[LOADER]: Scene Intro Movie.");

                    #if defined(__USE_OPENGL__)
                        // Flush any outstanding GL commands so the render thread sees a clean state.
                        glFlush();
                    #endif

                    threadManager.threadVars.bLoaderTaskFinished.store(true);
                    threadManager.PauseThread(THREAD_LOADER);
                    break;
                }

            /* ------------------------------------------------------------
               GAME TITLE SCENE
               Loads textures, title-screen lighting, scene geometry,
               audio, game menu and starfield.
            -------------------------------------------------------------- */
            case SceneType::SCENE_GAMETITLE:
            {
                threadManager.threadVars.bLoaderTaskFinished.store(false);
                fxManager.StopZooming();                                        // In case we are returning from somewhere where a zooming effect maybe active.
                fxManager.StopFireworks();                                      // In case we are returning from somewhere where fireworks maybe active.
                auto showStage = [](const wchar_t* msg) {
                    TextRenderStyle s;
                    s.fontName = LoadingTextFX::kFontName;
                    s.fontSize = 20.0f;
                    s.centered = true;
                    fxManager.ShowLoadingText(msg,
                        XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f),
                        0.2f, 0.05f,
                        XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f),
                        0.0f, -1.0f, &s);  // -1 = auto: renderer->iOrigHeight * LOADER_TEXT_Y_RATIO
                };

                threadManager.threadVars.b2DTexturesLoaded.store(false);
                debug.logLevelMessage(LogLevel::LOG_INFO, L"[LOADER]: Scene GameTitle.");
                showStage(L"Loading textures...");
                if (LoadAllKnownTextures())
                    threadManager.threadVars.b2DTexturesLoaded.store(true);

                if (!wasResizing.load())
                {
                    // ---- First load ---- //

                    // Parse the splash / title scene.
                    showStage(L"Parsing scene...");
                    {
                        ThreadLockHelper preAllocLock(threadManager, "SceneManager_PreAllocation", 2000);
                        if (preAllocLock.IsLocked())
                        {
                            scene.ParseSceneAutoDetect(AssetsDir / L"splash-hover1.gltf");
                            if (!scene.bGltfCameraParsed) scene.AutoFrameSceneToCamera();
                        }
                    }

                    showStage(L"Loading audio...");
                    Load_Music();

                    // Camera setup:
                    //   Vulkan uses iOrigWidth/iOrigHeight directly (original behaviour).
                    //   DX11, DX12, OpenGL read displayMode and honour bCameraJumped.
                    #if defined(__USE_VULKAN__)
                        myCamera.SetupDefaultCamera(static_cast<float>(iOrigWidth),
                                                    static_cast<float>(iOrigHeight));
                        myCamera.SetPosition(gtCameraStart.x, gtCameraStart.y, gtCameraStart.z);
                        myCamera.SetYawPitch(gtStartPY.x, gtStartPY.y);
                    #else
                        {
                            float camW, camH;
                            switch (config.myConfig.displayMode)
                            {
                                case 1: // Borderless -- use full monitor area
                                    #if defined(PLATFORM_WINDOWS)
                                        camW = static_cast<float>(winMetrics.monitorFullArea.right  - winMetrics.monitorFullArea.left);
                                        camH = static_cast<float>(winMetrics.monitorFullArea.bottom - winMetrics.monitorFullArea.top);
                                    #else
                                        camW = static_cast<float>(iOrigWidth);
                                        camH = static_cast<float>(iOrigHeight);
                                    #endif
                                    break;

                                case 2: // Fullscreen -- use configured target resolution
                                    camW = static_cast<float>(config.myConfig.resolutionWidth);
                                    camH = static_cast<float>(config.myConfig.resolutionHeight);
                                    break;

                                default: // Windowed -- client area only (excludes title bar / borders)
                                    #if defined(PLATFORM_WINDOWS)
                                        camW = static_cast<float>(winMetrics.clientWidth);
                                        camH = static_cast<float>(winMetrics.clientHeight);
                                    #else
                                        camW = static_cast<float>(iOrigWidth);
                                        camH = static_cast<float>(iOrigHeight);
                                    #endif
                                    break;
                            }
                            // Only apply default camera if no scene camera was parsed.
                            // If bCameraJumped is true a parsed camera already set position/projection.
                            if (!myCamera.bCameraJumped)
                                myCamera.SetupDefaultCamera(camW, camH);
                        }

                        if (!myCamera.bCameraJumped)
                        {
                            if (!scene.bGltfCameraParsed)
                            {
                                myCamera.SetPosition(gtCameraStart.x, gtCameraStart.y, gtCameraStart.z);
                                myCamera.SetYawPitch(gtStartPY.x, gtStartPY.y);
                            }
                        }
                    #endif  // !__USE_VULKAN__

                    showStage(L"Building interface...");
                    guiManager.CreateGameMenuWindow(L"winGameMenu");

                    showStage(L"Almost ready...");
                    // Reverse -- stars start spread near camera and converge toward the origin.
                    fxManager.CreateStarfield(80, 800.0f, 1000.0f, gtStarOrigin, true);

                    fxManager.StopLoadingText();
                    fxManager.FadeToImage(1.0f, 0.08f);
                    // Pulse the 2D Image Background with 20% depth
                    fxManager.ZoomInitialise(ZoomFXFunction::Zoom2D, 0.20f, 0.15f, int(BlitObj2DIndexType::IMG_GAMEINTRO1), 0, 0, iOrigWidth, iOrigHeight);
                    fxManager.StartZoom(0.015f);
                    fxManager.StartFireworks(0.5f);
                }
                else
                {
                    // ---- Resize path ---- //
                    // winMetrics is refreshed by GetWindowMetrics() in main.cpp before the loader
                    // runs, so it reflects the new window state for all display modes.
                    showStage(L"Updating display...");

                    // Camera on resize:
                    //   Vulkan uses iOrigWidth/iOrigHeight directly (original behaviour).
                    //   DX11, DX12, OpenGL read displayMode and honour bCameraJumped.
                    #if defined(__USE_VULKAN__)
                        myCamera.SetupDefaultCamera(static_cast<float>(iOrigWidth),
                                                    static_cast<float>(iOrigHeight));
                        myCamera.SetPosition(gtCameraStart.x, gtCameraStart.y, gtCameraStart.z);
                        myCamera.SetYawPitch(gtStartPY.x, gtStartPY.y);
                    #else
                        {
                            float camW, camH;
                            switch (config.myConfig.displayMode)
                            {
                                case 1:
                                    #if defined(PLATFORM_WINDOWS)
                                        camW = static_cast<float>(winMetrics.monitorFullArea.right  - winMetrics.monitorFullArea.left);
                                        camH = static_cast<float>(winMetrics.monitorFullArea.bottom - winMetrics.monitorFullArea.top);
                                    #else
                                        camW = static_cast<float>(iOrigWidth);
                                        camH = static_cast<float>(iOrigHeight);
                                    #endif
                                    break;

                                case 2:
                                    camW = static_cast<float>(config.myConfig.resolutionWidth);
                                    camH = static_cast<float>(config.myConfig.resolutionHeight);
                                    break;

                                default:
                                    #if defined(PLATFORM_WINDOWS)
                                        camW = static_cast<float>(winMetrics.clientWidth);
                                        camH = static_cast<float>(winMetrics.clientHeight);
                                    #else
                                        camW = static_cast<float>(iOrigWidth);
                                        camH = static_cast<float>(iOrigHeight);
                                    #endif
                                    break;
                            }

                            if (!myCamera.bCameraJumped)
                                myCamera.SetupDefaultCamera(camW, camH);


                            // Sync internal resolution/AR tracking regardless of camera mode
                            myCamera.UpdateResolution(static_cast<uint32_t>(camW), static_cast<uint32_t>(camH), camW / std::max(camH, 1.0f));
                        }

                        if (!myCamera.bCameraJumped)
                        {
                            myCamera.SetPosition(gtCameraStart.x, gtCameraStart.y, gtCameraStart.z);
                            myCamera.SetYawPitch(gtStartPY.x, gtStartPY.y);        // Restore title-screen orientation
                        }
                    #endif  // !__USE_VULKAN__

                    // OpenGL: RenderFrame skips the iOrigWidth = winMetrics.clientWidth update
                    // while bIsResizing=true (it early-continues the loop), so iOrigWidth still
                    // holds the Resize() value (LOWORD of lParam) which can differ from the
                    // clientWidth that GetClientRect() returns and the renderer uses for its
                    // ortho matrix once bIsResizing clears.  winMetrics is refreshed in main.cpp
                    // before this thread runs, so it always reflects the correct new client size.
                    #if defined(__USE_OPENGL__) && defined(PLATFORM_WINDOWS)
                        guiManager.OnWindowResize(winMetrics.clientWidth, winMetrics.clientHeight);
                    #else
                        guiManager.OnWindowResize(iOrigWidth, iOrigHeight);
                    #endif

                    // Starfield on resize
                    fxManager.CreateStarfield(80, 800.0f, 1000.0f, gtStarOrigin, true);
                    fxManager.StopLoadingText();
                    // Pulse the 2D Image Background with 20% depth
                    fxManager.ZoomInitialise(ZoomFXFunction::Zoom2D, 0.20f, 0.15f, int(BlitObj2DIndexType::IMG_GAMEINTRO1), 0, 0, iOrigWidth, iOrigHeight);
                    fxManager.StartZoom(0.015f);
                    fxManager.StartFireworks(0.5f);
                }

                /* OpenGL: flush pending GL commands before signalling the render thread. */
                #if defined(__USE_OPENGL__)
                    glFlush();
                #endif

                // This must go at the end so critical rendering can start.
                threadManager.PauseThread(THREAD_LOADER);
                threadManager.threadVars.bLoaderTaskFinished.store(true);
                break;
            }

            /* ------------------------------------------------------------
               MP3 PLAYER SCENE
               Simple MP3 playback scene.  XM tracker modules are the
               preferred format; MP3 is provided for those who need it.
               NOTE: COM initialisation is Windows-only; guard accordingly.
            -------------------------------------------------------------- */
            case SceneType::SCENE_LOAD_MP3:
            {
                try {
                    #if defined(__USE_MP3PLAYER__) && defined(PLATFORM_WINDOWS)
                        CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);             // Must initialise as STA (Single Threaded)

                        /* DX11 and DX12 expose player.Initialize(hwnd) via the renderer class.
                           OpenGL and Vulkan renderers do not carry this member. */
                        #if defined(__USE_DIRECTX_11__) || defined(__USE_DIRECTX_12__)
                            player.Initialize(hwnd);
                        #endif

                        auto fileName = AssetsDir / SingleMP3Filename;
                        if (player.loadFile(fileName)) {
                            debug.logLevelMessage(LogLevel::LOG_INFO, L"[LOADER]: MP3 File Re-loaded successfully.");
                            player.play();
                            player.fadeIn(5000);
                        }
                        else {
                            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[LOADER]: Failed to load MP3 file.");
                        }
                    #endif

                    // IMPORTANT: The MediaPlayer needs to process messages before playing.
                    #if defined(PLATFORM_WINDOWS)
                        sysUtils.GetMessageAndProcess();
                    #endif
                }
                catch (const std::exception& e) {
                    exceptionHandler.LogException(e, "[LOADER THREAD] SceneType::SCENE_LOAD_MP3");
                    debug.logLevelMessage(LogLevel::LOG_ERROR, L"[LOADER]: Exception: " +
                        std::wstring(e.what(), e.what() + strlen(e.what())));
                }

                threadManager.PauseThread(THREAD_LOADER);
                threadManager.threadVars.bLoaderTaskFinished.store(true);

                #if defined(__USE_MP3PLAYER__) && defined(PLATFORM_WINDOWS)
                    CoUninitialize();                                                // Uninitialise COM -- must release regardless
                #endif
                break;
            }

            /* ------------------------------------------------------------
               GAMEPLAY SCENE
               Loads all assets for the current game level.
                 First load : create lights, parse scene, load music.
                 Resize     : reload 2D textures only (models/lights survive).
            -------------------------------------------------------------- */
            case SceneType::SCENE_GAMEPLAY:
            {
                threadManager.threadVars.bLoaderTaskFinished.store(false);
                fxManager.StopZooming();                                        // In case we are returning from somewhere where a zooming effect maybe active.
                fxManager.StopFireworks();                                      // In case we are returning from somewhere where fireworks maybe active.
                auto showStage = [](const wchar_t* msg) {
                    TextRenderStyle s;
                    s.fontName = LoadingTextFX::kFontName;
                    s.fontSize = 20.0f;
                    s.centered = true;
                    fxManager.ShowLoadingText(msg,
                        XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f),
                        0.2f, 0.05f,
                        XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f),
                        0.0f, -1.0f, &s);  // -1 = auto: renderer->iOrigHeight * LOADER_TEXT_Y_RATIO
                };

                debug.logLevelMessage(LogLevel::LOG_INFO, L"[LOADER]: Scene GAMEPLAY Initialising.");
                threadManager.threadVars.b2DTexturesLoaded.store(false);
                showStage(L"Loading textures...");
                if (LoadAllKnownTextures())
                    threadManager.threadVars.b2DTexturesLoaded.store(true);

                if (!wasResizing.load())
                {
                    // ---- First load only ---- //
                    showStage(L"Initialising lighting...");
                    LightStruct sunLight{};
                    #if defined(PLATFORM_WINDOWS)
                        SecureZeroMemory(&sunLight, sizeof(LightStruct));
                    #endif
                    sunLight.active        = true;
                    sunLight.position      = XMFLOAT3(10.0f, -3.0f, -100.0f);
                    sunLight.direction     = XMFLOAT3(0.0f, 1.0f, 0.0f);
                    sunLight.color         = XMFLOAT3(1.0f, 1.0f, 1.0f);
                    sunLight.ambient       = XMFLOAT3(0.2f, 0.2f, 0.2f);
                    sunLight.intensity     = 0.5f;
                    sunLight.baseIntensity = 0.1f;
                    sunLight.Shiningness   = 0.0f;
                    sunLight.Reflection    = 0.0f;
                    sunLight.lightFalloff  = 0.1f;
                    sunLight.innerCone     = 30.0f;
                    sunLight.outerCone     = 60.0f;
                    sunLight.range         = 1000.0f;
                    sunLight.type          = int(LightType::DIRECTIONAL);
                    lightsManager.CreateLight(L"Sun", sunLight);

                    showStage(L"Parsing scene...");
                    {
                        ThreadLockHelper preAllocLock(threadManager, "SceneManager_PreAllocation", 2000);
                        if (preAllocLock.IsLocked())
                        {
                            scene.ParseGLTFScene(AssetsDir / L"test2.gltf");
                            if (!scene.bGltfCameraParsed) scene.AutoFrameSceneToCamera();
                        }
                    }

                    showStage(L"Loading audio...");
                    try
                    {
                        Load_Music();
                        // IMPORTANT: MediaPlayer needs to process messages before starting playback.
                        #if defined(PLATFORM_WINDOWS)
                            sysUtils.ProcessMessages();
                        #endif
                    }
                    catch (const std::exception& e) {
                        exceptionHandler.LogException(e, "[LOADER THREAD] SceneType::GAMEPLAY");
                        threadManager.threadVars.bLoaderTaskFinished.store(true);
                        threadManager.PauseThread(THREAD_LOADER);
                        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[LOADER]: Exception: " +
                            std::wstring(e.what(), e.what() + strlen(e.what())));
                    }
                }
                else
                {
                    // ---- Resize path ---- //
                    showStage(L"Updating display...");

                    /* OpenGL rebuilds the camera to the new viewport on resize.
                       DX11/DX12 camera is updated by DX##Renderer::Resize().
                       Vulkan camera is restored by RestoreCameraStateAfterResize() before the loader
                       runs -- do NOT call SetupDefaultCamera here as it resets the in-game position. */
                    #if defined(__USE_OPENGL__)
                        {
                            float camW, camH;
                            switch (config.myConfig.displayMode)
                            {
                                case 1:
                                    #if defined(PLATFORM_WINDOWS)
                                        camW = static_cast<float>(winMetrics.monitorFullArea.right  - winMetrics.monitorFullArea.left);
                                        camH = static_cast<float>(winMetrics.monitorFullArea.bottom - winMetrics.monitorFullArea.top);
                                    #else
                                        camW = static_cast<float>(iOrigWidth);
                                        camH = static_cast<float>(iOrigHeight);
                                    #endif
                                    break;

                                case 2:
                                    camW = static_cast<float>(config.myConfig.resolutionWidth);
                                    camH = static_cast<float>(config.myConfig.resolutionHeight);
                                    break;

                                default:
                                    #if defined(PLATFORM_WINDOWS)
                                        camW = static_cast<float>(winMetrics.clientWidth);
                                        camH = static_cast<float>(winMetrics.clientHeight);
                                    #else
                                        camW = static_cast<float>(iOrigWidth);
                                        camH = static_cast<float>(iOrigHeight);
                                    #endif
                                    break;
                            }
                            myCamera.SetupDefaultCamera(camW, camH);
                            myCamera.UpdateResolution(static_cast<uint32_t>(camW), static_cast<uint32_t>(camH), camW / std::max(camH, 1.0f));
                        }
                    #endif  // __USE_OPENGL__

                    // Same OpenGL coordinate-space fix as SCENE_GAMETITLE: use winMetrics
                    // so the GameMenu is positioned against the same client width the renderer
                    // will use for its ortho matrix (iOrigWidth is stale during bIsResizing).
                    #if defined(__USE_OPENGL__) && defined(PLATFORM_WINDOWS)
                        guiManager.OnWindowResize(winMetrics.clientWidth, winMetrics.clientHeight);
                    #else
                        guiManager.OnWindowResize(iOrigWidth, iOrigHeight);
                    #endif

                    // DX11/DX12/Vulkan trigger their fade-in here in the resize path.
                    // OpenGL always triggers its fade-in after the if/else block below.
                    #if !defined(__USE_OPENGL__)
                        fxManager.FadeToImage(1.0f, 0.08f);
                    #endif
                }

                /* OpenGL always triggers a fade-in after loading (first load or resize),
                   then flushes pending GL commands before signalling the render thread. */
                #if defined(__USE_OPENGL__)
                    fxManager.FadeToImage(1.0f, 0.08f);
                    glFlush();
                #endif

                fxManager.StopLoadingText();
                fxManager.ZoomInitialise(ZoomFXFunction::Zoom2D, 0.20f, 0.15f, int(BlitObj2DIndexType::IMG_GAMEINTRO1), 0, 0, iOrigWidth, iOrigHeight);
                fxManager.StartZoom(0.015f);
                fxManager.StartFireworks(2.0f);

                threadManager.threadVars.bLoaderTaskFinished.store(true);
                threadManager.PauseThread(THREAD_LOADER);
                break;
            }

            /* ------------------------------------------------------------
               GAME OVER SCENE
            -------------------------------------------------------------- */
            case SceneType::SCENE_GAMEOVER:
            {
                fxManager.StopFireworks();                                      // In case we are returning from somewhere where fireworks maybe active.
                threadManager.threadVars.b2DTexturesLoaded.store(false);
                debug.logLevelMessage(LogLevel::LOG_INFO, L"[LOADER]: Scene Game Over.");
                threadManager.threadVars.bLoaderTaskFinished.store(true);
                break;
            }

            // Unhandled scene type -- mark loading complete so the engine does not stall.
            default:
            {
                fxManager.StopFireworks();                                      // In case we are returning from somewhere where fireworks maybe active.
                threadManager.threadVars.bLoaderTaskFinished.store(true);
                break;
            }

        } // End switch (scene.stSceneType)
    }

    // Reset resize flag now that this loading pass is complete.
    if (!threadManager.threadVars.bIsShuttingDown.load())
        wasResizing.store(false);

    /* OpenGL: flush pending upload commands and release the loader context so the
       render thread can reclaim the GL context for its own use. */
    #if defined(__USE_OPENGL__) && defined(PLATFORM_WINDOWS)
        if (m_loaderGLContext) {
            glFlush();
            wglMakeCurrent(nullptr, nullptr);
        }
    #endif

    debug.logLevelMessage(LogLevel::LOG_INFO, L"[LOADER]: Scene Loading Complete - Pausing Thread");
}

//==============================================================================
// Example Helper Functions
//==============================================================================

// Example function to create a basic shoot-em-up player configuration
PlayerInfo CreateShootEmUpPlayer(int playerID, const std::string& playerName, const Vector2& startPosition) {
    PlayerInfo player;                                                  // Create new player information structure
    
    // Basic player identification
    player.playerID = playerID;                                         // Set unique player identifier
    player.playerName = playerName;                                     // Set player display name
    player.playerTag = "PILOT";                                         // Set player tag for shoot-em-up theme
    player.playerColor = MyColor(0, 255, 0, 255);                      // Green color for player ship
    
    // Visual representation for 2D shoot-em-up
    player.portraitImageIndex = BlitObj2DIndexType::IMG_COMPANYLOGO;    // Use logo as portrait placeholder
    player.frameImageIndex = BlitObj2DIndexType::IMG_WINFRAME1;         // Use window frame for UI
    
    // Position and movement setup
    player.position2D = startPosition;                                  // Set starting 2D position
    player.position3D = Vector3(startPosition.x, startPosition.y, 0.0f); // Convert to 3D position
    player.velocity2D = Vector2(0.0f, 0.0f);                           // No initial velocity
    player.velocity3D = Vector3(0.0f, 0.0f, 0.0f);                     // No initial 3D velocity
    player.mapPosition = startPosition;                                 // Set map position same as world position
    player.rotation = 0.0f;                                             // No initial rotation
    
    // Player state configuration
    player.currentState = PlayerState::ACTIVE;                         // Set player as active
    player.isDead = false;                                              // Player starts alive
    player.isActive = true;                                             // Player participates in game
    player.deathAnimation = DeathAnimationState::NONE;                  // No death animation initially
    
    // Health and combat statistics for shoot-em-up
    player.health = 100;                                                // Standard health amount
    player.maxHealth = 100;                                             // Maximum health capacity
    player.armour = 50;                                                 // Ship armour protection
    player.maxArmour = 100;                                             // Maximum armour capacity
    player.shield = 75;                                                 // Energy shield strength
    player.maxShield = 100;                                             // Maximum shield capacity
    
    // Scoring and progression
    player.score = 0;                                                   // Start with no score
    player.highScore = 0;                                               // No high score initially
    player.lives = 3;                                                   // Standard 3 lives for shoot-em-up
    player.level = 1;                                                   // Start at level 1
    player.experience = 0;                                              // No initial experience
    player.experienceToNext = 1000;                                     // 1000 XP needed for next level
    
    // Combat attributes for shoot-em-up gameplay
    player.attackPower = 25;                                            // Ship weapon damage
    player.defenseRating = 15;                                          // Ship defensive rating
    player.criticalChance = 10;                                         // 10% critical hit chance
    player.criticalMultiplier = 2;                                      // 2x critical damage
    player.attackSpeed = 2.5f;                                          // 2.5 attacks per second
    player.movementSpeed = 200.0f;                                      // 200 pixels per second movement
    
    // Resource management
    player.ammunition = 100;                                            // Starting ammunition count
    player.maxAmmunition = 150;                                         // Maximum ammunition capacity
    player.energy = 100;                                                // Energy for special weapons
    player.maxEnergy = 100;                                             // Maximum energy capacity
    
    // Timer system setup (useful for power-up durations, invincibility, etc.)
    player.timerActive = false;                                         // Timer not active initially
    player.timerStart = std::chrono::steady_clock::now();               // Initialize timer start
    player.timerCurrent = player.timerStart;                            // Initialize current time
    player.totalTimeElapsed = std::chrono::milliseconds(0);             // No elapsed time initially
    
    // Network player settings
    player.isNetworkPlayer = false;                                     // Local player by default
    player.networkSessionID = "";                                       // No network session initially
    player.networkLatency = 0;                                          // No network latency for local player
    
    #if defined(_DEBUG_GAMEPLAYER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"Shoot-em-up player %d created successfully", playerID);
    #endif
    
    return player;                                                      // Return configured player
}