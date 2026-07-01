#include "Includes.h"
#include "MPTMPlayer.h"
#include "Debug.h"
#include "Configuration.h"

#include <filesystem>
#include <limits>

extern Debug debug;
extern Configuration config;

#if defined(PLATFORM_WINDOWS)
    extern HWND hwnd;
    #pragma comment(lib, "dsound.lib")
    #pragma comment(lib, "dxguid.lib")
    #pragma comment(lib, "winmm.lib")
    #pragma comment(lib, "mfplat.lib")
    #pragma comment(lib, "mfreadwrite.lib")
    #pragma comment(lib, "mfuuid.lib")
#endif

namespace {
    constexpr uint32_t MPTM_SAMPLE_RATE = 44100;
    constexpr uint8_t MPTM_MAX_CHANNELS = 127; // MPTM extends IT's 64-channel limit to 127
    constexpr uint8_t MPTM_NOTE_CUT = 254;
    constexpr uint8_t MPTM_NOTE_OFF = 255;
    constexpr uint8_t MPTM_NOTE_FADE = 253;

    uint16_t ReadLE16(const uint8_t* data) {
        return static_cast<uint16_t>(data[0]) |
            (static_cast<uint16_t>(data[1]) << 8);
    }

    uint32_t ReadLE32(const uint8_t* data) {
        return static_cast<uint32_t>(data[0]) |
            (static_cast<uint32_t>(data[1]) << 8) |
            (static_cast<uint32_t>(data[2]) << 16) |
            (static_cast<uint32_t>(data[3]) << 24);
    }

    int16_t ReadLE16Signed(const uint8_t* data) {
        return static_cast<int16_t>(ReadLE16(data));
    }

    uint8_t ClampVolume(int value) {
        return static_cast<uint8_t>(std::clamp(value, 0, 64));
    }

    // IT volume-column commands. The single byte encodes a command and parameter by range.
    enum VolColumnCmd : uint8_t {
        VOLCMD_NONE = 0,
        VOLCMD_VOLUME,        // 0..64   set volume
        VOLCMD_FINEVOLUP,     // 65..74  fine volume slide up   (once, on tick 0)
        VOLCMD_FINEVOLDOWN,   // 75..84  fine volume slide down (once, on tick 0)
        VOLCMD_VOLSLIDEUP,    // 85..94  volume slide up   (per tick)
        VOLCMD_VOLSLIDEDOWN,  // 95..104 volume slide down (per tick)
        VOLCMD_PITCHDOWN,     // 105..114 portamento down (per tick)
        VOLCMD_PITCHUP,       // 115..124 portamento up   (per tick)
        VOLCMD_PANNING,       // 128..192 set panning (0..64)
        VOLCMD_TONEPORTA,     // 193..202 tone portamento (per tick, speed from table)
        VOLCMD_VIBRATO,       // 203..212 vibrato depth   (per tick)
    };

    void DecodeVolumeColumn(uint8_t v, uint8_t& cmd, uint8_t& param) {
        if (v <= 64)              { cmd = VOLCMD_VOLUME;       param = v; }
        else if (v <= 74)         { cmd = VOLCMD_FINEVOLUP;    param = static_cast<uint8_t>(v - 65); }
        else if (v <= 84)         { cmd = VOLCMD_FINEVOLDOWN;  param = static_cast<uint8_t>(v - 75); }
        else if (v <= 94)         { cmd = VOLCMD_VOLSLIDEUP;   param = static_cast<uint8_t>(v - 85); }
        else if (v <= 104)        { cmd = VOLCMD_VOLSLIDEDOWN; param = static_cast<uint8_t>(v - 95); }
        else if (v <= 114)        { cmd = VOLCMD_PITCHDOWN;    param = static_cast<uint8_t>(v - 105); }
        else if (v <= 124)        { cmd = VOLCMD_PITCHUP;      param = static_cast<uint8_t>(v - 115); }
        else if (v >= 128 && v <= 192) { cmd = VOLCMD_PANNING;   param = static_cast<uint8_t>(v - 128); }
        else if (v >= 193 && v <= 202) { cmd = VOLCMD_TONEPORTA; param = static_cast<uint8_t>(v - 193); }
        else if (v >= 203 && v <= 212) { cmd = VOLCMD_VIBRATO;   param = static_cast<uint8_t>(v - 203); }
        else                      { cmd = VOLCMD_NONE; param = 0; }
    }

    // IT 2.14 ITPACK sample decompressor.
    // 8-bit: blocks of 0x8000 samples, initial bit-width 9.
    // 16-bit: blocks of 0x4000 samples, initial bit-width 17.
    // Each block starts with a 16-bit LE byte-count, followed by the bit-stream (LSB first).
    // The output is a running-sum (delta) of signed deltas scaled to int16_t.
    bool DecompressITSample(const uint8_t* src, size_t srcLen,
                            std::vector<int16_t>& pcm, uint32_t numSamples, bool is16bit) {
        const uint8_t* const srcEnd = src + srcLen;
        uint32_t done = 0;

        while (done < numSamples) {
            if (src + 2 > srcEnd) break;
            const uint16_t blockBytes = uint16_t(src[0]) | (uint16_t(src[1]) << 8);
            src += 2;
            const uint8_t* blockEnd = (src + blockBytes <= srcEnd) ? src + blockBytes : srcEnd;

            const uint32_t blockSamples = std::min(numSamples - done,
                                                   is16bit ? uint32_t(0x4000) : uint32_t(0x8000));

            uint32_t bitBuf = 0;
            uint8_t  bitCnt = 0;
            uint8_t  width  = is16bit ? 17u : 9u;
            int32_t  last   = 0;

            // Read n bits LSB-first from the block's bit-stream.
            auto readBits = [&](uint8_t n) -> uint32_t {
                while (bitCnt < n) {
                    bitBuf |= uint32_t(src < blockEnd ? *src++ : uint8_t(0)) << bitCnt;
                    bitCnt += 8;
                }
                const uint32_t val = (n < 32u) ? (bitBuf & ((1u << n) - 1u)) : bitBuf;
                bitBuf >>= n;
                bitCnt  -= n;
                return val;
            };

            for (uint32_t s = 0; s < blockSamples; ) {
                uint32_t v = readBits(width);

                if (is16bit) {
                    if (width < 17u) {
                        const uint32_t center = 1u << (width - 1u);
                        if (v == center) {
                            const uint8_t n = uint8_t(readBits(4));
                            width = (n + 1u <= width) ? n + 1u : n + 2u;
                            if (width > 17u) width = 17u;
                            continue;
                        }
                        if (v >= center) v -= (1u << width);   // sign-extend
                    } else {
                        if (v & 0x10000u) {
                            width = uint8_t(v & 0xFFu) + 1u;
                            if (width > 17u) width = 17u;
                            continue;
                        }
                        if (v >= 0x8000u) v |= ~uint32_t(0xFFFFu);  // sign-extend 16→32
                    }
                    last = (last + int32_t(v)) & 0xFFFF;
                    pcm.push_back(int16_t(uint16_t(uint32_t(last) & 0xFFFFu)));
                } else {
                    if (width < 9u) {
                        const uint32_t center = 1u << (width - 1u);
                        if (v == center) {
                            const uint8_t n = uint8_t(readBits(3));
                            width = (n + 1u <= width) ? n + 1u : n + 2u;
                            if (width > 9u) width = 9u;
                            continue;
                        }
                        if (v >= center) v -= (1u << width);   // sign-extend
                    } else {
                        if (v & 0x100u) {
                            width = uint8_t(v & 0xFFu) + 1u;
                            if (width > 9u) width = 9u;
                            continue;
                        }
                        if (v >= 0x80u) v -= 0x100u;           // sign-extend 8→32
                    }
                    last = (last + int32_t(v)) & 0xFF;
                    pcm.push_back(int16_t(int8_t(uint8_t(uint32_t(last) & 0xFFu))) * int16_t(256));
                }
                ++s;
                ++done;
            }

            src = blockEnd;
        }

        return done == numSamples;
    }

    // IT maps the volume-column tone-portamento parameter (0..9) onto these Gxx speeds.
    constexpr uint8_t VolColumnPortaSpeed[10] = { 0, 1, 4, 8, 16, 32, 64, 96, 128, 255 };

    uint8_t ITNoteToLinear(uint8_t note) {
        // After ResolveSample, all note values are 0-based (0=C-0, 60=C-5) — matching
        // NoteToFrequency's c5Index of 60. Raw IT pattern bytes are 1-based (1=C-0, 61=C-5)
        // and are converted to 0-based inside ResolveSample before reaching here.
        // Do NOT subtract 1 here; mappedNote from ResolveSample is already 0-based.
        if (note == 0 || note >= MPTM_NOTE_FADE) {
            return note;
        }

        return static_cast<uint8_t>(std::min<int>(note, 119));
    }

    // Internal OpenMPT / IT effect byte values. These are enum indices, NOT ASCII display chars.
    enum MptmInternalCmd : uint8_t {
        CMD_MIDI          = 0x1A,   // Zxx: dual-purpose MIDI macro / native resonant filter
        CMD_SMOOTHMIDI    = 0x1B,   // \xx: smooth MIDI macro slide (VST automation only)
        CMD_DELAYCUT      = 0x1C,   // Xxx: note delay+cut (param < 0x80 native) / plugin extension (param >= 0x80)
        CMD_EXTMIDI       = 0x1D,   // #xx: plugin parameter extension byte (OpenMPT VST path only)
        CMD_FINETUNE      = 0x1E,   // qxx: fractional pitch finetune upward (native)
        CMD_FINETUNE_BKWD = 0x1F,   // txx: fractional pitch finetune downward (native)
    };

    bool IsUnsupportedMPTMCommand(uint8_t effect, uint8_t param) {
        // Pure VST automation commands with no native audio equivalent in this engine.
        if (effect == CMD_SMOOTHMIDI || effect == CMD_EXTMIDI) {
            return true;
        }

        // Zxx (CMD_MIDI): dual-purpose resonant filter / VST macro. This engine has no
        // native filter path, so all Zxx variants are treated as no-ops until filters
        // are implemented.
        if (effect == CMD_MIDI) {
            return true;
        }

        // Xxx (CMD_DELAYCUT): param 0x00-0x7F is native note delay+cut; param 0x80-0xFF
        // is an OpenMPT-only plugin parameter extension with no native meaning.
        if (effect == CMD_DELAYCUT && param >= 0x80) {
            return true;
        }

        // CMD_FINETUNE (qxx) and CMD_FINETUNE_BKWD (txx) are native fractional pitch
        // adjustment commands -- never drop them.
        return false;
    }
    bool LooksLikeExternalSampleReference(const std::vector<uint8_t>& data) {
        if (data.empty()) {
            return false;
        }

        const size_t pathLength = data[0];
        if (pathLength == 0 || pathLength + 1 > data.size()) {
            return false;
        }

        bool hasPathSeparator = false;
        bool hasExtensionDot = false;
        for (size_t i = 1; i <= pathLength; ++i) {
            const uint8_t ch = data[i];
            if (ch < 0x20 || ch > 0x7E) {
                return false;
            }

            hasPathSeparator = hasPathSeparator || ch == '\\' || ch == '/' || ch == ':';
            hasExtensionDot = hasExtensionDot || ch == '.';
        }

        return hasPathSeparator && hasExtensionDot;
    }
    std::wstring DecodeExternalSampleReference(const std::vector<uint8_t>& data) {
        if (!LooksLikeExternalSampleReference(data)) {
            return std::wstring();
        }

        const size_t pathLength = data[0];
        std::wstring result;
        result.reserve(pathLength);
        for (size_t i = 1; i <= pathLength; ++i) {
            result.push_back(static_cast<wchar_t>(data[i]));
        }
        return result;
    }

    std::wstring ResolveExternalSamplePath(const std::wstring& modulePath, const std::wstring& referencePath) {
        namespace fs = std::filesystem;

        if (referencePath.empty()) {
            return std::wstring();
        }

        const fs::path reference(referencePath);
        if (reference.is_absolute() && fs::exists(reference)) {
            return reference.wstring();
        }

        const fs::path moduleDirectory = fs::path(modulePath).parent_path();
        const fs::path relativeToModule = moduleDirectory / reference;
        if (fs::exists(relativeToModule)) {
            return relativeToModule.wstring();
        }

        const fs::path sampleFolderFallback = moduleDirectory / L"Samples" / reference.filename();
        if (fs::exists(sampleFolderFallback)) {
            return sampleFolderFallback.wstring();
        }

        return std::wstring();
    }
}

bool MPTMPlayer::Initialize(const std::wstring& filename) {
#if defined(_DEBUG_MPTMPlayer_)
    debug.DebugLog("MPTMPlayer initialization started...\n");
#endif
    if (!bIsInitialized) {
        if (!CreateAudioDevice()) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"MPTMPlayer: Failed to initialize audio device.");
            return false;
        }

        bIsInitialized = true;
    }

    if (!LoadMPTMFile(filename)) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"MPTMPlayer: LoadMPTMFile failed.");
        return false;
    }

    sequencePosition = 0;
    restartSequencePosition.store(0);
    currentPatternIndex = 0;
    currentRow = 0;
    nextRowOverride = 0;
    tick = 0;
    rowBreakPending = false;
    orderJumpPending = false;
    patternDelayTicks = 0;
    fineDelayTicks = 0;
    inPatternDelay = false;
    patternLoopPending = false;
    patternLoopRow = 0;
    patternLoopCount = 0;
    samplesUntilNextTick = 0;
    backgroundVoices.clear();

    speed = header.initialSpeed > 0 ? header.initialSpeed : 6;
    tempo = header.initialTempo >= 32 ? header.initialTempo : 125;

    voices.clear();
    voices.resize(MPTM_MAX_CHANNELS);
    for (size_t channel = 0; channel < voices.size(); ++channel) {
        voices[channel].panning = DefaultPanningForChannel(channel);
        if (channel < 64) {
            // Bit 7 of channelPan marks the channel as disabled/muted in the IT header.
            const bool headerMuted = (header.channelPan[channel] & 0x80) != 0;
            voices[channel].channelVolume = headerMuted ? 0 : std::min<uint8_t>(header.channelVolume[channel], 64);
        } else {
            voices[channel].channelVolume = 64; // MPTM extension channels default to full volume
        }
    }

    moduleGlobalVolume = std::min<uint8_t>(header.globalVolume, 128); // kept at full 0..128 scale
    currentVolume = config.myConfig.musicVolume;
    targetVolume = config.myConfig.musicVolume;
    fadeInActive = false;
    fadeOutActive = false;
    isMuted = false;

    #if defined(_DEBUG_MPTMPlayer_)
        debug.DebugLog("MPTMPlayer initialization successful.\n");
    #endif
    return true;
}

