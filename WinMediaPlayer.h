#pragma once

// Our General C++ System includes.
#include "Includes.h"

// These includes can be removed, if using this class for a different purpose.
#include "ThreadManager.h"
#include "SceneManager.h"

const int MCI_CMD_BUFFERSIZE = 2048;

// Link required Window dependencies
// We will clean this up later when
// we are using different platforms.
#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfplay.lib")

using namespace Microsoft::WRL;

class MediaPlayer : public IMFPMediaPlayerCallback {
public:
    MediaPlayer();
    virtual ~MediaPlayer();

    // IUnknown methods
    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObject) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    void Initialize(HWND hWnd);
    bool loadFile(const std::wstring& filePath);
    void play();
    void pause();
    void resume();
    void stop();
    void terminate();
    void setVolume(float volume);
    void fadeIn(int durationMs = 3000);
    void fadeOut(int durationMs = 3000);
    void seek(double positionMs);
    double getSeekPosition();

    // Playlist management
    void AddToPlaylist(const std::wstring& filePath);
    void ClearPlaylist();
    void PlayNext();

    // Public Thread Safety Access
    std::mutex mtx;
    std::wstring filePath;

    // IMFPMediaPlayerCallback methods
    STDMETHODIMP_(void) OnMediaPlayerEvent(MFP_EVENT_HEADER* pEventHeader) override;

private:
    bool isValidAudioFile(const std::wstring& filePath);
    void playbackLoop();
    void cleanup();

    bool bHasCleanedUp = false;
    std::vector<std::wstring> playlist;  // Playlist of files
    size_t currentPlaylistIndex = 0;     // Current index in the playlist
    std::thread playbackThread;
    std::atomic<bool> bNotStarted{ false };
    std::atomic<bool> playing{ false };
    std::atomic<bool> paused{ false };
    std::atomic<bool> terminateFlag{ false };
    std::atomic<float> volume{ 1.0f };
    HWND hwnd;

    ComPtr<IMFPMediaPlayer> mediaPlayer;
    std::atomic<ULONG> refCount{ 1 };    // Reference count for COM
};
