#include "Includes.h"
#include "XMMODPlayer.h"
#include "Debug.h"
#include "Configuration.h"
#include "ThreadManager.h"

extern HWND hwnd;
extern Debug debug;
extern Configuration config;

// dsound.h is already included via XMMODPlayer.h on PLATFORM_WINDOWS;
// keep the pragma comments here for the implementation TU (no-op if already linked).
#if defined(PLATFORM_WINDOWS)
    #pragma comment(lib, "dsound.lib")
    #pragma comment(lib, "dxguid.lib")
    #pragma comment(lib, "winmm.lib")
#endif

void XMMODPlayer::Shutdown() {
    #if defined(_DEBUG_XMPlayer_)    
        debug.DebugLog("Shutdown(): Cleaning up player state.\n");
    #endif
    // Stop playback thread
    isPlaying = false;
    if (playbackThread.joinable()) {
        playbackThread.join();
    }

    // Release DirectSound buffers
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
        primaryBuffer   = nullptr;
        directSound     = nullptr;
    #endif

    // Clear voices and patterns
    voices.clear();
    patterns.clear();
    instruments.clear();
    unpackedPatterns.clear();
    bIsInitialized = false;
    isTerminating = false;

    #if defined(_DEBUG_XMPlayer_)    
        debug.DebugLog("Shutdown(): Resources freed and state reset.\n");
    #endif
}

bool XMMODPlayer::Initialize(const std::wstring& filename) {
    #if defined(_DEBUG_XMPlayer_)    
        debug.DebugLog("XMMODPlayer initialization started...\n");
    #endif
    if (!bIsInitialized) {
        if (!CreateAudioDevice()) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to Initialize Audio Device");
            return false;
        }
        bIsInitialized = true;
    }

    if (!LoadXMFile(filename)) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"LoadXMFile Failed!");
        return false;
    }

    sequencePosition = 0;
    restartSequencePosition.store(0);
    currentPatternIndex = 0;
    currentRow = 0;
    tick = 0;

    tempo = xmHeader.defaultTempo > 0 ? xmHeader.defaultTempo : 6;
    bpm = xmHeader.defaultBPM > 0 ? xmHeader.defaultBPM : 125;

    voices.clear();
    voices.resize(xmHeader.numChannels);

    globalVolume = config.myConfig.musicVolume; // <-- explicitly set here
    currentVolume = config.myConfig.musicVolume; // ensure fade volume starts correctly
    targetVolume = config.myConfig.musicVolume;

    lastTickTime = std::chrono::steady_clock::now();

    debug.DebugLog("XMMODPlayer initialization successful.\n");
    return true;
}

float XMMODPlayer::VibratoTable(uint8_t pos) {
    return std::sin((pos % 64) * (3.14159f * 2.0f / 64.0f)); // Sine wave 0�63
}

bool XMMODPlayer::LoadXMFile(const std::wstring& filename) {
    debug.logLevelMessage(LogLevel::LOG_INFO, L"Loading XM file: " + std::wstring(filename.begin(), filename.end()));

    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to open XM file.");
        return false;
    }

#pragma pack(push, 1)
    struct XMHeaderStub {
        char idText[17];
        char moduleName[20];
        uint8_t signature;
        char trackerName[20];
        uint16_t version;
        uint32_t headerSize;
    };
#pragma pack(pop)

    XMHeaderStub stub{};
    file.read(reinterpret_cast<char*>(&stub), sizeof(stub));
    if (file.gcount() != sizeof(stub)) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to read base XM header.");
        return false;
    }

    if (strncmp(stub.idText, "Extended Module:", 15) != 0) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Invalid XM signature. Not an XM file.");
        return false;
    }
    if (stub.signature != 0x1A) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Invalid signature byte. Expected 0x1A");
        return false;
    }

    memcpy(xmHeader.idText, stub.idText, 17);
    memcpy(xmHeader.moduleName, stub.moduleName, 20);
    xmHeader.signature = stub.signature;
    memcpy(xmHeader.trackerName, stub.trackerName, 20);
    xmHeader.version = stub.version;
    xmHeader.headerSize = stub.headerSize;

    // FIXED: Read headerSize - 4 bytes (not full headerSize)
    const size_t remaining = stub.headerSize - 4;
    file.read(reinterpret_cast<char*>(&xmHeader.songLength), remaining);
    if (file.gcount() != static_cast<std::streamsize>(remaining)) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to read extended XM header.");
        return false;
    }

    #if defined(_DEBUG_XMPlayer_)    
        debug.logLevelMessage(LogLevel::LOG_DEBUG, L"Module: " + std::wstring(xmHeader.moduleName, xmHeader.moduleName + 20));
        debug.logLevelMessage(LogLevel::LOG_DEBUG, L"Tracker: " + std::wstring(xmHeader.trackerName, xmHeader.trackerName + 20));
        debug.logLevelMessage(LogLevel::LOG_DEBUG, L"Version: " + std::to_wstring(xmHeader.version));
        debug.logLevelMessage(LogLevel::LOG_DEBUG, L"Header Size: " + std::to_wstring(xmHeader.headerSize));
        debug.logLevelMessage(LogLevel::LOG_DEBUG, L"Song Length: " + std::to_wstring(xmHeader.songLength));
        debug.logLevelMessage(LogLevel::LOG_DEBUG, L"Channels: " + std::to_wstring(xmHeader.numChannels));
        debug.logLevelMessage(LogLevel::LOG_DEBUG, L"Patterns: " + std::to_wstring(xmHeader.numPatterns));
        debug.logLevelMessage(LogLevel::LOG_DEBUG, L"Instruments: " + std::to_wstring(xmHeader.numInstruments));
        debug.logLevelMessage(LogLevel::LOG_DEBUG, L"Tempo: " + std::to_wstring(xmHeader.defaultTempo));
        debug.logLevelMessage(LogLevel::LOG_DEBUG, L"BPM: " + std::to_wstring(xmHeader.defaultBPM));
    #endif
    bpm = xmHeader.defaultBPM;
    tempo = xmHeader.defaultTempo;
    currentPatternIndex = 0;
    currentRow = 0;
    tick = 0;
    lastTickTime = std::chrono::steady_clock::now();

    if (!LoadPatterns(file)) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to load patterns.");
        return false;
    }

    UnpackPatterns();

    if (!LoadInstruments(file)) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to load instruments.");
        return false;
    }

    UnpackSamples();

    debug.logLevelMessage(LogLevel::LOG_INFO, L"XM file loaded successfully.");
    return true;
}

bool XMMODPlayer::LoadPatterns(std::ifstream& file) {
    debug.logLevelMessage(LogLevel::LOG_INFO, L"Loading patterns...");

    patterns.resize(xmHeader.numPatterns);

    for (uint16_t i = 0; i < xmHeader.numPatterns; ++i) {
        XMPattern pattern{};

        // Step 1: Read header size
        uint32_t headerSize = 0;
        file.read(reinterpret_cast<char*>(&headerSize), sizeof(headerSize));
        if (file.gcount() != sizeof(headerSize)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to read pattern header size.");
            return false;
        }

        if (headerSize < 9 || headerSize > 1024) {
            debug.logLevelMessage(LogLevel::LOG_ERROR,
                L"Pattern header size too small or invalid: " + std::to_wstring(headerSize));
            return false;
        }

        // Step 2: Read full pattern header (headerSize - 4 already read)
        std::vector<uint8_t> headerBytes(headerSize - 4);
        file.read(reinterpret_cast<char*>(headerBytes.data()), headerBytes.size());
        if (file.gcount() != static_cast<std::streamsize>(headerBytes.size())) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to read pattern header body.");
            return false;
        }

        // Step 3: Parse packingType, numRows, dataSize
        if (headerBytes.size() < 5) {
            debug.logLevelMessage(LogLevel::LOG_ERROR,
                L"Pattern header too short to contain required fields.");
            return false;
        }

        uint8_t packingType = headerBytes[0];
        uint16_t numRows = *reinterpret_cast<uint16_t*>(&headerBytes[1]);
        uint16_t dataSize = *reinterpret_cast<uint16_t*>(&headerBytes[3]);

        pattern.header.headerSize = headerSize;
        pattern.header.packingType = packingType;
        pattern.header.numRows = numRows;
        pattern.header.dataSize = dataSize;

        #if defined(_DEBUG_XMPlayer_)    
            debug.logLevelMessage(LogLevel::LOG_INFO,
                L"  Pattern " + std::to_wstring(i) +
                L": Rows=" + std::to_wstring(numRows) +
                L", DataSize=" + std::to_wstring(dataSize));
        #endif

        if (numRows == 0) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"Invalid row count: 0");
            return false;
        }

        // Step 4: Read pattern data
        if (dataSize > 0) {
            pattern.data.resize(dataSize);
            file.read(reinterpret_cast<char*>(pattern.data.data()), dataSize);
            if (file.gcount() != static_cast<std::streamsize>(dataSize)) {
                debug.logLevelMessage(LogLevel::LOG_ERROR, L"Failed to read pattern data for pattern " + std::to_wstring(i));
                return false;
            }
        }

        patterns[i] = std::move(pattern);
    }

    debug.logLevelMessage(LogLevel::LOG_INFO, L"Patterns loaded successfully.");
    return true;
}

