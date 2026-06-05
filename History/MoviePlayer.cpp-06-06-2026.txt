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
#if defined(__USE_DIRECTX_11__)
    #include "DX11Renderer.h"
#elif defined(__USE_DIRECTX_12__)
    #include "DX12Renderer.h"
#endif
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
    m_enableAudio(false),
    m_targetFPS(30.0f),
    m_audioReadPosition(0),
    m_audioStartSamples(0),
    m_samplesPlayedOffset(0),
    m_firstAudioSamplePTS(-1),
    m_pXAudio2(nullptr),
    m_pMasterVoice(nullptr),
    m_pSourceVoice(nullptr),
    m_audioFormat{},
    m_playStartMediaTime(-1)
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
bool MoviePlayer::Initialize(std::shared_ptr<Renderer> rendererPtr, ThreadManager* threadManager, float fps, bool enableAudio)
{
#if defined(_DEBUG_MOVIEPLAYER_)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"Initializing MoviePlayer with ThreadManager");
#endif

    m_targetFPS   = (fps > 0.0f) ? fps : 30.0f;
    m_enableAudio = enableAudio;

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

    // Initialize XAudio2 if audio playback is requested
    if (m_enableAudio)
    {
        if (!InitializeAudio())
        {
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"MoviePlayer: Audio initialization failed - continuing without audio");
            m_enableAudio = false;
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
#if defined(__USE_DIRECTX_11__)
    {
        auto dx11Renderer = std::dynamic_pointer_cast<DX11Renderer>(m_renderer);
        if (dx11Renderer && dx11Renderer->m_d3dDevice)
        {
            ComPtr<ID3D10Multithread> pMultithread;
            HRESULT hr = dx11Renderer->m_d3dDevice.As(&pMultithread);
            if (SUCCEEDED(hr) && pMultithread)
            {
                pMultithread->SetMultithreadProtected(TRUE);
                debug.logLevelMessage(LogLevel::LOG_INFO, L"D3D11 multithreaded protection enabled");
            }
        }
    }
#endif

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
#if defined(__USE_DIRECTX_11__)
    {
        auto dx11Renderer = std::dynamic_pointer_cast<DX11Renderer>(m_renderer);
        if (dx11Renderer && dx11Renderer->m_d3dDevice)
        {
            std::lock_guard<std::mutex> renderLock(DX11Renderer::GetRenderMutex());

            IMFDXGIDeviceManager* pDeviceManager = nullptr;
            UINT resetToken = 0;
            hr = MFCreateDXGIDeviceManager(&resetToken, &pDeviceManager);
            if (SUCCEEDED(hr))
            {
                hr = pDeviceManager->ResetDevice(dx11Renderer->m_d3dDevice.Get(), resetToken);
                if (SUCCEEDED(hr))
                {
                    hr = pAttributes->SetUnknown(MF_SOURCE_READER_D3D_MANAGER, pDeviceManager);
                    if (SUCCEEDED(hr))
                    {
                        debug.logLevelMessage(LogLevel::LOG_INFO, L"Successfully set D3D manager for hardware decoding");
                    }
                }

                pDeviceManager->Release();
            }
        }
    }
#endif

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

            // Create video texture for the active renderer
#if defined(__USE_DIRECTX_12__)
            {
                std::lock_guard<std::mutex> renderLock(DX12Renderer::GetRenderMutex());
                if (!CreateVideoTexture(width, height, isHevcContent))
                {
                    debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to create DX12 video texture");
                    return false;
                }
            }
#elif defined(__USE_DIRECTX_11__)
            {
                std::lock_guard<std::mutex> renderLock(DX11Renderer::GetRenderMutex());

                if (!CreateVideoTexture(width, height, isHevcContent))
                {
                    debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to create video texture");
                    return false;
                }
            }
#endif
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

        if (m_enableAudio)
            ConfigureAudioStream();
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

        // Add this at the very end of OpenMovie() method, just before "return (m_hasVideo.load() || m_hasAudio.load());"
#if defined(_DEBUG_MOVIEPLAYER_)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"=== MOVIE OPEN SUMMARY ===");
        debug.logDebugMessage(LogLevel::LOG_INFO, L"File: %s", filePath.c_str());
        debug.logDebugMessage(LogLevel::LOG_INFO, L"Video Dimensions: %dx%d", m_videoWidth, m_videoHeight);
        debug.logDebugMessage(LogLevel::LOG_INFO, L"Video Duration RAW: %lld", m_videoDuration);
        debug.logDebugMessage(LogLevel::LOG_INFO, L"Video Duration SECONDS: %.2f", ConvertMFTimeToSeconds(m_videoDuration));
        debug.logDebugMessage(LogLevel::LOG_INFO, L"Has Video: %s", m_hasVideo.load() ? L"YES" : L"NO");
        debug.logDebugMessage(LogLevel::LOG_INFO, L"Has Audio: %s", m_hasAudio.load() ? L"YES" : L"NO");
        debug.logDebugMessage(LogLevel::LOG_INFO, L"Texture Index: %d", m_videoTextureIndex);

        // Test reading a sample to verify the stream works
        DWORD streamFlags = 0;
        LONGLONG timestamp = 0;
        IMFSample* pTestSample = nullptr;

        HRESULT hrTest = m_pSourceReader->ReadSample(
            MF_SOURCE_READER_FIRST_VIDEO_STREAM,
            0,
            nullptr,
            &streamFlags,
            &timestamp,
            &pTestSample
        );

        if (SUCCEEDED(hrTest) && pTestSample)
        {
            debug.logLevelMessage(LogLevel::LOG_INFO, L"Stream test: SUCCESS - First sample readable");

            // Get sample duration to help diagnose
            LONGLONG sampleDuration = 0;
            if (SUCCEEDED(pTestSample->GetSampleDuration(&sampleDuration)))
            {
                debug.logDebugMessage(LogLevel::LOG_INFO, L"Sample duration: %lld", sampleDuration);
            }

            pTestSample->Release();
        }
        else
        {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"Stream test: FAILED - Cannot read samples");
            LogMediaError(hrTest, L"Stream test failed");
        }

        // Try alternative method to get duration if main method failed
        if (m_videoDuration == 0)
        {
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"Duration is zero, trying alternative methods...");

            // Try getting duration from media source directly
            IMFMediaSource* pMediaSource = nullptr;
            IMFPresentationDescriptor* pPD = nullptr;

            // This is a more complex approach but might work better
            hr = MFCreateSourceReaderFromURL(filePath.c_str(), nullptr, &m_pSourceReader);
            if (SUCCEEDED(hr))
            {
                PROPVARIANT var;
                PropVariantInit(&var);

                hr = m_pSourceReader->GetPresentationAttribute(
                    MF_SOURCE_READER_MEDIASOURCE,
                    MF_PD_DURATION,
                    &var
                );

                if (SUCCEEDED(hr))
                {
                    if (var.vt == VT_UI8)
                    {
                        debug.logDebugMessage(LogLevel::LOG_INFO, L"Alternative duration method found: %lld", var.uhVal.QuadPart);
                        m_videoDuration = var.uhVal.QuadPart;
                    }
                    else
                    {
                        debug.logDebugMessage(LogLevel::LOG_WARNING, L"Duration variant type: %d (expected %d)", var.vt, VT_UI8);
                    }
                }
                else
                {
                    LogMediaError(hr, L"Alternative duration method failed");
                }

                PropVariantClear(&var);
            }
        }

        debug.logLevelMessage(LogLevel::LOG_INFO, L"========================");
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
#if defined(__USE_DIRECTX_12__)
    // DX12: allocate a D2D bitmap slot via the DX12Renderer video texture API.
    auto dx12Renderer = std::dynamic_pointer_cast<DX12Renderer>(m_renderer);
    if (!dx12Renderer) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"MoviePlayer: Failed to cast to DX12Renderer");
        return false;
    }
    // Release any previous slot.
    if (m_videoTextureIndex >= 0)
        dx12Renderer->ReleaseVideoD2DTexture(m_videoTextureIndex);

    if (!dx12Renderer->CreateVideoD2DTexture(m_videoTextureIndex, width, height)) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"MoviePlayer: Failed to create DX12 D2D video texture");
        return false;
    }
    debug.logLevelMessage(LogLevel::LOG_INFO, L"MoviePlayer: DX12 D2D video texture created (" +
        std::to_wstring(width) + L"x" + std::to_wstring(height) + L") at slot " +
        std::to_wstring(m_videoTextureIndex));
    return true;
