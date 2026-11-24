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

#if defined(PLATFORM_WINDOWS)
    #pragma warning(push)
    #pragma warning(disable: 4101)
#endif

#if defined(PLATFORM_WINDOWS)

// Constructor - Initialize member variables to safe defaults
TTSManager::TTSManager() :
    m_bIsInitialized(false),                                 // TTS not initialized yet
    m_bHasCleanedUp(false),                                  // Cleanup not performed yet
    m_currentState(TTSPlaybackState::STATE_STOPPED),         // Start in stopped state
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
        debug.logDiagLevelMessage(LogLevel::LOG_ERROR, L"TTSManager::Initialize() - Failed to acquire initialization lock");
        return false;
    }

#if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"TTSManager::Initialize() - Starting TTS initialization");
#endif

    // Check if already initialized
    if (m_bIsInitialized) {
        debug.logDiagLevelMessage(LogLevel::LOG_WARNING, L"TTSManager::Initialize() - Already initialized");
        return true;
    }

    try {
        // Step 1: Initialize COM for SAPI usage
        if (!InitializeCOM()) {
            debug.logDiagLevelMessage(LogLevel::LOG_ERROR, L"TTSManager::Initialize() - Failed to initialize COM");
            return false;
        }

        // Step 2: Create main voice interface
        if (!CreateVoiceInterface()) {
            debug.logDiagLevelMessage(LogLevel::LOG_ERROR, L"TTSManager::Initialize() - Failed to create voice interface");
            return false;
        }

        // Step 3: Enumerate available voices
        if (!EnumerateVoices()) {
            debug.logDiagLevelMessage(LogLevel::LOG_WARNING, L"TTSManager::Initialize() - Failed to enumerate voices, using default");
            // Continue with default voice if enumeration fails
        }

        // Step 4: Apply default voice settings
        if (!ApplyVoiceSettings()) {
            debug.logDiagLevelMessage(LogLevel::LOG_ERROR, L"TTSManager::Initialize() - Failed to apply voice settings");
            return false;
        }

        // Step 5: Setup audio output configuration
        if (!SetupAudioOutput()) {
            debug.logDiagLevelMessage(LogLevel::LOG_WARNING, L"TTSManager::Initialize() - Failed to setup audio output, using defaults");
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
        debug.logDiagMessage(LogLevel::LOG_TERMINATION, L"TTSManager::Initialize() - Exception caught: %S", e.what());

        // Cleanup partial initialization
        ReleaseCOMInterfaces();
        return false;
    }
    catch (...) {
        // Handle unknown exceptions
        debug.logDiagLevelMessage(LogLevel::LOG_TERMINATION, L"TTSManager::Initialize() - Unknown exception caught");

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
        debug.logDiagLevelMessage(LogLevel::LOG_WARNING, L"TTSManager::CleanUp() - Failed to acquire cleanup lock, proceeding anyway");
    }

#if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_INFO, L"TTSManager::CleanUp() - Starting TTS cleanup");
#endif

    // Check if already cleaned up
    if (m_bHasCleanedUp) {
        debug.logDiagLevelMessage(LogLevel::LOG_WARNING, L"TTSManager::CleanUp() - Already cleaned up");
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
        debug.logDiagMessage(LogLevel::LOG_ERROR, L"TTSManager::CleanUp() - Exception during cleanup: %S", e.what());
    }
    catch (...) {
        // Handle unknown exceptions during cleanup
        debug.logDiagLevelMessage(LogLevel::LOG_ERROR, L"TTSManager::CleanUp() - Unknown exception during cleanup");
    }
}

// Set voice volume level
bool TTSManager::SetVoiceVolume(float volume) {
    // Thread safety lock using ThreadManager
    ThreadLockHelper lock(threadManager, "tts_volume_lock", 2000);
    if (!lock.IsLocked()) {
        debug.logDiagLevelMessage(LogLevel::LOG_ERROR, L"TTSManager::SetVoiceVolume() - Failed to acquire volume lock");
        return false;
    }

#if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"TTSManager::SetVoiceVolume() - Setting volume to %.2f", volume);
#endif

    // Clamp volume to valid range (0.0 to 1.0).  SAPI's volume is a percentage (0 - 100) and cannot amplify, so anything above 1.0 is simply the maximum.
    volume = std::max(0.0f, std::min(1.0f, volume));

    // REMEMBER the requested volume before anything can make us bail out.  It used to be stored only after the voice had accepted it, so a request made while the engine was not
    // ready (not initialised yet, or the engine briefly busy) was thrown away and ApplyVoiceSettings() - which runs before every utterance - kept speaking at the stale default
    // (0.8): the volume setting seemed to be ignored.  Now the setting always reaches the next utterance, whatever state the engine was in when it was changed.
    m_currentConfig.volume = volume;

    // Check if initialized
    if (!m_bIsInitialized || !m_pVoice) {
        debug.logDiagLevelMessage(LogLevel::LOG_WARNING, L"TTSManager::SetVoiceVolume() - TTS not initialized yet: the volume is remembered and applied when it is");
        return false;
    }

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
        debug.logDiagMessage(LogLevel::LOG_ERROR, L"TTSManager::SetVoiceVolume() - Exception: %S", e.what());
        return false;
    }
}

// Set voice pitch level
bool TTSManager::SetVoicePitch(float pitch) {
    // Thread safety lock using ThreadManager
    ThreadLockHelper lock(threadManager, "tts_pitch_lock", 2000);
    if (!lock.IsLocked()) {
        debug.logDiagLevelMessage(LogLevel::LOG_ERROR, L"TTSManager::SetVoicePitch() - Failed to acquire pitch lock");
        return false;
    }

#if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"TTSManager::SetVoicePitch() - Setting pitch to %.2f", pitch);
#endif

    // Check if initialized
    if (!m_bIsInitialized || !m_pVoice) {
        debug.logDiagLevelMessage(LogLevel::LOG_ERROR, L"TTSManager::SetVoicePitch() - TTS not initialized");
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
        debug.logDiagMessage(LogLevel::LOG_ERROR, L"TTSManager::SetVoicePitch() - Exception: %S", e.what());
        return false;
    }
}

// Set speech rate
bool TTSManager::SetVoiceRate(float rate) {
    // Thread safety lock using ThreadManager
    ThreadLockHelper lock(threadManager, "tts_rate_lock", 2000);
    if (!lock.IsLocked()) {
        debug.logDiagLevelMessage(LogLevel::LOG_ERROR, L"TTSManager::SetVoiceRate() - Failed to acquire rate lock");
        return false;
    }

#if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
    debug.logDebugMessage(LogLevel::LOG_DEBUG, L"TTSManager::SetVoiceRate() - Setting rate to %.2f", rate);
#endif

    // Check if initialized
    if (!m_bIsInitialized || !m_pVoice) {
        debug.logDiagLevelMessage(LogLevel::LOG_ERROR, L"TTSManager::SetVoiceRate() - TTS not initialized");
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
        debug.logDiagMessage(LogLevel::LOG_ERROR, L"TTSManager::SetVoiceRate() - Exception: %S", e.what());
        return false;
    }
}

// Set speaker channel for audio output
bool TTSManager::SetSpeakerChannel(TTSSpeakerChannel channel) {
    // Thread safety lock using ThreadManager
    ThreadLockHelper lock(threadManager, "tts_channel_lock", 2000);
    if (!lock.IsLocked()) {
        debug.logDiagLevelMessage(LogLevel::LOG_ERROR, L"TTSManager::SetSpeakerChannel() - Failed to acquire channel lock");
        return false;
    }

    #if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"TTSManager::SetSpeakerChannel() - Setting channel to %d", static_cast<int>(channel));
    #endif

    // Check if initialized
    if (!m_bIsInitialized || !m_pVoice) {
        debug.logDiagLevelMessage(LogLevel::LOG_ERROR, L"TTSManager::SetSpeakerChannel() - TTS not initialized");
        return false;
    }

    try {
        // Update configuration (actual audio routing will be handled during playback)
        m_currentConfig.channel = channel;

        // Setup audio output with new channel configuration
        if (!SetupAudioOutput()) {
            debug.logDiagLevelMessage(LogLevel::LOG_WARNING, L"TTSManager::SetSpeakerChannel() - Failed to update audio output");
            return false;
        }

        #if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
            debug.logDebugMessage(LogLevel::LOG_INFO, L"TTSManager::SetSpeakerChannel() - Channel set successfully to %d", static_cast<int>(channel));
        #endif

        return true;
    }
    catch (const std::exception& e) {
        debug.logDiagMessage(LogLevel::LOG_ERROR, L"TTSManager::SetSpeakerChannel() - Exception: %S", e.what());
        return false;
    }
}

