#include "Includes.h"
#include "ITPlayer.h"
#include "Debug.h"
#include "Configuration.h"

#include <cmath>
#include <limits>

extern Debug debug;
extern Configuration config;

#if defined(PLATFORM_WINDOWS)
    extern HWND hwnd;
    #pragma comment(lib, "dsound.lib")
    #pragma comment(lib, "dxguid.lib")
    #pragma comment(lib, "winmm.lib")
#endif

namespace {
    constexpr uint32_t IT_SAMPLE_RATE = 44100;
    constexpr uint8_t IT_MAX_CHANNELS = 64;
    constexpr uint16_t IT_MAX_VIRTUAL_VOICES = 256;
    constexpr uint8_t IT_NOTE_CUT = 254;
    constexpr uint8_t IT_NOTE_OFF = 255;
    constexpr uint8_t IT_NOTE_FADE = 253;

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

    int16_t FinalizeMixSample(int32_t value) {
        if (value >= -32768 && value <= 32767) {
            return static_cast<int16_t>(value);
        }

        // Accumulate voices at full precision, then soften only true overflow. This avoids
        // per-voice hard clipping, which is a common source of crackle in dense tracker rows.
        const float normalized = static_cast<float>(value) / 32768.0f;
        const float shaped = normalized / (1.0f + std::fabs(normalized));
        const int32_t scaled = static_cast<int32_t>(std::lround(shaped * 32767.0f));
        return static_cast<int16_t>(std::clamp<int32_t>(scaled, -32768, 32767));
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

    // IT maps the volume-column tone-portamento parameter (0..9) onto these Gxx speeds.
    constexpr uint8_t VolColumnPortaSpeed[10] = { 0, 1, 4, 8, 16, 32, 64, 96, 128, 255 };

    uint8_t ITNoteToLinear(uint8_t note) {
        // IT pattern notes are 1-based: 1=C-0, 61=C-5 (middle), 120=B-9.
        // Byte 0 = empty cell; 253..255 = note-fade/cut/off sentinels (passed through).
        // Subtract 1 to convert to 0-based (0=C-0, 60=C-5) before passing to
        // NoteToFrequency or using as a noteMap index.  Both the raw note byte and any
        // value returned by the instrument noteMap need this conversion.
        if (note == 0 || note >= IT_NOTE_FADE) {
            return note;
        }

        return static_cast<uint8_t>(std::min<int>(static_cast<int>(note) - 1, 119));
    }

    // Internal OpenMPT / IT effect byte values. These are enum indices, NOT ASCII display chars.
    enum ITInternalCmd : uint8_t {
        CMD_MIDI          = 0x1A,   // Zxx: dual-purpose MIDI macro / native resonant filter
        CMD_SMOOTHMIDI    = 0x1B,   // \xx: smooth MIDI macro slide (VST automation only)
        CMD_DELAYCUT      = 0x1C,   // Xxx: note delay+cut (param < 0x80 native) / plugin extension (param >= 0x80)
        CMD_EXTMIDI       = 0x1D,   // #xx: plugin parameter extension byte (OpenMPT VST path only)
        CMD_FINETUNE      = 0x1E,   // qxx: fractional pitch finetune upward (native)
        CMD_FINETUNE_BKWD = 0x1F,   // txx: fractional pitch finetune downward (native)
    };

    bool IsUnsupportedITCommand(uint8_t effect, uint8_t param) {
        // Pure VST automation commands with no native audio equivalent in this engine.
        if (effect == CMD_SMOOTHMIDI || effect == CMD_EXTMIDI) {
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
    int32_t SignExtend(uint32_t value, uint8_t bits) {
        const uint32_t mask = 1U << (bits - 1);
        return static_cast<int32_t>((value ^ mask) - mask);
    }

    int32_t WrapSigned(int32_t value, bool is16Bit) {
        if (is16Bit) {
            return static_cast<int16_t>(value);
        }

        return static_cast<int8_t>(value);
    }

    class ITBitReader {
    public:
        ITBitReader(const std::vector<uint8_t>& source, size_t begin, size_t end)
            : data(source), byteOffset(begin), blockEnd(end) {
        }

        bool ReadBits(uint8_t bitCount, uint32_t& value) {
            value = 0;
            uint8_t written = 0;
            while (written < bitCount) {
                if (byteOffset >= blockEnd) {
                    return false;
                }

                const uint8_t available = static_cast<uint8_t>(8 - bitOffset);
                const uint8_t wanted = static_cast<uint8_t>(bitCount - written);
                const uint8_t take = std::min<uint8_t>(available, wanted);
                const uint8_t mask = static_cast<uint8_t>((1U << take) - 1U);
                value |= static_cast<uint32_t>((data[byteOffset] >> bitOffset) & mask) << written;

                bitOffset = static_cast<uint8_t>(bitOffset + take);
                written = static_cast<uint8_t>(written + take);
                if (bitOffset == 8) {
                    bitOffset = 0;
                    ++byteOffset;
                }
            }

            return true;
        }

    private:
        const std::vector<uint8_t>& data;
        size_t byteOffset = 0;
        size_t blockEnd = 0;
        uint8_t bitOffset = 0;
    };

    bool DecodeITCompressedChannel(const std::vector<uint8_t>& raw, size_t& offset, bool is16Bit,
        bool it215Compression, uint32_t sampleCount, std::vector<int32_t>& decoded) {
        constexpr uint32_t Block8BitSamples = 0x8000;
        constexpr uint32_t Block16BitSamples = 0x4000;

        decoded.assign(sampleCount, 0);
        const uint8_t maxWidth = is16Bit ? 17 : 9;
        const uint8_t lowWidthBits = is16Bit ? 4 : 3;
        const uint32_t blockSamples = is16Bit ? Block16BitSamples : Block8BitSamples;
        const uint32_t largeWidthBorderBase = is16Bit ? 0xFFFFU : 0xFFU;
        const uint32_t largeWidthRange = is16Bit ? 16U : 8U;
        int32_t d1 = 0;
        int32_t d2 = 0;
        uint32_t written = 0;

        while (written < sampleCount) {
            if (offset + sizeof(uint16_t) > raw.size()) {
                return false;
            }

            const uint16_t compressedBlockBytes = ReadLE16(&raw[offset]);
            offset += sizeof(uint16_t);
            if (compressedBlockBytes == 0 || offset + compressedBlockBytes > raw.size()) {
                return false;
            }

            const size_t blockEnd = offset + compressedBlockBytes;
            ITBitReader reader(raw, offset, blockEnd);
            uint8_t width = maxWidth;
            const uint32_t thisBlockSamples = std::min<uint32_t>(blockSamples, sampleCount - written);

            for (uint32_t blockSample = 0; blockSample < thisBlockSamples; ) {
                uint32_t value = 0;
                if (!reader.ReadBits(width, value)) {
                    return false;
                }

                if (width < 7) {
                    const uint32_t marker = 1U << (width - 1);
                    if (value == marker) {
                        uint32_t newWidth = 0;
                        if (!reader.ReadBits(lowWidthBits, newWidth)) {
                            return false;
                        }

                        // Method-1 width changes encode every legal width except the current
                        // one. Values at or above the current width are shifted up by one; using
                        // newWidth + 1 directly can leave the width unchanged and desync the rest
                        // of the compressed block.
                        ++newWidth;
                        width = static_cast<uint8_t>(std::clamp<uint32_t>(newWidth < width ? newWidth : newWidth + 1U, 1U, maxWidth));
                        continue;
                    }
                }
                else if (width < maxWidth) {
                    const uint32_t border = (largeWidthBorderBase >> (maxWidth - width)) - (largeWidthRange / 2U);
                    if (value > border && value <= border + largeWidthRange) {
                        value -= border;
                        width = static_cast<uint8_t>(std::clamp<uint32_t>(value < width ? value : value + 1U, 1U, maxWidth));
                        continue;
                    }
                }
                else if ((value & (1U << (maxWidth - 1))) != 0) {
                    width = static_cast<uint8_t>(std::clamp<uint32_t>((value + 1U) & 0xFFU, 1U, maxWidth));
                    continue;
                }

                const int32_t delta = width == maxWidth
                    ? (is16Bit ? static_cast<int32_t>(static_cast<int16_t>(value)) : static_cast<int32_t>(static_cast<int8_t>(value)))
                    : SignExtend(value, width);
                d1 = WrapSigned(d1 + delta, is16Bit);
                d2 = WrapSigned(d2 + d1, is16Bit);
                decoded[written++] = it215Compression ? d2 : d1;
                ++blockSample;
            }

            // Each compressed block is self-contained; skip trailing padding bits and resume at
            // the next byte block header to avoid accidental reads across block boundaries.
            offset = blockEnd;
        }

        return true;
    }

    bool DecodeITCompressedSample(const std::vector<uint8_t>& raw, bool is16Bit, uint32_t sourceChannels,
        bool it215Compression, uint32_t sampleCount, std::vector<int16_t>& pcm) {
        pcm.assign(sampleCount, 0);
        if (sampleCount == 0 || sourceChannels == 0) {
            return true;
        }

        size_t offset = 0;
        std::vector<int32_t> channelSamples;
        std::vector<int32_t> mixed(sampleCount, 0);
        for (uint32_t sourceChannel = 0; sourceChannel < sourceChannels; ++sourceChannel) {
            if (!DecodeITCompressedChannel(raw, offset, is16Bit, it215Compression, sampleCount, channelSamples)) {
                return false;
            }

            for (uint32_t sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex) {
                mixed[sampleIndex] += is16Bit ? channelSamples[sampleIndex] : (channelSamples[sampleIndex] << 8);
            }
        }

        for (uint32_t sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex) {
            pcm[sampleIndex] = static_cast<int16_t>(std::clamp<int32_t>(
                mixed[sampleIndex] / static_cast<int32_t>(sourceChannels),
                -32768,
                32767));
        }

        return true;
    }
}

bool ITPlayer::Initialize(const std::wstring& filename) {
#if defined(_DEBUG_ITPlayer_)
    debug.DebugLog("ITPlayer initialization started...\n");
#endif
    if (!bIsInitialized) {
        if (!CreateAudioDevice()) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"ITPlayer: Failed to initialize audio device.");
            return false;
        }

        bIsInitialized = true;
    }

    if (!LoadITFile(filename)) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"ITPlayer: LoadITFile failed.");
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