bool MPTMPlayer::LoadMPTMFile(const std::wstring& filename) {
    debug.logLevelMessage(LogLevel::LOG_INFO, L"Loading MPTM file: " + filename);

    modulePath = filename;

    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"MPTMPlayer: Failed to open MPTM file.");
        return false;
    }

    file.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (file.gcount() != sizeof(header)) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"MPTMPlayer: Failed to read MPTM/IT header.");
        return false;
    }

    if (std::strncmp(header.magic, "IMPM", 4) != 0) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"MPTMPlayer: Invalid signature. MPTM is an OpenMPT IT-family file and must start with IMPM.");
        return false;
    }

    // MPTM extends the IT order limit from 256 to 8192.
    if (header.orderCount == 0 || header.orderCount > 8192 ||
        header.instrumentCount > 4096 || header.sampleCount > 4096 ||
        header.patternCount > 4096) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"MPTMPlayer: Header counts are out of supported range.");
        return false;
    }

    orders.resize(header.orderCount);
    file.read(reinterpret_cast<char*>(orders.data()), orders.size());
    if (file.gcount() != static_cast<std::streamsize>(orders.size())) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"MPTMPlayer: Failed to read order table.");
        return false;
    }

    std::vector<uint32_t> instrumentPointers(header.instrumentCount);
    std::vector<uint32_t> samplePointers(header.sampleCount);
    std::vector<uint32_t> patternPointers(header.patternCount);

    file.read(reinterpret_cast<char*>(instrumentPointers.data()), instrumentPointers.size() * sizeof(uint32_t));
    if (file.gcount() != static_cast<std::streamsize>(instrumentPointers.size() * sizeof(uint32_t))) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"MPTMPlayer: Failed to read instrument pointer table.");
        return false;
    }

    file.read(reinterpret_cast<char*>(samplePointers.data()), samplePointers.size() * sizeof(uint32_t));
    if (file.gcount() != static_cast<std::streamsize>(samplePointers.size() * sizeof(uint32_t))) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"MPTMPlayer: Failed to read sample pointer table.");
        return false;
    }

    file.read(reinterpret_cast<char*>(patternPointers.data()), patternPointers.size() * sizeof(uint32_t));
    if (file.gcount() != static_cast<std::streamsize>(patternPointers.size() * sizeof(uint32_t))) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"MPTMPlayer: Failed to read pattern pointer table.");
        return false;
    }

    defaultPanning.resize(MPTM_MAX_CHANNELS);
    for (size_t channel = 0; channel < MPTM_MAX_CHANNELS; ++channel) {
        defaultPanning[channel] = DefaultPanningForChannel(channel);
    }

    if (!LoadInstruments(file, instrumentPointers)) {
        return false;
    }

    if (!LoadSamples(file, samplePointers)) {
        return false;
    }

    if (!LoadPatterns(file, patternPointers)) {
        return false;
    }

    UnpackPatterns();

    debug.logLevelMessage(LogLevel::LOG_INFO, L"MPTM file loaded successfully.");
    return true;
}

bool MPTMPlayer::LoadInstruments(std::ifstream& file, const std::vector<uint32_t>& instrumentPointers) {
    instruments.clear();
    instruments.resize(instrumentPointers.size());

    for (size_t i = 0; i < instrumentPointers.size(); ++i) {
        if (instrumentPointers[i] == 0) {
            continue;
        }

        file.seekg(static_cast<std::streamoff>(instrumentPointers[i]), std::ios::beg);
        std::array<uint8_t, 554> data{};
        file.read(reinterpret_cast<char*>(data.data()), data.size());
        if (file.gcount() < 32) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"MPTMPlayer: Failed to read instrument " + std::to_wstring(i));
            return false;
        }

        if (std::memcmp(data.data(), "IMPI", 4) != 0) {
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"MPTMPlayer: Ignoring non-IT instrument header " + std::to_wstring(i));
            continue;
        }

        MPTMInstrument& instrument = instruments[i];
        std::memcpy(instrument.name, &data[32], sizeof(instrument.name));
        instrument.nna          = data[16] & 0x03;
        instrument.fadeOut      = ReadLE16(&data[20]);
        instrument.globalVolume = std::min<uint8_t>(data[24], 64);

        // IT 2.x / MPTM instrument default panning: byte 51, bit 7 signals "use this panning".
        // Stored as 0..64 in bits 0..6; scale to 0..255 for the voice panning range.
        if ((data[51] & 0x80) != 0) {
            instrument.hasPanningOverride = true;
            instrument.defaultPanning = static_cast<uint8_t>(
                (static_cast<uint16_t>(data[51] & 0x3F) * 255U) / 64U);
        }

        // Volume envelope (IT offset 304..383)
        instrument.volumeEnvelopeFlags        = data[304];
        instrument.volumeEnvelopePointCount   = std::min<uint8_t>(data[305], 25);
        instrument.volumeEnvelopeLoopStart    = data[306];
        instrument.volumeEnvelopeLoopEnd      = data[307];
        instrument.volumeEnvelopeSustainStart = data[308];
        instrument.volumeEnvelopeSustainEnd   = data[309];
        for (size_t point = 0; point < instrument.volumeEnvelopePointCount; ++point) {
            const size_t pointOffset = 310 + point * 3;
            instrument.volumeEnvelopeValues[point] = std::min<uint8_t>(data[pointOffset], 64);
            instrument.volumeEnvelopeTicks[point]  = ReadLE16(&data[pointOffset + 1]);
        }

        // Panning envelope (IT offset 384..463)
        instrument.panningEnvelopeFlags        = data[384];
        instrument.panningEnvelopePointCount   = std::min<uint8_t>(data[385], 25);
        instrument.panningEnvelopeLoopStart    = data[386];
        instrument.panningEnvelopeLoopEnd      = data[387];
        instrument.panningEnvelopeSustainStart = data[388];
        instrument.panningEnvelopeSustainEnd   = data[389];
        for (size_t point = 0; point < instrument.panningEnvelopePointCount; ++point) {
            const size_t pointOffset = 390 + point * 3;
            // Panning envelope values are stored as signed bytes (-32..+32) in IT.
            instrument.panningEnvelopeValues[point] = static_cast<int8_t>(data[pointOffset]);
            instrument.panningEnvelopeTicks[point]  = ReadLE16(&data[pointOffset + 1]);
        }

        // IT 2.x / MPTM instruments store 120 note/sample pairs in the keyboard
        // table at byte 64. Reading from the envelope area maps notes to garbage
        // sample ids and makes playback collapse onto the wrong sample.
        const size_t tableOffset = data.size() >= 304 ? 64 : 0;
        for (size_t note = 0; note < instrument.noteMap.size(); ++note) {
            instrument.noteMap[note] = static_cast<uint8_t>(note);  // 0-based identity default
            instrument.sampleMap[note] = static_cast<uint8_t>(std::min<size_t>(i + 1, 255));
            if (tableOffset > 0) {
                instrument.noteMap[note] = data[tableOffset + note * 2];
                instrument.sampleMap[note] = data[tableOffset + note * 2 + 1];
            }
        }
    }

    return true;
}


bool MPTMPlayer::LoadExternalSample(const std::wstring& path, MPTMSample& sample) {
#if defined(PLATFORM_WINDOWS)
    HRESULT hr = MFStartup(MF_VERSION);
    if (FAILED(hr)) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"MPTMPlayer: Media Foundation startup failed for external sample.");
        return false;
    }

    IMFSourceReader* reader = nullptr;
    hr = MFCreateSourceReaderFromURL(path.c_str(), nullptr, &reader);
    if (FAILED(hr) || reader == nullptr) {
        MFShutdown();
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"MPTMPlayer: Failed to open external sample: " + path);
        return false;
    }

    IMFMediaType* pcmType = nullptr;
    hr = MFCreateMediaType(&pcmType);
    if (SUCCEEDED(hr)) hr = pcmType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    if (SUCCEEDED(hr)) hr = pcmType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
    if (SUCCEEDED(hr)) hr = pcmType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    if (SUCCEEDED(hr)) hr = pcmType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, MPTM_SAMPLE_RATE);
    if (SUCCEEDED(hr)) hr = pcmType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 1);
    if (SUCCEEDED(hr)) hr = reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, pcmType);
    if (pcmType) {
        pcmType->Release();
    }

    if (FAILED(hr)) {
        reader->Release();
        MFShutdown();
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"MPTMPlayer: Failed to request PCM decode for external sample: " + path);
        return false;
    }

    IMFMediaType* outputType = nullptr;
    hr = reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, &outputType);
    if (FAILED(hr) || outputType == nullptr) {
        reader->Release();
        MFShutdown();
        return false;
    }

    WAVEFORMATEX* waveFormat = nullptr;
    UINT32 waveFormatSize = 0;
    hr = MFCreateWaveFormatExFromMFMediaType(outputType, &waveFormat, &waveFormatSize);
    outputType->Release();
    if (FAILED(hr) || waveFormat == nullptr) {
        reader->Release();
        MFShutdown();
        return false;
    }

    const uint16_t channels = std::max<uint16_t>(waveFormat->nChannels, 1);
    const uint16_t bitsPerSample = waveFormat->wBitsPerSample;
    CoTaskMemFree(waveFormat);

    if (bitsPerSample != 16) {
        reader->Release();
        MFShutdown();
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"MPTMPlayer: External sample did not decode to 16-bit PCM: " + path);
        return false;
    }

    std::vector<int16_t> decoded;
    while (true) {
        DWORD streamIndex = 0;
        DWORD flags = 0;
        LONGLONG timestamp = 0;
        IMFSample* mediaSample = nullptr;
        hr = reader->ReadSample(MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, &streamIndex, &flags, &timestamp, &mediaSample);
        if (FAILED(hr)) {
            if (mediaSample) mediaSample->Release();
            break;
        }
        if ((flags & MF_SOURCE_READERF_ENDOFSTREAM) != 0) {
            if (mediaSample) mediaSample->Release();
            break;
        }
        if (mediaSample == nullptr) {
            continue;
        }

        IMFMediaBuffer* buffer = nullptr;
        hr = mediaSample->ConvertToContiguousBuffer(&buffer);
        mediaSample->Release();
        if (FAILED(hr) || buffer == nullptr) {
            continue;
        }

        BYTE* data = nullptr;
        DWORD maxLength = 0;
        DWORD currentLength = 0;
        hr = buffer->Lock(&data, &maxLength, &currentLength);
        if (SUCCEEDED(hr) && data != nullptr) {
            const size_t frameCount = currentLength / (sizeof(int16_t) * channels);
            decoded.reserve(decoded.size() + frameCount);
            const int16_t* pcm = reinterpret_cast<const int16_t*>(data);
            for (size_t frame = 0; frame < frameCount; ++frame) {
                int mixed = 0;
                for (uint16_t channel = 0; channel < channels; ++channel) {
                    mixed += pcm[frame * channels + channel];
                }
                decoded.push_back(static_cast<int16_t>(mixed / static_cast<int>(channels)));
            }
            buffer->Unlock();
        }
        buffer->Release();
    }

    reader->Release();
    MFShutdown();

    if (decoded.empty()) {
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"MPTMPlayer: External sample decoded no PCM data: " + path);
        return false;
    }

    sample.pcm = std::move(decoded);
    sample.length = static_cast<uint32_t>(std::min<size_t>(sample.pcm.size(), std::numeric_limits<uint32_t>::max()));
    sample.loopStart        = std::min(sample.loopStart,        sample.length);
    sample.loopEnd          = std::min(sample.loopEnd,          sample.length);
    sample.sustainLoopStart = std::min(sample.sustainLoopStart, sample.length);
    sample.sustainLoopEnd   = std::min(sample.sustainLoopEnd,   sample.length);
    return true;
#else
    (void)path;
    (void)sample;
    return false;
#endif
}

