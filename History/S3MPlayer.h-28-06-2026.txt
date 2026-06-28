#pragma once

#include "Debug.h"

#define S3M_BUFFER_SIZE (44100 * 2 * 2)

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

#pragma pack(push, 1)
struct S3MHeader {
    char songName[28];
    uint8_t signature;
    uint8_t type;
    uint16_t reserved0;
    uint16_t orderCount;
    uint16_t instrumentCount;
    uint16_t patternCount;
    uint16_t flags;
    uint16_t trackerVersion;
    uint16_t sampleType;
    char magic[4];
    uint8_t globalVolume;
    uint8_t initialSpeed;
    uint8_t initialTempo;
    uint8_t masterVolume;
    uint8_t ultraClickRemoval;
    uint8_t defaultPanFlag;
    uint8_t reserved1[8];
    uint16_t special;
    uint8_t channelSettings[32];
};

struct S3MSampleHeader {
    uint8_t type;
    char dosName[12];
    uint8_t dataPointerHighByte;
    uint16_t dataPointerLowWord;
    uint32_t length;
    uint32_t loopStart;
    uint32_t loopEnd;
    uint8_t volume;
    uint8_t reserved;
    uint8_t packing;
    uint8_t flags;
    uint32_t c2Speed;
    uint8_t internal[12];
    char sampleName[28];
    char magic[4];
};
#pragma pack(pop)

struct S3MSample {
    uint8_t type = 0;
    uint32_t length = 0;
    uint32_t loopStart = 0;
    uint32_t loopEnd = 0;
    uint8_t volume = 0;
    uint8_t flags = 0;
    uint32_t c2Speed = 8363;
    bool isAdLib = false;
    std::array<uint8_t, 12> adLibRegisters{};
    char name[28] = {};
    std::vector<int16_t> pcm;

    bool IsLooped() const { return (flags & 0x01) != 0 && loopEnd > loopStart + 1; }
};

struct S3MEvent {
    uint8_t note = 255;
    uint8_t instrument = 0;
    uint8_t volume = 255;
    uint8_t effect = 0;
    uint8_t effectData = 0;
};

struct S3MPattern {
    uint16_t packedLength = 0;
    std::vector<uint8_t> data;
};

struct S3MChannelVoice {
    const S3MSample* sample = nullptr;
    double position = 0.0;
    double phase = 0.0;
    double step = 0.0;
    double baseStep = 0.0;
    double targetStep = 0.0;
    uint8_t volume = 64;
    uint8_t baseVolume = 64;
    uint8_t channelVolume = 64;
    uint8_t panning = 128;
    uint8_t basePanning = 128;
    uint8_t note = 255;
    uint8_t instrument = 0;
    uint8_t effect = 0;
    uint8_t effectData = 0;
    uint8_t lastVolumeSlide = 0;
    uint8_t lastPortamento = 0;
    uint8_t lastVibrato = 0;
    uint8_t lastTremolo = 0;
    uint8_t lastSampleOffset = 0;
    uint8_t lastPanningSlide = 0;
    uint8_t lastPanbrello = 0;
    uint8_t lastRetrig = 0;
    uint8_t lastChannelVolumeSlide = 0;
    uint8_t lastGlobalVolumeSlide = 0;
    uint8_t vibratoWaveform = 0;
    uint8_t tremoloWaveform = 0;
    uint8_t panbrelloWaveform = 0;
    uint8_t vibratoPos = 0;
    uint8_t tremoloPos = 0;
    uint8_t panbrelloPos = 0;
    uint8_t tremorCounter = 0;
    uint8_t tremorOnTicks = 0;
    uint8_t tremorOffTicks = 0;
    uint8_t adLibEnvelopeStage = 0;
    float adLibEnvelope = 0.0f;
    uint8_t adLibModEnvelopeStage = 0;  // modulator operator ADSR stage
    float adLibModEnvelope = 0.0f;      // modulator operator envelope amplitude
    float adLibLastMod = 0.0f;          // previous modulator output, used for OPL2 self-feedback
    bool active = false;
    bool delayedNotePending = false;
    uint8_t delayTicks = 0;
    S3MEvent delayedEvent{};
};

class Debug;

class S3MPlayer {
public:
    bool Initialize(const std::wstring& filename);
    bool LoadS3MFile(const std::wstring& filename);
    bool LoadSamples(std::ifstream& file, const std::vector<uint16_t>& instrumentParaPointers);
    bool LoadPatterns(std::ifstream& file, const std::vector<uint16_t>& patternParaPointers);
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
    uint16_t sequencePosition = 0;
    uint16_t currentPatternIndex = 0;
    uint16_t currentRow = 0;
    uint16_t nextRowOverride = 0;
    uint16_t tick = 0;
    uint16_t speed = 6;
    uint16_t tempo = 125;
    bool rowBreakPending = false;
    bool orderJumpPending = false;
    bool patternLoopJumpPending = false;
    uint16_t rowDurationTicks = 6;
    std::array<uint16_t, 32> patternLoopRow{};
    std::array<int16_t, 32> patternLoopRemaining{};

    S3MHeader s3mHeader{};
    std::vector<uint8_t> orders;
    std::vector<S3MSample> samples;
    std::vector<S3MPattern> patterns;
    std::vector<std::vector<std::vector<S3MEvent>>> unpackedPatterns;
    std::vector<S3MChannelVoice> voices;
    std::vector<bool> channelEnabled;
    std::vector<uint8_t> defaultPanning;
    std::vector<int16_t> mixScratch;

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
    void UnpackPatterns();
    void TickRow();
    void TriggerEvent(size_t channel, const S3MEvent& event, bool fromDelay);
    void ApplyTickEffects();
    void ApplyVolumeSlide(S3MChannelVoice& voice);
    void ApplyPortamento(S3MChannelVoice& voice);
    void ApplyVibrato(S3MChannelVoice& voice);
    void ApplyTremolo(S3MChannelVoice& voice);
    void ApplyPanbrello(S3MChannelVoice& voice);
    void ApplyRetrig(S3MChannelVoice& voice);
    void AdvanceRow();
    double NoteToFrequency(uint8_t note, uint32_t c2Speed) const;
    double VibratoTable(uint8_t pos) const;
    uint8_t DefaultPanningForChannel(size_t channel) const;
};
