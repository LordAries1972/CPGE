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

// ===== --------------------------------------------------------------------------------- =====
// Name: SystemUtils::GetPrimaryMonitorFullScreenSize()
// 
// This function retrieves the full screen dimensions of the primary monitor
// Returns a tuple containing (width, height) of the primary monitor's full screen area
// This is optimized for gaming applications requiring precise monitor dimensions
// ===== --------------------------------------------------------------------------------- =====
std::tuple<int, int> SystemUtils::GetPrimaryMonitorFullScreenSize()
{
#if defined(_DEBUG_WINSYSTEM_)
    // Log function entry for debugging purposes
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"GetPrimaryMonitorFullScreenSize() - Retrieving primary monitor dimensions");
#endif

    // Get the primary monitor handle - this is the main display
    HMONITOR hPrimaryMonitor = MonitorFromPoint({ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);

    // Validate that we successfully obtained a monitor handle
    if (!hPrimaryMonitor) {
        // Log critical error if primary monitor cannot be found
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"Failed to obtain primary monitor handle");
        return std::make_tuple(0, 0);
    }

    // Initialize monitor info structure for querying monitor properties
    MONITORINFO monitorInfo;
    monitorInfo.cbSize = sizeof(MONITORINFO);  // Set structure size for version compatibility

    // Query the monitor information using the obtained handle
    if (!GetMonitorInfo(hPrimaryMonitor, &monitorInfo)) {
        // Log error with Windows error code for debugging
        DWORD errorCode = GetLastError();
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"GetMonitorInfo failed with error code: %d", errorCode);
        return std::make_tuple(0, 0);
    }

    // Extract full monitor dimensions from the monitor rectangle
    // rcMonitor contains the full screen area including taskbar and other system UI
    int fullWidth = monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left;
    int fullHeight = monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top;

    // Validate that we obtained valid dimensions (positive values)
    if (fullWidth <= 0 || fullHeight <= 0) {
        // Log warning for invalid monitor dimensions
        debug.logDebugMessage(LogLevel::LOG_WARNING, L"Invalid monitor dimensions detected: %dx%d", fullWidth, fullHeight);
        return std::make_tuple(0, 0);
    }

#if defined(_DEBUG_WINSYSTEM_)
    // Log successful retrieval with dimensions for debugging
    debug.logDebugMessage(LogLevel::LOG_INFO, L"Primary monitor full screen size: %dx%d pixels", fullWidth, fullHeight);

    // Also log work area for comparison (area excluding taskbar)
    int workWidth = monitorInfo.rcWork.right - monitorInfo.rcWork.left;
    int workHeight = monitorInfo.rcWork.bottom - monitorInfo.rcWork.top;
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Primary monitor work area size: %dx%d pixels", workWidth, workHeight);
#endif

    // Return the full screen dimensions as a tuple for easy unpacking
    return std::make_tuple(fullWidth, fullHeight);
}

