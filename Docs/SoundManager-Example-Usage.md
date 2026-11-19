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
20. [Sound Bank (many effects, loops, live volume)](#sound-bank-many-effects-loops-live-volume)
21. [Background music with the MediaPlayer](#background-music-with-the-mediaplayer)

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

### Threads and the sound bank
Every sound bank call (`RegisterBankSound`, `PlayBankSound`, `PlayBankLoop`, `SetBankLoopGain`, `StopBankSound`, `StopAllBankSounds`, `IsBankSoundLoaded`) takes one internal lock, so the game thread can play sounds while the options window changes the volume and the playback thread trims over-long samples. The global volume itself is an atomic. The only rule is the usual one: call `Initialize()` before registering sounds, and `CleanUp()` (which releases every bank voice) once, at shutdown. See [Sound Bank](#sound-bank-many-effects-loops-live-volume).

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

### Sound Bank Methods

#### `bool RegisterBankSound(int slot, const std::wstring& file, int voices = 1, unsigned minGapMs = 0, unsigned maxPlayMs = 0, float gain = 1.0f)`
Loads a WAV once and creates `voices` DirectSound buffers that share its sample data. Registering a slot again replaces it.
- **Parameters**: `slot` 0-4095; `voices` how many copies may overlap; `minGapMs` throttle between plays (0 = none); `maxPlayMs` cut-off for a long sample (0 = play it all); `gain` this sound's level against the others
- **Returns**: `false` (quietly) if the manager is not initialised, the file is missing, or it is not a PCM WAV
- **Thread Safety**: Thread-safe

#### `bool IsBankSoundLoaded(int slot)`
Whether `slot` holds a loaded sound. Thread-safe.

#### `void PlayBankSound(int slot, float volume = 1.0f)`
Plays on a free voice, else steals the oldest; ignored inside `minGapMs` of the previous play. Level = `volume x gain x global volume`. Thread-safe.

#### `void PlayBankLoop(int slot, float volume = 1.0f)`
Starts the sound looping unless it is already playing (cheap to call every frame). Thread-safe.

#### `void SetBankLoopGain(int slot, float volume)`
Changes the level of a sound that is playing. Thread-safe.

#### `void StopBankSound(int slot)` / `void StopAllBankSounds()`
Silences one slot / every slot. Thread-safe.

#### `void UpdateBankSounds()`
Stops voices that ran past their `maxPlayMs`. Called by the playback thread every 100 ms; calling it yourself is harmless.

#### `void SetGlobalVolume(float volume)` (updated)
Sets the global volume (0-1, clamped). It now also re-applies the level to every bank voice that is already playing, and volume is converted to DirectSound decibels (`2000 x log10(volume)`), so 0.5 is about -6 dB and 0 is silent.

#### `void SetAmbientVolume(float volume)`
Sets the **ambient** volume (0-1, clamped): an extra scale for every *looping* bank sound, i.e. anything started with `PlayBankLoop` (the background sirens, buzzes and engines). It multiplies on top of the global volume, leaves one-shot sounds (`PlayBankSound`) untouched, and, like `SetGlobalVolume`, re-applies at once to loops that are already playing. Default 1.0.

### MediaPlayer additions

#### `bool MediaPlayer::startPlaylistThread(HWND hWnd)` / `void MediaPlayer::stopPlaylistThread()`
Play the playlist on the player's own STA message-pump thread, and stop it (see [Background music with the MediaPlayer](#background-music-with-the-mediaplayer)).

#### `std::wstring MediaPlayer::currentFile()`
A thread-safe copy of the file now loaded.

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

## Sound Bank (many effects, loops, live volume)

The `SFX_ID` list is a fixed set of sounds that are queued, copying the sample into a new buffer for every play. A game with dozens of effects, looping sounds and rapid-fire weapons needs more, so the manager also has a **sound bank**: sounds are registered once at run time under an integer slot and played with no allocation.

```cpp
// Once, after soundManager.Initialize() - the slot can be any number you like (a game's own enum, say)
//                                  slot  file                        voices gapMs maxMs gain
soundManager.RegisterBankSound(0, L"./Assets/laser.wav",            4,     60,   400,  0.50f);   // 4 overlapping voices, at most ~16 shots a second
soundManager.RegisterBankSound(1, L"./Assets/engine-loop.wav",      1,      0,     0,  0.35f);   // a looping sound
soundManager.RegisterBankSound(2, L"./Assets/explosion.wav",        5,     55,  1100,  0.70f);   // cut after 1.1 s even if the sample is longer

// Whenever
soundManager.PlayBankSound(0);                  // plays on a free voice, or steals the oldest; throttled by gapMs
soundManager.PlayBankSound(2, 0.6f);            // quieter this time (volume 0..1)

// A looping sound: call every frame while it should run; it starts once and then does nothing
soundManager.PlayBankLoop(1, 0.8f);
soundManager.SetBankLoopGain(1, 0.3f + 0.7f * throttle);   // rev it up and down while it plays
soundManager.StopBankSound(1);                  // the stick was released

soundManager.StopAllBankSounds();               // leaving the level
```

| Call | What it does |
|---|---|
| `RegisterBankSound(slot, file, voices, minGapMs, maxPlayMs, gain)` | Loads the WAV. `voices` buffers share one copy of the sample. Returns `false` (quietly) if the file is missing, so an optional effect just stays silent. |
| `IsBankSoundLoaded(slot)` | Whether that slot has a sound, e.g. to fall back to speech when a voice WAV is absent. |
| `PlayBankSound(slot, volume)` | One-shot. Free voice, else the oldest is stolen. |
| `PlayBankLoop(slot, volume)` | Looping; a no-op while it is already playing. The loop is an **ambient** sound: it follows `SetAmbientVolume` as well as `SetGlobalVolume`. |
| `SetBankLoopGain(slot, volume)` | Re-levels a sound that is playing. |
| `StopBankSound(slot)` / `StopAllBankSounds()` | Silence one slot / everything. |
| `UpdateBankSounds()` | Cuts voices that ran past `maxPlayMs`. The playback thread calls it ten times a second; you never need to. |

#### `void RegisterBankSoundsAsync(std::vector<BankSoundDef> defs)` / `bool IsBankLoadingDone() const` / `void WaitForBankLoad()`
Loads a whole bank on ONE background worker thread. `BankSoundDef` is `{ slot, file, voices, minGapMs, maxPlayMs, gain }` (the same values `RegisterBankSound` takes, in the same order). The call returns at once; the worker registers each entry in turn (a missing file is skipped quietly).
- **Why**: a game bank is ~100 wavs, each decoded and duplicated into DirectSound buffers. Registered on the game thread that takes a second or more: one frame freezes and the first shots or sounds are lost.
- **While loading**: `PlayBankSound` / `PlayBankLoop` on a slot that is not in yet do nothing, so the game just starts and each effect comes alive as it finishes. `IsBankSoundLoaded(slot)` tells you about one slot; `IsBankLoadingDone()` is true when no batch is loading (also when none was ever started).
- **Order and shutdown**: a second call waits for the first batch. `CleanUp()` tells the worker to abandon the rest of its list and joins it before the DirectSound device is released; `WaitForBankLoad()` blocks until the batch is in (loading screens, tools; never in the frame loop).
- **Thread Safety**: Thread-safe. The worker only takes the bank mutex to publish each finished sound.

```cpp
std::vector<SoundSystem::SoundManager::BankSoundDef> list;
list.push_back({ kLaser,  (AssetsDir / L"firing.wav").wstring(),  6, 70, 800, 0.5f });   // slot, file, voices, minGapMs, maxPlayMs, gain
list.push_back({ kEngine, (AssetsDir / L"engine.wav").wstring(),  1,  0,   0, 0.4f });
soundManager.RegisterBankSoundsAsync(std::move(list));      // returns immediately: start the game now
// ... later, if you care: if (soundManager.IsBankLoadingDone()) { /* every effect is in */ }
```

### A complete lifecycle

```cpp
enum MySfx { kLaser = 0, kEngine = 1, kBoom = 2, kVoiceReady = 3, kSfxTotal };

void StartSound(HWND hwnd)
{
    soundManager.Initialize(hwnd);
    soundManager.StartPlaybackThread();                        // trims maxPlayMs samples; also needed by the queue
    soundManager.SetGlobalVolume(config.myConfig.dialogVolume / 64.0f);

    soundManager.RegisterBankSound(kLaser,      L"./Assets/laser.wav",   4,  60,  400, 0.50f);
    soundManager.RegisterBankSound(kEngine,     L"./Assets/engine.wav",  1,   0,    0, 0.35f);
    soundManager.RegisterBankSound(kBoom,       L"./Assets/boom.wav",    5,  55, 1100, 0.70f);
    soundManager.RegisterBankSound(kVoiceReady, L"./Assets/ready.wav",   1,   0,    0, 1.00f);   // optional: the file may not exist
}

void OnPlayerFires()  { soundManager.PlayBankSound(kLaser); }
void OnExplosion()    { soundManager.PlayBankSound(kBoom, 0.8f); }

void EveryFrame(bool thrusting)
{
    if (thrusting) soundManager.PlayBankLoop(kEngine, 0.6f);   // starts once; a no-op while it plays
    else           soundManager.StopBankSound(kEngine);
}

void StopSound()
{
    soundManager.StopAllBankSounds();
    soundManager.StopPlaybackThread();
    soundManager.CleanUp();                                    // releases every bank voice, then the DirectSound device
}
```

### Falling back when an optional file is missing

`RegisterBankSound` returns `false` for a missing or unreadable file without raising an error, so optional sounds simply stay silent. Use `IsBankSoundLoaded` to take another route:

```cpp
void SayReady()
{
    if (soundManager.IsBankSoundLoaded(kVoiceReady))
        soundManager.PlayBankSound(kVoiceReady);               // a recorded voice, if the WAV is there
    else
        ttsManager.PlayAsync(L"Get ready.");                   // otherwise the speech engine
}
```

### Live volume

`SetGlobalVolume` re-applies the new level to every bank voice, including ones that are in the middle of playing, so the options window can change the volume of a looping engine or a long explosion as the player drags the slider:

```cpp
soundManager.PlayBankLoop(kEngine, 1.0f);       // playing at 100% of the global volume
soundManager.SetGlobalVolume(0.25f);            // the engine drops to 25% immediately, not on its next start
soundManager.SetGlobalVolume(0.0f);             // silent (a true zero, not a very quiet sound)
soundManager.SetGlobalVolume(1.0f);

// The configuration window wires this up for you: config.applyLive() runs the callback in WinMain, which calls
// soundManager.SetGlobalVolume(cfg.dialogVolume / 64.0f) and soundManager.SetAmbientVolume(cfg.ambientVolume / 64.0f).
```

### Ambient volume (looping background sounds)

"Ambient" means the looping background sounds: the Stix buzz and drawing scrape, the Incoming sirens, the Chomper siren and blue-ghost warble. Anything started with `PlayBankLoop` is ambient, so it has its own level, set by the options window's **Ambient Volume** slider, on top of the Dialog Volume (global) that scales every effect:

```cpp
soundManager.PlayBankLoop(kEngine, 1.0f);       // level = 1.0 x gain x global x ambient
soundManager.SetAmbientVolume(0.5f);            // the loop drops to half at once; one-shot effects (PlayBankSound) are not affected
soundManager.SetAmbientVolume(config.myConfig.ambientVolume / 64.0f);   // what the configuration callback does
```

## Background music with the MediaPlayer

Music is not part of `SoundManager`; the engine's `MediaPlayer` (`WinMediaPlayer.h`) plays mp3 / m4a files through Media Foundation. It follows the Music Volume and Play Music settings, and keeps a playlist.

**Use `startPlaylistThread` for background music.** Media Foundation's MFPlay processes `Play()` and delivers its events through the message queue of the thread that *created* the player, so that thread must be a single-threaded apartment and keep pumping messages while the music plays. A player created on a game / render thread that does neither loads fine and logs "Playback started", but is silent. `startPlaylistThread` creates the player on its own STA thread and pumps messages for you; when a track ends the thread starts the next one (outside the player's callback), skipping any file that fails to load.

```cpp
static MediaPlayer music;                                      // the vector demo owns one instance

void StartMusic(HWND hwnd, const std::vector<std::wstring>& files)   // files: absolute paths, already shuffled if you like
{
    music.ClearPlaylist();
    for (const std::wstring& f : files) music.AddToPlaylist(f);
    music.startPlaylistThread(hwnd);                           // starts files[0] at the Music Volume setting (2 s fade-in), then moves on by itself
}

void OnAudioSettingsChanged()                                  // call from the Configuration apply callback
{
    music.setVolume(MediaPlayer::ConfigMusicVolume());         // 0..1 from the Music Volume slider (the master volume is the Windows output device's)
    music.applyPlayMusic(config.myConfig.playMusic);           // off: pause; on: resume (or start)
}

void EverySecond()
{
    // The player changes track on its own thread, so read the name through the thread-safe getter, not the public filePath member
    const std::wstring nowPlaying = std::filesystem::path(music.currentFile()).filename().wstring();
}

void StopMusic() { music.stopPlaylistThread(); }               // stops the music and joins the thread (the destructor does this too)
```

| Call | What it does |
|---|---|
| `startPlaylistThread(hwnd)` | Plays the playlist on the player's own STA message-pump thread. Returns `false` if the playlist is empty or a thread is already running. |
| `stopPlaylistThread()` | Stops the music and joins the thread. Safe to call when nothing is running. |
| `currentFile()` | Thread-safe copy of the file now loaded. |
| `PlayNext()` | Moves to the next playlist entry, now skipping files that fail to load (it used to stop the music for good on a bad file). |

Fixed in the same change: `loadFile` now always releases the previous track's player (`cleanup()` only worked once, so the second and later tracks were created on top of a player that was never released), and the end-of-track event no longer reloads the player from inside that player's own callback.

## Conclusion

The SoundManager class provides a robust, professional-grade audio system suitable for games and real-time applications. Its DirectSound foundation ensures stability and performance, while advanced features like priority queuing, fade effects, and cooldown management provide the flexibility needed for complex audio scenarios.

Key strengths include:
- **Reliability**: DirectSound-based for stable operation
- **Performance**: Low-latency playback with hardware acceleration
- **Flexibility**: Comprehensive feature set for professional audio management
- **Thread Safety**: All operations are thread-safe for multi-threaded applications
- **Ease of Use**: Simple API with sensible defaults for quick implementation

For best results, follow the initialization sequence carefully, implement proper error handling, and monitor system resources in long-running applications. The provided examples and best practices should serve as a solid foundation for implementing professional audio systems in your applications.
## Bank update cost (2026-11-13)

`BankVoice::appliedGain` caches only successful effective-volume writes, including
global and ambient scaling. An unchanged gain skips both the logarithm and the
DirectSound `SetVolume` call; changing either live volume control still updates
the voice. Failed writes are retried. `StopBankSound` / `StopAllBankSounds` skip
idle voices, while retaining the existing mutex and playback bookkeeping.

The vector bank contains 204 definitions, with a compile-time count check and a
separate Soldier On / arcade boundary check. Run
`python Scripts/validate-vector-audio.py` without compiling to check required
files and new library conversions. See
[the audio/performance audit](Vector-Audio-Performance-Audit.md). Playback remains
unverified until a manual engine run.
