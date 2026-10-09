#include "Includes.h"
#include "WinMediaPlayer.h"
#include "SceneManager.h"
#include "ThreadManager.h"
#include "Debug.h"
#include "Configuration.h"

extern Debug debug;
extern ThreadManager threadManager;
extern SceneManager scene;
extern Configuration config;

// Music volume from the config (0-64 -> 0.0-1.0).  Master volume is NOT applied here:
// it is set on the Windows output device (ApplySystemMasterVolume), which this player
// renders through, so it already scales the MP3 like every other audio subsystem.
float MediaPlayer::ConfigMusicVolume() {
    return static_cast<float>(std::clamp(config.myConfig.musicVolume, 0, 64)) / 64.0f;
}

// Implementation
MediaPlayer::MediaPlayer() {}

MediaPlayer::~MediaPlayer() {
    try {
        if (bHasCleanedUp) return;
        debug.logLevelMessage(LogLevel::LOG_INFO, L"MediaPlayer destroyed.");
        if (mediaPlayer) { mediaPlayer->Stop(); }
        cleanup();
    }
    catch (const std::exception& e) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Exception in MediaPlayer destructor: " + std::wstring(e.what(), e.what() + strlen(e.what())));
    }
    catch (...) {
        debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"Unknown exception in MediaPlayer destructor.");
    }

    bHasCleanedUp = true;
}

// IUnknown methods
STDMETHODIMP MediaPlayer::QueryInterface(REFIID riid, void** ppvObject) {
    if (ppvObject == nullptr) return E_POINTER;

    if (riid == IID_IUnknown || riid == __uuidof(IMFPMediaPlayerCallback)) {
        *ppvObject = static_cast<IMFPMediaPlayerCallback*>(this);
        AddRef();
        return S_OK;
    }

    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) MediaPlayer::AddRef() {
    return ++refCount;
}

STDMETHODIMP_(ULONG) MediaPlayer::Release() {
    ULONG count = --refCount;
    if (count == 0) {
        delete this;
    }
    return count;
}

void MediaPlayer::Initialize(HWND hWnd) {
    hwnd = hWnd;
    HRESULT hr = MFStartup(MF_VERSION);
    if (FAILED(hr)) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Media Foundation initialization failed. HRESULT: " + std::to_wstring(hr));
        return;
    }

    debug.logLevelMessage(LogLevel::LOG_INFO, L"MediaPlayer initialized.");
}

bool MediaPlayer::isValidAudioFile(const std::wstring& filePath) {
    std::ifstream file(filePath, std::ios::binary);
    if (!file) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to open file: " + filePath);
        return false;
    }

    char header[12] = {};
    file.read(header, sizeof(header));

    // Check for MP3 magic number (Frame Sync)
    if ((header[0] & 0xFF) == 0xFF && (header[1] & 0xE0) == 0xE0) {
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Valid MP3 file detected: " + filePath);
        return true;
    }

    // Check for M4A (MP4) file signature
    if (memcmp(header + 4, "ftypM4A", 7) == 0 || memcmp(header + 4, "ftypmp42", 7) == 0) {
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Valid M4A file detected: " + filePath);
        return true;
    }

    // Check for compressed MP3 files (ID3 header + additional frames)
    if ((header[0] & 0xFF) == 0x49 && (header[1] & 0xFF) == 0x44 && (header[2] & 0xFF) == 0x33) {
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Compressed MP3 (ID3) detected: " + filePath);
        return true;
    }

    debug.logLevelMessage(LogLevel::LOG_ERROR, L"Invalid audio file format: " + filePath);
    return false;
}

