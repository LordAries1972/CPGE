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
#include "FileIO.h"
#include "Debug.h"
#include "GUIManager.h"
#include "SoundManager.h"
#include "Joystick.h"
#include "Configuration.h"
#include "ThreadManager.h"
#include "WinSystem.h"
#include "NetworkManager.h"
#include "PUNPack.h"
#include "GamePlayer.h"
#include "GamingAI.h"
#include "MyRandomizer.h"

#if defined(_WIN64) || defined(_WIN32)
    #include "TTSManager.h"
#endif

#include "Renderer.h"
#if defined(__USE_DIRECTX_11__)
#include "DX11Renderer.h"
#elif define(__USE_DIRECTX_12__)
#include "DX12Renderer.h"
#elif defined(__USE_VULKAN__)
#include "VulkanRenderer.h"
#elif defined(__USE_OPENGL__)
#include "OpenGLRenderer.h"
#endif

// Include these after the Renderer's includes
#include "DX_FXManager.h"
#include "SceneManager.h"
#include "Models.h"
#include "Lights.h"
#include "MoviePlayer.h"

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
FileIO fileIO;
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
NetworkManager networkManager;
PUNPack punPack;
GamePlayer gamePlayer;
GamingAI gamingAI;
MyRandomizer myRandomizer;

#if defined(_WIN64) || defined(_WIN32)
    TTSManager ttsManager;
#endif

#if defined(__USE_MP3PLAYER__)
MediaPlayer player;
#elif defined(__USE_XMPLAYER__)
XMMODPlayer xmPlayer;
#endif

// Our Base Models Buffer, Resources & Data (Storage Only / Read Only!).
Model models[MAX_MODELS];

// Requied State Variables - DO NOT REMOVE!
HWND hwnd;
HINSTANCE hInst;
WindowMetrics winMetrics;

bool isStartingMovie = false;
bool isLeftClicked = false;
bool isRightClicked = false;
bool isMiddleClicked = false;
bool isSystemInitialized = false;

RECT rc;
POINT cursorPos;
Vector2 myMouseCoords;
std::atomic<bool> bResizeInProgress{ false };                           // Prevents multiple resize operations
std::atomic<bool> bFullScreenTransition{ false };                       // Prevents handling during fullscreen transitions

static std::chrono::steady_clock::time_point lastResizeTime;            // Debounce resize messages
static const int RESIZE_DEBOUNCE_MS = 100;                              // Minimum time between resize operations

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
void OpenMovieAndPlay();

// Supressed Warnings
#pragma warning(disable: 28251)
#pragma warning(disable: 4996)  // Suppress deprecated function warnings
#pragma warning(disable: 4267)  // Suppress size_t conversion warnings
#pragma warning(disable: 4101)  // Suppress warning C4101: 'e': unreferenced local variable

