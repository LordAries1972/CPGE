#include "Includes.h"
#include "Renderer.h"
#include "Configuration.h"
#include "Debug.h"

#if defined(__USE_DIRECTX_11__)
    #include "DX11Renderer.h"
#endif
#if defined(__USE_DIRECTX_12__)
    #include "DX12Renderer.h"
    #include <d3d12.h>
    #include <dxgi1_4.h>
    #pragma comment(lib, "d3d12.lib")
    #pragma comment(lib, "dxgi.lib")
#endif
#if defined(__USE_OPENGL__)
    #include "OpenGLRenderer.h"
#endif
#if defined(__USE_VULKAN__)
    #include "VULKAN_Renderer.h"
#endif

std::shared_ptr<Renderer> renderer;

// -----------------------------------------------------------------------
// Per-renderer hardware support checks
// Called BEFORE instantiating the renderer so the user gets a clear error
// message if their system cannot run the selected pipeline.
// -----------------------------------------------------------------------
#if defined(PLATFORM_WINDOWS)

#if defined(__USE_DIRECTX_11__)
static bool IsDX11Supported()
{
    D3D_FEATURE_LEVEL fl;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        0, nullptr, 0, D3D11_SDK_VERSION, nullptr, &fl, nullptr);
    return SUCCEEDED(hr) && fl >= D3D_FEATURE_LEVEL_11_0;
}
#endif

#if defined(__USE_DIRECTX_12__)
static bool IsDX12Supported()
{
    Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) return false;

    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
    for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i)
    {
        DXGI_ADAPTER_DESC1 desc;
        adapter->GetDesc1(&desc);
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) { adapter.Reset(); continue; }

        if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0,
                                        _uuidof(ID3D12Device), nullptr)))
            return true;

        adapter.Reset();
    }
    return false;
}
#endif

static void ShowUnsupportedRendererMessage(const wchar_t* rendererName)
{
    std::wstring msg = std::wstring(rendererName) +
        L" is not supported on your system, or the required drivers are missing!\n\n"
        L"The Game will NOW terminate!";
    MessageBoxW(nullptr, msg.c_str(), L"Unsupported Render Pipeline", MB_OK | MB_ICONERROR);
}

#endif // PLATFORM_WINDOWS

int CreateRendererInstance()
{
    const int rendererType = Configuration::ValidateRendererForPlatform(config.myConfig.rendererType);

#if defined(PLATFORM_WINDOWS)
    // Windows: 0=DirectX 11  1=DirectX 12  2=OpenGL  3=Vulkan
    switch (rendererType)
    {
    #if defined(__USE_DIRECTX_11__)
        case 0:
        {
            if (!IsDX11Supported())
            {
                debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"[Renderer] DirectX 11 is not supported on this system.");
                ShowUnsupportedRendererMessage(L"DirectX 11");
                return EXIT_FAILURE;
            }
            renderer = std::make_shared<DX11Renderer>();
            break;
        }
    #endif
    #if defined(__USE_DIRECTX_12__)
        case 1:
        {
            if (!IsDX12Supported())
            {
                debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"[Renderer] DirectX 12 is not supported on this system.");
                ShowUnsupportedRendererMessage(L"DirectX 12");
                return EXIT_FAILURE;
            }
            renderer = std::make_shared<DX12Renderer>();
            break;
        }
    #endif
    #if defined(__USE_OPENGL__)
        case 2: renderer = std::make_shared<OpenGLRenderer>();  break;
    #endif
    #if defined(__USE_VULKAN__)
        case 3: renderer = std::make_shared<VulkanRenderer>();  break;
    #endif
        default:
            debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"[Renderer] No valid renderer selected for Windows platform.");
            return EXIT_FAILURE;
    }
#elif defined(PLATFORM_LINUX) || defined(PLATFORM_ANDROID)
    // Linux / Android: 0=OpenGL  1=Vulkan
    switch (rendererType)
    {
    #if defined(__USE_OPENGL__)
        case 0: renderer = std::make_shared<OpenGLRenderer>();  break;
    #endif
    #if defined(__USE_VULKAN__)
        case 1: renderer = std::make_shared<VulkanRenderer>();  break;
    #endif
        default:
            debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"[Renderer] No valid renderer selected for Linux/Android platform.");
            return EXIT_FAILURE;
    }
#elif defined(PLATFORM_APPLE) || defined(PLATFORM_IOS)
    // macOS / iOS: OpenGL only
    #if defined(__USE_OPENGL__)
        renderer = std::make_shared<OpenGLRenderer>();
    #else
        debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"[Renderer] OpenGL is required but not compiled in for this Apple platform.");
        return EXIT_FAILURE;
    #endif
#else
    debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"[Renderer] Unknown platform — no renderer can be selected.");
    return EXIT_FAILURE;
#endif

    if (!renderer)
    {
        debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"[Renderer] Renderer instance creation failed.");
        return EXIT_FAILURE;
    }

    debug.logLevelMessage(LogLevel::LOG_INFO, L"[Renderer] Renderer instance created successfully.");
    return EXIT_SUCCESS;
}
