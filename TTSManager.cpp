// -------------------------------------------------------------------------------------------------------------
// TTSManager.cpp - Implementation of Text-to-Speech Manager
// 
// Provides comprehensive TTS functionality using Windows SAPI
// Integrates with existing debug and thread management systems
// Ensures thread-safe operations and proper resource cleanup
// -------------------------------------------------------------------------------------------------------------

#include "TTSManager.h"

// External references
extern Debug debug;
extern ThreadManager threadManager;

#pragma warning(push)
#pragma warning(disable: 4101)

// Constructor - Initialize member variables to safe defaults
TTSManager::TTSManager() :
    m_bIsInitialized(false),                                 // TTS not initialized yet
    m_bHasCleanedUp(false),                                  // Cleanup not performed yet
    m_currentState(TTSPlaybackState::STATE_STOPPED),        // Start in stopped state
    m_pVoice(nullptr),                                       // No voice interface yet
    m_pVoiceToken(nullptr),                                  // No voice token yet
    m_pEnumTokens(nullptr),                                  // No voice enumerator yet
    m_bSpeaking(false),                                      // Not speaking initially
    m_bStopRequested(false)                                  // No stop request initially
{
#if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"TTSManager constructor called");
#endif

    // Initialize configuration to defaults
    m_currentConfig = TTSConfiguration();
}

// Destructor - Ensure proper cleanup
TTSManager::~TTSManager() {
#if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"TTSManager destructor called");
#endif

    // Perform cleanup if not already done
    if (!m_bHasCleanedUp) {
        CleanUp();
    }
}

// Initialize TTS engine and all required components
bool TTSManager::Initialize() {
    // Thread safety lock for initialization using ThreadManager
    ThreadLockHelper lock(threadManager, "tts_init_lock", 5000);
    if (!lock.IsLocked()) {
#if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"TTSManager::Initialize() - Failed to acquire initialization lock");
#endif
        return false;
    }

#if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"TTSManager::Initialize() - Starting TTS initialization");
#endif

    // Check if already initialized
    if (m_bIsInitialized) {
#if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"TTSManager::Initialize() - Already initialized");
#endif
        return true;
    }

    try {
        // Step 1: Initialize COM for SAPI usage
        if (!InitializeCOM()) {
#if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"TTSManager::Initialize() - Failed to initialize COM");
#endif
            return false;
        }

        // Step 2: Create main voice interface
        if (!CreateVoiceInterface()) {
#if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"TTSManager::Initialize() - Failed to create voice interface");
#endif
            return false;
        }

        // Step 3: Enumerate available voices
        if (!EnumerateVoices()) {
#if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"TTSManager::Initialize() - Failed to enumerate voices, using default");
#endif
            // Continue with default voice if enumeration fails
        }

        // Step 4: Apply default voice settings
        if (!ApplyVoiceSettings()) {
#if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"TTSManager::Initialize() - Failed to apply voice settings");
#endif
            return false;
        }

        // Step 5: Setup audio output configuration
        if (!SetupAudioOutput()) {
#if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"TTSManager::Initialize() - Failed to setup audio output, using defaults");
#endif
            // Continue with default audio output if setup fails
        }

        // Mark as successfully initialized
        m_bIsInitialized = true;
        m_currentState = TTSPlaybackState::STATE_STOPPED;

#if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"TTSManager::Initialize() - TTS initialization completed successfully");
#endif

        return true;
    }
    catch (const std::exception& e) {
        // Handle any exceptions during initialization
#if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_TERMINATION, L"TTSManager::Initialize() - Exception caught: %S", e.what());
#endif

        // Cleanup partial initialization
        ReleaseCOMInterfaces();
        return false;
    }
    catch (...) {
        // Handle unknown exceptions
#if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_TERMINATION, L"TTSManager::Initialize() - Unknown exception caught");
#endif

        // Cleanup partial initialization
        ReleaseCOMInterfaces();
        return false;
    }
}

// Clean up all TTS resources and shutdown engine
void TTSManager::CleanUp() {
    // Thread safety lock for cleanup using ThreadManager
    ThreadLockHelper lock(threadManager, "tts_cleanup_lock", 5000);
    if (!lock.IsLocked()) {
#if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"TTSManager::CleanUp() - Failed to acquire cleanup lock, proceeding anyway");
#endif
    }

#if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"TTSManager::CleanUp() - Starting TTS cleanup");
#endif

    // Check if already cleaned up
    if (m_bHasCleanedUp) {
#if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"TTSManager::CleanUp() - Already cleaned up");
#endif
        return;
    }

    try {
        // Stop any current speech
        if (m_bSpeaking.load()) {
            Stop();
        }

        // Release all COM interfaces
        ReleaseCOMInterfaces();

        // Reset internal state
        ResetInternalState();

        // Mark cleanup as completed
        m_bHasCleanedUp = true;
        m_bIsInitialized = false;

#if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"TTSManager::CleanUp() - TTS cleanup completed successfully");
#endif
    }
    catch (const std::exception& e) {
        // Handle exceptions during cleanup
#if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"TTSManager::CleanUp() - Exception during cleanup: %S", e.what());
#endif
    }
    catch (...) {
        // Handle unknown exceptions during cleanup
#if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"TTSManager::CleanUp() - Unknown exception during cleanup");
#endif
    }
}

