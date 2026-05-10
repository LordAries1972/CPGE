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
#include <shellapi.h>
#include <mmreg.h>

#pragma comment(lib, "shell32.lib")

extern Debug debug;

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
    , m_micStreamIndex(0), m_micAudioTimestamp(0)
    , m_pMonitorAudioClient(nullptr), m_pMonitorRenderClient(nullptr)
    , m_pMonitorFormat(nullptr), m_monitorBufferFrames(0)
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
                                    AudioBitrate audioBitrate)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_isRecording.load()) return false;

    UINT32 fpsVal = static_cast<UINT32>(fps);
    if (fpsVal != 30 && fpsVal != 60 && fpsVal != 120)
    {
        debug.logLevelMessage(LogLevel::LOG_WARNING,
            L"ScreenRecorder: Unsupported fps " + std::to_wstring(fpsVal) + L" – defaulting to 60");
        fpsVal = 60; fps = RecordFPS::FPS_60;
    }

    m_width               = width  & ~1u;   // NVENC NV12 requires even dimensions
    m_height              = height & ~1u;
    m_targetFPS           = fps;
    m_framePeriod         = 10'000'000LL / static_cast<LONGLONG>(fpsVal);
    m_framePeriodSnapshot = m_framePeriod;
    m_videoFrameIndex.store(0);
    m_audioTimestamp      = 0;
    m_micMode             = micMode;
    m_audioBitrate        = audioBitrate;
    m_micAudioTimestamp   = 0;
    m_micAccum.clear();
    m_micAccumRead        = 0;
    m_micDataLogged       = false;

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
        debug.logLevelMessage(LogLevel::LOG_ERROR,
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
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"ScreenRecorder: Failed to add H.264 stream (" + HRStr(hr) + L")");
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
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"ScreenRecorder: Failed to set ARGB32 input (" + HRStr(hr) + L")");
            m_pSinkWriter->Release(); m_pSinkWriter = nullptr; return false;
        }
    }

    // 4. Game audio
    bool hasAudio = InitAudioCapture();
    if (!hasAudio)
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"ScreenRecorder: Game audio unavailable");

    // 5. Mic
    bool hasMic = false;
    if (micMode != MicMode::Off)
    {
        hasMic = InitMicCapture();
        if (!hasMic)
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"ScreenRecorder: Mic unavailable");
    }

    // 6. Mic monitoring — render mic audio to speakers in real-time
    if (hasMic && !InitMonitor())
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"ScreenRecorder: Mic monitoring unavailable");

    // 7. Begin writing
    hr = m_pSinkWriter->BeginWriting();
    if (FAILED(hr))
    {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"ScreenRecorder: BeginWriting failed (" + HRStr(hr) + L")");
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
        // Mixed mode: mic is polled inline inside AudioCaptureThread — start mic client here
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

    std::wstring micDesc = !hasMic ? L"" :
        (micMode == MicMode::Mixed ? L" + mic(mixed)" : L" + mic(separate track)");
    debug.logLevelMessage(LogLevel::LOG_INFO,
        L"ScreenRecorder: Started " + std::to_wstring(fpsVal) + L"fps [" +
        std::wstring(hasAudio ? L"game audio" : L"no audio") + micDesc + L"] -> " + outputPath);
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
    m_stagingTexture.Reset();
    m_width = m_height = 0;
    m_videoFrameIndex.store(-1);
    m_qpcRecordingStart.QuadPart = 0;

    debug.logLevelMessage(LogLevel::LOG_INFO, L"ScreenRecorder: Stopped");
}

