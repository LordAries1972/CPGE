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
    #include <initguid.h>                           // MUST be first: defines INITGUID so every DEFINE_PROPERTYKEY call below emits a real symbol definition into this .obj (not just a declaration)
    #include <endpointvolume.h>
    #include <mmdeviceapi.h>
    #include <functiondiscoverykeys_devpkey.h>
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
    #elif defined(__USE_OPENGL__)    
        #include "OpenGLRenderer.h"
    #elif defined(__USE_VULKAN__)
        #include "VULKAN_Renderer.h"
    #else
        #error No rendering backend defined for Windows platform!
    #endif
#else
    #if defined(PLATFORM_LINUX) || defined(PLATFORM_ANDROID)
        #if defined(__USE_VULKAN__)
            #include "VulkanRenderer.h" // Vulkan is the primary choice for Linux
        #elif defined(__USE_OPENGL__)
            #include "OpenGLRenderer.h" // OpenGL as a fallback for Linux
        #else
            #error No rendering backend defined for Linux platform!
        #endif
    #elif defined(PLATFORM_MACOS) || defined(PLATFORM_IOS)
        #include "OpenGLRenderer.h" // OpenGL as a fallback for macOS/iOS
    #else
        #error Unsupported Rendering platform!
    #endif
#endif

// --------------------------------------------
// Include these after the Renderer's includes
// --------------------------------------------
#include "FXManager.h"
#include "SceneManager.h"
#include "ShaderManager.h"
#include "Models.h"
#include "Lights.h"
#include "MoviePlayer.h"
#include "ScreenRecorder.h"
#include "XMLParser.h"
#include "ConsoleWindow.h"

#ifdef __USE_SCRIPT_MANAGER__
    #include "ScriptManager.h"
#endif

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
const LPCWSTR MY_WINDOW_CLASS_NAME = L"CPGE2026_WindowClass";
const LPCWSTR MY_WINDOW_TITLE = L"CPGE by Daniel J. Hobson of Australia 2023-2026";
const LPCWSTR lpDEFAULT_NAME = L"CPGE_";

//--------------------------------------------------------
// Required Class Instantiations / Declarations
//
// Some classes will reference each other, so we need to
// declare them here to avoid circular dependencies. 
//--------------------------------------------------------
ExceptionHandler exceptionHandler;
Configuration config;
ThreadManager threadManager;
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
MoviePlayer moviePlayer;
ScreenRecorder screenRecorder;
// Global XMLParser instance (can also be created locally per operation)
XMLParser xmlParser;
XMLDocument xmlDoc;
PUNPack punPack;
GamePlayer gamePlayer;
PlayerInfo playerInfo[MAX_PLAYERS]; // Player Info Array
GamingAI gamingAI;
MyRandomizer myRandomizer;

// Our externals we require
extern const std::string DIFFICULTY_WINDOW_NAME;
extern const std::string GameMenu_WindowName;
extern const std::string GAMEPLAYTYPES_WINDOW_NAME;
extern const std::string QUIT_CONFIRM_WINDOW_NAME;
extern ConsoleWindow consoleWindow;

#ifdef __USE_SCRIPT_MANAGER__
    ScriptManager scriptManager;
#endif

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

// Fixed (non-resizable) windowed style — no WS_THICKFRAME (resize border), no WS_MAXIMIZEBOX
static constexpr DWORD WS_WINDOWED_FIXED = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;

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
void StartGame();
void SwitchToGameIntro();
void SwitchToMovieIntro();
void OpenStartMovieAndPlay();
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