// *----------------------------------------------------------------------------------------------
// Program Start!
// *----------------------------------------------------------------------------------------------
int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    // Initialize local Buffers.
    SecureZeroMemory(&winMetrics, sizeof(WindowMetrics));
    SecureZeroMemory(&errorMsg, sizeof(errorMsg));

    baseDir = sysUtils.Get_Current_Directory();

    #if defined(_WIN32) || defined(_WIN64)
        WindowsVersion winVer = sysUtils.GetWindowsVersion();
        if (winVer < WindowsVersion::WINVER_WIN10)
        {
            MessageBox(nullptr, L"Unsupported Windows Version.\nPlease use Windows 10 SP1 64Bit or later.", L"Error", MB_OK | MB_ICONERROR);
            return EXIT_FAILURE;
        }

        // Are we using a 32 bit / 64Bit Operating system?
        if (sysUtils.Is64BitOperatingSystem())
        {
            #if defined(__USE_OPENGL__)
                MessageBox(nullptr, L"OpenGL must use 32Bit Compiling under the CPGE System.", L"Error", MB_OK | MB_ICONERROR);
                return EXIT_FAILURE;
            #endif
        }
    #endif

    // Create appropriate Renderer Interface
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

    sysUtils.CenterSystemWindow(hwnd);
    ShowWindow(hwnd, nCmdShow);

    try
    {
        if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) {
            debug.LogError("[SYSTEM]: Failed to initialize COM.");
            return EXIT_FAILURE;
        }

        // Load in our Configuration file.
        config.loadConfig();

        // Initialise our Randomizer system.
        if (!myRandomizer.Initialize()) {
            debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"Randomizer initialization has failed - Aborting!");
            return EXIT_FAILURE;
        }


        // Initialise our Sound Manager
        if (!soundManager.Initialize(hwnd)) {
            debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"Sound system initialization or loading failed.");
            return EXIT_FAILURE;
        }

        if (!FAST_MATH.Initialize())
        {
            #if defined(_DEBUG_MATHPRECALC_)
                debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"[Initialization] Failed to initialize MATHPrecalc!");
            #endif
            return EXIT_FAILURE;
        }

        // Initialize our Packer / UNPacker class
        if (!punPack.Initialize())
        {
            #if defined(_DEBUG_PUNPACK_)
                debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"[Initialization] Failed to initialize PUNPack!");
            #endif
            return EXIT_FAILURE;
        }

        // Initialize our GamingAI System.
        if (!gamingAI.Initialize())
        {
            #if defined(_DEBUG_PUNPACK_)
                debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"[Initialization] Failed to initialize PUNPack!");
            #endif
            return EXIT_FAILURE;
        }

        // Obtain Window Metrics
        sysUtils.GetWindowMetrics(hwnd, winMetrics);

        // Now Initialise our Renderer
        renderer->Initialize(hwnd, hInstance);

        // Initialise our SceneManager
        scene.Initialize(renderer);
        scene.stSceneType = SceneType::SCENE_SPLASH;
        scene.SetGotoScene(SceneType::SCENE_INTRO);
        #if defined(_DEBUG)
            winMetrics.isFullScreen = false;
            sysUtils.DisableMouseCursor();
            if (!renderer->StartRendererThreads())
            {
                MessageBox(nullptr, L"Problem Starting Renderer Threads!!!", L"Error", MB_OK | MB_ICONERROR);
                return EXIT_FAILURE;
            }
        #else
            sysUtils.DisableMouseCursor();
            renderer->SetFullExclusive(DEFAULT_WINDOW_WIDTH, DEFAULT_WINDOW_HEIGHT);
            winMetrics.isFullScreen = true;
        #endif

        // Initialise our GUI Manager
        guiManager.Initialize(renderer.get());
		// Initialise our FX Manager
        fxManager.Initialize();
        
        // If we are using windows, Initialize TTSManager (Text To Speech)
        #if defined(_WIN64) || defined(_WIN32)
            // Initialise our TTS Manager
            if (!ttsManager.Initialize()) {
                debug.logLevelMessage(LogLevel::LOG_WARNING, L"TTS system initialization failed - continuing without TTS");
            }
            else {
                #if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
                    debug.logLevelMessage(LogLevel::LOG_INFO, L"TTS system initialized successfully");
                #endif
            }
        #endif

        // Initialize Network Manager
        if (!networkManager.Initialize()) {
            debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"Network system initialization failed.");
            return EXIT_FAILURE;
        }
            
        std::wstring alert = L"This is an alert status message.\n\n"
        L"Congratulations if you're seeing this window!\n"
        L"It means the system initialized correctly.\n";

//        guiManager.CreateAlertWindow(alert);

        fxManager.FadeToImage(2.0f, 0.06f);
