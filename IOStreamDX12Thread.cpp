/* ----------------------------------------------------------------
   DO NOT INCLUDE THIS FILE!!! THE PROJECT ITSELF SCOPES THIS FILE!
/* ----------------------------------------------------------------
This is the placement code for DX12Renderer Loader Thread.

This where you are to load & initialise all necessary resources
for the given scene.
----------------------------------------------------------------- */
#include "Includes.h"

#if defined(__USE_DIRECTX_12__)

#include "MathPrecalculation.h"
#include "ExceptionHandler.h"
#include "ThreadManager.h"
#include "WinSystem.h"
#include "Configuration.h"
#include "ShaderManager.h"
#include "Models.h"
#include "Lights.h"
#include "SoundManager.h"
#include "SceneManager.h"
#include "GUIManager.h"
#include "DX12FXManager.h"
#include "DX12Renderer.h"

using namespace SoundSystem;

// Other required external references.
extern ExceptionHandler exceptionHandler;
extern ThreadManager threadManager;
extern SystemUtils sysUtils;
extern SceneManager scene;
extern ShaderManager shaderManager;
extern SoundManager soundManager;
extern GUIManager guiManager;
extern FXManager fxManager;
extern Model models[MAX_MODELS];
extern LightsManager lightsManager;
extern WindowMetrics winMetrics;
extern bool bResizing;
extern int textScrollerEffectID;
extern bool Load_Music();                                                       // Function in main.cpp to load music for the game

// NOTE: s_loaderMutex is defined in DX12Renderer.cpp; no re-definition here.