    speed = header.initialSpeed > 0 ? header.initialSpeed : 6;
    tempo = header.initialTempo >= 32 ? header.initialTempo : 125;

    hostChannels.clear();
    hostChannels.resize(IT_MAX_CHANNELS);
    voices.clear();
    voices.resize(IT_MAX_VIRTUAL_VOICES);
    for (size_t channel = 0; channel < hostChannels.size(); ++channel) {
        hostChannels[channel].panning = DefaultPanningForChannel(channel);
        hostChannels[channel].channelVolume = std::min<uint8_t>(header.channelVolume[channel], 64);
        hostChannels[channel].hostChannel = static_cast<uint8_t>(channel);
    }

    moduleGlobalVolume = static_cast<uint8_t>(std::min<uint16_t>(header.globalVolume, 128));
    currentVolume = config.myConfig.musicVolume;
    targetVolume = config.myConfig.musicVolume;
    fadeInActive = false;
    fadeOutActive = false;
    isMuted = false;

    #if defined(_DEBUG_ITPlayer_)
        debug.DebugLog("ITPlayer initialization successful.\n");
    #endif
    return true;
}

bool ITPlayer::LoadITFile(const std::wstring& filename) {
    debug.logLevelMessage(LogLevel::LOG_INFO, L"Loading IT file: " + filename);

    modulePath = filename;

    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"ITPlayer: Failed to open IT file.");
        return false;
    }

    file.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (file.gcount() != sizeof(header)) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"ITPlayer: Failed to read IT header.");
        return false;
    }

    if (std::strncmp(header.magic, "IMPM", 4) != 0) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"ITPlayer: Invalid signature. IT files must start with IMPM.");
        return false;
    }

    if (header.orderCount == 0 || header.orderCount > 256 ||
        header.instrumentCount > 4096 || header.sampleCount > 4096 ||
        header.patternCount > 4096) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"ITPlayer: Header counts are out of supported range.");
        return false;
    }

    orders.resize(header.orderCount);
    file.read(reinterpret_cast<char*>(orders.data()), orders.size());
    if (file.gcount() != static_cast<std::streamsize>(orders.size())) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"ITPlayer: Failed to read order table.");
        return false;
    }

    std::vector<uint32_t> instrumentPointers(header.instrumentCount);
    std::vector<uint32_t> samplePointers(header.sampleCount);
    std::vector<uint32_t> patternPointers(header.patternCount);

    file.read(reinterpret_cast<char*>(instrumentPointers.data()), instrumentPointers.size() * sizeof(uint32_t));
    if (file.gcount() != static_cast<std::streamsize>(instrumentPointers.size() * sizeof(uint32_t))) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"ITPlayer: Failed to read instrument pointer table.");
        return false;
    }

    file.read(reinterpret_cast<char*>(samplePointers.data()), samplePointers.size() * sizeof(uint32_t));
    if (file.gcount() != static_cast<std::streamsize>(samplePointers.size() * sizeof(uint32_t))) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"ITPlayer: Failed to read sample pointer table.");
        return false;
    }

    file.read(reinterpret_cast<char*>(patternPointers.data()), patternPointers.size() * sizeof(uint32_t));
    if (file.gcount() != static_cast<std::streamsize>(patternPointers.size() * sizeof(uint32_t))) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"ITPlayer: Failed to read pattern pointer table.");
        return false;
    }

    defaultPanning.resize(IT_MAX_CHANNELS);
    for (size_t channel = 0; channel < IT_MAX_CHANNELS; ++channel) {
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

    debug.logLevelMessage(LogLevel::LOG_INFO, L"IT file loaded successfully.");
    return true;
}

bool ITPlayer::LoadInstruments(std::ifstream& file, const std::vector<uint32_t>& instrumentPointers) {
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
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"ITPlayer: Failed to read instrument " + std::to_wstring(i));
            return false;
        }

        if (std::memcmp(data.data(), "IMPI", 4) != 0) {
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"ITPlayer: Ignoring non-IT instrument header " + std::to_wstring(i));
            continue;
        }

        ITInstrument& instrument = instruments[i];
        std::memcpy(instrument.name, &data[32], sizeof(instrument.name));
        instrument.nna = data[16] & 0x03;
        instrument.duplicateCheckType = data[17] & 0x03;
        instrument.duplicateCheckAction = data[18] & 0x03;
        instrument.fadeOut = ReadLE16(&data[20]);
        instrument.globalVolume = std::min<uint8_t>(data[24], 64);
        instrument.defaultPanning = (data[25] & 0x80) != 0
            ? static_cast<uint8_t>((static_cast<uint16_t>(data[25] & 0x7F) * 255U) / 64U)
            : 128;
        instrument.volumeEnvelopeFlags = data[304];
        instrument.volumeEnvelopePointCount = std::min<uint8_t>(data[305], 25);
        instrument.volumeEnvelopeLoopStart = data[306];
        instrument.volumeEnvelopeLoopEnd = data[307];
        instrument.volumeEnvelopeSustainStart = data[308];
        instrument.volumeEnvelopeSustainEnd = data[309];
        for (size_t point = 0; point < instrument.volumeEnvelopePointCount; ++point) {
            const size_t pointOffset = 310 + point * 3;
            instrument.volumeEnvelopeValues[point] = std::min<uint8_t>(data[pointOffset], 64);
            instrument.volumeEnvelopeTicks[point] = ReadLE16(&data[pointOffset + 1]);
        }

        instrument.panningEnvelopeFlags = data[381];
        instrument.panningEnvelopePointCount = std::min<uint8_t>(data[382], 25);
        instrument.panningEnvelopeLoopStart = data[383];
        instrument.panningEnvelopeLoopEnd = data[384];
        instrument.panningEnvelopeSustainStart = data[385];
        instrument.panningEnvelopeSustainEnd = data[386];
        for (size_t point = 0; point < instrument.panningEnvelopePointCount; ++point) {
            const size_t pointOffset = 387 + point * 3;
            instrument.panningEnvelopeValues[point] = static_cast<int8_t>(std::clamp<int>(static_cast<int>(data[pointOffset]) - 32, -32, 32));
            instrument.panningEnvelopeTicks[point] = ReadLE16(&data[pointOffset + 1]);
        }

        // IT 2.x / IT instruments store 120 note/sample pairs in the keyboard
        // table at byte 64. Reading from the envelope area maps notes to garbage
        // sample ids and makes playback collapse onto the wrong sample.
        const size_t tableOffset = data.size() >= 304 ? 64 : 0;
        for (size_t note = 0; note < instrument.noteMap.size(); ++note) {
            instrument.noteMap[note] = static_cast<uint8_t>(note + 1);
            instrument.sampleMap[note] = static_cast<uint8_t>(std::min<size_t>(i + 1, 255));
            if (tableOffset > 0) {
                instrument.noteMap[note] = data[tableOffset + note * 2];
                instrument.sampleMap[note] = data[tableOffset + note * 2 + 1];
            }
        }
    }

    return true;
}


bool ITPlayer::LoadSamples(std::ifstream& file, const std::vector<uint32_t>& samplePointers) {
    samples.clear();
    samples.resize(samplePointers.size());

    const std::streampos restorePosition = file.tellg();
    file.seekg(0, std::ios::end);
    const std::streamoff fileSize = file.tellg();
    file.seekg(restorePosition, std::ios::beg);
    if (fileSize <= 0) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"ITPlayer: Failed to determine IT file size.");
        return false;
    }

    for (size_t i = 0; i < samplePointers.size(); ++i) {
        if (samplePointers[i] == 0) {
            continue;
        }

        file.seekg(static_cast<std::streamoff>(samplePointers[i]), std::ios::beg);
        ITSampleHeader sampleHeader{};
        file.read(reinterpret_cast<char*>(&sampleHeader), sizeof(sampleHeader));
        if (file.gcount() != sizeof(sampleHeader)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"ITPlayer: Failed to read sample header " + std::to_wstring(i));
            return false;
        }

        if (std::strncmp(sampleHeader.magic, "IMPS", 4) != 0) {
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"ITPlayer: Ignoring non-IT sample header " + std::to_wstring(i));
            continue;
        }

        ITSample& sample = samples[i];
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
        sample.defaultPanning = (sampleHeader.defaultPanning & 0x80) != 0
            ? static_cast<uint8_t>((static_cast<uint16_t>(sampleHeader.defaultPanning & 0x7F) * 255U) / 64U)
            : 128;
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
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"ITPlayer: Sample data is too large to allocate " + std::to_wstring(i));
            return false;
        }

        if ((sample.flags & 0x08) != 0) {
            const uint64_t sampleOffset = sampleHeader.samplePointer;
            const uint64_t fileSize64 = static_cast<uint64_t>(fileSize);
            if (sampleOffset >= fileSize64) {
                debug.logLevelMessage(LogLevel::LOG_ERROR, L"ITPlayer: Compressed sample offset is outside the file " + std::to_wstring(i));
                return false;
            }

            // IT compressed samples are stored as a sequence of independently sized blocks.
            // Their packed size is not in the sample header, so read the remaining file tail and
            // let the block decoder stop after the expected sample frames have been produced.
            const size_t availableBytes = static_cast<size_t>(fileSize64 - sampleOffset);
            std::vector<uint8_t> raw(availableBytes);
            file.seekg(static_cast<std::streamoff>(sampleHeader.samplePointer), std::ios::beg);
            file.read(reinterpret_cast<char*>(raw.data()), raw.size());
            if (file.gcount() != static_cast<std::streamsize>(raw.size())) {
                debug.logLevelMessage(LogLevel::LOG_ERROR, L"ITPlayer: Failed to read compressed sample data " + std::to_wstring(i));
                return false;
            }

            const bool it215Compression = (sample.convert & 0x04) != 0;
            if (!DecodeITCompressedSample(raw, is16Bit, static_cast<uint32_t>(sourceChannels), it215Compression, sample.length, sample.pcm)) {
                sample.pcm.clear();
                debug.logLevelMessage(LogLevel::LOG_ERROR, L"ITPlayer: Failed to decode compressed IT sample " + std::to_wstring(i));
                return false;
            }

            continue;
        }

        const uint64_t sampleOffset = sampleHeader.samplePointer;
        const uint64_t fileSize64 = static_cast<uint64_t>(fileSize);
        const bool sampleDataFits = sampleOffset <= fileSize64 && byteCount64 <= fileSize64 - sampleOffset;
        if (!sampleDataFits) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"ITPlayer: Sample data extends past end of file " + std::to_wstring(i));
            return false;
        }

        const size_t byteCount = static_cast<size_t>(byteCount64);
        std::vector<uint8_t> raw(byteCount);
        file.seekg(static_cast<std::streamoff>(sampleHeader.samplePointer), std::ios::beg);
        file.read(reinterpret_cast<char*>(raw.data()), raw.size());
        if (file.gcount() != static_cast<std::streamsize>(raw.size())) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"ITPlayer: Failed to read sample data " + std::to_wstring(i));
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
                        last[sourceChannel] += value;
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
                        last[sourceChannel] += value;
                        value = last[sourceChannel];
                    }
                    mixed += value;
                }
                mixed /= static_cast<int32_t>(sourceChannels);
                sample.pcm[s] = static_cast<int16_t>(std::clamp(mixed << 8, -32768, 32767));
            }
        }
    }

    return true;
}