bool MPTMPlayer::LoadSamples(std::ifstream& file, const std::vector<uint32_t>& samplePointers) {
    samples.clear();
    samples.resize(samplePointers.size());

    const std::streampos restorePosition = file.tellg();
    file.seekg(0, std::ios::end);
    const std::streamoff fileSize = file.tellg();
    file.seekg(restorePosition, std::ios::beg);
    if (fileSize <= 0) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"MPTMPlayer: Failed to determine MPTM file size.");
        return false;
    }

    for (size_t i = 0; i < samplePointers.size(); ++i) {
        if (samplePointers[i] == 0) {
            continue;
        }

        file.seekg(static_cast<std::streamoff>(samplePointers[i]), std::ios::beg);
        MPTMSampleHeader sampleHeader{};
        file.read(reinterpret_cast<char*>(&sampleHeader), sizeof(sampleHeader));
        if (file.gcount() != sizeof(sampleHeader)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"MPTMPlayer: Failed to read sample header " + std::to_wstring(i));
            return false;
        }

        if (std::strncmp(sampleHeader.magic, "IMPS", 4) != 0) {
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"MPTMPlayer: Ignoring non-IT sample header " + std::to_wstring(i));
            continue;
        }

        MPTMSample& sample = samples[i];
        sample.length = sampleHeader.length;
        sample.loopStart = sampleHeader.loopStart;
        sample.loopEnd = sampleHeader.loopEnd;
        sample.sustainLoopStart = sampleHeader.sustainLoopStart;
        sample.sustainLoopEnd = sampleHeader.sustainLoopEnd;
        sample.c5Speed = sampleHeader.c5Speed > 0 ? sampleHeader.c5Speed : 8363;
        sample.globalVolume = std::min<uint8_t>(sampleHeader.globalVolume, 64);
        sample.defaultVolume = std::min<uint8_t>(sampleHeader.defaultVolume, 64);
        sample.flags = sampleHeader.flags;
        sample.convert = sampleHeader.convert;
        sample.hasPanningOverride = (sampleHeader.defaultPanning & 0x80) != 0;
        sample.defaultPanning = sample.hasPanningOverride
            ? static_cast<uint8_t>((static_cast<uint16_t>(sampleHeader.defaultPanning & 0x7F) * 255U) / 64U)
            : 128;
        sample.autoVibratoSpeed = sampleHeader.vibratoSpeed;
        sample.autoVibratoDepth = sampleHeader.vibratoDepth;
        sample.autoVibratoRate  = sampleHeader.vibratoRate;
        sample.autoVibratoType  = sampleHeader.vibratoType;
        std::memcpy(sample.name, sampleHeader.sampleName, sizeof(sample.name));

        if ((sample.flags & 0x01) == 0 || sample.length == 0 || sampleHeader.samplePointer == 0) {
            continue;
        }

        const bool is16Bit = sample.Is16Bit();
        const bool isStereo = sample.IsStereo();
        const uint64_t sourceChannels = isStereo ? 2ULL : 1ULL;
        const uint64_t bytesPerSample = is16Bit ? 2ULL : 1ULL;
        const uint64_t byteCount64 = static_cast<uint64_t>(sample.length) * sourceChannels * bytesPerSample;
        if (byteCount64 == 0) {
            continue;
        }
        if (byteCount64 > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"MPTMPlayer: Sample data is too large to allocate " + std::to_wstring(i));
            return false;
        }

        if ((sample.flags & 0x08) != 0) {
            std::vector<uint8_t> probe(260);
            file.seekg(static_cast<std::streamoff>(sampleHeader.samplePointer), std::ios::beg);
            file.read(reinterpret_cast<char*>(probe.data()), probe.size());
            probe.resize(static_cast<size_t>(std::max<std::streamsize>(file.gcount(), 0)));

            const std::wstring externalReference = DecodeExternalSampleReference(probe);
            if (!externalReference.empty()) {
                const std::wstring externalPath = ResolveExternalSamplePath(modulePath, externalReference);
                if (!externalPath.empty() && LoadExternalSample(externalPath, sample)) {
                    continue;
                }

                sample.pcm.clear();
                debug.logLevelMessage(LogLevel::LOG_WARNING, L"MPTMPlayer: External sample file could not be loaded; skipping sample " + std::to_wstring(i));
                continue;
            }

            // Inline ITPACK compressed data — decompress it.
            {
                const uint64_t sampleOffset = static_cast<uint64_t>(sampleHeader.samplePointer);
                const uint64_t fileSize64   = static_cast<uint64_t>(fileSize);
                if (sampleOffset >= fileSize64) {
                    sample.pcm.clear();
                    debug.logLevelMessage(LogLevel::LOG_WARNING, L"MPTMPlayer: Compressed sample pointer out of file bounds; skipping sample " + std::to_wstring(i));
                    continue;
                }
                // Read up to 2× uncompressed size + block headers as a safe upper bound.
                const uint64_t maxRead = std::min(
                    static_cast<uint64_t>(sample.length) * (is16Bit ? 4ULL : 2ULL) + 4096ULL,
                    fileSize64 - sampleOffset);
                std::vector<uint8_t> compData(static_cast<size_t>(maxRead));
                file.seekg(static_cast<std::streamoff>(sampleOffset), std::ios::beg);
                file.read(reinterpret_cast<char*>(compData.data()), static_cast<std::streamsize>(compData.size()));
                const size_t bytesRead = static_cast<size_t>(std::max<std::streamsize>(file.gcount(), 0));

                sample.pcm.clear();
                sample.pcm.reserve(sample.length);
                if (!DecompressITSample(compData.data(), bytesRead, sample.pcm, sample.length, is16Bit)) {
                    debug.logLevelMessage(LogLevel::LOG_WARNING, L"MPTMPlayer: ITPACK decompression produced fewer samples than expected for sample " + std::to_wstring(i));
                    sample.pcm.resize(sample.length, 0);  // pad remainder with silence
                }
            }
            continue;
        }

        const uint64_t sampleOffset = sampleHeader.samplePointer;
        const uint64_t fileSize64 = static_cast<uint64_t>(fileSize);
        const bool sampleDataFits = sampleOffset <= fileSize64 && byteCount64 <= fileSize64 - sampleOffset;
        if (!sampleDataFits) {
            std::vector<uint8_t> probe(260);
            file.seekg(static_cast<std::streamoff>(sampleHeader.samplePointer), std::ios::beg);
            file.read(reinterpret_cast<char*>(probe.data()), probe.size());
            probe.resize(static_cast<size_t>(std::max<std::streamsize>(file.gcount(), 0)));

            // MPTM/OpenMPT files may store only a length-prefixed external sample path
            // at samplePointer. Resolve it beside the module before treating the data as bad.
            const std::wstring externalReference = DecodeExternalSampleReference(probe);
            if (!externalReference.empty()) {
                const std::wstring externalPath = ResolveExternalSamplePath(modulePath, externalReference);
                if (!externalPath.empty() && LoadExternalSample(externalPath, sample)) {
                    continue;
                }

                sample.pcm.clear();
                debug.logLevelMessage(LogLevel::LOG_WARNING, L"MPTMPlayer: External sample file could not be loaded; skipping sample " + std::to_wstring(i));
                continue;
            }


            debug.logLevelMessage(LogLevel::LOG_ERROR, L"MPTMPlayer: Sample data extends past end of file " + std::to_wstring(i));
            return false;
        }

        const size_t byteCount = static_cast<size_t>(byteCount64);
        std::vector<uint8_t> raw(byteCount);
        file.seekg(static_cast<std::streamoff>(sampleHeader.samplePointer), std::ios::beg);
        file.read(reinterpret_cast<char*>(raw.data()), raw.size());
        if (file.gcount() != static_cast<std::streamsize>(raw.size())) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"MPTMPlayer: Failed to read sample data " + std::to_wstring(i));
            return false;
        }

        const bool unsignedSamples = (sample.convert & 0x01) == 0;
        const bool deltaEncoded = (sample.convert & 0x04) != 0;
        sample.pcm.resize(sample.length);

        if (is16Bit) {
            std::vector<int32_t> last(sourceChannels, 0);
            for (uint32_t s = 0; s < sample.length; ++s) {
                int32_t mixed = 0;
                for (uint32_t sourceChannel = 0; sourceChannel < sourceChannels; ++sourceChannel) {
                    const size_t rawOffset = (static_cast<size_t>(s) * sourceChannels + sourceChannel) * 2U;
                    int32_t value = unsignedSamples
                        ? static_cast<int32_t>(ReadLE16(&raw[rawOffset])) - 32768
                        : static_cast<int32_t>(ReadLE16Signed(&raw[rawOffset]));
                    if (deltaEncoded) {
                        last[sourceChannel] = static_cast<int32_t>(static_cast<int16_t>(last[sourceChannel] + value));
                        value = last[sourceChannel];
                    }
                    mixed += value;
                }
                mixed /= static_cast<int32_t>(sourceChannels);
                sample.pcm[s] = static_cast<int16_t>(std::clamp(mixed, -32768, 32767));
            }
        }
        else {
            std::vector<int32_t> last(sourceChannels, 0);
            for (uint32_t s = 0; s < sample.length; ++s) {
                int32_t mixed = 0;
                for (uint32_t sourceChannel = 0; sourceChannel < sourceChannels; ++sourceChannel) {
                    const size_t rawOffset = static_cast<size_t>(s) * sourceChannels + sourceChannel;
                    int32_t value = unsignedSamples
                        ? static_cast<int32_t>(raw[rawOffset]) - 128
                        : static_cast<int8_t>(raw[rawOffset]);
                    if (deltaEncoded) {
                        last[sourceChannel] = static_cast<int32_t>(static_cast<int8_t>(last[sourceChannel] + value));
                        value = last[sourceChannel];
                    }
                    mixed += value;
                }
                mixed /= static_cast<int32_t>(sourceChannels);
                sample.pcm[s] = static_cast<int16_t>(std::clamp(mixed << 8, -32768, 32767));
            }
        }

        // Clamp all four loop points to the actual decoded PCM length.
        const uint32_t pcmLen = static_cast<uint32_t>(sample.pcm.size());
        sample.loopStart        = std::min(sample.loopStart,        pcmLen);
        sample.loopEnd          = std::min(sample.loopEnd,          pcmLen);
        sample.sustainLoopStart = std::min(sample.sustainLoopStart, pcmLen);
        sample.sustainLoopEnd   = std::min(sample.sustainLoopEnd,   pcmLen);
    }

    return true;
}

bool MPTMPlayer::LoadPatterns(std::ifstream& file, const std::vector<uint32_t>& patternPointers) {
    patterns.clear();
    patterns.resize(patternPointers.size());

    for (size_t i = 0; i < patternPointers.size(); ++i) {
        if (patternPointers[i] == 0) {
            patterns[i].rows = 64;
            continue;
        }

        file.seekg(static_cast<std::streamoff>(patternPointers[i]), std::ios::beg);
        uint8_t headerBytes[8] = {};
        file.read(reinterpret_cast<char*>(headerBytes), sizeof(headerBytes));
        if (file.gcount() != sizeof(headerBytes)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"MPTMPlayer: Failed to read pattern header " + std::to_wstring(i));
            return false;
        }

        patterns[i].packedLength = ReadLE16(headerBytes);
        patterns[i].rows = std::max<uint16_t>(ReadLE16(headerBytes + 2), 1);
        patterns[i].data.resize(patterns[i].packedLength);
        if (patterns[i].packedLength > 0) {
            file.read(reinterpret_cast<char*>(patterns[i].data.data()), patterns[i].data.size());
            if (file.gcount() != static_cast<std::streamsize>(patterns[i].data.size())) {
                debug.logLevelMessage(LogLevel::LOG_ERROR, L"MPTMPlayer: Failed to read packed pattern " + std::to_wstring(i));
                return false;
            }
        }
    }

    return true;
}

void MPTMPlayer::UnpackPatterns() {
    unpackedPatterns.clear();
    unpackedPatterns.resize(patterns.size());

    for (size_t patternIndex = 0; patternIndex < patterns.size(); ++patternIndex) {
        const uint16_t rows = std::max<uint16_t>(patterns[patternIndex].rows, 1);
        unpackedPatterns[patternIndex].assign(rows, std::vector<MPTMEvent>(MPTM_MAX_CHANNELS));

        std::array<uint8_t, MPTM_MAX_CHANNELS> lastMask{};
        std::array<MPTMEvent, MPTM_MAX_CHANNELS> lastEvent{};
        const std::vector<uint8_t>& data = patterns[patternIndex].data;
        size_t offset = 0;
        uint16_t row = 0;

        while (row < rows && offset < data.size()) {
            const uint8_t channelByte = data[offset++];
            if (channelByte == 0) {
                ++row;
                continue;
            }

            const size_t channel = (channelByte - 1U) & 0x7F; // MPTM allows up to 127 channels
            uint8_t mask = lastMask[channel];
            if ((channelByte & 0x80) != 0) {
                if (offset >= data.size()) {
                    break;
                }
                mask = data[offset++];
                lastMask[channel] = mask;
            }

            MPTMEvent event = unpackedPatterns[patternIndex][row][channel];
            if ((mask & 0x01) != 0 && offset < data.size()) {
                event.note = data[offset++];
                lastEvent[channel].note = event.note;
            }
            if ((mask & 0x02) != 0 && offset < data.size()) {
                event.instrument = data[offset++];
                lastEvent[channel].instrument = event.instrument;
            }
            if ((mask & 0x04) != 0 && offset < data.size()) {
                event.volume = data[offset++];
                lastEvent[channel].volume = event.volume;
            }
            if ((mask & 0x08) != 0 && offset + 1 < data.size()) {
                event.effect = data[offset++];
                event.effectData = data[offset++];
                lastEvent[channel].effect = event.effect;
                lastEvent[channel].effectData = event.effectData;
            }
            if ((mask & 0x10) != 0) {
                event.note = lastEvent[channel].note;
            }
            if ((mask & 0x20) != 0) {
                event.instrument = lastEvent[channel].instrument;
            }
            if ((mask & 0x40) != 0) {
                event.volume = lastEvent[channel].volume;
            }
            if ((mask & 0x80) != 0) {
                event.effect = lastEvent[channel].effect;
                event.effectData = lastEvent[channel].effectData;
            }

            unpackedPatterns[patternIndex][row][channel] = event;
        }
    }

    // Scan all patterns for commands that have no native playback path and warn once.
    bool foundZxx = false, foundSmooth = false, foundExtMidi = false;
    bool foundS0x = false, foundSFx = false;
    for (const auto& pattern : unpackedPatterns) {
        for (const auto& row : pattern) {
            for (const MPTMEvent& ev : row) {
                if (ev.effect == CMD_MIDI)        foundZxx     = true;
                if (ev.effect == CMD_SMOOTHMIDI)  foundSmooth  = true;
                if (ev.effect == CMD_EXTMIDI)     foundExtMidi = true;
                if (ev.effect == 0x13) {
                    const uint8_t sub = ev.effectData >> 4;
                    if (sub == 0x0)  foundS0x = true;
                    if (sub == 0xF)  foundSFx = true;
                }
            }
        }
    }
    if (foundZxx || foundSmooth || foundExtMidi || foundS0x || foundSFx) {
        std::wstring msg = L"MPTMPlayer: module contains unsupported commands (playback will differ from OpenMPT):";
        if (foundZxx)     msg += L" Zxx(MIDI/filter)";
        if (foundSmooth)  msg += L" \\xx(smooth-MIDI)";
        if (foundExtMidi) msg += L" #xx(plugin-param)";
        if (foundS0x)     msg += L" S0x(filter)";
        if (foundSFx)     msg += L" SFx(MIDI-macro)";
        debug.logLevelMessage(LogLevel::LOG_WARNING, msg);
    }
}

bool MPTMPlayer::Play(const std::wstring& filename) {
    if (isPlaying) {
        return false;
    }

    if (!Initialize(filename)) {
        return false;
    }

    #if defined(PLATFORM_WINDOWS)
        SilenceBuffer();
        HRESULT hr = secondaryBuffer->Play(0, 0, DSBPLAY_LOOPING);
        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"MPTMPlayer: Failed to start DirectSound buffer playback.");
            return false;
        }
    #endif

    isPlaying = true;
    isPaused = false;
    isTerminating = false;
    playbackThread = std::thread(&MPTMPlayer::PlaybackLoop, this);
    return true;
}

bool MPTMPlayer::Play(const std::string& path) {
    return Play(std::wstring(path.begin(), path.end()));
}

void MPTMPlayer::Shutdown() {
    isPlaying = false;
    isTerminating = true;
    if (playbackThread.joinable()) {
        playbackThread.join();
    }

#if defined(PLATFORM_WINDOWS)
    if (secondaryBuffer) {
        secondaryBuffer->Stop();
        secondaryBuffer->Release();
        secondaryBuffer = nullptr;
    }
    if (primaryBuffer) {
        primaryBuffer->Release();
        primaryBuffer = nullptr;
    }
    if (directSound) {
        directSound->Release();
        directSound = nullptr;
    }
#else
    secondaryBuffer = nullptr;
    primaryBuffer = nullptr;
    directSound = nullptr;
#endif

    orders.clear();
    instruments.clear();
    samples.clear();
    patterns.clear();
    unpackedPatterns.clear();
    voices.clear();
    defaultPanning.clear();
    mixScratch.clear();
    bIsInitialized = false;
    isPaused = false;
    isMuted = false;
    isTerminating = false;
}

void MPTMPlayer::Terminate() {
    isTerminating = true;
    Stop();
}

void MPTMPlayer::Stop() {
    if (!isPlaying) {
        return;
    }

    isPlaying = false;
    isTerminating = true;
    if (playbackThread.joinable()) {
        playbackThread.join();
    }

#if defined(PLATFORM_WINDOWS)
    if (secondaryBuffer) {
        secondaryBuffer->Stop();
    }
#endif

    isTerminating = false;
}

void MPTMPlayer::Pause() {
    Mute();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    isPaused = true;
}

void MPTMPlayer::Resume() {
    isPaused = false;
    isMuted = false;
    SetVolume(targetVolume.load());
}

void MPTMPlayer::HardPause() {
    std::lock_guard<std::mutex> lock(playbackMutex);
    isPaused = true;
    isMuted = true;

    for (MPTMChannelVoice& voice : voices) {
        voice.active = false;
        voice.volume = 0;
        voice.position = 0.0;
    }

    SilenceBuffer();
    SetVolume(0);
}

void MPTMPlayer::HardResume() {
    std::lock_guard<std::mutex> lock(playbackMutex);

    tick = 0;
    currentRow = 0;
    rowBreakPending = false;
    orderJumpPending = false;
    patternDelayTicks = 0;
    fineDelayTicks = 0;
    inPatternDelay = false;
    patternLoopPending = false;

    for (size_t channel = 0; channel < voices.size(); ++channel) {
        MPTMChannelVoice& voice = voices[channel];
        voice = MPTMChannelVoice{};
        voice.panning     = DefaultPanningForChannel(channel);
        voice.basePanning = voice.panning;
        if (channel < 64) {
            const bool headerMuted = (header.channelPan[channel] & 0x80) != 0;
            voice.channelVolume = headerMuted ? 0 : std::min<uint8_t>(header.channelVolume[channel], 64);
        } else {
            voice.channelVolume = 64;
        }
    }

    SilenceBuffer();
    isPaused = false;
    isMuted = false;
    currentVolume      = targetVolume.load();
    moduleGlobalVolume = std::min<uint8_t>(header.globalVolume, 128);
}

