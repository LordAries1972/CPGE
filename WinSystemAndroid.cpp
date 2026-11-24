// ----------------------------------------------------------------------------------------------
// WinSystemAndroid.cpp -- Android implementation of the SystemUtils interface (see WinSystem.h).
//
// This file is compiled ONLY by the Android build (Linux/CMakeLists.txt); the Windows
// implementation lives in WinSystem.cpp and the Linux one in WinSystemLinux.cpp, both
// untouched. It mirrors the Windows method set one-for-one so shared code
// (Configuration, Models, SceneManager, IOLoaderThread, the renderers, main.cpp)
// compiles unchanged:
//
//   * Pure string/timer math (ToWString, widen, StripQuotes, ScaleMouseCoordinates,
//     StartTimer, CheckElapsedTime) is the same logic as WinSystemLinux.cpp.
//   * Filesystem queries use the activity's internal-storage path (the only
//     writable directory an app owns) instead of the process working directory:
//     on Android getcwd() is "/" and is not writable, so Get_Current_Directory()
//     returns AndroidApp::GetInternalDataPath().
//   * Window queries use ANativeWindow directly (the HWND carried by callers is
//     an ANativeWindow* — see Platform.h).
//   * Concepts with no Android equivalent (Win32 message pump, window class
//     destruction, OS cursor hiding, Windows version) are silent no-ops or
//     return a safe default; the main loop pumps the app-glue looper itself
//     (PumpPlatformMessages in main.cpp).
// ----------------------------------------------------------------------------------------------
#include "Includes.h"
#include "WinSystem.h"
#include "Debug.h"
#include "AndroidApp.h"

#include <cstdlib>          // std::mbstowcs
#include <cwchar>           // std::mbstowcs
#include <sys/utsname.h>    // uname()

#include <android/native_window.h>     // ANativeWindow_getWidth/Height
#include <android/configuration.h>     // AConfiguration_getDensity

namespace
{
// Narrow (UTF-8/ASCII) -> wide conversion shared by ToWString/widen.
// mbstowcs honours the current C locale; log/config strings are ASCII in
// practice, so the byte-wise fallback is exact for them and never fails.
std::wstring WidenString(const std::string& input)
{
    if (input.empty())
        return L"";
    std::wstring result(input.size(), L'\0');
    std::size_t converted = std::mbstowcs(result.data(), input.c_str(), input.size());
    if (converted == static_cast<std::size_t>(-1))
        result.assign(input.begin(), input.end());   // invalid multibyte: widen byte-wise
    else
        result.resize(converted);
    return result;
}

// The ANativeWindow hidden inside an HWND (see Platform.h). Returns nullptr for null.
ANativeWindow* NativeWindowFromHandle(HWND hWnd)
{
    if (!hWnd)
        return nullptr;
    return reinterpret_cast<ANativeWindow*>(hWnd);
}

// Display density in DPI (160 = mdpi baseline), or 0 when unavailable.
int32_t DisplayDensityDpi()
{
    struct android_app* app = AndroidApp::GetApp();
    if (!app || !app->config)
        return 0;
    return AConfiguration_getDensity(app->config);
}
} // namespace

SystemUtils::SystemUtils() : m_timerInitialized(false)
{
    m_timerStartTime = std::chrono::high_resolution_clock::time_point{};
}

SystemUtils::~SystemUtils() {}

WindowsVersion SystemUtils::GetWindowsVersion()
{
    // There is no Windows version on Android; callers only use this behind
    // PLATFORM_WINDOWS guards, but return the safe "unknown" value anyway.
    return WINVER_UNKNOWN;
}

void SystemUtils::CenterSystemWindow(HWND /*hwnd*/)
{
    // No-op on Android: a NativeActivity window is always fullscreen and its
    // placement is the system's job, not the app's.
}

void SystemUtils::DisableMouseCursor()
{
    // No-op on Android: touch devices have no OS cursor to hide.
}

void SystemUtils::EnableMouseCursor()
{
    // No-op on Android (see DisableMouseCursor).
}

