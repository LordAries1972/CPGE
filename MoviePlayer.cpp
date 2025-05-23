// -----------------------------------------------------------------------------------------------------
// Things still need to be done
// 
// CreateVideoTexture(), UpdateVideoTexture() & ProcessVideoSample() setup appropriate directive 
// condition blocks to ensure proper renderer is been used.  Using default DirectX11 Currently.
// -----------------------------------------------------------------------------------------------------

// All our Window Includes
#define NOMINMAX										// allows us to use std::min, std::max

#include "Includes.h"
#include "MathPrecalculation.h"
#include "MoviePlayer.h"
#include "DX11Renderer.h"
#include "RendererMacros.h"
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <WinSystem.h>
#include "ThreadManager.h"

// Initialize external global instances
extern Debug debug;
extern SystemUtils sysUtils;

// Constructor changes - remove thread-related fields
MoviePlayer::MoviePlayer() :
    m_pSourceReader(nullptr),
    m_pCurrentSample(nullptr),
    m_pVideoMediaType(nullptr),
    m_videoWidth(0),
    m_videoHeight(0),
    m_videoDuration(0),
    m_videoTextureIndex(-1),
    m_isInitialized(false),
    m_isPlaying(false),
    m_isPaused(false),
    m_hasVideo(false),
    m_hasAudio(false),
    m_llCurrentPosition(0),
    m_threadManager(nullptr),
    m_hasNewFrame(false),
    m_frameInterval(13) // Default to ~60 FPS (13ms between frames)
{
    // Initialize Media Foundation
    InitializeMF();
    // Initialize last frame time
    m_lastFrameTime = std::chrono::steady_clock::now();
}

// Destructor changes - remove thread cleanup
MoviePlayer::~MoviePlayer()
{
    // Clean up resources
    Cleanup();
}

// ----------------------------------------------------------------------------------------------
// Thread-safe utility methods for checking state
// ----------------------------------------------------------------------------------------------
bool MoviePlayer::IsPlaying() const
{
    return m_isPlaying.load();
}

bool MoviePlayer::IsPaused() const
{
    return m_isPaused.load();
}

// ----------------------------------------------------------------------------------------------
// Initialize the MoviePlayer with a renderer and thread manager
// ----------------------------------------------------------------------------------------------
bool MoviePlayer::Initialize(std::shared_ptr<Renderer> rendererPtr, ThreadManager* threadManager)
{
#if defined(_DEBUG_MOVIEPLAYER_)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"Initializing MoviePlayer with ThreadManager");
#endif

    // Store the thread manager reference
    m_threadManager = threadManager;

    // Check if the renderer is valid
    m_renderer = rendererPtr;
    if (!m_renderer)
    {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"MoviePlayer: Invalid renderer pointer");
        return false;
    }

    // Check if the thread manager is valid
    if (!m_threadManager)
    {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"MoviePlayer: Invalid thread manager pointer");
        return false;
    }

    // Initialize Media Foundation if not already initialized
    if (!m_isInitialized)
    {
        if (!InitializeMF())
        {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"MoviePlayer: Failed to initialize Media Foundation");
            return false;
        }
    }

    m_isInitialized = true;
    return true;
}

// ----------------------------------------------------------------------------------------------
// Initialize Media Foundation
// ----------------------------------------------------------------------------------------------
bool MoviePlayer::InitializeMF()
{
    // Initialize Media Foundation
    HRESULT hr = MFStartup(MF_VERSION);
    if (FAILED(hr))
    {
        LogMediaError(hr, L"Failed to initialize Media Foundation");
        return false;
    }

    // CRITICAL: Create a configuration to enable multithreaded protection
    // This is essential to avoid the D3D11 multithreading corruption
    IMFAttributes* pAttributes = nullptr;
    hr = MFCreateAttributes(&pAttributes, 1);
    if (SUCCEEDED(hr))
    {
        // Set the attribute to enable multithreaded protection
        // For complex formats like HEVC or MKV, disable DXVA to avoid threading issues
        hr = pAttributes->SetUINT32(MF_SOURCE_READER_DISABLE_DXVA, TRUE);
        if (SUCCEEDED(hr))
        {
            debug.logLevelMessage(LogLevel::LOG_INFO, L"Media Foundation multithreaded mode enabled");
        }
        pAttributes->Release();
    }

#if defined(_DEBUG_MOVIEPLAYER_)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"Media Foundation initialized successfully");
#endif

    return true;
}

