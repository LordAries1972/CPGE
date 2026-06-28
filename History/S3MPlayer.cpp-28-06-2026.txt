#include "Includes.h"
#include "S3MPlayer.h"
#include "Debug.h"
#include "Configuration.h"

extern Debug debug;
extern Configuration config;

#if defined(PLATFORM_WINDOWS)
    extern HWND hwnd;
    #pragma comment(lib, "dsound.lib")
    #pragma comment(lib, "dxguid.lib")
    #pragma comment(lib, "winmm.lib")
#endif

namespace {
    constexpr uint32_t S3M_SAMPLE_RATE = 44100;
    constexpr uint16_t S3M_ROWS_PER_PATTERN = 64;
    constexpr uint8_t S3M_EMPTY_NOTE = 255;
    constexpr uint8_t S3M_KEY_OFF = 254;

    uint16_t ReadLE16(const uint8_t* data) {
        return static_cast<uint16_t>(data[0]) |
            (static_cast<uint16_t>(data[1]) << 8);
    }

    int16_t ReadLE16Signed(const uint8_t* data) {
        return static_cast<int16_t>(ReadLE16(data));
    }

    uint8_t ClampVolume(int value) {
        return static_cast<uint8_t>(std::clamp(value, 0, 64));
    }

    // Per-sample attack envelope rate for OPL2 at 44100 Hz.
    // AR=0: no advance; AR>=15: instant (returns >1 so the clamp fires immediately).
    // Reference: AR=1 => ~7373 ms, halving each step (OPL2 hardware timing).
    float OplAttackRate(uint8_t ar) {
        if (ar == 0) return 0.0f;
        if (ar >= 15) return 2.0f;
        const float ms = 7372.8f / static_cast<float>(1 << (ar - 1));
        return 1000.0f / (ms * 44100.0f);
    }

    // Per-sample decay/release envelope rate for OPL2 at 44100 Hz.
    // Decay is approximately 2x slower than attack on real hardware.
    float OplDecayRate(uint8_t dr) {
        if (dr == 0) return 0.0f;
        if (dr >= 15) return 0.5f;
        const float ms = 14745.6f / static_cast<float>(1 << (dr - 1));
        return 1000.0f / (ms * 44100.0f);
    }

    // OPL2 sustain level: nibble 0-15, where 0=peak and 15=silence.
    // Each step is -3 dB; nibble 15 is treated as total silence.
    float OplSustainLevel(uint8_t sl) {
        if (sl >= 15) return 0.0f;
        if (sl == 0) return 1.0f;
        return std::pow(10.0f, -static_cast<float>(sl) * 3.0f / 20.0f);
    }

    // OPL2 total level (TL): 6-bit value, 0.75 dB per step.
    // TL=0 => unity (1.0); TL=63 => ~-47 dB (nearly silent).
    float DecodeOplTotalLevel(uint8_t levelRegister) {
        const uint8_t totalLevel = levelRegister & 0x3F;
        if (totalLevel == 0) return 1.0f;
        return std::pow(10.0f, -static_cast<float>(totalLevel) * 0.75f / 20.0f);
    }

    float DecodeOplMultiplier(uint8_t characteristicRegister) {
        const uint8_t multiplier = characteristicRegister & 0x0F;
        return multiplier == 0 ? 0.5f : static_cast<float>(multiplier);
    }

    float TrackerWaveform(uint8_t waveform, uint8_t position) {
        const uint8_t pos = position & 0x3F;
        switch (waveform & 0x03) {
        case 0x01:
            return pos < 32 ? 1.0f : -1.0f;
        case 0x02:
            return 1.0f - (static_cast<float>(pos) / 32.0f);
        case 0x03:
            return (pos & 1) ? -1.0f : 1.0f;
        default:
            return std::sin(static_cast<float>(pos) * (3.14159265358979323846f * 2.0f / 64.0f));
        }
    }

    float OplWaveform(uint8_t waveform, double phase) {
        const double wrapped = phase - std::floor(phase);
        const float sine = static_cast<float>(std::sin(wrapped * 6.28318530717958647692));

        switch (waveform & 0x07) {
        case 0x01:
            return sine < 0.0f ? 0.0f : sine;
        case 0x02:
            return std::fabs(sine);
        case 0x03:
            return wrapped < 0.5 ? std::fabs(sine) : 0.0f;
        case 0x04:
            return wrapped < 0.5 ? 1.0f : -1.0f;
        case 0x05:
            return (wrapped * 2.0f) - 1.0f;
        case 0x06:
            return wrapped < 0.5 ? ((wrapped * 4.0f) - 1.0f) : (3.0f - (wrapped * 4.0f));
        case 0x07:
            return sine >= 0.0f ? 1.0f : -1.0f;
        default:
            return sine;
        }
    }
}

bool S3MPlayer::Initialize(const std::wstring& filename) {
#if defined(_DEBUG_S3MPlayer_)
    debug.DebugLog("S3MPlayer initialization started...\n");
#endif
    if (!bIsInitialized) {
        if (!CreateAudioDevice()) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"S3MPlayer: Failed to initialize audio device.");
            return false;
        }

        bIsInitialized = true;
    }

    if (!LoadS3MFile(filename)) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"S3MPlayer: LoadS3MFile failed.");
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
    patternLoopJumpPending = false;

    speed = s3mHeader.initialSpeed > 0 ? s3mHeader.initialSpeed : 6;
    tempo = s3mHeader.initialTempo > 0 ? s3mHeader.initialTempo : 125;
    rowDurationTicks = std::max<uint16_t>(speed, 1);
    patternLoopRow.fill(0);
    patternLoopRemaining.fill(-1);

    voices.clear();
    voices.resize(32);
    for (size_t channel = 0; channel < voices.size(); ++channel) {
        voices[channel].panning = DefaultPanningForChannel(channel);
        voices[channel].basePanning = voices[channel].panning;
    }

    moduleGlobalVolume = std::min<uint8_t>(s3mHeader.globalVolume, 64);
    // Mirror the XMPlayer pattern: bind all three volume atomics to config.musicVolume
    // so SetVolume() / SetGlobalVolume() and the config system agree on the starting level.
    globalVolume  = static_cast<uint8_t>(std::clamp(config.myConfig.musicVolume, 0, 64));
    currentVolume = globalVolume.load();
    targetVolume  = globalVolume.load();
    fadeInActive = false;
    fadeOutActive = false;
    isMuted = false;

#if defined(_DEBUG_S3MPlayer_)
    debug.DebugLog("S3MPlayer initialization successful.\n");
#endif
    return true;
}