// Set voice volume level
bool TTSManager::SetVoiceVolume(float volume) {
    // Thread safety lock using ThreadManager
    ThreadLockHelper lock(threadManager, "tts_volume_lock", 2000);
    if (!lock.IsLocked()) {
#if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"TTSManager::SetVoiceVolume() - Failed to acquire volume lock");
#endif
        return false;
    }

#if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"TTSManager::SetVoiceVolume() - Setting volume to %.2f", volume);
#endif

    // Check if initialized
    if (!m_bIsInitialized || !m_pVoice) {
#if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"TTSManager::SetVoiceVolume() - TTS not initialized");
#endif
        return false;
    }

    // Clamp volume to valid range (0.0 to 1.0)
    volume = std::max(0.0f, std::min(1.0f, volume));

    try {
        // Convert volume to SAPI format
        USHORT sapiVolume;
        HRESULT hr = ConvertVolumeToSAPI(volume, sapiVolume);
        if (FAILED(hr)) {
            LogTTSError(hr, L"ConvertVolumeToSAPI");
            return false;
        }

        // Set volume on voice interface
        hr = m_pVoice->SetVolume(sapiVolume);
        if (FAILED(hr)) {
            LogTTSError(hr, L"ISpVoice::SetVolume");
            return false;
        }

        // Update configuration
        m_currentConfig.volume = volume;

#if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"TTSManager::SetVoiceVolume() - Volume set successfully to %.2f", volume);
#endif

        return true;
    }
    catch (const std::exception& e) {
#if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"TTSManager::SetVoiceVolume() - Exception: %S", e.what());
#endif
        return false;
    }
}

// Set voice pitch level
bool TTSManager::SetVoicePitch(float pitch) {
    // Thread safety lock using ThreadManager
    ThreadLockHelper lock(threadManager, "tts_pitch_lock", 2000);
    if (!lock.IsLocked()) {
#if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"TTSManager::SetVoicePitch() - Failed to acquire pitch lock");
#endif
        return false;
    }

#if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"TTSManager::SetVoicePitch() - Setting pitch to %.2f", pitch);
#endif

    // Check if initialized
    if (!m_bIsInitialized || !m_pVoice) {
#if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"TTSManager::SetVoicePitch() - TTS not initialized");
#endif
        return false;
    }

    // Clamp pitch to valid range (-10.0 to +10.0)
    pitch = std::max(-10.0f, std::min(10.0f, pitch));

    try {
        // Convert pitch to SAPI format
        long sapiPitch;
        HRESULT hr = ConvertPitchToSAPI(pitch, sapiPitch);
        if (FAILED(hr)) {
            LogTTSError(hr, L"ConvertPitchToSAPI");
            return false;
        }

        // Set pitch using XML markup in speech text
        // Note: SAPI doesn't have direct pitch control, we'll use SSML
        // This will be applied when speaking text

        // Update configuration
        m_currentConfig.pitch = pitch;

#if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"TTSManager::SetVoicePitch() - Pitch set successfully to %.2f", pitch);
#endif

        return true;
    }
    catch (const std::exception& e) {
#if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"TTSManager::SetVoicePitch() - Exception: %S", e.what());
#endif
        return false;
    }
}

// Set speech rate
bool TTSManager::SetVoiceRate(float rate) {
    // Thread safety lock using ThreadManager
    ThreadLockHelper lock(threadManager, "tts_rate_lock", 2000);
    if (!lock.IsLocked()) {
#if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"TTSManager::SetVoiceRate() - Failed to acquire rate lock");
#endif
        return false;
    }

#if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"TTSManager::SetVoiceRate() - Setting rate to %.2f", rate);
#endif

    // Check if initialized
    if (!m_bIsInitialized || !m_pVoice) {
#if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"TTSManager::SetVoiceRate() - TTS not initialized");
#endif
        return false;
    }

    // Clamp rate to valid range (-10.0 to +10.0)
    rate = std::max(-10.0f, std::min(10.0f, rate));

    try {
        // Convert rate to SAPI format
        long sapiRate;
        HRESULT hr = ConvertRateToSAPI(rate, sapiRate);
        if (FAILED(hr)) {
            LogTTSError(hr, L"ConvertRateToSAPI");
            return false;
        }

        // Set rate on voice interface
        hr = m_pVoice->SetRate(sapiRate);
        if (FAILED(hr)) {
            LogTTSError(hr, L"ISpVoice::SetRate");
            return false;
        }

        // Update configuration
        m_currentConfig.rate = rate;

#if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"TTSManager::SetVoiceRate() - Rate set successfully to %.2f", rate);
#endif

        return true;
    }
    catch (const std::exception& e) {
#if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"TTSManager::SetVoiceRate() - Exception: %S", e.what());
#endif
        return false;
    }
}

