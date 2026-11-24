// ScreenRecorder.cpp
// ==================
// Real-time D3D11 back-buffer + WASAPI loopback -> H.264/AAC MP4.
//
// MicMode::Mixed:
//   AudioCaptureThread polls the mic WASAPI client inline after each game
//   audio packet.  No separate thread, no shared buffer, no race conditions.
//   Mic samples are blended into the game audio buffer before AAC encoding.
//
// MicMode::Separate:
//   Dedicated MicCaptureThread writes mic PCM to a second AAC stream.
// ------------------------------------------------------------------------------------
#include "Includes.h"
#include "ScreenRecorder.h"
#include "Debug.h"
#if defined(PLATFORM_WINDOWS)
    #include <shellapi.h>
    #include <mmreg.h>
    #include <functiondiscoverykeys_devpkey.h>   // PKEY_Device_FriendlyName / PKEY_AudioEndpoint_FormFactor
    #pragma comment(lib, "shell32.lib")
#endif
#include <cmath>                              // log10f - mic level diagnostic

extern Debug debug;
ScreenRecorder gameplayRecorder;

#if defined(PLATFORM_WINDOWS)

void ScreenRecorder::SetCaptureEnabled(bool enabled)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_captureEnabled.load() == enabled) return;
    LARGE_INTEGER now; QueryPerformanceCounter(&now);
    if (!enabled) m_pauseStart = now.QuadPart;
    else if (m_pauseStart) {
        m_pausedTicks += now.QuadPart - m_pauseStart;
        m_pauseStart = 0; m_lastVideoQpc = now;
    }
    m_captureEnabled.store(enabled);
}

static constexpr UINT32 VIDEO_BITRATE = 10'000'000;

static std::wstring HRStr(HRESULT hr)
{
    wchar_t s[16];
    swprintf_s(s, L"0x%08X", static_cast<unsigned>(hr));
    return s;
}

static bool IsIEEEFloat(const WAVEFORMATEX* pFmt)
{
    if (pFmt->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) return true;
    if (pFmt->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
        return reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(pFmt)->SubFormat.Data1 == 0x00000003;
    return false;
}

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------
ScreenRecorder::ScreenRecorder()
    : m_pSinkWriter(nullptr)
    , m_videoStreamIndex(0), m_audioStreamIndex(0)
    , m_width(0), m_height(0)
    , m_targetFPS(RecordFPS::FPS_60)
    , m_audioBitrate(AudioBitrate::Kbps_192)
    , m_framePeriod(0), m_videoFrameIndex(-1), m_framePeriodSnapshot(0)
    , m_audioTimestamp(0)
    , m_pAudioClient(nullptr), m_pCaptureClient(nullptr), m_pWaveFormat(nullptr)
    , m_micMode(MicMode::Off)
    , m_pMicAudioClient(nullptr), m_pMicCaptureClient(nullptr), m_pMicWaveFormat(nullptr)
    , m_micAccumRead(0)
    , m_micDataLogged(false)
    , m_micDiagPeak(0.0f), m_micDiagFrames(0)
    , m_micStreamIndex(0), m_micAudioTimestamp(0)
    , m_pMonitorAudioClient(nullptr), m_pMonitorRenderClient(nullptr)
    , m_pMonitorFormat(nullptr), m_monitorBufferFrames(0)
    , m_monitorOnLoopbackDevice(false)
    , m_micMonitorGain(2.5f), m_micRecordGain(2.5f)
    , m_isRecording(false)
{
    m_qpcRecordingStart.QuadPart = 0;
    m_qpcFreq.QuadPart           = 0;
    m_lastVideoQpc.QuadPart      = 0;
}

ScreenRecorder::~ScreenRecorder()
{
    if (m_isRecording.load()) StopRecording();
}

// ---------------------------------------------------------------------------
// StartRecording
// ---------------------------------------------------------------------------
bool ScreenRecorder::StartRecording(UINT width, UINT height,
                                    RecordFPS fps,
                                    const std::wstring& outputPath,
                                    MicMode micMode,
                                    AudioBitrate audioBitrate, bool captureAudio)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_isRecording.load()) return false;

    UINT32 fpsVal = static_cast<UINT32>(fps);
    if (fpsVal != 30 && fpsVal != 60 && fpsVal != 120)
    {
        debug.logDiagLevelMessage(LogLevel::LOG_WARNING,
            L"ScreenRecorder: Unsupported fps " + std::to_wstring(fpsVal) + L" - defaulting to 60fps");
        fpsVal = 60; fps = RecordFPS::FPS_60;
    }

    m_width               = width  & ~1u;   // NVENC NV12 requires even dimensions
    m_height              = height & ~1u;
    m_targetFPS           = fps;
    m_framePeriod         = 10'000'000LL / static_cast<LONGLONG>(fpsVal);
    m_framePeriodSnapshot = m_framePeriod;
    m_videoFrameIndex.store(0);
    m_captureEnabled.store(true); m_pauseStart = m_pausedTicks = 0;
    m_audioTimestamp      = 0;
    m_micMode             = micMode;
    m_audioBitrate        = audioBitrate;
    m_micAudioTimestamp   = 0;
    m_micAccum.clear();
    m_micAccumRead        = 0;
    m_micDataLogged       = false;
    m_micDiagPeak         = 0.0f;
    m_micDiagFrames       = 0;

    // 1. Sink writer
    IMFAttributes* pAttribs = nullptr;
    HRESULT hr = MFCreateAttributes(&pAttribs, 3);
    if (SUCCEEDED(hr))
    {
        pAttribs->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
        pAttribs->SetUINT32(MF_SINK_WRITER_DISABLE_THROTTLING,       TRUE);
    }
    hr = MFCreateSinkWriterFromURL(outputPath.c_str(), nullptr, pAttribs, &m_pSinkWriter);
    if (pAttribs) { pAttribs->Release(); pAttribs = nullptr; }
    if (FAILED(hr))
    {
        debug.logDiagLevelMessage(LogLevel::LOG_ERROR,
            L"ScreenRecorder: MFCreateSinkWriterFromURL failed (" + HRStr(hr) + L")");
        return false;
    }

    // 2. H.264 output
    {
        IMFMediaType* pOut = nullptr;
        hr = MFCreateMediaType(&pOut);
        if (SUCCEEDED(hr)) hr = pOut->SetGUID   (MF_MT_MAJOR_TYPE,     MFMediaType_Video);
        if (SUCCEEDED(hr)) hr = pOut->SetGUID   (MF_MT_SUBTYPE,        MFVideoFormat_H264);
        if (SUCCEEDED(hr)) hr = pOut->SetUINT32 (MF_MT_AVG_BITRATE,    VIDEO_BITRATE);
        if (SUCCEEDED(hr)) hr = pOut->SetUINT32 (MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        if (SUCCEEDED(hr)) hr = MFSetAttributeSize (pOut, MF_MT_FRAME_SIZE,         m_width, m_height);
        if (SUCCEEDED(hr)) hr = MFSetAttributeRatio(pOut, MF_MT_FRAME_RATE,         fpsVal, 1);
        if (SUCCEEDED(hr)) hr = MFSetAttributeRatio(pOut, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
        if (SUCCEEDED(hr)) hr = m_pSinkWriter->AddStream(pOut, &m_videoStreamIndex);
        if (pOut) pOut->Release();
        if (FAILED(hr))
        {
        debug.logDiagLevelMessage(LogLevel::LOG_ERROR, L"ScreenRecorder: Failed to add H.264 stream (" + HRStr(hr) + L")");
            m_pSinkWriter->Release(); m_pSinkWriter = nullptr; return false;
        }
    }

    // 3. ARGB32 input
    {
        IMFMediaType* pIn = nullptr;
        hr = MFCreateMediaType(&pIn);
        if (SUCCEEDED(hr)) hr = pIn->SetGUID   (MF_MT_MAJOR_TYPE,     MFMediaType_Video);
        if (SUCCEEDED(hr)) hr = pIn->SetGUID   (MF_MT_SUBTYPE,        MFVideoFormat_ARGB32);
        if (SUCCEEDED(hr)) hr = pIn->SetUINT32 (MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        if (SUCCEEDED(hr)) hr = MFSetAttributeSize (pIn, MF_MT_FRAME_SIZE,         m_width, m_height);
        if (SUCCEEDED(hr)) hr = MFSetAttributeRatio(pIn, MF_MT_FRAME_RATE,         fpsVal, 1);
        if (SUCCEEDED(hr)) hr = MFSetAttributeRatio(pIn, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
        if (SUCCEEDED(hr)) hr = m_pSinkWriter->SetInputMediaType(m_videoStreamIndex, pIn, nullptr);
        if (pIn) pIn->Release();
        if (FAILED(hr))
        {
        debug.logDiagLevelMessage(LogLevel::LOG_ERROR, L"ScreenRecorder: Failed to set ARGB32 input (" + HRStr(hr) + L")");
            m_pSinkWriter->Release(); m_pSinkWriter = nullptr; return false;
        }
    }

    // 4. Game audio
    bool hasAudio = captureAudio && InitAudioCapture();
    if (captureAudio && !hasAudio)
    {
        debug.logDiagLevelMessage(LogLevel::LOG_WARNING, L"ScreenRecorder: Game audio unavailable");
    }

    // 5. Mic
    bool hasMic = false;
    if (captureAudio && micMode != MicMode::Off)
    {
        hasMic = InitMicCapture();
        if (!hasMic)
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"Microphone Input is disabled during Screen Recordings");
    }

    // 6. Mic monitoring - render mic audio to speakers in real-time
    if (hasMic && !InitMonitor())
    {
        debug.logDiagLevelMessage(LogLevel::LOG_WARNING, L"ScreenRecorder: Mic monitoring unavailable");
    }

    // 7. Begin writing
    hr = m_pSinkWriter->BeginWriting();
    if (FAILED(hr))
    {
        debug.logDiagLevelMessage(LogLevel::LOG_ERROR, L"ScreenRecorder: BeginWriting failed (" + HRStr(hr) + L")");
        CleanupAudio(); CleanupMic();
        m_pSinkWriter->Release(); m_pSinkWriter = nullptr; return false;
    }

    QueryPerformanceFrequency(&m_qpcFreq);
    QueryPerformanceCounter(&m_qpcRecordingStart);
    m_lastVideoQpc = m_qpcRecordingStart;
    m_isRecording.store(true);

    if (hasAudio)
    {
        m_pAudioClient->Start();
        // Mixed mode: mic is polled inline inside AudioCaptureThread - start mic client here
        if (hasMic && micMode == MicMode::Mixed)
            m_pMicAudioClient->Start();

        m_audioThread = std::thread(&ScreenRecorder::AudioCaptureThread, this);
    }

    // Separate mode gets its own thread
    if (hasMic && micMode == MicMode::Separate)
    {
        m_pMicAudioClient->Start();
        m_micThread = std::thread(&ScreenRecorder::MicCaptureThread, this);
    }

    #if defined(_DEBUG_SCREEN_RECORDER_)
        std::wstring micDesc = !hasMic ? L"" :
            (micMode == MicMode::Mixed ? L" + mic(mixed)" : L" + mic(separate track)");

        debug.logLevelMessage(LogLevel::LOG_INFO,
            L"ScreenRecorder: Started " + std::to_wstring(fpsVal) + L"fps [" +
            std::wstring(hasAudio ? L"game audio" : L"no audio") + micDesc + L"] -> " + outputPath);
    #endif

    return true;
}

// ---------------------------------------------------------------------------
// StopRecording
// ---------------------------------------------------------------------------
void ScreenRecorder::StopRecording()
{
    if (!m_isRecording.exchange(false)) return;

    if (m_audioThread.joinable()) m_audioThread.join();
    if (m_micThread.joinable())   m_micThread.join();

    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_pSinkWriter)
    {
        m_pSinkWriter->Finalize();
        m_pSinkWriter->Release();
        m_pSinkWriter = nullptr;
    }

    CleanupAudio();
    CleanupMic();
    CleanupMonitor();
    m_micMode = MicMode::Off;
    m_micAccum.clear();
    m_micAccumRead = 0;
    #if defined(__USE_DIRECTX_12__)
        CleanupDX12Capture();
    #elif defined(__USE_DIRECTX_11__)
        m_stagingTexture.Reset();
    #elif defined(__USE_VULKAN__) && defined(PLATFORM_WINDOWS)
        CleanupVulkanCapture();
    #endif

    m_width = m_height = 0;
    m_videoFrameIndex.store(-1);
    m_qpcRecordingStart.QuadPart = 0;

    #if defined(_DEBUG_SCREEN_RECORDER_)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"ScreenRecorder: Stopped");
    #endif
}

// ---------------------------------------------------------------------------
// CaptureFrame - DirectX 12
// ---------------------------------------------------------------------------
#if defined(__USE_DIRECTX_12__)
void ScreenRecorder::CleanupDX12Capture()
{
    if (m_dx12ReadbackBuffer) {
        m_dx12ReadbackBuffer->Unmap(0, nullptr);
        m_dx12ReadbackBuffer.Reset();
    }
    m_dx12ReadbackRowPitch = 0;
    m_dx12ReadbackWidth    = 0;
    m_dx12ReadbackHeight   = 0;
}

void ScreenRecorder::CaptureFrame(ID3D12Device*       device,
                                   ID3D12CommandQueue* queue,
                                   IDXGISwapChain4*    swapChain,
                                   UINT                frameIndex,
                                   std::mutex*         queueMutex)
{
    if (!m_captureEnabled.load() || !m_isRecording.load() || !device || !queue || !swapChain) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_captureEnabled.load() || !m_isRecording.load() || !m_pSinkWriter) return;

    // FPS throttle
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    {
        const LONGLONG elapsed100ns =
            (now.QuadPart - m_lastVideoQpc.QuadPart) * 10000000LL / m_qpcFreq.QuadPart;
        if (elapsed100ns < m_framePeriod) return;
        m_lastVideoQpc.QuadPart += m_qpcFreq.QuadPart * m_framePeriod / 10000000LL;
    }

    const LONGLONG ts = (now.QuadPart - m_qpcRecordingStart.QuadPart) * 10'000'000LL / m_qpcFreq.QuadPart;

    // Get the back-buffer for the current frame
    ComPtr<ID3D12Resource> backBuffer;
    if (FAILED(swapChain->GetBuffer(frameIndex, IID_PPV_ARGS(&backBuffer)))) return;

    D3D12_RESOURCE_DESC bbDesc = backBuffer->GetDesc();
    if (bbDesc.Width < m_width || bbDesc.Height < m_height) return;

    // Calculate 256-byte-aligned row pitch required by DX12 readback buffers
    const UINT bytesPerPixel = 4;   // BGRA
    const UINT64 rowPitch256 = (static_cast<UINT64>(m_width) * bytesPerPixel + 255) & ~255ULL;
    const UINT64 bufferSize  = rowPitch256 * m_height;

    // Create (or re-create) the readback buffer if dimensions changed
    if (!m_dx12ReadbackBuffer || m_dx12ReadbackWidth != m_width || m_dx12ReadbackHeight != m_height)
    {
        if (m_dx12ReadbackBuffer) {
            m_dx12ReadbackBuffer->Unmap(0, nullptr);
            m_dx12ReadbackBuffer.Reset();
        }
        CD3DX12_HEAP_PROPERTIES readbackHeap(D3D12_HEAP_TYPE_READBACK);
        CD3DX12_RESOURCE_DESC   bufDesc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize);
        HRESULT hr = device->CreateCommittedResource(&readbackHeap, D3D12_HEAP_FLAG_NONE,
            &bufDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
            IID_PPV_ARGS(&m_dx12ReadbackBuffer));
        if (FAILED(hr)) {
            debug.logDiagLevelMessage(LogLevel::LOG_ERROR, L"ScreenRecorder: Failed to create DX12 readback buffer.");
            return;
        }
        m_dx12ReadbackRowPitch = rowPitch256;
        m_dx12ReadbackWidth    = m_width;
        m_dx12ReadbackHeight   = m_height;
    }

    // One-shot command allocator + list for the copy
    ComPtr<ID3D12CommandAllocator>    copyAllocator;
    ComPtr<ID3D12GraphicsCommandList> copyList;
    if (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&copyAllocator)))) return;
    if (FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, copyAllocator.Get(), nullptr, IID_PPV_ARGS(&copyList)))) return;

    // Transition back-buffer: PRESENT → COPY_SOURCE
    D3D12_RESOURCE_BARRIER toSrc = CD3DX12_RESOURCE_BARRIER::Transition(
        backBuffer.Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_SOURCE);
    copyList->ResourceBarrier(1, &toSrc);

    // Copy the m_width × m_height region to the readback buffer
    D3D12_TEXTURE_COPY_LOCATION src = {};
    src.pResource        = backBuffer.Get();
    src.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.SubresourceIndex = 0;

    D3D12_TEXTURE_COPY_LOCATION dst = {};
    dst.pResource                          = m_dx12ReadbackBuffer.Get();
    dst.Type                               = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint.Offset             = 0;
    dst.PlacedFootprint.Footprint.Format   = bbDesc.Format;   // must match source texture format exactly
    dst.PlacedFootprint.Footprint.Width    = m_width;
    dst.PlacedFootprint.Footprint.Height   = m_height;
    dst.PlacedFootprint.Footprint.Depth    = 1;
    dst.PlacedFootprint.Footprint.RowPitch = static_cast<UINT>(rowPitch256);

    D3D12_BOX srcBox = { 0, 0, 0, m_width, m_height, 1 };
    copyList->CopyTextureRegion(&dst, 0, 0, 0, &src, &srcBox);

    // Transition back-buffer: COPY_SOURCE → PRESENT
    D3D12_RESOURCE_BARRIER toPresent = CD3DX12_RESOURCE_BARRIER::Transition(
        backBuffer.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_PRESENT);
    copyList->ResourceBarrier(1, &toPresent);

    if (FAILED(copyList->Close())) return;

    // Submit copy
    ID3D12CommandList* lists[] = { copyList.Get() };
    if (queueMutex) {
        std::lock_guard<std::mutex> qLock(*queueMutex);
        queue->ExecuteCommandLists(1, lists);
    } else {
        queue->ExecuteCommandLists(1, lists);
    }

    // Fence-wait for the copy to finish (uses a temporary inline fence)
    ComPtr<ID3D12Fence> fence;
    if (FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)))) return;
    HANDLE fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!fenceEvent) return;
    queue->Signal(fence.Get(), 1);
    fence->SetEventOnCompletion(1, fenceEvent);
    WaitForSingleObject(fenceEvent, 5000);
    CloseHandle(fenceEvent);

    // Map and hand to MF encoder
    void* mappedData = nullptr;
    D3D12_RANGE readRange = { 0, bufferSize };
    if (FAILED(m_dx12ReadbackBuffer->Map(0, &readRange, &mappedData))) return;
    if (mappedData)
    {
        // DX12 swap chain is R8G8B8A8_UNORM; MF ARGB32 input expects BGRA byte order.
        // Swap R (byte 0) and B (byte 2) in every pixel in-place before encoding.
        if (bbDesc.Format == DXGI_FORMAT_R8G8B8A8_UNORM ||
            bbDesc.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)
        {
            BYTE* base = static_cast<BYTE*>(mappedData);
            for (UINT row = 0; row < m_height; ++row)
            {
                UINT32* px = reinterpret_cast<UINT32*>(base + row * static_cast<size_t>(rowPitch256));
                for (UINT col = 0; col < m_width; ++col)
                {
                    const UINT32 v = px[col];
                    px[col] = (v & 0xFF00FF00u)
                             | ((v & 0x000000FFu) << 16)   // R → B position
                             | ((v & 0x00FF0000u) >> 16);  // B → R position
                }
            }
        }
        WriteVideoFrame(static_cast<const BYTE*>(mappedData),
                        static_cast<UINT>(rowPitch256), ts);
    }
    D3D12_RANGE writeRange = { 0, 0 };   // no CPU writes
    m_dx12ReadbackBuffer->Unmap(0, &writeRange);
}
#endif // __USE_DIRECTX_12__

