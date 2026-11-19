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

#pragma warning(push)
#pragma warning(disable: 4267)                  // Suppress size_t conversion warnings

#pragma comment(lib, "dsound.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "winmm.lib")

using namespace SoundSystem;

extern Debug debug;

#if defined(PLATFORM_WINDOWS)
namespace {
    // Linear gain (0..1) -> DirectSound volume.  DirectSound volume is in hundredths of a DECIBEL (0 = full, -10000 = silent), so a linear gain must go
    // through a logarithm: dB = 20 * log10(gain), i.e. 2000 * log10(gain) in these units.  Interpolating linearly between DSBVOLUME_MIN and 0 instead
    // (which this file used to do) made half volume -50 dB: effectively silent, so a volume slider only seemed to work near its top.
    LONG GainToDirectSound(float gain) {
        if (!(gain > 0.0001f)) return DSBVOLUME_MIN;            // silence (also catches NaN)
        if (gain >= 1.0f) return DSBVOLUME_MAX;
        const LONG db = static_cast<LONG>(2000.0f * std::log10(gain));
        return (db < DSBVOLUME_MIN) ? DSBVOLUME_MIN : db;
    }
}
#endif

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

// DirectSound is a Windows API regardless of renderer — guard on PLATFORM_WINDOWS,
// not __USE_DIRECTX_11__, so it works under Vulkan and OpenGL builds on Windows too.
#if defined(PLATFORM_WINDOWS)
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
#endif

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
            debug.logDiagLevelMessage(LogLevel::LOG_WARNING, L"Failed to preload: " + entry.second);
        }
    }
}

void SoundManager::AddToQueue(SFX_ID id, float volume, StereoBalance balance, PlaybackType type, float timeout) {
    AddToQueue(id, volume, balance, type, timeout, SFX_PRIORITY::priNORMAL);
}

void SoundManager::AddToQueue(SFX_ID id, float volume, StereoBalance balance, PlaybackType type, float timeout, SFX_PRIORITY priority, bool useFadeIn) {
    auto it = fileList.find(id);
    if (it == fileList.end()) {
        debug.logDiagLevelMessage(LogLevel::LOG_WARNING, L"AddToQueue failed: SFX_ID not found");
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
#if defined(PLATFORM_WINDOWS)
    {   // bank sounds already playing follow the new volume at once (the options window's volume sliders call this)
        std::lock_guard<std::mutex> lock(m_bankMutex);
        for (BankSound& b : m_bank)
            for (BankVoice& v : b.voices)
                if (v.buf) ApplyBankVoiceVolume(v);
    }
#endif
    #if defined(_DEBUG_SOUNDMANAGER_)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Global volume set to: " + std::to_wstring(m_globalVolume.load()));
    #endif
}

void SoundManager::SetAmbientVolume(float volume) {
    m_ambientVolume = std::clamp(volume, 0.0f, 1.0f);
#if defined(PLATFORM_WINDOWS)
    std::lock_guard<std::mutex> lock(m_bankMutex);              // loops that are playing follow the new level at once
    for (BankSound& b : m_bank)
        for (BankVoice& v : b.voices)
            if (v.buf && v.loop) ApplyBankVoiceVolume(v);
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

#if defined(PLATFORM_WINDOWS)
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
    tempBuffer->SetVolume(GainToDirectSound(m_globalVolume));
    tempBuffer->Play(0, 0, 0);

    #if defined(_DEBUG_SOUNDMANAGER_)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"DirectSound: Immediate SFX played");
    #endif
#endif // PLATFORM_WINDOWS
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

#if defined(PLATFORM_WINDOWS)
        LONG volumeDb = GainToDirectSound(volume);

        // Re-acquire the buffer and update volume (not implemented fully here)
        // In real implementation, we'd track the DirectSound buffer pointer in item or elsewhere.
        if (item.buffer) item.buffer->SetVolume(volumeDb);

            #if defined(_DEBUG_SOUNDMANAGER_)
                debug.logLevelMessage(LogLevel::LOG_DEBUG, L"Fade-in update - ID: " + std::to_wstring(static_cast<int>(item.id)) +
                    L" fadeRatio: " + std::to_wstring(fadeRatio) + L" db: " + std::to_wstring(volumeDb));
            #endif
#endif // PLATFORM_WINDOWS
    }
}

void SoundManager::PlayQueueList() {
#if defined(PLATFORM_WINDOWS)
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
            debug.logDiagLevelMessage(LogLevel::LOG_ERROR, L"PlayQueueList: CreateSoundBuffer failed");
            continue;
        }

        void* ptr = nullptr;
        DWORD size = 0;
        if (FAILED(buffer->Lock(0, item.audioData.size(), &ptr, &size, nullptr, nullptr, 0))) {
            debug.logDiagLevelMessage(LogLevel::LOG_ERROR, L"PlayQueueList: Lock failed");
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

        LONG volumeDb = (item.fadeIn) ? DSBVOLUME_MIN : GainToDirectSound(item.volume);
        buffer->SetVolume(volumeDb);

        buffer->Play(0, 0, (item.playbackType == PlaybackType::pbtSFX_Loop) ? DSBPLAY_LOOPING : 0);

        item.buffer = buffer;
        item.playTime = now;
        item.isPlaying = true;

        #if defined(_DEBUG_SOUNDMANAGER_)
            debug.logLevelMessage(LogLevel::LOG_INFO, L"PlayQueueList: Playing SFX - ID: " + std::to_wstring(static_cast<int>(item.id)));
        #endif
    }
