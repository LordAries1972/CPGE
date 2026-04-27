#pragma once

#include "Includes.h"
#include "Renderer.h"
#include "Debug.h"
#include "Vectors.h"
#include "Color.h"
#include "ThreadManager.h"
#include "ThreadLockHelper.h"  // Added the ThreadLockHelper include

// Windows & DirectShow related includes
#include <dshow.h>
#include <evr.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <d3d11.h>
#include <xaudio2.h>

// Link required libraries
#pragma comment(lib, "strmiids.lib")
#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "dxva2.lib")
#pragma comment(lib, "evr.lib")
#pragma comment(lib, "xaudio2.lib")

// Forward declarations
class Renderer;
class Debug;
class ThreadManager;

class MoviePlayer {
public:
    MoviePlayer();
    ~MoviePlayer();

    // Initialize the movie player.
    // fps       – target playback frame rate; used to gate how far audio reads ahead of video.
    // enableAudio – pass false to suppress audio entirely.
    bool Initialize(std::shared_ptr<Renderer> rendererPtr, ThreadManager* threadManager,
                    float fps = 30.0f, bool enableAudio = true);

    // Open a movie file for playback
    bool OpenMovie(const std::wstring& filePath);

    // Play, pause, stop control
    bool Play();
    bool Pause();
    bool Stop();

    // Seek to a position in the movie (in seconds)
    bool SeekTo(double timeInSeconds);

    // Get movie information
    double GetDuration();                                                                   // Returns duration in seconds
    double GetCurrentPosition();                                                            // Returns current position in seconds
    Vector2 GetVideoDimensions();                                                           // Returns video dimensions

    // Is the movie currently playing?
    bool IsPlaying() const;
    bool IsPaused() const;

    // NEW: Direct update method called from renderer each frame
    bool UpdateFrame();

    // Render the current video frame (called from the rendering thread)
    void Render(const Vector2& position, const Vector2& size);

    // Clean up resources
    void Cleanup();

private:
    // XAudio2 voice callback — frees the per-buffer BYTE* copy on completion
    struct AudioVoiceCallback : public IXAudio2VoiceCallback {
        void STDMETHODCALLTYPE OnBufferEnd(void* pContext) override { delete[] static_cast<BYTE*>(pContext); }
        void STDMETHODCALLTYPE OnVoiceProcessingPassStart(UINT32) override {}
        void STDMETHODCALLTYPE OnVoiceProcessingPassEnd() override {}
        void STDMETHODCALLTYPE OnStreamEnd() override {}
        void STDMETHODCALLTYPE OnBufferStart(void*) override {}
        void STDMETHODCALLTYPE OnLoopEnd(void*) override {}
        void STDMETHODCALLTYPE OnVoiceError(void*, HRESULT) override {}
    };

    // Initialize Media Foundation
    bool InitializeMF();

    // Detect video codec from the file
    bool DetectVideoCodec(const std::wstring& filePath, std::wstring& codecName);

    // Create Direct3D 11 texture for video rendering
    bool CreateVideoTexture(UINT width, UINT height, bool isHevcContent = false);

    // Update texture with current video frame
    bool UpdateVideoTexture();

    // Generate a placeholder frame when actual video frames cannot be decoded
    void GeneratePlaceholderFrame();

    // Process a video sample
    void ProcessVideoSample(IMFSample* pSample);

    // Convert DirectShow/MF time format to seconds
    double ConvertMFTimeToSeconds(LONGLONG mfTime);

    // Convert seconds to DirectShow/MF time format
    LONGLONG ConvertSecondsToMFTime(double seconds);

    // Helper for error reporting
    void LogMediaError(HRESULT hr, const wchar_t* operation);

    // NEW: Read the next video sample
    bool ReadNextSample();

    // Audio helpers
    bool InitializeAudio();
    void ConfigureAudioStream();
    bool ReadAudioSample();
    void CleanupAudio();

private:
    // Media Foundation objects
    IMFSourceReader* m_pSourceReader;
    IMFSample* m_pCurrentSample;
    IMFMediaType* m_pVideoMediaType;

    // Renderer reference
    std::shared_ptr<Renderer> m_renderer;

    // Thread Manager reference
    ThreadManager* m_threadManager;

    // DirectX texture for video frames
    ComPtr<ID3D11Texture2D> m_videoTexture;
    ComPtr<ID3D11Texture2D> m_videoRenderTexture;                                           // Additional texture for dual-texture system
    ComPtr<ID3D11ShaderResourceView> m_videoTextureView;
    int m_videoTextureIndex;                                                                // Index in the renderer's texture array

    // Video information
    UINT m_videoWidth;
    UINT m_videoHeight;
    LONGLONG m_videoDuration;
    GUID m_videoSubtype;

    // Playback states
    std::atomic<bool> m_isInitialized;
    std::atomic<bool> m_isPlaying;
    std::atomic<bool> m_isPaused;
    std::atomic<bool> m_hasVideo;
    std::atomic<bool> m_hasAudio;

    // Current playback position in 100-nanosecond units (Media Foundation time format)
    std::atomic<LONGLONG> m_llCurrentPosition;

    // Thread synchronization
    std::recursive_mutex m_mutex;

    // Playback wall-clock origin and corresponding media-time origin for A/V sync.
    // m_playStartWallTime is reset on Play() and recalibrated on resume-from-pause.
    // m_playStartMediaTime is the PTS of the first video frame (-1 = not yet known).
    std::chrono::steady_clock::time_point m_playStartWallTime;
    LONGLONG m_playStartMediaTime;

    // Timestamp of the last presented frame (for stats/debug).
    std::chrono::steady_clock::time_point m_lastFrameTime;

    // NEW: Frame ready flag to indicate if we have a new frame to render
    std::atomic<bool> m_hasNewFrame;

    // Audio playback
    bool m_enableAudio;
    float m_targetFPS;
    LONGLONG m_audioReadPosition; // MF timestamp of the last audio sample submitted to XAudio2
    UINT64 m_audioStartSamples;   // XAudio2 SamplesPlayed value captured at Play() start; used for audio-master A/V sync
    IXAudio2* m_pXAudio2;
    IXAudio2MasteringVoice* m_pMasterVoice;
    IXAudio2SourceVoice* m_pSourceVoice;
    WAVEFORMATEX m_audioFormat;
    AudioVoiceCallback m_voiceCallback;
    static constexpr UINT32 AUDIO_MAX_QUEUED_BUFFERS = 3;

    // Predefined lock names for consistency
    static constexpr const char* MOVIE_MUTEX_LOCK = "movie_mutex";
    static constexpr const char* MOVIE_UPDATE_LOCK = "movie_update_lock";
    static constexpr const char* MOVIE_CONTROL_LOCK = "movie_control_lock";
    static constexpr const char* RENDERER_FRAME_LOCK = "renderer_frame_lock";
    static constexpr const char* D2D_DRAW_LOCK = "d2d_draw_lock";
};