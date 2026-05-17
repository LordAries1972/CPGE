/* ---------------------------------------------------------------------------------------------------------
This is your base Includes.h header file for the specified Renderer & other engine sub-systems 
and to only include what is needed on a given platform when requested.

It includes all the necessary headers and libraries for Windows, DirectX, OpenGL, and other platforms.
This is to eliminate the need for multiple includes in your project files.

NOTE:   Becareful to not alter the order of the includes or directive conditional blocks, as some 
        libraries may depend on others being included first.  It also strongly focuses on the
        approach of only including what is needed for the specific platform and renderer being used.

        Be sensible when adding new includes to this file, as it is included in many places and 
        can affect compile times or cause problems, if not managed properly.
        
--------------------------------------------------------------------------------------------------------- */
#pragma once

#define __USE_XMPLAYER__
//#define __USE_MP3PLAYER__
#define __USING_JOYSTICKS__                                                             // Uncomment this line if you want to use Joysticks with this engine.
#define __USE_NETWORKING__                                                              // Uncomment this line if you want to use Networking TCP/UDP Protocols with this engine.
#define __USE_GAMINGAI__                                                                // Uncomment this line if you want to use Gaming AI with this engine.
//#define __USE_SCRIPT_MANAGER__                                                          // Uncomment this line to use the interal Script Manager System.

// -----------------------------------
// Windows Specific Includes
// -----------------------------------
#if defined(_WIN64) || defined(_WIN32)
    #ifndef PLATFORM_WINDOWS
        #define PLATFORM_WINDOWS

        // CRITICAL: Define this before any Windows includes to prevent macro conflicts
        #ifndef NOMINMAX
            #define NOMINMAX                                                            // Prevents Windows.h from defining min and max macros which allows then to std::min, std::max
        #endif

        #ifndef WIN32_LEAN_AND_MEAN
            #define WIN32_LEAN_AND_MEAN
        #endif

        #include <windows.h>
        #include <winerror.h>
        #include <wincodec.h>                                                               // WIC
        #include <memory>
        #include <mfapi.h>
        #include <mfidl.h>
        #include <mfobjects.h>
        #include <mfreadwrite.h>
        #include <mfplay.h>
        #include <wrl/client.h>
        #include <comdef.h>                                                                 // For _com_error

        using Microsoft::WRL::ComPtr;

        #ifdef _DEBUG
            #pragma comment(lib, "dbghelp.lib")
        #endif

        #if defined(__USE_NETWORKING__)
            #include <winsock2.h>                                                   // Main Winsock API
            #include <ws2tcpip.h>                                                   // Additional TCP/IP functions
            #include <iphlpapi.h>                                                   // IP Helper API

            // Required Network Link libraries
            #pragma comment(lib, "ws2_32.lib")                                      // Winsock 2 library
            #pragma comment(lib, "iphlpapi.lib")                                    // IP Helper API library
        #endif // __USE_NETWORKING__
        
        // Windows Platform — enable the renderer(s) you have installed.
        // Only one is needed at minimum; add more as they become available.
        // Runtime selection via config.myConfig.rendererType: 0=DX11  1=DX12  2=OpenGL  3=Vulkan
        #define __USE_DIRECTX_11__
        //#define __USE_DIRECTX_12__
        //#define __USE_OPENGL__
        //#define __USE_VULKAN__

        #if defined(__USE_DIRECTX_11__)
            // DirectX 11 Related includes
            #include <d3d11.h>                      // Core DirectX 11 interface
            #include <d3d11_1.h>                    // DirectX 11.1 extensions
            #include <d3dcompiler.h>                // Shader compilation
            #include <dxgi.h>                       // DirectX Graphics Infrastructure
            #include <dxgi1_2.h>                    // DXGI 1.2 extensions
            #include <DirectXMath.h>                // DirectX Math library
            #include <DirectXColors.h>              // Predefined color constants

            // Audio related includes
            #include <xaudio2.h>                    // XAudio2 for audio processing
            #include <dsound.h>                     // DirectSound for legacy audio support

            // Direct2D and DirectWrite for 2D graphics and text rendering
            #include <d2d1_1.h>                     // Direct2D 1.1 interface
            #include <d2d1_1helper.h>               // Direct2D helper functions
            #include <dwrite.h>                     // DirectWrite for text rendering

            // Link the required DirectX 11 libraries
            #pragma comment(lib, "d3d11.lib")       // Core DirectX 11 library - REQUIRED for D3D11CreateDevice
            #pragma comment(lib, "dxgi.lib")        // DirectX Graphics Infrastructure library
            #pragma comment(lib, "d3dcompiler.lib") // Shader compiler library

            // Audio libraries
            #pragma comment(lib, "xaudio2.lib")     // XAudio2 library
            #pragma comment(lib, "dsound.lib")      // DirectSound library

            // 2D Graphics and Text libraries
            #pragma comment(lib, "d2d1.lib")        // Direct2D library
            #pragma comment(lib, "dwrite.lib")      // DirectWrite library

            // Additional Windows libraries that DirectX depends on
            #pragma comment(lib, "dxguid.lib")      // DirectX GUIDs
            #pragma comment(lib, "winmm.lib")       // Windows Multimedia library
        #endif

        #if defined(__USE_DIRECTX_12__)
            // DirectX 12 specific includes
            #include <d3d12.h>
            #include <dxgi1_6.h>
            #include <d3dcompiler.h>
            #include <DirectXMath.h>
            #include <DirectXColors.h>
            #include <wrl.h>                                                                  // For Microsoft::WRL::ComPtr
            #include "d3dx12.h"                                                               // DirectX 12 helper library
            #include <d3dcompiler.h>

            // DirectX 11-12 compatibility includes for side-by-side operation
            #include <d3d11.h>
            #include <d3d11_1.h>

            #pragma comment(lib, "d3d12.lib")
            #pragma comment(lib, "dxgi.lib")
            #pragma comment(lib, "d3dcompiler.lib")

            using namespace DirectX;
            using Microsoft::WRL::ComPtr;
        #endif

        #if defined(__USE_OPENGL__)
            // GLEW must be first — it replaces and includes gl.h internally.
            // Fall back to the bare Windows SDK gl.h if GLEW is not installed.
            #if __has_include(<GL/glew.h>)
                #pragma warning(push)
                #pragma warning(disable: 4005)  // wglew.h redefines GLEWAPI already defined in glew.h
                #include <GL/glew.h>
                #pragma warning(pop)
                #pragma comment(lib, "glew32.lib")
            #else
                #include <GL/gl.h>      // Windows SDK — core OpenGL 1.1
            #endif
            #include <GL/glu.h>         // Windows SDK — GLU utilities

            #pragma comment(lib, "opengl32.lib")
            #pragma comment(lib, "glu32.lib")
            #pragma comment(lib, "user32.lib")
            #pragma comment(lib, "gdi32.lib")
            #pragma comment(lib, "shell32.lib")
        #endif

        #if defined(__USE_VULKAN__)
            #define VK_USE_PLATFORM_WIN32_KHR
            #include <vulkan/vulkan.h>
            #include <vulkan/vulkan_win32.h>
            #include <DirectXMath.h>
            #include <DirectXColors.h>
            #include <d2d1_1.h>
            #include <d2d1_1helper.h>
            #include <dwrite.h>

            #if __has_include(<shaderc/shaderc.hpp>)
                #include <shaderc/shaderc.hpp>
            #endif

            #pragma comment(lib, "vulkan-1.lib")
            #pragma comment(lib, "d2d1.lib")
            #pragma comment(lib, "dwrite.lib")
            #pragma comment(lib, "dxguid.lib")
            #pragma comment(lib, "winmm.lib")

            using namespace DirectX;
        #endif
    #endif // !PLATFORM_WINDOWS