// ---------------------------------------------------------------------------
// CaptureFrame
// ---------------------------------------------------------------------------
void ScreenRecorder::CaptureFrame(ID3D11Device*        device,
                                   ID3D11DeviceContext* context,
                                   IDXGISwapChain1*     swapChain)
{
    if (!m_isRecording.load() || !device || !context || !swapChain) return;
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

    // Skip if the back buffer is smaller than the encoder's frame — can't crop upward.
    if (bbDesc.Width < m_width || bbDesc.Height < m_height)
    {
        debug.logLevelMessage(LogLevel::LOG_WARNING,
            L"ScreenRecorder: back buffer smaller than recording dimensions — frame skipped.");
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
            debug.logLevelMessage(LogLevel::LOG_ERROR,
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

// ---------------------------------------------------------------------------
// WriteVideoFrame
// ---------------------------------------------------------------------------
void ScreenRecorder::WriteVideoFrame(const BYTE* pData, UINT rowPitch, LONGLONG ts)
{
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
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"ScreenRecorder: Video frame dropped");
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
// Called from AudioCaptureThread only — no mutex needed.
//
// Sync note: when the monitor render client is active, mic audio is already
// flowing into the loopback capture (the render endpoint mixes monitor audio
// in, and the loopback captures it). Blending mic here a second time would
// double it in the recording and corrupt the encoder's timing. Skip the blend
// when monitoring is active — the loopback path handles it naturally.
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
        debug.logLevelMessage(LogLevel::LOG_INFO,
            L"ScreenRecorder Mic: First audio data received — " +
            std::to_wstring(mixFrames) + L" frames");
    }

    const BYTE* pMicBase = m_micAccum.data() + m_micAccumRead;

    // Always push to monitor so the user hears themselves
    WriteMonitorSamples(pMicBase, mixFrames);

    // Always blend mic directly into the game audio buffer for recording.
    // The monitor uses the communications endpoint (headset) so it does not
    // contaminate the eConsole loopback; if both endpoints map to the same
    // device the mic will be slightly elevated in the recording, which
    // m_micRecordGain can compensate for.
    {
        const float recGain = m_micRecordGain.load();

        for (UINT32 f = 0; f < mixFrames; ++f)
        {
            float micSample;
            if (micIsFloat)
                micSample = reinterpret_cast<const float*>(pMicBase)[f * micChannels] * recGain;
            else
                micSample = reinterpret_cast<const int16_t*>(pMicBase)[f * micChannels] / 32768.0f * recGain;

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
// AudioCaptureThread  — loopback + inline mic poll (Mixed mode)
// ---------------------------------------------------------------------------
void ScreenRecorder::AudioCaptureThread()
{
    while (m_isRecording.load())
    {
        Sleep(10);

        UINT32 packetFrames = 0;
        if (FAILED(m_pCaptureClient->GetNextPacketSize(&packetFrames))) break;

        while (packetFrames > 0)
        {
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

                    // Mixed mode: blend mic inline — same thread, no locks needed
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
    }

    if (m_pAudioClient) m_pAudioClient->Stop();
    if (m_micMode == MicMode::Mixed && m_pMicAudioClient) m_pMicAudioClient->Stop();
}

// ---------------------------------------------------------------------------
// InitMonitor — opens the default render endpoint to feed mic audio back to
// speakers so the user can hear themselves before the mix hits the recording.
// ---------------------------------------------------------------------------
bool ScreenRecorder::InitMonitor()
{
    IMMDeviceEnumerator* pEnum = nullptr;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&pEnum));
    if (FAILED(hr)) return false;

    // Prefer the communications render endpoint (typically a headset).
    // This keeps monitor audio off the eConsole loopback capture path,
    // preventing the mic from being double-mixed into the recording.
    // Falls back to eConsole if no separate communications device exists.
    IMMDevice* pDevice = nullptr;
    hr = pEnum->GetDefaultAudioEndpoint(eRender, eCommunications, &pDevice);
    if (FAILED(hr))
        hr = pEnum->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice);
    pEnum->Release();
    if (FAILED(hr)) return false;

    hr = pDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                           reinterpret_cast<void**>(&m_pMonitorAudioClient));
    pDevice->Release();
    if (FAILED(hr)) return false;

    if (FAILED(m_pMonitorAudioClient->GetMixFormat(&m_pMonitorFormat))) return false;

    if (m_pMonitorFormat->nSamplesPerSec != m_pMicWaveFormat->nSamplesPerSec)
    {
        debug.logLevelMessage(LogLevel::LOG_WARNING,
            L"ScreenRecorder Monitor: Sample rate mismatch (mic=" +
            std::to_wstring(m_pMicWaveFormat->nSamplesPerSec) +
            L" render=" + std::to_wstring(m_pMonitorFormat->nSamplesPerSec) +
            L") — monitoring disabled");
        CleanupMonitor();
        return false;
    }

    hr = m_pMonitorAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, 0,
                                            30 * 10000, 0, m_pMonitorFormat, nullptr);
    if (FAILED(hr)) { CleanupMonitor(); return false; }

    if (FAILED(m_pMonitorAudioClient->GetBufferSize(&m_monitorBufferFrames))) { CleanupMonitor(); return false; }

    if (FAILED(m_pMonitorAudioClient->GetService(IID_PPV_ARGS(&m_pMonitorRenderClient)))) { CleanupMonitor(); return false; }

    m_pMonitorAudioClient->Start();
    debug.logLevelMessage(LogLevel::LOG_INFO,
        L"ScreenRecorder Monitor: Mic monitoring active -> " +
        std::to_wstring(m_pMonitorFormat->nSamplesPerSec) + L"Hz " +
        std::to_wstring(m_pMonitorFormat->nChannels) + L"ch");
    return true;
}