// ---------------------------------------------------------------------------
// CaptureFrame - DirectX 11
// ---------------------------------------------------------------------------
#if defined(__USE_DIRECTX_11__) && !defined(__USE_DIRECTX_12__)
void ScreenRecorder::CaptureFrame(ID3D11Device*        device,
                                   ID3D11DeviceContext* context,
                                   IDXGISwapChain1*     swapChain)
{
    if (!m_captureEnabled.load() || !m_isRecording.load() || !device || !context || !swapChain) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_isRecording.load()) return;

    // Throttle to target FPS; use the same QPC value for the video timestamp so
    // video and audio share one clock.
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    {
        const LONGLONG elapsed100ns =
            (now.QuadPart - m_lastVideoQpc.QuadPart) * 10000000LL / m_qpcFreq.QuadPart;
        if (elapsed100ns < m_framePeriod) return;
        m_lastVideoQpc.QuadPart += m_qpcFreq.QuadPart * m_framePeriod / 10000000LL;
    }

    const LONGLONG ts = (now.QuadPart - m_qpcRecordingStart.QuadPart) * 10'000'000LL / m_qpcFreq.QuadPart;

    ComPtr<ID3D11Texture2D> backBuffer;
    if (FAILED(swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer)))) return;

    D3D11_TEXTURE2D_DESC bbDesc = {};
    backBuffer->GetDesc(&bbDesc);

    // Skip if the back buffer is smaller than the encoder's frame - can't crop upward.
    if (bbDesc.Width < m_width || bbDesc.Height < m_height)
    {
        debug.logDiagLevelMessage(LogLevel::LOG_WARNING,
            L"ScreenRecorder: back buffer smaller than recording dimensions - frame skipped.");
        return;
    }

    // Staging texture created once at m_width × m_height (encoder dims, even-rounded).
    // Using the back-buffer's actual format avoids CopySubresourceRegion format mismatches.
    if (!m_stagingTexture)
    {
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width            = m_width;
        desc.Height           = m_height;
        desc.MipLevels        = 1;
        desc.ArraySize        = 1;
        desc.Format           = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage            = D3D11_USAGE_STAGING;
        desc.CPUAccessFlags   = D3D11_CPU_ACCESS_READ;

        if (FAILED(device->CreateTexture2D(&desc, nullptr, m_stagingTexture.GetAddressOf())))
        {
            debug.logDiagLevelMessage(LogLevel::LOG_ERROR,
                L"ScreenRecorder: Failed to create staging texture (bbDesc.Format=" +
                std::to_wstring(bbDesc.Format) + L")");
            return;
        }
    }

    // Copy only the m_width × m_height region so staging and encoder dims are always identical.
    D3D11_BOX srcBox = { 0, 0, 0, m_width, m_height, 1 };
    context->CopySubresourceRegion(m_stagingTexture.Get(), 0, 0, 0, 0,
                                    backBuffer.Get(), 0, &srcBox);

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (FAILED(context->Map(m_stagingTexture.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return;
    if (!mapped.pData) { context->Unmap(m_stagingTexture.Get(), 0); return; }
    WriteVideoFrame(static_cast<const BYTE*>(mapped.pData), static_cast<UINT>(mapped.RowPitch), ts);
    context->Unmap(m_stagingTexture.Get(), 0);
}
#endif // __USE_DIRECTX_11__

// ---------------------------------------------------------------------------
// CaptureFrame (OpenGL / Windows)
//
// Reads the fully-composed OpenGL back-buffer via glReadPixels (BGRA, top-down)
// and feeds it to the MF sink writer.  Call BEFORE SwapBuffers so the GPU has
// already finished compositing the frame.
// ---------------------------------------------------------------------------
#if defined(__USE_OPENGL__) && defined(PLATFORM_WINDOWS)
void ScreenRecorder::CaptureFrame(UINT width, UINT height)
{
    if (!m_captureEnabled.load() || !m_isRecording.load() || !m_pSinkWriter) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_isRecording.load()) return;

    // Throttle to target FPS using QPC timestamps (same logic as DX11 path)
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    {
        const LONGLONG elapsed100ns =
            (now.QuadPart - m_lastVideoQpc.QuadPart) * 10'000'000LL / m_qpcFreq.QuadPart;
        if (elapsed100ns < m_framePeriod) return;
        m_lastVideoQpc.QuadPart += m_qpcFreq.QuadPart * m_framePeriod / 10'000'000LL;
    }

    const LONGLONG ts =
        (now.QuadPart - m_qpcRecordingStart.QuadPart) * 10'000'000LL / m_qpcFreq.QuadPart;

    // Clamp to even dimensions (required by MF H.264 encoder)
    UINT w = (width  > 0 ? width  : m_width)  & ~1u;
    UINT h = (height > 0 ? height : m_height) & ~1u;
    if (w == 0 || h == 0) return;

    // Resize pixel buffer lazily
    UINT rowBytes = w * 4;
    if (w != m_glCaptureWidth || h != m_glCaptureHeight) {
        m_glPixelBuffer.resize(static_cast<size_t>(rowBytes) * h);
        m_glCaptureWidth  = w;
        m_glCaptureHeight = h;
    }

    // Read back from the default framebuffer (origin = bottom-left in OpenGL)
    // GL_BGRA_EXT matches the BGRA layout expected by the MF sink writer (DXGI_FORMAT_B8G8R8A8_UNORM)
    glReadPixels(0, 0, (GLsizei)w, (GLsizei)h, GL_BGRA_EXT, GL_UNSIGNED_BYTE, m_glPixelBuffer.data());

    // Flip rows: OpenGL origin is bottom-left; MF expects top-left
    std::vector<BYTE> flipped(m_glPixelBuffer.size());
    for (UINT y = 0; y < h; ++y)
        memcpy(flipped.data() + y * rowBytes,
               m_glPixelBuffer.data() + (h - 1u - y) * rowBytes,
               rowBytes);

    WriteVideoFrame(flipped.data(), rowBytes, ts);
}
#endif // __USE_OPENGL__ && PLATFORM_WINDOWS

// ---------------------------------------------------------------------------
// CaptureFrame (Vulkan / Windows)
//
// Submits a one-shot command buffer that:
//   1. Transitions the swap chain image  srcLayout → TRANSFER_SRC_OPTIMAL
//   2. Copies the image to a host-visible staging buffer
//   3. Transitions the image back to srcLayout
// Then waits for completion and writes the pixel data to the MF video stream.
//
// The caller must have waited for the render fence before calling this so the
// swap chain image is not still being written by the GPU.  The function holds
// queueMutex only for the VkQueueSubmit call so the render thread can proceed.
// ---------------------------------------------------------------------------
#if defined(__USE_VULKAN__) && defined(PLATFORM_WINDOWS)
void ScreenRecorder::CaptureFrame(VkDevice         device,
                                   VkPhysicalDevice physDevice,
                                   VkCommandPool    cmdPool,
                                   VkQueue          queue,
                                   VkImage          swapchainImage,
                                   VkImageLayout    srcLayout,
                                   VkFormat         format,
                                   uint32_t         width,
                                   uint32_t         height,
                                   std::mutex*      queueMutex)
{
    if (!m_captureEnabled.load() || !m_isRecording.load() || !device || !swapchainImage) return;

    // FPS throttle - same QPC clock as the DX11 path
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    {
        const LONGLONG elapsed100ns =
            (now.QuadPart - m_lastVideoQpc.QuadPart) * 10000000LL / m_qpcFreq.QuadPart;
        if (elapsed100ns < m_framePeriod) return;
        m_lastVideoQpc.QuadPart += m_qpcFreq.QuadPart * m_framePeriod / 10000000LL;
    }
    const LONGLONG ts =
        (now.QuadPart - m_qpcRecordingStart.QuadPart) * 10'000'000LL / m_qpcFreq.QuadPart;

    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_isRecording.load()) return;

    // Create / resize the staging buffer when dimensions change
    const VkDeviceSize bufSize = static_cast<VkDeviceSize>(width) * height * 4;
    if (m_vkStagingBuffer == VK_NULL_HANDLE ||
        m_vkStagingWidth  != width          ||
        m_vkStagingHeight != height)
    {
        CleanupVulkanCapture();

        VkBufferCreateInfo bi{};
        bi.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size        = bufSize;
        bi.usage       = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(device, &bi, nullptr, &m_vkStagingBuffer) != VK_SUCCESS) return;

        VkMemoryRequirements memReq{};
        vkGetBufferMemoryRequirements(device, m_vkStagingBuffer, &memReq);

        VkPhysicalDeviceMemoryProperties memProps{};
        vkGetPhysicalDeviceMemoryProperties(physDevice, &memProps);

        uint32_t memIdx = UINT32_MAX;
        const VkMemoryPropertyFlags needed =
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
            if ((memReq.memoryTypeBits & (1u << i)) &&
                (memProps.memoryTypes[i].propertyFlags & needed) == needed) {
                memIdx = i; break;
            }
        }
        if (memIdx == UINT32_MAX) { CleanupVulkanCapture(); return; }

        VkMemoryAllocateInfo ai{};
        ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize  = memReq.size;
        ai.memoryTypeIndex = memIdx;
        if (vkAllocateMemory(device, &ai, nullptr, &m_vkStagingMemory) != VK_SUCCESS) {
            CleanupVulkanCapture(); return;
        }
        vkBindBufferMemory(device, m_vkStagingBuffer, m_vkStagingMemory, 0);
        vkMapMemory(device, m_vkStagingMemory, 0, bufSize, 0, &m_vkStagingMapped);
        m_vkStagingWidth  = width;
        m_vkStagingHeight = height;
        m_vkDevice        = device;
    }

    // Allocate a one-shot command buffer
    VkCommandBufferAllocateInfo cbai{};
    cbai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbai.commandPool        = cmdPool;
    cbai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(device, &cbai, &cmd) != VK_SUCCESS) return;

    VkCommandBufferBeginInfo cbbi{};
    cbbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    cbbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &cbbi);

    // 1. Transition: srcLayout → TRANSFER_SRC_OPTIMAL
    VkImageMemoryBarrier toSrc{};
    toSrc.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toSrc.oldLayout           = srcLayout;
    toSrc.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    toSrc.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toSrc.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toSrc.image               = swapchainImage;
    toSrc.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    toSrc.srcAccessMask       = VK_ACCESS_MEMORY_READ_BIT;
    toSrc.dstAccessMask       = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &toSrc);

    // 2. Copy image → staging buffer
    VkBufferImageCopy region{};
    region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.imageExtent      = { width, height, 1 };
    vkCmdCopyImageToBuffer(cmd, swapchainImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           m_vkStagingBuffer, 1, &region);

    // 3. Transition back: TRANSFER_SRC_OPTIMAL → srcLayout
    VkImageMemoryBarrier toPresent{};
    toPresent.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toPresent.oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    toPresent.newLayout           = srcLayout;
    toPresent.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toPresent.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toPresent.image               = swapchainImage;
    toPresent.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    toPresent.srcAccessMask       = VK_ACCESS_TRANSFER_READ_BIT;
    toPresent.dstAccessMask       = VK_ACCESS_MEMORY_READ_BIT;
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        0, 0, nullptr, 0, nullptr, 1, &toPresent);

    vkEndCommandBuffer(cmd);

    // Create a local fence so we can wait for capture completion independently
    VkFenceCreateInfo fci{};
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VkFence captureFence = VK_NULL_HANDLE;
    if (vkCreateFence(device, &fci, nullptr, &captureFence) != VK_SUCCESS) {
        vkFreeCommandBuffers(device, cmdPool, 1, &cmd);
        return;
    }

    VkSubmitInfo si{};
    si.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers    = &cmd;
    // Serialise against the render thread's vkQueueSubmit via the shared queue mutex
    if (queueMutex) {
        std::lock_guard<std::mutex> qlock(*queueMutex);
        vkQueueSubmit(queue, 1, &si, captureFence);
    } else {
        vkQueueSubmit(queue, 1, &si, captureFence);
    }
    // Wait for the copy to complete (outside the queue lock so the render thread can proceed)
    vkWaitForFences(device, 1, &captureFence, VK_TRUE, UINT64_MAX);
    vkDestroyFence(device, captureFence, nullptr);
    vkFreeCommandBuffers(device, cmdPool, 1, &cmd);

    // The staging buffer is HOST_COHERENT so no explicit invalidation is needed.
    // Handle pixel format: MF expects ARGB32 (BGRA byte order in memory).
    // VK_FORMAT_B8G8R8A8_* → bytes are [B,G,R,A] → matches ARGB32 directly.
    // VK_FORMAT_R8G8B8A8_* → bytes are [R,G,B,A] → swap R and B.
    const bool needByteSwap = (format == VK_FORMAT_R8G8B8A8_UNORM  ||
                                format == VK_FORMAT_R8G8B8A8_SRGB   ||
                                format == VK_FORMAT_R8G8B8A8_SNORM  ||
                                format == VK_FORMAT_A8B8G8R8_UNORM_PACK32);
    if (needByteSwap) {
        uint8_t* px = static_cast<uint8_t*>(m_vkStagingMapped);
        const size_t pixCount = static_cast<size_t>(width) * height;
        for (size_t i = 0; i < pixCount; ++i)
            std::swap(px[i * 4 + 0], px[i * 4 + 2]);
    }

    WriteVideoFrame(static_cast<const BYTE*>(m_vkStagingMapped), width * 4, ts);
}