// Set speaker channel for audio output
bool TTSManager::SetSpeakerChannel(TTSSpeakerChannel channel) {
    // Thread safety lock using ThreadManager
    ThreadLockHelper lock(threadManager, "tts_channel_lock", 2000);
    if (!lock.IsLocked()) {
        #if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"TTSManager::SetSpeakerChannel() - Failed to acquire channel lock");
        #endif
        return false;
    }

    #if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"TTSManager::SetSpeakerChannel() - Setting channel to %d", static_cast<int>(channel));
    #endif

    // Check if initialized
    if (!m_bIsInitialized || !m_pVoice) {
        #if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"TTSManager::SetSpeakerChannel() - TTS not initialized");
        #endif
        return false;
    }

    try {
        // Update configuration (actual audio routing will be handled during playback)
        m_currentConfig.channel = channel;

        // Setup audio output with new channel configuration
        if (!SetupAudioOutput()) {
            #if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
                debug.logLevelMessage(LogLevel::LOG_WARNING, L"TTSManager::SetSpeakerChannel() - Failed to update audio output");
            #endif
            return false;
        }

        #if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_INFO, L"TTSManager::SetSpeakerChannel() - Channel set successfully to %d", static_cast<int>(channel));
        #endif

        return true;
    }
    catch (const std::exception& e) {
        #if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"TTSManager::SetSpeakerChannel() - Exception: %S", e.what());
        #endif
        return false;
    }
}

// Play text synchronously
bool TTSManager::Play(const std::wstring& text) {
    // Thread safety lock using ThreadManager
    ThreadLockHelper lock(threadManager, "tts_play_lock", 3000);
    if (!lock.IsLocked()) {
        #if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"TTSManager::Play() - Failed to acquire play lock");
        #endif
        return false;
    }

    #if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"TTSManager::Play() - Speaking text: %.50s...", text.c_str());
    #endif

    // Check if initialized
    if (!m_bIsInitialized || !m_pVoice) {
        #if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"TTSManager::Play() - TTS not initialized");
        #endif
        return false;
    }

    // Check if text is empty
    if (text.empty()) {
        #if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"TTSManager::Play() - Empty text provided");
        #endif
        return false;
    }

    try {
        // Stop any current speech
        if (m_bSpeaking.load()) {
            Stop();
        }

        // Apply current voice settings before speaking
        ApplyVoiceSettings();

        // Prepare text with SSML markup for pitch control
        std::wstring ssmlText = L"<speak>";
        if (m_currentConfig.pitch != 0.0f) {
            ssmlText += L"<prosody pitch=\"" + std::to_wstring(static_cast<int>(m_currentConfig.pitch * 10)) + L"%\">";
        }
        ssmlText += text;
        if (m_currentConfig.pitch != 0.0f) {
            ssmlText += L"</prosody>";
        }
        ssmlText += L"</speak>";

        // Set speaking flags
        m_bSpeaking.store(true);
        m_bStopRequested.store(false);
        m_currentState = TTSPlaybackState::STATE_PLAYING;

        // Speak the text synchronously
        HRESULT hr = m_pVoice->Speak(ssmlText.c_str(), SPF_DEFAULT, nullptr);

        // Update state after speaking
        m_bSpeaking.store(false);
        m_currentState = (SUCCEEDED(hr)) ? TTSPlaybackState::STATE_STOPPED : TTSPlaybackState::STATE_ERROR;

        if (FAILED(hr)) {
            LogTTSError(hr, L"ISpVoice::Speak");
            return false;
        }

        #if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_INFO, L"TTSManager::Play() - Text spoken successfully");
        #endif

        return true;
    }
    catch (const std::exception& e) {
        // Reset state on exception
        m_bSpeaking.store(false);
        m_currentState = TTSPlaybackState::STATE_ERROR;

#if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"TTSManager::Play() - Exception: %S", e.what());
#endif
        return false;
    }
}

