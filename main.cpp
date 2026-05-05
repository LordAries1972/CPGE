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

   MAIN POINTS:-

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
#include <memory>

#if defined(PLATFORM_WINDOWS)
    #include <endpointvolume.h>
    #include <mmdeviceapi.h>
#endif

// --------------------------------------------
// Engine Subsystems
// --------------------------------------------
#include "ExceptionHandler.h"
#include "SoundManager.h"
#include "MathPrecalculation.h"
#include "FileIO.h"
#include "Debug.h"
#include "GUIManager.h"
#include "Joystick.h"
#include "Configuration.h"
#include "ThreadManager.h"
#include "WinSystem.h"
#include "KeyboardHandler.h"

// Are we using Networking features?
#if defined(__USE_NETWORKING__)
    #include "NetworkManager.h"
#endif

#include "PUNPack.h"
#include "GamePlayer.h"
#include "GamingAI.h"
#include "MyRandomizer.h"

#if defined(PLATFORM_WINDOWS)
    #include "TTSManager.h"
#endif

#include "Renderer.h"
#if defined(PLATFORM_WINDOWS)
    #if defined(__USE_DIRECTX_11__)
        #include "DX11Renderer.h"
    #elif defined(__USE_DIRECTX_12__)
        #include "DX12Renderer.h"
    #elif defined(__USE_VULKAN__)
        #include "VulkanRenderer.h"
    #elif defined(__USE_OPENGL__)
        #include "OpenGLRenderer.h"
    #else
        #error No rendering backend defined for Windows platform!
    #endif
#else
    #if defined(PLATFORM_LINUX || PLATFORM_ANDROID)
        #if defined(__USE_VULKAN__)
            #include "VulkanRenderer.h" // Vulkan is the primary choice for Linux
        #elif defined(__USE_OPENGL__)
            #include "OpenGLRenderer.h" // OpenGL as a fallback for Linux
        #else
            #error No rendering backend defined for Linux platform!
        #endif
    #else
        #if defined(PLATFORM_MACOS || PLATFORM_IOS)
            #include "OpenGLRenderer.h" // OpenGL as a fallback for Linux
        #else
            #error Unsupported Rendering platform!
        #endif
    #endif
#endif

// --------------------------------------------
// Include these after the Renderer's includes
// --------------------------------------------
#include "DX_FXManager.h"
#include "SceneManager.h"
#include "ShaderManager.h"
#include "Models.h"
#include "Lights.h"
#include "MoviePlayer.h"
#include "ScreenRecorder.h"

//------------------------------------------
// Platform Configuration Macros
//------------------------------------------
#if defined(__USE_MP3PLAYER__)
    #include "WinMediaPlayer.h"
#elif defined(__USE_XMPLAYER__)
    #include "XMMODPlayer.h"
#endif

//------------------------------------------
// Constants
//------------------------------------------
const LPCWSTR MY_WINDOW_TITLE = L"DirectX 11 Renderer by Daniel J. Hobson of Australia 2023-2026";
const LPCWSTR lpDEFAULT_NAME = L"CPGE_";

//--------------------------------------------------------
// Required Class Instantiations / Declarations
//
// Some classes will reference each other, so we need to
// declare them here to avoid circular dependencies. 
//--------------------------------------------------------
ExceptionHandler exceptionHandler;
Configuration config;
FileIO fileIO;
Debug debug;
SystemUtils sysUtils;
Joystick js;
SoundManager soundManager;
GUIManager guiManager;
FXManager fxManager;
LightsManager lightsManager;
SceneManager scene;
ShaderManager shaderManager;
ThreadManager threadManager;
MoviePlayer moviePlayer;
ScreenRecorder screenRecorder;

PUNPack punPack;
GamePlayer gamePlayer;
PlayerInfo playerInfo[MAX_PLAYERS]; // Player Info Array
GamingAI gamingAI;
MyRandomizer myRandomizer;

// Are we using Networking features?
#if defined(__USE_NETWORKING__)
    NetworkManager networkManager;
#endif

// If we are using Text To Speech, we need to include the TTSManager (Only for Windows)
#if defined(PLATFORM_WINDOWS)
    TTSManager ttsManager;
#endif

#if defined(__USE_MP3PLAYER__)
    MediaPlayer player;
#elif defined(__USE_XMPLAYER__)
    XMMODPlayer xmPlayer;
    static std::wstring g_currentMusicFile;  // last-loaded module path; used for pattern-0 restart
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
std::atomic<bool> bRecordingToggleRequested{ false };                   // Set by keyboard hook; consumed by main loop
std::atomic<int>  micVolumeAdjustRequest{ 0 };                          // Accumulated NUMPAD+/- steps; consumed by main loop
std::atomic<int>  musicVolumeAdjustRequest{ 0 };                        // Accumulated ALT+NUMPAD+/- steps; consumed by main loop
std::atomic<int>  sfxVolumeAdjustRequest{ 0 };                          // Accumulated CTRL+NUMPAD+/- steps; consumed by main loop
std::atomic<int>  masterVolumeAdjustRequest{ 0 };                       // Accumulated CTRL+SHIFT+NUMPAD+/- steps; consumed by main loop
std::atomic<int>  ttsVolumeAdjustRequest{ 0 };                          // Accumulated CTRL+ALT+NUMPAD+/- steps; consumed by main loop
std::atomic<bool> bDismissAllSettingOSDs{ false };                      // Set by F2 key handler; consumed by main loop to close all volume OSDs

static std::chrono::steady_clock::time_point lastResizeTime;            // Debounce resize messages
static std::chrono::steady_clock::time_point micOSDLastShown;           // When mic volume OSD was last refreshed
static std::chrono::steady_clock::time_point musicOSDLastShown;         // When music volume OSD was last refreshed
static std::chrono::steady_clock::time_point sfxOSDLastShown;           // When SFX volume OSD was last refreshed
static std::chrono::steady_clock::time_point masterOSDLastShown;        // When master volume OSD was last refreshed
static std::chrono::steady_clock::time_point ttsOSDLastShown;           // When TTS volume OSD was last refreshed
static std::chrono::steady_clock::time_point lastScenePhaseChangeTime;  // Suppress WM_SIZE after scene transitions
static const int RESIZE_DEBOUNCE_MS = 100;                              // Minimum time between resize operations
static const int SCENE_PHASE_RESIZE_IGNORE_MS = 15000;                  // Ignore WM_SIZE for 15s after a phase change

int textScrollerEffectID = 0;
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
void StopMusicPlayback();
bool LoadAllShaders();
bool Load_Music();
void SetMyKeyUpHandler(KeyboardHandler& keyboard);

// Supressed Warnings
#pragma warning(push)
#pragma warning(disable: 28251)
#pragma warning(disable: 4996)  // Suppress deprecated function warnings
#pragma warning(disable: 4267)  // Suppress size_t conversion warnings
#pragma warning(disable: 4101)  // Suppress warning C4101: 'e': unreferenced local variable

// *----------------------------------------------------------------------------------------------
// Helpers
// *----------------------------------------------------------------------------------------------
#if defined(PLATFORM_WINDOWS)
// Sets the Windows default audio endpoint master volume. vol64 is 0-64 mapped to 0.0-1.0.
void ApplySystemMasterVolume(int vol64)
{
    float scalar = static_cast<float>(std::clamp(vol64, 0, 64)) / 64.0f;

    IMMDeviceEnumerator* pEnum = nullptr;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                                CLSCTX_ALL, IID_PPV_ARGS(&pEnum)))) return;

    IMMDevice* pDevice = nullptr;
    HRESULT hr = pEnum->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice);
    pEnum->Release();
    if (FAILED(hr)) return;

    IAudioEndpointVolume* pVol = nullptr;
    if (SUCCEEDED(pDevice->Activate(__uuidof(IAudioEndpointVolume),
                                    CLSCTX_ALL, nullptr,
                                    reinterpret_cast<void**>(&pVol))))
    {
        pVol->SetMasterVolumeLevelScalar(scalar, nullptr);
        pVol->Release();
    }
    pDevice->Release();
}
#endif

