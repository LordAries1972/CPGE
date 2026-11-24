# TTSManager Example Usage Guide

This document provides comprehensive examples of how to use the TTSManager class for text-to-speech functionality in your application.

## Table of Contents

- [Initialization](#initialization)
- [Basic Usage Examples](#basic-usage-examples)
- [Advanced Configuration](#advanced-configuration)
- [Voice Control](#voice-control)
- [Channel Management](#channel-management)
- [Game Integration Examples](#game-integration-examples)
- [Error Handling](#error-handling)

## Initialization

### Program Start Example

```cpp
// *----------------------------------------------------------------------------------------------
// Program Start!
// *----------------------------------------------------------------------------------------------
int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    // Initialise and create a window
    
    // ... (window creation code)
    
    // Initialise our TTS Manager
    if (!ttsManager.Initialize()) {
        debug.logLevelMessage(LogLevel::LOG_WARNING, L"TTS system initialization failed - continuing without TTS");
    }
    else {
        #if defined(_DEBUG_TTSMANAGER_) && defined(_DEBUG)
            debug.logLevelMessage(LogLevel::LOG_INFO, L"TTS system initialized successfully");
        #endif
    }
    
    // ... (rest of your application code)
}
```

## Basic Usage Examples

### Example 1: Basic Text-to-Speech

```cpp
// Simple text-to-speech playback
ttsManager.Play(L"Welcome to the game!");
```

### Example 2: Asynchronous Speech with Custom Settings

```cpp
// Configure voice settings
ttsManager.SetVoiceVolume(0.8f);           // 80% volume
ttsManager.SetVoicePitch(2.0f);            // Higher pitch
ttsManager.SetVoiceRate(-2.0f);            // Slower speech
ttsManager.SetSpeakerChannel(TTSSpeakerChannel::CHANNEL_LEFT);  // Left speaker only

// Play asynchronously (non-blocking)
ttsManager.PlayAsync(L"Loading game assets, please wait...");
```

## Advanced Configuration

### Voice Settings Combinations

```cpp
// Gaming announcer voice setup
ttsManager.SetVoiceVolume(0.9f);           // High volume for important announcements
ttsManager.SetVoicePitch(0.5f);            // Lower pitch for authority
ttsManager.SetVoiceRate(1.0f);             // Normal speech rate

// Notification voice setup
ttsManager.SetVoiceVolume(0.6f);           // Medium volume for background info
ttsManager.SetVoicePitch(1.5f);            // Higher pitch for alerts
ttsManager.SetVoiceRate(2.0f);             // Faster speech for quick notifications

// Emergency alert voice setup
ttsManager.SetVoiceVolume(1.0f);           // Maximum volume
ttsManager.SetVoicePitch(-1.0f);           // Lower pitch for urgency
ttsManager.SetVoiceRate(-1.0f);            // Slower for clarity
```

### Voice Profile Management

```cpp
// Create different voice profiles for different game contexts
void SetupVoiceProfiles()
{
    // Combat voice profile
    if (gameState == COMBAT) {
        ttsManager.SetVoiceVolume(0.8f);
        ttsManager.SetVoicePitch(0.0f);
        ttsManager.SetVoiceRate(1.5f);
        ttsManager.SetSpeakerChannel(TTSSpeakerChannel::CHANNEL_BOTH);
    }
    
    // Menu voice profile
    else if (gameState == MENU) {
        ttsManager.SetVoiceVolume(0.7f);
        ttsManager.SetVoicePitch(1.0f);
        ttsManager.SetVoiceRate(0.5f);
        ttsManager.SetSpeakerChannel(TTSSpeakerChannel::CHANNEL_BOTH);
    }
    
    // Stealth voice profile
    else if (gameState == STEALTH) {
        ttsManager.SetVoiceVolume(0.4f);
        ttsManager.SetVoicePitch(-0.5f);
        ttsManager.SetVoiceRate(-0.5f);
        ttsManager.SetSpeakerChannel(TTSSpeakerChannel::CHANNEL_BOTH);
    }
}
```

### Advanced Channel Configuration

```cpp
// Spatial audio configuration for 3D positioning
void ConfigureSpatialTTS(float playerX, float soundSourceX)
{
    if (soundSourceX > playerX + 5.0f) {
        // Sound is to the right
        ttsManager.SetSpeakerChannel(TTSSpeakerChannel::CHANNEL_RIGHT);
        ttsManager.SetVoiceVolume(0.8f);
    }
    else if (soundSourceX < playerX - 5.0f) {
        // Sound is to the left
        ttsManager.SetSpeakerChannel(TTSSpeakerChannel::CHANNEL_LEFT);
        ttsManager.SetVoiceVolume(0.8f);
    }
    else {
        // Sound is centered
        ttsManager.SetSpeakerChannel(TTSSpeakerChannel::CHANNEL_BOTH);
        ttsManager.SetVoiceVolume(0.9f);
    }
}
```

### Voice Queue Management

```cpp
// Advanced queuing system for multiple announcements
class TTSQueue
{
private:
    std::queue<std::wstring> messageQueue;
    bool isProcessing = false;
    
public:
    void QueueMessage(const std::wstring& message)
    {
        messageQueue.push(message);
        ProcessNext();
    }
    
    void ProcessNext()
    {
        if (!isProcessing && !messageQueue.empty() && !ttsManager.IsPlaying())
        {
            isProcessing = true;
            std::wstring nextMessage = messageQueue.front();
            messageQueue.pop();
            
            ttsManager.PlayAsync(nextMessage);
            isProcessing = false;
        }
    }
};

// Usage example
TTSQueue ttsQueue;
ttsQueue.QueueMessage(L"Enemy spotted!");
ttsQueue.QueueMessage(L"Health low!");
ttsQueue.QueueMessage(L"Ammunition depleted!");
```

## Volume: how the setting reaches the voice

* `SetVoiceVolume(float)` takes 0.0 - 1.0 (SAPI's volume is 0 - 100 % and cannot amplify; larger values are clamped to the maximum). The settings window's **TTS Volume** slider and the saved `TTSVolume` value use the same 0.0 - 1.0 range (the slider used to run 0 - 5, so most of its travel was just the maximum; an old saved value above 1.0 is clamped when the config loads).
* The requested volume is **remembered even when the engine cannot take it yet** (not initialised, or busy). `Play` / `PlayAsync` call `ApplyVoiceSettings()` before every utterance, which pushes the remembered volume to the voice, so a line always speaks at the current setting. (Before, a request made while the engine was not ready was dropped and the stale default of 0.8 was used.)
* The settings callback in `main.cpp` hands the slider's value to the manager on every change, whether or not Text-to-Speech is switched on at that moment.
* Limitation of SAPI: a volume change made while a line is **already being spoken** is heard from the next line, not mid-sentence.

## Voice Control

### Example 3: Voice Control During Gameplay

```cpp
// Check if TTS is currently playing
if (ttsManager.IsPlaying()) {
    ttsManager.Pause();                     // Pause current speech
}

// Later in your code...
ttsManager.Resume();                        // Resume paused speech
```

### Example 4: Emergency Stop

```cpp
// Stop speech immediately
ttsManager.Stop();
```

### Example 5: Voice Selection

```cpp
// Get available voices and select one
std::vector<std::wstring> voices = ttsManager.GetAvailableVoices();
if (!voices.empty()) {
    ttsManager.SetVoiceByName(voices[0]);   // Use first available voice
}
```

## Channel Management

### Example 6: Channel-Specific Announcements

```cpp
// Right channel announcement
ttsManager.SetSpeakerChannel(TTSSpeakerChannel::CHANNEL_RIGHT);
ttsManager.Play(L"Enemy approaching from the right!");

// Left channel announcement
ttsManager.SetSpeakerChannel(TTSSpeakerChannel::CHANNEL_LEFT);
ttsManager.Play(L"Power-up available on the left!");

// Both channels announcement
ttsManager.SetSpeakerChannel(TTSSpeakerChannel::CHANNEL_BOTH);
ttsManager.Play(L"Game over!");
```

## Complete Playback Example

```cpp
// Wait for speech to complete
while (ttsManager.IsPlaying())
{
    Sleep(25);  // Small delay to prevent CPU spinning
}

ttsManager.Stop();                          // Stop immediately

// Destroy and release resources
ttsManager.CleanUp();
```

## Game Integration Examples

### Scene Transition with TTS

#### Switching to Gameplay Scene

```cpp
void SwitchToGamePlay()
{
    // Add TTS announcement for gameplay start
    if (ttsManager.GetPlaybackState() != TTSPlaybackState::STATE_ERROR) {
        ttsManager.SetSpeakerChannel(TTSSpeakerChannel::CHANNEL_BOTH);
        ttsManager.SetVoiceVolume(0.7f);
        ttsManager.PlayAsync(L"Starting gameplay!");
    }
    
    scene.SetGotoScene(SCENE_GAMEPLAY);
    scene.InitiateScene();
    scene.SetGotoScene(SCENE_NONE);
    
    // Restart the Loader Thread to load in required assets
    renderer->ResumeLoader();
}
```

#### Switching to Movie Intro Scene

```cpp
void SwitchToMovieIntro()
{
    // Add TTS announcement for movie intro
    if (ttsManager.GetPlaybackState() != TTSPlaybackState::STATE_ERROR) {
        ttsManager.SetSpeakerChannel(TTSSpeakerChannel::CHANNEL_BOTH);
        ttsManager.SetVoiceVolume(0.6f);
        ttsManager.PlayAsync(L"Playing introduction movie");
    }
    
    scene.SetGotoScene(SCENE_INTRO_MOVIE);
    scene.InitiateScene();
    scene.SetGotoScene(SCENE_NONE);
    fxManager.FadeToImage(3.0f, 0.06f);
    OpenMovieAndPlay();
    
    // Restart the Loader Thread to load in required assets
    renderer->ResumeLoader();
}
```

## Error Handling

### Checking TTS System State

```cpp
// Check if TTS system is in an error state before use
if (ttsManager.GetPlaybackState() != TTSPlaybackState::STATE_ERROR) {
    // Safe to use TTS functionality
    ttsManager.Play(L"System operational");
}
else {
    // Handle error state - perhaps log or use alternative notification
    debug.logLevelMessage(LogLevel::LOG_WARNING, L"TTS system in error state");
}
```

## Best Practices

1. **Always check initialization status** before using TTS functionality
2. **Use asynchronous playback** (`PlayAsync`) for non-critical announcements to avoid blocking gameplay
3. **Check playback state** before attempting operations to avoid errors
4. **Properly clean up resources** by calling `CleanUp()` before application shutdown
5. **Use appropriate volume levels** for different types of announcements
6. **Consider speaker channels** for spatial audio cues in games
7. **Handle errors gracefully** by checking `GetPlaybackState()` return values

## Configuration Options

| Setting | Method | Description |
|---------|--------|-------------|
| Volume | `SetVoiceVolume(float)` | Set volume level (0.0 to 1.0) |
| Pitch | `SetVoicePitch(float)` | Adjust voice pitch |
| Rate | `SetVoiceRate(float)` | Control speech speed |
| Channel | `SetSpeakerChannel(TTSSpeakerChannel)` | Choose output channel |
| Voice | `SetVoiceByName(std::wstring)` | Select specific voice |

## Playback States

The TTSManager supports the following playback states:

- `STATE_IDLE` - Not playing anything
- `STATE_PLAYING` - Currently speaking
- `STATE_PAUSED` - Playback is paused
- `STATE_ERROR` - System error occurred

Check these states using `GetPlaybackState()` to ensure proper operation.

## Voice tone (merged 2026-11-19, uncompiled)

`TTSManager::ProsodyForTone(TTSManager::Tone tone, bool female)` returns the `{ rate, pitch }` to feed `SetVoiceRate` / `SetVoicePitch` for what a line says: `Official` (calm, crisp), `Stern` (level, firm), `Warning` (brisk, low), `Threat` (slower, lower), `Taunt` (lazy, cynical), `Flat` (deep and level: machines, aliens). All values stay near the engine's natural rate so speech stays clean.

## Linux backend — eSpeak-ng (23 November 2026)

On Linux, text-to-speech uses eSpeak-ng 1.51 instead of SAPI. The synth callback resamples 22050 Hz mono to 44100 Hz stereo into the miniaudio ring, with backpressure so a long line cannot overrun the buffer. All 7 `main.cpp` TTS call sites are enabled on Linux. Test: 42 checks, 0 failures, 131 voices; speech captured to `~/workspace/cpge-linux-android/scope/tts-test.wav`.

Call it exactly as on Windows — the public API is unchanged; only the backend differs, selected by the platform guards at compile time. On Android, TTS used to be compiled out entirely until Phase D wired the JNI bridge — that landed 24 November 2026 (see below). Rule: use TTS where the platform supports it, disable it completely where it does not.

## Android backend — TextToSpeech via JNI (24 November 2026)

On Android there is no NDK TTS API, so the backend is the platform
`android.speech.tts.TextToSpeech` driven through JNI from the Java
helper `com.ultimanium.cpge.CpgeTts`
(`Android/app/src/main/java/com/ultimanium/cpge/CpgeTts.java`). The
native side lives in its own `#elif defined(PLATFORM_ANDROID)` section
of `TTSManager.cpp` (a full implementation, like the Windows SAPI
section) — `Initialize()` takes the `JavaVM` from the NativeActivity,
loads `CpgeTts` through the activity's own class loader (a plain
`FindClass` from an attached native thread uses the system class
loader, which cannot see app classes), and calls `CpgeTts.init()`.
`TextToSpeech` construction is async, so `Initialize()` waits up to
2 s for `isReady()`; a device without TTS data returns false and the
state is `ERROR`, so `main.cpp` treats TTS as unavailable instead of
dropping speech silently.

```cpp
// Same public API as Windows/Linux — no caller changes:
ttsManager.Initialize();                 // JNI bridge + TextToSpeech
ttsManager.SetVoiceByName(L"en-US");     // BCP-47 tag, not an eSpeak name
ttsManager.PlayAsync(L"Hello, Android"); // async; callback clears the state
ttsManager.Stop();
ttsManager.CleanUp();
```

Notes: speech is async on Android — `Play()` speaks then waits for the
utterance callback, `PlayAsync()` returns at once. Each utterance gets
a unique ID (`cpge-tts-<n>`) so a late callback for a flushed utterance
cannot clear the new one. Rate/pitch map from the engine's -10..+10
scale onto the platform 1.0 = normal scale (rate 1.0 + rate×0.1,
pitch 1.0 + pitch×0.05); volume goes per-utterance via
`KEY_PARAM_VOLUME` (best-effort — some engines ignore it). Voices are
BCP-47 tags (`GetAvailableVoices` returns the tags the engine reports).
There is no speaker-channel or pause API in Android TTS:
`SetSpeakerChannel` / `SetVoiceQuality` store the config with no
audible effect, `Pause` / `Resume` return false. The Java class ships
in `classes.dex` (built by `scope/apk/build-dex.sh`, packaged by
`scope/apk/package-apk.sh`; the manifest no longer declares
`hasCode="false"`). Verification: both Android builds link clean
(`android-opengl-build-10.log`, `android-vulkan-build-10.log`, exit 0);
no device exists to hear it, so the speech itself is still untested.
