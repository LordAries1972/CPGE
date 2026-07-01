#pragma once

#include "Debug.h"

#define MOD_BUFFER_SIZE (44100 * 2 * 2)

#pragma comment(lib, "Winmm.lib")

#if defined(PLATFORM_WINDOWS)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
    #include <dsound.h>
    #pragma comment(lib, "dsound.lib")
    #pragma comment(lib, "dxguid.lib")
#endif

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct MODSample {
    char name[22] = {};
    uint32_t length = 0;
    uint8_t finetune = 0;
    uint8_t volume = 64;
    uint32_t loopStart = 0;
    uint32_t loopLength = 0;
    std::vector<int16_t> pcm;

    bool IsLooped() const { return loopLength > 2; }
};

struct MODEvent {
    uint16_t period = 0;
    uint8_t sample = 0;
    uint8_t effect = 0;
    uint8_t effectData = 0;
};

struct MODPattern {
    std::vector<std::array<MODEvent, 4>> rows;
};

struct MODChannelVoice {
    const MODSample* sample = nullptr;
    double position = 0.0;
    double step = 0.0;
    double baseStep = 0.0;
    double targetStep = 0.0;
    uint16_t period = 0;
    uint16_t targetPeriod = 0;
    uint8_t sampleIndex = 0;
    uint8_t volume = 64;
    uint8_t baseVolume = 64;
    uint8_t panning = 128;
    uint8_t effect = 0;
    uint8_t effectData = 0;
    uint8_t lastVolumeSlide = 0;
    uint8_t lastPortamento = 0;
    uint8_t lastVibrato = 0;
    uint8_t lastTremolo = 0;
    uint8_t lastSampleOffset = 0;
    uint8_t vibratoWaveform = 0;
    uint8_t tremoloWaveform = 0;
    uint8_t vibratoPos = 0;
    uint8_t tremoloPos = 0;
    uint8_t retrigTick = 0;
    bool active = false;
    bool delayedNotePending = false;
    uint8_t delayTicks = 0;
    MODEvent delayedEvent{};
};

class Debug;

class MODPlayer {
public:
    bool Initialize(const std::wstring& filename);
    bool LoadMODFile(const std::wstring& filename);
    void PlaybackLoop();
    void Shutdown();

    bool Play(const std::wstring& path);
    bool Play(const std::string& path);
    bool IsPlaying() const;
    bool IsPaused() const;
    void Stop();
    void Pause();
    void HardPause();
    void Terminate();
    void Resume();
    void HardResume();
    void Mute();
    void SetVolume(uint8_t volume);
    void SetGlobalVolume(uint8_t volume);
    void SetFadeIn(uint32_t durationMs);
    void SetFadeOut(uint32_t durationMs);
    void GotoSequenceID(uint16_t patternSeqID);

private:
    bool bIsInitialized = false;
    char songName[20] = {};
    std::array<MODSample, 31> samples{};
    std::array<uint8_t, 128> orders{};
    std::vector<MODPattern> patterns;
    std::array<MODChannelVoice, 4> voices{};
    std::vector<int16_t> mixScratch;

    uint8_t songLength = 0;
    uint8_t restartPosition = 0;
    uint16_t sequencePosition = 0;
    uint16_t currentPatternIndex = 0;
    uint16_t currentRow = 0;
    uint16_t nextRowOverride = 0;
    uint16_t tick = 0;
    uint16_t speed = 6;
    uint16_t tempo = 125;
    uint16_t rowDurationTicks = 6;
    uint8_t patternLoopRow = 0;
    uint8_t patternLoopCount = 0;
    bool rowBreakPending = false;
    bool orderJumpPending = false;
    bool patternLoopPending = false;

    std::atomic<uint8_t> currentVolume{ 64 };
    std::atomic<uint8_t> targetVolume{ 64 };
    std::atomic<uint32_t> fadeDurationMs{ 0 };
    std::atomic<uint32_t> fadeElapsedMs{ 0 };
    std::atomic<uint8_t> globalVolume{ 64 };
    std::atomic<uint8_t> moduleGlobalVolume{ 64 };
    std::atomic<uint16_t> restartSequencePosition{ 0 };
    std::atomic<bool> fadeInActive{ false };
    std::atomic<bool> fadeOutActive{ false };

    std::thread playbackThread;
    std::atomic<bool> isPlaying{ false };
    std::atomic<bool> isPaused{ false };
    std::atomic<bool> isTerminating{ false };
    std::atomic<bool> isMuted{ false };
    std::mutex playbackMutex;

#if defined(PLATFORM_WINDOWS)
    LPDIRECTSOUND8 directSound = nullptr;
    LPDIRECTSOUNDBUFFER primaryBuffer = nullptr;
    LPDIRECTSOUNDBUFFER secondaryBuffer = nullptr;
#else
    void* directSound = nullptr;
    void* primaryBuffer = nullptr;
    void* secondaryBuffer = nullptr;
#endif
    uint32_t writeCursor = 0;
    uint32_t bufferSize = 0;

    bool CreateAudioDevice();
    void FillAudioBuffer();
    void MixAudio(int16_t* buffer, size_t samplesToMix);
    void SilenceBuffer();
    void TickRow();
    void TriggerEvent(size_t channel, const MODEvent& event, bool fromDelay);
    void ApplyTickEffects();
    void ApplyVolumeSlide(MODChannelVoice& voice);
    void ApplyPortamento(MODChannelVoice& voice);
    void ApplyVibrato(MODChannelVoice& voice);
    void ApplyTremolo(MODChannelVoice& voice);
    void ApplyRetrig(MODChannelVoice& voice, uint8_t interval);
    void AdvanceRow();
    double PeriodToFrequency(uint16_t period, uint8_t finetune) const;
    double PeriodToStep(uint16_t period, const MODSample& sample) const;
    uint16_t ClampPeriod(int period) const;
    double TrackerWaveform(uint8_t waveform, uint8_t position) const;
    uint8_t DefaultPanningForChannel(size_t channel) const;
};