// Play text synchronously
bool TTSManager::Play(const std::wstring& text) {
    // Thread safety lock using ThreadManager
    ThreadLockHelper lock(threadManager, "tts_play_lock", 3000);
    if (!lock.IsLocked()) {
        debug.logDiagLevelMessage(LogLevel::LOG_ERROR, L"TTSManager::Play() - Failed to acquire play lock");
        return false;
    }

    #if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"TTSManager::Play() - Speaking text: %.50s...", text.c_str());
    #endif

    // Check if initialized
    if (!m_bIsInitialized || !m_pVoice) {
        debug.logDiagLevelMessage(LogLevel::LOG_ERROR, L"TTSManager::Play() - TTS not initialized");
        return false;
    }

    // Check if text is empty
    if (text.empty()) {
        debug.logDiagLevelMessage(LogLevel::LOG_WARNING, L"TTSManager::Play() - Empty text provided");
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

        debug.logDiagMessage(LogLevel::LOG_ERROR, L"TTSManager::Play() - Exception: %S", e.what());
        return false;
    }
}

// Play text asynchronously
bool TTSManager::PlayAsync(const std::wstring& text) {
    // Thread safety lock using ThreadManager
    ThreadLockHelper lock(threadManager, "tts_playasync_lock", 2000);

    if (!lock.IsLocked()) {
        debug.logDiagLevelMessage(LogLevel::LOG_ERROR, L"TTSManager::GetCurrentVoiceName() - Failed to acquire get voice lock");
        return false;
    }

    #if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"TTSManager::PlayAsync() - Speaking text asynchronously: %.50s...", text.c_str());
    #endif

    // Check if initialized
    if (!m_bIsInitialized || !m_pVoice) {
        debug.logDiagLevelMessage(LogLevel::LOG_ERROR, L"TTSManager::PlayAsync() - TTS not initialized");
        return false;
    }

    // Check if text is empty
    if (text.empty()) {
        debug.logDiagLevelMessage(LogLevel::LOG_WARNING, L"TTSManager::PlayAsync() - Empty text provided");
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

        debug.logDiagMessage(LogLevel::LOG_ERROR, L"TTSManager::PlayAsync() - Exception: %S", e.what());
        return false;
    }
}

// Pause current speech
bool TTSManager::Pause() {
    // Thread safety lock using ThreadManager
    ThreadLockHelper lock(threadManager, "tts_pause_lock", 2000);

    if (!lock.IsLocked()) {
        debug.logDiagLevelMessage(LogLevel::LOG_ERROR, L"TTSManager::GetCurrentVoiceName() - Failed to acquire get voice lock");
        return false;
    }

    #if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_DEBUG, L"TTSManager::Pause() - Pausing current speech");
    #endif

    // Check if initialized
    if (!m_bIsInitialized || !m_pVoice) {
        debug.logDiagLevelMessage(LogLevel::LOG_ERROR, L"TTSManager::Pause() - TTS not initialized");
        return false;
    }

    // Check if currently speaking
    if (!m_bSpeaking.load() || m_currentState != TTSPlaybackState::STATE_PLAYING) {
        debug.logDiagLevelMessage(LogLevel::LOG_WARNING, L"TTSManager::Pause() - Not currently speaking");
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
        debug.logDiagMessage(LogLevel::LOG_ERROR, L"TTSManager::Pause() - Exception: %S", e.what());
        return false;
    }
}

// Resume paused speech
bool TTSManager::Resume() {
    // Thread safety lock using ThreadManager
    ThreadLockHelper lock(threadManager, "tts_resume_lock", 2000);

    if (!lock.IsLocked()) {
        debug.logDiagLevelMessage(LogLevel::LOG_ERROR, L"TTSManager::GetCurrentVoiceName() - Failed to acquire get voice lock");
        return false;
    }

    #if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_DEBUG, L"TTSManager::Resume() - Resuming paused speech");
    #endif

    // Check if initialized
    if (!m_bIsInitialized || !m_pVoice) {
        debug.logDiagLevelMessage(LogLevel::LOG_ERROR, L"TTSManager::Resume() - TTS not initialized");
        return false;
    }

    // Check if currently paused
    if (m_currentState != TTSPlaybackState::STATE_PAUSED) {
        debug.logDiagLevelMessage(LogLevel::LOG_WARNING, L"TTSManager::Resume() - Speech is not paused");
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
        debug.logDiagMessage(LogLevel::LOG_ERROR, L"TTSManager::Resume() - Exception: %S", e.what());
        return false;
    }
}

// Stop current speech immediately
bool TTSManager::Stop() {
    // Thread safety lock using ThreadManager
    ThreadLockHelper lock(threadManager, "tts_stop_lock", 2000);

    if (!lock.IsLocked()) {
        debug.logDiagLevelMessage(LogLevel::LOG_ERROR, L"TTSManager::GetCurrentVoiceName() - Failed to acquire get voice lock");
        return false;
    }

    #if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
        debug.logLevelMessage(LogLevel::LOG_DEBUG, L"TTSManager::Stop() - Stopping current speech");
    #endif

    // Check if initialized
    if (!m_bIsInitialized || !m_pVoice) {
        debug.logDiagLevelMessage(LogLevel::LOG_ERROR, L"TTSManager::Stop() - TTS not initialized");
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

        debug.logDiagMessage(LogLevel::LOG_ERROR, L"TTSManager::Stop() - Exception: %S", e.what());
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
        debug.logDiagLevelMessage(LogLevel::LOG_ERROR, L"TTSManager::GetAvailableVoices() - Failed to acquire voices lock");
        return voiceList;
    }

    // Check if initialized
    if (!m_bIsInitialized) {
        debug.logDiagLevelMessage(LogLevel::LOG_ERROR, L"TTSManager::GetAvailableVoices() - TTS not initialized");
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
        debug.logDiagMessage(LogLevel::LOG_ERROR, L"TTSManager::GetAvailableVoices() - Exception: %S", e.what());
    }

    return voiceList;
}

// Get current voice name
std::wstring TTSManager::GetCurrentVoiceName() const {
    // Thread safety lock using ThreadManager
    ThreadLockHelper lock(threadManager, "tts_get_voice_lock", 2000);

    if (!lock.IsLocked()) {
        debug.logDiagLevelMessage(LogLevel::LOG_ERROR, L"TTSManager::GetCurrentVoiceName() - Failed to acquire get voice lock");
        return L"";
    }

#if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
    debug.logLevelMessage(LogLevel::LOG_DEBUG, L"TTSManager::GetCurrentVoiceName() - Getting current voice name");
#endif

    // Check if initialized
    if (!m_bIsInitialized || !m_pVoice) {
        debug.logDiagLevelMessage(LogLevel::LOG_ERROR, L"TTSManager::GetCurrentVoiceName() - TTS not initialized");
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
        debug.logDiagMessage(LogLevel::LOG_ERROR, L"TTSManager::GetCurrentVoiceName() - Exception: %S", e.what());
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
        debug.logDiagMessage(LogLevel::LOG_ERROR, L"TTSManager::CreateVoiceInterface() - Exception: %S", e.what());
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
        debug.logDiagMessage(LogLevel::LOG_ERROR, L"TTSManager::EnumerateVoices() - Exception: %S", e.what());
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
        debug.logDiagMessage(LogLevel::LOG_ERROR, L"TTSManager::ApplyVoiceSettings() - Exception: %S", e.what());
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
        debug.logDiagMessage(LogLevel::LOG_ERROR, L"TTSManager::ReleaseCOMInterfaces() - Exception: %S", e.what());
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

    debug.logDiagLevelMessage(LogLevel::LOG_ERROR, errorMsg);
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
        debug.logDiagLevelMessage(LogLevel::LOG_ERROR, L"TTSManager::GetCurrentVoiceName() - Failed to acquire get voice lock");
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
        debug.logDiagLevelMessage(LogLevel::LOG_ERROR, L"TTSManager::GetCurrentVoiceName() - Failed to acquire get voice lock");
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
        debug.logDiagLevelMessage(LogLevel::LOG_ERROR, L"TTSManager::GetCurrentVoiceName() - Failed to acquire get voice lock");
        return false;
    }

    #if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
        debug.logDebugMessage(LogLevel::LOG_DEBUG, L"TTSManager::SetVoiceByName() - Setting voice to: %s", voiceName.c_str());
    #endif

    if (!m_bIsInitialized || !m_pVoice) {
        debug.logDiagLevelMessage(LogLevel::LOG_ERROR, L"TTSManager::SetVoiceByName() - TTS not initialized");
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

        debug.logDiagMessage(LogLevel::LOG_WARNING, L"TTSManager::SetVoiceByName() - Voice not found: %s", voiceName.c_str());

        return false;
    }
    catch (const std::exception& e) {
        debug.logDiagMessage(LogLevel::LOG_ERROR, L"TTSManager::SetVoiceByName() - Exception: %S", e.what());
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
        debug.logDiagLevelMessage(LogLevel::LOG_ERROR, L"TTSManager::GetCurrentVoiceName() - Failed to acquire get voice lock");
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
        debug.logDiagMessage(LogLevel::LOG_ERROR, L"TTSManager::UpdatePlaybackState() - Exception: %S", e.what());
    }
}

#pragma warning(pop)