bool S3MPlayer::LoadS3MFile(const std::wstring& filename) {
    debug.logLevelMessage(LogLevel::LOG_INFO, L"Loading S3M file: " + filename);

    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"S3MPlayer: Failed to open S3M file.");
        return false;
    }

    file.read(reinterpret_cast<char*>(&s3mHeader), sizeof(s3mHeader));
    if (file.gcount() != sizeof(s3mHeader)) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"S3MPlayer: Failed to read S3M header.");
        return false;
    }

    if (s3mHeader.signature != 0x1A || s3mHeader.type != 0x10 ||
        std::strncmp(s3mHeader.magic, "SCRM", 4) != 0) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"S3MPlayer: Invalid S3M signature.");
        return false;
    }

    if (s3mHeader.orderCount == 0 || s3mHeader.orderCount > 256 ||
        s3mHeader.instrumentCount > 255 || s3mHeader.patternCount > 255) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"S3MPlayer: Header counts are out of supported range.");
        return false;
    }

    orders.resize(s3mHeader.orderCount);
    file.read(reinterpret_cast<char*>(orders.data()), orders.size());
    if (file.gcount() != static_cast<std::streamsize>(orders.size())) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"S3MPlayer: Failed to read order table.");
        return false;
    }

    std::vector<uint16_t> instrumentParaPointers(s3mHeader.instrumentCount);
    std::vector<uint16_t> patternParaPointers(s3mHeader.patternCount);

    file.read(reinterpret_cast<char*>(instrumentParaPointers.data()), instrumentParaPointers.size() * sizeof(uint16_t));
    if (file.gcount() != static_cast<std::streamsize>(instrumentParaPointers.size() * sizeof(uint16_t))) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"S3MPlayer: Failed to read instrument parapointers.");
        return false;
    }

    file.read(reinterpret_cast<char*>(patternParaPointers.data()), patternParaPointers.size() * sizeof(uint16_t));
    if (file.gcount() != static_cast<std::streamsize>(patternParaPointers.size() * sizeof(uint16_t))) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"S3MPlayer: Failed to read pattern parapointers.");
        return false;
    }

    channelEnabled.assign(32, false);
    defaultPanning.resize(32);
    for (size_t channel = 0; channel < 32; ++channel) {
        channelEnabled[channel] = s3mHeader.channelSettings[channel] < 16;
        if (s3mHeader.channelSettings[channel] < 16) {
            defaultPanning[channel] = s3mHeader.channelSettings[channel] < 8 ? 48 : 208;
        }
        else {
            defaultPanning[channel] = (channel & 1) == 0 ? 48 : 208;
        }
    }

    if (s3mHeader.defaultPanFlag == 0xFC) {
        uint8_t panSettings[32] = {};
        file.read(reinterpret_cast<char*>(panSettings), sizeof(panSettings));
        if (file.gcount() != sizeof(panSettings)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"S3MPlayer: Failed to read default pan table.");
            return false;
        }

        for (size_t channel = 0; channel < 32; ++channel) {
            if ((panSettings[channel] & 0x20) != 0) {
                const uint8_t panNibble = panSettings[channel] & 0x0F;
                const uint8_t pan = static_cast<uint8_t>((static_cast<uint16_t>(panNibble) * 255U) / 15U);
                defaultPanning[channel] = pan;
            }
        }
    }

    if (!LoadSamples(file, instrumentParaPointers)) {
        return false;
    }

    if (!LoadPatterns(file, patternParaPointers)) {
        return false;
    }

    UnpackPatterns();

    debug.logLevelMessage(LogLevel::LOG_INFO, L"S3M file loaded successfully.");
    return true;
}

bool S3MPlayer::LoadSamples(std::ifstream& file, const std::vector<uint16_t>& instrumentParaPointers) {
    samples.clear();
    samples.resize(instrumentParaPointers.size());

    for (size_t i = 0; i < instrumentParaPointers.size(); ++i) {
        const std::streamoff headerOffset = static_cast<std::streamoff>(instrumentParaPointers[i]) * 16;
        if (headerOffset <= 0) {
            continue;
        }

        file.seekg(headerOffset, std::ios::beg);
        S3MSampleHeader header{};
        file.read(reinterpret_cast<char*>(&header), sizeof(header));
        if (file.gcount() != sizeof(header)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"S3MPlayer: Failed to read sample header " + std::to_wstring(i));
            return false;
        }

        S3MSample& sample = samples[i];
        sample.type = header.type;
        sample.length = header.length;
        sample.loopStart = header.loopStart;
        sample.loopEnd = header.loopEnd;
        sample.volume = std::min<uint8_t>(header.volume, 64);
        sample.flags = header.flags;
        sample.c2Speed = header.c2Speed > 0 ? header.c2Speed : 8363;
        sample.isAdLib = sample.type >= 2 && sample.type <= 7;
        std::memcpy(sample.name, header.sampleName, sizeof(sample.name));

        if (sample.isAdLib) {
            std::memcpy(sample.adLibRegisters.data(), &header.length, sample.adLibRegisters.size());
            // SCRI instruments store the 12 core OPL operator/register bytes
            // at this location.  The mixer turns them into a lightweight
            // two-operator FM voice so AdLib-heavy S3Ms do not drop channels.
            sample.length = 0;
            sample.loopStart = 0;
            sample.loopEnd = 0;
            continue;
        }

        if (sample.type != 1 || sample.length == 0) {
            continue;
        }

        if (header.packing != 0) {
            debug.logLevelMessage(LogLevel::LOG_WARNING,
                L"S3MPlayer: Packed/compressed sample ignored at instrument " + std::to_wstring(i));
            continue;
        }

        // S3M stores the sample-data parapointer as high byte first, then a
        // little-endian low word.  Treating the three bytes as a normal 24-bit
        // little-endian integer seeks far past the real sample block.
        const uint32_t sampleParaPointer =
            (static_cast<uint32_t>(header.dataPointerHighByte) << 16) |
            static_cast<uint32_t>(header.dataPointerLowWord);
        const std::streamoff sampleOffset = static_cast<std::streamoff>(sampleParaPointer) * 16;
        const bool isStereo = (sample.flags & 0x02) != 0;
        const bool is16Bit = (sample.flags & 0x04) != 0;
        const bool signedSamples = s3mHeader.sampleType == 1;
        const uint32_t sourceChannels = isStereo ? 2U : 1U;
        const uint32_t bytesPerSample = is16Bit ? 2U : 1U;
        const uint32_t byteCount = sample.length * sourceChannels * bytesPerSample;

        if (sampleOffset <= 0 || byteCount == 0) {
            continue;
        }

        const std::streampos restorePosition = file.tellg();
        file.seekg(0, std::ios::end);
        const std::streamoff fileSize = static_cast<std::streamoff>(file.tellg());
        file.seekg(restorePosition, std::ios::beg);

        if (sampleOffset < 0 || sampleOffset + static_cast<std::streamoff>(byteCount) > fileSize) {
            debug.logLevelMessage(LogLevel::LOG_ERROR,
                L"S3MPlayer: Sample data outside file for instrument " + std::to_wstring(i) +
                L" (offset=" + std::to_wstring(static_cast<long long>(sampleOffset)) +
                L", bytes=" + std::to_wstring(byteCount) +
                L", fileSize=" + std::to_wstring(static_cast<long long>(fileSize)) + L")");
            return false;
        }

        std::vector<uint8_t> raw(byteCount);
        file.seekg(sampleOffset, std::ios::beg);
        file.read(reinterpret_cast<char*>(raw.data()), raw.size());
        if (file.gcount() != static_cast<std::streamsize>(raw.size())) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"S3MPlayer: Failed to read sample data " + std::to_wstring(i));
            return false;
        }

        sample.pcm.resize(sample.length);
        if (is16Bit) {
            for (uint32_t s = 0; s < sample.length; ++s) {
                int32_t mixedValue = 0;
                for (uint32_t sourceChannel = 0; sourceChannel < sourceChannels; ++sourceChannel) {
                    const size_t rawOffset = (static_cast<size_t>(sourceChannel) * sample.length + s) * 2U;
                    int32_t value = ReadLE16Signed(&raw[rawOffset]);
                    if (!signedSamples) {
                        value = static_cast<int32_t>(ReadLE16(&raw[rawOffset])) - 32768;
                    }
                    mixedValue += value;
                }
                mixedValue /= static_cast<int32_t>(sourceChannels);
                sample.pcm[s] = static_cast<int16_t>(std::clamp(mixedValue, -32768, 32767));
            }
        }
        else {
            for (uint32_t s = 0; s < sample.length; ++s) {
                int32_t mixedValue = 0;
                for (uint32_t sourceChannel = 0; sourceChannel < sourceChannels; ++sourceChannel) {
                    const size_t rawOffset = static_cast<size_t>(sourceChannel) * sample.length + s;
                    const int32_t value = signedSamples
                        ? static_cast<int8_t>(raw[rawOffset])
                        : static_cast<int32_t>(raw[rawOffset]) - 128;
                    mixedValue += value;
                }
                mixedValue /= static_cast<int32_t>(sourceChannels);
                sample.pcm[s] = static_cast<int16_t>(std::clamp(mixedValue << 8, -32768, 32767));
            }
        }
    }

    return true;
}

