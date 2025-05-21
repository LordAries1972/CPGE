/*
SoundManager.cpp - DirectSound-based SFX Sound Manager with WAV loader, parser, and queue playback logic
which was originally Programmed by Daniel J. Hobson of Australia.

We use DirectSound here because originally we used XAudio2 but its well known to have issues and one of
those issues starting occuring for me.  

So until Microsoft actually pulls it together, I will then update the DirectSound or whatever system
they decide to use in the future and update this code accordingly.  Thou with this, at least it works
and has robust & simple to use features!

EXAMPLE USAGE:-

#include "SoundManager.h"
#include <Windows.h>

using namespace SoundSystem;

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    // Create a dummy window for DirectSound initialization
    HWND hwnd = CreateWindowEx(0, L"STATIC", L"DummySoundWindow", WS_OVERLAPPEDWINDOW,
                               CW_USEDEFAULT, CW_USEDEFAULT, 100, 100,
                               nullptr, nullptr, hInstance, nullptr);

    if (!hwnd) return -1;

    // 1. Create and initialize SoundManager
    SoundManager soundManager;
    if (!soundManager.Initialize(hwnd)) {
        debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"SoundManager failed to initialize.");
        return -1;
    }

    // 2. Load all sounds (uses internal map of SFX_ID to filenames)
    soundManager.LoadAllSFX();

    // 3. Start playback thread (async management)
    soundManager.StartPlaybackThread();

    // 4. Set volume and optional cooldowns
    soundManager.SetGlobalVolume(1.0f);
    soundManager.SetCooldown(SFX_ID::SFX_CLICK, 1.5f); // 1.5s cooldown on click sound

    // 5. Play an immediate sound
    soundManager.PlayImmediateSFX(SFX_ID::SFX_CLICK);

    // 6. Queue a sound with fade-in
    soundManager.AddToQueue(SFX_ID::SFX_ALERT, 1.0f, StereoBalance::BALANCE_CENTER,
                            PlaybackType::pbtSFX_Once, 3.0f, SFX_PRIORITY::priHIGH, true);

    // 7. Run for a while (simulate game loop)
    Sleep(5000); // Let sounds play and fade in

    // 8. Shutdown safely
    soundManager.StopPlaybackThread();
    soundManager.CleanUp();

    DestroyWindow(hwnd);
    return 0;
}
*/

#include "Includes.h"
#include "SoundManager.h"
#include "Debug.h"

#pragma comment(lib, "dsound.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "winmm.lib")

using namespace SoundSystem;

extern Debug debug;

SoundManager::SoundManager() :
    m_directSound(nullptr),
    m_primaryBuffer(nullptr),
    m_initialized(false),
    m_cleanupDone(false),
    m_terminationFlag(false) {
}

SoundManager::~SoundManager() {
    if (!m_cleanupDone) {
        CleanUp();
    }
}

bool SoundManager::Initialize(HWND hwnd) {
    if (m_initialized) {
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"SoundManager already initialized");
        return true;
    }

    HRESULT hr = DirectSoundCreate8(NULL, &m_directSound, NULL);
    if (FAILED(hr)) {
        debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"DirectSoundCreate8 failed");
        return false;
    }

    hr = m_directSound->SetCooperativeLevel(hwnd, DSSCL_PRIORITY);
    if (FAILED(hr)) {
        debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"SetCooperativeLevel failed");
        return false;
    }

    DSBUFFERDESC primaryDesc = {};
    primaryDesc.dwSize = sizeof(DSBUFFERDESC);
    primaryDesc.dwFlags = DSBCAPS_PRIMARYBUFFER;

    hr = m_directSound->CreateSoundBuffer(&primaryDesc, &m_primaryBuffer, NULL);
    if (FAILED(hr)) {
        debug.logLevelMessage(LogLevel::LOG_CRITICAL, L"CreateSoundBuffer (primary) failed");
        return false;
    }

    m_initialized = true;