bool XMMODPlayer::LoadInstruments(std::ifstream& file) {
    instruments.clear();
    instruments.resize(xmHeader.numInstruments);

    for (uint16_t i = 0; i < xmHeader.numInstruments; ++i) {
        XMInstrument& instrument = instruments[i];

        // Log offset at the start of the instrument
        #if defined(_DEBUG_XMPlayer_)    
            debug.logLevelMessage(LogLevel::LOG_INFO,
                L"Offset BEFORE instrument[" + std::to_wstring(i) + L"] = " + std::to_wstring(file.tellg()));
        #endif
        // Step 1: Read instrument header size (first 4 bytes)
        uint32_t headerSize = 0;
        file.read(reinterpret_cast<char*>(&headerSize), sizeof(uint32_t));
        if (!file || headerSize < 29 || headerSize > 1024) {
            debug.logLevelMessage(LogLevel::LOG_ERROR,
                L"LoadInstruments: Invalid instrument header size at index " + std::to_wstring(i) +
                L" (reported=" + std::to_wstring(headerSize) + L")");
            return false;
        }

        // Step 2: Read the rest of the instrument header block
        std::vector<uint8_t> headerData(headerSize);
        memcpy(&headerData[0], &headerSize, sizeof(uint32_t));
        file.read(reinterpret_cast<char*>(&headerData[4]), headerSize - 4);
        if (!file) {
            debug.logLevelMessage(LogLevel::LOG_ERROR,
                L"LoadInstruments: Failed to read instrument header block at index " + std::to_wstring(i));
            return false;
        }

        // Trace position after instrument header read
        #if defined(_DEBUG_XMPlayer_)    
            debug.logLevelMessage(LogLevel::LOG_INFO,
                L"Offset AFTER header instrument[" + std::to_wstring(i) + L"] = " + std::to_wstring(file.tellg()));
        #endif
        // Step 3: Copy parsed header into our struct
        memset(&instrument.header, 0, sizeof(XMInstrumentHeader));
        size_t copySize = std::min<size_t>(sizeof(XMInstrumentHeader), headerSize);
        memcpy(&instrument.header, headerData.data(), copySize);

        // Step 4: Log instrument name
        std::wstring name;
        for (int k = 0; k < 22; ++k) {
            char c = instrument.header.instrumentName[k];
            if (c >= 32 && c <= 126)
                name += static_cast<wchar_t>(c);
        }

        #if defined(_DEBUG_XMPlayer_)    
            debug.logLevelMessage(LogLevel::LOG_INFO,
                L"LoadInstruments: Loaded instrument " + std::to_wstring(i) + L" (" + name + L")");
        #endif
        // Step 5: Handle samples if instrument contains any
        if (instrument.header.numSamples > 0) {
            instrument.samples.resize(instrument.header.numSamples);

            uint32_t sampleHeaderSize = instrument.header.sampleHeaderSize;

            #if defined(_DEBUG_XMPlayer_)    
                debug.logLevelMessage(LogLevel::LOG_INFO,
                    L"SampleHeaderSize for instrument[" + std::to_wstring(i) + L"] = " + std::to_wstring(sampleHeaderSize));
                debug.logLevelMessage(LogLevel::LOG_INFO,
                    L"Offset AFTER sampleHeaderSize instrument[" + std::to_wstring(i) + L"] = " + std::to_wstring(file.tellg()));
            #endif
            // Read sample headers
            for (uint16_t s = 0; s < instrument.header.numSamples; ++s) {
                XMSampleHeader sampleHeader = {};
                file.read(reinterpret_cast<char*>(&sampleHeader), sizeof(XMSampleHeader));
                if (!file) {
                    debug.logLevelMessage(LogLevel::LOG_ERROR,
                        L"LoadInstruments: Failed to read sample header " + std::to_wstring(s) +
                        L" for instrument " + std::to_wstring(i));
                    return false;
                }

                XMSample& sample = instrument.samples[s];
                sample.length = sampleHeader.length;
                sample.loopStart = sampleHeader.loopStart;
                sample.loopLength = sampleHeader.loopLength;
                sample.volume = sampleHeader.volume;
                sample.finetune = static_cast<int8_t>(sampleHeader.fineTune);
                sample.type = sampleHeader.type;
                sample.panning = sampleHeader.panning;
                sample.relativeNote = static_cast<int8_t>(sampleHeader.relativeNoteNumber);
                memcpy(sample.name, sampleHeader.sampleName, 22);
            }

            #if defined(_DEBUG_XMPlayer_)    
                debug.logLevelMessage(LogLevel::LOG_INFO,
                    L"Offset AFTER sample HEADERS instrument[" + std::to_wstring(i) + L"] = " + std::to_wstring(file.tellg()));
            #endif
            // Read sample data
            for (uint16_t s = 0; s < instrument.header.numSamples; ++s) {
                XMSample& sample = instrument.samples[s];

                #if defined(_DEBUG_XMPlayer_)    
                    debug.logLevelMessage(LogLevel::LOG_INFO,
                        L"Sample[" + std::to_wstring(s) + L"] Length = " + std::to_wstring(sample.length));
                #endif
                sample.sampleData.resize(sample.length);
                if (sample.length > 0) {
                    file.read(reinterpret_cast<char*>(sample.sampleData.data()), sample.length);
                    if (!file) {
                        debug.logLevelMessage(LogLevel::LOG_ERROR,
                            L"LoadInstruments: Failed to read sample data for sample " +
                            std::to_wstring(s) + L" in instrument " + std::to_wstring(i));
                        return false;
                    }
                }

                // Delta decode to signed PCM
                sample.decoded8.resize(sample.length);
                int8_t last = 0;
                for (size_t si = 0; si < sample.length; ++si) {
                    int8_t val = static_cast<int8_t>(sample.sampleData[si]);
                    last += val;
                    sample.decoded8[si] = last;
                }
            }

        #if defined(_DEBUG_XMPlayer_)    
            debug.logLevelMessage(LogLevel::LOG_INFO,
                L"Offset AFTER sample DATA instrument[" + std::to_wstring(i) + L"] = " + std::to_wstring(file.tellg()));
        #endif
        }
    }

    return true;
}

