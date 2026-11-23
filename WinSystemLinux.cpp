// ----------------------------------------------------------------------------------------------
// WinSystemLinux.cpp -- Linux implementation of the SystemUtils interface (see WinSystem.h).
//
// This file is compiled ONLY by the Linux build (Linux/CMakeLists.txt); the Windows
// implementation lives in WinSystem.cpp and is untouched. It mirrors the Windows
// method set one-for-one so shared code (Configuration, Models, SceneManager,
// IOLoaderThread, the renderers, main.cpp) compiles unchanged:
//
//   * Pure string/timer math (ToWString, widen, StripQuotes, ScaleMouseCoordinates,
//     StartTimer, CheckElapsedTime) is the same logic as WinSystem.cpp, with
//     MultiByteToWideChar replaced by mbstowcs (byte-wise widening fallback).
//   * Filesystem queries use std::filesystem / uname instead of the Win32 APIs.
//   * Window queries use X11 directly (the HWND carried by callers is an X11
//     Window cast through uintptr_t — see Platform.h).
//   * Concepts with no Linux equivalent (Win32 message pump, window class
//     destruction, OS cursor hiding, Windows version) are silent no-ops or
//     return a safe default; the main loop pumps X11 events itself.
// ----------------------------------------------------------------------------------------------
#include "Includes.h"
#include "WinSystem.h"
#include "Debug.h"

#include <cstdlib>          // std::mbstowcs
#include <cwchar>           // std::mbstowcs
#include <filesystem>
#include <sys/utsname.h>    // uname()

// X11 typedefs KeyCode (unsigned char), which collides with the engine's
// enum class KeyCode (KeyboardHandler.h). Rename the X11 typedef at include
// time so the two can coexist in one translation unit.
#define KeyCode X11KeyCode
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#undef KeyCode

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

// The X11 Window hidden inside an HWND (see Platform.h). Returns 0 for null.
::Window XWindowFromHandle(HWND hWnd)
{
    if (!hWnd)
        return 0;
    return static_cast<::Window>(reinterpret_cast<std::uintptr_t>(hWnd));
}
} // namespace

SystemUtils::SystemUtils() : m_timerInitialized(false)
{
    m_timerStartTime = std::chrono::high_resolution_clock::time_point{};
}

SystemUtils::~SystemUtils() {}

WindowsVersion SystemUtils::GetWindowsVersion()
{
    // There is no Windows version on Linux; callers only use this behind
    // PLATFORM_WINDOWS guards, but return the safe "unknown" value anyway.
    return WINVER_UNKNOWN;
}

void SystemUtils::CenterSystemWindow(HWND /*hwnd*/)
{
    // No-op on Linux: window placement/centering is the window manager's job
    // (the X11 window is created centred by main.cpp instead).
}

void SystemUtils::DisableMouseCursor()
{
    // No-op in this phase: the OS cursor is left visible; the engine renders
    // its own cursor in fullscreen. A future XFixes hide can hook in here.
}

void SystemUtils::EnableMouseCursor()
{
    // No-op on Linux (see DisableMouseCursor).
}

RECT SystemUtils::GetSystemWindowSize(HWND hWnd)
{
    RECT rect{ 0, 0, 0, 0 };
    ::Window window = XWindowFromHandle(hWnd);
    if (window == 0)
        return rect;

    Display* display = XOpenDisplay(nullptr);
    if (!display)
        return rect;

    XWindowAttributes attrs{};
    if (XGetWindowAttributes(display, window, &attrs))
    {
        rect.left   = attrs.x;
        rect.top    = attrs.y;
        rect.right  = attrs.x + attrs.width;
        rect.bottom = attrs.y + attrs.height;
    }
    XCloseDisplay(display);
    return rect;
}

void SystemUtils::DestroySystemWindow(HINSTANCE /*hInstance*/, HWND /*hwnd*/, LPCWSTR /*classname*/)
{
    // No-op on Linux: the X11 window is destroyed by main.cpp (XDestroyWindow);
    // there is no window class to unregister.
}