// *----------------------------------------------------------------------------------------------
// Program Start!
// *----------------------------------------------------------------------------------------------
int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    // Initialize local Buffers.
    SecureZeroMemory(&winMetrics, sizeof(WindowMetrics));
    SecureZeroMemory(&errorMsg, sizeof(errorMsg));

    baseDir = sysUtils.Get_Current_Directory();

    #if defined(PLATFORM_WINDOWS)
        WindowsVersion winVer = sysUtils.GetWindowsVersion();
        if (winVer < WindowsVersion::WINVER_WIN10)
        {
            MessageBox(nullptr, L"Unsupported Windows Version.\nPlease use Windows 10 SP1 64Bit or later.", L"Error", MB_OK | MB_ICONERROR);
            return EXIT_FAILURE;
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
        return EXIT_FAILURE;
    }

    // Initialize the exception handler for comprehensive error tracking
    if (!exceptionHandler.Initialize()) {
        debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"Failed to initialize exception handler - Aborting!");
        UnregisterClass(lpDEFAULT_NAME, hInstance);
        return EXIT_FAILURE;
    }

    exceptionHandler.RecordFunctionCall("WinMain");

    sysUtils.CenterSystemWindow(hwnd);
    ShowWindow(hwnd, nCmdShow);

    try
    {
        if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) {
            debug.LogError("[SYSTEM]: Failed to initialize COM.");
            UnregisterClass(lpDEFAULT_NAME, hInstance);
            return EXIT_FAILURE;
        }

        if (FAILED(MFStartup(MF_VERSION))) {
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"[SYSTEM]: Media Foundation startup failed – screen recording unavailable.");
        }

        // Load in our Configuration file.
        config.loadConfig();

        // Initialise our Randomizer system.
        if (!myRandomizer.Initialize()) {
            debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"Randomizer initialization has failed - Aborting!");
            UnregisterClass(lpDEFAULT_NAME, hInstance);
            return EXIT_FAILURE;
        }

        // Initialise our Randomizer system.
        if (!fileIO.Initialize()) {
            debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"FileIO initialization has failed - Aborting!");
            UnregisterClass(lpDEFAULT_NAME, hInstance);
            return EXIT_FAILURE;
        }

        // Start the FileIO Thread Handler.
        if (!fileIO.StartFileIOThread()) {
            debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"[DEMO] Failed to start FileIO thread");

            UnregisterClass(lpDEFAULT_NAME, hInstance);
            return false;
        }

        // Initialise our Sound Manager
        if (!soundManager.Initialize(hwnd)) {
            debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"Sound system initialization or loading failed.");

            UnregisterClass(lpDEFAULT_NAME, hInstance);
            return EXIT_FAILURE;
        }
        soundManager.LoadAllSFX();

        // Restore audio levels that were saved in the config
        soundManager.SetGlobalVolume(static_cast<float>(config.myConfig.dialogVolume) / 64.0f);
        #if defined(PLATFORM_WINDOWS)
            ApplySystemMasterVolume(config.myConfig.masterVolume);
        #endif

        if (!FAST_MATH.Initialize())
        {
            #if defined(_DEBUG_MATHPRECALC_)
                debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"[Initialization] Failed to initialize MATHPrecalc!");
            #endif

            UnregisterClass(lpDEFAULT_NAME, hInstance);
            return EXIT_FAILURE;
        }

        // Initialize our Packer / UNPacker class
        if (!punPack.Initialize())
        {
            #if defined(_DEBUG_PUNPACK_)
                debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"[Initialization] Failed to initialize PUNPack!");
            #endif

            UnregisterClass(lpDEFAULT_NAME, hInstance);
            return EXIT_FAILURE;
        }

        // Initialize our GamingAI System.
        #if defined(__USE_GAMINGAI__)
            if (!gamingAI.Initialize())
            {
                #if defined(_DEBUG_GAMINGAI_)
                    debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"[Initialization] Failed to initialize GamingAI Management System!");
                #endif

                UnregisterClass(lpDEFAULT_NAME, hInstance);
                return EXIT_FAILURE;
            }
        #endif

        // Obtain Window Metrics
        sysUtils.GetWindowMetrics(hwnd, winMetrics);

        // Initialize the GamePlayer system with basic initialization
        gamePlayer.Initialize();
        PlayerInfo* player = gamePlayer.GetPlayerInfo(PLAYER_1);                                // Get player information

        // Get Singleton KeyboardHandler instance
        KeyboardHandler& keyboard = KeyboardHandler::GetInstance();

        // Configuration optimized for competitive gaming
        KeyboardConfig gamingConfig;
        gamingConfig.enableKeyLogging = false;                                                  // Disable for maximum performance
        gamingConfig.enableHotKeyBlocking = true;                                               // Block distracting OS shortcuts
        gamingConfig.enableKeyRepeat = false;                                                   // Disable repeat for precise control
        gamingConfig.enableMultiKeyDetection = true;                                            // Enable for complex shortcuts
        gamingConfig.maxCombinationKeys = 4;                                                    // Limit to 4 keys for performance

        // Initialize the keyboard handler with the gaming configuration
        if (!keyboard.Initialize(gamingConfig)) {
            #if defined(_DEBUG_KEYBOARDHANDLER_)
                debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"[Initialization] Failed to initialize Keyboard Management System!");
            #endif
        }

        // Setup the Keyboard Handlers
        SetMyKeyUpHandler(keyboard);

        // Now Enable the keyboard system
        if (!keyboard.EnableKeyboardSystem())
        {
            // Handle enable failure
            debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"Failed to enable the Keyboard System!");
            UnregisterClass(lpDEFAULT_NAME, hInstance);
            return EXIT_FAILURE;
        }

        // Now Initialise our Renderer
        renderer->Initialize(hwnd, hInstance);

        // Now Initialise our Shader Manager
        if (!shaderManager.Initialize(renderer))
        {
            #if defined(_DEBUG_SHADERMANAGER_)
                debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"[Initialization] Failed to initialize Shader Management System!");
            #endif

            UnregisterClass(lpDEFAULT_NAME, hInstance);
            return EXIT_FAILURE;
        }

        #if defined(_DEBUG)
            // Enable hot-reloading in debug builds
            shaderManager.EnableHotReloading(true);
            debug.logLevelMessage(LogLevel::LOG_INFO, L"ShaderManager initialized with hot-reloading enabled.");
        #else
            debug.logLevelMessage(LogLevel::LOG_INFO, L"ShaderManager initialized successfully.");
        #endif

        // Load all required shaders using the ShaderManager system
        if (!LoadAllShaders()) {
            debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"[Initialization] Failed to load required shaders!");
            UnregisterClass(lpDEFAULT_NAME, hInstance);
            return EXIT_FAILURE;
        }

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
        #if defined(__USE_NETWORKING__)
            if (!networkManager.Initialize()) {
                debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"Network system initialization failed.");
                return EXIT_FAILURE;
            }
        #endif            

        // Initialise our SceneManager
        scene.Initialize(renderer);
        
        // Force a Scene Jump Switch to the GAMEPLAY scene when in debug mode, 
        // so we can get on with the game testing scene without having to wait 
        // for all the other scenes to load & play through.
//        #if defined(_DEBUG)
//            scene.stSceneType = SceneType::SCENE_GAMEPLAY;
//            scene.SetGotoScene(SceneType::SCENE_NONE);
//        #else
            scene.stSceneType = SceneType::SCENE_INTRO;
            scene.SetGotoScene(SceneType::SCENE_INTRO_MOVIE);
            lastScenePhaseChangeTime = std::chrono::steady_clock::now();
