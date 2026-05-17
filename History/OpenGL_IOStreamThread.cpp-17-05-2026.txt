/* ----------------------------------------------------------------
   DO NOT INCLUDE THIS FILE!!! THE PROJECT ITSELF SCOPES THIS FILE!
/* ----------------------------------------------------------------
This is the placement code for the OpenGLRenderer Loader Thread.

This is where you load & initialise all necessary resources
for the given scene.  Platform guards cover every OS-specific call.
----------------------------------------------------------------- */
#include "Includes.h"

#if defined(__USE_OPENGL__)

#include "MathPrecalculation.h"
#include "ExceptionHandler.h"
#include "ThreadManager.h"
#include "Models.h"
#include "Lights.h"
#include "SoundManager.h"
#include "SceneManager.h"
#include "GUIManager.h"
#include "OpenGLFXManager.h"
#include "OpenGLRenderer.h"

#if defined(PLATFORM_WINDOWS)
    #include "WinSystem.h"
    #include "ShaderManager.h"
#endif

using namespace SoundSystem;

extern ExceptionHandler exceptionHandler;
extern ThreadManager    threadManager;
extern SceneManager     scene;
extern SoundManager     soundManager;
extern GUIManager       guiManager;
extern GLFXManager      fxManager;
extern Model            models[MAX_MODELS];
extern LightsManager    lightsManager;
extern bool             bResizing;
extern int              textScrollerEffectID;
extern bool             Load_Music();

#if defined(PLATFORM_WINDOWS)
extern SystemUtils   sysUtils;
extern ShaderManager shaderManager;
extern WindowMetrics winMetrics;
#endif

std::mutex OpenGLRenderer::s_loaderMutex;

