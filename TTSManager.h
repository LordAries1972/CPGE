#pragma once
// -------------------------------------------------------------------------------------------------------------
// TTSManager.h - Text-to-Speech Manager for Windows SAPI Integration
// 
// This class provides comprehensive text-to-speech functionality including:
// - Voice configuration (pitch, volume, rate)
// - Speaker channel control (left, right, both)
// - Playback control (play, pause, resume, stop)
// - Thread-safe operations with proper cleanup
// 
// Uses Windows Speech API (SAPI) for TTS functionality
// Integrates with existing debug and thread management systems
// -------------------------------------------------------------------------------------------------------------

#include "Includes.h"
#include "Debug.h"
#include "ThreadManager.h"
#include "ThreadLockHelper.h"
#include "Color.h"
#include "Vectors.h"

#pragma warning(push)
#pragma warning(disable: 4996)                          // 'GetVersionExW': was declared deprecated

// Include Windows Speech API headers
#include <sapi.h>
#include <sphelper.h>
#include <comdef.h>

// Link required SAPI libraries
#pragma comment(lib, "sapi.lib")

// Forward declarations
class Debug;
class ThreadManager;

// TTS Speaker channel enumeration for audio output control
enum class TTSSpeakerChannel : int {
    CHANNEL_LEFT = 0,                                                       // Output audio to left speaker only
    CHANNEL_RIGHT = 1,                                                      // Output audio to right speaker only
    CHANNEL_BOTH = 2,                                                       // Output audio to both speakers (stereo)
    CHANNEL_CENTER = 3                                                      // Output audio to center channel
};

// TTS playback state enumeration for tracking current status
enum class TTSPlaybackState : int {
    STATE_STOPPED = 0,                                                      // TTS engine is stopped
    STATE_PLAYING = 1,                                                      // TTS engine is currently speaking
    STATE_PAUSED = 2,                                                       // TTS engine is paused
    STATE_ERROR = 3                                                         // TTS engine encountered an error
};

// TTS voice quality enumeration for different voice types
enum class TTSVoiceQuality : int {
    QUALITY_DEFAULT = 0,                                                    // Use system default voice quality
    QUALITY_LOW = 1,                                                        // Low quality voice (faster processing)
    QUALITY_MEDIUM = 2,                                                     // Medium quality voice (balanced)
    QUALITY_HIGH = 3                                                        // High quality voice (best quality)
};

// Structure to hold TTS configuration parameters
struct TTSConfiguration {
    float volume;                                                           // Voice volume (0.0f to 1.0f)
    float pitch;                                                            // Voice pitch (-10.0f to +10.0f)
    float rate;                                                             // Speech rate (-10.0f to +10.0f)
    TTSSpeakerChannel channel;                                              // Speaker channel configuration
    TTSVoiceQuality quality;                                                // Voice quality setting
    bool enableEvents;                                                      // Enable TTS event notifications
    std::wstring voiceName;                                                 // Specific voice name to use

    // Default constructor with sensible defaults
    TTSConfiguration() :
        volume(0.8f),
        pitch(0.5f),
        rate(0.0f),
        channel(TTSSpeakerChannel::CHANNEL_BOTH),
        quality(TTSVoiceQuality::QUALITY_MEDIUM),
        enableEvents(true),
        voiceName(L"") {
    }
};

// Main TTS Manager class for text-to-speech operations
class TTSManager {
public:
    // Constructor and destructor
    TTSManager();
    ~TTSManager();

    // Core initialization and cleanup methods
    bool Initialize();                                                      // Initialize TTS engine and COM interfaces
    void CleanUp();                                                         // Clean up resources and shutdown TTS engine

    // Voice configuration methods
    bool SetVoiceVolume(float volume);                                      // Set voice volume (0.0f to 1.0f)
    bool SetVoicePitch(float pitch);                                        // Set voice pitch (-10.0f to +10.0f)
    bool SetVoiceRate(float rate);                                          // Set speech rate (-10.0f to +10.0f)
    bool SetSpeakerChannel(TTSSpeakerChannel channel);                      // Set speaker output channel
    bool SetVoiceQuality(TTSVoiceQuality quality);                          // Set voice quality level
    bool SetVoiceByName(const std::wstring& voiceName);                     // Set specific voice by name

