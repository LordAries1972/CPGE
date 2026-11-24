// ScreenRecorder.h
// ================
// Real-time screen + audio capture to MP4 (H.264 video, AAC audio).
//
// MicMode::Mixed    — AudioCaptureThread polls both loopback and mic inline.
//                     Mic is blended into game audio before encoding. One track.
// MicMode::Separate — dedicated mic thread writes a second AAC stream.
//                     Two tracks in the MP4 for post-production mixing.
//
// Usage:
//   StartRecording(w, h, RecordFPS::FPS_60, path);
//   StartRecording(w, h, RecordFPS::FPS_60, path, MicMode::Mixed);
//   StartRecording(w, h, RecordFPS::FPS_60, path, MicMode::Separate, AudioBitrate::Kbps_256);
//   CaptureFrame(device, context, swapChain);  // once per frame, before Present
//   StopRecording();
//
// All audio streams (game + mic) are timestamp-locked to the video FPS clock.
// AudioBitrate applies to every AAC stream written to the MP4.
// ------------------------------------------------------------------------------------
#pragma once

#include "Includes.h"
#if defined(__USE_DIRECTX_12__)
    #include <d3d12.h>
    #include <dxgi1_4.h>
#endif
#if defined(PLATFORM_WINDOWS)
    #include <mfapi.h>
    #include <mfidl.h>
    #include <mfreadwrite.h>
    #include <mmdeviceapi.h>
    #include <audioclient.h>
#endif
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <thread>
#include <string>
#include <vector>

#if defined(PLATFORM_WINDOWS)
    #pragma comment(lib, "mf.lib")
    #pragma comment(lib, "mfplat.lib")
    #pragma comment(lib, "mfreadwrite.lib")
    #pragma comment(lib, "mfuuid.lib")
    #pragma comment(lib, "ole32.lib")
#endif

enum class RecordFPS : std::uint32_t
{
    FPS_15  = 15,
    FPS_30  = 30,
    FPS_60  = 60,
    FPS_90  = 90,
    FPS_120 = 120
};


// AAC audio bitrate — applied to every audio stream (game + mic).
// All streams are timestamp-locked to the video FPS clock regardless of bitrate.
enum class AudioBitrate : std::uint32_t
{
    Kbps_64  =  64'000,
    Kbps_96  =  96'000,
    Kbps_128 = 128'000,
    Kbps_192 = 192'000,   // default
    Kbps_256 = 256'000,
    Kbps_320 = 320'000
};

enum class MicMode : int
{
    Off      = 0,   // no microphone
    Mixed    = 1,   // mic blended into game audio track (single track)
    Separate = 2    // mic as separate AAC track (two tracks, use editor to balance)
};

class ScreenRecorder
{
public:
    ScreenRecorder();
    ~ScreenRecorder();

    bool StartRecording(UINT width, UINT height, RecordFPS fps,
                        const std::wstring& outputPath,
                        MicMode     micMode     = MicMode::Off,
                        AudioBitrate audioBitrate = AudioBitrate::Kbps_192,
                        bool captureAudio = true);
    // Gameplay archives omit menus/pauses without changing the playback rate.
    void SetCaptureEnabled(bool enabled);