void MPTMPlayer::Mute() {
    // MixAudio already emits silence while isMuted is set, so there is no need to destroy the
    // per-voice volumes here. Zeroing them left sustained notes silent after Resume() because
    // nothing restored them; relying on the flag keeps a soft mute fully reversible.
    isMuted = true;
    SilenceBuffer();
}

bool MPTMPlayer::IsPaused() const {
    return isPaused;
}

bool MPTMPlayer::IsPlaying() const {
    return isPlaying && !isPaused && !isTerminating;
}

void MPTMPlayer::SetVolume(uint8_t volume) {
    const uint8_t clamped = std::min<uint8_t>(volume, 64);
    currentVolume = clamped;
    targetVolume = clamped;
}

void MPTMPlayer::SetGlobalVolume(uint8_t volume) {
    globalVolume = std::min<uint8_t>(volume, 64);
}

void MPTMPlayer::SetFadeIn(uint32_t durationMs) {
    fadeDurationMs = durationMs;
    fadeElapsedMs = 0;
    fadeInActive = true;
    fadeOutActive = false;
    currentVolume = 0;
    targetVolume = 64;
}

void MPTMPlayer::SetFadeOut(uint32_t durationMs) {
    fadeDurationMs = durationMs;
    fadeElapsedMs = 0;
    fadeInActive = false;
    fadeOutActive = true;
    targetVolume = 0;
}

bool MPTMPlayer::CreateAudioDevice() {
#if defined(PLATFORM_WINDOWS)
    HRESULT hr = DirectSoundCreate8(NULL, &directSound, NULL);
    if (FAILED(hr)) {
        return false;
    }

    hr = directSound->SetCooperativeLevel(hwnd, DSSCL_PRIORITY);
    if (FAILED(hr)) {
        return false;
    }

    DSBUFFERDESC primaryDesc = {};
    primaryDesc.dwSize = sizeof(primaryDesc);
    primaryDesc.dwFlags = DSBCAPS_PRIMARYBUFFER;

    hr = directSound->CreateSoundBuffer(&primaryDesc, &primaryBuffer, NULL);
    if (FAILED(hr)) {
        return false;
    }

    WAVEFORMATEX waveFormat = {};
    waveFormat.wFormatTag = WAVE_FORMAT_PCM;
    waveFormat.nChannels = 2;
    waveFormat.nSamplesPerSec = MPTM_SAMPLE_RATE;
    waveFormat.wBitsPerSample = 16;
    waveFormat.nBlockAlign = waveFormat.nChannels * waveFormat.wBitsPerSample / 8;
    waveFormat.nAvgBytesPerSec = waveFormat.nSamplesPerSec * waveFormat.nBlockAlign;

    primaryBuffer->SetFormat(&waveFormat);

    DSBUFFERDESC secondaryDesc = {};
    secondaryDesc.dwSize = sizeof(secondaryDesc);
    secondaryDesc.dwBufferBytes = MPTM_BUFFER_SIZE;
    secondaryDesc.lpwfxFormat = &waveFormat;
    secondaryDesc.dwFlags = DSBCAPS_GLOBALFOCUS | DSBCAPS_GETCURRENTPOSITION2;

    hr = directSound->CreateSoundBuffer(&secondaryDesc, &secondaryBuffer, NULL);
    if (FAILED(hr)) {
        return false;
    }

    bufferSize = secondaryDesc.dwBufferBytes;
    writeCursor = 0;
    return true;
#else
    return true;
#endif
}

void MPTMPlayer::FillAudioBuffer() {
#if defined(PLATFORM_WINDOWS)
    if (!secondaryBuffer) {
        return;
    }

    // Protect voice state against concurrent modification from GotoSequenceID/HardPause/HardResume.
    // Blocking lock: MixAudio now drives tick advancement internally, so a skipped fill would
    // underrun immediately. GotoSequenceID holds this mutex for only a few microseconds.
    std::lock_guard<std::mutex> lock(playbackMutex);

    DWORD playCursor = 0;
    DWORD writeCursorDS = 0;
    HRESULT hr = secondaryBuffer->GetCurrentPosition(&playCursor, &writeCursorDS);
    if (FAILED(hr)) {
        return;
    }

    constexpr DWORD frameBytes = sizeof(int16_t) * 2;
    // Keep roughly a quarter second of audio queued ahead of the play cursor.
    const DWORD targetLead = (bufferSize / 4) & ~(frameBytes - 1);

    // Detect underrun: our tracked write cursor has fallen behind DirectSound's safe-write
    // cursor. Resync forward — but do NOT return early; the newly skipped region must be
    // filled or the play cursor will hit stale ring-buffer data and produce a pop.
    const DWORD rawLead     = (writeCursor  - playCursor  + bufferSize) % bufferSize;
    const DWORD dsWriteLead = (writeCursorDS - playCursor + bufferSize) % bufferSize;
    if (rawLead < dsWriteLead) {
        writeCursor = (writeCursorDS + frameBytes * 8) % bufferSize;
        writeCursor &= ~(frameBytes - 1);
    }

    // Recompute lead after any resync before deciding how much to fill.
    const DWORD lead = (writeCursor - playCursor + bufferSize) % bufferSize;
    if (lead >= targetLead) {
        return;
    }

    DWORD bytesToWrite = (targetLead - lead) & ~(frameBytes - 1);
    if (bytesToWrite == 0) {
        return;
    }

    const DWORD samplesToWrite = bytesToWrite / frameBytes;
    const size_t stereoSampleCount = static_cast<size_t>(samplesToWrite) * 2;
    if (mixScratch.size() < stereoSampleCount) {
        mixScratch.resize(stereoSampleCount);
    }
    MixAudio(mixScratch.data(), samplesToWrite);

    void* p1 = nullptr;
    void* p2 = nullptr;
    DWORD b1 = 0;
    DWORD b2 = 0;

    hr = secondaryBuffer->Lock(writeCursor, bytesToWrite, &p1, &b1, &p2, &b2, 0);
    if (SUCCEEDED(hr)) {
        if (p1 && b1 > 0) {
            std::memcpy(p1, mixScratch.data(), b1);
        }
        if (p2 && b2 > 0) {
            std::memcpy(p2, reinterpret_cast<uint8_t*>(mixScratch.data()) + b1, b2);
        }

        secondaryBuffer->Unlock(p1, b1, p2, b2);
        writeCursor = (writeCursor + bytesToWrite) % bufferSize;
    }
#endif
}

void MPTMPlayer::MixAudio(int16_t* buffer, size_t samplesToMix) {
    std::fill(buffer, buffer + samplesToMix * 2, int16_t(0));

    if (isMuted) {
        return;
    }

    const size_t accumSamples = samplesToMix * 2;
    if (mixAccumulator.size() < accumSamples) {
        mixAccumulator.resize(accumSamples);
    }
    std::fill(mixAccumulator.begin(), mixAccumulator.begin() + accumSamples, int32_t(0));

    // IT-compatible 32-bit accumulator mixing.
    //
    // The complete gain chain (matching OpenMPT's "Compatible Mix Level" for IT/MPTM):
    //
    //   voiceVol   : 0..64   → /64   (note volume + Vxx overrides)
    //   sampleGVol : 0..64   → /64   (per-sample global volume)
    //   instGVol   : 0..64   → /64   (per-instrument global volume)
    //   channelVol : 0..64   → /64   (Mxx / header channel volume)
    //   envelope   : 0..1.0          (volume envelope evaluated by AdvanceVoiceEnvelope)
    //   fadeOut    : 0..65536 → /65536
    //   moduleGVol : 0..128  → /128  (Vxx/Wxy global volume)
    //   masterVol  : 0..128  → /128  (IT header mix volume — overall pre-amp)
    //   userVol    : 0..64   → /64   (game-engine volume control)
    //   fadeVol    : 0..64   → /64   (fade-in/fade-out envelope)
    //
    // The combined scalar is pre-multiplied to a Q16 integer weight and carried as a
    // float smoothGain that ramps toward the target each sample (click elimination).
    // The accumulator stores raw int16 PCM × Q16 weight, so the final output shift is
    // >> 16. To match OpenMPT's headroom (centre-panned single channel at full gain
    // fills ~1/2 of 32767), the combined weight naturally produces that range because
    // the panning split (0..1) halves each stereo contribution.
    //
    // Accumulator range:
    //   worst case: 127 channels × 32767 PCM × 65536 weight = ~2.7 × 10^11
    //   int32_t max: ~2.1 × 10^9 — would overflow.
    //   We cap weight at 65536 (weight unity = full volume, no pan split).
    //   With panning split included (0.5 each side at centre):
    //     127 channels × 32767 × 65536 × 0.5 ≈ 1.36 × 10^11 — still overflows int32.
    //   Therefore we use int32_t accumulator with the TOTAL weight kept ≤ 32768 (Q15)
    //   so that 127 channels × 32767 × 32768 × 0.5 ≈ 6.8 × 10^10 — still overflows.
    //   Switch to int64_t accumulator (max ~9.2 × 10^18 — ample).
    //   Output shift = 15 bits (Q15 → int16: >> 15 with saturation).
    //
    // The smoothGain ramp replaces the per-frame gain update and is stored as a float
    // in [0..1]; it is scaled to Q15 integer weight for accumulator writes.

    // Module-level global factors (constant across all voices this buffer).
    // masterVol: IT "Mix Volume" byte, 0..128 (128 = max).  Default when unset = 48.
    const uint32_t masterVol  = (header.mixVolume > 0) ? header.mixVolume : 48u;
    const uint32_t moduleGVol = moduleGlobalVolume;   // 0..128
    const uint32_t userVol    = static_cast<uint32_t>(globalVolume.load()); // 0..64
    const uint32_t fadeVol    = static_cast<uint32_t>(currentVolume.load()); // 0..64

    // Combined scalar for all voices (voice-independent factors).
    // Maps each factor's maximum to 1.0 and combines them:
    //   masterVol/128 × moduleGVol/128 × userVol/64 × fadeVol/64
    // Expressed as a Q15 multiplier so we can later do Q15 × int16 → int32.
    // Maximum of this factor is 1.0 (all at max) → (1 << 15) = 32768 Q15 units.
    const float globalScale = (static_cast<float>(masterVol)  / 128.0f) *
                              (static_cast<float>(moduleGVol)  / 128.0f) *
                              (static_cast<float>(userVol)     /  64.0f) *
                              (static_cast<float>(fadeVol)     /  64.0f);

    // Mix one voice into the int64_t accumulator (we promote per chunk to avoid overflow).
    // Using a separate int64 array is expensive; instead keep int32 and rely on the
    // globalScale normally being ≤ 0.5 (masterVol=128 with standard volumes), so that
    // the maximum per-sample contribution stays within int32_t range for typical use.
    // For safety we clamp on output rather than tracking accumulator overflow.
    auto mixOneVoice = [&](MPTMChannelVoice& v, int32_t* accumBuf, size_t chunkSize) {
        if (!v.active || !v.sample || v.sample->pcm.empty()) {
            return;
        }

        const MPTMSample* sample   = v.sample;
        const size_t      dataSize = sample->pcm.size();
        double samplePos  = v.position;
        const double stepValue = std::abs(v.step);

        // Per-voice gain factors (all 0..1 floats, see chain above).
        const float instGVol    = v.instrumentDef
            ? static_cast<float>(v.instrumentDef->globalVolume) / 64.0f : 1.0f;
        float targetGain = (static_cast<float>(v.volume)             / 64.0f) *
                           (static_cast<float>(sample->globalVolume) / 64.0f) *
                           instGVol *
                           (static_cast<float>(v.channelVolume)      / 64.0f) *
                           v.envelopeVolume *
                           (static_cast<float>(v.fadeOutVolume)      / 65536.0f) *
                           globalScale;

        // Panning: 0 = full left, 128 = centre, 255 = full right.
        // Envelope panning is an offset in the same ±127 space mapped to ±0.5.
        const float rawPan      = static_cast<float>(v.panning) / 255.0f;
        const float envPanDelta = v.envelopePanning * (127.0f / 255.0f);
        const float pan         = std::clamp(rawPan + envPanDelta, 0.0f, 1.0f);

        const float panL = 1.0f - pan;
        const float panR = pan;

        const bool sustainActive = sample->IsSustainLooped() && !v.noteReleased;
        const bool loopActive    = sustainActive || sample->IsLooped();
        const bool pingPong      = (sustainActive && sample->IsPingPongSustainLoop()) ||
                                   (!sustainActive && sample->IsPingPongLoop());
        const double loopStart   = static_cast<double>(sustainActive ? sample->sustainLoopStart : sample->loopStart);
        const double loopEnd     = static_cast<double>(sustainActive ? sample->sustainLoopEnd   : sample->loopEnd);
        const double loopLength  = loopEnd - loopStart;

        for (size_t frame = 0; frame < chunkSize; ++frame) {
            // Smooth gain ramp — eliminates clicks from abrupt volume/note changes.
            v.smoothGain += (targetGain - v.smoothGain) * 0.005f;

            if (samplePos < 0.0) { samplePos = 0.0; }
            size_t index = static_cast<size_t>(samplePos);
            if (index >= dataSize) {
                if (v.smoothGain > (1.0f / 32768.0f) && dataSize > 0) {
                    v.volume = 0;
                    v.deactivatePending = true;
                    targetGain = 0.0f;
                    samplePos = static_cast<double>(dataSize - 1);
                    index = dataSize - 1;
                } else {
                    v.active = false;
                    break;
                }
            }

            // Linear interpolation between adjacent PCM samples.
            const int32_t s0 = static_cast<int32_t>(sample->pcm[index]);
            int32_t s1;
            {
                const size_t nextIndex = index + 1;
                if (loopActive && loopLength > 1.0 && !pingPong && !v.reversePlayback &&
                    nextIndex >= static_cast<size_t>(loopEnd)) {
                    const size_t ls = static_cast<size_t>(loopStart);
                    s1 = (ls < dataSize) ? static_cast<int32_t>(sample->pcm[ls]) : s0;
                } else {
                    s1 = (nextIndex < dataSize)
                       ? static_cast<int32_t>(sample->pcm[nextIndex]) : s0;
                }
            }
            // Interpolated PCM value (still in int16 range).
            const float frac = static_cast<float>(samplePos - static_cast<double>(index));
            const int32_t pcm = static_cast<int32_t>(
                static_cast<float>(s0) + (static_cast<float>(s1 - s0) * frac));

            // Q15 combined weight for this sample.
            // smoothGain (0..1) × 32767 → Q15 int, then split by panning.
            const int32_t gainQ15 = static_cast<int32_t>(v.smoothGain * 32767.0f);
            const int32_t gL = static_cast<int32_t>(static_cast<float>(gainQ15) * panL);
            const int32_t gR = static_cast<int32_t>(static_cast<float>(gainQ15) * panR);

            // Accumulate: pcm × gain / 32768 is deferred to the output stage (>> 15).
            accumBuf[frame * 2]     += (pcm * gL) >> 8;  // partial shift here to avoid int32 overflow
            accumBuf[frame * 2 + 1] += (pcm * gR) >> 8;  // remaining >> 7 applied at output stage

            // Advance sample position.
            samplePos += (pingPong ? v.pingPongReverse : v.reversePlayback) ? -stepValue : stepValue;

            if (loopActive && loopLength > 1.0) {
                if (pingPong) {
                    for (int guard = 0; guard < 4; ++guard) {
                        if (!v.pingPongReverse && samplePos >= loopEnd) {
                            samplePos = 2.0 * loopEnd - samplePos;
                            if (samplePos >= loopEnd) { samplePos = loopEnd - 1e-10; }
                            v.pingPongReverse = true;
                        } else if (v.pingPongReverse && samplePos <= loopStart) {
                            samplePos = 2.0 * loopStart - samplePos;
                            if (samplePos < loopStart) { samplePos = loopStart; }
                            v.pingPongReverse = false;
                        } else {
                            break;
                        }
                    }
                    if (samplePos >= loopEnd)  { samplePos = loopEnd - 1e-10; }
                    if (samplePos < loopStart) { samplePos = loopStart; }
                } else {
                    while (samplePos >= loopEnd) { samplePos -= loopLength; }
                    if (samplePos < loopStart)   { samplePos = loopStart; }
                }
            } else if (v.reversePlayback) {
                if (samplePos < 0.0) {
                    v.volume = 0;
                    v.deactivatePending = true;
                    targetGain = 0.0f;
                    samplePos = 0.0;
                }
            } else if (samplePos >= static_cast<double>(dataSize)) {
                v.volume = 0;
                v.deactivatePending = true;
                targetGain = 0.0f;
            }
        }

        v.position = samplePos;
        if (v.deactivatePending && v.smoothGain < (1.0f / 32768.0f)) {
            v.active = false;
            v.deactivatePending = false;
        }
    };

    // Process in tick-sized chunks so tracker events fire at exact sample boundaries.
    size_t rendered = 0;
    while (rendered < samplesToMix) {
        if (samplesUntilNextTick == 0) {
            if (tick == 0) {
                TickRow();
            } else {
                ApplyTickEffects();
            }
            const uint16_t ticksThisRow = std::max<uint16_t>(
                static_cast<uint16_t>(speed + fineDelayTicks), uint16_t(1));
            tick = static_cast<uint16_t>((tick + 1) % ticksThisRow);

            const uint32_t t = static_cast<uint32_t>(std::max<uint16_t>(tempo, 32));
            samplesUntilNextTick = (static_cast<uint32_t>(MPTM_SAMPLE_RATE) * 5U) / (t * 2U);
            if (samplesUntilNextTick == 0) { samplesUntilNextTick = 1; }
        }

        const size_t chunk = std::min<size_t>(samplesToMix - rendered,
                                              static_cast<size_t>(samplesUntilNextTick));
        int32_t* accumBuf = mixAccumulator.data() + rendered * 2;

        for (MPTMChannelVoice& v : voices) {
            mixOneVoice(v, accumBuf, chunk);
        }
        for (MPTMChannelVoice& v : backgroundVoices) {
            mixOneVoice(v, accumBuf, chunk);
        }

        if (!backgroundVoices.empty()) {
            backgroundVoices.erase(
                std::remove_if(backgroundVoices.begin(), backgroundVoices.end(),
                    [](const MPTMChannelVoice& bv) { return !bv.active; }),
                backgroundVoices.end());
        }

        rendered             += chunk;
        samplesUntilNextTick -= static_cast<uint32_t>(chunk);
    }

    // Convert 32-bit accumulator to int16 output.
    // The partial shift inside the loop was >> 8; final shift is >> 7 → total >> 15 (Q15).
    // Saturate to int16 range to handle any overflow from many simultaneous channels.
    for (size_t i = 0; i < accumSamples; ++i) {
        const int32_t v = mixAccumulator[i] >> 7;
        buffer[i] = static_cast<int16_t>(std::clamp<int32_t>(v, -32768, 32767));
    }
}