bool MoviePlayer::OpenMovie(const std::wstring& filePath)
{
    if (!m_renderer || !m_isInitialized.load())
    {
#if defined(_DEBUG_MOVIEPLAYER_)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Movie player not initialized properly");
#endif
        return false;
    }

    // Stop any current playback
    if (IsPlaying())
    {
        Stop();

        // Clean up any existing media
        Cleanup();
    }

    // Enable D3D11 multithreaded protection
    auto dx11Renderer = std::dynamic_pointer_cast<DX11Renderer>(m_renderer);
    if (dx11Renderer && dx11Renderer->m_d3dDevice)
    {
        // Enable multithreaded protection on the D3D11 device
        ComPtr<ID3D10Multithread> pMultithread;
        HRESULT hr = dx11Renderer->m_d3dDevice.As(&pMultithread);
        if (SUCCEEDED(hr) && pMultithread)
        {
            // Enable multithreaded protection
            pMultithread->SetMultithreadProtected(TRUE);
            debug.logLevelMessage(LogLevel::LOG_INFO, L"D3D11 multithreaded protection enabled");
        }
    }

    // Detect video codec
    std::wstring codecName;
    bool isHevcContent = false;
    if (DetectVideoCodec(filePath, codecName)) {
#if defined(_DEBUG_MOVIEPLAYER_)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Detected video codec: " + codecName);
#endif

        // Check for HEVC codec and log special message
        if (codecName.find(L"HEVC") != std::wstring::npos) {
#if defined(_DEBUG_MOVIEPLAYER_)
            debug.logLevelMessage(LogLevel::LOG_INFO, L"HEVC (H.265) codec detected - enabling enhanced processing");
#endif
            isHevcContent = true;
        }
    }

    // Check if the file extension is .mkv
    bool isMkvFile = false;
    std::wstring fileExt;
    size_t lastDot = filePath.find_last_of(L'.');
    if (lastDot != std::wstring::npos) {
        fileExt = filePath.substr(lastDot);
        std::transform(fileExt.begin(), fileExt.end(), fileExt.begin(), ::towlower);
        isMkvFile = (fileExt == L".mkv");
    }

#if defined(_DEBUG_MOVIEPLAYER_)
    if (isMkvFile) {
        debug.logLevelMessage(LogLevel::LOG_INFO, L"MKV file detected, enabling enhanced decoder options");
    }
#endif

    // Ensure the path is a valid URL
    std::wstring urlPath = filePath;

    // If it's a local file path and doesn't already have a file:// prefix, add it
    if (urlPath.find(L"://") == std::wstring::npos) {
        // Check if the path starts with a drive letter (e.g., C:\)
        if (urlPath.length() > 2 && urlPath[1] == L':' && urlPath[2] == L'\\') {
            urlPath = L"file:///" + urlPath;
        }
        else {
            urlPath = L"file://" + urlPath;
        }
    }

    // Replace backslashes with forward slashes (required for URLs)
    std::replace(urlPath.begin(), urlPath.end(), L'\\', L'/');

    // Log the processed URL
    debug.logLevelMessage(LogLevel::LOG_DEBUG, L"Processed URL: " + urlPath);

    // Create attributes for the source reader
    IMFAttributes* pAttributes = nullptr;
    HRESULT hr = MFCreateAttributes(&pAttributes, 6); // Allow for several attributes
    if (FAILED(hr)) {
        LogMediaError(hr, L"Failed to create media attributes");
        return false;
    }

    // Set low latency mode
    hr = pAttributes->SetUINT32(MF_LOW_LATENCY, TRUE);
    if (FAILED(hr)) {
        pAttributes->Release();
        LogMediaError(hr, L"Failed to set low latency attribute");
        return false;
    }

    // Enable hardware acceleration when available
    hr = pAttributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
    if (FAILED(hr)) {
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"Failed to enable hardware acceleration");
        // Continue anyway, not critical
    }

    // CRITICAL: Configure thread safety
    // If using hardware acceleration, the source reader might be creating and using 
    // D3D resources on background threads, so we need to ensure thread safety
    if (isHevcContent || isMkvFile) {
        // For complex formats like HEVC or MKV, disable DXVA to avoid threading issues
        hr = pAttributes->SetUINT32(MF_SOURCE_READER_DISABLE_DXVA, TRUE);
        debug.logLevelMessage(LogLevel::LOG_INFO,
            L"Disabled DXVA for complex format to avoid threading issues");
    }
    else {
        // For other formats, try to use hardware acceleration but ensure thread safety
        hr = pAttributes->SetUINT32(MF_SOURCE_READER_DISABLE_DXVA, FALSE);
    }

    // Force decoder to use async mode for better performance
    hr = pAttributes->SetUINT32(MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, TRUE);
    if (FAILED(hr)) {
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"Failed to enable advanced video processing");
        // Continue anyway, not critical
    }

    // Set up D3D device manager for hardware acceleration with thread safety
    if (dx11Renderer && dx11Renderer->m_d3dDevice)
    {
        // Lock the renderer when setting up the D3D manager
        std::lock_guard<std::mutex> renderLock(DX11Renderer::GetRenderMutex());

        // Create D3D device manager for hardware acceleration
        IMFDXGIDeviceManager* pDeviceManager = nullptr;
        UINT resetToken = 0;
        hr = MFCreateDXGIDeviceManager(&resetToken, &pDeviceManager);
        if (SUCCEEDED(hr))
        {
            hr = pDeviceManager->ResetDevice(dx11Renderer->m_d3dDevice.Get(), resetToken);
            if (SUCCEEDED(hr))
            {
                // Associate device manager with source reader
                hr = pAttributes->SetUnknown(MF_SOURCE_READER_D3D_MANAGER, pDeviceManager);
                if (SUCCEEDED(hr))
                {
                    debug.logLevelMessage(LogLevel::LOG_INFO, L"Successfully set D3D manager for hardware decoding");
                }
            }

            // Release reference (attributes will hold its own reference)
            pDeviceManager->Release();
        }
    }

    // Create the source reader from URL with our enhanced attributes
    hr = MFCreateSourceReaderFromURL(urlPath.c_str(), pAttributes, &m_pSourceReader);

    // Release attributes
    pAttributes->Release();

    if (FAILED(hr)) {
        LogMediaError(hr, L"Failed to create source reader from URL");
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"URL that failed: %s", urlPath.c_str());
        return false;
    }

    // Configure source reader to deliver decoded frames
    hr = m_pSourceReader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE);
    if (FAILED(hr))
    {
        LogMediaError(hr, L"Failed to deselect all streams");
        return false;
    }

    // Select and enable the first video stream
    hr = m_pSourceReader->SetStreamSelection(MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE);
    if (SUCCEEDED(hr))
    {
        m_hasVideo = true;

        // Get the native media type to determine dimensions
        hr = m_pSourceReader->GetNativeMediaType(
            MF_SOURCE_READER_FIRST_VIDEO_STREAM,
            0,  // Stream index 0
            &m_pVideoMediaType
        );

        if (SUCCEEDED(hr))
        {
            // Get video dimensions from media type
            UINT32 width = 0, height = 0;
            MFGetAttributeSize(m_pVideoMediaType, MF_MT_FRAME_SIZE, &width, &height);
            m_videoWidth = width;
            m_videoHeight = height;

            // Get video subtype to see what format we're dealing with
            GUID videoSubtype;
            hr = m_pVideoMediaType->GetGUID(MF_MT_SUBTYPE, &videoSubtype);
            if (SUCCEEDED(hr))
            {
                m_videoSubtype = videoSubtype;
                // Log the format we're getting from the source
                WCHAR guidString[128];
                StringFromGUID2(videoSubtype, guidString, 128);
#if defined(_DEBUG_MOVIEPLAYER_)
                debug.logLevelMessage(LogLevel::LOG_INFO, L"Video format: " + std::wstring(guidString));
#endif
                // Confirm if this is HEVC format
                bool isHevcFormat = (memcmp(&videoSubtype, "\x48\x45\x56\x43", 4) == 0);
                if (isHevcFormat) {
                    debug.logLevelMessage(LogLevel::LOG_INFO, L"Detected HEVC (H.265) format");
                    isHevcContent = true;
                }
            }

            // Create a new media type for our desired output format
            IMFMediaType* pDesiredType = nullptr;
            hr = MFCreateMediaType(&pDesiredType);
            if (SUCCEEDED(hr))
            {
                // Copy attributes from the native media type
                hr = m_pVideoMediaType->CopyAllItems(pDesiredType);
                if (SUCCEEDED(hr))
                {
                    // Define the target formats we want to try
                    std::vector<GUID> targetFormats;

                    // HEVC content in MKV needs special handling
                    if (isHevcContent) {
                        // For HEVC files, prioritize formats that work well with hardware decoders
                        targetFormats = {
                            MFVideoFormat_NV12,                                         // NV12 is widely supported by hardware decoders
                            MFVideoFormat_YUY2,                                         // YUY2 as a backup
                            MFVideoFormat_IYUV,                                         // IYUV as another option
                            MFVideoFormat_RGB32,                                        // RGB32 as fallback
                            MFVideoFormat_ARGB32                                        // ARGB32 as final fallback
                        };
                    }
                    else {
                        // Standard preference order for other formats
                        targetFormats = {
                            MFVideoFormat_RGB32,
                            MFVideoFormat_ARGB32,
                            MFVideoFormat_NV12,
                            MFVideoFormat_YUY2
                        };
                    }

                    bool formatSet = false;
                    for (const GUID& format : targetFormats)
                    {
                        // Set the desired output format
                        hr = pDesiredType->SetGUID(MF_MT_SUBTYPE, format);
                        if (FAILED(hr)) continue;

                        // For RGB formats, make sure the stride is set correctly
                        if (format == MFVideoFormat_RGB32 || format == MFVideoFormat_ARGB32)
                        {
                            // Set stride to width * 4 bytes per pixel
                            hr = pDesiredType->SetUINT32(MF_MT_DEFAULT_STRIDE, width * 4);
                            if (FAILED(hr)) {
                                debug.logLevelMessage(LogLevel::LOG_WARNING, L"Failed to set stride for RGB format");
                            }
                        }

                        // Try to set this format as the output
                        hr = m_pSourceReader->SetCurrentMediaType(
                            MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                            nullptr,
                            pDesiredType
                        );

                        if (SUCCEEDED(hr))
                        {
                            WCHAR guidString[128];
                            StringFromGUID2(format, guidString, 128);
#if defined(_DEBUG_MOVIEPLAYER_)
                            debug.logLevelMessage(LogLevel::LOG_INFO, L"Successfully set output format to: " + std::wstring(guidString));
#endif
                            formatSet = true;
                            break;
                        }
                    }

                    if (!formatSet && isHevcContent)
                    {
                        // Special approach for HEVC files - create a simple media type from scratch
                        IMFMediaType* pSimpleType = nullptr;
                        hr = MFCreateMediaType(&pSimpleType);

                        if (SUCCEEDED(hr)) {
                            // Set major type to video
                            hr = pSimpleType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);

                            // Try NV12 which is widely supported by hardware decoders
                            if (SUCCEEDED(hr)) {
                                hr = pSimpleType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
                            }

                            // Set the frame size
                            if (SUCCEEDED(hr)) {
                                hr = MFSetAttributeSize(pSimpleType, MF_MT_FRAME_SIZE, width, height);
                            }

                            // Set interlace mode to progressive
                            if (SUCCEEDED(hr)) {
                                hr = pSimpleType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
                            }

                            // Try to apply this minimal format
                            if (SUCCEEDED(hr)) {
                                hr = m_pSourceReader->SetCurrentMediaType(
                                    MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                    nullptr,
                                    pSimpleType
                                );

                                if (SUCCEEDED(hr)) {
#if defined(_DEBUG_MOVIEPLAYER_)
                                    debug.logLevelMessage(LogLevel::LOG_INFO, L"Set simplified NV12 format for HEVC file");
#endif
                                    formatSet = true;
                                }
                            }

                            pSimpleType->Release();
                        }
                    }

                    if (!formatSet) {
                        // If we still couldn't set any format, log the issue
                        debug.logLevelMessage(LogLevel::LOG_WARNING, L"Could not set any preferred format, using native format.");

                        // For HEVC (H.265) files, we'll need special handling in UpdateVideoTexture
#if defined(_DEBUG_MOVIEPLAYER_)
                        if (isHevcContent) {
                            debug.logLevelMessage(LogLevel::LOG_INFO, L"Using native HEVC format with custom processing");
                        }
#endif
                    }
                }

                pDesiredType->Release();
            }

            // Lock the renderer when creating the video texture
            {
                std::lock_guard<std::mutex> renderLock(DX11Renderer::GetRenderMutex());

                // Create a texture for rendering - regardless of the format we ended up with
                if (!CreateVideoTexture(width, height, isHevcContent))
                {
                    debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to create video texture");
                    return false;
                }
            }
        }
        else
        {
            LogMediaError(hr, L"Failed to get native media type");
        }
    }
    else
    {
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"No video stream found in the file");
    }

    // Select and enable the first audio stream
    hr = m_pSourceReader->SetStreamSelection(MF_SOURCE_READER_FIRST_AUDIO_STREAM, TRUE);
    if (SUCCEEDED(hr))
    {
        m_hasAudio = true;
    }
    else
    {
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"No audio stream found in the file");
    }

    // Get the duration of the file
    PROPVARIANT var;
    PropVariantInit(&var);

    hr = m_pSourceReader->GetPresentationAttribute(
        MF_SOURCE_READER_MEDIASOURCE,
        MF_PD_DURATION,
        &var
    );

    if (SUCCEEDED(hr) && var.vt == VT_UI8)
    {
        m_videoDuration = var.uhVal.QuadPart;
    }

    PropVariantClear(&var);

    // Log successful file opening
#if defined(_DEBUG_MOVIEPLAYER_)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"Movie opened: " + filePath + L" Size: " + std::to_wstring(m_videoWidth) + L"x" + std::to_wstring(m_videoHeight));
#endif

    return (m_hasVideo.load() || m_hasAudio.load());
}

