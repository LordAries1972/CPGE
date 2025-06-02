/* ---------------------------------------------------------------------------------------------------------
This is your base Includes.h header file for the specified Renderer & other engine sub-systems 
and to only include what is needed on a given platform.

It includes all the necessary headers and libraries for Windows, DirectX, OpenGL, and other platforms.
This is to eliminate the need for multiple includes in your project files.
--------------------------------------------------------------------------------------------------------- */
#pragma once

// All our Window Includes
#define NOMINMAX										                                // allows us to use std::min, std::max

#define __USE_XMPLAYER__
//#define __USE_MP3PLAYER__
#define __USING_JOYSTICKS__                                                             // Uncomment this line if you want to use Joysticks with this engine.

// Windows Specific Includes
#if defined(_WIN64) || defined(_WIN32)
    // CRITICAL: Define this before any Windows includes to prevent macro conflicts
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif

    #include <windows.h>
    #include <winerror.h>
    #include <wincodec.h>                                                               // WIC
    #include <mfapi.h>
    #include <mfidl.h>
    #include <mfobjects.h>
    #include <mfreadwrite.h>
    #include <mfplay.h>
    #include <wrl/client.h>
    #include <comdef.h>                                                                 // For _com_error

    using Microsoft::WRL::ComPtr;

#endif  // End of #if defined(_WIN64) || defined(_WIN32)

// Windows-specific includes & configuration
#if defined(_WIN32) || defined(_WIN64)
// Windows Platform Master Renderer Switches
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
    #include "opengl32.h"
    #include "GL\glew32.h"

    #pragma comment(lib, "glfw3.lib")
    #pragma comment(lib, "glew32.lib")
    #pragma comment(lib, "opengl32.lib")
    #pragma comment(lib, "user32.lib")
    #pragma comment(lib, "gdi32.lib")
    #pragma comment(lib, "shell32.lib")
#endif

#elif defined(__linux__)
//#define __USE_OPENGL__
//#define __USE_VULKAN__
#elif defined(__ANDROID__)
//#define __USE_OPENGL__
//#define __USE_VULKAN__
#elif defined(__APPLE__)
//#define __USE_OPENGL__
#elif defined(TARGET_OS_IPHONE) || (TARGET_IPHONE_SIMULATOR)
#else
    // FUTURE: Whatever other OS you may need to build a Renderer for.
#endif

// Your General C++ Includes
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
#include <map>				                            // Used for mapping spec indices to object hierarchy

//------------------------------------------
// XM / MP3 Modules
//------------------------------------------
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

// 2D Textures
inline const std::wstring texFilename[] = {
    L"cursor1.png", L"bg1.jpg", L"loadingring.png", L"window1.png", L"rectbutton1up.png",
    L"winclosebut1up.png", L"bevel1.png", L"titlebar1a.png", L"titlebar1.png",
    L"scrollbg1.png", L"scrollbg2.png", L"scrollbg3.png", L"splash1.png", L"gameintro1.png",
    L"titlebar2.png", L"winbody2.png", L"button2up.png", L"button2down.png", L"logo.png",
    L"tab2red.png", L"tab1gmg.png"
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

