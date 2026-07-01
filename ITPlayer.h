#pragma once

#include "Debug.h"

#define IT_BUFFER_SIZE (44100 * 2 * 2)

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
struct ITHeader {
    char magic[4];
    char songName[26];
    uint8_t highlightMinor;
    uint8_t highlightMajor;
    uint16_t orderCount;
    uint16_t instrumentCount;
    uint16_t sampleCount;
    uint16_t patternCount;
    uint16_t createdWithTracker;
    uint16_t compatibleWithTracker;
    uint16_t flags;
    uint16_t special;
    uint8_t globalVolume;
    uint8_t mixVolume;
    uint8_t initialSpeed;
    uint8_t initialTempo;
    uint8_t panningSeparation;
    uint8_t pitchWheelDepth;
    uint16_t messageLength;
    uint32_t messageOffset;
    uint8_t reserved[4];
    uint8_t channelPan[64];
    uint8_t channelVolume[64];
};

struct ITSampleHeader {
    char magic[4];          // 0x00 "IMPS"
    char dosName[12];       // 0x04 8.3 DOS filename
    uint8_t reserved;       // 0x10 reserved byte required by IT spec (always 0)
    uint8_t globalVolume;   // 0x11
    uint8_t flags;          // 0x12
    uint8_t defaultVolume;  // 0x13
    char sampleName[26];    // 0x14
    uint8_t convert;        // 0x2E
    uint8_t defaultPanning; // 0x2F
    uint32_t length;        // 0x30
    uint32_t loopStart;     // 0x34
    uint32_t loopEnd;       // 0x38
    uint32_t c5Speed;       // 0x3C
    uint32_t sustainLoopStart; // 0x40
    uint32_t sustainLoopEnd;   // 0x44
    uint32_t samplePointer; // 0x48
    uint8_t vibratoSpeed;   // 0x4C
    uint8_t vibratoDepth;   // 0x4D
    uint8_t vibratoRate;    // 0x4E
    uint8_t vibratoType;    // 0x4F
};
#pragma pack(pop)

// IT format guarantees exact header sizes; these will catch any future struct drift.
static_assert(sizeof(ITHeader)       == 192, "ITHeader must be exactly 192 bytes");
static_assert(sizeof(ITSampleHeader) ==  80, "ITSampleHeader must be exactly 80 bytes");

struct ITSample {
    uint32_t length = 0;
    uint32_t loopStart = 0;
    uint32_t loopEnd = 0;
    uint32_t sustainLoopStart = 0;
    uint32_t sustainLoopEnd = 0;
    uint32_t c5Speed = 8363;
    uint8_t globalVolume = 64;
    uint8_t defaultVolume = 64;
    uint8_t defaultPanning = 128;
    uint8_t flags = 0;
    uint8_t convert = 0;
    char name[26] = {};
    std::vector<int16_t> pcm;

    bool IsLooped() const { return (flags & 0x10) != 0 && loopEnd > loopStart + 1; }
    bool IsSustainLooped() const { return (flags & 0x20) != 0 && sustainLoopEnd > sustainLoopStart + 1; }
    bool Is16Bit() const { return (flags & 0x02) != 0; }
    bool IsStereo() const { return (flags & 0x04) != 0; }
};

struct ITInstrument {
    char name[26] = {};
    std::array<uint8_t, 120> noteMap{};
    std::array<uint8_t, 120> sampleMap{};
    uint16_t fadeOut = 0;
    uint8_t nna = 0;
    uint8_t duplicateCheckType = 0;
    uint8_t duplicateCheckAction = 0;
    uint8_t globalVolume = 64;
    uint8_t defaultPanning = 128;
    uint8_t volumeEnvelopeFlags = 0;
    uint8_t volumeEnvelopePointCount = 0;
    uint8_t volumeEnvelopeLoopStart = 0;
    uint8_t volumeEnvelopeLoopEnd = 0;
    uint8_t volumeEnvelopeSustainStart = 0;
    uint8_t volumeEnvelopeSustainEnd = 0;
    uint8_t panningEnvelopeFlags = 0;
    uint8_t panningEnvelopePointCount = 0;
    uint8_t panningEnvelopeLoopStart = 0;
    uint8_t panningEnvelopeLoopEnd = 0;
    uint8_t panningEnvelopeSustainStart = 0;
    uint8_t panningEnvelopeSustainEnd = 0;
    std::array<uint8_t, 25> volumeEnvelopeValues{};
    std::array<uint16_t, 25> volumeEnvelopeTicks{};
    std::array<int8_t, 25> panningEnvelopeValues{};
    std::array<uint16_t, 25> panningEnvelopeTicks{};

    bool HasVolumeEnvelope() const { return (volumeEnvelopeFlags & 0x01) != 0 && volumeEnvelopePointCount > 0; }
    bool HasPanningEnvelope() const { return (panningEnvelopeFlags & 0x01) != 0 && panningEnvelopePointCount > 0; }
};

struct ITEvent {
    uint8_t note = 0;
    uint8_t instrument = 0;
    uint8_t volume = 255;
    uint8_t effect = 0;
    uint8_t effectData = 0;
};

struct ITPattern {
    uint16_t packedLength = 0;
    uint16_t rows = 64;
    std::vector<uint8_t> data;
};