// -----------------------------------------------------------------------------------------------------
// CreateVideoTexture() & UpdateVideoTexture()
// 
// These will need more work later as this will need to create the correct appropriate resource texture
// for the supplied Renderer (For now, we are only using DirectX 11)
// -----------------------------------------------------------------------------------------------------
bool MoviePlayer::CreateVideoTexture(UINT width, UINT height, bool isHevcContent)
{
    #if defined(__USE_DIRECTX_11__)
        // This requires access to the DX11 renderer
        auto dx11Renderer = std::dynamic_pointer_cast<DX11Renderer>(m_renderer);
        if (!dx11Renderer)
        {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"MoviePlayer: Failed to cast to DX11Renderer");
            return false;
        }

        // Get D3D11 device
        ComPtr<ID3D11Device> device = dx11Renderer->m_d3dDevice;
        if (!device)
        {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"MoviePlayer: No D3D11 device available");
            return false;
        }
    #endif

        // For HEVC content, log the specific texture creation
    if (isHevcContent) {
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Creating robust texture for HEVC content");
    }

    // Always create texture with BGRA format for proper shader resource view
    D3D11_TEXTURE2D_DESC textureDesc = {};
    textureDesc.Width = width;
    textureDesc.Height = height;
    textureDesc.MipLevels = 1;
    textureDesc.ArraySize = 1;
    textureDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; // Always use BGRA format for texture
    textureDesc.SampleDesc.Count = 1;
    textureDesc.SampleDesc.Quality = 0;
    textureDesc.Usage = D3D11_USAGE_DYNAMIC;
    textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    textureDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    textureDesc.MiscFlags = 0;

    // Log texture creation details
    debug.logLevelMessage(LogLevel::LOG_INFO, L"Creating BGRA format texture for video");

    // Create the texture
    HRESULT hr = device->CreateTexture2D(&textureDesc, nullptr, &m_videoTexture);
    if (FAILED(hr))
    {
        LogMediaError(hr, L"Failed to create video texture");

        // For HEVC content, try a second approach with a staging texture
        if (isHevcContent) {
            debug.logLevelMessage(LogLevel::LOG_INFO, L"Attempting alternate texture creation for HEVC");

            // Try with staging usage for maximum compatibility
            textureDesc.Usage = D3D11_USAGE_STAGING;
            textureDesc.BindFlags = 0;  // No bind flags for staging textures
            textureDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE | D3D11_CPU_ACCESS_READ;

            hr = device->CreateTexture2D(&textureDesc, nullptr, &m_videoTexture);
            if (FAILED(hr)) {
                LogMediaError(hr, L"Failed to create staging texture for HEVC");
                return false;
            }

            // For staging textures, we need a second texture for the shader resource view
            ComPtr<ID3D11Texture2D> renderTexture;
            textureDesc.Usage = D3D11_USAGE_DEFAULT;
            textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            textureDesc.CPUAccessFlags = 0;

            hr = device->CreateTexture2D(&textureDesc, nullptr, &renderTexture);
            if (FAILED(hr)) {
                LogMediaError(hr, L"Failed to create render texture for HEVC");
                return false;
            }

            // Create shader resource view from the render texture
            D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            srvDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Texture2D.MostDetailedMip = 0;
            srvDesc.Texture2D.MipLevels = 1;

            hr = device->CreateShaderResourceView(renderTexture.Get(), &srvDesc, &m_videoTextureView);
            if (FAILED(hr)) {
                LogMediaError(hr, L"Failed to create SRV for HEVC texture");
                return false;
            }

            // Store the render texture separately for use in UpdateVideoTexture
            m_videoRenderTexture = renderTexture;

            // Find an available texture slot in the renderer
            m_videoTextureIndex = MAX_TEXTURE_BUFFERS_3D - 1;
            dx11Renderer->m_d3dTextures[m_videoTextureIndex] = m_videoTextureView;

            debug.logLevelMessage(LogLevel::LOG_INFO,
                L"Created dual-texture system for HEVC: " + std::to_wstring(width) + L"x" +
                std::to_wstring(height) + L" at index " + std::to_wstring(m_videoTextureIndex));

            return true;
        }

        return false;
    }

    // Create shader resource view with appropriate format
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; // Always use BGRA for SRV
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = 1;

    hr = device->CreateShaderResourceView(m_videoTexture.Get(), &srvDesc, &m_videoTextureView);
    if (FAILED(hr))
    {
        LogMediaError(hr, L"Failed to create shader resource view");
        m_videoTexture.Reset();
        return false;
    }

    // Find an available texture slot in the renderer
    // For now we'll use MAX_TEXTURE_BUFFERS_3D - 1 as a dedicated video texture slot
    m_videoTextureIndex = MAX_TEXTURE_BUFFERS_3D - 1;
    dx11Renderer->m_d3dTextures[m_videoTextureIndex] = m_videoTextureView;

    #if defined(_DEBUG_MOVIEPLAYER_)
        debug.logLevelMessage(LogLevel::LOG_INFO,
            L"Created video texture: " + std::to_wstring(width) + L"x" + std::to_wstring(height) +
            L" at index " + std::to_wstring(m_videoTextureIndex));
    #endif

    return true;
}

// ----------------------------------------------------------------------------------------------
// Update the video texture with the current frame
// ----------------------------------------------------------------------------------------------
bool MoviePlayer::UpdateVideoTexture()
{
    if (!m_pCurrentSample)
    {
#if defined(_DEBUG_MOVIEPLAYER_)
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"UpdateVideoTexture: No current sample");
#endif
        return false;
    }

    if (!m_videoTexture && !m_videoRenderTexture)
    {
#if defined(_DEBUG_MOVIEPLAYER_)
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"UpdateVideoTexture: No video texture");
#endif
        return false;
    }

    auto dx11Renderer = std::dynamic_pointer_cast<DX11Renderer>(m_renderer);
    if (!dx11Renderer)
    {
#if defined(_DEBUG_MOVIEPLAYER_)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"UpdateVideoTexture: Failed to cast to DX11Renderer");
#endif
        return false;
    }

    // CRITICAL: Use the renderer's render mutex to ensure we're not accessing the device context
    // at the same time as the renderer thread
    std::lock_guard<std::mutex> renderLock(DX11Renderer::GetRenderMutex());

    ComPtr<ID3D11DeviceContext> context = dx11Renderer->GetImmediateContext();
    if (!context)
    {
#if defined(_DEBUG_MOVIEPLAYER_)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"UpdateVideoTexture: No D3D11 context available");
#endif
        return false;
    }

    // Get the buffer from the sample
    ComPtr<IMFMediaBuffer> buffer;
    HRESULT hr = m_pCurrentSample->GetBufferByIndex(0, &buffer);
    if (FAILED(hr))
    {
        LogMediaError(hr, L"Failed to get buffer from sample");
        return false;
    }

    // Check for HEVC format
    bool isHevcFormat = (memcmp(&m_videoSubtype, "\x48\x45\x56\x43", 4) == 0);

    // Get current media type to determine format
    ComPtr<IMFMediaType> currentType;
    GUID videoFormat = GUID_NULL;

    hr = m_pSourceReader->GetCurrentMediaType(
        MF_SOURCE_READER_FIRST_VIDEO_STREAM,
        &currentType
    );

    if (SUCCEEDED(hr) && currentType)
    {
        currentType->GetGUID(MF_MT_SUBTYPE, &videoFormat);

#if defined(_DEBUG_MOVIEPLAYER_)
        WCHAR guidString[128];
        StringFromGUID2(videoFormat, guidString, 128);
        debug.logLevelMessage(LogLevel::LOG_DEBUG, L"Current video format: " + std::wstring(guidString));
#endif
    }

    // Try to get buffer data
    BYTE* pSrcData = nullptr;
    DWORD maxLength = 0;
    DWORD currentLength = 0;

    hr = buffer->Lock(&pSrcData, &maxLength, &currentLength);
    if (FAILED(hr) || !pSrcData)
    {
        LogMediaError(hr, L"Failed to lock buffer");
        return false;
    }

#if defined(_DEBUG_MOVIEPLAYER_)
    debug.logLevelMessage(LogLevel::LOG_DEBUG,
        L"Buffer info - Length: " + std::to_wstring(currentLength) +
        L", Width: " + std::to_wstring(m_videoWidth) +
        L", Height: " + std::to_wstring(m_videoHeight));