bool S3MPlayer::LoadPatterns(std::ifstream& file, const std::vector<uint16_t>& patternParaPointers) {
    patterns.clear();
    patterns.resize(patternParaPointers.size());

    for (size_t i = 0; i < patternParaPointers.size(); ++i) {
        const std::streamoff patternOffset = static_cast<std::streamoff>(patternParaPointers[i]) * 16;
        if (patternOffset <= 0) {
            continue;
        }

        file.seekg(patternOffset, std::ios::beg);
        uint16_t packedLength = 0;
        file.read(reinterpret_cast<char*>(&packedLength), sizeof(packedLength));
        if (file.gcount() != sizeof(packedLength)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"S3MPlayer: Failed to read pattern length " + std::to_wstring(i));
            return false;
        }

        patterns[i].packedLength = packedLength;
        patterns[i].data.resize(packedLength);
        if (packedLength > 0) {
            file.read(reinterpret_cast<char*>(patterns[i].data.data()), patterns[i].data.size());
            if (file.gcount() != static_cast<std::streamsize>(patterns[i].data.size())) {
                debug.logLevelMessage(LogLevel::LOG_ERROR, L"S3MPlayer: Failed to read packed pattern " + std::to_wstring(i));
                return false;
            }
        }
    }

    return true;
}

void S3MPlayer::UnpackPatterns() {
    unpackedPatterns.clear();
    unpackedPatterns.resize(patterns.size());

    for (size_t patternIndex = 0; patternIndex < patterns.size(); ++patternIndex) {
        unpackedPatterns[patternIndex].assign(S3M_ROWS_PER_PATTERN, std::vector<S3MEvent>(32));

        const std::vector<uint8_t>& data = patterns[patternIndex].data;
        size_t offset = 0;
        uint16_t row = 0;

        while (row < S3M_ROWS_PER_PATTERN && offset < data.size()) {
            const uint8_t what = data[offset++];
            if (what == 0) {
                ++row;
                continue;
            }

            const uint8_t channel = what & 0x1F;
            S3MEvent event{};

            if ((what & 0x20) != 0) {
                if (offset + 2 > data.size()) {
                    break;
                }
                event.note = data[offset++];
                event.instrument = data[offset++];
            }

            if ((what & 0x40) != 0) {
                if (offset + 1 > data.size()) {
                    break;
                }
                event.volume = data[offset++];
            }

            if ((what & 0x80) != 0) {
                if (offset + 2 > data.size()) {
                    break;
                }
                event.effect = data[offset++];
                event.effectData = data[offset++];
            }

            if (channel < 32) {
                unpackedPatterns[patternIndex][row][channel] = event;
            }
        }
    }
}

bool S3MPlayer::Play(const std::wstring& filename) {
    if (isPlaying) {
        return false;
    }

    if (!Initialize(filename)) {
        return false;
    }

#if defined(PLATFORM_WINDOWS)
    HRESULT hr = secondaryBuffer->Play(0, 0, DSBPLAY_LOOPING);
    if (FAILED(hr)) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"S3MPlayer: Failed to start DirectSound buffer playback.");
        return false;
    }
#endif

    isPlaying = true;
    isPaused = false;
    isTerminating = false;
    playbackThread = std::thread(&S3MPlayer::PlaybackLoop, this);
    return true;
}

bool S3MPlayer::Play(const std::string& path) {
    return Play(std::wstring(path.begin(), path.end()));
}

void S3MPlayer::Shutdown() {
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
    samples.clear();
    patterns.clear();
    unpackedPatterns.clear();
    voices.clear();
    channelEnabled.clear();
    defaultPanning.clear();
    mixScratch.clear();
    bIsInitialized = false;
    isPaused = false;
    isMuted = false;
    isTerminating = false;
}

void S3MPlayer::Terminate() {
    isTerminating = true;
    Stop();
}