#if defined(_DEBUG_SOUNDMANAGER_)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"SoundManager initialized using DirectSound");
#endif
    return true;
}

void SoundManager::LoadAllSFX() {
    for (const auto& entry : sfxFileNames) {
        LoadedSFX sfx;
        if (LoadSFXFile(entry.second, sfx)) {
            fileList[entry.first] = sfx;
#if defined(_DEBUG_SOUNDMANAGER_)
            debug.logLevelMessage(LogLevel::LOG_INFO, L"Preloaded: " + entry.second);
#endif
        }
        else {
#if defined(_DEBUG_SOUNDMANAGER_)
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"Failed to preload: " + entry.second);
#endif
        }
    }
}

void SoundManager::AddToQueue(SFX_ID id, float volume, StereoBalance balance, PlaybackType type, float timeout) {
    AddToQueue(id, volume, balance, type, timeout, SFX_PRIORITY::priNORMAL);
}

void SoundManager::AddToQueue(SFX_ID id, float volume, StereoBalance balance, PlaybackType type, float timeout, SFX_PRIORITY priority, bool useFadeIn) {
    auto it = fileList.find(id);
    if (it == fileList.end()) {
#if defined(_DEBUG_SOUNDMANAGER_)
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"AddToQueue failed: SFX_ID not found");
#endif
        return;
    }

    // Cooldown check
    const auto now = std::chrono::steady_clock::now();
    auto lastIt = m_lastPlayedTime.find(id);
    if (lastIt != m_lastPlayedTime.end()) {
        float elapsed = std::chrono::duration<float>(now - lastIt->second).count();
        auto cooldownIt = m_sfxCooldown.find(id);
        if (cooldownIt != m_sfxCooldown.end() && elapsed < cooldownIt->second) {
#if defined(_DEBUG_SOUNDMANAGER_)
            debug.logLevelMessage(LogLevel::LOG_DEBUG, L"Cooldown active - Skipping ID: " + std::to_wstring(static_cast<int>(id)));
#endif
            return;
        }
    }

    // Apply global volume scaling
    float finalVolume = volume * m_globalVolume;

    // Fade-in duration logic (milliseconds)
    constexpr DWORD fadeInDurationMs = 250; // Optional configurable value

    SoundQueueItem item{
        id,
        it->second.waveFormat,
        it->second.audioData,
        finalVolume,
        balance,
        type,
        priority,
        true,
        false,
        now,
        {},
        timeout
    };

    item.fadeIn = useFadeIn;
    if (useFadeIn)
    {
        item.fadeInStartTime = now;
        item.fadeInDuration = fadeInDurationMs / 1000.0f;
    }

    {
        std::lock_guard<std::mutex> lock(m_queueMutex);

        if (m_soundQueue.empty() || static_cast<int>(priority) >= static_cast<int>(m_soundQueue.back().priority)) {
            m_soundQueue.push_back(std::move(item));
        }
        else {
            auto insertPos = std::lower_bound(
                m_soundQueue.begin(),
                m_soundQueue.end(),
                priority,
                [](const SoundQueueItem& other, SFX_PRIORITY prio) {
                    return static_cast<int>(other.priority) < static_cast<int>(prio);
                }
            );
            m_soundQueue.insert(insertPos, std::move(item));
        }

        m_lastPlayedTime[id] = now;
    }

#if defined(_DEBUG_SOUNDMANAGER_)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"Added sound to queue - ID: " + std::to_wstring(static_cast<int>(id)) +
        L", priority: " + std::to_wstring(static_cast<int>(priority)));
#endif
}

void SoundManager::SetGlobalVolume(float volume) {
    m_globalVolume = std::clamp(volume, 0.0f, 1.0f);
#if defined(_DEBUG_SOUNDMANAGER_)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"Global volume set to: " + std::to_wstring(m_globalVolume));
#endif
}