bool MediaPlayer::loadFile(const std::wstring& filePath) {
    cleanup();

    if (!isValidAudioFile(filePath)) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Unsupported or corrupted file: " + filePath);
        return false;
    }

    this->filePath = filePath;
    itemReady = false;
    pendingPlay = false;
    HRESULT hr = MFPCreateMediaPlayer(
        filePath.c_str(),  // File path
        FALSE,             // Do not auto-play
        0,                 // Default flags
        this,              // Callback interface (this)
        NULL,              // Window handle for video (NULL for audio)
//        hwnd,              // Window handle for video (NULL for audio)
        &mediaPlayer       // Pointer to MediaPlayer object
    );

    if (FAILED(hr)) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to load file: " + filePath);
        return false;
    }
    setVolume(ConfigMusicVolume());                     // New players start at 1.0; honour the music volume setting

    // MFPCreateMediaPlayer() sets the media item asynchronously and the
    // MFP_EVENT_TYPE_MEDIAITEM_SET event is delivered through THIS thread's message
    // queue.  Play() before that event fails (MF_E_INVALIDREQUEST), so pump messages
    // here (bounded) until the item is ready.  The loader thread never pumps otherwise.
    for (int waited = 0; !itemReady && waited < 2000; waited += 10) {
        MSG msg;
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        if (itemReady) break;
        Sleep(10);
    }
    if (!itemReady)
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"Media item not ready after 2s; playback will start when it is: " + filePath);

    debug.logLevelMessage(LogLevel::LOG_INFO, L"File loaded successfully: " + filePath);
    return true;
}

void MediaPlayer::play() {
    if (!mediaPlayer) return;

    stop();

    terminateFlag = false;
    paused = false;

    // "Play Music" off: keep the file loaded but do not start it.
    // applyPlayMusic(true) starts it when the setting is switched back on.
    if (!config.myConfig.playMusic) {
        playing = false;
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Playback not started (Play Music is off).");
        return;
    }

    playing = true;
    bNotStarted = true;
    if (!itemReady) {
        pendingPlay = true;                             // OnMediaPlayerEvent(MEDIAITEM_SET) starts it
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Playback queued until the media item is ready.");
        return;
    }

    HRESULT hr = mediaPlayer->Play();
    if (FAILED(hr)) {
        playing = false;
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Play() failed. HRESULT: " + std::to_wstring(hr));
        return;
    }
    debug.logLevelMessage(LogLevel::LOG_INFO, L"Playback started.");
}

// Applies the "Play Music" setting to the loaded track: off pauses it, on resumes it
// (or starts it if it was loaded while music was off).
void MediaPlayer::applyPlayMusic(bool on) {
    if (!mediaPlayer || terminateFlag) return;

    if (!on) {
        pause();                                        // No-op unless currently playing
        return;
    }

    if (paused) {
        resume();
    }
    else if (!playing) {
        seek(0.0);
        play();
        fadeIn(2000);
    }
}

void MediaPlayer::pause() {
    if (!playing || paused) return;
    mediaPlayer->Pause();
    paused = true;
    debug.logLevelMessage(LogLevel::LOG_INFO, L"Playback paused.");
}

void MediaPlayer::resume() {
    if (mediaPlayer && paused) {
        HRESULT hr = mediaPlayer->Play();
        if (SUCCEEDED(hr)) {
            paused = false;
            debug.logLevelMessage(LogLevel::LOG_INFO, L"Playback resumed.");
        }
        else {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to resume playback.");
        }
    }
}

void MediaPlayer::stop() {
    if (!playing) return;
    playing = false;
    if (mediaPlayer) { mediaPlayer->Stop(); }
}

void MediaPlayer::terminate() {
    terminateFlag = true;
    if (mediaPlayer) { mediaPlayer->Stop(); }
    playing = false;
    debug.logLevelMessage(LogLevel::LOG_INFO, L"Playback thread terminated.");
}

void MediaPlayer::setVolume(float vol) {
    volume = vol;
    if (mediaPlayer) mediaPlayer->SetVolume(vol);
}

// Fades ramp a 0-1 fraction of the music volume setting, re-read every step so a
// slider change during a fade is honoured.
void MediaPlayer::fadeIn(int durationMs) {
    std::thread([this, durationMs]() {
        float step = 1.0f / (std::max(durationMs, 50) / 50.0f);
        for (float t = 0.0f; t < 1.0f; t += step) {
            setVolume(t * ConfigMusicVolume());
            Sleep(50);
        }
        setVolume(ConfigMusicVolume());                 // Land exactly on the configured level
        }).detach();
}

void MediaPlayer::fadeOut(int durationMs) {
    std::thread([this, durationMs]() {
        float step = 1.0f / (std::max(durationMs, 50) / 50.0f);
        for (float t = 1.0f; t > 0.0f; t -= step) {
            setVolume(t * ConfigMusicVolume());
            Sleep(50);
        }
        setVolume(0.0f);
        stop();
        }).detach();
}

