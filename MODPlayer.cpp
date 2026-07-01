#include "Includes.h"
#include "MODPlayer.h"
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
    constexpr uint32_t MOD_SAMPLE_RATE = 44100;
    constexpr uint16_t MOD_ROWS_PER_PATTERN = 64;
    constexpr uint16_t MOD_CHANNELS = 4;
    constexpr std::array<uint16_t, 36> MOD_PERIODS = {
        856, 808, 762, 720, 678, 640, 604, 570, 538, 508, 480, 453,
        428, 404, 381, 360, 339, 320, 302, 285, 269, 254, 240, 226,
        214, 202, 190, 180, 170, 160, 151, 143, 135, 127, 120, 113
    };

    uint16_t ReadBE16(const uint8_t* data) {
        return static_cast<uint16_t>((static_cast<uint16_t>(data[0]) << 8) | data[1]);
    }

    uint8_t ClampVolume(int value) {
        return static_cast<uint8_t>(std::clamp(value, 0, 64));
    }

    bool IsKnownMODSignature(const char signature[4]) {
        return std::memcmp(signature, "M.K.", 4) == 0 ||
            std::memcmp(signature, "M!K!", 4) == 0 ||
            std::memcmp(signature, "M&K!", 4) == 0 ||
            std::memcmp(signature, "N.T.", 4) == 0 ||
            std::memcmp(signature, "FLT4", 4) == 0 ||
            std::memcmp(signature, "4CHN", 4) == 0;
    }
}

bool MODPlayer::Initialize(const std::wstring& filename) {
    if (!bIsInitialized) {
        if (!CreateAudioDevice()) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"MODPlayer: Failed to initialize audio device.");
            return false;
        }

        bIsInitialized = true;
    }

    if (!LoadMODFile(filename)) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"MODPlayer: LoadMODFile failed.");
        return false;
    }

    sequencePosition = 0;
    restartSequencePosition.store(restartPosition < songLength ? restartPosition : 0);
    currentPatternIndex = 0;
    currentRow = 0;
    nextRowOverride = 0;
    tick = 0;
    speed = 6;
    tempo = 125;
    rowDurationTicks = speed;
    patternLoopRow = 0;
    patternLoopCount = 0;
    rowBreakPending = false;
    orderJumpPending = false;
    patternLoopPending = false;

    for (size_t channel = 0; channel < voices.size(); ++channel) {
        voices[channel] = MODChannelVoice{};
        voices[channel].panning = DefaultPanningForChannel(channel);
    }

    const uint8_t configuredVolume = static_cast<uint8_t>(std::clamp(config.myConfig.musicVolume, 0, 64));
    globalVolume = configuredVolume;
    currentVolume = configuredVolume;
    targetVolume = configuredVolume;
    moduleGlobalVolume = 64;
    fadeInActive = false;
    fadeOutActive = false;
    isMuted = false;

    return true;
}

