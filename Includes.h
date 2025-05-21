#pragma once

// All our Window Includes
#define NOMINMAX										// allows us to use std::min, std::max

#include <windows.h>
#include <string>

#include <wrl/client.h>
using Microsoft::WRL::ComPtr; 

#include <random>       // for std::mt19937, std::random_device, std::uniform_real_distribution
#include <comdef.h>                                     // For _com_error
#include <winerror.h>
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
#include <algorithm>        // Required for std::clamp
#include <mmreg.h>
#include <mmsystem.h>
#include <mutex>            // For std::mutex
#include <atomic>           // For std::atomic
#include <chrono>           // For std::chrono
#include <functional>
#include <condition_variable>
#include <filesystem>
#include <wincodec.h>       // WIC
#include <objbase.h>        // COM infrastructure (CoCreateInstance)
#include <tuple>
#include <mfapi.h>
#include <mfidl.h>
#include <mfobjects.h>
#include <mfplay.h>
#include <propvarutil.h>
#include <cfloat>           // for FLT_MAX
#include <vector>           // For std::vector
#include <unordered_map>    // For std::unordered_map
#include <map>				// Used in Step 5 for mapping spec indices to object hierarchy

// DirextX Related
#include <xaudio2.h>
#include <dsound.h>

#include <dxgi.h>
#include <dxgi1_2.h>

#include <d2d1_1.h>
#include <d2d1_1helper.h>
#include <dwrite.h>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

#define __USE_XMPLAYER__
//#define __USE_MP3PLAYER__

//#include "RendererDefines.h"