void ScreenRecorder::CleanupVulkanCapture()
{
    VkDevice dev = m_vkDevice;
    if (!dev) return;
    if (m_vkStagingMapped) {
        vkUnmapMemory(dev, m_vkStagingMemory);
        m_vkStagingMapped = nullptr;
    }
    if (m_vkStagingBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(dev, m_vkStagingBuffer, nullptr);
        m_vkStagingBuffer = VK_NULL_HANDLE;
    }
    if (m_vkStagingMemory != VK_NULL_HANDLE) {
        vkFreeMemory(dev, m_vkStagingMemory, nullptr);
        m_vkStagingMemory = VK_NULL_HANDLE;
    }
    m_vkStagingWidth  = 0;
    m_vkStagingHeight = 0;
    m_vkDevice        = VK_NULL_HANDLE;
}
#endif // __USE_VULKAN__ && PLATFORM_WINDOWS

// ---------------------------------------------------------------------------
// WriteVideoFrame
// ---------------------------------------------------------------------------
void ScreenRecorder::WriteVideoFrame(const BYTE* pData, UINT rowPitch, LONGLONG ts)
{
    ts -= m_pausedTicks * 10'000'000LL / m_qpcFreq.QuadPart;
    const UINT packed    = m_width * 4;
    const UINT frameBytes = packed * m_height;

    IMFMediaBuffer* pBuf = nullptr;
    if (FAILED(MFCreateMemoryBuffer(frameBytes, &pBuf))) return;
    BYTE* pDst = nullptr;
    if (FAILED(pBuf->Lock(&pDst, nullptr, nullptr))) { pBuf->Release(); return; }

    for (UINT row = 0; row < m_height; ++row)
        memcpy(pDst + row * packed, pData + row * rowPitch, packed);

    pBuf->Unlock();
    pBuf->SetCurrentLength(frameBytes);

    IMFSample* pS = nullptr;
    if (FAILED(MFCreateSample(&pS))) { pBuf->Release(); return; }
    pS->AddBuffer(pBuf); pBuf->Release();
    pS->SetSampleTime(ts);
    pS->SetSampleDuration(m_framePeriod);
    m_videoFrameIndex.fetch_add(1);

    if (FAILED(m_pSinkWriter->WriteSample(m_videoStreamIndex, pS)))
    {
        debug.logDiagLevelMessage(LogLevel::LOG_WARNING, L"ScreenRecorder: Video frame dropped");
    }
    pS->Release();
}
    
// ---------------------------------------------------------------------------
// Silence helper
// ---------------------------------------------------------------------------
static void WriteAudioSilence(IMFSinkWriter* pSW, DWORD idx,
                               const WAVEFORMATEX* pWF, UINT32 frames, LONGLONG ts)
{
    const LONGLONG dur   = static_cast<LONGLONG>(frames) * 10'000'000LL / pWF->nSamplesPerSec;
    const DWORD    bytes = frames * pWF->nBlockAlign;
    IMFMediaBuffer* pBuf = nullptr;
    if (FAILED(MFCreateMemoryBuffer(bytes, &pBuf))) return;
    BYTE* p = nullptr;
    if (SUCCEEDED(pBuf->Lock(&p, nullptr, nullptr)))
    {
        memset(p, 0, bytes); pBuf->Unlock(); pBuf->SetCurrentLength(bytes);
    }
    IMFSample* pS = nullptr;
    if (SUCCEEDED(MFCreateSample(&pS)))
    {
        pS->AddBuffer(pBuf); pS->SetSampleTime(ts); pS->SetSampleDuration(dur);
        pSW->WriteSample(idx, pS); pS->Release();
    }
    pBuf->Release();
}

// ---------------------------------------------------------------------------
// MixMicInline
// Called from AudioCaptureThread only - no mutex needed.
//
// Monitor path: mic is always pushed to the render client so the user hears
// themselves in real-time regardless of which mode is active.
//
// Recording blend: mic is ALWAYS blended directly into the game audio buffer.
// The loopback "picking up" monitor audio is timing-dependent and unreliable -
// by the time WriteMonitorSamples pushes mic audio to the render client, the
// WASAPI capture buffer for this poll cycle is already closed. Direct blend is
// the only guaranteed path. If m_monitorOnLoopbackDevice is true, the mic may
// also appear faintly via the loopback; m_micRecordGain can compensate.
// ---------------------------------------------------------------------------
void ScreenRecorder::MixMicInline(BYTE* pDst, UINT32 gameFrames)
{
    if (!m_pMicCaptureClient) return;

    const bool   gameIsFloat  = IsIEEEFloat(m_pWaveFormat);
    const bool   micIsFloat   = IsIEEEFloat(m_pMicWaveFormat);
    const UINT32 gameChannels = m_pWaveFormat->nChannels;
    const UINT32 micChannels  = m_pMicWaveFormat->nChannels;

    // Drain all available mic packets into m_micAccum
    UINT32 micPacket = 0;
    if (FAILED(m_pMicCaptureClient->GetNextPacketSize(&micPacket))) return;

    while (micPacket > 0)
    {
        BYTE* pMicData = nullptr; UINT32 micFrames = 0; DWORD micFlags = 0;
        if (FAILED(m_pMicCaptureClient->GetBuffer(&pMicData, &micFrames, &micFlags, nullptr, nullptr))) break;

        const DWORD  bytes = micFrames * m_pMicWaveFormat->nBlockAlign;
        const size_t old   = m_micAccum.size();
        m_micAccum.resize(old + bytes);

        if (micFlags & AUDCLNT_BUFFERFLAGS_SILENT)
            memset(m_micAccum.data() + old, 0, bytes);
        else
            memcpy(m_micAccum.data() + old, pMicData, bytes);

        m_pMicCaptureClient->ReleaseBuffer(micFrames);
        if (FAILED(m_pMicCaptureClient->GetNextPacketSize(&micPacket))) break;
    }

    const size_t  available   = m_micAccum.size() - m_micAccumRead;
    const UINT32  availFrames = static_cast<UINT32>(available / m_pMicWaveFormat->nBlockAlign);
    if (availFrames == 0) return;

    const UINT32 mixFrames = gameFrames < availFrames ? gameFrames : availFrames;

    if (!m_micDataLogged)
    {
        m_micDataLogged = true;
        #if defined(_DEBUG_SCREEN_RECORDER_)
            debug.logLevelMessage(LogLevel::LOG_INFO,
                L"ScreenRecorder Mic: First audio data received - " +
                std::to_wstring(mixFrames) + L" frames");
        #endif
    }

    const BYTE* pMicBase = m_micAccum.data() + m_micAccumRead;

    // Always push to monitor so the user hears themselves
    WriteMonitorSamples(pMicBase, mixFrames);

    // Direct blend - always applied; loopback timing makes it unreliable otherwise.
    {
        const float recGain = m_micRecordGain.load();

        for (UINT32 f = 0; f < mixFrames; ++f)
        {
            float micRaw;
            if (micIsFloat)
                micRaw = reinterpret_cast<const float*>(pMicBase)[f * micChannels];
            else
                micRaw = reinterpret_cast<const int16_t*>(pMicBase)[f * micChannels] / 32768.0f;

            // Diagnostic: track pre-gain peak so the periodic level log below can
            // prove whether the endpoint is delivering voice or just noise floor
            const float micAbs = micRaw < 0.0f ? -micRaw : micRaw;
            if (micAbs > m_micDiagPeak) m_micDiagPeak = micAbs;

            const float micSample = micRaw * recGain;

            if (gameIsFloat)
            {
                float* pGame = reinterpret_cast<float*>(pDst);
                for (UINT32 ch = 0; ch < gameChannels; ++ch)
                {
                    float& s = pGame[f * gameChannels + ch];
                    s += micSample;
                    s = s < -1.0f ? -1.0f : (s > 1.0f ? 1.0f : s);
                }
            }
            else
            {
                int16_t* pGame  = reinterpret_cast<int16_t*>(pDst);
                int32_t  micS16 = static_cast<int32_t>(micSample * 32767.0f);
                for (UINT32 ch = 0; ch < gameChannels; ++ch)
                {
                    int32_t v = static_cast<int32_t>(pGame[f * gameChannels + ch]) + micS16;
                    pGame[f * gameChannels + ch] = v < -32768 ? -32768 : (v > 32767 ? 32767 : static_cast<int16_t>(v));
                }
            }
        }
    }

    m_micAccumRead += mixFrames * m_pMicWaveFormat->nBlockAlign;

    if (m_micAccumRead > 96000)
    {
        m_micAccum.erase(m_micAccum.begin(), m_micAccum.begin() + m_micAccumRead);
        m_micAccumRead = 0;
    }

    // Periodic level log (~every 5 seconds of mic audio). Peak is pre-gain:
    //   > 0.05  - voice is reaching the endpoint
    //   ~ 0.001–0.01 steady - noise floor only (nothing spoken into this device,
    //                         or Windows input level near zero)
    //   0.0000  - digital silence (endpoint muted or privacy-blocked)
    m_micDiagFrames += mixFrames;
    if (m_micDiagFrames >= static_cast<UINT64>(m_pMicWaveFormat->nSamplesPerSec) * 5)
    {
        const float dB = m_micDiagPeak > 0.00001f
                       ? 20.0f * log10f(m_micDiagPeak) : -100.0f;
        #if defined(_DEBUG_SCREEN_RECORDER_)
            wchar_t lvl[128];
            swprintf_s(lvl, L"ScreenRecorder Mic: Level check - peak %.4f (%.1f dB)",
                       m_micDiagPeak, dB);
            debug.logLevelMessage(LogLevel::LOG_INFO, lvl);
        #endif
        m_micDiagPeak   = 0.0f;
        m_micDiagFrames = 0;
    }
}

// ---------------------------------------------------------------------------
// InitAudioCapture
// ---------------------------------------------------------------------------
bool ScreenRecorder::InitAudioCapture()
{
    HRESULT hr;
    IMMDeviceEnumerator* pEnum = nullptr;
    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&pEnum));
    if (FAILED(hr)) return false;

    IMMDevice* pDevice = nullptr;
    hr = pEnum->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice);
    pEnum->Release();
    if (FAILED(hr)) return false;

    hr = pDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                           reinterpret_cast<void**>(&m_pAudioClient));
    pDevice->Release();
    if (FAILED(hr)) return false;

    if (FAILED(m_pAudioClient->GetMixFormat(&m_pWaveFormat))) return false;

    if (FAILED(m_pAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_LOOPBACK, 0, 0, m_pWaveFormat, nullptr))) return false;

    if (FAILED(m_pAudioClient->GetService(IID_PPV_ARGS(&m_pCaptureClient)))) return false;

    {
        IMFMediaType* pOut = nullptr;
        hr = MFCreateMediaType(&pOut);
        if (SUCCEEDED(hr)) hr = pOut->SetGUID  (MF_MT_MAJOR_TYPE,                 MFMediaType_Audio);
        if (SUCCEEDED(hr)) hr = pOut->SetGUID  (MF_MT_SUBTYPE,                    MFAudioFormat_AAC);
        if (SUCCEEDED(hr)) hr = pOut->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE,      16);
        if (SUCCEEDED(hr)) hr = pOut->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND,   m_pWaveFormat->nSamplesPerSec);
        if (SUCCEEDED(hr)) hr = pOut->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS,         m_pWaveFormat->nChannels);
        if (SUCCEEDED(hr)) hr = pOut->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, static_cast<UINT32>(m_audioBitrate) / 8);
        if (SUCCEEDED(hr)) hr = m_pSinkWriter->AddStream(pOut, &m_audioStreamIndex);
        if (pOut) pOut->Release();
        if (FAILED(hr)) return false;
    }

    {
        IMFMediaType* pIn = nullptr;
        hr = MFCreateMediaType(&pIn);
        if (SUCCEEDED(hr))
        {
            UINT32 sz = sizeof(WAVEFORMATEX) + m_pWaveFormat->cbSize;
            hr = MFInitMediaTypeFromWaveFormatEx(pIn, m_pWaveFormat, sz);
        }
        if (SUCCEEDED(hr)) hr = m_pSinkWriter->SetInputMediaType(m_audioStreamIndex, pIn, nullptr);
        if (pIn) pIn->Release();
        if (FAILED(hr)) return false;
    }

    return true;
}