    void StopRecording();

#if defined(__USE_DIRECTX_12__)
    // Capture the DX12 back-buffer into the MF video stream.
    // Call this AFTER ExecuteCommandLists (GPU render done) and BEFORE Present.
    // device / queue / swapChain — active DX12 objects from the renderer.
    // frameIndex — current swap-chain back-buffer index.
    // queueMutex — serialises command submission from multiple threads.
    void CaptureFrame(ID3D12Device*       device,
                      ID3D12CommandQueue* queue,
                      IDXGISwapChain4*    swapChain,
                      UINT                frameIndex,
                      std::mutex*         queueMutex = nullptr);
#elif defined(__USE_DIRECTX_11__)
    void CaptureFrame(ID3D11Device*        device,
                      ID3D11DeviceContext* context,
                      IDXGISwapChain1*     swapChain);
#elif defined(__USE_OPENGL__)
    // Capture the current OpenGL back-buffer via glReadPixels.
    // Call BEFORE SwapBuffers so the fully-composed frame is read.
    // (Windows MF path and Linux FFmpeg path share this overload.)
    void CaptureFrame(UINT width, UINT height);
#elif defined(__USE_VULKAN__)
    // Capture the presented swap chain image into the video stream.
    // Call this AFTER vkWaitForFences (GPU render done) and BEFORE vkQueuePresentKHR.
    // srcLayout  — image layout after the render pass (typically VK_IMAGE_LAYOUT_PRESENT_SRC_KHR).
    // queueMutex — renderer's queue mutex to serialise one-shot CB submissions.
    // (Windows MF path and Linux FFmpeg path share this overload.)
    void CaptureFrame(VkDevice         device,
                      VkPhysicalDevice physDevice,
                      VkCommandPool    cmdPool,
                      VkQueue          queue,
                      VkImage          swapchainImage,
                      VkImageLayout    srcLayout,
                      VkFormat         format,
                      uint32_t         width,
                      uint32_t         height,
                      std::mutex*      queueMutex = nullptr);
#endif

    bool      IsRecording()  const { return m_isRecording.load(); }
    LONGLONG VideoFrames() const { return m_videoFrameIndex.load(); }
    RecordFPS GetTargetFPS() const { return m_targetFPS; }

    // Mic gain controls — safe to call while recording.
    // Monitor gain: how loud you hear yourself (0.0 = silent, 1.0 = unity, 2.0 = double).
    // Record  gain: how loud the mic is in the recording when not monitoring
    //               (monitoring active = loopback captures mic, this has no effect).
    void  SetMicMonitorGain(float g) { m_micMonitorGain = g < 0.f ? 0.f : (g > 20.f ? 20.f : g); }
    float GetMicMonitorGain() const  { return m_micMonitorGain; }
    void  SetMicRecordGain (float g) { m_micRecordGain  = g < 0.f ? 0.f : (g > 20.f ? 20.f : g); }
    float GetMicRecordGain () const  { return m_micRecordGain;  }

private:
#if defined(PLATFORM_WINDOWS)
    void WriteVideoFrame(const BYTE* pData, UINT rowPitch, LONGLONG ts);

    bool InitAudioCapture();
    void AudioCaptureThread();   // handles loopback + inline mic poll (Mixed mode)
    void CleanupAudio();

    bool InitMicCapture();
    void MicCaptureThread();     // Separate mode only
    void CleanupMic();

    bool InitMonitor();
    void WriteMonitorSamples(const BYTE* pMicData, UINT32 micFrames);
    void CleanupMonitor();

    // Inline mic mix — called from AudioCaptureThread only (no mutex needed)
    void MixMicInline(BYTE* pDst, UINT32 gameFrames);

    // ---- MF Sink Writer ----
    IMFSinkWriter*          m_pSinkWriter;
    DWORD                   m_videoStreamIndex;
    DWORD                   m_audioStreamIndex;