// Play text asynchronously
bool TTSManager::PlayAsync(const std::wstring& text) {
    // Thread safety lock using ThreadManager
    ThreadLockHelper lock(threadManager, "tts_playasync_lock", 2000);

    if (!lock.IsLocked()) {
        #if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"TTSManager::GetCurrentVoiceName() - Failed to acquire get voice lock");
        #endif
        return false;
    }

    #if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"TTSManager::PlayAsync() - Speaking text asynchronously: %.50s...", text.c_str());
    #endif

    // Check if initialized
    if (!m_bIsInitialized || !m_pVoice) {
        #if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"TTSManager::PlayAsync() - TTS not initialized");
        #endif
        return false;
    }

    // Check if text is empty
    if (text.empty()) {
        #if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"TTSManager::PlayAsync() - Empty text provided");
        #endif
        return false;
    }

    try {
        // Stop any current speech
        if (m_bSpeaking.load()) {
            Stop();
        }

        // Apply current voice settings before speaking
        ApplyVoiceSettings();

        // Prepare text with SSML markup for pitch control
        std::wstring ssmlText = L"<speak>";
        if (m_currentConfig.pitch != 0.0f) {
            ssmlText += L"<prosody pitch=\"" + std::to_wstring(static_cast<int>(m_currentConfig.pitch * 10)) + L"%\">";
        }
        ssmlText += text;
        if (m_currentConfig.pitch != 0.0f) {
            ssmlText += L"</prosody>";
        }
        ssmlText += L"</speak>";

        // Set speaking flags
        m_bSpeaking.store(true);
        m_bStopRequested.store(false);
        m_currentState = TTSPlaybackState::STATE_PLAYING;

        // Speak the text asynchronously
        HRESULT hr = m_pVoice->Speak(ssmlText.c_str(), SPF_ASYNC, nullptr);

        if (FAILED(hr)) {
            // Reset state on failure
            m_bSpeaking.store(false);
            m_currentState = TTSPlaybackState::STATE_ERROR;
            LogTTSError(hr, L"ISpVoice::Speak (Async)");
            return false;
        }

#if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"TTSManager::PlayAsync() - Text speech started asynchronously");
#endif

        return true;
    }
    catch (const std::exception& e) {
        // Reset state on exception
        m_bSpeaking.store(false);
        m_currentState = TTSPlaybackState::STATE_ERROR;

#if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"TTSManager::PlayAsync() - Exception: %S", e.what());
#endif
        return false;
    }
}

// Pause current speech
bool TTSManager::Pause() {
    // Thread safety lock using ThreadManager
    ThreadLockHelper lock(threadManager, "tts_pause_lock", 2000);

    if (!lock.IsLocked()) {
        #if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"TTSManager::GetCurrentVoiceName() - Failed to acquire get voice lock");
        #endif
        return false;
    }

    #if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_DEBUG, L"TTSManager::Pause() - Pausing current speech");
    #endif

    // Check if initialized
    if (!m_bIsInitialized || !m_pVoice) {
        #if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"TTSManager::Pause() - TTS not initialized");
        #endif
        return false;
    }

    // Check if currently speaking
    if (!m_bSpeaking.load() || m_currentState != TTSPlaybackState::STATE_PLAYING) {
        #if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"TTSManager::Pause() - Not currently speaking");
        #endif
        return false;
    }

    try {
        // Pause the voice
        HRESULT hr = m_pVoice->Pause();
        if (FAILED(hr)) {
            LogTTSError(hr, L"ISpVoice::Pause");
            return false;
        }

        // Update state
        m_currentState = TTSPlaybackState::STATE_PAUSED;

        #if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_INFO, L"TTSManager::Pause() - Speech paused successfully");
        #endif

        return true;
    }
    catch (const std::exception& e) {
        #if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"TTSManager::Pause() - Exception: %S", e.what());
        #endif
        return false;
    }
}

// Resume paused speech
bool TTSManager::Resume() {
    // Thread safety lock using ThreadManager
    ThreadLockHelper lock(threadManager, "tts_resume_lock", 2000);

    if (!lock.IsLocked()) {
        #if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"TTSManager::GetCurrentVoiceName() - Failed to acquire get voice lock");
        #endif
        return false;
    }

    #if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_DEBUG, L"TTSManager::Resume() - Resuming paused speech");
    #endif

    // Check if initialized
    if (!m_bIsInitialized || !m_pVoice) {
#if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"TTSManager::Resume() - TTS not initialized");
#endif
        return false;
    }

    // Check if currently paused
    if (m_currentState != TTSPlaybackState::STATE_PAUSED) {
        #if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_WARNING, L"TTSManager::Resume() - Speech is not paused");
        #endif
        return false;
    }

    try {
        // Resume the voice
        HRESULT hr = m_pVoice->Resume();
        if (FAILED(hr)) {
            LogTTSError(hr, L"ISpVoice::Resume");
            return false;
        }

        // Update state
        m_currentState = TTSPlaybackState::STATE_PLAYING;

        #if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_INFO, L"TTSManager::Resume() - Speech resumed successfully");
        #endif

        return true;
    }
    catch (const std::exception& e) {
        #if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"TTSManager::Resume() - Exception: %S", e.what());
        #endif
        return false;
    }
}