struct ITChannelVoice {
    const ITSample* sample = nullptr;
    const ITInstrument* instrumentDef = nullptr;
    double position = 0.0;
    double step = 0.0;
    double baseStep = 0.0;
    double targetStep = 0.0;
    float envelopeVolume = 1.0f;
    float envelopePanning = 0.0f;
    uint32_t fadeOutVolume = 65536;
    uint16_t envelopeTick = 0;
    uint16_t panEnvelopeTick = 0;
    int16_t activeVoiceIndex = -1;
    uint8_t hostChannel = 0;
    bool background = false;
    uint8_t volume = 64;
    uint8_t baseVolume = 64;
    uint8_t channelVolume = 64;
    uint8_t panning = 128;
    uint8_t note = 0;
    uint8_t instrument = 0;
    uint8_t effect = 0;
    uint8_t effectData = 0;
    uint8_t lastVolumeSlide = 0;
    uint8_t lastPortamento = 0;
    uint8_t lastVibrato = 0;
    uint8_t lastTremolo = 0;
    uint8_t lastSampleOffsetHigh = 0;
    uint16_t lastSampleOffset = 0;
    uint8_t lastRetrig = 0;
    uint8_t lastPanningSlide = 0;
    uint8_t lastChannelVolumeSlide = 0;
    uint8_t lastGlobalVolumeSlide = 0;
    bool glissandoControl = false;
    uint8_t vibratoPos = 0;
    uint8_t tremoloPos = 0;
    uint8_t panbrelloPos = 0;
    uint8_t vibratoWaveform = 0;   // S3x: 0 sine, 1 ramp-down, 2 square, 3 random
    uint8_t tremoloWaveform = 0;   // S4x
    uint8_t panbrelloWaveform = 0; // S5x
    uint8_t volCommand = 0;        // decoded IT volume-column command (see VolColumnCmd)
    uint8_t volParam = 0;          // parameter for the volume-column command
    bool filterEnabled = false;
    float filterCutoff = 1.0f;
    float filterResonance = 0.0f;
    float filterState = 0.0f;
    uint8_t tremorCounter = 0;
    uint8_t tremorOnTicks = 0;
    uint8_t tremorOffTicks = 0;
    uint8_t noteCutTick = 255;
    bool surround = false;
    bool reversePlayback = false;
    bool active = false;
    bool noteReleased = false;
    bool delayedNotePending = false;
    uint8_t delayTicks = 0;
    ITEvent delayedEvent{};
};

class Debug;

class ITPlayer {
public:
    bool Initialize(const std::wstring& filename);
    bool LoadITFile(const std::wstring& filename);
    bool LoadInstruments(std::ifstream& file, const std::vector<uint32_t>& instrumentPointers);
    bool LoadSamples(std::ifstream& file, const std::vector<uint32_t>& samplePointers);
    bool LoadPatterns(std::ifstream& file, const std::vector<uint32_t>& patternPointers);
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
    uint8_t patternLoopRow = 0;
    uint8_t patternLoopCount = 0;
    uint8_t patternDelayTicks = 0;
    uint8_t fineDelayTicks = 0;       // S6x: extra ticks added to the current row only
    bool rowBreakPending = false;
    bool orderJumpPending = false;
    bool patternLoopPending = false;  // SBx: replay from patternLoopRow without advancing
    bool inPatternDelay = false;      // SEx: guards against re-arming the delay every hold tick

    ITHeader header{};
    std::vector<uint8_t> orders;
    std::vector<ITInstrument> instruments;
    std::vector<ITSample> samples;
    std::vector<ITPattern> patterns;
    std::vector<std::vector<std::vector<ITEvent>>> unpackedPatterns;
    std::vector<ITChannelVoice> hostChannels;
    std::vector<ITChannelVoice> voices;
    std::vector<uint8_t> defaultPanning;
    std::vector<int16_t> mixScratch;
    std::vector<int32_t> mixAccumulator;
    std::wstring modulePath;

    std::atomic<uint8_t> currentVolume{ 64 };
    std::atomic<uint8_t> targetVolume{ 64 };
    std::atomic<uint32_t> fadeDurationMs{ 0 };
    std::atomic<uint32_t> fadeElapsedMs{ 0 };
    std::atomic<uint8_t> globalVolume{ 64 };
    std::atomic<uint8_t> moduleGlobalVolume{ 128 };
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
    void TriggerEvent(size_t channel, const ITEvent& event, bool fromDelay);
    void ApplyTickEffects();
    void ApplyVolumeSlide(ITChannelVoice& voice);
    void ApplyChannelVolumeSlide(ITChannelVoice& voice);
    void ApplyPanningSlide(ITChannelVoice& voice);
    void ApplyVolumeColumnTick(ITChannelVoice& voice);
    void ApplyGlobalVolumeSlide(ITChannelVoice& voice);
    void ApplyPortamento(ITChannelVoice& voice);
    void ApplyVibrato(ITChannelVoice& voice, bool fine);
    void ApplyTremolo(ITChannelVoice& voice);
    void ApplyPanbrello(ITChannelVoice& voice);
    void ApplyRetrig(ITChannelVoice& voice);
    void ApplySpecialCommand(ITChannelVoice& voice, uint8_t data, bool rowTick);
    float EvaluateVolumeEnvelope(const ITInstrument& instrument, uint16_t envelopeTick) const;
    float EvaluatePanningEnvelope(const ITInstrument& instrument, uint16_t envelopeTick) const;
    void AdvanceVoiceEnvelope(ITChannelVoice& voice);
    ITChannelVoice* ActiveVoiceForHost(size_t channel);
    ITChannelVoice& AllocateVoice(size_t channel);
    void ApplyNewNoteAction(ITChannelVoice& voice, uint8_t nna);
    void ApplyDuplicateCheck(const ITInstrument& instrument, uint8_t note, size_t sampleIndex);
    void AdvanceRow();
    const ITSample* ResolveSample(uint8_t instrument, uint8_t note, uint8_t& mappedNote) const;
    double NoteToFrequency(uint8_t note, uint32_t c5Speed) const;
    double Waveform(uint8_t pos, uint8_t type) const;
    uint8_t DefaultPanningForChannel(size_t channel) const;
};