void SoundManager::SetCooldown(SFX_ID id, float seconds) {
    m_sfxCooldown[id] = seconds;
#if defined(_DEBUG_SOUNDMANAGER_)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"Cooldown set - ID: " + std::to_wstring(static_cast<int>(id)) + L", seconds: " + std::to_wstring(seconds));
#endif
}

void SoundManager::ClearCooldown(SFX_ID id) {
    m_sfxCooldown.erase(id);
    m_lastPlayedTime.erase(id);
#if defined(_DEBUG_SOUNDMANAGER_)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"Cooldown cleared - ID: " + std::to_wstring(static_cast<int>(id)));
#endif
}

bool SoundManager::ParseWaveFile(const BYTE* data, size_t size, LoadedSFX& result) {
    if (size < 12) return false;

    if (memcmp(data, "RIFF", 4) != 0 || memcmp(data + 8, "WAVE", 4) != 0) return false;

    const BYTE* fmtChunk = nullptr;
    const BYTE* dataChunk = nullptr;
    size_t fmtSize = 0;
    size_t dataSize = 0;

    const BYTE* ptr = data + 12;
    const BYTE* end = data + size;

    while (ptr + 8 <= end) {
        const char* chunkId = reinterpret_cast<const char*>(ptr);
        DWORD chunkSize = *reinterpret_cast<const DWORD*>(ptr + 4);

        if (ptr + 8 + chunkSize > end) break;

        if (strncmp(chunkId, "fmt ", 4) == 0) {
            fmtChunk = ptr + 8;
            fmtSize = chunkSize;
        }
        else if (strncmp(chunkId, "data", 4) == 0) {
            dataChunk = ptr + 8;
            dataSize = chunkSize;
        }

        ptr += 8 + chunkSize;
    }

    if (!fmtChunk || !dataChunk) return false;

    result.waveFormat.wFormatTag = *reinterpret_cast<const WORD*>(fmtChunk);
    result.waveFormat.nChannels = *reinterpret_cast<const WORD*>(fmtChunk + 2);
    result.waveFormat.nSamplesPerSec = *reinterpret_cast<const DWORD*>(fmtChunk + 4);
    result.waveFormat.nAvgBytesPerSec = *reinterpret_cast<const DWORD*>(fmtChunk + 8);
    result.waveFormat.nBlockAlign = *reinterpret_cast<const WORD*>(fmtChunk + 12);
    result.waveFormat.wBitsPerSample = *reinterpret_cast<const WORD*>(fmtChunk + 14);
    result.waveFormat.cbSize = 0;

    result.audioData.assign(dataChunk, dataChunk + dataSize);

#if defined(_DEBUG_SOUNDMANAGER_)
    debug.logLevelMessage(LogLevel::LOG_DEBUG,
        L"Loaded WAV Format: tag=" + std::to_wstring(result.waveFormat.wFormatTag) +
        L", channels=" + std::to_wstring(result.waveFormat.nChannels) +
        L", rate=" + std::to_wstring(result.waveFormat.nSamplesPerSec) +
        L", bits=" + std::to_wstring(result.waveFormat.wBitsPerSample));
#endif
    return true;
}

bool SoundManager::LoadSFXFile(const std::wstring& filename, LoadedSFX& outSfx) {
    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to open SFX file: " + filename);
        return false;
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<BYTE> buffer(size);
    if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to read SFX file: " + filename);
        return false;
    }

    if (!ParseWaveFile(buffer.data(), buffer.size(), outSfx)) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to parse WAV file: " + filename);
        return false;
    }

#if defined(_DEBUG_SOUNDMANAGER_)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"Successfully loaded SFX file: " + filename);
#endif
    return true;
}