void XMMODPlayer::UnpackPatterns() {
    unpackedPatterns.clear();

    for (size_t i = 0; i < patterns.size(); ++i) {
        const XMPattern& pattern = patterns[i];
        const XMPatternHeader& header = pattern.header;

        if (header.numRows == 0 || header.numRows > 256) {
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"UnpackPatterns: Invalid row count in pattern " + std::to_wstring(i));
            continue;
        }

        std::vector<std::vector<XMEvent>> patternRows;
        patternRows.resize(header.numRows, std::vector<XMEvent>(xmHeader.numChannels));

        const uint8_t* data = pattern.data.data();
        const size_t dataSize = pattern.data.size();
        size_t offset = 0;

        for (uint16_t row = 0; row < header.numRows; ++row) {
            for (uint16_t ch = 0; ch < xmHeader.numChannels; ++ch) {
                XMEvent ev = {};

                if (offset >= dataSize) {
                    debug.logLevelMessage(LogLevel::LOG_WARNING,
                        L"UnpackPatterns: Ran out of data while reading pattern " + std::to_wstring(i));
                    patternRows[row][ch] = ev;
                    continue;
                }

                uint8_t flag = data[offset++];

                if (flag & 0x80) {
                    // Compressed format � only selected fields follow
                    if ((flag & 0x01) && (offset + 1 <= dataSize)) ev.note = data[offset++];
                    if ((flag & 0x02) && (offset + 1 <= dataSize)) ev.instrument = data[offset++];
                    if ((flag & 0x04) && (offset + 1 <= dataSize)) ev.volume = data[offset++];
                    if ((flag & 0x08) && (offset + 1 <= dataSize)) ev.effect = data[offset++];
                    if ((flag & 0x10) && (offset + 1 <= dataSize)) ev.effectData = data[offset++];
                }
                else {
                    // Uncompressed format � 5 full bytes expected
                    ev.note = flag;

                    if (offset + 4 <= dataSize) {
                        ev.instrument = data[offset++];
                        ev.volume = data[offset++];
                        ev.effect = data[offset++];
                        ev.effectData = data[offset++];
                    }
                    else {
                        debug.logLevelMessage(LogLevel::LOG_WARNING,
                            L"UnpackPatterns: Incomplete uncompressed event in pattern " + std::to_wstring(i));
                        break;
                    }
                }

                patternRows[row][ch] = ev;
            }
        }

        unpackedPatterns.push_back(std::move(patternRows));
    }
}

bool XMMODPlayer::Play(const std::wstring& filename) {
	if (isPlaying) return false; // Already playing then exit.
    if (!Initialize(filename)) return false;

    debug.DebugLog("Play(): Starting playback thread...\n");
    TickRow();

    #if defined(PLATFORM_WINDOWS)
        HRESULT hr = secondaryBuffer->Play(0, 0, DSBPLAY_LOOPING);
        if (FAILED(hr)) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"Play(): Failed to start DirectSound buffer playback.");
            return false;
        }
    #endif

    isPlaying = true;
    isPaused = false;
	fadeInActive = false;
	fadeOutActive = false;
    // Start the Playback Thread
    playbackThread = std::thread(&XMMODPlayer::PlaybackLoop, this);
    return true;
}

// Narrow-string overload (converts and delegates)
bool XMMODPlayer::Play(const std::string& path)
{
    std::wstring widePath(path.begin(), path.end());

    debug.logLevelMessage(LogLevel::LOG_INFO, L"XM Player: Converted std::string to std::wstring for Play()");

    return Play(widePath);
}

void XMMODPlayer::Terminate()
{
    isTerminating = true;
    
    Stop();
}

void XMMODPlayer::Mute() {
    #if defined(_DEBUG_XMPlayer_)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Mute: Silencing all active voices and buffer");
    #endif

    for (auto& voice : voices) {
        if (voice.active && voice.sample) {
            voice.volume = 0;
        }
    }

    SilenceBuffer();  // Clear buffer as well
    isMuted = true;
}

void XMMODPlayer::Pause() {
    #if defined(_DEBUG_XMPlayer_)    
        debug.DebugLog("Pause(): Playback paused.\n");
    #endif
    Mute();            // Mute immediately
	Sleep(100);        // Wait for a short duration to ensure silence
    isPaused = true;
}

void XMMODPlayer::Resume() {
    #if defined(_DEBUG_XMPlayer_)    
        debug.DebugLog("Resume(): Resuming playback.\n");
    #endif
    isPaused = false;
    isMuted = false;
    SetVolume(targetVolume); // Restore volume
}

void XMMODPlayer::HardPause() {
    #if defined(_DEBUG_XMPlayer_)
        debug.DebugLog("HardPause(): Forcing playback silence and halting voices.\n");
    #endif
    std::lock_guard<std::mutex> lock(playbackMutex);

    isPaused = true;
    isMuted = true;

    for (auto& voice : voices) {
        voice.active = false;
        voice.volume = 0;
        voice.baseVolume = 0;
        voice.position = 0.0f;
    }

    SilenceBuffer();
    SetVolume(0);
}

void XMMODPlayer::HardResume() {
    #if defined(_DEBUG_XMPlayer_)
        debug.DebugLog("HardResume(): Full playback reset and resume.\n");
    #endif
    std::lock_guard<std::mutex> lock(playbackMutex);

    // Reset tick/counters/state
    tick = 0;
    currentRow = 0;
    currentPatternIndex = xmHeader.patternOrderTable[sequencePosition];
    lastTickTime = std::chrono::steady_clock::now();

    // Reset voice state
    for (auto& voice : voices) {
        voice.active = false;
        voice.position = 0.0f;
        voice.step = 0.0f;
        voice.volume = 0;
        voice.baseVolume = 0;
        voice.envTick = 0;
        voice.envelopeReleased = false;
    }

    // Silence buffer to avoid artifacts
    SilenceBuffer();

    // Clear any mute/pause flags
    isPaused = false;
    isMuted = false;

    // Restore full volume
    globalVolume.store(64);                     // ? Correct way
    currentVolume.store(targetVolume.load());   // ? Atomic-to-atomic assignment
	targetVolume.store(64);                     // ? Atomic-to-atomic assignment
    // Force retrigger of current row
    TickRow();
}

bool XMMODPlayer::IsPaused() const {
	return isPaused;
}

void XMMODPlayer::FadeOutAndStop(uint32_t durationMs) {
    if (!isPlaying) return;

    SetFadeOut(durationMs);
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

    Stop();
}

void XMMODPlayer::Stop() {
	if (!isPlaying) return;
    #if defined(_DEBUG_XMPlayer_)
        debug.DebugLog("Stop(): Playback stopped.\n");
    #endif
    isPlaying = false;
    isTerminating = true;
    if (playbackThread.joinable()) {
        playbackThread.join();
    }
}

void XMMODPlayer::SilenceBuffer() {
#if defined(PLATFORM_WINDOWS)
    if (!secondaryBuffer) return;

    void* p1 = nullptr;
    void* p2 = nullptr;
    DWORD b1 = 0, b2 = 0;

    HRESULT hr = secondaryBuffer->Lock(0, bufferSize, &p1, &b1, &p2, &b2, DSBLOCK_ENTIREBUFFER);
    if (SUCCEEDED(hr)) {
        if (p1 && b1 > 0) memset(p1, 0, b1);
        if (p2 && b2 > 0) memset(p2, 0, b2);
        secondaryBuffer->Unlock(p1, b1, p2, b2);
    }
#endif
}

void XMMODPlayer::SetFadeIn(uint32_t durationMs) {
    fadeDurationMs = durationMs;
    fadeElapsedMs = 0;
    fadeInActive = true;
    fadeOutActive = false;
    currentVolume = 0;
    targetVolume = 64;
    #if defined(_DEBUG_XMPlayer_)
        debug.DebugLog("Fade-in started.\n");
    #endif 
}

void XMMODPlayer::SetFadeOut(uint32_t durationMs) {
    fadeDurationMs = durationMs;
    fadeElapsedMs = 0;
    fadeInActive = false;
    fadeOutActive = true;
    targetVolume = 0;
    #if defined(_DEBUG_XMPlayer_)
        debug.DebugLog("Fade-out started.\n");
    #endif
}