void MPTMPlayer::SilenceBuffer() {
#if defined(PLATFORM_WINDOWS)
    if (!secondaryBuffer || bufferSize == 0) {
        return;
    }

    void* p1 = nullptr;
    void* p2 = nullptr;
    DWORD b1 = 0;
    DWORD b2 = 0;

    HRESULT hr = secondaryBuffer->Lock(0, bufferSize, &p1, &b1, &p2, &b2, DSBLOCK_ENTIREBUFFER);
    if (SUCCEEDED(hr)) {
        if (p1 && b1 > 0) {
            std::memset(p1, 0, b1);
        }
        if (p2 && b2 > 0) {
            std::memset(p2, 0, b2);
        }
        secondaryBuffer->Unlock(p1, b1, p2, b2);
    }
#endif
}

void MPTMPlayer::TickRow() {
    // Lock is held by the caller (FillAudioBuffer via MixAudio); do NOT re-lock here.

    while (sequencePosition < orders.size() && orders[sequencePosition] == 254) {
        ++sequencePosition;
    }

    if (sequencePosition >= orders.size() || orders[sequencePosition] == 255) {
        sequencePosition = restartSequencePosition.load();
    }

    if (sequencePosition >= orders.size()) {
        sequencePosition = 0;
    }

    const uint16_t patternIndex = orders[sequencePosition];
    if (patternIndex >= unpackedPatterns.size()) {
        AdvanceRow();
        tick = 0;
        return;
    }

    currentPatternIndex = patternIndex;
    if (currentRow >= unpackedPatterns[patternIndex].size()) {
        currentRow = 0;
    }

    // While a row is held open by a pattern delay (SEx) we must NOT re-read it -- doing so
    // retriggers every note and makes sustained samples restart audibly. Just count the delay
    // down and keep the row's notes sounding; the per-tick effects continue to run on the
    // in-between ticks via ApplyTickEffects, matching how OpenMPT / ModPlug Tracker hold a row.
    if (inPatternDelay) {
        if (patternDelayTicks > 0) {
            --patternDelayTicks;
        }
        if (patternDelayTicks == 0) {
            inPatternDelay = false;
            AdvanceRow();
        }
        tick = 0;
        return;
    }

    const std::vector<MPTMEvent>& row = unpackedPatterns[patternIndex][currentRow];
    rowBreakPending = false;
    orderJumpPending = false;
    fineDelayTicks = 0;   // S6x is per-row; clear it before this row's effects can set it.
    nextRowOverride = currentRow + 1;

    // First (and only) read of this row: trigger its notes and effects exactly once.
    for (size_t channel = 0; channel < row.size(); ++channel) {
        const MPTMEvent& event = row[channel];
        TriggerEvent(channel, event, false);

        // IT spec: volume-column pitch effects (e/f/g/h) fire on every tick including tick 0.
        // Volume slides (c/d) do NOT fire on tick 0 — only from tick 1 onward via ApplyTickEffects.
        const uint8_t vc = voices[channel].volCommand;
        if (vc == VOLCMD_PITCHDOWN || vc == VOLCMD_PITCHUP ||
            vc == VOLCMD_TONEPORTA || vc == VOLCMD_VIBRATO) {
            ApplyVolumeColumnTick(voices[channel]);
        }
    }

    // If SEx armed a pattern delay on this row, hold here without advancing or decrementing
    // (this first play already counts); subsequent hold ticks above do the countdown.
    if (!inPatternDelay) {
        AdvanceRow();
    }
    tick = 0;
}