// Queries all four standard WASAPI endpoints at startup and logs their friendly names and
// hardware form factors. This makes it immediately visible which physical device each audio
// subsystem (SoundManager, ScreenRecorder loopback, mic capture, TTS) will route through,
// and exposes any mismatch between the user's Windows audio settings and what the engine expects.
static void DetectAndLogAudioDevices()
{
    auto GetDeviceName = [](IMMDevice* pDev) -> std::wstring {
        if (!pDev) return L"(none)";
        IPropertyStore* pStore = nullptr;
        if (FAILED(pDev->OpenPropertyStore(STGM_READ, &pStore))) return L"(unknown)";
        PROPVARIANT var;
        PropVariantInit(&var);
        std::wstring name = L"(unknown)";
        if (SUCCEEDED(pStore->GetValue(PKEY_Device_FriendlyName, &var)) && var.vt == VT_LPWSTR)
            name = var.pwszVal;
        PropVariantClear(&var);
        pStore->Release();
        return name;
    };

    auto GetFormFactor = [](IMMDevice* pDev) -> std::wstring {
        if (!pDev) return L"";
        IPropertyStore* pStore = nullptr;
        if (FAILED(pDev->OpenPropertyStore(STGM_READ, &pStore))) return L"";
        PROPVARIANT var;
        PropVariantInit(&var);
        std::wstring ff;
        if (SUCCEEDED(pStore->GetValue(PKEY_AudioEndpoint_FormFactor, &var)) && var.vt == VT_UI4)
        {
            switch (var.uintVal)
            {
            case RemoteNetworkDevice:       ff = L"RemoteNetwork";    break;
            case Speakers:                  ff = L"Speakers";         break;
            case LineLevel:                 ff = L"LineLevel";        break;
            case Headphones:                ff = L"Headphones";       break;
            case Microphone:                ff = L"Microphone";       break;
            case Headset:                   ff = L"Headset";          break;
            case Handset:                   ff = L"Handset";          break;
            case UnknownDigitalPassthrough: ff = L"DigitalPassthru";  break;
            case SPDIF:                     ff = L"SPDIF";            break;
            case DigitalAudioDisplayDevice: ff = L"HDMI/DisplayPort"; break;
            default:                        ff = L"Unknown";          break;
            }
        }
        PropVariantClear(&var);
        pStore->Release();
        return ff;
    };

    IMMDeviceEnumerator* pEnum = nullptr;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                                CLSCTX_ALL, IID_PPV_ARGS(&pEnum))))
    {
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"[AudioDevices]: Device enumerator unavailable.");
        return;
    }

    struct Query { EDataFlow flow; ERole role; const wchar_t* label; };
    const Query queries[] = {
        { eRender,  eConsole,        L"Playback  (Default)"       },
        { eRender,  eCommunications, L"Playback  (Communications)" },
        { eCapture, eConsole,        L"Capture   (Default)"        },
        { eCapture, eCommunications, L"Capture   (Communications)" },
    };

    debug.logLevelMessage(LogLevel::LOG_INFO, L"[AudioDevices] ======== Audio Device Report ========");
    for (const auto& q : queries)
    {
        IMMDevice* pDev = nullptr;
        HRESULT hr = pEnum->GetDefaultAudioEndpoint(q.flow, q.role, &pDev);
        if (SUCCEEDED(hr))
        {
            std::wstring name = GetDeviceName(pDev);
            std::wstring ff   = GetFormFactor(pDev);
            std::wstring msg  = std::wstring(q.label) + L": " + name;
            if (!ff.empty()) msg += L"  [" + ff + L"]";
            debug.logLevelMessage(LogLevel::LOG_INFO, L"[AudioDevices] " + msg);
            pDev->Release();
        }
        else
        {
            debug.logLevelMessage(LogLevel::LOG_INFO,
                std::wstring(L"[AudioDevices] ") + q.label + L": (not available)");
        }
    }
    debug.logLevelMessage(LogLevel::LOG_INFO, L"[AudioDevices] ====================================");
    pEnum->Release();
}

