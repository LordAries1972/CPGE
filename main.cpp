/* ------------------------------------------------------------------------------ */
// Main.cpp, the starting code to everything.
//
/* ------------------------------------------------------------------------------ */
/* DEVELOPER NOTES - Renderer Abstraction System                                  */
/* ------------------------------------------------------------------------------ */
/*
   PURPOSE:
   This project uses a unified rendering interface called "Renderer" to abstract
   multiple rendering backends (DirectX 11, DirectX 12, OpenGL, Vulkan, etc).

   MAIN POINTS:

   - Renderer.h defines the pure virtual interface: class Renderer
   - DX11Renderer, DX12Renderer, etc., are subclasses that implement it
   - At runtime, the engine selects and instantiates the desired backend:
        std::shared_ptr<Renderer> renderer = std::make_shared<DX11Renderer>();

   ENGINE CODE:
   - All core systems interact only through the Renderer interface:
        renderer->Initialize(...);
        renderer->RenderFrame();
        renderer->DrawMyText(...);

   DX11-SPECIFIC OR OTHER RENDERER'S CODE:
   - If you need a method defined in DX11Renderer lets say (but not in Renderer.h),
     then you MUST use a dynamic_pointer_cast safely as such:

        auto dx11 = std::dynamic_pointer_cast<DX11Renderer>(renderer);
        if (dx11) {
            dx11->SomeDX11SpecificMethod();
        }

   WARNING:
   - Do NOT define a global or local DX11Renderer named "renderer". It will conflict
     with the shared_ptr<Renderer> and cause linker/type errors.
   - Avoid using .get() unless absolutely necessary, and only on smart pointers.

   PLATFORM SAFETY:
   - DX11Renderer & DX12Renderer is Windows-only. Do NOT include DX11 or DX12 
     specific headers or calls in shared, platform-independent logic.

   See Renderer.h for a full architectural diagram.
/* ------------------------------------------------------------------------------ */
#include "Includes.h"
// Engine Subsystems
#include "MathPrecalculation.h"
#include "DX_FXManager.h"
#include "Debug.h"
#include "GUIManager.h"
#include "SoundManager.h"
#include "Models.h"
#include "Lights.h"
#include "Joystick.h"
#include "SceneManager.h"
#include "Configuration.h"
#include "ThreadManager.h"
#include "WinSystem.h"
#include "MoviePlayer.h"

#include "Renderer.h"
#include "RendererMacros.h"
#include "DX11Renderer.h"
#include "DX12Renderer.h"
#include "OpenGLRenderer.h"
#include "VulkanRenderer.h"

//------------------------------------------
// Platform Configuration Macros
//------------------------------------------
//#define __USING_JOYSTICKS__

#if defined(__USE_MP3PLAYER__)
#include "WinMediaPlayer.h"
#elif defined(__USE_XMPLAYER__)
#include "XMMODPlayer.h"
#endif

//------------------------------------------
// Constants
//------------------------------------------
const LPCWSTR MY_WINDOW_TITLE = L"DirectX 11 Renderer by Daniel J. Hobson of Australia 2024-2025";
const LPCWSTR lpDEFAULT_NAME = L"CPGE_";

// Player Joystick Controls
const int PLAYER_1 = 0;
const int PLAYER_2 = 1;
//const int PLAYER_3 = 2;
//const int PLAYER_4 = 3;

//--------------------------------------------------------
// Required Class Instantiations / Declarations
//
// Some classes will reference each other, so we need to
// declare them here to avoid circular dependencies.
//--------------------------------------------------------
Configuration config;
Joystick js;
SoundManager soundManager;
GUIManager guiManager;
Debug debug;
FXManager fxManager;
LightsManager lightsManager;
SceneManager scene;
ThreadManager threadManager;
SystemUtils sysUtils;
MoviePlayer moviePlayer;

#if defined(__USE_MP3PLAYER__)
MediaPlayer player;
#elif defined(__USE_XMPLAYER__)
XMMODPlayer xmPlayer;
#endif

