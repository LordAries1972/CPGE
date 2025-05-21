// SoundManager.h - DirectSound-based SFX Sound Manager (replaces XAudio2)
#pragma once

#include "Includes.h"

namespace SoundSystem {

    enum class PlaybackType {
        pbtSFX_Once,
        pbtSFX_Loop
    };

    enum class StereoBalance {
        BALANCE_CENTER,
        BALANCE_LEFT,
        BALANCE_RIGHT
    };

    enum class SFX_ID {
        SFX_INVALID = -1,
        SFX_CLICK = 1,
        SFX_BEEP = 2,
    };

    enum class SFX_PRIORITY {
        priIMMEDIATELY,
        priDELAYED_START,
        priHIGH,
        priABOVE_NORMAL,
        priNORMAL,
        priBELOW_NORMAL,
        priLOWEST
    };

    struct LoadedSFX {
        WAVEFORMATEX waveFormat;
        std::vector<BYTE> audioData;
    };

    struct SoundQueueItem {
        SFX_ID id;
        WAVEFORMATEX waveFormat;
        std::vector<BYTE> audioData;
        float volume;
        StereoBalance balance;
        PlaybackType playbackType;
        SFX_PRIORITY priority;
        bool enabled;
        bool isPlaying;
        std::chrono::steady_clock::time_point queueTime;
        std::chrono::steady_clock::time_point playTime;
        float timeoutSeconds;

        // Fade In / Fade Out Support
        bool fadeIn = false;
        float fadeInDuration = 0.0f;
        std::chrono::steady_clock::time_point fadeInStartTime;
        // DirectSound buffer tracking
        LPDIRECTSOUNDBUFFER buffer = nullptr;
    };

    class SoundManager {
    public:
        SoundManager();
        ~SoundManager();

        bool Initialize(HWND hwnd);
        void CleanUp();

        bool LoadSFXFile(const std::wstring& filename, LoadedSFX& outSfx);
        bool ParseWaveFile(const BYTE* data, size_t size, LoadedSFX& result);
        void LoadAllSFX();

        void PlayImmediateSFX(SFX_ID id);
        void PlayQueueList();

        // AddToQueue with optional priority + fade
        void AddToQueue(SFX_ID id, float volume = 1.0f, StereoBalance balance = StereoBalance::BALANCE_CENTER,
            PlaybackType type = PlaybackType::pbtSFX_Once, float timeout = 0.0f);
        void AddToQueue(SFX_ID id, float volume, StereoBalance balance, PlaybackType type,
            float timeout, SFX_PRIORITY priority, bool useFadeIn = false);

        // Volume + Cooldown
        void SetGlobalVolume(float volume);
        void SetCooldown(SFX_ID id, float seconds);
        void ClearCooldown(SFX_ID id);
        void UpdateFadeInVolumes();

        // ASync Thread Management
        void StartPlaybackThread();
        void StopPlaybackThread();

        // Public access to preloaded sounds
        std::unordered_map<SFX_ID, LoadedSFX> fileList;

    private:

        LPDIRECTSOUND8 m_directSound = nullptr;
        LPDIRECTSOUNDBUFFER m_primaryBuffer = nullptr;

        std::mutex m_queueMutex;
        std::vector<SoundQueueItem> m_soundQueue;

        std::unordered_map<SFX_ID, float> m_sfxCooldown;
        std::unordered_map<SFX_ID, std::chrono::steady_clock::time_point> m_lastPlayedTime;

        float m_globalVolume = 1.0f;

        bool m_initialized = false;
        bool m_cleanupDone = false;
        bool m_terminationFlag = false;

        std::thread m_workerThread;

        const std::unordered_map<SFX_ID, std::wstring> sfxFileNames = {
            { SFX_ID::SFX_CLICK,    L"./Assets/click1.wav" },
            { SFX_ID::SFX_BEEP,   L"./Assets/beep1.wav" },
//            { SFX_ID::SFX_ALERT,   L"./Assets/alert.wav" },
//            { SFX_ID::SFX_CANCEL,  L"./Assets/cancel.wav" }
        };
    };

} // namespace SoundSystem