bool MODPlayer::LoadMODFile(const std::wstring& filename) {
    debug.logLevelMessage(LogLevel::LOG_INFO, L"Loading MOD file: " + filename);

    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"MODPlayer: Failed to open MOD file.");
        return false;
    }

    file.read(songName, sizeof(songName));
    if (file.gcount() != sizeof(songName)) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"MODPlayer: Failed to read song name.");
        return false;
    }

    std::array<uint32_t, 31> sampleLengths{};

    for (size_t i = 0; i < samples.size(); ++i) {
        uint8_t header[30] = {};
        file.read(reinterpret_cast<char*>(header), sizeof(header));
        if (file.gcount() != sizeof(header)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"MODPlayer: Failed to read sample header " + std::to_wstring(i));
            return false;
        }

        MODSample& sample = samples[i];
        std::memcpy(sample.name, header, sizeof(sample.name));
        sample.length = static_cast<uint32_t>(ReadBE16(header + 22)) * 2U;
        sample.finetune = header[24] & 0x0F;
        sample.volume = std::min<uint8_t>(header[25], 64);
        sample.loopStart = static_cast<uint32_t>(ReadBE16(header + 26)) * 2U;
        sample.loopLength = static_cast<uint32_t>(ReadBE16(header + 28)) * 2U;

        if (sample.loopStart >= sample.length || sample.loopLength <= 2) {
            sample.loopStart = 0;
            sample.loopLength = 0;
        }
        else if (sample.loopStart + sample.loopLength > sample.length) {
            sample.loopLength = sample.length - sample.loopStart;
        }

        sampleLengths[i] = sample.length;
    }

    file.read(reinterpret_cast<char*>(&songLength), sizeof(songLength));
    file.read(reinterpret_cast<char*>(&restartPosition), sizeof(restartPosition));
    file.read(reinterpret_cast<char*>(orders.data()), orders.size());
    if (!file) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"MODPlayer: Failed to read order table.");
        return false;
    }

    char signature[4] = {};
    file.read(signature, sizeof(signature));
    if (file.gcount() != sizeof(signature) || !IsKnownMODSignature(signature)) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"MODPlayer: Unsupported or invalid ProTracker 4-channel signature.");
        return false;
    }

    if (songLength == 0 || songLength > orders.size()) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"MODPlayer: Invalid song length.");
        return false;
    }

    uint8_t highestPattern = 0;
    for (uint8_t i = 0; i < songLength; ++i) {
        highestPattern = std::max(highestPattern, orders[i]);
    }

    patterns.clear();
    patterns.resize(static_cast<size_t>(highestPattern) + 1U);
    for (MODPattern& pattern : patterns) {
        pattern.rows.resize(MOD_ROWS_PER_PATTERN);
        for (uint16_t row = 0; row < MOD_ROWS_PER_PATTERN; ++row) {
            for (uint16_t channel = 0; channel < MOD_CHANNELS; ++channel) {
                uint8_t eventBytes[4] = {};
                file.read(reinterpret_cast<char*>(eventBytes), sizeof(eventBytes));
                if (file.gcount() != sizeof(eventBytes)) {
                    debug.logLevelMessage(LogLevel::LOG_ERROR, L"MODPlayer: Failed to read pattern data.");
                    return false;
                }

                MODEvent& event = pattern.rows[row][channel];
                event.sample = static_cast<uint8_t>((eventBytes[0] & 0xF0) | (eventBytes[2] >> 4));
                event.period = static_cast<uint16_t>(((eventBytes[0] & 0x0F) << 8) | eventBytes[1]);
                event.effect = eventBytes[2] & 0x0F;
                event.effectData = eventBytes[3];
            }
        }
    }

    for (size_t i = 0; i < samples.size(); ++i) {
        MODSample& sample = samples[i];
        sample.pcm.clear();
        sample.pcm.resize(sampleLengths[i]);

        if (sample.length == 0) {
            continue;
        }

        std::vector<uint8_t> raw(sample.length);
        file.read(reinterpret_cast<char*>(raw.data()), raw.size());
        if (file.gcount() != static_cast<std::streamsize>(raw.size())) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"MODPlayer: Failed to read sample data " + std::to_wstring(i));
            return false;
        }

        // ProTracker MOD samples are signed 8-bit PCM. Expand once during load
        // so the realtime mixer only interpolates int16_t data.
        for (uint32_t s = 0; s < sample.length; ++s) {
            sample.pcm[s] = static_cast<int16_t>(static_cast<int8_t>(raw[s])) << 8;
        }
    }

    debug.logLevelMessage(LogLevel::LOG_INFO, L"MOD file loaded successfully.");
    return true;
}

bool MODPlayer::Play(const std::wstring& filename) {
    if (isPlaying) {
        return false;
    }

    if (!Initialize(filename)) {
        return false;
    }

#if defined(PLATFORM_WINDOWS)
    HRESULT hr = secondaryBuffer->Play(0, 0, DSBPLAY_LOOPING);
    if (FAILED(hr)) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"MODPlayer: Failed to start DirectSound buffer playback.");
        return false;
    }
#endif

    isPlaying = true;
    isPaused = false;
    isTerminating = false;
    playbackThread = std::thread(&MODPlayer::PlaybackLoop, this);
    return true;
}

bool MODPlayer::Play(const std::string& path) {
    return Play(std::wstring(path.begin(), path.end()));
}

void MODPlayer::Shutdown() {
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

    patterns.clear();
    for (MODSample& sample : samples) {
        sample.pcm.clear();
    }
    mixScratch.clear();
    bIsInitialized = false;
    isPaused = false;
    isMuted = false;
    isTerminating = false;
}

void MODPlayer::Terminate() {
    isTerminating = true;
    Stop();
}