bool ITPlayer::LoadPatterns(std::ifstream& file, const std::vector<uint32_t>& patternPointers) {
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
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"ITPlayer: Failed to read pattern header " + std::to_wstring(i));
            return false;
        }

        patterns[i].packedLength = ReadLE16(headerBytes);
        patterns[i].rows = std::max<uint16_t>(ReadLE16(headerBytes + 2), 1);
        patterns[i].data.resize(patterns[i].packedLength);
        if (patterns[i].packedLength > 0) {
            file.read(reinterpret_cast<char*>(patterns[i].data.data()), patterns[i].data.size());
            if (file.gcount() != static_cast<std::streamsize>(patterns[i].data.size())) {
                debug.logLevelMessage(LogLevel::LOG_ERROR, L"ITPlayer: Failed to read packed pattern " + std::to_wstring(i));
                return false;
            }
        }
    }

    return true;
}

void ITPlayer::UnpackPatterns() {
    unpackedPatterns.clear();
    unpackedPatterns.resize(patterns.size());

    for (size_t patternIndex = 0; patternIndex < patterns.size(); ++patternIndex) {
        const uint16_t rows = std::max<uint16_t>(patterns[patternIndex].rows, 1);
        unpackedPatterns[patternIndex].assign(rows, std::vector<ITEvent>(IT_MAX_CHANNELS));

        std::array<uint8_t, IT_MAX_CHANNELS> lastMask{};
        std::array<ITEvent, IT_MAX_CHANNELS> lastEvent{};
        const std::vector<uint8_t>& data = patterns[patternIndex].data;
        size_t offset = 0;
        uint16_t row = 0;

        while (row < rows && offset < data.size()) {
            const uint8_t channelByte = data[offset++];
            if (channelByte == 0) {
                ++row;
                continue;
            }

            const size_t channel = (channelByte - 1U) & 0x3F;
            uint8_t mask = lastMask[channel];
            if ((channelByte & 0x80) != 0) {
                if (offset >= data.size()) {
                    break;
                }
                mask = data[offset++];
                lastMask[channel] = mask;
            }

            ITEvent event = unpackedPatterns[patternIndex][row][channel];
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
}

bool ITPlayer::Play(const std::wstring& filename) {
    if (isPlaying) {
        return false;
    }

    if (!Initialize(filename)) {
        return false;
    }

    #if defined(PLATFORM_WINDOWS)
        HRESULT hr = secondaryBuffer->Play(0, 0, DSBPLAY_LOOPING);
        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"ITPlayer: Failed to start DirectSound buffer playback.");
            return false;
        }
    #endif

    isPlaying = true;
    isPaused = false;
    isTerminating = false;
    playbackThread = std::thread(&ITPlayer::PlaybackLoop, this);
    return true;
}

bool ITPlayer::Play(const std::string& path) {
    return Play(std::wstring(path.begin(), path.end()));
}

void ITPlayer::Shutdown() {
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
    hostChannels.clear();
    voices.clear();
    defaultPanning.clear();
    mixScratch.clear();
    mixAccumulator.clear();
    bIsInitialized = false;
    isPaused = false;
    isMuted = false;
    isTerminating = false;
}

void ITPlayer::Terminate() {
    isTerminating = true;
    Stop();
}

