#include "Includes.h"
#include "WinMediaPlayer.h"
#include "SceneManager.h"
#include "ThreadManager.h"
#include "Debug.h"

extern Debug debug;
extern ThreadManager threadManager;
extern SceneManager scene;

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
    debug.logLevelMessage(LogLevel::LOG_INFO, L"File loaded successfully: " + filePath);
    return true;
}

void MediaPlayer::play() {
    if (!mediaPlayer) return;

    stop();

    playing = true;
    paused = false;
    terminateFlag = false;
    mediaPlayer->Play();
    bNotStarted = true;
    debug.logLevelMessage(LogLevel::LOG_INFO, L"Playback started.");
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

void MediaPlayer::fadeIn(int durationMs) {
    std::thread([this, durationMs]() {
        float step = 1.0f / (durationMs / 50.0f);
        for (float v = 0; v <= 1.0f; v += step) {
            setVolume(v);
            Sleep(50);
        }
        }).detach();
}

void MediaPlayer::fadeOut(int durationMs) {
    std::thread([this, durationMs]() {
        float step = 1.0f / (durationMs / 50.0f);
        for (float v = 1.0f; v >= 0.0f; v -= step) {
            setVolume(v);
            Sleep(50);
        }
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
    }
    else if (pEventHeader->eEventType == MFP_EVENT_TYPE_PLAYBACK_ENDED) {
        // Handle playback ended
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Playback ended.");

        // Pause for 1 seconds before next action
        Sleep(1000);

        // If there's a playlist, play the next file
        if (!playlist.empty()) {
            PlayNext();
        }
        else {
            // No playlist, stop playback and restart the playback.
            stop();
            scene.stSceneType = SceneType::SCENE_LOAD_MP3;
            threadManager.ResumeThread(THREAD_LOADER);
        }
    }
}

