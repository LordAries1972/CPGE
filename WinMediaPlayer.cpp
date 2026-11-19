#include "Includes.h"
#include "WinMediaPlayer.h"
#include "SceneManager.h"
#include "ThreadManager.h"
#include "Debug.h"
#include "Configuration.h"
#include <algorithm>
#include <chrono>
#include <cwctype>
#include <filesystem>
#include <random>

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
    stopPlaylistThread();                               // a joinable std::thread must never be destroyed
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

bool MediaPlayer::startPlaylistThread(HWND hWnd) {
    if (playlist.empty() || playbackThread.joinable()) return false;
    threadQuit = false;
    playbackThread = std::thread([this, hWnd]() {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);              // MFPlay needs a single-threaded apartment on the thread that creates the player
        Initialize(hWnd);
        currentPlaylistIndex = 0;
        if (loadFile(playlist[0])) {
            play();
            fadeIn(2000);                                               // lands exactly on the Music Volume setting
        }
        ULONGLONG lastSampleMs = 0;
        while (!threadQuit) {                                           // keep this thread's message queue serviced for as long as the music plays
            MSG msg;
            while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
            if (advanceRequested.exchange(false)) PlayNext();           // a track ended: start the next one here, outside the MFPlay callback
            const ULONGLONG nowMs = GetTickCount64();
            if (nowMs - lastSampleMs >= 500)                            // sample the position HERE, on the player's own thread: nothing else may touch mediaPlayer
            {
                lastSampleMs = nowMs;
                if (mediaPlayer && playing) { PROPVARIANT v; PropVariantInit(&v); if (SUCCEEDED(mediaPlayer->GetPosition(GUID_NULL, &v)) && v.vt == VT_I8) samplePosMs = static_cast<double>(v.hVal.QuadPart) / 10000.0; PropVariantClear(&v); }
            }
            if (trackRequested.exchange(false))                         // requestTrack(): switch to a given playlist entry / time index, also outside any callback
            {
                std::wstring target; bool same = false;
                { std::lock_guard<std::mutex> lock(mtx); target = requestedTrack; same = (filePath == target); }
                const double ms = requestedSeekMs.exchange(-1.0);
                if (same) { if (ms >= 0.0) seekQuiet(ms); }
                else if (!target.empty())
                {
                    for (size_t i = 0; i < playlist.size(); ++i) if (playlist[i] == target) currentPlaylistIndex = i;     // the playlist carries on after this entry
                    if (loadFile(target)) { seekAfterLoad = ms; play(); }
                    else PlayNext();                                    // loadFile released the old player: never leave the music dead
                }
            }
            Sleep(15);
        }
        terminate();
        CoUninitialize();
    });
    return true;
}

void MediaPlayer::stopPlaylistThread() {
    threadQuit = true;
    if (playbackThread.joinable()) playbackThread.join();
}

bool MediaPlayer::requestTrack(const std::wstring& file, double positionMs) {
    if (file.empty() || !playbackThread.joinable()) return false;
    const auto leaf = [](const std::wstring& p) { return std::filesystem::path(p).filename().wstring(); };
    std::wstring match;
    for (const std::wstring& entry : playlist) if (leaf(entry) == leaf(file)) { match = entry; break; }      // the playlist is only built before the thread starts
    if (match.empty()) return false;
    { std::lock_guard<std::mutex> lock(mtx); requestedTrack = match; }
    requestedSeekMs = positionMs; trackRequested = true;
    return true;
}

std::wstring MediaPlayer::currentFile() {
    std::lock_guard<std::mutex> lock(mtx);
    return filePath;
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
    // Release the previous track's player every time.  (cleanup() sets bHasCleanedUp and then does nothing on later calls, so the second and later tracks
    // used to be created on top of a player that was never released.)
    if (mediaPlayer) { mediaPlayer->Stop(); mediaPlayer.Reset(); }

    if (!isValidAudioFile(filePath)) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Unsupported or corrupted file: " + filePath);
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(mtx);          // currentFile() may be reading it from another thread
        this->filePath = filePath;
    }
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

