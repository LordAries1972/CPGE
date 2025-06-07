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