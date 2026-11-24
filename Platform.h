#pragma once

// <atomic> is needed on every platform: the cross-platform quit flags at
// the bottom of this file (g_cpgeQuitRequested, g_cpgeMainLoopActive) are
// std::atomic, and the <atomic> include inside the non-Windows block below
// is skipped on Windows.
#include <atomic>

// One OS identity shared by all CPGE base classes. Android also defines
// __linux__, and every Apple target defines __APPLE__: test the narrower
// targets first so their input, timer and filesystem backends stay correct.
#if defined(_WIN32) || defined(_WIN64)
    #define PLATFORM_WINDOWS
#elif defined(__ANDROID__)
    #define PLATFORM_ANDROID
#elif defined(__linux__)
    #define PLATFORM_LINUX
#elif defined(__APPLE__)
    #include <TargetConditionals.h>
    #if TARGET_OS_IPHONE
        #define PLATFORM_IOS
    #else
        #define PLATFORM_MACOS
        #define PLATFORM_APPLE
    #endif
#else
    #error Unsupported CPGE operating system
#endif

// ----------------------------------------------------------------------------------------------
// Portable Win32-compatible types (Linux / Android / Apple only).
// A large amount of shared engine code names HWND, RECT, DWORD, LPCWSTR, etc. directly
// (window handles, metrics structs, log helpers). On Windows these come from <windows.h>.
// On every other platform the aliases below give those names the same width and meaning
// so the shared code compiles unchanged; platform code converts to the native handle
// at the boundary (e.g. an X11 Window is carried in an HWND via uintptr_t).
// Nothing in this block is visible on Windows, so the Windows build is unaffected.
// ----------------------------------------------------------------------------------------------
#if !defined(PLATFORM_WINDOWS)
    #include <cstdint>
    #include <cstddef>
    #include <cstring>      // std::memset (ZeroMemory / SecureZeroMemory below)
    #include <atomic>
    #include <chrono>
    #include <thread>

    using HWND      = void*;                // Linux: X11 Window via (HWND)(uintptr_t)window; Android: ANativeWindow*
    using HINSTANCE = void*;
    using HMODULE   = void*;
    using HANDLE    = void*;
    using HDC       = void*;
    using HBRUSH    = void*;
    using HICON     = void*;
    using HCURSOR   = void*;
    using HGDIOBJ   = void*;
    using HRESULT   = std::int32_t;
    using DWORD     = std::uint32_t;
    using UINT      = unsigned int;
    using WORD      = std::uint16_t;
    using BYTE      = std::uint8_t;
    using FLOAT     = float;
    using BOOL      = int;
    using LONG      = std::int32_t;
    using ULONG     = std::uint32_t;
    using SHORT     = std::int16_t;
    using USHORT    = std::uint16_t;
    using ULONGLONG = std::uint64_t;
    using LONGLONG  = std::int64_t;
    // Fixed-width Win32 scalar aliases used across the engine (OpenGLRenderer,
    // Vulkan headers, model/texture code). On Windows these come from windows.h.
    using UINT64    = std::uint64_t;
    using INT64     = std::int64_t;
    using UINT32    = std::uint32_t;
    using INT32     = std::int32_t;
    using UINT16    = std::uint16_t;
    using INT16     = std::int16_t;
    using UINT8     = std::uint8_t;
    using INT8      = std::int8_t;
    using ULONG_PTR = std::uintptr_t;
    using DWORD_PTR = std::uintptr_t;
    using LONG_PTR  = std::intptr_t;
    using SIZE_T    = std::size_t;
    using WPARAM    = std::uintptr_t;
    using LPARAM    = std::intptr_t;
    using LRESULT   = std::intptr_t;
    using LPCWSTR   = const wchar_t*;
    using LPWSTR    = wchar_t*;
    using LPCSTR    = const char*;
    using LPSTR     = char*;
    using WCHAR     = wchar_t;

    struct RECT { LONG left; LONG top; LONG right; LONG bottom; };
    struct POINT { LONG x; LONG y; };
    struct SIZE { LONG cx; LONG cy; };

    #ifndef FAILED
        #define FAILED(hr) ((hr) < 0)
    #endif
    #ifndef SUCCEEDED
        #define SUCCEEDED(hr) ((hr) >= 0)
    #endif
    #ifndef S_OK
        #define S_OK ((HRESULT)0)
    #endif
    #ifndef S_FALSE
        #define S_FALSE ((HRESULT)1)
    #endif
    #ifndef E_FAIL
        #define E_FAIL ((HRESULT)0x80004005)
    #endif
    #ifndef TRUE
        #define TRUE 1
    #endif
    #ifndef FALSE
        #define FALSE 0
    #endif
    #ifndef MAX_PATH
        #define MAX_PATH 260
    #endif
    // ZeroMemory / SecureZeroMemory are Win32 helpers used unguarded in a few
    // shared files (OpenGLRenderer ctor, IOLoaderThread). On POSIX they are
    // plain memset; the "secure" no-optimise-away guarantee is not needed for
    // the POD buffers they are applied to here.
    #ifndef ZeroMemory
        #define ZeroMemory(dest, size) std::memset((dest), 0, (size))
    #endif
    #ifndef SecureZeroMemory
        #define SecureZeroMemory(dest, size) std::memset((dest), 0, (size))
    #endif

    // Portable replacements for the two Win32 helpers the shared code calls
    // directly. GetTickCount64() matches the Win32 monotonic-ms semantics
    // (steady_clock, so it never jumps with wall-clock changes); Sleep() is
    // the usual millisecond sleep used by the main loop and StartGame().
    inline std::uint64_t GetTickCount64()
    {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
    }
    inline void Sleep(unsigned long ms)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    }
#endif

// Cross-platform quit request. Set by any subsystem that used to call
// PostQuitMessage(0) (GUIWindows, ScriptManager, Debug fatal path) and
// polled by the main loop in main.cpp. One inline variable (C++17) so
// every translation unit shares the same flag without a .cpp definition.
// Declared outside the non-Windows block above on purpose: the shared main
// loop and the Windows message pump both use this flag, so guarding it out
// on Windows breaks the Windows build.
inline std::atomic<bool> g_cpgeQuitRequested{ false };

// Set while the main loop in main.cpp is running. The non-Windows fatal
// path in Debug::EmitMessage only requests a quit for LOG_ERROR /
// LOG_CRITICAL once the loop is active: errors raised during startup
// (a missing intro movie, ...) must not kill the engine before it runs.
inline std::atomic<bool> g_cpgeMainLoopActive{ false };