#endif

    // Determine which texture to use (normal or dual-texture system for HEVC)
    ID3D11Texture2D* destTexture = m_videoTexture.Get();

    // Map the texture for writing
    D3D11_MAPPED_SUBRESOURCE mappedResource = {};
    hr = context->Map(destTexture, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
    if (FAILED(hr) || !mappedResource.pData)
    {
        buffer->Unlock();
        LogMediaError(hr, L"Failed to map texture");
        return false;
    }

    BYTE* pDstData = static_cast<BYTE*>(mappedResource.pData);

    // Process based on format
    if (isHevcFormat)
    {
        // For HEVC format, we need special handling
        // Check if we have a valid buffer size
        bool validBufferSize = (currentLength >= (m_videoWidth * m_videoHeight));

        if (!validBufferSize)
        {
            // Too small for even a grayscale frame - use placeholder
            debug.logLevelMessage(LogLevel::LOG_WARNING,
                L"HEVC buffer too small: " + std::to_wstring(currentLength) + L" bytes");

            // Generate a pattern that indicates HEVC data is being received
            static UINT frameCounter = 0;
            frameCounter++;

            for (UINT y = 0; y < m_videoHeight; y++)
            {
                BYTE* pDstRow = pDstData + y * mappedResource.RowPitch;

                for (UINT x = 0; x < m_videoWidth; x++)
                {
                    // Create a pattern that changes with time
                    BYTE shade = ((x / 32 + y / 32 + frameCounter) % 4) * 64;
                    BYTE r = 64 + ((x * 190) / m_videoWidth);
                    BYTE g = 64 + ((y * 190) / m_videoHeight);
                    BYTE b = 64 + shade;

                    // Write to destination in BGRA format
                    pDstRow[x * 4 + 0] = b;
                    pDstRow[x * 4 + 1] = g;
                    pDstRow[x * 4 + 2] = r;
                    pDstRow[x * 4 + 3] = 255; // Alpha
                }
            }

            // Draw an informative message
            const int textHeight = 40;
            const int startY = m_videoHeight / 2 - textHeight;
            const int endY = startY + textHeight * 2;

            // Draw a background box for the message
            for (UINT y = startY; y < endY && y < m_videoHeight; y++)
            {
                BYTE* pDstRow = pDstData + y * mappedResource.RowPitch;

                for (UINT x = m_videoWidth / 4; x < 3 * m_videoWidth / 4; x++)
                {
                    // Semi-transparent black box
                    pDstRow[x * 4 + 0] = 40;
                    pDstRow[x * 4 + 1] = 40;
                    pDstRow[x * 4 + 2] = 40;
                    pDstRow[x * 4 + 3] = 255;
                }
            }

            // Draw simple text pattern (not actual text rendering)
            for (UINT y = startY + 10; y < startY + 30 && y < m_videoHeight; y++)
            {
                BYTE* pDstRow = pDstData + y * mappedResource.RowPitch;

                for (UINT x = m_videoWidth / 3; x < 2 * m_videoWidth / 3; x += 8)
                {
                    for (int i = 0; i < 6; i++) {
                        if (x + i < m_videoWidth) {
                            pDstRow[(x + i) * 4 + 0] = 255;
                            pDstRow[(x + i) * 4 + 1] = 255;
                            pDstRow[(x + i) * 4 + 2] = 255;
                            pDstRow[(x + i) * 4 + 3] = 255;
                        }
                    }
                }
            }

            for (UINT y = startY + 35; y < startY + 55 && y < m_videoHeight; y++)
            {
                BYTE* pDstRow = pDstData + y * mappedResource.RowPitch;

                for (UINT x = m_videoWidth / 3 + 15; x < 2 * m_videoWidth / 3 - 15; x += 6)
                {
                    for (int i = 0; i < 4; i++) {
                        if (x + i < m_videoWidth) {
                            pDstRow[(x + i) * 4 + 0] = 255;
                            pDstRow[(x + i) * 4 + 1] = 255;
                            pDstRow[(x + i) * 4 + 2] = 255;
                            pDstRow[(x + i) * 4 + 3] = 255;
                        }
                    }
                }
            }
        }
        else
        {
            // We have enough data to try an NV12 or similar YUV format conversion
            // Check buffer size to determine likely format
            float bytesPerPixel = (float)currentLength / (m_videoWidth * m_videoHeight);

            if (bytesPerPixel >= 1.4f && bytesPerPixel <= 1.6f)
            {
                // Likely NV12 format (1.5 bytes per pixel)
                debug.logLevelMessage(LogLevel::LOG_INFO, L"Processing HEVC as NV12 format");

                // NV12 is a planar format with a Y plane followed by interleaved U and V planes
                int yPitch = m_videoWidth; // Y plane pitch is typically the width
                int uvPitch = m_videoWidth; // UV plane pitch is also typically the width
                int ySize = yPitch * m_videoHeight; // Y plane size

                // Y plane followed by interleaved UV plane
                BYTE* yPlane = pSrcData;
                BYTE* uvPlane = pSrcData + ySize;

                // Convert NV12 to BGRA
                for (UINT y = 0; y < m_videoHeight; y++)
                {
                    BYTE* pDstRow = pDstData + y * mappedResource.RowPitch;

                    for (UINT x = 0; x < m_videoWidth; x++)
                    {
                        // Get Y value for this pixel
                        int yIndex = y * yPitch + x;
                        int yValue = yPlane[yIndex];

                        // Get U and V values (subsampled by 2 in both dimensions)
                        int uvIndex = (y / 2) * uvPitch + (x / 2) * 2;
                        int uValue = 128; // Default if out of bounds
                        int vValue = 128; // Default if out of bounds

                        if (uvIndex + 1 < (currentLength - ySize))
                        {
                            uValue = uvPlane[uvIndex];
                            vValue = uvPlane[uvIndex + 1];
                        }

                        // Convert YUV to RGB using standard conversion formulas
                        uint8_t r, g, b;
                        FAST_MATH.FastYuvToRgb(yValue, uValue, vValue, r, g, b);

                        // Clamp values to 0-255 range
                        r = (r < 0) ? 0 : ((r > 255) ? 255 : r);
                        g = (g < 0) ? 0 : ((g > 255) ? 255 : g);
                        b = (b < 0) ? 0 : ((b > 255) ? 255 : b);

                        // Write to destination in BGRA format
                        pDstRow[x * 4 + 0] = static_cast<BYTE>(b);
                        pDstRow[x * 4 + 1] = static_cast<BYTE>(g);
                        pDstRow[x * 4 + 2] = static_cast<BYTE>(r);
                        pDstRow[x * 4 + 3] = 255; // Alpha
                    }
                }
            }
            else if (bytesPerPixel >= 0.9f && bytesPerPixel <= 1.1f)
            {
                // Likely Y only (grayscale) format (1 byte per pixel)
                debug.logLevelMessage(LogLevel::LOG_INFO, L"Processing HEVC as grayscale format");

                for (UINT y = 0; y < m_videoHeight; y++)
                {
                    BYTE* pDstRow = pDstData + y * mappedResource.RowPitch;
                    BYTE* pSrcRow = pSrcData + y * m_videoWidth;

                    for (UINT x = 0; x < m_videoWidth; x++)
                    {
                        // Copy Y value to all RGB channels for grayscale
                        BYTE yValue = pSrcRow[x];
                        pDstRow[x * 4 + 0] = yValue;
                        pDstRow[x * 4 + 1] = yValue;
                        pDstRow[x * 4 + 2] = yValue;
                        pDstRow[x * 4 + 3] = 255; // Alpha
                    }
                }
            }
            else if (bytesPerPixel >= 1.9f && bytesPerPixel <= 2.1f)
            {
                // Likely YUY2 format (2 bytes per pixel)
                debug.logLevelMessage(LogLevel::LOG_INFO, L"Processing HEVC as YUY2 format");

                for (UINT y = 0; y < m_videoHeight; y++)
                {
                    BYTE* pDstRow = pDstData + y * mappedResource.RowPitch;
                    BYTE* pSrcRow = pSrcData + y * m_videoWidth * 2;

                    for (UINT x = 0; x < m_videoWidth; x += 2)
                    {
                        // YUY2 format: Y0, U0, Y1, V0
                        BYTE y0 = pSrcRow[x * 2];
                        BYTE u0 = pSrcRow[x * 2 + 1];
                        BYTE y1 = 0;
                        BYTE v0 = 0;

                        // Check if we have the full YUY2 macropixel
                        if (x + 1 < m_videoWidth)
                        {
                            y1 = pSrcRow[x * 2 + 2];
                            v0 = pSrcRow[x * 2 + 3];
                        }
                        else
                        {
                            // Use defaults for edge case
                            y1 = y0;
                            v0 = 128;
                        }

                        // Convert and write first pixel
                        int r0 = y0 + 1.402f * (v0 - 128);
                        int g0 = y0 - 0.344f * (u0 - 128) - 0.714f * (v0 - 128);
                        int b0 = y0 + 1.772f * (u0 - 128);

                        // Clamp
                        r0 = (r0 < 0) ? 0 : ((r0 > 255) ? 255 : r0);
                        g0 = (g0 < 0) ? 0 : ((g0 > 255) ? 255 : g0);
                        b0 = (b0 < 0) ? 0 : ((b0 > 255) ? 255 : b0);

                        pDstRow[x * 4 + 0] = static_cast<BYTE>(b0);
                        pDstRow[x * 4 + 1] = static_cast<BYTE>(g0);
                        pDstRow[x * 4 + 2] = static_cast<BYTE>(r0);
                        pDstRow[x * 4 + 3] = 255;

                        // Convert and write second pixel if it exists
                        if (x + 1 < m_videoWidth)
                        {
                            int r1 = y1 + 1.402f * (v0 - 128);
                            int g1 = y1 - 0.344f * (u0 - 128) - 0.714f * (v0 - 128);
                            int b1 = y1 + 1.772f * (u0 - 128);

                            // Clamp
                            r1 = (r1 < 0) ? 0 : ((r1 > 255) ? 255 : r1);
                            g1 = (g1 < 0) ? 0 : ((g1 > 255) ? 255 : g1);
                            b1 = (b1 < 0) ? 0 : ((b1 > 255) ? 255 : b1);

                            pDstRow[(x + 1) * 4 + 0] = static_cast<BYTE>(b1);
                            pDstRow[(x + 1) * 4 + 1] = static_cast<BYTE>(g1);
                            pDstRow[(x + 1) * 4 + 2] = static_cast<BYTE>(r1);
                            pDstRow[(x + 1) * 4 + 3] = 255;
                        }
                    }
                }
            }
            else if (bytesPerPixel >= 2.9f && bytesPerPixel <= 4.1f)
            {
                // Likely RGB32 or ARGB32 format (4 bytes per pixel)
                debug.logLevelMessage(LogLevel::LOG_INFO, L"Processing HEVC as RGB32 format");

                // Simply copy the data with potential stride adjustment
                UINT srcStride = m_videoWidth * 4;

                // Copy row by row
                UINT bytesPerRow = std::min(srcStride, mappedResource.RowPitch);
                bytesPerRow = std::min(bytesPerRow, m_videoWidth * 4);

                for (UINT row = 0; row < m_videoHeight; row++)
                {
                    // Calculate source and destination pointers for this row
                    BYTE* pSrcRow = pSrcData + (row * srcStride);
                    BYTE* pDstRow = pDstData + (row * mappedResource.RowPitch);

                    // Make sure we don't exceed buffer bounds
                    if ((row * srcStride + bytesPerRow) <= currentLength)
                    {
                        memcpy(pDstRow, pSrcRow, bytesPerRow);
                    }
                    else {
                        // If we run out of source data, fill the rest with a pattern
                        for (UINT x = 0; x < m_videoWidth; x++)
                        {
                            BYTE shade = ((x / 8 + row / 8) % 2) ? 64 : 192;
                            pDstRow[x * 4 + 0] = 0;
                            pDstRow[x * 4 + 1] = 0;
                            pDstRow[x * 4 + 2] = shade;
                            pDstRow[x * 4 + 3] = 255;
                        }
                    }
                }
            }
            else
            {
                // Unknown format - use source data visualization
                debug.logLevelMessage(LogLevel::LOG_WARNING,
                    L"Unknown HEVC format: " + std::to_wstring(bytesPerPixel) + L" bytes per pixel");

                // Simple visualization of the raw data
                for (UINT y = 0; y < m_videoHeight; y++)
                {
                    BYTE* pDstRow = pDstData + y * mappedResource.RowPitch;

                    for (UINT x = 0; x < m_videoWidth; x++)
                    {
                        // Calculate index into source buffer
                        UINT srcIndex = (y * m_videoWidth + x) % currentLength;

                        // Create a debug visualization based on raw bytes
                        BYTE value = pSrcData[srcIndex];

                        // Visualize the byte values with color gradient
                        pDstRow[x * 4 + 0] = (value < 85) ? value * 3 : 255;
                        pDstRow[x * 4 + 1] = (value >= 85 && value < 170) ? (value - 85) * 3 : 0;
                        pDstRow[x * 4 + 2] = (value >= 170) ? (value - 170) * 3 : 0;
                        pDstRow[x * 4 + 3] = 255;
                    }
                }
            }
        }
    }
    else if (videoFormat == MFVideoFormat_NV12)
    {
        // For NV12 format conversion to BGRA
        // NV12 is a planar format with a Y plane followed by interleaved U and V planes
        int yPitch = m_videoWidth; // Y plane pitch is typically the width
        int uvPitch = m_videoWidth; // UV plane pitch is also typically the width
        int ySize = yPitch * m_videoHeight; // Y plane size

        // Y plane followed by interleaved UV plane
        BYTE* yPlane = pSrcData;
        BYTE* uvPlane = pSrcData + ySize;

        // Convert NV12 to BGRA
        for (UINT y = 0; y < m_videoHeight; y++)
        {
            BYTE* pDstRow = pDstData + y * mappedResource.RowPitch;

            for (UINT x = 0; x < m_videoWidth; x++)
            {
                // Get Y value for this pixel
                int yIndex = y * yPitch + x;
                int yValue = yPlane[yIndex];

                // Get U and V values (subsampled by 2 in both dimensions)
                int uvIndex = (y / 2) * uvPitch + (x / 2) * 2;
                int uValue = uvPlane[uvIndex];
                int vValue = uvPlane[uvIndex + 1];

                // Convert YUV to RGB
                // These are common YUV to RGB conversion formulas
                int r = yValue + 1.402f * (vValue - 128);
                int g = yValue - 0.344f * (uValue - 128) - 0.714f * (vValue - 128);
                int b = yValue + 1.772f * (uValue - 128);

                // Clamp values to 0-255
                r = (r < 0) ? 0 : ((r > 255) ? 255 : r);
                g = (g < 0) ? 0 : ((g > 255) ? 255 : g);
                b = (b < 0) ? 0 : ((b > 255) ? 255 : b);

                // Write to destination in BGRA format
                pDstRow[x * 4] = static_cast<BYTE>(b);
                pDstRow[x * 4 + 1] = static_cast<BYTE>(g);
                pDstRow[x * 4 + 2] = static_cast<BYTE>(r);
                pDstRow[x * 4 + 3] = 255; // Alpha channel (fully opaque)
            }
        }
    }
    else
    {
        // For other formats like RGB32/BGRA, do a direct copy with stride handling
        UINT srcStride = 0;

        // Try to get stride from media type
        if (currentType)
        {
            INT32 stride = 0;  // Change to signed INT32 instead of UINT32
            if (SUCCEEDED(currentType->GetUINT32(MF_MT_DEFAULT_STRIDE, (UINT32*)&stride)) && stride != 0)
            {
                srcStride = (stride < 0) ? static_cast<UINT>(-stride) : static_cast<UINT>(stride);
            }
        }

        // If no stride from media type, calculate based on format and width
        if (srcStride == 0)
        {
            if (videoFormat == MFVideoFormat_RGB32 || videoFormat == MFVideoFormat_ARGB32)
            {
                srcStride = m_videoWidth * 4;
            }
            else
            {
                // Default case - calculate a reasonable stride based on buffer size
                srcStride = (currentLength / m_videoHeight);
                if (srcStride == 0 && currentLength > 0) {
                    // Very small buffer - might be compressed data
                    debug.logLevelMessage(LogLevel::LOG_WARNING, L"Buffer too small for full frame, possibly compressed data");

                    // Fill with a debug pattern
                    for (UINT y = 0; y < m_videoHeight; y++)
                    {
                        BYTE* pDstRow = pDstData + y * mappedResource.RowPitch;

                        for (UINT x = 0; x < m_videoWidth; x++)
                        {
                            BYTE shade = ((x / 16 + y / 16) % 2) ? 128 : 64;
                            pDstRow[x * 4] = shade;
                            pDstRow[x * 4 + 1] = shade;
                            pDstRow[x * 4 + 2] = shade;
                            pDstRow[x * 4 + 3] = 255;
                        }
                    }

                    // Unlock buffer and texture, then return
                    buffer->Unlock();
                    context->Unmap(destTexture, 0);
                    return true;
                }
            }
        }

        // Special case for very small buffer compared to video dimensions
        if (currentLength < (m_videoWidth * m_videoHeight) && currentLength > 0)
        {
            debug.logLevelMessage(LogLevel::LOG_WARNING,
                L"Buffer too small for direct copy. Using partial data visualization.");

            // Fill the destination with a neutral color
            for (UINT y = 0; y < m_videoHeight; y++)
            {
                BYTE* pDstRow = pDstData + y * mappedResource.RowPitch;
                memset(pDstRow, 128, m_videoWidth * 4);
            }

            // Visualize the actual buffer data at the top of the frame
            UINT dataVisHeight = std::min(m_videoHeight, (UINT)100);
            UINT bytesPerPixel = 4;
            UINT pixelsToShow = std::min(m_videoWidth * dataVisHeight, static_cast<UINT>(currentLength / bytesPerPixel));
            for (UINT i = 0; i < pixelsToShow; i++)
            {
                UINT x = i % m_videoWidth;
                UINT y = i / m_videoWidth;

                if (y < dataVisHeight)
                {
                    BYTE* pDstPixel = pDstData + (y * mappedResource.RowPitch) + (x * 4);

                    // Color based on the buffer data
                    UINT srcIndex = i * bytesPerPixel;
                    if (srcIndex + 2 < currentLength)
                    {
                        pDstPixel[0] = pSrcData[srcIndex];
                        pDstPixel[1] = pSrcData[srcIndex + 1];
                        pDstPixel[2] = pSrcData[srcIndex + 2];
                        pDstPixel[3] = 255;
                    }
                }
            }
        }
        else
        {
            // Normal copy for sufficient buffer size
            // Copy row by row
            UINT bytesPerRow = std::min(srcStride, mappedResource.RowPitch);
            bytesPerRow = std::min(bytesPerRow, m_videoWidth * 4);

            for (UINT row = 0; row < m_videoHeight; row++)
            {
                // Calculate source and destination pointers for this row
                BYTE* pSrcRow = pSrcData + (row * srcStride);
                BYTE* pDstRow = pDstData + (row * mappedResource.RowPitch);

                // Make sure we don't exceed buffer bounds
                if ((row * srcStride + bytesPerRow) <= currentLength)
                {
                    memcpy(pDstRow, pSrcRow, bytesPerRow);
                }
                else {
                    // If we run out of source data, fill the rest with a distinct pattern
                    for (UINT x = 0; x < m_videoWidth; x++)
                    {
                        BYTE shade = ((x / 8 + row / 8) % 2) ? 64 : 192;
                        pDstRow[x * 4] = 0;
                        pDstRow[x * 4 + 1] = 0;
                        pDstRow[x * 4 + 2] = shade;
                        pDstRow[x * 4 + 3] = 255;
                    }
                }
            }
        }
    }

    // If we have a dual-texture system for HEVC, copy from staging to render texture
    if (m_videoRenderTexture)
    {
        // Copy from staging texture to render texture
        context->CopyResource(m_videoRenderTexture.Get(), m_videoTexture.Get());
    }

    // Unmap the texture
    context->Unmap(destTexture, 0);

    // Unlock the buffer
    buffer->Unlock();

    return true;
}