// Our Models Buffer, Resources & Data.
Model models[MAX_MODELS];

// Requied State Variables - DO NOT REMOVE!
HWND hwnd;
HINSTANCE hInst;
WindowMetrics winMetrics;

bool isLeftClicked = false;
bool isRightClicked = false;
bool isMiddleClicked = false;
bool isSystemInitialized = false;
RECT rc;
POINT cursorPos;
Vector2 myMouseCoords;
char errorMsg[512];
POINT lastMousePos = {};
float yaw = 0.0f;
float pitch = 0.0f;
std::wstring baseDir = L"";

// Abstract Renderer Pointer
extern std::shared_ptr<Renderer> renderer;

// Forward Declarations
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
void SwitchToGamePlay();
void SwitchToGameIntro();
void SwitchToMovieIntro();

// Supressed Warnings
#pragma warning(disable : 28251)

// *----------------------------------------------------------------------------------------------
// Program Start!
// *----------------------------------------------------------------------------------------------
int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    SecureZeroMemory(&errorMsg, sizeof(errorMsg));
    baseDir = sysUtils.Get_Current_Directory();

    WindowsVersion winVer = sysUtils.GetWindowsVersion();
    if (winVer < WindowsVersion::WINVER_WIN10)
    {
        MessageBox(nullptr, L"Unsupported Windows Version.\nPlease use Windows 10 SP1 or later.", L"Error", MB_OK | MB_ICONERROR);
        return EXIT_FAILURE;
    }

    // We need to check for 64 Bit OS?
    // as we DO NOT support 32 bit no longer (Legacy!)
    
    
    // Create appropriate Render Interface
    if (CreateRendererInstance() != EXIT_SUCCESS)
        return EXIT_FAILURE;

    // Set up our Primary Window Class
    WNDCLASSEX wc = {};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(LR_DEFAULTCOLOR);
    wc.lpszClassName = lpDEFAULT_NAME;
    wc.hIconSm = LoadIcon(nullptr, IDI_APPLICATION);

    // Did Primary Window Registration fail?
    if (!RegisterClassEx(&wc)) {
        // Yes! Report to User and Exit!
        debug.LogError("[SYSTEM]: Failed to register window class.\n");
        return EXIT_FAILURE;
    }

    // Attempt to create our primary master window for our game!
    hwnd = CreateWindowEx(0, lpDEFAULT_NAME, MY_WINDOW_TITLE, WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, DEFAULT_WINDOW_WIDTH, DEFAULT_WINDOW_HEIGHT,
        nullptr, nullptr, hInstance, nullptr);

    // Did we fail to create our primary window?
    if (!hwnd) {
        // Yes! Report to User and Exit!
        debug.LogError("[SYSTEM]: Failed to create window.\n");
        UnregisterClass(lpDEFAULT_NAME, hInstance);
        return EXIT_FAILURE;
    }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);
    sysUtils.CenterSystemWindow(hwnd);

    try
    {
        if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) {
            debug.LogError("[SYSTEM]: Failed to initialize COM.");
            return EXIT_FAILURE;
        }

        // Load in our Configuration file.
        config.loadConfig();
        // Initialise our Sound Manager
        if (!soundManager.Initialize(hwnd)) {
            debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"Sound system initialization or loading failed.");
            return EXIT_FAILURE;
        }

        if (!FAST_MATH.Initialize())
        {
            #if defined(_DEBUG_MATHPRECALC_)
                debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"[Example] Failed to initialize MathPrecalculation!");
            #endif
            return -1;
        }

        // Now Initialise our Renderer
        renderer->Initialize(hwnd, hInstance);
        // Initialise our SceneManager
        scene.Initialize(renderer);
        scene.stSceneType = SceneType::SCENE_SPLASH;
        scene.SetGotoScene(SceneType::SCENE_INTRO);

        // Obtain Window Metrics
        sysUtils.GetWindowMetrics(hwnd, winMetrics);

        // Initialise our GUI Manager
        guiManager.Initialize(renderer.get());
		// Initialise our FX Manager
        fxManager.Initialize();
        // Initialise our Joysticks if we have any.
        #if !defined(RENDERER_IS_THREAD) && defined(__USE_DIRECTX_11__)
            WithDX11Renderer([](std::shared_ptr<DX11Renderer> dx11)
            {
                // Bind our Camera to the Joystick Controller.
                js.setCamera(&dx11->myCamera);
                // Configure for 3D usage (Like for FPS, TPS, 3D flight etc)
                js.ConfigureFor3DMovement();
            });
        #endif
        
        std::wstring alert = L"This is an alert status message.\n\n"
        L"Congratulations if you're seeing this window!\n"
        L"It means the system initialized correctly.\n";

