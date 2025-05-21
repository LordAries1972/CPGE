#pragma once

#include "Debug.h"  // Ensure Debug.h is included

#define BUFFER_SIZE (44100 * 2 * 2)

#pragma comment(lib, "Winmm.lib")

#pragma pack(push, 1)
struct XMSampleHeader {
    uint32_t length;
    uint32_t loopStart;
    uint32_t loopLength;
    uint8_t volume;
    int8_t fineTune;
    uint8_t type;
    uint8_t panning;
    int8_t relativeNoteNumber;
    uint8_t reserved;
    char sampleName[22];
};
#pragma pack(pop)

#pragma pack(push, 1)
struct XMInstrumentHeader {
    uint32_t headerSize = 0;                // Offset 0: size of this header
    char instrumentName[22] = {};           // Offset 4: instrument name (ASCII, null-padded)
    uint8_t instrumentType = 0;             // Offset 26: always 0
    uint16_t numSamples = 0;                // Offset 27: number of samples

    uint32_t sampleHeaderSize = 0;          // Offset 29: size of each sample header
    uint8_t sampleNoteNumber[96] = {};      // Offset 33: note sample map

    uint16_t volumeEnvelope[24] = {};       // Offset 129: 12 points (x,y) pairs
    uint16_t panningEnvelope[24] = {};      // Offset 177: 12 points (x,y) pairs

    uint8_t numVolumePoints = 0;            // Offset 225
    uint8_t numPanningPoints = 0;           // Offset 226
    uint8_t volumeSustainPoint = 0;         // Offset 227
    uint8_t volumeLoopStartPoint = 0;       // Offset 228
    uint8_t volumeLoopEndPoint = 0;         // Offset 229
    uint8_t panningSustainPoint = 0;        // Offset 230
    uint8_t panningLoopStartPoint = 0;      // Offset 231
    uint8_t panningLoopEndPoint = 0;        // Offset 232

    uint8_t volumeType = 0;                 // Offset 233
    uint8_t panningType = 0;                // Offset 234
    uint8_t vibratoType = 0;                // Offset 235
    uint8_t vibratoSweep = 0;               // Offset 236
    uint8_t vibratoDepth = 0;               // Offset 237
    uint8_t vibratoRate = 0;                // Offset 238

    uint16_t volumeFadeout = 0;             // Offset 239–240 (correct!)
    uint16_t reserved = 0;                  // Offset 241–242 (reserved, pad to 243 bytes)
};
#pragma pack(pop)

#pragma pack(push, 1)
struct XMPatternHeader {
    uint32_t headerSize;
    uint8_t packingType;
    uint16_t numRows;
    uint16_t dataSize;
};
#pragma pack(pop)

#pragma pack(push, 1)
struct XMHeader {
    char idText[17];           // "Extended Module: "
    char moduleName[20];       // padded with nulls
    uint8_t signature;         // 0x1A
    char trackerName[20];      // padded with nulls
    uint16_t version;          // minor * 256 + major
    uint32_t headerSize;       // size of rest of header
    uint16_t songLength;
    uint16_t restartPosition;
    uint16_t numChannels;
    uint16_t numPatterns;
    uint16_t numInstruments;
    uint16_t flags;
    uint16_t defaultTempo;
    uint16_t defaultBPM;
    uint8_t patternOrderTable[256];
};
#pragma pack(pop)

struct XMSample {
    uint32_t length = 0;
    uint32_t loopStart = 0;
    uint32_t loopLength = 0;
    uint8_t volume = 0;
    int8_t finetune = 0;
    uint8_t type = 0;
    uint8_t panning = 0;
    int8_t relativeNote = 0;
    char name[22] = { 0 };

    std::vector<uint8_t> sampleData;     // Raw delta-encoded sample
    std::vector<int8_t> decoded8;        // Unpacked 8-bit
    std::vector<int16_t> decoded16;      // Unpacked 16-bit
};

struct XMInstrument {
    XMInstrumentHeader header;
    std::vector<XMSample> samples;
};

struct XMPattern {
    XMPatternHeader header{};
    std::vector<uint8_t> data;
};

struct XMEvent {
    uint8_t note = 0;
    uint8_t instrument = 0;
    uint8_t volume = 0;
    uint8_t effect = 0;
    uint8_t effectData = 0;
};