    // Playback control methods
    bool Play(const std::wstring& text);                                    // Speak the provided text
    bool PlayAsync(const std::wstring& text);                               // Speak text asynchronously
    bool Pause();                                                           // Pause current speech
    bool Resume();                                                          // Resume paused speech
    bool Stop();                                                            // Stop current speech immediately

    // Status and information methods
    TTSPlaybackState GetPlaybackState() const;                              // Get current playback state
    bool IsPlaying() const;                                                 // Check if TTS is currently speaking
    bool IsPaused() const;                                                  // Check if TTS is paused
    float GetCurrentVolume() const;                                         // Get current voice volume
    float GetCurrentPitch() const;                                          // Get current voice pitch
    float GetCurrentRate() const;                                           // Get current speech rate
    TTSSpeakerChannel GetCurrentChannel() const;                            // Get current speaker channel

    // Voice enumeration and selection methods
    std::vector<std::wstring> GetAvailableVoices();                         // Get list of available voices
    std::wstring GetCurrentVoiceName() const;                               // Get currently selected voice name
    bool IsVoiceAvailable(const std::wstring& voiceName);                   // Check if specific voice is available

    // Configuration management methods
    void SaveConfiguration(const TTSConfiguration& config);                 // Save TTS configuration
    TTSConfiguration LoadConfiguration();                                   // Load TTS configuration
    void ResetToDefaults();                                                 // Reset all settings to defaults

private:
    // Private member variables
    bool m_bIsInitialized;                                                  // Flag indicating if TTS is initialized
    bool m_bHasCleanedUp;                                                   // Flag indicating if cleanup has been performed
    TTSPlaybackState m_currentState;                                        // Current playback state
    TTSConfiguration m_currentConfig;                                       // Current TTS configuration

    // SAPI COM interface pointers
    ComPtr<ISpVoice> m_pVoice;                                              // Main TTS voice interface
    ComPtr<ISpObjectToken> m_pVoiceToken;                                   // Voice token for current voice
    ComPtr<IEnumSpObjectTokens> m_pEnumTokens;                              // Enumerator for available voices

    // Thread safety
    std::atomic<bool> m_bSpeaking;                                          // Atomic flag for speaking state
    std::atomic<bool> m_bStopRequested;                                     // Atomic flag for stop request

    // Private helper methods
    bool InitializeCOM();                                                   // Initialize COM for SAPI
    bool CreateVoiceInterface();                                            // Create main voice interface
    bool EnumerateVoices();                                                 // Enumerate available voices
    bool ApplyVoiceSettings();                                              // Apply current voice settings to engine
    bool SetupAudioOutput();                                                // Configure audio output settings
    void UpdatePlaybackState();                                             // Update current playback state
    HRESULT ConvertVolumeToSAPI(float volume, USHORT& sapiVolume);          // Convert volume to SAPI format
    HRESULT ConvertPitchToSAPI(float pitch, long& sapiPitch);               // Convert pitch to SAPI format
    HRESULT ConvertRateToSAPI(float rate, long& sapiRate);                  // Convert rate to SAPI format

    // Event handling methods
    static void CALLBACK TTSEventCallback(WPARAM wParam, LPARAM lParam);    // Static callback for TTS events
    void HandleTTSEvent(SPEVENT& event);                                    // Handle individual TTS events

    // Cleanup helper methods
    void ReleaseCOMInterfaces();                                            // Release all COM interfaces
    void ResetInternalState();                                              // Reset internal state variables

    // Debug and logging helpers
    void LogTTSError(HRESULT hr, const std::wstring& operation) const;      // Log TTS-specific errors
    std::wstring HResultToString(HRESULT hr) const;                         // Convert HRESULT to readable string
};

// External references to maintain consistency with existing architecture
extern Debug debug;
extern ThreadManager threadManager;

// Restore warning settings
#pragma warning(pop)