/* -------------------------------------------------------------- */
// Main Tasking Thread for our I/O Loader Tasking Service
/* -------------------------------------------------------------- */
void OpenGLRenderer::LoaderTaskThread()
{
    exceptionHandler.RecordFunctionCall("OpenGLRenderer::LoaderTaskThread");
    std::lock_guard<std::mutex> lock(s_loaderMutex);

    ThreadStatus status = threadManager.GetThreadStatus(THREAD_LOADER);
    threadManager.threadVars.bLoaderTaskFinished.store(false);

    while (((status == ThreadStatus::Running) || (status == ThreadStatus::Paused)) &&
           (status != ThreadStatus::Terminated) &&
           (!threadManager.threadVars.bIsShuttingDown.load()) &&
           (status != ThreadStatus::Stopped))
    {
#if defined(PLATFORM_WINDOWS)
        sysUtils.ProcessMessages();
#endif

        status = threadManager.GetThreadStatus(THREAD_LOADER);
        if (status == ThreadStatus::Paused)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        switch (scene.stSceneType)
        {
#if defined(_DEBUG)
            case SceneType::SCENE_EXPERIMENT:
            {
                threadManager.threadVars.b2DTexturesLoaded.store(false);
                if (LoadAllKnownTextures())
                    threadManager.threadVars.b2DTexturesLoaded.store(true);

                fxManager.SaveAndSuspendFXForScene();
                fxManager.Init3DWarpDOTTunnel(0.0f, 0.0f, 1000.0f, 10.0f, 200.0f,
                    TunnelSpinCycle::Clockwise, 100, false, 24, 100);

                threadManager.PauseThread(THREAD_LOADER);
                threadManager.threadVars.bLoaderTaskFinished.store(true);
                fxManager.FadeToImage(1.0f, 0.08f);
                break;
            }
#endif

            case SceneType::SCENE_INTRO:
            {
                threadManager.threadVars.b2DTexturesLoaded.store(false);
                if (LoadAllKnownTextures())
                    threadManager.threadVars.b2DTexturesLoaded.store(true);

                threadManager.PauseThread(THREAD_LOADER);
                threadManager.threadVars.bLoaderTaskFinished.store(true);
                break;
            }

            case SceneType::SCENE_GAMETITLE:
            {
                threadManager.threadVars.b2DTexturesLoaded.store(false);
                debug.logLevelMessage(LogLevel::LOG_INFO, L"[LOADER]: Scene GameTitle.");
                if (LoadAllKnownTextures())
                    threadManager.threadVars.b2DTexturesLoaded.store(true);

                if (!wasResizing.load())
                {
                    LightStruct sunLight{};
#if defined(PLATFORM_WINDOWS)
                    SecureZeroMemory(&sunLight, sizeof(LightStruct));
#endif
                    sunLight.active        = true;
                    sunLight.position      = XMFLOAT3(0.0f, 5.0f, -150.0f);
                    sunLight.direction     = XMFLOAT3(0.0f, -0.2425f, -0.9701f);
                    sunLight.color         = XMFLOAT3(1.0f, 0.95f, 0.85f);
                    sunLight.ambient       = XMFLOAT3(0.32f, 0.32f, 0.38f);
                    sunLight.intensity     = 1.1f;
                    sunLight.baseIntensity = 0.4f;
                    sunLight.Shiningness   = 0.0f;
                    sunLight.Reflection    = 0.0f;
                    sunLight.lightFalloff  = 0.1f;
                    sunLight.innerCone     = 30.0f;
                    sunLight.outerCone     = 60.0f;
                    sunLight.range         = 2000.0f;
                    sunLight.type          = int(LightType::DIRECTIONAL);
                    lightsManager.CreateLight(L"Sun", sunLight);

                    {
                        ThreadLockHelper preAllocLock(threadManager, "SceneManager_PreAllocation", 2000);
                        if (preAllocLock.IsLocked())
                        {
                            scene.ParseGLTFScene(AssetsDir / L"splash-hover1.gltf");
                            if (!scene.bGltfCameraParsed) scene.AutoFrameSceneToCamera();
                        }
                    }

                    Load_Music();

                    {
                        float camW, camH;
                        switch (config.myConfig.displayMode)
                        {
                            case 2:
                                camW = static_cast<float>(config.myConfig.resolutionWidth);
                                camH = static_cast<float>(config.myConfig.resolutionHeight);
                                break;
                            case 1:
#if defined(PLATFORM_WINDOWS)
                                camW = static_cast<float>(winMetrics.monitorFullArea.right  - winMetrics.monitorFullArea.left);
                                camH = static_cast<float>(winMetrics.monitorFullArea.bottom - winMetrics.monitorFullArea.top);
#else
                                camW = static_cast<float>(iOrigWidth);
                                camH = static_cast<float>(iOrigHeight);
#endif
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
                    }
                    myCamera.SetPosition(-5.0f, 2.0f, -20.0f);
                    myCamera.SetYawPitch(0.0f, 0.0f);

                    guiManager.CreateGameMenuWindow(L"winGameMenu");

                    XMFLOAT3 starOrigin(-180.0f, 0.0f, 0.0f);
                    fxManager.CreateStarfield(100, 800.0f, 1000.0f, starOrigin, true);
                    fxManager.FadeToImage(1.0f, 0.08f);
                }
                else
                {
                    {
                        float camW, camH;
                        switch (config.myConfig.displayMode)
                        {
                            case 2:
                                camW = static_cast<float>(config.myConfig.resolutionWidth);
                                camH = static_cast<float>(config.myConfig.resolutionHeight);
                                break;
                            case 1:
#if defined(PLATFORM_WINDOWS)
                                camW = static_cast<float>(winMetrics.monitorFullArea.right  - winMetrics.monitorFullArea.left);
                                camH = static_cast<float>(winMetrics.monitorFullArea.bottom - winMetrics.monitorFullArea.top);
#else
                                camW = static_cast<float>(iOrigWidth);
                                camH = static_cast<float>(iOrigHeight);
#endif
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
                    }
                    myCamera.SetPosition(-5.0f, 2.0f, -20.0f);
                    guiManager.OnWindowResize(iOrigWidth, iOrigHeight);

                    XMFLOAT3 starOrigin(-180.0f, 0.0f, 0.0f);
                    fxManager.CreateStarfield(100, 800.0f, 1000.0f, starOrigin, true);
                }

                threadManager.PauseThread(THREAD_LOADER);
                threadManager.threadVars.bLoaderTaskFinished.store(true);
                break;
            }

            case SceneType::SCENE_LOAD_MP3:
            {
                try {
#if defined(__USE_MP3PLAYER__) && defined(PLATFORM_WINDOWS)
                    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
                    auto fileName = AssetsDir / SingleMP3Filename;
                    if (player.loadFile(fileName)) {
                        debug.logLevelMessage(LogLevel::LOG_INFO, L"[LOADER]: MP3 loaded.");
                        player.play();
                        player.fadeIn(5000);
                    }
                    else {
                        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[LOADER]: Failed to load MP3.");
                    }
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
#if defined(PLATFORM_WINDOWS) && defined(__USE_MP3PLAYER__)
                CoUninitialize();
#endif
                break;
            }

            case SceneType::SCENE_GAMEPLAY:
            {
                debug.logLevelMessage(LogLevel::LOG_INFO, L"[LOADER]: Scene GAMEPLAY Initialising.");
                threadManager.threadVars.b2DTexturesLoaded.store(false);
                if (LoadAllKnownTextures())
                    threadManager.threadVars.b2DTexturesLoaded.store(true);

                if (!wasResizing.load())
                {
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

                    {
                        ThreadLockHelper preAllocLock(threadManager, "SceneManager_PreAllocation", 2000);
                        if (preAllocLock.IsLocked())
                        {
                            scene.ParseGLTFScene(AssetsDir / L"test2.gltf");
                            if (!scene.bGltfCameraParsed) scene.AutoFrameSceneToCamera();
                        }
                    }

                    try
                    {
                        Load_Music();
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

                threadManager.threadVars.bLoaderTaskFinished.store(true);
                threadManager.PauseThread(THREAD_LOADER);
                break;
            }

            case SceneType::SCENE_GAMEOVER:
            {
                debug.logLevelMessage(LogLevel::LOG_INFO, L"[LOADER]: Scene Game Over.");
                threadManager.threadVars.bLoaderTaskFinished.store(true);
                break;
            }

            default:
            {
                threadManager.threadVars.bLoaderTaskFinished.store(true);
                break;
            }
        }
    }

    if (!threadManager.threadVars.bIsShuttingDown.load())
        wasResizing.store(false);

    debug.logLevelMessage(LogLevel::LOG_INFO, L"[LOADER]: Scene Loading Complete - Pausing Thread");
}

#endif // __USE_OPENGL__