// ---------------------------------------------------------------------------
// AudioCaptureThread  - loopback + inline mic poll (Mixed mode)
// ---------------------------------------------------------------------------
void ScreenRecorder::AudioCaptureThread()
{
    while (m_isRecording.load())
    {
        Sleep(10);

        UINT32 packetFrames = 0;
        if (FAILED(m_pCaptureClient->GetNextPacketSize(&packetFrames))) break;

        bool hadLoopbackData = false;

        while (packetFrames > 0)
        {
            hadLoopbackData = true;

            BYTE* pData = nullptr; UINT32 numFrames = 0; DWORD flags = 0;
            if (FAILED(m_pCaptureClient->GetBuffer(&pData, &numFrames, &flags, nullptr, nullptr))) break;

            // If WASAPI signals a gap, resync audio clock to wall clock so it
            // stays locked to the QPC-based video timestamps after a glitch.
            if (flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY)
            {
                LARGE_INTEGER now;
                QueryPerformanceCounter(&now);
                const LONGLONG wallTS = (now.QuadPart - m_qpcRecordingStart.QuadPart) * 10'000'000LL / m_qpcFreq.QuadPart;
                if (wallTS > m_audioTimestamp)
                {
                    const UINT32 gapFrames = static_cast<UINT32>(
                        (wallTS - m_audioTimestamp) * m_pWaveFormat->nSamplesPerSec / 10'000'000LL);
                    if (gapFrames > 0)
                    {
                        std::lock_guard<std::mutex> lk(m_mutex);
                        WriteAudioSilence(m_pSinkWriter, m_audioStreamIndex, m_pWaveFormat, gapFrames, m_audioTimestamp);
                    }
                    m_audioTimestamp = wallTS;
                }
            }

            const LONGLONG dur    = static_cast<LONGLONG>(numFrames) * 10'000'000LL / m_pWaveFormat->nSamplesPerSec;
            const LONGLONG audioTS = m_audioTimestamp;
            m_audioTimestamp      += dur;

            const DWORD bytes = numFrames * m_pWaveFormat->nBlockAlign;

            IMFMediaBuffer* pBuf = nullptr;
            if (SUCCEEDED(MFCreateMemoryBuffer(bytes, &pBuf)))
            {
                BYTE* pDst = nullptr;
                if (SUCCEEDED(pBuf->Lock(&pDst, nullptr, nullptr)))
                {
                    if (flags & AUDCLNT_BUFFERFLAGS_SILENT)
                        memset(pDst, 0, bytes);
                    else
                        memcpy(pDst, pData, bytes);

                    // Release WASAPI buffer before mixing so mic poll doesn't stall
                    m_pCaptureClient->ReleaseBuffer(numFrames);

                    // Mixed mode: blend mic inline - same thread, no locks needed
                    if (m_micMode == MicMode::Mixed)
                        MixMicInline(pDst, numFrames);

                    pBuf->Unlock();
                    pBuf->SetCurrentLength(bytes);
                }
                else
                {
                    m_pCaptureClient->ReleaseBuffer(numFrames);
                }

                IMFSample* pS = nullptr;
                if (SUCCEEDED(MFCreateSample(&pS)))
                {
                    pS->AddBuffer(pBuf);
                    pS->SetSampleTime(audioTS);
                    pS->SetSampleDuration(dur);
                    {
                        std::lock_guard<std::mutex> lk(m_mutex);
                        m_pSinkWriter->WriteSample(m_audioStreamIndex, pS);
                    }
                    pS->Release();
                }
                pBuf->Release();
            }
            else
            {
                m_pCaptureClient->ReleaseBuffer(numFrames);
            }

            if (FAILED(m_pCaptureClient->GetNextPacketSize(&packetFrames))) break;
        }

        // Idle path: when the loopback has no game audio, WASAPI returns 0 packets
        // and MixMicInline never runs. Drain the mic WASAPI buffer manually and write
        // silence+mic packets so voice is captured even during silent game periods.
        if (!hadLoopbackData && m_micMode == MicMode::Mixed &&
            m_pMicCaptureClient && m_pWaveFormat && m_pMicWaveFormat)
        {
            // Step 1: drain WASAPI mic buffer into the accumulator
            UINT32 micPkt = 0;
            if (SUCCEEDED(m_pMicCaptureClient->GetNextPacketSize(&micPkt)))
            {
                while (micPkt > 0)
                {
                    BYTE* pMicRaw = nullptr; UINT32 micFrames = 0; DWORD micFlags = 0;
                    if (FAILED(m_pMicCaptureClient->GetBuffer(&pMicRaw, &micFrames, &micFlags, nullptr, nullptr))) break;
                    const DWORD  micBytes = micFrames * m_pMicWaveFormat->nBlockAlign;
                    const size_t old      = m_micAccum.size();
                    m_micAccum.resize(old + micBytes);
                    if (micFlags & AUDCLNT_BUFFERFLAGS_SILENT)
                        memset(m_micAccum.data() + old, 0, micBytes);
                    else
                        memcpy(m_micAccum.data() + old, pMicRaw, micBytes);
                    m_pMicCaptureClient->ReleaseBuffer(micFrames);
                    if (FAILED(m_pMicCaptureClient->GetNextPacketSize(&micPkt))) break;
                }
            }

            // Step 2: write silence+mic chunks while the accumulator has >= 10ms of data
            // MixMicInline reads from the accumulator (WASAPI already drained above)
            const UINT32 chunkFrames = m_pWaveFormat->nSamplesPerSec / 100; // 10ms
            for (;;)
            {
                const size_t  avail      = m_micAccum.size() - m_micAccumRead;
                const UINT32  availFrms  = static_cast<UINT32>(avail / m_pMicWaveFormat->nBlockAlign);
                if (availFrms < chunkFrames) break;

                const DWORD    bytes   = chunkFrames * m_pWaveFormat->nBlockAlign;
                const LONGLONG dur     = static_cast<LONGLONG>(chunkFrames) * 10'000'000LL / m_pWaveFormat->nSamplesPerSec;
                const LONGLONG audioTS = m_audioTimestamp;
                m_audioTimestamp      += dur;

                IMFMediaBuffer* pBuf = nullptr;
                if (SUCCEEDED(MFCreateMemoryBuffer(bytes, &pBuf)))
                {
                    BYTE* pDst = nullptr;
                    if (SUCCEEDED(pBuf->Lock(&pDst, nullptr, nullptr)))
                    {
                        memset(pDst, 0, bytes);             // silence base
                        MixMicInline(pDst, chunkFrames);    // blend mic on top
                        pBuf->Unlock();
                        pBuf->SetCurrentLength(bytes);
                    }
                    IMFSample* pS = nullptr;
                    if (SUCCEEDED(MFCreateSample(&pS)))
                    {
                        pS->AddBuffer(pBuf);
                        pS->SetSampleTime(audioTS);
                        pS->SetSampleDuration(dur);
                        std::lock_guard<std::mutex> lk(m_mutex);
                        m_pSinkWriter->WriteSample(m_audioStreamIndex, pS);
                        pS->Release();
                    }
                    pBuf->Release();
                }
            }
        }
    }

    if (m_pAudioClient) m_pAudioClient->Stop();
    if (m_micMode == MicMode::Mixed && m_pMicAudioClient) m_pMicAudioClient->Stop();
}

// ---------------------------------------------------------------------------
// InitMonitor - opens the default render endpoint to feed mic audio back to
// speakers so the user can hear themselves before the mix hits the recording.
// ---------------------------------------------------------------------------
bool ScreenRecorder::InitMonitor()
{
    IMMDeviceEnumerator* pEnum = nullptr;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&pEnum));
    if (FAILED(hr)) return false;

    // Query both the communications and console render endpoints so we can
    // compare their IDs. If they share the same physical device the monitor
    // output flows straight back into the eConsole loopback capture, and
    // MixMicInline must skip its direct blend to avoid doubling the mic level.
    // When they differ (separate headset vs speakers) the communications
    // endpoint is preferred and the direct blend is still applied.
    IMMDevice* pCommsDevice   = nullptr;
    IMMDevice* pConsoleDevice = nullptr;
    pEnum->GetDefaultAudioEndpoint(eRender, eCommunications, &pCommsDevice);
    pEnum->GetDefaultAudioEndpoint(eRender, eConsole,        &pConsoleDevice);
    pEnum->Release();

    IMMDevice* pDevice        = nullptr;
    m_monitorOnLoopbackDevice = true;   // conservative default until proven otherwise

    if (pCommsDevice && pConsoleDevice)
    {
        // Compare endpoint IDs to detect whether both roles resolve to the same device
        LPWSTR commsId   = nullptr;
        LPWSTR consoleId = nullptr;
        pCommsDevice->GetId(&commsId);
        pConsoleDevice->GetId(&consoleId);

        if (commsId && consoleId && wcscmp(commsId, consoleId) != 0)
        {
            // Physically separate endpoints - monitor will NOT contaminate the loopback
            pDevice = pCommsDevice;
            pCommsDevice = nullptr;   // ownership transferred
            m_monitorOnLoopbackDevice = false;
        }
        if (commsId)   CoTaskMemFree(commsId);
        if (consoleId) CoTaskMemFree(consoleId);
    }

    // Fall back to eConsole if eCommunications was unavailable or mapped to the same device
    if (!pDevice)
    {
        pDevice = pConsoleDevice;
        pConsoleDevice = nullptr;
        m_monitorOnLoopbackDevice = true;
    }

    if (pCommsDevice)  { pCommsDevice->Release();  pCommsDevice  = nullptr; }
    if (pConsoleDevice){ pConsoleDevice->Release(); pConsoleDevice = nullptr; }
    if (!pDevice) return false;

    hr = pDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                           reinterpret_cast<void**>(&m_pMonitorAudioClient));
    pDevice->Release();
    if (FAILED(hr)) return false;

    if (FAILED(m_pMonitorAudioClient->GetMixFormat(&m_pMonitorFormat))) return false;

    if (m_pMonitorFormat->nSamplesPerSec != m_pMicWaveFormat->nSamplesPerSec)
    {
        debug.logDiagLevelMessage(LogLevel::LOG_WARNING,
            L"ScreenRecorder Monitor: Sample rate mismatch (mic=" +
            std::to_wstring(m_pMicWaveFormat->nSamplesPerSec) +
            L" render=" + std::to_wstring(m_pMonitorFormat->nSamplesPerSec) +
            L") - monitoring disabled");
        CleanupMonitor();
        return false;
    }

    hr = m_pMonitorAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, 0,
                                            30 * 10000, 0, m_pMonitorFormat, nullptr);
    if (FAILED(hr)) { CleanupMonitor(); return false; }

    if (FAILED(m_pMonitorAudioClient->GetBufferSize(&m_monitorBufferFrames))) { CleanupMonitor(); return false; }

    if (FAILED(m_pMonitorAudioClient->GetService(IID_PPV_ARGS(&m_pMonitorRenderClient)))) { CleanupMonitor(); return false; }

    m_pMonitorAudioClient->Start();
    #if defined(_DEBUG_SCREEN_RECORDER_)
        debug.logLevelMessage(LogLevel::LOG_INFO,
            std::wstring(m_monitorOnLoopbackDevice
                ? L"ScreenRecorder Monitor: shared eConsole endpoint (mic may also echo via loopback) -> "
                : L"ScreenRecorder Monitor: separate eCommunications endpoint -> ") +
            std::to_wstring(m_pMonitorFormat->nSamplesPerSec) + L"Hz " +
            std::to_wstring(m_pMonitorFormat->nChannels) + L"ch");
    #endif
    return true;
}

// ---------------------------------------------------------------------------
// WriteMonitorSamples - pushes raw mic PCM to the render client so the user
// hears the mic in real-time. Upmixes mono mic to however many channels the
// render device uses. Both formats are assumed float32 (WASAPI shared mode).
// ---------------------------------------------------------------------------
void ScreenRecorder::WriteMonitorSamples(const BYTE* pMicData, UINT32 micFrames)
{
    if (!m_pMonitorRenderClient || !m_pMonitorFormat || !pMicData || micFrames == 0) return;

    UINT32 padding = 0;
    if (FAILED(m_pMonitorAudioClient->GetCurrentPadding(&padding))) return;

    const UINT32 available = m_monitorBufferFrames > padding ? m_monitorBufferFrames - padding : 0;
    const UINT32 toWrite   = micFrames < available ? micFrames : available;
    if (toWrite == 0) return;

    BYTE* pBuf = nullptr;
    if (FAILED(m_pMonitorRenderClient->GetBuffer(toWrite, &pBuf))) return;

    const UINT32 monCh  = m_pMonitorFormat->nChannels;
    const UINT32 micCh  = m_pMicWaveFormat->nChannels;
    const bool   monFlt = IsIEEEFloat(m_pMonitorFormat);
    const bool   micFlt = IsIEEEFloat(m_pMicWaveFormat);

    if (monFlt && micFlt)
    {
        const float* src  = reinterpret_cast<const float*>(pMicData);
        float*       dst  = reinterpret_cast<float*>(pBuf);
        const float  gain = m_micMonitorGain.load();
        for (UINT32 f = 0; f < toWrite; ++f)
        {
            float s = src[f * micCh] * gain;  // first mic channel, scaled
            s = s < -1.0f ? -1.0f : (s > 1.0f ? 1.0f : s);
            for (UINT32 ch = 0; ch < monCh; ++ch)
                dst[f * monCh + ch] = s;
        }
    }
    else
    {
        // Unsupported format combination - write silence rather than noise
        memset(pBuf, 0, toWrite * m_pMonitorFormat->nBlockAlign);
    }

    m_pMonitorRenderClient->ReleaseBuffer(toWrite, 0);
}