    // ---- Renderer-specific capture resources ----
#if defined(__USE_DIRECTX_12__)
    // DX12 readback buffer — CPU-readable buffer used to stage the back-buffer pixels.
    // Lazily created the first time CaptureFrame is called and reused thereafter.
    ComPtr<ID3D12Resource>  m_dx12ReadbackBuffer;
    UINT64                  m_dx12ReadbackRowPitch = 0;   // 256-byte-aligned row pitch
    UINT                    m_dx12ReadbackWidth    = 0;   // Back-buffer width at creation
    UINT                    m_dx12ReadbackHeight   = 0;   // Back-buffer height at creation
    void CleanupDX12Capture();
#elif defined(__USE_DIRECTX_11__)
    ComPtr<ID3D11Texture2D> m_stagingTexture;
#elif defined(__USE_OPENGL__) && defined(PLATFORM_WINDOWS)
    // glReadPixels pixel buffer — resized lazily inside CaptureFrame
    std::vector<BYTE> m_glPixelBuffer;
    UINT              m_glCaptureWidth  = 0;
    UINT              m_glCaptureHeight = 0;
#elif defined(__USE_VULKAN__) && defined(PLATFORM_WINDOWS)
    VkBuffer        m_vkStagingBuffer  = VK_NULL_HANDLE;
    VkDeviceMemory  m_vkStagingMemory  = VK_NULL_HANDLE;
    void*           m_vkStagingMapped  = nullptr;
    uint32_t        m_vkStagingWidth   = 0;
    uint32_t        m_vkStagingHeight  = 0;
    VkDevice        m_vkDevice         = VK_NULL_HANDLE;
    void CleanupVulkanCapture();
#endif

    // ---- capture dimensions (even-rounded, set once in StartRecording) ----
    UINT                    m_width;
    UINT                    m_height;

    // ---- frame-rate / timestamp state ----
    RecordFPS               m_targetFPS;
    AudioBitrate            m_audioBitrate;
    LONGLONG                m_framePeriod;
    std::atomic<LONGLONG>   m_videoFrameIndex;
    LONGLONG                m_framePeriodSnapshot;

    LARGE_INTEGER           m_qpcRecordingStart;
    LARGE_INTEGER           m_qpcFreq;
    LARGE_INTEGER           m_lastVideoQpc;    // QPC value of the last written video frame; used to throttle CaptureFrame to target FPS

    // ---- game audio ----
    LONGLONG                m_audioTimestamp;
    IAudioClient*           m_pAudioClient;
    IAudioCaptureClient*    m_pCaptureClient;
    WAVEFORMATEX*           m_pWaveFormat;
    std::thread             m_audioThread;

    // ---- microphone ----
    MicMode                 m_micMode;
    IAudioClient*           m_pMicAudioClient;
    IAudioCaptureClient*    m_pMicCaptureClient;
    WAVEFORMATEX*           m_pMicWaveFormat;
    std::thread             m_micThread;           // Separate mode only

    // Mixed mode: mic accumulator owned exclusively by AudioCaptureThread — no mutex
    std::vector<BYTE>       m_micAccum;
    size_t                  m_micAccumRead;        // read offset — avoids O(n) erase
    bool                    m_micDataLogged;       // one-shot "first data flowing" diagnostic

    // Mic level diagnostics — peak amplitude of the raw mic signal (pre-gain),
    // accumulated in MixMicInline and logged every ~5 seconds. Distinguishes
    // "mic captures voice" from "endpoint delivers noise floor / silence only".
    float                   m_micDiagPeak;
    UINT64                  m_micDiagFrames;

    // Separate mode: mic AAC stream state
    DWORD                   m_micStreamIndex;
    LONGLONG                m_micAudioTimestamp;

    // ---- mic monitoring (render mic to speakers in real-time) ----
    IAudioClient*           m_pMonitorAudioClient;
    IAudioRenderClient*     m_pMonitorRenderClient;
    WAVEFORMATEX*           m_pMonitorFormat;
    UINT32                  m_monitorBufferFrames;
    bool                    m_monitorOnLoopbackDevice; // true when monitor and eConsole loopback share the same physical endpoint
    std::atomic<float>      m_micMonitorGain;     // monitor output volume
    std::atomic<float>      m_micRecordGain;      // blend volume applied only when m_monitorOnLoopbackDevice == false

