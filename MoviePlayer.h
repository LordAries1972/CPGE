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

// Link required libraries
#pragma comment(lib, "strmiids.lib")
#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "dxva2.lib")
#pragma comment(lib, "evr.lib")

// Forward declarations
class Renderer;
class Debug;
class ThreadManager;

class MoviePlayer {
public:
    MoviePlayer();
    ~MoviePlayer();

    // Initialize the movie player with the renderer and thread manager
    bool Initialize(std::shared_ptr<Renderer> rendererPtr, ThreadManager* threadManager);

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

    // NEW: Last frame time tracking for frame rate control
    std::chrono::steady_clock::time_point m_lastFrameTime;
    std::chrono::milliseconds m_frameInterval;

    // NEW: Frame ready flag to indicate if we have a new frame to render
    std::atomic<bool> m_hasNewFrame;

    // Predefined lock names for consistency
    static constexpr const char* MOVIE_MUTEX_LOCK = "movie_mutex";
    static constexpr const char* MOVIE_UPDATE_LOCK = "movie_update_lock";
    static constexpr const char* MOVIE_CONTROL_LOCK = "movie_control_lock";
    static constexpr const char* RENDERER_FRAME_LOCK = "renderer_frame_lock";
    static constexpr const char* D2D_DRAW_LOCK = "d2d_draw_lock";
};