void ITPlayer::Stop() {
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

void ITPlayer::Pause() {
    Mute();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    isPaused = true;
}

void ITPlayer::Resume() {
    isPaused = false;
    isMuted = false;
    SetVolume(targetVolume.load());
}

void ITPlayer::HardPause() {
    std::lock_guard<std::mutex> lock(playbackMutex);
    isPaused = true;
    isMuted = true;

    for (ITChannelVoice& host : hostChannels) {
        host.activeVoiceIndex = -1;
    }

    for (ITChannelVoice& voice : voices) {
        voice.active = false;
        voice.volume = 0;
        voice.position = 0.0;
    }

    SilenceBuffer();
    SetVolume(0);
}

void ITPlayer::HardResume() {
    std::lock_guard<std::mutex> lock(playbackMutex);

    tick = 0;
    currentRow = 0;
    rowBreakPending = false;
    orderJumpPending = false;
    patternDelayTicks = 0;
    fineDelayTicks = 0;
    inPatternDelay = false;
    patternLoopPending = false;

    for (size_t channel = 0; channel < hostChannels.size(); ++channel) {
        ITChannelVoice& host = hostChannels[channel];
        host = ITChannelVoice{};
        host.hostChannel = static_cast<uint8_t>(channel);
        host.channelVolume = std::min<uint8_t>(header.channelVolume[channel], 64);
        host.panning = DefaultPanningForChannel(channel);
    }
    for (ITChannelVoice& voice : voices) {
        voice = ITChannelVoice{};
    }

    SilenceBuffer();
    isPaused = false;
    isMuted = false;
    currentVolume = targetVolume.load();
    moduleGlobalVolume = static_cast<uint8_t>(std::min<uint16_t>(header.globalVolume, 128));
}

void ITPlayer::Mute() {
    // MixAudio already emits silence while isMuted is set, so there is no need to destroy the
    // per-voice volumes here. Zeroing them left sustained notes silent after Resume() because
    // nothing restored them; relying on the flag keeps a soft mute fully reversible.
    isMuted = true;
    SilenceBuffer();
}

bool ITPlayer::IsPaused() const {
    return isPaused;
}

bool ITPlayer::IsPlaying() const {
    return isPlaying && !isPaused && !isTerminating;
}

void ITPlayer::SetVolume(uint8_t volume) {
    const uint8_t clamped = std::min<uint8_t>(volume, 64);
    currentVolume = clamped;
    targetVolume = clamped;
}

void ITPlayer::SetGlobalVolume(uint8_t volume) {
    globalVolume = std::min<uint8_t>(volume, 64);
}

void ITPlayer::SetFadeIn(uint32_t durationMs) {
    fadeDurationMs = durationMs;
    fadeElapsedMs = 0;
    fadeInActive = true;
    fadeOutActive = false;
    currentVolume = 0;
    targetVolume = 64;
}

void ITPlayer::SetFadeOut(uint32_t durationMs) {
    fadeDurationMs = durationMs;
    fadeElapsedMs = 0;
    fadeInActive = false;
    fadeOutActive = true;
    targetVolume = 0;
}

bool ITPlayer::CreateAudioDevice() {
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
    waveFormat.nSamplesPerSec = IT_SAMPLE_RATE;
    waveFormat.wBitsPerSample = 16;
    waveFormat.nBlockAlign = waveFormat.nChannels * waveFormat.wBitsPerSample / 8;
    waveFormat.nAvgBytesPerSec = waveFormat.nSamplesPerSec * waveFormat.nBlockAlign;

    primaryBuffer->SetFormat(&waveFormat);

    DSBUFFERDESC secondaryDesc = {};
    secondaryDesc.dwSize = sizeof(secondaryDesc);
    secondaryDesc.dwBufferBytes = IT_BUFFER_SIZE;
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

void ITPlayer::FillAudioBuffer() {
#if defined(PLATFORM_WINDOWS)
    if (!secondaryBuffer) {
        return;
    }

    DWORD playCursor = 0;
    DWORD writeCursorDS = 0;
    HRESULT hr = secondaryBuffer->GetCurrentPosition(&playCursor, &writeCursorDS);
    if (FAILED(hr)) {
        return;
    }

    constexpr DWORD frameBytes = sizeof(int16_t) * 2;
    // Keep roughly a quarter second of audio queued ahead of the play cursor. The old code held
    // only a ~2 KB margin (~11 ms), so any scheduling hiccup produced underruns that sounded
    // like stuttering/stuck playback. A frame-aligned lead removes that while staying short
    // enough that pause/fade respond quickly and startup latency stays low.
    const DWORD targetLead = (bufferSize / 4) & ~(frameBytes - 1);
    const DWORD lead = (writeCursor - playCursor + bufferSize) % bufferSize;
    if (lead >= targetLead) {
        return;
    }

    DWORD bytesToWrite = targetLead - lead;
    bytesToWrite -= bytesToWrite % frameBytes;
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

void ITPlayer::MixAudio(int16_t* buffer, size_t samplesToMix) {
    const size_t stereoSampleCount = samplesToMix * 2;
    std::fill(buffer, buffer + stereoSampleCount, 0);

    if (isMuted || samplesToMix == 0) {
        return;
    }

    if (mixAccumulator.size() < stereoSampleCount) {
        mixAccumulator.resize(stereoSampleCount);
    }
    std::fill(mixAccumulator.begin(), mixAccumulator.begin() + stereoSampleCount, 0);

    const float globalVolFactor = static_cast<float>(globalVolume.load()) / 64.0f;
    const float moduleGlobalVolFactor = static_cast<float>(moduleGlobalVolume.load()) / 128.0f;
    // IT header.mixVolume (0-128, default 48) prescales the mix output so simultaneous voices
    // don't clip. It is independent of globalVolume. Guard against zero (means "use default 48").
    const uint8_t rawMixVol = header.mixVolume > 0 ? header.mixVolume : 48;
    const float mixVolFactor = static_cast<float>(rawMixVol) / 128.0f;
    const float fadeVolFactor = static_cast<float>(currentVolume.load()) / 64.0f;

    for (ITChannelVoice& voice : voices) {
        if (!voice.active || !voice.sample || voice.sample->pcm.empty()) {
            continue;
        }

        const ITSample* sample = voice.sample;
        const size_t dataSize = sample->pcm.size();
        double samplePos = voice.position;
        const double stepValue = std::max(voice.step, 0.0);
        const double stepDirection = voice.reversePlayback ? -stepValue : stepValue;
        const float pan = static_cast<float>(std::clamp<int>(static_cast<int>(voice.panning) + static_cast<int>(voice.envelopePanning * 128.0f), 0, 255)) / 255.0f;
        const float instrumentVol = voice.instrumentDef ? static_cast<float>(voice.instrumentDef->globalVolume) / 64.0f : 1.0f;
        const float volume = (static_cast<float>(voice.volume) / 64.0f) *
            (static_cast<float>(voice.channelVolume) / 64.0f) *
            (static_cast<float>(sample->globalVolume) / 64.0f) * instrumentVol *
            voice.envelopeVolume * (static_cast<float>(voice.fadeOutVolume) / 65536.0f);
        // Collapse pan, master/fade/mix volume and per-voice volume into one scalar per side so the
        // inner loop is a single multiply + accumulate per channel instead of four multiplies.
        const float finalLeft  = (1.0f - pan) * globalVolFactor * moduleGlobalVolFactor * mixVolFactor * fadeVolFactor * volume;
        const float finalRight = pan            * globalVolFactor * moduleGlobalVolFactor * mixVolFactor * fadeVolFactor * volume;

        const bool sustainLooped = sample->IsSustainLooped() && !voice.noteReleased;
        const bool looped = sustainLooped || sample->IsLooped();
        const uint32_t loopStartSample = sustainLooped ? sample->sustainLoopStart : sample->loopStart;
        const uint32_t loopEndSample = sustainLooped ? sample->sustainLoopEnd : sample->loopEnd;
        const double loopStart = static_cast<double>(std::min<uint32_t>(loopStartSample, static_cast<uint32_t>(dataSize)));
        const double loopEnd = static_cast<double>(std::min<uint32_t>(loopEndSample, static_cast<uint32_t>(dataSize)));
        const double loopLength = loopEnd - loopStart;

        for (size_t frame = 0; frame < samplesToMix; ++frame) {
            const size_t index = static_cast<size_t>(samplePos);
            if (index >= dataSize) {
                voice.active = false;
                break;
            }

            size_t nextIndex = index + 1;
            // When approaching the loop end, wrap the interpolation look-ahead to loopStart so
            // the interpolated output is continuous at the loop boundary. Without this, nextIndex
            // reads the sample PAST loopEnd which will never be played, producing a click on every
            // loop cycle.
            if (looped && loopLength > 1.0 && static_cast<double>(nextIndex) >= loopEnd) {
                nextIndex = static_cast<size_t>(loopStart);
            }
            else if (nextIndex >= dataSize) {
                nextIndex = (looped && loopLength > 1.0) ? static_cast<size_t>(loopStart) : index;
            }

            const float fraction = static_cast<float>(samplePos - std::floor(samplePos));
            const float currentSample = static_cast<float>(sample->pcm[index]);
            const float nextSample = static_cast<float>(sample->pcm[nextIndex]);
            float value = currentSample + ((nextSample - currentSample) * fraction);
            if (voice.filterEnabled) {
                const float cutoff = std::clamp(voice.filterCutoff * (1.0f - (voice.filterResonance * 0.35f)), 0.01f, 1.0f);
                voice.filterState += (value - voice.filterState) * cutoff;
                value = voice.filterState;
            }

            int32_t* out = &mixAccumulator[frame * 2];
            out[0] += static_cast<int32_t>(value * finalLeft);
            out[1] += static_cast<int32_t>(value * (voice.surround ? -finalRight : finalRight));

            samplePos += stepDirection;
            if (looped && loopLength > 1.0) {
                if (voice.reversePlayback) {
                    while (samplePos < loopStart) {
                        samplePos += loopLength;
                    }
                }
                else {
                    while (samplePos >= loopEnd) {
                        samplePos -= loopLength;
                        if (samplePos < loopStart) {
                            samplePos = loopStart;
                        }
                    }
                }
            }
            else if ((!voice.reversePlayback && samplePos >= static_cast<double>(dataSize)) ||
                (voice.reversePlayback && samplePos < 0.0)) {
                voice.active = false;
                break;
            }
        }

        voice.position = samplePos;
    }

    for (size_t i = 0; i < stereoSampleCount; ++i) {
        buffer[i] = FinalizeMixSample(mixAccumulator[i]);
    }
}
void ITPlayer::SilenceBuffer() {
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

void ITPlayer::TickRow() {
    std::lock_guard<std::mutex> lock(playbackMutex);

    while (sequencePosition < orders.size() && orders[sequencePosition] == 254) {
        ++sequencePosition;
    }

    if (sequencePosition >= orders.size() || orders[sequencePosition] == 255) {
        sequencePosition = restartSequencePosition.load();
    }

    if (sequencePosition >= orders.size()) {
        sequencePosition = 0;
    }

    const uint8_t patternIndex = orders[sequencePosition];
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

    const std::vector<ITEvent>& row = unpackedPatterns[patternIndex][currentRow];
    rowBreakPending = false;
    orderJumpPending = false;
    fineDelayTicks = 0;   // S6x is per-row; clear it before this row's effects can set it.
    nextRowOverride = currentRow + 1;

    // First (and only) read of this row: trigger its notes and effects exactly once.
    for (size_t channel = 0; channel < row.size(); ++channel) {
        const ITEvent& event = row[channel];
        TriggerEvent(channel, event, false);
    }

    // If SEx armed a pattern delay on this row, hold here without advancing or decrementing
    // (this first play already counts); subsequent hold ticks above do the countdown.
    if (!inPatternDelay) {
        AdvanceRow();
    }
    tick = 0;
}

ITChannelVoice* ITPlayer::ActiveVoiceForHost(size_t channel) {
    if (channel >= hostChannels.size()) {
        return nullptr;
    }

    const int16_t voiceIndex = hostChannels[channel].activeVoiceIndex;
    if (voiceIndex < 0 || static_cast<size_t>(voiceIndex) >= voices.size()) {
        return nullptr;
    }

    ITChannelVoice& voice = voices[static_cast<size_t>(voiceIndex)];
    return voice.active ? &voice : nullptr;
}

void ITPlayer::ApplyNewNoteAction(ITChannelVoice& voice, uint8_t nna) {
    switch (nna & 0x03) {
    case 0: // Cut
        voice.active = false;
        break;
    case 1: // Continue
        voice.background = true;
        break;
    case 2: // Note off
        voice.noteReleased = true;
        voice.background = true;
        if (!voice.instrumentDef || (!voice.instrumentDef->HasVolumeEnvelope() && voice.instrumentDef->fadeOut == 0)) {
            voice.active = false;
        }
        break;
    case 3: // Fade
        voice.noteReleased = true;
        voice.background = true;
        if (!voice.instrumentDef || voice.instrumentDef->fadeOut == 0) {
            voice.active = false;
        }
        break;
    default:
        break;
    }
}

void ITPlayer::ApplyDuplicateCheck(const ITInstrument& instrument, uint8_t note, size_t sampleIndex) {
    if (instrument.duplicateCheckType == 0 || instrument.duplicateCheckAction == 0) {
        return;
    }

    for (ITChannelVoice& voice : voices) {
        if (!voice.active || voice.instrumentDef != &instrument) {
            continue;
        }

        bool duplicate = false;
        switch (instrument.duplicateCheckType) {
        case 1: // Note
            duplicate = voice.note == note;
            break;
        case 2: // Sample
            duplicate = voice.sample && sampleIndex > 0 && sampleIndex <= samples.size() && voice.sample == &samples[sampleIndex - 1];
            break;
        case 3: // Instrument
            duplicate = true;
            break;
        default:
            break;
        }

        if (!duplicate) {
            continue;
        }

        switch (instrument.duplicateCheckAction) {
        case 1: // Cut
            voice.active = false;
            break;
        case 2: // Note off
            voice.noteReleased = true;
            voice.background = true;
            break;
        case 3: // Fade
            voice.noteReleased = true;
            voice.background = true;
            voice.fadeOutVolume = std::min<uint32_t>(voice.fadeOutVolume, 32768);
            break;
        default:
            break;
        }
    }
}

ITChannelVoice& ITPlayer::AllocateVoice(size_t channel) {
    ITChannelVoice* reusable = nullptr;
    for (ITChannelVoice& voice : voices) {
        if (!voice.active) {
            reusable = &voice;
            break;
        }
    }

    if (reusable == nullptr) {
        reusable = &voices[0];
        for (ITChannelVoice& voice : voices) {
            if (voice.background && voice.fadeOutVolume < reusable->fadeOutVolume) {
                reusable = &voice;
            }
        }
    }

    const size_t voiceIndex = static_cast<size_t>(reusable - voices.data());
    *reusable = ITChannelVoice{};
    reusable->hostChannel = static_cast<uint8_t>(std::min<size_t>(channel, IT_MAX_CHANNELS - 1));
    reusable->panning = DefaultPanningForChannel(channel);
    if (channel < hostChannels.size()) {
        hostChannels[channel].activeVoiceIndex = static_cast<int16_t>(voiceIndex);
    }

    return *reusable;
}
void ITPlayer::TriggerEvent(size_t channel, const ITEvent& event, bool fromDelay) {
    if (channel >= hostChannels.size()) {
        return;
    }

    ITChannelVoice& host = hostChannels[channel];
    ITChannelVoice* activeVoice = ActiveVoiceForHost(channel);
    ITChannelVoice* effectVoice = activeVoice != nullptr ? activeVoice : &host;
    const uint8_t effect = event.effect;
    uint8_t data = event.effectData;

    if (effect == 0x13 && (data >> 4) == 0x0D && (data & 0x0F) > 0 && !fromDelay) {
        host.delayedNotePending = true;
        host.delayTicks = data & 0x0F;
        host.delayedEvent = event;
        host.delayedEvent.effect = 0;
        host.delayedEvent.effectData = 0;
        return;
    }

    if (effect == CMD_DELAYCUT && (data >> 4) > 0 && !fromDelay) {
        host.delayedNotePending      = true;
        host.delayTicks              = data >> 4;
        host.delayedEvent            = event;
        host.delayedEvent.effect     = 0;
        host.delayedEvent.effectData = 0;
        if ((data & 0x0F) > 0 && activeVoice) {
            activeVoice->noteCutTick = data & 0x0F;
        }
        return;
    }

    if (event.instrument > 0) {
        host.instrument = event.instrument;
    }

    uint8_t volCommand = VOLCMD_NONE;
    uint8_t volParam = 0;
    if (event.volume <= 212) {
        DecodeVolumeColumn(event.volume, volCommand, volParam);
    }
    const bool volTonePorta = (volCommand == VOLCMD_TONEPORTA);

    uint8_t mappedNote = event.note;
    const ITSample* mappedSample = ResolveSample(host.instrument, event.note, mappedNote);
    const bool hasPlayableNote = mappedNote > 0 && mappedNote < IT_NOTE_FADE;
    const bool tonePortamentoNote = (effect == 0x07 || effect == 0x0C || volTonePorta) && hasPlayableNote && activeVoice && activeVoice->sample;

    if (event.note == IT_NOTE_CUT) {
        if (activeVoice) {
            activeVoice->active = false;
        }
        host.activeVoiceIndex = -1;
    }
    else if (event.note == IT_NOTE_OFF || event.note == IT_NOTE_FADE) {
        if (activeVoice) {
            activeVoice->noteReleased = true;
            if (event.note == IT_NOTE_FADE) {
                activeVoice->fadeOutVolume = std::min<uint32_t>(activeVoice->fadeOutVolume, 32768);
            }
            if (!activeVoice->instrumentDef || (!activeVoice->instrumentDef->HasVolumeEnvelope() && activeVoice->instrumentDef->fadeOut == 0)) {
                activeVoice->active = false;
                host.activeVoiceIndex = -1;
            }
        }
    }
    else if (hasPlayableNote && mappedSample && !mappedSample->pcm.empty()) {
        const double noteStep = NoteToFrequency(ITNoteToLinear(mappedNote), mappedSample->c5Speed) /
            static_cast<double>(IT_SAMPLE_RATE);

        if (tonePortamentoNote) {
            activeVoice->targetStep = noteStep;
            activeVoice->note = mappedNote;
            effectVoice = activeVoice;
        }
        else {
            const ITInstrument* instrumentDef = host.instrument > 0 && host.instrument <= instruments.size()
                ? &instruments[host.instrument - 1]
                : nullptr;
            const size_t sampleIndex = mappedSample >= samples.data() && mappedSample < samples.data() + samples.size()
                ? static_cast<size_t>((mappedSample - samples.data()) + 1)
                : 0;

            if (instrumentDef) {
                ApplyDuplicateCheck(*instrumentDef, mappedNote, sampleIndex);
            }
            if (activeVoice) {
                ApplyNewNoteAction(*activeVoice, instrumentDef ? instrumentDef->nna : 0);
            }

            ITChannelVoice& newVoice = AllocateVoice(channel);
            newVoice.sample = mappedSample;
            newVoice.instrumentDef = instrumentDef;
            newVoice.position = 0.0;
            newVoice.envelopeTick = 0;
            newVoice.panEnvelopeTick = 0;
            newVoice.envelopeVolume = instrumentDef && instrumentDef->HasVolumeEnvelope()
                ? EvaluateVolumeEnvelope(*instrumentDef, 0)
                : 1.0f;
            newVoice.envelopePanning = instrumentDef && instrumentDef->HasPanningEnvelope()
                ? EvaluatePanningEnvelope(*instrumentDef, 0)
                : 0.0f;
            newVoice.fadeOutVolume = 65536;
            newVoice.noteReleased = false;
            newVoice.volume = mappedSample->defaultVolume;
            newVoice.baseVolume = mappedSample->defaultVolume;
            newVoice.channelVolume = host.channelVolume;
            newVoice.panning = mappedSample->defaultPanning != 128
                ? mappedSample->defaultPanning
                : (instrumentDef ? instrumentDef->defaultPanning : DefaultPanningForChannel(channel));
            newVoice.step = noteStep;
            newVoice.baseStep = newVoice.step;
            newVoice.targetStep = noteStep;
            newVoice.note = mappedNote;
            newVoice.instrument = host.instrument;
            newVoice.lastVolumeSlide = host.lastVolumeSlide;
            newVoice.lastPortamento = host.lastPortamento;
            newVoice.lastVibrato = host.lastVibrato;
            newVoice.lastTremolo = host.lastTremolo;
            newVoice.lastSampleOffsetHigh = host.lastSampleOffsetHigh;
            newVoice.lastSampleOffset = host.lastSampleOffset;
            newVoice.lastRetrig = host.lastRetrig;
            newVoice.lastPanningSlide = host.lastPanningSlide;
            newVoice.lastChannelVolumeSlide = host.lastChannelVolumeSlide;
            newVoice.lastGlobalVolumeSlide = host.lastGlobalVolumeSlide;
            newVoice.glissandoControl  = host.glissandoControl;
            newVoice.vibratoWaveform   = host.vibratoWaveform;
            newVoice.tremoloWaveform   = host.tremoloWaveform;
            newVoice.panbrelloWaveform = host.panbrelloWaveform;
            // Bit 2 of each waveform byte is the IT "no-retrig" flag: when set, the oscillator
            // phase must continue from where the previous note left off rather than resetting.
            newVoice.vibratoPos   = (host.vibratoWaveform   & 0x04) ? host.vibratoPos   : 0;
            newVoice.tremoloPos   = (host.tremoloWaveform   & 0x04) ? host.tremoloPos   : 0;
            newVoice.panbrelloPos = (host.panbrelloWaveform & 0x04) ? host.panbrelloPos : 0;
            newVoice.filterEnabled = host.filterEnabled;
            newVoice.filterCutoff = host.filterCutoff;
            newVoice.filterResonance = host.filterResonance;
            newVoice.filterState = 0.0f;
            newVoice.surround = host.surround;
            newVoice.reversePlayback = host.reversePlayback;
            newVoice.noteCutTick = 255;
            newVoice.active = true;
            effectVoice = &newVoice;
        }
    }

    host.volCommand = volCommand;
    host.volParam = volParam;
    if (effectVoice) {
        effectVoice->volCommand = volCommand;
        effectVoice->volParam = volParam;
    }

    auto applyVolumeColumnImmediate = [&](ITChannelVoice& voice) {
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
            voice.panning = static_cast<uint8_t>((static_cast<uint16_t>(volParam) * 255U) / 64U);
            break;
        default:
            break;
        }
    };
    applyVolumeColumnImmediate(host);
    if (effectVoice && effectVoice != &host) {
        applyVolumeColumnImmediate(*effectVoice);
    }

    if (IsUnsupportedITCommand(effect, data)) {
#if defined(_DEBUG_ITPlayer_)
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"ITPlayer: MIDI/filter/plugin effect ignored.");
#endif
    }

    ITChannelVoice& voice = effectVoice ? *effectVoice : host;
    switch (effect) {
    case 0x01:
        if (data > 0) speed = data;
        break;
    case 0x02:
        sequencePosition = data < orders.size() ? data : 0;
        currentRow = 0;
        orderJumpPending = true;
        break;
    case 0x03: {
        const uint8_t row = static_cast<uint8_t>(((data >> 4) * 10) + (data & 0x0F));
        const uint16_t maxRow = unpackedPatterns[currentPatternIndex].empty()
            ? 0
            : static_cast<uint16_t>(unpackedPatterns[currentPatternIndex].size() - 1);
        nextRowOverride = std::min<uint16_t>(row, maxRow);
        ++sequencePosition;
        rowBreakPending = true;
        break;
    }
    case 0x04:
        if (data != 0) host.lastVolumeSlide = data;
        voice.lastVolumeSlide = host.lastVolumeSlide;
        // Fine volume slides (DFy = slide down, DxF = slide up) fire once on tick 0.
        // Regular slides are handled per-tick in ApplyVolumeSlide.
        {
            const uint8_t slideUp   = host.lastVolumeSlide >> 4;
            const uint8_t slideDown = host.lastVolumeSlide & 0x0F;
            if (slideUp == 0x0F && slideDown != 0) {
                voice.volume    = ClampVolume(static_cast<int>(voice.volume) - static_cast<int>(slideDown));
                voice.baseVolume = voice.volume;
            }
            else if (slideDown == 0x0F && slideUp != 0) {
                voice.volume    = ClampVolume(static_cast<int>(voice.volume) + static_cast<int>(slideUp));
                voice.baseVolume = voice.volume;
            }
        }
        break;
    case 0x05:
    case 0x06:
    case 0x07:
        if (data != 0) host.lastPortamento = data;
        voice.lastPortamento = host.lastPortamento;
        if (effect == 0x07 && hasPlayableNote && voice.sample) {
            voice.targetStep = NoteToFrequency(ITNoteToLinear(mappedNote), voice.sample->c5Speed) /
                static_cast<double>(IT_SAMPLE_RATE);
        }
        // Fine (EFy/FFy) and extra-fine (EEy/FEy) portamento fire once on tick 0.
        // Regular slides run per-tick inside ApplyTickEffects.
        if ((effect == 0x05 || effect == 0x06) && voice.active) {
            const uint8_t param = voice.lastPortamento;
            double fineFactor = 0.0;
            if ((param & 0xF0) == 0xF0 && (param & 0x0F) != 0) {
                // Fine portamento: EFy / FFy
                fineFactor = std::pow(2.0, static_cast<double>(param & 0x0F) / 768.0);
            }
            else if ((param & 0xF0) == 0xE0 && (param & 0x0F) != 0) {
                // Extra-fine portamento: EEy / FEy
                fineFactor = std::pow(2.0, static_cast<double>(param & 0x0F) / 3072.0);
            }
            if (fineFactor != 0.0) {
                voice.step     = (effect == 0x05) ? voice.step / fineFactor : voice.step * fineFactor;
                voice.baseStep = voice.step;
            }
        }
        break;
    case 0x08:
    case 0x15:
        // Hxx (vibrato) and Uxx (fine vibrato): parameter = speed/depth, stored in lastVibrato.
        if (data != 0) host.lastVibrato = data;
        voice.lastVibrato = host.lastVibrato;
        break;
    case 0x0B:
        // Kxx = H00 + Dxx: vibrato continues from last Hxx memory; xx param is the volume slide.
        if (data != 0) host.lastVolumeSlide = data;
        voice.lastVolumeSlide = host.lastVolumeSlide;
        voice.lastVibrato = host.lastVibrato;
        break;
    case 0x09:
        voice.tremorOnTicks  = (data >> 4) + 1;
        voice.tremorOffTicks = (data & 0x0F) + 1;
        // Per IT spec, the tremor counter persists across rows on the same channel; reset only
        // when a new voice is allocated (which zero-initialises all fields naturally).
        break;
    case 0x0C:
        // Lxx = G00 + Dxx: portamento continues from last Gxx memory; xx param is the volume slide.
        if (data != 0) host.lastVolumeSlide = data;
        voice.lastVolumeSlide = host.lastVolumeSlide;
        voice.lastPortamento = host.lastPortamento;
        if (hasPlayableNote && voice.sample) {
            voice.targetStep = NoteToFrequency(ITNoteToLinear(mappedNote), voice.sample->c5Speed) /
                static_cast<double>(IT_SAMPLE_RATE);
        }
        break;
    case 0x0D:
        host.channelVolume = ClampVolume(data);
        voice.channelVolume = host.channelVolume;
        break;
    case 0x0E:
        if (data != 0) host.lastChannelVolumeSlide = data;
        voice.lastChannelVolumeSlide = host.lastChannelVolumeSlide;
        // Fine channel-volume slides (NFy / NxF) fire once on tick 0.
        {
            const uint8_t cvUp   = host.lastChannelVolumeSlide >> 4;
            const uint8_t cvDown = host.lastChannelVolumeSlide & 0x0F;
            if (cvUp == 0x0F && cvDown != 0) {
                const uint8_t cv = ClampVolume(static_cast<int>(voice.channelVolume) - static_cast<int>(cvDown));
                voice.channelVolume = cv;
                host.channelVolume  = cv;
            }
            else if (cvDown == 0x0F && cvUp != 0) {
                const uint8_t cv = ClampVolume(static_cast<int>(voice.channelVolume) + static_cast<int>(cvUp));
                voice.channelVolume = cv;
                host.channelVolume  = cv;
            }
        }
        break;
    case 0x0F:
        if (data != 0) host.lastSampleOffset = data;
        voice.lastSampleOffset = host.lastSampleOffset;
        voice.lastSampleOffsetHigh = host.lastSampleOffsetHigh;
        if (voice.sample && hasPlayableNote && !tonePortamentoNote) {
            const uint32_t offset = (static_cast<uint32_t>(voice.lastSampleOffsetHigh) << 16) |
                (static_cast<uint32_t>(voice.lastSampleOffset) << 8);
            voice.position = static_cast<double>(std::min<uint32_t>(offset, voice.sample->length));
        }
        break;
    case 0x11:
        if (data != 0) host.lastRetrig = data;
        voice.lastRetrig = host.lastRetrig;
        break;
    case 0x12:
        if (data != 0) host.lastTremolo = data;
        voice.lastTremolo = host.lastTremolo;
        break;
    case 0x10:
        if (data != 0) host.lastPanningSlide = data;
        voice.lastPanningSlide = host.lastPanningSlide;
        break;
    case 0x13:
        ApplySpecialCommand(voice, data, true);
        host.lastSampleOffsetHigh = voice.lastSampleOffsetHigh;
        host.filterEnabled = voice.filterEnabled;
        host.filterCutoff = voice.filterCutoff;
        host.filterResonance = voice.filterResonance;
        host.surround = voice.surround;
        host.reversePlayback = voice.reversePlayback;
        host.panning = voice.panning;
        break;
    case 0x14:
        if (data >= 32) {
            tempo = data;
        }
        else if ((data >> 4) == 0x0 && (data & 0x0F) != 0) {
            tempo = static_cast<uint16_t>(std::max<int>(32, static_cast<int>(tempo) - static_cast<int>(data & 0x0F)));
        }
        else if ((data >> 4) == 0x1 && (data & 0x0F) != 0) {
            tempo = static_cast<uint16_t>(std::min<int>(255, static_cast<int>(tempo) + static_cast<int>(data & 0x0F)));
        }
        break;
    case 0x16:
        moduleGlobalVolume = static_cast<uint8_t>(std::min<uint16_t>(data, 128));
        break;
    case 0x17:
        if (data != 0) host.lastGlobalVolumeSlide = data;
        voice.lastGlobalVolumeSlide = host.lastGlobalVolumeSlide;
        // Fine global-volume slides (WFy / WxF) fire once on tick 0.
        {
            const uint8_t gvUp   = host.lastGlobalVolumeSlide >> 4;
            const uint8_t gvDown = host.lastGlobalVolumeSlide & 0x0F;
            int gvol = moduleGlobalVolume.load();
            if (gvUp == 0x0F && gvDown != 0) {
                moduleGlobalVolume = static_cast<uint8_t>(std::clamp(gvol - static_cast<int>(gvDown), 0, 128));
            }
            else if (gvDown == 0x0F && gvUp != 0) {
                moduleGlobalVolume = static_cast<uint8_t>(std::clamp(gvol + static_cast<int>(gvUp), 0, 128));
            }
        }
        break;
    case 0x18:
        voice.panning = data;
        host.panning = data;
        break;
    case 0x19:
        if (data != 0) voice.effectData = data;
        break;
    case 0x1A:
        if (data == 0) {
            voice.filterEnabled = false;
        }
        else {
            // Zxx maps the active MIDI macro byte onto this native one-pole low-pass path.
            // It is intentionally cheap enough for tracker polyphony while still honoring
            // filter-heavy IT files that use Zxx without VST automation.
            voice.filterEnabled = true;
            voice.filterCutoff = std::clamp((static_cast<float>(data) + 1.0f) / 256.0f, 0.02f, 1.0f);
            voice.filterResonance = static_cast<float>(data >> 4) / 15.0f;
        }
        host.filterEnabled = voice.filterEnabled;
        host.filterCutoff = voice.filterCutoff;
        host.filterResonance = voice.filterResonance;
        break;
    case 0x1C: {
        const uint8_t cutTick = data & 0x0F;
        if (cutTick > 0) voice.noteCutTick = cutTick;
        break;
    }
    case 0x1E:
        if (voice.active && data > 0) {
            voice.step *= std::pow(2.0, static_cast<double>(data) / 768.0);
            voice.baseStep = voice.step;
        }
        break;
    case 0x1F:
        if (voice.active && data > 0) {
            voice.step /= std::pow(2.0, static_cast<double>(data) / 768.0);
            voice.baseStep = voice.step;
        }
        break;
    default:
        break;
    }

    host.effect = effect;
    host.effectData = data;
    if (effectVoice) {
        effectVoice->effect = effect;
        effectVoice->effectData = data;
    }
}
void ITPlayer::ApplyTickEffects() {
    std::lock_guard<std::mutex> lock(playbackMutex);

    for (ITChannelVoice& voice : voices) {
        AdvanceVoiceEnvelope(voice);
    }

    for (size_t channel = 0; channel < hostChannels.size(); ++channel) {
        ITChannelVoice& host = hostChannels[channel];

        if (host.delayedNotePending && tick == host.delayTicks) {
            host.delayedNotePending = false;
            TriggerEvent(channel, host.delayedEvent, true);
        }

        ITChannelVoice* activeVoice = ActiveVoiceForHost(channel);
        if (!activeVoice || !activeVoice->sample || !activeVoice->active) {
            switch (host.effect) {
            case 0x0E:
                ApplyChannelVolumeSlide(host);
                break;
            case 0x10:
                ApplyPanningSlide(host);
                break;
            case 0x13:
                ApplySpecialCommand(host, host.effectData, false);
                break;
            case 0x17:
                ApplyGlobalVolumeSlide(host);
                break;
            default:
                break;
            }
            continue;
        }

        ITChannelVoice& voice = *activeVoice;
        if (voice.noteCutTick != 255 && tick == voice.noteCutTick) {
            voice.active = false;
            voice.noteCutTick = 255;
            host.activeVoiceIndex = -1;
            continue;
        }

        switch (voice.effect) {
        case 0x04:
            ApplyVolumeSlide(voice);
            break;

        case 0x05: {
            const uint8_t amount = voice.lastPortamento;
            // Fine (EFy) and extra-fine (EEy) already fired on tick 0 in TriggerEvent; skip here.
            if ((amount & 0xF0) != 0xF0 && (amount & 0xF0) != 0xE0) {
                voice.step /= std::pow(2.0, static_cast<double>(amount) / 768.0);
                voice.baseStep = voice.step;
            }
            break;
        }

        case 0x06: {
            const uint8_t amount = voice.lastPortamento;
            // Fine (FFy) and extra-fine (FEy) already fired on tick 0 in TriggerEvent; skip here.
            if ((amount & 0xF0) != 0xF0 && (amount & 0xF0) != 0xE0) {
                voice.step *= std::pow(2.0, static_cast<double>(amount) / 768.0);
                voice.baseStep = voice.step;
            }
            break;
        }

        case 0x07:
            ApplyPortamento(voice);
            break;

        case 0x08:
            ApplyVibrato(voice, false);
            host.vibratoPos = voice.vibratoPos;
            break;

        case 0x09: {
            const uint8_t cycle = voice.tremorOnTicks + voice.tremorOffTicks;
            if (cycle > 0) {
                voice.volume = (voice.tremorCounter % cycle) < voice.tremorOnTicks ? voice.baseVolume : 0;
                ++voice.tremorCounter;
            }
            break;
        }

        case 0x0A: {
            const uint8_t offsetA = voice.effectData >> 4;
            const uint8_t offsetB = voice.effectData & 0x0F;
            const uint8_t semitoneOffset = (tick % 3 == 1) ? offsetA : ((tick % 3 == 2) ? offsetB : 0);
            const int transposed = std::clamp(static_cast<int>(ITNoteToLinear(voice.note)) + static_cast<int>(semitoneOffset), 0, 119);
            voice.step = NoteToFrequency(static_cast<uint8_t>(transposed), voice.sample->c5Speed) /
                static_cast<double>(IT_SAMPLE_RATE);
            break;
        }

        case 0x0B:
            ApplyVibrato(voice, false);
            host.vibratoPos = voice.vibratoPos;
            ApplyVolumeSlide(voice);
            break;

        case 0x0C:
            ApplyPortamento(voice);
            ApplyVolumeSlide(voice);
            break;

        case 0x0E:
            ApplyChannelVolumeSlide(voice);
            host.channelVolume = voice.channelVolume;
            break;

        case 0x10:
            ApplyPanningSlide(voice);
            host.panning = voice.panning;
            break;

        case 0x11:
            ApplyRetrig(voice);
            break;

        case 0x12:
            ApplyTremolo(voice);
            host.tremoloPos = voice.tremoloPos;
            break;

        case 0x13:
            ApplySpecialCommand(voice, voice.effectData, false);
            break;

        case 0x15:
            ApplyVibrato(voice, true);
            host.vibratoPos = voice.vibratoPos;
            break;

        case 0x17:
            ApplyGlobalVolumeSlide(voice);
            break;

        case 0x19:
            ApplyPanbrello(voice);
            host.panbrelloPos = voice.panbrelloPos;
            break;

        default:
            break;
        }

        ApplyVolumeColumnTick(voice);
        // Sync the volume-column vibrato oscillator position back to host so the no-retrig
        // phase carry works on the next note trigger.
        if (voice.volCommand == VOLCMD_VIBRATO) {
            host.vibratoPos = voice.vibratoPos;
        }
    }
}
void ITPlayer::ApplyVolumeColumnTick(ITChannelVoice& voice) {
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
        if (voice.targetStep > 0.0) {
            // Same multiplicative semitone-space arithmetic as Gxx; the VolColumnPortaSpeed table
            // provides the raw param value, which is 1/64-semitone units per tick.
            const uint8_t rawSpeed = VolColumnPortaSpeed[std::min<uint8_t>(voice.volParam, 9)];
            const double factor = std::pow(2.0, static_cast<double>(rawSpeed) / 768.0);
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
        // Caller (ApplyTickEffects) syncs vibratoPos back to the host channel after this call.
        break;
    }

    default:
        break;
    }
}

void ITPlayer::ApplyVolumeSlide(ITChannelVoice& voice) {
    const uint8_t data = voice.lastVolumeSlide != 0 ? voice.lastVolumeSlide : voice.effectData;
    const uint8_t up = data >> 4;
    const uint8_t down = data & 0x0F;
    int volume = voice.volume;

    if (up == 0x0F && down != 0) {
        // Fine slide down (DFy) -- already applied on tick 0 in TriggerEvent; no per-tick action.
    }
    else if (down == 0x0F && up != 0) {
        // Fine slide up (DxF) -- already applied on tick 0 in TriggerEvent; no per-tick action.
    }
    else if (up != 0 && down == 0) {
        volume += up;
    }
    else if (down != 0 && up == 0) {
        volume -= down;
    }

    voice.volume = ClampVolume(volume);
    voice.baseVolume = voice.volume;
}

void ITPlayer::ApplyChannelVolumeSlide(ITChannelVoice& voice) {
    const uint8_t data = voice.lastChannelVolumeSlide != 0 ? voice.lastChannelVolumeSlide : voice.effectData;
    const uint8_t up = data >> 4;
    const uint8_t down = data & 0x0F;
    int volume = voice.channelVolume;

    if (up == 0x0F && down != 0) {
        // Fine slide down (NFy) -- already applied on tick 0 in TriggerEvent; no per-tick action.
    }
    else if (down == 0x0F && up != 0) {
        // Fine slide up (NxF) -- already applied on tick 0 in TriggerEvent; no per-tick action.
    }
    else if (up != 0 && down == 0) {
        volume += up;
    }
    else if (down != 0 && up == 0) {
        volume -= down;
    }

    voice.channelVolume = ClampVolume(volume);
}

void ITPlayer::ApplyPanningSlide(ITChannelVoice& voice) {
    const uint8_t data = voice.lastPanningSlide != 0 ? voice.lastPanningSlide : voice.effectData;
    const uint8_t right = data >> 4;
    const uint8_t left = data & 0x0F;
    int pan = voice.panning;

    if (right == 0x0F && left != 0) {
        if (tick == 1) {
            pan -= left;
        }
    }
    else if (left == 0x0F && right != 0) {
        if (tick == 1) {
            pan += right;
        }
    }
    else if (right != 0 && left == 0) {
        pan += right * 4;
    }
    else if (left != 0 && right == 0) {
        pan -= left * 4;
    }

    voice.panning = static_cast<uint8_t>(std::clamp(pan, 0, 255));
}
void ITPlayer::ApplyGlobalVolumeSlide(ITChannelVoice& voice) {
    const uint8_t data = voice.lastGlobalVolumeSlide != 0 ? voice.lastGlobalVolumeSlide : voice.effectData;
    const uint8_t up = data >> 4;
    const uint8_t down = data & 0x0F;
    int volume = moduleGlobalVolume.load();

    if (up == 0x0F && down != 0) {
        // Fine slide down (WFy) -- already applied on tick 0 in TriggerEvent; no per-tick action.
    }
    else if (down == 0x0F && up != 0) {
        // Fine slide up (WxF) -- already applied on tick 0 in TriggerEvent; no per-tick action.
    }
    else if (up != 0 && down == 0) {
        volume += up;
    }
    else if (down != 0 && up == 0) {
        volume -= down;
    }

    moduleGlobalVolume = static_cast<uint8_t>(std::clamp(volume, 0, 128));
}

void ITPlayer::ApplyPortamento(ITChannelVoice& voice) {
    if (voice.targetStep <= 0.0) {
        return;
    }

    // IT linear-frequency slide: each unit = 1/64 of a semitone, so the per-tick factor is
    // 2^(param/768). Additive step arithmetic is wrong here -- it produces pitch changes that
    // vary with frequency (fast at low notes, imperceptibly slow at high notes).
    const uint8_t param = voice.lastPortamento != 0 ? voice.lastPortamento : voice.effectData;
    const double factor = std::pow(2.0, static_cast<double>(param) / 768.0);
    if (voice.step < voice.targetStep) {
        voice.step = std::min(voice.step * factor, voice.targetStep);
    }
    else if (voice.step > voice.targetStep) {
        voice.step = std::max(voice.step / factor, voice.targetStep);
    }
    if (voice.glissandoControl && voice.sample) {
        const double frequency = voice.step * static_cast<double>(IT_SAMPLE_RATE);
        const double note = 60.0 + 12.0 * std::log2(frequency / static_cast<double>(voice.sample->c5Speed));
        const uint8_t rounded = static_cast<uint8_t>(std::clamp<int>(static_cast<int>(std::round(note)), 0, 119));
        voice.step = NoteToFrequency(rounded, voice.sample->c5Speed) / static_cast<double>(IT_SAMPLE_RATE);
    }
    voice.baseStep = voice.step;
}

void ITPlayer::ApplyVibrato(ITChannelVoice& voice, bool fine) {
    const uint8_t data = voice.lastVibrato != 0 ? voice.lastVibrato : voice.effectData;
    const uint8_t speedValue = data >> 4;
    const uint8_t depth = data & 0x0F;
    const double divisor = fine ? 8192.0 : 2048.0;
    const double delta = Waveform(voice.vibratoPos, voice.vibratoWaveform) * static_cast<double>(depth) / divisor;
    voice.step = std::max(0.0, voice.baseStep + delta);
    voice.vibratoPos = static_cast<uint8_t>(voice.vibratoPos + speedValue);
}

void ITPlayer::ApplyTremolo(ITChannelVoice& voice) {
    const uint8_t data = voice.lastTremolo != 0 ? voice.lastTremolo : voice.effectData;
    const uint8_t speedValue = data >> 4;
    const uint8_t depth = data & 0x0F;
    const int delta = static_cast<int>(Waveform(voice.tremoloPos, voice.tremoloWaveform) * static_cast<double>(depth));
    voice.volume = ClampVolume(static_cast<int>(voice.baseVolume) + delta);
    voice.tremoloPos = static_cast<uint8_t>(voice.tremoloPos + speedValue);
}

void ITPlayer::ApplyPanbrello(ITChannelVoice& voice) {
    const uint8_t speedValue = voice.effectData >> 4;
    const uint8_t depth = voice.effectData & 0x0F;
    const int delta = static_cast<int>(Waveform(voice.panbrelloPos, voice.panbrelloWaveform) * static_cast<double>(depth) * 4.0);
    voice.panning = static_cast<uint8_t>(std::clamp(static_cast<int>(voice.panning) + delta, 0, 255));
    voice.panbrelloPos = static_cast<uint8_t>(voice.panbrelloPos + speedValue);
}

void ITPlayer::ApplyRetrig(ITChannelVoice& voice) {
    const uint8_t data = voice.lastRetrig != 0 ? voice.lastRetrig : voice.effectData;
    const uint8_t interval = data & 0x0F;
    const uint8_t volumeCommand = data >> 4;

    if (interval == 0 || (tick % interval) != 0) {
        return;
    }

    voice.position = 0.0;
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

float ITPlayer::EvaluateVolumeEnvelope(const ITInstrument& instrument, uint16_t envelopeTick) const {
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

float ITPlayer::EvaluatePanningEnvelope(const ITInstrument& instrument, uint16_t envelopeTick) const {
    const uint8_t pointCount = instrument.panningEnvelopePointCount;
    if (!instrument.HasPanningEnvelope()) {
        return 0.0f;
    }

    if (envelopeTick <= instrument.panningEnvelopeTicks[0]) {
        return static_cast<float>(instrument.panningEnvelopeValues[0]) / 32.0f;
    }

    for (uint8_t point = 1; point < pointCount; ++point) {
        const uint16_t leftTick = instrument.panningEnvelopeTicks[point - 1];
        const uint16_t rightTick = instrument.panningEnvelopeTicks[point];
        if (envelopeTick <= rightTick) {
            const float leftValue = static_cast<float>(instrument.panningEnvelopeValues[point - 1]);
            const float rightValue = static_cast<float>(instrument.panningEnvelopeValues[point]);
            const uint16_t tickSpan = rightTick > leftTick ? static_cast<uint16_t>(rightTick - leftTick) : 1;
            const float t = static_cast<float>(envelopeTick - leftTick) / static_cast<float>(tickSpan);
            return (leftValue + (rightValue - leftValue) * t) / 32.0f;
        }
    }

    return static_cast<float>(instrument.panningEnvelopeValues[pointCount - 1]) / 32.0f;
}

void ITPlayer::AdvanceVoiceEnvelope(ITChannelVoice& voice) {
    const ITInstrument* instrument = voice.instrumentDef;
    if (!voice.active || !instrument) {
        return;
    }

    if (instrument->HasVolumeEnvelope()) {
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
            voice.active = false;
        }
    }

    if (instrument->HasPanningEnvelope()) {
        voice.envelopePanning = EvaluatePanningEnvelope(*instrument, voice.panEnvelopeTick);

        const uint8_t lastPoint = static_cast<uint8_t>(instrument->panningEnvelopePointCount - 1);
        const uint16_t lastTick = instrument->panningEnvelopeTicks[lastPoint];
        const bool sustainEnabled = (instrument->panningEnvelopeFlags & 0x04) != 0 && !voice.noteReleased;
        const bool loopEnabled = (instrument->panningEnvelopeFlags & 0x02) != 0;

        if (sustainEnabled && instrument->panningEnvelopeSustainStart <= instrument->panningEnvelopeSustainEnd &&
            instrument->panningEnvelopeSustainEnd <= lastPoint &&
            voice.panEnvelopeTick >= instrument->panningEnvelopeTicks[instrument->panningEnvelopeSustainEnd]) {
            voice.panEnvelopeTick = instrument->panningEnvelopeTicks[instrument->panningEnvelopeSustainStart];
        }
        else if (loopEnabled && instrument->panningEnvelopeLoopStart <= instrument->panningEnvelopeLoopEnd &&
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
            voice.active = false;
        }
    }
}
void ITPlayer::ApplySpecialCommand(ITChannelVoice& voice, uint8_t data, bool rowTick) {
    const uint8_t sub = data >> 4;
    const uint8_t value = data & 0x0F;

    if (rowTick) {
        switch (sub) {
        case 0x0: // S0x: native cutoff control for IT filter macros.
            voice.filterEnabled = value != 0;
            voice.filterCutoff = std::clamp((static_cast<float>(value) + 1.0f) / 16.0f, 0.02f, 1.0f);
            break;

        case 0x1: // S1x: glissando control for tone portamento.
            voice.glissandoControl = value != 0;
            break;

        case 0x2: // S2x: finetune. Approximate IT's table as eighth-semitone offsets.
            if (voice.active) {
                const int offset = static_cast<int>(value) - 8;
                voice.step *= std::pow(2.0, static_cast<double>(offset) / 96.0);
                voice.baseStep = voice.step;
            }
            break;

        case 0x3: // S3x: set vibrato waveform; bits 0-1 = shape, bit 2 = no-retrig on new note
            voice.vibratoWaveform = value & 0x07;
            break;

        case 0x4: // S4x: set tremolo waveform; bits 0-1 = shape, bit 2 = no-retrig
            voice.tremoloWaveform = value & 0x07;
            break;

        case 0x5: // S5x: set panbrello waveform; bits 0-1 = shape, bit 2 = no-retrig
            voice.panbrelloWaveform = value & 0x07;
            break;

        case 0x6: // S6x: fine pattern delay -- add x extra ticks to this row only.
            fineDelayTicks = value;
            break;

        case 0x7: // S7x: IT NNA / envelope controls.
            switch (value) {
            case 0x0: case 0x1: case 0x2: case 0x3:
                ApplyNewNoteAction(voice, value);
                break;
            case 0x4:
                voice.envelopeTick = 0;
                voice.panEnvelopeTick = 0;
                voice.noteReleased = false;
                break;
            case 0x5:
                voice.noteReleased = true;
                break;
            case 0x6:
                voice.noteReleased = true;
                voice.fadeOutVolume = std::min<uint32_t>(voice.fadeOutVolume, 32768);
                break;
            default:
                break;
            }
            break;

        case 0x8: // S8x: set coarse panning (0..15 -> 0..255)
            voice.panning = static_cast<uint8_t>((static_cast<uint16_t>(value) * 255U) / 15U);
            break;

        case 0x9: // S9x: sound control. Implement the native surround/reverse subset.
            switch (value) {
            case 0x0:
                voice.surround = false;
                voice.reversePlayback = false;
                break;
            case 0x1:
                voice.surround = true;
                break;
            case 0x8:
                voice.surround = false;
                break;
            case 0xB:
                voice.reversePlayback = true;
                if (voice.sample && voice.position <= 0.0 && !voice.sample->pcm.empty()) {
                    voice.position = static_cast<double>(voice.sample->pcm.size() - 1);
                }
                break;
            case 0xC:
                voice.reversePlayback = false;
                break;
            default:
                break;
            }
            break;

        case 0xA: // SAy: high byte of Oxx sample offset.
            voice.lastSampleOffsetHigh = value;
            break;

        case 0xB: // SBx: pattern loop.
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

        case 0xC: // SCx: note cut after x ticks.
            voice.noteCutTick = value;
            if (value == 0) {
                voice.active = false;
            }
            break;

        case 0xE: // SEx: pattern delay for x rows.
            if (!inPatternDelay) {
                patternDelayTicks = value;
                inPatternDelay = true;
            }
            break;

        case 0xF: // SFx: select macro slot; native playback has no external plugin macro table.
            break;

        // SDx note delay is handled before the row event is triggered.
        default:
            break;
        }
        return;
    }

    if (sub == 0xC && tick == value) {
        voice.active = false;
        voice.noteCutTick = 255;
    }
}
void ITPlayer::AdvanceRow() {
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

const ITSample* ITPlayer::ResolveSample(uint8_t instrument, uint8_t note, uint8_t& mappedNote) const {
    if (instrument == 0) {
        return nullptr;
    }

    if (!instruments.empty() && instrument <= instruments.size() && note > 0 && note < IT_NOTE_FADE) {
        const ITInstrument& inst = instruments[instrument - 1];
        const size_t noteIndex = static_cast<size_t>(std::min<uint8_t>(ITNoteToLinear(note), 119));
        const uint8_t sampleIndex = inst.sampleMap[noteIndex];
        mappedNote = inst.noteMap[noteIndex] != 0 ? inst.noteMap[noteIndex] : note;
        if (sampleIndex > 0 && sampleIndex <= samples.size()) {
            return &samples[sampleIndex - 1];
        }
    }

    if (instrument <= samples.size()) {
        return &samples[instrument - 1];
    }

    return nullptr;
}

double ITPlayer::NoteToFrequency(uint8_t note, uint32_t c5Speed) const {
    const int noteIndex = std::clamp<int>(note, 0, 119);
    const int c5Index = 5 * 12;
    return static_cast<double>(c5Speed) * std::pow(2.0, static_cast<double>(noteIndex - c5Index) / 12.0);
}

double ITPlayer::Waveform(uint8_t pos, uint8_t type) const {
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

uint8_t ITPlayer::DefaultPanningForChannel(size_t channel) const {
    if (channel < IT_MAX_CHANNELS) {
        const uint8_t pan = header.channelPan[channel];
        if ((pan & 0x80) != 0) {
            return 128;
        }
        return static_cast<uint8_t>((static_cast<uint16_t>(std::min<uint8_t>(pan, 64)) * 255U) / 64U);
    }

    return (channel & 1) == 0 ? 48 : 208;
}

void ITPlayer::GotoSequenceID(uint16_t patternSeqID) {
    if (!isPlaying) {
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"ITPlayer: GotoSequenceID called while player is not active.");
        return;
    }

    if (patternSeqID >= orders.size()) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"ITPlayer: Invalid PatternSeqID " + std::to_wstring(patternSeqID));
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
        rowBreakPending = false;
        orderJumpPending = false;
        patternDelayTicks = 0;
        fineDelayTicks = 0;
        inPatternDelay = false;
        patternLoopPending = false;
    }

    SetFadeIn(1000);
}

void ITPlayer::PlaybackLoop() {
    using namespace std::chrono;

    auto tickStart = high_resolution_clock::now();
    auto fadeClock = high_resolution_clock::now();
    while (isPlaying && !isTerminating) {
        if (!isPaused) {
            FillAudioBuffer();

            const auto now = high_resolution_clock::now();
            const auto elapsedTime = duration_cast<microseconds>(now - tickStart).count();
            const double tickDurationUs = (2500.0 / static_cast<double>(std::max<uint16_t>(tempo, 32))) * 1000.0;

            if (elapsedTime >= tickDurationUs) {
                tickStart = now;

                if (tick == 0) {
                    TickRow();
                }
                else {
                    ApplyTickEffects();
                }

                // S6x fine pattern delay lengthens only the current row by fineDelayTicks ticks.
                const uint16_t ticksThisRow = std::max<uint16_t>(static_cast<uint16_t>(speed + fineDelayTicks), 1);
                tick = static_cast<uint16_t>((tick + 1) % ticksThisRow);
            }

            if (fadeInActive || fadeOutActive) {
                const auto fadeNow = high_resolution_clock::now();
                const uint32_t elapsedMs = static_cast<uint32_t>(duration_cast<milliseconds>(fadeNow - fadeClock).count());
                fadeClock = fadeNow;

                const uint32_t duration = fadeDurationMs.load();
                if (duration > 0) {
                    fadeElapsedMs = std::min<uint32_t>(duration, fadeElapsedMs.load() + elapsedMs);
                    const float amount = static_cast<float>(fadeElapsedMs.load()) / static_cast<float>(duration);
                    if (fadeInActive) {
                        currentVolume = static_cast<uint8_t>(std::clamp<int>(static_cast<int>(64.0f * amount), 0, 64));
                        if (fadeElapsedMs >= duration) {
                            fadeInActive = false;
                        }
                    }
                    else {
                        currentVolume = static_cast<uint8_t>(std::clamp<int>(static_cast<int>(64.0f * (1.0f - amount)), 0, 64));
                        if (fadeElapsedMs >= duration) {
                            fadeOutActive = false;
                        }
                    }
                }
            }
            else {
                fadeClock = high_resolution_clock::now();
            }

            std::this_thread::sleep_for(milliseconds(1));
        }
        else {
            FillAudioBuffer();
            std::this_thread::sleep_for(milliseconds(1));
        }
    }

    isTerminating = false;
}
