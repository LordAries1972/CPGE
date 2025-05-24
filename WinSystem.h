#pragma once

enum WindowsVersion
{
    WINVER_UNKNOWN = 0,								        // Unsupported Version
	WINVER_WIN7 = 1,								        // Windows Version 7    (DirectX11.0 Minimum with SP1 installed to get 11.1)
	WINVER_WIN8 = 2,								        // Windows Version 8
	WINVER_WIN8_1 = 3,								        // Windows Version 8.1  (DirectX11.1) supported.
	WINVER_WIN10 = 4,								        // Windows Version 10   (DirectX11.1/12 Minimum Requirement)
	WINVER_WIN11 = 5,								        // Windows Version 11.  (DirectX12)
};

// Structure to hold comprehensive window metrics information
struct WindowMetrics {
    // Window handle
    HWND hWnd;
    bool isFullScreen;

    // Window position and size (screen coordinates)
    int x;
    int y;
    int width;
    int height;

    // Client area size (client coordinates)
    int clientWidth;
    int clientHeight;

    // Borders and non-client areas (calculated)
    int borderWidth;
    int titleBarHeight;

    // DPI information
    UINT dpi;
    float dpiScaleFactor;

    // System metrics (from GetSystemMetricsForDpi)
    int systemBorderWidth;
    int systemBorderHeight;
    int systemTitleBarHeight;
    int menuBarHeight;

    // Window state
    bool isMaximized;
    bool isMinimized;
    bool hasToolWindowBorder;
    bool hasDialogFrame;

    // Monitor information
    RECT monitorWorkArea;
    RECT monitorFullArea;
    bool isPrimaryMonitor;

    // Constructor - initialize with default values
    WindowMetrics() :
        hWnd(NULL), isFullScreen(false),
        x(0), y(0), width(0), height(0),
        clientWidth(0), clientHeight(0),
        borderWidth(0), titleBarHeight(0),
        dpi(96), dpiScaleFactor(1.0f),
        systemBorderWidth(0), systemBorderHeight(0),
        systemTitleBarHeight(0), menuBarHeight(0),
        isMaximized(false), isMinimized(false),
        hasToolWindowBorder(false), hasDialogFrame(false),
        isPrimaryMonitor(false)
    {
        ZeroMemory(&monitorWorkArea, sizeof(RECT));
        ZeroMemory(&monitorFullArea, sizeof(RECT));
    }
}; 

class SystemUtils
{
public:
	SystemUtils();
	~SystemUtils();

	WindowsVersion GetWindowsVersion();
	void CenterSystemWindow(HWND hwnd);
	void DisableMouseCursor();
	void EnableMouseCursor();
	RECT GetSystemWindowSize(HWND hWnd);
	void DestroySystemWindow(HINSTANCE hInstance, HWND hwnd, LPCWSTR classname);
	void GetMessageAndProcess();
	void ProcessMessages();
	bool IsWindowMinimized();
    bool GetWindowMetrics(HWND hWnd, WindowMetrics& outMetrics);
    bool Is64BitOperatingSystem();
    std::wstring GetProcessorArchitecture();

    std::tuple<int, int> GetPrimaryMonitorFullScreenSize();

	std::wstring Get_Current_Directory();
	std::wstring ToWString(const std::string& input);
	std::wstring widen(const std::string& str);
	std::wstring StripQuotes(const std::wstring& input);
	std::wstring GetExecutableVersion();
	std::tuple<int, int> ScaleMouseCoordinates(int originalX, int originalY, int originalWidth, int originalHeight, int newWidth, int newHeight);

private:

};