#elif defined(__linux__)
    #ifndef PLATFORM_LINUX
        #define PLATFORM_LINUX
    #endif

    // Linux — enable renderers you have installed; runtime selection via rendererType: 0=OpenGL  1=Vulkan
    //#define __USE_OPENGL__
    //#define __USE_VULKAN__
#elif defined(__ANDROID__)
    #ifndef PLATFORM_ANDROID
        #define PLATFORM_ANDROID
    #endif // !PLATFORM_ANDROID

    // Android — enable renderers you have installed; runtime selection via rendererType: 0=OpenGL  1=Vulkan
    //#define __USE_OPENGL__
    //#define __USE_VULKAN__
#elif defined(__APPLE__)
    #ifndef PLATFORM_APPLE
        #define PLATFORM_APPLE
    #endif // !PLATFORM_APPLE
    // macOS — OpenGL only
    //#define __USE_OPENGL__
#elif defined(TARGET_OS_IPHONE) || (TARGET_IPHONE_SIMULATOR)
    #ifndef PLATFORM_IOS
        #define PLATFORM_IOS
    #endif // !PLATFORM_IOS
    // iOS — OpenGL only
    #define __USE_OPENGL__
#endif // !PLATFORM_WINDOWS, PLATFORM_LINUX, PLATFORM_ANDROID, PLATFORM_APPLE, PLATFORM_IOS