bool XMMODPlayer::CreateAudioDevice() {
#if defined(PLATFORM_WINDOWS)
    HRESULT hr = DirectSoundCreate8(NULL, &directSound, NULL);
    if (FAILED(hr)) return false;

    hr = directSound->SetCooperativeLevel(hwnd, DSSCL_PRIORITY);
    if (FAILED(hr)) return false;

    DSBUFFERDESC primaryDesc = {};
    primaryDesc.dwSize = sizeof(primaryDesc);
    primaryDesc.dwFlags = DSBCAPS_PRIMARYBUFFER;

    hr = directSound->CreateSoundBuffer(&primaryDesc, &primaryBuffer, NULL);
    if (FAILED(hr)) return false;

    WAVEFORMATEX waveFormat = {};
    waveFormat.wFormatTag = WAVE_FORMAT_PCM;
    waveFormat.nChannels = 2;
    waveFormat.nSamplesPerSec = 44100;
    waveFormat.wBitsPerSample = 16;
    waveFormat.nBlockAlign = waveFormat.nChannels * waveFormat.wBitsPerSample / 8;
    waveFormat.nAvgBytesPerSec = waveFormat.nSamplesPerSec * waveFormat.nBlockAlign;

    primaryBuffer->SetFormat(&waveFormat);

    DSBUFFERDESC secondaryDesc = {};
    secondaryDesc.dwSize = sizeof(secondaryDesc);
    secondaryDesc.dwBufferBytes = 44100 * 2 * 2; // 2 seconds of stereo audio
    secondaryDesc.lpwfxFormat = &waveFormat;
    secondaryDesc.dwFlags = DSBCAPS_GLOBALFOCUS | DSBCAPS_GETCURRENTPOSITION2;

    hr = directSound->CreateSoundBuffer(&secondaryDesc, &secondaryBuffer, NULL);
    if (FAILED(hr)) return false;

    bufferSize = secondaryDesc.dwBufferBytes;
    writeCursor = 0;

    return true;
#else
    return true; // Non-DX: audio handled by platform audio layer
#endif
}

void XMMODPlayer::FillAudioBuffer() {
#if defined(PLATFORM_WINDOWS)
    #if defined(_DEBUG_XMPlayer_)
        debug.logLevelMessage(LogLevel::LOG_DEBUG, L"FillAudioBuffer: Begin");
    #endif

    if (!secondaryBuffer) return;

    DWORD playCursor = 0, writeCursorDS = 0;
    HRESULT hr = secondaryBuffer->GetCurrentPosition(&playCursor, &writeCursorDS);
    if (FAILED(hr)) {
        #if defined(_DEBUG_XMPlayer_)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"FillAudioBuffer: Failed to get current position.");
        #endif
        return;
    }

    constexpr DWORD safeMargin = 2048; // ~20ms margin for safety at 44.1kHz stereo
    DWORD writePos = writeCursor;
    DWORD distance = (playCursor + safeMargin - writePos + bufferSize) % bufferSize;
    DWORD samplesToWrite = distance / (sizeof(int16_t) * 2);

    if (samplesToWrite == 0) {
        #if defined(_DEBUG_XMPlayer_)
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"FillAudioBuffer: No samples to write.");
        #endif
        return;
    }

    #if defined(_DEBUG_XMPlayer_)
        debug.logLevelMessage(LogLevel::LOG_DEBUG,
            L"FillAudioBuffer: playCursor=" + std::to_wstring(playCursor) +
            L", writeCursor=" + std::to_wstring(writePos) +
            L", samplesToWrite=" + std::to_wstring(samplesToWrite));
    #endif

    // ?? Use stack or static buffer (avoid dynamic allocation for real-time use)
    std::vector<int16_t> mixBuffer(samplesToWrite * 2, 0);
    MixAudio(mixBuffer.data(), samplesToWrite);

    DWORD bytesToWrite = samplesToWrite * sizeof(int16_t) * 2;

    void* p1 = nullptr;
    void* p2 = nullptr;
    DWORD b1 = 0, b2 = 0;

    hr = secondaryBuffer->Lock(writePos, bytesToWrite, &p1, &b1, &p2, &b2, 0);
    if (SUCCEEDED(hr)) {
        #if defined(_DEBUG_XMPlayer_)
            debug.logLevelMessage(LogLevel::LOG_DEBUG,
                L"FillAudioBuffer: Lock success. Bytes1=" + std::to_wstring(b1) +
                L", Bytes2=" + std::to_wstring(b2));
        #endif

        if (p1 && b1 > 0) memcpy(p1, mixBuffer.data(), b1);
        if (p2 && b2 > 0) memcpy(p2, reinterpret_cast<uint8_t*>(mixBuffer.data()) + b1, b2);

        secondaryBuffer->Unlock(p1, b1, p2, b2);

        writeCursor = (writeCursor + bytesToWrite) % bufferSize;

        #if defined(_DEBUG_XMPlayer_)
            debug.logLevelMessage(LogLevel::LOG_DEBUG,
                L"FillAudioBuffer: Updated writeCursor=" + std::to_wstring(writeCursor));
        #endif
    }
    else {
        #if defined(_DEBUG_XMPlayer_)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"FillAudioBuffer: Lock failed.");
        #endif
    }

    #if defined(_DEBUG_XMPlayer_)
        debug.logLevelMessage(LogLevel::LOG_DEBUG, L"FillAudioBuffer: End");
    #endif
#endif // PLATFORM_WINDOWS
}

void XMMODPlayer::MixAudio(int16_t* buffer, size_t samples) {
    // Zero the output buffer (interleaved stereo)
    std::fill(buffer, buffer + samples * 2, 0);

    const float globalVolFactor = static_cast<float>(globalVolume.load()) / 64.0f;
    const float fadeVolFactor = static_cast<float>(currentVolume.load()) / 64.0f;

    for (ChannelVoice& voice : voices) {
        if (!voice.active || !voice.sample || voice.step <= 0.0f)
            continue;

        const XMSample* sample = voice.sample;
        const bool is16Bit = (sample->type & 0x10) != 0;
        const size_t dataSize = is16Bit ? sample->decoded16.size() : sample->decoded8.size();

        float samplePos = voice.position;
        float vol = static_cast<float>(voice.baseVolume) / 64.0f;
        float pan = static_cast<float>(voice.panning) / 255.0f;

        // Volume envelope + key-off fadeout (advanced once per tick in UpdateEnvelopes,
        // not per audio buffer, so it tracks musical time rather than wall-clock time)
        const float volEnv = static_cast<float>(voice.envValue) / 64.0f;
        const float fadeEnv = voice.fadeVolume / 32768.0f;

        const float finalVol = vol * volEnv * fadeEnv * globalVolFactor * fadeVolFactor;
        const float leftVol = finalVol * (1.0f - pan) * 32767.0f;
        const float rightVol = finalVol * pan * 32767.0f;

        int16_t* out = buffer;
        const float step = voice.step;

        if (is16Bit) {
            const auto* data = sample->decoded16.data();
            for (size_t i = 0; i < samples; ++i, out += 2) {
                const size_t idx = static_cast<size_t>(samplePos);
                if (idx >= dataSize) {
                    voice.active = false;
                    break;
                }

                float s = static_cast<float>(data[idx]) / 32768.0f;

                if (idx + 1 < dataSize) {
                    float next = static_cast<float>(data[idx + 1]) / 32768.0f;
                    s += (next - s) * (samplePos - idx);
                }

                const int32_t l = static_cast<int32_t>(s * leftVol);
                const int32_t r = static_cast<int32_t>(s * rightVol);

                out[0] = std::clamp<int32_t>(out[0] + l, -32768, 32767);
                out[1] = std::clamp<int32_t>(out[1] + r, -32768, 32767);

                samplePos += step;

                if ((sample->type & 0x03) && sample->loopLength > 1) {
                    const size_t loopEnd = sample->loopStart + sample->loopLength;
                    if (loopEnd <= dataSize && samplePos >= loopEnd) {
                        samplePos = sample->loopStart + fmod(samplePos - sample->loopStart, static_cast<float>(sample->loopLength));
                    }
                }
                else if (samplePos >= dataSize) {
                    voice.active = false;
                    break;
                }
            }
        }
        else {
            const auto* data = sample->decoded8.data();
            for (size_t i = 0; i < samples; ++i, out += 2) {
                const size_t idx = static_cast<size_t>(samplePos);
                if (idx >= dataSize) {
                    voice.active = false;
                    break;
                }

                float s = static_cast<float>(data[idx]) / 128.0f;

                if (idx + 1 < dataSize) {
                    float next = static_cast<float>(data[idx + 1]) / 128.0f;
                    s += (next - s) * (samplePos - idx);
                }

                const int32_t l = static_cast<int32_t>(s * leftVol);
                const int32_t r = static_cast<int32_t>(s * rightVol);

                out[0] = std::clamp<int32_t>(out[0] + l, -32768, 32767);
                out[1] = std::clamp<int32_t>(out[1] + r, -32768, 32767);

                samplePos += step;

                if ((sample->type & 0x03) && sample->loopLength > 1) {
                    const size_t loopEnd = sample->loopStart + sample->loopLength;
                    if (loopEnd <= dataSize && samplePos >= loopEnd) {
                        samplePos = sample->loopStart + fmod(samplePos - sample->loopStart, static_cast<float>(sample->loopLength));
                    }
                }
                else if (samplePos >= dataSize) {
                    voice.active = false;
                    break;
                }
            }
        }

        voice.position = samplePos;
    }
}