#endif // PLATFORM_WINDOWS
}

// ---- Sound bank -------------------------------------------------------------------------------------------------------------------
void SoundManager::ApplyBankVoiceVolume(BankVoice& v) {
    #if defined(PLATFORM_WINDOWS)
        if (!v.buf) return;
        const float gain = v.level * m_globalVolume.load() * (v.loop ? m_ambientVolume.load() : 1.0f);
        if (gain == v.appliedGain) return;
        // Retry failed driver writes; every volume control still takes effect immediately.
        if (SUCCEEDED(v.buf->SetVolume(GainToDirectSound(gain)))) v.appliedGain = gain;
    #endif
}

void SoundManager::ReleaseBankSound(BankSound& b) {
#if defined(PLATFORM_WINDOWS)
    for (BankVoice& v : b.voices)
        if (v.buf) { v.buf->Stop(); v.buf->Release(); v.buf = nullptr; }
#endif
    b.voices.clear();
    b.ok = false;
}

bool SoundManager::RegisterBankSound(int slot, const std::wstring& file, int voices, unsigned minGapMs, unsigned maxPlayMs, float gain) {
#if defined(PLATFORM_WINDOWS)
    if (!m_initialized || !m_directSound || slot < 0 || slot > 4095 || voices < 1) return false;
    std::error_code ec;
    if (!std::filesystem::exists(file, ec)) return false;       // checked here: LoadSFXFile logs an error for a missing file, and a missing optional effect is not one

    LoadedSFX sfx;
    if (!LoadSFXFile(file, sfx)) return false;

    DSBUFFERDESC d = {};
    d.dwSize = sizeof(d);
    d.dwFlags = DSBCAPS_CTRLVOLUME | DSBCAPS_GLOBALFOCUS | DSBCAPS_GETCURRENTPOSITION2;     // GLOBALFOCUS: keeps playing when the window loses focus
    d.dwBufferBytes = static_cast<DWORD>(sfx.audioData.size());
    d.lpwfxFormat = &sfx.waveFormat;
    LPDIRECTSOUNDBUFFER master = nullptr;
    if (FAILED(m_directSound->CreateSoundBuffer(&d, &master, nullptr)) || !master) return false;
    void* p = nullptr; DWORD n = 0;
    if (SUCCEEDED(master->Lock(0, d.dwBufferBytes, &p, &n, nullptr, nullptr, 0))) { memcpy(p, sfx.audioData.data(), n); master->Unlock(p, n, nullptr, 0); }

    BankSound bs;
    bs.minGapMs = minGapMs; bs.maxPlayMs = maxPlayMs; bs.gain = gain;
    bs.voices.push_back({ master, 0, 0.0f });
    for (int i = 1; i < voices; ++i) {                          // extra voices SHARE the master's sample data
        LPDIRECTSOUNDBUFFER dup = nullptr;
        if (FAILED(m_directSound->DuplicateSoundBuffer(master, &dup)) || !dup) break;
        bs.voices.push_back({ dup, 0, 0.0f });
    }
    bs.ok = true;

    std::lock_guard<std::mutex> lock(m_bankMutex);
    if (static_cast<size_t>(slot) >= m_bank.size()) m_bank.resize(static_cast<size_t>(slot) + 1);
    ReleaseBankSound(m_bank[slot]);                             // registering a slot twice replaces it
    m_bank[slot] = std::move(bs);
    return true;
#else
    (void)slot; (void)file; (void)voices; (void)minGapMs; (void)maxPlayMs; (void)gain;
    return false;
#endif
}

bool SoundManager::IsBankSoundLoaded(int slot) {
    std::lock_guard<std::mutex> lock(m_bankMutex);
    return slot >= 0 && static_cast<size_t>(slot) < m_bank.size() && m_bank[slot].ok;
}

void SoundManager::PlayBankSound(int slot, float volume) {
#if defined(PLATFORM_WINDOWS)
    std::lock_guard<std::mutex> lock(m_bankMutex);
    if (slot < 0 || static_cast<size_t>(slot) >= m_bank.size() || !m_bank[slot].ok) return;
    BankSound& b = m_bank[slot];
    const unsigned long long now = GetTickCount64();
    if (now - b.lastPlayed < b.minGapMs) return;                // throttle rapid retriggers

    BankVoice* pick = nullptr;                                  // a free voice, else steal the oldest one
    for (BankVoice& v : b.voices) {
        DWORD st = 0;
        v.buf->GetStatus(&st);
        if (!(st & DSBSTATUS_PLAYING)) { pick = &v; break; }
        if (!pick || v.started < pick->started) pick = &v;
    }
    if (!pick) return;
    pick->level = volume * b.gain;
    pick->loop = false;                                         // a one-shot effect: not ambient
    pick->buf->Stop();
    pick->buf->SetCurrentPosition(0);
    ApplyBankVoiceVolume(*pick);
    pick->buf->Play(0, 0, 0);
    pick->started = now; b.lastPlayed = now;
#else
    (void)slot; (void)volume;
#endif
}