void MODPlayer::Stop() {
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

void MODPlayer::Pause() {
    Mute();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    isPaused = true;
}

void MODPlayer::Resume() {
    isPaused = false;
    isMuted = false;
    SetVolume(targetVolume.load());
}

void MODPlayer::HardPause() {
    std::lock_guard<std::mutex> lock(playbackMutex);
    isPaused = true;
    isMuted = true;

    for (MODChannelVoice& voice : voices) {
        voice.active = false;
        voice.volume = 0;
        voice.position = 0.0;
    }

    SilenceBuffer();
    SetVolume(0);
}

void MODPlayer::HardResume() {
    std::lock_guard<std::mutex> lock(playbackMutex);

    tick = 0;
    currentRow = 0;
    rowBreakPending = false;
    orderJumpPending = false;
    patternLoopPending = false;

    for (size_t channel = 0; channel < voices.size(); ++channel) {
        voices[channel] = MODChannelVoice{};
        voices[channel].panning = DefaultPanningForChannel(channel);
    }

    SilenceBuffer();
    isPaused = false;
    isMuted = false;
    currentVolume = targetVolume.load();
    moduleGlobalVolume = 64;
}

void MODPlayer::Mute() {
    SilenceBuffer();
    isMuted = true;
}

bool MODPlayer::IsPaused() const {
    return isPaused;
}

bool MODPlayer::IsPlaying() const {
    return isPlaying && !isPaused && !isTerminating;
}

void MODPlayer::SetVolume(uint8_t volume) {
    const uint8_t clamped = std::min<uint8_t>(volume, 64);
    currentVolume = clamped;
    targetVolume = clamped;
}

void MODPlayer::SetGlobalVolume(uint8_t volume) {
    globalVolume = std::min<uint8_t>(volume, 64);
}

void MODPlayer::SetFadeIn(uint32_t durationMs) {
    fadeDurationMs = durationMs;
    fadeElapsedMs = 0;
    fadeInActive = true;
    fadeOutActive = false;
    currentVolume = 0;
    targetVolume = 64;
}

void MODPlayer::SetFadeOut(uint32_t durationMs) {
    fadeDurationMs = durationMs;
    fadeElapsedMs = 0;
    fadeInActive = false;
    fadeOutActive = true;
    targetVolume = 0;
}

bool MODPlayer::CreateAudioDevice() {
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
    waveFormat.nSamplesPerSec = MOD_SAMPLE_RATE;
    waveFormat.wBitsPerSample = 16;
    waveFormat.nBlockAlign = waveFormat.nChannels * waveFormat.wBitsPerSample / 8;
    waveFormat.nAvgBytesPerSec = waveFormat.nSamplesPerSec * waveFormat.nBlockAlign;
    primaryBuffer->SetFormat(&waveFormat);

    DSBUFFERDESC secondaryDesc = {};
    secondaryDesc.dwSize = sizeof(secondaryDesc);
    secondaryDesc.dwBufferBytes = MOD_BUFFER_SIZE;
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

void MODPlayer::FillAudioBuffer() {
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

void MODPlayer::MixAudio(int16_t* buffer, size_t samplesToMix) {
    std::fill(buffer, buffer + samplesToMix * 2, 0);

    if (isMuted) {
        return;
    }

    const float userGlobalVolFactor = static_cast<float>(globalVolume.load()) / 64.0f;
    const float moduleGlobalVolFactor = static_cast<float>(moduleGlobalVolume.load()) / 64.0f;
    const float fadeVolFactor = static_cast<float>(currentVolume.load()) / 64.0f;

    for (MODChannelVoice& voice : voices) {
        if (!voice.active || !voice.sample || voice.step <= 0.0 || voice.sample->pcm.empty()) {
            continue;
        }

        const MODSample* sample = voice.sample;
        const size_t dataSize = sample->pcm.size();
        double samplePos = voice.position;
        const double step = voice.step;
        const float pan = static_cast<float>(voice.panning) / 255.0f;
        const float finalVol = (static_cast<float>(voice.volume) / 64.0f) * moduleGlobalVolFactor * userGlobalVolFactor * fadeVolFactor;
        const float leftVol = finalVol * (1.0f - pan);
        const float rightVol = finalVol * pan;
        int16_t* out = buffer;

        for (size_t i = 0; i < samplesToMix; ++i, out += 2) {
            const size_t idx = static_cast<size_t>(samplePos);
            if (idx >= dataSize) {
                voice.active = false;
                break;
            }

            float value = static_cast<float>(sample->pcm[idx]);
            if (idx + 1 < dataSize) {
                const float next = static_cast<float>(sample->pcm[idx + 1]);
                value += (next - value) * static_cast<float>(samplePos - static_cast<double>(idx));
            }

            const int32_t left = static_cast<int32_t>(value * leftVol);
            const int32_t right = static_cast<int32_t>(value * rightVol);
            out[0] = static_cast<int16_t>(std::clamp<int32_t>(static_cast<int32_t>(out[0]) + left, -32768, 32767));
            out[1] = static_cast<int16_t>(std::clamp<int32_t>(static_cast<int32_t>(out[1]) + right, -32768, 32767));

            samplePos += step;
            if (sample->IsLooped()) {
                const double loopStart = static_cast<double>(sample->loopStart);
                const double loopLength = static_cast<double>(sample->loopLength);
                const double loopEnd = loopStart + loopLength;
                if (samplePos >= loopEnd && loopLength > 1.0) {
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

void MODPlayer::SilenceBuffer() {
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

void MODPlayer::TickRow() {
    std::lock_guard<std::mutex> lock(playbackMutex);

    if (sequencePosition >= songLength) {
        sequencePosition = restartSequencePosition.load();
    }
    if (sequencePosition >= songLength) {
        sequencePosition = 0;
    }

    const uint8_t patternIndex = orders[sequencePosition];
    if (patternIndex >= patterns.size()) {
        AdvanceRow();
        tick = 0;
        return;
    }

    currentPatternIndex = patternIndex;
    const std::array<MODEvent, 4>& row = patterns[patternIndex].rows[currentRow];

    rowBreakPending = false;
    orderJumpPending = false;
    patternLoopPending = false;
    rowDurationTicks = std::max<uint16_t>(speed, 1);
    nextRowOverride = currentRow + 1;

    for (size_t channel = 0; channel < row.size(); ++channel) {
        TriggerEvent(channel, row[channel], false);
    }

    AdvanceRow();
    tick = 0;
}

void MODPlayer::TriggerEvent(size_t channel, const MODEvent& event, bool fromDelay) {
    MODChannelVoice& voice = voices[channel];

    if (event.effect == 0x0E && (event.effectData >> 4) == 0x0D && !fromDelay) {
        voice.delayedNotePending = true;
        voice.delayTicks = event.effectData & 0x0F;
        voice.delayedEvent = event;
        voice.delayedEvent.effect = 0;
        voice.delayedEvent.effectData = 0;
        return;
    }

    if (event.sample > 0 && event.sample <= samples.size()) {
        voice.sampleIndex = event.sample;
        const MODSample& sample = samples[event.sample - 1];
        voice.sample = &sample;
        voice.volume = sample.volume;
        voice.baseVolume = sample.volume;
    }

    const bool hasPeriod = event.period > 0;
    const bool tonePortamentoNote = event.effect == 0x03 || event.effect == 0x05;

    if (hasPeriod && voice.sample) {
        voice.targetPeriod = ClampPeriod(event.period);
        voice.targetStep = PeriodToStep(voice.targetPeriod, *voice.sample);

        if (!tonePortamentoNote) {
            voice.period = voice.targetPeriod;
            voice.step = voice.targetStep;
            voice.baseStep = voice.step;
            voice.position = 0.0;
            voice.active = !voice.sample->pcm.empty();
        }
    }

    switch (event.effect) {
    case 0x01: // 1xx: portamento up
    case 0x02: // 2xx: portamento down
        if (event.effectData != 0) {
            voice.lastPortamento = event.effectData;
        }
        break;

    case 0x03: // 3xx: tone portamento
        if (event.effectData != 0) {
            voice.lastPortamento = event.effectData;
        }
        break;

    case 0x04: // 4xy: vibrato
    case 0x06: // 6xy: vibrato + volume slide
        if (event.effectData != 0) {
            voice.lastVibrato = event.effectData;
        }
        break;

    case 0x05: // 5xy: tone portamento + volume slide
    case 0x0A: // Axy: volume slide
        if (event.effectData != 0) {
            voice.lastVolumeSlide = event.effectData;
        }
        break;

    case 0x07: // 7xy: tremolo
        if (event.effectData != 0) {
            voice.lastTremolo = event.effectData;
        }
        break;

    case 0x08: // 8xx: panning, common ProTracker extension
        voice.panning = static_cast<uint8_t>((std::min<uint16_t>(event.effectData, 0x80) * 255U) / 0x80U);
        break;

    case 0x09: // 9xx: sample offset
        if (event.effectData != 0) {
            voice.lastSampleOffset = event.effectData;
        }
        if (voice.sample) {
            const uint32_t offset = static_cast<uint32_t>(voice.lastSampleOffset) << 8;
            voice.position = static_cast<double>(std::min<uint32_t>(offset, voice.sample->length));
        }
        break;

    case 0x0B: // Bxx: position jump
        sequencePosition = event.effectData < songLength ? event.effectData : 0;
        currentRow = 0;
        orderJumpPending = true;
        break;

    case 0x0C: // Cxx: set volume
        voice.volume = ClampVolume(event.effectData);
        voice.baseVolume = voice.volume;
        break;

    case 0x0D: { // Dxx: pattern break, BCD row
        const uint8_t row = static_cast<uint8_t>(((event.effectData >> 4) * 10) + (event.effectData & 0x0F));
        nextRowOverride = std::min<uint8_t>(row, MOD_ROWS_PER_PATTERN - 1);
        ++sequencePosition;
        rowBreakPending = true;
        break;
    }

    case 0x0E: {
        const uint8_t sub = event.effectData >> 4;
        const uint8_t value = event.effectData & 0x0F;
        switch (sub) {
        case 0x01: // E1x: fine portamento up
            if (voice.period > 0) {
                voice.period = ClampPeriod(static_cast<int>(voice.period) - value);
                if (voice.sample) {
                    voice.step = PeriodToStep(voice.period, *voice.sample);
                    voice.baseStep = voice.step;
                }
            }
            break;

        case 0x02: // E2x: fine portamento down
            if (voice.period > 0) {
                voice.period = ClampPeriod(static_cast<int>(voice.period) + value);
                if (voice.sample) {
                    voice.step = PeriodToStep(voice.period, *voice.sample);
                    voice.baseStep = voice.step;
                }
            }
            break;

        case 0x04: // E4x: vibrato waveform
            voice.vibratoWaveform = value & 0x03;
            break;

        case 0x05: // E5x: set finetune for current sample
            if (voice.sample && voice.sampleIndex > 0) {
                MODSample* mutableSample = &samples[voice.sampleIndex - 1];
                mutableSample->finetune = value & 0x0F;
                voice.sample = mutableSample;
                if (voice.period > 0) {
                    voice.step = PeriodToStep(voice.period, *voice.sample);
                    voice.baseStep = voice.step;
                }
            }
            break;

        case 0x06: // E6x: pattern loop
            if (value == 0) {
                patternLoopRow = static_cast<uint8_t>(currentRow);
                patternLoopCount = 0;
            }
            else {
                if (patternLoopCount == 0) {
                    patternLoopCount = value;
                }

                if (patternLoopCount > 0) {
                    --patternLoopCount;
                    nextRowOverride = patternLoopRow;
                    patternLoopPending = true;
                }
            }
            break;

        case 0x07: // E7x: tremolo waveform
            voice.tremoloWaveform = value & 0x03;
            break;

        case 0x08: // E8x: panning, nibble extension
            voice.panning = static_cast<uint8_t>((static_cast<uint16_t>(value) * 255U) / 15U);
            break;

        case 0x09: // E9x: retrigger note
            ApplyRetrig(voice, value);
            break;

        case 0x0A: // EAx: fine volume slide up
            voice.volume = ClampVolume(static_cast<int>(voice.volume) + value);
            voice.baseVolume = voice.volume;
            break;

        case 0x0B: // EBx: fine volume slide down
            voice.volume = ClampVolume(static_cast<int>(voice.volume) - value);
            voice.baseVolume = voice.volume;
            break;

        case 0x0C: // ECx: note cut
            if (value == 0) {
                voice.active = false;
            }
            break;

        case 0x0E: // EEx: pattern delay
            rowDurationTicks = static_cast<uint16_t>(std::max<uint16_t>(speed, 1) * static_cast<uint16_t>(value + 1));
            break;

        default:
            break;
        }
        break;
    }

    case 0x0F: // Fxx: speed / tempo
        if (event.effectData > 0) {
            if (event.effectData <= 31) {
                speed = event.effectData;
                rowDurationTicks = std::max<uint16_t>(speed, 1);
            }
            else {
                tempo = event.effectData;
            }
        }
        break;

    default:
        break;
    }

    voice.effect = event.effect;
    voice.effectData = event.effectData;
}

void MODPlayer::ApplyTickEffects() {
    std::lock_guard<std::mutex> lock(playbackMutex);

    for (size_t channel = 0; channel < voices.size(); ++channel) {
        MODChannelVoice& voice = voices[channel];

        if (voice.delayedNotePending && tick == voice.delayTicks) {
            voice.delayedNotePending = false;
            TriggerEvent(channel, voice.delayedEvent, true);
        }

        if (!voice.sample || !voice.active) {
            continue;
        }

        switch (voice.effect) {
        case 0x00: { // 0xy: arpeggio
            if (voice.effectData == 0 || voice.period == 0) {
                break;
            }
            const uint8_t offsetA = voice.effectData >> 4;
            const uint8_t offsetB = voice.effectData & 0x0F;
            const uint8_t semitoneOffset = (tick % 3 == 1) ? offsetA : ((tick % 3 == 2) ? offsetB : 0);
            double step = PeriodToStep(voice.period, *voice.sample) * std::pow(2.0, static_cast<double>(semitoneOffset) / 12.0);
            voice.step = step;
            break;
        }

        case 0x01: // 1xx: portamento up
            if (voice.period > 0) {
                voice.period = ClampPeriod(static_cast<int>(voice.period) - (voice.lastPortamento != 0 ? voice.lastPortamento : voice.effectData));
                voice.step = PeriodToStep(voice.period, *voice.sample);
                voice.baseStep = voice.step;
            }
            break;

        case 0x02: // 2xx: portamento down
            if (voice.period > 0) {
                voice.period = ClampPeriod(static_cast<int>(voice.period) + (voice.lastPortamento != 0 ? voice.lastPortamento : voice.effectData));
                voice.step = PeriodToStep(voice.period, *voice.sample);
                voice.baseStep = voice.step;
            }
            break;

        case 0x03:
            ApplyPortamento(voice);
            break;

        case 0x04:
            ApplyVibrato(voice);
            break;

        case 0x05:
            ApplyPortamento(voice);
            ApplyVolumeSlide(voice);
            break;

        case 0x06:
            ApplyVibrato(voice);
            ApplyVolumeSlide(voice);
            break;

        case 0x07:
            ApplyTremolo(voice);
            break;

        case 0x0A:
            ApplyVolumeSlide(voice);
            break;

        case 0x0E: {
            const uint8_t sub = voice.effectData >> 4;
            const uint8_t value = voice.effectData & 0x0F;
            if (sub == 0x09) {
                ApplyRetrig(voice, value);
            }
            else if (sub == 0x0C && tick == value) {
                voice.active = false;
            }
            break;
        }

        default:
            break;
        }
    }
}

void MODPlayer::ApplyVolumeSlide(MODChannelVoice& voice) {
    const uint8_t data = voice.lastVolumeSlide != 0 ? voice.lastVolumeSlide : voice.effectData;
    const uint8_t up = data >> 4;
    const uint8_t down = data & 0x0F;
    int volume = voice.volume;

    if (up != 0 && down == 0) {
        volume += up;
    }
    else if (down != 0 && up == 0) {
        volume -= down;
    }

    voice.volume = ClampVolume(volume);
    voice.baseVolume = voice.volume;
}

void MODPlayer::ApplyPortamento(MODChannelVoice& voice) {
    if (voice.targetPeriod == 0 || voice.period == 0 || !voice.sample) {
        return;
    }

    const uint16_t amount = voice.lastPortamento != 0 ? voice.lastPortamento : voice.effectData;
    if (voice.period > voice.targetPeriod) {
        voice.period = static_cast<uint16_t>(std::max<int>(voice.targetPeriod, static_cast<int>(voice.period) - amount));
    }
    else if (voice.period < voice.targetPeriod) {
        voice.period = static_cast<uint16_t>(std::min<int>(voice.targetPeriod, static_cast<int>(voice.period) + amount));
    }

    voice.step = PeriodToStep(voice.period, *voice.sample);
    voice.baseStep = voice.step;
}

void MODPlayer::ApplyVibrato(MODChannelVoice& voice) {
    if (voice.period == 0 || !voice.sample) {
        return;
    }

    const uint8_t data = voice.lastVibrato != 0 ? voice.lastVibrato : voice.effectData;
    const uint8_t speedValue = data >> 4;
    const uint8_t depth = data & 0x0F;
    const int periodDelta = static_cast<int>(TrackerWaveform(voice.vibratoWaveform, voice.vibratoPos) * static_cast<double>(depth * 4));
    voice.step = PeriodToStep(ClampPeriod(static_cast<int>(voice.period) + periodDelta), *voice.sample);
    voice.vibratoPos = static_cast<uint8_t>(voice.vibratoPos + speedValue);
}

void MODPlayer::ApplyTremolo(MODChannelVoice& voice) {
    const uint8_t data = voice.lastTremolo != 0 ? voice.lastTremolo : voice.effectData;
    const uint8_t speedValue = data >> 4;
    const uint8_t depth = data & 0x0F;
    const int delta = static_cast<int>(TrackerWaveform(voice.tremoloWaveform, voice.tremoloPos) * static_cast<double>(depth));
    voice.volume = ClampVolume(static_cast<int>(voice.baseVolume) + delta);
    voice.tremoloPos = static_cast<uint8_t>(voice.tremoloPos + speedValue);
}

void MODPlayer::ApplyRetrig(MODChannelVoice& voice, uint8_t interval) {
    if (interval == 0 || tick == 0 || (tick % interval) != 0) {
        return;
    }

    voice.position = 0.0;
}

void MODPlayer::AdvanceRow() {
    if (orderJumpPending) {
        return;
    }

    if (patternLoopPending) {
        currentRow = nextRowOverride;
        return;
    }

    if (rowBreakPending) {
        currentRow = nextRowOverride;
        if (sequencePosition >= songLength) {
            sequencePosition = restartSequencePosition.load();
        }
        return;
    }


    ++currentRow;
    if (currentRow >= MOD_ROWS_PER_PATTERN) {
        currentRow = 0;
        ++sequencePosition;
        if (sequencePosition >= songLength) {
            sequencePosition = restartSequencePosition.load();
        }
    }
}

double MODPlayer::PeriodToFrequency(uint16_t period, uint8_t finetune) const {
    if (period == 0) {
        return 0.0;
    }

    const int signedFinetune = finetune < 8 ? finetune : static_cast<int>(finetune) - 16;
    const double finetuneFactor = std::pow(2.0, static_cast<double>(signedFinetune) / (8.0 * 12.0));
    return (7093789.2 / (static_cast<double>(period) * 2.0)) * finetuneFactor;
}

double MODPlayer::PeriodToStep(uint16_t period, const MODSample& sample) const {
    return PeriodToFrequency(period, sample.finetune) / static_cast<double>(MOD_SAMPLE_RATE);
}

uint16_t MODPlayer::ClampPeriod(int period) const {
    return static_cast<uint16_t>(std::clamp(period, static_cast<int>(MOD_PERIODS.back()), static_cast<int>(MOD_PERIODS.front())));
}

double MODPlayer::TrackerWaveform(uint8_t waveform, uint8_t position) const {
    const uint8_t pos = position & 0x3F;
    switch (waveform & 0x03) {
    case 0x01:
        return pos < 32 ? 1.0 : -1.0;
    case 0x02:
        return 1.0 - (static_cast<double>(pos) / 32.0);
    case 0x03:
        return (pos & 1) ? -1.0 : 1.0;
    default:
        return std::sin(static_cast<double>(pos) * (3.14159265358979323846 * 2.0 / 64.0));
    }
}

uint8_t MODPlayer::DefaultPanningForChannel(size_t channel) const {
    // ProTracker's original Amiga channel layout is L/R/R/L.
    return (channel == 0 || channel == 3) ? 48 : 208;
}

void MODPlayer::GotoSequenceID(uint16_t patternSeqID) {
    if (!isPlaying) {
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"MODPlayer: GotoSequenceID called while player is not active.");
        return;
    }

    if (patternSeqID >= songLength) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"MODPlayer: Invalid PatternSeqID " + std::to_wstring(patternSeqID));
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
        patternLoopPending = false;
        rowDurationTicks = std::max<uint16_t>(speed, 1);
    }

    SetFadeIn(1000);
}

void MODPlayer::PlaybackLoop() {
    using namespace std::chrono;

#if defined(PLATFORM_WINDOWS)
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
                tickStart += microseconds(tickDurationUs);
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
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

#if defined(PLATFORM_WINDOWS)
    timeEndPeriod(1);
#endif
}