// Sets the Windows default audio endpoint master volume. vol64 is 0-MAX_GLOBAL_VOLUME mapped to 0.0-1.0.
void ApplySystemMasterVolume(int vol64)
{
    float scalar = static_cast<float>(std::clamp(vol64, 0, MAX_GLOBAL_VOLUME)) / 64.0f;

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

// Sets DPI awareness before any window or display-mode API call so Win32 APIs report physical
// pixels.  Without this, on 150-200% scaled monitors, the window/surface/display mode can be
// mismatched: DXGI/OpenGL/Vulkan render at physical resolution while the window is positioned
// in logical pixels, producing the "only top-left quarter visible" symptom.
// Tries DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 (Win10 1703+) first, falls back to V1,
// then to the legacy SetProcessDPIAware for older Windows 10 builds.
static void ConfigureProcessDpiAwareness()
{
    if (SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
        return;
    if (SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE))
        return;
    SetProcessDPIAware();
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

    // Load in our Configuration file.
    config.loadConfig();

    #if defined(PLATFORM_WINDOWS)
        WindowsVersion winVer = sysUtils.GetWindowsVersion();
        if (winVer < WindowsVersion::WINVER_WIN10)
        {
            MessageBox(nullptr, L"Unsupported Windows Version.\nPlease use Windows 10 SP1 64Bit or later.", L"Error", MB_OK | MB_ICONERROR);
            return EXIT_FAILURE;
        }
    #endif

    // Set DPI awareness before any Win32 window creation or display-mode query.
    // Must be called here — before RegisterClassEx / CreateWindowEx / AdjustWindowRect —
    // so every subsequent Win32 call returns physical pixels, matching what DXGI / WGL /
    // Vulkan surface allocation uses.  On a 200% scaled display without this, the window
    // is created at logical (half) dimensions while the swap chain renders at full physical
    // resolution, causing the "top-left quarter visible" fullscreen presentation bug.
    #if defined(PLATFORM_WINDOWS)
        DisableProcessWindowsGhosting();
        ConfigureProcessDpiAwareness();
    #endif

    // Create appropriate Renderer Interface
    if (CreateRendererInstance() != EXIT_SUCCESS)
        return EXIT_FAILURE;

    // Set up our Primary Window Class
    WNDCLASSEX wc = {};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    // CS_OWNDC gives each OpenGL window its own persistent DC, required for WGL context creation
    #if defined(__USE_OPENGL__)
    if (renderer && renderer->RenderType == RendererType::RT_OpenGL)
        wc.style |= CS_OWNDC;
    #endif
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

    // Compute outer window size so the CLIENT area equals the configured resolution.
    // Windowed mode uses WS_WINDOWED_FIXED (no resize border / maximize) so the frame
    // is slightly thinner than WS_OVERLAPPEDWINDOW; AdjustWindowRect accounts for this.
    // Borderless and fullscreen override the style after creation so we use the fixed
    // style here — the override makes the initial style irrelevant for those modes.
    int wndOuterW = config.myConfig.resolutionWidth;
    int wndOuterH = config.myConfig.resolutionHeight;
    #if defined(PLATFORM_WINDOWS)
    {
        RECT wndRect = { 0, 0, config.myConfig.resolutionWidth, config.myConfig.resolutionHeight };
        AdjustWindowRect(&wndRect, WS_WINDOWED_FIXED, FALSE);
        wndOuterW = wndRect.right  - wndRect.left;
        wndOuterH = wndRect.bottom - wndRect.top;
    }
    #endif

    // Attempt to create our primary master window for our game!
    hwnd = CreateWindowEx(0, lpDEFAULT_NAME, MY_WINDOW_TITLE, WS_WINDOWED_FIXED,
        CW_USEDEFAULT, CW_USEDEFAULT, wndOuterW, wndOuterH,
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

        // Detect and log every active audio endpoint so it is immediately clear which
        // physical device (headphones, speakers, headset mic, built-in mic, etc.) each
        // audio subsystem will route through. Check the debug log if audio goes to the
        // wrong output — the mismatch will be visible here.
        #if defined(PLATFORM_WINDOWS)
            DetectAndLogAudioDevices();
        #endif

        // Query the system logical processor count and log the recommended engine thread
        // layout.  This MUST be called before any SetThread() invocation so that:
        //   (a) the LP count is confirmed and logged as startup diagnostic info,
        //   (b) SetThread() can safely call PreferCore() knowing available LP indices.
        // All renderer, loader, FileIO, AI, and network threads are started after this point.
        #if defined(PLATFORM_WINDOWS) && defined(_DEBUG)
            threadManager.InitialiseThreadAffinity();
        #endif

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
            debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"Failed to start the FileIO thread");

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

        // GetWindowMetrics reads the just-created windowed window, so in fullscreen exclusive
        // mode it returns the windowed size (e.g. 800x600) rather than the target resolution.
        // Override the client dimensions from config immediately so every subsystem initialised
        // before SetFullExclusive() — including renderer->Initialize() — sees the correct target.
        // SetFullExclusive() will re-confirm these from the actual back-buffer and update again.
        #if defined(PLATFORM_WINDOWS)
            if (config.myConfig.displayMode == 2)
            {
                winMetrics.width         = config.myConfig.resolutionWidth;
                winMetrics.height        = config.myConfig.resolutionHeight;
                winMetrics.clientWidth   = config.myConfig.resolutionWidth;
                winMetrics.clientHeight  = config.myConfig.resolutionHeight;
                winMetrics.borderWidth   = 0;
                winMetrics.titleBarHeight = 0;
            }
        #endif

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

        // Apply the display mode from config.
        // SetDisplayMode() reads displayMode, resolutionWidth, resolutionHeight, and refreshRate
        // and applies the correct window and swap chain state:
        //   0 = Windowed            — normal window, restores swap chain to config resolution
        //   1 = BorderlessFullscreen — WS_POPUP + SetWindowPos to full monitor, swap chain stays windowed
        //   2 = ExclusiveFullscreen  — DXGI ResizeTarget + SetFullscreenState(TRUE) sequence
        // winMetrics.isFullScreen / isBorderless are written inside SetDisplayMode.
        // Cursor policy:
        //   Windowed / Borderless — OS cursor visible on title bar or outside the window;
        //                           hidden inside the client area by WM_SETCURSOR in WindowProc.
        //   Exclusive fullscreen  — OS cursor fully suppressed; engine renders its own cursor.
        if (config.myConfig.displayMode == 2)
            sysUtils.DisableMouseCursor();
        renderer->SetDisplayMode();

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

        // Integrate the console window into GUIManager so it participates in the
        // z-order / exclusive-focus system alongside all other GUI windows.
        consoleWindow.CreateInGUIManager(guiManager);

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

        // Load the models geometry cache if present; avoids a full GLTF/GLB re-parse on startup.
        scene.LoadCache(MODELS_CACHE_FILENAME);

        #ifdef __USE_SCRIPT_MANAGER__
            scriptManager.Initialize(&fxManager, &threadManager, &soundManager, &guiManager, &scene, &gamePlayer, nullptr, &js, &renderer->myCamera);

            // Route console command bar input to ScriptManager
            consoleWindow.SetCommandCallback([](const std::wstring& cmd) {
                std::string narrowCmd(cmd.begin(), cmd.end());
                scriptManager.ExecuteCommandLine(narrowCmd);
            });

            scriptManager.LoadSceneScript(SceneType::SCENE_INITIALISE);
            scriptManager.ExecuteScriptAsync();
        #endif

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
            AdjustWindowRect(&rc, WS_WINDOWED_FIXED, FALSE);
            SetWindowPos(hwnd, nullptr, 0, 0, rc.right - rc.left, rc.bottom - rc.top,
                         SWP_NOMOVE | SWP_NOZORDER | SWP_FRAMECHANGED);
            sysUtils.CenterSystemWindow(hwnd);
        }

//        std::wstring alert = L"This is an alert status message.\n\n"
//        L"Congratulations if you're seeing this window!\n"
//        L"It means the system initialized correctly.\n";

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

        // Apply saved microphone volume from configuration (monitor and record stay in sync)
        screenRecorder.SetMicMonitorGain(static_cast<float>(config.myConfig.microphoneVolume));
        screenRecorder.SetMicRecordGain (static_cast<float>(config.myConfig.microphoneVolume));

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
                        uint8_t vol = static_cast<uint8_t>(std::clamp(cfg.musicVolume, 0, MAX_GLOBAL_VOLUME));
                        std::wstring musicFile = g_currentMusicFile;
                        std::thread([vol, musicFile]() {
                            xmPlayer.Stop();
                            if (!musicFile.empty() && xmPlayer.Play(musicFile))
                                xmPlayer.SetVolume(vol);
                        }).detach();
                    } else if (xmPlayer.IsPlaying()) {
                        xmPlayer.SetVolume(static_cast<uint8_t>(std::clamp(cfg.musicVolume, 0, MAX_GLOBAL_VOLUME)));
                    }
                } else {
                    if (xmPlayer.IsPlaying() && !xmPlayer.IsPaused())
                        xmPlayer.Pause();
                }
            #endif
            screenRecorder.SetMicMonitorGain(static_cast<float>(cfg.microphoneVolume));
            screenRecorder.SetMicRecordGain (static_cast<float>(cfg.microphoneVolume));
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
                ttsManager.PlayAsync(L"This system uses the Cross Platform Gaming Engine by Daniel J. Hobson of Australia 2023 to 2026.");
            }
        }

        //  --------------------------------------------------------------------------
        //  --- Main Loop ---
        //  --------------------------------------------------------------------------
        MSG msg = {};
		sysUtils.StartTimer();                                  // Start the system timer
        OpenStartMovieAndPlay();                                // Start the opening movie
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

                // Poll for joystick connect/disconnect every 3 seconds.
                // joyGetPosEx is inexpensive but scanning all slots on every frame
                // is wasteful, so we throttle to once per 3000 ms.
                {
                    static ULONGLONG jsLastPollMs = 0;
                    ULONGLONG nowMs = GetTickCount64();
                    if (nowMs - jsLastPollMs >= 3000ULL) {
                        js.PollControllers();
                        jsLastPollMs = nowMs;
                    }
                }

                // Determine what Scene we are in, and execute code based on that.
                switch (scene.stSceneType)
                {
                    // Our Starting / CPGE Splash Screen / Movie Playback.
                    case SceneType::SCENE_INTRO:
                    {
                        try {
                            // Check if splash screen duration has elapsed and we haven't started scene switching yet
                            if ((sysUtils.CheckElapsedTime(11)) && (!scene.bSceneSwitching))
                            {
                                // The CPGE intro movie is 10 seconds long, so we give it a generous 
                                // 11-second window before forcing the transition to the next scene. 
                                // This ensures that even if there are minor hiccups in loading or playback, 
                                // we won't cut off the intro prematurely. The extra second acts as a buffer to 
                                // accommodate any unexpected delays while still providing a smooth user experience.
                                // OpenMovieAndPlay() now performs a fast per-file resource
                                // release internally, so no Stop()/Reset() is needed here.
                                scene.bSceneSwitching = true;
                                scene.SetGotoScene(SCENE_INTRO_MOVIE);
                                scene.InitiateScene();
                                scene.SetGotoScene(SCENE_NONE);
                                scene.bSceneSwitching = false;  // Prevent main loop from calling SwitchToMovieIntro again
                                OpenMovieAndPlay();
                                fxManager.FadeToImage(0.5f, 0.06f);
                                renderer->ResumeLoader();
                                
                                #if defined(_DEBUG_SCENE_TRANSITION_)
                                    debug.logLevelMessage(LogLevel::LOG_INFO, L"[SCENE] Starting fade out from splash screen");
                                #endif
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
                                break;
                            }
                        }

                        break;
                    }

                    // Game Title / Main Menu screen
                    case SceneType::SCENE_GAMETITLE:
                    {
                        guiManager.HandleAllInput(myMouseCoords, isLeftClicked);

                        #if defined(_DEBUG)
                            // Debug camera: gamepad/joystick drives the camera so the ship
                            // position in world space can be observed (debug builds only).
                            if (js.HasActiveControllers())
                            {
                                // Read deadzone-filtered axes; only move the camera when the
                                // stick is actually deflected so an idle pad never disturbs the view.
                                JoystickAxes axes = js.getNormalizedAxes(PLAYER_1);
                                if (axes.x  != 0.0f || axes.y  != 0.0f || axes.z  != 0.0f ||
                                    axes.rx != 0.0f || axes.ry != 0.0f || axes.rz != 0.0f)
                                {
                                    // Apply movement/rotation to the bound camera (3D mode)
                                    js.processJoystickMovement(PLAYER_1);
                                }
                            }
                        #endif
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

                        // Process joystick input only when at least one controller is active.
                        if (js.HasActiveControllers()) {
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
                        // Query the renderer for precise back-buffer dimensions.
                        // GetClientRect can disagree with the swap-chain on DPI-aware setups:
                        // DXGI/Vulkan may allocate in physical pixels while GetClientRect
                        // returns logical pixels. Using the renderer's reported dimensions
                        // avoids dimension mismatches that produce garbled or truncated video.
                        UINT recWidth = 0, recHeight = 0;
                        {
                            #if defined(__USE_DIRECTX_11__) || defined(__USE_DIRECTX_12__)
                                // DX11/DX12: read directly from the DXGI swap-chain descriptor.
                                IDXGISwapChain1* pSC = static_cast<IDXGISwapChain1*>(renderer->GetSwapChain());
                                DXGI_SWAP_CHAIN_DESC1 scDesc = {};
                                if (pSC && SUCCEEDED(pSC->GetDesc1(&scDesc)))
                                {
                                    recWidth  = scDesc.Width;
                                    recHeight = scDesc.Height;
                                }
                                else
                            #elif defined(__USE_VULKAN__)
                                // Vulkan: read from the VkExtent2D reported by the swapchain.
                                // m_loaderCommandPool race aside, dimensions must match CaptureFrame's
                                // m_swapchainExtent — GetClientRect returns logical pixels on DPI-scaled
                                // systems and can disagree with the physical swapchain extent.
                                if (auto* vr = dynamic_cast<VulkanRenderer*>(renderer.get()))
                                {
                                    VkExtent2D ext = vr->GetSwapchainExtent();
                                    if (ext.width > 0 && ext.height > 0)
                                    {
                                        recWidth  = ext.width;
                                        recHeight = ext.height;
                                    }
                                }
                                if (recWidth == 0 || recHeight == 0)
                            #endif
                            {
                                RECT clientRect = {};
                                GetClientRect(hwnd, &clientRect);
                                recWidth  = static_cast<UINT>(clientRect.right  - clientRect.left);
                                recHeight = static_cast<UINT>(clientRect.bottom - clientRect.top);
                            }
                        }

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
                        gain = gain < 0.0f ? 0.0f : (gain > 20.0f ? 20.0f : gain);
                        screenRecorder.SetMicMonitorGain(gain);
                        screenRecorder.SetMicRecordGain (gain);
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
                                + (pct == 0    ? L"  (muted)"   :
                                   pct >= 1900 ? L"  (max)"     :
                                   pct >= 1000 ? L"  (boosted)" : L"");
                        }
                        micOSDLastShown = std::chrono::steady_clock::now();
                    }

                    // Auto-remove OSD 10 seconds after the last adjustment
                    if (guiManager.GetWindow(MIC_OSD))
                    {
                        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                            std::chrono::steady_clock::now() - micOSDLastShown).count();
                        if (elapsed >= 5)
                            guiManager.RemoveWindow(MIC_OSD);
                    }
                }

                // Music volume — ALT+NUMPAD+ raises, ALT+NUMPAD- lowers. Range: 0-MAX_GLOBAL_VOLUME.
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
                        vol = vol < 0 ? 0 : (vol > MAX_GLOBAL_VOLUME ? MAX_GLOBAL_VOLUME : vol);
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
                                   config.myConfig.musicVolume == MAX_GLOBAL_VOLUME ? L"  (max)"   : L"");
                        }
                        musicOSDLastShown = std::chrono::steady_clock::now();
                    }

                    if (guiManager.GetWindow(MUSIC_OSD))
                    {
                        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                            std::chrono::steady_clock::now() - musicOSDLastShown).count();
                        if (elapsed >= 5)
                            guiManager.RemoveWindow(MUSIC_OSD);
                    }
                }

                // SFX volume — CTRL+NUMPAD+ raises, CTRL+NUMPAD- lowers. Range: 0-${MAX_GLOBAL_VOLUME}.
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
                        vol = vol < 0 ? 0 : (vol > MAX_GLOBAL_VOLUME ? MAX_GLOBAL_VOLUME : vol);
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
                                   config.myConfig.dialogVolume == MAX_GLOBAL_VOLUME ? L"  (max)"   : L"");
                        }
                        sfxOSDLastShown = std::chrono::steady_clock::now();
                    }

                    if (guiManager.GetWindow(SFX_OSD))
                    {
                        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                            std::chrono::steady_clock::now() - sfxOSDLastShown).count();
                        if (elapsed >= 5)
                            guiManager.RemoveWindow(SFX_OSD);
                    }
                }

                // Master volume — CTRL+SHIFT+NUMPAD+ raises, CTRL+SHIFT+NUMPAD- lowers. Range: 0-${MAX_GLOBAL_VOLUME}.
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
                        vol = vol < 0 ? 0 : (vol > MAX_GLOBAL_VOLUME ? MAX_GLOBAL_VOLUME : vol);
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
                                L"  Volume:  " + std::to_wstring(config.myConfig.masterVolume) + L" / " + std::to_wstring(MAX_GLOBAL_VOLUME)
                                + (config.myConfig.masterVolume == 0  ? L"  (muted)" :
                                   config.myConfig.masterVolume == MAX_GLOBAL_VOLUME ? L"  (max)"   : L"");
                        }
                        masterOSDLastShown = std::chrono::steady_clock::now();
                    }

                    if (guiManager.GetWindow(MASTER_OSD))
                    {
                        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                            std::chrono::steady_clock::now() - masterOSDLastShown).count();
                        if (elapsed >= 5)
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
                        if (elapsed >= 5)
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
                #if !defined(RENDERER_IS_THREAD) && defined(__USE_DIRECTX_12__)
                    renderer->RenderFrame();
                #endif

                // --- OpenGL Rendering inline safety calls NON Threaded ---
                #if !defined(RENDERER_IS_THREAD) && defined(__USE_OPENGL__)
                    renderer->RenderFrame();
                #endif

                // --- Vulkan Rendering inline safety calls NON Threaded ---
                #if !defined(RENDERER_IS_THREAD) && defined(__USE_VULKAN__)
                    renderer->RenderFrame();
                #endif
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

    // Save the models geometry cache before releasing scene and model data.
    scene.SaveCache(MODELS_CACHE_FILENAME);

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

            GetCursorPos(&cursorPos);                                    // Get current cursor position
            ScreenToClient(hwnd, &cursorPos);                            // Convert to client coordinates
            myMouseCoords.x = static_cast<float>(cursorPos.x);           // Store mouse X coordinate
            myMouseCoords.y = static_cast<float>(cursorPos.y);           // Store mouse Y coordinate

            // Clamp logical coords to window bounds; do NOT call SetCursorPos here —
            // cursorPos is already in client space after ScreenToClient, and passing
            // client coordinates to SetCursorPos (which expects screen coordinates)
            // causes the cursor to jump to a wrong screen position.
            if (cursorPos.x >= winMetrics.width)
                myMouseCoords.x = static_cast<float>(winMetrics.width - 1);

            if (cursorPos.y >= winMetrics.height)
                myMouseCoords.y = static_cast<float>(winMetrics.height - 1);

            guiManager.HandleAllInput(myMouseCoords, isLeftClicked);

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

        case WM_GETMINMAXINFO:
        {
            // Prevent the window from being resized or maximised in Windowed and Borderless modes.
            // Fullscreen is handled by the OS/exclusive mode; no restriction needed there.
            if (!winMetrics.isFullScreen)
            {
                MINMAXINFO* mmi = reinterpret_cast<MINMAXINFO*>(lParam);

                // Calculate the required outer window size for the configured client resolution.
                RECT rc = { 0, 0, config.myConfig.resolutionWidth, config.myConfig.resolutionHeight };
                if (!winMetrics.isBorderless)
                    AdjustWindowRect(&rc, WS_WINDOWED_FIXED, FALSE);
                const int totalW = rc.right  - rc.left;
                const int totalH = rc.bottom - rc.top;

                // Lock both min and max to the same size to prevent any resize.
                mmi->ptMinTrackSize.x = totalW;
                mmi->ptMinTrackSize.y = totalH;
                mmi->ptMaxTrackSize.x = totalW;
                mmi->ptMaxTrackSize.y = totalH;
                return 0;
            }
            return DefWindowProc(hwnd, uMsg, wParam, lParam);
        }

        case WM_SIZE:
        {
            // User window-resizing is NOT supported — window geometry is managed entirely by
            // SetFullExclusive() / SetWindowedScreen() in the renderer.  WM_SIZE fires as a
            // side-effect of those transitions (SetFullscreenState, SetWindowPos) and also for
            // system events such as minimise/restore.  All those renderer-internal transitions
            // gate themselves with bSettingFullScreen; the only actions we take here are:
            //   a) minimise   → set bIsMinimized so the render thread skips frames
            //   b) restore    → clear bIsMinimized so rendering resumes
            //   c) anything else → sync winMetrics from the renderer's confirmed back-buffer
            //      dimensions (iOrigWidth / iOrigHeight) so the coordinate spaces used by the
            //      cameras, GUI, FX system, and loader thread remain coherent.

            // Block re-entrant processing (e.g. triggered from within DefWindowProc)
            if (bResizeInProgress.load())
                return 0;

            // Suppress any WM_SIZE that fires during an active fullscreen/windowed transition —
            // the renderer is already handling the resize internally at that point.
            if (threadManager.threadVars.bSettingFullScreen.load())
                return 0;

            bResizeInProgress.store(true);

            // --- Minimise ---
            if (wParam == SIZE_MINIMIZED) {
                #if defined(_DEBUG_WINSYSTEM_) && defined(_DEBUG)
                    debug.logLevelMessage(LogLevel::LOG_INFO, L"[WM_SIZE] Window minimised");
                #endif
                if (renderer && renderer->bIsInitialized.load())
                    renderer->bIsMinimized.store(true);
                bResizeInProgress.store(false);
                return 0;
            }

            // --- Restore from minimise ---
            if (renderer && renderer->bIsInitialized.load())
                renderer->bIsMinimized.store(false);

            // --- Sync winMetrics from the renderer's confirmed back-buffer dimensions ---
            // The renderer's SetFullExclusive / SetWindowedScreen / Resize() methods own the
            // actual back-buffer size.  Update winMetrics from those authoritative values so
            // the camera, GUI, FX, and loader thread all share a coherent coordinate space.
            // This replaces the old GetWindowMetrics() call (which could return stale OS values).
            if (isSystemInitialized && renderer && renderer->bIsInitialized.load() &&
                !threadManager.threadVars.bIsShuttingDown.load()) {

                int w = renderer->iOrigWidth;
                int h = renderer->iOrigHeight;
                if (w > 0 && h > 0) {
                    winMetrics.width        = w;
                    winMetrics.height       = h;
                    winMetrics.clientWidth  = w;
                    winMetrics.clientHeight = h;
                    if (winMetrics.isFullScreen) {
                        // In fullscreen exclusive the monitor rect equals the back-buffer rect
                        winMetrics.monitorFullArea = { 0, 0, w, h };
                        winMetrics.monitorWorkArea = { 0, 0, w, h };
                    }
                    #if defined(_DEBUG_WINSYSTEM_) && defined(_DEBUG)
                        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"[WM_SIZE] winMetrics synced to renderer dims %dx%d", w, h);
                    #endif
                }
            }

            bResizeInProgress.store(false);
            return 0;
        }

        case WM_CHAR:
        {
            // Route keyboard characters through GUIManager to the focused window.
            // The console window registers onCharInput; other windows ignore it.
            guiManager.HandleChar(static_cast<wchar_t>(wParam));
            return 0;
        }

        case WM_MOUSEWHEEL:
        {
            // Ignore mouse wheel during resize or fullscreen transitions
            if (bResizeInProgress.load() || bFullScreenTransition.load()) {
                return 0;
            }

            // Route scroll wheel through GUIManager to the focused window (e.g. console scroll).
            guiManager.HandleMouseWheel(GET_WHEEL_DELTA_WPARAM(wParam));

            // Any open GUI window (console, config, dialog …) holds exclusive wheel focus.
            // Absorb the event so backend camera zoom never fires while the UI is active.
            if (guiManager.GetFocusedWindow())
                return 0;

            switch (scene.stSceneType)
            {
                #if defined(_DEBUG)
                case SceneType::SCENE_GAMETITLE:
                #endif
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

        case WM_CLOSE:
        {
            // Mirror the ESC-key shutdown path (KBHandlersCode.cpp): signal all
            // threads to stop before the window is destroyed so the cleanup code
            // in WinMain can join them without deadlocking.  Without bIsShuttingDown,
            // the renderer and loader threads keep running after WM_QUIT causes the
            // main loop to exit, and Cleanup() blocks waiting for them indefinitely.
            StopMusicPlayback();
            threadManager.threadVars.bIsShuttingDown.store(true);
            DestroyWindow(hwnd);
            return 0;
        }

        case WM_DESTROY:
        {
            // Always post WM_QUIT so WinMain's message loop exits regardless of
            // which code path triggered the destroy.  The earlier bIsShuttingDown
            // guard caused a permanent hang when the quit-button callback set
            // bIsShuttingDown=true before PostMessage(WM_CLOSE): WM_DESTROY fired
            // but the guard suppressed PostQuitMessage, so the main loop never saw
            // WM_QUIT and looped forever.  A duplicate WM_QUIT (from the ESC path
            // which also calls PostQuitMessage directly) is harmless — the loop
            // exits on the first one and the second sits unread until process exit.
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

    #ifdef __USE_SCRIPT_MANAGER__
        scriptManager.StopExecution();
    #endif
    scene.CleanUp();
    scene.SetGotoScene(SCENE_GAMEPLAY);
    scene.InitiateScene();
    scene.SetGotoScene(SCENE_NONE);
    lastScenePhaseChangeTime = std::chrono::steady_clock::now();
    #ifdef __USE_SCRIPT_MANAGER__
        scriptManager.LoadSceneScript(SceneType::SCENE_GAMEPLAY);
        scriptManager.ExecuteScriptAsync();
    #endif
    // Reset Camera to default position.
    renderer->myCamera.SetYawPitch(0.0f, 0.0f);

    // Restart the Loader Thread to load in required assets.
    renderer->ResumeLoader();
    // Arm fade-in before resuming so the first rendered frame fires the effect.
    threadManager.threadVars.bInitiateFader.store(true);
    // Resume the Renderer Thread
    threadManager.ResumeThread(THREAD_RENDERER);
}

void OpenMovieAndPlay()
{
    auto fileName = baseDir + L"\\Assets\\test1.mp4";
    if (!moviePlayer.OpenMovie(fileName))
    {
        debug.logDebugMessage(LogLevel::LOG_CRITICAL, L"Failed to open Video File for Playback! [%s]", fileName.c_str());
        SwitchToGameIntro();
        return;
    }

    // Start the Movie Playback
    moviePlayer.Play();
}

void OpenStartMovieAndPlay()
{
    auto fileName = baseDir + L"\\Assets\\cpge.mp4";
    if (!moviePlayer.OpenMovie(fileName))
    {
        debug.logDebugMessage(LogLevel::LOG_CRITICAL, L"Failed to open Video File for Playback! [%s]", fileName.c_str());
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

    #ifdef __USE_SCRIPT_MANAGER__
        scriptManager.StopExecution();
    #endif
    scene.SetGotoScene(SCENE_INTRO_MOVIE);
    scene.InitiateScene();
    scene.SetGotoScene(SCENE_NONE);
    lastScenePhaseChangeTime = std::chrono::steady_clock::now();
    #ifdef __USE_SCRIPT_MANAGER__
        scriptManager.LoadSceneScript(SceneType::SCENE_INTRO_MOVIE);
        scriptManager.ExecuteScriptAsync();
    #endif
    // Open and start the movie before the fade so the first frame is decoded
    // and queued by the time the brief fade-in completes.
    OpenMovieAndPlay();
    fxManager.FadeToImage(0.5f, 0.06f);
    // Restart the Loader Thread to load in required assets.
    renderer->ResumeLoader();
    // Resume the Renderer Thread
    threadManager.ResumeThread(THREAD_RENDERER);
}

void SwitchToGameIntro()
{
    threadManager.PauseThread(THREAD_RENDERER); // Pause the Renderer Thread
    // State that the game is to reset!
    threadManager.threadVars.bHasReset.store(true);
    // Stop Music Playback!
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

    #ifdef __USE_SCRIPT_MANAGER__
        scriptManager.StopExecution();
    #endif
    // Cleanup the current old scene!
    scene.CleanUp();
    // Select our New Scene!
    scene.SetGotoScene(SCENE_GAMETITLE);
    // Set Initialisation for our new Scene
    scene.InitiateScene();
    // No GOTO Scene - User now choses the SCENE of action before deciding!
    scene.SetGotoScene(SCENE_NONE);
    lastScenePhaseChangeTime = std::chrono::steady_clock::now();
    #ifdef __USE_SCRIPT_MANAGER__
        scriptManager.LoadSceneScript(SceneType::SCENE_GAMETITLE);
        scriptManager.ExecuteScriptAsync();
    #endif
    // Restart the Loader Thread to load in required assets.
    renderer->ResumeLoader();
    // Resume Game State
    threadManager.threadVars.bHasReset.store(false);
    // Arm fade-in before resuming so the first rendered frame fires the effect.
    threadManager.threadVars.bInitiateFader.store(true);
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

void StartGame()
{
    // Start Game
    // Initiate fade to black effect with proper timing
    fxManager.FadeToBlack(1.0f, 0.06f);

    // Wait for fade effect to complete with proper timeout to prevent infinite loop
    int fadeTimeout = 0;
    const int MAX_FADE_TIMEOUT = 300; // 3 seconds maximum wait time (300 * 10ms)
    while (fxManager.IsFadeActive() && fadeTimeout < MAX_FADE_TIMEOUT) {
        Sleep(10); // Sleep for 10 milliseconds
        fadeTimeout++;
    }

    // Remove the game menu window safely before application shutdown
    guiManager.RemoveWindow(GameMenu_WindowName);

    // Initialize Player #1 for Game Play.

    // Switch to GamePlay Scene.
    SwitchToGamePlay();
}

#pragma warning(pop)