/* -------------------------------------------------------------- */
// Main Tasking Thread for our I/O Loader Tasking Service
/* -------------------------------------------------------------- */
void DX12Renderer::LoaderTaskThread()
{
    exceptionHandler.RecordFunctionCall("DX12Renderer::LoaderTaskThread");
    std::lock_guard<std::mutex> lock(s_loaderMutex);

    // Check the status of the Loader thread
    ThreadStatus status = threadManager.GetThreadStatus(THREAD_LOADER);
    // State that we have loading to do....
    threadManager.threadVars.bLoaderTaskFinished.store(false);

    while (((status == ThreadStatus::Running) || (status == ThreadStatus::Paused)) &&
          (status != ThreadStatus::Terminated) && (!threadManager.threadVars.bIsShuttingDown.load()) && (status != ThreadStatus::Stopped))
    {
        // Be careful here! ProcessMessages() will initiate your message handler queue,
        // so make sure you don't do anything that could cause a crash and check all status
        // flags carefully before handling your messages within the message handler queue - !
        sysUtils.ProcessMessages();

        // Check status of our loader thread!
        status = threadManager.GetThreadStatus(THREAD_LOADER);
        if (status == ThreadStatus::Paused)
        {
            // Pause and then recheck
            Sleep(10);
            continue;
        }

        // (Add I/O loading tasks here)
        switch (scene.stSceneType)
        {
            #if defined(_DEBUG)
                // This is strictly for testing purposes, to test the loader thread and resource loading without having
                // to load the entire intro scene.  You can place any resources you want to test here, but remember
                // to #define _DEBUG in your preprocessor settings to enable this scene type.
                case SceneType::SCENE_EXPERIMENT:
                {
                    threadManager.threadVars.b2DTexturesLoaded.store(false);
                    if (LoadAllKnownTextures())
                        threadManager.threadVars.b2DTexturesLoaded.store(true);

                    // Screen is already black (button handler faded before ResumeLoader).
                    // Suspend any prior FX then start the tunnel before the fade-in begins.
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
                    // State that we have loaded all our required 2D Textures.
                    threadManager.threadVars.b2DTexturesLoaded.store(true);

                threadManager.PauseThread(THREAD_LOADER);
                threadManager.threadVars.bLoaderTaskFinished.store(true);
                break;
            }

            case SceneType::SCENE_GAMETITLE:
            {
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
                debug.logLevelMessage(LogLevel::LOG_INFO, L"[DX12 LOADER]: Scene Game Title.");
                showStage(L"Loading textures...");
                if (LoadAllKnownTextures())
                    // State that we have loaded all our required 2D Textures.
                    threadManager.threadVars.b2DTexturesLoaded.store(true);

                // If we are NOT resizing our window, then ....
                if (!wasResizing.load())
                {
                    // Title-screen back light: positioned deep in the background behind
                    // the ship, angled slightly downward toward the viewer.  This creates
                    // a dramatic rim/halo on the ship silhouette while the raised ambient
                    // keeps all material colours visible on the camera-facing surfaces.
                    //
                    // Direction convention: the vector is the direction the light TRAVELS.
                    // Shader computes L = -direction, so (0, -0.25, -1) normalised means
                    // light travels slightly down and INTO the screen; L points upward and
                    // toward the camera — illuminating the ship's front-upper geometry and
                    // creating bright specular highlights on any metallic parts.
                    showStage(L"Initialising lighting...");
                    LightStruct sunLight;
                    SecureZeroMemory(&sunLight, sizeof(LightStruct));
                    sunLight.active = true;
                    sunLight.position  = XMFLOAT3(0.0f, 5.0f, 150.0f);   // behind camera, slightly above centre

                    // Normalise direction manually: (0, -0.25, -1) / length = (0, -0.2425, -0.9701)
                    sunLight.direction = XMFLOAT3(0.0f, -0.2425f, -0.9701f);

                    sunLight.color        = XMFLOAT3(1.0f, 0.95f, 0.85f); // warm white — sun-like
                    // Raised ambient gives camera-facing surfaces a visible base colour even
                    // before direct light reaches them.  Slight cool-blue tint reads as
                    // outer-space fill light and prevents the dark side going pure black.
                    sunLight.ambient      = XMFLOAT3(0.32f, 0.32f, 0.38f);
                    sunLight.intensity    = 1.1f;
                    sunLight.baseIntensity = 0.4f;
                    sunLight.Shiningness  = 0.0f;
                    sunLight.Reflection   = 0.0f;
                    sunLight.lightFalloff = 0.1f;
                    sunLight.innerCone    = 30.0f;
                    sunLight.outerCone    = 60.0f;
                    sunLight.range        = 2000.0f;
                    sunLight.type = int(LightType::DIRECTIONAL);

                    lightsManager.CreateLight(L"Sun", sunLight);

                    // -----------------------------------------------------------------------------
                    // === PRE-ALLOCATE ALL MODEL TEXTURE VECTORS BEFORE LOADING ===
                    // -----------------------------------------------------------------------------
                    showStage(L"Parsing scene...");
                    ThreadLockHelper preAllocLock(threadManager, "SceneManager_PreAllocation", 2000);
                    if (preAllocLock.IsLocked())
                    {
                        scene.ParseGLTFScene(AssetsDir / L"splash-hover1.gltf");
                        if (!scene.bGltfCameraParsed)
                        {
                            scene.AutoFrameSceneToCamera();
                        }
                    }

                    showStage(L"Loading audio...");
                    Load_Music();

                    // Create Game Menu — derive viewport dimensions from the active display mode
                    {
                        float camW, camH;
                        switch (config.myConfig.displayMode)
                        {
                            case 2: // Fullscreen — use configured target resolution
                                camW = static_cast<float>(config.myConfig.resolutionWidth);
                                camH = static_cast<float>(config.myConfig.resolutionHeight);
                                break;
                            case 1: // Borderless — use full monitor area
                                camW = static_cast<float>(winMetrics.monitorFullArea.right  - winMetrics.monitorFullArea.left);
                                camH = static_cast<float>(winMetrics.monitorFullArea.bottom - winMetrics.monitorFullArea.top);
                                break;
                            default: // Windowed — client area only (excludes title bar / borders)
                                camW = static_cast<float>(winMetrics.clientWidth);
                                camH = static_cast<float>(winMetrics.clientHeight);
                                break;
                        }
                        myCamera.SetupDefaultCamera(camW, camH);
                    }
                    // Set Camera Position
                    myCamera.SetPosition(-5.0f, 2.0f, -20.0f);
                    // Now set Yaw and Pitch
                    myCamera.SetYawPitch(0.0f, 0.0f);

                    showStage(L"Building interface...");
                    guiManager.CreateGameMenuWindow(L"winGameMenu");

                    showStage(L"Almost ready...");
                    // Reverse — stars start spread near camera and converge toward {0, 0, 0}
                    fxManager.CreateStarfield(100, 800.0f, 1000.0f, XMFLOAT3(-180.0f, 0.0f, 0.0f), true);

                    fxManager.StopLoadingText();
                    fxManager.FadeToImage(1.0f, 0.08f);
                }
                else
                {
                    showStage(L"Updating display...");
                    // After a resize: reposition the GUI and recreate screen-size-dependent FX.
                    // winMetrics is refreshed by GetWindowMetrics() in main.cpp before the loader
                    // thread runs, so it reflects the new window state for all display modes.
                    {
                        float camW, camH;
                        switch (config.myConfig.displayMode)
                        {
                            case 2: // Fullscreen — use configured target resolution
                                camW = static_cast<float>(config.myConfig.resolutionWidth);
                                camH = static_cast<float>(config.myConfig.resolutionHeight);
                                break;
                            case 1: // Borderless — use full monitor area
                                camW = static_cast<float>(winMetrics.monitorFullArea.right  - winMetrics.monitorFullArea.left);
                                camH = static_cast<float>(winMetrics.monitorFullArea.bottom - winMetrics.monitorFullArea.top);
                                break;
                            default: // Windowed — client area only (excludes title bar / borders)
                                camW = static_cast<float>(winMetrics.clientWidth);
                                camH = static_cast<float>(winMetrics.clientHeight);
                                break;
                        }
                        myCamera.SetupDefaultCamera(camW, camH);
                    }
                    myCamera.SetPosition(-5.0f, 2.0f, -20.0f);
                    guiManager.OnWindowResize(iOrigWidth, iOrigHeight);

                    // Reverse — stars start spread near camera and converge toward {0, 0, 0}
                    fxManager.CreateStarfield(100, 800.0f, 1000.0f, XMFLOAT3(-180.0f, 0.0f, 0.0f), true);
                    fxManager.StopLoadingText();
                }

                // This must go at the end, so critical rendering can start
                threadManager.PauseThread(THREAD_LOADER);
                threadManager.threadVars.bLoaderTaskFinished.store(true);
                break;
            }

            /* This is used when you are using the MP3 Music Player!
               Ideally, this is for those who use DPM for music creation
               and to simplify the less hard work for them in understanding music
               integration.  But NOTE THIS!  MP3's can be quite expensive in memory
               consumption and of course, has its own goods and bads with the given
               operating system that you may be developing for.

               Just remember, using XM Trackers (I will promise to add other formats later)
               that using module style programming is far less memory conservative and
               warrants much more functionality in what you can or could do with ya music
               tracker file.  For example, I could write 5 songs into one tracker module
               file and when I use them in my scene switching, I position the tracker
               index before initiating playback.  Along that and been said, you can fade in or
               fade out your music before switching tracks, so your switch can be nicely done
               with volume controls etc.

               This is more a last resort for those who do not fully understand music integration systems.
            */
            case SceneType::SCENE_LOAD_MP3:
            {
                try {
                    #if defined(__USE_MP3PLAYER__)
                        CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);             // Must Initialize as STA (Single Threaded)
                        player.Initialize(hwnd);
                        auto fileName = AssetsDir / SingleMP3Filename;
                        if (player.loadFile(fileName)) {
                            debug.logLevelMessage(LogLevel::LOG_INFO, L"[DX12 LOADER]: MP3 File Re-loaded successfully.");
                            player.play();
                            player.fadeIn(5000);
                        }
                        else {
                            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[DX12 LOADER]: Failed to load file.");
                        }
                    #endif

                    // IMPORTANT: This must be called as the MediaPlayer will need
                    // ========== to process messages before playing!
                    sysUtils.GetMessageAndProcess();
                }
                catch (const std::exception& e) {
                    exceptionHandler.LogException(e, "[DX12 LOADER THREAD] SceneType::SCENE_LOAD_MP3");
                    debug.logLevelMessage(LogLevel::LOG_ERROR, L"[DX12 LOADER]: Exception: " + std::wstring(e.what(), e.what() + strlen(e.what())));
                }

                threadManager.PauseThread(THREAD_LOADER);
                threadManager.threadVars.bLoaderTaskFinished.store(true);
                // Make sure we release this regardless.
                CoUninitialize();                                                   // Uninitialize COM
                break;
            }

            /* Gameplay Scene is the level loaded for the current game progress where you are.
               This is basic atm, but the idea here is you load in all your assets and initialise
               as according, that is by setting your initial starting flags, the loading of your
               3D or 2D scene data and most of all, ensuring everything is done safely before
               presenting to the user to ensure things do not crash on you!  You can switch
               this around all you like, hence the wonderful thing of this project for you
               as a game developer / show caser! Since this is a thread loading system, locks
               and proper code order placements within the render system are here to ensure a
               proper render pipeline and safe-guard fail safes!
            */
            case SceneType::SCENE_GAMEPLAY:
            {
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

                debug.logLevelMessage(LogLevel::LOG_INFO, L"[DX12 LOADER]: Scene GAMEPLAY Initialising.");
                threadManager.threadVars.b2DTexturesLoaded.store(false);
                showStage(L"Loading textures...");
                if (LoadAllKnownTextures())
                    // State that we have loaded all our required 2D Textures.
                    threadManager.threadVars.b2DTexturesLoaded.store(true);

                // On first load only: create lights, parse scene, load music.
                // On resize: only 2D textures need reloading (models/lights survive resize).
                if (!wasResizing.load())
                {
                    // Create a default light
                    showStage(L"Initialising lighting...");
                    LightStruct sunLight;
                    SecureZeroMemory(&sunLight, sizeof(LightStruct));
                    sunLight.active = true;
                    sunLight.position = XMFLOAT3(10.0f, -3.0f, -100.0f);
                    sunLight.direction = XMFLOAT3(0.0f, 1.0f, 0.0f);
                    sunLight.color = XMFLOAT3(1.0f, 1.0f, 1.0f);
                    sunLight.ambient = XMFLOAT3(0.2f, 0.2f, 0.2f);
                    sunLight.intensity = 0.5f;
                    sunLight.baseIntensity = 0.1f;
                    sunLight.Shiningness = 0.0f;
                    sunLight.Reflection = 0.0f;
                    sunLight.lightFalloff = 0.1f;
                    sunLight.innerCone = 30.0f;
                    sunLight.outerCone = 60.0f;
                    sunLight.range = 1000.0f;
                    sunLight.type = int(LightType::DIRECTIONAL);

                    lightsManager.CreateLight(L"Sun", sunLight);

                    // -----------------------------------------------------------------------------
                    // === PRE-ALLOCATE ALL MODEL TEXTURE VECTORS BEFORE LOADING ===
                    // -----------------------------------------------------------------------------
                    showStage(L"Parsing scene...");
                    ThreadLockHelper preAllocLock(threadManager, "SceneManager_PreAllocation", 2000);
                    if (preAllocLock.IsLocked())
                    {
                        scene.ParseGLTFScene(AssetsDir / L"test2.gltf");
                        if (!scene.bGltfCameraParsed)
                        {
                            scene.AutoFrameSceneToCamera();
                        }
                    }

                    try
                    {
                        showStage(L"Loading audio...");
                        Load_Music();

                        // IMPORTANT: This must be called as the MediaPlayer will need
                        // ========== to process messages before starting playback!
                        sysUtils.ProcessMessages();
                    }
                    catch (const std::exception& e) {
                        exceptionHandler.LogException(e, "[DX12 LOADER THREAD] SceneType::GAMEPLAY");
                        threadManager.threadVars.bLoaderTaskFinished.store(true);
                        threadManager.PauseThread(THREAD_LOADER);
                        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[DX12 LOADER]: Exception: " + std::wstring(e.what(), e.what() + strlen(e.what())));
                    }
                } // End of first-load-only block

                fxManager.StopLoadingText();
                threadManager.threadVars.bLoaderTaskFinished.store(true);
                threadManager.PauseThread(THREAD_LOADER);
                break;
            }

            case SceneType::SCENE_GAMEOVER:
            {
                debug.logLevelMessage(LogLevel::LOG_INFO, L"[DX12 LOADER]: Scene Game Over.");
                threadManager.threadVars.bLoaderTaskFinished.store(true);
                break;
            }

            default:
            {
                threadManager.threadVars.bLoaderTaskFinished.store(true);
                break;
            }
        } // End of switch (scene.stSceneType)
    }

    // Reset Resize State Flag
    if (!threadManager.threadVars.bIsShuttingDown.load())
       wasResizing.store(false);

    debug.logLevelMessage(LogLevel::LOG_INFO, L"[DX12 LOADER]: Scene Loading Complete - Pausing Thread");
}
#endif // __USE_DIRECTX_12__