//        #endif

        // Crash dumps only in release builds
        #if !defined(_DEBUG)
            exceptionHandler.SetCrashDumpEnabled(true);
        #endif

        // Apply the display mode selected in the configuration file.
        // Cursor policy:
        //   Windowed    — Windows cursor visible on title bar / borders, hidden inside client area
        //   Borderless  — Windows cursor hidden inside client area, visible outside the window
        //   Full Screen — Windows cursor fully suppressed; engine owns the entire display
        // WM_SETCURSOR in WindowProc enforces the client-area hide for Windowed and Borderless.
        switch (config.myConfig.displayMode)
        {
            case 0:  // Windowed — set flags only; geometry applied after threads start
            {
                winMetrics.isFullScreen = false;
                winMetrics.isBorderless = false;
                break;
            }
            case 1:  // Borderless windowed — strip title bar now; resize applied after threads start
            {
                winMetrics.isFullScreen = false;
                winMetrics.isBorderless = true;
                SetWindowLong(hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
                break;
            }
            default:  // 2 = Full Screen exclusive (and any unrecognised value)
            {
                winMetrics.isFullScreen = true;
                winMetrics.isBorderless = false;
                sysUtils.DisableMouseCursor();
                renderer->SetFullExclusive(config.myConfig.resolutionWidth, config.myConfig.resolutionHeight);
                break;
            }
        }

        // Start renderer and loader threads BEFORE any SetWindowPos call.
        // SetWindowPos with a size change fires WM_SIZE synchronously, which triggers
        // Resize() -> Clean2DTextures() -> m_d2dRenderTarget.Reset() and wipes D2D state
        // before the loader thread has a chance to load the ring texture.  Starting threads
        // first means any subsequent WM_SIZE executes through the designed resize path with
        // the loader already active.
        if (!renderer->StartRendererThreads())
        {
            MessageBox(nullptr, L"Problem Starting Renderer Threads!!!", L"Error", MB_OK | MB_ICONERROR);
            PostQuitMessage(0);
            return EXIT_FAILURE;
        }

        // Apply window geometry now that threads are running.
        if (winMetrics.isBorderless)
        {
            int monW = GetSystemMetrics(SM_CXSCREEN);
            int monH = GetSystemMetrics(SM_CYSCREEN);
            SetWindowPos(hwnd, HWND_TOP, 0, 0, monW, monH, SWP_FRAMECHANGED);
        }
        else if (!winMetrics.isFullScreen)
        {
             RECT rc = { 0, 0, config.myConfig.resolutionWidth, config.myConfig.resolutionHeight };
            AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
            SetWindowPos(hwnd, nullptr, 0, 0, rc.right - rc.left, rc.bottom - rc.top,
                         SWP_NOMOVE | SWP_NOZORDER | SWP_FRAMECHANGED);
            sysUtils.CenterSystemWindow(hwnd);
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

        // Apply saved microphone monitor volume from configuration
        screenRecorder.SetMicMonitorGain(static_cast<float>(config.myConfig.microphoneVolume));

        // Whenever the config window saves, immediately push new values to all live subsystems.
        config.setOnApplyCallback([](const MyConfig& cfg) {
            soundManager.SetGlobalVolume(static_cast<float>(cfg.dialogVolume) / 64.0f);
            #if defined(PLATFORM_WINDOWS)
                ApplySystemMasterVolume(cfg.masterVolume);
            #endif
            #if defined(__USE_XMPLAYER__)
                if (cfg.playMusic) {
                    if (xmPlayer.IsPaused()) {
                        // Restart from pattern 0 with correct volume.
                        // Stop()+Play() is blocking so run it off the main thread.
                        uint8_t vol = static_cast<uint8_t>(std::clamp(cfg.musicVolume, 0, 64));
                        std::wstring musicFile = g_currentMusicFile;
                        std::thread([vol, musicFile]() {
                            xmPlayer.Stop();
                            if (!musicFile.empty() && xmPlayer.Play(musicFile))
                                xmPlayer.SetVolume(vol);
                        }).detach();
                    } else if (xmPlayer.IsPlaying()) {
                        xmPlayer.SetVolume(static_cast<uint8_t>(std::clamp(cfg.musicVolume, 0, 64)));
                    }
                } else {
                    if (xmPlayer.IsPlaying() && !xmPlayer.IsPaused())
                        xmPlayer.Pause();
                }
            #endif
            screenRecorder.SetMicMonitorGain(static_cast<float>(cfg.microphoneVolume));
            #if defined(_WIN64) || defined(_WIN32)
                if (cfg.UseTTS && ttsManager.GetPlaybackState() != TTSPlaybackState::STATE_ERROR)
                    ttsManager.SetVoiceVolume(static_cast<float>(cfg.TTSVolume));
            #endif
        });

        // Add TTS announcement for Startup Splash Screen
        if (config.myConfig.UseTTS)
        {
            if (ttsManager.GetPlaybackState() != TTSPlaybackState::STATE_ERROR) {
                ttsManager.SetSpeakerChannel(TTSSpeakerChannel::CHANNEL_BOTH);
                ttsManager.SetVoiceVolume(config.myConfig.TTSVolume);
                ttsManager.PlayAsync(L"This Game Production uses the Cross Platform Gaming Engine by Daniel J. Hobson of Australia 2025.");
            }
        }

        //  --------------------------------------------------------------------------
        //  --- Main Loop ---
        //  --------------------------------------------------------------------------
        MSG msg = {};
		sysUtils.StartTimer();                                  // Start the system timer
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
                // Update key states for edge detection (call once per frame)
                keyboard.UpdateKeyStates();

                switch (scene.stSceneType)
                {
                    // Our Starting / CPGE Splash Screen (Need to create a linking library for this section.
                    case SceneType::SCENE_INTRO:
                    {
                        try {
                            // Check if splash screen duration has elapsed and we haven't started scene switching yet
                            if ((sysUtils.CheckElapsedTime(7)) && (!scene.bSceneSwitching))
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
                                    auto dx11 = std::dynamic_pointer_cast<DX11Renderer>(renderer);
                                    if (!dx11) {
                                        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[SCENE] DX11 renderer is null during fade");
                                        return 0;
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
                                            auto dx11 = std::dynamic_pointer_cast<DX11Renderer>(renderer);
                                            if (!dx11) {
                                                debug.logLevelMessage(LogLevel::LOG_ERROR, L"[SCENE] DX11 renderer is null during fade in");
                                                return 0;
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
                            debug.logLevelMessage(LogLevel::LOG_ERROR, L"[SCENE] Exception in SCENE_GAMETITLE: " +
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
                        if (movieHasStarted && framesSinceMovieStart > 10 && (!moviePlayer.IsPlaying()) && (!scene.bSceneSwitching))
                        {
                            #if defined(_DEBUG_SCENE_TRANSITION_)
                                debug.logLevelMessage(LogLevel::LOG_INFO, L"[SCENE] Movie finished playing, starting scene transition");
                            #endif

                            scene.bSceneSwitching = true;
                            fxManager.FadeToBlack(1.0f, 0.06f);

                            #if !defined(RENDERER_IS_THREAD) && defined(__USE_DIRECTX_11__)
                                auto dx11 = std::dynamic_pointer_cast<DX11Renderer>(renderer);
                                while (fxManager.IsFadeActive())
                                {
                                    dx11->RenderFrame();
                                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                                }
                            #endif      
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

                    // Game Title / Main Menu screen
                    case SceneType::SCENE_GAMETITLE:
                    {
                        // Guard: do not process GUI input if the config window is not yet open
                        // and there are no visible GUI windows to interact with.
                        guiManager.HandleAllInput(myMouseCoords, isLeftClicked);
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

                // Recording toggle requested by HOME key — runs every scene.
                // Kept outside the scene switch so demonstration videos can be
                // recorded from splash, intro, gameplay, or any other scene.
                // Heavy MF initialisation happens here, not inside the WH_KEYBOARD_LL
                // hook, so the message pump is never blocked.
                if (bRecordingToggleRequested.exchange(false))
                {
                    if (screenRecorder.IsRecording())
                    {
                        screenRecorder.StopRecording();
                        soundManager.PlayImmediateSFX(SFX_ID::SFX_BEEP);
                        debug.logLevelMessage(LogLevel::LOG_INFO, L"[REC] Recording stopped.");
                    }
                    else
                    {
                        // Use the client area, not winMetrics (which includes title bar / borders).
                        // The swap chain back buffer matches the client area exactly.
                        RECT clientRect = {};
                        GetClientRect(hwnd, &clientRect);
                        UINT recWidth  = static_cast<UINT>(clientRect.right  - clientRect.left);
                        UINT recHeight = static_cast<UINT>(clientRect.bottom - clientRect.top);

                        std::wstring outPath = L"Assets\\recording.mp4";
                        if (screenRecorder.StartRecording(recWidth, recHeight, RecordFPS::FPS_60, outPath, MicMode::Mixed))
                        {
                            soundManager.PlayImmediateSFX(SFX_ID::SFX_BEEP);
                            debug.logLevelMessage(LogLevel::LOG_INFO, L"[REC] Recording started -> " + outPath);
                        }
                    }
                }

                // Close every volume OSD window (used when F2 debug OSD takes over)
                auto dismissAllVolOSDs = [&]() {
                    for (const char* id : {"mic_vol_osd", "music_vol_osd", "sfx_vol_osd", "master_vol_osd", "tts_vol_osd"})
                        if (guiManager.GetWindow(id)) guiManager.RemoveWindow(id);
                };
                // Close every OTHER volume OSD and the debug OSD (used when a volume OSD takes over)
                auto dismissOtherVolOSDs = [&](const std::string& keepID) {
                    renderer->bDebugOSDActive = false;
                    for (const char* id : {"mic_vol_osd", "music_vol_osd", "sfx_vol_osd", "master_vol_osd", "tts_vol_osd"})
                        if (std::string(id) != keepID && guiManager.GetWindow(id)) guiManager.RemoveWindow(id);
                };
                // Consume the F2 signal — close any open volume OSDs before the debug OSD appears
                if (bDismissAllSettingOSDs.exchange(false))
                    dismissAllVolOSDs();

                // Mic monitor volume — NUMPAD+ raises, NUMPAD- lowers, only while recording.
                // OSD sits bottom-centre and is removed automatically after 10 seconds.
                {
                    const std::string MIC_OSD     = "mic_vol_osd";
                    const float       OSD_W        = 380.0f;
                    const float       OSD_H        = 80.0f;
                    const float       OSD_MARGIN_B = 30.0f;   // gap above bottom edge

                    int adj = micVolumeAdjustRequest.exchange(0);
                    if (adj != 0 && screenRecorder.IsRecording())
                    {
                        dismissOtherVolOSDs(MIC_OSD);
                        float gain = screenRecorder.GetMicMonitorGain() + adj * 0.1f;
                        gain = gain < 0.0f ? 0.0f : (gain > 4.0f ? 4.0f : gain);
                        screenRecorder.SetMicMonitorGain(gain);
                        config.myConfig.microphoneVolume = gain;

                        if (!guiManager.GetWindow(MIC_OSD))
                        {
                            // Centre horizontally, sit near the bottom of the client area
                            float posX = (winMetrics.width  - OSD_W) * 0.5f;
                            float posY =  winMetrics.height - OSD_H - OSD_MARGIN_B;

                            guiManager.CreateMyWindow(MIC_OSD, GUIWindowType::Standard,
                                Vector2(posX, posY), Vector2(OSD_W, OSD_H),
                                MyColor(15, 15, 15, 220), -1);

                            if (auto w = guiManager.GetWindow(MIC_OSD))
                            {
                                // Title row
                                GUIControl title;
                                title.id          = "mic_vol_title";
                                title.type        = GUIControlType::TextArea;
                                title.position    = Vector2(posX + 12.0f, posY + 8.0f);
                                title.size        = Vector2(OSD_W - 24.0f, 24.0f);
                                title.lblFontSize = 11.0f;
                                title.txtColor    = MyColor(255, 200, 60, 255);
                                title.bgColor     = MyColor(0, 0, 0, 0);
                                title.label       = L"  \u25CF  Mic Monitor Settings";
                                w->AddControl(title);

                                // Value row
                                GUIControl value;
                                value.id          = "mic_vol_value";
                                value.type        = GUIControlType::TextArea;
                                value.position    = Vector2(posX + 12.0f, posY + 40.0f);
                                value.size        = Vector2(OSD_W - 24.0f, 28.0f);
                                value.lblFontSize = 14.0f;
                                value.txtColor    = MyColor(255, 255, 255, 255);
                                value.bgColor     = MyColor(0, 0, 0, 0);
                                w->AddControl(value);
                            }
                        }

                        // Update value label (index 1) with current percentage
                        if (auto w = guiManager.GetWindow(MIC_OSD); w && w->controls.size() >= 2)
                        {
                            int pct = static_cast<int>(screenRecorder.GetMicMonitorGain() * 100.0f + 0.5f);
                            w->controls[1].label =
                                L"  Monitor Volume:  " + std::to_wstring(pct) + L"%"
                                + (pct == 0   ? L"  (muted)" :
                                   pct >= 300 ? L"  (max)"    :
                                   pct >= 200 ? L"  (boosted)" : L"");
                        }
                        micOSDLastShown = std::chrono::steady_clock::now();
                    }

                    // Auto-remove OSD 10 seconds after the last adjustment
                    if (guiManager.GetWindow(MIC_OSD))
                    {
                        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                            std::chrono::steady_clock::now() - micOSDLastShown).count();
                        if (elapsed >= 10)
                            guiManager.RemoveWindow(MIC_OSD);
                    }
                }

                // Music volume — ALT+NUMPAD+ raises, ALT+NUMPAD- lowers. Range: 0-64.
                // OSD sits bottom-centre and is removed automatically after 10 seconds.
                {
                    const std::string MUSIC_OSD    = "music_vol_osd";
                    const float       OSD_W        = 380.0f;
                    const float       OSD_H        = 80.0f;
                    const float       OSD_MARGIN_B = 30.0f;

                    int adj = musicVolumeAdjustRequest.exchange(0);
                    if (adj != 0)
                    {
                        dismissOtherVolOSDs(MUSIC_OSD);
                        int vol = config.myConfig.musicVolume + adj;
                        vol = vol < 0 ? 0 : (vol > 64 ? 64 : vol);
                        config.myConfig.musicVolume = vol;

                        #if defined(__USE_XMPLAYER__)
                            xmPlayer.SetVolume(static_cast<uint8_t>(vol));
                        #elif defined(__USE_MP3PLAYER__)
                            player.setVolume(static_cast<float>(vol) / 64.0f);
                        #endif

                        if (!guiManager.GetWindow(MUSIC_OSD))
                        {
                            float posX = (winMetrics.width  - OSD_W) * 0.5f;
                            float posY =  winMetrics.height - OSD_H - OSD_MARGIN_B;

                            guiManager.CreateMyWindow(MUSIC_OSD, GUIWindowType::Standard,
                                Vector2(posX, posY), Vector2(OSD_W, OSD_H),
                                MyColor(15, 15, 15, 220), -1);

                            if (auto w = guiManager.GetWindow(MUSIC_OSD))
                            {
                                GUIControl title;
                                title.id          = "music_vol_title";
                                title.type        = GUIControlType::TextArea;
                                title.position    = Vector2(posX + 12.0f, posY + 8.0f);
                                title.size        = Vector2(OSD_W - 24.0f, 24.0f);
                                title.lblFontSize = 11.0f;
                                title.txtColor    = MyColor(255, 200, 60, 255);
                                title.bgColor     = MyColor(0, 0, 0, 0);
                                title.label       = L"  ♪  Music Volume";
                                w->AddControl(title);

                                GUIControl value;
                                value.id          = "music_vol_value";
                                value.type        = GUIControlType::TextArea;
                                value.position    = Vector2(posX + 12.0f, posY + 40.0f);
                                value.size        = Vector2(OSD_W - 24.0f, 28.0f);
                                value.lblFontSize = 14.0f;
                                value.txtColor    = MyColor(255, 255, 255, 255);
                                value.bgColor     = MyColor(0, 0, 0, 0);
                                w->AddControl(value);
                            }
                        }

                        if (auto w = guiManager.GetWindow(MUSIC_OSD); w && w->controls.size() >= 2)
                        {
                            w->controls[1].label =
                                L"  Volume:  " + std::to_wstring(config.myConfig.musicVolume) + L" / 64"
                                + (config.myConfig.musicVolume == 0  ? L"  (muted)" :
                                   config.myConfig.musicVolume == 64 ? L"  (max)"   : L"");
                        }
                        musicOSDLastShown = std::chrono::steady_clock::now();
                    }

                    if (guiManager.GetWindow(MUSIC_OSD))
                    {
                        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                            std::chrono::steady_clock::now() - musicOSDLastShown).count();
                        if (elapsed >= 10)
                            guiManager.RemoveWindow(MUSIC_OSD);
                    }
                }

                // SFX volume — CTRL+NUMPAD+ raises, CTRL+NUMPAD- lowers. Range: 0-64.
                // Maps to soundManager.SetGlobalVolume(); OSD auto-removed after 10 seconds.
                {
                    const std::string SFX_OSD     = "sfx_vol_osd";
                    const float       OSD_W        = 380.0f;
                    const float       OSD_H        = 80.0f;
                    const float       OSD_MARGIN_B = 30.0f;

                    int adj = sfxVolumeAdjustRequest.exchange(0);
                    if (adj != 0)
                    {
                        dismissOtherVolOSDs(SFX_OSD);
                        int vol = config.myConfig.dialogVolume + adj;
                        vol = vol < 0 ? 0 : (vol > 64 ? 64 : vol);
                        config.myConfig.dialogVolume = vol;
                        soundManager.SetGlobalVolume(static_cast<float>(vol) / 64.0f);

                        if (!guiManager.GetWindow(SFX_OSD))
                        {
                            float posX = (winMetrics.width  - OSD_W) * 0.5f;
                            float posY =  winMetrics.height - OSD_H - OSD_MARGIN_B;

                            guiManager.CreateMyWindow(SFX_OSD, GUIWindowType::Standard,
                                Vector2(posX, posY), Vector2(OSD_W, OSD_H),
                                MyColor(15, 15, 15, 220), -1);

                            if (auto w = guiManager.GetWindow(SFX_OSD))
                            {
                                GUIControl title;
                                title.id          = "sfx_vol_title";
                                title.type        = GUIControlType::TextArea;
                                title.position    = Vector2(posX + 12.0f, posY + 8.0f);
                                title.size        = Vector2(OSD_W - 24.0f, 24.0f);
                                title.lblFontSize = 11.0f;
                                title.txtColor    = MyColor(255, 200, 60, 255);
                                title.bgColor     = MyColor(0, 0, 0, 0);
                                title.label       = L"  ▶  SFX Volume";
                                w->AddControl(title);

                                GUIControl value;
                                value.id          = "sfx_vol_value";
                                value.type        = GUIControlType::TextArea;
                                value.position    = Vector2(posX + 12.0f, posY + 40.0f);
                                value.size        = Vector2(OSD_W - 24.0f, 28.0f);
                                value.lblFontSize = 14.0f;
                                value.txtColor    = MyColor(255, 255, 255, 255);
                                value.bgColor     = MyColor(0, 0, 0, 0);
                                w->AddControl(value);
                            }
                        }

                        if (auto w = guiManager.GetWindow(SFX_OSD); w && w->controls.size() >= 2)
                        {
                            w->controls[1].label =
                                L"  Volume:  " + std::to_wstring(config.myConfig.dialogVolume) + L" / 64"
                                + (config.myConfig.dialogVolume == 0  ? L"  (muted)" :
                                   config.myConfig.dialogVolume == 64 ? L"  (max)"   : L"");
                        }
                        sfxOSDLastShown = std::chrono::steady_clock::now();
                    }

                    if (guiManager.GetWindow(SFX_OSD))
                    {
                        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                            std::chrono::steady_clock::now() - sfxOSDLastShown).count();
                        if (elapsed >= 10)
                            guiManager.RemoveWindow(SFX_OSD);
                    }
                }

                // Master volume — CTRL+SHIFT+NUMPAD+ raises, CTRL+SHIFT+NUMPAD- lowers. Range: 0-64.
                // Applies to the Windows default audio endpoint; OSD auto-removed after 10 seconds.
                {
                    const std::string MASTER_OSD  = "master_vol_osd";
                    const float       OSD_W        = 380.0f;
                    const float       OSD_H        = 80.0f;
                    const float       OSD_MARGIN_B = 30.0f;

                    int adj = masterVolumeAdjustRequest.exchange(0);
                    if (adj != 0)
                    {
                        dismissOtherVolOSDs(MASTER_OSD);
                        int vol = config.myConfig.masterVolume + adj;
                        vol = vol < 0 ? 0 : (vol > 64 ? 64 : vol);
                        config.myConfig.masterVolume = vol;
                        #if defined(PLATFORM_WINDOWS)
                            ApplySystemMasterVolume(vol);
                        #endif

                        if (!guiManager.GetWindow(MASTER_OSD))
                        {
                            float posX = (winMetrics.width  - OSD_W) * 0.5f;
                            float posY =  winMetrics.height - OSD_H - OSD_MARGIN_B;

                            guiManager.CreateMyWindow(MASTER_OSD, GUIWindowType::Standard,
                                Vector2(posX, posY), Vector2(OSD_W, OSD_H),
                                MyColor(15, 15, 15, 220), -1);

                            if (auto w = guiManager.GetWindow(MASTER_OSD))
                            {
                                GUIControl title;
                                title.id          = "master_vol_title";
                                title.type        = GUIControlType::TextArea;
                                title.position    = Vector2(posX + 12.0f, posY + 8.0f);
                                title.size        = Vector2(OSD_W - 24.0f, 24.0f);
                                title.lblFontSize = 11.0f;
                                title.txtColor    = MyColor(255, 200, 60, 255);
                                title.bgColor     = MyColor(0, 0, 0, 0);
                                title.label       = L"  ■  Master Volume (System)";
                                w->AddControl(title);

                                GUIControl value;
                                value.id          = "master_vol_value";
                                value.type        = GUIControlType::TextArea;
                                value.position    = Vector2(posX + 12.0f, posY + 40.0f);
                                value.size        = Vector2(OSD_W - 24.0f, 28.0f);
                                value.lblFontSize = 14.0f;
                                value.txtColor    = MyColor(255, 255, 255, 255);
                                value.bgColor     = MyColor(0, 0, 0, 0);
                                w->AddControl(value);
                            }
                        }

                        if (auto w = guiManager.GetWindow(MASTER_OSD); w && w->controls.size() >= 2)
                        {
                            w->controls[1].label =
                                L"  Volume:  " + std::to_wstring(config.myConfig.masterVolume) + L" / 64"
                                + (config.myConfig.masterVolume == 0  ? L"  (muted)" :
                                   config.myConfig.masterVolume == 64 ? L"  (max)"   : L"");
                        }
                        masterOSDLastShown = std::chrono::steady_clock::now();
                    }

                    if (guiManager.GetWindow(MASTER_OSD))
                    {
                        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                            std::chrono::steady_clock::now() - masterOSDLastShown).count();
                        if (elapsed >= 10)
                            guiManager.RemoveWindow(MASTER_OSD);
                    }
                }

                #if defined(PLATFORM_WINDOWS)
                // TTS volume — CTRL+ALT+NUMPAD+ raises, CTRL+ALT+NUMPAD- lowers. Range: 0.0-1.0 (step 0.05).
                // Applied immediately to ttsManager and persisted in config; OSD auto-removed after 10 seconds.
                {
                    const std::string TTS_OSD     = "tts_vol_osd";
                    const float       OSD_W        = 380.0f;
                    const float       OSD_H        = 80.0f;
                    const float       OSD_MARGIN_B = 30.0f;

                    int adj = ttsVolumeAdjustRequest.exchange(0);
                    if (adj != 0)
                    {
                        dismissOtherVolOSDs(TTS_OSD);
                        long double vol = config.myConfig.TTSVolume + adj * 0.05;
                        vol = vol < 0.0 ? 0.0 : (vol > 1.0 ? 1.0 : vol);
                        config.myConfig.TTSVolume = vol;
                        ttsManager.SetVoiceVolume(static_cast<float>(vol));

                        if (!guiManager.GetWindow(TTS_OSD))
                        {
                            float posX = (winMetrics.width  - OSD_W) * 0.5f;
                            float posY =  winMetrics.height - OSD_H - OSD_MARGIN_B;

                            guiManager.CreateMyWindow(TTS_OSD, GUIWindowType::Standard,
                                Vector2(posX, posY), Vector2(OSD_W, OSD_H),
                                MyColor(15, 15, 15, 220), -1);

                            if (auto w = guiManager.GetWindow(TTS_OSD))
                            {
                                GUIControl title;
                                title.id          = "tts_vol_title";
                                title.type        = GUIControlType::TextArea;
                                title.position    = Vector2(posX + 12.0f, posY + 8.0f);
                                title.size        = Vector2(OSD_W - 24.0f, 24.0f);
                                title.lblFontSize = 11.0f;
                                title.txtColor    = MyColor(255, 200, 60, 255);
                                title.bgColor     = MyColor(0, 0, 0, 0);
                                title.label       = L"  ☺  TTS Volume";
                                w->AddControl(title);

                                GUIControl value;
                                value.id          = "tts_vol_value";
                                value.type        = GUIControlType::TextArea;
                                value.position    = Vector2(posX + 12.0f, posY + 40.0f);
                                value.size        = Vector2(OSD_W - 24.0f, 28.0f);
                                value.lblFontSize = 14.0f;
                                value.txtColor    = MyColor(255, 255, 255, 255);
                                value.bgColor     = MyColor(0, 0, 0, 0);
                                w->AddControl(value);
                            }
                        }

                        if (auto w = guiManager.GetWindow(TTS_OSD); w && w->controls.size() >= 2)
                        {
                            int pct = static_cast<int>(config.myConfig.TTSVolume * 100.0 + 0.5);
                            w->controls[1].label =
                                L"  Volume:  " + std::to_wstring(pct) + L"%"
                                + (pct == 0   ? L"  (muted)" :
                                   pct == 100 ? L"  (max)"   : L"");
                        }
                        ttsOSDLastShown = std::chrono::steady_clock::now();
                    }

                    if (guiManager.GetWindow(TTS_OSD))
                    {
                        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                            std::chrono::steady_clock::now() - ttsOSDLastShown).count();
                        if (elapsed >= 10)
                            guiManager.RemoveWindow(TTS_OSD);
                    }
                }
                #endif // PLATFORM_WINDOWS

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
        // Log the exception with context
        exceptionHandler.LogException(e, "WinMain");
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

    // Release our Shader Management System.
    shaderManager.CleanUp();

    // Stop screen recorder before the renderer releases the D3D11 device,
    // so the staging texture ComPtr is dropped while the device is still alive.
    screenRecorder.StopRecording();

    // Release the Renderer System.
    renderer->Cleanup();

    renderer->SetWindowedScreen();

    // Force Stop AI monitoring and Clean-Up
    #if defined(__USE_GAMINGAI__)
        gamingAI.EndMonitoring();
        gamingAI.Cleanup();
    #endif

    // Clean up the GamePlayer Manager
    gamePlayer.Cleanup();

    // Clean up Network Manager
    #if defined(__USE_NETWORKING__)
        networkManager.Cleanup();
    #endif

    // Cleanup Cached Base Models
//    for (int i = 0; i < MAX_MODELS; i++)
//        models[i].DestroyModel();

    // Stop and Cleanup the Keyboard Handler system.
    KeyboardHandler& keyboard = KeyboardHandler::GetInstance();
    keyboard.DisableKeyboardSystem();
    keyboard.Cleanup();

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
    const int maxMonitorAttempts = 50;                                  // Monitor for up to 5 seconds

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

	// Finally now remove the Exception Handler.
	exceptionHandler.Cleanup();

    MFShutdown();
    CoUninitialize();
    return EXIT_SUCCESS;
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
        case WM_SETCURSOR:
        {
            // Hide the Windows cursor whenever it is inside our client area so the engine
            // can draw its own.  For windowed mode the non-client area (title bar, resize
            // borders) is left to Windows so the user can still drag / resize the window.
            // For borderless there is no non-client area, so this always fires for HTCLIENT.
            // For fullscreen the cursor was suppressed globally via ShowCursor(FALSE) so this
            // message is irrelevant, but we guard it anyway for correctness.
            if (!winMetrics.isFullScreen && LOWORD(lParam) == HTCLIENT) {
                SetCursor(nullptr);
                return TRUE;
            }
            return DefWindowProc(hwnd, uMsg, wParam, lParam);
        }

        case WM_MOUSEMOVE:
        {
            // Check if we are in fullscreen transition or resize - ignore mouse messages
            if (threadManager.threadVars.bSettingFullScreen.load() ||
                bResizeInProgress.load() || !isSystemInitialized ||
                bFullScreenTransition.load()) {
                return 0;
            }

            GetCursorPos(&cursorPos);                                   // Get current cursor position
            ScreenToClient(hwnd, &cursorPos);                           // Convert to client coordinates
            myMouseCoords.x = static_cast<float>(cursorPos.x);          // Store mouse X coordinate
            myMouseCoords.y = static_cast<float>(cursorPos.y);          // Store mouse Y coordinate

            // Clamp mouse coordinates to window bounds
            if (cursorPos.x >= winMetrics.width) {
                myMouseCoords.x = static_cast<float>(winMetrics.width - 1);
                SetCursorPos(cursorPos.x, cursorPos.y);
            }

            if (cursorPos.y >= winMetrics.height) {
                myMouseCoords.y = static_cast<float>(winMetrics.height - 1);
                SetCursorPos(cursorPos.x, cursorPos.y);
            }

            guiManager.HandleAllInput(myMouseCoords, isLeftClicked);     // Handle GUI input

            switch (scene.stSceneType)
            {
                case SceneType::SCENE_GAMEPLAY:
                {
                    // Handle right mouse button camera control
                    if (isRightClicked && renderer && renderer->bIsInitialized.load())
                    {
                        // Ensure we have valid previous mouse position
                        if (lastMousePos.x == 0 && lastMousePos.y == 0)
                        {
                            lastMousePos = cursorPos;                   // Initialize previous position
                            return 0;                                   // Skip first frame to prevent jitter
                        }

                        // Calculate mouse movement delta
                        int deltaX = cursorPos.x - lastMousePos.x;      // X movement delta
                        int deltaY = cursorPos.y - lastMousePos.y;      // Y movement delta
                        lastMousePos = cursorPos;                       // Update previous position

                        // Get sensitivity from configuration
                        float sensitivity = config.myConfig.moveSensitivity; // Get sensitivity from config

                        // Use new camera method that preserves current view
                        renderer->myCamera.UpdateCameraFromMouseMovement(
                            static_cast<float>(deltaX), 
                            static_cast<float>(deltaY), 
                            sensitivity
                        );

                        #if defined(_DEBUG_CAMERA_) && defined(_DEBUG)
                            debug.logDebugMessage(LogLevel::LOG_DEBUG, 
                                L"[MOUSE] Applied mouse delta - X: %d, Y: %d, Sensitivity: %.3f", 
                                deltaX, deltaY, sensitivity);
                        #endif
                    }
                    break;
                }
            } // End of switch (scene.stSceneType)
            return 0;
        }

        case WM_LBUTTONDOWN:
            // Ignore input during resize or fullscreen transitions
            if (bResizeInProgress.load() || bFullScreenTransition.load()) {
                return 0;
            }

            isLeftClicked = true;
            guiManager.HandleAllInput(myMouseCoords, isLeftClicked);
            return 0;

        case WM_RBUTTONDOWN:
        {
            // Ignore input during resize or fullscreen transitions
            if (bResizeInProgress.load() || bFullScreenTransition.load()) {
                return 0;
            }

            isRightClicked = true;
            GetCursorPos(&lastMousePos);
            ScreenToClient(hwnd, &lastMousePos);
            return 0;
        }

        case WM_LBUTTONUP:
        {
            // Ignore input during resize or fullscreen transitions
            if (bResizeInProgress.load() || bFullScreenTransition.load()) {
                return 0;
            }

            isLeftClicked = false;
            guiManager.HandleAllInput(myMouseCoords, isLeftClicked);
            return 0;
        }

        case WM_RBUTTONUP:
        {
            // Ignore input during resize or fullscreen transitions
            if (bResizeInProgress.load() || bFullScreenTransition.load()) {
                return 0;
            }

            isRightClicked = false;
            guiManager.HandleAllInput(myMouseCoords, isLeftClicked);
            return 0;
        }

        case WM_SIZE:
        {
            // Step 1: Early exit conditions and debouncing
            if (!isSystemInitialized || bResizeInProgress.load()) {
                #if defined(_DEBUG_WINSYSTEM_) && defined(_DEBUG)
                    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[WM_SIZE] Skipping resize - system not ready or already resizing. Init: %d, InProgress: %d",
                        isSystemInitialized, bResizeInProgress.load());
                #endif
                return 0;
            }

            // Suppress spurious WM_SIZE fired during/after scene phase transitions (initial loads, scene switches).
            {
                auto msSincePhaseChange = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - lastScenePhaseChangeTime).count();
                if (msSincePhaseChange < SCENE_PHASE_RESIZE_IGNORE_MS) {
                    #if defined(_DEBUG_WINSYSTEM_) && defined(_DEBUG)
                        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[WM_SIZE] Ignoring resize - only %lldms since last scene phase change (suppressing for %dms)",
                            msSincePhaseChange, SCENE_PHASE_RESIZE_IGNORE_MS);
                    #endif
                    return 0;
                }
            }

            // Block resize while a scene switch is actively in progress.
            if (scene.bSceneSwitching) {
                #if defined(_DEBUG_WINSYSTEM_) && defined(_DEBUG)
                    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[WM_SIZE] Ignoring resize - scene switch in progress");
                #endif
                return 0;
            }

            // Block resize until the loader thread has finished its current scene load.
            if (!threadManager.threadVars.bLoaderTaskFinished.load()) {
                #if defined(_DEBUG_WINSYSTEM_) && defined(_DEBUG)
                    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[WM_SIZE] Ignoring resize - loader task not yet finished");
                #endif
                return 0;
            }

            // Lock out any re-entrant WM_SIZE delivered during DefWindowProc/message pump calls below.
            bResizeInProgress.store(true);

            // Handle minimization state immediately without further processing
            if (wParam == SIZE_MINIMIZED) {
                #if defined(_DEBUG_WINSYSTEM_) && defined(_DEBUG)
                    debug.logLevelMessage(LogLevel::LOG_INFO, L"[WM_SIZE] Window minimized - setting renderer minimized flag");
                #endif
                
                #if defined(__USE_DIRECTX_11__)
                    if (renderer && renderer->bIsInitialized.load()) {
                        // Mark renderer as minimized
                        renderer->bIsMinimized.store(true);
                    }
                #endif
                bResizeInProgress.store(false);
                return 0;
            }

            // Handle restoration from minimized state
            if (wParam == SIZE_RESTORED && renderer && renderer->bIsInitialized.load()) {
                #if defined(_DEBUG_WINSYSTEM_) && defined(_DEBUG)
                    debug.logLevelMessage(LogLevel::LOG_INFO, L"[WM_SIZE] Window restored from minimized state");
                #endif
                
                #if defined(__USE_DIRECTX_11__)
                    renderer->bIsMinimized.store(false);                // Clear minimized flag
                #endif
            }

            // Only process resize for valid size changes when renderer is ready
            if (renderer && renderer->bIsInitialized.load() && 
                !threadManager.threadVars.bIsShuttingDown.load() && 
                !threadManager.threadVars.bSettingFullScreen.load() &&
                !threadManager.threadVars.bIsResizing.load()) {

                // Implement debouncing to prevent rapid resize message flooding
                auto currentTime = std::chrono::steady_clock::now();
                auto timeSinceLastResize = std::chrono::duration_cast<std::chrono::milliseconds>(
                    currentTime - lastResizeTime).count();

                if (timeSinceLastResize < RESIZE_DEBOUNCE_MS) {
                    #if defined(_DEBUG_WINSYSTEM_) && defined(_DEBUG)
                        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[WM_SIZE] Debouncing resize message - only %lld ms since last resize", timeSinceLastResize);
                    #endif
                    bResizeInProgress.store(false);
                    return 0;
                }

                lastResizeTime = currentTime;                           // Update last resize time for debouncing

                // Extract new dimensions from message parameters
                UINT width = LOWORD(lParam);                            // Extract width from low word
                UINT height = HIWORD(lParam);                           // Extract height from high word

                // Validate dimensions are reasonable (prevent zero or invalid sizes)
                if (width < 320 || height < 240 || width > 4096 || height > 4096) {
                    #if defined(_DEBUG_WINSYSTEM_) && defined(_DEBUG)
                        debug.logDebugMessage(LogLevel::LOG_WARNING, L"[WM_SIZE] Invalid resize dimensions: %dx%d - ignoring", width, height);
                    #endif
                    bResizeInProgress.store(false);
                    return 0;
                }

                #if defined(_DEBUG_WINSYSTEM_) && defined(_DEBUG)
                    debug.logDebugMessage(LogLevel::LOG_INFO, L"[WM_SIZE] Beginning controlled resize operation to %dx%d", width, height);
                #endif

                // bResizeInProgress already set above (after the initial guard check)
                threadManager.threadVars.bIsResizing.store(true);       // ThreadManager flag for resize state

                // Use ThreadManager locking system for proper synchronization
                ThreadLockHelper resizeLock(threadManager, "window_resize_operation", 5000);
                if (!resizeLock.IsLocked()) {
                    #if defined(_DEBUG_WINSYSTEM_) && defined(_DEBUG)
                        debug.logLevelMessage(LogLevel::LOG_ERROR, L"[WM_SIZE] Failed to acquire resize lock - aborting resize operation");
                    #endif
                    bResizeInProgress.store(false);                     // Clear progress flag on lock failure
                    threadManager.threadVars.bIsResizing.store(false);  // Clear ThreadManager resize flag
                    return 0;
                }

                try {
                    // Wait for renderer to finish current operations and pause thread
                    #if defined(_DEBUG_WINSYSTEM_) && defined(_DEBUG)
                        debug.logLevelMessage(LogLevel::LOG_INFO, L"[WM_SIZE] Step 2: Waiting for renderer completion and pausing thread");
                    #endif

                    // Wait for RenderFrame() & GPU to complete, then to safely pause renderer thread
                    renderer->WaitToFinishThenPauseThread();

                    // Pause the loader thread and wait for it to stop before resize
                    threadManager.PauseThread(THREAD_LOADER);
                    {
                        int loaderWait = 0;
                        while (threadManager.GetThreadStatus(THREAD_LOADER) == ThreadStatus::Running && loaderWait < 200)
                        {
                            Sleep(10);
                            ++loaderWait;
                        }
                    }

                    // Free all resources required for DirectX resize process
                    #if defined(_DEBUG_WINSYSTEM_) && defined(_DEBUG)
                        debug.logLevelMessage(LogLevel::LOG_INFO, L"[WM_SIZE] Step 3: Freeing DirectX resources for resize");
                    #endif

                    // Save camera state BEFORE any resize operations
                    renderer->myCamera.SaveCameraStateForResize();

                    // Stop all FX effects before resize to prevent rendering conflicts
                    fxManager.StopAllFXForResize();

                    // Handle scene-specific resize operations
                    switch (scene.stSceneType)
                    {
                        case SceneType::SCENE_INTRO:
                        case SceneType::SCENE_GAMEPLAY:
                        case SceneType::SCENE_GAMETITLE:
                        case SceneType::SCENE_INTRO_MOVIE:
                        {
                            #if defined(_DEBUG_WINSYSTEM_) && defined(_DEBUG)
                                debug.logDebugMessage(LogLevel::LOG_INFO, L"[WM_SIZE] Processing resize for scene type: %d", static_cast<int>(scene.stSceneType));
                            #endif

                            // Clear minimized flag for valid resize operations
                            #if defined(__USE_DIRECTX_11__)
                                renderer->bIsMinimized.store(false);     // Clear minimized state
                            #endif

                            // Perform the actual DirectX resize operation with error handling
                            try {
                                // Execute DirectX buffer resize
                                if (!renderer->Resize(width, height))
                                {
                                    // Clear flags on resize failure
                                    threadManager.threadVars.bIsResizing.store(false);
                                    bResizeInProgress.store(false);
                                    #if defined(_DEBUG_WINSYSTEM_) && defined(_DEBUG)
                                        debug.logDebugMessage(LogLevel::LOG_CRITICAL, L"[WM_SIZE] DirectX resize operation failed for %dx%d", width, height);
                                    #endif
                                    
                                    return 0; // Exit early on failure
                                }
                                
                                #if defined(_DEBUG_WINSYSTEM_) && defined(_DEBUG)
                                    debug.logDebugMessage(LogLevel::LOG_INFO, L"[WM_SIZE] DirectX resize operation completed successfully for %dx%d", width, height);
                                #endif

                                // Update window metrics after successful resize
                                sysUtils.GetWindowMetrics(hwnd, winMetrics);

                                #if defined(_DEBUG_WINSYSTEM_) && defined(_DEBUG)
                                    debug.logDebugMessage(LogLevel::LOG_INFO, L"[WM_SIZE] Window metrics updated - new size: %dx%d", winMetrics.width, winMetrics.height);
                                #endif
                            }
                            catch (const std::exception& e) {
                                #if defined(_DEBUG_WINSYSTEM_) && defined(_DEBUG)
                                    debug.logDebugMessage(LogLevel::LOG_CRITICAL, L"[WM_SIZE] DirectX resize operation failed: %hs", e.what());
                                #endif
                                
                                // Clear flags on resize failure
                                threadManager.threadVars.bIsResizing.store(false);
                                bResizeInProgress.store(false);
                                return 0;
                            }

                            // STEP 4: Resume the Renderer Thread
                            #if defined(_DEBUG_WINSYSTEM_) && defined(_DEBUG)
                                debug.logLevelMessage(LogLevel::LOG_INFO, L"[WM_SIZE] Step 4: Resuming renderer thread");
                            #endif

                            threadManager.ResumeThread(THREAD_RENDERER);    // Resume renderer thread for continued operation

                            // STEP 5: Resume the Loader Thread to reload required resources
                            #if defined(_DEBUG_WINSYSTEM_) && defined(_DEBUG)
                                debug.logLevelMessage(LogLevel::LOG_INFO, L"[WM_SIZE] Step 5: Resuming loader thread for resource reload");
                            #endif

                            renderer->ResumeLoader(true);               // Resume loader with resize flag to reload resources

                            // Restore camera state AFTER all resources are recreated
                            renderer->myCamera.RestoreCameraStateAfterResize();

                            break;
                        }

                        default:
                            #if defined(_DEBUG_WINSYSTEM_) && defined(_DEBUG)
                                debug.logDebugMessage(LogLevel::LOG_WARNING, L"[WM_SIZE] Resize attempted in unsupported scene type: %d", static_cast<int>(scene.stSceneType));
                            #endif
                            break;
                    }
                }
                catch (const std::exception& e) {
                    // CRITICAL: Exception handling to prevent crashes
                    #if defined(_DEBUG_WINSYSTEM_) && defined(_DEBUG)
                        debug.logDebugMessage(LogLevel::LOG_CRITICAL, L"[WM_SIZE] Exception during resize operation: %hs", e.what());
                    #endif

                    // Clean up all flags on exception
                    threadManager.threadVars.bIsResizing.store(false);
                }

                // STEP 6: Clear resize flags to indicate completion
                threadManager.threadVars.bIsResizing.store(false);      // Clear ThreadManager resize flag
                bResizeInProgress.store(false);                         // Clear atomic resize progress flag

                #if defined(_DEBUG_WINSYSTEM_) && defined(_DEBUG)
                    debug.logLevelMessage(LogLevel::LOG_INFO, L"[WM_SIZE] Step 6: Resize operation completed successfully - flags cleared");
                #endif
            }
            else {
                #if defined(_DEBUG_WINSYSTEM_) && defined(_DEBUG)
                    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[WM_SIZE] Skipping resize - conditions not met. Renderer: %s, Initialized: %d, Shutting down: %d, Setting fullscreen: %d, Resizing: %d",
                        renderer ? "valid" : "null",
                        renderer ? renderer->bIsInitialized.load() : 0,
                        threadManager.threadVars.bIsShuttingDown.load(),
                        threadManager.threadVars.bSettingFullScreen.load(),
                        threadManager.threadVars.bIsResizing.load());
                #endif
                bResizeInProgress.store(false);
            }

            return 0;
        }

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
                    // Safe camera zoom with proper validation
                    if (renderer && renderer->bIsInitialized.load() && 
                        !threadManager.threadVars.bIsResizing.load())
                    {
                        short delta = GET_WHEEL_DELTA_WPARAM(wParam);    // Get wheel delta
                        const float zoomStep = 1.0f;                     // Zoom sensitivity

                        if (delta > 0)
                        {
                            renderer->myCamera.MoveIn(zoomStep);         // Zoom in
                            #if defined(_DEBUG_CAMERA_) && defined(_DEBUG)
                                debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[CAMERA] Zoom In: delta = %d", delta);
                            #endif
                        }
                        else if (delta < 0)
                        {
                            renderer->myCamera.MoveOut(zoomStep);        // Zoom out
                            #if defined(_DEBUG_CAMERA_) && defined(_DEBUG)
                                debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[CAMERA] Zoom Out: delta = %d", delta);
                            #endif
                        }
                    }
                    return 0;
                }
            }
            return 0;
        }

        case WM_ACTIVATE:
        {
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
        }

        case WM_DESTROY:
        {
            // Handle destroy message
            if (!threadManager.threadVars.bIsShuttingDown.load())
                PostQuitMessage(0);
            return 0;
        }

        default:
            return DefWindowProc(hwnd, uMsg, wParam, lParam);

    } // End of switch (uMsg)
}