//        guiManager.CreateAlertWindow(alert);

#ifndef _DEBUG
        renderer->SetFullScreen();
#endif

        fxManager.FadeToImage(1.0f, 0.04f);
//        fxManager.StartScrollEffect(BlitObj2DIndexType::IMG_SCROLLBG1, FXSubType::ScrollRight, 2, 800, 600, 0.016f);
//        fxManager.StartScrollEffect(BlitObj2DIndexType::IMG_SCROLLBG2, FXSubType::ScrollRight, 4, 800, 600, 0.016f);
//        fxManager.StartScrollEffect(BlitObj2DIndexType::IMG_SCROLLBG3, FXSubType::ScrollRight, 8, 800, 600, 0.016f);
        soundManager.StartPlaybackThread();
        
        // Initialise our MoviePlayer
        moviePlayer.Initialize(renderer, &threadManager);

        // --- Main Loop ---
        MSG msg = {};
        while (msg.message != WM_QUIT)
        {
            if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) 
            {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
            else 
            //  --------------------------------------------------------------------------
            //  Your Main Loop code goes here!!!!!
            //  --------------------------------------------------------------------------
            {
                switch (scene.stSceneType)
                {
                    // Our Starting / CPGE Splash Screen (Need to create a linking library for this section.
                    case SceneType::SCENE_SPLASH:
                    {
                        scene.sceneFrameCounter++;
                        if ((scene.sceneFrameCounter >= 300) && (!scene.bSceneSwitching))
                        {
                            scene.bSceneSwitching = true;
                            fxManager.FadeToBlack(1.0f, 0.06f);
                            WithDX11Renderer([](std::shared_ptr<DX11Renderer> dx11)
                            {
                                while (fxManager.IsFadeActive())
                                {
                                    dx11->RenderFrame();
                                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                                }
                            });
                        }

                        if ((fxManager.IsFadeActive()) && (scene.bSceneSwitching))
                        {
                            break;
                        }
                        else
                        {
                            if (scene.bSceneSwitching)
                            {
                                SwitchToMovieIntro();
                                WithDX11Renderer([](std::shared_ptr<DX11Renderer> dx11)
                                {
                                    while (fxManager.IsFadeActive())
                                    {
                                        dx11->RenderFrame();
                                        std::this_thread::sleep_for(std::chrono::milliseconds(5));
                                    }
                                });

                                break;
                            }
                        }

                        break;
                    }

                    // Our Game Intro Movie Screen
                    case SceneType::SCENE_INTRO_MOVIE:
                    {
                        if ((!moviePlayer.IsPlaying()) && (!scene.bSceneSwitching))
                        {
                            scene.bSceneSwitching = true;
                            fxManager.FadeToBlack(1.0f, 0.06f);
                            WithDX11Renderer([](std::shared_ptr<DX11Renderer> dx11)
                                {
                                    while (fxManager.IsFadeActive())
                                    {
                                        dx11->RenderFrame();
                                        std::this_thread::sleep_for(std::chrono::milliseconds(5));
                                    }
                                });
                        }

                        if ((fxManager.IsFadeActive()) && (scene.bSceneSwitching))
                        {
                            break;
                        }
                        else
                        {
                            if (scene.bSceneSwitching)
                            {
                                SwitchToGameIntro();
                                WithDX11Renderer([](std::shared_ptr<DX11Renderer> dx11)
                                    {
                                        while (fxManager.IsFadeActive())
                                        {
                                            dx11->RenderFrame();
                                            std::this_thread::sleep_for(std::chrono::milliseconds(5));
                                        }
                                    });

                                break;
                            }
                        }

                        break;
                    }

                    // Our Main Game
                    case SceneType::SCENE_GAMEPLAY:
                    {
                        // Handle events for all our Windows. 
                        guiManager.HandleAllInput(myMouseCoords, isLeftClicked);
                        // Process joystick input for buttons
                        js.processJoystickInput();

                        // Process joystick movement (for the first joystick)
                        // This will update the camera in 3D mode or update internal 2D 
                        // position in 2D mode
                        js.processJoystickMovement(PLAYER_1);

                        // If in 2D mode, get the current position (Joystick/Game pad example)
                        if (!js.is3DMode) {
                            float posX = js.getLastX();
                            float posY = js.getLastY();

                            // Use the 2D position to update game objects if using 
                            // 2D Gaming preference....


                            #if defined(_DEBUG)
                                debug.logLevelMessage(LogLevel::LOG_DEBUG,
                                    L"2D Position: X=" + std::to_wstring(posX) +
                                    L" Y=" + std::to_wstring(posY));
                            #endif
                        }
                        break;
                    }

                    default:
                        break;
                }

				// --- Direct X 11 Rendering inline safety calls NON Threaded ---
                #if !defined(RENDERER_IS_THREAD) && defined(__USE_DIRECTX_11__)
                    WithDX11Renderer([](std::shared_ptr<DX11Renderer> dx11)
                    {
                        if (!threadManager.threadVars.bIsRendering.load())
						    // Render the frame
                            dx11->RenderFrame();
                    });
                #endif

                // --- Direct X 12 Rendering inline safety calls NON Threaded ---
                               
                // --- OpenGL Rendering inline safety calls NON Threaded ---

                // --- Vulkan Rendering inline safety calls NON Threaded ---
            }
        } // End of while (msg.message != WM_QUIT) 
    }
    catch (const std::exception& e)
    {
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"Critical Error: %s", e.what());
    }

	// Save our Configuration file, Do this here, in-case cleanup fails
    config.saveConfig();

    if (moviePlayer.IsPlaying())
       moviePlayer.Stop();
    
    renderer->SetWindowedScreen();