// ---------------------------------------------------------------------------
// CleanupMonitor
// ---------------------------------------------------------------------------
void ScreenRecorder::CleanupMonitor()
{
    if (m_pMonitorAudioClient)  { m_pMonitorAudioClient->Stop(); m_pMonitorAudioClient->Release(); m_pMonitorAudioClient = nullptr; }
    if (m_pMonitorRenderClient) { m_pMonitorRenderClient->Release(); m_pMonitorRenderClient = nullptr; }
    if (m_pMonitorFormat)       { CoTaskMemFree(m_pMonitorFormat); m_pMonitorFormat = nullptr; }
    m_monitorBufferFrames = 0;
}

// ---------------------------------------------------------------------------
// CleanupAudio
// ---------------------------------------------------------------------------
void ScreenRecorder::CleanupAudio()
{
    if (m_pCaptureClient) { m_pCaptureClient->Release(); m_pCaptureClient = nullptr; }
    if (m_pAudioClient)   { m_pAudioClient->Release();   m_pAudioClient   = nullptr; }
    if (m_pWaveFormat)    { CoTaskMemFree(m_pWaveFormat); m_pWaveFormat    = nullptr; }
    m_audioTimestamp = 0;
}

// ---------------------------------------------------------------------------
// ScoreMicEndpoint
//
// Rates a capture endpoint's suitability as the recording microphone.
// The highest-scoring endpoint is the one used.
//
//   Headset      +100  gaming headsets are the preferred voice source
//   Microphone    +60  desktop / built-in mic arrays
//   Headphones    +40  may carry an inline mic
//   anything else +10
//   default (eConsole)        +20  user's chosen Windows default
//   default (eCommunications) +10
//
// Instrument / line adapters (e.g. the Rocksmith USB Guitar Adapter - a cable
// for guitars or a singing mic, not speech) are penalised -1000 by name so
// they can never win against a real microphone. A device must score > 0 to be
// eligible at all.
// ---------------------------------------------------------------------------
static int ScoreMicEndpoint(IMMDevice* pDevice, bool isDefaultConsole,
                            bool isDefaultComms, std::wstring& nameOut)
{
    nameOut = L"<unknown>";

    IPropertyStore* pProps = nullptr;
    if (FAILED(pDevice->OpenPropertyStore(STGM_READ, &pProps))) return -1;

    PROPVARIANT v;
    PropVariantInit(&v);
    if (SUCCEEDED(pProps->GetValue(PKEY_Device_FriendlyName, &v)) && v.vt == VT_LPWSTR)
        nameOut = v.pwszVal;
    PropVariantClear(&v);

    UINT formFactor = UnknownFormFactor;
    PropVariantInit(&v);
    if (SUCCEEDED(pProps->GetValue(PKEY_AudioEndpoint_FormFactor, &v)) && v.vt == VT_UI4)
        formFactor = v.ulVal;
    PropVariantClear(&v);
    pProps->Release();

    int score = 0;
    switch (formFactor)
    {
        case Headset:    score += 100; break;
        case Microphone: score +=  60; break;
        case Headphones: score +=  40; break;
        default:         score +=  10; break;
    }
    if (isDefaultConsole) score += 20;
    if (isDefaultComms)   score += 10;

    // Name-based disqualifiers: instrument cables, line inputs and virtual
    // loopback devices are not speech sources.
    std::wstring lower = nameOut;
    for (wchar_t& c : lower) c = towlower(c);
    static const wchar_t* instrumentHints[] = {
        L"guitar", L"rocksmith", L"instrument", L"line in", L"line-in",
        L"loopback", L"virtual", L"cable", L"stereo mix", L"what u hear"
    };
    for (const wchar_t* hint : instrumentHints)
        if (lower.find(hint) != std::wstring::npos) { score -= 1000; break; }

    return score;
}

// ---------------------------------------------------------------------------
// InitMicCapture
//
// Enumerates every active capture endpoint, scores each with ScoreMicEndpoint
// and activates the highest-scoring one. If no endpoint scores above zero the
// recording proceeds without a microphone and a warning is logged.
// ---------------------------------------------------------------------------
bool ScreenRecorder::InitMicCapture()
{
    HRESULT hr;
    IMMDeviceEnumerator* pEnum = nullptr;
    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&pEnum));
    if (FAILED(hr))
    {
        debug.logDiagLevelMessage(LogLevel::LOG_ERROR, L"ScreenRecorder Mic: MMDeviceEnumerator failed (" + HRStr(hr) + L")");
        return false;
    }

    // Resolve the two default endpoint IDs so the scorer can award their bonuses
    auto defaultEndpointId = [pEnum](ERole role) -> std::wstring
    {
        std::wstring id;
        IMMDevice* pDev = nullptr;
        if (SUCCEEDED(pEnum->GetDefaultAudioEndpoint(eCapture, role, &pDev)))
        {
            LPWSTR pId = nullptr;
            if (SUCCEEDED(pDev->GetId(&pId)) && pId) { id = pId; CoTaskMemFree(pId); }
            pDev->Release();
        }
        return id;
    };
    const std::wstring defConsoleId = defaultEndpointId(eConsole);
    const std::wstring defCommsId   = defaultEndpointId(eCommunications);

    IMMDeviceCollection* pColl = nullptr;
    hr = pEnum->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &pColl);
    pEnum->Release();
    if (FAILED(hr) || !pColl)
    {
        debug.logDiagLevelMessage(LogLevel::LOG_ERROR, L"ScreenRecorder Mic: EnumAudioEndpoints failed (" + HRStr(hr) + L")");
        return false;
    }

    UINT count = 0;
    pColl->GetCount(&count);

    IMMDevice*   pBestDevice = nullptr;
    int          bestScore   = 0;      // require > 0 - disqualified devices never win
    std::wstring bestName;

    for (UINT i = 0; i < count; ++i)
    {
        IMMDevice* pDev = nullptr;
        if (FAILED(pColl->Item(i, &pDev)) || !pDev) continue;

        std::wstring devId;
        LPWSTR pId = nullptr;
        if (SUCCEEDED(pDev->GetId(&pId)) && pId) { devId = pId; CoTaskMemFree(pId); }

        std::wstring name;
        const int score = ScoreMicEndpoint(pDev,
                                           !devId.empty() && devId == defConsoleId,
                                           !devId.empty() && devId == defCommsId,
                                           name);

        #if defined(_DEBUG_SCREEN_RECORDER_)
            debug.logLevelMessage(LogLevel::LOG_INFO,
                L"ScreenRecorder Mic: Candidate \"" + name + L"\" score " + std::to_wstring(score));
        #endif

        if (score > bestScore)
        {
            if (pBestDevice) pBestDevice->Release();
            pBestDevice = pDev;          // keep the reference
            bestScore   = score;
            bestName    = name;
        }
        else
        {
            pDev->Release();
        }
    }
    pColl->Release();

    if (!pBestDevice)
    {
        debug.logDiagLevelMessage(LogLevel::LOG_WARNING,
            L"ScreenRecorder Mic: No suitable microphone endpoint found");
        return false;
    }

    #if defined(_DEBUG_SCREEN_RECORDER_)
        debug.logLevelMessage(LogLevel::LOG_INFO,
            L"ScreenRecorder Mic: Selected \"" + bestName + L"\" (score " + std::to_wstring(bestScore) + L")");
    #endif

    hr = pBestDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                               reinterpret_cast<void**>(&m_pMicAudioClient));
    pBestDevice->Release();
    if (FAILED(hr))
    {
        if (hr == E_ACCESSDENIED || hr == HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED))
        {
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"ScreenRecorder Mic: Access denied - opening privacy settings");
            ShellExecuteW(nullptr, L"open", L"ms-settings:privacy-microphone", nullptr, nullptr, SW_SHOW);
        }
        else
        {
            debug.logDiagLevelMessage(LogLevel::LOG_ERROR, L"ScreenRecorder Mic: Activate failed (" + HRStr(hr) + L")");
        }
        return false;
    }

    if (FAILED(m_pMicAudioClient->GetMixFormat(&m_pMicWaveFormat)))
    {
        debug.logDiagLevelMessage(LogLevel::LOG_ERROR, L"ScreenRecorder Mic: GetMixFormat failed");
        return false;
    }

    // If the mic's native format differs from the game loopback format (e.g. mic
    // endpoint set to 44100Hz while the render device runs 48000Hz), capture the
    // mic in the GAME format instead and let WASAPI resample via AUTOCONVERTPCM.
    // This keeps mic, loopback and monitor sample rates locked so the inline
    // blend stays 1:1 and mic monitoring can never fail on a rate mismatch.
    DWORD micStreamFlags = 0;
    if (m_pWaveFormat &&
        (m_pMicWaveFormat->nSamplesPerSec != m_pWaveFormat->nSamplesPerSec ||
         m_pMicWaveFormat->nChannels     != m_pWaveFormat->nChannels))
    {
        const UINT32 fmtSize = sizeof(WAVEFORMATEX) + m_pWaveFormat->cbSize;
        WAVEFORMATEX* pClone = static_cast<WAVEFORMATEX*>(CoTaskMemAlloc(fmtSize));
        if (pClone)
        {
            memcpy(pClone, m_pWaveFormat, fmtSize);
            #if defined(_DEBUG_SCREEN_RECORDER_)
                debug.logLevelMessage(LogLevel::LOG_INFO,
                    L"ScreenRecorder Mic: Native format " +
                    std::to_wstring(m_pMicWaveFormat->nSamplesPerSec) + L"Hz " +
                    std::to_wstring(m_pMicWaveFormat->nChannels) + L"ch differs from game audio - capturing at " +
                    std::to_wstring(pClone->nSamplesPerSec) + L"Hz " +
                    std::to_wstring(pClone->nChannels) + L"ch via WASAPI auto-convert");
            #endif
            CoTaskMemFree(m_pMicWaveFormat);
            m_pMicWaveFormat = pClone;
            micStreamFlags   = AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                               AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
        }
    }

    hr = m_pMicAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, micStreamFlags,
                                        200 * 10000, 0, m_pMicWaveFormat, nullptr);
    if (FAILED(hr)) {
        debug.logDiagLevelMessage(LogLevel::LOG_ERROR, L"ScreenRecorder Mic: Initialize failed (" + HRStr(hr) + L")");
        return false;
    }

    if (FAILED(m_pMicAudioClient->GetService(IID_PPV_ARGS(&m_pMicCaptureClient))))
    {
        debug.logDiagLevelMessage(LogLevel::LOG_ERROR, L"ScreenRecorder Mic: GetService failed");
        return false;
    }

    // Separate mode: add a second AAC stream
    if (m_micMode == MicMode::Separate)
    {
        IMFMediaType* pOut = nullptr;
        hr = MFCreateMediaType(&pOut);
        if (SUCCEEDED(hr)) hr = pOut->SetGUID  (MF_MT_MAJOR_TYPE,                 MFMediaType_Audio);
        if (SUCCEEDED(hr)) hr = pOut->SetGUID  (MF_MT_SUBTYPE,                    MFAudioFormat_AAC);
        if (SUCCEEDED(hr)) hr = pOut->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE,      16);
        if (SUCCEEDED(hr)) hr = pOut->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND,   m_pMicWaveFormat->nSamplesPerSec);
        if (SUCCEEDED(hr)) hr = pOut->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS,         m_pMicWaveFormat->nChannels);
        if (SUCCEEDED(hr)) hr = pOut->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, static_cast<UINT32>(m_audioBitrate) / 8);
        if (SUCCEEDED(hr)) hr = m_pSinkWriter->AddStream(pOut, &m_micStreamIndex);
        if (pOut) pOut->Release();

        if (SUCCEEDED(hr))
        {
            IMFMediaType* pIn = nullptr;
            hr = MFCreateMediaType(&pIn);
            if (SUCCEEDED(hr))
            {
                UINT32 sz = sizeof(WAVEFORMATEX) + m_pMicWaveFormat->cbSize;
                hr = MFInitMediaTypeFromWaveFormatEx(pIn, m_pMicWaveFormat, sz);
            }
            if (SUCCEEDED(hr)) hr = m_pSinkWriter->SetInputMediaType(m_micStreamIndex, pIn, nullptr);
            if (pIn) pIn->Release();
        }

        if (FAILED(hr))
        {
            debug.logDiagLevelMessage(LogLevel::LOG_ERROR, L"ScreenRecorder Mic: Failed to add separate stream (" + HRStr(hr) + L")");
            return false;
        }
    }

    const bool micFloat = IsIEEEFloat(m_pMicWaveFormat);
    #if defined(_DEBUG_SCREEN_RECORDER_)
        debug.logLevelMessage(LogLevel::LOG_INFO,
            std::wstring(m_micMode == MicMode::Mixed
                ? L"ScreenRecorder Mic: Ready (mixed inline) - "
                : L"ScreenRecorder Mic: Ready (separate track) - ") +
            std::to_wstring(m_pMicWaveFormat->nSamplesPerSec) + L"Hz " +
        std::to_wstring(m_pMicWaveFormat->nChannels) + L"ch " +
        (micFloat ? L"float32" : L"int16"));
    #endif
    return true;
}

// ---------------------------------------------------------------------------
// MicCaptureThread  - Separate mode only
// ---------------------------------------------------------------------------
void ScreenRecorder::MicCaptureThread()
{
    while (m_isRecording.load())
    {
        Sleep(10);

        UINT32 packetFrames = 0;
        if (FAILED(m_pMicCaptureClient->GetNextPacketSize(&packetFrames))) break;

        while (packetFrames > 0)
        {
            BYTE* pData = nullptr; UINT32 numFrames = 0; DWORD flags = 0;
            if (FAILED(m_pMicCaptureClient->GetBuffer(&pData, &numFrames, &flags, nullptr, nullptr))) break;

            if (flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY)
            {
                LARGE_INTEGER now;
                QueryPerformanceCounter(&now);
                const LONGLONG wallTS = (now.QuadPart - m_qpcRecordingStart.QuadPart) * 10'000'000LL / m_qpcFreq.QuadPart;
                if (wallTS > m_micAudioTimestamp)
                {
                    const UINT32 gapFrames = static_cast<UINT32>(
                        (wallTS - m_micAudioTimestamp) * m_pMicWaveFormat->nSamplesPerSec / 10'000'000LL);
                    if (gapFrames > 0)
                    {
                        std::lock_guard<std::mutex> lk(m_mutex);
                        WriteAudioSilence(m_pSinkWriter, m_micStreamIndex, m_pMicWaveFormat, gapFrames, m_micAudioTimestamp);
                    }
                    m_micAudioTimestamp = wallTS;
                }
            }

            const LONGLONG dur  = static_cast<LONGLONG>(numFrames) * 10'000'000LL / m_pMicWaveFormat->nSamplesPerSec;
            const LONGLONG micTS = m_micAudioTimestamp;
            m_micAudioTimestamp += dur;
            const DWORD bytes = numFrames * m_pMicWaveFormat->nBlockAlign;

            IMFMediaBuffer* pBuf = nullptr;
            if (SUCCEEDED(MFCreateMemoryBuffer(bytes, &pBuf)))
            {
                BYTE* pDst = nullptr;
                if (SUCCEEDED(pBuf->Lock(&pDst, nullptr, nullptr)))
                {
                    if (flags & AUDCLNT_BUFFERFLAGS_SILENT)
                        memset(pDst, 0, bytes);
                    else
                    {
                        memcpy(pDst, pData, bytes);
                        WriteMonitorSamples(pData, numFrames);
                    }
                    pBuf->Unlock(); pBuf->SetCurrentLength(bytes);
                }
                IMFSample* pS = nullptr;
                if (SUCCEEDED(MFCreateSample(&pS)))
                {
                    pS->AddBuffer(pBuf); pS->SetSampleTime(micTS); pS->SetSampleDuration(dur);
                    {
                        std::lock_guard<std::mutex> lk(m_mutex);
                        m_pSinkWriter->WriteSample(m_micStreamIndex, pS);
                    }
                    pS->Release();
                }
                pBuf->Release();
            }

            m_pMicCaptureClient->ReleaseBuffer(numFrames);
            if (FAILED(m_pMicCaptureClient->GetNextPacketSize(&packetFrames))) break;
        }
    }

    if (m_pMicAudioClient) m_pMicAudioClient->Stop();
}