void XMMODPlayer::SetVolume(uint8_t volume) {
    currentVolume = volume;
    targetVolume = volume;
}

void XMMODPlayer::SetGlobalVolume(uint8_t volume) {
    globalVolume = std::clamp(volume, static_cast<uint8_t>(0), static_cast<uint8_t>(64));
}

bool XMMODPlayer::IsPlaying() const {
    return isPlaying && !isPaused && !isTerminating;
}

float XMMODPlayer::NoteToFrequency(uint8_t note, int8_t finetune, int8_t relativeNote) {
    // XM default C-4 is 8363 Hz
    // Note value is 1�96, where 49 = C-4 (note 48 zero-based)
    int actualNote = static_cast<int>(note) + static_cast<int>(relativeNote) - 1;
    float baseFreq = 8363.0f;

    // Calculate semitone offset from C-4
    int semitoneOffset = actualNote - 48;

    // Fine-tune shifts pitch by up to +/- 128 cents (100 cent = 1 semitone)
    float finetuneOffset = finetune / 128.0f;

    // Convert to frequency using 2^(n/12)
    float freq = baseFreq * powf(2.0f, (semitoneOffset + finetuneOffset) / 12.0f);

    return freq;
}

void XMMODPlayer::TriggerNote(ChannelVoice& voice, const XMEvent& ev) {
    const XMInstrument* inst = nullptr;
    if (ev.instrument > 0 && ev.instrument <= instruments.size())
        inst = &instruments[ev.instrument - 1];
    else
        inst = voice.instrument;    // FT2: note without instrument keeps the current one

    if (!inst || ev.note == 0 || ev.note >= 97) return;

    const uint8_t sampleIndex = inst->header.sampleNoteNumber[ev.note - 1];
    if (sampleIndex >= inst->samples.size()) return;

    const XMSample& sample = inst->samples[sampleIndex];
    if (sample.length == 0) {
        voice.active = false;       // FT2: empty sample cuts the channel
        return;
    }

    voice.sample = &sample;
    voice.instrument = inst;
    voice.position = 0.0f;
    voice.baseVolume = std::min<uint8_t>(sample.volume, 64);
    voice.volume = voice.baseVolume;
    voice.step = NoteToFrequency(ev.note, sample.finetune, sample.relativeNote) / 44100.0f;
    voice.baseStep = voice.step;
    voice.note = ev.note;
    voice.active = true;
    voice.envTick = 0;
    voice.envValue = 64;
    voice.fadeVolume = 32768.0f;
    voice.envelopeReleased = false;
    voice.vibratoPos = 0;
    voice.tremoloPos = 0;
    voice.panning = sample.panning;
}

void XMMODPlayer::ReleaseVoice(ChannelVoice& voice) {
    voice.envelopeReleased = true;
    // FT2: key off without an enabled volume envelope cuts the note immediately
    if (!voice.instrument || !(voice.instrument->header.volumeType & 0x01)) {
        voice.volume = 0;
        voice.baseVolume = 0;
    }
}