void SwitchToGamePlay()
{
	threadManager.PauseThread(THREAD_RENDERER); // Pause the Renderer Thread
    fxManager.StopStarfield();
    fxManager.StopTextScroller(textScrollerEffectID);
    StopMusicPlayback();
    if (config.myConfig.UseTTS)
    {
        if (ttsManager.GetPlaybackState() != TTSPlaybackState::STATE_ERROR) {
            ttsManager.SetSpeakerChannel(TTSSpeakerChannel::CHANNEL_BOTH);
            ttsManager.SetVoiceVolume(config.myConfig.TTSVolume);
            ttsManager.PlayAsync(L"Now loading the Scene into Memory,   Please Stand by!");
        }
    }

    scene.CleanUp();
    scene.SetGotoScene(SCENE_GAMEPLAY);
    scene.InitiateScene();
    scene.SetGotoScene(SCENE_NONE);
    lastScenePhaseChangeTime = std::chrono::steady_clock::now();
    // Reset Camera to default position.
    renderer->myCamera.SetYawPitch(0.0f, 0.0f);

    // Restart the Loader Thread to load in required assets.
    renderer->ResumeLoader();
    // Resume the Renderer Thread
    threadManager.ResumeThread(THREAD_RENDERER);
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
    threadManager.PauseThread(THREAD_RENDERER); // Pause the Renderer Thread
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
    lastScenePhaseChangeTime = std::chrono::steady_clock::now();
    fxManager.FadeToImage(3.0f, 0.06f);
    OpenMovieAndPlay();
    // Restart the Loader Thread to load in required assets.
    renderer->ResumeLoader();
    // Resume the Renderer Thread
    threadManager.ResumeThread(THREAD_RENDERER);
}