RECT SystemUtils::GetSystemWindowSize(HWND hWnd)
{
    RECT rect{ 0, 0, 0, 0 };
    ANativeWindow* window = NativeWindowFromHandle(hWnd);
    if (!window)
        return rect;

    const int32_t width  = ANativeWindow_getWidth(window);
    const int32_t height = ANativeWindow_getHeight(window);
    if (width <= 0 || height <= 0)
        return rect;

    rect.right  = width;
    rect.bottom = height;
    return rect;
}

void SystemUtils::DestroySystemWindow(HINSTANCE /*hInstance*/, HWND /*hwnd*/, LPCWSTR /*classname*/)
{
    // No-op on Android: the ANativeWindow is owned by the activity and is
    // destroyed by the system (APP_CMD_TERM_WINDOW), not by the engine.
}

void SystemUtils::GetMessageAndProcess()
{
    // No-op on Android: there is no Win32 message queue. The main loop in
    // main.cpp pumps the app-glue looper directly (PumpPlatformMessages).
}

void SystemUtils::ProcessMessages()
{
    // No-op on Android (see GetMessageAndProcess).
}

bool SystemUtils::IsWindowMinimized()
{
    // Minimised-state tracking is not wired up on Android yet; report false so
    // callers keep rendering instead of pausing on a stale state. (The
    // activity lifecycle pauses the app at the OS level instead.)
    return false;
}

bool SystemUtils::GetWindowMetrics(HWND hWnd, WindowMetrics& outMetrics)
{
    ANativeWindow* window = NativeWindowFromHandle(hWnd);
    if (!window)
    {
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"Invalid window handle provided to GetWindowMetrics");
        return false;
    }

    const int32_t width  = ANativeWindow_getWidth(window);
    const int32_t height = ANativeWindow_getHeight(window);
    if (width <= 0 || height <= 0)
    {
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"GetWindowMetrics: native window has no size yet");
        return false;
    }

    const int32_t densityDpi = DisplayDensityDpi();
    outMetrics.hWnd         = hWnd;
    outMetrics.x            = 0;
    outMetrics.y            = 0;
    outMetrics.width        = width;
    outMetrics.height       = height;
    outMetrics.clientWidth  = width;
    outMetrics.clientHeight = height;
    outMetrics.borderWidth  = 0;
    outMetrics.titleBarHeight = 0;
    outMetrics.dpi          = densityDpi > 0 ? static_cast<UINT>(densityDpi) : 96;
    outMetrics.dpiScaleFactor = densityDpi > 0 ? static_cast<float>(densityDpi) / 160.0f : 1.0f;
    outMetrics.systemBorderWidth  = 0;
    outMetrics.systemBorderHeight = 0;
    outMetrics.systemTitleBarHeight = 0;
    outMetrics.menuBarHeight = 0;
    outMetrics.isMaximized  = true;   // NativeActivity is always fullscreen
    outMetrics.isMinimized  = false;
    outMetrics.hasToolWindowBorder = false;
    outMetrics.hasDialogFrame = false;
    outMetrics.monitorWorkArea = { 0, 0, width, height };
    outMetrics.monitorFullArea = outMetrics.monitorWorkArea;
    outMetrics.isPrimaryMonitor = true;
    outMetrics.isFullScreen = true;
    outMetrics.isBorderless = true;

    return true;
}

bool SystemUtils::Is64BitOperatingSystem()
{
    return sizeof(void*) == 8;
}

std::wstring SystemUtils::GetProcessorArchitecture()
{
    // uname() machine field: "aarch64", "x86_64", "armv7l", ...
    struct utsname info {};
    if (uname(&info) == 0)
        return WidenString(info.machine);
    return L"Unknown";
}

void SystemUtils::StartTimer()
{
    // Capture the current high-resolution time point (same semantics as the
    // Windows version: high_resolution_clock for maximum precision).
    m_timerStartTime = std::chrono::high_resolution_clock::now();
    m_timerInitialized = true;
}

