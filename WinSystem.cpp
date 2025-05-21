#include "Includes.h"
#include "WinSystem.h"
#include "Debug.h"

extern HWND hwnd;
extern Debug debug;

#pragma comment(lib, "Version.lib")

//SystemUtils::SystemUtils() : playing(false), paused(false) {}
SystemUtils::SystemUtils() {}

SystemUtils::~SystemUtils() {}

WindowsVersion SystemUtils::GetWindowsVersion() 
{
    OSVERSIONINFOEXW osInfo = { 0 };
    osInfo.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEXW);

    // Use RtlGetVersion() for accurate Windows version detection
    typedef LONG(WINAPI* RtlGetVersionPtr)(PRTL_OSVERSIONINFOEXW);
    HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");

    if (hNtdll) {
        auto RtlGetVersion = (RtlGetVersionPtr)GetProcAddress(hNtdll, "RtlGetVersion");
        if (RtlGetVersion) {
            RtlGetVersion(&osInfo);
        }
    }

    // Windows version checking
    if (osInfo.dwMajorVersion == 10) {
        if (osInfo.dwBuildNumber >= 22000) {
            return WINVER_WIN11;
        }
        return WINVER_WIN10;
    }
    else if (osInfo.dwMajorVersion == 6) {
        if (osInfo.dwMinorVersion == 3) return WINVER_WIN8_1;
        if (osInfo.dwMinorVersion == 2) return WINVER_WIN8;
        if (osInfo.dwMinorVersion == 1) return WINVER_WIN7;
    }

    return WINVER_UNKNOWN;
}

// For Windows:
bool SystemUtils::IsWindowMinimized()
{
	// Check if the window is minimized
	if (hwnd) 
    {
		WINDOWPLACEMENT wp;
        SecureZeroMemory(&wp, sizeof(WINDOWPLACEMENT));
        GetWindowPlacement(hwnd, &wp);
		return (wp.showCmd == SW_SHOWMINIMIZED);
	}
	return false;
}

std::wstring SystemUtils::widen(const std::string& str) 
{
    if (str.empty()) return L"";
    int size = MultiByteToWideChar(CP_ACP, 0, &str[0], (int)str.size(), NULL, 0);
    std::wstring result(size, 0);
    MultiByteToWideChar(CP_ACP, 0, &str[0], (int)str.size(), &result[0], size);
    return result;
}

std::wstring SystemUtils::Get_Current_Directory()
{
	wchar_t buffer[MAX_PATH];
	GetCurrentDirectoryW(MAX_PATH, buffer);
	std::wstring currentDir(buffer);
    #if defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"Current Directory: %ls", currentDir.c_str());
    #endif
    
        return currentDir;
}

// ===== --------------------------------------------------------------------------------- =====
// Name: SystemUtils::StripQuotes(const std::wstring& input)
// 
// This function removes leading and trailing whitespace and quotes from the input string
// 
// Handy for preventing SQL injection attacks and other security issues, including
// unintentional string formatting issues such as JSON parsing.
// ===== --------------------------------------------------------------------------------- =====
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

std::wstring SystemUtils::ToWString(const std::string& input)
{
    if (input.empty()) return L"";

    // Determine the size required for the wide string
    int wideSize = MultiByteToWideChar(CP_UTF8, 0, input.c_str(), -1, nullptr, 0);
    if (wideSize == 0) return L"";

    // Allocate buffer and perform conversion
    std::wstring wideString(wideSize, 0);
    MultiByteToWideChar(CP_UTF8, 0, input.c_str(), -1, &wideString[0], wideSize);

    // Remove null terminator that was added
    if (!wideString.empty() && wideString.back() == L'\0') {
        wideString.pop_back();
    }

    return wideString;
}

void SystemUtils::GetMessageAndProcess()
{
    MSG msg;
    GetMessage(&msg, nullptr, 0, 0);
    TranslateMessage(&msg);
    DispatchMessage(&msg);
}

void SystemUtils::ProcessMessages()
{
    MSG msg;
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) 
    {
       TranslateMessage(&msg);
       DispatchMessage(&msg);
    }
}