void SwitchToGameIntro()
{
    threadManager.PauseThread(THREAD_RENDERER); // Pause the Renderer Thread
    StopMusicPlayback();
    // Add TTS announcement for movie intro
    if (config.myConfig.UseTTS)
    {
        if (ttsManager.GetPlaybackState() != TTSPlaybackState::STATE_ERROR) {
            ttsManager.SetSpeakerChannel(TTSSpeakerChannel::CHANNEL_BOTH);
            ttsManager.SetVoiceVolume(config.myConfig.TTSVolume);
            ttsManager.PlayAsync(L"Loading Game Assets into Memory!");
        }
    }

    scene.CleanUp();
    scene.SetGotoScene(SCENE_GAMETITLE);
    scene.InitiateScene();
    scene.SetGotoScene(SCENE_NONE);
    lastScenePhaseChangeTime = std::chrono::steady_clock::now();
    // Restart the Loader Thread to load in required assets.
    renderer->ResumeLoader();
    // Resume the Renderer Thread
    threadManager.ResumeThread(THREAD_RENDERER);
}

void StopMusicPlayback()
{
    #if defined(__USE_MP3PLAYER__)
        // Stop the MP3 player
        if (player.isPlaying())
            player.stop();
    #elif defined(__USE_XMPLAYER__)
        // Stop the XM player
        if (xmPlayer.IsPlaying())
            xmPlayer.Stop();

        xmPlayer.Shutdown();
    #endif
}