void MediaPlayer::seek(double positionMs) {
    if (mediaPlayer) {
        PROPVARIANT var;
        InitPropVariantFromInt64(static_cast<LONGLONG>(positionMs * 10000), &var); // Convert ms to 100-nanosecond units
        HRESULT hr = mediaPlayer->SetPosition(GUID_NULL, &var);
        PropVariantClear(&var);
        if (SUCCEEDED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_INFO, L"Seeked to position " + std::to_wstring(positionMs) + L" ms");
        }
        else {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to seek to position " + std::to_wstring(positionMs) + L" ms");
        }
    }
}

double MediaPlayer::getSeekPosition() {
    if (!mediaPlayer) return 0.0;

    PROPVARIANT var;
    PropVariantInit(&var);

    HRESULT hr = mediaPlayer->GetPosition(GUID_NULL, &var); // GUID_NULL used here

    if (SUCCEEDED(hr) && var.vt == VT_I8) {
        double positionMs = static_cast<double>(var.hVal.QuadPart) / 10000.0; // Convert to milliseconds
        PropVariantClear(&var);
        return positionMs;
    }

    PropVariantClear(&var);
    debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to get playback position.");
    return 0.0;
}

void MediaPlayer::cleanup() {
    if (bHasCleanedUp) { return; }
    if (mediaPlayer) { mediaPlayer.Reset(); mediaPlayer = nullptr; }
    bHasCleanedUp = true;
}

// Playlist management
void MediaPlayer::AddToPlaylist(const std::wstring& filePath) {
    playlist.push_back(filePath);
    debug.logLevelMessage(LogLevel::LOG_INFO, L"Added to playlist: " + filePath);
}

void MediaPlayer::ClearPlaylist() {
    playlist.clear();
    currentPlaylistIndex = 0;
    debug.logLevelMessage(LogLevel::LOG_INFO, L"Playlist cleared.");
}

void MediaPlayer::PlayNext() {
    if (playlist.empty()) {
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"No files in playlist.");
        return;
    }

    // Move to the next file in the playlist
    currentPlaylistIndex = (currentPlaylistIndex + 1) % playlist.size();
    loadFile(playlist[currentPlaylistIndex]);
    play();
}

// IMFPMediaPlayerCallback implementation
STDMETHODIMP_(void) MediaPlayer::OnMediaPlayerEvent(MFP_EVENT_HEADER* pEventHeader) {
    if (pEventHeader->eEventType == MFP_EVENT_TYPE_MEDIAITEM_CREATED) {
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Media item created.");
    }
    else if (pEventHeader->eEventType == MFP_EVENT_TYPE_MEDIAITEM_SET) {
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Media item set.");
        itemReady = true;
        if (pendingPlay.exchange(false) && mediaPlayer && !terminateFlag && playing && !paused) {
            HRESULT hr = mediaPlayer->Play();
            if (FAILED(hr)) {
                playing = false;
                debug.logLevelMessage(LogLevel::LOG_ERROR, L"Deferred Play() failed. HRESULT: " + std::to_wstring(hr));
            }
            else {
                debug.logLevelMessage(LogLevel::LOG_INFO, L"Deferred playback started.");
            }
        }
    }
    else if (pEventHeader->eEventType == MFP_EVENT_TYPE_PLAYBACK_ENDED) {
        // Handle playback ended
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Playback ended.");

        // If there's a playlist, play the next file
        if (!playlist.empty()) {
            PlayNext();
        }
        else if (!config.myConfig.playMusic) {
            playing = false;                            // applyPlayMusic(true) restarts it from 0
        }
        else if (mediaPlayer && !terminateFlag) {
            // No playlist: loop the current track in place on the existing player.
            // The scene type is NOT touched, so the current scene keeps rendering.
            // (Switching to SCENE_LOAD_MP3 here lost the scene -> black screen, and the
            // loader re-created the player on a thread that stops pumping messages.)
            // No Sleep() here: this callback runs on the player's message-pump thread.
            seek(0.0);
            HRESULT hr = mediaPlayer->Play();
            if (SUCCEEDED(hr)) {
                playing = true;
                paused  = false;
                debug.logLevelMessage(LogLevel::LOG_INFO, L"Playback restarted (loop).");
            }
            else {
                playing = false;
                debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to restart playback. HRESULT: " + std::to_wstring(hr));
            }
        }
    }
}