//        fxManager.StartScrollEffect(BlitObj2DIndexType::IMG_SCROLLBG1, FXSubType::ScrollRight, 2, 800, 600, 0.016f);
//        fxManager.StartScrollEffect(BlitObj2DIndexType::IMG_SCROLLBG2, FXSubType::ScrollRight, 4, 800, 600, 0.016f);
//        fxManager.StartScrollEffect(BlitObj2DIndexType::IMG_SCROLLBG3, FXSubType::ScrollRight, 8, 800, 600, 0.016f);
        
        // Start Sound Manager Thread.
        soundManager.StartPlaybackThread();
        
        // Initialise our MoviePlayer
        moviePlayer.Initialize(renderer, &threadManager);

        // Bind our Camera to the Joystick Controller.
        js.setCamera(&renderer->myCamera);
        // Configure for 3D usage (Like for FPS, TPS, 3D flight etc)
        js.ConfigureFor3DMovement();

        // Start Required Renderer Threads
        #if !defined(_DEBUG)
            if (!renderer->StartRendererThreads())
            {
                MessageBox(nullptr, L"Problem Starting Renderer Threads!!!", L"Error", MB_OK | MB_ICONERROR);
                PostQuitMessage(0);
                return EXIT_FAILURE;
            }
        #endif

        // Add TTS announcement for Startup Splash Screen
        if (config.myConfig.UseTTS)
        {
            if (ttsManager.GetPlaybackState() != TTSPlaybackState::STATE_ERROR) {
                ttsManager.SetSpeakerChannel(TTSSpeakerChannel::CHANNEL_BOTH);
                ttsManager.SetVoiceVolume(config.myConfig.TTSVolume);
                ttsManager.PlayAsync(L"This Game Production uses the Cross Platform Gaming Engine by Daniel J. Hobson of Australia 2025.");
            }
        }

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
                        try {
                            // Increment frame counter for splash screen duration timing
                            scene.sceneFrameCounter++;

                            // Check if splash screen duration has elapsed and we haven't started scene switching yet
                            if ((scene.sceneFrameCounter >= 4500000) && (!scene.bSceneSwitching))
                            {
                                #if defined(RENDERER_IS_THREAD) && defined(__USE_DIRECTX_11__)
                                    // Mark that we are beginning the scene transition process
                                    scene.bSceneSwitching = true;

                                    // Start fade to black effect with 1 second duration and small delay
                                    fxManager.FadeToBlack(2.0f, 0.06f);
                                    while (fxManager.IsFadeActive())
                                    {
                                        std::this_thread::sleep_for(std::chrono::milliseconds(10)); // Small delay to prevent CPU spinning
                                    }
                                
                                    scene.bSceneSwitching = true;
                                #endif
                                
                                #if defined(_DEBUG_SCENE_TRANSITION_)
                                    debug.logLevelMessage(LogLevel::LOG_INFO, L"[SCENE] Starting fade out from splash screen");
                                #endif

                                #if !defined(RENDERER_IS_THREAD) && defined(__USE_DIRECTX_11__)
                                // If renderer is not threaded, manually render frames during fade
                                try {
                                    WithDX11Renderer([](std::shared_ptr<DX11Renderer> dx11)
                                        {
                                            if (!dx11) {
                                                debug.logLevelMessage(LogLevel::LOG_ERROR, L"[SCENE] DX11 renderer is null during fade");
                                                return;
                                            }

                                            // Continue rendering while fade effect is active
                                            int frameCount = 0;                                    // Safety counter to prevent infinite loops
                                            const int maxFrames = 300;                             // Maximum frames to render (5 seconds at 60fps)

                                            while (fxManager.IsFadeActive() && frameCount < maxFrames)
                                            {
                                                try {
                                                    dx11->RenderFrame();                           // Render current frame
                                                    frameCount++;                                  // Increment safety counter
                                                    std::this_thread::sleep_for(std::chrono::milliseconds(5)); // Small delay to prevent CPU spinning
                                                }
                                                catch (const std::exception& e) {
                                                    debug.logLevelMessage(LogLevel::LOG_ERROR, L"[SCENE] Exception during fade rendering: " +
                                                        std::wstring(e.what(), e.what() + strlen(e.what())));
                                                    break;                                         // Exit render loop on error
                                                }
                                            }

                                            if (frameCount >= maxFrames) {
                                                debug.logLevelMessage(LogLevel::LOG_WARNING, L"[SCENE] Fade rendering exceeded maximum frames - forcing completion");
                                            }
                                        });
                                }
                                catch (const std::exception& e) {
                                    debug.logLevelMessage(LogLevel::LOG_ERROR, L"[SCENE] Exception in fade rendering: " +
                                        std::wstring(e.what(), e.what() + strlen(e.what())));
                                }
                                #else
                                #endif

                            }

                            // Check if we are in scene switching mode and fade has completed
                            if (scene.bSceneSwitching && !fxManager.IsFadeActive())
                            {
                                try {
                                    // Fade has completed, now perform the actual scene switch
                                    SwitchToMovieIntro();

                                    #if !defined(RENDERER_IS_THREAD) && defined(__USE_DIRECTX_11__)
                                        // If renderer is not threaded, render frames during fade in
                                        try {
                                            WithDX11Renderer([](std::shared_ptr<DX11Renderer> dx11)
                                                {
                                                    if (!dx11) {
                                                        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[SCENE] DX11 renderer is null during fade in");
                                                        return;
                                                    }

                                                    // Continue rendering while new scene fade effect is active
                                                    int frameCount = 0;                               // Safety counter
                                                    const int maxFrames = 300;                        // Maximum frames to render

                                                    while (fxManager.IsFadeActive() && frameCount < maxFrames)
                                                    {
                                                        try {
                                                            dx11->RenderFrame();                       // Render current frame
                                                            frameCount++;                              // Increment safety counter
                                                            std::this_thread::sleep_for(std::chrono::milliseconds(5)); // Small delay
                                                        }
                                                        catch (const std::exception& e) {
                                                            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[SCENE] Exception during fade in rendering: " +
                                                                std::wstring(e.what(), e.what() + strlen(e.what())));
                                                            break;
                                                        }
                                                    }
                                                });
                                        }
                                        catch (const std::exception& e) {
                                            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[SCENE] Exception in fade in rendering: " +
                                                std::wstring(e.what(), e.what() + strlen(e.what())));
                                        }
                                    #endif
                                }
                                catch (const std::exception& e) {
                                    debug.logLevelMessage(LogLevel::LOG_ERROR, L"[SCENE] Exception during scene switch: " +
                                        std::wstring(e.what(), e.what() + strlen(e.what())));
                                    scene.bSceneSwitching = false;                                 // Reset flag to prevent hanging
                                }
                            }
                        }
                        catch (const std::exception& e) {
                            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[SCENE] Exception in SCENE_SPLASH: " +
                                std::wstring(e.what(), e.what() + strlen(e.what())));

                            // Force scene transition to prevent hanging
                            SwitchToMovieIntro();
                        }

                        // Continue normal splash screen processing if not switching scenes
                        break;
                    }

                    // Our Game Intro Movie Screen
                    case SceneType::SCENE_INTRO_MOVIE:
                    {
                        static bool loggedMovieEntry = false;
                        static int framesSinceMovieStart = 0;
                        static bool movieInitialized = false;

                        if (!loggedMovieEntry)
                        {
                            debug.logLevelMessage(LogLevel::LOG_INFO, L"[SCENE] Entered SCENE_INTRO_MOVIE");
                            loggedMovieEntry = true;
                            framesSinceMovieStart = 0;
                            movieInitialized = false;
                        }

                        framesSinceMovieStart++;

                        // Check if movie has been initialized (has duration > 0)
                        if (!movieInitialized && moviePlayer.GetDuration() > 0.0)
                        {
                            movieInitialized = true;
                            debug.logLevelMessage(LogLevel::LOG_INFO, L"[SCENE] Movie initialized successfully");
                        }

                        // CRITICAL: Only check for movie completion if:
                        // 1. Movie has been properly initialized (has duration > 0)
                        // 2. Movie was actually playing at some point
                        // 3. We've given enough time for the movie to start
                        static bool movieHasStarted = false;

                        // Check if movie has started playing
                        if (moviePlayer.IsPlaying() && moviePlayer.GetDuration() > 0.0)
                        {
                            movieHasStarted = true;
                        }

                        // Only check for completion after movie has started and sufficient time has passed
                        if (movieHasStarted && framesSinceMovieStart > 120 && (!moviePlayer.IsPlaying()) && (!scene.bSceneSwitching))
                        {
                            #if defined(_DEBUG_SCENE_TRANSITION_)
                                debug.logLevelMessage(LogLevel::LOG_INFO, L"[SCENE] Movie finished playing, starting scene transition");
                            #endif

                            scene.bSceneSwitching = true;
                            fxManager.FadeToBlack(1.0f, 0.06f);

                            #if !defined(RENDERER_IS_THREAD) && defined(__USE_DIRECTX_11__)
                                WithDX11Renderer([](std::shared_ptr<DX11Renderer> dx11)
                                {
                                    while (fxManager.IsFadeActive())
                                    {
                                        dx11->RenderFrame();
                                        std::this_thread::sleep_for(std::chrono::milliseconds(5));
                                    }
                                });
                            #endif      
                        }

                        // Handle space bar skip
                        if (GetAsyncKeyState(VK_SPACE) & 0x8000 && moviePlayer.IsPlaying())
                        {
                            #if defined(_DEBUG_SCENE_TRANSITION_)
                                debug.logLevelMessage(LogLevel::LOG_INFO, L"[SCENE] Space bar pressed - skipping movie");
                            #endif

                            moviePlayer.Stop();
                            scene.bSceneSwitching = true;
                            fxManager.FadeToBlack(1.0f, 0.06f);
                        }

                        if ((fxManager.IsFadeActive()) && (scene.bSceneSwitching))
                        {
                            break;
                        }
                        else
                        {
                            if (scene.bSceneSwitching)
                            {
                                #if defined(_DEBUG_SCENE_TRANSITION_)
                                    debug.logLevelMessage(LogLevel::LOG_INFO, L"[SCENE] Switching to game intro");
                                #endif

                                // Reset static variables for next time
                                framesSinceMovieStart = 0;
                                movieHasStarted = false;
                                movieInitialized = false;

                                SwitchToGameIntro();

                                #if !defined(RENDERER_IS_THREAD)
                                    while (fxManager.IsFadeActive())
                                    {
                                        renderer->RenderFrame();
                                        std::this_thread::sleep_for(std::chrono::milliseconds(5));
                                    }
                                #endif

                                break;
                            }
                        }

                        break;
                    }

                    // Our Main Game
                    case SceneType::SCENE_GAMEPLAY:
                    {
                        // Collect AI data if monitoring is active
                        if (gamingAI.IsMonitoring()) {
                            // Collect player position data for AI analysis
                            for (int playerID : gamePlayer.GetActivePlayerIDs()) {
                                const PlayerInfo* playerInfo = gamePlayer.GetPlayerInfo(playerID);
                                if (playerInfo != nullptr) {
                                    gamingAI.CollectPlayerPositionData(static_cast<uint32_t>(playerID), playerInfo->position2D);
                                }
                            }
                        }

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
					// Render the frame
                    renderer->RenderFrame();
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

    // TTS Stop
    #if defined(_WIN64) || defined(_WIN32)
        if (config.myConfig.UseTTS)
        {
            ttsManager.Stop();                          // Stop immediately
            ttsManager.CleanUp();
        }
    #endif

    // Release the Renderer System.
    renderer->Cleanup();

    renderer->SetWindowedScreen();

    // Force Stop AI monitoring and Clean-Up
    gamingAI.EndMonitoring();
    gamingAI.Cleanup();

    // Clean up the GamePlayer Manager
    gamePlayer.Cleanup();

    // Clean up Network Manager
    networkManager.Cleanup();

    // Cleanup Cached Base Models
    for (int i = 0; i < MAX_MODELS; i++)
        models[i].DestroyModel();

    // Stop and Terminate SoundManager system.
    soundManager.StopPlaybackThread();
    soundManager.CleanUp();

    // Destory our PUNPack Class.
    punPack.Cleanup();

    // Destory our Created system window.
    sysUtils.DestroySystemWindow(hInstance, hwnd, lpDEFAULT_NAME);

    // Stop Music Playback.
    #if defined(__USE_MP3PLAYER__)
        player.stop();
    #elif defined(__USE_XMPLAYER__)
        xmPlayer.Shutdown();
    #endif

    // -------------------------------
    // Stop the FileIO tasking thread.
    // -------------------------------
    // Check initial state of FileIO queue for any write operations.
    // We need to do this check to ensure we DO NOT corrupt files.
    bool initialHasPendingWrites = fileIO.HasPendingWriteTasks();
    size_t writeCount = fileIO.GetPendingWriteTaskCount();

    // Monitor write task completion over time
    int monitorAttempts = 0;
    const int maxMonitorAttempts = 150;                                  // Monitor for up to 15 seconds

    while (monitorAttempts < maxMonitorAttempts) {
        bool currentHasPendingWrites = fileIO.HasPendingWriteTasks();
        size_t currentWriteCount = fileIO.GetPendingWriteTaskCount();

        if (currentWriteCount != writeCount) {
            // Write task count changed - log the update
            #if defined(_DEBUG_FILEIO_DEMO_) && defined(_DEBUG)
                debug.logDebugMessage(LogLevel::LOG_WARNING, L"[FileIO] Write task progress - Previous count: %zu, Current count: %zu",
                    writeCount, currentWriteCount);
            #endif
            writeCount = currentWriteCount;
        }

        // Break if all write tasks completed
        if (!currentHasPendingWrites) {
            #if defined(_DEBUG_FILEIO_DEMO_) && defined(_DEBUG)
                debug.logLevelMessage(LogLevel::LOG_INFO, L"[FileIO] All write tasks have now completed successfully");
            #endif
            break;
        }

        // Sleep briefly before next check
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        monitorAttempts++;
    }

    // Final status check
    bool finalHasPendingWrites = fileIO.HasPendingWriteTasks();
    size_t finalWriteCount = fileIO.GetPendingWriteTaskCount();

    // Now we can STOP the fileIO system.
    fileIO.Cleanup();

    // Cleanup our MyRandomizer class instance.
    myRandomizer.Cleanup();

    // IMPORTANT: DO THIS LAST!!! 
    // Now clean up the Thread Manager.
    threadManager.Cleanup();

    CoUninitialize();
    return EXIT_SUCCESS;
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
        case WM_MOUSEMOVE:
            // Check if we are in fullscreen transition or resize - ignore mouse messages
            if (threadManager.threadVars.bSettingFullScreen.load() ||
                bResizeInProgress.load() || !isSystemInitialized ||
                bFullScreenTransition.load()) {
                return 0;
            }

            GetCursorPos(&cursorPos);
            ScreenToClient(hwnd, &cursorPos);
            myMouseCoords.x = static_cast<float>(cursorPos.x);
            myMouseCoords.y = static_cast<float>(cursorPos.y);
            if (cursorPos.x >= winMetrics.width) { myMouseCoords.x = winMetrics.width; SetCursorPos(cursorPos.x, cursorPos.y); }
            if (cursorPos.y >= winMetrics.height) { myMouseCoords.y = winMetrics.height; SetCursorPos(cursorPos.x, cursorPos.y); }
            guiManager.HandleAllInput(myMouseCoords, isLeftClicked);

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
                            FAST_COS(pitch) * FAST_SIN(yaw),
                            FAST_SIN(pitch),
                            FAST_COS(pitch) * FAST_COS(yaw),
                            0.0f
                        );

                        XMVECTOR right = XMVector3Normalize(XMVector3Cross({ 0, 1, 0 }, forward));
                        XMVECTOR up = XMVector3Normalize(XMVector3Cross(forward, right));

                        renderer->myCamera.SetYawPitch(yaw, pitch);
                    }

                    // Only render if not resizing or in fullscreen transition
                    #if !defined(RENDERER_IS_THREAD)
                        if (!bResizeInProgress.load() && !bFullScreenTransition.load()) {
                            renderer->RenderFrame();
                        }
                    #endif
                    break;
                }
            }

            guiManager.HandleAllInput(myMouseCoords, isLeftClicked);
            return 0;

        case WM_LBUTTONDOWN:
            // Ignore input during resize or fullscreen transitions
            if (bResizeInProgress.load() || bFullScreenTransition.load()) {
                return 0;
            }

            isLeftClicked = true;
            guiManager.HandleAllInput(myMouseCoords, isLeftClicked);
            return 0;

        case WM_RBUTTONDOWN:
            // Ignore input during resize or fullscreen transitions
            if (bResizeInProgress.load() || bFullScreenTransition.load()) {
                return 0;
            }

            isRightClicked = true;
            GetCursorPos(&lastMousePos);
            ScreenToClient(hwnd, &lastMousePos);
            return 0;

        case WM_LBUTTONUP:
            // Ignore input during resize or fullscreen transitions
            if (bResizeInProgress.load() || bFullScreenTransition.load()) {
                return 0;
            }

            isLeftClicked = false;
            guiManager.HandleAllInput(myMouseCoords, isLeftClicked);
            return 0;

        case WM_RBUTTONUP:
            // Ignore input during resize or fullscreen transitions
            if (bResizeInProgress.load() || bFullScreenTransition.load()) {
                return 0;
            }

            isRightClicked = false;
            guiManager.HandleAllInput(myMouseCoords, isLeftClicked);
            return 0;

        case WM_SIZE:
            // Early exit if system not initialized or already resizing
            if (!isSystemInitialized || bResizeInProgress.load()) {
                return 0;
            }

            // Handle minimization state immediately
            if (wParam == SIZE_MINIMIZED) {
                #if defined(__USE_DIRECTX_11__)
                    renderer->bIsMinimized.store(true);
                #endif
                return 0;
            }

            // Only process resize if renderer is initialized, not minimised and not terminating
            if (wParam != SIZE_MINIMIZED && renderer && renderer->bIsInitialized.load() && !threadManager.threadVars.bIsShuttingDown.load()) {

                // Implement debouncing to prevent rapid resize messages
                auto currentTime = std::chrono::steady_clock::now();
                auto timeSinceLastResize = std::chrono::duration_cast<std::chrono::milliseconds>(
                    currentTime - lastResizeTime).count();

                if (timeSinceLastResize < RESIZE_DEBOUNCE_MS) {
                    return 0; // Too soon, ignore this resize message
                }

                lastResizeTime = currentTime;

                // Set resize in progress flag to prevent race conditions
                bResizeInProgress.store(true);

                UINT width = LOWORD(lParam);
                UINT height = HIWORD(lParam);

                debug.logLevelMessage(LogLevel::LOG_INFO, L"WM_SIZE - Beginning resize to " +
                    std::to_wstring(width) + L"x" + std::to_wstring(height));

                // Stop all active FX effects before resize
                fxManager.StopAllFXForResize();

                switch (scene.stSceneType)
                {
                    case SceneType::SCENE_INTRO:
                    case SceneType::SCENE_GAMEPLAY:
                    {
                        // Perform the resize operation
                        renderer->bIsMinimized.store(false);
                        threadManager.threadVars.bIsResizing.store(true);

                        // Perform DirectX resize
                        renderer->Resize(width, height);

                        // Update window metrics
                        sysUtils.GetWindowMetrics(hwnd, winMetrics);

                        // Resume loader with resize flag
                        renderer->ResumeLoader(true);

                        // Clear resize flag
                        threadManager.threadVars.bIsResizing.store(false);
                        #if defined(RENDERER_IS_THREAD)
                            // Now Resume the Renderer Thread.
                            if (threadManager.threadVars.bLoaderTaskFinished.load())
                                threadManager.ResumeThread(THREAD_RENDERER);
                        #endif
                        break;
                    }

                default:
                    debug.logLevelMessage(LogLevel::LOG_WARNING, L"Resize attempted in unsupported scene type");
                    break;
                }

                // Clear resize in progress flag
                bResizeInProgress.store(false);

                debug.logLevelMessage(LogLevel::LOG_INFO, L"WM_SIZE - Resize completed successfully");
            }
            return 0;

        case WM_KILLFOCUS:
            // No special handling needed during resize
            return 0;

        case WM_MOUSEWHEEL:
        {
            // Ignore mouse wheel during resize or fullscreen transitions
            if (bResizeInProgress.load() || bFullScreenTransition.load()) {
                return 0;
            }

            switch (scene.stSceneType)
            {
                case SceneType::SCENE_GAMEPLAY:
                {
                    short delta = GET_WHEEL_DELTA_WPARAM(wParam);

                    // Zoom sensitivity scaling factor
                    const float zoomStep = 1.0f;

                    if (delta > 0)
                    {
                        renderer->myCamera.MoveIn(zoomStep);
                        #if defined(_DEBUG_CAMERA_)
                            debug.logDebugMessage(LogLevel::LOG_INFO, L"Camera Zoom In: delta = %d", delta);
                        #endif
                    }
                    else if (delta < 0)
                    {
                        renderer->myCamera.MoveOut(zoomStep);
                        #if defined(_DEBUG_CAMERA_)
                            debug.logDebugMessage(LogLevel::LOG_INFO, L"Camera Zoom Out: delta = %d", delta);
                        #endif
                    }
                    return 0;
                }
            }
            return 0;
        }

        case WM_ACTIVATE:
            // Handle activation/deactivation, but be careful during resize
            if (wParam == WA_INACTIVE) {
                if (!isSystemInitialized || bResizeInProgress.load()) {
                    return 0;
                }
                #if defined(__USE_MP3PLAYER__)
                    player.pause();
                #elif defined(__USE_XMPLAYER__)
                    // Uncomment if needed: 
                    // if (!xmPlayer.IsPaused()) xmPlayer.Pause();
                #endif
            }

            if (!isSystemInitialized) {
                isSystemInitialized = true;
                return 0;
            }
            else if (wParam == WA_ACTIVE && !bResizeInProgress.load()) {
                #if defined(__USE_MP3PLAYER__)
                    player.resume();
                #elif defined(__USE_XMPLAYER__)
                    // Uncomment if needed: 
                    // xmPlayer.HardResume();
                #endif
            }

            return 0;

        case WM_SETFOCUS:
            // No special handling needed
            return 0;

        case WM_KEYUP:
            // Ignore key input during resize or fullscreen transitions
            if (bResizeInProgress.load() || bFullScreenTransition.load()) {
                return 0;
            }

            switch (scene.stSceneType)
            {
                case SceneType::SCENE_GAMEPLAY:
                {
                    if (wParam == VK_F2 && !threadManager.threadVars.bIsShuttingDown.load())
                    {
                        renderer->bWireframeMode = renderer->bWireframeMode;
                        return 0;
                    }

                    // ADJUST Later: Have the AI Monitor Keyboard interaction.
                    if (gamingAI.IsMonitoring()) {
                        gamingAI.CollectInputEventData(INPUT_TYPE_KEYBOARD, static_cast<uint32_t>(wParam)); // For keyboard
                    }
                }
            }

            // Handle ESC key for shutdown
            if (wParam == VK_ESCAPE && !threadManager.threadVars.bIsShuttingDown.load()) {
                fxManager.FadeToBlack(1.0f, 0.03f);
                soundManager.PlayImmediateSFX(SFX_ID::SFX_BEEP);
                while (fxManager.IsFadeActive()) 
                {
                    #if !defined(RENDERER_IS_THREAD)
                        renderer->RenderFrame();
                    #endif
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }
                threadManager.threadVars.bIsShuttingDown.store(true);
                PostQuitMessage(0);
            }

            return 0;

        case WM_CLOSE:
            // Handle close request with proper fade effect
            if (!threadManager.threadVars.bIsShuttingDown.load()) {
                fxManager.FadeToBlack(1.0f, 0.03f);
                soundManager.PlayImmediateSFX(SFX_ID::SFX_BEEP);
                while (fxManager.IsFadeActive()) 
                {
                    #if !defined(RENDERER_IS_THREAD)
                        renderer->RenderFrame();
                    #endif
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }
                threadManager.threadVars.bIsShuttingDown.store(true);
                PostQuitMessage(0);
            }
            return 0;

        case WM_DESTROY:
            // Handle destroy message
            if (!threadManager.threadVars.bIsShuttingDown.load())
                PostQuitMessage(0);
            return 0;

        case WM_KEYDOWN:
        {
            // Ignore key input during resize or fullscreen transitions
            if (bResizeInProgress.load() || bFullScreenTransition.load()) {
                return 0;
            }

            constexpr float moveStep = 0.75f;

            switch (wParam)
            {
                case VK_UP:
                    switch (scene.stSceneType)
                    {
                        case SceneType::SCENE_GAMEPLAY:
                            renderer->myCamera.MoveUp(moveStep);
                            #if !defined(RENDERER_IS_THREAD)
                                if (!bResizeInProgress.load()) {
                                    renderer->RenderFrame();
                                }
                            #endif

                            // ADJUST Later: Have the AI Monitor Keyboard interaction.
                            if (gamingAI.IsMonitoring()) {
                                gamingAI.CollectInputEventData(INPUT_TYPE_KEYBOARD, static_cast<uint32_t>(wParam)); // For keyboard
                            }

                            break;
                    }

                case VK_DOWN:
                    switch (scene.stSceneType)
                    {
                        case SceneType::SCENE_GAMEPLAY:
                            renderer->myCamera.MoveDown(moveStep);
                            #if !defined(RENDERER_IS_THREAD)
                                if (!bResizeInProgress.load()) {
                                    renderer->RenderFrame();
                                }
                            #endif

                            if (!bResizeInProgress.load()) {
                                renderer->RenderFrame();
                            }

                            // ADJUST Later: Have the AI Monitor Keyboard interaction.
                            if (gamingAI.IsMonitoring()) {
                                gamingAI.CollectInputEventData(INPUT_TYPE_KEYBOARD, static_cast<uint32_t>(wParam)); // For keyboard
                            }

                            break;
                    }

                case VK_LEFT:
                    switch (scene.stSceneType)
                    {
                        case SceneType::SCENE_GAMEPLAY:
                            renderer->myCamera.MoveLeft(moveStep);
                            #if !defined(RENDERER_IS_THREAD)
                                if (!bResizeInProgress.load()) {
                                    renderer->RenderFrame();
                                }
                            #endif

                            // ADJUST Later: Have the AI Monitor Keyboard interaction.
                            if (gamingAI.IsMonitoring()) {
                                gamingAI.CollectInputEventData(INPUT_TYPE_KEYBOARD, static_cast<uint32_t>(wParam)); // For keyboard
                            }

                            break;
                    }

                case VK_RIGHT:
                    switch (scene.stSceneType)
                    {
                        case SceneType::SCENE_GAMEPLAY:
                            renderer->myCamera.MoveRight(moveStep);
                            #if !defined(RENDERER_IS_THREAD)
                                if (!bResizeInProgress.load()) {
                                    renderer->RenderFrame();
                                }
                            #endif

                            // ADJUST Later: Have the AI Monitor Keyboard interaction.
                            if (gamingAI.IsMonitoring()) {
                                gamingAI.CollectInputEventData(INPUT_TYPE_KEYBOARD, static_cast<uint32_t>(wParam)); // For keyboard
                            }

                            break;
                    }
            } // End of switch (wParam)

            return 0;
        } // End of case WM_KEYDOWN:

        default:
            return DefWindowProc(hwnd, uMsg, wParam, lParam);

    } // End of switch (uMsg)
}