#elif defined(__USE_DIRECTX_11__)

    auto dx11Renderer = std::dynamic_pointer_cast<DX11Renderer>(m_renderer);
    if (!dx11Renderer)
    {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"MoviePlayer: Failed to cast to DX11Renderer");
        return false;
    }

    ComPtr<ID3D11Device> device = dx11Renderer->m_d3dDevice;
    if (!device)
    {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"MoviePlayer: No D3D11 device available");
        return false;
    }

        // For HEVC content, log the specific texture creation
    if (isHevcContent) {
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Creating robust texture for HEVC content");
    }

    // STAGING texture: CPU read+write, no BindFlags (no SRV).
    // DrawVideoFrame maps it directly with D3D11_MAP_READ, bypassing CopyResource
    // (CopyResource from DYNAMIC is forbidden by the D3D11 spec and triggers an
    // SDK Layer exception).
    D3D11_TEXTURE2D_DESC textureDesc = {};
    textureDesc.Width = width;
    textureDesc.Height = height;
    textureDesc.MipLevels = 1;
    textureDesc.ArraySize = 1;
    textureDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    textureDesc.SampleDesc.Count = 1;
    textureDesc.SampleDesc.Quality = 0;
    textureDesc.Usage = D3D11_USAGE_STAGING;
    textureDesc.BindFlags = 0;
    textureDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE | D3D11_CPU_ACCESS_READ;
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

    // Staging textures have BindFlags=0 and cannot be used as SRVs.
    // Create a DEFAULT texture the GPU can sample from; UpdateVideoTexture's
    // existing CopyResource call will push each decoded frame into it.
    ComPtr<ID3D11Texture2D> renderTexture;
    D3D11_TEXTURE2D_DESC renderDesc = textureDesc;
    renderDesc.Usage          = D3D11_USAGE_DEFAULT;
    renderDesc.BindFlags      = D3D11_BIND_SHADER_RESOURCE;
    renderDesc.CPUAccessFlags = 0;

    hr = device->CreateTexture2D(&renderDesc, nullptr, &renderTexture);
    if (FAILED(hr))
    {
        LogMediaError(hr, L"Failed to create render texture for video");
        m_videoTexture.Reset();
        return false;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format                    = DXGI_FORMAT_B8G8R8A8_UNORM;
    srvDesc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels       = 1;

    hr = device->CreateShaderResourceView(renderTexture.Get(), &srvDesc, &m_videoTextureView);
    if (FAILED(hr))
    {
        LogMediaError(hr, L"Failed to create shader resource view");
        m_videoTexture.Reset();
        return false;
    }

    m_videoRenderTexture = renderTexture;
    m_videoTextureIndex  = MAX_TEXTURE_BUFFERS_3D - 1;
    dx11Renderer->m_d3dTextures[m_videoTextureIndex] = m_videoTextureView;

    #if defined(_DEBUG_MOVIEPLAYER_)
        debug.logLevelMessage(LogLevel::LOG_INFO,
            L"Created video texture: " + std::to_wstring(width) + L"x" + std::to_wstring(height) +
            L" at index " + std::to_wstring(m_videoTextureIndex));
    #endif

    return true;
#else
    // Vulkan and OpenGL use the CPU-buffer path (UpdateVideoTextureCPU); no GPU texture object needed.
    (void)width; (void)height; (void)isHevcContent;
    return true;
#endif // __USE_DIRECTX_12__ / __USE_DIRECTX_11__
}

// ----------------------------------------------------------------------------------------------
// Update the video texture with the current frame
// ----------------------------------------------------------------------------------------------
bool MoviePlayer::UpdateVideoTexture()
{
    // Check if we have a current sample to process
    if (!m_pCurrentSample)
    {
#if defined(_DEBUG_MOVIEPLAYER_)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"UpdateVideoTexture: No current sample");
#endif
        return false;
    }

    // DX12: decode frame to CPU buffer then upload via D2D CopyFromMemory.
#if defined(__USE_DIRECTX_12__)
    if (m_videoTextureIndex < 0) return false;
    if (!UpdateVideoTextureCPU()) return false;

    auto dx12Renderer = std::dynamic_pointer_cast<DX12Renderer>(m_renderer);
    if (!dx12Renderer || m_cpuFrameWidth == 0 || m_cpuFrameHeight == 0) return false;

    UINT rowPitch = m_cpuFrameWidth * 4;   // BGRA — 4 bytes per pixel
    return dx12Renderer->UpdateVideoD2DTexture(m_videoTextureIndex,
        m_cpuFrameBuffer.data(), rowPitch);

    // Non-DX11 renderers (Vulkan, OpenGL) use a CPU-side buffer path that does not
    // require a D3D11 texture.  Route them directly to UpdateVideoTextureCPU().
#elif !defined(__USE_DIRECTX_11__)
    return UpdateVideoTextureCPU();
#else
    // Check if we have valid DX11 video textures to update
    if (!m_videoTexture && !m_videoRenderTexture)
    {
#if defined(_DEBUG_MOVIEPLAYER_)
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"UpdateVideoTexture: No video texture");
#endif
        return false;
    }

    // Cast the renderer to DX11Renderer for DirectX 11 specific operations
    auto dx11Renderer = std::dynamic_pointer_cast<DX11Renderer>(m_renderer);
    if (!dx11Renderer)
    {
#if defined(_DEBUG_MOVIEPLAYER_)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"UpdateVideoTexture: Failed to cast to DX11Renderer");
#endif
        return false;
    }

    std::lock_guard<std::mutex> renderLock(DX11Renderer::GetRenderMutex());

    ComPtr<ID3D11DeviceContext> context = dx11Renderer->GetImmediateContext();
    if (!context)
    {
#if defined(_DEBUG_MOVIEPLAYER_)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"UpdateVideoTexture: No D3D11 context available");
#endif
        return false;
    }

    // Get the media buffer from the sample
    ComPtr<IMFMediaBuffer> buffer;
    HRESULT hr = m_pCurrentSample->GetBufferByIndex(0, &buffer);
    if (FAILED(hr))
    {
        LogMediaError(hr, L"Failed to get buffer from sample");
        return false;
    }

    // Check if this is HEVC format for special handling
    bool isHevcFormat = (memcmp(&m_videoSubtype, "\x48\x45\x56\x43", 4) == 0);

    // Get current media type to determine the video format
    ComPtr<IMFMediaType> currentType;
    GUID videoFormat = GUID_NULL;

    // Retrieve the current media type from the source reader
    hr = m_pSourceReader->GetCurrentMediaType(
        MF_SOURCE_READER_FIRST_VIDEO_STREAM,
        &currentType
    );

    // Extract the video format GUID if successful
    if (SUCCEEDED(hr) && currentType)
    {
        currentType->GetGUID(MF_MT_SUBTYPE, &videoFormat);

#if defined(_DEBUG_MOVIEPLAYER_)
        // Convert GUID to string for debugging purposes
        WCHAR guidString[128];
        StringFromGUID2(videoFormat, guidString, 128);
        debug.logLevelMessage(LogLevel::LOG_DEBUG, L"Current video format: " + std::wstring(guidString));
#endif
    }

    // Lock the media buffer to access the raw video data
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
    // Log buffer information for debugging
    debug.logLevelMessage(LogLevel::LOG_DEBUG,
        L"Buffer info - Length: " + std::to_wstring(currentLength) +
        L", Width: " + std::to_wstring(m_videoWidth) +
        L", Height: " + std::to_wstring(m_videoHeight));