// ===== --------------------------------------------------------------------------------- =====
// Name: SystemUtils::Is64BitOperatingSystem()
// 
// This function determines if the current operating system is running on a 64-bit architecture
// Returns true if the OS is 64-bit, false if 32-bit or if determination fails
// This is critical for gaming applications that need to know system architecture capabilities
// ===== --------------------------------------------------------------------------------- =====
bool SystemUtils::Is64BitOperatingSystem()
{
#if defined(_DEBUG_WINSYSTEM_)
    // Log function entry for debugging purposes
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Is64BitOperatingSystem() - Checking OS architecture");
#endif

    // Method 1: Check if we're running as a 64-bit process
    // If our process is 64-bit, then the OS must be 64-bit
#ifdef _WIN64
    // We are compiled as 64-bit, so OS must be 64-bit
#if defined(_DEBUG_WINSYSTEM_)
    debug.logDebugMessage(LogLevel::LOG_INFO, L"Process compiled as 64-bit - OS is definitely 64-bit");
#endif
    return true;
#else
    // We are compiled as 32-bit, but OS could still be 64-bit
    // Need to check if we're running under WOW64 (Windows on Windows 64)

    // Declare function pointer for IsWow64Process
    typedef BOOL(WINAPI* LPFN_ISWOW64PROCESS) (HANDLE, PBOOL);
    LPFN_ISWOW64PROCESS fnIsWow64Process;

    // Get handle to current process
    HANDLE hProcess = GetCurrentProcess();
    BOOL bIsWow64 = FALSE;

    // Get the IsWow64Process function from kernel32.dll
    fnIsWow64Process = (LPFN_ISWOW64PROCESS)GetProcAddress(
        GetModuleHandle(TEXT("kernel32")), "IsWow64Process");

    // Check if IsWow64Process function is available (Windows XP SP2 and later)
    if (fnIsWow64Process != NULL) {
        // Call IsWow64Process to determine if we're running under WOW64
        if (!fnIsWow64Process(hProcess, &bIsWow64)) {
            // Function call failed - log error and return false as fallback
            DWORD errorCode = GetLastError();
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"IsWow64Process failed with error code: %d", errorCode);
            return false;
        }

        // If bIsWow64 is TRUE, we're a 32-bit process running on 64-bit OS
        if (bIsWow64) {
#if defined(_DEBUG_WINSYSTEM_)
            debug.logDebugMessage(LogLevel::LOG_INFO, L"32-bit process running under WOW64 - OS is 64-bit");
#endif
            return true;
        }
        else {
#if defined(_DEBUG_WINSYSTEM_)
            debug.logDebugMessage(LogLevel::LOG_INFO, L"32-bit process on native 32-bit OS");
#endif
            return false;
        }
    }
    else {
        // IsWow64Process not available - we're on very old Windows (pre-XP SP2)
        // These systems don't support 64-bit, so return false
#if defined(_DEBUG_WINSYSTEM_)
        debug.logDebugMessage(LogLevel::LOG_WARNING, L"IsWow64Process not available - assuming 32-bit OS");
#endif
        return false;
    }
#endif
}

// ===== --------------------------------------------------------------------------------- =====
// Name: SystemUtils::GetProcessorArchitecture()
// 
// This function returns a detailed string describing the processor architecture
// Returns architecture string (e.g., "AMD64", "x86", "ARM64", etc.)
// Useful for detailed system information and compatibility checking
// ===== --------------------------------------------------------------------------------- =====
std::wstring SystemUtils::GetProcessorArchitecture()
{
#if defined(_DEBUG_WINSYSTEM_)
    // Log function entry for debugging purposes
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"GetProcessorArchitecture() - Determining processor architecture");
#endif

    // Get system information structure
    SYSTEM_INFO systemInfo;

    // Use GetNativeSystemInfo to get actual hardware architecture
    // (not affected by WOW64 emulation layer)
    GetNativeSystemInfo(&systemInfo);

    // Convert processor architecture to readable string
    std::wstring architecture;

    switch (systemInfo.wProcessorArchitecture) {
    case PROCESSOR_ARCHITECTURE_AMD64:
        // 64-bit Intel/AMD processors
        architecture = L"AMD64 (x86-64)";
        break;

    case PROCESSOR_ARCHITECTURE_INTEL:
        // 32-bit Intel/AMD processors
        architecture = L"x86 (32-bit)";
        break;

    case PROCESSOR_ARCHITECTURE_ARM:
        // ARM processors (32-bit)
        architecture = L"ARM (32-bit)";
        break;

    case PROCESSOR_ARCHITECTURE_ARM64:
        // ARM processors (64-bit)
        architecture = L"ARM64 (64-bit)";
        break;

    case PROCESSOR_ARCHITECTURE_IA64:
        // Intel Itanium processors (64-bit)
        architecture = L"IA64 (Itanium 64-bit)";
        break;

    case PROCESSOR_ARCHITECTURE_UNKNOWN:
    default:
        // Unknown or unsupported architecture
        architecture = L"Unknown";
        debug.logDebugMessage(LogLevel::LOG_WARNING, L"Unknown processor architecture detected: %d",
            systemInfo.wProcessorArchitecture);
        break;
    }

#if defined(_DEBUG_WINSYSTEM_)
    // Log the detected architecture
    debug.logDebugMessage(LogLevel::LOG_INFO, L"Processor architecture: %ls", architecture.c_str());

    // Log additional processor information
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Number of processors: %d", systemInfo.dwNumberOfProcessors);
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"Page size: %d bytes", systemInfo.dwPageSize);
#endif

    return architecture;
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