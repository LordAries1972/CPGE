# SoundManager Class - Comprehensive Usage Guide

## Table of Contents

1.  [Overview](#overview)
2.  [Features](#features)
3.  [Prerequisites](#prerequisites)
4.  [Basic Setup](#basic-setup)
5.  [Initialization](#initialization)
6.  [Loading Sound Files](#loading-sound-files)

7.  [Playing Sounds](#playing-sounds)
    - [Immediate Playback](#immediate-playback)
    - [Queued Playback](#queued-playback)

8.  [Volume Control](#volume-control)
9.  [Stereo Balance](#stereo-balance)
10. [Sound Priorities](#sound-priorities)
11. [Fade Effects](#fade-effects)
12. [Cooldown System](#cooldown-system)
13. [Playback Types](#playback-types)
14. [Threading Model](#threading-model)
15. [Error Handling](#error-handling)
16. [Complete Example Applications](#complete-example-applications)
17. [Best Practices](#best-practices)
18. [Troubleshooting](#troubleshooting)
19. [API Reference](#api-reference)

---

## Overview

The `SoundManager` class is a DirectSound-based audio management system designed for real-time applications and games. It provides robust sound effect (SFX) playback with features like priority queuing, fade effects, cooldown management, and asynchronous processing.

The system was specifically designed to replace XAudio2 due to known reliability issues, offering a more stable DirectSound-based alternative while maintaining advanced features for professional audio management.  That been said, this is currently only developed for the Windows Platform and the other platforms are still yet to come in the very near future.

---

## Features

- **DirectSound Integration**: Stable, low-latency audio playback
- **Priority Queue System**: Manage sound playback order based on importance
- **Fade-In Effects**: Smooth volume transitions for professional audio quality
- **Cooldown Management**: Prevent audio spam and manage resource usage
- **Stereo Balance Control**: Left, right, and center audio positioning
- **Asynchronous Processing**: Non-blocking audio operations via worker thread
- **WAV File Support**: Built-in WAV parser with full format support
- **Volume Control**: Global and per-sound volume management
- **Loop Support**: Both one-shot and looping playback modes
- **Memory Efficient**: Smart preloading and cleanup systems

---

## Prerequisites

### System Requirements
- Windows 10 or higher (64-bit)
- DirectSound 8.0 or later
- Visual Studio 2019/2022 with C++17 support

### Dependencies
- `dsound.lib` - DirectSound library
- `dxguid.lib` - DirectX GUID definitions
- `winmm.lib` - Windows Multimedia API

### Required Headers
```cpp
#include "SoundManager.h"
#include "Debug.h"
#include <Windows.h>
```

---

## Basic Setup

### 1. Include Namespace
```cpp
using namespace SoundSystem;
```

### 2. Create SoundManager Instance
```cpp
SoundManager soundManager;
```

### 3. Window Handle Requirement
DirectSound requires a valid window handle (HWND). You can create a minimal window:

```cpp
// Create a minimal window for DirectSound
HWND CreateAudioWindow(HINSTANCE hInstance) {
    HWND hwnd = CreateWindowEx(
        0,                          // Extended window style
        L"STATIC",                  // Window class name
        L"AudioWindow",             // Window title
        WS_OVERLAPPEDWINDOW,        // Window style
        CW_USEDEFAULT,              // X position
        CW_USEDEFAULT,              // Y position
        100,                        // Width
        100,                        // Height
        nullptr,                    // Parent window
        nullptr,                    // Menu
        hInstance,                  // Instance handle
        nullptr                     // Additional application data
    );
    
    return hwnd;
}
```

---

## Initialization

### Basic Initialization
```cpp
bool InitializeAudio(HWND hwnd) {
    if (!soundManager.Initialize(hwnd)) {
        debug.logLevelMessage(LogLevel::LOG_CRITICAL, 
                            L"SoundManager failed to initialize");
        return false;
    }
    
    debug.logLevelMessage(LogLevel::LOG_INFO, 
                        L"SoundManager initialized successfully");
    return true;
}
```

### Complete Initialization Sequence
```cpp
bool SetupAudioSystem(HINSTANCE hInstance) {
    // 1. Create window handle
    HWND audioWindow = CreateAudioWindow(hInstance);
    if (!audioWindow) {
        debug.logLevelMessage(LogLevel::LOG_CRITICAL, 
                            L"Failed to create audio window");
        return false;
    }
    
    // 2. Initialize SoundManager
    if (!soundManager.Initialize(audioWindow)) {
        debug.logLevelMessage(LogLevel::LOG_CRITICAL, 
                            L"SoundManager initialization failed");
        DestroyWindow(audioWindow);
        return false;
    }
    
    // 3. Load all predefined sound effects
    soundManager.LoadAllSFX();
    
    // 4. Start asynchronous playback thread
    soundManager.StartPlaybackThread();
    
    // 5. Configure global settings
    soundManager.SetGlobalVolume(0.8f); // 80% volume
    
    debug.logLevelMessage(LogLevel::LOG_INFO, 
                        L"Audio system setup completed successfully");
    return true;
}
```

---

## Loading Sound Files

### Automatic Loading with LoadAllSFX()
The `LoadAllSFX()` method automatically loads all sounds defined in the internal mapping:

```cpp
// This loads all sounds from the predefined sfxFileNames map
soundManager.LoadAllSFX();
```

### Manual File Loading
For custom sound loading outside the predefined list:

```cpp
bool LoadCustomSound() {
    LoadedSFX customSfx;
    if (soundManager.LoadSFXFile(L"./Assets/custom_sound.wav", customSfx)) {
        // Manually add to fileList if needed
        soundManager.fileList[SFX_ID::SFX_CUSTOM] = customSfx;
        
        debug.logLevelMessage(LogLevel::LOG_INFO, 
                            L"Custom sound loaded successfully");
        return true;
    }
    
    debug.logLevelMessage(LogLevel::LOG_ERROR, 
                        L"Failed to load custom sound");
    return false;
}
```

### Adding New Sound IDs
To add new sounds, modify the enum and file mapping in SoundManager.h:

```cpp
// In SoundManager.h
enum class SFX_ID {
    SFX_INVALID = -1,
    SFX_CLICK = 1,
    SFX_BEEP = 2,
    SFX_ALERT = 3,        // New sound ID
    SFX_EXPLOSION = 4,    // New sound ID
    SFX_POWERUP = 5       // New sound ID
};

// Update the sfxFileNames map
const std::unordered_map<SFX_ID, std::wstring> sfxFileNames = {
    { SFX_ID::SFX_CLICK,     L"./Assets/click1.wav" },
    { SFX_ID::SFX_BEEP,      L"./Assets/beep1.wav" },
    { SFX_ID::SFX_ALERT,     L"./Assets/alert.wav" },
    { SFX_ID::SFX_EXPLOSION, L"./Assets/explosion.wav" },
    { SFX_ID::SFX_POWERUP,   L"./Assets/powerup.wav" }
};
```

---

## Playing Sounds

### Immediate Playback
For instant sound playback without queuing:

```cpp
void PlayButtonClick() {
    soundManager.PlayImmediateSFX(SFX_ID::SFX_CLICK);
}

void PlayBeepSound() {
    soundManager.PlayImmediateSFX(SFX_ID::SFX_BEEP);
}
```

### Queued Playback
For managed playback with priority and effects:

#### Basic Queue Usage
```cpp
void PlayQueuedSound() {
    soundManager.AddToQueue(
        SFX_ID::SFX_ALERT,              // Sound to play
        1.0f,                           // Volume (0.0 to 1.0)
        StereoBalance::BALANCE_CENTER,  // Stereo position
        PlaybackType::pbtSFX_Once       // Play once
    );
}
```

#### Advanced Queue Usage with All Parameters
```cpp
void PlayAdvancedQueuedSound() {
    soundManager.AddToQueue(
        SFX_ID::SFX_EXPLOSION,          // Sound ID
        0.9f,                           // Volume at 90%
        StereoBalance::BALANCE_LEFT,    // Play from left speaker
        PlaybackType::pbtSFX_Once,      // Single playback
        5.0f,                           // 5-second timeout
        SFX_PRIORITY::priHIGH,          // High priority
        true                            // Enable fade-in effect
    );
}
```

---

## Volume Control

### Global Volume Control
```cpp
// Set master volume to 75%
soundManager.SetGlobalVolume(0.75f);

// Mute all sounds
soundManager.SetGlobalVolume(0.0f);

// Maximum volume
soundManager.SetGlobalVolume(1.0f);
```

### Per-Sound Volume Control
Volume is specified when adding sounds to the queue:

```cpp
// Quiet background sound
soundManager.AddToQueue(SFX_ID::SFX_AMBIENT, 0.3f);

// Loud explosion sound
soundManager.AddToQueue(SFX_ID::SFX_EXPLOSION, 1.0f);

// Medium volume UI sound
soundManager.AddToQueue(SFX_ID::SFX_CLICK, 0.6f);
```

### Dynamic Volume Adjustment Example
```cpp
class VolumeController {
private:
    float masterVolume = 0.8f;
    
public:
    void IncreaseMasterVolume() {
        masterVolume = std::min(1.0f, masterVolume + 0.1f);
        soundManager.SetGlobalVolume(masterVolume);
        
        debug.logLevelMessage(LogLevel::LOG_INFO, 
                            L"Master volume: " + std::to_wstring(masterVolume));
    }
    
    void DecreaseMasterVolume() {
        masterVolume = std::max(0.0f, masterVolume - 0.1f);
        soundManager.SetGlobalVolume(masterVolume);
        
        debug.logLevelMessage(LogLevel::LOG_INFO, 
                            L"Master volume: " + std::to_wstring(masterVolume));
    }
};
```

---

## Stereo Balance

Control the stereo positioning of sounds:

```cpp
// Play sound from center (default)
soundManager.AddToQueue(SFX_ID::SFX_CLICK, 1.0f, 
                       StereoBalance::BALANCE_CENTER);

// Play sound from left speaker
soundManager.AddToQueue(SFX_ID::SFX_FOOTSTEP_LEFT, 1.0f, 
                       StereoBalance::BALANCE_LEFT);

// Play sound from right speaker
soundManager.AddToQueue(SFX_ID::SFX_FOOTSTEP_RIGHT, 1.0f, 
                       StereoBalance::BALANCE_RIGHT);
```

### Positional Audio Example
```cpp
void PlayPositionalSound(float playerX, float soundX) {
    StereoBalance balance = StereoBalance::BALANCE_CENTER;
    
    float distance = soundX - playerX;
    if (distance < -50.0f) {
        balance = StereoBalance::BALANCE_LEFT;
    } else if (distance > 50.0f) {
        balance = StereoBalance::BALANCE_RIGHT;
    }
    
    soundManager.AddToQueue(SFX_ID::SFX_AMBIENT, 0.7f, balance);
}
```

---

## Sound Priorities

The priority system ensures important sounds play before less important ones:

### Priority Levels (Highest to Lowest)
1. `priIMMEDIATELY` - Critical system sounds
2. `priDELAYED_START` - Important delayed sounds
3. `priHIGH` - High priority game events
4. `priABOVE_NORMAL` - Above normal priority
5. `priNORMAL` - Standard priority (default)
6. `priBELOW_NORMAL` - Below normal priority
7. `priLOWEST` - Background/ambient sounds

### Priority Usage Examples
```cpp
// Critical error sound - plays immediately
soundManager.AddToQueue(SFX_ID::SFX_ERROR, 1.0f, 
                       StereoBalance::BALANCE_CENTER,
                       PlaybackType::pbtSFX_Once, 0.0f,
                       SFX_PRIORITY::priIMMEDIATELY);

// Important game event
soundManager.AddToQueue(SFX_ID::SFX_LEVELUP, 0.9f,
                       StereoBalance::BALANCE_CENTER,
                       PlaybackType::pbtSFX_Once, 0.0f,
                       SFX_PRIORITY::priHIGH);

// Background ambient sound
soundManager.AddToQueue(SFX_ID::SFX_WIND, 0.4f,
                       StereoBalance::BALANCE_CENTER,
                       PlaybackType::pbtSFX_Loop, 0.0f,
                       SFX_PRIORITY::priLOWEST);
```

### Dynamic Priority System
```cpp
class GameAudioManager {
public:
    void PlayUISound() {
        soundManager.AddToQueue(SFX_ID::SFX_CLICK, 0.7f,
                               StereoBalance::BALANCE_CENTER,
                               PlaybackType::pbtSFX_Once, 0.0f,
                               SFX_PRIORITY::priNORMAL);
    }
    
    void PlayCombatSound() {
        soundManager.AddToQueue(SFX_ID::SFX_SWORD_CLASH, 0.9f,
                               StereoBalance::BALANCE_CENTER,
                               PlaybackType::pbtSFX_Once, 0.0f,
                               SFX_PRIORITY::priHIGH);
    }
    
    void PlayCriticalAlert() {
        soundManager.AddToQueue(SFX_ID::SFX_CRITICAL_ALERT, 1.0f,
                               StereoBalance::BALANCE_CENTER,
                               PlaybackType::pbtSFX_Once, 0.0f,
                               SFX_PRIORITY::priIMMEDIATELY);
    }
};
```

---

## Fade Effects

### Fade-In Effect
Create smooth volume transitions for professional audio quality:

```cpp
// Basic fade-in
soundManager.AddToQueue(SFX_ID::SFX_MUSIC, 0.8f,
                       StereoBalance::BALANCE_CENTER,
                       PlaybackType::pbtSFX_Loop, 0.0f,
                       SFX_PRIORITY::priNORMAL,
                       true);  // Enable fade-in

// Ambient sound with fade-in
soundManager.AddToQueue(SFX_ID::SFX_RAIN, 0.5f,
                       StereoBalance::BALANCE_CENTER,
                       PlaybackType::pbtSFX_Loop, 0.0f,
                       SFX_PRIORITY::priBELOW_NORMAL,
                       true);  // Smooth fade-in
```

### Fade Configuration
The fade-in duration is currently set to 250ms but can be modified in the source code:

```cpp
// In SoundManager.cpp, modify this value:
constexpr DWORD fadeInDurationMs = 250; // Milliseconds
```

### Practical Fade Usage
```cpp
class MusicManager {
public:
    void StartBackgroundMusic() {
        // Start music with smooth fade-in
        soundManager.AddToQueue(SFX_ID::SFX_BGM_LEVEL1, 0.6f,
                               StereoBalance::BALANCE_CENTER,
                               PlaybackType::pbtSFX_Loop, 0.0f,
                               SFX_PRIORITY::priBELOW_NORMAL,
                               true);  // Fade-in enabled
        
        debug.logLevelMessage(LogLevel::LOG_INFO, 
                            L"Background music started with fade-in");
    }
    
    void TransitionToNewTrack() {
        // Note: For fade-out, you would need to implement custom logic
        // or modify the SoundManager to support fade-out effects
        
        StartBackgroundMusic(); // New track with fade-in
    }
};
```

---

## Cooldown System

Prevent audio spam and manage resource usage with the cooldown system:

### Setting Cooldowns
```cpp
// Set 2-second cooldown on click sound
soundManager.SetCooldown(SFX_ID::SFX_CLICK, 2.0f);

// Set 5-second cooldown on explosion sound
soundManager.SetCooldown(SFX_ID::SFX_EXPLOSION, 5.0f);

// Set 0.5-second cooldown on rapid-fire weapon
soundManager.SetCooldown(SFX_ID::SFX_LASER, 0.5f);
```

### Clearing Cooldowns
```cpp
// Remove cooldown restriction
soundManager.ClearCooldown(SFX_ID::SFX_CLICK);

// Emergency clear - allows immediate replay
soundManager.ClearCooldown(SFX_ID::SFX_ALERT);
```

### Practical Cooldown Example
```cpp
class WeaponAudio {
private:
    bool rapidFireMode = false;
    
public:
    void InitializeWeaponAudio() {
        if (rapidFireMode) {
            // Rapid fire - short cooldown
            soundManager.SetCooldown(SFX_ID::SFX_GUNSHOT, 0.1f);
        } else {
            // Single shot - longer cooldown for realism
            soundManager.SetCooldown(SFX_ID::SFX_GUNSHOT, 0.5f);
        }
    }
    
    void FireWeapon() {
        // The cooldown system automatically prevents spam
        soundManager.AddToQueue(SFX_ID::SFX_GUNSHOT, 0.8f,
                               StereoBalance::BALANCE_CENTER,
                               PlaybackType::pbtSFX_Once, 0.0f,
                               SFX_PRIORITY::priHIGH);
    }
    
    void ToggleRapidFire() {
        rapidFireMode = !rapidFireMode;
        soundManager.ClearCooldown(SFX_ID::SFX_GUNSHOT);
        InitializeWeaponAudio();
    }
};
```

---

## Playback Types

### Single Playback (pbtSFX_Once)
Play sound once and stop:

```cpp
// UI click sound
soundManager.AddToQueue(SFX_ID::SFX_CLICK, 1.0f,
                       StereoBalance::BALANCE_CENTER,
                       PlaybackType::pbtSFX_Once);

// Explosion effect
soundManager.AddToQueue(SFX_ID::SFX_EXPLOSION, 0.9f,
                       StereoBalance::BALANCE_CENTER,
                       PlaybackType::pbtSFX_Once);
```

### Looping Playback (pbtSFX_Loop)
Continuously loop until stopped or timeout:

```cpp
// Background ambient sound
soundManager.AddToQueue(SFX_ID::SFX_WIND, 0.4f,
                       StereoBalance::BALANCE_CENTER,
                       PlaybackType::pbtSFX_Loop);

// Engine sound with 30-second timeout
soundManager.AddToQueue(SFX_ID::SFX_ENGINE, 0.7f,
                       StereoBalance::BALANCE_CENTER,
                       PlaybackType::pbtSFX_Loop,
                       30.0f);  // Timeout after 30 seconds
```

### Timeout Management
```cpp
class AmbientSoundManager {
public:
    void StartLevelAmbient() {
        // Play ambient sound for 120 seconds
        soundManager.AddToQueue(SFX_ID::SFX_FOREST_AMBIENT, 0.5f,
                               StereoBalance::BALANCE_CENTER,
                               PlaybackType::pbtSFX_Loop,
                               120.0f,  // 2 minutes timeout
                               SFX_PRIORITY::priBELOW_NORMAL);
    }
    
    void StartTemporaryEffect() {
        // Short-term looping effect
        soundManager.AddToQueue(SFX_ID::SFX_MAGIC_BARRIER, 0.6f,
                               StereoBalance::BALANCE_CENTER,
                               PlaybackType::pbtSFX_Loop,
                               10.0f,  // 10 seconds only
                               SFX_PRIORITY::priNORMAL);
    }
};
```

---

## Threading Model

The SoundManager uses asynchronous processing for non-blocking audio operations:

### Starting the Audio Thread
```cpp
void StartAudioSystem() {
    // Initialize SoundManager first
    if (soundManager.Initialize(hwnd)) {
        soundManager.LoadAllSFX();
        
        // Start the asynchronous processing thread
        soundManager.StartPlaybackThread();
        
        debug.logLevelMessage(LogLevel::LOG_INFO, 
                            L"Audio thread started successfully");
    }
}
```

### Stopping the Audio Thread
```cpp
void ShutdownAudioSystem() {
    // Stop the worker thread gracefully
    soundManager.StopPlaybackThread();
    
    // Clean up resources
    soundManager.CleanUp();
    
    debug.logLevelMessage(LogLevel::LOG_INFO, 
                        L"Audio system shutdown completed");
}
```

### Thread-Safe Operations
All public methods are thread-safe and can be called from any thread:

```cpp
// Safe to call from game thread
void OnGameEvent() {
    soundManager.AddToQueue(SFX_ID::SFX_SCORE, 0.8f);
}

// Safe to call from UI thread
void OnButtonClick() {
    soundManager.PlayImmediateSFX(SFX_ID::SFX_CLICK);
}

// Safe to call from network thread
void OnNetworkEvent() {
    soundManager.AddToQueue(SFX_ID::SFX_MESSAGE, 0.6f,
                           StereoBalance::BALANCE_CENTER,
                           PlaybackType::pbtSFX_Once, 0.0f,
                           SFX_PRIORITY::priNORMAL);
}
```

---

## Error Handling

### Initialization Error Handling
```cpp
bool SafeInitializeAudio(HWND hwnd) {
    try {
        if (!soundManager.Initialize(hwnd)) {
            debug.logLevelMessage(LogLevel::LOG_CRITICAL, 
                                L"DirectSound initialization failed");
            return false;
        }
        
        soundManager.LoadAllSFX();
        soundManager.StartPlaybackThread();
        
        return true;
    }
    catch (const std::exception& e) {
        std::string error = e.what();
        std::wstring werror(error.begin(), error.end());
        debug.logLevelMessage(LogLevel::LOG_CRITICAL, 
                            L"Audio initialization exception: " + werror);
        return false;
    }
}
```

### Runtime Error Recovery
```cpp
class AudioErrorHandler {
public:
    bool ValidateSoundSystem() {
        // Check if sound system is responsive
        try {
            soundManager.PlayImmediateSFX(SFX_ID::SFX_CLICK);
            return true;
        }
        catch (...) {
            debug.logLevelMessage(LogLevel::LOG_ERROR, 
                                L"Sound system validation failed");
            return false;
        }
    }
    
    void HandleAudioFailure() {
        debug.logLevelMessage(LogLevel::LOG_WARNING, 
                            L"Attempting audio system recovery");
        
        // Stop current operations
        soundManager.StopPlaybackThread();
        
        // Restart the system
        soundManager.StartPlaybackThread();
        
        debug.logLevelMessage(LogLevel::LOG_INFO, 
                            L"Audio system recovery completed");
    }
};
```

### File Loading Error Handling
```cpp
bool SafeLoadCustomSounds() {
    std::vector<std::pair<SFX_ID, std::wstring>> customSounds = {
        {SFX_ID::SFX_CUSTOM1, L"./Assets/custom1.wav"},
        {SFX_ID::SFX_CUSTOM2, L"./Assets/custom2.wav"},
        {SFX_ID::SFX_CUSTOM3, L"./Assets/custom3.wav"}
    };
    
    bool allLoaded = true;
    
    for (const auto& sound : customSounds) {
        LoadedSFX sfx;
        if (!soundManager.LoadSFXFile(sound.second, sfx)) {
            debug.logLevelMessage(LogLevel::LOG_WARNING, 
                                L"Failed to load: " + sound.second);
            allLoaded = false;
            continue;
        }
        
        soundManager.fileList[sound.first] = sfx;
        debug.logLevelMessage(LogLevel::LOG_INFO, 
                            L"Loaded: " + sound.second);
    }
    
    return allLoaded;
}
```

---

## Complete Example Applications

### Example 1: Simple Game Audio
```cpp
#include "SoundManager.h"
#include <Windows.h>

using namespace SoundSystem;

class SimpleGameAudio {
private:
    SoundManager soundManager;
    HWND audioWindow;
    
public:
    bool Initialize(HINSTANCE hInstance) {
        // Create window for DirectSound
        audioWindow = CreateWindowEx(0, L"STATIC", L"GameAudio",
                                    WS_OVERLAPPEDWINDOW,
                                    CW_USEDEFAULT, CW_USEDEFAULT,
                                    100, 100, nullptr, nullptr,
                                    hInstance, nullptr);
        
        if (!audioWindow) return false;
        
        // Initialize sound system
        if (!soundManager.Initialize(audioWindow)) {
            DestroyWindow(audioWindow);
            return false;
        }
        
        // Load sounds and start processing
        soundManager.LoadAllSFX();
        soundManager.StartPlaybackThread();
        
        // Configure audio settings
        soundManager.SetGlobalVolume(0.8f);
        soundManager.SetCooldown(SFX_ID::SFX_CLICK, 0.5f);
        
        return true;
    }
    
    void PlayButtonSound() {
        soundManager.PlayImmediateSFX(SFX_ID::SFX_CLICK);
    }
    
    void PlayGameSound() {
        soundManager.AddToQueue(SFX_ID::SFX_BEEP, 0.7f,
                               StereoBalance::BALANCE_CENTER,
                               PlaybackType::pbtSFX_Once, 0.0f,
                               SFX_PRIORITY::priNORMAL);
    }
    
    void Cleanup() {
        soundManager.StopPlaybackThread();
        soundManager.CleanUp();
        if (audioWindow) {
            DestroyWindow(audioWindow);
            audioWindow = nullptr;
        }
    }
    
    ~SimpleGameAudio() {
        Cleanup();
    }
};

// Usage in WinMain
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    SimpleGameAudio gameAudio;
    
    if (!gameAudio.Initialize(hInstance)) {
        return -1;
    }
    
    // Simulate game events
    gameAudio.PlayButtonSound();
    Sleep(1000);
    gameAudio.PlayGameSound();
    Sleep(2000);
    
    return 0;
}
```

### Example 2: Advanced Audio Manager
```cpp
#include "SoundManager.h"
#include <Windows.h>
#include <map>

using namespace SoundSystem;

class AdvancedAudioManager {
private:
    SoundManager soundManager;
    HWND audioWindow;
    std::map<std::string, float> volumeCategories;
    
public:
    bool Initialize(HINSTANCE hInstance) {
        // Setup volume categories
        volumeCategories["UI"] = 0.8f;
        volumeCategories["SFX"] = 0.9f;
        volumeCategories["Ambient"] = 0.4f;
        volumeCategories["Music"] = 0.6f;
        
        // Create window
        audioWindow = CreateWindowEx(0, L"STATIC", L"AdvancedAudio",
                                    WS_OVERLAPPEDWINDOW,
                                    CW_USEDEFAULT, CW_USEDEFAULT,
                                    100, 100, nullptr, nullptr,
                                    hInstance, nullptr);
        
        if (!audioWindow) return false;
        
        // Initialize sound system
        if (!soundManager.Initialize(audioWindow)) {
            DestroyWindow(audioWindow);
            return false;
        }
        
        soundManager.LoadAllSFX();
        soundManager.StartPlaybackThread();
        SetupAudioEnvironment();
        
        return true;
    }
    
    void SetupAudioEnvironment() {
        // Configure master volume
        soundManager.SetGlobalVolume(0.85f);
        
        // Setup cooldowns for different sound types
        soundManager.SetCooldown(SFX_ID::SFX_CLICK, 0.2f);    // UI sounds
        soundManager.SetCooldown(SFX_ID::SFX_BEEP, 1.0f);     // Alert sounds
    }
    
    void PlayUISound(SFX_ID id) {
        float volume = volumeCategories["UI"];
        soundManager.AddToQueue(id, volume,
                               StereoBalance::BALANCE_CENTER,
                               PlaybackType::pbtSFX_Once, 0.0f,
                               SFX_PRIORITY::priNORMAL);
    }
    
    void PlaySFXSound(SFX_ID id, StereoBalance balance = StereoBalance::BALANCE_CENTER) {
        float volume = volumeCategories["SFX"];
        soundManager.AddToQueue(id, volume, balance,
                               PlaybackType::pbtSFX_Once, 0.0f,
                               SFX_PRIORITY::priHIGH);
    }
    
    void PlayAmbientSound(SFX_ID id, float timeout = 0.0f) {
        float volume = volumeCategories["Ambient"];
        soundManager.AddToQueue(id, volume,
                               StereoBalance::BALANCE_CENTER,
                               PlaybackType::pbtSFX_Loop, timeout,
                               SFX_PRIORITY::priBELOW_NORMAL, true);
    }
    
    void PlayMusicWithFade(SFX_ID id) {
        float volume = volumeCategories["Music"];
        soundManager.AddToQueue(id, volume,
                               StereoBalance::BALANCE_CENTER,
                               PlaybackType::pbtSFX_Loop, 0.0f,
                               SFX_PRIORITY::priBELOW_NORMAL, true);
    }
    
    void SetCategoryVolume(const std::string& category, float volume) {
        volumeCategories[category] = std::clamp(volume, 0.0f, 1.0f);
        debug.logLevelMessage(LogLevel::LOG_INFO, 
                            L"Volume category '" + std::wstring(category.begin(), category.end()) + 
                            L"' set to: " + std::to_wstring(volume));
    }
    
    void EmergencyStopAll() {
        soundManager.StopPlaybackThread();
        soundManager.StartPlaybackThread();
        debug.logLevelMessage(LogLevel::LOG_INFO, L"Emergency audio restart completed");
    }
    
    void Cleanup() {
        soundManager.StopPlaybackThread();
        soundManager.CleanUp();
        if (audioWindow) {
            DestroyWindow(audioWindow);
            audioWindow = nullptr;
        }
    }
    
    ~AdvancedAudioManager() {
        Cleanup();
    }
};

// Usage example
void DemonstrateAdvancedAudio() {
    AdvancedAudioManager audioMgr;
    
    if (!audioMgr.Initialize(GetModuleHandle(nullptr))) {
        return;
    }
    
    // Play different categories of sounds
    audioMgr.PlayUISound(SFX_ID::SFX_CLICK);
    Sleep(500);
    
    audioMgr.PlaySFXSound(SFX_ID::SFX_BEEP, StereoBalance::BALANCE_LEFT);
    Sleep(500);
    
    audioMgr.PlayAmbientSound(SFX_ID::SFX_WIND, 10.0f); // 10 second ambient
    Sleep(1000);
    
    // Adjust category volumes
    audioMgr.SetCategoryVolume("UI", 0.5f);
    audioMgr.SetCategoryVolume("SFX", 1.0f);
    
    Sleep(5000); // Let sounds play
}
```

### Example 3: Real-Time Game Audio System
```cpp
class GameAudioSystem {
private:
    SoundManager soundManager;
    HWND gameWindow;
    std::atomic<bool> gameRunning{true};
    std::thread gameThread;
    
public:
    bool Initialize(HWND window) {
        gameWindow = window;
        
        if (!soundManager.Initialize(gameWindow)) {
            debug.logLevelMessage(LogLevel::LOG_CRITICAL, 
                                L"Game audio initialization failed");
            return false;
        }
        
        soundManager.LoadAllSFX();
        soundManager.StartPlaybackThread();
        SetupGameAudio();
        
        // Start game simulation thread
        gameThread = std::thread(&GameAudioSystem::GameLoop, this);
        
        return true;
    }
    
    void SetupGameAudio() {
        soundManager.SetGlobalVolume(0.8f);
        
        // Setup realistic cooldowns
        soundManager.SetCooldown(SFX_ID::SFX_GUNSHOT, 0.1f);    // Rapid fire
        soundManager.SetCooldown(SFX_ID::SFX_FOOTSTEP, 0.3f);   // Walking pace
        soundManager.SetCooldown(SFX_ID::SFX_JUMP, 1.0f);       // Jump cooldown
        soundManager.SetCooldown(SFX_ID::SFX_PICKUP, 0.5f);     // Item pickup
    }
    
    void GameLoop() {
        while (gameRunning) {
            // Simulate game events
            SimulatePlayerActions();
            SimulateEnvironmentalSounds();
            SimulateEnemyActions();
            
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    
    void SimulatePlayerActions() {
        static int actionCounter = 0;
        actionCounter++;
        
        if (actionCounter % 30 == 0) { // Every 3 seconds
            PlayFootstepSound();
        }
        
        if (actionCounter % 50 == 0) { // Every 5 seconds
            FireWeapon();
        }
        
        if (actionCounter % 100 == 0) { // Every 10 seconds
            PickupItem();
        }
    }
    
    void SimulateEnvironmentalSounds() {
        static bool ambientPlaying = false;
        static int envCounter = 0;
        envCounter++;
        
        if (!ambientPlaying && envCounter > 20) {
            PlayAmbientWind();
            ambientPlaying = true;
            envCounter = 0;
        }
    }
    
    void SimulateEnemyActions() {
        static int enemyCounter = 0;
        enemyCounter++;
        
        if (enemyCounter % 80 == 0) { // Every 8 seconds
            PlayEnemySound();
        }
    }
    
    void PlayFootstepSound() {
        soundManager.AddToQueue(SFX_ID::SFX_FOOTSTEP, 0.6f,
                               StereoBalance::BALANCE_CENTER,
                               PlaybackType::pbtSFX_Once, 0.0f,
                               SFX_PRIORITY::priNORMAL);
    }
    
    void FireWeapon() {
        soundManager.AddToQueue(SFX_ID::SFX_GUNSHOT, 0.9f,
                               StereoBalance::BALANCE_CENTER,
                               PlaybackType::pbtSFX_Once, 0.0f,
                               SFX_PRIORITY::priHIGH);
    }
    
    void PickupItem() {
        soundManager.AddToQueue(SFX_ID::SFX_PICKUP, 0.7f,
                               StereoBalance::BALANCE_CENTER,
                               PlaybackType::pbtSFX_Once, 0.0f,
                               SFX_PRIORITY::priABOVE_NORMAL);
    }
    
    void PlayAmbientWind() {
        soundManager.AddToQueue(SFX_ID::SFX_WIND, 0.3f,
                               StereoBalance::BALANCE_CENTER,
                               PlaybackType::pbtSFX_Loop, 15.0f, // 15 second loop
                               SFX_PRIORITY::priLOWEST, true);   // With fade-in
    }
    
    void PlayEnemySound() {
        // Randomize left/right positioning
        StereoBalance balance = (rand() % 2 == 0) ? 
                               StereoBalance::BALANCE_LEFT : 
                               StereoBalance::BALANCE_RIGHT;
        
        soundManager.AddToQueue(SFX_ID::SFX_ENEMY_GROWL, 0.8f, balance,
                               PlaybackType::pbtSFX_Once, 0.0f,
                               SFX_PRIORITY::priHIGH);
    }
    
    void Shutdown() {
        gameRunning = false;
        if (gameThread.joinable()) {
            gameThread.join();
        }
        
        soundManager.StopPlaybackThread();
        soundManager.CleanUp();
    }
    
    ~GameAudioSystem() {
        Shutdown();
    }
};
```

---

## Best Practices

### 1. Resource Management
```cpp
class AudioResourceManager {
public:
    // Always initialize in proper order
    bool InitializeAudioSystem(HWND hwnd) {
        if (!soundManager.Initialize(hwnd)) return false;
        soundManager.LoadAllSFX();              // Load after init
        soundManager.StartPlaybackThread();     // Start thread last
        return true;
    }
    
    // Always cleanup in reverse order
    void CleanupAudioSystem() {
        soundManager.StopPlaybackThread();      // Stop thread first
        soundManager.CleanUp();                 // Cleanup resources last
    }
};
```

### 2. Performance Optimization
```cpp
class AudioPerformanceManager {
private:
    std::chrono::steady_clock::time_point lastOptimization;
    
public:
    void OptimizeAudioPerformance() {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            now - lastOptimization).count();
        
        if (elapsed > 30) { // Optimize every 30 seconds
            // Clear old cooldown entries
            ClearExpiredCooldowns();
            
            // Adjust priorities based on current game state
            AdjustPrioritiesForGameState();
            
            lastOptimization = now;
        }
    }
    
private:
    void ClearExpiredCooldowns() {
        // Implementation would clear old cooldown entries
        // This prevents memory buildup in long-running games
    }
    
    void AdjustPrioritiesForGameState() {
        // Dynamically adjust audio priorities based on game state
        // For example, lower ambient priority during combat
    }
};
```

### 3. Error Recovery
```cpp
class AudioErrorRecovery {
public:
    bool RecoverFromAudioFailure() {
        debug.logLevelMessage(LogLevel::LOG_WARNING, 
                            L"Attempting audio system recovery");
        
        try {
            // Stop current operations
            soundManager.StopPlaybackThread();
            
            // Small delay to ensure cleanup
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            
            // Restart the thread
            soundManager.StartPlaybackThread();
            
            // Test the system
            soundManager.PlayImmediateSFX(SFX_ID::SFX_CLICK);
            
            debug.logLevelMessage(LogLevel::LOG_INFO, 
                                L"Audio system recovery successful");
            return true;
        }
        catch (...) {
            debug.logLevelMessage(LogLevel::LOG_CRITICAL, 
                                L"Audio system recovery failed");
            return false;
        }
    }
};
```

### 4. Memory Management
```cpp
class AudioMemoryManager {
public:
    void PreloadCriticalSounds() {
        // Load only essential sounds at startup
        std::vector<SFX_ID> criticalSounds = {
            SFX_ID::SFX_CLICK,
            SFX_ID::SFX_ERROR,
            SFX_ID::SFX_SUCCESS
        };
        
        for (SFX_ID id : criticalSounds) {
            // Ensure these are loaded
            if (soundManager.fileList.find(id) == soundManager.fileList.end()) {
                debug.logLevelMessage(LogLevel::LOG_WARNING, 
                                    L"Critical sound not loaded: " + 
                                    std::to_wstring(static_cast<int>(id)));
            }
        }
    }
    
    void LoadSoundsOnDemand(const std::vector<SFX_ID>& soundsNeeded) {
        // Load additional sounds as needed
        for (SFX_ID id : soundsNeeded) {
            if (soundManager.fileList.find(id) == soundManager.fileList.end()) {
                // Load this sound file
                LoadSpecificSound(id);
            }
        }
    }
    
private:
    void LoadSpecificSound(SFX_ID id) {
        // Implementation to load a specific sound file
        // This would use the internal filename mapping
    }
};
```

---

## Troubleshooting

### Common Issues and Solutions

#### 1. DirectSound Initialization Failure
**Problem**: `Initialize()` returns false
**Causes**:
- Invalid window handle
- DirectSound not available
- Hardware acceleration disabled

**Solutions**:
```cpp
bool TroubleshootInitialization(HWND hwnd) {
    // Verify window handle
    if (!IsWindow(hwnd)) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, 
                            L"Invalid window handle provided");
        return false;
    }
    
    // Try different cooperative levels
    HRESULT hr = DirectSoundCreate8(NULL, &testDS, NULL);
    if (FAILED(hr)) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, 
                            L"DirectSound device creation failed");
        return false;
    }
    
    // Test cooperative level
    hr = testDS->SetCooperativeLevel(hwnd, DSSCL_NORMAL);
    if (FAILED(hr)) {
        debug.logLevelMessage(LogLevel::LOG_ERROR, 
                            L"Cooperative level failed, trying DSSCL_NORMAL");
    }
    
    testDS->Release();
    return SUCCEEDED(hr);
}
```

#### 2. No Sound Output
**Problem**: Sounds appear to play but no audio is heard
**Causes**:
- Volume set to 0
- Audio device muted
- Wrong audio format

**Solutions**:
```cpp
void DiagnoseNoSoundOutput() {
    // Check global volume
    debug.logLevelMessage(LogLevel::LOG_DEBUG, 
                        L"Checking global volume settings");
    
    // Test with maximum volume
    soundManager.SetGlobalVolume(1.0f);
    
    // Play test sound with known settings
    soundManager.PlayImmediateSFX(SFX_ID::SFX_CLICK);
    
    // Verify sound file format
    auto it = soundManager.fileList.find(SFX_ID::SFX_CLICK);
    if (it != soundManager.fileList.end()) {
        const auto& format = it->second.waveFormat;
        debug.logLevelMessage(LogLevel::LOG_DEBUG, 
                            L"Audio format: " + std::to_wstring(format.nSamplesPerSec) +
                            L"Hz, " + std::to_wstring(format.wBitsPerSample) + 
                            L" bits, " + std::to_wstring(format.nChannels) + L" channels");
    }
}
```

#### 3. Audio Stuttering or Crackling
**Problem**: Audio playback is choppy or distorted
**Causes**:
- Buffer underrun
- High CPU usage
- Thread scheduling issues

**Solutions**:
```cpp
void ReduceAudioLatency() {
    // Reduce queue processing frequency if needed
    // This would require modifying the internal sleep duration
    
    // Priority boost for audio thread (Windows-specific)
    HANDLE currentThread = GetCurrentThread();
    SetThreadPriority(currentThread, THREAD_PRIORITY_ABOVE_NORMAL);
    
    // Reduce non-essential audio operations
    soundManager.SetGlobalVolume(0.8f); // Slightly lower volume reduces processing
    
    debug.logLevelMessage(LogLevel::LOG_INFO, 
                        L"Audio latency reduction measures applied");
}
```

#### 4. Memory Leaks
**Problem**: Memory usage increases over time
**Causes**:
- Sounds not properly released
- Queue items accumulating
- Thread not cleaning up

**Solutions**:
```cpp
class AudioMemoryMonitor {
private:
    size_t lastQueueSize = 0;
    std::chrono::steady_clock::time_point lastCheck;
    
public:
    void MonitorMemoryUsage() {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            now - lastCheck).count();
        
        if (elapsed > 10) { // Check every 10 seconds
            size_t currentQueueSize = GetCurrentQueueSize();
            
            if (currentQueueSize > lastQueueSize + 100) {
                debug.logLevelMessage(LogLevel::LOG_WARNING, 
                                    L"Audio queue size growing: " + 
                                    std::to_wstring(currentQueueSize));
                
                // Potential memory leak - restart audio system
                RecoverAudioSystem();
            }
            
            lastQueueSize = currentQueueSize;
            lastCheck = now;
        }
    }
    
private:
    size_t GetCurrentQueueSize() {
        // This would require access to internal queue size
        // Implementation depends on exposing queue size publicly
        return 0;
    }
    
    void RecoverAudioSystem() {
        soundManager.StopPlaybackThread();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        soundManager.StartPlaybackThread();
    }
};
```

---

## API Reference

### Core Methods

#### `bool Initialize(HWND hwnd)`
Initializes the DirectSound system with the provided window handle.
- **Parameters**: `hwnd` - Valid window handle for DirectSound
- **Returns**: `true` if successful, `false` otherwise
- **Thread Safety**: Not thread-safe, call during initialization only

#### `void CleanUp()`
Releases all DirectSound resources and shuts down the audio system.
- **Thread Safety**: Not thread-safe, call during shutdown only
- **Note**: Automatically called in destructor

#### `void LoadAllSFX()`
Loads all sound files defined in the internal `sfxFileNames` mapping.
- **Thread Safety**: Not thread-safe, call during initialization
- **Note**: Should be called after `Initialize()` but before starting playback

#### `void StartPlaybackThread()`
Starts the asynchronous audio processing thread.
- **Thread Safety**: Thread-safe
- **Note**: Must be called after `LoadAllSFX()`

#### `void StopPlaybackThread()`
Stops the audio processing thread gracefully.
- **Thread Safety**: Thread-safe
- **Note**: Blocks until thread terminates

### Sound Playback Methods

#### `void PlayImmediateSFX(SFX_ID id)`
Plays a sound immediately without queuing or priority management.
- **Parameters**: `id` - Sound identifier to play
- **Thread Safety**: Thread-safe
- **Use Case**: Simple UI sounds, immediate feedback

#### `void AddToQueue(SFX_ID id, float volume, StereoBalance balance, PlaybackType type, float timeout)`
Adds a sound to the priority queue with basic parameters.
- **Parameters**:
  - `id` - Sound identifier
  - `volume` - Volume level (0.0 to 1.0)
  - `balance` - Stereo positioning
  - `type` - Playback mode (once or loop)
  - `timeout` - Maximum playback duration in seconds (0 = no limit)
- **Thread Safety**: Thread-safe

#### `void AddToQueue(SFX_ID id, float volume, StereoBalance balance, PlaybackType type, float timeout, SFX_PRIORITY priority, bool useFadeIn)`
Advanced queue method with priority and fade-in support.
- **Additional Parameters**:
  - `priority` - Queue priority level
  - `useFadeIn` - Enable smooth volume fade-in
- **Thread Safety**: Thread-safe

### Volume and Effect Methods

#### `void SetGlobalVolume(float volume)`
Sets the master volume for all audio output.
- **Parameters**: `volume` - Global volume multiplier (0.0 to 1.0)
- **Thread Safety**: Thread-safe
- **Note**: Affects all currently playing and future sounds

#### `void SetCooldown(SFX_ID id, float seconds)`
Sets a cooldown period for a specific sound to prevent spam.
- **Parameters**:
  - `id` - Sound identifier
  - `seconds` - Cooldown duration
- **Thread Safety**: Thread-safe

#### `void ClearCooldown(SFX_ID id)`
Removes the cooldown restriction for a specific sound.
- **Parameters**: `id` - Sound identifier
- **Thread Safety**: Thread-safe

### Enumerations

#### `SFX_ID`
Sound identifiers for the audio system:
- `SFX_INVALID` - Invalid/uninitialized sound
- `SFX_CLICK` - UI click sound
- `SFX_BEEP` - Alert/notification sound

#### `PlaybackType`
Sound playback modes:
- `pbtSFX_Once` - Play once and stop
- `pbtSFX_Loop` - Loop continuously until stopped

#### `StereoBalance`
Audio positioning:
- `BALANCE_CENTER` - Center positioning (default)
- `BALANCE_LEFT` - Left speaker/channel
- `BALANCE_RIGHT` - Right speaker/channel

#### `SFX_PRIORITY`
Queue priority levels (highest to lowest):
- `priIMMEDIATELY` - Critical priority
- `priDELAYED_START` - High priority with delay
- `priHIGH` - High priority
- `priABOVE_NORMAL` - Above normal priority
- `priNORMAL` - Standard priority (default)
- `priBELOW_NORMAL` - Below normal priority
- `priLOWEST` - Lowest priority

### Data Structures

#### `LoadedSFX`
Container for loaded audio data:
```cpp
struct LoadedSFX {
    WAVEFORMATEX waveFormat;    // Audio format information
    std::vector<BYTE> audioData; // Raw audio data
};
```

#### `SoundQueueItem`
Internal queue item structure containing all playback parameters and state information.

---

## Performance Considerations

### CPU Usage
- The audio thread runs continuously with a 10ms sleep interval
- WAV parsing is done once during loading, not during playback
- DirectSound provides hardware acceleration when available

### Memory Usage
- All sounds are preloaded into memory for low-latency playback
- Queue items are automatically cleaned up after playback completion
- Memory usage scales with the number and size of loaded sound files

### Latency
- Immediate playback has minimal latency (DirectSound hardware buffers)
- Queued sounds have slight additional latency due to priority processing
- Fade-in effects add 250ms to the effective start time

### Scaling Recommendations
- For large numbers of sounds, consider implementing dynamic loading/unloading
- Use cooldowns to prevent excessive simultaneous playback
- Monitor queue size in long-running applications
- Consider using lower quality audio files for less critical sounds

---

## Conclusion

The SoundManager class provides a robust, professional-grade audio system suitable for games and real-time applications. Its DirectSound foundation ensures stability and performance, while advanced features like priority queuing, fade effects, and cooldown management provide the flexibility needed for complex audio scenarios.

Key strengths include:
- **Reliability**: DirectSound-based for stable operation
- **Performance**: Low-latency playback with hardware acceleration
- **Flexibility**: Comprehensive feature set for professional audio management
- **Thread Safety**: All operations are thread-safe for multi-threaded applications
- **Ease of Use**: Simple API with sensible defaults for quick implementation

For best results, follow the initialization sequence carefully, implement proper error handling, and monitor system resources in long-running applications. The provided examples and best practices should serve as a solid foundation for implementing professional audio systems in your applications.