//    fxManager.StopScrollEffect(BlitObj2DIndexType::IMG_SCROLLBG1);

    switch (scene.stSceneType)
    {
        case SceneType::SCENE_INTRO:
        {
            // Stop the 3D Starfield effect
            if (fxManager.starfieldID > 0)
            {
                fxManager.StopStarfield();
                break;
            }
        }

        default:
            break;
    }

    // Now Release FXManager
    fxManager.CleanUp();
    // We are Now finished with Scene Management
    scene.CleanUp();

    // Release the Renderer System.
    renderer->Cleanup();

    for (int i = 0; i < MAX_MODELS; i++)
        models[i].DestroyModel();

    // Stop and Terminate SoundManager system.
    soundManager.StopPlaybackThread();
    soundManager.CleanUp();

    // Destory our Created system window.
    sysUtils.DestroySystemWindow(hInstance, hwnd, lpDEFAULT_NAME);

    // Stop Music Playback.
    #if defined(__USE_MP3PLAYER__)
        player.stop();
    #elif defined(__USE_XMPLAYER__)
        xmPlayer.Shutdown();
    #endif

    CoUninitialize();
    return EXIT_SUCCESS;
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
        case WM_MOUSEMOVE:
            if (threadManager.threadVars.bSettingFullScreen.load()) { return 0; };
            GetCursorPos(&cursorPos);
            ScreenToClient(hwnd, &cursorPos);
            myMouseCoords.x = static_cast<float>(cursorPos.x);
            myMouseCoords.y = static_cast<float>(cursorPos.y);
            guiManager.HandleAllInput(myMouseCoords, isLeftClicked);

            WithDX11Renderer([](std::shared_ptr<DX11Renderer> dx11)
            {
                switch (scene.stSceneType)
                {
                    case SceneType::SCENE_GAMEPLAY:
                    {
                        // Has the Right Mouse button been clicked?
                        if (isRightClicked)
                        {
                            // Ensure we have no negative coordinates.
                            if (lastMousePos.x == 0 && lastMousePos.y == 0)
                            {
                                lastMousePos = cursorPos;
                                return 0; // prevent junk delta
                            }

                            // Adjust Camera to new position.
                            int deltaX = cursorPos.x - lastMousePos.x;
                            int deltaY = cursorPos.y - lastMousePos.y;
                            lastMousePos = cursorPos;

                            float sensitivity = config.myConfig.moveSensitivity;

                            yaw += deltaX * sensitivity;
                            pitch += deltaY * sensitivity;
                            float pitchMax = XMConvertToRadians(static_cast<float>(config.myConfig.maxPitch));
                            float pitchMin = XMConvertToRadians(static_cast<float>(config.myConfig.minPitch));
                            pitch = std::clamp(pitch, pitchMin, pitchMax);

                            XMVECTOR forward = XMVectorSet(
                                cosf(pitch) * sinf(yaw),
                                sinf(pitch),
                                cosf(pitch) * cosf(yaw),
                                0.0f
                            );

                            XMVECTOR right = XMVector3Normalize(XMVector3Cross({ 0, 1, 0 }, forward));
                            XMVECTOR up = XMVector3Normalize(XMVector3Cross(forward, right));

                            dx11->myCamera.SetYawPitch(yaw, pitch);
                        }

                        dx11->RenderFrame();
                        break;
                    }
                }

                guiManager.HandleAllInput(myMouseCoords, isLeftClicked);
                return 0;
            });

            return 0;

        case WM_LBUTTONDOWN:
            isLeftClicked = true;
            guiManager.HandleAllInput(myMouseCoords, isLeftClicked);
            return 0;

        case WM_RBUTTONDOWN:
            isRightClicked = true;
            GetCursorPos(&lastMousePos);
            ScreenToClient(hwnd, &lastMousePos);
            return 0;

        case WM_LBUTTONUP:
            isLeftClicked = false;
            guiManager.HandleAllInput(myMouseCoords, isLeftClicked);
            return 0;

        case WM_RBUTTONUP:
            isRightClicked = false;
            guiManager.HandleAllInput(myMouseCoords, isLeftClicked);
    //        fxManager.CreateParticleExplosion(myMouseCoords.x, myMouseCoords.y, 30, 300);  // x, y, NumPixels, MaxRadius
            return 0;

        case WM_SIZE:
		    if (!isSystemInitialized) return 0;
		    if (wParam == SIZE_MINIMIZED) 
            {
                #if defined(__USE_DIRECTX_11__)
                    WithDX11Renderer([](std::shared_ptr<DX11Renderer> dx11)
                    {
                        dx11->bIsMinimized.store(true);
                    });
                #endif
                return 0;
		    }

            if (wParam != SIZE_MINIMIZED && renderer && renderer->bIsInitialized.load()) {
                UINT width = LOWORD(lParam);
                UINT height = HIWORD(lParam);
                debug.logLevelMessage(LogLevel::LOG_INFO, L"WM_SIZE - Resizing to " +
                    std::to_wstring(width) + L"x" + std::to_wstring(height));

                switch (scene.stSceneType)
                {
                    case SceneType::SCENE_INTRO:
                    {
                        // Stop the 3D Starfield effect
                        if (fxManager.starfieldID > 0)
                        {
                            fxManager.StopStarfield();
                        }

                        return 0;
                    }

                    case SceneType::SCENE_GAMEPLAY:
                    {
                        #if defined(__USE_DIRECTX_11__)
                            WithDX11Renderer([width, height, hwnd](std::shared_ptr<DX11Renderer> dx11)
                            {
                                dx11->bIsMinimized.store(false);
                                threadManager.threadVars.bIsResizing.store(true);
                                // Reinitialise DirectX
                                dx11->Resize(width, height);
                                // Obtain New Window Metrics
                                sysUtils.GetWindowMetrics(hwnd, winMetrics);
                                // Resume the Loader thread and ensure nothing needs to be loaded / reloaded.
                                dx11->ResumeLoader(true);
                                // Resizing is now complete.
                                threadManager.threadVars.bIsResizing.store(false);
                            });
                        #endif
                        return 0;
                    }

                    default:
                        return 0;
                }
            }
            return 0;

        case WM_KILLFOCUS:
            return 0;

        case WM_MOUSEWHEEL:
        {
            switch (scene.stSceneType)
            {
                case SceneType::SCENE_GAMEPLAY:
                {
                    short delta = GET_WHEEL_DELTA_WPARAM(wParam);

                    // Zoom sensitivity scaling factor
                    const float zoomStep = 1.0f;

                    auto dx11 = std::dynamic_pointer_cast<DX11Renderer>(renderer);
                    if (dx11)
                    {
                        if (delta > 0)
                        {
                            dx11->myCamera.MoveIn(zoomStep);
                            #if defined(_DEBUG_CAMERA_)
                                debug.logDebugMessage(LogLevel::LOG_INFO, L"Camera Zoom In: delta = %d", delta);
                            #endif
                        }
                        else if (delta < 0)
                        {
                            dx11->myCamera.MoveOut(zoomStep);
                            #if defined(_DEBUG_CAMERA_)
                                debug.logDebugMessage(LogLevel::LOG_INFO, L"Camera Zoom Out: delta = %d", delta);
                            #endif
                        }
                    }
                    return 0;
                }
            }
            return 0;
        }
    
        case WM_ACTIVATE:
            // Are we in an INACTIVE state?
            if (wParam == WA_INACTIVE) {
			    if (!isSystemInitialized) { return 0; }
                #if defined(__USE_MP3PLAYER__)
                    player.pause();
                #elif defined(__USE_XMPLAYER__)
    //            if (!xmPlayer.IsPaused())
    //				xmPlayer.Pause();
                #endif
            }
		    // Are we in an ACTIVE state?
		    if (!isSystemInitialized) {
                isSystemInitialized = true;
                return 0; 
            }
            else if (wParam == WA_ACTIVE) {
                #if defined(__USE_MP3PLAYER__)
                    player.resume();
                #elif defined(__USE_XMPLAYER__)
    //                xmPlayer.HardResume();
                #endif
            }

            return 0;

        case WM_SETFOCUS:
            return 0;

        case WM_KEYUP:
            switch (scene.stSceneType)
            {
                case SceneType::SCENE_GAMEPLAY:
                {
                    if (wParam == VK_F2 && !threadManager.threadVars.bIsShuttingDown.load()) 
                    {
                        WithDX11Renderer([](std::shared_ptr<DX11Renderer> dx11)
                        {
                            dx11->bWireframeMode = !dx11->bWireframeMode;
                        });

                        return 0;
                    }
                }
            }

            if (wParam == VK_ESCAPE && !threadManager.threadVars.bIsShuttingDown.load()) {
                fxManager.FadeToBlack(1.0f, 0.03f);
                soundManager.PlayImmediateSFX(SFX_ID::SFX_BEEP);
                #if defined(__USE_DIRECTX_11__)
                    WithDX11Renderer([](std::shared_ptr<DX11Renderer> dx11)
                    {
                        while (fxManager.IsFadeActive()) {
                            dx11->RenderFrame();
                            std::this_thread::sleep_for(std::chrono::milliseconds(5));
                        }
                    });
                #endif
                threadManager.threadVars.bIsShuttingDown.store(true);
                PostQuitMessage(0);
            }
            return 0;

        case WM_CLOSE:
            if (!threadManager.threadVars.bIsShuttingDown.load()) {
                fxManager.FadeToBlack(1.0f, 0.03f);
                soundManager.PlayImmediateSFX(SFX_ID::SFX_BEEP);
                #if defined(__USE_DIRECTX_11__)
                    WithDX11Renderer([](std::shared_ptr<DX11Renderer> dx11)
                    {
                        while (fxManager.IsFadeActive()) {
                            dx11->RenderFrame();
                            std::this_thread::sleep_for(std::chrono::milliseconds(5));
                        }
                    });
                #endif
                threadManager.threadVars.bIsShuttingDown.store(true);
                PostQuitMessage(0);
            }
            return 0;

        case WM_DESTROY:
            if (!threadManager.threadVars.bIsShuttingDown.load())
                PostQuitMessage(0);
            return 0;

        case WM_KEYDOWN:
        {
            constexpr float moveStep = 0.75f;

            switch (wParam)
            {
            case VK_UP:
                #if defined(__USE_DIRECTX_11__)
                    WithDX11Renderer([](std::shared_ptr<DX11Renderer> dx11)
                    {
                        dx11->myCamera.MoveUp(moveStep);
                        dx11->RenderFrame();
                    });
                #endif
                break;

            case VK_DOWN:
                #if defined(__USE_DIRECTX_11__)
                    WithDX11Renderer([](std::shared_ptr<DX11Renderer> dx11)
                    {
                        dx11->myCamera.MoveDown(moveStep);
                        dx11->RenderFrame();
                    });
                #endif
                break;

            case VK_LEFT:
                #if defined(__USE_DIRECTX_11__)
                    WithDX11Renderer([](std::shared_ptr<DX11Renderer> dx11)
                    {
                        dx11->myCamera.MoveLeft(moveStep);
                        dx11->RenderFrame();
                    });
                #endif
                break;

            case VK_RIGHT:
                #if defined(__USE_DIRECTX_11__)
                    WithDX11Renderer([](std::shared_ptr<DX11Renderer> dx11)
                    {
                        dx11->myCamera.MoveRight(moveStep);
                        dx11->RenderFrame();
                    });
                #endif
                break;
            }

            return 0;
        }

        default:
            return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
}