// ---------------------------------------------------------------------------
// CleanupMic
// ---------------------------------------------------------------------------
void ScreenRecorder::CleanupMic()
{
    if (m_pMicCaptureClient) { m_pMicCaptureClient->Release(); m_pMicCaptureClient = nullptr; }
    if (m_pMicAudioClient)   { m_pMicAudioClient->Release();   m_pMicAudioClient   = nullptr; }
    if (m_pMicWaveFormat)    { CoTaskMemFree(m_pMicWaveFormat); m_pMicWaveFormat    = nullptr; }
    m_micAudioTimestamp = 0;
}

#else
// ============================================================================
// Linux / Android FFmpeg backend (Phase B)
//
// Replaces Media Foundation / WASAPI with libavformat / libavcodec /
// libswscale / libswresample: H.264 video (libx264, falling back to the
// native mpeg4 encoder) and AAC audio muxed into an MP4 file.
//
// Video: CaptureFrame reads the back-buffer (glReadPixels on OpenGL, a
// staging-buffer copy on Vulkan -- the same readback code as the Windows
// path, which is platform-independent) and WriteVideoFrameFFmpeg scales
// BGRA to YUV420P and encodes it. Video timestamps use the frame index
// in the stream time base (1/fps), so they are evenly spaced and
// monotonic regardless of wall-clock jitter.
//
// Audio: game music is captured from the tracker players through the
// shared Miniaudio::Stream tap (SetTapOnAllStreams -- it installs the tap
// on streams that already exist as well as ones created later). The tap
// runs on the audio thread and appends mixed 44100 Hz stereo s16 PCM to
// a lock-protected queue; the render thread drains the queue, resamples
// to 48 kHz float planar and encodes AAC, timestamp-locked to the video
// clock (audio is never encoded ahead of the current video frame).
// SoundManager one-shots (SFX) go through ma_engine, which has no output
// tap, so SFX are NOT in the recording -- only tracker music is.
//
// Mic: not available on Linux yet (no WASAPI / PulseAudio capture wired
// in). A non-Off MicMode logs one warning and records without a mic.
//
// FFmpeg is optional: when the headers are absent (__has_include fails)
// HAS_FFMPEG is 0 and StartRecording keeps the old stub behaviour -- one
// "FFmpeg not available" warning, false, no throw, no quit -- so the code
// still compiles on a machine without FFmpeg dev files. CMake links the
// FFmpeg libraries only when both the headers and the libraries are
// found (see Linux/CMakeLists.txt).
// ============================================================================

#if defined(PLATFORM_LINUX) || defined(PLATFORM_ANDROID)
    #if __has_include(<libavformat/avformat.h>) && __has_include(<libavcodec/avcodec.h>) && \
        __has_include(<libswscale/swscale.h>) && __has_include(<libswresample/swresample.h>)
        #define HAS_FFMPEG 1
        extern "C" {
        #include <libavformat/avformat.h>
        #include <libavcodec/avcodec.h>
        #include <libswscale/swscale.h>
        #include <libswresample/swresample.h>
        #include <libavutil/avutil.h>
        #include <libavutil/imgutils.h>
        #include <libavutil/opt.h>
        #include <libavutil/channel_layout.h>
        }
        #include "MiniaudioStream.h"
    #else
        #define HAS_FFMPEG 0
    #endif
#else
    #define HAS_FFMPEG 0
#endif

// Android MediaCodec backend (Phase D step 4, 24 Nov 2026): the platform
// media APIs (mediandk) are always present on Android (minSdk 26), so
// there are no headers to probe for -- unlike FFmpeg, which is optional
// on Linux. HAS_MEDIACODEC is 1 on Android, 0 everywhere else, and the
// backend itself lives in android-screenrecorder.cpp. The two backends
// are independent: a build can have FFmpeg, MediaCodec, both, or neither.
// (On Android, HAS_FFMPEG is 0 -- the FFmpeg headers are not in the NDK
// sysroot -- so MediaCodec is the only recording path there.)
#if defined(PLATFORM_ANDROID)
    #define HAS_MEDIACODEC 1
#else
    #define HAS_MEDIACODEC 0
#endif

// GL headers for the OpenGL CaptureFrame (glReadPixels). The Windows GL
// headers arrive via Includes.h; on Linux they do not, and
// ScreenRecorder.cpp does not include OpenGLRenderer.h (which is where
// they live). Desktop GL on Linux, GLES3 on Android.
#if defined(PLATFORM_LINUX) && defined(__USE_OPENGL__)
    #include <GL/glew.h>
    #include <GL/gl.h>
#elif defined(PLATFORM_ANDROID) && defined(__USE_OPENGL__)
    #include <GLES3/gl3.h>
#endif

namespace {
    // One warning per process, however often the stub path is used.
    void LogScreenRecorderUnavailableOnce()
    {
        static std::atomic<bool> warned{ false };
        if (!warned.exchange(true))
        {
            Debug::logDiagLevelMessage(LogLevel::LOG_WARNING,
                L"ScreenRecorder is unavailable on this build (FFmpeg headers not found: libavformat / libavcodec / libswscale / libswresample); screen recording is disabled.");
        }
    }
    // One warning per process for the missing microphone backend.
    void LogRecorderMicUnavailableOnce()
    {
        static std::atomic<bool> warned{ false };
        if (!warned.exchange(true))
        {
            Debug::logDiagLevelMessage(LogLevel::LOG_WARNING,
                L"ScreenRecorder: Microphone capture is not available on Linux yet; recording without a microphone.");
        }
    }
}

// H.264 output bitrate, matching the Windows MF path.
static constexpr int kRecorderVideoBitrate = 10'000'000;
// AAC output sample rate (the native AAC encoder's preferred rate).
static constexpr int kRecorderAudioRate = 48000;

#if HAS_FFMPEG

namespace
{

// All FFmpeg state for one recording. Owned through the opaque void*
// ScreenRecorder::m_ffmpeg so the FFmpeg types never leak into the header.
struct RecorderFFmpeg
{
    AVFormatContext* outFmt = nullptr;
    AVStream* vstream = nullptr;
    AVStream* astream = nullptr;
    AVCodecContext* venc = nullptr;
    AVCodecContext* aenc = nullptr;
    SwsContext* sws = nullptr;
    SwrContext* swr = nullptr;
    AVFrame* vframe = nullptr;          // YUV420P, m_width x m_height
    AVFrame* aframe = nullptr;          // FLTP, 48000 Hz stereo, aenc->frame_size samples
    AVPacket* pkt = nullptr;
    int swsInW = 0;                     // input size the sws was created for
    int swsInH = 0;
    int64_t audioPts = 0;               // audio samples encoded (48 kHz clock)
    std::string outPath;
    bool hasAudio = false;
    // Resampled audio staging (planar float, 48 kHz). Filled by the
    // resampler, drained in whole AAC frames by WriteAudioFramesFFmpeg.
    std::vector<float> audioStageL;
    std::vector<float> audioStageR;
};

void FreeRecorderFFmpeg(RecorderFFmpeg* r)
{
    if (!r)
        return;
    if (r->vframe)
        av_frame_free(&r->vframe);
    if (r->aframe)
        av_frame_free(&r->aframe);
    if (r->pkt)
        av_packet_free(&r->pkt);
    if (r->sws)
    {
        sws_freeContext(r->sws);
        r->sws = nullptr;
    }
    if (r->swr)
        swr_free(&r->swr);
    if (r->venc)
        avcodec_free_context(&r->venc);
    if (r->aenc)
        avcodec_free_context(&r->aenc);
    if (r->outFmt)
    {
        // The file was already closed (avio_closep) by StopRecording's
        // trailer path; if we get here early, close it now.
        if (r->outFmt->pb)
            avio_closep(&r->outFmt->pb);
        avformat_free_context(r->outFmt);
    }
    delete r;
}

// Encode one frame (or flush when frame == nullptr) and write every
// packet the encoder produces. Returns false on an encode/write error.
bool EncodeAndWrite(RecorderFFmpeg* r, AVCodecContext* enc, AVStream* stream, AVFrame* frame)
{
    if (avcodec_send_frame(enc, frame) < 0)
        return false;
    while (true)
    {
        const int rc = avcodec_receive_packet(enc, r->pkt);
        if (rc == AVERROR(EAGAIN) || rc == AVERROR_EOF)
            return true;
        if (rc < 0)
            return false;
        av_packet_rescale_ts(r->pkt, enc->time_base, stream->time_base);
        r->pkt->stream_index = stream->index;
        if (av_interleaved_write_frame(r->outFmt, r->pkt) < 0)
        {
            av_packet_unref(r->pkt);
            return false;
        }
        av_packet_unref(r->pkt);
    }
}

} // namespace

#endif // HAS_FFMPEG

ScreenRecorder::ScreenRecorder()
    : m_width(0), m_height(0)
    , m_targetFPS(RecordFPS::FPS_60)
    , m_audioBitrate(AudioBitrate::Kbps_192)
    , m_framePeriod(0), m_videoFrameIndex(-1), m_framePeriodSnapshot(0)
    , m_audioTimestamp(0)
    , m_micMode(MicMode::Off)
    , m_micAccumRead(0)
    , m_micDataLogged(false)
    , m_micDiagPeak(0.0f), m_micDiagFrames(0)
    , m_micStreamIndex(0), m_micAudioTimestamp(0)
    , m_micMonitorGain(2.5f), m_micRecordGain(2.5f)
    , m_isRecording(false)
{
    m_recordingStart = std::chrono::steady_clock::now();
    m_lastVideoTime = m_recordingStart;
}

ScreenRecorder::~ScreenRecorder()
{
    if (m_isRecording.load()) StopRecording();
}

bool ScreenRecorder::StartRecording(UINT width, UINT height,
                                    RecordFPS fps,
                                    const std::wstring& outputPath,
                                    MicMode micMode,
                                    AudioBitrate audioBitrate, bool captureAudio)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_isRecording.load()) return false;

    UINT32 fpsVal = static_cast<UINT32>(fps);
    if (fpsVal != 15 && fpsVal != 30 && fpsVal != 60 && fpsVal != 90 && fpsVal != 120)
    {
        debug.logDiagLevelMessage(LogLevel::LOG_WARNING,
            L"ScreenRecorder: Unsupported fps " + std::to_wstring(fpsVal) + L" - defaulting to 60fps");
        fpsVal = 60; fps = RecordFPS::FPS_60;
    }

    m_width               = width  & ~1u;   // H.264 requires even dimensions
    m_height              = height & ~1u;
    m_targetFPS           = fps;
    m_framePeriod         = 10'000'000LL / static_cast<LONGLONG>(fpsVal);
    m_framePeriodSnapshot = m_framePeriod;
    m_videoFrameIndex.store(0);
    m_captureEnabled.store(true); m_pauseStart = m_pausedTicks = 0;
    m_audioTimestamp      = 0;
    m_micMode             = micMode;
    m_audioBitrate        = audioBitrate;
    m_micAccum.clear();
    m_micAccumRead        = 0;
    m_micDataLogged       = false;
    m_micDiagPeak         = 0.0f;
    m_micDiagFrames       = 0;
    m_micAudioTimestamp   = 0;

    if (micMode != MicMode::Off)
        LogRecorderMicUnavailableOnce();

#if !HAS_FFMPEG && !HAS_MEDIACODEC
    LogScreenRecorderUnavailableOnce();
    return false;
#elif HAS_MEDIACODEC
    // Android MediaCodec backend (android-screenrecorder.cpp). The shared
    // setup above (dimensions, frame period, mic mode) already ran.
    return StartRecordingMediaCodecLocked(fpsVal, outputPath, captureAudio);