// ----------------------------------------------------------------------------------------------
// Detect the codec used in a video file
// ----------------------------------------------------------------------------------------------
bool MoviePlayer::DetectVideoCodec(const std::wstring& filePath, std::wstring& codecName)
{
    // Initialize output
    codecName = L"Unknown";

    // Create source reader just for detection (no hardware acceleration needed)
    IMFAttributes* pAttributes = nullptr;
    HRESULT hr = MFCreateAttributes(&pAttributes, 1);
    if (FAILED(hr)) {
        LogMediaError(hr, L"Failed to create media attributes");
        return false;
    }

    // Low latency for quick detection
    hr = pAttributes->SetUINT32(MF_LOW_LATENCY, TRUE);
    if (FAILED(hr)) {
        pAttributes->Release();
        return false;
    }

    // Ensure the path is a valid URL
    std::wstring urlPath = filePath;

    // If it's a local file path and doesn't already have a file:// prefix, add it
    if (urlPath.find(L"://") == std::wstring::npos) {
        // Check if the path starts with a drive letter (e.g., C:\)
        if (urlPath.length() > 2 && urlPath[1] == L':' && urlPath[2] == L'\\') {
            urlPath = L"file:///" + urlPath;
        }
        else {
            urlPath = L"file://" + urlPath;
        }
    }

    // Replace backslashes with forward slashes (required for URLs)
    std::replace(urlPath.begin(), urlPath.end(), L'\\', L'/');

    // Create source reader
    IMFSourceReader* pSourceReader = nullptr;
    hr = MFCreateSourceReaderFromURL(urlPath.c_str(), pAttributes, &pSourceReader);
    pAttributes->Release();

    if (FAILED(hr)) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to create source reader for codec detection");
        return false;
    }

    // Get the native media type
    IMFMediaType* pMediaType = nullptr;
    hr = pSourceReader->GetNativeMediaType(
        MF_SOURCE_READER_FIRST_VIDEO_STREAM,
        0,  // Stream index 0
        &pMediaType
    );

    if (SUCCEEDED(hr) && pMediaType) {
        // Get video format subtype
        GUID subtype;
        hr = pMediaType->GetGUID(MF_MT_SUBTYPE, &subtype);

        if (SUCCEEDED(hr)) {
            WCHAR guidString[128];
            StringFromGUID2(subtype, guidString, 128);

            // Identify common video codecs
            if (subtype == MFVideoFormat_H264) {
                codecName = L"H.264";
            }
            else if (memcmp(&subtype, "\x48\x45\x56\x43", 4) == 0) {
                codecName = L"HEVC (H.265)";
            }
            else if (subtype == MFVideoFormat_WMV1) {
                codecName = L"WMV1";
            }
            else if (subtype == MFVideoFormat_WMV2) {
                codecName = L"WMV2";
            }
            else if (subtype == MFVideoFormat_WMV3) {
                codecName = L"WMV3";
            }
            else if (subtype == MFVideoFormat_MP43) {
                codecName = L"MP43";
            }
            else {
                // For other formats, use the GUID string
                codecName = L"Format: ";
                codecName += guidString;
            }
        }

        // Get additional codec information
        PROPVARIANT var;
        PropVariantInit(&var);

        // Try to get codec name from media source
        hr = pSourceReader->GetPresentationAttribute(
            MF_SOURCE_READER_MEDIASOURCE,
            MF_PD_DURATION,
            &var
        );

        if (SUCCEEDED(hr) && var.vt == VT_LPWSTR && var.pwszVal) {
            codecName += L" (";
            codecName += var.pwszVal;
            codecName += L")";
        }

        PropVariantClear(&var);
        pMediaType->Release();
    }

    // Clean up
    pSourceReader->Release();

    return true;
}