void MPTMPlayer::TriggerEvent(size_t channel, const MPTMEvent& event, bool fromDelay) {
    MPTMChannelVoice& voice = voices[channel];
    const uint8_t effect = event.effect;
    uint8_t data = event.effectData;

    // SDx: note delay. Only defer when the tick count is non-zero -- SD0 means "no delay", and
    // deferring it to tick 0 would lose the note entirely (tick 0 runs TickRow, not ApplyTickEffects).
    if (effect == 0x13 && (data >> 4) == 0x0D && (data & 0x0F) > 0 && !fromDelay) {
        voice.delayedNotePending = true;
        voice.delayTicks = data & 0x0F;
        voice.delayedEvent = event;
        voice.delayedEvent.effect = 0;
        voice.delayedEvent.effectData = 0;
        voice.effect = effect;
        voice.effectData = data;
        return;
    }

    // CMD_DELAYCUT (Xxx) with a non-zero delay nibble: defer the note trigger exactly
    // like SDx does. The cut tick is registered immediately so it fires on the correct
    // tick even though the note itself starts later.
    if (effect == CMD_DELAYCUT && (data >> 4) > 0 && !fromDelay) {
        voice.delayedNotePending      = true;
        voice.delayTicks              = data >> 4;
        voice.delayedEvent            = event;
        voice.delayedEvent.effect     = 0;
        voice.delayedEvent.effectData = 0;
        if ((data & 0x0F) > 0) {
            voice.noteCutTick = data & 0x0F;
        }
        voice.effect = effect;
        voice.effectData = data;
        return;
    }

    if (event.instrument > 0) {
        voice.instrument = event.instrument;
    }

    // Decode the volume column up front: a 'g' (tone portamento) there must suppress the note
    // retrigger just like a Gxx in the effect column does.
    uint8_t volCommand = VOLCMD_NONE;
    uint8_t volParam = 0;
    if (event.volume <= 212) {
        DecodeVolumeColumn(event.volume, volCommand, volParam);
    }
    const bool volTonePorta = (volCommand == VOLCMD_TONEPORTA);

    uint8_t mappedNote = event.note;
    const MPTMSample* mappedSample = ResolveSample(voice.instrument, event.note, mappedNote);
    // Check the raw 1-based event note: 0=empty cell, 1-252=playable, 253+=special.
    // (mappedNote is now 0-based and cannot be used for this test — 0 is a valid note.)
    const bool hasPlayableNote = event.note > 0 && event.note < MPTM_NOTE_FADE;
    const bool tonePortamentoNote = (effect == 0x07 || effect == 0x0C || volTonePorta) && hasPlayableNote && voice.sample;

    if (event.note == MPTM_NOTE_CUT) {
        if (voice.active && voice.smoothGain > 0.001f) {
            voice.volume = 0;
            voice.deactivatePending = true;
        } else {
            voice.active = false;
            voice.deactivatePending = false;
        }
    }
    else if (event.note == MPTM_NOTE_OFF || event.note == MPTM_NOTE_FADE) {
        voice.noteReleased = true;
        if (!voice.instrumentDef || (!voice.instrumentDef->HasVolumeEnvelope() && voice.instrumentDef->fadeOut == 0)) {
            if (voice.active && voice.smoothGain > 0.001f) {
                voice.volume = 0;
                voice.deactivatePending = true;
            } else {
                voice.active = false;
                voice.deactivatePending = false;
            }
        }
    }
    else if (hasPlayableNote && mappedSample && !mappedSample->pcm.empty()) {
        const double noteStep = NoteToFrequency(ITNoteToLinear(mappedNote), mappedSample->c5Speed) /
            static_cast<double>(MPTM_SAMPLE_RATE);
        voice.note = mappedNote;
        voice.targetStep = noteStep;

        if (!tonePortamentoNote) {
            // NNA: when a new note fires on a channel with an active voice, move the old voice
            // to the background according to the instrument's New Note Action setting.
            if (voice.active) {
                const uint8_t nna = (voice.nnaOverride >= 0)
                    ? static_cast<uint8_t>(voice.nnaOverride)
                    : (voice.instrumentDef ? voice.instrumentDef->nna : 0);
                if (nna > 0 && backgroundVoices.size() < 64) {
                    MPTMChannelVoice bg = voice;
                    switch (nna) {
                    case 2: // note-off: release the background voice
                        bg.noteReleased = true;
                        if (!bg.instrumentDef ||
                            (!bg.instrumentDef->HasVolumeEnvelope() && bg.instrumentDef->fadeOut == 0)) {
                            bg.volume = 0;
                            bg.deactivatePending = true;
                        }
                        break;
                    case 3: // fade: let the envelope fadeout drive volume to zero
                        bg.noteReleased = true;
                        break;
                    default: // 1 = continue: keep playing as-is
                        break;
                    }
                    backgroundVoices.push_back(std::move(bg));
                }
                // nna == 0 (cut): old voice is overwritten by the new note reset below.
            }

            voice.sample        = mappedSample;
            voice.instrumentDef = voice.instrument > 0 && voice.instrument <= instruments.size()
                ? &instruments[voice.instrument - 1] : nullptr;
            voice.position       = 0.0;
            voice.envelopeTick   = 0;
            voice.panEnvelopeTick = 0;
            voice.envelopeVolume = (voice.instrumentDef &&
            (voice.volEnvOverride == 1 ||
             (voice.volEnvOverride != 0 && voice.instrumentDef->HasVolumeEnvelope())))
                ? EvaluateVolumeEnvelope(*voice.instrumentDef, 0)
                : 1.0f;
            voice.envelopePanning = 0.0f;
            voice.fadeOutVolume   = 65536;
            voice.noteReleased    = false;
            voice.reversePlayback = false;
            voice.pingPongReverse = false; // reset ping-pong direction for the new note
            voice.volume          = mappedSample->defaultVolume;
            voice.baseVolume      = mappedSample->defaultVolume;
            // Panning priority (highest wins): sample > instrument > channel.
            // voice.panning already holds the channel default from initialisation.
            // Only override when the respective level explicitly has a panning set.
            if (voice.instrumentDef && voice.instrumentDef->hasPanningOverride) {
                voice.panning = voice.instrumentDef->defaultPanning;
            }
            if (mappedSample->hasPanningOverride) {
                voice.panning = mappedSample->defaultPanning;
            }
            voice.basePanning = voice.panning;
            // Reset sample auto-vibrato state for the new note.
            voice.autoVibratoPos   = 0;
            voice.autoVibratoDepth = 0;
            voice.step     = noteStep;
            voice.baseStep = voice.step;
            // A fresh note retriggers oscillator phases and clears any pending cut.
            voice.vibratoPos  = 0;
            voice.tremoloPos  = 0;
            voice.panbrelloPos = 0;
            voice.noteCutTick  = 255;
            // Always ramp from 0 on a new note. Inheriting smoothGain meant the new note's
            // first sample played at full amplitude with a waveform discontinuity from the
            // old sample position, which is a louder click than a brief ramp-up from silence.
            voice.smoothGain = 0.0f;
            voice.deactivatePending = false;
            voice.active            = true;
        }
    }

    // Volume column. Store the decoded command/parameter for the per-tick phase, then apply the
    // commands that take effect immediately on tick 0 (set volume/panning, fine slides).
    voice.volCommand = volCommand;
    voice.volParam = volParam;
    switch (volCommand) {
    case VOLCMD_VOLUME:
        voice.volume = volParam;
        voice.baseVolume = volParam;
        break;
    case VOLCMD_FINEVOLUP:
        voice.volume = ClampVolume(static_cast<int>(voice.volume) + static_cast<int>(volParam));
        voice.baseVolume = voice.volume;
        break;
    case VOLCMD_FINEVOLDOWN:
        voice.volume = ClampVolume(static_cast<int>(voice.volume) - static_cast<int>(volParam));
        voice.baseVolume = voice.volume;
        break;
    case VOLCMD_PANNING:
        voice.panning    = static_cast<uint8_t>((static_cast<uint16_t>(volParam) * 255U) / 64U);
        voice.basePanning = voice.panning;
        break;
    default:
        // Slides / pitch / tone-portamento / vibrato are continuous: applied per tick below.
        break;
    }

    if (IsUnsupportedMPTMCommand(effect, data)) {
#if defined(_DEBUG_MPTMPlayer_)
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"MPTMPlayer: OpenMPT plugin/MIDI/sample-cue effect ignored.");
#endif
    }

    switch (effect) {
    case 0x01: // Axx: set speed
        if (data > 0) {
            speed = data;
        }
        break;

    case 0x02: // Bxx: position jump
        sequencePosition = data < orders.size() ? data : 0;
        currentRow = 0;
        orderJumpPending = true;
        break;

    case 0x03: { // Cxx: pattern break, BCD row
        const uint8_t row = static_cast<uint8_t>(((data >> 4) * 10) + (data & 0x0F));
        const uint16_t maxRow = unpackedPatterns[currentPatternIndex].empty()
            ? 0
            : static_cast<uint16_t>(unpackedPatterns[currentPatternIndex].size() - 1);
        nextRowOverride = std::min<uint16_t>(row, maxRow);
        ++sequencePosition;
        rowBreakPending = true;
        break;
    }

    case 0x04: // Dxy: volume slide -- fine slides (DxF/DFy) fire once at tick 0 per IT spec.
        if (data != 0) {
            voice.lastVolumeSlide = data;
        }
        {
            const uint8_t slideData = voice.lastVolumeSlide;
            const uint8_t up   = slideData >> 4;
            const uint8_t down = slideData & 0x0F;
            if (up == 0x0F && down != 0) {
                voice.volume    = ClampVolume(static_cast<int>(voice.volume) - static_cast<int>(down));
                voice.baseVolume = voice.volume;
            } else if (down == 0x0F && up != 0) {
                voice.volume    = ClampVolume(static_cast<int>(voice.volume) + static_cast<int>(up));
                voice.baseVolume = voice.volume;
            }
        }
        break;

    case 0x05: // Exx: pitch slide down -- fine/xfine variants fire once at tick 0 per IT spec.
        if (data != 0) {
            voice.lastPitchSlide = data;
        }
        if (voice.active) {
            const uint8_t amount = voice.lastPitchSlide;
            if ((amount & 0xF0) == 0xF0 && (amount & 0x0F) > 0) {
                voice.step    /= std::pow(2.0, static_cast<double>(amount & 0x0F) / 3072.0);
                voice.baseStep = voice.step;
            } else if ((amount & 0xF0) == 0xE0 && (amount & 0x0F) > 0) {
                voice.step    /= std::pow(2.0, static_cast<double>(amount & 0x0F) / 768.0);
                voice.baseStep = voice.step;
            }
        }
        break;

    case 0x06: // Fxx: pitch slide up -- fine/xfine variants fire once at tick 0 per IT spec.
        if (data != 0) {
            voice.lastPitchSlide = data;
        }
        if (voice.active) {
            const uint8_t amount = voice.lastPitchSlide;
            if ((amount & 0xF0) == 0xF0 && (amount & 0x0F) > 0) {
                voice.step    *= std::pow(2.0, static_cast<double>(amount & 0x0F) / 3072.0);
                voice.baseStep = voice.step;
            } else if ((amount & 0xF0) == 0xE0 && (amount & 0x0F) > 0) {
                voice.step    *= std::pow(2.0, static_cast<double>(amount & 0x0F) / 768.0);
                voice.baseStep = voice.step;
            }
        }
        break;

    case 0x07: // Gxx: tone portamento -- memory is shared with Lxy but NOT with Exx/Fxx
        if (data != 0) {
            voice.lastPortamento = data;
        }
        if (effect == 0x07 && hasPlayableNote && voice.sample) {
            voice.targetStep = NoteToFrequency(ITNoteToLinear(mappedNote), voice.sample->c5Speed) /
                static_cast<double>(MPTM_SAMPLE_RATE);
        }
        break;

    case 0x08: // Hxy: vibrato -- x=speed, y=depth; both stored in lastVibrato
    case 0x15: // Uxy: fine vibrato
        if (data != 0) {
            voice.lastVibrato = data;
        }
        break;

    case 0x0B: // Kxy: continue vibrato + volume slide -- xy is the slide param (like Dxy),
               // NOT the vibrato param; vibrato continues from whatever lastVibrato holds.
        if (data != 0) {
            voice.lastVolumeSlide = data;
        }
        break;

    case 0x09: // Ixy: tremor
        voice.tremorOnTicks = (data >> 4) + 1;
        voice.tremorOffTicks = (data & 0x0F) + 1;
        voice.tremorCounter = 0;
        break;

    case 0x0C: // Lxy: tone portamento + volume slide (x=vol-slide nibble, y=portatamento nibble)
        // Each nibble is independent memory: only update the slot when its nibble is non-zero.
        {
            const uint8_t volNibble  = data >> 4;
            const uint8_t portNibble = data & 0x0F;
            if (volNibble  != 0) { voice.lastVolumeSlide = static_cast<uint8_t>(volNibble  << 4); }
            if (portNibble != 0) { voice.lastPortamento  = portNibble; }
        }
        if (hasPlayableNote && voice.sample) {
            voice.targetStep = NoteToFrequency(ITNoteToLinear(mappedNote), voice.sample->c5Speed) /
                static_cast<double>(MPTM_SAMPLE_RATE);
        }
        break;

    case 0x0D: // Mxx: channel volume (0..64 multiplier, persists across note triggers)
        voice.channelVolume = ClampVolume(data);
        break;

    case 0x0E: // Nxy: channel volume slide -- uses its own memory slot, separate from Dxy.
        if (data != 0) {
            voice.lastChannelVolumeSlide = data;
        }
        break;

    case 0x0F: // Oxx: sample offset. O00 repeats the last offset. The high byte comes from
               // SAy (NOT S9x). IT only applies the offset when a note is present on the row.
        if (data != 0) {
            voice.lastSampleOffset = data;
        }
        if (voice.sample && hasPlayableNote && !tonePortamentoNote) {
            const uint32_t offset = (static_cast<uint32_t>(voice.lastSampleOffsetHigh) << 16) |
                (static_cast<uint32_t>(voice.lastSampleOffset) << 8);
            voice.position = static_cast<double>(std::min<uint32_t>(offset, voice.sample->length));
        }
        break;

    case 0x10: // Pxy: panning slide -- save to its own dedicated memory slot.
        if (data != 0) {
            voice.lastPanningSlide = data;
        }
        break;

    case 0x11: // Qxy: retrigger note with volume change
        if (data != 0) {
            voice.lastRetrig = data;
        }
        break;

    case 0x12: // Rxy: tremolo
        if (data != 0) {
            voice.lastTremolo = data;
        }
        break;

    case 0x13: // Sxy: extended effects
        ApplySpecialCommand(voice, data, true);
        break;

    case 0x14: // Txx: set tempo, or fine-slide it once per row.
        // T20..TFF: set absolute tempo.
        // T01..T0F: slide tempo up by low nibble (once on tick 0).
        // T10..T1F: slide tempo down by low nibble (once on tick 0).
        if (data >= 0x20) {
            tempo = data;
        }
        else if (data > 0x00 && data < 0x10) {
            // Fine tempo slide up.
            tempo = static_cast<uint16_t>(std::min<int>(255, static_cast<int>(tempo) + (data & 0x0F)));
        }
        else if (data >= 0x10) {
            // Fine tempo slide down.
            tempo = static_cast<uint16_t>(std::max<int>(32,  static_cast<int>(tempo) - (data & 0x0F)));
        }
        break;

    case 0x16: // Vxx: global volume (0..128, full IT range, no halving)
        moduleGlobalVolume = static_cast<uint8_t>(std::min<uint16_t>(data, 128));
        break;

    case 0x17: // Wxy: global volume slide -- fine slides fire once at tick 0 per IT spec.
        if (data != 0) {
            voice.lastGlobalVolumeSlide = data;
        }
        {
            const uint8_t slideData = voice.lastGlobalVolumeSlide;
            const uint8_t up   = slideData >> 4;
            const uint8_t down = slideData & 0x0F;
            int vol = static_cast<int>(moduleGlobalVolume);
            if (up == 0x0F && down != 0) {
                vol -= down;
                moduleGlobalVolume = static_cast<uint8_t>(std::clamp(vol, 0, 128));
            } else if (down == 0x0F && up != 0) {
                vol += up;
                moduleGlobalVolume = static_cast<uint8_t>(std::clamp(vol, 0, 128));
            }
        }
        break;

    case 0x18: // Xxx: set panning. IT stores the full 0x00 (left) .. 0xFF (right) range with
               // 0x80 as centre, so the byte maps straight onto the 0..255 voice panning.
        voice.panning    = data;
        voice.basePanning = data;
        break;

    case 0x19: // Yxy: panbrello -- save to dedicated memory, not the shared effectData slot.
        if (data != 0) {
            voice.lastPanbrello = data;
        }
        break;

    case 0x1C: { // Xxx (CMD_DELAYCUT): cut-only variant -- delay nibble is 0, so the note
                 // already triggered above. We only need to arm the note cut tick here.
        const uint8_t cutTick = data & 0x0F;
        if (cutTick > 0) {
            voice.noteCutTick = cutTick;
        }
        break;
    }

    case 0x1E: // qxx (CMD_FINETUNE): fractional pitch finetune up -- 64 steps per semitone
        if (voice.active && data > 0) {
            voice.step     *= std::pow(2.0, static_cast<double>(data) / 768.0);
            voice.baseStep  = voice.step;
        }
        break;

    case 0x1F: // txx (CMD_FINETUNE_BKWD): fractional pitch finetune down -- 64 steps per semitone
        if (voice.active && data > 0) {
            voice.step     /= std::pow(2.0, static_cast<double>(data) / 768.0);
            voice.baseStep  = voice.step;
        }
        break;

    default:
        break;
    }

    voice.effect = effect;
    voice.effectData = data;
}

void MPTMPlayer::ApplyTickEffects() {
    // Lock is held by the caller (FillAudioBuffer via MixAudio); do NOT re-lock here.

    for (size_t channel = 0; channel < voices.size(); ++channel) {
        MPTMChannelVoice& voice = voices[channel];

        AdvanceVoiceEnvelope(voice);

        if (voice.delayedNotePending && tick == voice.delayTicks) {
            voice.delayedNotePending = false;
            TriggerEvent(channel, voice.delayedEvent, true);
        }

        if (voice.noteCutTick != 255 && tick == voice.noteCutTick) {
            if (voice.active && voice.smoothGain > 0.001f) {
                voice.volume = 0;
                voice.deactivatePending = true;
            } else {
                voice.active = false;
                voice.deactivatePending = false;
            }
            voice.noteCutTick = 255;
        }

        if (!voice.sample || !voice.active) {
            continue;
        }

        switch (voice.effect) {
        case 0x04: // Dxy
            ApplyVolumeSlide(voice);
            break;

        case 0x05: { // Exx: portamento DOWN
            // IT linear frequency mode:
            //   Normal (0x01-0xDF): speed * 4 fine-periods per tick  → factor = 2^(speed / 192)
            //   Fine   (0xE prefix) / X-fine (0xF prefix): already fired once at tick 0 in TriggerEvent.
            const uint8_t amount = voice.lastPitchSlide != 0 ? voice.lastPitchSlide : voice.effectData;
            if ((amount & 0xF0) == 0xF0 || (amount & 0xF0) == 0xE0) {
                break; // fine/xfine already applied at tick 0; nothing to do on tick 1+
            }
            voice.step    /= std::pow(2.0, static_cast<double>(amount) / 192.0);
            voice.baseStep = voice.step;
            break;
        }

        case 0x06: { // Fxx: portamento UP
            const uint8_t amount = voice.lastPitchSlide != 0 ? voice.lastPitchSlide : voice.effectData;
            if ((amount & 0xF0) == 0xF0 || (amount & 0xF0) == 0xE0) {
                break; // fine/xfine already applied at tick 0; nothing to do on tick 1+
            }
            voice.step    *= std::pow(2.0, static_cast<double>(amount) / 192.0);
            voice.baseStep = voice.step;
            break;
        }

        case 0x07:
            ApplyPortamento(voice);
            break;

        case 0x08:
            ApplyVibrato(voice, false);
            break;

        case 0x09: {
            const uint8_t cycle = voice.tremorOnTicks + voice.tremorOffTicks;
            if (cycle > 0) {
                voice.volume = (voice.tremorCounter % cycle) < voice.tremorOnTicks ? voice.baseVolume : 0;
                ++voice.tremorCounter;
            }
            break;
        }

        case 0x0A: { // Jxy: arpeggio
            const uint8_t offsetA = voice.effectData >> 4;
            const uint8_t offsetB = voice.effectData & 0x0F;
            const uint8_t semitoneOffset = (tick % 3 == 1) ? offsetA : ((tick % 3 == 2) ? offsetB : 0);
            const int transposed = std::clamp(static_cast<int>(ITNoteToLinear(voice.note)) + static_cast<int>(semitoneOffset), 0, 119);
            voice.step = NoteToFrequency(static_cast<uint8_t>(transposed), voice.sample->c5Speed) /
                static_cast<double>(MPTM_SAMPLE_RATE);
            break;
        }

        case 0x0B:
            ApplyVibrato(voice, false);
            ApplyVolumeSlide(voice);
            break;

        case 0x0C:
            ApplyPortamento(voice);
            ApplyVolumeSlide(voice);
            break;

        case 0x0E: // Nxy: channel volume slide (separate memory from Dxy)
            ApplyChannelVolumeSlide(voice);
            break;

        case 0x10: // Pxy: panning slide
            ApplyPanningSlide(voice);
            break;

        case 0x11:
            ApplyRetrig(voice);
            break;

        case 0x12:
            ApplyTremolo(voice);
            break;

        case 0x13:
            ApplySpecialCommand(voice, voice.effectData, false);
            break;

        case 0x15:
            ApplyVibrato(voice, true);
            break;

        case 0x17:
            ApplyGlobalVolumeSlide(voice);
            break;

        case 0x19:
            ApplyPanbrello(voice);
            break;

        default:
            break;
        }

        // The volume column runs in parallel with the effect column, so apply its continuous
        // commands (slides, pitch slides, tone portamento, vibrato) every tick as well.
        ApplyVolumeColumnTick(voice);

        // Sample auto-vibrato: per-sample sine/ramp/square/random pitch modulation, swept in.
        if (voice.active && voice.sample && voice.sample->autoVibratoDepth > 0) {
            const MPTMSample& s = *voice.sample;
            // Ramp up depth linearly over autoVibratoRate ticks (0 = instant full depth).
            if (s.autoVibratoRate == 0) {
                voice.autoVibratoDepth = static_cast<uint16_t>(s.autoVibratoDepth) << 8;
            } else {
                const uint16_t maxDepth = static_cast<uint16_t>(s.autoVibratoDepth) << 8;
                voice.autoVibratoDepth = static_cast<uint16_t>(
                    std::min<uint32_t>(voice.autoVibratoDepth + (maxDepth / s.autoVibratoRate), maxDepth));
            }
            voice.autoVibratoPos = static_cast<uint8_t>(voice.autoVibratoPos + s.autoVibratoSpeed);

            int delta = 0;
            switch (s.autoVibratoType & 0x03) {
            case 0: // sine
                delta = static_cast<int>(std::sin(voice.autoVibratoPos * (2.0 * 3.14159265358979323846 / 256.0)) * voice.autoVibratoDepth);
                break;
            case 1: // ramp-down
                delta = static_cast<int>(voice.autoVibratoDepth) - (static_cast<int>(voice.autoVibratoPos) * static_cast<int>(voice.autoVibratoDepth) * 2 / 256);
                break;
            case 2: // square
                delta = (voice.autoVibratoPos < 128) ? static_cast<int>(voice.autoVibratoDepth) : -static_cast<int>(voice.autoVibratoDepth);
                break;
            case 3: // random
                delta = ((rand() & 1) ? 1 : -1) * static_cast<int>(voice.autoVibratoDepth);
                break;
            }
            // IT auto-vibrato delta is in units of 1/64th of a semitone (per 256 depth units).
            const double semitones = static_cast<double>(delta) / (64.0 * 256.0);
            voice.step *= std::pow(2.0, semitones / 12.0);
        }
    }

    // Advance envelopes and fadeout for NNA background voices.
    for (MPTMChannelVoice& bv : backgroundVoices) {
        AdvanceVoiceEnvelope(bv);
    }
}