void SoundManager::PlayBankLoop(int slot, float volume) {
#if defined(PLATFORM_WINDOWS)
    std::lock_guard<std::mutex> lock(m_bankMutex);
    if (slot < 0 || static_cast<size_t>(slot) >= m_bank.size() || !m_bank[slot].ok) return;
    BankSound& b = m_bank[slot];
    BankVoice& v = b.voices[0];
    DWORD st = 0;
    v.buf->GetStatus(&st);
    if (st & DSBSTATUS_PLAYING) return;                         // already running: nothing to do
    v.level = volume * b.gain;
    v.loop = true;                                              // a looping background sound: scaled by the ambient volume
    v.buf->SetCurrentPosition(0);
    ApplyBankVoiceVolume(v);
    v.buf->Play(0, 0, DSBPLAY_LOOPING);
    v.started = GetTickCount64();
#else
    (void)slot; (void)volume;
#endif
}

void SoundManager::SetBankLoopGain(int slot, float volume) {
    std::lock_guard<std::mutex> lock(m_bankMutex);
    if (slot < 0 || static_cast<size_t>(slot) >= m_bank.size() || !m_bank[slot].ok) return;
    BankVoice& v = m_bank[slot].voices[0];
    v.level = volume * m_bank[slot].gain;
    ApplyBankVoiceVolume(v);
}

void SoundManager::StopBankSound(int slot) {
    std::lock_guard<std::mutex> lock(m_bankMutex);
    if (slot < 0 || static_cast<size_t>(slot) >= m_bank.size() || !m_bank[slot].ok) return;
#if defined(PLATFORM_WINDOWS)
    // Games stop inactive loops every frame. An idle voice needs no driver call.
    for (BankVoice& v : m_bank[slot].voices)
        if (v.started != 0) { v.buf->Stop(); v.started = 0; }
#endif
}

void SoundManager::StopAllBankSounds() {
    std::lock_guard<std::mutex> lock(m_bankMutex);
#if defined(PLATFORM_WINDOWS)
    for (BankSound& b : m_bank)
        for (BankVoice& v : b.voices)
            if (v.buf && v.started != 0) { v.buf->Stop(); v.started = 0; }
#endif
}

void SoundManager::UpdateBankSounds() {
#if defined(PLATFORM_WINDOWS)
    std::lock_guard<std::mutex> lock(m_bankMutex);
    const unsigned long long now = GetTickCount64();
    for (BankSound& b : m_bank) {
        if (!b.ok || b.maxPlayMs == 0) continue;
        for (BankVoice& v : b.voices)
            if (v.started && now - v.started > b.maxPlayMs) { v.buf->Stop(); v.started = 0; }
    }
#endif
}

void SoundManager::RegisterBankSoundsAsync(std::vector<BankSoundDef> defs) {
    WaitForBankLoad();                                          // one batch at a time: a second call queues behind the first
    if (defs.empty() || m_cleanupDone) return;
    m_bankAbort = false;
    m_bankLoading = true;
    m_bankLoader = std::thread([this, list = std::move(defs)]() {
        for (const BankSoundDef& d : list) {
            if (m_bankAbort) break;                             // shutting down: abandon the rest
            RegisterBankSound(d.slot, d.file, d.voices, d.minGapMs, d.maxPlayMs, d.gain);   // locks the bank only to publish each finished sound
        }
        m_bankLoading.store(false, std::memory_order_release);
    });
}

void SoundManager::WaitForBankLoad() {
    if (m_bankLoader.joinable()) m_bankLoader.join();
}

void SoundManager::CleanUp() {
    m_cleanupDone = true;
    m_bankAbort = true;                                         // stop the background loader before anything it uses is released
    WaitForBankLoad();

    {   // bank voices are children of the DirectSound device: release them first
        std::lock_guard<std::mutex> lock(m_bankMutex);
        for (BankSound& b : m_bank) ReleaseBankSound(b);
        m_bank.clear();
    }

#if defined(PLATFORM_WINDOWS)
    if (m_primaryBuffer) {
        m_primaryBuffer->Release();
        m_primaryBuffer = nullptr;
    }

    if (m_directSound) {
        m_directSound->Release();
        m_directSound = nullptr;
    }
#else
    m_primaryBuffer = nullptr;
    m_directSound   = nullptr;
#endif

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
        int bankTick = 0;
        while (!m_terminationFlag) {
            PlayQueueList();
            UpdateFadeInVolumes();
            if (++bankTick >= 10) { bankTick = 0; UpdateBankSounds(); }          // every 100 ms
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

#pragma warning(pop)