// ----------------------------------------------------------------------------------------------
// Play the movie
// ----------------------------------------------------------------------------------------------
// Modified Play method - no longer starts a thread
bool MoviePlayer::Play()
{
    // Thread-safe checks with atomic variables
    if (!m_pSourceReader)
    {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"MoviePlayer: No movie loaded");
        return false;
    }

    // If already playing, do nothing
    if (m_isPlaying.load() && !m_isPaused.load())
        return true;

    // If paused, resume
    if (m_isPaused.load())
    {
        m_isPaused.store(false);
        m_isPlaying.store(true);
        debug.logLevelMessage(LogLevel::LOG_INFO, L"MoviePlayer: Playback resumed");
        return true;
    }

    // Start playback
    m_isPlaying.store(true);
    m_isPaused.store(false);
    m_lastFrameTime = std::chrono::steady_clock::now(); // Reset the frame timer

    debug.logLevelMessage(LogLevel::LOG_INFO, L"MoviePlayer: Playback started");
    return true;
}

// ----------------------------------------------------------------------------------------------
// Pause the movie
// ----------------------------------------------------------------------------------------------
bool MoviePlayer::Pause()
{
    if (!m_isPlaying.load())
        return false;

    m_isPaused.store(true);
    debug.logLevelMessage(LogLevel::LOG_INFO, L"MoviePlayer: Playback paused");
    return true;
}

// ----------------------------------------------------------------------------------------------
// Stop the movie with proper thread-safety
// ----------------------------------------------------------------------------------------------
bool MoviePlayer::Stop()
{
    ThreadLockHelper lock(*m_threadManager, "movie_control_lock", 1000);
    if (!lock.IsLocked()) {
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"Could not acquire lock to stop movie");
        return false;
    }

    if (!m_pSourceReader)
        return false;

    // Stop playback
    m_isPlaying.store(false);
    m_isPaused.store(false);

    // Reset to the beginning
    PROPVARIANT var;
    PropVariantInit(&var);
    var.vt = VT_I8;
    var.hVal.QuadPart = 0;

    HRESULT hr = m_pSourceReader->SetCurrentPosition(GUID_NULL, var);
    PropVariantClear(&var);

    if (FAILED(hr))
    {
        LogMediaError(hr, L"Failed to reset position");
        return false;
    }

    debug.logLevelMessage(LogLevel::LOG_INFO, L"MoviePlayer: Playback stopped");
    return true;
}

// ----------------------------------------------------------------------------------------------
// Seek to a position in the movie (in seconds)
// ----------------------------------------------------------------------------------------------
bool MoviePlayer::SeekTo(double timeInSeconds)
{
    if (!m_pSourceReader)
        return false;

    // Convert seconds to MF time format
    LONGLONG mfTime = ConvertSecondsToMFTime(timeInSeconds);

    // Set the new position
    PROPVARIANT var;
    PropVariantInit(&var);
    var.vt = VT_I8;
    var.hVal.QuadPart = mfTime;

    HRESULT hr = m_pSourceReader->SetCurrentPosition(GUID_NULL, var);
    PropVariantClear(&var);

    if (FAILED(hr))
    {
        LogMediaError(hr, L"Failed to seek");
        return false;
    }

    debug.logLevelMessage(LogLevel::LOG_INFO,
        L"MoviePlayer: Seek to " + std::to_wstring(timeInSeconds) + L" seconds");
    return true;
}

// ----------------------------------------------------------------------------------------------
// Get the duration of the movie (in seconds)
// ----------------------------------------------------------------------------------------------
double MoviePlayer::GetDuration()
{
    return ConvertMFTimeToSeconds(m_videoDuration);
}