// Your General C/C++ Includes
#include <string>
#include <random>                                       // for std::mt19937, std::random_device, std::uniform_real_distribution
#include <stdexcept>
#include <exception>
#include <cstdio>
#include <cstring>                                      // Optional, for string functions if needed
#include <iostream>
#include <sstream>
#include <locale>
#include <codecvt>
#include <fstream>
#include <iomanip>
#include <cmath>
#include <cstdint>
#include <thread>
#include <memory>
#include <algorithm>                                    // Required for std::clamp
#include <mmreg.h>
#include <mmsystem.h>
#include <mutex>                                        // For std::mutex
#include <queue>                                        // For std::priority_queue
#include <functional>                                   // For std::function and lambda support
#include <array>                                        // For std::array<uint32_t, 256>
#include <atomic>                                       // For std::atomic
#include <chrono>                                       // For std::chrono
#include <condition_variable>
#include <filesystem>
#include <objbase.h>                                    // COM infrastructure (CoCreateInstance)
#include <tuple>
#include <propvarutil.h>
#include <cfloat>                                       // for FLT_MAX
#include <vector>                                       // For std::vector
#include <unordered_map>                                // For std::unordered_map
#include <unordered_set>                                // For std::unordered_set
#include <map>				                            // Used for mapping spec indices to object hierarchy

//------------------------------------------
// Cross-Platform Math Type Aliases
// When NOT using DirectX 11/12, map the DX
// math types to the engine's built-in Vector
// classes so shared headers compile cleanly.
//------------------------------------------
#if !defined(__USE_DIRECTX_11__) && !defined(__USE_DIRECTX_12__)
    #include "Vectors.h"    // Vector2, Vector3, Vector4

    // Minimal 4x4 float matrix — identity by default, row-major to match XMMATRIX.
    struct Matrix4x4 {
        float m[4][4];
        Matrix4x4() {
            for (int i = 0; i < 4; i++)
                for (int j = 0; j < 4; j++)
                    m[i][j] = (i == j) ? 1.0f : 0.0f;
        }
    };

    // Alias DirectX math types to portable engine types so existing code compiles.
    using XMFLOAT2 = Vector2;
    using XMFLOAT3 = Vector3;
    using XMFLOAT4 = Vector4;
    using XMMATRIX = Matrix4x4;
    using XMVECTOR = Vector4;

    #ifndef XM_PI
        #define XM_PI 3.141592653589793f
    #endif

    // Empty DirectX namespace stub — lets "using namespace DirectX;" in shared
    // headers compile without pulling in any DX symbols.
    namespace DirectX {}
#endif // !__USE_DIRECTX_11__ && !__USE_DIRECTX_12__

//------------------------------------------
// XM / MP3 Modules
//------------------------------------------
const int MAX_GLOBAL_VOLUME = 64;
#if defined(__USE_MP3PLAYER__)
    inline const std::filesystem::path mp3FilePlaylist[] = { L"game1.mp3" };
    inline const std::filesystem::path SingleMP3Filename = "game1.mp3";
    inline const int MAX_MP3_MODULES = ARRAYSIZE(mp3FilePlaylist);
#elif defined(__USE_XMPLAYER__)
    inline const std::filesystem::path xmFilePlaylist[] = { L"thevoid.xm", L"electro2.xm", L"battle.xm" };
    inline const std::filesystem::path SingleXMFilename = "todie4.xm";
    inline const std::filesystem::path IntroXMFilename = "thevoid.xm";
    inline const int MAX_XM_MODULES = ARRAYSIZE(xmFilePlaylist);
#endif

//------------------------------------------------
// File Tables for Assets (AssetsDir is prepended)
//------------------------------------------------
const std::filesystem::path AssetsDir = L"./Assets/";
const std::filesystem::path WinAssetsDir = L".\\Assets\\";
const std::filesystem::path ShadersDir = L"./Assets/Shaders/";

// Shaders (Just the name, without the extension ie. .hlsl or .glsl
const std::vector<std::string> MyShaders = { "ModelVertex", "ModelPixel" };

// Reserved Shader Slots for Render Pipeline ( b? slot )
const int SLOT_CONST_BUFFER = 0;                                                // Constant Buffer Slot   
const int SLOT_LIGHT_BUFFER = 1;                                                // Model Light Buffer Slot
const int SLOT_DEBUG_BUFFER = 2;                                                // Debug Buffer Slot
const int SLOT_GLOBAL_LIGHT_BUFFER = 3;                                         // Global Light Buffer Slot
const int SLOT_MATERIAL_BUFFER = 4;                                             // Material Buffer Slot
const int SLOT_ENVIRONMENT_BUFFER = 5;                                          // Environment Settings Buffer Slot