void SystemUtils::CenterSystemWindow(HWND hwnd)
{
    // Get the dimensions of the primary monitor's working area
    RECT rectScreen;
    SystemParametersInfo(SPI_GETWORKAREA, 0, &rectScreen, 0);

    // Get the dimensions of the window
    RECT rectWindow;
    GetWindowRect(hwnd, &rectWindow);

    // Calculate window dimensions
    int windowWidth = rectWindow.right - rectWindow.left;
    int windowHeight = rectWindow.bottom - rectWindow.top;

    // Calculate the centered position
    int posX = (rectScreen.left + (rectScreen.right - rectScreen.left - windowWidth)) >> 1;
    int posY = (rectScreen.top + (rectScreen.bottom - rectScreen.top - windowHeight)) >> 1;

    // Move the window to the center position
    SetWindowPos(hwnd, nullptr, posX, posY, 0, 0, SWP_NOZORDER | SWP_NOSIZE);
}

// Cleanup the window and unregister the window class
void SystemUtils::DestroySystemWindow(HINSTANCE hInstance, HWND hwnd, LPCWSTR classname) {
    if (hwnd) {
        DestroyWindow(hwnd);
    }
    UnregisterClass(classname, hInstance);
}

// Function to disable the cursor
void SystemUtils::DisableMouseCursor()
{
    // Hide the mouse cursor
    ShowCursor(FALSE);
}

// Function to Enable the cursor
void SystemUtils::EnableMouseCursor()
{
    // Show the mouse cursor
    ShowCursor(TRUE);
}

RECT SystemUtils::GetSystemWindowSize(HWND hWnd)
{
    RECT clientRect;
    GetClientRect(hWnd, &clientRect);
    return clientRect;
}

// Function to scale mouse coordinates
std::tuple<int, int> SystemUtils::ScaleMouseCoordinates(int originalX, int originalY, int originalWidth, int originalHeight, int newWidth, int newHeight)
{
    if (originalWidth == 0 || originalHeight == 0) {
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

    // Log the scaling process for debugging
/*
    std::wstring logMessage = L"Scaling mouse coordinates from (" +
        std::to_wstring(originalX) + L", " + std::to_wstring(originalY) + L") at resolution " +
        std::to_wstring(originalWidth) + L"x" + std::to_wstring(originalHeight) + L" to (" +
        std::to_wstring(scaledX) + L", " + std::to_wstring(scaledY) + L") at resolution " +
        std::to_wstring(newWidth) + L"x" + std::to_wstring(newHeight) + L").";
    Debug::logLevelMessage(LogLevel::LOG_INFO, logMessage);
*/
    
    // Return the scaled coordinates
    return std::make_tuple(scaledX, scaledY);
}

std::wstring SystemUtils::GetExecutableVersion() {
    // Get the path of the current executable
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);

    DWORD dummy;
    DWORD size = GetFileVersionInfoSizeW(exePath, &dummy);
    if (size == 0) {
        return L"Failed to retrieve version info. Error: " + std::to_wstring(GetLastError());
    }

    std::vector<BYTE> versionData(size);
    if (!GetFileVersionInfoW(exePath, 0, size, versionData.data())) {
        return L"Failed to get file version info. Error: " + std::to_wstring(GetLastError());
    }

    void* pVersion = nullptr;
    UINT versionLen = 0;
    if (!VerQueryValueW(versionData.data(), L"\\", &pVersion, &versionLen)) {
        return L"Failed to query version data.";
    }

    VS_FIXEDFILEINFO* versionInfo = static_cast<VS_FIXEDFILEINFO*>(pVersion);
    if (versionInfo->dwSignature != 0xFEEF04BD) {
        return L"Invalid version signature.";
    }

    DWORD major = (versionInfo->dwFileVersionMS >> 16) & 0xFFFF;
    DWORD minor = (versionInfo->dwFileVersionMS) & 0xFFFF;
    DWORD build = (versionInfo->dwFileVersionLS >> 16) & 0xFFFF;
    DWORD revision = (versionInfo->dwFileVersionLS) & 0xFFFF;

    std::wstring versionString = std::wstring(L"Build Version: ") +
        std::to_wstring(major) + L"." +
        std::to_wstring(minor) + L"." +
        std::to_wstring(build) + L"." +
        std::to_wstring(revision);

    return std::wstring(versionString);
}

