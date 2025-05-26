#pragma once

// All our Window Includes
#define NOMINMAX										// allows us to use std::min, std::max

#define __USE_XMPLAYER__
//#define __USE_MP3PLAYER__

// Windows Specific Includes
#if defined(_WIN64) || defined(_WIN32)
    #include <windows.h>
    #include <winerror.h>
    #include <wincodec.h>                                   // WIC
    #include <mfapi.h>
    #include <mfidl.h>
    #include <mfobjects.h>
    #include <mfplay.h>
    #include <wrl/client.h>
    #include <comdef.h>                                     // For _com_error

    using Microsoft::WRL::ComPtr;

    // DirectX Related
    #include <xaudio2.h>
    #include <dsound.h>

    #include <dxgi.h>
    #include <dxgi1_2.h>

    #include <d2d1_1.h>
    #include <d2d1_1helper.h>
    #include <dwrite.h>

    #pragma comment(lib, "d2d1.lib")
    #pragma comment(lib, "dwrite.lib")
#endif  // End of #if defined(_WIN64) || defined(_WIN32)

// Windows-specific includes & configuration
#if defined(_WIN32) || defined(_WIN64)
#define __USING_DX_2D__
#define __USING_DX_3D__
#define __USE_DIRECTX_11__
//#define __USE_DIRECTX_12__
//#define __USE_OPENGL__
//#define __USE_VULKAN__
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
#include <array>
#include <algorithm>                                    // Required for std::clamp
#include <mmreg.h>
#include <mmsystem.h>
#include <mutex>                                        // For std::mutex
#include <atomic>                                       // For std::atomic
#include <chrono>                                       // For std::chrono
#include <functional>
#include <condition_variable>
#include <filesystem>
#include <objbase.h>                                    // COM infrastructure (CoCreateInstance)
#include <tuple>
#include <propvarutil.h>
#include <cfloat>                                       // for FLT_MAX
#include <vector>                                       // For std::vector
#include <unordered_map>                                // For std::unordered_map
#include <map>				                            // Used for mapping spec indices to object hierarchy