// Reserved Texture Slots for Pixel Shader ( t? slot ).
const int SLOT_diffuseTexture = 0;                                              // Diffuse Textures.
const int SLOT_normalMap = 1;                                                   // Normal Texture Mappings.
const int SLOT_metallicMap = 2;                                                 // Metallic Mappings.
const int SLOT_roughnessMap = 3;                                                // Roughness Mappings.
const int SLOT_aoMap = 4;                                                       // Ambient Occulusion Mapping.
const int SLOT_environmentMap = 5;                                              // Environment Mappings for Reflections.

// Reserved Sampler Slots for Pixel Shader ( s? slot )
const int SLOT_SAMPLER_STATE = 0;
const int SLOT_ENVIRO_SAMPLER_STATE = 1;

#if defined(PLATFORM_WINDOWS)
    //------------------------------------------
    // VIDEO & Window Specs: Default Settings
    //------------------------------------------
    const bool START_IN_FULLSCREEN = false;
    const int DEFAULT_WINDOW_WIDTH = 800;
    const int DEFAULT_WINDOW_HEIGHT = 600;
    const float fDEFAULT_WINDOW_WIDTH = 800.0f;
    const float fDEFAULT_WINDOW_HEIGHT = 600.0f;

    const int MAX_WINDOWS = 32;
    const int MAX_SCREEN_MODES = 64;
#endif

// -----------------------------------
// Game Player Relative information
// -----------------------------------
// Player Joystick Controls
const int PLAYER_1 = 0;
//const int PLAYER_2 = 1;
//const int PLAYER_3 = 2;
//const int PLAYER_4 = 3;

// Maximum number of Players supported (1-8)
const int MAX_PLAYERS = 1;

// 2D Textures
inline const std::wstring texFilename[] = {
    L"cursor1.png", L"bg1.jpg", L"loadingring.png", L"window1.png", L"rectbutton1up.png",
    L"winclosebut1up.png", L"bevel1.png", L"titlebar1a.png", L"titlebar1.png",
    L"splash1.png", L"gameintro1.png",
    L"titlebar2.png", L"winbody2.png", L"button2up.png", L"button2down.png", L"logo.png",
    L"tab2red.png", L"tab1gmg.png", L"loading.png"
};

// 3D Textures
inline const std::wstring tex3DFilename[] = {
    L"bricks1.png", L"water1.jpg"
};

// 3D Models
inline const std::wstring modelFilePath[] = {
    L"", L"cube1.obj", L"floor1.obj"
};

const int MAX_TEXTURE_BUFFERS = ARRAYSIZE(texFilename);
const int MAX_TEXTURE_BUFFERS_3D = ARRAYSIZE(tex3DFilename);
const int MAX_MODEL_FILES = ARRAYSIZE(modelFilePath);

// Returns the canonical aspect ratio for a given resolution.
// Matches against the standard display-mode table; falls back to w/h.
inline float LookupAspectRatio(int w, int h)
{
    struct Entry { int w, h; float ar; };
    static constexpr Entry kTable[] = {
        // 16:9
        { 1280,  720, 16.f/9.f }, { 1366,  768, 16.f/9.f }, { 1600,  900, 16.f/9.f },
        { 1920, 1080, 16.f/9.f }, { 2560, 1440, 16.f/9.f }, { 3840, 2160, 16.f/9.f },
        { 7680, 4320, 16.f/9.f },
        // 16:10
        { 1280,  800, 16.f/10.f }, { 1680, 1050, 16.f/10.f },
        { 1920, 1200, 16.f/10.f }, { 2560, 1600, 16.f/10.f },
        // 21:9 ultrawide
        { 2560, 1080, 21.f/9.f }, { 3440, 1440, 21.f/9.f }, { 5120, 2160, 21.f/9.f },
        // 32:9 super-ultrawide
        { 3840, 1080, 32.f/9.f }, { 5120, 1440, 32.f/9.f },
        // 4:3
        {  640,  480, 4.f/3.f }, {  800,  600, 4.f/3.f }, { 1024,  768, 4.f/3.f },
        { 1600, 1200, 4.f/3.f }, { 2048, 1536, 4.f/3.f },
        // 3:2
        {  480,  320, 3.f/2.f }, { 1152,  768, 3.f/2.f }, { 1440,  960, 3.f/2.f },
        // 5:4
        { 1280, 1024, 5.f/4.f }, { 2560, 2048, 5.f/4.f },
        // 17:9 DCI cinema
        { 2048, 1080, 17.f/9.f }, { 4096, 2160, 17.f/9.f },
    };
    for (const auto& e : kTable)
        if (e.w == w && e.h == h) return e.ar;
    return (h > 0) ? static_cast<float>(w) / static_cast<float>(h) : 16.f/9.f;
}

// Model Names
const std::wstring ShipName1 = L"Ship1";