bool Load_Music()
{
    #if defined(__USE_MP3PLAYER__)
        auto fileName = AssetsDir / SingleMP3Filename;
        if (player.loadFile(fileName))
        {
            player.play();
            player.fadeIn(5000);
        }
        else
        {
            threadManager.threadVars.bLoaderTaskFinished.store(true);
            debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"[LOADER]: Failed to load Music File.");
            return false;
        }
    #elif defined(__USE_XMPLAYER__)
        // Attempt to load in our XM Music Module for playback.
        switch (scene.stSceneType)
        {
            case SceneType::SCENE_INTRO_MOVIE:
                break;

            case SceneType::SCENE_GAMETITLE:
            {
                std::wstring XMFilename = L"electro3.xm";
                auto fileName = AssetsDir / XMFilename;
                xmPlayer.Initialize(fileName);
                if (!xmPlayer.Play(fileName))
                {
                    threadManager.threadVars.bLoaderTaskFinished.store(true);
                    debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"[LOADER]: Failed to Play the requested Module file.");
                    return false;
                }
                g_currentMusicFile = fileName.wstring();
                if (!config.myConfig.playMusic)
                    xmPlayer.Pause();
                break;
            }

            case SceneType::SCENE_GAMEPLAY:
            {
                std::wstring XMFilename = L"thevoid.xm";
                auto fileName = AssetsDir / XMFilename;
                xmPlayer.Initialize(fileName);
                if (!xmPlayer.Play(fileName))
                {
                    threadManager.threadVars.bLoaderTaskFinished.store(true);
                    debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"[LOADER]: Failed to Play the requested Module file.");
                    return false;
                }
                g_currentMusicFile = fileName.wstring();
                if (!config.myConfig.playMusic)
                    xmPlayer.Pause();
                break;
            }
            default:
            {
                break;
            }
        }

    #endif

    return true;
}

#pragma warning(pop)