#elif defined(PLATFORM_ANDROID)
// ============================================================================
// Android TextToSpeech backend via JNI (Phase D step 3)
//
// Text-to-speech via the platform android.speech.tts.TextToSpeech, driven
// through JNI from the Java helper com.ultimanium.cpge.CpgeTts
// (Android/app/src/main/java/com/ultimanium/cpge/CpgeTts.java). Android
// TTS has no NDK API, so a small Java class is unavoidable — this is the
// real backend the TTS rule asks for ("use it where the platform supports
// it"), not a stub: until this section landed, Initialize returned false
// and the state was ERROR, exactly like a header-less Linux build.
//
// How it works: Initialize() takes the JavaVM from the NativeActivity
// (AndroidApp::GetApp()->activity->vm), loads CpgeTts through the
// activity's own class loader (FindClass from an attached native thread
// uses the system class loader, which cannot see app classes), and calls
// CpgeTts.init(activity). TextToSpeech construction is async, so
// Initialize waits up to 2 s for isReady(). Speech is async too: Play()
// speaks then waits on a condition until the utterance callback arrives
// (or Stop() is called); PlayAsync() returns immediately and the callback
// clears the speaking state. Each utterance gets a unique ID
// ("cpge-tts-<n>") so a late callback for a flushed utterance cannot
// clear the state of the utterance that replaced it.
//
// Engine -10..+10 rate/pitch map onto the platform scales (setSpeechRate /
// setPitch are 1.0 = normal): rate 1.0 + rate*0.1 (clamped 0.25..4.0),
// pitch 1.0 + pitch*0.05 (clamped 0.5..2.0). Volume 0.0..1.0 is passed
// per-utterance via the KEY_PARAM_VOLUME bundle (best-effort: some TTS
// engines ignore it). There is no speaker-channel or pause API in
// Android TTS: SetSpeakerChannel / SetVoiceQuality store the config and
// return true with no audible effect; Pause / Resume return false.
//
// Voices are BCP-47 language tags (e.g. "en-US"): GetAvailableVoices
// returns the tags TextToSpeech reports, SetVoiceByName sets the language
// by tag. No device exists to hear the result yet — this is compile/link
// verified only, like the rest of the Android port.
// ============================================================================

#include "AndroidApp.h"
#include <jni.h>
#include <android_native_app_glue.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>