#else
    RecorderFFmpeg* r = new RecorderFFmpeg();
    r->outPath = NarrowPathForFstream(outputPath);
    r->hasAudio = captureAudio;

    // ---- Output container (MP4, guessed from the extension) ----
    if (avformat_alloc_output_context2(&r->outFmt, nullptr, nullptr, r->outPath.c_str()) < 0 ||
        r->outFmt == nullptr)
    {
        delete r;
        debug.logDiagLevelMessage(LogLevel::LOG_WARNING, L"ScreenRecorder: Could not create MP4 output context.");
        return false;
    }

    // ---- Video stream: H.264 (libx264, else the native mpeg4 encoder) ----
    const AVCodec* vcodec = avcodec_find_encoder_by_name("libx264");
    if (!vcodec)
        vcodec = avcodec_find_encoder(AV_CODEC_ID_MPEG4);
    if (!vcodec)
    {
        FreeRecorderFFmpeg(r);
        debug.logDiagLevelMessage(LogLevel::LOG_WARNING, L"ScreenRecorder: No H.264 / MPEG-4 encoder available.");
        return false;
    }
    r->venc = avcodec_alloc_context3(vcodec);
    if (!r->venc)
    {
        FreeRecorderFFmpeg(r);
        return false;
    }
    r->venc->width = static_cast<int>(m_width);
    r->venc->height = static_cast<int>(m_height);
    r->venc->time_base = AVRational{ 1, static_cast<int>(fpsVal) };
    r->venc->framerate = AVRational{ static_cast<int>(fpsVal), 1 };
    r->venc->pix_fmt = AV_PIX_FMT_YUV420P;
    r->venc->bit_rate = kRecorderVideoBitrate;
    r->venc->gop_size = static_cast<int>(fpsVal);   // one keyframe per second
    r->venc->max_b_frames = 2;
    if (r->outFmt->oformat->flags & AVFMT_GLOBALHEADER)
        r->venc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    AVDictionary* vopts = nullptr;
    if (vcodec->id == AV_CODEC_ID_H264)
        av_dict_set(&vopts, "preset", "veryfast", 0);
    if (avcodec_open2(r->venc, vcodec, &vopts) < 0)
    {
        av_dict_free(&vopts);
        FreeRecorderFFmpeg(r);
        debug.logDiagLevelMessage(LogLevel::LOG_WARNING, L"ScreenRecorder: Could not open the video encoder.");
        return false;
    }
    av_dict_free(&vopts);
    r->vstream = avformat_new_stream(r->outFmt, nullptr);
    if (!r->vstream || avcodec_parameters_from_context(r->vstream->codecpar, r->venc) < 0)
    {
        FreeRecorderFFmpeg(r);
        debug.logDiagLevelMessage(LogLevel::LOG_WARNING, L"ScreenRecorder: Could not add the video stream.");
        return false;
    }
    r->vstream->time_base = r->venc->time_base;

    // ---- Audio stream: AAC (native encoder), 48 kHz stereo FLTP ----
    if (r->hasAudio)
    {
        const AVCodec* acodec = avcodec_find_encoder(AV_CODEC_ID_AAC);
        if (acodec)
        {
            r->aenc = avcodec_alloc_context3(acodec);
            if (r->aenc)
            {
                r->aenc->sample_rate = kRecorderAudioRate;
                r->aenc->ch_layout = AV_CHANNEL_LAYOUT_STEREO;
                r->aenc->sample_fmt = AV_SAMPLE_FMT_FLTP;
                r->aenc->bit_rate = static_cast<int>(audioBitrate);
                r->aenc->time_base = AVRational{ 1, kRecorderAudioRate };
                if (r->outFmt->oformat->flags & AVFMT_GLOBALHEADER)
                    r->aenc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
                if (avcodec_open2(r->aenc, acodec, nullptr) < 0)
                {
                    avcodec_free_context(&r->aenc);
                    r->aenc = nullptr;
                }
                else
                {
                    r->astream = avformat_new_stream(r->outFmt, nullptr);
                    if (!r->astream ||
                        avcodec_parameters_from_context(r->astream->codecpar, r->aenc) < 0)
                    {
                        avcodec_free_context(&r->aenc);
                        r->aenc = nullptr;
                        r->astream = nullptr;
                    }
                    else
                    {
                        r->astream->time_base = r->aenc->time_base;
                    }
                }
            }
        }
        if (!r->aenc)
        {
            r->hasAudio = false;
            debug.logDiagLevelMessage(LogLevel::LOG_WARNING,
                L"ScreenRecorder: AAC encoder unavailable; recording without game audio.");
        }
    }

    // ---- Resampler (game audio: 44100 Hz stereo s16 -> 48 kHz stereo FLTP) ----
    if (r->hasAudio)
    {
        const AVChannelLayout outLayout = AV_CHANNEL_LAYOUT_STEREO;
        const AVChannelLayout inLayout = AV_CHANNEL_LAYOUT_STEREO;
        swr_alloc_set_opts2(&r->swr,
            &outLayout, AV_SAMPLE_FMT_FLTP, kRecorderAudioRate,
            &inLayout, AV_SAMPLE_FMT_S16, Miniaudio::kSampleRate,
            0, nullptr);
        if (!r->swr || swr_init(r->swr) < 0)
        {
            if (r->swr)
                swr_free(&r->swr);
            r->hasAudio = false;
            debug.logDiagLevelMessage(LogLevel::LOG_WARNING,
                L"ScreenRecorder: Could not create the audio resampler; recording without game audio.");
        }
    }
    r->vframe = av_frame_alloc();
    r->aframe = av_frame_alloc();
    r->pkt = av_packet_alloc();
    if (!r->vframe || !r->aframe || !r->pkt)
    {
        FreeRecorderFFmpeg(r);
        return false;
    }
    r->vframe->format = AV_PIX_FMT_YUV420P;
    r->vframe->width = static_cast<int>(m_width);
    r->vframe->height = static_cast<int>(m_height);
    if (av_frame_get_buffer(r->vframe, 32) < 0)
    {
        FreeRecorderFFmpeg(r);
        return false;
    }
    if (r->hasAudio && r->aenc)
    {
        r->aframe->format = AV_SAMPLE_FMT_FLTP;
        r->aframe->ch_layout = AV_CHANNEL_LAYOUT_STEREO;
        r->aframe->sample_rate = kRecorderAudioRate;
        r->aframe->nb_samples = r->aenc->frame_size;
        if (av_frame_get_buffer(r->aframe, 0) < 0)
        {
            FreeRecorderFFmpeg(r);
            return false;
        }
    }

    // ---- Open the file and write the header ----
    if (avio_open(&r->outFmt->pb, r->outPath.c_str(), AVIO_FLAG_WRITE) < 0)
    {
        FreeRecorderFFmpeg(r);
        debug.logDiagLevelMessage(LogLevel::LOG_WARNING, L"ScreenRecorder: Could not open output file for writing.");
        return false;
    }
    if (avformat_write_header(r->outFmt, nullptr) < 0)
    {
        FreeRecorderFFmpeg(r);
        debug.logDiagLevelMessage(LogLevel::LOG_WARNING, L"ScreenRecorder: Could not write the MP4 header.");
        return false;
    }

    m_ffmpeg = r;

    // ---- Game audio tap (tracker music only; SFX have no output tap) ----
    if (r->hasAudio)
    {
        m_audioTapQueue = std::make_shared<AudioTapQueue>();
        std::shared_ptr<AudioTapQueue> queue = m_audioTapQueue;
        Miniaudio::SetTapOnAllStreams(
            [queue](const int16_t* frames, size_t frameCount)
            {
                // Audio thread: append mixed PCM to the queue. Short lock,
                // no allocation in the common case (the vector grows once).
                std::lock_guard<std::mutex> qlock(queue->mutex);
                queue->pcm.insert(queue->pcm.end(), frames, frames + frameCount * 2);
            });
    }

    m_recordingStart = std::chrono::steady_clock::now();
    m_lastVideoTime = m_recordingStart;
    m_isRecording.store(true);
    debug.logLevelMessage(LogLevel::LOG_INFO,
        L"ScreenRecorder: Started " + std::to_wstring(fpsVal) + L"fps [" +
        std::wstring(r->hasAudio ? L"game audio" : L"no audio") + L"] -> " + outputPath);
    return true;
#endif
}

void ScreenRecorder::SetCaptureEnabled(bool enabled)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_captureEnabled.load() == enabled) return;
    // Pause accounting in MF 100-ns units (matching m_framePeriod), from
    // the steady clock instead of the Windows QPC members.
    const auto now = std::chrono::steady_clock::now();
    const LONGLONG now100ns = static_cast<LONGLONG>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            now.time_since_epoch()).count() / 100);
    if (!enabled) m_pauseStart = now100ns;
    else if (m_pauseStart) {
        m_pausedTicks += now100ns - m_pauseStart;
        m_pauseStart = 0; m_lastVideoTime = now;
    }
    m_captureEnabled.store(enabled);
}

void ScreenRecorder::StopRecording()
{
    if (!m_isRecording.exchange(false)) return;
    std::lock_guard<std::mutex> lock(m_mutex);
#if !HAS_FFMPEG && !HAS_MEDIACODEC
    LogScreenRecorderUnavailableOnce();
    m_isRecording.store(false);
    return;
#elif HAS_MEDIACODEC
    // Android MediaCodec backend (android-screenrecorder.cpp).
    StopRecordingMediaCodecLocked();
#else
    // Remove the audio tap first so no new audio arrives while flushing.
    Miniaudio::SetTapOnAllStreams(nullptr);

    RecorderFFmpeg* r = static_cast<RecorderFFmpeg*>(m_ffmpeg);
    if (r)
    {
        // Drain any audio still queued, then flush the resampler and the
        // encoders so the tail of the recording is written.
        WriteAudioFramesFFmpeg();
        if (r->swr && r->hasAudio)
        {
            // Flush the resampler's internal buffer.
            const int outFrames = static_cast<int>(av_rescale_rnd(
                swr_get_delay(r->swr, Miniaudio::kSampleRate), kRecorderAudioRate,
                Miniaudio::kSampleRate, AV_ROUND_UP));
            if (outFrames > 0)
            {
                const size_t baseL = r->audioStageL.size();
                const size_t baseR = r->audioStageR.size();
                r->audioStageL.resize(baseL + static_cast<size_t>(outFrames));
                r->audioStageR.resize(baseR + static_cast<size_t>(outFrames));
                uint8_t* out[2] = {
                    reinterpret_cast<uint8_t*>(r->audioStageL.data() + baseL),
                    reinterpret_cast<uint8_t*>(r->audioStageR.data() + baseR) };
                const int converted = swr_convert(r->swr, out, outFrames, nullptr, 0);
                r->audioStageL.resize(baseL + (converted > 0 ? static_cast<size_t>(converted) : 0));
                r->audioStageR.resize(baseR + (converted > 0 ? static_cast<size_t>(converted) : 0));
            }
            // Encode everything left, padding the last partial frame.
            WriteAudioFramesFFmpeg(true);
        }
        if (r->venc)
            EncodeAndWrite(r, r->venc, r->vstream, nullptr);
        if (r->aenc && r->hasAudio)
            EncodeAndWrite(r, r->aenc, r->astream, nullptr);
        av_write_trailer(r->outFmt);
        const LONGLONG frames = m_videoFrameIndex.load();
        if (r->outFmt->pb)
            avio_closep(&r->outFmt->pb);
        // FreeRecorderFFmpeg also closes pb if still open; clear it here
        // so it does not double-close.
        r->outFmt->pb = nullptr;
        FreeRecorderFFmpeg(r);
        m_ffmpeg = nullptr;
        debug.logLevelMessage(LogLevel::LOG_INFO,
            L"ScreenRecorder: Stopped (" + std::to_wstring(frames) + L" video frames).");
    }
#endif
    // Shared tail: the Vulkan staging capture and the audio tap queue are
    // backend-independent, so both backends release them here (moved out
    // of the FFmpeg arm when the MediaCodec arm was added, 24 Nov 2026).
#if defined(__USE_VULKAN__)
    CleanupVulkanCapture();
#endif
    m_audioTapQueue.reset();
}

#if HAS_FFMPEG

// Free the FFmpeg encoder state. Caller must hold m_mutex. (Also called
// from StopRecording; the file is already closed there.)
void ScreenRecorder::CleanupFFmpegLocked()
{
    RecorderFFmpeg* r = static_cast<RecorderFFmpeg*>(m_ffmpeg);
    if (r)
    {
        FreeRecorderFFmpeg(r);
        m_ffmpeg = nullptr;
    }
}

// Encode every complete AAC frame available in the staging buffers, but
// never ahead of the current video time (audioTime < videoTime), so the
// audio stays timestamp-locked to the video clock. When flush is true
// (StopRecording) the video-time bound is ignored and the final partial
// frame is padded with silence and encoded, so the tail of the audio is
// not dropped.
// Caller must hold m_mutex.
void ScreenRecorder::WriteAudioFramesFFmpeg(bool flush)
{
    RecorderFFmpeg* r = static_cast<RecorderFFmpeg*>(m_ffmpeg);
    if (!r || !r->hasAudio || !r->aenc || !r->swr || !r->aframe)
        return;

    // 1) Drain the tap queue into the resampler.
    std::vector<int16_t> queued;
    if (m_audioTapQueue)
    {
        std::lock_guard<std::mutex> qlock(m_audioTapQueue->mutex);
        queued.swap(m_audioTapQueue->pcm);
    }
    if (!queued.empty())
    {
        const int inFrames = static_cast<int>(queued.size() / 2);
        const int outFrames = static_cast<int>(av_rescale_rnd(
            swr_get_delay(r->swr, Miniaudio::kSampleRate) + inFrames,
            kRecorderAudioRate, Miniaudio::kSampleRate, AV_ROUND_UP));
        if (outFrames > 0)
        {
            const size_t baseL = r->audioStageL.size();
            const size_t baseR = r->audioStageR.size();
            r->audioStageL.resize(baseL + static_cast<size_t>(outFrames));
            r->audioStageR.resize(baseR + static_cast<size_t>(outFrames));
            uint8_t* out[2] = {
                reinterpret_cast<uint8_t*>(r->audioStageL.data() + baseL),
                reinterpret_cast<uint8_t*>(r->audioStageR.data() + baseR) };
            const uint8_t* in[1] = { reinterpret_cast<const uint8_t*>(queued.data()) };
            const int converted = swr_convert(r->swr, out, outFrames, in, inFrames);
            r->audioStageL.resize(baseL + (converted > 0 ? static_cast<size_t>(converted) : 0));
            r->audioStageR.resize(baseR + (converted > 0 ? static_cast<size_t>(converted) : 0));
        }
    }

    // 2) Emit whole AAC frames, locked to the video clock.
    const int frameSize = r->aenc->frame_size > 0 ? r->aenc->frame_size : 1024;
    const double videoTimeSecs = static_cast<double>(m_videoFrameIndex.load()) /
        static_cast<double>(static_cast<uint32_t>(m_targetFPS));
    while (static_cast<int>(r->audioStageL.size()) >= frameSize)
    {
        const double audioTimeSecs = static_cast<double>(r->audioPts) /
            static_cast<double>(kRecorderAudioRate);
        if (!flush && audioTimeSecs >= videoTimeSecs)
            break;  // do not encode audio ahead of the video
        if (av_frame_make_writable(r->aframe) < 0)
            break;
        r->aframe->nb_samples = frameSize;
        r->aframe->pts = r->audioPts;
        std::memcpy(r->aframe->data[0], r->audioStageL.data(), static_cast<size_t>(frameSize) * sizeof(float));
        std::memcpy(r->aframe->data[1], r->audioStageR.data(), static_cast<size_t>(frameSize) * sizeof(float));
        r->audioStageL.erase(r->audioStageL.begin(), r->audioStageL.begin() + frameSize);
        r->audioStageR.erase(r->audioStageR.begin(), r->audioStageR.begin() + frameSize);
        if (!EncodeAndWrite(r, r->aenc, r->astream, r->aframe))
            break;
        r->audioPts += frameSize;
    }

    // 3) Flush: pad the final partial frame with silence and encode it,
    // so the tail of the audio is not dropped at StopRecording.
    if (flush && !r->audioStageL.empty())
    {
        r->audioStageL.resize(static_cast<size_t>(frameSize), 0.0f);
        r->audioStageR.resize(static_cast<size_t>(frameSize), 0.0f);
        if (av_frame_make_writable(r->aframe) == 0)
        {
            r->aframe->nb_samples = frameSize;
            r->aframe->pts = r->audioPts;
            std::memcpy(r->aframe->data[0], r->audioStageL.data(), static_cast<size_t>(frameSize) * sizeof(float));
            std::memcpy(r->aframe->data[1], r->audioStageR.data(), static_cast<size_t>(frameSize) * sizeof(float));
            if (EncodeAndWrite(r, r->aenc, r->astream, r->aframe))
                r->audioPts += frameSize;
        }
        r->audioStageL.clear();
        r->audioStageR.clear();
    }
}