void SoundManager::PlayImmediateSFX(SFX_ID id) {
    if (!m_initialized) return;

    auto it = fileList.find(id);
    if (it == fileList.end()) {
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"PlayImmediateSFX: SFX_ID not found");
        return;
    }

    const LoadedSFX& sfx = it->second;

    DSBUFFERDESC bufferDesc = {};
    bufferDesc.dwSize = sizeof(DSBUFFERDESC);
    bufferDesc.dwFlags = DSBCAPS_CTRLVOLUME;
    bufferDesc.dwBufferBytes = static_cast<DWORD>(sfx.audioData.size());
    bufferDesc.lpwfxFormat = const_cast<WAVEFORMATEX*>(&sfx.waveFormat);

    LPDIRECTSOUNDBUFFER tempBuffer = nullptr;
    HRESULT hr = m_directSound->CreateSoundBuffer(&bufferDesc, &tempBuffer, NULL);
    if (FAILED(hr)) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"CreateSoundBuffer failed");
        return;
    }

    void* pBuffer = nullptr;
    DWORD bufferSize = 0;
    hr = tempBuffer->Lock(0, sfx.audioData.size(), &pBuffer, &bufferSize, NULL, NULL, 0);
    if (FAILED(hr)) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Lock failed");
        tempBuffer->Release();
        return;
    }

    memcpy(pBuffer, sfx.audioData.data(), bufferSize);
    tempBuffer->Unlock(pBuffer, bufferSize, NULL, 0);

    tempBuffer->SetCurrentPosition(0);
    tempBuffer->SetVolume(DSBVOLUME_MAX);
    tempBuffer->Play(0, 0, 0);

#if defined(_DEBUG_SOUNDMANAGER_)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"DirectSound: Immediate SFX played");
#endif
}

void SoundManager::UpdateFadeInVolumes() {
    std::lock_guard<std::mutex> lock(m_queueMutex);
    auto now = std::chrono::steady_clock::now();

    for (auto& item : m_soundQueue) {
        if (!item.isPlaying || !item.fadeIn)
            continue;

        float elapsed = std::chrono::duration<float>(now - item.fadeInStartTime).count();
        if (elapsed >= item.fadeInDuration) {
            item.fadeIn = false;
            continue;
        }

        float fadeRatio = elapsed / item.fadeInDuration;
        fadeRatio = std::clamp(fadeRatio, 0.0f, 1.0f);
        float volume = item.volume * fadeRatio;

        LONG volumeDb = static_cast<LONG>(DSBVOLUME_MIN + (DSBVOLUME_MAX - DSBVOLUME_MIN) * volume);

        // Re-acquire the buffer and update volume (not implemented fully here)
        // In real implementation, we'd track the DirectSound buffer pointer in item or elsewhere.
        if (item.buffer) item.buffer->SetVolume(volumeDb);

#if defined(_DEBUG_SOUNDMANAGER_)
        debug.logLevelMessage(LogLevel::LOG_DEBUG, L"Fade-in update - ID: " + std::to_wstring(static_cast<int>(item.id)) +
            L" fadeRatio: " + std::to_wstring(fadeRatio) + L" db: " + std::to_wstring(volumeDb));
#endif
    }
}