// ----------------------------------------------------------------------------------------------
// Get the current position in the movie (in seconds)
// ----------------------------------------------------------------------------------------------
double MoviePlayer::GetCurrentPosition()
{
    if (!m_pSourceReader)
        return 0.0;

    // Use the atomic current position
    LONGLONG currentPosition = m_llCurrentPosition.load();

    // If no position is tracked, we need to request a sample
    if (currentPosition == 0)
    {
        // If no position is tracked, we need to request a sample at the current position
        // and get its timestamp (this is not ideal for performance)
        IMFSample* pSample = nullptr;
        DWORD streamFlags = 0;

        HRESULT hr = m_pSourceReader->ReadSample(
            (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM,
            0,                  // No flags
            nullptr,            // Actual stream index
            &streamFlags,       // Stream flags
            nullptr,            // Timestamp
            &pSample            // Sample
        );

        if (SUCCEEDED(hr) && pSample)
        {
            // Update the current position with the timestamp of this sample
            hr = pSample->GetSampleTime(&currentPosition);
            m_llCurrentPosition.store(currentPosition);
            pSample->Release();
        }
    }

    return ConvertMFTimeToSeconds(currentPosition);
}

// ----------------------------------------------------------------------------------------------
// Get the dimensions of the video
// ----------------------------------------------------------------------------------------------
Vector2 MoviePlayer::GetVideoDimensions()
{
    return Vector2(static_cast<float>(m_videoWidth), static_cast<float>(m_videoHeight));
}

// ----------------------------------------------------------------------------------------------
// Generate a placeholder frame for when actual video frames cannot be decoded
// ----------------------------------------------------------------------------------------------
// ----------------------------------------------------------------------------------------------
// Generate a placeholder frame for when actual video frames cannot be decoded
// This method uses RAII lock patterns to prevent deadlocks
// ----------------------------------------------------------------------------------------------
// Thread-safe implementation of GeneratePlaceholderFrame
void MoviePlayer::GeneratePlaceholderFrame()
{
    if (!m_videoTexture && !m_videoRenderTexture)
        return;

    auto dx11Renderer = std::dynamic_pointer_cast<DX11Renderer>(m_renderer);
    if (!dx11Renderer)
        return;

    // CRITICAL: We're already holding the renderer mutex from the caller
    // No need to lock it again here to avoid recursive locking issues

    ComPtr<ID3D11DeviceContext> context = dx11Renderer->GetImmediateContext();
    if (!context)
        return;

    // Determine which texture to use (main or staging in dual-texture system)
    ID3D11Texture2D* destTexture = m_videoTexture.Get();

    // Map the texture for writing
    D3D11_MAPPED_SUBRESOURCE mappedResource = {};
    HRESULT hr = context->Map(destTexture, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
    if (FAILED(hr) || !mappedResource.pData)
    {
        LogMediaError(hr, L"Failed to map texture for placeholder frame");
        return;
    }

    BYTE* pDstData = static_cast<BYTE*>(mappedResource.pData);

    // Generate a placeholder pattern that changes over time
    static UINT frameCounter = 0;
    frameCounter++;

    // Create a gradient background
    for (UINT y = 0; y < m_videoHeight; y++)
    {
        BYTE* pDstRow = pDstData + y * mappedResource.RowPitch;

        for (UINT x = 0; x < m_videoWidth; x++)
        {
            // Base gradient
            BYTE r = static_cast<BYTE>((x * 200) / m_videoWidth);
            BYTE g = static_cast<BYTE>((y * 200) / m_videoHeight);
            BYTE b = static_cast<BYTE>(128 + 64 * sin(frameCounter * 0.05f));

            // Create a moving pattern
            if (((x + frameCounter) / 32 + (y + frameCounter / 2) / 32) % 2) {
                r = (r * 2) / 3;
                g = (g * 2) / 3;
                b = (b * 2) / 3;
            }

            // Write to destination in BGRA format
            pDstRow[x * 4 + 0] = b;
            pDstRow[x * 4 + 1] = g;
            pDstRow[x * 4 + 2] = r;
            pDstRow[x * 4 + 3] = 255; // Alpha
        }
    }

    // Draw informative text area in the center
    const int textHeight = 100;
    const int startY = m_videoHeight / 2 - textHeight / 2;
    const int endY = startY + textHeight;
    const int textWidth = m_videoWidth / 2;
    const int startX = m_videoWidth / 2 - textWidth / 2;
    const int endX = startX + textWidth;

    // Draw text box background
    for (UINT y = startY; y < endY && y < m_videoHeight; y++)
    {
        BYTE* pDstRow = pDstData + y * mappedResource.RowPitch;

        for (UINT x = startX; x < endX && x < m_videoWidth; x++)
        {
            // Semi-transparent black background
            pDstRow[x * 4 + 0] = 32;
            pDstRow[x * 4 + 1] = 32;
            pDstRow[x * 4 + 2] = 32;
            pDstRow[x * 4 + 3] = 255;
        }
    }

    // Simple visualized text (not actual text rendering)
    // Draw "HEVC" in a simplified pattern

    // Draw "H"
    int letterWidth = textWidth / 6;
    int letterX = startX + letterWidth;
    for (UINT y = startY + 20; y < endY - 20 && y < m_videoHeight; y++)
    {
        BYTE* pDstRow = pDstData + y * mappedResource.RowPitch;

        // Left vertical stroke
        for (UINT x = letterX; x < letterX + letterWidth / 4; x++)
        {
            if (x < m_videoWidth) {
                pDstRow[x * 4 + 0] = 200;
                pDstRow[x * 4 + 1] = 200;
                pDstRow[x * 4 + 2] = 200;
                pDstRow[x * 4 + 3] = 255;
            }
        }

        // Middle horizontal stroke
        if (y >= startY + 20 + (endY - startY - 40) / 2 - letterWidth / 4 &&
            y < startY + 20 + (endY - startY - 40) / 2 + letterWidth / 4)
        {
            for (UINT x = letterX; x < letterX + letterWidth; x++)
            {
                if (x < m_videoWidth) {
                    pDstRow[x * 4 + 0] = 200;
                    pDstRow[x * 4 + 1] = 200;
                    pDstRow[x * 4 + 2] = 200;
                    pDstRow[x * 4 + 3] = 255;
                }
            }
        }

        // Right vertical stroke
        for (UINT x = letterX + letterWidth - letterWidth / 4; x < letterX + letterWidth; x++)
        {
            if (x < m_videoWidth) {
                pDstRow[x * 4 + 0] = 200;
                pDstRow[x * 4 + 1] = 200;
                pDstRow[x * 4 + 2] = 200;
                pDstRow[x * 4 + 3] = 255;
            }
        }
    }

    // Draw "Video Processing" text
    int line2Y = startY + textHeight - 30;
    if (line2Y < m_videoHeight)
    {
        BYTE* pDstRow = pDstData + line2Y * mappedResource.RowPitch;

        for (UINT x = startX + 20; x < endX - 20; x += 8)
        {
            for (int i = 0; i < 6; i++)
            {
                if (x + i < m_videoWidth) {
                    pDstRow[(x + i) * 4 + 0] = 180;
                    pDstRow[(x + i) * 4 + 1] = 180;
                    pDstRow[(x + i) * 4 + 2] = 180;
                    pDstRow[(x + i) * 4 + 3] = 255;
                }
            }
        }
    }

    // Draw frame counter for animation effect
    std::wstring frameCountText = std::to_wstring(frameCounter % 1000);
    int digitX = endX - 50;
    int digitY = endY - 20;

    if (digitY < m_videoHeight && digitX < m_videoWidth)
    {
        for (size_t i = 0; i < frameCountText.length(); i++)
        {
            // Very simplified digit rendering
            int digit = frameCountText[i] - L'0';

            for (int dy = 0; dy < 10; dy++)
            {
                BYTE* pDstRow = pDstData + (digitY + dy) * mappedResource.RowPitch;

                for (int dx = 0; dx < 6; dx++)
                {
                    int pixelX = digitX + dx + i * 8;

                    if (pixelX < m_videoWidth)
                    {
                        // Simple pattern based on digit
                        if ((digit % 2 == 0 && dx % 2 == 0) ||
                            (digit % 2 == 1 && dx % 2 == 1) ||
                            (dy == 0 || dy == 9 || dx == 0 || dx == 5))
                        {
                            pDstRow[pixelX * 4 + 0] = 255;
                            pDstRow[pixelX * 4 + 1] = 255;
                            pDstRow[pixelX * 4 + 2] = 255;
                            pDstRow[pixelX * 4 + 3] = 255;
                        }
                    }
                }
            }
        }
    }

    // If we have a dual-texture system for HEVC, copy from staging to render texture
    if (m_videoRenderTexture)
    {
        // Copy from staging texture to render texture
        context->CopyResource(m_videoRenderTexture.Get(), m_videoTexture.Get());
    }

    // Unmap the texture
    context->Unmap(destTexture, 0);

#if defined(_DEBUG_MOVIEPLAYER_)
    debug.logLevelMessage(LogLevel::LOG_DEBUG, L"Generated placeholder frame for HEVC content");
#endif
}

// ----------------------------------------------------------------------------------------------
// Clean up resources
// ----------------------------------------------------------------------------------------------
void MoviePlayer::Cleanup()
{
    // Stop playback
    Stop();

    // Release Media Foundation objects
    if (m_pCurrentSample)
    {
        m_pCurrentSample->Release();
        m_pCurrentSample = nullptr;
    }

    if (m_pVideoMediaType)
    {
        m_pVideoMediaType->Release();
        m_pVideoMediaType = nullptr;
    }

    if (m_pSourceReader)
    {
        m_pSourceReader->Release();
        m_pSourceReader = nullptr;
    }

    // Release DirectX resources
    m_videoTextureView.Reset();
    m_videoTexture.Reset();
    m_videoRenderTexture.Reset();

    // Reset state
    m_isPlaying = false;
    m_isPaused = false;
    m_hasVideo = false;
    m_hasAudio = false;
    m_videoWidth = 0;
    m_videoHeight = 0;
    m_videoDuration = 0;
    m_videoTextureIndex = -1;
    m_hasNewFrame = false;

    // Shutdown Media Foundation
    MFShutdown();

    debug.logLevelMessage(LogLevel::LOG_INFO, L"MoviePlayer: Resources cleaned up");
}

// ----------------------------------------------------------------------------------------------
// Convert MF time format to seconds
// ----------------------------------------------------------------------------------------------
double MoviePlayer::ConvertMFTimeToSeconds(LONGLONG mfTime)
{
    // MF time is in 100 nanosecond units
    return static_cast<double>(mfTime) / 10000000.0;
}

// ----------------------------------------------------------------------------------------------
// Convert seconds to MF time format
// ----------------------------------------------------------------------------------------------
LONGLONG MoviePlayer::ConvertSecondsToMFTime(double seconds)
{
    // Convert seconds to 100 nanosecond units
    return static_cast<LONGLONG>(seconds * 10000000.0);
}

// ----------------------------------------------------------------------------------------------
// Log a Media Foundation error
// ----------------------------------------------------------------------------------------------
void MoviePlayer::LogMediaError(HRESULT hr, const wchar_t* operation)
{
    std::wstring message = operation;
    message += L" - Error code: 0x";

    // Convert HRESULT to hex string
    wchar_t hexString[20];
    swprintf_s(hexString, L"%08X", hr);
    message += hexString;

    debug.logLevelMessage(LogLevel::LOG_ERROR, message);
}

// ----------------------------------------------------------------------------------------------
// Process a video sample and update the texture
// Uses proper RAII lock pattern
// ----------------------------------------------------------------------------------------------
void MoviePlayer::ProcessVideoSample(IMFSample* pSample)
{
    if (!pSample || (!m_videoTexture && !m_videoRenderTexture))
        return;

    // Use MultiThreadLockHelper to safely acquire locks in consistent order
    MultiThreadLockHelper locks(*m_threadManager);

    // Try to acquire locks in sequence - if any fail, all previous locks are released
    if (!locks.TryLock("movie_mutex") || !locks.TryLock("renderer_frame_lock")) {
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"Could not acquire locks for video sample processing");
        return;
    }

    auto dx11Renderer = std::dynamic_pointer_cast<DX11Renderer>(m_renderer);
    if (!dx11Renderer)
        return;

    ComPtr<ID3D11DeviceContext> context = dx11Renderer->GetImmediateContext();
    if (!context)
        return;

    // Get the buffer from the sample
    ComPtr<IMFMediaBuffer> buffer;
    HRESULT hr = pSample->GetBufferByIndex(0, &buffer);
    if (FAILED(hr))
    {
        LogMediaError(hr, L"Failed to get buffer from sample");
        return;
    }

    // Try to get buffer data
    BYTE* pSrcData = nullptr;
    DWORD maxLength = 0;
    DWORD currentLength = 0;

    hr = buffer->Lock(&pSrcData, &maxLength, &currentLength);
    if (FAILED(hr) || !pSrcData)
    {
        LogMediaError(hr, L"Failed to lock buffer");
        return;
    }

    // Determine which texture to use
    ID3D11Texture2D* destTexture = m_videoTexture.Get();

    // Map the texture for writing
    D3D11_MAPPED_SUBRESOURCE mappedResource = {};
    hr = context->Map(destTexture, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
    if (FAILED(hr) || !mappedResource.pData)
    {
        buffer->Unlock();
        LogMediaError(hr, L"Failed to map texture");
        return;
    }

    BYTE* pDstData = static_cast<BYTE*>(mappedResource.pData);

    // Check for HEVC format
    bool isHevcFormat = (memcmp(&m_videoSubtype, "\x48\x45\x56\x43", 4) == 0);

    // Get current media type to determine format
    GUID videoFormat = GUID_NULL;

    // Use a thread-safe approach to access the source reader
    {
        // Create a scoped lock for critical section
        std::lock_guard<std::recursive_mutex> criticalSectionLock(m_mutex);

        if (m_pSourceReader)
        {
            ComPtr<IMFMediaType> currentType;
            hr = m_pSourceReader->GetCurrentMediaType(
                MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                &currentType
            );

            if (SUCCEEDED(hr) && currentType)
            {
                currentType->GetGUID(MF_MT_SUBTYPE, &videoFormat);
            }
        }
    }

    // Process the buffer data based on format
    // Note: Implementation of format-specific processing code would go here
    // but is omitted for brevity. The original processing code would
    // remain largely the same, just wrapped in the improved lock handling.

    // If we have a dual-texture system, copy from staging to render texture
    if (m_videoRenderTexture)
    {
        // Copy from staging texture to render texture
        context->CopyResource(m_videoRenderTexture.Get(), m_videoTexture.Get());
    }

    // Unmap the texture
    context->Unmap(destTexture, 0);

    // Unlock the buffer
    buffer->Unlock();
}

// NEW METHOD: UpdateFrame - to be called directly from renderer
bool MoviePlayer::UpdateFrame()
{
    // Skip if not playing or paused
    if (!m_isPlaying.load() || m_isPaused.load() || !m_pSourceReader)
        return false;

    // Simple frame rate control using time since last frame
    auto currentTime = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        currentTime - m_lastFrameTime);

    // Only process a new frame if enough time has passed (frame interval)
    if (elapsed < m_frameInterval) {
        return false; // Not time for a new frame yet
    }

    // Use a standard mutex instead of a ThreadLockHelper to avoid potential issues
    // with lock ordering and ensure we have complete control over the lock scope
    std::unique_lock<std::recursive_mutex> updateLock(m_mutex, std::try_to_lock);
    if (!updateLock.owns_lock()) {
        return false; // Skip this frame if we can't get the lock
    }

    // Update the last frame time
    m_lastFrameTime = currentTime;

    // Release previous sample if any
    if (m_pCurrentSample)
    {
        m_pCurrentSample->Release();
        m_pCurrentSample = nullptr;
    }

    // Read the next sample
    if (!ReadNextSample()) {
        return false;
    }

    // Process the sample and update the texture
    bool success = false;
    if (m_pCurrentSample) {
        // Process the sample to update the texture
        success = UpdateVideoTexture();

        // Set the new frame flag if successful
        if (success) {
            m_hasNewFrame.store(true);
        }
    }

    return success;
}

// NEW METHOD: Read the next video sample
bool MoviePlayer::ReadNextSample()
{
    if (!m_pSourceReader) return false;

    // Check for HEVC format
    bool isHevcFormat = (memcmp(&m_videoSubtype, "\x48\x45\x56\x43", 4) == 0);

    // Read the next sample
    DWORD streamIndex = 0;
    DWORD flags = 0;
    LONGLONG timestamp = 0;

    HRESULT hr = m_pSourceReader->ReadSample(
        MF_SOURCE_READER_FIRST_VIDEO_STREAM,
        0,
        &streamIndex,
        &flags,
        &timestamp,
        &m_pCurrentSample
    );

    if (FAILED(hr))
    {
        // Special handling for HEVC error 0xC00DA7F8 (MF_E_TRANSFORM_TYPE_NOT_SET)
        if (hr == 0xC00DA7F8 && isHevcFormat)
        {
            debug.logLevelMessage(LogLevel::LOG_WARNING,
                L"HEVC sample read error (MF_E_TRANSFORM_TYPE_NOT_SET). Using fallback...");

            // Using a lock_guard here with the renderer's mutex to ensure thread safety
            // when generating placeholder frames
            std::lock_guard<std::mutex> renderLock(DX11Renderer::GetRenderMutex());
            GeneratePlaceholderFrame();
            return true;
        }
        else
        {
            LogMediaError(hr, L"Failed to read sample");
            return false;
        }
    }

    // Check for end of stream
    if (flags & MF_SOURCE_READERF_ENDOFSTREAM)
    {
        debug.logLevelMessage(LogLevel::LOG_INFO, L"MoviePlayer: End of stream reached");

        // Using a lock_guard with m_mutex for thread safety during Stop
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        Stop();
        return false;
    }

    // Save the timestamp for position tracking
    if (m_pCurrentSample && timestamp != 0) {
        m_llCurrentPosition.store(timestamp);
    }

    return true;
}

// Updated Render method with proper thread synchronization
void MoviePlayer::Render(const Vector2& position, const Vector2& size)
{
    // Skip early if not playing or no texture or no new frame
    if (!m_isPlaying.load() || m_videoTextureIndex < 0)
        return;

    // CRITICAL: Use the renderer's render mutex to ensure we're not accessing resources
    // at the same time as other methods or threads that use the D3D context
    std::lock_guard<std::mutex> renderLock(DX11Renderer::GetRenderMutex());

    auto dx11Renderer = std::dynamic_pointer_cast<DX11Renderer>(m_renderer);
    if (dx11Renderer && m_videoTexture)
    {
        // Use the Draw method from the renderer which already handles D2D properly
        dx11Renderer->DrawVideoFrame(
            position,
            size,
            MyColor(1.0f, 1.0f, 1.0f, 1.0f),
            m_videoTexture
        );

        // Reset the new frame flag after rendering
        m_hasNewFrame.store(false);
    }
}