struct ChannelVoice {
    const XMSample* sample = nullptr;           // Pointer to the current sample
    float position = 0.0f;                      // Playback position (in fractional samples)
    float step = 0.0f;                          // Step size per output sample (frequency)
    uint8_t volume = 64;                        // Playback volume (0–64)
    bool active = false;                        // Is the voice currently active?
    uint16_t envTick = 0;                       // Volume envelope tick counter
    uint32_t panEnvTick = 0;                    // Panning envelope tick counter (*** FIXED ***)
    uint8_t baseVolume = 64;                    // Base volume used in MixAudio
    const XMInstrument* instrument = nullptr;   // Pointer to the associated instrument
    uint8_t effect = 0;                         // Last effect command
    uint8_t effectData = 0;                     // Last effect data
    uint8_t note = 0;                           // Base note index
    uint8_t retrigTick = 0;                     // Retrigger tick for E9x
    bool envelopeSustain = false;               // True if sustain is active
    bool envelopeReleased = false;              // True if note release phase is entered
    uint8_t vibratoPos = 0;                     // Vibrato table index
    uint8_t tremoloPos = 0;                     // Tremolo table index
    bool delayedNotePending = false;            // True if EDx is active
    uint8_t delayTicks = 0;                     // Delay countdown for EDx
    XMEvent delayedEvent{};                     // Stored delayed event
    uint8_t panning = 128;                      // Static pan value (0 = Left, 255 = Right)
};

struct Voice {
    bool active;
    XMSample* sample;
    XMInstrument* instrument;
    float position;
    float step;
    uint8_t baseVolume;
    uint32_t envTick;
    bool envelopeReleased;
};

class Debug;

class XMMODPlayer {
public:
    bool Initialize(const std::wstring& filename);
    bool LoadXMFile(const std::wstring& filename);
    bool LoadPatterns(std::ifstream& file);
    bool LoadInstruments(std::ifstream& file);
    void PlaybackLoop();
    void Shutdown();

    bool Play(const std::wstring& path);                        // ONLY use wstring
    // Narrow-string overload (calls wide version)
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

    // The ID to be passed here is the Playbacks pattern sequence ID, 
    // this is very handy if you write multiple tunes within the one XM Module.
    void GotoSequenceID(uint16_t patternSeqID);

private:
    bool bIsInitialized = false;
    // Playback position
    uint16_t sequencePosition = 0;
    uint16_t currentPatternIndex = 0;
    uint16_t currentRow = 0;
    uint16_t tick = 0;
    uint8_t patternDelayTicks = 0;
    uint8_t patternDelayCounter = 0;
    uint16_t tempo = 6; // Default from XM header
    uint16_t bpm = 125;

    // Timing
    std::chrono::steady_clock::time_point lastTickTime;

    XMHeader xmHeader{};
    std::vector<XMInstrument> instruments;
    std::vector<XMPattern> patterns;
    std::vector<std::vector<std::vector<XMEvent>>> unpackedPatterns;    // [pattern][row][channel]
    std::vector<ChannelVoice> voices;                                   // one per channel

    // Fade and volume control
    std::atomic<uint8_t> currentVolume{ 64 };
    std::atomic<uint8_t> targetVolume{ 64 };
    std::atomic<uint32_t> fadeDurationMs{ 0 };
    std::atomic<uint32_t> fadeElapsedMs{ 0 };
    std::atomic<uint8_t> globalVolume{ 64 };                            // New global volume (0-64)
    std::atomic<uint16_t> restartSequencePosition{ 0 };                 // Where to restart playback
    std::atomic<bool> fadeInActive{ false };
    std::atomic<bool> fadeOutActive{ false };

    // playback state, thread, etc.
    std::thread playbackThread;
    std::atomic<bool> isPlaying{ false };
    std::atomic<bool> isPaused{ false };
    std::atomic<bool> isTerminating{ false };
    std::atomic<bool> isMuted{ false };
    std::mutex playbackMutex;

    LPDIRECTSOUND8 directSound = nullptr;
    LPDIRECTSOUNDBUFFER primaryBuffer = nullptr;
    LPDIRECTSOUNDBUFFER secondaryBuffer = nullptr;
    DWORD writeCursor = 0;
    DWORD bufferSize = 0;

    void MixAudio(int16_t* buffer, size_t samples);
    void TickRow();
    void ApplyTickEffects();
    void UnpackSamples();
    float NoteToFrequency(uint8_t note, int8_t finetune, int8_t relativeNote);
    bool CreateAudioDevice();
    void FillAudioBuffer();
	void SilenceBuffer();
    void UnpackPatterns();
    float VibratoTable(uint8_t pos);
    void ApplyTickEffects_VolumeSlide(ChannelVoice& voice);
    void ApplyTickEffects_TonePortamento(ChannelVoice& voice);
    void ApplyTickEffects_Vibrato(ChannelVoice& voice);

};