bool SystemUtils::CheckElapsedTime(int NumOfSeconds)
{
    // Validate that the timer has been initialized with StartTimer()
    if (!m_timerInitialized)
    {
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"CheckElapsedTime() called before StartTimer() - timer not initialized");
        return false;
    }

    // Validate input parameter - negative seconds don't make sense
    if (NumOfSeconds < 0)
    {
        debug.logDebugMessage(LogLevel::LOG_WARNING, L"CheckElapsedTime() called with negative seconds (%d) - returning false", NumOfSeconds);
        return false;
    }

    // Capture current time for elapsed time calculation
    auto currentTime = std::chrono::high_resolution_clock::now();
    auto elapsedDuration = currentTime - m_timerStartTime;
    auto elapsedSeconds = std::chrono::duration_cast<std::chrono::seconds>(elapsedDuration).count();

    return elapsedSeconds >= NumOfSeconds;
}

std::tuple<int, int> SystemUtils::GetPrimaryMonitorFullScreenSize()
{
    // The NativeActivity window IS the screen (fullscreen): its size is the
    // display size. Fall back to a common size so callers never divide by
    // zero or create a 0x0 window when no window exists yet.
    ANativeWindow* window = AndroidApp::GetNativeWindow();
    if (window)
    {
        const int32_t width  = ANativeWindow_getWidth(window);
        const int32_t height = ANativeWindow_getHeight(window);
        if (width > 0 && height > 0)
            return std::make_tuple(static_cast<int>(width), static_cast<int>(height));
    }

    debug.logDebugMessage(LogLevel::LOG_WARNING, L"GetPrimaryMonitorFullScreenSize: no native window, defaulting to 1920x1080");
    return std::make_tuple(1920, 1080);
}

std::wstring SystemUtils::Get_Current_Directory()
{
    // Android has no meaningful process working directory for an app (getcwd
    // returns "/" and it is not writable). The writable home of the app is its
    // internal-storage directory, so that is the engine's base directory:
    // GameConfig.cfg, DebugLog.txt and saves all live there.
    return WidenString(AndroidApp::GetInternalDataPath());
}

std::wstring SystemUtils::ToWString(const std::string& input)
{
    return WidenString(input);
}

std::wstring SystemUtils::widen(const std::string& str)
{
    return WidenString(str);
}

std::wstring SystemUtils::StripQuotes(const std::wstring& input)
{
    std::wstring trimmed = input;
    // Remove leading/trailing whitespace
    trimmed.erase(0, trimmed.find_first_not_of(L" \t\r\n"));
    trimmed.erase(trimmed.find_last_not_of(L" \t\r\n") + 1);

    if (trimmed.size() >= 2 && trimmed.front() == L'"' && trimmed.back() == L'"')
        return trimmed.substr(1, trimmed.size() - 2);

    return trimmed;
}

std::wstring SystemUtils::GetExecutableVersion()
{
    // Android has no Win32 version resource; the engine's build identity lives
    // in Version.id / BuildInfo.h instead.
    return L"";
}

std::tuple<int, int> SystemUtils::ScaleMouseCoordinates(int originalX, int originalY, int originalWidth, int originalHeight, int newWidth, int newHeight)
{
    if (originalWidth == 0 || originalHeight == 0)
    {
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"Original width or height is zero.");
        return std::make_tuple(0, 0);
    }

    // Calculate scaling ratios
    float scaleX = static_cast<float>(newWidth) / originalWidth;
    float scaleY = static_cast<float>(newHeight) / originalHeight;

    // Scale the mouse coordinates
    int scaledX = static_cast<int>(originalX * scaleX);
    int scaledY = static_cast<int>(originalY * scaleY);

    // Apply clamping if required.
    scaledX = std::min(std::max(scaledX, 0), newWidth - 1);
    scaledY = std::min(std::max(scaledY, 0), newHeight - 1);

    // Return the scaled coordinates
    return std::make_tuple(scaledX, scaledY);
}
