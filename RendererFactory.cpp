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

// -----------------------------------------------------------------------
// Renderer::DrawFilledPolygon - portable scanline fallback
// Fills a CONVEX polygon with 1-pixel-high spans drawn through DrawRectangle, which every
// backend implements.  For each pixel row we intersect the row centre with every edge and keep
// the left-most / right-most crossing; convexity guarantees one span per row.  Backends should
// override this with native geometry - it is only the safety net.
// -----------------------------------------------------------------------
void Renderer::DrawFilledPolygon(const Vector2* points, int count, const MyColor& color)
{
    if (!points || count < 3) return;

    // Vertical extent of the polygon, clipped to non-negative rows.
    float minY = points[0].y, maxY = points[0].y;
    for (int i = 1; i < count; ++i)
    {
        if (points[i].y < minY) minY = points[i].y;
        if (points[i].y > maxY) maxY = points[i].y;
    }
    int rowStart = static_cast<int>(std::ceil(minY - 0.5f));
    const int rowEnd = static_cast<int>(std::floor(maxY - 0.5f));
    if (rowStart < 0) rowStart = 0;

    for (int row = rowStart; row <= rowEnd; ++row)
    {
        const float cy = static_cast<float>(row) + 0.5f;                  // sample at the pixel-row centre
        float left = 1e30f, right = -1e30f;
        for (int i = 0; i < count; ++i)
        {
            const Vector2& a = points[i];
            const Vector2& b = points[(i + 1) % count];
            // Edge straddles the sample line (half-open so shared vertices are counted once).
            if ((a.y <= cy && b.y > cy) || (b.y <= cy && a.y > cy))
            {
                const float x = a.x + (cy - a.y) * (b.x - a.x) / (b.y - a.y);
                if (x < left)  left = x;
                if (x > right) right = x;
            }
        }
        if (right > left)
            DrawRectangle(Vector2(left, static_cast<float>(row)), Vector2(right - left, 1.0f), color, true);
    }
}

// Portable batch fallback: one DrawFilledPolygon per polygon.
void Renderer::DrawFilledPolygonBatch(const Vector2* const* polys, const int* counts, int n, const MyColor& color)
{
    for (int i = 0; i < n; ++i) DrawFilledPolygon(polys[i], counts[i], color);
}

// Portable ellipse fallback: a regular polygon, rotated about the centre (identical to the polygon the callers used to build themselves).
void Renderer::DrawFilledEllipse(float cx, float cy, float rx, float ry, float rotRad, const MyColor& color, int segments)
{
    if (segments < 3) segments = 3;
    if (segments > 48) segments = 48;
    Vector2 pts[48];
    const float cr = std::cos(rotRad), sr = std::sin(rotRad);
    for (int i = 0; i < segments; ++i)
    {
        const float a = 6.2831853f * static_cast<float>(i) / static_cast<float>(segments), px = rx * std::cos(a), py = ry * std::sin(a);
        pts[i] = Vector2(cx + px * cr - py * sr, cy + px * sr + py * cr);
    }
    DrawFilledPolygon(pts, segments, color);
}

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

#if   defined(__USE_DIRECTX_12__)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"[Renderer] DirectX 12 renderer instance successfully initialised.");
#elif defined(__USE_DIRECTX_11__)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"[Renderer] DirectX 11 renderer instance successfully initialised.");
#elif defined(__USE_VULKAN__)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"[Renderer] Vulkan renderer instance successfully initialised.");
#elif defined(__USE_OPENGL__)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"[Renderer] OpenGL renderer instance successfully initialised.");
#else
    debug.logLevelMessage(LogLevel::LOG_INFO, L"[Renderer] Renderer instance successfully initialised.");
#endif
    return EXIT_SUCCESS;
}