// Stop current speech immediately
bool TTSManager::Stop() {
    // Thread safety lock using ThreadManager
    ThreadLockHelper lock(threadManager, "tts_stop_lock", 2000);

    if (!lock.IsLocked()) {
        #if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"TTSManager::GetCurrentVoiceName() - Failed to acquire get voice lock");
        #endif
        return false;
    }

    #if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_DEBUG, L"TTSManager::Stop() - Stopping current speech");
    #endif

    // Check if initialized
    if (!m_bIsInitialized || !m_pVoice) {
        #if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"TTSManager::Stop() - TTS not initialized");
        #endif
        return false;
    }

    try {
        // Set stop requested flag
        m_bStopRequested.store(true);

        // Purge the voice queue to stop immediately
        HRESULT hr = m_pVoice->Speak(nullptr, SPF_PURGEBEFORESPEAK, nullptr);
        if (FAILED(hr)) {
            LogTTSError(hr, L"ISpVoice::Speak (Stop)");
            // Continue anyway to reset state
        }

        // Reset state
        m_bSpeaking.store(false);
        m_currentState = TTSPlaybackState::STATE_STOPPED;

        #if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_INFO, L"TTSManager::Stop() - Speech stopped successfully");
        #endif

        return true;
    }
    catch (const std::exception& e) {
        // Reset state even on exception
        m_bSpeaking.store(false);
        m_currentState = TTSPlaybackState::STATE_STOPPED;

        #if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"TTSManager::Stop() - Exception: %S", e.what());
        #endif
        return false;
    }
}

// Get current playback state
TTSPlaybackState TTSManager::GetPlaybackState() const {
    return m_currentState;
}

// Check if currently playing
bool TTSManager::IsPlaying() const {
    return (m_currentState == TTSPlaybackState::STATE_PLAYING) && m_bSpeaking.load();
}

// Check if currently paused
bool TTSManager::IsPaused() const {
    return (m_currentState == TTSPlaybackState::STATE_PAUSED);
}

// Get current volume setting
float TTSManager::GetCurrentVolume() const {
    return m_currentConfig.volume;
}

// Get current pitch setting
float TTSManager::GetCurrentPitch() const {
    return m_currentConfig.pitch;
}

// Get current rate setting
float TTSManager::GetCurrentRate() const {
    return m_currentConfig.rate;
}

// Get current speaker channel setting
TTSSpeakerChannel TTSManager::GetCurrentChannel() const {
    return m_currentConfig.channel;
}

// Get list of available voices
std::vector<std::wstring> TTSManager::GetAvailableVoices() {
    // Thread safety lock using ThreadManager
    ThreadLockHelper lock(threadManager, "tts_voices_lock", 3000);

    std::vector<std::wstring> voiceList;

#if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_DEBUG, L"TTSManager::GetAvailableVoices() - Enumerating available voices");
#endif

    if (!lock.IsLocked()) {
#if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"TTSManager::GetAvailableVoices() - Failed to acquire voices lock");
#endif
        return voiceList;
    }

    // Check if initialized
    if (!m_bIsInitialized) {
#if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"TTSManager::GetAvailableVoices() - TTS not initialized");
#endif
        return voiceList;
    }

    try {
        // Create enumerator for voice tokens
        ComPtr<IEnumSpObjectTokens> pEnum;
        HRESULT hr = SpEnumTokens(SPCAT_VOICES, nullptr, nullptr, &pEnum);
        if (FAILED(hr)) {
            LogTTSError(hr, L"SpEnumTokens");
            return voiceList;
        }

        // Get count of available voices
        ULONG ulCount = 0;
        hr = pEnum->GetCount(&ulCount);
        if (FAILED(hr)) {
            LogTTSError(hr, L"IEnumSpObjectTokens::GetCount");
            return voiceList;
        }

        // Enumerate each voice
        for (ULONG i = 0; i < ulCount; i++) {
            ComPtr<ISpObjectToken> pToken;
            hr = pEnum->Next(1, &pToken, nullptr);
            if (SUCCEEDED(hr) && pToken) {
                // Get voice name
                LPWSTR pszValue = nullptr;
                hr = pToken->GetStringValue(nullptr, &pszValue);
                if (SUCCEEDED(hr) && pszValue) {
                    voiceList.push_back(std::wstring(pszValue));
                    CoTaskMemFree(pszValue);
                }
            }
        }

#if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"TTSManager::GetAvailableVoices() - Found %d voices", static_cast<int>(voiceList.size()));
#endif
    }
    catch (const std::exception& e) {
#if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"TTSManager::GetAvailableVoices() - Exception: %S", e.what());
#endif
    }

    return voiceList;
}