// ---------------------------------------------------------------------------
// WriteMonitorSamples — pushes raw mic PCM to the render client so the user
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
        // Unsupported format combination — write silence rather than noise
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
// InitMicCapture
// ---------------------------------------------------------------------------
bool ScreenRecorder::InitMicCapture()
{
    HRESULT hr;
    IMMDeviceEnumerator* pEnum = nullptr;
    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&pEnum));
    if (FAILED(hr))
    {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"ScreenRecorder Mic: MMDeviceEnumerator failed (" + HRStr(hr) + L")");
        return false;
    }

    IMMDevice* pDevice = nullptr;
    hr = pEnum->GetDefaultAudioEndpoint(eCapture, eConsole, &pDevice);
    pEnum->Release();
    if (FAILED(hr))
    {
        if (hr == E_ACCESSDENIED || hr == HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED))
        {
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"ScreenRecorder Mic: Access denied – opening privacy settings");
            ShellExecuteW(nullptr, L"open", L"ms-settings:privacy-microphone", nullptr, nullptr, SW_SHOW);
        }
        else
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"ScreenRecorder Mic: No capture endpoint (" + HRStr(hr) + L")");
        return false;
    }

    hr = pDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                           reinterpret_cast<void**>(&m_pMicAudioClient));
    pDevice->Release();
    if (FAILED(hr)) { debug.logLevelMessage(LogLevel::LOG_ERROR, L"ScreenRecorder Mic: Activate failed (" + HRStr(hr) + L")"); return false; }

    if (FAILED(m_pMicAudioClient->GetMixFormat(&m_pMicWaveFormat)))
    { debug.logLevelMessage(LogLevel::LOG_ERROR, L"ScreenRecorder Mic: GetMixFormat failed"); return false; }

    hr = m_pMicAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, 0,
                                        200 * 10000, 0, m_pMicWaveFormat, nullptr);
    if (FAILED(hr)) { debug.logLevelMessage(LogLevel::LOG_ERROR, L"ScreenRecorder Mic: Initialize failed (" + HRStr(hr) + L")"); return false; }

    if (FAILED(m_pMicAudioClient->GetService(IID_PPV_ARGS(&m_pMicCaptureClient))))
    { debug.logLevelMessage(LogLevel::LOG_ERROR, L"ScreenRecorder Mic: GetService failed"); return false; }

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
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"ScreenRecorder Mic: Failed to add separate stream (" + HRStr(hr) + L")");
            return false;
        }
    }

    const bool micFloat = IsIEEEFloat(m_pMicWaveFormat);
    debug.logLevelMessage(LogLevel::LOG_INFO,
        std::wstring(m_micMode == MicMode::Mixed
            ? L"ScreenRecorder Mic: Ready (mixed inline) — "
            : L"ScreenRecorder Mic: Ready (separate track) — ") +
        std::to_wstring(m_pMicWaveFormat->nSamplesPerSec) + L"Hz " +
        std::to_wstring(m_pMicWaveFormat->nChannels) + L"ch " +
        (micFloat ? L"float32" : L"int16"));
    return true;
}

// ---------------------------------------------------------------------------
// MicCaptureThread  — Separate mode only
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