void MPTMPlayer::ApplyVolumeColumnTick(MPTMChannelVoice& voice) {
    switch (voice.volCommand) {
    case VOLCMD_VOLSLIDEUP:
        voice.volume = ClampVolume(static_cast<int>(voice.volume) + static_cast<int>(voice.volParam));
        voice.baseVolume = voice.volume;
        break;

    case VOLCMD_VOLSLIDEDOWN:
        voice.volume = ClampVolume(static_cast<int>(voice.volume) - static_cast<int>(voice.volParam));
        voice.baseVolume = voice.volume;
        break;

    case VOLCMD_PITCHDOWN: // 'e' -- per-tick pitch slide down (IT scales the parameter by 4)
        voice.step /= std::pow(2.0, (static_cast<double>(voice.volParam) * 4.0) / 768.0);
        voice.baseStep = voice.step;
        break;

    case VOLCMD_PITCHUP: // 'f' -- per-tick pitch slide up
        voice.step *= std::pow(2.0, (static_cast<double>(voice.volParam) * 4.0) / 768.0);
        voice.baseStep = voice.step;
        break;

    case VOLCMD_TONEPORTA: { // 'g' -- glide toward the target note at the table speed
        if (voice.targetStep > 0.0 && voice.step > 0.0) {
            // Treat the table-mapped Gxx speed exactly like ApplyPortamento: 2^(speed/768) per tick.
            const double speed  = static_cast<double>(VolColumnPortaSpeed[std::min<uint8_t>(voice.volParam, 9)]);
            const double factor = std::pow(2.0, speed / 768.0);
            if (voice.step < voice.targetStep) {
                voice.step = std::min(voice.step * factor, voice.targetStep);
            }
            else if (voice.step > voice.targetStep) {
                voice.step = std::max(voice.step / factor, voice.targetStep);
            }
            voice.baseStep = voice.step;
        }
        break;
    }

    case VOLCMD_VIBRATO: { // 'h' -- depth from the volume column, speed from vibrato memory
        const uint8_t speedValue = voice.lastVibrato >> 4;
        const double delta = Waveform(voice.vibratoPos, voice.vibratoWaveform) * static_cast<double>(voice.volParam) / 2048.0;
        voice.step = std::max(0.0, voice.baseStep + delta);
        voice.vibratoPos = static_cast<uint8_t>(voice.vibratoPos + speedValue);
        break;
    }

    default:
        break;
    }
}

void MPTMPlayer::ApplyVolumeSlide(MPTMChannelVoice& voice) {
    const uint8_t data = voice.lastVolumeSlide != 0 ? voice.lastVolumeSlide : voice.effectData;
    const uint8_t up   = data >> 4;
    const uint8_t down = data & 0x0F;

    // Fine slides (DxF / DFy) already applied once at tick 0 in TriggerEvent; skip here.
    if (up == 0x0F || down == 0x0F) {
        return;
    }

    int volume = voice.volume;
    if (up != 0 && down == 0) {
        volume += up;
    }
    else if (down != 0 && up == 0) {
        volume -= down;
    }

    voice.volume    = ClampVolume(volume);
    voice.baseVolume = voice.volume;
}

void MPTMPlayer::ApplyGlobalVolumeSlide(MPTMChannelVoice& voice) {
    const uint8_t data = voice.lastGlobalVolumeSlide != 0 ? voice.lastGlobalVolumeSlide : voice.effectData;
    const uint8_t up   = data >> 4;
    const uint8_t down = data & 0x0F;

    // Fine slides (WxF / WFy) already applied once at tick 0 in TriggerEvent; skip here.
    if (up == 0x0F || down == 0x0F) {
        return;
    }

    int volume = static_cast<int>(moduleGlobalVolume);
    if (up != 0 && down == 0) {
        volume += up;
    }
    else if (down != 0 && up == 0) {
        volume -= down;
    }

    moduleGlobalVolume = static_cast<uint8_t>(std::clamp(volume, 0, 128));
}

void MPTMPlayer::ApplyChannelVolumeSlide(MPTMChannelVoice& voice) {
    // Nxy: slide the channel volume (Mxx multiplier), not the voice volume.
    const uint8_t data = voice.lastChannelVolumeSlide != 0 ? voice.lastChannelVolumeSlide : voice.effectData;
    const uint8_t up   = data >> 4;
    const uint8_t down = data & 0x0F;
    int vol = voice.channelVolume;

    if (up == 0x0F && down != 0) {
        if (tick == 1) { vol -= down; }
    }
    else if (down == 0x0F && up != 0) {
        if (tick == 1) { vol += up; }
    }
    else if (up != 0 && down == 0) {
        vol += up;
    }
    else if (down != 0 && up == 0) {
        vol -= down;
    }

    voice.channelVolume = ClampVolume(vol);
}

void MPTMPlayer::ApplyPanningSlide(MPTMChannelVoice& voice) {
    // Pxy: x slides right (increases panning), y slides left (decreases panning).
    // IT scales the nibble by 4 for the 0..255 panning range.
    const uint8_t data = voice.lastPanningSlide != 0 ? voice.lastPanningSlide : voice.effectData;
    const uint8_t up   = data >> 4;
    const uint8_t down = data & 0x0F;
    // Slide the base panning (not the panbrello-modified output) so the effect
    // accumulates correctly even when panbrello is also running.
    int pan = voice.basePanning;

    if (up != 0 && down == 0) {
        pan += up * 4;
    }
    else if (down != 0 && up == 0) {
        pan -= down * 4;
    }

    voice.basePanning = static_cast<uint8_t>(std::clamp(pan, 0, 255));
    voice.panning     = voice.basePanning;
}

void MPTMPlayer::ApplyPortamento(MPTMChannelVoice& voice) {
    if (voice.targetStep <= 0.0 || voice.step <= 0.0) {
        return;
    }

    // IT linear frequency: Gxx slides by speed/64 semitones per tick (1 unit = 1/64 semitone).
    // factor = 2^(speed / (64 * 12)) = 2^(speed / 768).
    const double speed  = static_cast<double>(voice.lastPortamento != 0 ? voice.lastPortamento : voice.effectData);
    const double factor = std::pow(2.0, speed / 768.0);

    if (voice.step < voice.targetStep) {
        voice.step = std::min(voice.step * factor, voice.targetStep);
    }
    else if (voice.step > voice.targetStep) {
        voice.step = std::max(voice.step / factor, voice.targetStep);
    }

    // S1x glissando: snap the current pitch to the nearest semitone (round toward target).
    if (voice.glissandoControl && voice.sample && voice.sample->c5Speed > 0) {
        const double c5Step = static_cast<double>(voice.sample->c5Speed) / static_cast<double>(MPTM_SAMPLE_RATE);
        const double semitones = std::log2(voice.step / c5Step) * 12.0;
        const double rounded   = std::round(semitones);
        voice.step = c5Step * std::pow(2.0, rounded / 12.0);
    }

    voice.baseStep = voice.step;
}

void MPTMPlayer::ApplyVibrato(MPTMChannelVoice& voice, bool fine) {
    const uint8_t data = voice.lastVibrato != 0 ? voice.lastVibrato : voice.effectData;
    const uint8_t speedValue = data >> 4;
    const uint8_t depth = data & 0x0F;
    const double divisor = fine ? 8192.0 : 2048.0;
    const double delta = Waveform(voice.vibratoPos, voice.vibratoWaveform) * static_cast<double>(depth) / divisor;
    voice.step = std::max(0.0, voice.baseStep + delta);
    voice.vibratoPos = static_cast<uint8_t>(voice.vibratoPos + speedValue);
}

void MPTMPlayer::ApplyTremolo(MPTMChannelVoice& voice) {
    const uint8_t data = voice.lastTremolo != 0 ? voice.lastTremolo : voice.effectData;
    const uint8_t speedValue = data >> 4;
    const uint8_t depth = data & 0x0F;
    const int delta = static_cast<int>(Waveform(voice.tremoloPos, voice.tremoloWaveform) * static_cast<double>(depth));
    voice.volume = ClampVolume(static_cast<int>(voice.baseVolume) + delta);
    voice.tremoloPos = static_cast<uint8_t>(voice.tremoloPos + speedValue);
}

void MPTMPlayer::ApplyPanbrello(MPTMChannelVoice& voice) {
    const uint8_t data       = voice.lastPanbrello != 0 ? voice.lastPanbrello : voice.effectData;
    const uint8_t speedValue = data >> 4;
    const uint8_t depth      = data & 0x0F;
    const int delta = static_cast<int>(Waveform(voice.panbrelloPos, voice.panbrelloWaveform) * static_cast<double>(depth) * 4.0);
    // Apply oscillating delta around the stored base panning so the effect
    // doesn't walk voice.panning into a rail and get permanently stuck.
    voice.panning = static_cast<uint8_t>(std::clamp(static_cast<int>(voice.basePanning) + delta, 0, 255));
    voice.panbrelloPos = static_cast<uint8_t>(voice.panbrelloPos + speedValue);
}

void MPTMPlayer::ApplyRetrig(MPTMChannelVoice& voice) {
    const uint8_t data = voice.lastRetrig != 0 ? voice.lastRetrig : voice.effectData;
    const uint8_t interval = data & 0x0F;
    const uint8_t volumeCommand = data >> 4;

    if (interval == 0 || (tick % interval) != 0) {
        return;
    }

    voice.position = 0.0;
    // Do NOT reset smoothGain here: zeroing it mid-playback creates an abrupt amplitude drop
    // before the ramp recovers, which sounds like a click. Let the ramp continue naturally;
    // the waveform discontinuity from resetting position is already softened by it.
    int volume = voice.volume;
    switch (volumeCommand) {
    case 0x1: volume -= 1; break;
    case 0x2: volume -= 2; break;
    case 0x3: volume -= 4; break;
    case 0x4: volume -= 8; break;
    case 0x5: volume -= 16; break;
    case 0x6: volume = (volume * 2) / 3; break;
    case 0x7: volume /= 2; break;
    case 0x9: volume += 1; break;
    case 0xA: volume += 2; break;
    case 0xB: volume += 4; break;
    case 0xC: volume += 8; break;
    case 0xD: volume += 16; break;
    case 0xE: volume = (volume * 3) / 2; break;
    case 0xF: volume *= 2; break;
    default: break;
    }

    voice.volume = ClampVolume(volume);
    voice.baseVolume = voice.volume;
}

float MPTMPlayer::EvaluateVolumeEnvelope(const MPTMInstrument& instrument, uint16_t envelopeTick) const {
    const uint8_t pointCount = instrument.volumeEnvelopePointCount;
    if (!instrument.HasVolumeEnvelope()) {
        return 1.0f;
    }

    if (envelopeTick <= instrument.volumeEnvelopeTicks[0]) {
        return static_cast<float>(instrument.volumeEnvelopeValues[0]) / 64.0f;
    }

    for (uint8_t point = 1; point < pointCount; ++point) {
        const uint16_t leftTick = instrument.volumeEnvelopeTicks[point - 1];
        const uint16_t rightTick = instrument.volumeEnvelopeTicks[point];
        if (envelopeTick <= rightTick) {
            const float leftValue = static_cast<float>(instrument.volumeEnvelopeValues[point - 1]);
            const float rightValue = static_cast<float>(instrument.volumeEnvelopeValues[point]);
            const uint16_t tickSpan = rightTick > leftTick ? static_cast<uint16_t>(rightTick - leftTick) : 1;
            const float t = static_cast<float>(envelopeTick - leftTick) / static_cast<float>(tickSpan);
            return (leftValue + (rightValue - leftValue) * t) / 64.0f;
        }
    }

    return static_cast<float>(instrument.volumeEnvelopeValues[pointCount - 1]) / 64.0f;
}

float MPTMPlayer::EvaluatePanningEnvelope(const MPTMInstrument& instrument, uint16_t envelopeTick) const {
    const uint8_t pointCount = instrument.panningEnvelopePointCount;
    if (!instrument.HasPanningEnvelope()) {
        return 0.0f;
    }

    if (envelopeTick <= instrument.panningEnvelopeTicks[0]) {
        return static_cast<float>(instrument.panningEnvelopeValues[0]) / 32.0f;
    }

    for (uint8_t point = 1; point < pointCount; ++point) {
        const uint16_t leftTick  = instrument.panningEnvelopeTicks[point - 1];
        const uint16_t rightTick = instrument.panningEnvelopeTicks[point];
        if (envelopeTick <= rightTick) {
            const float leftValue  = static_cast<float>(instrument.panningEnvelopeValues[point - 1]);
            const float rightValue = static_cast<float>(instrument.panningEnvelopeValues[point]);
            const uint16_t tickSpan = rightTick > leftTick ? static_cast<uint16_t>(rightTick - leftTick) : 1;
            const float t = static_cast<float>(envelopeTick - leftTick) / static_cast<float>(tickSpan);
            return (leftValue + (rightValue - leftValue) * t) / 32.0f;
        }
    }

    return static_cast<float>(instrument.panningEnvelopeValues[pointCount - 1]) / 32.0f;
}