bool MediaPlayer::seekQuiet(double positionMs) {
    if (!mediaPlayer) return false;
    PROPVARIANT var; InitPropVariantFromInt64(static_cast<LONGLONG>(positionMs * 10000), &var);
    const HRESULT hr = mediaPlayer->SetPosition(GUID_NULL, &var); PropVariantClear(&var);
    return SUCCEEDED(hr);
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

// Asset scanning ---------------------------------------------------------------------------------------------------------------------
std::vector<std::wstring> MediaPlayer::FindFiles(const std::wstring& root, const std::wstring& ext, bool recursive) {
    std::vector<std::wstring> found;
    namespace fs = std::filesystem;
    std::error_code ec;
    if (root.empty() || !fs::is_directory(fs::path(root), ec)) return found;

    // Lower-case the wanted extension once; each candidate is lower-cased the same way (the extension is short, so this is cheap).
    std::wstring want = ext;
    if (!want.empty() && want[0] != L'.') want.insert(want.begin(), L'.');
    for (wchar_t& c : want) c = static_cast<wchar_t>(std::towlower(c));

    auto consider = [&](const fs::directory_entry& e) {
        std::error_code e2;
        if (!e.is_regular_file(e2)) return;
        std::wstring x = e.path().extension().wstring();
        for (wchar_t& c : x) c = static_cast<wchar_t>(std::towlower(c));
        if (x == want) found.push_back(fs::absolute(e.path(), e2).lexically_normal().make_preferred().wstring());
    };
    if (recursive) {
        for (fs::recursive_directory_iterator it(fs::path(root), fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec))
            consider(*it);
    } else {
        for (fs::directory_iterator it(fs::path(root), fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec))
            consider(*it);
    }
    std::sort(found.begin(), found.end());
    return found;
}

size_t MediaPlayer::AddFolderToPlaylist(const std::wstring& root, const std::wstring& ext, bool shuffle, bool recursive) {
    std::vector<std::wstring> files = FindFiles(root, ext, recursive);
    ClearPlaylist();
    if (files.empty()) {
        debug.logLevelMessage(LogLevel::LOG_INFO, L"No '" + ext + L"' files found under: " + root);
        return 0;
    }
    if (shuffle) {                                   // Fisher-Yates, seeded from the clock so every run plays a different order
        std::mt19937 rng(static_cast<std::mt19937::result_type>(std::chrono::steady_clock::now().time_since_epoch().count()));
        for (size_t i = files.size(); i > 1; --i)
            std::swap(files[i - 1], files[std::uniform_int_distribution<size_t>(0, i - 1)(rng)]);
    }
    playlist.reserve(files.size());
    for (const std::wstring& f : files) AddToPlaylist(f);
    return files.size();
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

    // Move to the next file in the playlist, skipping any that fail to load (a bad file used to end the music for good: the finished player was replayed)
    for (size_t tries = 0; tries < playlist.size(); ++tries) {
        currentPlaylistIndex = (currentPlaylistIndex + 1) % playlist.size();
        if (loadFile(playlist[currentPlaylistIndex])) {
            play();
            return;
        }
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"Skipping unplayable playlist entry: " + playlist[currentPlaylistIndex]);
    }
    debug.logLevelMessage(LogLevel::LOG_ERROR, L"No playable file in the playlist.");
}

// IMFPMediaPlayerCallback implementation
STDMETHODIMP_(void) MediaPlayer::OnMediaPlayerEvent(MFP_EVENT_HEADER* pEventHeader) {
    if (pEventHeader->eEventType == MFP_EVENT_TYPE_MEDIAITEM_CREATED) {
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Media item created.");
    }
    else if (pEventHeader->eEventType == MFP_EVENT_TYPE_MEDIAITEM_SET) {
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Media item set.");
        itemReady = true;
        { const double ms = seekAfterLoad.exchange(-1.0); if (ms >= 0.0 && mediaPlayer && !terminateFlag) seekQuiet(ms); }       // requestTrack(): start inside the new track
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

        // If there's a playlist, play the next file.  With a playlist thread the advance is only REQUESTED here: loadFile() releases and recreates the MFPlay player, which
        // must not happen from inside that same player's own callback.  The thread's loop does it a few milliseconds later, outside any callback.
        if (!playlist.empty()) {
            if (playbackThread.joinable()) advanceRequested = true;
            else PlayNext();
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