void SystemUtils::GetMessageAndProcess()
{
    // No-op on Linux: there is no Win32 message queue. The main loop in
    // main.cpp pumps X11 events directly, and KeyboardHandler runs its own
    // X11 event thread.
}

void SystemUtils::ProcessMessages()
{
    // No-op on Linux (see GetMessageAndProcess).
}

bool SystemUtils::IsWindowMinimized()
{
    // Minimised-state tracking is not wired up on Linux yet; report false so
    // callers keep rendering instead of pausing on a stale state.
    return false;
}

bool SystemUtils::GetWindowMetrics(HWND hWnd, WindowMetrics& outMetrics)
{
    ::Window window = XWindowFromHandle(hWnd);
    if (window == 0)
    {
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"Invalid window handle provided to GetWindowMetrics");
        return false;
    }

    Display* display = XOpenDisplay(nullptr);
    if (!display)
    {
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"GetWindowMetrics: could not open X11 display");
        return false;
    }

    XWindowAttributes attrs{};
    if (!XGetWindowAttributes(display, window, &attrs))
    {
        XCloseDisplay(display);
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"GetWindowMetrics: XGetWindowAttributes failed");
        return false;
    }

    const int screen = DefaultScreen(display);
    outMetrics.hWnd         = hWnd;
    outMetrics.x            = attrs.x;
    outMetrics.y            = attrs.y;
    outMetrics.width        = attrs.width;
    outMetrics.height       = attrs.height;
    outMetrics.clientWidth  = attrs.width;
    outMetrics.clientHeight = attrs.height;
    outMetrics.borderWidth  = attrs.border_width;
    outMetrics.titleBarHeight = 0;
    outMetrics.dpi          = 96;
    outMetrics.dpiScaleFactor = 1.0f;
    outMetrics.systemBorderWidth  = attrs.border_width;
    outMetrics.systemBorderHeight = attrs.border_width;
    outMetrics.systemTitleBarHeight = 0;
    outMetrics.menuBarHeight = 0;
    outMetrics.isMaximized  = false;
    outMetrics.isMinimized  = false;
    outMetrics.hasToolWindowBorder = false;
    outMetrics.hasDialogFrame = false;
    outMetrics.monitorWorkArea = { 0, 0, DisplayWidth(display, screen), DisplayHeight(display, screen) };
    outMetrics.monitorFullArea = outMetrics.monitorWorkArea;
    outMetrics.isPrimaryMonitor = true;
    outMetrics.isFullScreen = false;
    outMetrics.isBorderless = false;

    XCloseDisplay(display);
    return true;
}

bool SystemUtils::Is64BitOperatingSystem()
{
    return sizeof(void*) == 8;
}

std::wstring SystemUtils::GetProcessorArchitecture()
{
    // uname() machine field: "x86_64", "aarch64", "i686", ...
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
    Display* display = XOpenDisplay(nullptr);
    if (display)
    {
        const int screen = DefaultScreen(display);
        const int width  = DisplayWidth(display, screen);
        const int height = DisplayHeight(display, screen);
        XCloseDisplay(display);
        if (width > 0 && height > 0)
            return std::make_tuple(width, height);
    }

    // No X display (headless build check): fall back to a common desktop size
    // so callers never divide by zero or create a 0x0 window.
    debug.logDebugMessage(LogLevel::LOG_WARNING, L"GetPrimaryMonitorFullScreenSize: no X display, defaulting to 1920x1080");
    return std::make_tuple(1920, 1080);
}

std::wstring SystemUtils::Get_Current_Directory()
{
    std::error_code ec;
    std::filesystem::path cwd = std::filesystem::current_path(ec);
    if (ec)
        return L"";
    return cwd.wstring();
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

    if (trimmed.size() >= 2 && trimmed.front() == L'\"' && trimmed.back() == L'\"')
        return trimmed.substr(1, trimmed.size() - 2);

    return trimmed;
}

std::wstring SystemUtils::GetExecutableVersion()
{
    // Linux has no Win32 version resource; the engine's build identity lives
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
