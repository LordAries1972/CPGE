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
    void applyPlayMusic(bool on);
    void setVolume(float volume);
    void fadeIn(int durationMs = 3000);
    void fadeOut(int durationMs = 3000);
    void seek(double positionMs);
    double getSeekPosition();

    // The track position (ms) as last sampled by the playlist thread itself (twice a second). Safe from any thread and never logs: the player is only ever touched on its own thread,
    // and a failed sample (a track change in progress) just keeps the previous value.  Use this - not getSeekPosition() - from game code.
    double currentPositionMs() const { return samplePosMs.load(); }
    // Like seek() but silent on failure (LOG_ERROR is fatal in this engine, and a seek right after a track change can legitimately fail).  Player thread / callback only.
    bool seekQuiet(double positionMs);

    // Asks the playlist thread to play `file` (matched by file name against the current track and the playlist) starting at `positionMs`, then continue the playlist from there.  The
    // work happens on the player's own thread (MFPlay's thread affinity); this only records the request.  False when the playlist thread is not running or the file is not in the list.
    bool requestTrack(const std::wstring& file, double positionMs);

    // config.myConfig.musicVolume as 0.0-1.0 (master volume is applied by the output device).
    static float ConfigMusicVolume();

    // Plays the playlist on the player's OWN thread.  Media Foundation's MFPlay processes Play() and delivers its events through the message queue of the thread that
    // CREATED the player, so that thread has to be a single-threaded apartment and keep pumping messages for as long as the music plays.  A player created on a game thread
    // that does neither loads and reports "Playback started" but stays silent.  Add the files with AddToPlaylist() first; the player then starts the first one, fades it
    // in, and moves through the list by itself.  Returns false if the playlist is empty or a thread is already running.  stopPlaylistThread() stops the music and joins.
    bool startPlaylistThread(HWND hWnd);
    void stopPlaylistThread();

    // The file now loaded, as a copy.  Thread-safe: with a playlist the player moves to the next track on its own thread, so read the
    // name through this instead of the public filePath member.
    std::wstring currentFile();

    // Asset scanning: every regular file under `root` whose extension equals `ext` (case-insensitive, e.g. L".mp3"), sorted by path so the
    // result is deterministic.  Unreadable folders are skipped silently.  recursive = false looks in `root` only.
    static std::vector<std::wstring> FindFiles(const std::wstring& root, const std::wstring& ext = L".mp3", bool recursive = true);

    // Replaces the playlist with every `ext` file found under `root` (see FindFiles), optionally shuffled (Fisher-Yates, a different order each
    // run).  Paths are stored absolute and normalised.  Returns how many files were added (0 = none found, playlist left empty).  Follow it with
    // startPlaylistThread() to play: e.g.  player.AddFolderToPlaylist(L"Assets").  Not for use while a playlist thread is running.
    size_t AddFolderToPlaylist(const std::wstring& root, const std::wstring& ext = L".mp3", bool shuffle = true, bool recursive = true);

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
    std::atomic<bool> itemReady{ false };       // Set by MFP_EVENT_TYPE_MEDIAITEM_SET: Play() is only valid after this
    std::atomic<bool> pendingPlay{ false };     // play() was requested before the media item was ready
    std::atomic<bool> playing{ false };
    std::atomic<bool> paused{ false };
    std::atomic<bool> terminateFlag{ false };
    std::atomic<bool> threadQuit{ false };      // asks the playlist thread to finish
    std::atomic<double> samplePosMs{ 0.0 };     // currentPositionMs()
    std::wstring requestedTrack;                // requestTrack(): the playlist entry to play next (guarded by mtx)
    std::atomic<bool> trackRequested{ false };  // requestTrack(): the playlist thread should switch to requestedTrack
    std::atomic<double> requestedSeekMs{ -1.0 };// requestTrack(): where to start inside it (< 0: from the beginning)
    std::atomic<double> seekAfterLoad{ -1.0 };  // applied once the new media item is ready (see OnMediaPlayerEvent)
    std::atomic<bool> advanceRequested{ false };// a track ended: the playlist thread starts the next one (never from inside the MFPlay callback)
    std::atomic<float> volume{ 1.0f };
    HWND hwnd;

    ComPtr<IMFPMediaPlayer> mediaPlayer;
    std::atomic<ULONG> refCount{ 1 };    // Reference count for COM
};