// Get current voice name
std::wstring TTSManager::GetCurrentVoiceName() const {
    // Thread safety lock using ThreadManager
    ThreadLockHelper lock(threadManager, "tts_get_voice_lock", 2000);

    if (!lock.IsLocked()) {
#if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"TTSManager::GetCurrentVoiceName() - Failed to acquire get voice lock");
#endif
        return L"";
    }

#if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_DEBUG, L"TTSManager::GetCurrentVoiceName() - Getting current voice name");
#endif

    // Check if initialized
    if (!m_bIsInitialized || !m_pVoice) {
#if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"TTSManager::GetCurrentVoiceName() - TTS not initialized");
#endif
        return L"";
    }

    try {
        // Get current voice token
        ComPtr<ISpObjectToken> pToken;
        HRESULT hr = m_pVoice->GetVoice(&pToken);
        if (FAILED(hr) || !pToken) {
            LogTTSError(hr, L"ISpVoice::GetVoice");
            return L"";
        }

        // Get voice name from token
        LPWSTR pszValue = nullptr;
        hr = pToken->GetStringValue(nullptr, &pszValue);
        if (FAILED(hr) || !pszValue) {
            LogTTSError(hr, L"ISpObjectToken::GetStringValue");
            return L"";
        }

        std::wstring voiceName(pszValue);
        CoTaskMemFree(pszValue);

#if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"TTSManager::GetCurrentVoiceName() - Current voice: %s", voiceName.c_str());
#endif

        return voiceName;
    }
    catch (const std::exception& e) {
#if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"TTSManager::GetCurrentVoiceName() - Exception: %S", e.what());
#endif
        return L"";
    }
}

// Private helper methods implementation

// Initialize COM for SAPI usage
bool TTSManager::InitializeCOM() {
#if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_DEBUG, L"TTSManager::InitializeCOM() - Initializing COM for SAPI");
#endif

    // COM should already be initialized by the main application
    // We just verify it's available
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        LogTTSError(hr, L"CoInitializeEx");
        return false;
    }

#if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"TTSManager::InitializeCOM() - COM initialized successfully");
#endif

    return true;
}

// Create main voice interface
bool TTSManager::CreateVoiceInterface() {
#if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_DEBUG, L"TTSManager::CreateVoiceInterface() - Creating voice interface");
#endif

    try {
        // Create the SAPI voice object
        HRESULT hr = CoCreateInstance(CLSID_SpVoice, nullptr, CLSCTX_ALL, IID_ISpVoice, (void**)&m_pVoice);
        if (FAILED(hr)) {
            LogTTSError(hr, L"CoCreateInstance(CLSID_SpVoice)");
            return false;
        }

#if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"TTSManager::CreateVoiceInterface() - Voice interface created successfully");
#endif

        return true;
    }
    catch (const std::exception& e) {
#if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"TTSManager::CreateVoiceInterface() - Exception: %S", e.what());
#endif
        return false;
    }
}

// Enumerate available voices
bool TTSManager::EnumerateVoices() {
#if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_DEBUG, L"TTSManager::EnumerateVoices() - Enumerating available voices");
#endif

    try {
        // Enumerate voice tokens
        HRESULT hr = SpEnumTokens(SPCAT_VOICES, nullptr, nullptr, &m_pEnumTokens);
        if (FAILED(hr)) {
            LogTTSError(hr, L"SpEnumTokens");
            return false;
        }

        // Get count of available voices
        ULONG ulCount = 0;
        hr = m_pEnumTokens->GetCount(&ulCount);
        if (FAILED(hr)) {
            LogTTSError(hr, L"IEnumSpObjectTokens::GetCount");
            return false;
        }

#if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"TTSManager::EnumerateVoices() - Found %d available voices", ulCount);
#endif

        return true;
    }
    catch (const std::exception& e) {
#if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"TTSManager::EnumerateVoices() - Exception: %S", e.what());
#endif
        return false;
    }
}

// Apply current voice settings to engine
bool TTSManager::ApplyVoiceSettings() {
#if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_DEBUG, L"TTSManager::ApplyVoiceSettings() - Applying voice settings");
#endif

    if (!m_pVoice) {
        return false;
    }

    try {
        // Apply volume setting
        USHORT sapiVolume;
        HRESULT hr = ConvertVolumeToSAPI(m_currentConfig.volume, sapiVolume);
        if (SUCCEEDED(hr)) {
            hr = m_pVoice->SetVolume(sapiVolume);
            if (FAILED(hr)) {
                LogTTSError(hr, L"ISpVoice::SetVolume (ApplySettings)");
            }
        }

        // Apply rate setting
        long sapiRate;
        hr = ConvertRateToSAPI(m_currentConfig.rate, sapiRate);
        if (SUCCEEDED(hr)) {
            hr = m_pVoice->SetRate(sapiRate);
            if (FAILED(hr)) {
                LogTTSError(hr, L"ISpVoice::SetRate (ApplySettings)");
            }
        }

#if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"TTSManager::ApplyVoiceSettings() - Voice settings applied");
#endif

        return true;
    }
    catch (const std::exception& e) {
#if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"TTSManager::ApplyVoiceSettings() - Exception: %S", e.what());
#endif
        return false;
    }
}