    // ---- state ----
    std::atomic<bool>       m_isRecording;
    std::atomic<bool>       m_captureEnabled { true };
    LONGLONG m_pauseStart = 0, m_pausedTicks = 0;
    std::mutex              m_mutex;
#else
    // Linux / Android state (Phase B): FFmpeg replaces Media Foundation /
    // WASAPI. Only the state the inline accessors and the FFmpeg .cpp need
    // is kept, mirroring the Windows block above.
    UINT                    m_width;
    UINT                    m_height;
    RecordFPS               m_targetFPS;
    AudioBitrate            m_audioBitrate;
    LONGLONG                m_framePeriod;
    std::atomic<LONGLONG>   m_videoFrameIndex;
    LONGLONG                m_framePeriodSnapshot;
    LONGLONG                m_audioTimestamp;
    std::thread             m_audioThread;
    MicMode                 m_micMode;
    std::vector<BYTE>       m_micAccum;
    size_t                  m_micAccumRead;
    bool                    m_micDataLogged;
    float                   m_micDiagPeak;
    std::uint64_t           m_micDiagFrames;
    std::thread             m_micThread;
    DWORD                   m_micStreamIndex;
    LONGLONG                m_micAudioTimestamp;
    std::atomic<float>      m_micMonitorGain;
    std::atomic<float>      m_micRecordGain;
    std::atomic<bool>       m_isRecording;
    std::atomic<bool>       m_captureEnabled { true };
    LONGLONG m_pauseStart = 0, m_pausedTicks = 0;
    std::mutex              m_mutex;

    // FFmpeg encoder state. Opaque void* so the FFmpeg headers stay out of
    // this header -- the real struct (RecorderFFmpeg) lives in
    // ScreenRecorder.cpp and is only allocated when HAS_FFMPEG is true.
    // nullptr when FFmpeg is unavailable or not recording.
    void* m_ffmpeg = nullptr;

    // Renderer capture resources (Linux/Android, per renderer).
#if defined(__USE_OPENGL__)
    // glReadPixels pixel buffer -- resized lazily inside CaptureFrame
    std::vector<BYTE> m_glPixelBuffer;
    UINT              m_glCaptureWidth  = 0;
    UINT              m_glCaptureHeight = 0;
#endif
#if defined(__USE_VULKAN__)
    VkBuffer        m_vkStagingBuffer  = VK_NULL_HANDLE;
    VkDeviceMemory  m_vkStagingMemory  = VK_NULL_HANDLE;
    void*           m_vkStagingMapped  = nullptr;
    uint32_t        m_vkStagingWidth   = 0;
    uint32_t        m_vkStagingHeight  = 0;
    VkDevice        m_vkDevice         = VK_NULL_HANDLE;
    void CleanupVulkanCapture();
#endif

    // Wall-clock recording clock (std::chrono replaces the Windows QPC
    // members, which do not exist on Linux). m_lastVideoTime throttles
    // CaptureFrame to the target FPS; m_recordingStart anchors timestamps.
    std::chrono::steady_clock::time_point m_recordingStart;
    std::chrono::steady_clock::time_point m_lastVideoTime;

    // Game-audio tap queue: the miniaudio tap (audio thread) appends mixed
    // music PCM (44100 Hz stereo s16) here; the render thread drains it
    // into the AAC encoder. Held by shared_ptr so a tap invocation already
    // in flight cannot dangle if the recorder is destroyed.
    struct AudioTapQueue { std::mutex mutex; std::vector<int16_t> pcm; };
    std::shared_ptr<AudioTapQueue> m_audioTapQueue;

    // FFmpeg helpers (ScreenRecorder.cpp, Linux/Android only). The caller
    // must hold m_mutex. ts100ns is a timestamp in MF 100-ns units
    // relative to the recording start, matching the Windows path.
    void WriteVideoFrameFFmpeg(const uint8_t* bgra, uint32_t width, uint32_t height, uint32_t rowPitch, LONGLONG ts100ns);
    void WriteAudioFramesFFmpeg(bool flush = false);
    void CleanupFFmpegLocked();
#endif
};

// Independent of the user's manual screen recording session.
extern ScreenRecorder gameplayRecorder;