void SwitchToGamePlay()
{
    scene.SetGotoScene(SCENE_GAMEPLAY);
    scene.InitiateScene();
    scene.SetGotoScene(SCENE_NONE);
    // Restart the Loader Thread to load in required assets.
    renderer->ResumeLoader();
}

void OpenMovieAndPlay()
{
    auto fileName = baseDir + L"\\Assets\\test1.mp4";
    if (!moviePlayer.OpenMovie(fileName))
    {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to open Video File for Playback!");
        SwitchToGameIntro();
        return;
    }

    // Start the Movie Playback
    moviePlayer.Play();
}

void SwitchToMovieIntro()
{
    // Add TTS announcement for movie intro
    if (config.myConfig.UseTTS)
    {
        if (ttsManager.GetPlaybackState() != TTSPlaybackState::STATE_ERROR) {
            ttsManager.SetSpeakerChannel(TTSSpeakerChannel::CHANNEL_BOTH);
            ttsManager.SetVoiceVolume(config.myConfig.TTSVolume);
            ttsManager.PlayAsync(L"Attempting to Play Game Introduction Movie");
        }
    }

    scene.SetGotoScene(SCENE_INTRO_MOVIE);
    scene.InitiateScene();
    scene.SetGotoScene(SCENE_NONE);
    fxManager.FadeToImage(3.0f, 0.06f);
    OpenMovieAndPlay();
    // Restart the Loader Thread to load in required assets.
    renderer->ResumeLoader();

}

void SwitchToGameIntro()
{
    // Add TTS announcement for movie intro
    if (config.myConfig.UseTTS)
    {
        if (ttsManager.GetPlaybackState() != TTSPlaybackState::STATE_ERROR) {
            ttsManager.SetSpeakerChannel(TTSSpeakerChannel::CHANNEL_BOTH);
            ttsManager.SetVoiceVolume(config.myConfig.TTSVolume);
            ttsManager.PlayAsync(L"Welcome to the CPGE Gaming Engine Game Intro Screen");
        }
    }

    scene.SetGotoScene(SCENE_INTRO);
    scene.InitiateScene();
    scene.SetGotoScene(SCENE_NONE);
    // Restart the Loader Thread to load in required assets.
    renderer->ResumeLoader();
}