void XMMODPlayer::TickRow() {
    // EEx Pattern Delay: repeat the current row without re-triggering notes.
    // Per-tick effects (vibrato/arpeggio/slides) keep running via ApplyTickEffects
    // on the intervening ticks using the effect state already latched on the row.
    if (patternDelayCounter > 0) {
        --patternDelayCounter;
        tick = 0;
        return;
    }

    bool posJump = false;
    bool patBreak = false;
    uint16_t nextSequencePosition = sequencePosition;
    uint16_t nextRow = currentRow + 1;

    // Cache references
    const uint8_t patternIndex = xmHeader.patternOrderTable[sequencePosition];
    if (patternIndex >= unpackedPatterns.size()) {
        // Corrupt order entry: skip forward instead of indexing out of range
        sequencePosition = (sequencePosition + 1 < xmHeader.songLength) ? sequencePosition + 1 : 0;
        currentRow = 0;
        tick = 0;
        return;
    }
    currentPatternIndex = patternIndex;

    const auto& pattern = unpackedPatterns[patternIndex];
    const auto& row = pattern[currentRow];

    const size_t numChannels = row.size();

    for (size_t ch = 0; ch < numChannels; ++ch) {
        const XMEvent& ev = row[ch];
        ChannelVoice& voice = voices[ch];

        // Restore pitch and volume modified by per-tick effects on the
        // previous row (vibrato, arpeggio, tremolo, tremor are temporary in FT2)
        if (voice.sample) {
            if (voice.baseStep > 0.0f) voice.step = voice.baseStep;
            voice.baseVolume = voice.volume;
        }
        voice.delayedNotePending = false;

        const bool validNote = (ev.note > 0 && ev.note < 97);
        const bool keyOffNote = (ev.note == 97);
        const bool validInstr = (ev.instrument > 0 && ev.instrument <= instruments.size());
        const bool tonePortaRow = voice.sample &&
            (ev.effect == 0x03 || ev.effect == 0x05 || (ev.volume >> 4) == 0x0F);

        // EDx (Note Delay): defer the whole event until tick x
        if (ev.effect == 0x0E && (ev.effectData >> 4) == 0x0D && (ev.effectData & 0x0F) > 0) {
            voice.delayedNotePending = true;
            voice.delayTicks = ev.effectData & 0x0F;
            voice.delayedEvent = ev;
            voice.effect = ev.effect;
            voice.effectData = ev.effectData;
            voice.volCol = 0;
            continue;
        }

        // Note handling
        if (validNote) {
            if (tonePortaRow) {
                // 3xx/5xx/volume-column Fx with a note sets the glide target
                // without retriggering the sample (FT2)
                voice.targetStep = NoteToFrequency(ev.note, voice.sample->finetune,
                    voice.sample->relativeNote) / 44100.0f;
                voice.note = ev.note;
            }
            else {
                TriggerNote(voice, ev);
            }
        }
        else if (keyOffNote) {
            // Note 97 = key off: release envelope / cut (previously ignored,
            // which left looped samples sustaining forever)
            ReleaseVoice(voice);
        }
        else if (validInstr && voice.sample) {
            // Instrument number without a note: reset volume and envelope (FT2)
            voice.volume = std::min<uint8_t>(voice.sample->volume, 64);
            voice.baseVolume = voice.volume;
            voice.envTick = 0;
            voice.envValue = 64;
            voice.fadeVolume = 32768.0f;
            voice.envelopeReleased = false;
        }

        // Volume column (tick 0 part)
        const uint8_t vc = ev.volume;
        if (vc >= 0x10 && vc <= 0x50) {
            const uint8_t vol = std::min<uint8_t>(vc - 0x10, 64);
            voice.baseVolume = vol;
            voice.volume = vol;
        }
        else {
            const uint8_t vcVal = vc & 0x0F;
            switch (vc >> 4) {
            case 0x8: // Fine volume slide down
                voice.volume = static_cast<uint8_t>(std::max<int>(0, voice.volume - vcVal));
                voice.baseVolume = voice.volume;
                break;
            case 0x9: // Fine volume slide up
                voice.volume = static_cast<uint8_t>(std::min<int>(64, voice.volume + vcVal));
                voice.baseVolume = voice.volume;
                break;
            case 0xA: // Set vibrato speed
                if (vcVal) voice.vibratoMem = (voice.vibratoMem & 0x0F) | static_cast<uint8_t>(vcVal << 4);
                break;
            case 0xC: // Set panning (0-15 scaled to 0-255)
                voice.panning = static_cast<uint8_t>(vcVal * 17);
                break;
            case 0xF: // Tone portamento speed (x16, FT2 scaling)
                if (vcVal) voice.portaSpeed = static_cast<uint8_t>(vcVal << 4);
                break;
            default:
                break;
            }
        }

        // Effect column (tick 0 part)
        const uint8_t fx = ev.effect;
        const uint8_t data = ev.effectData;

        switch (fx) {
        case 0x01: // Portamento Up: update parameter memory
            if (data) voice.portaUpMem = data;
            break;

        case 0x02: // Portamento Down: update parameter memory
            if (data) voice.portaDownMem = data;
            break;

        case 0x03: // Tone Portamento: update speed memory
            if (data) voice.portaSpeed = data;
            break;

        case 0x04: // Vibrato: per-nibble parameter memory
            if (data >> 4)   voice.vibratoMem = (voice.vibratoMem & 0x0F) | (data & 0xF0);
            if (data & 0x0F) voice.vibratoMem = (voice.vibratoMem & 0xF0) | (data & 0x0F);
            break;

        case 0x05: // Tone Porta + Vol Slide / Vibrato + Vol Slide / Vol Slide memory
        case 0x06:
        case 0x0A:
            if (data) voice.volSlideMem = data;
            break;

        case 0x08: // Set Panning
            voice.panning = data;
            break;

        case 0x09: // Sample Offset
            if (voice.sample) {
                const uint32_t offset = static_cast<uint32_t>(data) << 8;
                voice.position = static_cast<float>(std::min(offset, voice.sample->length));
            }
            break;

        case 0x0B: // Position Jump
            nextSequencePosition = data;
            nextRow = 0;
            posJump = true;
            break;

        case 0x0C: { // Set Volume
            const uint8_t vol = std::min<uint8_t>(data, 64);
            voice.volume = vol;
            voice.baseVolume = vol;
            break;
        }

        case 0x0D: // Pattern Break (BCD row target; keep a Bxx target set this row)
            nextRow = (data >> 4) * 10 + (data & 0x0F);
            if (!posJump) nextSequencePosition = sequencePosition + 1;
            patBreak = true;
            break;

        case 0x0E: { // Extended commands executed on tick 0
            const uint8_t sub = data >> 4;
            const uint8_t val = data & 0x0F;
            switch (sub) {
            case 0x1: // Fine Portamento Up (once per row)
                if (voice.baseStep > 0.0f) {
                    voice.baseStep *= powf(2.0f, (4.0f * val) / 768.0f);
                    voice.step = voice.baseStep;
                }
                break;
            case 0x2: // Fine Portamento Down (once per row)
                if (voice.baseStep > 0.0f) {
                    voice.baseStep *= powf(2.0f, (-4.0f * val) / 768.0f);
                    voice.step = voice.baseStep;
                }
                break;
            case 0x8: // Coarse panning
                voice.panning = static_cast<uint8_t>(val * 17);
                break;
            case 0xA: // Fine Volume Slide Up (once per row)
                voice.volume = static_cast<uint8_t>(std::min<int>(64, voice.volume + val));
                voice.baseVolume = voice.volume;
                break;
            case 0xB: // Fine Volume Slide Down (once per row)
                voice.volume = static_cast<uint8_t>(std::max<int>(0, voice.volume - val));
                voice.baseVolume = voice.volume;
                break;
            case 0xC: // Note Cut at tick 0
                if (val == 0) {
                    voice.volume = 0;
                    voice.baseVolume = 0;
                }
                break;
            case 0xE: // Pattern Delay: repeat this row val extra times
                if (val > 0 && patternDelayCounter == 0)
                    patternDelayCounter = val;
                break;
            default:
                break;
            }
            break;
        }

        case 0x0F: // Set Speed (01-1F) / Set BPM (20-FF)
            if (data == 0) break;   // F00 ignored
            if (data < 0x20) tempo = data;
            else bpm = data;
            break;

        case 0x10: // Gxx Set Global Volume (was wrongly mapped to 0x11)
            globalVolume = std::min<uint8_t>(data, 64);
            break;

        case 0x14: // Kxx Key Off
            ReleaseVoice(voice);
            break;

        case 0x15: // Lxx Set Envelope Position
            voice.envTick = data;
            voice.panEnvTick = data;
            break;

        case 0x21: { // Xxx Extra Fine Portamento (X1x up / X2x down, once per row)
            const uint8_t sub = data >> 4;
            const uint8_t val = data & 0x0F;
            if (voice.baseStep > 0.0f) {
                if (sub == 0x01)      voice.baseStep *= powf(2.0f, (1.0f * val) / 768.0f);
                else if (sub == 0x02) voice.baseStep *= powf(2.0f, (-1.0f * val) / 768.0f);
                voice.step = voice.baseStep;
            }
            break;
        }

        default:
            break;
        }

        // Save row data for per-tick processing
        voice.effect = fx;
        voice.effectData = data;
        voice.volCol = vc;
    }

    // Row / sequence advancement
    if (!posJump && !patBreak) {
        if (nextRow >= patterns[currentPatternIndex].header.numRows) {
            nextRow = 0;
            nextSequencePosition++;
        }
    }

    if (nextSequencePosition >= xmHeader.songLength) {
        nextSequencePosition = (xmHeader.restartPosition < xmHeader.songLength)
            ? xmHeader.restartPosition : 0;
    }

    // Clamp a break target that exceeds the next pattern's row count
    const uint8_t nextPattern = xmHeader.patternOrderTable[nextSequencePosition];
    if (nextPattern < patterns.size() && nextRow >= patterns[nextPattern].header.numRows) {
        nextRow = 0;
    }

    currentRow = nextRow;
    sequencePosition = nextSequencePosition;
    tick = 0;
}