void S3MPlayer::Stop() {
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

void S3MPlayer::Pause() {
    Mute();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    isPaused = true;
}

void S3MPlayer::Resume() {
    isPaused = false;
    isMuted = false;
    SetVolume(targetVolume.load());
}

void S3MPlayer::HardPause() {
    std::lock_guard<std::mutex> lock(playbackMutex);
    isPaused = true;
    isMuted = true;

    for (S3MChannelVoice& voice : voices) {
        voice.active = false;
        voice.volume = 0;
        voice.position = 0.0;
    }

    SilenceBuffer();
    SetVolume(0);
}

void S3MPlayer::HardResume() {
    std::lock_guard<std::mutex> lock(playbackMutex);

    tick = 0;
    currentRow = 0;
    rowBreakPending = false;
    orderJumpPending = false;

    for (size_t channel = 0; channel < voices.size(); ++channel) {
        S3MChannelVoice& voice = voices[channel];
        voice = S3MChannelVoice{};
        voice.panning = DefaultPanningForChannel(channel);
        voice.basePanning = voice.panning;
    }

    SilenceBuffer();
    isPaused = false;
    isMuted = false;
    currentVolume = targetVolume.load();
    moduleGlobalVolume = std::min<uint8_t>(s3mHeader.globalVolume, 64);
}

void S3MPlayer::Mute() {
    // Muting is an output concern.  Preserve tracker channel volumes so Resume()
    // does not leave already-playing notes permanently silent.
    SilenceBuffer();
    isMuted = true;
}

bool S3MPlayer::IsPaused() const {
    return isPaused;
}

bool S3MPlayer::IsPlaying() const {
    return isPlaying && !isPaused && !isTerminating;
}

void S3MPlayer::SetVolume(uint8_t volume) {
    const uint8_t clamped = std::min<uint8_t>(volume, 64);
    currentVolume = clamped;
    targetVolume = clamped;
}

void S3MPlayer::SetGlobalVolume(uint8_t volume) {
    globalVolume = std::min<uint8_t>(volume, 64);
}

void S3MPlayer::SetFadeIn(uint32_t durationMs) {
    fadeDurationMs = durationMs;
    fadeElapsedMs = 0;
    fadeInActive = true;
    fadeOutActive = false;
    currentVolume = 0;
    targetVolume = 64;
}

void S3MPlayer::SetFadeOut(uint32_t durationMs) {
    fadeDurationMs = durationMs;
    fadeElapsedMs = 0;
    fadeInActive = false;
    fadeOutActive = true;
    targetVolume = 0;
}

bool S3MPlayer::CreateAudioDevice() {
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
    waveFormat.nSamplesPerSec = S3M_SAMPLE_RATE;
    waveFormat.wBitsPerSample = 16;
    waveFormat.nBlockAlign = waveFormat.nChannels * waveFormat.wBitsPerSample / 8;
    waveFormat.nAvgBytesPerSec = waveFormat.nSamplesPerSec * waveFormat.nBlockAlign;

    primaryBuffer->SetFormat(&waveFormat);

    DSBUFFERDESC secondaryDesc = {};
    secondaryDesc.dwSize = sizeof(secondaryDesc);
    secondaryDesc.dwBufferBytes = S3M_BUFFER_SIZE;
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

void S3MPlayer::FillAudioBuffer() {
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

    constexpr DWORD safeMargin = 2048;
    const DWORD writePos = writeCursor;
    const DWORD distance = (playCursor + safeMargin - writePos + bufferSize) % bufferSize;
    const DWORD samplesToWrite = distance / (sizeof(int16_t) * 2);

    if (samplesToWrite == 0) {
        return;
    }

    const size_t stereoSampleCount = static_cast<size_t>(samplesToWrite) * 2;
    if (mixScratch.size() < stereoSampleCount) {
        mixScratch.resize(stereoSampleCount);
    }
    MixAudio(mixScratch.data(), samplesToWrite);

    const DWORD bytesToWrite = samplesToWrite * sizeof(int16_t) * 2;
    void* p1 = nullptr;
    void* p2 = nullptr;
    DWORD b1 = 0;
    DWORD b2 = 0;

    hr = secondaryBuffer->Lock(writePos, bytesToWrite, &p1, &b1, &p2, &b2, 0);
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

void S3MPlayer::MixAudio(int16_t* buffer, size_t samplesToMix) {
    std::fill(buffer, buffer + samplesToMix * 2, 0);

    if (isMuted) {
        return;
    }

    const float userGlobalVolFactor = static_cast<float>(globalVolume.load()) / 64.0f;
    const float moduleGlobalVolFactor = static_cast<float>(moduleGlobalVolume.load()) / 64.0f;
    const float fadeVolFactor = static_cast<float>(currentVolume.load()) / 64.0f;

    for (S3MChannelVoice& voice : voices) {
        if (!voice.active || !voice.sample || voice.step <= 0.0) {
            continue;
        }

        const S3MSample* sample = voice.sample;
        if (!sample->isAdLib && sample->pcm.empty()) {
            continue;
        }

        const size_t dataSize = sample->pcm.size();
        double samplePos = voice.position;
        double phase = voice.phase;
        const double step = voice.step;
        const float pan = static_cast<float>(voice.panning) / 255.0f;
        const float channelVolFactor = static_cast<float>(voice.channelVolume) / 64.0f;
        const float finalVol = (static_cast<float>(voice.volume) / 64.0f) * channelVolFactor * moduleGlobalVolFactor * userGlobalVolFactor * fadeVolFactor;
        const float leftVol = finalVol * (1.0f - pan) * 32767.0f;
        const float rightVol = finalVol * pan * 32767.0f;
        int16_t* out = buffer;

        if (sample->isAdLib) {
            const auto& r = sample->adLibRegisters;

            // Operator characteristics (AM/VIB/EGT/KSR/MULT bytes)
            const float modMultiplier = DecodeOplMultiplier(r[0]);
            const float carMultiplier  = DecodeOplMultiplier(r[1]);
            // EGT bit (bit 5): 1=sustain-hold mode, 0=decay straight through to release
            const bool modEGT = (r[0] & 0x20) != 0;
            const bool carEGT  = (r[1] & 0x20) != 0;

            // Total level: modulator TL controls FM modulation depth;
            // carrier TL controls the output amplitude.
            const float modTL = DecodeOplTotalLevel(r[2]);
            const float carTL  = DecodeOplTotalLevel(r[3]);

            // ADSR nibbles — modulator uses r[4] (AR/DR) and r[6] (SL/RR);
            // carrier uses r[5] (AR/DR) and r[7] (SL/RR).
            const uint8_t modAR = r[4] >> 4;
            const uint8_t modDR = r[4] & 0x0F;
            const uint8_t modSL = r[6] >> 4;
            const uint8_t modRR = r[6] & 0x0F;
            const uint8_t carAR = r[5] >> 4;
            const uint8_t carDR = r[5] & 0x0F;
            const uint8_t carSL = r[7] >> 4;
            const uint8_t carRR = r[7] & 0x0F;

            // Waveform selects and FM connection
            const uint8_t modWave = r[8] & 0x07;
            const uint8_t carWave  = r[9] & 0x07;
            const uint8_t fbLevel  = (r[10] >> 1) & 0x07;
            const bool additive    = (r[10] & 0x01) != 0;

            // Per-sample envelope increments (OPL2-accurate timing at 44100 Hz)
            const float modAttRate = OplAttackRate(modAR);
            const float modDecRate = OplDecayRate(modDR);
            const float modRelRate = OplDecayRate(modRR);
            const float modSustain = OplSustainLevel(modSL);
            const float carAttRate = OplAttackRate(carAR);
            const float carDecRate = OplDecayRate(carDR);
            const float carRelRate = OplDecayRate(carRR);
            const float carSustain = OplSustainLevel(carSL);

            // OPL2 self-feedback: fb=7 => max deviation of pi/2 rad = 0.25 cycles.
            // Formula: deviation = (1 << fbLevel) / 512  (in normalised [0,1] cycle units)
            const float feedbackAmt = (fbLevel > 0) ? (static_cast<float>(1 << fbLevel) / 512.0f) : 0.0f;

            for (size_t i = 0; i < samplesToMix; ++i, out += 2) {
                // ---- Modulator envelope ADSR ----
                // Stages: 0=attack, 1=decay, 2=sustain-hold, 3=release, 4=done
                switch (voice.adLibModEnvelopeStage) {
                case 0:
                    voice.adLibModEnvelope += modAttRate;
                    if (voice.adLibModEnvelope >= 1.0f) {
                        voice.adLibModEnvelope = 1.0f;
                        voice.adLibModEnvelopeStage = 1;
                    }
                    break;
                case 1:
                    if (modDecRate > 0.0f) voice.adLibModEnvelope -= modDecRate;
                    if (voice.adLibModEnvelope <= modSustain) {
                        voice.adLibModEnvelope = modSustain;
                        // EGT=0: decay straight to release; EGT=1: hold at sustain
                        voice.adLibModEnvelopeStage = modEGT ? 2 : 3;
                    }
                    break;
                case 3:
                    if (modRelRate > 0.0f) voice.adLibModEnvelope -= modRelRate;
                    if (voice.adLibModEnvelope <= 0.0f) {
                        voice.adLibModEnvelope = 0.0f;
                        voice.adLibModEnvelopeStage = 4;
                    }
                    break;
                default:
                    break;
                }

                // ---- Carrier envelope ADSR ----
                switch (voice.adLibEnvelopeStage) {
                case 0:
                    voice.adLibEnvelope += carAttRate;
                    if (voice.adLibEnvelope >= 1.0f) {
                        voice.adLibEnvelope = 1.0f;
                        voice.adLibEnvelopeStage = 1;
                    }
                    break;
                case 1:
                    if (carDecRate > 0.0f) voice.adLibEnvelope -= carDecRate;
                    if (voice.adLibEnvelope <= carSustain) {
                        voice.adLibEnvelope = carSustain;
                        voice.adLibEnvelopeStage = carEGT ? 2 : 3;
                    }
                    break;
                case 3:
                    if (carRelRate > 0.0f) voice.adLibEnvelope -= carRelRate;
                    if (voice.adLibEnvelope <= 0.0f) {
                        voice.adLibEnvelope = 0.0f;
                        voice.adLibEnvelopeStage = 4;
                        voice.active = false;
                    }
                    break;
                default:
                    break;
                }

                if (!voice.active) break;

                // ---- Modulator synthesis with OPL2 self-feedback ----
                // Feedback: previous modulator output (envelope-weighted) feeds back
                // into the modulator's own phase input (self-modulation).
                const double modPhase = phase * modMultiplier
                    + static_cast<double>(voice.adLibLastMod) * feedbackAmt;
                const float modRaw = OplWaveform(modWave, modPhase);
                // Attenuate by envelope and TL (TL=modulation depth in FM mode)
                const float modOut = modRaw * voice.adLibModEnvelope * modTL;
                // Store envelope-weighted raw value for next sample's feedback
                voice.adLibLastMod = modRaw * voice.adLibModEnvelope;

                // ---- Carrier synthesis ----
                float value;
                if (additive) {
                    // Additive: sum both operators, each with their own envelope
                    const double carPhase = phase * carMultiplier;
                    const float carOut = OplWaveform(carWave, carPhase) * voice.adLibEnvelope * carTL;
                    value = (carOut + modOut) * 0.5f;
                } else {
                    // FM: modulator phase-modulates the carrier.
                    // Scale of 1.0 maps full modulator amplitude to ±1 full cycle of
                    // carrier phase deviation, matching typical OPL2 modulation depth.
                    const double carPhase = phase * carMultiplier
                        + static_cast<double>(modOut) * 1.0;
                    value = OplWaveform(carWave, carPhase) * voice.adLibEnvelope * carTL;
                }

                const int32_t left  = static_cast<int32_t>(value * leftVol);
                const int32_t right = static_cast<int32_t>(value * rightVol);
                out[0] = static_cast<int16_t>(std::clamp<int32_t>(static_cast<int32_t>(out[0]) + left,  -32768, 32767));
                out[1] = static_cast<int16_t>(std::clamp<int32_t>(static_cast<int32_t>(out[1]) + right, -32768, 32767));
                phase += step;
                if (phase >= 1.0) phase -= std::floor(phase);
            }
            voice.phase = phase;
            continue;
        }

        for (size_t i = 0; i < samplesToMix; ++i, out += 2) {
            const size_t idx = static_cast<size_t>(samplePos);
            if (idx >= dataSize) {
                voice.active = false;
                break;
            }

            float value = static_cast<float>(sample->pcm[idx]) / 32768.0f;
            if (idx + 1 < dataSize) {
                const float next = static_cast<float>(sample->pcm[idx + 1]) / 32768.0f;
                value += (next - value) * static_cast<float>(samplePos - static_cast<double>(idx));
            }

            const int32_t left = static_cast<int32_t>(value * leftVol);
            const int32_t right = static_cast<int32_t>(value * rightVol);
            out[0] = static_cast<int16_t>(std::clamp<int32_t>(static_cast<int32_t>(out[0]) + left, -32768, 32767));
            out[1] = static_cast<int16_t>(std::clamp<int32_t>(static_cast<int32_t>(out[1]) + right, -32768, 32767));

            samplePos += step;
            if (sample->IsLooped()) {
                const double loopStart = static_cast<double>(sample->loopStart);
                const double loopLength = static_cast<double>(sample->loopEnd - sample->loopStart);
                if (samplePos >= sample->loopEnd && loopLength > 1.0) {
                    samplePos = loopStart + std::fmod(samplePos - loopStart, loopLength);
                }
            }
            else if (samplePos >= dataSize) {
                voice.active = false;
                break;
            }
        }

        voice.position = samplePos;
    }
}

void S3MPlayer::SilenceBuffer() {
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

void S3MPlayer::TickRow() {
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
    const std::vector<S3MEvent>& row = unpackedPatterns[patternIndex][currentRow];

    rowBreakPending = false;
    orderJumpPending = false;
    patternLoopJumpPending = false;
    rowDurationTicks = std::max<uint16_t>(speed, 1);
    nextRowOverride = currentRow + 1;

    for (size_t channel = 0; channel < row.size(); ++channel) {
        if (channel < channelEnabled.size() && !channelEnabled[channel]) {
            continue;
        }

        const S3MEvent& event = row[channel];
        TriggerEvent(channel, event, false);
    }

    AdvanceRow();
    tick = 0;
}

void S3MPlayer::TriggerEvent(size_t channel, const S3MEvent& event, bool fromDelay) {
    S3MChannelVoice& voice = voices[channel];
    const uint8_t effect = event.effect;
    uint8_t data = event.effectData;

    if (effect == 0x13 && (data >> 4) == 0x0D && !fromDelay) {
        voice.delayedNotePending = true;
        voice.delayTicks = data & 0x0F;
        voice.delayedEvent = event;
        voice.delayedEvent.effect = 0;
        voice.delayedEvent.effectData = 0;
        return;
    }

    if (event.instrument > 0 && event.instrument <= samples.size()) {
        voice.instrument = event.instrument;
    }

    const bool hasPlayableNote = event.note != S3M_EMPTY_NOTE && event.note != S3M_KEY_OFF;
    const bool tonePortamentoNote = (effect == 0x07 || effect == 0x0C) && hasPlayableNote && voice.sample;
    if (event.note == S3M_KEY_OFF) {
        if (voice.sample && voice.sample->isAdLib) {
            // Trigger release on both carrier and modulator envelopes
            voice.adLibEnvelopeStage    = 3;
            voice.adLibModEnvelopeStage = 3;
        }
        else {
            voice.active = false;
        }
    }
    else if (hasPlayableNote && voice.instrument > 0 && voice.instrument <= samples.size()) {
        const S3MSample& sample = samples[voice.instrument - 1];
        if (sample.isAdLib || !sample.pcm.empty()) {
            const double noteStep = NoteToFrequency(event.note, sample.c2Speed) / static_cast<double>(S3M_SAMPLE_RATE);
            voice.note = event.note;
            voice.targetStep = noteStep;

            if (!tonePortamentoNote) {
                voice.sample = &sample;
                voice.position = 0.0;
                voice.phase = 0.0;
                voice.adLibEnvelope = 0.0f;
                voice.adLibEnvelopeStage = 0;
                voice.adLibModEnvelope = 0.0f;
                voice.adLibModEnvelopeStage = 0;
                voice.adLibLastMod = 0.0f;
                voice.volume = sample.volume;
                voice.baseVolume = sample.volume;
                voice.channelVolume = 64;
                voice.basePanning = voice.panning;
                voice.step = noteStep;
                voice.baseStep = voice.step;
                voice.active = true;
            }
        }
    }

    if (event.volume <= 64) {
        voice.volume = event.volume;
        voice.baseVolume = event.volume;
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
        nextRowOverride = std::min<uint8_t>(row, S3M_ROWS_PER_PATTERN - 1);
        ++sequencePosition;
        rowBreakPending = true;
        break;
    }

    case 0x04: // Dxx: volume slide
        if (data != 0) {
            voice.lastVolumeSlide = data;
        }
        break;

    case 0x05: // Exx: portamento down
    case 0x06: // Fxx: portamento up
        if (data != 0) {
            voice.lastPortamento = data;
        }
        break;

    case 0x07: // Gxx: tone portamento
        if (data != 0) {
            voice.lastPortamento = data;
        }
        if (hasPlayableNote && voice.sample) {
            voice.targetStep = NoteToFrequency(event.note, voice.sample->c2Speed) / static_cast<double>(S3M_SAMPLE_RATE);
        }
        break;

    case 0x08: // Hxy: vibrato
    case 0x0B: // Kxy: vibrato + volume slide
    case 0x15: // Uxy: fine vibrato
        if (data != 0) {
            voice.lastVibrato = data;
        }
        break;

    case 0x09: // Ixy: tremor
        voice.tremorOnTicks = (data >> 4) + 1;
        voice.tremorOffTicks = (data & 0x0F) + 1;
        voice.tremorCounter = 0;
        break;

    case 0x0C: // Lxy: tone portamento + volume slide
        if (data != 0) {
            voice.lastVolumeSlide = data;
            voice.lastPortamento = data;
        }
        if (hasPlayableNote && voice.sample) {
            voice.targetStep = NoteToFrequency(event.note, voice.sample->c2Speed) / static_cast<double>(S3M_SAMPLE_RATE);
        }
        break;

    case 0x0D: // Mxx: channel volume
        voice.channelVolume = std::min<uint8_t>(data, 64);
        break;

    case 0x0E: // Nxy: channel volume slide
        if (data != 0) {
            voice.lastChannelVolumeSlide = data;
        }
        break;

    case 0x0F: // Oxx: sample offset
        if (data != 0) {
            voice.lastSampleOffset = data;
        }
        if (voice.sample) {
            const uint32_t offset = static_cast<uint32_t>(voice.lastSampleOffset) << 8;
            voice.position = static_cast<double>(std::min<uint32_t>(offset, voice.sample->length));
        }
        break;

    case 0x10: // Pxy: panning slide
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

    case 0x13: { // Sxy: special commands
        const uint8_t sub = data >> 4;
        const uint8_t value = data & 0x0F;
        if (sub == 0x03) {
            voice.vibratoWaveform = value & 0x03;
        }
        else if (sub == 0x04) {
            voice.tremoloWaveform = value & 0x03;
        }
        else if (sub == 0x05) {
            voice.panbrelloWaveform = value & 0x03;
        }
        else if (sub == 0x08) {
            voice.panning = static_cast<uint8_t>((static_cast<uint16_t>(value) * 255U) / 15U);
            voice.basePanning = voice.panning;
        }
        else if (sub == 0x0B) {
            if (value == 0) {
                patternLoopRow[channel] = currentRow;
                patternLoopRemaining[channel] = -1;
            }
            else {
                if (patternLoopRemaining[channel] < 0) {
                    patternLoopRemaining[channel] = value;
                }

                if (patternLoopRemaining[channel] > 0) {
                    --patternLoopRemaining[channel];
                    nextRowOverride = patternLoopRow[channel];
                    patternLoopJumpPending = true;
                }
                else {
                    patternLoopRemaining[channel] = -1;
                }
            }
        }
        else if (sub == 0x0C && value == 0) {
            voice.active = false;
        }
        else if (sub == 0x0E) {
            rowDurationTicks = static_cast<uint16_t>(std::max<uint16_t>(speed, 1) * static_cast<uint16_t>(value + 1));
        }
        break;
    }

    case 0x14: // Txx: tempo
        // S3M tempo is BPM-like and valid from 32 upward.  Lower values are
        // ignored by Scream Tracker 3; speed changes use Axx instead.
        if (data >= 32) {
            tempo = data;
        }
        break;

    case 0x16: // Vxx: global volume
        moduleGlobalVolume = std::min<uint8_t>(data, 64);
        break;

    case 0x17: // Wxy: global volume slide
        if (data != 0) {
            voice.lastGlobalVolumeSlide = data;
        }
        break;

    case 0x18: // Xxx: set panning, 0..80h
        voice.panning = static_cast<uint8_t>((std::min<uint16_t>(data, 0x80) * 255U) / 0x80U);
        voice.basePanning = voice.panning;
        break;

    case 0x19: // Yxy: panbrello, used by IT-compatible S3M players.
        if (data != 0) {
            voice.lastPanbrello = data;
        }
        break;

    case 0x1A: // Zxx: MIDI macro. Consumed intentionally; no MIDI/plugin target exists here.
        break;

    default:
        break;
    }

    voice.effect = effect;
    voice.effectData = data;
}

void S3MPlayer::ApplyTickEffects() {
    std::lock_guard<std::mutex> lock(playbackMutex);

    for (size_t channel = 0; channel < voices.size(); ++channel) {
        S3MChannelVoice& voice = voices[channel];

        if (voice.delayedNotePending && tick == voice.delayTicks) {
            voice.delayedNotePending = false;
            TriggerEvent(channel, voice.delayedEvent, true);
        }

        if (voice.effect == 0x17) {
            const uint8_t slide = voice.lastGlobalVolumeSlide != 0 ? voice.lastGlobalVolumeSlide : voice.effectData;
            const uint8_t up = slide >> 4;
            const uint8_t down = slide & 0x0F;
            int newGlobalVolume = moduleGlobalVolume.load();
            if (up != 0 && down != 0x0F) {
                newGlobalVolume += up;
            }
            else if (down != 0) {
                newGlobalVolume -= down;
            }
            moduleGlobalVolume = ClampVolume(newGlobalVolume);
        }

        if (!voice.sample || !voice.active) {
            continue;
        }

        switch (voice.effect) {
        case 0x04: // Dxx
            ApplyVolumeSlide(voice);
            break;

        case 0x05: { // Exx: pitch slide DOWN — lower step = lower pitch
            const uint8_t amount = voice.lastPortamento;
            if ((amount & 0xF0) == 0xF0) {
                // Extra-fine: apply once on first tick only
                if (tick == 1) {
                    voice.step /= std::pow(2.0, static_cast<double>(amount & 0x0F) / 768.0);
                }
            }
            else if ((amount & 0xF0) == 0xE0) {
                // Fine: apply once on first tick only, smaller unit
                if (tick == 1) {
                    voice.step /= std::pow(2.0, static_cast<double>(amount & 0x0F) / (768.0 * 4.0));
                }
            }
            else {
                voice.step /= std::pow(2.0, static_cast<double>(amount) / 768.0);
            }
            voice.baseStep = voice.step;
            break;
        }

        case 0x06: { // Fxx: pitch slide UP — higher step = higher pitch
            const uint8_t amount = voice.lastPortamento;
            if ((amount & 0xF0) == 0xF0) {
                // Extra-fine: apply once on first tick only
                if (tick == 1) {
                    voice.step *= std::pow(2.0, static_cast<double>(amount & 0x0F) / 768.0);
                }
            }
            else if ((amount & 0xF0) == 0xE0) {
                // Fine: apply once on first tick only, smaller unit
                if (tick == 1) {
                    voice.step *= std::pow(2.0, static_cast<double>(amount & 0x0F) / (768.0 * 4.0));
                }
            }
            else {
                voice.step *= std::pow(2.0, static_cast<double>(amount) / 768.0);
            }
            voice.baseStep = voice.step;
            break;
        }

        case 0x07: // Gxx
            ApplyPortamento(voice);
            break;

        case 0x08: // Hxy
        case 0x15: // Uxy: fine vibrato, approximated by the same waveform at lower depth in S3M timing.
            ApplyVibrato(voice);
            break;

        case 0x09: { // Ixy
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
            const int baseNote = (voice.note >> 4) * 12 + (voice.note & 0x0F);
            const int transposed = std::clamp(baseNote + static_cast<int>(semitoneOffset), 0, 95);
            const uint8_t note = static_cast<uint8_t>(((transposed / 12) << 4) | (transposed % 12));
            voice.step = NoteToFrequency(note, voice.sample->c2Speed) / static_cast<double>(S3M_SAMPLE_RATE);
            break;
        }

        case 0x0B: // Kxy
            ApplyVibrato(voice);
            ApplyVolumeSlide(voice);
            break;

        case 0x0C: // Lxy
            ApplyPortamento(voice);
            ApplyVolumeSlide(voice);
            break;

        case 0x0E: { // Nxy: channel volume slide
            const uint8_t slide = voice.lastChannelVolumeSlide != 0 ? voice.lastChannelVolumeSlide : voice.effectData;
            const uint8_t up = slide >> 4;
            const uint8_t down = slide & 0x0F;
            int newChannelVolume = voice.channelVolume;
            if (up != 0 && down != 0x0F) {
                newChannelVolume += up;
            }
            else if (down != 0) {
                newChannelVolume -= down;
            }
            voice.channelVolume = ClampVolume(newChannelVolume);
            break;
        }

        case 0x10: { // Pxy: panning slide
            const uint8_t slide = voice.lastPanningSlide != 0 ? voice.lastPanningSlide : voice.effectData;
            const uint8_t right = slide >> 4;
            const uint8_t left = slide & 0x0F;
            int newPan = voice.panning;
            if (right != 0 && left != 0x0F) {
                newPan += right * 4;
            }
            else if (left != 0) {
                newPan -= left * 4;
            }
            voice.panning = static_cast<uint8_t>(std::clamp(newPan, 0, 255));
            voice.basePanning = voice.panning;
            break;
        }

        case 0x11: // Qxy
            ApplyRetrig(voice);
            break;

        case 0x12: // Rxy
            ApplyTremolo(voice);
            break;

        case 0x19: // Yxy
            ApplyPanbrello(voice);
            break;

        case 0x13: { // S commands that occur on non-zero ticks
            const uint8_t sub = voice.effectData >> 4;
            const uint8_t value = voice.effectData & 0x0F;
            if (sub == 0x0C && tick == value) {
                voice.active = false;
            }
            break;
        }

        default:
            break;
        }
    }
}

void S3MPlayer::ApplyVolumeSlide(S3MChannelVoice& voice) {
    const uint8_t data = voice.lastVolumeSlide != 0 ? voice.lastVolumeSlide : voice.effectData;
    const uint8_t up = data >> 4;
    const uint8_t down = data & 0x0F;
    int volume = voice.volume;

    if (up == 0x0F && down != 0) {
        if (tick == 1) {
            volume -= down;
        }
    }
    else if (down == 0x0F && up != 0) {
        if (tick == 1) {
            volume += up;
        }
    }
    else if (up != 0) {
        volume += up;
    }
    else {
        volume -= down;
    }

    voice.volume = ClampVolume(volume);
    voice.baseVolume = voice.volume;
}

void S3MPlayer::ApplyPortamento(S3MChannelVoice& voice) {
    if (voice.targetStep <= 0.0) {
        return;
    }

    const double amount = static_cast<double>(voice.lastPortamento != 0 ? voice.lastPortamento : voice.effectData) / 4096.0;
    if (voice.step < voice.targetStep) {
        voice.step = std::min(voice.step + amount, voice.targetStep);
    }
    else if (voice.step > voice.targetStep) {
        voice.step = std::max(voice.step - amount, voice.targetStep);
    }
    voice.baseStep = voice.step;
}

void S3MPlayer::ApplyVibrato(S3MChannelVoice& voice) {
    const uint8_t data = voice.lastVibrato != 0 ? voice.lastVibrato : voice.effectData;
    const uint8_t speedValue = data >> 4;
    const uint8_t depth = data & 0x0F;
    const double delta = static_cast<double>(TrackerWaveform(voice.vibratoWaveform, voice.vibratoPos)) * static_cast<double>(depth) / 2048.0;
    voice.step = std::max(0.0, voice.baseStep + delta);
    voice.vibratoPos = static_cast<uint8_t>(voice.vibratoPos + speedValue);
}

void S3MPlayer::ApplyTremolo(S3MChannelVoice& voice) {
    const uint8_t data = voice.lastTremolo != 0 ? voice.lastTremolo : voice.effectData;
    const uint8_t speedValue = data >> 4;
    const uint8_t depth = data & 0x0F;
    const int delta = static_cast<int>(TrackerWaveform(voice.tremoloWaveform, voice.tremoloPos) * static_cast<double>(depth));
    voice.volume = ClampVolume(static_cast<int>(voice.baseVolume) + delta);
    voice.tremoloPos = static_cast<uint8_t>(voice.tremoloPos + speedValue);
}


void S3MPlayer::ApplyPanbrello(S3MChannelVoice& voice) {
    const uint8_t data = voice.lastPanbrello != 0 ? voice.lastPanbrello : voice.effectData;
    const uint8_t speedValue = data >> 4;
    const uint8_t depth = data & 0x0F;
    const int delta = static_cast<int>(TrackerWaveform(voice.panbrelloWaveform, voice.panbrelloPos) * static_cast<float>(depth * 4));
    voice.panning = static_cast<uint8_t>(std::clamp(static_cast<int>(voice.basePanning) + delta, 0, 255));
    voice.panbrelloPos = static_cast<uint8_t>(voice.panbrelloPos + speedValue);
}

void S3MPlayer::ApplyRetrig(S3MChannelVoice& voice) {
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

void S3MPlayer::AdvanceRow() {
    if (orderJumpPending) {
        return;
    }

    if (patternLoopJumpPending) {
        currentRow = nextRowOverride;
        return;
    }

    if (rowBreakPending) {
        currentRow = nextRowOverride;
        if (sequencePosition >= orders.size()) {
            sequencePosition = restartSequencePosition.load();
        }
        return;
    }

    ++currentRow;
    if (currentRow >= S3M_ROWS_PER_PATTERN) {
        currentRow = 0;
        ++sequencePosition;
        if (sequencePosition >= orders.size()) {
            sequencePosition = restartSequencePosition.load();
        }
    }
}

double S3MPlayer::NoteToFrequency(uint8_t note, uint32_t c2Speed) const {
    if (note == S3M_EMPTY_NOTE || note == S3M_KEY_OFF) {
        return 0.0;
    }

    const int octave = note >> 4;
    const int semitone = note & 0x0F;
    if (semitone > 11) {
        return 0.0;
    }

    const int noteIndex = octave * 12 + semitone;
    const int c4Index = 4 * 12;
    return static_cast<double>(c2Speed) * std::pow(2.0, static_cast<double>(noteIndex - c4Index) / 12.0);
}

double S3MPlayer::VibratoTable(uint8_t pos) const {
    return std::sin(static_cast<double>(pos & 0x3F) * (3.14159265358979323846 * 2.0 / 64.0));
}

uint8_t S3MPlayer::DefaultPanningForChannel(size_t channel) const {
    if (channel < defaultPanning.size()) {
        return defaultPanning[channel];
    }

    if (channel < 32 && s3mHeader.channelSettings[channel] < 16) {
        return (s3mHeader.channelSettings[channel] < 8) ? 48 : 208;
    }

    return (channel & 1) == 0 ? 48 : 208;
}

void S3MPlayer::GotoSequenceID(uint16_t patternSeqID) {
    if (!isPlaying) {
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"S3MPlayer: GotoSequenceID called while player is not active.");
        return;
    }

    if (patternSeqID >= orders.size()) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"S3MPlayer: Invalid PatternSeqID " + std::to_wstring(patternSeqID));
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
        patternLoopJumpPending = false;
    }

    SetFadeIn(1000);
}

void S3MPlayer::PlaybackLoop() {
    using namespace std::chrono;

#if defined(PLATFORM_WINDOWS)
    // Request 1 ms OS timer resolution so sleep_for(1ms) actually sleeps ~1 ms.
    // Without this, Windows defaults to ~15.6 ms, making ticks fire late and
    // causing audible tempo drift.
    timeBeginPeriod(1);
#endif

    auto tickStart = high_resolution_clock::now();
    auto fadeClock = high_resolution_clock::now();

    while (isPlaying && !isTerminating) {
        if (!isPaused) {
            FillAudioBuffer();

            const auto now = high_resolution_clock::now();
            const long long elapsedTime = duration_cast<microseconds>(now - tickStart).count();
            const long long tickDurationUs = static_cast<long long>((2500.0 / static_cast<double>(tempo)) * 1000.0);

            if (elapsedTime >= tickDurationUs) {
                // Advance by exactly one tick period rather than snapping to 'now',
                // so scheduling jitter does not accumulate into tempo drift.
                tickStart += microseconds(tickDurationUs);

                // If we're more than one period behind (e.g. after a CPU spike or
                // debugger pause), snap forward to avoid a burst of catch-up ticks.
                if (duration_cast<microseconds>(now - tickStart).count() > tickDurationUs) {
                    tickStart = now;
                }

                if (tick == 0) {
                    TickRow();
                }
                else {
                    ApplyTickEffects();
                }

                tick = static_cast<uint16_t>((tick + 1) % std::max<uint16_t>(rowDurationTicks, 1));
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

#if defined(PLATFORM_WINDOWS)
    timeEndPeriod(1);
#endif

    isTerminating = false;
}