void SwitchToGamePlay()
{
    scene.SetGotoScene(SCENE_GAMEPLAY);
    scene.InitiateScene();
    scene.SetGotoScene(SCENE_NONE);
    #if defined(__USE_DIRECTX_11__)
        WithDX11Renderer([](std::shared_ptr<DX11Renderer> dx11)
        {
            dx11->ResumeLoader();
        });
    #endif
}

void SwitchToMovieIntro()
{
    auto fileName = baseDir + L"\\Assets\\test1.mp4";
    if (!moviePlayer.OpenMovie(fileName))
    {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to open Video File for Playback!");
    }

    moviePlayer.Play();
    scene.SetGotoScene(SCENE_INTRO_MOVIE);
    scene.InitiateScene();
    scene.SetGotoScene(SCENE_GAMEPLAY);
    #if defined(__USE_DIRECTX_11__)
        WithDX11Renderer([](std::shared_ptr<DX11Renderer> dx11)
        {
            dx11->ResumeLoader();
        });
    #endif
}

void SwitchToGameIntro()
{
    scene.SetGotoScene(SCENE_INTRO);
    scene.InitiateScene();
    scene.SetGotoScene(SCENE_NONE);
    #if defined(__USE_DIRECTX_11__)
        WithDX11Renderer([](std::shared_ptr<DX11Renderer> dx11)
        {
            dx11->ResumeLoader();
        });
    #endif
}