void XMMODPlayer::ApplyTickEffects() {
    const size_t numVoices = voices.size();

    for (size_t ch = 0; ch < numVoices; ++ch) {
        ChannelVoice& voice = voices[ch];

        // EDx: trigger the delayed event once its tick arrives
        if (voice.delayedNotePending && tick == voice.delayTicks) {
            voice.delayedNotePending = false;
            const XMEvent& dev = voice.delayedEvent;
            if (dev.note > 0 && dev.note < 97) TriggerNote(voice, dev);
            else if (dev.note == 97) ReleaseVoice(voice);
            if (dev.volume >= 0x10 && dev.volume <= 0x50) {
                const uint8_t vol = std::min<uint8_t>(dev.volume - 0x10, 64);
                voice.volume = vol;
                voice.baseVolume = vol;
            }
        }

        if (!voice.sample || !voice.active) continue;

        const uint8_t fx = voice.effect;
        const uint8_t data = voice.effectData;

        // Volume column (per-tick part)
        const uint8_t vc = voice.volCol;
        const uint8_t vcVal = vc & 0x0F;
        switch (vc >> 4) {
        case 0x6: // Volume slide down
            voice.volume = static_cast<uint8_t>(std::max<int>(0, voice.volume - vcVal));
            voice.baseVolume = voice.volume;
            break;
        case 0x7: // Volume slide up
            voice.volume = static_cast<uint8_t>(std::min<int>(64, voice.volume + vcVal));
            voice.baseVolume = voice.volume;
            break;
        case 0xB: // Vibrato (depth nibble, speed from Ax memory)
            if (vcVal) voice.vibratoMem = (voice.vibratoMem & 0xF0) | vcVal;
            ApplyTickEffects_Vibrato(voice);
            break;
        case 0xD: // Panning slide left
            voice.panning = static_cast<uint8_t>(std::max<int>(0, voice.panning - vcVal));
            break;
        case 0xE: // Panning slide right
            voice.panning = static_cast<uint8_t>(std::min<int>(255, voice.panning + vcVal));
            break;
        case 0xF: // Tone portamento
            ApplyTickEffects_TonePortamento(voice);
            break;
        default:
            break;
        }

        switch (fx) {
        case 0x00: // Arpeggio (temporary pitch, restored at next row)
            if (data) {
                const int sel = tick % 3;
                float mult = 1.0f;
                if (sel == 1)      mult = powf(2.0f, static_cast<float>(data >> 4) / 12.0f);
                else if (sel == 2) mult = powf(2.0f, static_cast<float>(data & 0x0F) / 12.0f);
                voice.step = voice.baseStep * mult;
            }
            break;

        case 0x01: // Portamento Up
            if (voice.baseStep > 0.0f) {
                voice.baseStep *= powf(2.0f, (4.0f * voice.portaUpMem) / 768.0f);
                voice.step = voice.baseStep;
            }
            break;

        case 0x02: // Portamento Down
            if (voice.baseStep > 0.0f) {
                voice.baseStep *= powf(2.0f, (-4.0f * voice.portaDownMem) / 768.0f);
                voice.step = voice.baseStep;
            }
            break;

        case 0x03: // Tone Portamento
            ApplyTickEffects_TonePortamento(voice);
            break;

        case 0x04: // Vibrato
            ApplyTickEffects_Vibrato(voice);
            break;

        case 0x05: // Tone Portamento + Volume Slide (no vibrato in FT2)
            ApplyTickEffects_TonePortamento(voice);
            ApplyTickEffects_VolumeSlide(voice);
            break;

        case 0x06: // Vibrato + Volume Slide
            ApplyTickEffects_Vibrato(voice);
            ApplyTickEffects_VolumeSlide(voice);
            break;

        case 0x07: { // Tremolo (temporary volume, restored at next row)
            const float trem = VibratoTable(voice.tremoloPos) * (data & 0x0F) * 4.0f;
            voice.baseVolume = static_cast<uint8_t>(
                std::clamp<int>(static_cast<int>(voice.volume) + static_cast<int>(trem), 0, 64));
            voice.tremoloPos = (voice.tremoloPos + (data >> 4)) & 63;
            break;
        }

        case 0x0A: // Volume Slide
            ApplyTickEffects_VolumeSlide(voice);
            break;

        case 0x0E: { // Extended per-tick subcommands
            const uint8_t sub = data >> 4;
            const uint8_t val = data & 0x0F;
            if (sub == 0x09 && val && (tick % val == 0)) {   // Retrig
                voice.position = 0.0f;
            }
            else if (sub == 0x0C && tick == val) {           // Note Cut at tick x
                voice.volume = 0;
                voice.baseVolume = 0;
            }
            break;
        }

        case 0x11: { // Hxy Global Volume Slide (was wrongly mapped to 0x19)
            uint8_t gv = globalVolume.load();
            const uint8_t up = data >> 4;
            const uint8_t down = data & 0x0F;
            if (up) gv = std::min<uint8_t>(64, gv + up);
            else if (down) gv = static_cast<uint8_t>(std::max<int>(0, gv - down));
            globalVolume = gv;
            break;
        }

        case 0x19: { // Pxy Panning Slide (was wrongly mapped to 0x1E)
            const uint8_t right = data >> 4;
            const uint8_t left = data & 0x0F;
            if (right)
                voice.panning = static_cast<uint8_t>(std::min<int>(255, voice.panning + right));
            else if (left)
                voice.panning = static_cast<uint8_t>(std::max<int>(0, voice.panning - left));
            break;
        }

        case 0x1B: { // Rxy Multi Retrig Note
            const uint8_t retrig = data & 0x0F;
            const uint8_t vChange = data >> 4;
            if (retrig && (tick % retrig == 0)) {
                voice.position = 0.0f;
                int v = voice.volume;
                switch (vChange) {
                case 0x1: v -= 1; break;
                case 0x2: v -= 2; break;
                case 0x3: v -= 4; break;
                case 0x4: v -= 8; break;
                case 0x5: v -= 16; break;
                case 0x6: v = (v * 2) / 3; break;
                case 0x7: v /= 2; break;
                case 0x9: v += 1; break;
                case 0xA: v += 2; break;
                case 0xB: v += 4; break;
                case 0xC: v += 8; break;
                case 0xD: v += 16; break;
                case 0xE: v = (v * 3) / 2; break;
                case 0xF: v *= 2; break;
                default: break;
                }
                voice.volume = static_cast<uint8_t>(std::clamp(v, 0, 64));
                voice.baseVolume = voice.volume;
            }
            break;
        }

        case 0x1D: { // Txy Tremor (temporary volume, restored at next row)
            const uint8_t on = data >> 4;
            const uint8_t off = data & 0x0F;
            const uint8_t total = on + off;
            if (total > 0)
                voice.baseVolume = ((tick % total) >= on) ? 0 : voice.volume;
            break;
        }

        default:
            break;
        }
    }
}

// Additional helper functions for clarity
void XMMODPlayer::ApplyTickEffects_VolumeSlide(ChannelVoice& voice) {
    const uint8_t param = voice.volSlideMem;    // Axy/5xy/6xy share FT2 parameter memory
    const uint8_t up = param >> 4;
    const uint8_t down = param & 0x0F;

    int newVol = static_cast<int>(voice.volume);
    if (up) newVol += up;
    else if (down) newVol -= down;

    voice.volume = static_cast<uint8_t>(std::clamp(newVol, 0, 64));
    voice.baseVolume = voice.volume;
}

void XMMODPlayer::ApplyTickEffects_TonePortamento(ChannelVoice& voice) {
    if (voice.targetStep <= 0.0f || voice.baseStep <= 0.0f || voice.portaSpeed == 0)
        return;

    // FT2 linear mode: 3xx changes the period by 4*speed per tick
    const float mult = powf(2.0f, (4.0f * voice.portaSpeed) / 768.0f);

    if (voice.baseStep < voice.targetStep) {
        voice.baseStep *= mult;
        if (voice.baseStep > voice.targetStep) voice.baseStep = voice.targetStep;
    }
    else if (voice.baseStep > voice.targetStep) {
        voice.baseStep /= mult;
        if (voice.baseStep < voice.targetStep) voice.baseStep = voice.targetStep;
    }

    voice.step = voice.baseStep;
}

void XMMODPlayer::ApplyTickEffects_Vibrato(ChannelVoice& voice) {
    if (voice.baseStep <= 0.0f) return;

    const uint8_t speed = voice.vibratoMem >> 4;
    const uint8_t depth = voice.vibratoMem & 0x0F;

    // Temporary period offset around baseStep (about 2 semitones at depth 15,
    // matching FT2); the base pitch is restored at the next row
    const float periodDelta = VibratoTable(voice.vibratoPos) * depth * 8.0f;
    voice.step = voice.baseStep * powf(2.0f, -periodDelta / 768.0f);
    voice.vibratoPos = (voice.vibratoPos + speed) & 63;
}