// This function retrieves comprehensive window metrics for a specified window handle in Windows 11
// Returns true if successful, false otherwise
bool SystemUtils::GetWindowMetrics(HWND hWnd, WindowMetrics& outMetrics)
{
    // Validate the window handle - return early if invalid
    if (!IsWindow(hWnd)) {
        // Log error for invalid window handle
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"Invalid window handle provided to GetWindowMetrics");
        return false;
    }

    // Initialize output struct with zeros
    ZeroMemory(&outMetrics, sizeof(WindowMetrics));

    // Store the provided window handle
    outMetrics.hWnd = hWnd;

    // ----------- Window Rectangle (in screen coordinates) -----------
    // Get the window rectangle (entire window including non-client area)
    RECT windowRect;
    if (!GetWindowRect(hWnd, &windowRect)) {
        // Log error if GetWindowRect fails
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"GetWindowRect failed with error code: %d", GetLastError());
        return false;
    }

    // Store window position and size
    outMetrics.x = windowRect.left;
    outMetrics.y = windowRect.top;
    outMetrics.width = windowRect.right - windowRect.left;
    outMetrics.height = windowRect.bottom - windowRect.top;

    // ----------- Client Area Rectangle (in client coordinates) -----------
    // Get the client area rectangle (area inside the window borders)
    RECT clientRect;
    if (!GetClientRect(hWnd, &clientRect)) {
        // Log error if GetClientRect fails
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"GetClientRect failed with error code: %d", GetLastError());
        return false;
    }

    // Store client area size
    outMetrics.clientWidth = clientRect.right - clientRect.left;
    outMetrics.clientHeight = clientRect.bottom - clientRect.top;

    // ----------- Non-Client Area Measurements -----------
    // Calculate non-client area dimensions (window frame, title bar, etc.)
    outMetrics.borderWidth = (outMetrics.width - outMetrics.clientWidth) / 2;
    outMetrics.titleBarHeight = outMetrics.height - outMetrics.clientHeight - outMetrics.borderWidth;

    // ----------- DPI Information -----------
    // Get DPI for the window (Windows 10+ API)
    UINT dpi = GetDpiForWindow(hWnd);
    outMetrics.dpi = dpi;
    outMetrics.dpiScaleFactor = static_cast<float>(dpi) / 96.0f; // 96 is the default DPI

    // ----------- System Metrics (adjusted for DPI) -----------
    // Get various system metrics and adjust them for the window's DPI
    // These values are more reliable than calculating ourselves

    // Get border width from system metrics (adjusted for DPI)
    int sysMetricBorderX = GetSystemMetricsForDpi(SM_CXSIZEFRAME, dpi);
    int sysMetricBorderY = GetSystemMetricsForDpi(SM_CYSIZEFRAME, dpi);
    outMetrics.systemBorderWidth = sysMetricBorderX;
    outMetrics.systemBorderHeight = sysMetricBorderY;

    // Get title bar height from system metrics (adjusted for DPI)
    outMetrics.systemTitleBarHeight = GetSystemMetricsForDpi(SM_CYCAPTION, dpi);

    // Get menu bar height if present
    BOOL hasMenu = (GetMenu(hWnd) != NULL);
    outMetrics.menuBarHeight = hasMenu ? GetSystemMetricsForDpi(SM_CYMENU, dpi) : 0;

    // ----------- Window State -----------
    // Determine if window is maximized, minimized, or normal
    WINDOWPLACEMENT wp;
    wp.length = sizeof(WINDOWPLACEMENT);

    if (GetWindowPlacement(hWnd, &wp)) {
        switch (wp.showCmd) {
        case SW_MAXIMIZE:
            outMetrics.isMaximized = true;
            break;
        case SW_MINIMIZE:
        case SW_SHOWMINIMIZED:
            outMetrics.isMinimized = true;
            break;
        default:
            // Normal state
            break;
        }
    }

    // Check if window has specific extended styles
    DWORD exStyle = GetWindowLong(hWnd, GWL_EXSTYLE);
    outMetrics.hasToolWindowBorder = (exStyle & WS_EX_TOOLWINDOW) != 0;
    outMetrics.hasDialogFrame = (exStyle & WS_EX_DLGMODALFRAME) != 0;

    // ----------- Monitor Information -----------
    // Get monitor information for the window
    HMONITOR hMonitor = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
    if (hMonitor) {
        MONITORINFO monitorInfo;
        monitorInfo.cbSize = sizeof(MONITORINFO);

        if (GetMonitorInfo(hMonitor, &monitorInfo)) {
            // Store monitor work area and full area
            outMetrics.monitorWorkArea = monitorInfo.rcWork;
            outMetrics.monitorFullArea = monitorInfo.rcMonitor;
            outMetrics.isPrimaryMonitor = (monitorInfo.dwFlags & MONITORINFOF_PRIMARY) != 0;
        }
    }

    // Log success with window dimensions
    debug.logDebugMessage(LogLevel::LOG_INFO, L"Window metrics retrieved successfully: %dx%d (client: %dx%d)",
        outMetrics.width, outMetrics.height, outMetrics.clientWidth, outMetrics.clientHeight);

    return true;
}