// Setup audio output configuration
bool TTSManager::SetupAudioOutput() {
#if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_DEBUG, L"TTSManager::SetupAudioOutput() - Setting up audio output");
#endif

    // Audio output configuration will be handled by the system
    // Channel routing can be implemented with audio effects if needed
    // For now, we'll use default system audio output

#if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"TTSManager::SetupAudioOutput() - Audio output setup completed");
#endif

    return true;
}

// Convert volume from float to SAPI format
HRESULT TTSManager::ConvertVolumeToSAPI(float volume, USHORT& sapiVolume) {
    // Clamp volume to valid range
    volume = std::max(0.0f, std::min(1.0f, volume));

    // Convert to SAPI range (0-100)
    sapiVolume = static_cast<USHORT>(volume * 100.0f);

    return S_OK;
}

// Convert pitch from float to SAPI format
HRESULT TTSManager::ConvertPitchToSAPI(float pitch, long& sapiPitch) {
    // Clamp pitch to valid range
    pitch = std::max(-10.0f, std::min(10.0f, pitch));

    // Convert to SAPI percentage (-100% to +100%)
    sapiPitch = static_cast<long>(pitch * 10.0f);

    return S_OK;
}

// Convert rate from float to SAPI format
HRESULT TTSManager::ConvertRateToSAPI(float rate, long& sapiRate) {
    // Clamp rate to valid range
    rate = std::max(-10.0f, std::min(10.0f, rate));

    // Convert to SAPI range (-10 to +10)
    sapiRate = static_cast<long>(rate);

    return S_OK;
}

// Release all COM interfaces
void TTSManager::ReleaseCOMInterfaces() {
#if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_DEBUG, L"TTSManager::ReleaseCOMInterfaces() - Releasing COM interfaces");
#endif

    try {
        // Release voice interface
        if (m_pVoice) {
            m_pVoice.Reset();
        }

        // Release voice token
        if (m_pVoiceToken) {
            m_pVoiceToken.Reset();
        }

        // Release enumerator
        if (m_pEnumTokens) {
            m_pEnumTokens.Reset();
        }

#if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_INFO, L"TTSManager::ReleaseCOMInterfaces() - COM interfaces released");
#endif
    }
    catch (const std::exception& e) {
#if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_ERROR, L"TTSManager::ReleaseCOMInterfaces() - Exception: %S", e.what());
#endif
    }
}

// Reset internal state variables
void TTSManager::ResetInternalState() {
#if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_DEBUG, L"TTSManager::ResetInternalState() - Resetting internal state");
#endif

    // Reset atomic flags
    m_bSpeaking.store(false);
    m_bStopRequested.store(false);

    // Reset state
    m_currentState = TTSPlaybackState::STATE_STOPPED;

    // Reset configuration to defaults
    m_currentConfig = TTSConfiguration();

#if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"TTSManager::ResetInternalState() - Internal state reset");
#endif
}

// Log TTS-specific errors
void TTSManager::LogTTSError(HRESULT hr, const std::wstring& operation) const {
    std::wstring errorMsg = operation + L" failed with HRESULT: " + HResultToString(hr);

#if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_ERROR, errorMsg);
#endif
}

// Convert HRESULT to readable string
std::wstring TTSManager::HResultToString(HRESULT hr) const {
    _com_error err(hr);
    return std::wstring(err.ErrorMessage());
}

// Additional methods for completeness

// Save TTS configuration
void TTSManager::SaveConfiguration(const TTSConfiguration& config) {
    // Thread safety lock using ThreadManager
    ThreadLockHelper lock(threadManager, "tts_get_voice_lock", 2000);

    if (!lock.IsLocked()) {
#if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"TTSManager::GetCurrentVoiceName() - Failed to acquire get voice lock");
#endif
        return;
    }
    m_currentConfig = config;

#if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"TTSManager::SaveConfiguration() - Configuration saved");
#endif
}

// Load TTS configuration
TTSConfiguration TTSManager::LoadConfiguration() {
#if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"TTSManager::LoadConfiguration() - Configuration loaded");
#endif

    return m_currentConfig;
}

// Reset all settings to defaults
void TTSManager::ResetToDefaults() {
    // Thread safety lock using ThreadManager
    ThreadLockHelper lock(threadManager, "tts_get_voice_lock", 2000);

    if (!lock.IsLocked()) {
#if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_ERROR, L"TTSManager::GetCurrentVoiceName() - Failed to acquire get voice lock");
#endif
        return;
    }

#if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"TTSManager::ResetToDefaults() - Resetting to default settings");
#endif

    // Reset configuration
    m_currentConfig = TTSConfiguration();

    // Apply default settings if initialized
    if (m_bIsInitialized) {
        ApplyVoiceSettings();
    }
}