void XMMODPlayer::UpdateEnvelopes() {
    for (ChannelVoice& voice : voices) {
        if (!voice.active || !voice.instrument) continue;

        const XMInstrumentHeader& h = voice.instrument->header;

        if ((h.volumeType & 0x01) && h.numVolumePoints > 0) {
            const uint8_t numPts = std::min<uint8_t>(h.numVolumePoints, 12);
            const uint16_t endX = h.volumeEnvelope[(numPts - 1) * 2];

            // Interpolate the current envelope value (0-64)
            const uint16_t pos = voice.envTick;
            if (pos >= endX) {
                voice.envValue = static_cast<uint8_t>(
                    std::min<uint16_t>(h.volumeEnvelope[(numPts - 1) * 2 + 1], 64));
            }
            else {
                uint8_t seg = 0;
                while (seg + 1 < numPts && h.volumeEnvelope[(seg + 1) * 2] <= pos) seg++;
                const uint16_t x0 = h.volumeEnvelope[seg * 2];
                const uint16_t y0 = h.volumeEnvelope[seg * 2 + 1];
                const uint16_t x1 = h.volumeEnvelope[(seg + 1) * 2];
                const uint16_t y1 = h.volumeEnvelope[(seg + 1) * 2 + 1];
                float v = static_cast<float>(y0);
                if (x1 > x0)
                    v += (static_cast<float>(y1) - static_cast<float>(y0)) *
                         static_cast<float>(pos - x0) / static_cast<float>(x1 - x0);
                voice.envValue = static_cast<uint8_t>(std::clamp<int>(static_cast<int>(v), 0, 64));
            }

            // Advance position; hold at the sustain point until key off
            const uint16_t susX =
                h.volumeEnvelope[std::min<uint8_t>(h.volumeSustainPoint, numPts - 1) * 2];
            const bool holdSustain =
                (h.volumeType & 0x02) && !voice.envelopeReleased && voice.envTick >= susX;

            if (!holdSustain && voice.envTick < endX) {
                voice.envTick++;
                if (h.volumeType & 0x04) {  // Envelope loop
                    const uint16_t loopEndX =
                        h.volumeEnvelope[std::min<uint8_t>(h.volumeLoopEndPoint, numPts - 1) * 2];
                    const uint16_t loopStartX =
                        h.volumeEnvelope[std::min<uint8_t>(h.volumeLoopStartPoint, numPts - 1) * 2];
                    if (voice.envTick >= loopEndX) voice.envTick = loopStartX;
                }
            }
        }
        else {
            voice.envValue = 64;
        }

        // Key-off fadeout: FT2 subtracts volumeFadeout from 32768 once per tick
        if (voice.envelopeReleased) {
            voice.fadeVolume -= static_cast<float>(h.volumeFadeout);
            if (voice.fadeVolume <= 0.0f) {
                voice.fadeVolume = 0.0f;
                voice.active = false;
            }
        }
    }
}

void XMMODPlayer::UnpackSamples() {
    for (size_t i = 0; i < instruments.size(); ++i) {
        XMInstrument& inst = instruments[i];

        for (size_t j = 0; j < inst.samples.size(); ++j) {
            XMSample& sample = inst.samples[j];

            // Skip if no data or length
            if (sample.length == 0 || sample.sampleData.empty())
                continue;

            const bool is16Bit = (sample.type & 0x10) != 0;

            if (is16Bit) {
                // 16-bit delta decoding
                const size_t count = sample.length / 2;
                std::vector<int16_t> unpacked(count);
                int16_t accumulator = 0;

                for (size_t k = 0; k < count; ++k) {
                    if ((k * 2 + 1) >= sample.sampleData.size())
                        break;

                    // Read 16-bit little endian delta value
                    int16_t delta = static_cast<int16_t>(
                        static_cast<uint16_t>(sample.sampleData[k * 2]) |
                        (static_cast<uint16_t>(sample.sampleData[k * 2 + 1]) << 8)
                        );

                    accumulator += delta;
                    unpacked[k] = accumulator;
                }

                sample.decoded16 = std::move(unpacked);
            }
            else {
                // 8-bit delta decoding
                std::vector<int8_t> unpacked(sample.length);
                int8_t accumulator = 0;

                for (size_t k = 0; k < sample.length && k < sample.sampleData.size(); ++k) {
                    accumulator += static_cast<int8_t>(sample.sampleData[k]);
                    unpacked[k] = accumulator;
                }

                sample.decoded8 = std::move(unpacked);
            }

            // Clean up original data
            sample.sampleData.clear();
            sample.sampleData.shrink_to_fit();

            #if defined(_DEBUG_XMPlayer_)
                debug.logLevelMessage(LogLevel::LOG_INFO,
                    L"UnpackSamples: Instrument[" + std::to_wstring(i) + L"] Sample[" + std::to_wstring(j) +
                    (is16Bit ? L"] decoded16 size = " : L"] decoded8 size = ") +
                    std::to_wstring(is16Bit ? sample.decoded16.size() : sample.decoded8.size()));
            #endif
        }
    }

    #if defined(_DEBUG_XMPlayer_)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"UnpackSamples: Completed decoding all samples.");
    #endif
}

void XMMODPlayer::GotoSequenceID(uint16_t patternSeqID) {
    if (!isPlaying) {
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"GotoSequenceID called but player not active");
        return;
    }

    if (patternSeqID >= xmHeader.songLength) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"GotoSequenceID invalid PatternSeqID: " + std::to_wstring(patternSeqID));
        return;
    }

    debug.logLevelMessage(LogLevel::LOG_INFO, L"GotoSequenceID: Initiating transition to sequence ID " + std::to_wstring(patternSeqID));

    // Begin fade-out
    SetFadeOut(1000);

    // Wait for fade-out completion
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

    std::this_thread::sleep_for(std::chrono::seconds(1));

    // Update sequence position
    sequencePosition = patternSeqID;
    restartSequencePosition = patternSeqID;                                     // <-- store restart position explicitly
    currentPatternIndex = xmHeader.patternOrderTable[sequencePosition];
    currentRow = 0;
    tick = 0;

    // Begin fade-in
    SetFadeIn(1000);
}

void XMMODPlayer::SetPlaybackThreadPriority(int priority) {
    playbackThreadPriority = priority;
}

// Our threads playback loop
void XMMODPlayer::PlaybackLoop() {
    using namespace std::chrono;

    #if defined(PLATFORM_WINDOWS)
        // Tracker playback must never lag; run this thread at the highest scheduling
        // priority (or whatever the caller set via SetPlaybackThreadPriority()).
        ThreadUtils::NameCurrentThread(L"XM-Playback-Thread");
        ThreadUtils::SetPriority(playbackThreadPriority);
    #endif

    #if defined(_DEBUG_XMPlayer_)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"XM PlaybackLoop: Thread started");
    #endif

    lastTickTime = high_resolution_clock::now();

    auto tickStart = high_resolution_clock::now();
    while (isPlaying && !isTerminating) {
        if (!isPaused) {
            FillAudioBuffer();

            auto now = high_resolution_clock::now();
            auto elapsedTime = duration_cast<microseconds>(now - tickStart).count();
            double tickDurationUs = (2500.0 / static_cast<double>(bpm)) * 1000.0;

            if (elapsedTime >= tickDurationUs) {
                tickStart = now;

                if (tick == 0) {
                    TickRow();
                }
                else {
                    ApplyTickEffects();
                }
                UpdateEnvelopes();

                tick = (tick + 1) % tempo;
            }
        }
        else
        {
			// paused, we can still fill the buffer to avoid glitches
            SetVolume(0); // Mute audio output
            FillAudioBuffer();
            std::this_thread::sleep_for(milliseconds(1));
        }

        // We need to look at this later, we get the true module playback speed
        // now, but at the cost of load on the cpu.  There has to be a way to 
        // fix this calculation somehow and I am not quite sure how precise
        // my maths is on this.  Even thou I am calculating at FT/OpenGPT standard.
//        std::this_thread::sleep_for(milliseconds(1)); // Minimal, dynamic sleep
    }

    // Clear Termination State.
    isTerminating = false;

    #if defined(_DEBUG_XMPlayer_)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"XM PlaybackLoop: Thread exiting");
    #endif
}