namespace {

// All JNI state for the Android backend. Owned through the opaque void*
// TTSManager::m_android so jni.h never leaks into the header.
struct TTSAndroid
{
    JavaVM* jvm = nullptr;              // from the NativeActivity (never owned)
    jclass ttsClass = nullptr;          // global ref to com.ultimanium.cpge.CpgeTts
    jmethodID mInit = nullptr;          // (Landroid/content/Context;)Z
    jmethodID mIsReady = nullptr;       // ()Z
    jmethodID mSpeak = nullptr;         // (Ljava/lang/String;Ljava/lang/String;)Z
    jmethodID mStop = nullptr;          // ()V
    jmethodID mShutdown = nullptr;      // ()V
    jmethodID mIsSpeaking = nullptr;    // ()Z
    jmethodID mSetRate = nullptr;       // (F)V
    jmethodID mSetPitch = nullptr;      // (F)V
    jmethodID mSetVolume = nullptr;     // (F)V
    jmethodID mSetLanguage = nullptr;   // (Ljava/lang/String;)Z
    jmethodID mGetVoiceNames = nullptr; // ()[Ljava/lang/String;
    jmethodID mGetVoiceTag = nullptr;   // ()Ljava/lang/String;
    // Completion of the current utterance: the Java listener reports back
    // through nativeOnUtteranceDone, which sets utteranceDone and notifies
    // doneCv. utteranceSeq numbers utterances; a callback is ignored
    // unless its ID matches currentUtteranceId (a flushed utterance's late
    // callback must not clear the utterance that replaced it).
    std::atomic<bool> utteranceDone{ false };
    std::mutex doneMutex;
    std::condition_variable doneCv;
    uint64_t utteranceSeq = 0;
    std::string currentUtteranceId;
    std::string voiceTag = "en-US";     // last voice set (BCP-47 tag)
};

// The single TTSManager in the engine, for the utterance callback. Set in
// Initialize(), cleared in CleanUp() (same pattern as eSpeak's g_activeTTS).
TTSManager* g_activeTTSAndroid = nullptr;

// Convert a wide string (UTF-32 wchar_t on Android, like Linux) to UTF-8
// for the JNI calls. (Same manual conversion as the eSpeak backend's
// WideToUtf8, which lives in the HAS_ESPEAK namespace and is not visible
// here; std::wstring_convert is deprecated in C++17.)
std::string AndroidWideToUtf8(const std::wstring& wide)
{
    std::string out;
    out.reserve(wide.size());
    for (wchar_t wc : wide)
    {
        const uint32_t cp = static_cast<uint32_t>(wc);
        if (cp < 0x80)
            out.push_back(static_cast<char>(cp));
        else if (cp < 0x800)
        {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
        else if (cp < 0x10000)
        {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
        else
        {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    return out;
}

// BCP-47 voice tags are ASCII, so a plain widening is enough for the
// getVoiceNames results (no general UTF-8 decoding needed here).
std::wstring AndroidUtf8ToWide(const std::string& narrow)
{
    std::wstring out;
    out.reserve(narrow.size());
    for (char c : narrow)
        out.push_back(static_cast<wchar_t>(static_cast<unsigned char>(c)));
    return out;
}

// Map the engine's -10..+10 rate scale onto Android's setSpeechRate
// (1.0 = normal; the platform accepts roughly 0.25..4.0).
float AndroidRateToSpeechRate(float rate)
{
    const float r = 1.0f + rate * 0.1f;
    return r < 0.25f ? 0.25f : (r > 4.0f ? 4.0f : r);
}

// Map the engine's -10..+10 pitch scale onto Android's setPitch
// (1.0 = normal; must stay above 0, usable range roughly 0.5..2.0).
float AndroidPitchToSpeechPitch(float pitch)
{
    const float p = 1.0f + pitch * 0.05f;
    return p < 0.5f ? 0.5f : (p > 2.0f ? 2.0f : p);
}

// Attach the calling thread to the JVM when it is not attached already;
// detach on destruction only when this helper did the attaching (a
// thread the glue attached must not be detached by us).
struct JniEnvScope
{
    JavaVM* jvm = nullptr;
    JNIEnv* env = nullptr;
    bool attached = false;

    explicit JniEnvScope(JavaVM* vm) : jvm(vm)
    {
        if (!jvm)
            return;
        if (jvm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) == JNI_EDETACHED)
        {
            if (jvm->AttachCurrentThread(&env, nullptr) == JNI_OK)
                attached = true;
            else
                env = nullptr;
        }
    }
    ~JniEnvScope()
    {
        if (attached && jvm)
            jvm->DetachCurrentThread();
    }
    bool ok() const { return env != nullptr; }
};

// Log a pending JNI exception and clear it, so one failed call cannot
// poison the next one. Returns false when an exception was pending.
bool CheckJni(JNIEnv* env, const wchar_t* what)
{
    if (!env->ExceptionCheck())
        return true;
    env->ExceptionDescribe();
    env->ExceptionClear();
    debug.logDiagLevelMessage(LogLevel::LOG_WARNING,
        std::wstring(L"TTSManager (Android JNI): exception during ") + what);
    return false;
}

// Call a CpgeTts static ()V method. False when there is no JVM, the
// thread cannot attach, or the call raised (logged and cleared).
void AndroidCallVoid(TTSAndroid* at, jmethodID method, const wchar_t* what)
{
    if (!at || !at->jvm || !method)
        return;
    JniEnvScope jni(at->jvm);
    if (!jni.ok())
        return;
    jni.env->CallStaticVoidMethod(at->ttsClass, method);
    CheckJni(jni.env, what);
}

// Call a CpgeTts static (F)V method with one float argument.
void AndroidCallFloat(TTSAndroid* at, jmethodID method, float value, const wchar_t* what)
{
    if (!at || !at->jvm || !method)
        return;
    JniEnvScope jni(at->jvm);
    if (!jni.ok())
        return;
    jni.env->CallStaticVoidMethod(at->ttsClass, method, value);
    CheckJni(jni.env, what);
}

// Stop the Java engine and wake any Play() waiting on the current
// utterance. Used by Play/PlayAsync before starting a new utterance,
// and by Stop() / CleanUp().
void AndroidStopUtterance(TTSAndroid* at)
{
    if (!at)
        return;
    AndroidCallVoid(at, at->mStop, L"CpgeTts.stop");
    {
        std::lock_guard<std::mutex> guard(at->doneMutex);
        at->utteranceDone.store(true);
        at->currentUtteranceId.clear();
    }
    at->doneCv.notify_all();
}

} // namespace

// JNI callback from CpgeTts (UtteranceProgressListener): the utterance
// finished, failed, or was stopped. Wakes a waiting Play() and clears
// the speaking state — unless the callback belongs to an utterance that
// was already flushed by a newer speak (stale IDs are ignored).
extern "C" JNIEXPORT void JNICALL
Java_com_ultimanium_cpge_CpgeTts_nativeOnUtteranceDone(JNIEnv* env, jclass, jstring utteranceId)
{
    TTSManager* self = g_activeTTSAndroid;
    if (!self)
        return;
    std::string id;
    if (utteranceId)
    {
        const char* utf = env->GetStringUTFChars(utteranceId, nullptr);
        if (utf)
        {
            id = utf;
            env->ReleaseStringUTFChars(utteranceId, utf);
        }
    }
    self->OnAndroidUtteranceDone(id);
}

void TTSManager::OnAndroidUtteranceDone(const std::string& utteranceId)
{
    TTSAndroid* at = static_cast<TTSAndroid*>(m_android);
    if (!at)
        return;
    {
        std::lock_guard<std::mutex> guard(at->doneMutex);
        // A late callback for an utterance that was flushed by a newer
        // speak must not clear the newer utterance's state.
        if (!utteranceId.empty() && utteranceId != at->currentUtteranceId)
            return;
        at->utteranceDone.store(true);
        at->currentUtteranceId.clear();
    }
    at->doneCv.notify_all();
    // Only clear the speaking state when this callback ends the utterance
    // the manager thinks is playing; Stop() may already have moved on.
    if (m_currentState == TTSPlaybackState::STATE_PLAYING)
    {
        m_bSpeaking.store(false);
        m_currentState = TTSPlaybackState::STATE_STOPPED;
    }
}

TTSManager::TTSManager() :
    m_bIsInitialized(false),
    m_bHasCleanedUp(false),
    m_currentState(TTSPlaybackState::STATE_STOPPED),
    m_bSpeaking(false),
    m_bStopRequested(false)
{
    m_currentConfig = TTSConfiguration();
}

TTSManager::~TTSManager() {
    if (!m_bHasCleanedUp) {
        CleanUp();
    }
}

bool TTSManager::Initialize() {
    ThreadLockHelper lock(threadManager, "tts_init_lock", 5000);
    if (!lock.IsLocked()) {
        debug.logDiagLevelMessage(LogLevel::LOG_ERROR, L"TTSManager::Initialize() - Failed to acquire initialization lock");
        return false;
    }
    if (m_bIsInitialized) {
        debug.logDiagLevelMessage(LogLevel::LOG_WARNING, L"TTSManager::Initialize() - Already initialized");
        return true;
    }
    // The NativeActivity owns the JavaVM; without android_main having run
    // there is no JVM to talk to (a host-process test, for example).
    android_app* app = AndroidApp::GetApp();
    if (!app || !app->activity || !app->activity->vm) {
        debug.logDiagLevelMessage(LogLevel::LOG_ERROR, L"TTSManager::Initialize() - No Android activity / JavaVM (android_main has not run)");
        m_currentState = TTSPlaybackState::STATE_ERROR;
        return false;
    }
    TTSAndroid* at = new TTSAndroid();
    at->jvm = app->activity->vm;
    JniEnvScope jni(at->jvm);
    // Every failure below unwinds the same way: drop the global ref (if
    // one was made), delete the state, leave the manager in ERROR so
    // main.cpp treats TTS as unavailable instead of failing the engine.
    auto fail = [&](const wchar_t* message) -> bool {
        if (at) {
            if (at->ttsClass && jni.env)
                jni.env->DeleteGlobalRef(at->ttsClass);
            delete at;
            at = nullptr;
        }
        debug.logDiagLevelMessage(LogLevel::LOG_ERROR, message);
        m_currentState = TTSPlaybackState::STATE_ERROR;
        return false;
    };
    if (!jni.ok())
        return fail(L"TTSManager::Initialize() - Could not attach to the JVM");
    JNIEnv* env = jni.env;
    // FindClass from an attached native thread uses the system class
    // loader, which cannot see the app's classes — load CpgeTts through
    // the activity's own class loader instead.
    jobject activity = app->activity->clazz;
    jclass activityClass = env->GetObjectClass(activity);
    jmethodID getClassLoader = env->GetMethodID(activityClass, "getClassLoader", "()Ljava/lang/ClassLoader;");
    if (!getClassLoader || !CheckJni(env, L"getClassLoader lookup"))
        return fail(L"TTSManager::Initialize() - Could not find Activity.getClassLoader");
    jobject loader = env->CallObjectMethod(activity, getClassLoader);
    if (!loader || !CheckJni(env, L"getClassLoader call"))
        return fail(L"TTSManager::Initialize() - Could not get the activity class loader");
    jclass loaderClass = env->GetObjectClass(loader);
    jmethodID loadClass = env->GetMethodID(loaderClass, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
    if (!loadClass || !CheckJni(env, L"loadClass lookup"))
        return fail(L"TTSManager::Initialize() - Could not find ClassLoader.loadClass");
    jstring className = env->NewStringUTF("com.ultimanium.cpge.CpgeTts");
    jclass localClass = static_cast<jclass>(env->CallObjectMethod(loader, loadClass, className));
    if (!localClass || !CheckJni(env, L"CpgeTts loadClass"))
        return fail(L"TTSManager::Initialize() - CpgeTts class not found (is classes.dex in the APK?)");
    at->ttsClass = static_cast<jclass>(env->NewGlobalRef(localClass));
    // Cache the static method IDs (a missing one is a build mismatch
    // between the Java helper and this file — fail loudly, not silently).
    at->mInit = env->GetStaticMethodID(at->ttsClass, "init", "(Landroid/content/Context;)Z");
    at->mIsReady = env->GetStaticMethodID(at->ttsClass, "isReady", "()Z");
    at->mSpeak = env->GetStaticMethodID(at->ttsClass, "speak", "(Ljava/lang/String;Ljava/lang/String;)Z");
    at->mStop = env->GetStaticMethodID(at->ttsClass, "stop", "()V");
    at->mShutdown = env->GetStaticMethodID(at->ttsClass, "shutdown", "()V");
    at->mIsSpeaking = env->GetStaticMethodID(at->ttsClass, "isSpeaking", "()Z");
    at->mSetRate = env->GetStaticMethodID(at->ttsClass, "setRate", "(F)V");
    at->mSetPitch = env->GetStaticMethodID(at->ttsClass, "setPitch", "(F)V");
    at->mSetVolume = env->GetStaticMethodID(at->ttsClass, "setVolume", "(F)V");
    at->mSetLanguage = env->GetStaticMethodID(at->ttsClass, "setLanguage", "(Ljava/lang/String;)Z");
    at->mGetVoiceNames = env->GetStaticMethodID(at->ttsClass, "getVoiceNames", "()[Ljava/lang/String;");
    at->mGetVoiceTag = env->GetStaticMethodID(at->ttsClass, "getVoiceTag", "()Ljava/lang/String;");
    if (!at->mInit || !at->mIsReady || !at->mSpeak || !at->mStop || !at->mShutdown ||
        !at->mIsSpeaking || !at->mSetRate || !at->mSetPitch || !at->mSetVolume ||
        !at->mSetLanguage || !at->mGetVoiceNames || !at->mGetVoiceTag ||
        !CheckJni(env, L"CpgeTts method lookup"))
        return fail(L"TTSManager::Initialize() - CpgeTts method lookup failed (Java/native mismatch)");
    if (env->CallStaticBooleanMethod(at->ttsClass, at->mInit, activity) != JNI_TRUE ||
        !CheckJni(env, L"CpgeTts.init"))
        return fail(L"TTSManager::Initialize() - CpgeTts.init failed");
    // TextToSpeech construction is async: wait up to 2 s for onInit.
    // A device without TTS data never becomes ready; returning false
    // keeps TTS disabled instead of dropping speech silently.
    bool ready = false;
    for (int i = 0; i < 40; ++i) {
        if (env->CallStaticBooleanMethod(at->ttsClass, at->mIsReady) == JNI_TRUE &&
            CheckJni(env, L"CpgeTts.isReady")) {
            ready = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    if (!ready)
        return fail(L"TTSManager::Initialize() - Android TextToSpeech engine did not become ready (missing TTS data?)");
    m_android = at;
    g_activeTTSAndroid = this;
    // Apply the stored configuration (defaults on first use).
    SetVoiceVolume(m_currentConfig.volume);
    SetVoicePitch(m_currentConfig.pitch);
    SetVoiceRate(m_currentConfig.rate);
    if (!m_currentConfig.voiceName.empty())
        SetVoiceByName(m_currentConfig.voiceName);
    m_bIsInitialized = true;
    m_bHasCleanedUp = false;
    m_currentState = TTSPlaybackState::STATE_STOPPED;
    debug.logLevelMessage(LogLevel::LOG_INFO, L"TTSManager::Initialize() - Android TextToSpeech initialised");
    return true;
}

void TTSManager::CleanUp() {
    ThreadLockHelper lock(threadManager, "tts_cleanup_lock", 5000);
    if (!m_bHasCleanedUp) {
        Stop();
        TTSAndroid* at = static_cast<TTSAndroid*>(m_android);
        if (at) {
            JniEnvScope jni(at->jvm);
            if (jni.ok()) {
                jni.env->CallStaticVoidMethod(at->ttsClass, at->mShutdown);
                CheckJni(jni.env, L"CpgeTts.shutdown");
                jni.env->DeleteGlobalRef(at->ttsClass);
            }
            delete at;
            m_android = nullptr;
        }
        if (g_activeTTSAndroid == this)
            g_activeTTSAndroid = nullptr;
    }
    m_bIsInitialized = false;
    m_bHasCleanedUp = true;
    m_currentState = TTSPlaybackState::STATE_STOPPED;
    m_bSpeaking.store(false);
    m_bStopRequested.store(false);
}

bool TTSManager::SetVoiceVolume(float volume) {
    m_currentConfig.volume = volume < 0.0f ? 0.0f : (volume > 1.0f ? 1.0f : volume);
    if (m_bIsInitialized) {
        TTSAndroid* at = static_cast<TTSAndroid*>(m_android);
        if (at)
            AndroidCallFloat(at, at->mSetVolume, m_currentConfig.volume, L"CpgeTts.setVolume");
    }
    return m_bIsInitialized;
}

bool TTSManager::SetVoicePitch(float pitch) {
    m_currentConfig.pitch = pitch < -10.0f ? -10.0f : (pitch > 10.0f ? 10.0f : pitch);
    if (m_bIsInitialized) {
        TTSAndroid* at = static_cast<TTSAndroid*>(m_android);
        if (at)
            AndroidCallFloat(at, at->mSetPitch, AndroidPitchToSpeechPitch(m_currentConfig.pitch), L"CpgeTts.setPitch");
    }
    return m_bIsInitialized;
}

bool TTSManager::SetVoiceRate(float rate) {
    m_currentConfig.rate = rate < -10.0f ? -10.0f : (rate > 10.0f ? 10.0f : rate);
    if (m_bIsInitialized) {
        TTSAndroid* at = static_cast<TTSAndroid*>(m_android);
        if (at)
            AndroidCallFloat(at, at->mSetRate, AndroidRateToSpeechRate(m_currentConfig.rate), L"CpgeTts.setRate");
    }
    return m_bIsInitialized;
}

bool TTSManager::SetSpeakerChannel(TTSSpeakerChannel channel) {
    m_currentConfig.channel = channel;
    // Android TTS has no speaker-channel API: the setting is stored for
    // API compatibility and has no audible effect.
    return m_bIsInitialized;
}

bool TTSManager::SetVoiceQuality(TTSVoiceQuality quality) {
    m_currentConfig.quality = quality;
    // Android picks its own voice quality; the setting is stored for API
    // compatibility and has no audible effect.
    return m_bIsInitialized;
}

bool TTSManager::SetVoiceByName(const std::wstring& voiceName) {
    if (!m_bIsInitialized)
        return false;
    TTSAndroid* at = static_cast<TTSAndroid*>(m_android);
    if (!at)
        return false;
    const std::string tag = AndroidWideToUtf8(voiceName);
    JniEnvScope jni(at->jvm);
    if (!jni.ok())
        return false;
    jstring jtag = jni.env->NewStringUTF(tag.c_str());
    const bool ok = jni.env->CallStaticBooleanMethod(at->ttsClass, at->mSetLanguage, jtag) == JNI_TRUE;
    const bool clean = CheckJni(jni.env, L"CpgeTts.setLanguage");
    if (ok && clean) {
        at->voiceTag = tag;
        m_currentConfig.voiceName = voiceName;
        return true;
    }
    return false;
}

// Speak one utterance through CpgeTts (QUEUE_FLUSH). Caller must have
// stopped any in-flight utterance and cleared utteranceDone. Returns
// false when the engine is not ready or the call was rejected.
bool TTSManager::AndroidSpeak(const std::string& utf8Text)
{
    TTSAndroid* at = static_cast<TTSAndroid*>(m_android);
    if (!at)
        return false;
    JniEnvScope jni(at->jvm);
    if (!jni.ok())
        return false;
    JNIEnv* env = jni.env;
    const std::string id = "cpge-tts-" + std::to_string(++at->utteranceSeq);
    {
        // Record the ID before speaking: the completion callback could
        // otherwise arrive before the ID is stored and be ignored as stale.
        std::lock_guard<std::mutex> guard(at->doneMutex);
        at->currentUtteranceId = id;
    }
    jstring jtext = env->NewStringUTF(utf8Text.c_str());
    jstring jid = env->NewStringUTF(id.c_str());
    const bool ok = env->CallStaticBooleanMethod(at->ttsClass, at->mSpeak, jtext, jid) == JNI_TRUE;
    const bool clean = CheckJni(env, L"CpgeTts.speak");
    if (!ok || !clean) {
        std::lock_guard<std::mutex> guard(at->doneMutex);
        at->currentUtteranceId.clear();
        return false;
    }
    return true;
}

bool TTSManager::Play(const std::wstring& text) {
    if (!m_bIsInitialized || text.empty())
        return false;
    ThreadLockHelper lock(threadManager, "tts_play_lock", 5000);
    if (!lock.IsLocked())
        return false;
    TTSAndroid* at = static_cast<TTSAndroid*>(m_android);
    if (!at)
        return false;
    AndroidStopUtterance(at);
    m_bStopRequested.store(false);
    m_bSpeaking.store(true);
    m_currentState = TTSPlaybackState::STATE_PLAYING;
    {
        std::lock_guard<std::mutex> guard(at->doneMutex);
        at->utteranceDone.store(false);
    }
    if (!AndroidSpeak(AndroidWideToUtf8(text))) {
        m_bSpeaking.store(false);
        m_currentState = TTSPlaybackState::STATE_STOPPED;
        return false;
    }
    // Synchronous: wait for the utterance callback (or a Stop request
    // from another thread). Short waits so a Stop() ends the wait
    // promptly instead of blocking on the utterance.
    std::unique_lock<std::mutex> guard(at->doneMutex);
    while (!at->utteranceDone.load() && !m_bStopRequested.load())
        at->doneCv.wait_for(guard, std::chrono::milliseconds(20));
    m_bSpeaking.store(false);
    m_currentState = TTSPlaybackState::STATE_STOPPED;
    return true;
}

bool TTSManager::PlayAsync(const std::wstring& text) {
    if (!m_bIsInitialized || text.empty())
        return false;
    ThreadLockHelper lock(threadManager, "tts_play_lock", 5000);
    if (!lock.IsLocked())
        return false;
    TTSAndroid* at = static_cast<TTSAndroid*>(m_android);
    if (!at)
        return false;
    AndroidStopUtterance(at);
    m_bStopRequested.store(false);
    m_bSpeaking.store(true);
    m_currentState = TTSPlaybackState::STATE_PLAYING;
    {
        std::lock_guard<std::mutex> guard(at->doneMutex);
        at->utteranceDone.store(false);
    }
    if (!AndroidSpeak(AndroidWideToUtf8(text))) {
        m_bSpeaking.store(false);
        m_currentState = TTSPlaybackState::STATE_STOPPED;
        return false;
    }
    return true;
}

bool TTSManager::Pause() {
    // Android TTS has no pause: an utterance can only be stopped
    // (which cannot be resumed mid-utterance), so Pause is unsupported.
    return false;
}

bool TTSManager::Resume() {
    // No pause exists to resume from on Android (see Pause()).
    return false;
}

bool TTSManager::Stop() {
    if (!m_bIsInitialized)
        return false;
    TTSAndroid* at = static_cast<TTSAndroid*>(m_android);
    m_bStopRequested.store(true);
    if (at)
        AndroidStopUtterance(at);
    m_bSpeaking.store(false);
    m_currentState = TTSPlaybackState::STATE_STOPPED;
    return true;
}

TTSPlaybackState TTSManager::GetPlaybackState() const {
    return m_currentState;
}

bool TTSManager::IsPlaying() const {
    return m_bSpeaking.load() && m_currentState == TTSPlaybackState::STATE_PLAYING;
}

bool TTSManager::IsPaused() const {
    return m_currentState == TTSPlaybackState::STATE_PAUSED;
}

float TTSManager::GetCurrentVolume() const {
    return m_currentConfig.volume;
}

float TTSManager::GetCurrentPitch() const {
    return m_currentConfig.pitch;
}

float TTSManager::GetCurrentRate() const {
    return m_currentConfig.rate;
}

TTSSpeakerChannel TTSManager::GetCurrentChannel() const {
    return m_currentConfig.channel;
}

std::vector<std::wstring> TTSManager::GetAvailableVoices() {
    std::vector<std::wstring> voices;
    if (!m_bIsInitialized)
        return voices;
    TTSAndroid* at = static_cast<TTSAndroid*>(m_android);
    if (!at)
        return voices;
    JniEnvScope jni(at->jvm);
    if (!jni.ok())
        return voices;
    JNIEnv* env = jni.env;
    jobjectArray array = static_cast<jobjectArray>(env->CallStaticObjectMethod(at->ttsClass, at->mGetVoiceNames));
    if (!array || !CheckJni(env, L"CpgeTts.getVoiceNames"))
        return voices;
    const jsize count = env->GetArrayLength(array);
    for (jsize i = 0; i < count; ++i) {
        jstring item = static_cast<jstring>(env->GetObjectArrayElement(array, i));
        if (!item)
            continue;
        const char* utf = env->GetStringUTFChars(item, nullptr);
        if (utf) {
            voices.push_back(AndroidUtf8ToWide(utf));
            env->ReleaseStringUTFChars(item, utf);
        }
        env->DeleteLocalRef(item);
    }
    return voices;
}

std::wstring TTSManager::GetCurrentVoiceName() const {
    const TTSAndroid* at = static_cast<const TTSAndroid*>(m_android);
    if (at && !at->voiceTag.empty())
        return AndroidUtf8ToWide(at->voiceTag);
    return m_currentConfig.voiceName;
}

bool TTSManager::IsVoiceAvailable(const std::wstring& voiceName) {
    if (!m_bIsInitialized)
        return false;
    const std::vector<std::wstring> voices = GetAvailableVoices();
    for (const std::wstring& v : voices) {
        if (v == voiceName)
            return true;
    }
    return false;
}

void TTSManager::SaveConfiguration(const TTSConfiguration& config) {
    m_currentConfig = config;
    if (m_bIsInitialized) {
        SetVoiceVolume(m_currentConfig.volume);
        SetVoicePitch(m_currentConfig.pitch);
        SetVoiceRate(m_currentConfig.rate);
        if (!m_currentConfig.voiceName.empty())
            SetVoiceByName(m_currentConfig.voiceName);
    }
}

TTSConfiguration TTSManager::LoadConfiguration() {
    return m_currentConfig;
}

void TTSManager::ResetToDefaults() {
    m_currentConfig = TTSConfiguration();
    m_currentState = TTSPlaybackState::STATE_STOPPED;
    if (m_bIsInitialized) {
        SetVoiceVolume(m_currentConfig.volume);
        SetVoicePitch(m_currentConfig.pitch);
        SetVoiceRate(m_currentConfig.rate);
        SetVoiceByName(L"en-US");
    }
}

#else
// ============================================================================
// Linux eSpeak-ng backend (Phase B)
//
// Text-to-speech via eSpeak-ng (libespeak-ng), the Linux counterpart to
// Windows SAPI. Synthesis runs on a worker thread: espeak_Synth +
// espeak_Synchronize produce mono s16 PCM at the eSpeak sample rate
// (22050 Hz) through the synth callback, which resamples it to 44100 Hz
// stereo s16 (linear interpolation) and feeds the shared Miniaudio::Stream
// ring, so speech plays through the engine's audio device -- or the null
// backend when no audio device exists. Voice volume / pitch / rate use
// the same -10..+10 engine scale as SAPI, mapped onto eSpeak's
// parameter ranges; the speaker channel is applied in the callback
// (the silent channel is zeroed).
//
// Android (Phase D step 3) uses the platform TextToSpeech via JNI
// (TTSManager.cpp, Android section, plus CpgeTts.java); the disabled
// behaviour below is now Linux-without-eSpeak-headers only.
//
// eSpeak-ng is optional: when the headers are absent (__has_include
// fails) HAS_ESPEAK is 0 and every method keeps the old disabled
// behaviour -- Initialize returns false, GetPlaybackState returns
// STATE_ERROR (the value main.cpp already treats as "TTS unavailable"),
// the getters return the stored configuration defaults, no warning is
// logged and nothing throws or quits -- so the code still compiles on a
// machine without eSpeak-ng dev files and the engine runs without TTS
// instead of failing the build. CMake links libespeak-ng only when both
// the headers and the library are found (see Linux/CMakeLists.txt).
// ============================================================================

// HAS_ESPEAK comes from TTSManager.h (1 when the eSpeak-ng headers are
// present on Linux, 0 otherwise -- Android and header-less Linux).
#ifndef HAS_ESPEAK
    #define HAS_ESPEAK 0
#endif
#if HAS_ESPEAK
    #include "MiniaudioStream.h"
        #include <thread>
        #include <chrono>
#endif

namespace {
    // One warning per process when TTS is used without eSpeak-ng.
    // (Only for the !HAS_ESPEAK path; with eSpeak-ng there is no stub.)
    void LogTTSUnavailableOnce()
    {
        static std::atomic<bool> warned{ false };
        if (!warned.exchange(true))
        {
            Debug::logDiagLevelMessage(LogLevel::LOG_WARNING,
                L"TTSManager is unavailable on this build (eSpeak-ng headers not found); text-to-speech is disabled.");
        }
    }
}

#if HAS_ESPEAK

namespace
{

// All eSpeak state for the Linux backend. Owned through the opaque void*
// TTSManager::m_espeak so the eSpeak types never leak into the header.
struct TTSESpeak
{
    Miniaudio::Stream* audioStream = nullptr;   // 44100 Hz stereo s16 output
    int sampleRate = 0;                         // eSpeak sample rate (22050)
    std::wstring voiceName;                     // current eSpeak voice (e.g. L"en")
    std::thread synthThread;                    // worker running espeak_Synth + Synchronize
    std::atomic<bool> synthRunning{ false };   // a synth thread is active
    // Streaming linear-interpolation resampler state (mono @sampleRate ->
    // stereo @44100). srcBuf holds source samples not yet consumed;
    // srcReadPos is the read position in source samples.
    std::vector<int16_t> srcBuf;
    double srcReadPos = 0.0;
    // PCM held while paused (44100 Hz stereo s16), drained on Resume.
    std::mutex backlogMutex;
    std::vector<int16_t> backlog;
    bool paused = false;
};

// The eSpeak synth callback has no user-data parameter, so it reaches the
// manager through this file-scope pointer. There is a single TTSManager
// in the engine; it is set in Initialize() and cleared in CleanUp().
TTSManager* g_activeTTS = nullptr;

// Convert a wide string (UTF-32 wchar_t on Linux) to UTF-8 for eSpeak.
// (std::wstring_convert is deprecated in C++17; wcstombs depends on the
// process locale, which may not be UTF-8 -- so convert manually.)
std::string WideToUtf8(const std::wstring& wide)
{
    std::string out;
    out.reserve(wide.size());
    for (wchar_t wc : wide)
    {
        const uint32_t cp = static_cast<uint32_t>(wc);
        if (cp < 0x80)
        {
            out.push_back(static_cast<char>(cp));
        }
        else if (cp < 0x800)
        {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
        else if (cp < 0x10000)
        {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
        else
        {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    return out;
}

// Narrow a wide string to the current locale (for eSpeak voice names,
// which are ASCII -- "en", "en-us", "de", ...).
std::string WideToNarrow(const std::wstring& wide)
{
    std::string out;
    out.reserve(wide.size());
    for (wchar_t wc : wide)
        out.push_back(static_cast<char>(wc & 0x7F));
    return out;
}

std::wstring NarrowToWide(const std::string& narrow)
{
    std::wstring out;
    out.reserve(narrow.size());
    for (char c : narrow)
        out.push_back(static_cast<wchar_t>(static_cast<unsigned char>(c)));
    return out;
}

// Map the engine's -10..+10 rate scale onto eSpeak words-per-minute
// (default 175, usable range ~80-450).
int EngineRateToWpm(float rate)
{
    const int wpm = static_cast<int>(175.0f + rate * 15.0f);
    return wpm < 80 ? 80 : (wpm > 450 ? 450 : wpm);
}

// Map the engine's -10..+10 pitch scale onto eSpeak pitch 0-99
// (default 50).
int EnginePitchToEspeak(float pitch)
{
    const int p = static_cast<int>(50.0f + pitch * 5.0f);
    return p < 0 ? 0 : (p > 99 ? 99 : p);
}

} // namespace

// eSpeak synth callback (runs on eSpeak's synthesis thread). Receives
// mono s16 PCM at the eSpeak sample rate; returns 0 to continue.
// A static member so it can reach the private OnEspeakAudio.
int TTSManager::EspeakSynthCallback(short* wav, int numsamples, espeak_EVENT* events)
{
    (void)events;
    TTSManager* self = g_activeTTS;
    if (!self || !wav || numsamples <= 0)
        return 0;
    self->OnEspeakAudio(wav, numsamples);
    return 0;
}


#endif // HAS_ESPEAK

TTSManager::TTSManager() :
    m_bIsInitialized(false),
    m_bHasCleanedUp(false),
    m_currentState(TTSPlaybackState::STATE_STOPPED),
    m_bSpeaking(false),
    m_bStopRequested(false)
{
    m_currentConfig = TTSConfiguration();
}

TTSManager::~TTSManager() {
    if (!m_bHasCleanedUp) {
        CleanUp();
    }
}

bool TTSManager::Initialize() {
#if !HAS_ESPEAK
    LogTTSUnavailableOnce();
    m_currentState = TTSPlaybackState::STATE_ERROR;
    return false;
#else
    ThreadLockHelper lock(threadManager, "tts_init_lock", 5000);
    if (!lock.IsLocked()) {
        debug.logDiagLevelMessage(LogLevel::LOG_ERROR, L"TTSManager::Initialize() - Failed to acquire initialization lock");
        return false;
    }
    if (m_bIsInitialized) {
        debug.logDiagLevelMessage(LogLevel::LOG_WARNING, L"TTSManager::Initialize() - Already initialized");
        return true;
    }

    // eSpeak-ng with retrieval output: the synth callback receives the PCM
    // instead of eSpeak opening an audio device itself.
    const int rate = espeak_Initialize(AUDIO_OUTPUT_RETRIEVAL, 0, nullptr, 0);
    if (rate <= 0) {
        debug.logDiagLevelMessage(LogLevel::LOG_ERROR, L"TTSManager::Initialize() - eSpeak-ng initialisation failed");
        m_currentState = TTSPlaybackState::STATE_ERROR;
        return false;
    }
    TTSESpeak* es = new TTSESpeak();
    es->sampleRate = rate;
    es->voiceName = L"en";
    es->audioStream = new Miniaudio::Stream();
    if (!es->audioStream->IsReady()) {
        delete es->audioStream;
        delete es;
        espeak_Terminate();
        debug.logDiagLevelMessage(LogLevel::LOG_ERROR, L"TTSManager::Initialize() - Could not create the TTS audio stream");
        m_currentState = TTSPlaybackState::STATE_ERROR;
        return false;
    }
    m_espeak = es;
    g_activeTTS = this;
    espeak_SetSynthCallback(EspeakSynthCallback);
    espeak_SetVoiceByName("en");

    // Apply the stored configuration (defaults on first use).
    espeak_SetParameter(espeakVOLUME, static_cast<int>(m_currentConfig.volume * 100.0f), 0);
    espeak_SetParameter(espeakPITCH, EnginePitchToEspeak(m_currentConfig.pitch), 0);
    espeak_SetParameter(espeakRATE, EngineRateToWpm(m_currentConfig.rate), 0);
    if (!m_currentConfig.voiceName.empty())
        SetVoiceByName(m_currentConfig.voiceName);

    m_bIsInitialized = true;
    m_bHasCleanedUp = false;
    m_currentState = TTSPlaybackState::STATE_STOPPED;
    debug.logLevelMessage(LogLevel::LOG_INFO, L"TTSManager::Initialize() - eSpeak-ng initialised");
    return true;
#endif
}

void TTSManager::CleanUp() {
#if !HAS_ESPEAK
    m_bIsInitialized = false;
    m_bHasCleanedUp = true;
    m_currentState = TTSPlaybackState::STATE_STOPPED;
    m_bSpeaking.store(false);
    m_bStopRequested.store(false);
    return;
#else
    ThreadLockHelper lock(threadManager, "tts_cleanup_lock", 5000);
    if (!m_bHasCleanedUp) {
        Stop();
        TTSESpeak* es = static_cast<TTSESpeak*>(m_espeak);
        if (es) {
            if (es->audioStream) {
                es->audioStream->Stop();
                delete es->audioStream;
                es->audioStream = nullptr;
            }
            delete es;
            m_espeak = nullptr;
        }
        if (g_activeTTS == this)
            g_activeTTS = nullptr;
        espeak_Terminate();
    }
    m_bIsInitialized = false;
    m_bHasCleanedUp = true;
    m_currentState = TTSPlaybackState::STATE_STOPPED;
    m_bSpeaking.store(false);
    m_bStopRequested.store(false);
#endif
}

#if HAS_ESPEAK

// Stop any in-flight synthesis and join its worker thread. Caller must
// hold no lock that the synth thread needs (it only touches m_espeak
// state and the atomics).
void TTSManager::StopSynthThreadLocked(void* esVoid)
{
    TTSESpeak* es = static_cast<TTSESpeak*>(esVoid);
    if (!es)
        return;
    // Cancel a running synthesis, then ALWAYS join a joinable thread:
    // assigning (or destroying) a joinable std::thread calls
    // std::terminate, so a finished-but-unjoined thread must be reaped
    // here before PlayAsync creates the next one.
    if (es->synthRunning.load())
        espeak_Cancel();
    if (es->synthThread.joinable())
        es->synthThread.join();
    es->synthRunning.store(false);
}

// Resample one callback chunk (mono s16 @sampleRate) to 44100 Hz stereo
// s16, applying volume and speaker channel, and feed it to the audio
// ring (or the paused backlog). Runs on eSpeak's synthesis thread.
void TTSManager::OnEspeakAudio(const short* wav, int numsamples)
{
    TTSESpeak* es = static_cast<TTSESpeak*>(m_espeak);
    if (!es || !es->audioStream || !wav || numsamples <= 0)
        return;
    // Append the new source samples, then resample as far as possible.
    // Keep one source sample of overlap for the interpolation.
    const size_t base = es->srcBuf.size();
    es->srcBuf.insert(es->srcBuf.end(), wav, wav + numsamples);
    (void)base;
    if (es->srcBuf.size() < 2 || es->sampleRate <= 0)
        return;
    const double step = static_cast<double>(es->sampleRate) / static_cast<double>(Miniaudio::kSampleRate);
    const float vol = m_currentConfig.volume;
    const TTSSpeakerChannel channel = m_currentConfig.channel;
    std::vector<int16_t> out;
    while (es->srcReadPos + 1.0 < static_cast<double>(es->srcBuf.size())) {
        const size_t i0 = static_cast<size_t>(es->srcReadPos);
        const double frac = es->srcReadPos - static_cast<double>(i0);
        const double s = static_cast<double>(es->srcBuf[i0]) * (1.0 - frac) +
                         static_cast<double>(es->srcBuf[i0 + 1]) * frac;
        int32_t v = static_cast<int32_t>(s * static_cast<double>(vol));
        v = v < -32768 ? -32768 : (v > 32767 ? 32767 : v);
        const int16_t sample = static_cast<int16_t>(v);
        // Speaker channel: zero the silent channel.
        int16_t left = sample, right = sample;
        if (channel == TTSSpeakerChannel::CHANNEL_LEFT)
            right = 0;
        else if (channel == TTSSpeakerChannel::CHANNEL_RIGHT)
            left = 0;
        out.push_back(left);
        out.push_back(right);
        es->srcReadPos += step;
    }
    // Drop consumed source samples, keeping the one the read position
    // still references.
    const size_t consumed = static_cast<size_t>(es->srcReadPos);
    if (consumed > 0) {
        es->srcBuf.erase(es->srcBuf.begin(), es->srcBuf.begin() + static_cast<std::ptrdiff_t>(consumed));
        es->srcReadPos -= static_cast<double>(consumed);
    }
    if (out.empty())
        return;
    const size_t frames = out.size() / 2;
    if (es->paused) {
        std::lock_guard<std::mutex> clock(es->backlogMutex);
        es->backlog.insert(es->backlog.end(), out.begin(), out.end());
    } else {
        // Backpressure: eSpeak synthesises faster than real-time playback,
        // so wait for ring space instead of dropping speech. Bounded
        // (2 s) and aborts promptly on Stop (m_bStopRequested) so Stop()
        // cannot deadlock on a blocked callback.
        size_t offset = 0;
        int waitedMs = 0;
        while (offset < frames && waitedMs < 2000) {
            if (m_bStopRequested.load())
                break;
            const size_t written = es->audioStream->Write(out.data() + offset * 2, frames - offset);
            offset += written;
            if (offset < frames) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                waitedMs += 5;
            }
        }
    }
}

void TTSManager::OnEspeakUtteranceDone()
{
    m_bSpeaking.store(false);
    if (m_currentState == TTSPlaybackState::STATE_PLAYING)
        m_currentState = TTSPlaybackState::STATE_STOPPED;
}

// Worker for PlayAsync: synthesise the text (blocking inside eSpeak),
// then mark the utterance done. Runs on its own thread.
void TTSManager::SynthThreadMain(std::string utf8Text)
{
    unsigned int uid = 0;
    espeak_Synth(utf8Text.c_str(), utf8Text.size() + 1, 0, POS_CHARACTER, 0,
        espeakCHARS_UTF8, &uid, nullptr);
    espeak_Synchronize();
    TTSESpeak* es = static_cast<TTSESpeak*>(m_espeak);
    if (es)
        es->synthRunning.store(false);
    OnEspeakUtteranceDone();
}

#endif // HAS_ESPEAK

bool TTSManager::SetVoiceVolume(float volume) {
    m_currentConfig.volume = volume < 0.0f ? 0.0f : (volume > 1.0f ? 1.0f : volume);
#if HAS_ESPEAK
    if (m_bIsInitialized)
        espeak_SetParameter(espeakVOLUME, static_cast<int>(m_currentConfig.volume * 100.0f), 0);
    return m_bIsInitialized;
#else
    (void)volume;
    return false;
#endif
}

bool TTSManager::SetVoicePitch(float pitch) {
    m_currentConfig.pitch = pitch < -10.0f ? -10.0f : (pitch > 10.0f ? 10.0f : pitch);
#if HAS_ESPEAK
    if (m_bIsInitialized)
        espeak_SetParameter(espeakPITCH, EnginePitchToEspeak(m_currentConfig.pitch), 0);
    return m_bIsInitialized;
#else
    (void)pitch;
    return false;
#endif
}

bool TTSManager::SetVoiceRate(float rate) {
    m_currentConfig.rate = rate < -10.0f ? -10.0f : (rate > 10.0f ? 10.0f : rate);
#if HAS_ESPEAK
    if (m_bIsInitialized)
        espeak_SetParameter(espeakRATE, EngineRateToWpm(m_currentConfig.rate), 0);
    return m_bIsInitialized;
#else
    (void)rate;
    return false;
#endif
}

bool TTSManager::SetSpeakerChannel(TTSSpeakerChannel channel) {
    m_currentConfig.channel = channel;
#if HAS_ESPEAK
    return m_bIsInitialized;
#else
    (void)channel;
    return false;
#endif
}

bool TTSManager::SetVoiceQuality(TTSVoiceQuality quality) {
    m_currentConfig.quality = quality;
#if HAS_ESPEAK
    // eSpeak-ng has no quality tiers; the setting is stored for API
    // compatibility and has no audible effect.
    return m_bIsInitialized;
#else
    (void)quality;
    return false;
#endif
}

bool TTSManager::SetVoiceByName(const std::wstring& voiceName) {
#if !HAS_ESPEAK
    (void)voiceName;
    return false;
#else
    if (!m_bIsInitialized)
        return false;
    const std::string narrow = WideToNarrow(voiceName);
    if (espeak_SetVoiceByName(narrow.c_str()) != EE_OK)
        return false;
    TTSESpeak* es = static_cast<TTSESpeak*>(m_espeak);
    if (es)
        es->voiceName = voiceName;
    m_currentConfig.voiceName = voiceName;
    return true;
#endif
}

bool TTSManager::Play(const std::wstring& text) {
#if !HAS_ESPEAK
    (void)text;
    return false;
#else
    if (!m_bIsInitialized || text.empty())
        return false;
    ThreadLockHelper lock(threadManager, "tts_play_lock", 5000);
    if (!lock.IsLocked())
        return false;
    TTSESpeak* es = static_cast<TTSESpeak*>(m_espeak);
    if (!es)
        return false;
    StopSynthThreadLocked(es);
    {
        std::lock_guard<std::mutex> clock(es->backlogMutex);
        es->backlog.clear();
        es->srcBuf.clear();
        es->srcReadPos = 0.0;
        es->paused = false;
    }
    es->audioStream->Clear();
    es->audioStream->Start();
    m_bStopRequested.store(false);
    m_bSpeaking.store(true);
    m_currentState = TTSPlaybackState::STATE_PLAYING;
    // Synchronous: synthesise on this thread and block until done.
    SynthThreadMain(WideToUtf8(text));
    return true;
#endif
}

bool TTSManager::PlayAsync(const std::wstring& text) {
#if !HAS_ESPEAK
    (void)text;
    return false;
#else
    if (!m_bIsInitialized || text.empty())
        return false;
    ThreadLockHelper lock(threadManager, "tts_play_lock", 5000);
    if (!lock.IsLocked())
        return false;
    TTSESpeak* es = static_cast<TTSESpeak*>(m_espeak);
    if (!es)
        return false;
    StopSynthThreadLocked(es);
    {
        std::lock_guard<std::mutex> clock(es->backlogMutex);
        es->backlog.clear();
        es->srcBuf.clear();
        es->srcReadPos = 0.0;
        es->paused = false;
    }
    es->audioStream->Clear();
    es->audioStream->Start();
    m_bStopRequested.store(false);
    m_bSpeaking.store(true);
    m_currentState = TTSPlaybackState::STATE_PLAYING;
    es->synthRunning.store(true);
    es->synthThread = std::thread(&TTSManager::SynthThreadMain, this, WideToUtf8(text));
    return true;
#endif
}

bool TTSManager::Pause() {
#if !HAS_ESPEAK
    return false;
#else
    if (!m_bIsInitialized || !m_bSpeaking.load() || m_currentState != TTSPlaybackState::STATE_PLAYING)
        return false;
    TTSESpeak* es = static_cast<TTSESpeak*>(m_espeak);
    if (!es)
        return false;
    es->paused = true;
    es->audioStream->Stop();
    m_currentState = TTSPlaybackState::STATE_PAUSED;
    return true;
#endif
}

bool TTSManager::Resume() {
#if !HAS_ESPEAK
    return false;
#else
    if (!m_bIsInitialized || m_currentState != TTSPlaybackState::STATE_PAUSED)
        return false;
    TTSESpeak* es = static_cast<TTSESpeak*>(m_espeak);
    if (!es)
        return false;
    {
        std::lock_guard<std::mutex> clock(es->backlogMutex);
        if (!es->backlog.empty()) {
            es->audioStream->Write(es->backlog.data(), es->backlog.size() / 2);
            es->backlog.clear();
        }
        es->paused = false;
    }
    es->audioStream->Start();
    m_currentState = TTSPlaybackState::STATE_PLAYING;
    return true;
#endif
}

bool TTSManager::Stop() {
#if !HAS_ESPEAK
    return false;
#else
    if (!m_bIsInitialized)
        return false;
    TTSESpeak* es = static_cast<TTSESpeak*>(m_espeak);
    m_bStopRequested.store(true);
    if (es) {
        StopSynthThreadLocked(es);
        es->audioStream->Stop();
        es->audioStream->Clear();
        {
            std::lock_guard<std::mutex> clock(es->backlogMutex);
            es->backlog.clear();
            es->srcBuf.clear();
            es->srcReadPos = 0.0;
            es->paused = false;
        }
    }
    m_bSpeaking.store(false);
    m_currentState = TTSPlaybackState::STATE_STOPPED;
    return true;
#endif
}

TTSPlaybackState TTSManager::GetPlaybackState() const {
    return m_currentState;
}

bool TTSManager::IsPlaying() const {
    return m_bSpeaking.load() && m_currentState == TTSPlaybackState::STATE_PLAYING;
}

bool TTSManager::IsPaused() const {
    return m_currentState == TTSPlaybackState::STATE_PAUSED;
}

float TTSManager::GetCurrentVolume() const {
    return m_currentConfig.volume;
}

float TTSManager::GetCurrentPitch() const {
    return m_currentConfig.pitch;
}

float TTSManager::GetCurrentRate() const {
    return m_currentConfig.rate;
}

TTSSpeakerChannel TTSManager::GetCurrentChannel() const {
    return m_currentConfig.channel;
}

std::vector<std::wstring> TTSManager::GetAvailableVoices() {
#if !HAS_ESPEAK
    return std::vector<std::wstring>();
#else
    std::vector<std::wstring> voices;
    if (!m_bIsInitialized)
        return voices;
    const espeak_VOICE** list = espeak_ListVoices(nullptr);
    if (!list)
        return voices;
    // Return the voice identifier's last component (e.g. "en" from
    // "gmw/en") -- that is the value espeak_SetVoiceByName accepts.
    // The display name ("English (Great Britain)") does NOT work with
    // SetVoiceByName, so returning it would make IsVoiceAvailable lie.
    for (int i = 0; list[i] != nullptr; ++i) {
        const espeak_VOICE* v = list[i];
        std::string id;
        if (v->identifier && v->identifier[0]) {
            id = v->identifier;
            const size_t slash = id.find_last_of('/');
            if (slash != std::string::npos)
                id = id.substr(slash + 1);
        } else if (v->languages && v->languages[0]) {
            // languages starts with a priority byte, then the code.
            id = v->languages + 1;
        } else if (v->name) {
            id = v->name;
        }
        if (!id.empty())
            voices.push_back(NarrowToWide(id));
    }
    return voices;
#endif
}

std::wstring TTSManager::GetCurrentVoiceName() const {
#if !HAS_ESPEAK
    return m_currentConfig.voiceName;
#else
    TTSESpeak* es = static_cast<TTSESpeak*>(m_espeak);
    if (es && !es->voiceName.empty())
        return es->voiceName;
    return m_currentConfig.voiceName;
#endif
}

bool TTSManager::IsVoiceAvailable(const std::wstring& voiceName) {
#if !HAS_ESPEAK
    (void)voiceName;
    return false;
#else
    if (!m_bIsInitialized)
        return false;
    const std::vector<std::wstring> voices = GetAvailableVoices();
    for (const std::wstring& v : voices) {
        if (v == voiceName)
            return true;
    }
    return false;
#endif
}

void TTSManager::SaveConfiguration(const TTSConfiguration& config) {
    m_currentConfig = config;
#if HAS_ESPEAK
    if (m_bIsInitialized) {
        espeak_SetParameter(espeakVOLUME, static_cast<int>(m_currentConfig.volume * 100.0f), 0);
        espeak_SetParameter(espeakPITCH, EnginePitchToEspeak(m_currentConfig.pitch), 0);
        espeak_SetParameter(espeakRATE, EngineRateToWpm(m_currentConfig.rate), 0);
        if (!m_currentConfig.voiceName.empty())
            SetVoiceByName(m_currentConfig.voiceName);
    }
#endif
}

TTSConfiguration TTSManager::LoadConfiguration() {
    return m_currentConfig;
}

void TTSManager::ResetToDefaults() {
    m_currentConfig = TTSConfiguration();
    m_currentState = TTSPlaybackState::STATE_STOPPED;
#if HAS_ESPEAK
    if (m_bIsInitialized) {
        espeak_SetParameter(espeakVOLUME, static_cast<int>(m_currentConfig.volume * 100.0f), 0);
        espeak_SetParameter(espeakPITCH, EnginePitchToEspeak(m_currentConfig.pitch), 0);
        espeak_SetParameter(espeakRATE, EngineRateToWpm(m_currentConfig.rate), 0);
        SetVoiceByName(L"en");
    }
#endif
}

#endif // !PLATFORM_WINDOWS