void MPTMPlayer::AdvanceVoiceEnvelope(MPTMChannelVoice& voice) {
    const MPTMInstrument* instrument = voice.instrumentDef;
    if (!voice.active || !instrument) {
        return;
    }

    const bool volEnvActive = (voice.volEnvOverride == 1) ||
        (voice.volEnvOverride != 0 && instrument->HasVolumeEnvelope());
    const bool panEnvActive = (voice.panEnvOverride == 1) ||
        (voice.panEnvOverride != 0 && instrument->HasPanningEnvelope());

    if (volEnvActive) {
        voice.envelopeVolume = EvaluateVolumeEnvelope(*instrument, voice.envelopeTick);

        const uint8_t lastPoint = static_cast<uint8_t>(instrument->volumeEnvelopePointCount - 1);
        const uint16_t lastTick = instrument->volumeEnvelopeTicks[lastPoint];
        const bool sustainEnabled = (instrument->volumeEnvelopeFlags & 0x04) != 0 && !voice.noteReleased;
        const bool loopEnabled = (instrument->volumeEnvelopeFlags & 0x02) != 0;

        if (sustainEnabled && instrument->volumeEnvelopeSustainStart <= instrument->volumeEnvelopeSustainEnd &&
            instrument->volumeEnvelopeSustainEnd <= lastPoint &&
            voice.envelopeTick >= instrument->volumeEnvelopeTicks[instrument->volumeEnvelopeSustainEnd]) {
            voice.envelopeTick = instrument->volumeEnvelopeTicks[instrument->volumeEnvelopeSustainStart];
        }
        else if (loopEnabled && instrument->volumeEnvelopeLoopStart <= instrument->volumeEnvelopeLoopEnd &&
            instrument->volumeEnvelopeLoopEnd <= lastPoint &&
            voice.envelopeTick >= instrument->volumeEnvelopeTicks[instrument->volumeEnvelopeLoopEnd]) {
            voice.envelopeTick = instrument->volumeEnvelopeTicks[instrument->volumeEnvelopeLoopStart];
        }
        else if (voice.envelopeTick < lastTick) {
            ++voice.envelopeTick;
        }
        else if (instrument->volumeEnvelopeValues[lastPoint] == 0) {
            voice.volume = 0;
            voice.deactivatePending = true;
        }
    }

    // Panning envelope.
    if (panEnvActive) {
        voice.envelopePanning = EvaluatePanningEnvelope(*instrument, voice.panEnvelopeTick);

        const uint8_t  lastPoint = static_cast<uint8_t>(instrument->panningEnvelopePointCount - 1);
        const uint16_t lastTick  = instrument->panningEnvelopeTicks[lastPoint];
        const bool sustainEnabled = (instrument->panningEnvelopeFlags & 0x04) != 0 && !voice.noteReleased;
        const bool loopEnabled    = (instrument->panningEnvelopeFlags & 0x02) != 0;

        if (sustainEnabled &&
            instrument->panningEnvelopeSustainStart <= instrument->panningEnvelopeSustainEnd &&
            instrument->panningEnvelopeSustainEnd <= lastPoint &&
            voice.panEnvelopeTick >= instrument->panningEnvelopeTicks[instrument->panningEnvelopeSustainEnd]) {
            voice.panEnvelopeTick = instrument->panningEnvelopeTicks[instrument->panningEnvelopeSustainStart];
        }
        else if (loopEnabled &&
            instrument->panningEnvelopeLoopStart <= instrument->panningEnvelopeLoopEnd &&
            instrument->panningEnvelopeLoopEnd <= lastPoint &&
            voice.panEnvelopeTick >= instrument->panningEnvelopeTicks[instrument->panningEnvelopeLoopEnd]) {
            voice.panEnvelopeTick = instrument->panningEnvelopeTicks[instrument->panningEnvelopeLoopStart];
        }
        else if (voice.panEnvelopeTick < lastTick) {
            ++voice.panEnvelopeTick;
        }
    }

    if (voice.noteReleased && instrument->fadeOut > 0) {
        const uint32_t fadeStep = std::max<uint32_t>(1, instrument->fadeOut);
        voice.fadeOutVolume = fadeStep >= voice.fadeOutVolume ? 0 : voice.fadeOutVolume - fadeStep;
        if (voice.fadeOutVolume == 0) {
            voice.volume = 0;
            voice.deactivatePending = true;
        }
    }
}

void MPTMPlayer::ApplySpecialCommand(MPTMChannelVoice& voice, uint8_t data, bool rowTick) {
    const uint8_t sub = data >> 4;
    const uint8_t value = data & 0x0F;

    if (rowTick) {
        switch (sub) {
        case 0x3: // S3x: set vibrato waveform (0 sine, 1 ramp down, 2 square, 3 random)
            voice.vibratoWaveform = value & 0x03;
            break;

        case 0x4: // S4x: set tremolo waveform
            voice.tremoloWaveform = value & 0x03;
            break;

        case 0x5: // S5x: set panbrello waveform
            voice.panbrelloWaveform = value & 0x03;
            break;

        case 0x6: // S6x: fine pattern delay -- add x extra ticks to THIS row only.
                  // (This is NOT a pattern loop; SBx is the loop command.)
            fineDelayTicks = value;
            break;

        case 0x8: // S8x: set coarse panning (0..15 -> 0..255)
            voice.panning    = static_cast<uint8_t>((static_cast<uint16_t>(value) * 255U) / 15U);
            voice.basePanning = voice.panning;
            break;

        case 0xA: // SAy: set the high byte of the sample offset (final offset += y * 0x10000).
                  // This -- not S9x -- is the IT high-offset command consumed by Oxx.
            voice.lastSampleOffsetHigh = value;
            break;

        case 0xB: // SBx: pattern loop. SB0 marks the loopback row; SBx replays the block x times.
            if (value == 0) {
                patternLoopRow = static_cast<uint8_t>(currentRow);
            }
            else if (patternLoopCount == 0) {
                patternLoopCount = value;
                patternLoopPending = true;
            }
            else if (--patternLoopCount > 0) {
                patternLoopPending = true;
            }
            break;

        case 0xC: // SCx: note cut after x ticks (SC0 cuts immediately)
            voice.noteCutTick = value;
            if (value == 0) {
                if (voice.active && voice.smoothGain > 0.001f) {
                    voice.volume = 0;
                    voice.deactivatePending = true;
                } else {
                    voice.active = false;
                    voice.deactivatePending = false;
                }
            }
            break;

        case 0xE: // SEx: pattern delay for x rows. Arm it only once: the row keeps being
                  // re-processed while held, and re-setting the counter here would make it
                  // never reach zero (an infinite hang on the row).
            if (!inPatternDelay) {
                patternDelayTicks = value;
                inPatternDelay = true;
            }
            break;

        case 0x1: // S1x: glissando control for tone portamento (S10=off, S11=on).
            voice.glissandoControl = (value != 0);
            break;

        case 0x9: // S9x: sound control.
            // S9E = play sample backwards; S9F = play sample backwards (some trackers differ);
            // S90/S9F disambiguation: OpenMPT uses S9E=forward, S9F=reverse.
            if (value == 0x0E) {
                voice.reversePlayback = false;
            }
            else if (value == 0x0F) {
                voice.reversePlayback = true;
                // Start from the loop end (or sample end) when reversing a running sample.
                if (voice.active && voice.sample && voice.position <= 0.0) {
                    const bool hasLoop = voice.sample->IsLooped();
                    voice.position = static_cast<double>(
                        hasLoop ? voice.sample->loopEnd : voice.sample->length) - 1.0;
                }
            }
            break;

        case 0x7: // S7x: NNA + envelope on/off (per IT spec section 7.1)
            switch (value) {
            case 0x0: case 0x1: case 0x2: case 0x3:
                voice.nnaOverride = static_cast<int8_t>(value);
                break;
            case 0x4: voice.volEnvOverride = 0; break;
            case 0x5: voice.volEnvOverride = 1; break;
            case 0x6: voice.panEnvOverride = 0; break;
            case 0x7: voice.panEnvOverride = 1; break;
            // S78/S79 (pitch envelope) not implemented; S7A..S7F reserved.
            default: break;
            }
            break;

        // S0x filter, S2x finetune (legacy), and SFx MIDI macro have no native audio path.
        default:
            break;
        }
        return;
    }

    // Per-tick phase: SCx is the only sub-command that must fire on a later tick.
    if (sub == 0xC && tick == value) {
        if (voice.active && voice.smoothGain > 0.001f) {
            voice.volume = 0;
            voice.deactivatePending = true;
        } else {
            voice.active = false;
            voice.deactivatePending = false;
        }
        voice.noteCutTick = 255;
    }
}

void MPTMPlayer::AdvanceRow() {
    if (orderJumpPending) {
        return;
    }

    if (rowBreakPending) {
        currentRow = nextRowOverride;
        if (sequencePosition >= orders.size()) {
            sequencePosition = restartSequencePosition.load();
        }
        return;
    }

    // SBx pattern loop: jump straight back to the loopback row in the same pattern,
    // without the +1 a normal advance would add (which would skip the loop's first row).
    if (patternLoopPending) {
        patternLoopPending = false;
        currentRow = patternLoopRow;
        return;
    }

    ++currentRow;
    const uint16_t rows = currentPatternIndex < unpackedPatterns.size()
        ? static_cast<uint16_t>(unpackedPatterns[currentPatternIndex].size())
        : 64;
    if (currentRow >= rows) {
        currentRow = 0;
        ++sequencePosition;
        if (sequencePosition >= orders.size()) {
            sequencePosition = restartSequencePosition.load();
        }
    }
}

const MPTMSample* MPTMPlayer::ResolveSample(uint8_t instrument, uint8_t note, uint8_t& mappedNote) const {
    if (instrument == 0) {
        return nullptr;
    }

    if (!instruments.empty() && instrument <= instruments.size() && note > 0 && note < MPTM_NOTE_FADE) {
        const MPTMInstrument& inst = instruments[instrument - 1];
        // IT pattern notes are 1-based (1=C-0, 61=C-5); keyboard table is 0-based.
        const uint8_t linearNote = static_cast<uint8_t>(note - 1);
        const size_t noteIndex = static_cast<size_t>(std::min<uint8_t>(linearNote, 119));
        const uint8_t sampleIndex = inst.sampleMap[noteIndex];
        mappedNote = inst.noteMap[noteIndex] != 0 ? inst.noteMap[noteIndex] : linearNote;
        // sampleMap[note] == 0 explicitly means "no sample for this note" — do NOT fall through.
        if (sampleIndex == 0) return nullptr;
        if (sampleIndex <= samples.size()) {
            return &samples[sampleIndex - 1];
        }
        return nullptr;
    }

    // Sample-only fallback (no instrument table): also convert to 0-based.
    if (instrument <= samples.size()) {
        if (note > 0 && note < MPTM_NOTE_FADE) {
            mappedNote = static_cast<uint8_t>(note - 1);
        }
        return &samples[instrument - 1];
    }

    return nullptr;
}

double MPTMPlayer::NoteToFrequency(uint8_t note, uint32_t c5Speed) const {
    const int noteIndex = std::clamp<int>(note, 0, 119);
    const int c5Index = 5 * 12;
    return static_cast<double>(c5Speed) * std::pow(2.0, static_cast<double>(noteIndex - c5Index) / 12.0);
}

double MPTMPlayer::Waveform(uint8_t pos, uint8_t type) const {
    const uint8_t phase = pos & 0x3F;
    switch (type & 0x03) {
    case 1: // ramp down (sawtooth): +1 -> -1 across the cycle
        return 1.0 - (static_cast<double>(phase) / 32.0);
    case 2: // square
        return phase < 32 ? 1.0 : -1.0;
    case 3: { // random
        static thread_local std::mt19937 rng{ std::random_device{}() };
        static thread_local std::uniform_real_distribution<double> dist(-1.0, 1.0);
        return dist(rng);
    }
    default: // sine
        return std::sin(static_cast<double>(phase) * (3.14159265358979323846 * 2.0 / 64.0));
    }
}

uint8_t MPTMPlayer::DefaultPanningForChannel(size_t channel) const {
    // IT header only has 64 channel slots; channels 64-126 are MPTM extensions (default to centre).
    if (channel < 64) {
        const uint8_t pan = header.channelPan[channel];
        if ((pan & 0x80) != 0) {
            return 128;
        }
        return static_cast<uint8_t>((static_cast<uint16_t>(std::min<uint8_t>(pan, 64)) * 255U) / 64U);
    }

    return 128; // center for MPTM extension channels (64-126)
}

void MPTMPlayer::GotoSequenceID(uint16_t patternSeqID) {
    if (!isPlaying) {
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"MPTMPlayer: GotoSequenceID called while player is not active.");
        return;
    }

    if (patternSeqID >= orders.size()) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"MPTMPlayer: Invalid PatternSeqID " + std::to_wstring(patternSeqID));
        return;
    }

    SetFadeOut(1000);
    auto fadeStartTime = std::chrono::high_resolution_clock::now();
    while (fadeOutActive) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        fadeElapsedMs = static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::high_resolution_clock::now() - fadeStartTime).count());

        if (fadeElapsedMs >= fadeDurationMs) {
            fadeOutActive = false;
            currentVolume = 0;
        }
    }

    {
        std::lock_guard<std::mutex> lock(playbackMutex);
        sequencePosition = patternSeqID;
        restartSequencePosition = patternSeqID;
        currentRow = 0;
        tick = 0;
        samplesUntilNextTick = 0;
        rowBreakPending = false;
        orderJumpPending = false;
        patternDelayTicks = 0;
        fineDelayTicks = 0;
        inPatternDelay = false;
        patternLoopPending = false;
        backgroundVoices.clear();

        // Clear all voices so stale notes from the old sequence don't bleed into the fade-in.
        for (size_t channel = 0; channel < voices.size(); ++channel) {
            voices[channel] = MPTMChannelVoice{};
            voices[channel].panning = DefaultPanningForChannel(channel);
            if (channel < 64) {
                const bool headerMuted = (header.channelPan[channel] & 0x80) != 0;
                voices[channel].channelVolume = headerMuted ? 0 : std::min<uint8_t>(header.channelVolume[channel], 64);
            } else {
                voices[channel].channelVolume = 64;
            }
        }
    }

    SetFadeIn(1000);
}

void MPTMPlayer::PlaybackLoop() {
    using namespace std::chrono;

    // Tick advancement is now driven sample-accurately inside MixAudio (called from
    // FillAudioBuffer). This loop only needs to keep the DirectSound buffer filled and
    // handle fade-in/fade-out volume ramping.
    auto fadeClock = high_resolution_clock::now();
    while (isPlaying && !isTerminating) {
        FillAudioBuffer();

        if (!isPaused) {
            if (fadeInActive || fadeOutActive) {
                const auto fadeNow = high_resolution_clock::now();
                const uint32_t elapsedMs = static_cast<uint32_t>(
                    duration_cast<milliseconds>(fadeNow - fadeClock).count());
                fadeClock = fadeNow;

                const uint32_t duration = fadeDurationMs.load();
                if (duration > 0) {
                    fadeElapsedMs = std::min<uint32_t>(duration, fadeElapsedMs.load() + elapsedMs);
                    const float amount = static_cast<float>(fadeElapsedMs.load()) /
                                        static_cast<float>(duration);
                    if (fadeInActive) {
                        currentVolume = static_cast<uint8_t>(
                            std::clamp<int>(static_cast<int>(64.0f * amount), 0, 64));
                        if (fadeElapsedMs >= duration) {
                            fadeInActive = false;
                        }
                    } else {
                        currentVolume = static_cast<uint8_t>(
                            std::clamp<int>(static_cast<int>(64.0f * (1.0f - amount)), 0, 64));
                        if (fadeElapsedMs >= duration) {
                            fadeOutActive = false;
                        }
                    }
                }
            } else {
                fadeClock = high_resolution_clock::now();
            }
        }

        std::this_thread::sleep_for(milliseconds(1));
    }

    isTerminating = false;
}