// Set voice by name
bool TTSManager::SetVoiceByName(const std::wstring& voiceName) {
    // Thread safety lock using ThreadManager
    ThreadLockHelper lock(threadManager, "tts_setvoicebyname_lock", 2000);

    if (!lock.IsLocked()) {
        #if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"TTSManager::GetCurrentVoiceName() - Failed to acquire get voice lock");
        #endif
        return false;
    }

    #if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"TTSManager::SetVoiceByName() - Setting voice to: %s", voiceName.c_str());
    #endif

    if (!m_bIsInitialized || !m_pVoice) {
        #if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"TTSManager::SetVoiceByName() - TTS not initialized");
        #endif
        return false;
    }

    try {
        // Find voice token by name
        ComPtr<IEnumSpObjectTokens> pEnum;
        HRESULT hr = SpEnumTokens(SPCAT_VOICES, nullptr, nullptr, &pEnum);
        if (FAILED(hr)) {
            LogTTSError(hr, L"SpEnumTokens (SetVoiceByName)");
            return false;
        }

        ULONG ulCount = 0;
        hr = pEnum->GetCount(&ulCount);
        if (FAILED(hr)) {
            LogTTSError(hr, L"IEnumSpObjectTokens::GetCount (SetVoiceByName)");
            return false;
        }

        // Search for matching voice
        for (ULONG i = 0; i < ulCount; i++) {
            ComPtr<ISpObjectToken> pToken;
            hr = pEnum->Next(1, &pToken, nullptr);
            if (SUCCEEDED(hr) && pToken) {
                LPWSTR pszValue = nullptr;
                hr = pToken->GetStringValue(nullptr, &pszValue);
                if (SUCCEEDED(hr) && pszValue) {
                    if (voiceName == std::wstring(pszValue)) {
                        // Found matching voice, set it
                        hr = m_pVoice->SetVoice(pToken.Get());
                        CoTaskMemFree(pszValue);

                        if (SUCCEEDED(hr)) {
                            m_currentConfig.voiceName = voiceName;

                            #if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
                                debug.logDebugMessage(LogLevel::LOG_INFO, L"TTSManager::SetVoiceByName() - Voice set successfully to: %s", voiceName.c_str());
                            #endif

                            return true;
                        }
                        else {
                            LogTTSError(hr, L"ISpVoice::SetVoice");
                            return false;
                        }
                    }
                    CoTaskMemFree(pszValue);
                }
            }
        }

        #if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_WARNING, L"TTSManager::SetVoiceByName() - Voice not found: %s", voiceName.c_str());
        #endif

        return false;
    }
    catch (const std::exception& e) {
        #if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"TTSManager::SetVoiceByName() - Exception: %S", e.what());
        #endif
        return false;
    }
}

// Check if specific voice is available
bool TTSManager::IsVoiceAvailable(const std::wstring& voiceName) {
    std::vector<std::wstring> voices = GetAvailableVoices();
    return std::find(voices.begin(), voices.end(), voiceName) != voices.end();
}

// Set voice quality
bool TTSManager::SetVoiceQuality(TTSVoiceQuality quality) {
    // Thread safety lock using ThreadManager
    ThreadLockHelper lock(threadManager, "tts_setvoicequality_lock", 2000);

    if (!lock.IsLocked()) {
        #if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_ERROR, L"TTSManager::GetCurrentVoiceName() - Failed to acquire get voice lock");
        #endif
        return false;
    }

    #if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"TTSManager::SetVoiceQuality() - Setting quality to %d", static_cast<int>(quality));
    #endif

    // Update configuration
    m_currentConfig.quality = quality;

    #if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_INFO, L"TTSManager::SetVoiceQuality() - Quality set to %d", static_cast<int>(quality));
    #endif

    return true;
}

// Update current playback state
void TTSManager::UpdatePlaybackState() {
    if (!m_pVoice) {
        return;
    }

    try {
        SPVOICESTATUS status;
        HRESULT hr = m_pVoice->GetStatus(&status, nullptr);
        if (SUCCEEDED(hr)) {
            if (status.dwRunningState == SPRS_IS_SPEAKING) {
                m_currentState = TTSPlaybackState::STATE_PLAYING;
                m_bSpeaking.store(true);
            }
            else {
                if (m_currentState == TTSPlaybackState::STATE_PLAYING) {
                    m_currentState = TTSPlaybackState::STATE_STOPPED;
                }
                m_bSpeaking.store(false);
            }
        }
    }
    catch (const std::exception& e) {
        #if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_ERROR, L"TTSManager::UpdatePlaybackState() - Exception: %S", e.what());
        #endif
    }
}

#pragma warning(pop)