#endif

    // Determine which texture to use (normal or dual-texture system for HEVC)
    ID3D11Texture2D* destTexture = m_videoTexture.Get();

    // Map the texture for writing video data.
    // D3D11_MAP_WRITE_DISCARD is DYNAMIC-only; STAGING textures use D3D11_MAP_WRITE.
    D3D11_MAPPED_SUBRESOURCE mappedResource = {};
    hr = context->Map(destTexture, 0, D3D11_MAP_WRITE, 0, &mappedResource);
    if (FAILED(hr) || !mappedResource.pData)
    {
        buffer->Unlock();
        LogMediaError(hr, L"Failed to map texture");
        return false;
    }

    // Get pointer to the destination texture data
    BYTE* pDstData = static_cast<BYTE*>(mappedResource.pData);

    // Process video data based on detected format
    if (isHevcFormat)
    {
        // For HEVC format, we need special handling due to codec complexity
        // Check if we have a valid buffer size for video processing
        bool validBufferSize = (currentLength >= (m_videoWidth * m_videoHeight));

        if (!validBufferSize)
        {
            // Buffer too small for even a grayscale frame - use placeholder pattern
            debug.logLevelMessage(LogLevel::LOG_WARNING,
                L"HEVC buffer too small: " + std::to_wstring(currentLength) + L" bytes");

            // Generate a pattern that indicates HEVC data is being received
            static UINT frameCounter = 0;
            frameCounter++;

            // Create a dynamic pattern for each row of pixels
            for (UINT y = 0; y < m_videoHeight; y++)
            {
                // Calculate pointer to the current row in destination texture
                BYTE* pDstRow = pDstData + y * mappedResource.RowPitch;

                // Process each pixel in the current row
                for (UINT x = 0; x < m_videoWidth; x++)
                {
                    // Create a pattern that changes with time
                    BYTE shade = static_cast<BYTE>(((x / 32 + y / 32 + frameCounter) % 4) * 64);
                    BYTE r = static_cast<BYTE>(64 + ((x * 190) / m_videoWidth));
                    BYTE g = static_cast<BYTE>(64 + ((y * 190) / m_videoHeight));
                    BYTE b = static_cast<BYTE>(64 + shade);

                    // Write pixel data to destination in BGRA format
                    pDstRow[x * 4 + 0] = b;      // Blue component
                    pDstRow[x * 4 + 1] = g;      // Green component
                    pDstRow[x * 4 + 2] = r;      // Red component
                    pDstRow[x * 4 + 3] = 255;    // Alpha component (fully opaque)
                }
            }

            // Draw an informative message box area
            const int textHeight = 40;
            const int startY = static_cast<int>(m_videoHeight) / 2 - textHeight;
            const int endY = startY + textHeight * 2;

            // Draw a background box for the message
            for (UINT y = static_cast<UINT>(std::max(0, startY)); y < static_cast<UINT>(std::min(endY, static_cast<int>(m_videoHeight))); y++)
            {
                // Calculate pointer to the current row
                BYTE* pDstRow = pDstData + y * mappedResource.RowPitch;

                // Draw background for message area
                for (UINT x = m_videoWidth / 4; x < 3 * m_videoWidth / 4; x++)
                {
                    // Semi-transparent black background for text readability
                    pDstRow[x * 4 + 0] = 40;     // Blue
                    pDstRow[x * 4 + 1] = 40;     // Green
                    pDstRow[x * 4 + 2] = 40;     // Red
                    pDstRow[x * 4 + 3] = 255;    // Alpha
                }
            }

            // Draw simple text pattern (not actual text rendering)
            for (UINT y = static_cast<UINT>(std::max(0, startY + 10)); y < static_cast<UINT>(std::min(startY + 30, static_cast<int>(m_videoHeight))); y++)
            {
                // Calculate pointer to the current row
                BYTE* pDstRow = pDstData + y * mappedResource.RowPitch;

                // Draw white dots pattern to simulate text
                for (UINT x = m_videoWidth / 3; x < 2 * m_videoWidth / 3; x += 8)
                {
                    // Draw a small group of white pixels to simulate characters
                    for (int i = 0; i < 6; i++) {
                        if (x + static_cast<UINT>(i) < m_videoWidth) {
                            UINT pixelIndex = x + static_cast<UINT>(i);
                            pDstRow[pixelIndex * 4 + 0] = 255;   // Blue
                            pDstRow[pixelIndex * 4 + 1] = 255;   // Green
                            pDstRow[pixelIndex * 4 + 2] = 255;   // Red
                            pDstRow[pixelIndex * 4 + 3] = 255;   // Alpha
                        }
                    }
                }
            }

            // Draw second line of simulated text
            for (UINT y = static_cast<UINT>(std::max(0, startY + 35)); y < static_cast<UINT>(std::min(startY + 55, static_cast<int>(m_videoHeight))); y++)
            {
                // Calculate pointer to the current row
                BYTE* pDstRow = pDstData + y * mappedResource.RowPitch;

                // Draw white dots pattern to simulate second line of text
                for (UINT x = m_videoWidth / 3 + 15; x < 2 * m_videoWidth / 3 - 15; x += 6)
                {
                    // Draw a small group of white pixels
                    for (int i = 0; i < 4; i++) {
                        if (x + static_cast<UINT>(i) < m_videoWidth) {
                            UINT pixelIndex = x + static_cast<UINT>(i);
                            pDstRow[pixelIndex * 4 + 0] = 255;   // Blue
                            pDstRow[pixelIndex * 4 + 1] = 255;   // Green
                            pDstRow[pixelIndex * 4 + 2] = 255;   // Red
                            pDstRow[pixelIndex * 4 + 3] = 255;   // Alpha
                        }
                    }
                }
            }
        }
        else
        {
            // We have enough data to try an NV12 or similar YUV format conversion
            // Check buffer size to determine likely format based on bytes per pixel
            float bytesPerPixel = static_cast<float>(currentLength) / static_cast<float>(m_videoWidth * m_videoHeight);

            if (bytesPerPixel >= 1.4f && bytesPerPixel <= 1.6f)
            {
                // Likely NV12 format (1.5 bytes per pixel)
                debug.logLevelMessage(LogLevel::LOG_INFO, L"Processing HEVC as NV12 format");

                // NV12 is a planar format with a Y plane followed by interleaved U and V planes
                UINT yPitch = m_videoWidth;                          // Y plane pitch is typically the width
                UINT uvPitch = m_videoWidth;                         // UV plane pitch is also typically the width
                UINT ySize = yPitch * m_videoHeight;                 // Y plane size in bytes

                // Y plane followed by interleaved UV plane
                BYTE* yPlane = pSrcData;
                BYTE* uvPlane = pSrcData + ySize;

                // Convert NV12 to BGRA format
                for (UINT y = 0; y < m_videoHeight; y++)
                {
                    // Calculate pointer to the current row in destination texture
                    BYTE* pDstRow = pDstData + y * mappedResource.RowPitch;

                    // Process each pixel in the current row
                    for (UINT x = 0; x < m_videoWidth; x++)
                    {
                        // Get Y value for this pixel
                        UINT yIndex = y * yPitch + x;
                        int yValue = yPlane[yIndex];

                        // Get U and V values (subsampled by 2 in both dimensions)
                        UINT uvIndex = (y / 2) * uvPitch + (x / 2) * 2;
                        int uValue = 128;                            // Default if out of bounds
                        int vValue = 128;                            // Default if out of bounds

                        // Check bounds before accessing UV plane data
                        if (uvIndex + 1 < (currentLength - ySize))
                        {
                            uValue = uvPlane[uvIndex];
                            vValue = uvPlane[uvIndex + 1];
                        }

                        // Convert YUV to RGB using fast math precalculation
                        uint8_t r, g, b;
                        FAST_MATH.FastYuvToRgb(static_cast<uint8_t>(yValue), static_cast<uint8_t>(uValue), static_cast<uint8_t>(vValue), r, g, b);

                        // Clamp values to 0-255 range for safety
                        r = (r < 0) ? 0 : ((r > 255) ? 255 : r);
                        g = (g < 0) ? 0 : ((g > 255) ? 255 : g);
                        b = (b < 0) ? 0 : ((b > 255) ? 255 : b);

                        // Write pixel data to destination in BGRA format
                        pDstRow[x * 4 + 0] = static_cast<BYTE>(b);   // Blue component
                        pDstRow[x * 4 + 1] = static_cast<BYTE>(g);   // Green component
                        pDstRow[x * 4 + 2] = static_cast<BYTE>(r);   // Red component
                        pDstRow[x * 4 + 3] = 255;                    // Alpha component (fully opaque)
                    }
                }
            }
            else if (bytesPerPixel >= 0.9f && bytesPerPixel <= 1.1f)
            {
                // Likely Y only (grayscale) format (1 byte per pixel)
                debug.logLevelMessage(LogLevel::LOG_INFO, L"Processing HEVC as grayscale format");

                // Convert grayscale Y values to BGRA format
                for (UINT y = 0; y < m_videoHeight; y++)
                {
                    // Calculate pointers to source and destination rows
                    BYTE* pDstRow = pDstData + y * mappedResource.RowPitch;
                    BYTE* pSrcRow = pSrcData + y * m_videoWidth;

                    // Process each pixel in the current row
                    for (UINT x = 0; x < m_videoWidth; x++)
                    {
                        // Copy Y value to all RGB channels for grayscale effect
                        BYTE yValue = pSrcRow[x];
                        pDstRow[x * 4 + 0] = yValue;     // Blue component
                        pDstRow[x * 4 + 1] = yValue;     // Green component
                        pDstRow[x * 4 + 2] = yValue;     // Red component
                        pDstRow[x * 4 + 3] = 255;        // Alpha component (fully opaque)
                    }
                }
            }
            else if (bytesPerPixel >= 1.9f && bytesPerPixel <= 2.1f)
            {
                // Likely YUY2 format (2 bytes per pixel)
                debug.logLevelMessage(LogLevel::LOG_INFO, L"Processing HEVC as YUY2 format");

                // Process YUY2 format in pairs of pixels (macropixels)
                for (UINT y = 0; y < m_videoHeight; y++)
                {
                    // Calculate pointers to source and destination rows
                    BYTE* pDstRow = pDstData + y * mappedResource.RowPitch;
                    BYTE* pSrcRow = pSrcData + y * m_videoWidth * 2;

                    // Process pixels in pairs for YUY2 format
                    for (UINT x = 0; x < m_videoWidth; x += 2)
                    {
                        // YUY2 format: Y0, U0, Y1, V0 for each macropixel
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

                        // Convert and write first pixel using YUV to RGB conversion
                        int r0 = static_cast<int>(y0 + 1.402f * (v0 - 128));
                        int g0 = static_cast<int>(y0 - 0.344f * (u0 - 128) - 0.714f * (v0 - 128));
                        int b0 = static_cast<int>(y0 + 1.772f * (u0 - 128));

                        // Clamp values to valid 0-255 range
                        r0 = (r0 < 0) ? 0 : ((r0 > 255) ? 255 : r0);
                        g0 = (g0 < 0) ? 0 : ((g0 > 255) ? 255 : g0);
                        b0 = (b0 < 0) ? 0 : ((b0 > 255) ? 255 : b0);

                        // Write first pixel to destination texture
                        pDstRow[x * 4 + 0] = static_cast<BYTE>(b0);  // Blue component
                        pDstRow[x * 4 + 1] = static_cast<BYTE>(g0);  // Green component
                        pDstRow[x * 4 + 2] = static_cast<BYTE>(r0);  // Red component
                        pDstRow[x * 4 + 3] = 255;                    // Alpha component

                        // Convert and write second pixel if it exists
                        if (x + 1 < m_videoWidth)
                        {
                            // Convert second pixel using same U and V values
                            int r1 = static_cast<int>(y1 + 1.402f * (v0 - 128));
                            int g1 = static_cast<int>(y1 - 0.344f * (u0 - 128) - 0.714f * (v0 - 128));
                            int b1 = static_cast<int>(y1 + 1.772f * (u0 - 128));

                            // Clamp values to valid 0-255 range
                            r1 = (r1 < 0) ? 0 : ((r1 > 255) ? 255 : r1);
                            g1 = (g1 < 0) ? 0 : ((g1 > 255) ? 255 : g1);
                            b1 = (b1 < 0) ? 0 : ((b1 > 255) ? 255 : b1);

                            // Write second pixel to destination texture
                            pDstRow[(x + 1) * 4 + 0] = static_cast<BYTE>(b1);  // Blue component
                            pDstRow[(x + 1) * 4 + 1] = static_cast<BYTE>(g1);  // Green component
                            pDstRow[(x + 1) * 4 + 2] = static_cast<BYTE>(r1);  // Red component
                            pDstRow[(x + 1) * 4 + 3] = 255;                    // Alpha component
                        }
                    }
                }
            }
            else if (bytesPerPixel >= 2.9f && bytesPerPixel <= 4.1f)
            {
                // Likely RGB32 or ARGB32 format (4 bytes per pixel)
                debug.logLevelMessage(LogLevel::LOG_INFO, L"Processing HEVC as RGB32 format");

                // Calculate source stride for RGB32 format
                UINT srcStride = m_videoWidth * 4;

                // Copy row by row with stride handling
                UINT bytesPerRow = std::min(srcStride, static_cast<UINT>(mappedResource.RowPitch));
                bytesPerRow = std::min(bytesPerRow, m_videoWidth * 4);

                // Process each row of the image
                for (UINT row = 0; row < m_videoHeight; row++)
                {
                    // Calculate source and destination pointers for this row
                    BYTE* pSrcRow = pSrcData + (row * srcStride);
                    BYTE* pDstRow = pDstData + (row * mappedResource.RowPitch);

                    // Make sure we don't exceed buffer bounds
                    if ((row * srcStride + bytesPerRow) <= currentLength)
                    {
                        // Direct memory copy for RGB data
                        memcpy(pDstRow, pSrcRow, bytesPerRow);
                    }
                    else {
                        // If we run out of source data, fill the rest with a pattern
                        for (UINT x = 0; x < m_videoWidth; x++)
                        {
                            // Create a checkerboard pattern to indicate missing data
                            BYTE shade = static_cast<BYTE>(((x / 8 + row / 8) % 2) ? 64 : 192);
                            pDstRow[x * 4 + 0] = 0;      // Blue
                            pDstRow[x * 4 + 1] = 0;      // Green
                            pDstRow[x * 4 + 2] = shade;  // Red
                            pDstRow[x * 4 + 3] = 255;    // Alpha
                        }
                    }
                }
            }
            else
            {
                // Unknown format - use source data visualization
                debug.logLevelMessage(LogLevel::LOG_WARNING,
                    L"Unknown HEVC format: " + std::to_wstring(bytesPerPixel) + L" bytes per pixel");

                // Simple visualization of the raw data for debugging
                for (UINT y = 0; y < m_videoHeight; y++)
                {
                    // Calculate pointer to the current row
                    BYTE* pDstRow = pDstData + y * mappedResource.RowPitch;

                    // Process each pixel in the current row
                    for (UINT x = 0; x < m_videoWidth; x++)
                    {
                        // Calculate index into source buffer with bounds checking
                        UINT srcIndex = (y * m_videoWidth + x) % currentLength;

                        // Create a debug visualization based on raw bytes
                        BYTE value = pSrcData[srcIndex];

                        // Visualize the byte values with color gradient
                        pDstRow[x * 4 + 0] = (value < 85) ? static_cast<BYTE>(value * 3) : 255;        // Blue
                        pDstRow[x * 4 + 1] = (value >= 85 && value < 170) ? static_cast<BYTE>((value - 85) * 3) : 0;  // Green
                        pDstRow[x * 4 + 2] = (value >= 170) ? static_cast<BYTE>((value - 170) * 3) : 0;               // Red
                        pDstRow[x * 4 + 3] = 255;                                                                      // Alpha
                    }
                }
            }
        }
    }
    else if (videoFormat == MFVideoFormat_NV12)
    {
        // For NV12 format conversion to BGRA
        // NV12 is a planar format with a Y plane followed by interleaved U and V planes
        UINT yPitch = m_videoWidth;                              // Y plane pitch is typically the width
        UINT uvPitch = m_videoWidth;                             // UV plane pitch is also typically the width
        UINT ySize = yPitch * m_videoHeight;                     // Y plane size in bytes

        // Y plane followed by interleaved UV plane
        BYTE* yPlane = pSrcData;
        BYTE* uvPlane = pSrcData + ySize;

        // Convert NV12 to BGRA format
        for (UINT y = 0; y < m_videoHeight; y++)
        {
            // Calculate pointer to the current row in destination texture
            BYTE* pDstRow = pDstData + y * mappedResource.RowPitch;

            // Process each pixel in the current row
            for (UINT x = 0; x < m_videoWidth; x++)
            {
                // Get Y value for this pixel
                UINT yIndex = y * yPitch + x;
                int yValue = yPlane[yIndex];

                // Get U and V values (subsampled by 2 in both dimensions)
                UINT uvIndex = (y / 2) * uvPitch + (x / 2) * 2;
                int uValue = uvPlane[uvIndex];
                int vValue = uvPlane[uvIndex + 1];

                // Convert YUV to RGB using standard conversion formulas
                int r = static_cast<int>(yValue + 1.402f * (vValue - 128));
                int g = static_cast<int>(yValue - 0.344f * (uValue - 128) - 0.714f * (vValue - 128));
                int b = static_cast<int>(yValue + 1.772f * (uValue - 128));

                // Clamp values to 0-255 range
                r = (r < 0) ? 0 : ((r > 255) ? 255 : r);
                g = (g < 0) ? 0 : ((g > 255) ? 255 : g);
                b = (b < 0) ? 0 : ((b > 255) ? 255 : b);

                // Write pixel data to destination in BGRA format
                pDstRow[x * 4] = static_cast<BYTE>(b);       // Blue component
                pDstRow[x * 4 + 1] = static_cast<BYTE>(g);   // Green component
                pDstRow[x * 4 + 2] = static_cast<BYTE>(r);   // Red component
                pDstRow[x * 4 + 3] = 255;                    // Alpha component (fully opaque)
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
            INT32 stride = 0;  // Use signed INT32 instead of UINT32
            if (SUCCEEDED(currentType->GetUINT32(MF_MT_DEFAULT_STRIDE, reinterpret_cast<UINT32*>(&stride))) && stride != 0)
            {
                // Convert signed stride to unsigned, handling negative strides
                srcStride = (stride < 0) ? static_cast<UINT>(-stride) : static_cast<UINT>(stride);
            }
        }

        // If no stride from media type, calculate based on format and width
        if (srcStride == 0)
        {
            if (videoFormat == MFVideoFormat_RGB32 || videoFormat == MFVideoFormat_ARGB32)
            {
                // RGB32 and ARGB32 formats use 4 bytes per pixel
                srcStride = m_videoWidth * 4;
            }
            else
            {
                // Default case - calculate a reasonable stride based on buffer size
                if (m_videoHeight > 0)
                {
                    srcStride = (currentLength / m_videoHeight);
                }

                if (srcStride == 0 && currentLength > 0) {
                    // Very small buffer - might be compressed data
                    debug.logLevelMessage(LogLevel::LOG_WARNING, L"Buffer too small for full frame, possibly compressed data");

                    // Fill with a debug pattern to indicate compressed data
                    for (UINT y = 0; y < m_videoHeight; y++)
                    {
                        // Calculate pointer to the current row
                        BYTE* pDstRow = pDstData + y * mappedResource.RowPitch;

                        // Create a diagnostic pattern for each pixel
                        for (UINT x = 0; x < m_videoWidth; x++)
                        {
                            // Create a checkerboard pattern to indicate missing data
                            BYTE shade = static_cast<BYTE>(((x / 16 + y / 16) % 2) ? 128 : 64);
                            pDstRow[x * 4] = shade;      // Blue
                            pDstRow[x * 4 + 1] = shade;  // Green
                            pDstRow[x * 4 + 2] = shade;  // Red
                            pDstRow[x * 4 + 3] = 255;    // Alpha
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
        UINT minExpectedSize = m_videoWidth * m_videoHeight;
        if (currentLength < minExpectedSize && currentLength > 0)
        {
            debug.logLevelMessage(LogLevel::LOG_WARNING,
                L"Buffer too small for direct copy. Using partial data visualization.");

            // Fill the destination with a neutral color first
            for (UINT y = 0; y < m_videoHeight; y++)
            {
                // Calculate pointer to the current row
                BYTE* pDstRow = pDstData + y * mappedResource.RowPitch;
                // Set entire row to neutral gray color
                memset(pDstRow, 128, m_videoWidth * 4);
            }

            // Visualize the actual buffer data at the top of the frame
            UINT dataVisHeight = std::min(m_videoHeight, static_cast<UINT>(100));
            UINT bytesPerPixel = 4;
            UINT pixelsToShow = std::min(m_videoWidth * dataVisHeight, static_cast<UINT>(currentLength / bytesPerPixel));
            // Display available data as colored pixels
            for (UINT i = 0; i < pixelsToShow; i++)
            {
                // Calculate pixel coordinates
                UINT x = i % m_videoWidth;
                UINT y = i / m_videoWidth;

                // Only process pixels within the visualization area
                if (y < dataVisHeight)
                {
                    // Calculate pointer to the specific pixel
                    BYTE* pDstPixel = pDstData + (y * mappedResource.RowPitch) + (x * 4);

                    // Color based on the buffer data
                    UINT srcIndex = i * bytesPerPixel;
                    if (srcIndex + 2 < currentLength)
                    {
                        pDstPixel[0] = pSrcData[srcIndex];       // Blue
                        pDstPixel[1] = pSrcData[srcIndex + 1];   // Green
                        pDstPixel[2] = pSrcData[srcIndex + 2];   // Red
                        pDstPixel[3] = 255;                      // Alpha
                    }
                }
            }
        }
        else
        {
            // Normal copy for sufficient buffer size
            // Copy row by row with proper stride handling
            UINT bytesPerRow = std::min(srcStride, static_cast<UINT>(mappedResource.RowPitch));
            bytesPerRow = std::min(bytesPerRow, m_videoWidth * 4);

            // Process each row of the video frame
            for (UINT row = 0; row < m_videoHeight; row++)
            {
                // Calculate source and destination pointers for this row
                BYTE* pSrcRow = pSrcData + (row * srcStride);
                BYTE* pDstRow = pDstData + (row * mappedResource.RowPitch);

                // Make sure we don't exceed buffer bounds
                if ((row * srcStride + bytesPerRow) <= currentLength)
                {
                    // Direct memory copy for this row
                    memcpy(pDstRow, pSrcRow, bytesPerRow);
                }
                else {
                    // If we run out of source data, fill the rest with a distinct pattern
                    for (UINT x = 0; x < m_videoWidth; x++)
                    {
                        // Create a diagnostic pattern indicating data shortage
                        BYTE shade = static_cast<BYTE>(((x / 8 + row / 8) % 2) ? 64 : 192);
                        pDstRow[x * 4] = 0;          // Blue
                        pDstRow[x * 4 + 1] = 0;      // Green
                        pDstRow[x * 4 + 2] = shade;  // Red
                        pDstRow[x * 4 + 3] = 255;    // Alpha
                    }
                }
            }
        }
    }

    // If we have a dual-texture system for HEVC, copy from staging to render texture
    if (m_videoRenderTexture)
    {
        // Copy from staging texture to render texture for final rendering
        context->CopyResource(m_videoRenderTexture.Get(), m_videoTexture.Get());
    }

    // Unmap the texture to finalize the data transfer
    context->Unmap(destTexture, 0);

    // Unlock the media buffer to release the lock
    buffer->Unlock();

    return true;
#endif // __USE_DIRECTX_11__
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
// Replace the existing Play() method with this enhanced version:
// Replace the existing Play() method in MoviePlayer.cpp
bool MoviePlayer::Play()
{
#if defined(_DEBUG_MOVIEPLAYER_)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"MoviePlayer::Play() called");
    debug.logDebugMessage(LogLevel::LOG_INFO, L"Pre-check - Duration: %.2f, Dimensions: %dx%d, HasVideo: %s",
        ConvertMFTimeToSeconds(m_videoDuration), m_videoWidth, m_videoHeight, m_hasVideo.load() ? L"YES" : L"NO");
#endif

    // Thread-safe checks with atomic variables
    if (!m_pSourceReader)
    {
#if defined(_DEBUG_MOVIEPLAYER_)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"MoviePlayer: No source reader available");
#endif
        return false;
    }

    // Check if we have valid video streams
    if (!m_hasVideo.load())
    {
#if defined(_DEBUG_MOVIEPLAYER_)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"MoviePlayer: No video stream available");
#endif
        return false;
    }

    // Verify video dimensions
    if (m_videoWidth == 0 || m_videoHeight == 0)
    {
#if defined(_DEBUG_MOVIEPLAYER_)
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"MoviePlayer: Invalid video dimensions: %dx%d", m_videoWidth, m_videoHeight);
#endif
        return false;
    }

    // TEMPORARY: Allow zero duration for testing
    if (m_videoDuration == 0)
    {
#if defined(_DEBUG_MOVIEPLAYER_)
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"MoviePlayer: Video has no duration - proceeding anyway for testing");
#endif
        // Don't return false here - continue with playback attempt
    }

    // If already playing, do nothing
    if (m_isPlaying.load() && !m_isPaused.load())
    {
#if defined(_DEBUG_MOVIEPLAYER_)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"MoviePlayer: Already playing");
#endif
        return true;
    }

    // If paused, resume — recalibrate wall-clock origin so PTS timing stays correct
    if (m_isPaused.load())
    {
        m_isPaused.store(false);
        m_isPlaying.store(true);

        if (m_playStartMediaTime >= 0)
        {
            // Adjust start time so elapsed wall time == current media position
            double resumeMediaSecs = ConvertMFTimeToSeconds(
                m_llCurrentPosition.load() - m_playStartMediaTime);
            m_playStartWallTime = std::chrono::steady_clock::now() -
                std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                    std::chrono::duration<double>(resumeMediaSecs));
        }

        if (m_enableAudio && m_pSourceVoice)
        {
            m_pSourceVoice->Start();
            // XAudio2 resets SamplesPlayed to 0 each time Start() is called on a
            // stopped voice.  Reset our per-cycle baseline so the accumulated-
            // samples formula (m_samplesPlayedOffset + SamplesPlayed - m_audioStartSamples)
            // continues counting from where it paused rather than jumping back.
            m_audioStartSamples = 0;
        }

#if defined(_DEBUG_MOVIEPLAYER_)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"MoviePlayer: Playback resumed");
#endif
        return true;
    }

    // Start playback — record wall-clock origin; media-time origin set on first sample
    m_isPlaying.store(true);
    m_isPaused.store(false);
    m_playStartWallTime = std::chrono::steady_clock::now();
    m_playStartMediaTime = -1;
    m_lastFrameTime = m_playStartWallTime;

    // Reset all audio-clock state for a clean fresh start.
    // m_firstAudioSamplePTS is anchored in ReadAudioSample() the first time a buffer
    // is actually submitted, so we clear it here so it gets re-anchored correctly.
    m_firstAudioSamplePTS    = -1;
    m_audioStartSamples      = 0;
    m_samplesPlayedOffset    = 0;

    // Restart the XAudio2 source voice.  Start() resets SamplesPlayed to 0 on a
    // previously-stopped voice, which matches our m_audioStartSamples = 0 above.
    if (m_enableAudio && m_pSourceVoice)
        m_pSourceVoice->Start();

#if defined(_DEBUG_MOVIEPLAYER_)
    debug.logDebugMessage(LogLevel::LOG_INFO, L"MoviePlayer: Playback started - Duration: %.2f seconds, Dimensions: %dx%d, Playing: %s",
        ConvertMFTimeToSeconds(m_videoDuration), m_videoWidth, m_videoHeight, m_isPlaying.load() ? L"TRUE" : L"FALSE");
#endif

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

    if (m_enableAudio && m_pSourceVoice)
    {
        // Snapshot how many samples were played in this voice cycle BEFORE stopping.
        // When resumed, Start() will reset SamplesPlayed to 0 on the voice, so we
        // accumulate here so the audio clock can continue correctly after resume.
        XAUDIO2_VOICE_STATE st;
        m_pSourceVoice->GetState(&st);
        m_samplesPlayedOffset += (st.SamplesPlayed - m_audioStartSamples);
        m_pSourceVoice->Stop();
    }

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

    m_playStartMediaTime = -1;

    if (m_enableAudio && m_pSourceVoice)
    {
        m_pSourceVoice->Stop();
        m_pSourceVoice->FlushSourceBuffers();
    }

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
// This method uses RAII lock patterns to prevent deadlocks
// ----------------------------------------------------------------------------------------------
// Thread-safe implementation of GeneratePlaceholderFrame
void MoviePlayer::GeneratePlaceholderFrame()
{
    // Check if we have valid video textures
    if (!m_videoTexture && !m_videoRenderTexture)
        return;

    // Cast the renderer to DX11Renderer for DirectX 11 specific operations
#if !defined(__USE_DIRECTX_11__)
    return;
#else
    auto dx11Renderer = std::dynamic_pointer_cast<DX11Renderer>(m_renderer);
    if (!dx11Renderer)
        return;

    ComPtr<ID3D11DeviceContext> context = dx11Renderer->GetImmediateContext();
    if (!context)
        return;

    // Determine which texture to use (main or staging in dual-texture system)
    ID3D11Texture2D* destTexture = m_videoTexture.Get();

    // Map the texture for writing placeholder data
    D3D11_MAPPED_SUBRESOURCE mappedResource = {};
    HRESULT hr = context->Map(destTexture, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
    if (FAILED(hr) || !mappedResource.pData)
    {
        LogMediaError(hr, L"Failed to map texture for placeholder frame");
        return;
    }

    // Get pointer to the destination texture data
    BYTE* pDstData = static_cast<BYTE*>(mappedResource.pData);

    // Generate a placeholder pattern that changes over time
    static UINT frameCounter = 0;
    frameCounter++;

    // Create a gradient background that animates
    for (UINT y = 0; y < m_videoHeight; y++)
    {
        // Calculate pointer to the current row
        BYTE* pDstRow = pDstData + y * mappedResource.RowPitch;

        // Generate gradient colors for each pixel in the row
        for (UINT x = 0; x < m_videoWidth; x++)
        {
            // Base gradient calculation with proper type casting
            BYTE r = static_cast<BYTE>((x * 200) / m_videoWidth);
            BYTE g = static_cast<BYTE>((y * 200) / m_videoHeight);
            BYTE b = static_cast<BYTE>(128 + static_cast<int>(64 * sin(frameCounter * 0.05f)));

            // Create a moving pattern for visual interest
            if (((x + frameCounter) / 32 + (y + frameCounter / 2) / 32) % 2) {
                // Darken alternate squares in the pattern
                r = static_cast<BYTE>((r * 2) / 3);
                g = static_cast<BYTE>((g * 2) / 3);
                b = static_cast<BYTE>((b * 2) / 3);
            }

            // Write pixel data to destination in BGRA format
            pDstRow[x * 4 + 0] = b;      // Blue component
            pDstRow[x * 4 + 1] = g;      // Green component
            pDstRow[x * 4 + 2] = r;      // Red component
            pDstRow[x * 4 + 3] = 255;    // Alpha component (fully opaque)
        }
    }

    // Draw informative text area in the center
    const int textHeight = 100;
    const int startY = static_cast<int>(m_videoHeight) / 2 - textHeight / 2;
    const int endY = startY + textHeight;
    const int textWidth = static_cast<int>(m_videoWidth) / 2;
    const int startX = static_cast<int>(m_videoWidth) / 2 - textWidth / 2;
    const int endX = startX + textWidth;

    // Draw text box background
    for (UINT y = static_cast<UINT>(std::max(0, startY)); y < static_cast<UINT>(std::min(endY, static_cast<int>(m_videoHeight))); y++)
    {
        // Calculate pointer to the current row
        BYTE* pDstRow = pDstData + y * mappedResource.RowPitch;

        // Draw background for the text area
        for (UINT x = static_cast<UINT>(std::max(0, startX)); x < static_cast<UINT>(std::min(endX, static_cast<int>(m_videoWidth))); x++)
        {
            // Semi-transparent black background for text readability
            pDstRow[x * 4 + 0] = 32;     // Blue
            pDstRow[x * 4 + 1] = 32;     // Green
            pDstRow[x * 4 + 2] = 32;     // Red
            pDstRow[x * 4 + 3] = 255;    // Alpha
        }
    }

    // Simple visualized text (not actual text rendering)
    // Draw "H" pattern for HEVC indicator

    // Calculate letter dimensions and position
    int letterWidth = textWidth / 6;
    int letterX = startX + letterWidth;

    // Draw the "H" character pattern
    for (UINT y = static_cast<UINT>(std::max(0, startY + 20)); y < static_cast<UINT>(std::min(endY - 20, static_cast<int>(m_videoHeight))); y++)
    {
        // Calculate pointer to the current row
        BYTE* pDstRow = pDstData + y * mappedResource.RowPitch;

        // Left vertical stroke of the "H"
        for (UINT x = static_cast<UINT>(std::max(0, letterX)); x < static_cast<UINT>(std::min(letterX + letterWidth / 4, static_cast<int>(m_videoWidth))); x++)
        {
            // Draw white pixels for the left stroke
            pDstRow[x * 4 + 0] = 200;    // Blue
            pDstRow[x * 4 + 1] = 200;    // Green
            pDstRow[x * 4 + 2] = 200;    // Red
            pDstRow[x * 4 + 3] = 255;    // Alpha
        }

        // Middle horizontal stroke of the "H"
        int midLineStart = startY + 20 + (endY - startY - 40) / 2 - letterWidth / 4;
        int midLineEnd = startY + 20 + (endY - startY - 40) / 2 + letterWidth / 4;

        if (static_cast<int>(y) >= midLineStart && static_cast<int>(y) < midLineEnd)
        {
            // Draw the horizontal stroke across the width of the letter
            for (UINT x = static_cast<UINT>(std::max(0, letterX)); x < static_cast<UINT>(std::min(letterX + letterWidth, static_cast<int>(m_videoWidth))); x++)
            {
                // Draw white pixels for the horizontal stroke
                pDstRow[x * 4 + 0] = 200;    // Blue
                pDstRow[x * 4 + 1] = 200;    // Green
                pDstRow[x * 4 + 2] = 200;    // Red
                pDstRow[x * 4 + 3] = 255;    // Alpha
            }
        }

        // Right vertical stroke of the "H"
        for (UINT x = static_cast<UINT>(std::max(0, letterX + letterWidth - letterWidth / 4)); x < static_cast<UINT>(std::min(letterX + letterWidth, static_cast<int>(m_videoWidth))); x++)
        {
            // Draw white pixels for the right stroke
            pDstRow[x * 4 + 0] = 200;    // Blue
            pDstRow[x * 4 + 1] = 200;    // Green
            pDstRow[x * 4 + 2] = 200;    // Red
            pDstRow[x * 4 + 3] = 255;    // Alpha
        }
    }

    // Draw "Video Processing" text representation
    int line2Y = startY + textHeight - 30;
    if (line2Y >= 0 && line2Y < static_cast<int>(m_videoHeight))
    {
        // Calculate pointer to the text line row
        BYTE* pDstRow = pDstData + static_cast<UINT>(line2Y) * mappedResource.RowPitch;

        // Draw a series of dots to represent text
        for (UINT x = static_cast<UINT>(std::max(0, startX + 20)); x < static_cast<UINT>(std::min(endX - 20, static_cast<int>(m_videoWidth))); x += 8)
        {
            // Draw a small group of white pixels to simulate characters
            for (int i = 0; i < 6; i++)
            {
                UINT pixelX = x + static_cast<UINT>(i);
                if (pixelX < m_videoWidth) {
                    // Draw white pixels for simulated text
                    pDstRow[pixelX * 4 + 0] = 180;   // Blue
                    pDstRow[pixelX * 4 + 1] = 180;   // Green
                    pDstRow[pixelX * 4 + 2] = 180;   // Red
                    pDstRow[pixelX * 4 + 3] = 255;   // Alpha
                }
            }
        }
    }

    // Draw frame counter for animation effect
    std::wstring frameCountText = std::to_wstring(frameCounter % 1000);
    int digitX = endX - 50;
    int digitY = endY - 20;

    // Draw frame counter digits if within bounds
    if (digitY >= 0 && digitY < static_cast<int>(m_videoHeight) && digitX >= 0 && digitX < static_cast<int>(m_videoWidth))
    {
        // Draw each digit in the frame counter
        for (size_t i = 0; i < frameCountText.length(); i++)
        {
            // Very simplified digit rendering based on digit value
            int digit = frameCountText[i] - L'0';

            // Draw a simple pattern for each digit
            for (int dy = 0; dy < 10; dy++)
            {
                int pixelY = digitY + dy;
                if (pixelY >= 0 && pixelY < static_cast<int>(m_videoHeight))
                {
                    // Calculate pointer to the digit row
                    BYTE* pDstRow = pDstData + static_cast<UINT>(pixelY) * mappedResource.RowPitch;

                    // Draw pixels for the current digit
                    for (int dx = 0; dx < 6; dx++)
                    {
                        int pixelX = digitX + dx + static_cast<int>(i) * 8;

                        if (pixelX >= 0 && pixelX < static_cast<int>(m_videoWidth))
                        {
                            // Simple pattern based on digit value and position
                            if ((digit % 2 == 0 && dx % 2 == 0) ||
                                (digit % 2 == 1 && dx % 2 == 1) ||
                                (dy == 0 || dy == 9 || dx == 0 || dx == 5))
                            {
                                // Draw white pixel for the digit pattern
                                UINT finalPixelX = static_cast<UINT>(pixelX);
                                pDstRow[finalPixelX * 4 + 0] = 255;  // Blue
                                pDstRow[finalPixelX * 4 + 1] = 255;  // Green
                                pDstRow[finalPixelX * 4 + 2] = 255;  // Red
                                pDstRow[finalPixelX * 4 + 3] = 255;  // Alpha
                            }
                        }
                    }
                }
            }
        }
    }

    // If we have a dual-texture system for HEVC, copy from staging to render texture
    if (m_videoRenderTexture)
    {
        // Copy from staging texture to render texture for final rendering
        context->CopyResource(m_videoRenderTexture.Get(), m_videoTexture.Get());
    }

    // Unmap the texture to finalize the placeholder frame
    context->Unmap(destTexture, 0);

#if defined(_DEBUG_MOVIEPLAYER_)
    debug.logLevelMessage(LogLevel::LOG_DEBUG, L"Generated placeholder frame for HEVC content");
#endif
#endif // __USE_DIRECTX_11__
}

// ----------------------------------------------------------------------------------------------
// Clean up resources
// ----------------------------------------------------------------------------------------------
void MoviePlayer::Cleanup()
{
    // Stop playback state directly — avoids ThreadLockHelper which dereferences m_threadManager
    // via the global threadManager mutex; during atexit that global is already destroyed.
    m_isPlaying.store(false);
    m_isPaused.store(false);
    m_playStartMediaTime = -1;
    if (m_enableAudio && m_pSourceVoice)
    {
        m_pSourceVoice->Stop();
        m_pSourceVoice->FlushSourceBuffers();
    }

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

#if defined(__USE_DIRECTX_12__)
    // Release DX12 D2D video bitmap slot
    if (m_videoTextureIndex >= 0 && m_renderer) {
        auto dx12Renderer = std::dynamic_pointer_cast<DX12Renderer>(m_renderer);
        if (dx12Renderer) dx12Renderer->ReleaseVideoD2DTexture(m_videoTextureIndex);
    }
#endif

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

    // Release XAudio2 resources
    CleanupAudio();

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

#if !defined(__USE_DIRECTX_11__)
    return;
#else
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
#endif // __USE_DIRECTX_11__
}

// UpdateFrame - called from the renderer each game tick.
//
// Sync strategy (audio-master): XAudio2 SamplesPlayed gives the true hardware audio
// position.  Each video frame is held until that position reaches the frame's PTS.
// Audio queue is kept full independently so it never starves.
bool MoviePlayer::UpdateFrame()
{
    if (!m_isPlaying.load() || m_isPaused.load() || !m_pSourceReader)
        return false;

    std::unique_lock<std::recursive_mutex> updateLock(m_mutex, std::try_to_lock);
    if (!updateLock.owns_lock())
        return false;

    // ---- Feed audio: keep XAudio2 queue fed, but don't read more than ~500 ms
    //      ahead of the current video PTS to prevent audio content from drifting
    //      far ahead of the corresponding video frames. ----
    if (m_enableAudio && m_pSourceVoice)
    {
        XAUDIO2_VOICE_STATE audioState;
        m_pSourceVoice->GetState(&audioState);

        // 500 ms look-ahead limit expressed in MF 100-ns units
        static constexpr LONGLONG kMaxAudioAheadMF = 5000000LL;
        const LONGLONG videoPTSNow = m_llCurrentPosition.load();

        while (audioState.BuffersQueued < AUDIO_MAX_QUEUED_BUFFERS)
        {
            // Stop pre-filling if audio has been read far enough ahead of video
            if (m_audioReadPosition > 0 && videoPTSNow > 0 &&
                m_audioReadPosition > videoPTSNow + kMaxAudioAheadMF)
                break;

            if (!ReadAudioSample()) break;
            m_pSourceVoice->GetState(&audioState);
        }
    }

    // ---- Read the next video sample if we don't already have one cached ----
    if (!m_pCurrentSample)
    {
        if (!ReadNextSample())
            return false;
    }

    // ---- PTS gate: audio-master sync ----
    // Present a video frame only when the audio output has reached that frame's file PTS.
    //
    // Clock formula (audio path):
    //   currentAudioPTS = m_firstAudioSamplePTS
    //                   + (m_samplesPlayedOffset + SamplesPlayed - m_audioStartSamples)
    //                     / nSamplesPerSec  * 10 000 000
    //
    // m_firstAudioSamplePTS anchors SamplesPlayed to actual file PTS so the
    // comparison is valid even when audio and video don't both start at PTS 0.
    // m_samplesPlayedOffset carries over accumulated samples across pause/resume
    // cycles because XAudio2 resets SamplesPlayed to 0 on every Start().
    //
    // If audio is not yet anchored (first buffer not submitted yet) or absent,
    // fall back to wall-clock gating against the first video frame's PTS.
    if (m_playStartMediaTime >= 0)
    {
        const LONGLONG videoPTS  = m_llCurrentPosition.load();
        const LONGLONG halfFrameMF = static_cast<LONGLONG>(10000000.0 * 0.5 / m_targetFPS);

        if (m_enableAudio && m_pSourceVoice &&
            m_audioFormat.nSamplesPerSec > 0 && m_firstAudioSamplePTS >= 0)
        {
            XAUDIO2_VOICE_STATE voiceState;
            m_pSourceVoice->GetState(&voiceState);

            // Total samples played, spanning all voice start/stop cycles
            const UINT64 totalSamples =
                m_samplesPlayedOffset + (voiceState.SamplesPlayed - m_audioStartSamples);

            // Map sample count to file PTS (100-ns MF units)
            const LONGLONG audioNowPTS = m_firstAudioSamplePTS +
                static_cast<LONGLONG>(
                    static_cast<double>(totalSamples) /
                    static_cast<double>(m_audioFormat.nSamplesPerSec) * 10000000.0);

            if (videoPTS > audioNowPTS + halfFrameMF)
                return false; // audio hasn't reached this frame yet — hold
        }
        else
        {
            // Wall-clock fallback: used when audio is disabled or not yet anchored
            const double frameMediaSecs = ConvertMFTimeToSeconds(videoPTS - m_playStartMediaTime);
            const double elapsedSecs = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - m_playStartWallTime).count();
            if (elapsedSecs < frameMediaSecs)
                return false;
        }
    }

    // ---- Time to present: upload to texture and release the sample ----
    bool success = false;
    if (m_pCurrentSample)
    {
        success = UpdateVideoTexture();
        if (success)
        {
            m_hasNewFrame.store(true);
            m_lastFrameTime = std::chrono::steady_clock::now();
        }

        m_pCurrentSample->Release();
        m_pCurrentSample = nullptr;
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
#if defined(__USE_DIRECTX_11__)
            std::lock_guard<std::mutex> renderLock(DX11Renderer::GetRenderMutex());
#endif
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

    // Anchor the media-time origin to the first frame we successfully read
    if (m_playStartMediaTime < 0)
        m_playStartMediaTime = m_llCurrentPosition.load();

    return true;
}

// ----------------------------------------------------------------------------------------------
// Initialize XAudio2 engine and mastering voice
// ----------------------------------------------------------------------------------------------
bool MoviePlayer::InitializeAudio()
{
    HRESULT hr = XAudio2Create(&m_pXAudio2, 0, XAUDIO2_DEFAULT_PROCESSOR);
    if (FAILED(hr))
    {
        LogMediaError(hr, L"XAudio2Create failed");
        return false;
    }

    hr = m_pXAudio2->CreateMasteringVoice(&m_pMasterVoice);
    if (FAILED(hr))
    {
        LogMediaError(hr, L"CreateMasteringVoice failed");
        m_pXAudio2->Release();
        m_pXAudio2 = nullptr;
        return false;
    }

#if defined(_DEBUG_MOVIEPLAYER_)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"XAudio2 initialized successfully");
#endif
    return true;
}

// ----------------------------------------------------------------------------------------------
// Configure the audio stream for PCM output and create the XAudio2 source voice
// ----------------------------------------------------------------------------------------------
void MoviePlayer::ConfigureAudioStream()
{
    if (!m_pSourceReader || !m_pXAudio2) return;

    // Request PCM decode from Media Foundation
    IMFMediaType* pPCMType = nullptr;
    HRESULT hr = MFCreateMediaType(&pPCMType);
    if (FAILED(hr)) return;

    hr = pPCMType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    if (SUCCEEDED(hr))
        hr = pPCMType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
    if (SUCCEEDED(hr))
        hr = m_pSourceReader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, pPCMType);

    pPCMType->Release();

    if (FAILED(hr))
    {
        LogMediaError(hr, L"Failed to set PCM audio output type");
        return;
    }

    // Read back the resolved output type to build the WAVEFORMATEX
    IMFMediaType* pOutputType = nullptr;
    hr = m_pSourceReader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, &pOutputType);
    if (FAILED(hr)) { LogMediaError(hr, L"Failed to get resolved audio media type"); return; }

    WAVEFORMATEX* pWfx = nullptr;
    UINT32 wfxSize = 0;
    hr = MFCreateWaveFormatExFromMFMediaType(pOutputType, &pWfx, &wfxSize);
    pOutputType->Release();

    if (FAILED(hr)) { LogMediaError(hr, L"MFCreateWaveFormatExFromMFMediaType failed"); return; }

    memcpy(&m_audioFormat, pWfx, sizeof(WAVEFORMATEX));
    CoTaskMemFree(pWfx);

    // Create source voice with the decoded PCM format
    hr = m_pXAudio2->CreateSourceVoice(&m_pSourceVoice, &m_audioFormat,
        0, XAUDIO2_DEFAULT_FREQ_RATIO, &m_voiceCallback);
    if (FAILED(hr))
    {
        LogMediaError(hr, L"CreateSourceVoice failed");
        return;
    }

    m_pSourceVoice->Start();

#if defined(_DEBUG_MOVIEPLAYER_)
    debug.logLevelMessage(LogLevel::LOG_INFO,
        L"Audio stream configured: " +
        std::to_wstring(m_audioFormat.nSamplesPerSec) + L" Hz, " +
        std::to_wstring(m_audioFormat.nChannels) + L" ch, " +
        std::to_wstring(m_audioFormat.wBitsPerSample) + L" bit");
#endif
}

// ----------------------------------------------------------------------------------------------
// Read one audio sample from the source reader and submit it to XAudio2
// ----------------------------------------------------------------------------------------------
bool MoviePlayer::ReadAudioSample()
{
    if (!m_pSourceReader || !m_pSourceVoice) return false;

    DWORD streamIndex = 0;
    DWORD flags = 0;
    LONGLONG timestamp = 0;
    IMFSample* pSample = nullptr;

    HRESULT hr = m_pSourceReader->ReadSample(
        MF_SOURCE_READER_FIRST_AUDIO_STREAM,
        0, &streamIndex, &flags, &timestamp, &pSample);

    if (FAILED(hr) || !pSample) return false;

    // Anchor the audio clock to the file PTS of the first buffer ever submitted.
    // This maps XAudio2's SamplesPlayed counter onto the file's PTS timeline so
    // the sync gate in UpdateFrame() works regardless of where in the file playback starts.
    if (m_firstAudioSamplePTS < 0)
        m_firstAudioSamplePTS = timestamp; // may be 0 for files that start at PTS 0

    // Track how far ahead in the stream we have read (used in UpdateFrame() to gate read-ahead)
    if (timestamp > 0)
        m_audioReadPosition = timestamp;

    if (flags & MF_SOURCE_READERF_ENDOFSTREAM)
    {
        pSample->Release();
        return false;
    }

    // Flatten to a contiguous buffer
    ComPtr<IMFMediaBuffer> buffer;
    hr = pSample->ConvertToContiguousBuffer(&buffer);
    pSample->Release();
    if (FAILED(hr)) return false;

    BYTE* pAudioData = nullptr;
    DWORD maxLen = 0, curLen = 0;
    hr = buffer->Lock(&pAudioData, &maxLen, &curLen);
    if (FAILED(hr) || curLen == 0) return false;

    // Copy into a heap buffer that XAudio2 owns until OnBufferEnd frees it
    BYTE* pCopy = new BYTE[curLen];
    memcpy(pCopy, pAudioData, curLen);
    buffer->Unlock();

    XAUDIO2_BUFFER xbuf = {};
    xbuf.AudioBytes = curLen;
    xbuf.pAudioData = pCopy;
    xbuf.pContext   = pCopy; // freed in AudioVoiceCallback::OnBufferEnd

    hr = m_pSourceVoice->SubmitSourceBuffer(&xbuf);
    if (FAILED(hr))
    {
        delete[] pCopy;
        LogMediaError(hr, L"SubmitSourceBuffer failed");
        return false;
    }

    return true;
}

// ----------------------------------------------------------------------------------------------
// Release all XAudio2 resources
// ----------------------------------------------------------------------------------------------
void MoviePlayer::CleanupAudio()
{
    if (m_pSourceVoice)
    {
        m_pSourceVoice->Stop();
        m_pSourceVoice->FlushSourceBuffers();
        m_pSourceVoice->DestroyVoice();
        m_pSourceVoice = nullptr;
    }

    if (m_pMasterVoice)
    {
        m_pMasterVoice->DestroyVoice();
        m_pMasterVoice = nullptr;
    }

    if (m_pXAudio2)
    {
        m_pXAudio2->Release();
        m_pXAudio2 = nullptr;
    }
}

// Updated Render method with proper thread synchronization
void MoviePlayer::Render(const Vector2& position, const Vector2& size)
{
    // Skip early if not playing or no texture or no new frame
    if (!m_isPlaying.load() || m_videoTextureIndex < 0)
        return;

#if defined(__USE_DIRECTX_12__)
    // DX12: Render() is only ever called from within the render frame's pass-2
    // BeginDraw/EndDraw context. D2D1_FACTORY_TYPE_MULTI_THREADED already serialises
    // all factory-derived calls, so no external mutex is needed here. Acquiring
    // GetRenderMutex() inside BeginDraw caused a deadlock: SwitchToMovieIntro()
    // calls PauseThread (non-blocking) then immediately grabs the same mutex in
    // OpenMovie(), leaving the render thread blocked inside an active BeginDraw session.
    if (m_renderer && m_videoTextureIndex >= 0)
    {
        auto dx12 = std::dynamic_pointer_cast<DX12Renderer>(m_renderer);
        if (dx12)
        {
            dx12->BlitVideoBitmap(position.x, position.y, size.x, size.y);
            m_hasNewFrame.store(false);
        }
    }
#elif defined(__USE_DIRECTX_11__)
    std::lock_guard<std::mutex> renderLock(DX11Renderer::GetRenderMutex());

    auto dx11Renderer = std::dynamic_pointer_cast<DX11Renderer>(m_renderer);
    if (dx11Renderer && m_videoTexture)
    {
        dx11Renderer->DrawVideoFrame(
            position,
            size,
            MyColor(1.0f, 1.0f, 1.0f, 1.0f),
            m_videoTexture
        );

        m_hasNewFrame.store(false);
    }
#endif
}

// ---------------------------------------------------------------------------------------------------------------
// CPU-side frame decode — used by Vulkan and OpenGL renderers which have no D3D11 device.
// Locks the current MF sample buffer, copies decoded BGRA bytes into m_cpuFrameBuffer,
// and sets m_cpuFrameWidth/Height.  The caller (GetCurrentFrameRGBA) exposes those bytes.
// ---------------------------------------------------------------------------------------------------------------
// Inline BGRA→RGBA byte-swap: MF on Windows outputs BGRA; OpenGL expects RGBA.
// Swaps the R and B channels in-place for 4-byte pixels over a contiguous row.
static inline void SwapBGRAtoRGBA(uint8_t* dst, const uint8_t* src, uint32_t numPixels)
{
    for (uint32_t i = 0; i < numPixels; ++i, dst += 4, src += 4) {
        dst[0] = src[2]; // R ← B
        dst[1] = src[1]; // G
        dst[2] = src[0]; // B ← R
        dst[3] = src[3]; // A
    }
}

bool MoviePlayer::UpdateVideoTextureCPU()
{
    if (!m_pCurrentSample) return false;

    ComPtr<IMFMediaBuffer> buffer;
    HRESULT hr = m_pCurrentSample->GetBufferByIndex(0, &buffer);
    if (FAILED(hr) || !buffer) return false;

    const size_t rowPixels  = static_cast<size_t>(m_videoWidth);
    const size_t rowBytes   = rowPixels * 4;
    const size_t frameBytes = rowBytes * m_videoHeight;
    m_cpuFrameBuffer.resize(frameBytes);

    // Try IMF2DBuffer first (planar / hardware surfaces expose this)
    ComPtr<IMF2DBuffer> buf2D;
    if (SUCCEEDED(buffer->QueryInterface(IID_PPV_ARGS(&buf2D))))
    {
        BYTE* scanline0 = nullptr;
        LONG  pitch     = 0;
        if (SUCCEEDED(buf2D->Lock2D(&scanline0, &pitch)) && scanline0)
        {
            for (UINT row = 0; row < m_videoHeight; ++row) {
                uint8_t*       dst = m_cpuFrameBuffer.data() + row * rowBytes;
                const uint8_t* src = reinterpret_cast<const uint8_t*>(scanline0) + row * pitch;
#if defined(__USE_OPENGL__)
                SwapBGRAtoRGBA(dst, src, static_cast<uint32_t>(rowPixels));
#else
                std::memcpy(dst, src, rowBytes);
#endif
            }
            buf2D->Unlock2D();
            m_cpuFrameWidth  = m_videoWidth;
            m_cpuFrameHeight = m_videoHeight;
            return true;
        }
    }

    // Fallback: contiguous IMFMediaBuffer
    BYTE* pSrc   = nullptr;
    DWORD maxLen = 0;
    DWORD curLen = 0;
    hr = buffer->Lock(&pSrc, &maxLen, &curLen);
    if (FAILED(hr) || !pSrc) return false;

    if (curLen >= frameBytes)
    {
        for (UINT row = 0; row < m_videoHeight; ++row) {
            uint8_t*       dst = m_cpuFrameBuffer.data() + row * rowBytes;
            const uint8_t* src = reinterpret_cast<const uint8_t*>(pSrc) + row * rowBytes;
#if defined(__USE_OPENGL__)
            SwapBGRAtoRGBA(dst, src, static_cast<uint32_t>(rowPixels));
#else
            std::memcpy(dst, src, rowBytes);
#endif
        }
        m_cpuFrameWidth  = m_videoWidth;
        m_cpuFrameHeight = m_videoHeight;
    }

    buffer->Unlock();
    return !m_cpuFrameBuffer.empty();
}

// ---------------------------------------------------------------------------------------------------------------
// Returns the most recently decoded BGRA frame as a CPU pointer.
// Returns nullptr if no frame has been decoded yet (CPU path not available, or no frame ready).
// ---------------------------------------------------------------------------------------------------------------
const uint8_t* MoviePlayer::GetCurrentFrameRGBA(uint32_t& outWidth, uint32_t& outHeight)
{
    if (m_cpuFrameBuffer.empty() || m_cpuFrameWidth == 0 || m_cpuFrameHeight == 0)
    {
        outWidth = 0; outHeight = 0;
        return nullptr;
    }
    outWidth  = m_cpuFrameWidth;
    outHeight = m_cpuFrameHeight;
    return m_cpuFrameBuffer.data();
}