void SoundManager::PlayQueueList() {
    // Automatically remove expired or finished sounds
    m_soundQueue.erase(std::remove_if(m_soundQueue.begin(), m_soundQueue.end(), [](SoundQueueItem& item) {
        if (!item.isPlaying || !item.buffer)
            return false;

        DWORD status = 0;
        item.buffer->GetStatus(&status);

        bool isLooping = item.playbackType == PlaybackType::pbtSFX_Loop;
        bool hasTimeout = item.timeoutSeconds > 0.0f;
        auto now = std::chrono::steady_clock::now();
        float elapsed = std::chrono::duration<float>(now - item.playTime).count();

        bool expired = !isLooping && !(status & DSBSTATUS_PLAYING);
        bool timeout = hasTimeout && elapsed >= item.timeoutSeconds;

        if (expired || timeout) {
            item.buffer->Stop();
            item.buffer->Release();
#if defined(_DEBUG_SOUNDMANAGER_)
            debug.logLevelMessage(LogLevel::LOG_DEBUG, L"[PlayQueueList] Sound expired and removed - ID: " + std::to_wstring(static_cast<int>(item.id)));
#endif
            return true;
        }

        return false;
        }), m_soundQueue.end());

    std::lock_guard<std::mutex> lock(m_queueMutex);
    const auto now = std::chrono::steady_clock::now();

    for (auto& item : m_soundQueue) {
        if (!item.enabled || item.isPlaying)
            continue;

        DSBUFFERDESC desc;
        ZeroMemory(&desc, sizeof(DSBUFFERDESC));
        desc.dwSize = sizeof(DSBUFFERDESC);
        desc.dwFlags = DSBCAPS_CTRLVOLUME | DSBCAPS_CTRLPAN;
        desc.dwBufferBytes = static_cast<DWORD>(item.audioData.size());
        desc.lpwfxFormat = &item.waveFormat;

        LPDIRECTSOUNDBUFFER buffer = nullptr;
        if (FAILED(m_directSound->CreateSoundBuffer(&desc, &buffer, nullptr))) {
#if defined(_DEBUG_SOUNDMANAGER_)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"PlayQueueList: CreateSoundBuffer failed");
#endif
            continue;
        }

        void* ptr = nullptr;
        DWORD size = 0;
        if (FAILED(buffer->Lock(0, item.audioData.size(), &ptr, &size, nullptr, nullptr, 0))) {
#if defined(_DEBUG_SOUNDMANAGER_)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"PlayQueueList: Lock failed");
#endif
            buffer->Release();
            continue;
        }

        memcpy(ptr, item.audioData.data(), size);
        buffer->Unlock(ptr, size, nullptr, 0);

        buffer->SetCurrentPosition(0);

        switch (item.balance) {
        case StereoBalance::BALANCE_LEFT: buffer->SetPan(DSBPAN_LEFT); break;
        case StereoBalance::BALANCE_RIGHT: buffer->SetPan(DSBPAN_RIGHT); break;
        default: buffer->SetPan(DSBPAN_CENTER); break;
        }

        LONG volumeDb = (item.fadeIn) ? DSBVOLUME_MIN : static_cast<LONG>(DSBVOLUME_MAX * item.volume);
        buffer->SetVolume(volumeDb);

        buffer->Play(0, 0, (item.playbackType == PlaybackType::pbtSFX_Loop) ? DSBPLAY_LOOPING : 0);

        item.buffer = buffer;
        item.playTime = now;
        item.isPlaying = true;

#if defined(_DEBUG_SOUNDMANAGER_)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"PlayQueueList: Playing SFX - ID: " + std::to_wstring(static_cast<int>(item.id)));
#endif
    }
}

void SoundManager::CleanUp() {
    m_cleanupDone = true;

    if (m_primaryBuffer) {
        m_primaryBuffer->Release();
        m_primaryBuffer = nullptr;
    }

    if (m_directSound) {
        m_directSound->Release();
        m_directSound = nullptr;
    }

#if defined(_DEBUG_SOUNDMANAGER_)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"SoundManager cleanup completed");
#endif
}

void SoundManager::StartPlaybackThread() {
    m_terminationFlag = false;
    m_workerThread = std::thread([this]() {
#if defined(_DEBUG_SOUNDMANAGER_)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"[SoundThread] Playback thread started");
#endif
        while (!m_terminationFlag) {
            PlayQueueList();
            UpdateFadeInVolumes();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
#if defined(_DEBUG_SOUNDMANAGER_)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"[SoundThread] Playback thread terminating");
#endif
        });
}

void SoundManager::StopPlaybackThread() {
    m_terminationFlag = true;
    if (m_workerThread.joinable()) {
        m_workerThread.join();
#if defined(_DEBUG_SOUNDMANAGER_)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"[SoundThread] Playback thread stopped");
#endif
    }
}