// Encode one BGRA video frame. The input may be any size (the back-buffer
// size); it is scaled to the recording size (m_width x m_height) by the
// sws. ts100ns is the frame timestamp in MF 100-ns units relative to the
// recording start (kept for API symmetry with the Windows path -- the
// video pts itself is the frame index, so timestamps stay evenly spaced).
// Caller must hold m_mutex.
void ScreenRecorder::WriteVideoFrameFFmpeg(const uint8_t* bgra, uint32_t width, uint32_t height, uint32_t rowPitch, LONGLONG ts100ns)
{
    (void)ts100ns;
    RecorderFFmpeg* r = static_cast<RecorderFFmpeg*>(m_ffmpeg);
    if (!r || !r->venc || !r->vframe || !bgra || width == 0 || height == 0)
        return;

    // Recreate the scaler when the input size changes.
    if (!r->sws || r->swsInW != static_cast<int>(width) || r->swsInH != static_cast<int>(height))
    {
        if (r->sws)
            sws_freeContext(r->sws);
        r->sws = sws_getContext(static_cast<int>(width), static_cast<int>(height), AV_PIX_FMT_BGRA,
            static_cast<int>(m_width), static_cast<int>(m_height), AV_PIX_FMT_YUV420P,
            SWS_BILINEAR, nullptr, nullptr, nullptr);
        r->swsInW = static_cast<int>(width);
        r->swsInH = static_cast<int>(height);
        if (!r->sws)
            return;
    }

    if (av_frame_make_writable(r->vframe) < 0)
        return;
    const uint8_t* src[4] = { bgra, nullptr, nullptr, nullptr };
    int srcLinesize[4] = { static_cast<int>(rowPitch), 0, 0, 0 };
    sws_scale(r->sws, src, srcLinesize, 0, static_cast<int>(height),
        r->vframe->data, r->vframe->linesize);
    r->vframe->pts = m_videoFrameIndex.load();
    if (!EncodeAndWrite(r, r->venc, r->vstream, r->vframe))
        return;
    m_videoFrameIndex.fetch_add(1);

    // Keep the audio timestamp-locked to the video just written.
    WriteAudioFramesFFmpeg();
}

#endif // HAS_FFMPEG

// ---------------------------------------------------------------------------
// CaptureFrame (OpenGL / Linux)
// ---------------------------------------------------------------------------
#if defined(__USE_OPENGL__)
void ScreenRecorder::CaptureFrame(UINT width, UINT height)
{
    if (!m_captureEnabled.load() || !m_isRecording.load()) return;
#if !HAS_FFMPEG && !HAS_MEDIACODEC
    LogScreenRecorderUnavailableOnce();
    return;
#else
    // Throttle to target FPS with the steady clock (same logic as the
    // Windows QPC path, but in nanoseconds).
    const auto now = std::chrono::steady_clock::now();
    const auto framePeriodNs = std::chrono::nanoseconds(m_framePeriod * 100);
    {
        const auto elapsed = now - m_lastVideoTime;
        if (elapsed < framePeriodNs) return;
        m_lastVideoTime += framePeriodNs;
    }
    const LONGLONG ts100ns = static_cast<LONGLONG>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            now - m_recordingStart).count() / 100) - m_pausedTicks;

    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_isRecording.load()) return;

    // Clamp to even dimensions (required by the H.264 encoder)
    UINT w = (width  > 0 ? width  : m_width)  & ~1u;
    UINT h = (height > 0 ? height : m_height) & ~1u;
    if (w == 0 || h == 0) return;

    // Resize pixel buffer lazily
    UINT rowBytes = w * 4;
    if (w != m_glCaptureWidth || h != m_glCaptureHeight) {
        m_glPixelBuffer.resize(static_cast<size_t>(rowBytes) * h);
        m_glCaptureWidth  = w;
        m_glCaptureHeight = h;
    }

    // Read back from the default framebuffer (origin = bottom-left in OpenGL).
    // Desktop GL reads BGRA directly (the layout the encoder expects);
    // GLES has no BGRA readback, so Android reads RGBA and swaps R/B below.
#if defined(PLATFORM_LINUX)
    glReadPixels(0, 0, (GLsizei)w, (GLsizei)h, GL_BGRA_EXT, GL_UNSIGNED_BYTE, m_glPixelBuffer.data());

    // Flip rows: OpenGL origin is bottom-left; the encoder expects top-left
    std::vector<BYTE> flipped(m_glPixelBuffer.size());
    for (UINT y = 0; y < h; ++y)
        memcpy(flipped.data() + y * rowBytes,
               m_glPixelBuffer.data() + (h - 1u - y) * rowBytes,
               rowBytes);
#elif defined(PLATFORM_ANDROID)
    glReadPixels(0, 0, (GLsizei)w, (GLsizei)h, GL_RGBA, GL_UNSIGNED_BYTE, m_glPixelBuffer.data());

    // Flip rows and swap R/B (RGBA -> BGRA) in one pass.
    std::vector<BYTE> flipped(m_glPixelBuffer.size());
    for (UINT y = 0; y < h; ++y)
    {
        const BYTE* src = m_glPixelBuffer.data() + (h - 1u - y) * rowBytes;
        BYTE* dst = flipped.data() + y * rowBytes;
        for (UINT x = 0; x < w; ++x)
        {
            dst[x * 4 + 0] = src[x * 4 + 2];
            dst[x * 4 + 1] = src[x * 4 + 1];
            dst[x * 4 + 2] = src[x * 4 + 0];
            dst[x * 4 + 3] = src[x * 4 + 3];
        }
    }
#else
    // Other platforms: no GL readback wired in yet (Phase D).
    return;
#endif

#if HAS_MEDIACODEC
    WriteVideoFrameMediaCodec(flipped.data(), w, h, rowBytes, ts100ns);
#else
    WriteVideoFrameFFmpeg(flipped.data(), w, h, rowBytes, ts100ns);
#endif
#endif
}
#endif // __USE_OPENGL__

// ---------------------------------------------------------------------------
// CaptureFrame (Vulkan / Linux)
//
// Submits a one-shot command buffer that:
//   1. Transitions the swap chain image  srcLayout -> TRANSFER_SRC_OPTIMAL
//   2. Copies the image to a host-visible staging buffer
//   3. Transitions the image back to srcLayout
// Then waits for completion and writes the pixel data to the video stream.
//
// The caller must have waited for the render fence before calling this so the
// swap chain image is not still being written by the GPU.  The function holds
// queueMutex only for the VkQueueSubmit call so the render thread can proceed.
// ---------------------------------------------------------------------------
#if defined(__USE_VULKAN__)
void ScreenRecorder::CaptureFrame(VkDevice         device,
                                   VkPhysicalDevice physDevice,
                                   VkCommandPool    cmdPool,
                                   VkQueue          queue,
                                   VkImage          swapchainImage,
                                   VkImageLayout    srcLayout,
                                   VkFormat         format,
                                   uint32_t         width,
                                   uint32_t         height,
                                   std::mutex*      queueMutex)
{
    if (!m_captureEnabled.load() || !m_isRecording.load() || !device || !swapchainImage) return;
#if !HAS_FFMPEG && !HAS_MEDIACODEC
    LogScreenRecorderUnavailableOnce();
    return;
#else
    // FPS throttle -- same steady-clock logic as the OpenGL path
    const auto now = std::chrono::steady_clock::now();
    const auto framePeriodNs = std::chrono::nanoseconds(m_framePeriod * 100);
    {
        const auto elapsed = now - m_lastVideoTime;
        if (elapsed < framePeriodNs) return;
        m_lastVideoTime += framePeriodNs;
    }
    const LONGLONG ts100ns = static_cast<LONGLONG>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            now - m_recordingStart).count() / 100) - m_pausedTicks;

    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_isRecording.load()) return;

    // Create / resize the staging buffer when dimensions change
    const VkDeviceSize bufSize = static_cast<VkDeviceSize>(width) * height * 4;
    if (m_vkStagingBuffer == VK_NULL_HANDLE ||
        m_vkStagingWidth  != width          ||
        m_vkStagingHeight != height)
    {
        CleanupVulkanCapture();

        VkBufferCreateInfo bi{};
        bi.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size        = bufSize;
        bi.usage       = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(device, &bi, nullptr, &m_vkStagingBuffer) != VK_SUCCESS) return;

        VkMemoryRequirements memReq{};
        vkGetBufferMemoryRequirements(device, m_vkStagingBuffer, &memReq);

        VkPhysicalDeviceMemoryProperties memProps{};
        vkGetPhysicalDeviceMemoryProperties(physDevice, &memProps);

        uint32_t memIdx = UINT32_MAX;
        const VkMemoryPropertyFlags needed =
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
            if ((memReq.memoryTypeBits & (1u << i)) &&
                (memProps.memoryTypes[i].propertyFlags & needed) == needed) {
                memIdx = i; break;
            }
        }
        if (memIdx == UINT32_MAX) { CleanupVulkanCapture(); return; }

        VkMemoryAllocateInfo ai{};
        ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize  = memReq.size;
        ai.memoryTypeIndex = memIdx;
        if (vkAllocateMemory(device, &ai, nullptr, &m_vkStagingMemory) != VK_SUCCESS) {
            CleanupVulkanCapture(); return;
        }
        vkBindBufferMemory(device, m_vkStagingBuffer, m_vkStagingMemory, 0);
        vkMapMemory(device, m_vkStagingMemory, 0, bufSize, 0, &m_vkStagingMapped);
        m_vkStagingWidth  = width;
        m_vkStagingHeight = height;
        m_vkDevice        = device;
    }

    // Allocate a one-shot command buffer
    VkCommandBufferAllocateInfo cbai{};
    cbai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbai.commandPool        = cmdPool;
    cbai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(device, &cbai, &cmd) != VK_SUCCESS) return;

    VkCommandBufferBeginInfo cbbi{};
    cbbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    cbbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &cbbi);

    // 1. Transition: srcLayout -> TRANSFER_SRC_OPTIMAL
    VkImageMemoryBarrier toSrc{};
    toSrc.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toSrc.oldLayout           = srcLayout;
    toSrc.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    toSrc.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toSrc.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toSrc.image               = swapchainImage;
    toSrc.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    toSrc.srcAccessMask       = VK_ACCESS_MEMORY_READ_BIT;
    toSrc.dstAccessMask       = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &toSrc);

    // 2. Copy image -> staging buffer
    VkBufferImageCopy region{};
    region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.imageExtent      = { width, height, 1 };
    vkCmdCopyImageToBuffer(cmd, swapchainImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           m_vkStagingBuffer, 1, &region);

    // 3. Transition back: TRANSFER_SRC_OPTIMAL -> srcLayout
    VkImageMemoryBarrier toPresent{};
    toPresent.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toPresent.oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    toPresent.newLayout           = srcLayout;
    toPresent.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toPresent.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toPresent.image               = swapchainImage;
    toPresent.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    toPresent.srcAccessMask       = VK_ACCESS_TRANSFER_READ_BIT;
    toPresent.dstAccessMask       = VK_ACCESS_MEMORY_READ_BIT;
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        0, 0, nullptr, 0, nullptr, 1, &toPresent);

    vkEndCommandBuffer(cmd);

    // Create a local fence so we can wait for capture completion independently
    VkFenceCreateInfo fci{};
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VkFence captureFence = VK_NULL_HANDLE;
    if (vkCreateFence(device, &fci, nullptr, &captureFence) != VK_SUCCESS) {
        vkFreeCommandBuffers(device, cmdPool, 1, &cmd);
        return;
    }

    VkSubmitInfo si{};
    si.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers    = &cmd;
    // Serialise against the render thread's vkQueueSubmit via the shared queue mutex
    if (queueMutex) {
        std::lock_guard<std::mutex> qlock(*queueMutex);
        vkQueueSubmit(queue, 1, &si, captureFence);
    } else {
        vkQueueSubmit(queue, 1, &si, captureFence);
    }
    // Wait for the copy to complete (outside the queue lock so the render thread can proceed)
    vkWaitForFences(device, 1, &captureFence, VK_TRUE, UINT64_MAX);
    vkDestroyFence(device, captureFence, nullptr);
    vkFreeCommandBuffers(device, cmdPool, 1, &cmd);

    // The staging buffer is HOST_COHERENT so no explicit invalidation is needed.
    // Handle pixel format: the encoder expects BGRA byte order in memory.
    // VK_FORMAT_B8G8R8A8_* -> bytes are [B,G,R,A] -> matches directly.
    // VK_FORMAT_R8G8B8A8_* -> bytes are [R,G,B,A] -> swap R and B.
    const bool needByteSwap = (format == VK_FORMAT_R8G8B8A8_UNORM  ||
                                format == VK_FORMAT_R8G8B8A8_SRGB   ||
                                format == VK_FORMAT_R8G8B8A8_SNORM  ||
                                format == VK_FORMAT_A8B8G8R8_UNORM_PACK32);
    if (needByteSwap) {
        uint8_t* px = static_cast<uint8_t*>(m_vkStagingMapped);
        const size_t pixCount = static_cast<size_t>(width) * height;
        for (size_t i = 0; i < pixCount; ++i)
            std::swap(px[i * 4 + 0], px[i * 4 + 2]);
    }
#if HAS_MEDIACODEC
    WriteVideoFrameMediaCodec(static_cast<const uint8_t*>(m_vkStagingMapped), width, height, width * 4, ts100ns);
#else
    WriteVideoFrameFFmpeg(static_cast<const uint8_t*>(m_vkStagingMapped), width, height, width * 4, ts100ns);
#endif
#endif
}

void ScreenRecorder::CleanupVulkanCapture()
{
    VkDevice dev = m_vkDevice;
    if (!dev) return;
    if (m_vkStagingMapped) {
        vkUnmapMemory(dev, m_vkStagingMemory);
        m_vkStagingMapped = nullptr;
    }
    if (m_vkStagingBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(dev, m_vkStagingBuffer, nullptr);
        m_vkStagingBuffer = VK_NULL_HANDLE;
    }
    if (m_vkStagingMemory != VK_NULL_HANDLE) {
        vkFreeMemory(dev, m_vkStagingMemory, nullptr);
        m_vkStagingMemory = VK_NULL_HANDLE;
    }
    m_vkStagingWidth  = 0;
    m_vkStagingHeight = 0;
    m_vkDevice        = VK_NULL_HANDLE;
}
#endif // __USE_VULKAN__

#endif // !PLATFORM_WINDOWS
