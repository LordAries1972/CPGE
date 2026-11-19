// SoundManager.h - DirectSound-based SFX Sound Manager (replaces XAudio2)
#pragma once

#include "Includes.h"

// dsound.h must be included before any LPDIRECTSOUNDBUFFER member declaration.
// Guard on PLATFORM_WINDOWS so the header remains compilable on Linux/Android.
#if defined(PLATFORM_WINDOWS)
    #include <dsound.h>
    #pragma comment(lib, "dsound.lib")
    #pragma comment(lib, "dxguid.lib")
#endif

#pragma warning(push)
#pragma warning(disable: 4101)

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
        SFX_VOICE1 = 3,
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
#if defined(PLATFORM_WINDOWS)
        // DirectSound buffer tracking (Windows — used by all renderers on this platform)
        LPDIRECTSOUNDBUFFER buffer = nullptr;
#else
        void* buffer = nullptr;
#endif
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

        // ---- Sound bank -----------------------------------------------------------------------------
        // For games with many effects: sounds are registered ONCE at run time under an integer slot (the SFX_ID list above is fixed and
        // queues a fresh copy of the sample for every play).  A bank sound owns `voices` DirectSound buffers that share one copy of the
        // sample data, so playing it is just Stop / rewind / Play - no allocation.  Every bank sound follows the global volume LIVE:
        // SetGlobalVolume() re-applies it to voices that are already playing.
        //   RegisterBankSound : loads `file` (a missing file returns false quietly).  minGapMs throttles rapid retriggers, maxPlayMs cuts a
        //                       long sample short (0 = play it all), gain scales this sound against the others.
        //   PlayBankSound     : plays on a free voice, else steals the oldest.   volume 0..1.
        //   PlayBankLoop      : starts a looping sound unless it is already playing (cheap enough to call every frame).
        //   SetBankLoopGain   : changes the level of a sound that is already playing (a live throb or engine rev).
        //   StopBankSound / StopAllBankSounds : silence one slot / everything.
        bool RegisterBankSound(int slot, const std::wstring& file, int voices = 1, unsigned minGapMs = 0, unsigned maxPlayMs = 0, float gain = 1.0f);
        bool IsBankSoundLoaded(int slot);
        void PlayBankSound(int slot, float volume = 1.0f);
        void PlayBankLoop(int slot, float volume = 1.0f);
        void SetBankLoopGain(int slot, float volume);
        void StopBankSound(int slot);
        void StopAllBankSounds();

        // ---- Background loading of a whole bank -----------------------------------------------------------------------------------
        // Registering many effects (a game has ~100 wavs, each decoded and duplicated into DirectSound buffers) takes a second or more: done on the game thread it freezes a
        // frame and the first shots are lost.  RegisterBankSoundsAsync hands the list to ONE worker thread and returns at once; the thread calls RegisterBankSound for each
        // entry (a missing file is skipped quietly).  PlayBankSound / PlayBankLoop on a slot that is not loaded yet already do nothing, so a game can simply start playing and
        // the effects come alive as they finish; poll IsBankLoadingDone() if you want to know when everything is in.  A second call waits for the first batch to finish.
        // CleanUp() stops the worker (it abandons the rest of the list) and joins it before the DirectSound device is released.
        struct BankSoundDef {
            int slot = 0;
            std::wstring file;
            int voices = 1;
            unsigned minGapMs = 0;
            unsigned maxPlayMs = 0;
            float gain = 1.0f;
        };
        void RegisterBankSoundsAsync(std::vector<BankSoundDef> defs);
        bool IsBankLoadingDone() const { return !m_bankLoading.load(std::memory_order_acquire); }   // true when no batch is loading (also when none was ever started)
        void WaitForBankLoad();                                 // blocks until the current batch is in (use at shut-down or a loading screen, not in the frame loop)
        void UpdateBankSounds();                                // cuts samples that ran past maxPlayMs; the playback thread calls it ten times a second

        // Volume + Cooldown
        void SetGlobalVolume(float volume);
        // AMBIENT volume (0..1): the level of the LOOPING bank sounds - the background sirens, buzzes and engines (PlayBankLoop).  It scales them on top of the global volume and,
        // like it, takes effect at once on loops that are already playing.  (The options window's Ambient Volume.)
        void SetAmbientVolume(float volume);
        void SetCooldown(SFX_ID id, float seconds);
        void ClearCooldown(SFX_ID id);
        void UpdateFadeInVolumes();

        // ASync Thread Management
        void StartPlaybackThread();
        void StopPlaybackThread();

        // Public access to preloaded sounds
        std::unordered_map<SFX_ID, LoadedSFX> fileList;

    private:

#if defined(PLATFORM_WINDOWS)
        LPDIRECTSOUND8      m_directSound   = nullptr;
        LPDIRECTSOUNDBUFFER m_primaryBuffer = nullptr;
#else
        void* m_directSound   = nullptr;
        void* m_primaryBuffer = nullptr;
#endif

        std::mutex m_queueMutex;
        std::vector<SoundQueueItem> m_soundQueue;

        std::unordered_map<SFX_ID, float> m_sfxCooldown;
        std::unordered_map<SFX_ID, std::chrono::steady_clock::time_point> m_lastPlayedTime;

        // Sound bank storage (see RegisterBankSound).  `level` is volume * the sound's gain BEFORE the global volume, kept so SetGlobalVolume can re-apply it.
        struct BankVoice {
#if defined(PLATFORM_WINDOWS)
            LPDIRECTSOUNDBUFFER buf = nullptr;
#else
            void* buf = nullptr;
#endif
            unsigned long long started = 0;                     // GetTickCount64() when it began; 0 = idle
            float level = 0.0f;
            float appliedGain = -1.0f;                         // last successful effective gain; avoids repeated log/driver work
            bool loop = false;                                  // started by PlayBankLoop: follows the AMBIENT volume as well
        };
        struct BankSound {
            std::vector<BankVoice> voices;
            unsigned long long lastPlayed = 0;
            unsigned minGapMs = 0, maxPlayMs = 0;
            float gain = 1.0f;
            bool ok = false;
        };
        std::vector<BankSound> m_bank;
        std::thread m_bankLoader;                               // RegisterBankSoundsAsync's worker
        std::atomic<bool> m_bankLoading{ false };               // a batch is being loaded
        std::atomic<bool> m_bankAbort{ false };                 // CleanUp asks the worker to stop
        std::mutex m_bankMutex;                                 // the game thread plays, the options window changes volume, the playback thread cuts samples
        void ApplyBankVoiceVolume(BankVoice& v);                // bank mutex must be held
        void ReleaseBankSound(BankSound& b);                    // bank mutex must be held

        std::atomic<float> m_globalVolume{ 1.0f };              // atomic: the options window sets it while other threads play
        std::atomic<float> m_ambientVolume{ 1.0f };             // extra scale for looping (ambient) bank sounds

        bool m_initialized = false;
        bool m_cleanupDone = false;
        bool m_terminationFlag = false;

        std::thread m_workerThread;

        const std::unordered_map<SFX_ID, std::wstring> sfxFileNames = {
            { SFX_ID::SFX_CLICK,    L"./Assets/click1.wav" },
            { SFX_ID::SFX_BEEP,   L"./Assets/beep1.wav" },
            { SFX_ID::SFX_VOICE1,   L"./Assets/voice1.wav" },
//            { SFX_ID::SFX_ALERT,   L"./Assets/alert.wav" },
//            { SFX_ID::SFX_CANCEL,  L"./Assets/cancel.wav" }
        };
    };

} // namespace SoundSystem

#pragma warning(pop)